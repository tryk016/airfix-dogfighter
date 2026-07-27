#!/usr/bin/env python3
"""Export a deterministic, path-free Rizin function report.

The report is intentionally a compact normalization of Rizin JSON responses,
not a dump of tool-internal objects.  This keeps the durable schema stable
across machines and prevents an input or tool installation path from entering
the report.
"""

from __future__ import annotations

import argparse
from collections.abc import Callable, Iterable, Mapping, Sequence
import copy
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from typing import Any, Protocol


SCHEMA = "airfix.re.rizin-function.v1"
RZPIPE_VERSION = "0.6.2"
_SHA256_RE = re.compile(r"^[0-9a-fA-F]{64}$")
_HEX_ADDRESS_RE = re.compile(r"^(?:0[xX])?([0-9a-fA-F]+)$")
_FUNCTION_ID_RE = re.compile(r"^FN-[A-Z0-9_]+-[0-9A-F]{8,16}$")
_CALL_TYPES = frozenset(
    {
        "call",
        "ccall",
        "icall",
        "ircall",
        "rcall",
        "ucall",
    }
)
_CODE_REF_TYPES = frozenset(
    {
        "call",
        "code",
        "exec",
        "icall",
        "ircall",
        "jump",
        "rcall",
        "ucall",
    }
)


class ExportError(RuntimeError):
    """Raised when a reproducible report cannot be produced."""


class RizinPipe(Protocol):
    def cmd(self, command: str) -> str:
        """Run a text command."""

    def cmdj(self, command: str) -> Any:
        """Run a JSON command."""

    def quit(self) -> None:
        """Quit the Rizin process."""


def parse_address(value: str) -> int:
    """Parse an RVA supplied as decimal or hexadecimal text.

    A ``0x`` prefix is preferred.  Unprefixed values containing A-F are
    hexadecimal; digit-only values are decimal.
    """

    text = value.strip()
    match = _HEX_ADDRESS_RE.fullmatch(text)
    if not match:
        raise argparse.ArgumentTypeError("address must be a non-negative integer")
    try:
        base = 16 if text.lower().startswith("0x") or re.search(r"[a-fA-F]", text) else 10
        parsed = int(text, base)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("address must be a non-negative integer") from exc
    if parsed < 0 or parsed > 0xFFFFFFFFFFFFFFFF:
        raise argparse.ArgumentTypeError("address is outside the unsigned 64-bit range")
    return parsed


def normalize_sha256(value: str) -> str:
    if not _SHA256_RE.fullmatch(value):
        raise ExportError("expected SHA-256 must contain exactly 64 hexadecimal digits")
    return value.lower()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def format_address(value: int) -> str:
    if value < 0:
        raise ExportError("negative addresses are not supported")
    if value > 0xFFFFFFFFFFFFFFFF:
        raise ExportError("address is outside the unsigned 64-bit range")
    width = 8 if value <= 0xFFFFFFFF else 16
    return f"0x{value:0{width}X}"


def _integer(value: Any) -> int | None:
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        return value if value >= 0 else None
    if isinstance(value, str):
        text = value.strip()
        if not text:
            return None
        try:
            if text.lower().startswith("0x"):
                parsed = int(text, 16)
            elif re.search(r"[a-fA-F]", text):
                parsed = int(text, 16)
            else:
                parsed = int(text, 10)
        except ValueError:
            return None
        return parsed if parsed >= 0 else None
    return None


def _first_integer(mapping: Mapping[str, Any], names: Iterable[str]) -> int | None:
    for name in names:
        parsed = _integer(mapping.get(name))
        if parsed is not None:
            return parsed
    return None


def _string(value: Any) -> str | None:
    if not isinstance(value, str):
        return None
    stripped = value.strip()
    return stripped or None


def _first_string(mapping: Mapping[str, Any], names: Iterable[str]) -> str | None:
    for name in names:
        parsed = _string(mapping.get(name))
        if parsed is not None:
            return parsed
    return None


def _mapping_list(value: Any) -> list[Mapping[str, Any]]:
    if isinstance(value, Mapping):
        return [value]
    if not isinstance(value, Sequence) or isinstance(value, (str, bytes, bytearray)):
        return []
    return [item for item in value if isinstance(item, Mapping)]


def _image_base(info: Any) -> int:
    if not isinstance(info, Mapping):
        raise ExportError("Rizin ij did not return an object")
    bin_info = info.get("bin")
    if isinstance(bin_info, Mapping):
        base = _first_integer(bin_info, ("baddr", "image_base", "imagebase"))
        if base is not None:
            return base
    core_info = info.get("core")
    if isinstance(core_info, Mapping):
        base = _first_integer(core_info, ("baddr", "image_base", "imagebase"))
        if base is not None:
            return base
    raise ExportError("Rizin ij did not report an image base")


def _binary_metadata(info: Any) -> dict[str, Any]:
    if not isinstance(info, Mapping):
        raise ExportError("Rizin ij did not return an object")
    core = info.get("core")
    binary = info.get("bin")
    if not isinstance(core, Mapping) or not isinstance(binary, Mapping):
        raise ExportError("Rizin ij did not report PE metadata")

    binary_format = (_first_string(core, ("format",)) or "").lower()
    binary_type = (_first_string(binary, ("bintype",)) or "").lower()
    binary_class = (_first_string(binary, ("class",)) or "").upper()
    architecture = (_first_string(binary, ("arch",)) or "").lower()
    bits = _first_integer(binary, ("bits",))
    if binary_format != "pe" and binary_type != "pe":
        raise ExportError("input is not a PE image")
    if binary_class != "PE32" or architecture != "x86" or bits != 32:
        raise ExportError("input must be native PE32/x86 with 32-bit code")

    return {
        "architecture": architecture,
        "bits": bits,
        "class": binary_class,
        "endian": _first_string(binary, ("endian",)),
        "format": "pe",
        "image_base": format_address(_image_base(info)),
        "machine": _first_string(binary, ("machine",)),
    }


def _requested_location(
    sections: Any,
    requested_va: int,
) -> dict[str, Any]:
    for section in _mapping_list(sections):
        virtual_address = _first_integer(section, ("vaddr", "va"))
        virtual_size = _first_integer(section, ("vsize",))
        raw_size = _first_integer(section, ("size",))
        physical_address = _first_integer(section, ("paddr", "offset"))
        if virtual_address is None:
            continue
        span = max(virtual_size or 0, raw_size or 0)
        if span <= 0 or not (virtual_address <= requested_va < virtual_address + span):
            continue
        delta = requested_va - virtual_address
        file_offset = None
        if (
            physical_address is not None
            and raw_size is not None
            and delta < raw_size
        ):
            file_offset = physical_address + delta
        return {
            "file_offset": (
                format_address(file_offset) if file_offset is not None else None
            ),
            "section": _first_string(section, ("name",)),
        }
    raise ExportError("requested VA is not contained in a reported PE section")


def _select_function(functions: Any, requested_va: int) -> Mapping[str, Any]:
    candidates = _mapping_list(functions)
    if not candidates:
        raise ExportError("Rizin afij did not identify the requested function")
    for candidate in candidates:
        if _first_integer(candidate, ("offset", "addr", "address")) == requested_va:
            return candidate
    raise ExportError("Rizin afij did not identify the requested function")


def _normalize_signature(function: Mapping[str, Any]) -> dict[str, Any]:
    nargs = _first_integer(function, ("nargs", "args"))
    return {
        "arguments": nargs,
        "calling_convention": _first_string(
            function,
            ("cc", "calltype", "calling_convention"),
        ),
        "name": _first_string(function, ("name", "realname")),
        "text": _first_string(function, ("signature", "sig", "type")),
    }


def _normalize_instructions(pdf: Any) -> list[dict[str, Any]]:
    if isinstance(pdf, Mapping):
        raw_ops = pdf.get("ops", [])
    else:
        raw_ops = []
    instructions: list[dict[str, Any]] = []
    for op in _mapping_list(raw_ops):
        address = _first_integer(op, ("offset", "addr", "address"))
        if address is None:
            continue
        entry: dict[str, Any] = {
            "bytes": _first_string(op, ("bytes",)),
            "opcode": _first_string(op, ("opcode", "disasm")),
            "size": _first_integer(op, ("size",)),
            "type": _first_string(op, ("type",)),
            "va": format_address(address),
        }
        jump = _first_integer(op, ("jump",))
        fail = _first_integer(op, ("fail",))
        if jump is not None:
            entry["jump"] = format_address(jump)
        if fail is not None:
            entry["fail"] = format_address(fail)
        instructions.append(entry)
    instructions.sort(key=lambda item: (item["va"], item.get("opcode") or ""))
    return instructions


def _graph_blocks(graph: Any) -> list[Mapping[str, Any]]:
    graphs = _mapping_list(graph)
    if not graphs:
        return []
    blocks = graphs[0].get("blocks", [])
    return _mapping_list(blocks)


def _normalize_cfg(graph: Any) -> dict[str, Any]:
    blocks: list[dict[str, Any]] = []
    edges: list[dict[str, str]] = []
    for block in _graph_blocks(graph):
        start = _first_integer(block, ("offset", "addr", "address"))
        if start is None:
            continue
        normalized: dict[str, Any] = {
            "size": _first_integer(block, ("size",)),
            "start": format_address(start),
        }
        jump = _first_integer(block, ("jump",))
        fail = _first_integer(block, ("fail",))
        if jump is not None:
            normalized["jump"] = format_address(jump)
            edges.append(
                {
                    "from": format_address(start),
                    "kind": "jump",
                    "to": format_address(jump),
                }
            )
        if fail is not None:
            normalized["fail"] = format_address(fail)
            edges.append(
                {
                    "from": format_address(start),
                    "kind": "fail",
                    "to": format_address(fail),
                }
            )
        blocks.append(normalized)
    blocks.sort(key=lambda item: item["start"])
    edges.sort(key=lambda item: (item["from"], item["kind"], item["to"]))
    return {"blocks": blocks, "edges": edges}


def _normalize_xrefs(xrefs: Any) -> list[dict[str, Any]]:
    normalized: list[dict[str, Any]] = []
    for xref in _mapping_list(xrefs):
        source = _first_integer(xref, ("from", "at", "source"))
        target = _first_integer(xref, ("to", "addr", "target"))
        if source is None and target is None:
            continue
        entry: dict[str, Any] = {
            "kind": (_first_string(xref, ("type", "kind")) or "unknown").lower(),
            "name": _first_string(xref, ("name", "refname", "target_name")),
            "site": format_address(source) if source is not None else None,
            "target": format_address(target) if target is not None else None,
        }
        normalized.append(entry)
    normalized.sort(
        key=lambda item: (
            item["site"] or "",
            item["target"] or "",
            item["kind"],
            item["name"] or "",
        )
    )
    return _deduplicate_dicts(normalized)


def _reference(
    *,
    site: int | None,
    target: int | None,
    kind: str,
    name: str | None = None,
) -> dict[str, Any] | None:
    if site is None and target is None:
        return None
    return {
        "kind": kind.lower(),
        "name": name,
        "site": format_address(site) if site is not None else None,
        "target": format_address(target) if target is not None else None,
    }


def _function_references(
    function: Mapping[str, Any],
    pdf: Any,
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    calls: list[dict[str, Any]] = []
    data_refs: list[dict[str, Any]] = []

    for raw in _mapping_list(function.get("callrefs", [])):
        kind = (_first_string(raw, ("type", "kind")) or "").lower()
        if kind not in _CALL_TYPES:
            continue
        reference = _reference(
            site=_first_integer(raw, ("from", "at")),
            target=_first_integer(raw, ("addr", "to", "target")),
            kind="call",
            name=_first_string(raw, ("name", "refname")),
        )
        if reference is not None:
            calls.append(reference)

    raw_datarefs = function.get("datarefs", [])
    if isinstance(raw_datarefs, Sequence) and not isinstance(
        raw_datarefs,
        (str, bytes, bytearray),
    ):
        for raw in raw_datarefs:
            if isinstance(raw, Mapping):
                reference = _reference(
                    site=_first_integer(raw, ("from", "at")),
                    target=_first_integer(raw, ("addr", "to", "target")),
                    kind=_first_string(raw, ("type", "kind")) or "data",
                    name=_first_string(raw, ("name", "refname")),
                )
            else:
                reference = _reference(
                    site=None,
                    target=_integer(raw),
                    kind="data",
                )
            if reference is not None:
                data_refs.append(reference)

    ops = pdf.get("ops", []) if isinstance(pdf, Mapping) else []
    for op in _mapping_list(ops):
        site = _first_integer(op, ("offset", "addr", "address"))
        op_type = (_first_string(op, ("type",)) or "unknown").lower()
        if op_type in _CALL_TYPES:
            targets: list[tuple[int, str | None]] = []
            jump = _first_integer(op, ("jump", "target"))
            if jump is not None:
                targets.append((jump, _first_string(op, ("name", "refname"))))
            for raw_ref in _mapping_list(op.get("xrefs_from", [])):
                ref_kind = (
                    _first_string(raw_ref, ("type", "kind")) or ""
                ).lower()
                target = _first_integer(raw_ref, ("addr", "to", "target"))
                if ref_kind in _CALL_TYPES and target is not None:
                    targets.append(
                        (target, _first_string(raw_ref, ("name", "refname")))
                    )
            for target, name in targets:
                reference = _reference(
                    site=site,
                    target=target,
                    kind="call",
                    name=name,
                )
                if reference is not None:
                    calls.append(reference)
        raw_op_refs = [
            *_mapping_list(op.get("refs", [])),
            *_mapping_list(op.get("xrefs_from", [])),
        ]
        for raw_ref in raw_op_refs:
            ref_kind = (_first_string(raw_ref, ("type", "kind")) or "data").lower()
            target = _first_integer(raw_ref, ("addr", "to", "target"))
            reference = _reference(
                site=site,
                target=target,
                kind=ref_kind,
                name=_first_string(raw_ref, ("name", "refname")),
            )
            if reference is None:
                continue
            if ref_kind not in _CODE_REF_TYPES and ref_kind not in _CALL_TYPES:
                data_refs.append(reference)

    calls = _merge_references(calls)
    data_refs = _merge_references(data_refs)
    key = lambda item: (
        item["site"] or "",
        item["target"] or "",
        item["kind"],
        item["name"] or "",
    )
    calls.sort(key=key)
    data_refs.sort(key=key)
    return calls, data_refs


def _deduplicate_dicts(items: list[dict[str, Any]]) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    seen: set[str] = set()
    for item in items:
        key = json.dumps(item, sort_keys=True, separators=(",", ":"))
        if key not in seen:
            seen.add(key)
            result.append(item)
    return result


def _merge_references(items: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Merge duplicate observations while retaining the best available name."""

    merged: dict[tuple[Any, ...], dict[str, Any]] = {}
    for item in items:
        key = (item["site"], item["target"], item["kind"])
        previous = merged.get(key)
        if previous is None:
            merged[key] = copy.deepcopy(item)
        elif previous["name"] is None and item["name"] is not None:
            previous["name"] = item["name"]
    return list(merged.values())


def _redact_paths(value: Any, paths: Sequence[str]) -> Any:
    if isinstance(value, dict):
        return {key: _redact_paths(item, paths) for key, item in value.items()}
    if isinstance(value, list):
        return [_redact_paths(item, paths) for item in value]
    if not isinstance(value, str):
        return value
    result = value
    for path in paths:
        if not path:
            continue
        variants = {
            path,
            path.replace("\\", "/"),
            path.replace("/", "\\"),
        }
        for variant in sorted(variants, key=len, reverse=True):
            result = re.sub(
                re.escape(variant),
                "<redacted-path>",
                result,
                flags=re.IGNORECASE,
            )
    return result


def _sleigh_config_command(sleigh_home: str) -> str:
    if any(character in sleigh_home for character in ("\0", "\r", "\n", ";")):
        raise ExportError("Sleigh home contains a character unsafe for a Rizin command")
    # Forward slashes work on every supported host and avoid Rizin treating
    # Windows backslashes as command escapes.
    value = sleigh_home.replace("\\", "/")
    return f"e ghidra.sleighhome={value}"


def build_report(
    pipe: RizinPipe,
    *,
    source_sha256: str,
    function_id: str,
    rva: int,
    rizin_metadata: Mapping[str, str | None],
    create_missing_function: bool = False,
    sleigh_home: str | None = None,
    redact_paths: Sequence[str] = (),
) -> dict[str, Any]:
    """Collect and normalize one function from an already-open Rizin pipe."""

    normalized_function_id = function_id.strip()
    if not _FUNCTION_ID_RE.fullmatch(normalized_function_id):
        raise ExportError("function ID must follow FN-<MODULE>-<RVA>")
    if rva < 0:
        raise ExportError("RVA must not be negative")

    pipe.cmd("e scr.color=0")
    pipe.cmd("e scr.utf8=false")
    pipe.cmd("aaa")

    info = pipe.cmdj("ij")
    binary_metadata = _binary_metadata(info)
    base = _image_base(info)
    va = base + rva
    if va > 0xFFFFFFFFFFFFFFFF:
        raise ExportError("image base plus RVA is outside the unsigned 64-bit range")
    at = format_address(va)
    sections = pipe.cmdj("iSj")
    location = _requested_location(sections, va)

    raw_function = pipe.cmdj(f"afij @ {at}")
    automatically_discovered = bool(_mapping_list(raw_function))
    if not automatically_discovered and create_missing_function:
        pipe.cmd(f"af @ {at}")
        raw_function = pipe.cmdj(f"afij @ {at}")
    raw_pdf = pipe.cmdj(f"pdfj @ {at}")
    raw_graph = pipe.cmdj(f"agfj @ {at}")
    raw_xrefs = pipe.cmdj(f"axfj @ {at}")

    function = _select_function(raw_function, va)
    reported_offset = _first_integer(function, ("offset", "addr", "address"))
    reported_size = _first_integer(function, ("size",))
    reported_realsz = _first_integer(function, ("realsz",))
    minimum_bound = _first_integer(function, ("minbound",))
    maximum_bound = _first_integer(function, ("maxbound",))
    start = minimum_bound
    if start is None:
        start = reported_offset if reported_offset is not None else va
    end = maximum_bound
    if end is None:
        fallback_size = (
            reported_realsz if reported_realsz is not None else reported_size
        )
        end = start + fallback_size if fallback_size is not None else None
    if end is not None and end < start:
        raise ExportError("Rizin afij reported an inverted function boundary")

    instructions = _normalize_instructions(raw_pdf)
    xrefs = _normalize_xrefs(raw_xrefs)
    calls, data_refs = _function_references(
        function,
        raw_pdf,
    )

    function_report: dict[str, Any] = {
        "boundary": {
            "end_exclusive": format_address(end) if end is not None else None,
            "offset": (
                format_address(reported_offset)
                if reported_offset is not None
                else None
            ),
            "realsz": reported_realsz,
            "size": reported_size,
            "start": format_address(start),
        },
        "calls": calls,
        "cfg": _normalize_cfg(raw_graph),
        "data_refs": data_refs,
        "discovery": {
            "automatic": automatically_discovered,
            "created_at_requested_va": not automatically_discovered,
        },
        "id": normalized_function_id,
        "instructions": instructions,
        "location": location,
        "rva": format_address(rva),
        "signature": _normalize_signature(function),
        "va": at,
        "xrefs": xrefs,
    }

    if sleigh_home is not None:
        pipe.cmd(_sleigh_config_command(sleigh_home))
        pseudocode = pipe.cmd(f"pdg @ {at}")
        function_report["pseudocode"] = {
            "engine": "rz-ghidra",
            "text": pseudocode.replace("\r\n", "\n").replace("\r", "\n").rstrip(),
        }

    report = {
        "binary": binary_metadata,
        "functions": [function_report],
        "schema": SCHEMA,
        "source": {"sha256": normalize_sha256(source_sha256)},
        "tools": {
            "rizin": {
                "commit": _string(rizin_metadata.get("commit")),
                "platform": _string(rizin_metadata.get("platform")),
                "version": _string(rizin_metadata.get("version")),
            },
            "rzpipe": {"version": RZPIPE_VERSION},
        },
    }
    return _redact_paths(report, redact_paths)


def _rizin_executable(rizin_home: Path) -> Path:
    executable_name = "rizin.exe" if os.name == "nt" else "rizin"
    return rizin_home / executable_name


def query_rizin_metadata(rizin_home: Path) -> dict[str, str | None]:
    """Read reproducibility metadata directly from the selected Rizin binary."""

    try:
        result = subprocess.run(
            [os.fspath(_rizin_executable(rizin_home)), "-v"],
            check=False,
            capture_output=True,
            text=True,
            timeout=10,
        )
    except (OSError, subprocess.SubprocessError) as exc:
        raise ExportError("failed to query the configured Rizin version") from exc
    if result.returncode != 0:
        raise ExportError("configured Rizin failed its version query")

    output = "\n".join((result.stdout, result.stderr))
    version_match = re.search(
        r"(?im)^\s*rizin\s+([^\s@]+)(?:\s+@\s+([^\r\n]+))?\s*$",
        output,
    )
    commit_match = re.search(r"(?im)^\s*commit:\s*([0-9a-f]+)\s*$", output)
    if version_match is None:
        raise ExportError("configured Rizin returned an unrecognized version string")
    return {
        "commit": commit_match.group(1).lower() if commit_match else None,
        "platform": (
            version_match.group(2).strip() if version_match.group(2) else None
        ),
        "version": version_match.group(1),
    }


def _open_pipe(input_path: Path, rizin_home: Path) -> RizinPipe:
    try:
        import rzpipe  # type: ignore[import-not-found]
    except ImportError as exc:
        raise ExportError(
            "rzpipe is not installed; install tools/rizin/requirements.txt"
        ) from exc
    if rzpipe.version() != RZPIPE_VERSION:
        raise ExportError(
            f"rzpipe {RZPIPE_VERSION} is required for reproducible reports"
        )
    try:
        return rzpipe.open(
            os.fspath(input_path),
            flags=["-2", "-N"],
            rizin_home=os.fspath(rizin_home),
        )
    except Exception as exc:
        raise ExportError("failed to start the configured Rizin executable") from exc


def _validate_runtime_paths(
    input_path: Path,
    output_path: Path,
    rizin_home: Path,
    sleigh_home: Path | None,
) -> None:
    if not input_path.is_file():
        raise ExportError("input must be an existing regular file")
    if output_path.resolve() == input_path.resolve():
        raise ExportError("output must not overwrite the input file")
    if not rizin_home.is_dir() or not _rizin_executable(rizin_home).is_file():
        raise ExportError("Rizin home must contain the Rizin executable")
    if sleigh_home is not None and not sleigh_home.is_dir():
        raise ExportError("Sleigh home must be an existing directory")


def export_to_file(
    *,
    input_path: Path,
    output_path: Path,
    rizin_home: Path,
    function_id: str,
    rva: int,
    expected_sha256: str,
    create_missing_function: bool = False,
    sleigh_home: Path | None = None,
    pipe_opener: Callable[[Path, Path], RizinPipe] = _open_pipe,
    rizin_metadata_provider: Callable[
        [Path],
        Mapping[str, str | None],
    ] = query_rizin_metadata,
) -> dict[str, Any]:
    _validate_runtime_paths(input_path, output_path, rizin_home, sleigh_home)
    expected = normalize_sha256(expected_sha256)
    actual = sha256_file(input_path)
    if actual != expected:
        raise ExportError("input SHA-256 does not match the expected manifest value")

    rizin_metadata = rizin_metadata_provider(rizin_home)
    pipe = pipe_opener(input_path, rizin_home)
    try:
        report = build_report(
            pipe,
            source_sha256=actual,
            function_id=function_id,
            rva=rva,
            rizin_metadata=rizin_metadata,
            create_missing_function=create_missing_function,
            sleigh_home=os.fspath(sleigh_home.resolve()) if sleigh_home else None,
            redact_paths=(
                os.fspath(input_path.resolve()),
                os.fspath(rizin_home.resolve()),
                os.fspath(sleigh_home.resolve()) if sleigh_home else "",
            ),
        )
    finally:
        pipe.quit()

    final_hash = sha256_file(input_path)
    if final_hash != expected:
        raise ExportError("input SHA-256 changed during analysis")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    temporary = output_path.with_name(output_path.name + ".tmp")
    try:
        with temporary.open("w", encoding="utf-8", newline="\n") as stream:
            json.dump(
                report,
                stream,
                ensure_ascii=True,
                indent=2,
                sort_keys=True,
            )
            stream.write("\n")
        os.replace(temporary, output_path)
    except BaseException:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass
        raise
    return report


def create_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Export one deterministic Rizin function report.",
    )
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--rizin-home", required=True, type=Path)
    parser.add_argument("--function-id", required=True)
    parser.add_argument("--rva", required=True, type=parse_address)
    parser.add_argument("--expected-sha256", required=True)
    parser.add_argument("--create-missing-function", action="store_true")
    parser.add_argument("--sleigh-home", type=Path)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = create_argument_parser()
    args = parser.parse_args(argv)
    try:
        export_to_file(
            input_path=args.input,
            output_path=args.output,
            rizin_home=args.rizin_home,
            function_id=args.function_id,
            rva=args.rva,
            expected_sha256=args.expected_sha256,
            create_missing_function=args.create_missing_function,
            sleigh_home=args.sleigh_home,
        )
    except (ExportError, OSError) as exc:
        print(f"rizin-export: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
