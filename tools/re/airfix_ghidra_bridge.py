#!/usr/bin/env python3
"""Portable, path-free core for the Airfix Ghidra Knowledge Bridge.

This module intentionally owns no Ghidra project state.  It validates and
normalizes public JSON exchanged by headless exporters, and it keeps the
comparison with Rizin deliberately narrow: a report contains only evidence
that differs.  The code uses only the Python standard library so that it can
run in the same controlled environments as the existing analysis wrappers.
"""

from __future__ import annotations

import argparse
import copy
import json
import math
import os
from pathlib import Path
import re
import tempfile
from typing import Any, Callable, Iterable, Mapping, Sequence


KNOWLEDGE_SCHEMA = "airfix.re.ghidra-knowledge.v1"
FUNCTION_SCHEMA = "airfix.re.ghidra-function.v1"
RIZIN_FUNCTION_SCHEMA = "airfix.re.rizin-function.v1"
DIFF_SCHEMA = "airfix.re.ghidra-rizin-diff.v1"
MAX_INPUT_BYTES = 32 * 1024 * 1024
MAX_COLLECTION_ITEMS = 100_000
MAX_TEXT_BYTES = 1_000_000
MAX_KNOWLEDGE_IDENTIFIER_CHARS = 160
MAX_EVIDENCE_SUMMARY_CHARS = 1_000
MAX_NOTE_CHARS = 4_000
MAX_VTABLE_SLOT_INDEX = 4_095
MAX_STRUCTURE_SIZE = 0x00FF_FFFF

_SHA256_RE = re.compile(r"^[0-9a-fA-F]{64}$")
_EVIDENCE_ID_RE = re.compile(r"^EV-[0-9]{8}-[0-9]{3}$")
_FUNCTION_ID_RE = re.compile(r"^FN-[A-Z0-9][A-Z0-9_-]*-([0-9A-F]{8})$")
_ADDRESS_RE = re.compile(r"^(?:0[xX])?([0-9a-fA-F]+)$")
_DRIVE_PATH_RE = re.compile(r"(?:^|[\s\"'])?[A-Za-z]:[\\/]")
_UNC_PATH_RE = re.compile(r"(?:^|[\s\"'])(?:\\\\)[^\s\"'<>|]+")
_POSIX_PATH_RE = re.compile(
    r"(?i)(?:^|[\s\"'])"
    r"(/(?:[A-Za-z0-9._+@=-]+/)*[A-Za-z0-9._+@=-]+[^\s\"'<>|]*)"
)
_SAFE_IDENTIFIER_RE = re.compile(r"^[^\x00\r\n]+$")
_ASSESSMENT_STATUSES = frozenset(
    {"confirmed", "conditional", "hypothesis", "unknown"}
)
_CONSTANT_KINDS = frozenset({"integer", "f32", "f64"})


class BridgeError(ValueError):
    """Raised when bridge input is malformed, unsafe, or inconsistent."""


def _require_mapping(value: Any, context: str) -> Mapping[str, Any]:
    if not isinstance(value, Mapping):
        raise BridgeError(f"{context} must be an object")
    return value


def _require_list(value: Any, context: str) -> list[Any]:
    if not isinstance(value, list):
        raise BridgeError(f"{context} must be an array")
    if len(value) > MAX_COLLECTION_ITEMS:
        raise BridgeError(f"{context} exceeds the item limit")
    return value


def _require_exact_keys(
    mapping: Mapping[str, Any],
    *,
    required: Iterable[str],
    optional: Iterable[str] = (),
    context: str,
) -> None:
    required_set = set(required)
    permitted = required_set | set(optional)
    missing = sorted(required_set - set(mapping))
    unknown = sorted(set(mapping) - permitted)
    if missing:
        raise BridgeError(f"{context} is missing required fields: {', '.join(missing)}")
    if unknown:
        raise BridgeError(f"{context} has unknown fields: {', '.join(unknown)}")


def _safe_text(
    value: Any,
    context: str,
    *,
    identifier: bool = False,
    max_chars: int | None = None,
) -> str:
    if not isinstance(value, str):
        raise BridgeError(f"{context} must be a string")
    if not value:
        raise BridgeError(f"{context} must not be empty")
    if len(value.encode("utf-8")) > MAX_TEXT_BYTES:
        raise BridgeError(f"{context} exceeds the text limit")
    if max_chars is not None and len(value) > max_chars:
        raise BridgeError(f"{context} exceeds the {max_chars}-character limit")
    if "\x00" in value:
        raise BridgeError(f"{context} contains a NUL character")
    if identifier and not _SAFE_IDENTIFIER_RE.fullmatch(value):
        raise BridgeError(f"{context} contains a newline or control injection")
    if _looks_like_local_path(value):
        raise BridgeError(f"{context} must not contain an absolute or local path")
    return value


def _looks_like_local_path(value: str) -> bool:
    """Reject forms that could leak a host path into public evidence."""

    text = value.strip()
    return bool(
        _DRIVE_PATH_RE.search(text)
        or _UNC_PATH_RE.search(text)
        or _POSIX_PATH_RE.search(text)
    )


def _assert_no_unsafe_text(value: Any, context: str = "input") -> None:
    """Walk free-form exporter fields that are intentionally schema-flexible."""

    if isinstance(value, str):
        if value == "":
            return
        _safe_text(value, context)
        return
    if value is None or isinstance(value, (bool, int, float)):
        return
    if isinstance(value, Mapping):
        if len(value) > MAX_COLLECTION_ITEMS:
            raise BridgeError(f"{context} exceeds the item limit")
        for key, nested in value.items():
            _safe_text(key, f"{context} key", identifier=True)
            _assert_no_unsafe_text(nested, f"{context}.{key}")
        return
    if isinstance(value, list):
        if len(value) > MAX_COLLECTION_ITEMS:
            raise BridgeError(f"{context} exceeds the item limit")
        for index, nested in enumerate(value):
            _assert_no_unsafe_text(nested, f"{context}[{index}]")
        return
    raise BridgeError(f"{context} contains unsupported JSON value")


def normalize_sha256(value: Any, context: str = "sha256") -> str:
    if not isinstance(value, str) or not _SHA256_RE.fullmatch(value):
        raise BridgeError(f"{context} must contain exactly 64 hexadecimal digits")
    return value.lower()


def _parse_uint(value: Any, context: str, *, maximum: int = 0xFFFFFFFF) -> int:
    if isinstance(value, bool):
        raise BridgeError(f"{context} must be an unsigned integer")
    if isinstance(value, int):
        parsed = value
    elif isinstance(value, str):
        _safe_text(value, context, identifier=True)
        matched = _ADDRESS_RE.fullmatch(value.strip())
        if not matched:
            raise BridgeError(f"{context} must be an unsigned hexadecimal integer")
        parsed = int(matched.group(1), 16)
    else:
        raise BridgeError(f"{context} must be an unsigned integer")
    if parsed < 0 or parsed > maximum:
        raise BridgeError(f"{context} is outside the supported range")
    return parsed


def normalize_rva(value: Any, context: str = "RVA") -> str:
    return f"0x{_parse_uint(value, context):08X}"


def _normalize_identifier(
    value: Any, context: str, *, max_chars: int | None = None
) -> str:
    normalized = _safe_text(
        value, context, identifier=True, max_chars=max_chars
    ).strip()
    if not normalized:
        raise BridgeError(f"{context} must not be whitespace")
    return normalized


def _canonical_json_value(value: Any, context: str = "value") -> Any:
    """Return recursively sorted JSON while rejecting unsafe free-form data."""

    if isinstance(value, str):
        return _safe_text(value, context)
    if value is None or isinstance(value, (bool, int)):
        return value
    if isinstance(value, float):
        if not math.isfinite(value):
            raise BridgeError(f"{context} contains a non-finite number")
        return value
    if isinstance(value, Mapping):
        if len(value) > MAX_COLLECTION_ITEMS:
            raise BridgeError(f"{context} exceeds the item limit")
        normalized: dict[str, Any] = {}
        for key in sorted(value):
            normalized_key = _normalize_identifier(key, f"{context} key")
            normalized[normalized_key] = _canonical_json_value(
                value[key], f"{context}.{normalized_key}"
            )
        return normalized
    if isinstance(value, list):
        if len(value) > MAX_COLLECTION_ITEMS:
            raise BridgeError(f"{context} exceeds the item limit")
        return [
            _canonical_json_value(nested, f"{context}[{index}]")
            for index, nested in enumerate(value)
        ]
    raise BridgeError(f"{context} contains an unsupported JSON value")


def _stable_key(value: Any) -> str:
    return json.dumps(value, ensure_ascii=True, sort_keys=True, separators=(",", ":"))


def _sorted_unique(items: Sequence[Any], context: str) -> list[Any]:
    canonical = [_canonical_json_value(item, context) for item in items]
    result: list[Any] = []
    previous: str | None = None
    for item in sorted(canonical, key=_stable_key):
        key = _stable_key(item)
        if key == previous:
            continue
        result.append(item)
        previous = key
    return result


def _normalize_notes(value: Any, context: str) -> dict[str, list[str]]:
    mapping = _require_mapping(value, context)
    _require_exact_keys(mapping, required=("comments", "hypotheses"), context=context)
    result: dict[str, list[str]] = {}
    for name in ("comments", "hypotheses"):
        entries = _require_list(mapping[name], f"{context}.{name}")
        result[name] = sorted(
            {
                _safe_text(
                    entry, f"{context}.{name}", max_chars=MAX_NOTE_CHARS
                )
                for entry in entries
            }
        )
    return result


def _normalize_assessment(value: Any, context: str) -> dict[str, Any]:
    mapping = _require_mapping(value, context)
    _require_exact_keys(
        mapping,
        required=("status", "confidence", "evidence", "apply_eligible"),
        context=context,
    )
    status = _normalize_identifier(mapping["status"], f"{context}.status").lower()
    if status not in _ASSESSMENT_STATUSES:
        raise BridgeError(f"{context}.status is not a recognized assessment status")
    confidence = mapping["confidence"]
    if isinstance(confidence, bool) or not isinstance(confidence, int):
        raise BridgeError(f"{context}.confidence must be an integer from 0 to 3")
    if confidence < 0 or confidence > 3:
        raise BridgeError(f"{context}.confidence must be from 0 to 3")
    apply_eligible = mapping["apply_eligible"]
    if not isinstance(apply_eligible, bool):
        raise BridgeError(f"{context}.apply_eligible must be boolean")
    evidence_entries = _require_list(mapping["evidence"], f"{context}.evidence")
    evidence: list[dict[str, str]] = []
    for index, raw_entry in enumerate(evidence_entries):
        entry = _require_mapping(raw_entry, f"{context}.evidence[{index}]")
        _require_exact_keys(
            entry,
            required=("id", "summary"),
            context=f"{context}.evidence[{index}]",
        )
        evidence_id = _normalize_identifier(
            entry["id"],
            f"{context}.evidence[{index}].id",
            max_chars=MAX_KNOWLEDGE_IDENTIFIER_CHARS,
        )
        if not _EVIDENCE_ID_RE.fullmatch(evidence_id):
            raise BridgeError(
                f"{context}.evidence[{index}].id must use EV-YYYYMMDD-NNN"
            )
        evidence.append(
            {
                "id": evidence_id,
                "summary": _safe_text(
                    entry["summary"],
                    f"{context}.evidence[{index}].summary",
                    max_chars=MAX_EVIDENCE_SUMMARY_CHARS,
                ),
            }
        )
    evidence.sort(key=lambda item: (item["id"], item["summary"]))
    deduplicated: list[dict[str, str]] = []
    for entry in evidence:
        if not deduplicated or entry != deduplicated[-1]:
            deduplicated.append(entry)
    if apply_eligible and (
        status != "confirmed" or confidence < 2 or not deduplicated
    ):
        raise BridgeError(
            f"{context}.apply_eligible is only legal for confirmed, confidence>=2, "
            "evidence-backed records"
        )
    return {
        "status": status,
        "confidence": confidence,
        "evidence": deduplicated,
        "apply_eligible": apply_eligible,
    }


def _normalize_argument(value: Any, context: str) -> dict[str, str]:
    mapping = _require_mapping(value, context)
    _require_exact_keys(mapping, required=("name", "type"), optional=("storage",), context=context)
    normalized = {
        "name": _normalize_identifier(
            mapping["name"], f"{context}.name", max_chars=MAX_KNOWLEDGE_IDENTIFIER_CHARS
        ),
        "type": _normalize_identifier(
            mapping["type"], f"{context}.type", max_chars=MAX_KNOWLEDGE_IDENTIFIER_CHARS
        ),
    }
    if "storage" in mapping:
        normalized["storage"] = _normalize_identifier(
            mapping["storage"],
            f"{context}.storage",
            max_chars=MAX_KNOWLEDGE_IDENTIFIER_CHARS,
        )
    return normalized


def _normalize_this_field(value: Any, context: str) -> dict[str, Any]:
    mapping = _require_mapping(value, context)
    _require_exact_keys(
        mapping,
        required=("offset", "name", "type", "assessment", "notes"),
        context=context,
    )
    return {
        "offset": normalize_rva(mapping["offset"], f"{context}.offset"),
        "name": _normalize_identifier(
            mapping["name"], f"{context}.name", max_chars=MAX_KNOWLEDGE_IDENTIFIER_CHARS
        ),
        "type": _normalize_identifier(
            mapping["type"], f"{context}.type", max_chars=MAX_KNOWLEDGE_IDENTIFIER_CHARS
        ),
        "assessment": _normalize_assessment(mapping["assessment"], f"{context}.assessment"),
        "notes": _normalize_notes(mapping["notes"], f"{context}.notes"),
    }


def _assert_unique(items: Sequence[Any], key: Callable[[Any], Any], context: str) -> None:
    seen: set[Any] = set()
    for item in items:
        item_key = key(item)
        if item_key in seen:
            raise BridgeError(f"{context} contains a duplicate key {item_key!r}")
        seen.add(item_key)


def _normalize_function_knowledge(value: Any, context: str) -> dict[str, Any]:
    mapping = _require_mapping(value, context)
    _require_exact_keys(
        mapping,
        required=("rva", "name", "signature", "this_fields", "assessment", "notes"),
        context=context,
    )
    signature = _require_mapping(mapping["signature"], f"{context}.signature")
    _require_exact_keys(
        signature,
        required=("calling_convention", "return_type", "arguments"),
        context=f"{context}.signature",
    )
    arguments = [
        _normalize_argument(raw, f"{context}.signature.arguments[{index}]")
        for index, raw in enumerate(
            _require_list(signature["arguments"], f"{context}.signature.arguments")
        )
    ]
    _assert_unique(arguments, lambda item: item["name"], f"{context}.signature.arguments")
    fields = [
        _normalize_this_field(raw, f"{context}.this_fields[{index}]")
        for index, raw in enumerate(
            _require_list(mapping["this_fields"], f"{context}.this_fields")
        )
    ]
    _assert_unique(fields, lambda item: item["offset"], f"{context}.this_fields")
    fields.sort(key=lambda item: int(item["offset"][2:], 16))
    return {
        "rva": normalize_rva(mapping["rva"], f"{context}.rva"),
        "name": _normalize_identifier(
            mapping["name"], f"{context}.name", max_chars=MAX_KNOWLEDGE_IDENTIFIER_CHARS
        ),
        "signature": {
            "calling_convention": _normalize_identifier(
                signature["calling_convention"],
                f"{context}.signature.calling_convention",
                max_chars=MAX_KNOWLEDGE_IDENTIFIER_CHARS,
            ),
            "return_type": _normalize_identifier(
                signature["return_type"],
                f"{context}.signature.return_type",
                max_chars=MAX_KNOWLEDGE_IDENTIFIER_CHARS,
            ),
            "arguments": arguments,
        },
        "this_fields": fields,
        "assessment": _normalize_assessment(mapping["assessment"], f"{context}.assessment"),
        "notes": _normalize_notes(mapping["notes"], f"{context}.notes"),
    }


def _normalize_structure(value: Any, context: str) -> dict[str, Any]:
    mapping = _require_mapping(value, context)
    _require_exact_keys(
        mapping,
        required=("name", "size", "fields", "assessment", "notes"),
        context=context,
    )
    fields = [
        _normalize_this_field(raw, f"{context}.fields[{index}]")
        for index, raw in enumerate(_require_list(mapping["fields"], f"{context}.fields"))
    ]
    _assert_unique(fields, lambda item: item["offset"], f"{context}.fields")
    fields.sort(key=lambda item: int(item["offset"][2:], 16))
    size = _parse_uint(
        mapping["size"],
        f"{context}.size",
        maximum=MAX_STRUCTURE_SIZE,
    )
    if size == 0:
        raise BridgeError(f"{context}.size must be greater than zero")
    return {
        "name": _normalize_identifier(
            mapping["name"], f"{context}.name", max_chars=MAX_KNOWLEDGE_IDENTIFIER_CHARS
        ),
        "size": normalize_rva(size, f"{context}.size"),
        "fields": fields,
        "assessment": _normalize_assessment(mapping["assessment"], f"{context}.assessment"),
        "notes": _normalize_notes(mapping["notes"], f"{context}.notes"),
    }


def _normalize_vtable_slot(value: Any, context: str) -> dict[str, Any]:
    mapping = _require_mapping(value, context)
    _require_exact_keys(
        mapping,
        required=("index", "target_rva", "assessment", "notes"),
        optional=("name",),
        context=context,
    )
    index = mapping["index"]
    if (
        isinstance(index, bool)
        or not isinstance(index, int)
        or index < 0
        or index > MAX_VTABLE_SLOT_INDEX
    ):
        raise BridgeError(
            f"{context}.index must be an integer from 0 to {MAX_VTABLE_SLOT_INDEX}"
        )
    result: dict[str, Any] = {
        "index": index,
        "target_rva": normalize_rva(mapping["target_rva"], f"{context}.target_rva"),
        "assessment": _normalize_assessment(mapping["assessment"], f"{context}.assessment"),
        "notes": _normalize_notes(mapping["notes"], f"{context}.notes"),
    }
    if "name" in mapping:
        result["name"] = _normalize_identifier(
            mapping["name"], f"{context}.name", max_chars=MAX_KNOWLEDGE_IDENTIFIER_CHARS
        )
    return result


def _normalize_vtable(value: Any, context: str) -> dict[str, Any]:
    mapping = _require_mapping(value, context)
    _require_exact_keys(
        mapping,
        required=("rva", "class_name", "slots", "assessment", "notes"),
        context=context,
    )
    slots = [
        _normalize_vtable_slot(raw, f"{context}.slots[{index}]")
        for index, raw in enumerate(_require_list(mapping["slots"], f"{context}.slots"))
    ]
    _assert_unique(slots, lambda item: item["index"], f"{context}.slots")
    slots.sort(key=lambda item: item["index"])
    return {
        "rva": normalize_rva(mapping["rva"], f"{context}.rva"),
        "class_name": _normalize_identifier(
            mapping["class_name"],
            f"{context}.class_name",
            max_chars=MAX_KNOWLEDGE_IDENTIFIER_CHARS,
        ),
        "slots": slots,
        "assessment": _normalize_assessment(mapping["assessment"], f"{context}.assessment"),
        "notes": _normalize_notes(mapping["notes"], f"{context}.notes"),
    }


def _normalize_event(value: Any, context: str) -> dict[str, Any]:
    mapping = _require_mapping(value, context)
    _require_exact_keys(
        mapping,
        required=("id", "name", "assessment", "notes"),
        context=context,
    )
    return {
        "id": normalize_rva(mapping["id"], f"{context}.id"),
        "name": _normalize_identifier(
            mapping["name"], f"{context}.name", max_chars=MAX_KNOWLEDGE_IDENTIFIER_CHARS
        ),
        "assessment": _normalize_assessment(mapping["assessment"], f"{context}.assessment"),
        "notes": _normalize_notes(mapping["notes"], f"{context}.notes"),
    }


def normalize_knowledge(document: Any) -> dict[str, Any]:
    """Validate and deterministically normalize public curated knowledge."""

    mapping = _require_mapping(document, "knowledge")
    _require_exact_keys(mapping, required=("schema", "modules"), context="knowledge")
    if mapping["schema"] != KNOWLEDGE_SCHEMA:
        raise BridgeError(f"knowledge.schema must be {KNOWLEDGE_SCHEMA!r}")
    modules: list[dict[str, Any]] = []
    for module_index, raw_module in enumerate(_require_list(mapping["modules"], "knowledge.modules")):
        context = f"knowledge.modules[{module_index}]"
        module = _require_mapping(raw_module, context)
        _require_exact_keys(
            module,
            required=("sha256", "functions", "structures", "vtables", "event_ids"),
            context=context,
        )
        functions = [
            _normalize_function_knowledge(raw, f"{context}.functions[{index}]")
            for index, raw in enumerate(_require_list(module["functions"], f"{context}.functions"))
        ]
        structures = [
            _normalize_structure(raw, f"{context}.structures[{index}]")
            for index, raw in enumerate(_require_list(module["structures"], f"{context}.structures"))
        ]
        vtables = [
            _normalize_vtable(raw, f"{context}.vtables[{index}]")
            for index, raw in enumerate(_require_list(module["vtables"], f"{context}.vtables"))
        ]
        events = [
            _normalize_event(raw, f"{context}.event_ids[{index}]")
            for index, raw in enumerate(_require_list(module["event_ids"], f"{context}.event_ids"))
        ]
        _assert_unique(functions, lambda item: item["rva"], f"{context}.functions")
        _assert_unique(structures, lambda item: item["name"], f"{context}.structures")
        _assert_unique(vtables, lambda item: item["rva"], f"{context}.vtables")
        _assert_unique(events, lambda item: item["id"], f"{context}.event_ids")
        modules.append(
            {
                "sha256": normalize_sha256(module["sha256"], f"{context}.sha256"),
                "functions": sorted(functions, key=lambda item: item["rva"]),
                "structures": sorted(structures, key=lambda item: item["name"]),
                "vtables": sorted(vtables, key=lambda item: item["rva"]),
                "event_ids": sorted(events, key=lambda item: item["id"]),
            }
        )
    if not modules:
        raise BridgeError("knowledge.modules must not be empty")
    _assert_unique(modules, lambda item: item["sha256"], "knowledge.modules")
    modules.sort(key=lambda item: item["sha256"])
    return {"schema": KNOWLEDGE_SCHEMA, "modules": modules}


def _normalize_reference(
    value: Any,
    context: str,
    *,
    require_target: bool = True,
) -> dict[str, Any]:
    mapping = _require_mapping(value, context)
    _require_exact_keys(
        mapping,
        required=("target_rva",) if require_target else (),
        optional=(
            "target_rva",
            "site_rva",
            "name",
            "kind",
            "site_space",
            "target_space",
            "text",
            "direction",
            "candidate_displacements",
        ),
        context=context,
    )
    if not require_target and "target_rva" not in mapping and "site_rva" not in mapping:
        raise BridgeError(f"{context} must contain target_rva or site_rva")
    result: dict[str, Any] = {}
    if "target_rva" in mapping:
        result["target_rva"] = normalize_rva(
            mapping["target_rva"], f"{context}.target_rva"
        )
    for name in (
        "site_rva",
        "name",
        "kind",
        "site_space",
        "target_space",
        "text",
        "direction",
    ):
        if name in mapping:
            result[name] = (
                normalize_rva(mapping[name], f"{context}.{name}")
                if name == "site_rva"
                else _normalize_identifier(mapping[name], f"{context}.{name}")
            )
    if "direction" in result and result["direction"] not in {
        "incoming",
        "outgoing",
    }:
        raise BridgeError(f"{context}.direction must be incoming or outgoing")
    if "candidate_displacements" in mapping:
        result["candidate_displacements"] = sorted(
            {
                normalize_rva(
                    raw, f"{context}.candidate_displacements"
                )
                for raw in _require_list(
                    mapping["candidate_displacements"],
                    f"{context}.candidate_displacements",
                )
            }
        )
    return result


def _normalize_constants(value: Any, context: str) -> list[dict[str, Any]]:
    constants: list[dict[str, Any]] = []
    for index, raw in enumerate(_require_list(value, context)):
        entry = _require_mapping(raw, f"{context}[{index}]")
        _require_exact_keys(
            entry,
            required=("kind", "bits"),
            optional=("value", "width", "signed_value", "source_rva"),
            context=f"{context}[{index}]",
        )
        kind = _normalize_identifier(entry["kind"], f"{context}[{index}].kind").lower()
        if kind not in _CONSTANT_KINDS:
            raise BridgeError(f"{context}[{index}].kind is not supported")
        bits_text = _normalize_identifier(entry["bits"], f"{context}[{index}].bits")
        match = _ADDRESS_RE.fullmatch(bits_text)
        if match is None:
            raise BridgeError(f"{context}[{index}].bits must be hexadecimal")
        bit_digits = len(match.group(1))
        if kind == "f32" and bit_digits != 8:
            raise BridgeError(f"{context}[{index}].bits must be 32-bit for f32")
        if kind == "f64" and bit_digits != 16:
            raise BridgeError(f"{context}[{index}].bits must be 64-bit for f64")
        if kind == "integer" and (bit_digits < 2 or bit_digits > 16 or bit_digits % 2):
            raise BridgeError(f"{context}[{index}].integer bits must span one to eight bytes")
        bits = int(match.group(1), 16)
        normalized: dict[str, Any] = {
            "kind": kind,
            "bits": f"0x{bits:0{bit_digits}X}",
        }
        if "value" in entry:
            normalized["value"] = _safe_text(entry["value"], f"{context}[{index}].value")
        if "width" in entry:
            width = entry["width"]
            if isinstance(width, bool) or not isinstance(width, int) or width < 1 or width > 8:
                raise BridgeError(f"{context}[{index}].width must be from 1 to 8")
            expected_width = bit_digits // 2
            if width != expected_width:
                raise BridgeError(f"{context}[{index}].width does not match bits")
            normalized["width"] = width
        if "signed_value" in entry:
            signed_value = entry["signed_value"]
            if isinstance(signed_value, bool) or not isinstance(signed_value, int):
                raise BridgeError(f"{context}[{index}].signed_value must be an integer")
            normalized["signed_value"] = signed_value
        if "source_rva" in entry:
            normalized["source_rva"] = normalize_rva(
                entry["source_rva"], f"{context}[{index}].source_rva"
            )
        constants.append(normalized)
    return _sorted_unique(constants, context)


def _normalize_flexible_entries(value: Any, context: str) -> list[Any]:
    entries = _require_list(value, context)
    return _sorted_unique(entries, context)


def _normalize_ordered_entries(value: Any, context: str) -> list[Any]:
    """Normalize ordered program evidence without changing its semantics."""

    return [
        _canonical_json_value(item, f"{context}[{index}]")
        for index, item in enumerate(_require_list(value, context))
    ]


def _normalize_function_signature(value: Any, context: str) -> dict[str, Any]:
    mapping = _require_mapping(value, context)
    _require_exact_keys(
        mapping,
        required=("calling_convention", "return_type", "arguments"),
        optional=("no_return", "varargs"),
        context=context,
    )
    arguments: list[dict[str, Any]] = []
    for index, raw_argument in enumerate(_require_list(mapping["arguments"], f"{context}.arguments")):
        argument = _require_mapping(raw_argument, f"{context}.arguments[{index}]")
        _require_exact_keys(
            argument,
            required=("name", "type", "storage"),
            optional=("ordinal", "auto"),
            context=f"{context}.arguments[{index}]",
        )
        normalized_argument: dict[str, Any] = {
            "name": _normalize_identifier(argument["name"], f"{context}.arguments[{index}].name"),
            "type": _normalize_identifier(argument["type"], f"{context}.arguments[{index}].type"),
            "storage": _normalize_identifier(
                argument["storage"], f"{context}.arguments[{index}].storage"
            ),
        }
        if "ordinal" in argument:
            ordinal = argument["ordinal"]
            if isinstance(ordinal, bool) or not isinstance(ordinal, int) or ordinal < 0:
                raise BridgeError(f"{context}.arguments[{index}].ordinal must be non-negative")
            normalized_argument["ordinal"] = ordinal
        if "auto" in argument:
            if not isinstance(argument["auto"], bool):
                raise BridgeError(f"{context}.arguments[{index}].auto must be boolean")
            normalized_argument["auto"] = argument["auto"]
        arguments.append(normalized_argument)
    _assert_unique(
        arguments,
        lambda item: item.get("ordinal", item["name"]),
        f"{context}.arguments",
    )
    normalized: dict[str, Any] = {
        "calling_convention": _normalize_identifier(
            mapping["calling_convention"], f"{context}.calling_convention"
        ),
        "return_type": _normalize_identifier(mapping["return_type"], f"{context}.return_type"),
        "arguments": arguments,
    }
    for name in ("no_return", "varargs"):
        if name in mapping:
            if not isinstance(mapping[name], bool):
                raise BridgeError(f"{context}.{name} must be boolean")
            normalized[name] = mapping[name]
    return normalized


def _normalize_instruction(value: Any, context: str) -> dict[str, str]:
    mapping = _require_mapping(value, context)
    _require_exact_keys(
        mapping,
        required=("rva", "bytes", "mnemonic", "text"),
        context=context,
    )
    bytes_text = _normalize_identifier(mapping["bytes"], f"{context}.bytes").upper()
    if not re.fullmatch(r"[0-9A-F]+", bytes_text) or len(bytes_text) % 2:
        raise BridgeError(f"{context}.bytes must contain whole hexadecimal bytes")
    return {
        "rva": normalize_rva(mapping["rva"], f"{context}.rva"),
        "bytes": bytes_text,
        "mnemonic": _normalize_identifier(mapping["mnemonic"], f"{context}.mnemonic"),
        "text": _safe_text(mapping["text"], f"{context}.text"),
    }


def _normalize_rva_range(value: Any, context: str) -> dict[str, str]:
    mapping = _require_mapping(value, context)
    _require_exact_keys(mapping, required=("start_rva", "end_rva_exclusive"), context=context)
    start = normalize_rva(mapping["start_rva"], f"{context}.start_rva")
    end = normalize_rva(mapping["end_rva_exclusive"], f"{context}.end_rva_exclusive")
    if int(end[2:], 16) < int(start[2:], 16):
        raise BridgeError(f"{context} is inverted")
    return {"start_rva": start, "end_rva_exclusive": end}


def _normalize_cfg_block(value: Any, context: str) -> dict[str, str]:
    return _normalize_rva_range(value, context)


def _normalize_cfg_edge(value: Any, context: str) -> dict[str, str]:
    mapping = _require_mapping(value, context)
    _require_exact_keys(mapping, required=("source_rva", "target_rva", "flow"), context=context)
    return {
        "source_rva": normalize_rva(mapping["source_rva"], f"{context}.source_rva"),
        "target_rva": normalize_rva(mapping["target_rva"], f"{context}.target_rva"),
        "flow": _normalize_identifier(mapping["flow"], f"{context}.flow"),
    }


def _normalize_this_offset(value: Any, context: str) -> dict[str, Any]:
    mapping = _require_mapping(value, context)
    _require_exact_keys(mapping, required=("offset", "basis", "sites"), context=context)
    sites = sorted(
        {
            normalize_rva(raw, f"{context}.sites")
            for raw in _require_list(mapping["sites"], f"{context}.sites")
        }
    )
    return {
        "offset": normalize_rva(mapping["offset"], f"{context}.offset"),
        "basis": _normalize_identifier(mapping["basis"], f"{context}.basis"),
        "sites": sites,
    }


def _normalize_switch_table(value: Any, context: str) -> dict[str, Any]:
    mapping = _require_mapping(value, context)
    _require_exact_keys(mapping, required=("site_rva", "cases"), context=context)
    cases: list[dict[str, Any]] = []
    for index, raw in enumerate(_require_list(mapping["cases"], f"{context}.cases")):
        case = _require_mapping(raw, f"{context}.cases[{index}]")
        _require_exact_keys(
            case,
            required=("target_rva",),
            optional=("value",),
            context=f"{context}.cases[{index}]",
        )
        normalized_case: dict[str, Any] = {
            "target_rva": normalize_rva(case["target_rva"], f"{context}.cases[{index}].target_rva")
        }
        if "value" in case:
            value_number = case["value"]
            if isinstance(value_number, bool) or not isinstance(value_number, int):
                raise BridgeError(f"{context}.cases[{index}].value must be an integer")
            normalized_case["value"] = value_number
        cases.append(normalized_case)
    return {
        "site_rva": normalize_rva(mapping["site_rva"], f"{context}.site_rva"),
        "cases": _sorted_unique(cases, f"{context}.cases"),
    }


def _normalize_indirect_call(value: Any, context: str) -> dict[str, Any]:
    mapping = _require_mapping(value, context)
    _require_exact_keys(
        mapping,
        required=("site_rva", "text", "candidate_displacements"),
        context=context,
    )
    return {
        "site_rva": normalize_rva(mapping["site_rva"], f"{context}.site_rva"),
        "text": _safe_text(mapping["text"], f"{context}.text"),
        "candidate_displacements": sorted(
            {
                normalize_rva(raw, f"{context}.candidate_displacements")
                for raw in _require_list(
                    mapping["candidate_displacements"], f"{context}.candidate_displacements"
                )
            }
        ),
    }


def _normalize_pcode(value: Any, context: str) -> list[dict[str, Any]]:
    normalized: list[dict[str, Any]] = []
    last_key: tuple[int, int] | None = None
    for index, raw in enumerate(_require_list(value, context)):
        mapping = _require_mapping(raw, f"{context}[{index}]")
        _require_exact_keys(
            mapping,
            required=("site_rva", "ordinal", "opcode", "inputs"),
            optional=("output",),
            context=f"{context}[{index}]",
        )
        ordinal = mapping["ordinal"]
        if isinstance(ordinal, bool) or not isinstance(ordinal, int) or ordinal < 0:
            raise BridgeError(f"{context}[{index}].ordinal must be non-negative")
        site_rva = normalize_rva(mapping["site_rva"], f"{context}[{index}].site_rva")
        location = (int(site_rva[2:], 16), ordinal)
        if last_key is not None and location < last_key:
            raise BridgeError(f"{context} must be ordered by site_rva and ordinal")
        last_key = location
        inputs = [
            _normalize_identifier(item, f"{context}[{index}].inputs")
            for item in _require_list(mapping["inputs"], f"{context}[{index}].inputs")
        ]
        item: dict[str, Any] = {
            "site_rva": site_rva,
            "ordinal": ordinal,
            "opcode": _normalize_identifier(mapping["opcode"], f"{context}[{index}].opcode"),
            "inputs": inputs,
        }
        if "output" in mapping:
            item["output"] = _normalize_identifier(mapping["output"], f"{context}[{index}].output")
        normalized.append(item)
    return normalized


def _normalize_x87_finding(value: Any, context: str) -> dict[str, Any]:
    mapping = _require_mapping(value, context)
    _require_exact_keys(
        mapping,
        required=("site_rva", "mnemonic", "kind"),
        optional=(
            "memory_width",
            "pops_stack",
            "retains_x87_value",
            "precision_control_sensitive",
            "rounding_control_sensitive",
        ),
        context=context,
    )
    result: dict[str, Any] = {
        "site_rva": normalize_rva(mapping["site_rva"], f"{context}.site_rva"),
        "mnemonic": _normalize_identifier(mapping["mnemonic"], f"{context}.mnemonic"),
        "kind": _normalize_identifier(mapping["kind"], f"{context}.kind"),
    }
    for name in (
        "pops_stack",
        "retains_x87_value",
        "precision_control_sensitive",
        "rounding_control_sensitive",
    ):
        if name in mapping:
            if not isinstance(mapping[name], bool):
                raise BridgeError(f"{context}.{name} must be boolean")
            result[name] = mapping[name]
    if "memory_width" in mapping:
        width = mapping["memory_width"]
        if isinstance(width, bool) or not isinstance(width, int) or width < 1 or width > 16:
            raise BridgeError(f"{context}.memory_width must be from 1 to 16")
        result["memory_width"] = width
    return result


def _normalize_binary_metadata(value: Any, context: str) -> dict[str, str]:
    mapping = _require_mapping(value, context)
    _require_exact_keys(
        mapping,
        required=("language", "compiler", "image_base", "format"),
        context=context,
    )
    return {
        name: _normalize_identifier(mapping[name], f"{context}.{name}")
        for name in ("language", "compiler", "image_base", "format")
    }


def _normalize_tool_metadata(value: Any, context: str) -> dict[str, Any]:
    mapping = _require_mapping(value, context)
    _require_exact_keys(mapping, required=("ghidra",), context=context)
    ghidra = _require_mapping(mapping["ghidra"], f"{context}.ghidra")
    _require_exact_keys(ghidra, required=("version",), context=f"{context}.ghidra")
    return {"ghidra": {"version": _normalize_identifier(ghidra["version"], f"{context}.ghidra.version")}}


def normalize_function_report(document: Any) -> dict[str, Any]:
    """Validate and normalize one deterministic Ghidra function report."""

    mapping = _require_mapping(document, "function report")
    _require_exact_keys(
        mapping,
        required=("schema", "source", "functions"),
        optional=("binary", "tools"),
        context="function report",
    )
    if mapping["schema"] != FUNCTION_SCHEMA:
        raise BridgeError(f"function report.schema must be {FUNCTION_SCHEMA!r}")
    source = _require_mapping(mapping["source"], "function report.source")
    _require_exact_keys(source, required=("sha256",), context="function report.source")
    raw_functions = _require_list(mapping["functions"], "function report.functions")
    if len(raw_functions) != 1:
        raise BridgeError("function report.functions must contain exactly one function")
    function = _require_mapping(raw_functions[0], "function report.functions[0]")
    _require_exact_keys(
        function,
        required=(
            "id",
            "name",
            "rva",
            "boundary",
            "signature",
            "cfg",
            "instructions",
            "pseudocode",
            "callers",
            "callees",
            "xrefs",
            "globals",
            "this_offsets",
            "constants",
            "switch_tables",
            "indirect_calls",
            "pcode",
            "x87",
        ),
        context="function report.functions[0]",
    )
    boundary = _require_mapping(function["boundary"], "function report.functions[0].boundary")
    _require_exact_keys(
        boundary,
        required=("start_rva", "end_rva_exclusive"),
        optional=("size", "ranges"),
        context="function report.functions[0].boundary",
    )
    start_rva = normalize_rva(boundary["start_rva"], "function report.functions[0].boundary.start_rva")
    end_rva = normalize_rva(
        boundary["end_rva_exclusive"], "function report.functions[0].boundary.end_rva_exclusive"
    )
    if int(end_rva[2:], 16) < int(start_rva[2:], 16):
        raise BridgeError("function report boundary is inverted")
    cfg = _require_mapping(function["cfg"], "function report.functions[0].cfg")
    _require_exact_keys(
        cfg,
        required=("block_count",),
        optional=("blocks", "edges"),
        context="function report.functions[0].cfg",
    )
    block_count = cfg["block_count"]
    if isinstance(block_count, bool) or not isinstance(block_count, int) or block_count < 0:
        raise BridgeError("function report cfg.block_count must be a non-negative integer")
    pseudocode = _require_mapping(function["pseudocode"], "function report.functions[0].pseudocode")
    _require_exact_keys(
        pseudocode,
        required=("text",),
        optional=("engine",),
        context="function report.functions[0].pseudocode",
    )
    globals_value = _require_mapping(function["globals"], "function report.functions[0].globals")
    _require_exact_keys(
        globals_value,
        required=("reads", "writes"),
        context="function report.functions[0].globals",
    )

    def references(name: str, *, require_target: bool = True) -> list[dict[str, Any]]:
        normalized = [
            _normalize_reference(
                raw,
                f"function report.functions[0].{name}[{index}]",
                require_target=require_target,
            )
            for index, raw in enumerate(_require_list(function[name], f"function report.functions[0].{name}"))
        ]
        return _sorted_unique(normalized, f"function report.functions[0].{name}")

    normalized_function: dict[str, Any] = {
        "rva": normalize_rva(function["rva"], "function report.functions[0].rva"),
        "boundary": {"start_rva": start_rva, "end_rva_exclusive": end_rva},
        "cfg": {"block_count": block_count},
        "instructions": [
            _normalize_instruction(raw, f"function report.functions[0].instructions[{index}]")
            for index, raw in enumerate(
                _require_list(function["instructions"], "function report.functions[0].instructions")
            )
        ],
        "pseudocode": {
            "text": _safe_text(
                pseudocode["text"], "function report.functions[0].pseudocode.text"
            )
        },
        "callers": references("callers", require_target=False),
        "callees": references("callees", require_target=False),
        "xrefs": references("xrefs", require_target=False),
        "globals": {
            "reads": _sorted_unique(
                [
                    _normalize_reference(
                        raw,
                        f"function report.functions[0].globals.reads[{index}]",
                    )
                    for index, raw in enumerate(
                        _require_list(
                            globals_value["reads"], "function report.functions[0].globals.reads"
                        )
                    )
                ],
                "function report.functions[0].globals.reads",
            ),
            "writes": _sorted_unique(
                [
                    _normalize_reference(
                        raw,
                        f"function report.functions[0].globals.writes[{index}]",
                    )
                    for index, raw in enumerate(
                        _require_list(
                            globals_value["writes"], "function report.functions[0].globals.writes"
                        )
                    )
                ],
                "function report.functions[0].globals.writes",
            ),
        },
        "this_offsets": _sorted_unique(
            [
                _normalize_this_offset(raw, f"function report.functions[0].this_offsets[{index}]")
                for index, raw in enumerate(
                    _require_list(function["this_offsets"], "function report.functions[0].this_offsets")
                )
            ],
            "function report.functions[0].this_offsets",
        ),
        "constants": _normalize_constants(function["constants"], "function report.functions[0].constants"),
        "switch_tables": _sorted_unique(
            [
                _normalize_switch_table(raw, f"function report.functions[0].switch_tables[{index}]")
                for index, raw in enumerate(
                    _require_list(function["switch_tables"], "function report.functions[0].switch_tables")
                )
            ],
            "function report.functions[0].switch_tables",
        ),
        "indirect_calls": _sorted_unique(
            [
                _normalize_indirect_call(raw, f"function report.functions[0].indirect_calls[{index}]")
                for index, raw in enumerate(
                    _require_list(function["indirect_calls"], "function report.functions[0].indirect_calls")
                )
            ],
            "function report.functions[0].indirect_calls",
        ),
        "pcode": _normalize_pcode(function["pcode"], "function report.functions[0].pcode"),
        "x87": _sorted_unique(
            [
                _normalize_x87_finding(raw, f"function report.functions[0].x87[{index}]")
                for index, raw in enumerate(
                    _require_list(function["x87"], "function report.functions[0].x87")
                )
            ],
            "function report.functions[0].x87",
        ),
    }
    if normalized_function["rva"] != start_rva:
        raise BridgeError("function report rva must equal boundary.start_rva")
    if "size" in boundary:
        size = boundary["size"]
        if isinstance(size, bool) or not isinstance(size, int) or size < 0:
            raise BridgeError("function report boundary.size must be a non-negative integer")
        normalized_function["boundary"]["size"] = size
    if "ranges" in boundary:
        normalized_function["boundary"]["ranges"] = _sorted_unique(
            [
                _normalize_rva_range(raw, f"function report.functions[0].boundary.ranges[{index}]")
                for index, raw in enumerate(
                    _require_list(boundary["ranges"], "function report.functions[0].boundary.ranges")
                )
            ],
            "function report.functions[0].boundary.ranges",
        )
    if "blocks" in cfg:
        normalized_function["cfg"]["blocks"] = _sorted_unique(
            [
                _normalize_cfg_block(raw, f"function report.functions[0].cfg.blocks[{index}]")
                for index, raw in enumerate(
                    _require_list(cfg["blocks"], "function report.functions[0].cfg.blocks")
                )
            ],
            "function report.functions[0].cfg.blocks",
        )
    if "edges" in cfg:
        normalized_function["cfg"]["edges"] = _sorted_unique(
            [
                _normalize_cfg_edge(raw, f"function report.functions[0].cfg.edges[{index}]")
                for index, raw in enumerate(
                    _require_list(cfg["edges"], "function report.functions[0].cfg.edges")
                )
            ],
            "function report.functions[0].cfg.edges",
        )
    if "engine" in pseudocode:
        normalized_function["pseudocode"]["engine"] = _normalize_identifier(
            pseudocode["engine"], "function report.functions[0].pseudocode.engine"
        )
    normalized_function["name"] = _normalize_identifier(
        function["name"], "function report.functions[0].name"
    )
    normalized_function["signature"] = _normalize_function_signature(
        function["signature"], "function report.functions[0].signature"
    )
    function_id = _normalize_identifier(
        function["id"], "function report.functions[0].id"
    )
    match = _FUNCTION_ID_RE.fullmatch(function_id)
    if match is None:
        raise BridgeError("function report ID must use canonical FN-MODULE-RVA form")
    if match.group(1) != normalized_function["rva"][2:]:
        raise BridgeError("function report ID RVA suffix must match function rva")
    normalized_function = {"id": function_id, **normalized_function}
    normalized_report: dict[str, Any] = {
        "schema": FUNCTION_SCHEMA,
        "source": {"sha256": normalize_sha256(source["sha256"], "function report.source.sha256")},
        "functions": [normalized_function],
    }
    if "binary" in mapping:
        normalized_report["binary"] = _normalize_binary_metadata(mapping["binary"], "function report.binary")
    if "tools" in mapping:
        normalized_report["tools"] = _normalize_tool_metadata(mapping["tools"], "function report.tools")
    return normalized_report


def _rizin_address_to_rva(value: Any, image_base: int, context: str) -> str:
    address = _parse_uint(value, context, maximum=0xFFFFFFFFFFFFFFFF)
    if address < image_base or address - image_base > 0xFFFFFFFF:
        raise BridgeError(f"{context} is not an RVA inside the PE32 image")
    return normalize_rva(address - image_base, context)


def _normalize_rizin_reference(
    value: Any,
    image_base: int,
    context: str,
    *,
    preserve_access: bool = False,
) -> dict[str, str]:
    mapping = _require_mapping(value, context)
    if "target" not in mapping:
        raise BridgeError(f"{context} is missing target")
    result: dict[str, str] = {}
    if mapping["target"] is None:
        result["target_space"] = "UNRESOLVED"
    else:
        result["target_rva"] = _rizin_address_to_rva(
            mapping["target"], image_base, f"{context}.target"
        )
    if mapping.get("site") is not None:
        result["site_rva"] = _rizin_address_to_rva(mapping["site"], image_base, f"{context}.site")
    if preserve_access:
        raw_kind = mapping.get("kind", mapping.get("type", "unknown"))
        kind = raw_kind.lower() if isinstance(raw_kind, str) else "unknown"
        result["access"] = kind if kind in {"read", "write"} else "unknown"
    return result


def _normalize_rizin_constants(
    function: Mapping[str, Any],
) -> list[dict[str, Any]] | None:
    # Existing Rizin v1 reports do not yet export constants.  A later Rizin
    # exporter may add this public extension; validate it strictly when present.
    if "constants" not in function:
        return None
    return _normalize_constants(function["constants"], "Rizin function.constants")


def _normalize_rizin_indirect_calls(function: Mapping[str, Any]) -> list[Any] | None:
    # Likewise, keep compatibility with today's Rizin v1 while allowing a
    # future exporter to provide explicit indirect-call observations.
    if "indirect_calls" not in function:
        return None
    return _normalize_flexible_entries(function["indirect_calls"], "Rizin function.indirect_calls")


def _normalize_rizin_for_compare(document: Any) -> dict[str, Any]:
    mapping = _require_mapping(document, "Rizin report")
    _assert_no_unsafe_text(mapping, "Rizin report")
    if mapping.get("schema") != RIZIN_FUNCTION_SCHEMA:
        raise BridgeError(f"Rizin report.schema must be {RIZIN_FUNCTION_SCHEMA!r}")
    source = _require_mapping(mapping.get("source"), "Rizin report.source")
    sha256 = normalize_sha256(source.get("sha256"), "Rizin report.source.sha256")
    binary = _require_mapping(mapping.get("binary"), "Rizin report.binary")
    image_base = _parse_uint(
        binary.get("image_base"), "Rizin report.binary.image_base", maximum=0xFFFFFFFF
    )
    functions = _require_list(mapping.get("functions"), "Rizin report.functions")
    if len(functions) != 1:
        raise BridgeError("Rizin report.functions must contain exactly one function")
    function = _require_mapping(functions[0], "Rizin report.functions[0]")
    rva = normalize_rva(function.get("rva"), "Rizin report.functions[0].rva")
    boundary = _require_mapping(function.get("boundary"), "Rizin report.functions[0].boundary")
    start = _rizin_address_to_rva(
        boundary.get("start"), image_base, "Rizin report.functions[0].boundary.start"
    )
    end_raw = boundary.get("end_exclusive")
    if end_raw is None:
        raise BridgeError("Rizin report boundary end_exclusive is required for comparison")
    end = _rizin_address_to_rva(
        end_raw, image_base, "Rizin report.functions[0].boundary.end_exclusive"
    )
    if rva != start:
        raise BridgeError("Rizin report function rva must equal the boundary start")
    cfg = _require_mapping(function.get("cfg"), "Rizin report.functions[0].cfg")
    blocks = _require_list(cfg.get("blocks"), "Rizin report.functions[0].cfg.blocks")
    calls = [
        _normalize_rizin_reference(raw, image_base, f"Rizin report.functions[0].calls[{index}]")
        for index, raw in enumerate(_require_list(function.get("calls"), "Rizin report.functions[0].calls"))
    ]
    globals_observed = [
        _normalize_rizin_reference(
            raw,
            image_base,
            f"Rizin report.functions[0].data_refs[{index}]",
            preserve_access=True,
        )
        for index, raw in enumerate(
            _require_list(function.get("data_refs"), "Rizin report.functions[0].data_refs")
        )
    ]
    return {
        "source_sha256": sha256,
        "rva": rva,
        "boundary": {"start_rva": start, "end_rva_exclusive": end},
        "block_count": len(blocks),
        "callees": _sorted_unique(calls, "Rizin report.functions[0].calls"),
        "globals": _sorted_unique(globals_observed, "Rizin report.functions[0].data_refs"),
        "constants": _normalize_rizin_constants(function),
        "indirect_calls": _normalize_rizin_indirect_calls(function),
    }


def _reference_projection(
    items: Sequence[Mapping[str, Any]], *, access: str | None = None
) -> list[dict[str, str]]:
    """Project references to data portable across the two exporters."""

    projected: list[dict[str, str]] = []
    for item in items:
        value: dict[str, str] = {}
        if "target_rva" in item:
            value["target_rva"] = item["target_rva"]
        else:
            if "target_space" in item:
                value["target_space"] = item["target_space"]
            else:
                value["target_space"] = "UNRESOLVED"
            if "name" in item:
                value["name"] = item["name"]
        if "site_rva" in item:
            value["site_rva"] = item["site_rva"]
        if access is not None:
            value["access"] = access
        if not value:
            continue
        projected.append(value)
    return _sorted_unique(projected, "comparison reference")


def _difference(category: str, ghidra: Any, rizin: Any) -> dict[str, Any] | None:
    if ghidra == rizin:
        return None
    return {"category": category, "ghidra": ghidra, "rizin": rizin}


def compare_ghidra_to_rizin(ghidra_document: Any, rizin_document: Any) -> dict[str, Any]:
    """Return only cross-tool evidence differences for one matching function."""

    ghidra = normalize_function_report(ghidra_document)
    rizin = _normalize_rizin_for_compare(rizin_document)
    source_sha256 = ghidra["source"]["sha256"]
    function = ghidra["functions"][0]
    if source_sha256 != rizin["source_sha256"]:
        raise BridgeError("Ghidra and Rizin reports have different source SHA-256 values")
    if function["rva"] != rizin["rva"]:
        raise BridgeError("Ghidra and Rizin reports describe different function RVAs")
    ghidra_globals = _sorted_unique(
        [
            *_reference_projection(function["globals"]["reads"], access="read"),
            *_reference_projection(function["globals"]["writes"], access="write"),
        ],
        "comparison globals",
    )
    ghidra_callees = _reference_projection(function["callees"])
    differences = [
        _difference(
            "boundary",
            {
                "start_rva": function["boundary"]["start_rva"],
                "end_rva_exclusive": function["boundary"]["end_rva_exclusive"],
            },
            rizin["boundary"],
        ),
        _difference("block_count", function["cfg"]["block_count"], rizin["block_count"]),
        _difference("callees", ghidra_callees, rizin["callees"]),
        _difference("globals", ghidra_globals, rizin["globals"]),
    ]
    unavailable: list[dict[str, str]] = []
    for category in ("constants", "indirect_calls"):
        if rizin[category] is None:
            unavailable.append(
                {
                    "category": category,
                    "source": "rizin",
                    "reason": "field-not-exported",
                }
            )
        else:
            differences.append(
                _difference(category, function[category], rizin[category])
            )
    return {
        "schema": DIFF_SCHEMA,
        "source": {"sha256": source_sha256},
        "function": {"rva": function["rva"]},
        "differences": [item for item in differences if item is not None],
        "unavailable": unavailable,
    }


def load_json_file(path: Path) -> Any:
    """Load a bounded UTF-8 JSON input without copying its host path into data."""

    try:
        size = path.stat().st_size
    except OSError as exc:
        raise BridgeError("input file cannot be read") from exc
    if size < 0 or size > MAX_INPUT_BYTES:
        raise BridgeError("input file exceeds the size limit")
    try:
        raw = path.read_bytes()
    except OSError as exc:
        raise BridgeError("input file cannot be read") from exc
    if len(raw) > MAX_INPUT_BYTES:
        raise BridgeError("input file exceeds the size limit")

    def reject_constant(value: str) -> None:
        raise BridgeError(f"input JSON contains non-finite number {value}")

    def unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in pairs:
            if key in result:
                raise BridgeError(f"input JSON contains duplicate object key {key!r}")
            result[key] = value
        return result

    try:
        return json.loads(
            raw.decode("utf-8"),
            object_pairs_hook=unique_object,
            parse_constant=reject_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError, RecursionError) as exc:
        raise BridgeError("input file is not valid UTF-8 JSON") from exc


def atomic_write_json(path: Path, document: Any) -> None:
    """Atomically replace output only after serialization has completed."""

    try:
        encoded = (
            json.dumps(
                document,
                ensure_ascii=True,
                sort_keys=True,
                separators=(",", ":"),
                allow_nan=False,
            )
            + "\n"
        ).encode("utf-8")
    except (TypeError, ValueError) as exc:
        raise BridgeError("output cannot be serialized as JSON") from exc
    if len(encoded) > MAX_INPUT_BYTES * 4:
        raise BridgeError("output exceeds the size limit")
    output_directory = path.parent
    if not output_directory.is_dir():
        raise BridgeError("output directory does not exist")
    temporary_name: str | None = None
    try:
        descriptor, temporary_name = tempfile.mkstemp(
            prefix=f".{path.name}.", suffix=".tmp", dir=output_directory
        )
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(encoded)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary_name, path)
        temporary_name = None
    except OSError as exc:
        raise BridgeError("output file could not be atomically replaced") from exc
    finally:
        if temporary_name is not None:
            try:
                os.unlink(temporary_name)
            except OSError:
                pass


def _command_normalize_knowledge(args: argparse.Namespace) -> None:
    atomic_write_json(Path(args.output), normalize_knowledge(load_json_file(Path(args.input))))


def _command_normalize_function(args: argparse.Namespace) -> None:
    atomic_write_json(
        Path(args.output), normalize_function_report(load_json_file(Path(args.input)))
    )


def _command_compare(args: argparse.Namespace) -> None:
    atomic_write_json(
        Path(args.output),
        compare_ghidra_to_rizin(
            load_json_file(Path(args.ghidra)), load_json_file(Path(args.rizin))
        ),
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    for command, handler, help_text in (
        ("normalize-knowledge", _command_normalize_knowledge, "validate and normalize curated knowledge"),
        ("normalize-function", _command_normalize_function, "validate and normalize a Ghidra function report"),
    ):
        child = subparsers.add_parser(command, help=help_text)
        child.add_argument("--input", required=True)
        child.add_argument("--output", required=True)
        child.set_defaults(handler=handler)
    compare = subparsers.add_parser("compare", help="emit only Ghidra/Rizin differences")
    compare.add_argument("--ghidra", required=True)
    compare.add_argument("--rizin", required=True)
    compare.add_argument("--output", required=True)
    compare.set_defaults(handler=_command_compare)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        args.handler(args)
    except (BridgeError, RecursionError) as exc:
        print(f"airfix-ghidra-bridge: {exc}", file=os.sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
