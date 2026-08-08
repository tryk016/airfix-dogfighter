#!/usr/bin/env python3
"""Verify a private Ad Hoc IPA and emit a path-free build manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import plistlib
import re
import stat
import subprocess
import sys
import tempfile
import zipfile

from validate_ios_provisioning_profile import (
    ProfileValidationFailure,
    validate_profile_document,
)


MAX_IPA_BYTES = 512 * 1024 * 1024
MAX_ENTRY_COUNT = 4096
MAX_UNCOMPRESSED_BYTES = 1024 * 1024 * 1024
SOURCE_SHA_PATTERN = re.compile(r"^[0-9a-f]{40}$")
RUN_ID_PATTERN = re.compile(r"^[1-9][0-9]*$")
RUNNER_IMAGE_PATTERN = re.compile(r"^[A-Za-z0-9._/-]{1,128}$")
BLOCKED_SUFFIXES = {
    ".afpack",
    ".up",
    ".gti",
    ".ccf",
    ".object",
    ".level",
    ".world",
    ".afs",
    ".brf",
    ".icd",
    ".exe",
    ".dll",
    ".mode",
    ".type",
    ".p12",
    ".cer",
    ".mobileprovision",
    ".provisionprofile",
}
BLOCKED_ARCHIVE_DIRECTORY_NAMES = {
    "original",
    "private-textures",
    "roms",
    "signing",
}
PRIVATE_PATH_PATTERN = re.compile(
    rb"(?:[A-Za-z]:[\\/]|/Users/[^/\x00]+/|/home/[^/\x00]+/)"
    rb"[^\x00\r\n]{0,384}(?:roms|private-textures)[\\/]",
    re.IGNORECASE,
)
SIMULATOR_MARKER = b"airfix.ios-simulator-smoke"
APP_ROOT = PurePosixPath("Payload/AirfixDogfighter.app")
ALLOWED_APP_FILES = {
    APP_ROOT / "AirfixDogfighter",
    APP_ROOT / "Info.plist",
    APP_ROOT / "PkgInfo",
    APP_ROOT / "default.metallib",
    APP_ROOT / "embedded.mobileprovision",
    APP_ROOT / "third-party-licenses/LodePNG.txt",
    APP_ROOT / "_CodeSignature/CodeResources",
}
ALLOWED_APP_DIRECTORIES = {
    PurePosixPath(*file.parts[:length])
    for file in ALLOWED_APP_FILES
    for length in range(1, len(file.parts))
}


class IpaVerificationFailure(RuntimeError):
    pass


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while block := source.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def validate_archive_member(info: zipfile.ZipInfo) -> PurePosixPath:
    name = info.filename
    if not name or "\\" in name or "\x00" in name:
        raise IpaVerificationFailure("IPA contains an invalid archive member")
    path = PurePosixPath(name)
    if path.is_absolute() or any(part in ("", ".", "..") for part in path.parts):
        raise IpaVerificationFailure("IPA contains an unsafe archive path")
    unix_mode = info.external_attr >> 16
    file_type = stat.S_IFMT(unix_mode)
    if file_type not in (0, stat.S_IFREG, stat.S_IFDIR):
        raise IpaVerificationFailure("IPA contains a non-regular archive member")
    if info.flag_bits & 0x1:
        raise IpaVerificationFailure("IPA contains an encrypted archive member")
    lowered_parts = tuple(part.lower() for part in path.parts)
    allowed_profile = path == APP_ROOT / "embedded.mobileprovision"
    if (
        path.suffix.lower() in BLOCKED_SUFFIXES
        and not allowed_profile
    ) or any(part in BLOCKED_ARCHIVE_DIRECTORY_NAMES for part in lowered_parts):
        raise IpaVerificationFailure("IPA contains a forbidden payload member")
    if info.is_dir():
        if path not in ALLOWED_APP_DIRECTORIES:
            raise IpaVerificationFailure("IPA contains an unexpected directory")
    elif path not in ALLOWED_APP_FILES:
        raise IpaVerificationFailure("IPA contains an unexpected archive member")
    return path


def extract_ipa(ipa: Path, destination: Path) -> Path:
    try:
        if not ipa.is_file() or ipa.stat().st_size <= 0 or ipa.stat().st_size > MAX_IPA_BYTES:
            raise IpaVerificationFailure("IPA is missing, empty, or oversized")
        with zipfile.ZipFile(ipa) as archive:
            members = archive.infolist()
            if not members or len(members) > MAX_ENTRY_COUNT:
                raise IpaVerificationFailure("IPA entry count is outside policy")
            total_uncompressed = 0
            validated: list[tuple[zipfile.ZipInfo, PurePosixPath]] = []
            normalized_members: set[str] = set()
            for info in members:
                path = validate_archive_member(info)
                normalized = path.as_posix().casefold()
                if normalized in normalized_members:
                    raise IpaVerificationFailure("IPA contains a duplicate archive member")
                normalized_members.add(normalized)
                total_uncompressed += info.file_size
                if total_uncompressed > MAX_UNCOMPRESSED_BYTES:
                    raise IpaVerificationFailure("IPA expands beyond the size limit")
                validated.append((info, path))

            app_roots = {
                PurePosixPath(*path.parts[:2])
                for _, path in validated
                if len(path.parts) >= 2
                and path.parts[0] == "Payload"
                and path.parts[1].endswith(".app")
            }
            if app_roots != {APP_ROOT}:
                raise IpaVerificationFailure("IPA must contain exactly one expected app")

            resolved_destination = destination.resolve()
            for info, path in validated:
                target = destination.joinpath(*path.parts)
                resolved_target = target.resolve()
                if resolved_destination not in resolved_target.parents and resolved_target != resolved_destination:
                    raise IpaVerificationFailure("IPA extraction escaped its destination")
                if info.is_dir():
                    target.mkdir(parents=True, exist_ok=True)
                    continue
                target.parent.mkdir(parents=True, exist_ok=True)
                with archive.open(info) as source, target.open("wb") as output:
                    overlap = b""
                    while block := source.read(1024 * 1024):
                        if PRIVATE_PATH_PATTERN.search(overlap + block):
                            raise IpaVerificationFailure(
                                "private source path leaked into signed IPA"
                            )
                        output.write(block)
                        overlap = (overlap + block)[-1024:]
    except IpaVerificationFailure:
        raise
    except (OSError, zipfile.BadZipFile, RuntimeError) as error:
        raise IpaVerificationFailure("IPA archive is malformed") from error
    return destination / "Payload" / "AirfixDogfighter.app"


def run_command(*args: str, output_path: Path | None = None) -> bytes:
    try:
        result = subprocess.run(
            args,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=120,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise IpaVerificationFailure("required Apple verification tool failed") from error
    if result.returncode != 0:
        raise IpaVerificationFailure("signed IPA failed an Apple verification tool")
    if output_path is not None:
        try:
            output_path.write_bytes(result.stdout)
        except OSError as error:
            raise IpaVerificationFailure("could not retain verification output") from error
    return result.stdout or result.stderr


def load_plist(path: Path, field: str) -> dict[str, object]:
    try:
        value = plistlib.loads(path.read_bytes())
    except (OSError, plistlib.InvalidFileException, ValueError) as error:
        raise IpaVerificationFailure(f"{field} is malformed") from error
    if not isinstance(value, dict):
        raise IpaVerificationFailure(f"{field} is not a dictionary")
    return value


def load_plist_bytes(value: bytes, field: str) -> dict[str, object]:
    try:
        document = plistlib.loads(value)
    except (plistlib.InvalidFileException, ValueError) as error:
        raise IpaVerificationFailure(f"{field} is malformed") from error
    if not isinstance(document, dict):
        raise IpaVerificationFailure(f"{field} is not a dictionary")
    return document


def entitlement_value_is_authorized(signed: object, permitted: object) -> bool:
    if signed == permitted:
        return True
    if isinstance(signed, str) and isinstance(permitted, str):
        return permitted.endswith("*") and signed.startswith(permitted[:-1])
    if isinstance(signed, list) and isinstance(permitted, list):
        return all(
            any(entitlement_value_is_authorized(value, candidate) for candidate in permitted)
            for value in signed
        )
    if isinstance(signed, dict) and isinstance(permitted, dict):
        return all(
            key in permitted
            and entitlement_value_is_authorized(value, permitted[key])
            for key, value in signed.items()
        )
    return False


def require_profile_authorizes_entitlements(
    signed: dict[str, object], profile: dict[str, object]
) -> None:
    for key, value in signed.items():
        if key not in profile or not entitlement_value_is_authorized(
            value, profile[key]
        ):
            raise IpaVerificationFailure(
                "signed entitlement is not authorized by the provisioning profile"
            )


def require_signing_certificate_authorized(
    leaf_certificate: bytes, authorized_certificates: list[bytes]
) -> None:
    leaf_digest = hashlib.sha256(leaf_certificate).digest()
    authorized_digests = {
        hashlib.sha256(certificate).digest()
        for certificate in authorized_certificates
        if certificate
    }
    if leaf_digest not in authorized_digests:
        raise IpaVerificationFailure(
            "application signing certificate is not authorized by the profile"
        )


def verify_bundle_contents(app: Path, *, bundle_id: str) -> tuple[Path, dict[str, object]]:
    plist = load_plist(app / "Info.plist", "application Info.plist")
    if plist.get("CFBundleIdentifier") != bundle_id:
        raise IpaVerificationFailure("bundle identifier does not match")
    if plist.get("MinimumOSVersion") not in ("16.4", "16.4.0"):
        raise IpaVerificationFailure("bundle minimum OS is not 16.4")
    executable_name = plist.get("CFBundleExecutable")
    if not isinstance(executable_name, str) or not executable_name:
        raise IpaVerificationFailure("bundle executable name is invalid")
    executable = app / executable_name
    required_files = (
        executable,
        app / "default.metallib",
        app / "third-party-licenses" / "LodePNG.txt",
        app / "embedded.mobileprovision",
    )
    if any(not path.is_file() or path.stat().st_size <= 0 for path in required_files):
        raise IpaVerificationFailure("signed bundle is missing a required file")
    package_info = app / "PkgInfo"
    if package_info.exists() and (
        not package_info.is_file() or package_info.read_bytes() != b"APPL????"
    ):
        raise IpaVerificationFailure("optional application PkgInfo is invalid")

    for path in app.rglob("*"):
        if not path.is_file():
            continue
        relative = path.relative_to(app).as_posix()
        suffix = path.suffix.lower()
        if suffix == ".mobileprovision" and relative == "embedded.mobileprovision":
            pass
        elif suffix in BLOCKED_SUFFIXES or suffix == ".mobileprovision":
            raise IpaVerificationFailure("signed bundle contains a forbidden file type")
    marker_overlap = b""
    with executable.open("rb") as source:
        while block := source.read(1024 * 1024):
            if SIMULATOR_MARKER in marker_overlap + block:
                raise IpaVerificationFailure(
                    "simulator smoke marker leaked into device binary"
                )
            marker_overlap = (marker_overlap + block)[-len(SIMULATOR_MARKER) :]
    return executable, plist


def verify_native_signature(
    app: Path,
    executable: Path,
    *,
    team_id: str,
    bundle_id: str,
    profile_document: dict[str, object],
    certificate_prefix: Path,
) -> None:
    run_command("codesign", "--verify", "--deep", "--strict", "--verbose=2", os.fspath(app))
    details = run_command("codesign", "-d", "--verbose=4", os.fspath(app)).decode(
        "utf-8", errors="replace"
    )
    if f"Identifier={bundle_id}" not in details or f"TeamIdentifier={team_id}" not in details:
        raise IpaVerificationFailure("code signature identity does not match")
    entitlement_bytes = run_command(
        "codesign",
        "-d",
        "--entitlements",
        ":-",
        os.fspath(app),
    )
    entitlements = load_plist_bytes(entitlement_bytes, "signed entitlements")
    expected_application_identifier = f"{team_id}.{bundle_id}"
    if entitlements.get("application-identifier") != expected_application_identifier:
        raise IpaVerificationFailure("signed application identifier does not match")
    if entitlements.get("com.apple.developer.team-identifier") != team_id:
        raise IpaVerificationFailure("signed entitlement team does not match")
    if entitlements.get("get-task-allow") is not False:
        raise IpaVerificationFailure("signed application is not an Ad Hoc build")
    profile_entitlements = profile_document.get("Entitlements")
    if not isinstance(profile_entitlements, dict):
        raise IpaVerificationFailure("embedded profile entitlements are missing")
    require_profile_authorizes_entitlements(entitlements, profile_entitlements)

    run_command(
        "codesign",
        "-d",
        "--extract-certificates",
        os.fspath(certificate_prefix),
        os.fspath(app),
    )
    leaf_certificate_path = Path(f"{certificate_prefix}0")
    try:
        leaf_certificate = leaf_certificate_path.read_bytes()
    except OSError as error:
        raise IpaVerificationFailure(
            "could not extract the application signing certificate"
        ) from error
    profile_certificates = profile_document.get("DeveloperCertificates")
    if not isinstance(profile_certificates, list) or any(
        not isinstance(certificate, bytes) for certificate in profile_certificates
    ):
        raise IpaVerificationFailure("embedded profile certificates are invalid")
    require_signing_certificate_authorized(
        leaf_certificate,
        profile_certificates,
    )

    architectures = run_command("lipo", "-archs", os.fspath(executable)).decode(
        "utf-8", errors="replace"
    ).split()
    if architectures != ["arm64"]:
        raise IpaVerificationFailure("signed executable is not ARM64-only")
    build_version = run_command("xcrun", "vtool", "-show-build", os.fspath(executable)).decode(
        "utf-8", errors="replace"
    )
    if re.search(r"platform\s+IOS(?:\s|$)", build_version) is None:
        raise IpaVerificationFailure("signed executable is not an iphoneos binary")
    if re.search(r"minos\s+16\.4(?:\.0)?(?:\s|$)", build_version) is None:
        raise IpaVerificationFailure("signed executable minimum OS is not 16.4")


def verify_ipa(
    ipa: Path,
    manifest_output: Path,
    *,
    team_id: str,
    bundle_id: str,
    profile_name: str,
    device_udids: list[str],
    source_sha: str,
    workflow_run_id: str,
    runner_image: str,
    dependency_lock: Path,
) -> dict[str, object]:
    if SOURCE_SHA_PATTERN.fullmatch(source_sha) is None:
        raise IpaVerificationFailure("invalid source SHA")
    if RUN_ID_PATTERN.fullmatch(workflow_run_id) is None:
        raise IpaVerificationFailure("invalid workflow run identifier")
    if RUNNER_IMAGE_PATTERN.fullmatch(runner_image) is None:
        raise IpaVerificationFailure("invalid runner image identity")
    if not dependency_lock.is_file():
        raise IpaVerificationFailure("dependency lock is missing")

    with tempfile.TemporaryDirectory(prefix="airfix-ipa-verify-") as raw:
        temporary = Path(raw)
        app = extract_ipa(ipa, temporary / "extracted")
        executable, plist = verify_bundle_contents(app, bundle_id=bundle_id)
        decoded_profile_path = temporary / "embedded-profile.plist"
        profile_bytes = run_command(
            "security", "cms", "-D", "-i", os.fspath(app / "embedded.mobileprovision")
        )
        decoded_profile_path.write_bytes(profile_bytes)
        try:
            profile_document = load_plist(
                decoded_profile_path, "embedded provisioning profile"
            )
            profile_summary = validate_profile_document(
                profile_document,
                team_id=team_id,
                bundle_id=bundle_id,
                profile_name=profile_name,
                device_udids=device_udids,
            )
        except ProfileValidationFailure as error:
            raise IpaVerificationFailure("embedded provisioning profile is invalid") from error
        verify_native_signature(
            app,
            executable,
            team_id=team_id,
            bundle_id=bundle_id,
            profile_document=profile_document,
            certificate_prefix=temporary / "signing-certificate-",
        )

    xcode_version = run_command("xcodebuild", "-version").decode(
        "utf-8", errors="replace"
    ).strip().replace("\n", "; ")
    sdk_version = run_command("xcrun", "--sdk", "iphoneos", "--show-sdk-version").decode(
        "utf-8", errors="replace"
    ).strip()
    compiler_version = run_command(
        "xcrun", "--sdk", "iphoneos", "clang", "--version"
    ).decode("utf-8", errors="replace").strip().replace("\n", "; ")
    manifest = {
        "schema": "airfix.ios-private-ipa",
        "version": 1,
        "sourceSha": source_sha,
        "workflowRunId": workflow_run_id,
        "runnerImage": runner_image,
        "xcode": xcode_version,
        "iphoneosSdk": sdk_version,
        "compiler": compiler_version,
        "bundleIdentifier": bundle_id,
        "minimumOS": plist.get("MinimumOSVersion"),
        "architectures": ["arm64"],
        "externalContentSchema": "AFPACK-1.0",
        "ipaSha256": sha256_file(ipa),
        "ipaBytes": ipa.stat().st_size,
        "profileExpirationUtc": profile_summary["expirationUtc"],
        "provisionedDeviceCount": profile_summary["provisionedDeviceCount"],
        "dependencyLockSha256": sha256_file(dependency_lock),
    }
    manifest_output.parent.mkdir(parents=True, exist_ok=True)
    manifest_output.write_text(
        json.dumps(manifest, sort_keys=True, indent=2) + "\n",
        encoding="utf-8",
    )
    print("verified private signed IPA: arm64, iOS 16.4, Ad Hoc profile")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ipa", required=True, type=Path)
    parser.add_argument("--manifest-output", required=True, type=Path)
    parser.add_argument("--team-id", required=True)
    parser.add_argument("--bundle-id", required=True)
    parser.add_argument("--profile-name", required=True)
    parser.add_argument("--device-udid", required=True, action="append")
    parser.add_argument("--source-sha", required=True)
    parser.add_argument("--workflow-run-id", required=True)
    parser.add_argument("--runner-image", required=True)
    parser.add_argument("--dependency-lock", required=True, type=Path)
    args = parser.parse_args()
    try:
        verify_ipa(
            args.ipa,
            args.manifest_output,
            team_id=args.team_id,
            bundle_id=args.bundle_id,
            profile_name=args.profile_name,
            device_udids=args.device_udid,
            source_sha=args.source_sha,
            workflow_run_id=args.workflow_run_id,
            runner_image=args.runner_image,
            dependency_lock=args.dependency_lock,
        )
        return 0
    except IpaVerificationFailure as error:
        print(f"signed IPA verification failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
