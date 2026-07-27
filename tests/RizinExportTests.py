#!/usr/bin/env python3

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = REPOSITORY_ROOT / "tools" / "rizin" / "export_function_report.py"
SPEC = importlib.util.spec_from_file_location("airfix_rizin_export", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("unable to load the Rizin exporter")
EXPORTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXPORTER)


class FakePipe:
    def __init__(self, responses: dict[str, object]) -> None:
        self.responses = copy.deepcopy(responses)
        self.commands: list[str] = []
        self.quit_called = False

    def cmd(self, command: str) -> str:
        self.commands.append(command)
        response = self.responses.get(command, "")
        if not isinstance(response, str):
            raise AssertionError(f"non-text response configured for {command}")
        return response

    def cmdj(self, command: str) -> object:
        self.commands.append(command)
        if command not in self.responses:
            raise AssertionError(f"no JSON response configured for {command}")
        return copy.deepcopy(self.responses[command])

    def quit(self) -> None:
        self.quit_called = True


def tool_metadata() -> dict[str, str]:
    return {
        "commit": "c3a90e9226d977f58f4e9c75f78fa6b07afe13c7",
        "platform": "windows-x86-64",
        "version": "0.9.1",
    }


def sample_responses() -> dict[str, object]:
    at = "0x10003F40"
    return {
        "ij": {
            "core": {
                "file": r"C:\private\Dogfighter.exe",
                "format": "pe",
            },
            "bin": {
                "arch": "x86",
                "baddr": 0x10000000,
                "bintype": "pe",
                "bits": 32,
                "class": "PE32",
                "endian": "LE",
                "file": "must-not-enter-report.bin",
                "machine": "i386",
            }
        },
        "iSj": [
            {
                "name": ".text",
                "paddr": 0x400,
                "size": 0x7000,
                "vaddr": 0x10001000,
                "vsize": 0x8000,
            }
        ],
        f"afij @ {at}": [
            {
                "callrefs": [
                    {"addr": 0x100065B0, "at": 0x10004190, "type": "CALL"},
                    {"addr": 0x10005200, "at": 0x10004198, "type": "CODE"},
                ],
                "cc": "thiscall",
                "datarefs": [0x1000D610],
                "maxbound": 0x10003FB8,
                "minbound": 0x10003F40,
                "name": "sym.AircraftFlightForceStep",
                "nargs": 2,
                "offset": 0x10003F40,
                "realsz": 0x70,
                "signature": "void AircraftFlightForceStep(void *, float)",
                "size": 0x80,
            }
        ],
        f"pdfj @ {at}": {
            "ops": [
                {
                    "bytes": "e86b240000",
                    "jump": 0x100065B0,
                    "offset": 0x10004190,
                    "opcode": "call sym.TransformVec3ByBasis",
                    "size": 5,
                    "type": "ucall",
                    "xrefs_from": [
                        {
                            "addr": 0x100065B0,
                            "name": "sym.TransformVec3ByBasis",
                            "type": "CALL",
                        }
                    ],
                },
                {
                    "bytes": "d80d10d60010",
                    "offset": 0x100041A0,
                    "opcode": "fmul dword [0x1000d610]",
                    "size": 6,
                    "type": "load",
                    "xrefs_from": [{"addr": 0x1000D610, "type": "DATA"}],
                },
                {
                    "bytes": "7505",
                    "offset": 0x100041A6,
                    "opcode": "jne 0x100041ad",
                    "size": 2,
                    "type": "cjmp",
                    "xrefs_from": [{"addr": 0x100041AD, "type": "CODE"}],
                },
                {
                    "bytes": "c20400",
                    "offset": 0x10003F40,
                    "opcode": "ret 4",
                    "size": 3,
                    "type": "ret",
                },
            ]
        },
        f"agfj @ {at}": [
            {
                "blocks": [
                    {
                        "offset": 0x10004180,
                        "size": 0x30,
                        "jump": 0x100041C0,
                        "fail": 0x100041B0,
                    },
                    {"offset": 0x10003F40, "size": 0x40, "jump": 0x10004180},
                ],
            }
        ],
        f"axfj @ {at}": [
            {
                "from": 0x100041A0,
                "name": "obj.flight_damping",
                "to": 0x1000D610,
                "type": "DATA",
            },
            {
                "from": 0x10009999,
                "name": "sym.InboundCaller",
                "to": 0x10003F40,
                "type": "CALL",
            },
        ],
    }


class RizinExportTests(unittest.TestCase):
    def test_open_pipe_disables_user_configuration(self) -> None:
        opened: dict[str, object] = {}
        expected_pipe = object()

        class FakeRzPipeModule:
            @staticmethod
            def version() -> str:
                return "0.6.2"

            @staticmethod
            def open(
                input_path: str,
                *,
                flags: list[str],
                rizin_home: str,
            ) -> object:
                opened.update(
                    {
                        "flags": flags,
                        "input_path": input_path,
                        "rizin_home": rizin_home,
                    }
                )
                return expected_pipe

        input_path = Path("private-input.bin")
        rizin_home = Path("verified-rizin")
        with mock.patch.dict(sys.modules, {"rzpipe": FakeRzPipeModule}):
            result = EXPORTER._open_pipe(input_path, rizin_home)

        self.assertIs(result, expected_pipe)
        self.assertEqual(opened["flags"], ["-2", "-N"])
        self.assertEqual(opened["input_path"], str(input_path))
        self.assertEqual(opened["rizin_home"], str(rizin_home))

    def test_maps_rva_and_normalizes_required_queries(self) -> None:
        pipe = FakePipe(sample_responses())
        report = EXPORTER.build_report(
            pipe,
            source_sha256="A" * 64,
            function_id="FN-AIRCRAFT-00003F40",
            rva=0x3F40,
            rizin_metadata=tool_metadata(),
        )

        self.assertEqual(report["schema"], "airfix.re.rizin-function.v1")
        self.assertEqual(report["source"], {"sha256": "a" * 64})
        self.assertEqual(
            report["binary"],
            {
                "architecture": "x86",
                "bits": 32,
                "class": "PE32",
                "endian": "LE",
                "format": "pe",
                "image_base": "0x10000000",
                "machine": "i386",
            },
        )
        self.assertEqual(
            report["tools"],
            {
                "rizin": tool_metadata(),
                "rzpipe": {"version": "0.6.2"},
            },
        )
        function = report["functions"][0]
        self.assertEqual(
            function["discovery"],
            {"automatic": True, "created_at_requested_va": False},
        )
        self.assertEqual(function["id"], "FN-AIRCRAFT-00003F40")
        self.assertEqual(function["rva"], "0x00003F40")
        self.assertEqual(function["va"], "0x10003F40")
        self.assertEqual(
            function["boundary"],
            {
                "end_exclusive": "0x10003FB8",
                "offset": "0x10003F40",
                "realsz": 0x70,
                "size": 0x80,
                "start": "0x10003F40",
            },
        )
        self.assertEqual(
            function["location"],
            {"file_offset": "0x00003340", "section": ".text"},
        )
        self.assertEqual(function["signature"]["calling_convention"], "thiscall")
        self.assertEqual(function["signature"]["arguments"], 2)
        self.assertEqual(
            [call["target"] for call in function["calls"]],
            ["0x100065B0"],
        )
        self.assertEqual(function["calls"][0]["name"], "sym.TransformVec3ByBasis")
        self.assertNotIn(
            "0x10005200",
            [call["target"] for call in function["calls"]],
        )
        self.assertNotIn(
            "0x10003F40",
            [call["target"] for call in function["calls"]],
        )
        self.assertIn(
            "0x1000D610",
            [reference["target"] for reference in function["data_refs"]],
        )
        self.assertNotIn(
            "0x100041AD",
            [reference["target"] for reference in function["data_refs"]],
        )
        self.assertEqual(
            pipe.commands,
            [
                "e scr.color=0",
                "e scr.utf8=false",
                "aaa",
                "ij",
                "iSj",
                "afij @ 0x10003F40",
                "pdfj @ 0x10003F40",
                "agfj @ 0x10003F40",
                "axfj @ 0x10003F40",
            ],
        )

        serialized = json.dumps(report, sort_keys=True)
        self.assertNotIn("must-not-enter-report.bin", serialized)
        self.assertNotIn(str(REPOSITORY_ROOT), serialized)

    def test_output_is_deterministic_for_unsorted_tool_results(self) -> None:
        first = sample_responses()
        second = sample_responses()
        second["pdfj @ 0x10003F40"]["ops"].reverse()
        second["agfj @ 0x10003F40"][0]["blocks"].reverse()
        second["axfj @ 0x10003F40"].reverse()

        first_report = EXPORTER.build_report(
            FakePipe(first),
            source_sha256="b" * 64,
            function_id="FN-AIRCRAFT-00003F40",
            rva=0x3F40,
            rizin_metadata=tool_metadata(),
        )
        second_report = EXPORTER.build_report(
            FakePipe(second),
            source_sha256="b" * 64,
            function_id="FN-AIRCRAFT-00003F40",
            rva=0x3F40,
            rizin_metadata=tool_metadata(),
        )
        self.assertEqual(
            json.dumps(first_report, sort_keys=True),
            json.dumps(second_report, sort_keys=True),
        )

    def test_optional_sleigh_home_enables_pdg_without_leaking_path(self) -> None:
        responses = sample_responses()
        responses["pdg @ 0x10003F40"] = "void f(void) {\r\n  return;\r\n}\r\n"
        pipe = FakePipe(responses)
        report = EXPORTER.build_report(
            pipe,
            source_sha256="c" * 64,
            function_id="FN-AIRCRAFT-00003F40",
            rva=0x3F40,
            rizin_metadata=tool_metadata(),
            sleigh_home=r"C:\Local Tools\Sleigh",
        )

        self.assertIn(
            "e ghidra.sleighhome=C:/Local Tools/Sleigh",
            pipe.commands,
        )
        self.assertIn("pdg @ 0x10003F40", pipe.commands)
        self.assertEqual(
            report["functions"][0]["pseudocode"],
            {
                "engine": "rz-ghidra",
                "text": "void f(void) {\n  return;\n}",
            },
        )
        self.assertNotIn("Local Tools", json.dumps(report, sort_keys=True))

    def test_hash_mismatch_does_not_open_rizin_or_write_output(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            input_path = root / "reference.bin"
            input_path.write_bytes(b"reference bytes")
            output_path = root / "report.json"
            rizin_home = root / "rizin"
            rizin_home.mkdir()
            (rizin_home / ("rizin.exe" if EXPORTER.os.name == "nt" else "rizin")).touch()
            opened = False

            def fail_if_opened(_input: Path, _home: Path) -> FakePipe:
                nonlocal opened
                opened = True
                raise AssertionError("Rizin must not open after a hash mismatch")

            with self.assertRaisesRegex(EXPORTER.ExportError, "SHA-256"):
                EXPORTER.export_to_file(
                    input_path=input_path,
                    output_path=output_path,
                    rizin_home=rizin_home,
                    function_id="FN-TEST-00000000",
                    rva=0,
                    expected_sha256="0" * 64,
                    pipe_opener=fail_if_opened,
                )

            self.assertFalse(opened)
            self.assertFalse(output_path.exists())

    def test_rejects_noncanonical_function_id(self) -> None:
        with self.assertRaisesRegex(EXPORTER.ExportError, "FN-<MODULE>-<RVA>"):
            EXPORTER.build_report(
                FakePipe(sample_responses()),
                source_sha256="d" * 64,
                function_id=r"C:\private\function",
                rva=0x3F40,
                rizin_metadata=tool_metadata(),
            )

    def test_rejects_non_pe32_x86_input(self) -> None:
        responses = sample_responses()
        responses["ij"]["bin"]["bits"] = 64
        with self.assertRaisesRegex(EXPORTER.ExportError, "PE32/x86"):
            EXPORTER.build_report(
                FakePipe(responses),
                source_sha256="d" * 64,
                function_id="FN-AIRCRAFT-00003F40",
                rva=0x3F40,
                rizin_metadata=tool_metadata(),
            )

    def test_missing_function_requires_explicit_creation_opt_in(self) -> None:
        responses = sample_responses()
        responses["afij @ 0x10003F40"] = []
        with self.assertRaisesRegex(EXPORTER.ExportError, "did not identify"):
            EXPORTER.build_report(
                FakePipe(responses),
                source_sha256="e" * 64,
                function_id="FN-AIRCRAFT-00003F40",
                rva=0x3F40,
                rizin_metadata=tool_metadata(),
            )

    def test_rejects_afij_candidate_at_a_different_address(self) -> None:
        responses = sample_responses()
        responses["afij @ 0x10003F40"][0]["offset"] = 0x10003F50
        with self.assertRaisesRegex(EXPORTER.ExportError, "did not identify"):
            EXPORTER.build_report(
                FakePipe(responses),
                source_sha256="e" * 64,
                function_id="FN-AIRCRAFT-00003F40",
                rva=0x3F40,
                rizin_metadata=tool_metadata(),
            )

    def test_explicit_opt_in_creates_and_requeries_missing_function(self) -> None:
        responses = sample_responses()
        detected_function = responses["afij @ 0x10003F40"]
        pipe = FakePipe(responses)
        responses_sequence = [[], detected_function]
        original_cmdj = pipe.cmdj

        def sequenced_cmdj(command: str) -> object:
            if command == "afij @ 0x10003F40":
                pipe.commands.append(command)
                return copy.deepcopy(responses_sequence.pop(0))
            return original_cmdj(command)

        pipe.cmdj = sequenced_cmdj
        report = EXPORTER.build_report(
            pipe,
            source_sha256="f" * 64,
            function_id="FN-AIRCRAFT-00003F40",
            rva=0x3F40,
            rizin_metadata=tool_metadata(),
            create_missing_function=True,
        )

        self.assertEqual(
            report["functions"][0]["discovery"],
            {"automatic": False, "created_at_requested_va": True},
        )
        self.assertEqual(
            [
                command
                for command in pipe.commands
                if command in ("afij @ 0x10003F40", "af @ 0x10003F40")
            ],
            [
                "afij @ 0x10003F40",
                "af @ 0x10003F40",
                "afij @ 0x10003F40",
            ],
        )

    def test_export_quits_pipe_and_writes_path_free_json(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            input_path = root / "reference.bin"
            input_path.write_bytes(b"reference bytes")
            expected_hash = hashlib.sha256(input_path.read_bytes()).hexdigest()
            output_path = root / "report.json"
            rizin_home = root / "rizin"
            rizin_home.mkdir()
            (rizin_home / ("rizin.exe" if EXPORTER.os.name == "nt" else "rizin")).touch()
            pipe = FakePipe(sample_responses())

            report = EXPORTER.export_to_file(
                input_path=input_path,
                output_path=output_path,
                rizin_home=rizin_home,
                function_id="FN-AIRCRAFT-00003F40",
                rva=0x3F40,
                expected_sha256=expected_hash.upper(),
                pipe_opener=lambda _input, _home: pipe,
                rizin_metadata_provider=lambda _home: tool_metadata(),
            )

            self.assertTrue(pipe.quit_called)
            self.assertEqual(json.loads(output_path.read_text("utf-8")), report)
            serialized = output_path.read_text("utf-8")
            self.assertNotIn(str(input_path), serialized)
            self.assertNotIn(str(rizin_home), serialized)
            self.assertTrue(serialized.endswith("\n"))

    def test_export_rejects_input_changed_during_analysis(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            input_path = root / "reference.bin"
            input_path.write_bytes(b"reference bytes")
            expected_hash = hashlib.sha256(input_path.read_bytes()).hexdigest()
            output_path = root / "report.json"
            rizin_home = root / "rizin"
            rizin_home.mkdir()
            (
                rizin_home
                / ("rizin.exe" if EXPORTER.os.name == "nt" else "rizin")
            ).touch()
            pipe = FakePipe(sample_responses())

            def mutating_opener(
                _input: Path,
                _home: Path,
            ) -> FakePipe:
                input_path.write_bytes(b"changed during analysis")
                return pipe

            with self.assertRaisesRegex(
                EXPORTER.ExportError,
                "changed during analysis",
            ):
                EXPORTER.export_to_file(
                    input_path=input_path,
                    output_path=output_path,
                    rizin_home=rizin_home,
                    function_id="FN-AIRCRAFT-00003F40",
                    rva=0x3F40,
                    expected_sha256=expected_hash,
                    pipe_opener=mutating_opener,
                    rizin_metadata_provider=lambda _home: tool_metadata(),
                )

            self.assertTrue(pipe.quit_called)
            self.assertFalse(output_path.exists())


if __name__ == "__main__":
    unittest.main()
