#!/usr/bin/env python3
"""Synthetic tests for the private iOS signing boundary and validators."""

from __future__ import annotations

import copy
from datetime import datetime, timezone
from pathlib import Path
import plistlib
import shutil
import stat
import subprocess
import sys
import tempfile
import warnings
import zipfile


REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
TOOLS_ROOT = REPOSITORY_ROOT / "tools" / "ci"
sys.path.insert(0, str(TOOLS_ROOT))

import validate_ios_provisioning_profile as profile_validator  # noqa: E402
import verify_ios_signed_ipa as ipa_verifier  # noqa: E402


TEAM_ID = "ABCDEFGHIJ"
BUNDLE_ID = "com.example.airfix"
PROFILE_NAME = "Airfix Ad Hoc"
DEVICE_UDIDS = [
    "00008110-0011223344556677",
    "00008120-8899AABBCCDDEEFF",
]
NOW = datetime(2026, 8, 8, tzinfo=timezone.utc)


def valid_profile() -> dict[str, object]:
    return {
        "UUID": "12345678-1234-1234-1234-123456789ABC",
        "Name": PROFILE_NAME,
        "TeamIdentifier": [TEAM_ID],
        "ApplicationIdentifierPrefix": [TEAM_ID],
        "ExpirationDate": datetime(2027, 8, 8, tzinfo=timezone.utc),
        "Entitlements": {
            "application-identifier": f"{TEAM_ID}.{BUNDLE_ID}",
            "com.apple.developer.team-identifier": TEAM_ID,
            "get-task-allow": False,
        },
        "ProvisionedDevices": DEVICE_UDIDS + ["00008130-0123456789ABCDEF"],
        "DeveloperCertificates": [b"synthetic-certificate"],
    }


def expect_profile_failure(document: object, message: str) -> None:
    try:
        profile_validator.validate_profile_document(
            document,
            team_id=TEAM_ID,
            bundle_id=BUNDLE_ID,
            profile_name=PROFILE_NAME,
            device_udids=DEVICE_UDIDS,
            now=NOW,
        )
    except profile_validator.ProfileValidationFailure as error:
        if message not in str(error):
            raise AssertionError(f"unexpected profile failure: {error}") from error
    else:
        raise AssertionError(f"profile unexpectedly passed: {message}")


def test_profile_validation() -> None:
    summary = profile_validator.validate_profile_document(
        valid_profile(),
        team_id=TEAM_ID,
        bundle_id=BUNDLE_ID,
        profile_name=PROFILE_NAME,
        device_udids=DEVICE_UDIDS,
        now=NOW,
    )
    if summary["provisionedDeviceCount"] != 3:
        raise AssertionError("profile summary lost the bounded device count")
    if set(summary) != {
        "schema",
        "version",
        "profileUuid",
        "expirationUtc",
        "provisionedDeviceCount",
    }:
        raise AssertionError("profile summary schema changed unexpectedly")

    expired = copy.deepcopy(valid_profile())
    expired["ExpirationDate"] = NOW
    expect_profile_failure(expired, "expired")

    wrong_team = copy.deepcopy(valid_profile())
    wrong_team["Entitlements"]["com.apple.developer.team-identifier"] = "ZZZZZZZZZZ"  # type: ignore[index]
    expect_profile_failure(wrong_team, "entitlement team")

    wrong_bundle = copy.deepcopy(valid_profile())
    wrong_bundle["Entitlements"]["application-identifier"] = f"{TEAM_ID}.wrong.bundle"  # type: ignore[index]
    expect_profile_failure(wrong_bundle, "application identifier")

    development = copy.deepcopy(valid_profile())
    development["Entitlements"]["get-task-allow"] = True  # type: ignore[index]
    expect_profile_failure(development, "not an Ad Hoc")

    missing_device = copy.deepcopy(valid_profile())
    missing_device["ProvisionedDevices"] = [DEVICE_UDIDS[0]]
    expect_profile_failure(missing_device, "both expected devices")

    enterprise = copy.deepcopy(valid_profile())
    enterprise["ProvisionsAllDevices"] = True
    expect_profile_failure(enterprise, "enterprise")

    try:
        profile_validator.validate_expected_inputs(
            TEAM_ID,
            BUNDLE_ID,
            PROFILE_NAME,
            [DEVICE_UDIDS[0], DEVICE_UDIDS[0]],
        )
    except profile_validator.ProfileValidationFailure:
        pass
    else:
        raise AssertionError("duplicate expected device identifiers were accepted")


def populate_synthetic_app(app: Path, *, minimum_os: str = "16.4") -> None:
    app.mkdir(parents=True)
    info = {
        "CFBundleIdentifier": BUNDLE_ID,
        "CFBundleExecutable": "AirfixDogfighter",
        "MinimumOSVersion": minimum_os,
    }
    (app / "Info.plist").write_bytes(plistlib.dumps(info, sort_keys=True))
    (app / "AirfixDogfighter").write_bytes(b"synthetic-arm64-device-executable")
    (app / "default.metallib").write_bytes(b"synthetic-metallib")
    licenses = app / "third-party-licenses"
    licenses.mkdir()
    (licenses / "LodePNG.txt").write_text("synthetic license\n", encoding="utf-8")
    (app / "embedded.mobileprovision").write_bytes(b"synthetic-profile")


def write_app_archive(ipa: Path, app: Path) -> None:
    with zipfile.ZipFile(ipa, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for source in sorted(app.rglob("*")):
            if source.is_file():
                archive.write(source, source.relative_to(app.parent.parent).as_posix())


def expect_ipa_failure(operation, message: str) -> None:  # type: ignore[no-untyped-def]
    try:
        operation()
    except ipa_verifier.IpaVerificationFailure as error:
        if message not in str(error):
            raise AssertionError(f"unexpected IPA failure: {error}") from error
    else:
        raise AssertionError(f"IPA operation unexpectedly passed: {message}")


def test_bundle_and_archive_validation() -> None:
    with tempfile.TemporaryDirectory(prefix="airfix-ios-signing-tests-") as raw:
        root = Path(raw)
        app = root / "source" / "Payload" / "AirfixDogfighter.app"
        populate_synthetic_app(app)
        executable, info = ipa_verifier.verify_bundle_contents(app, bundle_id=BUNDLE_ID)
        if executable.name != "AirfixDogfighter" or info["MinimumOSVersion"] != "16.4":
            raise AssertionError("valid synthetic bundle was not retained exactly")

        ipa = root / "valid.ipa"
        write_app_archive(ipa, app)
        extracted = ipa_verifier.extract_ipa(ipa, root / "extracted")
        ipa_verifier.verify_bundle_contents(extracted, bundle_id=BUNDLE_ID)

        forbidden_app = root / "forbidden" / "AirfixDogfighter.app"
        populate_synthetic_app(forbidden_app)
        (forbidden_app / "private.afpack").write_bytes(b"synthetic")
        expect_ipa_failure(
            lambda: ipa_verifier.verify_bundle_contents(
                forbidden_app, bundle_id=BUNDLE_ID
            ),
            "forbidden file type",
        )

        simulator_app = root / "simulator" / "AirfixDogfighter.app"
        populate_synthetic_app(simulator_app)
        (simulator_app / "AirfixDogfighter").write_bytes(
            b"synthetic " + ipa_verifier.SIMULATOR_MARKER
        )
        expect_ipa_failure(
            lambda: ipa_verifier.verify_bundle_contents(
                simulator_app, bundle_id=BUNDLE_ID
            ),
            "simulator smoke marker",
        )

        wrong_minimum = root / "minimum" / "AirfixDogfighter.app"
        populate_synthetic_app(wrong_minimum, minimum_os="17.0")
        expect_ipa_failure(
            lambda: ipa_verifier.verify_bundle_contents(
                wrong_minimum, bundle_id=BUNDLE_ID
            ),
            "minimum OS",
        )

        bad_package_info = root / "pkginfo" / "AirfixDogfighter.app"
        populate_synthetic_app(bad_package_info)
        (bad_package_info / "PkgInfo").write_bytes(b"private")
        expect_ipa_failure(
            lambda: ipa_verifier.verify_bundle_contents(
                bad_package_info, bundle_id=BUNDLE_ID
            ),
            "PkgInfo",
        )

        traversal = root / "traversal.ipa"
        write_app_archive(traversal, app)
        with zipfile.ZipFile(traversal, "a") as archive:
            archive.writestr("../escape", b"synthetic")
        expect_ipa_failure(
            lambda: ipa_verifier.extract_ipa(traversal, root / "traversal-out"),
            "unsafe archive path",
        )

        symlink = root / "symlink.ipa"
        write_app_archive(symlink, app)
        link = zipfile.ZipInfo("Payload/AirfixDogfighter.app/unsafe-link")
        link.create_system = 3
        link.external_attr = (stat.S_IFLNK | 0o777) << 16
        with zipfile.ZipFile(symlink, "a") as archive:
            archive.writestr(link, b"outside")
        expect_ipa_failure(
            lambda: ipa_verifier.extract_ipa(symlink, root / "symlink-out"),
            "non-regular archive member",
        )

        extra_app = root / "extra-app.ipa"
        write_app_archive(extra_app, app)
        with zipfile.ZipFile(extra_app, "a") as archive:
            archive.writestr("Payload/Unexpected.app/Info.plist", b"synthetic")
        expect_ipa_failure(
            lambda: ipa_verifier.extract_ipa(extra_app, root / "extra-app-out"),
            "unexpected archive member",
        )

        duplicate = root / "duplicate.ipa"
        write_app_archive(duplicate, app)
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", UserWarning)
            with zipfile.ZipFile(duplicate, "a") as archive:
                archive.writestr(
                    "Payload/AirfixDogfighter.app/Info.plist",
                    b"synthetic-duplicate",
                )
        expect_ipa_failure(
            lambda: ipa_verifier.extract_ipa(duplicate, root / "duplicate-out"),
            "duplicate archive member",
        )

        private_member = root / "private-member.ipa"
        write_app_archive(private_member, app)
        with zipfile.ZipFile(private_member, "a") as archive:
            archive.writestr("Payload/private-textures/derived.txt", b"synthetic")
        expect_ipa_failure(
            lambda: ipa_verifier.extract_ipa(
                private_member, root / "private-member-out"
            ),
            "forbidden payload member",
        )

        leaked_path = root / "leaked-path.ipa"
        leaked_app = root / "leaked" / "Payload" / "AirfixDogfighter.app"
        populate_synthetic_app(leaked_app)
        (leaked_app / "default.metallib").write_bytes(
            b"/Users/synthetic/roms/Airfix/private.file"
        )
        write_app_archive(leaked_path, leaked_app)
        expect_ipa_failure(
            lambda: ipa_verifier.extract_ipa(leaked_path, root / "leaked-path-out"),
            "private source path",
        )

        arbitrary_outside = root / "arbitrary-outside.ipa"
        write_app_archive(arbitrary_outside, app)
        with zipfile.ZipFile(arbitrary_outside, "a") as archive:
            archive.writestr("Payload/unverified-private-notes.txt", b"synthetic")
        expect_ipa_failure(
            lambda: ipa_verifier.extract_ipa(
                arbitrary_outside, root / "arbitrary-outside-out"
            ),
            "unexpected archive member",
        )

        arbitrary_inside = root / "arbitrary-inside.ipa"
        write_app_archive(arbitrary_inside, app)
        with zipfile.ZipFile(arbitrary_inside, "a") as archive:
            archive.writestr(
                "Payload/AirfixDogfighter.app/unverified-notes.txt",
                b"synthetic",
            )
        expect_ipa_failure(
            lambda: ipa_verifier.extract_ipa(
                arbitrary_inside, root / "arbitrary-inside-out"
            ),
            "unexpected archive member",
        )


def test_signature_authorization_helpers() -> None:
    ipa_verifier.require_signing_certificate_authorized(
        b"synthetic-leaf", [b"other", b"synthetic-leaf"]
    )
    expect_ipa_failure(
        lambda: ipa_verifier.require_signing_certificate_authorized(
            b"synthetic-leaf", [b"other"]
        ),
        "not authorized by the profile",
    )

    profile_entitlements = {
        "application-identifier": f"{TEAM_ID}.*",
        "com.apple.developer.team-identifier": TEAM_ID,
        "get-task-allow": False,
        "keychain-access-groups": [f"{TEAM_ID}.*"],
    }
    signed_entitlements = {
        "application-identifier": f"{TEAM_ID}.{BUNDLE_ID}",
        "com.apple.developer.team-identifier": TEAM_ID,
        "get-task-allow": False,
        "keychain-access-groups": [f"{TEAM_ID}.{BUNDLE_ID}"],
    }
    ipa_verifier.require_profile_authorizes_entitlements(
        signed_entitlements, profile_entitlements
    )
    unauthorized = dict(signed_entitlements)
    unauthorized["aps-environment"] = "production"
    expect_ipa_failure(
        lambda: ipa_verifier.require_profile_authorizes_entitlements(
            unauthorized, profile_entitlements
        ),
        "not authorized by the provisioning profile",
    )


def require_text(path: Path, fragments: tuple[str, ...]) -> str:
    text = path.read_text(encoding="utf-8")
    for fragment in fragments:
        if fragment not in text:
            raise AssertionError(f"{path.name} is missing required policy: {fragment}")
    return text


def test_workflow_boundary() -> None:
    workflow = require_text(
        REPOSITORY_ROOT / ".github" / "workflows" / "ios-private-ipa.yml",
        (
            "workflow_dispatch:",
            'AIRFIX_REPOSITORY_PRIVATE: ${{ github.event.repository.private }}',
            'if [[ "$AIRFIX_REPOSITORY_PRIVATE" != "true" ]]',
            'if [[ "$GITHUB_REF" != "refs/heads/main" ]]',
            "name: ios-private",
            "persist-credentials: false",
            "retention-days: 1",
            "if-no-files-found: error",
            "BUILD_CERTIFICATE_BASE64: ${{ secrets.BUILD_CERTIFICATE_BASE64 }}",
            "BUILD_PROVISION_PROFILE_BASE64: ${{ secrets.BUILD_PROVISION_PROFILE_BASE64 }}",
        ),
    )
    for forbidden_trigger in ("pull_request:", "push:", "schedule:"):
        if forbidden_trigger in workflow:
            raise AssertionError(f"private signing has forbidden trigger {forbidden_trigger}")
    if "actions/checkout@de0fac2e4500dabe0009e67214ff5f5447ce83dd" not in workflow:
        raise AssertionError("private workflow checkout action is not pinned")
    if "actions/upload-artifact@043fb46d1a93c77aae656e7c1c64a875d1fc6a0a" not in workflow:
        raise AssertionError("private workflow upload action is not pinned")

    script = require_text(
        REPOSITORY_ROOT / "tools" / "ci" / "build_ios_private_ipa.sh",
        (
            'if [[ "$AIRFIX_REPOSITORY_PRIVATE" != "true" ]]',
            'if [[ "${GITHUB_REF:-}" != "refs/heads/main" ]]',
            'if [[ "$AIRFIX_IOS_SIGNING_IDENTITY" != "Apple Distribution" ]]',
            "-DAIRFIX_IOS_ENABLE_SIGNING=ON",
            "-DAIRFIX_IOS_SIMULATOR_SMOKE=OFF",
            '"method": "ad-hoc"',
            "verify_ios_signed_ipa.py",
            '--runner-image "$ImageOS/$ImageVersion"',
        ),
    )
    if "AIRFIX_IOS_ENABLE_SIGNING=ON" not in script:
        raise AssertionError("private builder does not enable explicit signing")

    root_cmake = require_text(
        REPOSITORY_ROOT / "CMakeLists.txt",
        ('option(AIRFIX_IOS_ENABLE_SIGNING', '"Allow explicit manual code signing'),
    )
    if '"Allow explicit manual code signing for a private iphoneos build" OFF)' not in root_cmake:
        raise AssertionError("iOS signing must remain disabled by default")


def test_cmake_signing_gate() -> None:
    compiler = shutil.which("g++") or shutil.which("clang++")
    if compiler is None:
        raise AssertionError("no C++ compiler is available for the CMake gate test")
    with tempfile.TemporaryDirectory(prefix="airfix-ios-signing-cmake-") as raw:
        result = subprocess.run(
            [
                "cmake",
                "-S",
                str(REPOSITORY_ROOT),
                "-B",
                raw,
                "-G",
                "Ninja",
                f"-DCMAKE_CXX_COMPILER={compiler}",
                "-DCMAKE_CXX_COMPILER_WORKS=TRUE",
                "-DCMAKE_CXX_COMPILER_FORCED=TRUE",
                "-DAIRFIX_BUILD_IOS_APP=OFF",
                "-DAIRFIX_IOS_ENABLE_SIGNING=ON",
            ],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=30,
        )
        if result.returncode == 0:
            raise AssertionError("CMake accepted signing without the iOS application")
        expected = "AIRFIX_IOS_ENABLE_SIGNING requires AIRFIX_BUILD_IOS_APP=ON"
        if expected not in result.stdout:
            raise AssertionError("CMake signing failure was not the fail-closed gate")


def main() -> int:
    test_profile_validation()
    test_bundle_and_archive_validation()
    test_signature_authorization_helpers()
    test_workflow_boundary()
    test_cmake_signing_gate()
    print("iOS private signing tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
