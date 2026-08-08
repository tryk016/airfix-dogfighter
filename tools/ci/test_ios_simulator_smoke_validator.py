#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
from pathlib import Path
import unittest


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
