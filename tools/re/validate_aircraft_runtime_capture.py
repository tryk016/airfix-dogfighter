#!/usr/bin/env python3
"""Validate a bounded, path-free AirCraft/x87 runtime capture.

The input is private JSONL produced from a controlled working-copy run.  This
validator knows only the reference-build module identities and the exact
consumer/scheduler sites documented by the public reconstruction reports.
It never emits captured payloads, state words, hashes, or input paths.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from fractions import Fraction
import json
from pathlib import Path
import stat
import sys
from typing import Any, NoReturn


SCHEMA = "airfix-aircraft-runtime-capture/v1"
MAX_CAPTURE_BYTES = 4 * 1024 * 1024
MAX_RECORDS = 10_000
MAX_LINE_BYTES = 64 * 1024

REFERENCE_MODULES = {
    "Dogfighter.exe": "F48CD7D16E8D993B262F4E35731ABFB1D95288E0C688506065F168E1B4090E89",
    "AfEngine.dll": "A4CCC7E59E4119488322E80077803A5BB9922731D84CE1A420FD27516D45711E",
    "Cc.dll": "18002A3AF1405D932579C6D6888256CE26B8A479735093D33441F10FC27AA8AF",
}


@dataclass(frozen=True)
class Site:
    module: str
    rva: int
    category: str


SITES = {
    "startup.before_controlfp": Site("Dogfighter.exe", 0x00035C3D, "startup"),
    "startup.after_controlfp": Site("Dogfighter.exe", 0x00035C42, "startup"),
    "loop.input_drain_begin": Site("Dogfighter.exe", 0x00010FA7, "ordering"),
    "clock.poll_remote": Site("AfEngine.dll", 0x000485AC, "ordering"),
    "clock.refresh_dependants": Site("AfEngine.dll", 0x000485E0, "ordering"),
    "scheduler.aircraft_refresh": Site("AfEngine.dll", 0x00040392, "ordering"),
    "loop.before_render": Site("Dogfighter.exe", 0x00010FDC, "ordering"),
    "event.bank.fild": Site("AfEngine.dll", 0x0001E4E4, "event"),
    "event.pitch.fild": Site("AfEngine.dll", 0x0001E505, "event"),
    "event.shared.compare": Site("AfEngine.dll", 0x0001E514, "event"),
    "rigid.euler.entry": Site("Cc.dll", 0x0002A420, "rigid"),
    "rigid.derive.entry": Site("Cc.dll", 0x0002A8A0, "rigid"),
    "rigid.normalize.entry": Site("Cc.dll", 0x0002C2C0, "rigid"),
    "rigid.postode.entry": Site("Cc.dll", 0x0002A890, "rigid"),
}

REQUIRED_ACCEPTANCE_SITES = frozenset(SITES)
EVENT_ACCEPTANCE_PAYLOADS = frozenset(
    {0, 1, -1, 32, -32, 3, -3, 16_777_217, -16_777_217,
     1_555_145_203, -1_555_145_203}
)

SAMPLE_REQUIRED_KEYS = frozenset(
    {
        "record",
        "sequence",
        "site_id",
        "module",
        "rva",
        "thread_id",
        "x87_control_word",
        "x87_status_word",
        "mxcsr",
    }
)
SAMPLE_OPTIONAL_KEYS = frozenset(
    {
        "iteration",
        "scheduled_time_ms",
        "axis",
        "payload_s32",
        "observed_store_bits",
        "state_words",
        "vector_id",
        "observed",
    }
)

CATEGORY_OPTIONAL_KEYS = {
    "startup": frozenset(),
    "ordering": frozenset({"iteration", "scheduled_time_ms"}),
    "event": frozenset({"iteration", "axis", "payload_s32", "observed_store_bits"}),
    "rigid": frozenset({"iteration", "state_words", "vector_id", "observed"}),
}


class CaptureValidationError(ValueError):
    def __init__(self, code: str, line: int) -> None:
        super().__init__(code)
        self.code = code
        self.line = line


@dataclass(frozen=True)
class CaptureReport:
    profile: str
    decision: str
    sample_count: int
    precision: str
    rounding: str
    missing_sites: tuple[str, ...]
    missing_vectors: tuple[str, ...]


def _fail(code: str, line: int) -> NoReturn:
    raise CaptureValidationError(code, line)


def _object_without_duplicates(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate-key")
        result[key] = value
    return result


def _parse_hex(value: Any, digits: int, code: str, line: int) -> int:
    if not isinstance(value, str) or len(value) != digits + 2 or not value.startswith("0x"):
        _fail(code, line)
    try:
        parsed = int(value[2:], 16)
    except ValueError:
        _fail(code, line)
    if parsed < 0 or parsed >= 1 << (digits * 4):
        _fail(code, line)
    return parsed


def _parse_dwords(value: Any, count: int, code: str, line: int) -> tuple[int, ...]:
    if not isinstance(value, list) or len(value) != count:
        _fail(code, line)
    return tuple(_parse_hex(word, 8, code, line) for word in value)


def _load_records(path: Path) -> list[dict[str, Any]]:
    try:
        metadata = path.lstat()
    except OSError:
        _fail("input-io", 0)
    if stat.S_ISLNK(metadata.st_mode) or not stat.S_ISREG(metadata.st_mode):
        _fail("input-not-regular", 0)
    if metadata.st_size == 0 or metadata.st_size > MAX_CAPTURE_BYTES:
        _fail("input-size", 0)
    try:
        raw = path.read_bytes()
        text = raw.decode("utf-8", errors="strict")
    except (OSError, UnicodeDecodeError):
        _fail("input-encoding", 0)
    if "\x00" in text:
        _fail("input-nul", 0)

    lines = text.splitlines()
    if not lines or len(lines) > MAX_RECORDS + 1:
        _fail("record-count", 0)
    records: list[dict[str, Any]] = []
    for line_number, raw_line in enumerate(lines, 1):
        if not raw_line or len(raw_line.encode("utf-8")) > MAX_LINE_BYTES:
            _fail("line-size", line_number)
        try:
            value = json.loads(raw_line, object_pairs_hook=_object_without_duplicates)
        except (json.JSONDecodeError, ValueError):
            _fail("invalid-json", line_number)
        if not isinstance(value, dict):
            _fail("record-not-object", line_number)
        records.append(value)
    return records


def _validate_header(header: dict[str, Any]) -> str:
    if set(header) != {"record", "schema", "profile", "modules"}:
        _fail("header-fields", 1)
    if header["record"] != "header" or header["schema"] != SCHEMA:
        _fail("header-schema", 1)
    profile = header["profile"]
    if profile not in {"observation", "acceptance"}:
        _fail("header-profile", 1)
    modules = header["modules"]
    if not isinstance(modules, list) or len(modules) != len(REFERENCE_MODULES):
        _fail("header-modules", 1)
    observed: dict[str, str] = {}
    for module in modules:
        if not isinstance(module, dict) or set(module) != {"name", "sha256"}:
            _fail("header-module-fields", 1)
        name = module["name"]
        digest = module["sha256"]
        if not isinstance(name, str) or not isinstance(digest, str):
            _fail("header-module-fields", 1)
        if name in observed:
            _fail("header-module-duplicate", 1)
        observed[name] = digest.upper()
    if observed != REFERENCE_MODULES:
        _fail("header-module-identity", 1)
    return profile


def _validate_category_fields(source: dict[str, Any], site: Site, line: int) -> None:
    if set(source) != SAMPLE_REQUIRED_KEYS | (
        set(source) & CATEGORY_OPTIONAL_KEYS[site.category]
    ):
        _fail("sample-category-fields", line)
    if site.category == "ordering" and "iteration" not in source:
        _fail("ordering-iteration", line)
    if site.category == "event" and not {
        "axis", "payload_s32", "observed_store_bits"
    } <= set(source):
        _fail("event-fields", line)
    if site.category == "rigid" and "state_words" not in source:
        _fail("rigid-state", line)


def decode_precision(control_word: int) -> str:
    return {0: "pc24", 1: "reserved", 2: "pc53", 3: "pc64"}[
        (control_word >> 8) & 0x3
    ]


def decode_rounding(control_word: int) -> str:
    return {0: "nearest", 1: "down", 2: "up", 3: "zero"}[
        (control_word >> 10) & 0x3
    ]


def _binary32_fraction(bits: int) -> Fraction:
    sign = -1 if bits >> 31 else 1
    exponent = (bits >> 23) & 0xFF
    fraction = bits & 0x7FFFFF
    if exponent == 0 or exponent == 0xFF:
        raise ValueError("only finite normal binary32 values are supported")
    significand = (1 << 23) | fraction
    shift = exponent - 127 - 23
    value = Fraction(sign * significand, 1)
    return value * (1 << shift) if shift >= 0 else value / (1 << -shift)


def _floor_log2(numerator: int, denominator: int) -> int:
    exponent = numerator.bit_length() - denominator.bit_length()
    if exponent >= 0:
        if numerator < denominator << exponent:
            exponent -= 1
    elif numerator << -exponent < denominator:
        exponent -= 1
    return exponent


def _round_components(value: Fraction, precision: int, rounding: str) -> tuple[int, int, int]:
    if value == 0:
        return 0, 0, 0
    sign = -1 if value < 0 else 1
    magnitude = abs(value)
    exponent = _floor_log2(magnitude.numerator, magnitude.denominator)
    shift = exponent - (precision - 1)
    if shift >= 0:
        numerator = magnitude.numerator
        denominator = magnitude.denominator << shift
    else:
        numerator = magnitude.numerator << -shift
        denominator = magnitude.denominator
    quotient, remainder = divmod(numerator, denominator)
    increment = False
    if remainder:
        if rounding == "nearest":
            comparison = remainder * 2 - denominator
            increment = comparison > 0 or (comparison == 0 and quotient & 1 == 1)
        elif rounding == "down":
            increment = sign < 0
        elif rounding == "up":
            increment = sign > 0
        elif rounding != "zero":
            raise ValueError("unsupported rounding")
    if increment:
        quotient += 1
    if quotient == 1 << precision:
        quotient >>= 1
        exponent += 1
    return sign, exponent, quotient


def _components_fraction(sign: int, exponent: int, significand: int, precision: int) -> Fraction:
    if sign == 0:
        return Fraction(0)
    shift = exponent - (precision - 1)
    result = Fraction(sign * significand, 1)
    return result * (1 << shift) if shift >= 0 else result / (1 << -shift)


def event_store_bits(payload: int, precision: str, rounding: str) -> int:
    if isinstance(payload, bool) or not -(1 << 31) <= payload < 1 << 31:
        raise ValueError("payload outside signed int32")
    if payload == 0:
        return 0
    precision_bits = {"pc24": 24, "pc53": 53, "pc64": 64}.get(precision)
    if precision_bits is None:
        raise ValueError("unsupported x87 precision")
    exact = Fraction(payload, 1) * _binary32_fraction(0x3D3020C5)
    sign, exponent, significand = _round_components(exact, precision_bits, rounding)
    x87_value = _components_fraction(sign, exponent, significand, precision_bits)
    sign, exponent, significand = _round_components(x87_value, 24, rounding)
    if exponent < -126 or exponent > 127:
        raise ValueError("event result outside supported normal binary32 range")
    return (
        (0x80000000 if sign < 0 else 0)
        | ((exponent + 127) << 23)
        | (significand - (1 << 23))
    )


IDENTITY_MATRIX = (
    0x3F800000, 0, 0,
    0, 0x3F800000, 0,
    0, 0, 0x3F800000,
)
ZERO_STATE = (0,) * 13
VECTOR_SITES = {
    "V1": "rigid.euler.entry",
    "V2": "rigid.euler.entry",
    "V3": "rigid.derive.entry",
    "V4": "rigid.normalize.entry",
    "V5": "rigid.derive.entry",
    "V6": "rigid.euler.entry",
}
VECTOR_STATES = {
    "V1": (0, 0, 0, 0x3F800000, 0, 0, 0, 0, 0, 0, 0, 0, 0),
    "V2": (0, 0, 0, 0x3F800000, 0, 0, 0,
           0x40000000, 0xC0800000, 0x41000000, 0, 0, 0),
    "V3": (0, 0, 0, 0x3F000000, 0x3F000000, 0x3F000000, 0x3F000000,
           0, 0, 0, 0x3F800000, 0x40000000, 0x40400000),
    "V4": (0, 0, 0, 0x3F800000, 0, 0, 0, 0, 0, 0, 0x40000000, 0, 0),
    "V5": (0, 0, 0, 0x3F800000, 0, 0, 0, 0, 0, 0,
           0x3F000011, 0x3F000011, 0x3F000011),
    "V6": (0x3F800000,) + (0,) * 12,
}


def _vector_expected(vector_id: str, precision: str, rounding: str) -> dict[str, Any]:
    if vector_id in {"V1", "V2", "V3", "V4", "V5"} and rounding != "nearest":
        raise ValueError("directed-rounding oracle is not established for this vector")
    if vector_id == "V1":
        derivative = list(ZERO_STATE)
        derivative[3] = 0x80000000
        return {
            "derivative_words": tuple(derivative),
            "post_state_words": VECTOR_STATES["V1"],
            "accumulator_words": (0,) * 6,
        }
    if vector_id == "V2":
        return {
            "post_state_words": (
                0x3F000000, 0xBF800000, 0x40000000,
                0x3F800000, 0, 0, 0,
                0x40200000, 0xC0A00000, 0x41200000,
                0, 0, 0,
            ),
            "velocity_words": (0x3FA00000, 0xC0200000, 0x40A00000),
            "rotation_words": IDENTITY_MATRIX,
            "world_inverse_inertia_words": IDENTITY_MATRIX,
            "accumulator_words": (0,) * 6,
        }
    if vector_id == "V3":
        return {
            "rotation_words": (
                0, 0x3F800000, 0,
                0, 0, 0x3F800000,
                0x3F800000, 0, 0,
            ),
            "world_inverse_inertia_words": (
                0x41000000, 0, 0,
                0, 0x40000000, 0,
                0, 0, 0x40800000,
            ),
            "angular_velocity_words": (0x41000000, 0x40800000, 0x41400000),
        }
    if vector_id == "V4":
        return {
            "derivative_quaternion_words": (0x80000000, 0x3F800000, 0, 0),
            "post_quaternion_words": (0x3F3504F3, 0x3F3504F3, 0, 0),
        }
    if vector_id == "V5":
        derivatives = (
            (0x3F733332, 0x3F733332, 0x3F733332)
            if precision == "pc24"
            else (0x3F733332, 0x3F733332, 0x3F733331)
        )
        return {
            "stored_damping_xy_bits": 0x3D4CCCE8,
            "angular_momentum_derivative_words": derivatives,
        }
    if vector_id == "V6":
        upper = rounding == "up" or (rounding == "nearest" and precision == "pc64")
        return {
            "dt_bits": 0x39803009,
            "derivative_bits": 0x397FA012,
            "stored_state_bits": 0x3F800001 if upper else 0x3F800000,
        }
    raise ValueError("unknown vector")


def _validate_observed(observed: Any, expected: dict[str, Any], line: int) -> None:
    if not isinstance(observed, dict) or set(observed) != set(expected):
        _fail("vector-observed-fields", line)
    for key, expected_value in expected.items():
        if isinstance(expected_value, tuple):
            actual = _parse_dwords(
                observed[key], len(expected_value), "vector-observed-value", line
            )
        else:
            actual = _parse_hex(observed[key], 8, "vector-observed-value", line)
        if actual != expected_value:
            _fail("vector-oracle-mismatch", line)


def _validate_ordering(samples: list[dict[str, Any]], profile: str) -> None:
    ordering = [sample for sample in samples if SITES[sample["site_id"]].category == "ordering"]
    if not ordering:
        if profile == "acceptance":
            _fail("ordering-missing", 0)
        return
    groups: dict[int, list[dict[str, Any]]] = {}
    iteration_sequence: list[int] = []
    for sample in ordering:
        iteration = sample.get("iteration")
        if isinstance(iteration, bool) or not isinstance(iteration, int) or iteration < 0:
            _fail("ordering-iteration", sample["_line"])
        iteration_sequence.append(iteration)
        groups.setdefault(iteration, []).append(sample)
    if iteration_sequence != sorted(iteration_sequence):
        _fail("ordering-iteration-order", 0)
    if sorted(groups) != list(range(len(groups))):
        _fail("ordering-iteration-sequence", 0)

    refresh_counts: set[int] = set()
    for records in groups.values():
        names = [record["site_id"] for record in records]
        for required in (
            "loop.input_drain_begin",
            "clock.poll_remote",
            "clock.refresh_dependants",
            "loop.before_render",
        ):
            if names.count(required) != 1:
                _fail("ordering-marker-count", records[0]["_line"])
        begin = names.index("loop.input_drain_begin")
        poll = names.index("clock.poll_remote")
        dispatch = names.index("clock.refresh_dependants")
        render = names.index("loop.before_render")
        refresh_positions = [
            index for index, name in enumerate(names)
            if name == "scheduler.aircraft_refresh"
        ]
        if not (begin < poll < dispatch < render) or any(
            not dispatch < position < render for position in refresh_positions
        ):
            _fail("ordering-site-sequence", records[0]["_line"])
        refreshes = [records[index] for index in refresh_positions]
        refresh_counts.add(len(refreshes))
        times: list[int] = []
        for refresh in refreshes:
            scheduled = refresh.get("scheduled_time_ms")
            if (
                isinstance(scheduled, bool)
                or not isinstance(scheduled, int)
                or not 0 <= scheduled < 1 << 63
            ):
                _fail("ordering-scheduled-time", refresh["_line"])
            times.append(scheduled)
        if any(right - left != 12 for left, right in zip(times, times[1:])):
            _fail("ordering-refresh-interval", records[0]["_line"])
    if profile == "acceptance" and not (
        0 in refresh_counts and 1 in refresh_counts and any(count >= 2 for count in refresh_counts)
    ):
        _fail("ordering-coverage", 0)


def validate_capture(path: Path) -> CaptureReport:
    records = _load_records(path)
    profile = _validate_header(records[0])
    samples = records[1:]
    if not samples or len(samples) > MAX_RECORDS:
        _fail("sample-count", 0)

    seen_sites: set[str] = set()
    seen_vectors: set[str] = set()
    shared_payloads: set[int] = set()
    decoded_samples: list[dict[str, Any]] = []
    thread_id: int | None = None
    stable_control: int | None = None
    stable_mxcsr: int | None = None

    for index, source in enumerate(samples):
        line = index + 2
        if not SAMPLE_REQUIRED_KEYS <= set(source) or not set(source) <= (
            SAMPLE_REQUIRED_KEYS | SAMPLE_OPTIONAL_KEYS
        ):
            _fail("sample-fields", line)
        sequence = source["sequence"]
        if (
            source["record"] != "sample"
            or isinstance(sequence, bool)
            or not isinstance(sequence, int)
            or sequence != index
        ):
            _fail("sample-sequence", line)
        site_id = source["site_id"]
        if not isinstance(site_id, str) or site_id not in SITES:
            _fail("sample-site", line)
        site = SITES[site_id]
        _validate_category_fields(source, site, line)
        rva = _parse_hex(source["rva"], 8, "sample-rva", line)
        if source["module"] != site.module or rva != site.rva:
            _fail("sample-site-address", line)
        current_thread = source["thread_id"]
        if (
            isinstance(current_thread, bool)
            or not isinstance(current_thread, int)
            or not 0 < current_thread < 1 << 32
        ):
            _fail("sample-thread", line)
        if thread_id is None:
            thread_id = current_thread
        elif current_thread != thread_id:
            _fail("sample-thread-change", line)

        control = _parse_hex(source["x87_control_word"], 4, "sample-control-word", line)
        status_word = _parse_hex(source["x87_status_word"], 4, "sample-status-word", line)
        mxcsr = _parse_hex(source["mxcsr"], 8, "sample-mxcsr", line)
        precision = decode_precision(control)
        rounding = decode_rounding(control)
        if precision == "reserved":
            _fail("sample-reserved-precision", line)
        mxcsr_policy = mxcsr & ~0x3F
        if site_id != "startup.before_controlfp":
            if stable_control is None:
                stable_control = control
                stable_mxcsr = mxcsr_policy
            elif control != stable_control or mxcsr_policy != stable_mxcsr:
                _fail("sample-policy-change", line)
        if profile == "acceptance":
            if control & 0x3F != 0x3F:
                _fail("sample-x87-exception-masks", line)
            if mxcsr & 0x1F80 != 0x1F80:
                _fail("sample-mxcsr-exception-masks", line)

        sample = dict(source)
        sample.update(
            {
                "_line": line,
                "_control": control,
                "_status": status_word,
                "_mxcsr": mxcsr,
                "_precision": precision,
                "_rounding": rounding,
            }
        )
        seen_sites.add(site_id)

        if "iteration" in source:
            iteration = source["iteration"]
            if isinstance(iteration, bool) or not isinstance(iteration, int) or iteration < 0:
                _fail("sample-iteration", line)

        if site.category == "ordering":
            has_scheduled_time = "scheduled_time_ms" in source
            if (site_id == "scheduler.aircraft_refresh") != has_scheduled_time:
                _fail("ordering-scheduled-time-fields", line)

        if site.category == "event":
            axis = source.get("axis")
            if axis not in {"pitch", "bank"}:
                _fail("event-axis", line)
            if site_id == "event.bank.fild" and axis != "bank":
                _fail("event-axis-site", line)
            if site_id == "event.pitch.fild" and axis != "pitch":
                _fail("event-axis-site", line)
            payload = source.get("payload_s32")
            if (
                isinstance(payload, bool)
                or not isinstance(payload, int)
                or not -(1 << 31) <= payload < 1 << 31
            ):
                _fail("event-payload", line)
            observed_bits = _parse_hex(
                source.get("observed_store_bits"), 8, "event-store-bits", line
            )
            if observed_bits != event_store_bits(payload, precision, rounding):
                _fail("event-oracle-mismatch", line)
            if site_id == "event.shared.compare":
                shared_payloads.add(payload)

        if site.category == "rigid":
            state_words = _parse_dwords(source.get("state_words"), 13, "rigid-state", line)
            vector_id = source.get("vector_id")
            if vector_id is not None:
                if vector_id not in VECTOR_SITES or VECTOR_SITES[vector_id] != site_id:
                    _fail("vector-site", line)
                if state_words != VECTOR_STATES[vector_id]:
                    _fail("vector-state", line)
                try:
                    expected = _vector_expected(vector_id, precision, rounding)
                except ValueError:
                    _fail("vector-policy-unsupported", line)
                _validate_observed(source.get("observed"), expected, line)
                seen_vectors.add(vector_id)
            elif "observed" in source:
                _fail("vector-fields", line)

        decoded_samples.append(sample)

    if stable_control is None or stable_mxcsr is None:
        _fail("policy-missing", 0)
    precision = decode_precision(stable_control)
    rounding = decode_rounding(stable_control)
    missing_sites = tuple(sorted(REQUIRED_ACCEPTANCE_SITES - seen_sites))
    missing_vectors = tuple(sorted(set(VECTOR_SITES) - seen_vectors))
    _validate_ordering(decoded_samples, profile)

    startup_before = [
        sample for sample in decoded_samples
        if sample["site_id"] == "startup.before_controlfp"
    ]
    startup_after = [
        sample for sample in decoded_samples
        if sample["site_id"] == "startup.after_controlfp"
    ]
    if profile == "acceptance" and (
        len(startup_before) != 1
        or len(startup_after) != 1
        or startup_before[0]["sequence"] != 0
        or startup_after[0]["sequence"] != 1
    ):
        _fail("startup-sequence", 0)

    if profile == "acceptance":
        if missing_sites:
            _fail("acceptance-site-coverage", 0)
        if missing_vectors:
            _fail("acceptance-vector-coverage", 0)
        if shared_payloads != EVENT_ACCEPTANCE_PAYLOADS:
            _fail("acceptance-event-coverage", 0)
        if precision != "pc53" or rounding != "nearest":
            _fail("acceptance-policy", 0)
        decision = "go"
    else:
        decision = "no-go"

    return CaptureReport(
        profile=profile,
        decision=decision,
        sample_count=len(samples),
        precision=precision,
        rounding=rounding,
        missing_sites=missing_sites,
        missing_vectors=missing_vectors,
    )


def _safe_summary(report: CaptureReport) -> dict[str, Any]:
    return {
        "schema": SCHEMA,
        "profile": report.profile,
        "decision": report.decision,
        "sample_count": report.sample_count,
        "precision": report.precision,
        "rounding": report.rounding,
        "missing_site_count": len(report.missing_sites),
        "missing_vector_count": len(report.missing_vectors),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Validate a private AirCraft/x87 capture without echoing it."
    )
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--json-summary", action="store_true")
    parser.add_argument("--require-go", action="store_true")
    arguments = parser.parse_args(argv)
    try:
        report = validate_capture(arguments.input)
    except CaptureValidationError as error:
        print(
            f"aircraft-capture: rejected code={error.code} line={error.line}",
            file=sys.stderr,
        )
        return 1
    if arguments.json_summary:
        print(json.dumps(_safe_summary(report), sort_keys=True, separators=(",", ":")))
    else:
        print(
            "aircraft-capture: valid "
            f"profile={report.profile} decision={report.decision} "
            f"samples={report.sample_count} "
            f"policy={report.precision}/{report.rounding}"
        )
    if arguments.require_go and report.decision != "go":
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
