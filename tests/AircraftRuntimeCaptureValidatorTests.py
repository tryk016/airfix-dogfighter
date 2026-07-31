#!/usr/bin/env python3

from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = REPOSITORY_ROOT / "tools" / "re" / "validate_aircraft_runtime_capture.py"
SPEC = importlib.util.spec_from_file_location("airfix_aircraft_capture", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("unable to load the aircraft capture validator")
VALIDATOR = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = VALIDATOR
SPEC.loader.exec_module(VALIDATOR)


def word(value: int) -> str:
    return f"0x{value:08X}"


def header(profile: str = "observation") -> dict[str, object]:
    return {
        "record": "header",
        "schema": VALIDATOR.SCHEMA,
        "profile": profile,
        "modules": [
            {"name": name, "sha256": digest}
            for name, digest in VALIDATOR.REFERENCE_MODULES.items()
        ],
    }


def sample(
    site_id: str,
    *,
    control_word: int = 0x027F,
    status_word: int = 0,
    mxcsr: int = 0x00001F80,
    thread_id: int = 77,
    **extra: object,
) -> dict[str, object]:
    site = VALIDATOR.SITES[site_id]
    return {
        "record": "sample",
        "sequence": -1,
        "site_id": site_id,
        "module": site.module,
        "rva": word(site.rva),
        "thread_id": thread_id,
        "x87_control_word": f"0x{control_word:04X}",
        "x87_status_word": f"0x{status_word:04X}",
        "mxcsr": word(mxcsr),
        **extra,
    }


def encode_observed(expected: dict[str, object]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in expected.items():
        if isinstance(value, tuple):
            result[key] = [word(item) for item in value]
        else:
            result[key] = word(value)
    return result


def event_sample(site_id: str, axis: str, payload: int) -> dict[str, object]:
    return sample(
        site_id,
        axis=axis,
        payload_s32=payload,
        observed_store_bits=word(
            VALIDATOR.event_store_bits(payload, "pc53", "nearest")
        ),
    )


def vector_sample(vector_id: str) -> dict[str, object]:
    return sample(
        VALIDATOR.VECTOR_SITES[vector_id],
        state_words=[word(item) for item in VALIDATOR.VECTOR_STATES[vector_id]],
        vector_id=vector_id,
        observed=encode_observed(
            VALIDATOR._vector_expected(vector_id, "pc53", "nearest")
        ),
    )


def ordering_iteration(iteration: int, refresh_times: tuple[int, ...]) -> list[dict[str, object]]:
    records = [
        sample("loop.input_drain_begin", iteration=iteration),
        sample("clock.poll_remote", iteration=iteration),
        sample("clock.refresh_dependants", iteration=iteration),
    ]
    records.extend(
        sample(
            "scheduler.aircraft_refresh",
            iteration=iteration,
            scheduled_time_ms=scheduled,
        )
        for scheduled in refresh_times
    )
    records.append(sample("loop.before_render", iteration=iteration))
    return records


def acceptance_records() -> list[dict[str, object]]:
    records: list[dict[str, object]] = [
        header("acceptance"),
        sample("startup.before_controlfp", control_word=0x037F),
        sample("startup.after_controlfp"),
        event_sample("event.bank.fild", "bank", 32),
        event_sample("event.pitch.fild", "pitch", -32),
    ]
    records.extend(
        event_sample("event.shared.compare", "pitch" if payload >= 0 else "bank", payload)
        for payload in sorted(VALIDATOR.EVENT_ACCEPTANCE_PAYLOADS)
    )
    records.extend(vector_sample(vector_id) for vector_id in VALIDATOR.VECTOR_SITES)
    records.append(
        sample(
            "rigid.postode.entry",
            state_words=[word(item) for item in VALIDATOR.VECTOR_STATES["V1"]],
        )
    )
    records.extend(ordering_iteration(0, ()))
    records.extend(ordering_iteration(1, (120,)))
    records.extend(ordering_iteration(2, (240, 252, 264)))
    for sequence, record in enumerate(records[1:]):
        record["sequence"] = sequence
    return records


class CaptureFile:
    def __init__(self, records: list[dict[str, object]]) -> None:
        self.directory = tempfile.TemporaryDirectory(prefix="airfix-capture-test-")
        self.path = Path(self.directory.name) / "private.aircraft-capture.jsonl"
        self.path.write_text(
            "".join(json.dumps(record, separators=(",", ":")) + "\n" for record in records),
            encoding="utf-8",
        )

    def close(self) -> None:
        self.directory.cleanup()

    def __enter__(self) -> Path:
        return self.path

    def __exit__(self, *unused: object) -> None:
        self.close()


class AircraftRuntimeCaptureValidatorTests(unittest.TestCase):
    def assert_rejected(self, records: list[dict[str, object]], code: str) -> None:
        with CaptureFile(records) as path:
            with self.assertRaises(VALIDATOR.CaptureValidationError) as caught:
                VALIDATOR.validate_capture(path)
        self.assertEqual(caught.exception.code, code)

    def test_pc53_nearest_event_vectors_match_exact_public_table(self) -> None:
        expected = {
            0: 0x00000000,
            1: 0x3D3020C5,
            -1: 0xBD3020C5,
            32: 0x3FB020C5,
            -32: 0xBFB020C5,
            3: 0x3E041894,
            -3: 0xBE041894,
            16_777_217: 0x493020C6,
            -16_777_217: 0xC93020C6,
            1_555_145_203: 0x4C7F17F4,
            -1_555_145_203: 0xCC7F17F4,
        }
        self.assertEqual(
            {payload: VALIDATOR.event_store_bits(payload, "pc53", "nearest") for payload in expected},
            expected,
        )
        self.assertEqual(
            VALIDATOR.event_store_bits(1_555_145_203, "pc64", "nearest"),
            0x4C7F17F3,
        )

    def test_v5_and_v6_policy_discriminators(self) -> None:
        self.assertEqual(
            VALIDATOR._vector_expected("V5", "pc24", "nearest")[
                "angular_momentum_derivative_words"
            ],
            (0x3F733332, 0x3F733332, 0x3F733332),
        )
        self.assertEqual(
            VALIDATOR._vector_expected("V5", "pc53", "nearest")[
                "angular_momentum_derivative_words"
            ],
            (0x3F733332, 0x3F733332, 0x3F733331),
        )
        for precision in ("pc24", "pc53", "pc64"):
            for rounding in ("nearest", "down", "up", "zero"):
                expected = 0x3F800001 if (
                    rounding == "up" or (rounding == "nearest" and precision == "pc64")
                ) else 0x3F800000
                self.assertEqual(
                    VALIDATOR._vector_expected("V6", precision, rounding)[
                        "stored_state_bits"
                    ],
                    expected,
                )

    def test_complete_acceptance_capture_is_go(self) -> None:
        records = acceptance_records()
        records[3]["x87_status_word"] = "0x0020"
        records[3]["mxcsr"] = "0x00001FA0"
        with CaptureFile(records) as path:
            report = VALIDATOR.validate_capture(path)
        self.assertEqual(report.profile, "acceptance")
        self.assertEqual(report.decision, "go")
        self.assertEqual(report.precision, "pc53")
        self.assertEqual(report.rounding, "nearest")
        self.assertFalse(report.missing_sites)
        self.assertFalse(report.missing_vectors)

    def test_partial_observation_is_valid_but_no_go(self) -> None:
        records = [header(), sample("startup.after_controlfp")]
        records[1]["sequence"] = 0
        with CaptureFile(records) as path:
            report = VALIDATOR.validate_capture(path)
        self.assertEqual(report.decision, "no-go")
        self.assertTrue(report.missing_sites)
        self.assertTrue(report.missing_vectors)

    def test_address_thread_and_policy_changes_fail_closed(self) -> None:
        wrong_address = acceptance_records()
        wrong_address[2]["rva"] = "0x00000000"
        self.assert_rejected(wrong_address, "sample-site-address")

        changed_thread = acceptance_records()
        changed_thread[3]["thread_id"] = 78
        self.assert_rejected(changed_thread, "sample-thread-change")

        changed_policy = acceptance_records()
        changed_policy[3]["x87_control_word"] = "0x037F"
        self.assert_rejected(changed_policy, "sample-policy-change")

    def test_event_state_and_vector_mismatches_fail_closed(self) -> None:
        bad_event = acceptance_records()
        bad_event[3]["observed_store_bits"] = "0x00000000"
        self.assert_rejected(bad_event, "event-oracle-mismatch")

        bad_state = acceptance_records()
        vector = next(record for record in bad_state if record.get("vector_id") == "V1")
        vector["state_words"] = vector["state_words"][:-1]
        self.assert_rejected(bad_state, "rigid-state")

        bad_vector = acceptance_records()
        vector = next(record for record in bad_vector if record.get("vector_id") == "V6")
        vector["observed"]["stored_state_bits"] = "0x3F800001"
        self.assert_rejected(bad_vector, "vector-oracle-mismatch")

    def test_ordering_and_acceptance_coverage_fail_closed(self) -> None:
        wrong_order = acceptance_records()
        poll = next(i for i, record in enumerate(wrong_order) if record.get("site_id") == "clock.poll_remote")
        dispatch = poll + 1
        wrong_order[poll], wrong_order[dispatch] = wrong_order[dispatch], wrong_order[poll]
        for sequence, record in enumerate(wrong_order[1:]):
            record["sequence"] = sequence
        self.assert_rejected(wrong_order, "ordering-site-sequence")

        interleaved_iterations = acceptance_records()
        ordering_start = next(
            index
            for index, record in enumerate(interleaved_iterations)
            if record.get("site_id") == "loop.input_drain_begin"
            and record.get("iteration") == 1
        )
        iteration_two_start = ordering_start + 5
        iteration_one = interleaved_iterations[ordering_start:iteration_two_start]
        iteration_two = interleaved_iterations[iteration_two_start:]
        interleaved_iterations[ordering_start:] = iteration_two + iteration_one
        for sequence, record in enumerate(interleaved_iterations[1:]):
            record["sequence"] = sequence
        self.assert_rejected(interleaved_iterations, "ordering-iteration-order")

        oversized_time = acceptance_records()
        refresh = next(
            record
            for record in oversized_time
            if record.get("site_id") == "scheduler.aircraft_refresh"
        )
        refresh["scheduled_time_ms"] = 1 << 63
        self.assert_rejected(oversized_time, "ordering-scheduled-time")

        missing_vector = acceptance_records()
        vector = next(record for record in missing_vector if record.get("vector_id") == "V4")
        del vector["vector_id"]
        del vector["observed"]
        self.assert_rejected(missing_vector, "acceptance-vector-coverage")

    def test_unknown_fields_duplicate_keys_and_profile_are_rejected(self) -> None:
        unknown = acceptance_records()
        unknown[1]["local_path"] = "must-not-be-accepted"
        self.assert_rejected(unknown, "sample-fields")

        invalid_profile = acceptance_records()
        invalid_profile[0]["profile"] = "maybe"
        self.assert_rejected(invalid_profile, "header-profile")

        boolean_sequence = acceptance_records()
        boolean_sequence[1]["sequence"] = False
        self.assert_rejected(boolean_sequence, "sample-sequence")

        late_startup = acceptance_records()
        late_startup[2], late_startup[3] = late_startup[3], late_startup[2]
        for sequence, record in enumerate(late_startup[1:]):
            record["sequence"] = sequence
        self.assert_rejected(late_startup, "startup-sequence")

        with tempfile.TemporaryDirectory(prefix="airfix-capture-duplicate-") as raw:
            path = Path(raw) / "capture.jsonl"
            path.write_text(
                '{"record":"header","record":"header","schema":"x","profile":"observation","modules":[]}\n',
                encoding="utf-8",
            )
            with self.assertRaises(VALIDATOR.CaptureValidationError) as caught:
                VALIDATOR.validate_capture(path)
        self.assertEqual(caught.exception.code, "invalid-json")

    def test_cli_output_never_echoes_input_path_or_capture_data(self) -> None:
        records = [header(), sample("startup.after_controlfp")]
        records[1]["sequence"] = 0
        with tempfile.TemporaryDirectory(prefix="private-marker-airfix-") as raw:
            path = Path(raw) / "secret-capture.aircraft-capture.jsonl"
            path.write_text(
                "".join(json.dumps(record) + "\n" for record in records),
                encoding="utf-8",
            )
            result = subprocess.run(
                [sys.executable, str(MODULE_PATH), "--input", str(path), "--require-go"],
                cwd=REPOSITORY_ROOT,
                capture_output=True,
                text=True,
                check=False,
            )
        combined = result.stdout + result.stderr
        self.assertEqual(result.returncode, 1)
        self.assertIn("decision=no-go", combined)
        self.assertNotIn("private-marker", combined)
        self.assertNotIn("secret-capture", combined)
        self.assertNotIn(VALIDATOR.REFERENCE_MODULES["Dogfighter.exe"], combined)


if __name__ == "__main__":
    unittest.main()
