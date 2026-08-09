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
        "audioProbe": {
            "registered": True,
            "submissionAccepted": True,
            "stopped": True,
        },
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


def diagnostic_record(
    sequence: int, event: str, fields: dict[str, object]
) -> dict[str, object]:
    return {
        "elapsedMs": sequence,
        "event": event,
        "fields": fields,
        "schema": MODULE.JOURNAL_SCHEMA,
        "sequence": sequence,
    }


def valid_diagnostic_records() -> list[dict[str, object]]:
    return [
        diagnostic_record(1, "session.started", {}),
        diagnostic_record(2, "renderer.initialized", {"succeeded": True}),
        diagnostic_record(3, "content.state", {"state": "missing"}),
        diagnostic_record(
            4, "lifecycle.transition", {"state": "resign-active"}
        ),
        diagnostic_record(5, "lifecycle.transition", {"state": "background"}),
        diagnostic_record(6, "lifecycle.transition", {"state": "foreground"}),
        diagnostic_record(7, "lifecycle.transition", {"state": "active"}),
    ]


def write_valid_diagnostic_workspace(documents: Path) -> None:
    (documents / "README.txt").write_bytes(
        MODULE.WORKSPACE_README.encode("utf-8")
    )
    (documents / "Imports").mkdir()
    diagnostics = documents / "Diagnostics"
    diagnostics.mkdir()
    journal = diagnostics / "latest.jsonl"
    journal.write_text(
        "".join(
            json.dumps(record, sort_keys=True, separators=(",", ":")) + "\n"
            for record in valid_diagnostic_records()
        ),
        encoding="utf-8",
    )


class ResultValidationTests(unittest.TestCase):
    def test_accepts_complete_result(self) -> None:
        self.assertEqual(MODULE.validate_result(valid_result())["status"], "pass")

    def test_rejects_auto_resume(self) -> None:
        result = valid_result()
        result["lifecycle"]["autoResumed"] = True
        with self.assertRaises(MODULE.SmokeFailure):
            MODULE.validate_result(result)

    def test_rejects_unproven_audio_probe_state(self) -> None:
        for name in ("registered", "submissionAccepted", "stopped"):
            with self.subTest(name=name):
                result = valid_result()
                result["audioProbe"][name] = False
                with self.assertRaisesRegex(MODULE.SmokeFailure, name):
                    MODULE.validate_result(result)

    def test_rejects_extra_audio_probe_field(self) -> None:
        result = valid_result()
        result["audioProbe"]["privatePath"] = "/private/value"
        with self.assertRaisesRegex(MODULE.SmokeFailure, "unexpected schema"):
            MODULE.validate_result(result)

    def test_reports_bounded_application_failure_stage(self) -> None:
        result = {
            "schema": "airfix.ios-simulator-smoke",
            "version": 1,
            "status": "fail",
            "failureStage": "missing-drawable",
        }
        with self.assertRaisesRegex(
            MODULE.SmokeFailure, "failure stage: missing-drawable"
        ):
            MODULE.validate_result(result)

    def test_rejects_unknown_application_failure_stage(self) -> None:
        result = {
            "schema": "airfix.ios-simulator-smoke",
            "version": 1,
            "status": "fail",
            "failureStage": "private/path/value",
        }
        with self.assertRaisesRegex(MODULE.SmokeFailure, "unknown failure stage"):
            MODULE.validate_result(result)

    def test_accepts_bounded_watchdog_failure_stages(self) -> None:
        for stage in (
            "renderer-initialization-timeout",
            "draw-call-timeout",
            "smoke-sequence-timeout",
        ):
            with self.subTest(stage=stage):
                result = {
                    "schema": "airfix.ios-simulator-smoke",
                    "version": 1,
                    "status": "fail",
                    "failureStage": stage,
                }
                with self.assertRaisesRegex(MODULE.SmokeFailure, stage):
                    MODULE.validate_result(result)

    def test_rejects_extra_application_failure_field(self) -> None:
        result = {
            "schema": "airfix.ios-simulator-smoke",
            "version": 1,
            "status": "fail",
            "failureStage": "missing-drawable",
            "localPath": "/private/value",
        }
        with self.assertRaisesRegex(MODULE.SmokeFailure, "unexpected top-level"):
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
    def test_validates_extended_input_sample_contract(self) -> None:
        fields = {
            "bank": -12000,
            "controllerConnected": False,
            "fireHeld": True,
            "firePressed": True,
            "fireReleased": False,
            "pitch": 8000,
            "simulationHash": "0123456789ABCDEF",
            "simulationStep": 120,
            "source": "touch",
            "throttle": 24576,
            "tick": 240,
        }
        MODULE.validate_journal_fields("input.sample", fields)
        for name in ("firePressed", "fireReleased", "throttle"):
            with self.subTest(name=name):
                invalid = dict(fields)
                del invalid[name]
                with self.assertRaisesRegex(
                    MODULE.SmokeFailure, "input fields are invalid"
                ):
                    MODULE.validate_journal_fields("input.sample", invalid)

    def test_accepts_bounded_path_free_diagnostic_workspace(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            documents = Path(directory)
            write_valid_diagnostic_workspace(documents)
            records = MODULE.validate_diagnostic_workspace(documents)
        self.assertEqual(len(records), 7)

    def test_accepts_rotated_journal_with_continuation_marker(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            documents = Path(directory)
            write_valid_diagnostic_workspace(documents)
            journal = documents / "Diagnostics" / "latest.jsonl"
            records = valid_diagnostic_records()
            records[0]["event"] = "session.continued"
            journal.write_text(
                "".join(json.dumps(record) + "\n" for record in records),
                encoding="utf-8",
            )
            validated = MODULE.validate_diagnostic_workspace(documents)
        self.assertEqual(validated[0]["event"], "session.continued")

    def test_rejects_unknown_diagnostic_event_and_field(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            documents = Path(directory)
            write_valid_diagnostic_workspace(documents)
            journal = documents / "Diagnostics" / "latest.jsonl"
            invalid = diagnostic_record(
                8, "private.path", {"path": "/private/value"}
            )
            with journal.open("a", encoding="utf-8") as output:
                output.write(json.dumps(invalid) + "\n")
            with self.assertRaisesRegex(
                MODULE.SmokeFailure, "identity is invalid"
            ):
                MODULE.validate_diagnostic_workspace(documents)

    def test_rejects_diagnostic_lifecycle_reordering(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            documents = Path(directory)
            write_valid_diagnostic_workspace(documents)
            journal = documents / "Diagnostics" / "latest.jsonl"
            records = valid_diagnostic_records()
            records[-1]["fields"]["state"] = "background"
            journal.write_text(
                "".join(json.dumps(record) + "\n" for record in records),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(
                MODULE.SmokeFailure, "lifecycle sequence changed"
            ):
                MODULE.validate_diagnostic_workspace(documents)

    def test_rejects_oversized_diagnostic_journal(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            documents = Path(directory)
            write_valid_diagnostic_workspace(documents)
            journal = documents / "Diagnostics" / "latest.jsonl"
            journal.write_bytes(b"x" * (MODULE.MAX_JOURNAL_BYTES + 1))
            with self.assertRaisesRegex(
                MODULE.SmokeFailure, "invalid type or size"
            ):
                MODULE.validate_diagnostic_workspace(documents)

    def test_rejects_changed_workspace_readme(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            documents = Path(directory)
            write_valid_diagnostic_workspace(documents)
            (documents / "README.txt").write_text(
                "private/path/value\n", encoding="utf-8"
            )
            with self.assertRaisesRegex(
                MODULE.SmokeFailure, "README content changed"
            ):
                MODULE.validate_diagnostic_workspace(documents)

    def test_launch_uses_fresh_standard_acknowledged_path(self) -> None:
        command = MODULE.simulator_launch_command(
            "00000000-0000-0000-0000-000000000000"
        )
        self.assertNotIn("--console", command)
        self.assertNotIn("--terminate-running-process", command)
        self.assertNotIn("/bin/ps", command)
        self.assertEqual(command[-1], MODULE.BUNDLE_ID)

    def test_launch_accepts_exact_positive_pid_acknowledgement(self) -> None:
        acknowledgement = subprocess.CompletedProcess(
            ["xcrun", "simctl"], 0, f"{MODULE.BUNDLE_ID}: 42\n", ""
        )
        with mock.patch.object(MODULE, "run", return_value=acknowledgement) as run:
            MODULE.launch_application(
                "00000000-0000-0000-0000-000000000000"
            )
        self.assertEqual(
            run.call_args.kwargs["timeout"],
            MODULE.SIMULATOR_LAUNCH_TIMEOUT_SECONDS,
        )

    def test_launch_rejects_malformed_acknowledgement(self) -> None:
        acknowledgement = subprocess.CompletedProcess(
            ["xcrun", "simctl"], 0, f"{MODULE.BUNDLE_ID}: not-a-pid\n", ""
        )
        with mock.patch.object(MODULE, "run", return_value=acknowledgement):
            with self.assertRaisesRegex(
                MODULE.RetryableSimulatorFailure, "invalid launch"
            ):
                MODULE.launch_application(
                    "00000000-0000-0000-0000-000000000000"
                )

    def test_launch_timeout_is_retryable(self) -> None:
        with mock.patch.object(
            MODULE,
            "run",
            side_effect=MODULE.SmokeFailure("launch timed out"),
        ):
            with self.assertRaisesRegex(
                MODULE.RetryableSimulatorFailure, "did not become ready"
            ):
                MODULE.launch_application(
                    "00000000-0000-0000-0000-000000000000"
                )

    def test_boot_readiness_timeout_is_retryable_before_launch(self) -> None:
        completed = subprocess.CompletedProcess(["xcrun", "simctl"], 0, "", "")
        with (
            mock.patch.object(
                MODULE,
                "run",
                side_effect=[completed, MODULE.SmokeFailure("boot timed out")],
            ) as run,
            mock.patch.object(MODULE, "launch_application") as launch,
        ):
            with self.assertRaisesRegex(
                MODULE.RetryableSimulatorFailure, "acknowledged application launch"
            ):
                MODULE.prepare_application_for_launch(
                    "00000000-0000-0000-0000-000000000000",
                    Path("AirfixDogfighter.app"),
                )
        self.assertEqual(run.call_count, 2)
        launch.assert_not_called()

    def test_install_failure_is_retryable_before_launch(self) -> None:
        completed = subprocess.CompletedProcess(["xcrun", "simctl"], 0, "", "")
        with (
            mock.patch.object(
                MODULE,
                "run",
                side_effect=[
                    completed,
                    completed,
                    MODULE.SmokeFailure("install failed"),
                ],
            ),
            mock.patch.object(MODULE, "launch_application") as launch,
        ):
            with self.assertRaises(MODULE.RetryableSimulatorFailure):
                MODULE.prepare_application_for_launch(
                    "00000000-0000-0000-0000-000000000000",
                    Path("AirfixDogfighter.app"),
                )
        launch.assert_not_called()

    def test_execute_retries_inconclusive_startup_once(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory) / "AirfixDogfighter.app"
            app.mkdir()
            output = Path(directory) / "result.json"
            with (
                mock.patch.object(MODULE, "select_runtime", return_value="runtime"),
                mock.patch.object(MODULE, "select_device_type", return_value="device"),
                mock.patch.object(
                    MODULE,
                    "execute_attempt",
                    side_effect=[MODULE.RetryableSimulatorFailure("retry"), None],
                ) as attempt,
                mock.patch("builtins.print"),
            ):
                MODULE.execute(app, output, 60)
        self.assertEqual(attempt.call_count, 2)
        self.assertEqual(attempt.call_args_list[0].args[-1], 1)
        self.assertEqual(attempt.call_args_list[1].args[-1], 2)

    def test_execute_does_not_retry_a_result_timeout(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory) / "AirfixDogfighter.app"
            app.mkdir()
            output = Path(directory) / "result.json"
            with (
                mock.patch.object(MODULE, "select_runtime", return_value="runtime"),
                mock.patch.object(MODULE, "select_device_type", return_value="device"),
                mock.patch.object(
                    MODULE,
                    "execute_attempt",
                    side_effect=MODULE.SmokeFailure(
                        "application did not produce a result"
                    ),
                ) as attempt,
            ):
                with self.assertRaisesRegex(
                    MODULE.SmokeFailure, "did not produce a result"
                ):
                    MODULE.execute(app, output, 60)
        self.assertEqual(attempt.call_count, 1)

    def test_execute_fails_after_two_inconclusive_attempts(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory) / "AirfixDogfighter.app"
            app.mkdir()
            output = Path(directory) / "result.json"
            with (
                mock.patch.object(MODULE, "select_runtime", return_value="runtime"),
                mock.patch.object(MODULE, "select_device_type", return_value="device"),
                mock.patch.object(
                    MODULE,
                    "execute_attempt",
                    side_effect=MODULE.RetryableSimulatorFailure("retry"),
                ) as attempt,
                mock.patch("builtins.print"),
            ):
                with self.assertRaisesRegex(
                    MODULE.SmokeFailure, "after 2 isolated attempts"
                ):
                    MODULE.execute(app, output, 60)
        self.assertEqual(attempt.call_count, 2)

    def test_result_path_is_resolved_from_the_current_data_container(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            container = Path(directory)
            response = subprocess.CompletedProcess(
                ["xcrun", "simctl"], 0, f"{container}\n", ""
            )
            with mock.patch.object(MODULE, "run", return_value=response):
                result_path = MODULE.application_result_path(
                    "00000000-0000-0000-0000-000000000000"
                )
        self.assertEqual(
            result_path, container / "Documents" / MODULE.RESULT_NAME
        )

    def test_result_path_rejects_a_relative_or_missing_container(self) -> None:
        response = subprocess.CompletedProcess(
            ["xcrun", "simctl"], 0, "relative/container\n", ""
        )
        with mock.patch.object(MODULE, "run", return_value=response):
            with self.assertRaisesRegex(MODULE.SmokeFailure, "invalid application"):
                MODULE.application_result_path(
                    "00000000-0000-0000-0000-000000000000"
                )

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

    def test_consume_accepts_an_atomic_result(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result_path = root / "result.json"
            output = root / "validated.json"
            write_valid_diagnostic_workspace(root)
            result_path.write_text(json.dumps(valid_result()), encoding="utf-8")
            with mock.patch("builtins.print"):
                completed = MODULE.consume_result_if_present(result_path, output)
            self.assertTrue(completed)
            self.assertTrue(output.is_file())

    def test_consume_keeps_waiting_when_result_is_absent(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            completed = MODULE.consume_result_if_present(
                root / "missing.json", root / "validated.json"
            )
            self.assertFalse(completed)

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
