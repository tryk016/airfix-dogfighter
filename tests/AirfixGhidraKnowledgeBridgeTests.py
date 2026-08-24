"""Synthetic coverage for the portable Airfix Ghidra Knowledge Bridge core."""

from __future__ import annotations

import copy
import importlib.util
from pathlib import Path
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
MODULE_PATH = REPOSITORY_ROOT / "tools" / "re" / "airfix_ghidra_bridge.py"
SPEC = importlib.util.spec_from_file_location("airfix_ghidra_bridge", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load Airfix Ghidra Knowledge Bridge")
BRIDGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BRIDGE)

SHA256 = "a" * 64


def assessment(*, eligible: bool = False, status: str = "confirmed") -> dict[str, object]:
    return {
        "status": status,
        "confidence": 3,
        "evidence": [{"id": "EV-20260824-001", "summary": "Synthetic static evidence."}],
        "apply_eligible": eligible,
    }


def notes() -> dict[str, list[str]]:
    return {"comments": ["Synthetic comment."], "hypotheses": []}


def field(offset: str = "0x448") -> dict[str, object]:
    return {
        "offset": offset,
        "name": "pitch",
        "type": "float",
        "assessment": assessment(eligible=True),
        "notes": notes(),
    }


def knowledge_document() -> dict[str, object]:
    return {
        "schema": BRIDGE.KNOWLEDGE_SCHEMA,
        "modules": [
            {
                "sha256": SHA256.upper(),
                "functions": [
                    {
                        "rva": "0x3f40",
                        "name": "AfVehicle::ProcessEvent",
                        "signature": {
                            "calling_convention": "thiscall",
                            "return_type": "void",
                            "arguments": [
                                {"name": "event", "type": "AfEvent*", "storage": "stack+4"}
                            ],
                        },
                        "this_fields": [field()],
                        "assessment": assessment(eligible=True),
                        "notes": notes(),
                    }
                ],
                "structures": [
                    {
                        "name": "AfVehicle",
                        "size": "0x500",
                        "fields": [field()],
                        "assessment": assessment(),
                        "notes": notes(),
                    }
                ],
                "vtables": [
                    {
                        "rva": "0x2200",
                        "class_name": "AfVehicle",
                        "slots": [
                            {
                                "index": 3,
                                "target_rva": "0x3f40",
                                "name": "ProcessEvent",
                                "assessment": assessment(),
                                "notes": notes(),
                            }
                        ],
                        "assessment": assessment(),
                        "notes": notes(),
                    }
                ],
                "event_ids": [
                    {
                        "id": "0x5f",
                        "name": "PITCH_SET",
                        "assessment": assessment(),
                        "notes": notes(),
                    }
                ],
            }
        ],
    }


def ghidra_report() -> dict[str, object]:
    return {
        "schema": BRIDGE.FUNCTION_SCHEMA,
        "source": {"sha256": SHA256.upper()},
        "functions": [
            {
                "id": "FN-AIRCRAFT-00003F40",
                "name": "Synthetic::ProcessEvent",
                "rva": "0x3f40",
                "boundary": {"start_rva": "0x3f40", "end_rva_exclusive": "0x3fb8"},
                "signature": {
                    "calling_convention": "__thiscall",
                    "return_type": "void",
                    "arguments": [],
                },
                "cfg": {"block_count": 2},
                "instructions": [
                    {
                        "rva": "0x3f40",
                        "bytes": "55",
                        "mnemonic": "PUSH",
                        "text": "PUSH EBP",
                    }
                ],
                "pseudocode": {"text": "void ProcessEvent(void) {}"},
                "callers": [],
                "callees": [{"site_rva": "0x3f50", "target_rva": "0x6000"}],
                "xrefs": [],
                "globals": {"reads": [{"target_rva": "0xd610"}], "writes": []},
                "this_offsets": [
                    {
                        "offset": "0x448",
                        "basis": "direct-this-register",
                        "sites": ["0x3f40"],
                    }
                ],
                "constants": [],
                "switch_tables": [],
                "indirect_calls": [],
                "pcode": [
                    {
                        "site_rva": "0x3f40",
                        "ordinal": 0,
                        "opcode": "COPY",
                        "output": "reg:eax:4",
                        "inputs": ["unique:u0:4"],
                    }
                ],
                "x87": [],
            }
        ],
    }


def rizin_report() -> dict[str, object]:
    return {
        "schema": BRIDGE.RIZIN_FUNCTION_SCHEMA,
        "source": {"sha256": SHA256},
        "binary": {"image_base": "0x10000000"},
        "functions": [
            {
                "rva": "0x00003F40",
                "boundary": {"start": "0x10003F40", "end_exclusive": "0x10003FB8"},
                "cfg": {"blocks": [{}, {}]},
                "calls": [{"site": "0x10003F50", "target": "0x10006000"}],
                "data_refs": [{"target": "0x1000D610", "kind": "read"}],
                "constants": [],
                "indirect_calls": [],
            }
        ],
    }


class KnowledgeNormalizationTests(unittest.TestCase):
    def test_normalization_is_deterministic_and_permutation_safe(self) -> None:
        first = knowledge_document()
        second = copy.deepcopy(first)
        module = second["modules"][0]
        module["event_ids"] = [
            *module["event_ids"],
            {
                "id": "0x60",
                "name": "APPLY",
                "assessment": assessment(status="hypothesis"),
                "notes": {"comments": ["Z", "A", "A"], "hypotheses": ["H2", "H1"]},
            },
        ]
        module["event_ids"].reverse()
        first["modules"][0]["event_ids"] = list(reversed(module["event_ids"]))

        normalized_first = BRIDGE.normalize_knowledge(first)
        normalized_second = BRIDGE.normalize_knowledge(second)

        self.assertEqual(normalized_first, normalized_second)
        self.assertEqual(normalized_first["modules"][0]["sha256"], SHA256)
        self.assertEqual(normalized_first["modules"][0]["functions"][0]["rva"], "0x00003F40")
        self.assertEqual(
            [item["id"] for item in normalized_first["modules"][0]["event_ids"]],
            ["0x0000005F", "0x00000060"],
        )

    def test_requires_explicit_eligibility_rule(self) -> None:
        document = knowledge_document()
        document["modules"][0]["functions"][0]["assessment"] = {
            "status": "hypothesis",
            "confidence": 2,
            "evidence": [{"id": "EV-20260824-002", "summary": "Insufficient."}],
            "apply_eligible": True,
        }
        with self.assertRaisesRegex(BRIDGE.BridgeError, "only legal"):
            BRIDGE.normalize_knowledge(document)

    def test_rejects_apply_eligible_low_confidence_record(self) -> None:
        document = knowledge_document()
        document["modules"][0]["functions"][0]["assessment"]["confidence"] = 1
        with self.assertRaisesRegex(BRIDGE.BridgeError, "confidence>=2"):
            BRIDGE.normalize_knowledge(document)

    def test_rejects_noncanonical_evidence_id(self) -> None:
        document = knowledge_document()
        document["modules"][0]["functions"][0]["assessment"]["evidence"][0][
            "id"
        ] = "EV-TEST-001"
        with self.assertRaisesRegex(BRIDGE.BridgeError, "EV-YYYYMMDD-NNN"):
            BRIDGE.normalize_knowledge(document)

    def test_enforces_schema_text_and_vtable_index_limits(self) -> None:
        document = knowledge_document()
        document["modules"][0]["functions"][0]["name"] = "N" * 161
        with self.assertRaisesRegex(BRIDGE.BridgeError, "160-character"):
            BRIDGE.normalize_knowledge(document)

        document = knowledge_document()
        document["modules"][0]["functions"][0]["assessment"]["evidence"][0][
            "summary"
        ] = "S" * 1001
        with self.assertRaisesRegex(BRIDGE.BridgeError, "1000-character"):
            BRIDGE.normalize_knowledge(document)

        document = knowledge_document()
        document["modules"][0]["functions"][0]["notes"]["comments"] = [
            "C" * 4001
        ]
        with self.assertRaisesRegex(BRIDGE.BridgeError, "4000-character"):
            BRIDGE.normalize_knowledge(document)

        document = knowledge_document()
        document["modules"][0]["vtables"][0]["slots"][0]["index"] = 4294967297
        with self.assertRaisesRegex(BRIDGE.BridgeError, "0 to 4095"):
            BRIDGE.normalize_knowledge(document)

        document = knowledge_document()
        document["modules"][0]["structures"][0]["size"] = "0x01000000"
        with self.assertRaisesRegex(BRIDGE.BridgeError, "supported range"):
            BRIDGE.normalize_knowledge(document)

        document = knowledge_document()
        document["modules"][0]["structures"][0]["size"] = "0x00000000"
        with self.assertRaisesRegex(BRIDGE.BridgeError, "greater than zero"):
            BRIDGE.normalize_knowledge(document)

    def test_rejects_unknown_and_injected_identifier_fields(self) -> None:
        document = knowledge_document()
        document["unexpected"] = True
        with self.assertRaisesRegex(BRIDGE.BridgeError, "unknown fields"):
            BRIDGE.normalize_knowledge(document)

        document = knowledge_document()
        document["modules"][0]["functions"][0]["name"] = "name\nother"
        with self.assertRaisesRegex(BRIDGE.BridgeError, "injection"):
            BRIDGE.normalize_knowledge(document)

    def test_rejects_private_path_in_free_text(self) -> None:
        document = knowledge_document()
        document["modules"][0]["functions"][0]["notes"]["comments"] = [
            r"C:\private\Dogfighter.exe"
        ]
        with self.assertRaisesRegex(BRIDGE.BridgeError, "local path"):
            BRIDGE.normalize_knowledge(document)

        document = knowledge_document()
        document["modules"][0]["functions"][0]["notes"]["comments"] = [
            "note: /srv/secret/project/file.c"
        ]
        with self.assertRaisesRegex(BRIDGE.BridgeError, "local path"):
            BRIDGE.normalize_knowledge(document)

        document = knowledge_document()
        document["modules"][0]["functions"][0]["notes"]["comments"] = [
            "captured under /tmp/private/report.json"
        ]
        with self.assertRaisesRegex(BRIDGE.BridgeError, "local path"):
            BRIDGE.normalize_knowledge(document)


class FunctionReportTests(unittest.TestCase):
    def test_normalizes_function_and_rejects_schema_failure(self) -> None:
        normalized = BRIDGE.normalize_function_report(ghidra_report())
        function = normalized["functions"][0]
        self.assertEqual(normalized["source"]["sha256"], SHA256)
        self.assertEqual(function["rva"], "0x00003F40")
        self.assertEqual(function["constants"], [])

        malformed = ghidra_report()
        malformed["functions"][0]["boundary"]["end_rva_exclusive"] = "0x3f00"
        with self.assertRaisesRegex(BRIDGE.BridgeError, "inverted"):
            BRIDGE.normalize_function_report(malformed)

    def test_accepts_documented_ghidra_exporter_metadata(self) -> None:
        document = ghidra_report()
        document["binary"] = {
            "language": "x86:LE:32:default",
            "compiler": "windows",
            "image_base": "10000000",
            "format": "Portable Executable (PE)",
        }
        document["tools"] = {"ghidra": {"version": "12.1.2"}}
        function = document["functions"][0]
        function["name"] = "AfVehicle::ProcessEvent"
        function["signature"] = {
            "calling_convention": "__thiscall",
            "return_type": "void",
            "no_return": False,
            "varargs": False,
            "arguments": [
                {
                    "ordinal": 0,
                    "name": "event",
                    "type": "AfEvent *",
                    "storage": "Stack[0x4]:4",
                    "auto": False,
                }
            ],
        }
        function["boundary"].update(
            {"size": 120, "ranges": [{"start_rva": "0x3f40", "end_rva_exclusive": "0x3fb8"}]}
        )
        function["cfg"].update(
            {
                "blocks": [{"start_rva": "0x3f40", "end_rva_exclusive": "0x3fb8"}],
                "edges": [{"source_rva": "0x3f50", "target_rva": "0x3f60", "flow": "CONDITIONAL_JUMP"}],
            }
        )
        function["this_offsets"] = [
            {"offset": "0x448", "basis": "direct-this-register", "sites": ["0x3f60"]}
        ]
        function["constants"] = [
            {"kind": "integer", "bits": "0x01", "width": 1, "signed_value": 1},
            {"kind": "f32", "bits": "0x3F800000", "value": "1.0", "source_rva": "0x5000"},
        ]
        function["switch_tables"] = [
            {"site_rva": "0x3f70", "cases": [{"value": 1, "target_rva": "0x3f80"}]}
        ]
        function["indirect_calls"] = [
            {"site_rva": "0x3f90", "text": "CALL dword ptr [EAX + 0xB4]", "candidate_displacements": ["0xb4"]}
        ]
        function["x87"] = [
            {
                "site_rva": "0x3fa0",
                "mnemonic": "FST",
                "kind": "spill",
                "memory_width": 4,
                "pops_stack": False,
                "retains_x87_value": True,
                "precision_control_sensitive": True,
                "rounding_control_sensitive": False,
            }
        ]

        normalized = BRIDGE.normalize_function_report(document)
        normalized_function = normalized["functions"][0]
        self.assertEqual(normalized["binary"]["image_base"], "10000000")
        self.assertEqual(normalized_function["constants"][0]["bits"], "0x01")
        self.assertEqual(normalized_function["pcode"][0]["opcode"], "COPY")

    def test_rejects_pseudocode_host_path(self) -> None:
        document = ghidra_report()
        document["functions"][0]["pseudocode"]["text"] = "/home/user/private"
        with self.assertRaisesRegex(BRIDGE.BridgeError, "local path"):
            BRIDGE.normalize_function_report(document)

    def test_rejects_embedded_generic_posix_host_path(self) -> None:
        document = ghidra_report()
        document["functions"][0]["pseudocode"]["text"] = (
            "warning: generated from /nix/store/abc123/project/source.c"
        )
        with self.assertRaisesRegex(BRIDGE.BridgeError, "local path"):
            BRIDGE.normalize_function_report(document)

        document = ghidra_report()
        document["functions"][0]["pseudocode"]["text"] = (
            "warning: generated from /credentials"
        )
        with self.assertRaisesRegex(BRIDGE.BridgeError, "local path"):
            BRIDGE.normalize_function_report(document)

    def test_rejects_invalid_reference_direction(self) -> None:
        document = ghidra_report()
        document["functions"][0]["xrefs"] = [
            {"site_rva": "0x3f40", "direction": "sideways"}
        ]
        with self.assertRaisesRegex(BRIDGE.BridgeError, "direction"):
            BRIDGE.normalize_function_report(document)

    def test_rejects_function_id_rva_mismatch(self) -> None:
        document = ghidra_report()
        document["functions"][0]["id"] = "FN-AIRCRAFT-00003F41"
        with self.assertRaisesRegex(BRIDGE.BridgeError, "suffix"):
            BRIDGE.normalize_function_report(document)

    def test_decompiler_leading_comment_is_not_a_posix_path(self) -> None:
        document = ghidra_report()
        document["functions"][0]["pseudocode"]["text"] = (
            "/* WARNING: synthetic decompiler note */\nvoid f(void) {}"
        )
        normalized = BRIDGE.normalize_function_report(document)
        self.assertTrue(
            normalized["functions"][0]["pseudocode"]["text"].startswith("/*")
        )


class ComparisonTests(unittest.TestCase):
    def assert_difference(self, category: str, ghidra: dict[str, object]) -> None:
        result = BRIDGE.compare_ghidra_to_rizin(ghidra, rizin_report())
        self.assertEqual([entry["category"] for entry in result["differences"]], [category])

    def test_reports_no_differences_when_projection_matches(self) -> None:
        result = BRIDGE.compare_ghidra_to_rizin(ghidra_report(), rizin_report())
        self.assertEqual(result["differences"], [])
        self.assertEqual(result["unavailable"], [])

    def test_accepts_empty_optional_rizin_pseudocode(self) -> None:
        rizin = rizin_report()
        rizin["functions"][0]["pseudocode"] = {"engine": "rz-ghidra", "text": ""}
        result = BRIDGE.compare_ghidra_to_rizin(ghidra_report(), rizin)
        self.assertEqual(result["differences"], [])

    def test_external_ghidra_callee_is_reported_instead_of_crashing(self) -> None:
        ghidra = ghidra_report()
        ghidra["functions"][0]["callees"].append(
            {
                "site_rva": "0x00003F60",
                "target_space": "EXTERNAL",
                "name": "synthetic_import",
            }
        )
        result = BRIDGE.compare_ghidra_to_rizin(ghidra, rizin_report())
        self.assertEqual(result["differences"][0]["category"], "callees")

    def test_unresolved_callee_representation_compares_by_site(self) -> None:
        ghidra = ghidra_report()
        ghidra["functions"][0]["callees"] = [{"site_rva": "0x00003F50"}]
        rizin = rizin_report()
        rizin["functions"][0]["calls"] = [
            {"site": "0x10003F50", "target": None}
        ]
        result = BRIDGE.compare_ghidra_to_rizin(ghidra, rizin)
        self.assertEqual(result["differences"], [])

    def test_unexported_rizin_categories_are_unavailable_not_equal_or_different(self) -> None:
        rizin = rizin_report()
        del rizin["functions"][0]["constants"]
        del rizin["functions"][0]["indirect_calls"]
        result = BRIDGE.compare_ghidra_to_rizin(ghidra_report(), rizin)
        self.assertEqual(result["differences"], [])
        self.assertEqual(
            [item["category"] for item in result["unavailable"]],
            ["constants", "indirect_calls"],
        )

    def test_read_write_disagreement_is_not_hidden(self) -> None:
        ghidra = ghidra_report()
        observed = ghidra["functions"][0]["globals"]["reads"]
        ghidra["functions"][0]["globals"] = {"reads": [], "writes": observed}
        self.assert_difference("globals", ghidra)

    def test_multiple_sites_to_same_targets_compare_without_loss(self) -> None:
        ghidra = ghidra_report()
        ghidra["functions"][0]["callees"] = [
            {"site_rva": "0x00003F50", "target_rva": "0x00006000"},
            {"site_rva": "0x00003F54", "target_rva": "0x00006000"},
        ]
        ghidra["functions"][0]["globals"] = {
            "reads": [
                {"site_rva": "0x00003F58", "target_rva": "0x0000D610"},
                {"site_rva": "0x00003F5C", "target_rva": "0x0000D610"},
            ],
            "writes": [],
        }
        rizin = rizin_report()
        rizin["functions"][0]["calls"] = [
            {"site": "0x10003F50", "target": "0x10006000"},
            {"site": "0x10003F54", "target": "0x10006000"},
        ]
        rizin["functions"][0]["data_refs"] = [
            {"site": "0x10003F58", "target": "0x1000D610", "kind": "read"},
            {"site": "0x10003F5C", "target": "0x1000D610", "kind": "read"},
        ]
        result = BRIDGE.compare_ghidra_to_rizin(ghidra, rizin)
        self.assertEqual(result["differences"], [])

    def test_unresolved_rizin_call_is_preserved_as_a_difference(self) -> None:
        rizin = rizin_report()
        rizin["functions"][0]["calls"].append(
            {"site": "0x10003F60", "target": None}
        )
        result = BRIDGE.compare_ghidra_to_rizin(ghidra_report(), rizin)
        self.assertEqual(result["differences"][0]["category"], "callees")

    def test_reports_each_comparison_category(self) -> None:
        boundary = ghidra_report()
        boundary["functions"][0]["boundary"]["end_rva_exclusive"] = "0x3fc0"
        self.assert_difference("boundary", boundary)

        blocks = ghidra_report()
        blocks["functions"][0]["cfg"]["block_count"] = 3
        self.assert_difference("block_count", blocks)

        callees = ghidra_report()
        callees["functions"][0]["callees"] = []
        self.assert_difference("callees", callees)

        globals_value = ghidra_report()
        globals_value["functions"][0]["globals"] = {"reads": [], "writes": []}
        self.assert_difference("globals", globals_value)

        constants = ghidra_report()
        constants["functions"][0]["constants"] = [{"kind": "f32", "bits": "0x3F800000"}]
        self.assert_difference("constants", constants)

        indirect = ghidra_report()
        indirect["functions"][0]["indirect_calls"] = [
            {
                "site_rva": "0x3f60",
                "text": "CALL dword ptr [EAX + 0xB4]",
                "candidate_displacements": ["0xb4"],
            }
        ]
        self.assert_difference("indirect_calls", indirect)

    def test_fails_closed_for_hash_and_rva_mismatch(self) -> None:
        wrong_hash = rizin_report()
        wrong_hash["source"]["sha256"] = "b" * 64
        with self.assertRaisesRegex(BRIDGE.BridgeError, "source SHA"):
            BRIDGE.compare_ghidra_to_rizin(ghidra_report(), wrong_hash)

        wrong_rva = rizin_report()
        wrong_rva["functions"][0]["rva"] = "0x00004000"
        wrong_rva["functions"][0]["boundary"]["start"] = "0x10004000"
        with self.assertRaisesRegex(BRIDGE.BridgeError, "different function RVAs"):
            BRIDGE.compare_ghidra_to_rizin(ghidra_report(), wrong_rva)


class FileSafetyTests(unittest.TestCase):
    def test_rejects_nonfinite_numbers_and_duplicate_json_keys(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            path = Path(raw) / "input.json"
            path.write_text('{"value":NaN}', encoding="utf-8")
            with self.assertRaisesRegex(BRIDGE.BridgeError, "non-finite"):
                BRIDGE.load_json_file(path)

            path.write_text('{"value":1,"value":2}', encoding="utf-8")
            with self.assertRaisesRegex(BRIDGE.BridgeError, "duplicate object key"):
                BRIDGE.load_json_file(path)

    def test_rejects_oversized_input(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            path = Path(raw) / "input.json"
            path.write_text("{}", encoding="utf-8")
            original_limit = BRIDGE.MAX_INPUT_BYTES
            try:
                BRIDGE.MAX_INPUT_BYTES = 1
                with self.assertRaisesRegex(BRIDGE.BridgeError, "size limit"):
                    BRIDGE.load_json_file(path)
            finally:
                BRIDGE.MAX_INPUT_BYTES = original_limit

    def test_atomic_output_preserves_existing_file_on_serialization_failure(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            path = Path(raw) / "result.json"
            path.write_text("{\"previous\":true}\n", encoding="utf-8")
            with self.assertRaisesRegex(BRIDGE.BridgeError, "serialized"):
                BRIDGE.atomic_write_json(path, {"not_json": object()})
            self.assertEqual(path.read_text(encoding="utf-8"), "{\"previous\":true}\n")


if __name__ == "__main__":
    unittest.main()
