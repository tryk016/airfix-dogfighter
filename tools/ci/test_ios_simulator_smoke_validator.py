#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import subprocess
import unittest
from unittest import mock


SCRIPT = Path(__file__).with_name("run-ios-simulator-smoke.py")
SPEC = importlib.util.spec_from_file_location("airfix_ios_smoke", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def valid_result() -> dict[str, object]:
    return {
        "schema": "airfix.ios-simulator-smoke",
        "version": 1,
        "status": "pass",
        "dataLess": True,
        "metalFrame": {
            "commandBufferCompleted": True,
            "publicSyntheticScene": True,
            "sceneDrawCalls": 1,
            "sceneTriangles": 12,
        },
        "lifecycle": {
            "sequence": ["resign-active", "background", "foreground", "active"],
            "inactivePaused": True,
            "backgroundPaused": True,
            "foregroundPaused": True,
            "activeStillPaused": True,
            "autoResumed": False,
        },
    }


class ResultValidationTests(unittest.TestCase):
    def test_accepts_complete_result(self) -> None:
        self.assertEqual(MODULE.validate_result(valid_result())["status"], "pass")

    def test_rejects_auto_resume(self) -> None:
        result = valid_result()
        result["lifecycle"]["autoResumed"] = True
        with self.assertRaises(MODULE.SmokeFailure):
            MODULE.validate_result(result)


class RuntimeSelectionTests(unittest.TestCase):
    def test_accepts_platform_less_ios_runtime(self) -> None:
        payload = {
            "runtimes": [
                {
                    "identifier": "com.apple.CoreSimulator.SimRuntime.iOS-26-5",
                    "version": "26.5",
                    "isAvailable": True,
                }
            ]
        }
        self.assertEqual(
            MODULE.select_runtime_from_payload(payload),
            "com.apple.CoreSimulator.SimRuntime.iOS-26-5",
        )

    def test_rejects_non_ios_runtime(self) -> None:
        payload = {
            "runtimes": [
                {
                    "identifier": "com.apple.CoreSimulator.SimRuntime.tvOS-26-5",
                    "version": "26.5",
                    "isAvailable": True,
                }
            ]
        }
        with self.assertRaises(MODULE.SmokeFailure):
            MODULE.select_runtime_from_payload(payload)

    def test_rejects_malformed_runtime_catalogue(self) -> None:
        with self.assertRaises(MODULE.SmokeFailure):
            MODULE.select_runtime_from_payload({"runtimes": "not-a-list"})

    def test_rejects_non_public_frame(self) -> None:
        result = valid_result()
        result["metalFrame"]["publicSyntheticScene"] = False
        with self.assertRaises(MODULE.SmokeFailure):
            MODULE.validate_result(result)


class DiagnosticHardeningTests(unittest.TestCase):
    def test_launch_captures_stdout_and_stderr_to_host_paths(self) -> None:
        command = MODULE.simulator_launch_command(
            "00000000-0000-0000-0000-000000000000",
            Path("capture-stdout.log"),
            Path("capture-stderr.log"),
        )
        self.assertIn("--stdout=capture-stdout.log", command)
        self.assertIn("--stderr=capture-stderr.log", command)
        self.assertEqual(command[-1], MODULE.BUNDLE_ID)

    def test_redacts_paths_controls_and_bounds_output(self) -> None:
        text = (
            "\x1b[31m/Users/alice/work/file.mm\x1b[0m\n"
            "C:\\private\\asset.gti\n"
            "/host/capture/app-stderr.log\n"
        )
        with mock.patch.dict(os.environ, {"GITHUB_WORKSPACE": "/Users/alice/work"}):
            redacted = MODULE.redact_diagnostic_text(
                text, ("/host/capture",)
            )
        self.assertNotIn("alice", redacted)
        self.assertNotIn("asset.gti", redacted)
        self.assertNotIn("/host/capture", redacted)
        self.assertNotIn("\x1b", redacted)

    def test_bounds_output_to_final_diagnostic_bytes(self) -> None:
        redacted = MODULE.redact_diagnostic_text("x" * 40_000)
        self.assertTrue(redacted.startswith("[truncated"))
        self.assertLessEqual(
            len(redacted.encode("utf-8")), MODULE.MAX_DIAGNOSTIC_BYTES
        )

    def test_subprocess_timeout_becomes_smoke_failure(self) -> None:
        with mock.patch.object(
            MODULE.subprocess,
            "run",
            side_effect=subprocess.TimeoutExpired(["xcrun", "simctl"], 7),
        ):
            with self.assertRaisesRegex(
                MODULE.SmokeFailure, "timed out after 7 seconds"
            ):
                MODULE.run("xcrun", "simctl", timeout=7)

    def test_command_failure_detail_is_redacted(self) -> None:
        completed = subprocess.CompletedProcess(
            ["xcrun", "simctl"], 1, "", "/Users/alice/private/error"
        )
        with mock.patch.object(MODULE.subprocess, "run", return_value=completed):
            with self.assertRaises(MODULE.SmokeFailure) as raised:
                MODULE.run("xcrun", "simctl")
        self.assertNotIn("alice", str(raised.exception))

    def test_cleanup_timeout_does_not_escape(self) -> None:
        with mock.patch.object(
            MODULE,
            "run",
            side_effect=MODULE.SmokeFailure("cleanup timed out"),
        ):
            MODULE.run_non_masking("xcrun", "simctl", timeout=1)

    def test_rejects_unknown_fields(self) -> None:
        result = valid_result()
        result["localPath"] = "/private/value"
        with self.assertRaises(MODULE.SmokeFailure):
            MODULE.validate_result(result)

    def test_rejects_boolean_integer_substitution(self) -> None:
        result = valid_result()
        result["dataLess"] = 1
        with self.assertRaises(MODULE.SmokeFailure):
            MODULE.validate_result(result)


if __name__ == "__main__":
    unittest.main()
