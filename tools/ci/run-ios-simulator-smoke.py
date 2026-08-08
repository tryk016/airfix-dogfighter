#!/usr/bin/env python3
"""Boot and validate the public data-less iOS simulator smoke application."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import uuid

RESULT_NAME = "airfix-ios-simulator-smoke-v1.json"
BUNDLE_ID = "com.tryk016.airfixdogfighter"
EXPECTED_KEYS = {"schema", "version", "status", "dataLess", "metalFrame", "lifecycle"}


class SmokeFailure(RuntimeError):
    pass


def run(*args: str, check: bool = True, timeout: int = 60) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        args, check=False, capture_output=True, text=True, timeout=timeout
    )
    if check and result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        raise SmokeFailure(f"command failed ({result.returncode}): {args[0]} {args[1]}: {detail}")
    return result


def require_bool(value: object, name: str) -> None:
    if type(value) is not bool:  # bool must not silently accept 0/1.
        raise SmokeFailure(f"{name} must be a JSON boolean")


def validate_result(document: object) -> dict[str, object]:
    if not isinstance(document, dict) or set(document) != EXPECTED_KEYS:
        raise SmokeFailure("result has an unexpected top-level schema")
    if document["schema"] != "airfix.ios-simulator-smoke" or document["version"] != 1:
        raise SmokeFailure("result schema or version is unsupported")
    if document["status"] != "pass":
        raise SmokeFailure("application reported a failed smoke result")
    require_bool(document["dataLess"], "dataLess")
    if document["dataLess"] is not True:
        raise SmokeFailure("simulator smoke was not data-less")

    frame = document["metalFrame"]
    if not isinstance(frame, dict) or set(frame) != {
        "commandBufferCompleted", "publicSyntheticScene", "sceneDrawCalls", "sceneTriangles"
    }:
        raise SmokeFailure("Metal frame result has an unexpected schema")
    require_bool(frame["commandBufferCompleted"], "metalFrame.commandBufferCompleted")
    require_bool(frame["publicSyntheticScene"], "metalFrame.publicSyntheticScene")
    if frame["commandBufferCompleted"] is not True or frame["publicSyntheticScene"] is not True:
        raise SmokeFailure("a completed public synthetic Metal frame was not proven")
    for name in ("sceneDrawCalls", "sceneTriangles"):
        value = frame[name]
        if type(value) is not int or value <= 0 or value > 1_000_000:
            raise SmokeFailure(f"metalFrame.{name} is outside the accepted bound")

    lifecycle = document["lifecycle"]
    expected_lifecycle_keys = {
        "sequence", "inactivePaused", "backgroundPaused", "foregroundPaused",
        "activeStillPaused", "autoResumed"
    }
    if not isinstance(lifecycle, dict) or set(lifecycle) != expected_lifecycle_keys:
        raise SmokeFailure("lifecycle result has an unexpected schema")
    if lifecycle["sequence"] != ["resign-active", "background", "foreground", "active"]:
        raise SmokeFailure("lifecycle sequence changed")
    for name in ("inactivePaused", "backgroundPaused", "foregroundPaused", "activeStillPaused"):
        require_bool(lifecycle[name], f"lifecycle.{name}")
        if lifecycle[name] is not True:
            raise SmokeFailure(f"lifecycle.{name} was not proven")
    require_bool(lifecycle["autoResumed"], "lifecycle.autoResumed")
    if lifecycle["autoResumed"] is not False:
        raise SmokeFailure("foreground transition auto-resumed gameplay")
    return document


def load_json(path: Path) -> object:
    if not path.is_file() or path.stat().st_size > 64 * 1024:
        raise SmokeFailure("result is missing, not a file, or exceeds 64 KiB")
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise SmokeFailure(f"result is not valid UTF-8 JSON: {error}") from error


def select_runtime_from_payload(payload: object) -> str:
    if not isinstance(payload, dict) or not isinstance(payload.get("runtimes"), list):
        raise SmokeFailure("simctl returned an invalid runtime catalogue")
    candidates = [
        runtime for runtime in payload.get("runtimes", [])
        if isinstance(runtime, dict)
        and runtime.get("isAvailable") is True
        and isinstance(runtime.get("identifier"), str)
        and runtime["identifier"].startswith(
            "com.apple.CoreSimulator.SimRuntime.iOS-"
        )
    ]
    if not candidates:
        raise SmokeFailure("no available iOS Simulator runtime")
    def version_key(item: dict[str, object]) -> tuple[int, ...]:
        version = item.get("version", "0")
        if not isinstance(version, str):
            return (0,)
        try:
            return tuple(int(component) for component in version.split("."))
        except ValueError:
            return (0,)

    candidates.sort(key=version_key, reverse=True)
    return candidates[0]["identifier"]


def select_runtime() -> str:
    payload = json.loads(run("xcrun", "simctl", "list", "runtimes", "--json").stdout)
    return select_runtime_from_payload(payload)


def select_device_type() -> str:
    payload = json.loads(run("xcrun", "simctl", "list", "devicetypes", "--json").stdout)
    devices = payload.get("devicetypes", [])
    preferred = ("iPhone 17 Pro", "iPhone 16 Pro", "iPhone 15 Pro", "iPhone SE (3rd generation)")
    for name in preferred:
        for device in devices:
            if device.get("name") == name and isinstance(device.get("identifier"), str):
                return device["identifier"]
    raise SmokeFailure("no supported iPhone Simulator device type")


def redacted_failure_log(device: str) -> str:
    result = run(
        "xcrun", "simctl", "spawn", device, "log", "show", "--last", "2m",
        "--style", "compact", "--predicate", 'process == "AirfixDogfighter"',
        check=False, timeout=30,
    )
    text = (result.stdout or result.stderr)[-32_000:]
    text = re.sub(r"/Users/[^/\s]+/", "/Users/<redacted>/", text)
    text = re.sub(r"(?i)[A-Z]:[\\/][^\r\n\t ]+", "<redacted-path>", text)
    return text


def execute(app: Path, output: Path, timeout_seconds: int) -> None:
    app = app.resolve(strict=True)
    if not app.is_dir() or app.suffix != ".app":
        raise SmokeFailure("--app must name an existing .app bundle")
    runtime = select_runtime()
    device_type = select_device_type()
    device_name = f"AirfixSimulatorSmoke-{os.getpid()}-{uuid.uuid4().hex[:8]}"
    device = ""
    try:
        device = run("xcrun", "simctl", "create", device_name, device_type, runtime).stdout.strip()
        if not re.fullmatch(r"[0-9A-Fa-f-]{36}", device):
            raise SmokeFailure("simctl returned an invalid device identifier")
        run("xcrun", "simctl", "boot", device)
        run("xcrun", "simctl", "bootstatus", device, "-b", timeout=180)
        run("xcrun", "simctl", "install", device, str(app), timeout=90)
        data_container = Path(
            run("xcrun", "simctl", "get_app_container", device, BUNDLE_ID, "data").stdout.strip()
        )
        result_path = data_container / "Documents" / RESULT_NAME
        launch = run("xcrun", "simctl", "launch", "--terminate-running-process", device, BUNDLE_ID)
        match = re.search(r":\s*([0-9]+)\s*$", launch.stdout)
        if match is None:
            raise SmokeFailure("simctl launch did not return an application PID")
        pid = match.group(1)

        deadline = time.monotonic() + timeout_seconds
        while time.monotonic() < deadline:
            if result_path.is_file():
                document = validate_result(load_json(result_path))
                output.parent.mkdir(parents=True, exist_ok=True)
                output.write_text(json.dumps(document, sort_keys=True, indent=2) + "\n", encoding="utf-8")
                print(json.dumps(document, sort_keys=True))
                return
            process = run(
                "xcrun", "simctl", "spawn", device, "/bin/ps", "-p", pid, "-o", "pid=",
                check=False, timeout=10,
            )
            if process.returncode != 0 or pid not in process.stdout.split():
                raise SmokeFailure("application exited before producing a result")
            time.sleep(1.0)
        raise SmokeFailure(f"application did not produce a result within {timeout_seconds} seconds")
    except Exception:
        if device:
            log = redacted_failure_log(device)
            if log:
                print("--- redacted AirfixDogfighter simulator log ---", file=sys.stderr)
                print(log, file=sys.stderr)
        raise
    finally:
        if device:
            run("xcrun", "simctl", "shutdown", device, check=False, timeout=30)
            run("xcrun", "simctl", "delete", device, check=False, timeout=30)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app", type=Path)
    parser.add_argument("--result-output", type=Path)
    parser.add_argument("--timeout-seconds", type=int, default=45)
    parser.add_argument("--validate-only", type=Path)
    args = parser.parse_args()
    try:
        if args.validate_only is not None:
            validate_result(load_json(args.validate_only))
            return 0
        if args.app is None or args.result_output is None:
            parser.error("--app and --result-output are required unless --validate-only is used")
        if not 5 <= args.timeout_seconds <= 120:
            raise SmokeFailure("timeout must be in the range 5..120 seconds")
        execute(args.app, args.result_output, args.timeout_seconds)
        return 0
    except SmokeFailure as error:
        print(f"iOS simulator smoke failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
