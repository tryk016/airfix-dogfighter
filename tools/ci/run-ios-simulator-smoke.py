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
import tempfile
import time
import uuid

RESULT_NAME = "airfix-ios-simulator-smoke-v1.json"
BUNDLE_ID = "com.tryk016.airfixdogfighter"
PASS_KEYS = {"schema", "version", "status", "dataLess", "metalFrame", "lifecycle"}
FAILURE_KEYS = {"schema", "version", "status", "failureStage"}
FAILURE_STAGES = {
    "invalid-data-less-startup-state",
    "draw-not-entered",
    "draw-entered",
    "invalid-thread",
    "invalid-view",
    "awaiting-presentation",
    "missing-scene-sampling",
    "missing-sampler",
    "missing-snapshot",
    "missing-resources",
    "missing-diagnostics",
    "missing-gameplay-camera",
    "missing-fallback",
    "missing-drawable",
    "output-extent-mismatch",
    "invalid-layout",
    "render-target-mismatch",
    "missing-scaled-target",
    "missing-command-buffer",
    "missing-scene-encoder",
    "missing-presentation-encoder",
    "submitted-without-completion",
    "metal-frame-failed",
    "lifecycle-invariant-failed",
    "renderer-initialization-timeout",
    "draw-call-timeout",
    "smoke-sequence-timeout",
    "unknown-draw-stage",
}
MAX_DIAGNOSTIC_BYTES = 32_000


class SmokeFailure(RuntimeError):
    pass


def run(*args: str, check: bool = True, timeout: int = 60) -> subprocess.CompletedProcess[str]:
    try:
        result = subprocess.run(
            args, check=False, capture_output=True, text=True, timeout=timeout
        )
    except subprocess.TimeoutExpired as error:
        command = " ".join(args[:2])
        raise SmokeFailure(
            f"command timed out after {timeout} seconds: {command}"
        ) from error
    except OSError as error:
        command = " ".join(args[:2])
        raise SmokeFailure(f"unable to execute command: {command}") from error
    if check and result.returncode != 0:
        detail = redact_diagnostic_text(result.stderr or result.stdout)
        raise SmokeFailure(f"command failed ({result.returncode}): {args[0]} {args[1]}: {detail}")
    return result


def run_non_masking(*args: str, timeout: int) -> None:
    try:
        run(*args, check=False, timeout=timeout)
    except (SmokeFailure, OSError):
        # Diagnostics and cleanup must never replace the primary failure.
        return


def redact_diagnostic_text(text: str, sensitive_paths: tuple[str, ...] = ()) -> str:
    text = text.replace("\x00", "")
    text = re.sub(r"\x1B\[[0-?]*[ -/]*[@-~]", "", text)
    for path in sorted((value for value in sensitive_paths if value), key=len, reverse=True):
        text = text.replace(path, "<redacted-path>")
    workspace = os.environ.get("GITHUB_WORKSPACE", "")
    if workspace:
        text = text.replace(workspace, "<redacted-workspace>")
    text = re.sub(r"/Users/[^/\s]+/", "/Users/<redacted>/", text)
    text = re.sub(r"(?i)[A-Z]:[\\/][^\r\n\t ]+", "<redacted-path>", text)
    encoded = text.encode("utf-8", errors="replace")
    if len(encoded) > MAX_DIAGNOSTIC_BYTES:
        prefix = b"[truncated to final bytes]\n"
        encoded = encoded[-(MAX_DIAGNOSTIC_BYTES - len(prefix)):]
        text = prefix.decode("ascii") + encoded.decode(
            "utf-8", errors="replace"
        )
    return text.strip()


def require_bool(value: object, name: str) -> None:
    if type(value) is not bool:  # bool must not silently accept 0/1.
        raise SmokeFailure(f"{name} must be a JSON boolean")


def validate_result(document: object) -> dict[str, object]:
    if not isinstance(document, dict):
        raise SmokeFailure("result has an unexpected top-level schema")
    if document.get("schema") != "airfix.ios-simulator-smoke" or document.get("version") != 1:
        raise SmokeFailure("result schema or version is unsupported")
    if document.get("status") == "fail":
        if set(document) != FAILURE_KEYS:
            raise SmokeFailure("failure result has an unexpected top-level schema")
        failure_stage = document.get("failureStage")
        if not isinstance(failure_stage, str) or failure_stage not in FAILURE_STAGES:
            raise SmokeFailure("application reported an unknown failure stage")
        raise SmokeFailure(
            f"application reported simulator failure stage: {failure_stage}"
        )
    if document.get("status") != "pass" or set(document) != PASS_KEYS:
        raise SmokeFailure("result has an unexpected top-level schema")
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


def redacted_failure_log(device: str, sensitive_paths: tuple[str, ...]) -> str:
    try:
        result = run(
            "xcrun", "simctl", "spawn", device, "log", "show", "--last", "2m",
            "--style", "compact", "--predicate", 'process == "AirfixDogfighter"',
            check=False, timeout=20,
        )
    except (SmokeFailure, OSError):
        return ""
    return redact_diagnostic_text(result.stdout or result.stderr, sensitive_paths)


def simulator_launch_command(device: str) -> tuple[str, ...]:
    return (
        "xcrun", "simctl", "launch", "--console",
        "--terminate-running-process", device, BUNDLE_ID,
    )


def start_supervised_launch(device: str) -> subprocess.Popen[bytes]:
    try:
        return subprocess.Popen(
            simulator_launch_command(device),
            stdin=subprocess.DEVNULL,
            # CoreSimulator may leave these descriptors inherited by the
            # detached application after simctl itself exits.  Pipes would
            # then have no bounded EOF and could deadlock failure cleanup.
            # The strict result file is the success oracle; bounded unified
            # logging below remains the diagnostic channel.
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            bufsize=0,
        )
    except OSError as error:
        raise SmokeFailure("unable to start supervised simctl launch") from error


def stop_supervised_launch(process: subprocess.Popen[bytes] | None) -> None:
    if process is None:
        return
    try:
        if process.poll() is None:
            process.terminate()
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        try:
            process.kill()
            process.wait(timeout=5)
        except (OSError, subprocess.TimeoutExpired):
            return
    except OSError:
        return


def consume_result_if_present(result_path: Path, output: Path) -> bool:
    if not result_path.is_file():
        return False
    document = validate_result(load_json(result_path))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(document, sort_keys=True, indent=2) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(document, sort_keys=True))
    return True


def poll_supervised_result(
    launcher: subprocess.Popen[bytes], result_path: Path, output: Path
) -> tuple[bool, int | None]:
    if consume_result_if_present(result_path, output):
        return (True, None)
    return_code = launcher.poll()
    if return_code is None:
        return (False, None)
    # Close the atomic publication-vs-exit race before declaring failure.
    if consume_result_if_present(result_path, output):
        return (True, None)
    # CoreSimulator may return successfully as soon as the application has
    # launched, even with --console.  A zero status therefore acknowledges a
    # detached launch; the strict result document remains the success oracle.
    if return_code == 0:
        return (False, None)
    return (False, return_code)


def execute(app: Path, output: Path, timeout_seconds: int) -> None:
    app = app.resolve(strict=True)
    if not app.is_dir() or app.suffix != ".app":
        raise SmokeFailure("--app must name an existing .app bundle")
    runtime = select_runtime()
    device_type = select_device_type()
    device_name = f"AirfixSimulatorSmoke-{os.getpid()}-{uuid.uuid4().hex[:8]}"
    device = ""
    with tempfile.TemporaryDirectory(prefix="airfix-ios-smoke-") as capture_root:
        capture_directory = Path(capture_root)
        sensitive_paths = (str(app), str(capture_directory))
        launcher: subprocess.Popen[bytes] | None = None
        try:
            device = run(
                "xcrun", "simctl", "create", device_name, device_type, runtime
            ).stdout.strip()
            if not re.fullmatch(r"[0-9A-Fa-f-]{36}", device):
                raise SmokeFailure("simctl returned an invalid device identifier")
            run("xcrun", "simctl", "boot", device)
            run("xcrun", "simctl", "bootstatus", device, "-b", timeout=180)
            run("xcrun", "simctl", "install", device, str(app), timeout=90)
            data_container = Path(
                run(
                    "xcrun", "simctl", "get_app_container", device, BUNDLE_ID,
                    "data",
                ).stdout.strip()
            )
            result_path = data_container / "Documents" / RESULT_NAME
            launcher = start_supervised_launch(device)

            deadline = time.monotonic() + timeout_seconds
            while time.monotonic() < deadline:
                completed, return_code = poll_supervised_result(
                    launcher, result_path, output
                )
                if completed:
                    return
                if return_code is not None:
                    raise SmokeFailure(
                        "supervised simctl launch exited before producing a "
                        f"result (exit code {return_code})"
                    )
                time.sleep(1.0)
            raise SmokeFailure(
                f"application did not produce a result within "
                f"{timeout_seconds} seconds"
            )
        except Exception:
            stop_supervised_launch(launcher)
            if device:
                log = redacted_failure_log(device, sensitive_paths)
                if log:
                    print(
                        "--- redacted AirfixDogfighter simulator log ---",
                        file=sys.stderr,
                    )
                    print(log, file=sys.stderr)
            raise
        finally:
            stop_supervised_launch(launcher)
            if device:
                run_non_masking(
                    "xcrun", "simctl", "shutdown", device, timeout=20
                )
                run_non_masking(
                    "xcrun", "simctl", "delete", device, timeout=20
                )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app", type=Path)
    parser.add_argument("--result-output", type=Path)
    parser.add_argument("--timeout-seconds", type=int, default=60)
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
