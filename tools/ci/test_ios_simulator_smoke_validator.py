#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
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
    def test_launch_uses_supervised_console_without_ps_probe(self) -> None:
        command = MODULE.simulator_launch_command(
            "00000000-0000-0000-0000-000000000000"
        )
        self.assertIn("--console", command)
        self.assertFalse(any(part.startswith("--stdout=") for part in command))
        self.assertNotIn("/bin/ps", command)
        self.assertEqual(command[-1], MODULE.BUNDLE_ID)

    def test_supervised_launch_cannot_inherit_blocking_console_pipes(self) -> None:
        process = mock.sentinel.process
        with mock.patch.object(MODULE.subprocess, "Popen", return_value=process) as popen:
            self.assertIs(
                MODULE.start_supervised_launch(
                    "00000000-0000-0000-0000-000000000000"
                ),
                process,
            )
        self.assertIs(popen.call_args.kwargs["stdout"], subprocess.DEVNULL)
        self.assertIs(popen.call_args.kwargs["stderr"], subprocess.DEVNULL)

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

    def test_supervised_stop_escalates_without_escaping(self) -> None:
        process = mock.Mock()
        process.poll.return_value = None
        process.wait.side_effect = [
            subprocess.TimeoutExpired(["xcrun", "simctl"], 5),
            0,
        ]
        MODULE.stop_supervised_launch(process)
        process.terminate.assert_called_once_with()
        process.kill.assert_called_once_with()

    def test_result_published_during_exit_poll_is_accepted(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result_path = root / "result.json"
            output = root / "validated.json"
            launcher = mock.Mock()

            def publish_then_exit() -> int:
                result_path.write_text(
                    json.dumps(valid_result()), encoding="utf-8"
                )
                return 0

            launcher.poll.side_effect = publish_then_exit
            with mock.patch("builtins.print"):
                completed, return_code = MODULE.poll_supervised_result(
                    launcher, result_path, output
                )
            self.assertTrue(completed)
            self.assertIsNone(return_code)
            self.assertTrue(output.is_file())

    def test_successful_detached_launch_keeps_waiting_for_result(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            launcher = mock.Mock()
            launcher.poll.return_value = 0

            completed, return_code = MODULE.poll_supervised_result(
                launcher, root / "missing.json", root / "validated.json"
            )

            self.assertFalse(completed)
            self.assertIsNone(return_code)

    def test_failed_launch_is_reported_before_result(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            launcher = mock.Mock()
            launcher.poll.return_value = 3

            completed, return_code = MODULE.poll_supervised_result(
                launcher, root / "missing.json", root / "validated.json"
            )

            self.assertFalse(completed)
            self.assertEqual(return_code, 3)

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
