#!/usr/bin/env python3
"""Package and verify the public, data-less unsigned iOS device artifact."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import plistlib
import re
import stat
import struct
import sys
import tempfile
import zipfile


SCHEMA = "airfix.ios-unsigned-ipa-v1"
APP_ROOT = PurePosixPath("Payload/AirfixDogfighter.app")
EXECUTABLE_NAME = "AirfixDogfighter"
DEFAULT_BUNDLE_ID = "com.tryk016.airfixdogfighter"
MINIMUM_IOS = "16.4"
MACHO_64_MAGIC = 0xFEEDFACF
CPU_TYPE_ARM64 = 0x0100000C
MH_EXECUTE = 2
LC_CODE_SIGNATURE = 0x1D
LC_BUILD_VERSION = 0x32
PLATFORM_IOS = 2
MINIMUM_IOS_PACKED = (16 << 16) | (4 << 8)
MAX_IPA_BYTES = 256 * 1024 * 1024
MAX_UNCOMPRESSED_BYTES = 512 * 1024 * 1024
MAX_FILE_BYTES = 256 * 1024 * 1024
MAX_MANIFEST_BYTES = 64 * 1024
MAX_DEPENDENCY_LOCK_BYTES = 1024 * 1024
CANONICAL_ZIP_VERSION = 20
SOURCE_SHA_PATTERN = re.compile(r"^[0-9a-f]{40}$")
RUN_ID_PATTERN = re.compile(r"^[1-9][0-9]*$")
SAFE_METADATA_PATTERN = re.compile(r"^[ -~]{1,256}$")
HOST_PATH_PATTERN = re.compile(
    rb"(?<![A-Za-z0-9])(?:[A-Za-z]:[\\/]|/(?:Users|home)/[^/\x00]+/)"
    rb"[^\x00\r\n]{0,512}",
    re.IGNORECASE,
)
SIMULATOR_MARKER = b"airfix.ios-simulator-smoke"

REQUIRED_APP_FILES = {
    PurePosixPath(EXECUTABLE_NAME),
    PurePosixPath("Info.plist"),
    PurePosixPath("default.metallib"),
    PurePosixPath("third-party-licenses/LodePNG.txt"),
}
OPTIONAL_APP_FILES = {PurePosixPath("PkgInfo")}
ALLOWED_APP_DIRECTORIES = {PurePosixPath("third-party-licenses")}
ARCHIVE_DIRECTORIES = {
    PurePosixPath("Payload"),
    APP_ROOT,
    APP_ROOT / "third-party-licenses",
}


class UnsignedIpaFailure(RuntimeError):
    pass


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while block := source.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def sha256_normalized_text_file(path: Path, field: str) -> str:
    require_regular_file(path, field, MAX_DEPENDENCY_LOCK_BYTES)
    try:
        data = path.read_bytes()
    except OSError as error:
        raise UnsignedIpaFailure(f"{field} could not be read") from error
    if not data or len(data) > MAX_DEPENDENCY_LOCK_BYTES:
        raise UnsignedIpaFailure(f"{field} is empty or oversized")
    try:
        text = data.decode("utf-8")
    except UnicodeError as error:
        raise UnsignedIpaFailure(f"{field} is not valid UTF-8 text") from error
    normalized = text.replace("\r\n", "\n")
    if "\r" in normalized:
        raise UnsignedIpaFailure(f"{field} contains a non-canonical line ending")
    return hashlib.sha256(normalized.encode("utf-8")).hexdigest()


def require_safe_metadata(value: str, field: str) -> str:
    if not SAFE_METADATA_PATTERN.fullmatch(value) or HOST_PATH_PATTERN.search(
        value.encode("utf-8")
    ):
        raise UnsignedIpaFailure(f"{field} is invalid or contains a host path")
    return value


def require_regular_file(
    path: Path, field: str, maximum_bytes: int = MAX_FILE_BYTES
) -> None:
    try:
        status = path.lstat()
    except OSError as error:
        raise UnsignedIpaFailure(f"{field} is missing") from error
    if not stat.S_ISREG(status.st_mode) or path.is_symlink():
        raise UnsignedIpaFailure(f"{field} is not a regular file")
    if status.st_size <= 0 or status.st_size > maximum_bytes:
        raise UnsignedIpaFailure(f"{field} is empty or oversized")


def validate_macho(executable: bytes) -> None:
    if len(executable) < 32:
        raise UnsignedIpaFailure("application executable has a truncated Mach-O header")
    magic, cpu_type, _, file_type, command_count, command_bytes, _, _ = (
        struct.unpack_from("<8I", executable)
    )
    if magic != MACHO_64_MAGIC or cpu_type != CPU_TYPE_ARM64:
        raise UnsignedIpaFailure("application executable is not thin ARM64 Mach-O")
    if file_type != MH_EXECUTE:
        raise UnsignedIpaFailure("application Mach-O is not an executable")
    if command_count > 4096 or command_bytes > len(executable) - 32:
        raise UnsignedIpaFailure("application Mach-O load commands are malformed")

    cursor = 32
    command_end = 32 + command_bytes
    found_build_version = False
    for _ in range(command_count):
        if cursor + 8 > command_end:
            raise UnsignedIpaFailure("application Mach-O load command is truncated")
        command, command_size = struct.unpack_from("<2I", executable, cursor)
        if (
            command_size < 8
            or command_size % 8 != 0
            or cursor + command_size > command_end
        ):
            raise UnsignedIpaFailure("application Mach-O load command size is invalid")
        if command == LC_CODE_SIGNATURE:
            raise UnsignedIpaFailure("application executable unexpectedly contains a code signature")
        if command == LC_BUILD_VERSION:
            if command_size < 24:
                raise UnsignedIpaFailure("application build-version command is malformed")
            _, _, platform, minimum_os, _, tool_count = struct.unpack_from(
                "<6I", executable, cursor
            )
            if command_size != 24 + tool_count * 8:
                raise UnsignedIpaFailure("application build-version command is malformed")
            if found_build_version:
                raise UnsignedIpaFailure("application has duplicate build-version commands")
            if platform != PLATFORM_IOS or minimum_os != MINIMUM_IOS_PACKED:
                raise UnsignedIpaFailure(
                    "application Mach-O platform or minimum iOS is incorrect"
                )
            found_build_version = True
        cursor += command_size
    if cursor != command_end or not found_build_version:
        raise UnsignedIpaFailure("application Mach-O build-version contract is incomplete")


def validate_info_plist(data: bytes, bundle_id: str) -> None:
    try:
        document = plistlib.loads(data)
    except (plistlib.InvalidFileException, ValueError) as error:
        raise UnsignedIpaFailure("application Info.plist is malformed") from error
    if not isinstance(document, dict):
        raise UnsignedIpaFailure("application Info.plist is not a dictionary")
    expected = {
        "CFBundleExecutable": EXECUTABLE_NAME,
        "CFBundleIdentifier": bundle_id,
        "CFBundlePackageType": "APPL",
        "MinimumOSVersion": MINIMUM_IOS,
        "LSRequiresIPhoneOS": True,
    }
    for key, value in expected.items():
        actual = document.get(key)
        if type(actual) is not type(value) or actual != value:
            raise UnsignedIpaFailure(f"application Info.plist has an invalid {key}")


def scan_public_bytes(data: bytes, field: str) -> None:
    if SIMULATOR_MARKER in data:
        raise UnsignedIpaFailure(f"{field} contains the simulator-only harness")
    if HOST_PATH_PATTERN.search(data):
        raise UnsignedIpaFailure(f"{field} contains a local host path")


def collect_source_app(app: Path, bundle_id: str) -> dict[PurePosixPath, bytes]:
    try:
        if app.name != "AirfixDogfighter.app" or not app.is_dir() or app.is_symlink():
            raise UnsignedIpaFailure("source application bundle is invalid")
        found_files: set[PurePosixPath] = set()
        found_directories: set[PurePosixPath] = set()
        for root, directories, files in os.walk(app, followlinks=False):
            root_path = Path(root)
            relative_root = root_path.relative_to(app)
            if relative_root != Path("."):
                found_directories.add(PurePosixPath(relative_root.as_posix()))
            for directory in directories:
                candidate = root_path / directory
                if candidate.is_symlink():
                    raise UnsignedIpaFailure("source application contains a symlink")
            for filename in files:
                candidate = root_path / filename
                relative = PurePosixPath(candidate.relative_to(app).as_posix())
                require_regular_file(candidate, "application member")
                found_files.add(relative)

        if found_directories != ALLOWED_APP_DIRECTORIES:
            raise UnsignedIpaFailure("source application has unexpected directories")
        if not REQUIRED_APP_FILES.issubset(found_files) or not found_files.issubset(
            REQUIRED_APP_FILES | OPTIONAL_APP_FILES
        ):
            raise UnsignedIpaFailure("source application has missing or unexpected files")

        payloads: dict[PurePosixPath, bytes] = {}
        total = 0
        for relative in sorted(found_files, key=lambda item: item.as_posix()):
            data = (app / Path(*relative.parts)).read_bytes()
            total += len(data)
            if total > MAX_UNCOMPRESSED_BYTES:
                raise UnsignedIpaFailure("source application exceeds the size limit")
            scan_public_bytes(data, relative.as_posix())
            payloads[relative] = data
    except UnsignedIpaFailure:
        raise
    except (OSError, ValueError) as error:
        raise UnsignedIpaFailure("source application could not be inspected") from error

    validate_info_plist(payloads[PurePosixPath("Info.plist")], bundle_id)
    validate_macho(payloads[PurePosixPath(EXECUTABLE_NAME)])
    if not payloads[PurePosixPath("default.metallib")]:
        raise UnsignedIpaFailure("Metal library is empty")
    if not payloads[PurePosixPath("third-party-licenses/LodePNG.txt")]:
        raise UnsignedIpaFailure("third-party license is empty")
    pkg_info = payloads.get(PurePosixPath("PkgInfo"))
    if pkg_info is not None and pkg_info != b"APPL????":
        raise UnsignedIpaFailure("PkgInfo has unexpected content")
    return payloads


def directory_info(path: PurePosixPath) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(path.as_posix().rstrip("/") + "/", (1980, 1, 1, 0, 0, 0))
    info.create_system = 3
    info.compress_type = zipfile.ZIP_STORED
    info.external_attr = (stat.S_IFDIR | 0o755) << 16 | 0x10
    return info


def file_info(path: PurePosixPath, executable: bool) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(path.as_posix(), (1980, 1, 1, 0, 0, 0))
    info.create_system = 3
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = (stat.S_IFREG | (0o755 if executable else 0o644)) << 16
    return info


def write_deterministic_ipa(
    payloads: dict[PurePosixPath, bytes], output: Path
) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            prefix=output.name + ".", suffix=".tmp", dir=output.parent, delete=False
        ) as stream:
            temporary = Path(stream.name)
        with zipfile.ZipFile(
            temporary, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
        ) as archive:
            for directory in sorted(ARCHIVE_DIRECTORIES, key=lambda item: item.as_posix()):
                archive.writestr(directory_info(directory), b"")
            for relative, data in sorted(
                payloads.items(), key=lambda item: item[0].as_posix()
            ):
                archive_path = APP_ROOT / relative
                archive.writestr(
                    file_info(
                        archive_path,
                        relative == PurePosixPath(EXECUTABLE_NAME),
                    ),
                    data,
                )
        os.replace(temporary, output)
        temporary = None
    except (OSError, RuntimeError, zipfile.BadZipFile) as error:
        raise UnsignedIpaFailure("unsigned IPA could not be written") from error
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def validate_archive_member(info: zipfile.ZipInfo) -> tuple[PurePosixPath, bool]:
    if not info.filename or "\\" in info.filename or "\x00" in info.filename:
        raise UnsignedIpaFailure("unsigned IPA contains an invalid member name")
    is_directory = info.filename.endswith("/")
    raw_path = info.filename[:-1] if is_directory else info.filename
    raw_parts = raw_path.split("/")
    if (
        not raw_path
        or (is_directory and info.filename.endswith("//"))
        or any(part in ("", ".", "..") for part in raw_parts)
    ):
        raise UnsignedIpaFailure("unsigned IPA contains an unsafe member path")
    path = PurePosixPath(raw_path)
    canonical_name = path.as_posix() + ("/" if is_directory else "")
    if path.is_absolute() or canonical_name != info.filename:
        raise UnsignedIpaFailure("unsigned IPA member name is not canonical")
    if (
        info.create_system != 3
        or info.create_version != CANONICAL_ZIP_VERSION
        or info.extract_version != CANONICAL_ZIP_VERSION
        or info.reserved != 0
        or info.volume != 0
        or info.internal_attr != 0
        or info.date_time != (1980, 1, 1, 0, 0, 0)
        or info.flag_bits != 0
        or info.extra
        or info.comment
    ):
        raise UnsignedIpaFailure("unsigned IPA contains unsupported ZIP metadata")
    unix_mode = info.external_attr >> 16
    file_type = stat.S_IFMT(unix_mode)
    if info.is_dir() != is_directory:
        raise UnsignedIpaFailure("unsigned IPA member type is inconsistent")
    expected_type = stat.S_IFDIR if is_directory else stat.S_IFREG
    if file_type != expected_type:
        raise UnsignedIpaFailure("unsigned IPA contains a non-regular member")
    expected_permissions = (
        0o755 if is_directory or path == APP_ROOT / EXECUTABLE_NAME else 0o644
    )
    if stat.S_IMODE(unix_mode) != expected_permissions:
        raise UnsignedIpaFailure("unsigned IPA member permissions are not canonical")
    expected_external_attr = (
        (stat.S_IFDIR | 0o755) << 16 | 0x10
        if is_directory
        else (stat.S_IFREG | expected_permissions) << 16
    )
    if info.external_attr != expected_external_attr:
        raise UnsignedIpaFailure("unsigned IPA contains unsupported ZIP metadata")
    if is_directory and path not in ARCHIVE_DIRECTORIES:
        raise UnsignedIpaFailure("unsigned IPA contains an unexpected directory")
    if not is_directory:
        if len(path.parts) <= len(APP_ROOT.parts) or PurePosixPath(
            *path.parts[: len(APP_ROOT.parts)]
        ) != APP_ROOT:
            raise UnsignedIpaFailure("unsigned IPA contains a file outside the application")
        relative = PurePosixPath(*path.parts[len(APP_ROOT.parts) :])
        if relative not in REQUIRED_APP_FILES | OPTIONAL_APP_FILES:
            raise UnsignedIpaFailure("unsigned IPA contains an unexpected application file")
        if info.file_size <= 0 or info.file_size > MAX_FILE_BYTES:
            raise UnsignedIpaFailure("unsigned IPA member is empty or oversized")
        if info.compress_type != zipfile.ZIP_DEFLATED:
            raise UnsignedIpaFailure("unsigned IPA file compression is not canonical")
    elif info.compress_type != zipfile.ZIP_STORED:
        raise UnsignedIpaFailure("unsigned IPA directory compression is not canonical")
    return path, is_directory


def validate_unsigned_ipa(ipa: Path, bundle_id: str) -> dict[PurePosixPath, bytes]:
    try:
        require_regular_file(ipa, "unsigned IPA")
        if ipa.stat().st_size > MAX_IPA_BYTES:
            raise UnsignedIpaFailure("unsigned IPA is oversized")
        with zipfile.ZipFile(ipa) as archive:
            if archive.comment:
                raise UnsignedIpaFailure("unsigned IPA has an archive comment")
            members = archive.infolist()
            if not members or len(members) > 32:
                raise UnsignedIpaFailure("unsigned IPA entry count is outside policy")
            seen: set[str] = set()
            directories: set[PurePosixPath] = set()
            payloads: dict[PurePosixPath, bytes] = {}
            total = 0
            validated_files: list[tuple[zipfile.ZipInfo, PurePosixPath]] = []
            for info in members:
                path, is_directory = validate_archive_member(info)
                normalized = path.as_posix().casefold()
                if normalized in seen:
                    raise UnsignedIpaFailure("unsigned IPA contains a duplicate member")
                seen.add(normalized)
                if is_directory:
                    directories.add(path)
                    continue
                total += info.file_size
                if total > MAX_UNCOMPRESSED_BYTES:
                    raise UnsignedIpaFailure("unsigned IPA expands beyond the size limit")
                validated_files.append((info, path))
            for info, path in validated_files:
                data = archive.read(info)
                scan_public_bytes(data, path.as_posix())
                relative = PurePosixPath(*path.parts[len(APP_ROOT.parts) :])
                payloads[relative] = data
    except UnsignedIpaFailure:
        raise
    except (OSError, RuntimeError, zipfile.BadZipFile, KeyError) as error:
        raise UnsignedIpaFailure("unsigned IPA archive is malformed") from error

    if directories != ARCHIVE_DIRECTORIES:
        raise UnsignedIpaFailure("unsigned IPA directory set is not canonical")
    files = set(payloads)
    if not REQUIRED_APP_FILES.issubset(files) or not files.issubset(
        REQUIRED_APP_FILES | OPTIONAL_APP_FILES
    ):
        raise UnsignedIpaFailure("unsigned IPA file set is incomplete")
    validate_info_plist(payloads[PurePosixPath("Info.plist")], bundle_id)
    validate_macho(payloads[PurePosixPath(EXECUTABLE_NAME)])
    pkg_info = payloads.get(PurePosixPath("PkgInfo"))
    if pkg_info is not None and pkg_info != b"APPL????":
        raise UnsignedIpaFailure("PkgInfo has unexpected content")
    return payloads


def write_manifest(
    *,
    output: Path,
    ipa: Path,
    source_sha: str,
    workflow_run_id: str,
    runner_image: str,
    xcode_version: str,
    compiler: str,
    sdk_version: str,
    dependency_lock: Path,
    bundle_id: str,
) -> dict[str, object]:
    require_regular_file(ipa, "unsigned IPA", MAX_IPA_BYTES)
    if not SOURCE_SHA_PATTERN.fullmatch(source_sha):
        raise UnsignedIpaFailure("source SHA is invalid")
    if not RUN_ID_PATTERN.fullmatch(workflow_run_id):
        raise UnsignedIpaFailure("workflow run ID is invalid")
    require_safe_metadata(runner_image, "runner image")
    require_safe_metadata(xcode_version, "Xcode version")
    require_safe_metadata(compiler, "compiler")
    require_safe_metadata(sdk_version, "SDK version")
    dependency_lock_sha256 = sha256_normalized_text_file(
        dependency_lock, "dependency lock"
    )
    expected_name = f"AirfixDogfighter-{source_sha}-unsigned.ipa"
    if ipa.name != expected_name:
        raise UnsignedIpaFailure("unsigned IPA filename does not match its source SHA")
    document: dict[str, object] = {
        "schema": SCHEMA,
        "artifact": {
            "file": ipa.name,
            "sha256": sha256_file(ipa),
            "sizeBytes": ipa.stat().st_size,
            "signed": False,
            "installableWithoutLocalSigning": False,
        },
        "build": {
            "sourceSha": source_sha,
            "workflowRunId": workflow_run_id,
            "runnerImage": runner_image,
            "xcodeVersion": xcode_version,
            "compiler": compiler,
            "sdkVersion": sdk_version,
            "configuration": "Release",
            "platform": "IOS",
            "architecture": "arm64",
            "minimumOS": MINIMUM_IOS,
            "bundleIdentifier": bundle_id,
            "dependencyLockSha256": dependency_lock_sha256,
        },
        "contentPolicy": {
            "dataLess": True,
            "originalGameAssetsIncluded": False,
            "privateHdTexturesIncluded": False,
            "signingMaterialIncluded": False,
        },
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name + ".tmp")
    try:
        temporary.write_text(
            json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        os.replace(temporary, output)
    except OSError as error:
        raise UnsignedIpaFailure("unsigned IPA manifest could not be written") from error
    finally:
        temporary.unlink(missing_ok=True)
    return document


def load_and_validate_manifest(
    manifest: Path,
    ipa: Path,
    expected_source_sha: str | None = None,
    expected_dependency_lock: Path | None = None,
) -> dict[str, object]:
    try:
        require_regular_file(
            manifest, "unsigned IPA manifest", MAX_MANIFEST_BYTES
        )
        require_regular_file(ipa, "unsigned IPA", MAX_IPA_BYTES)
        document = json.loads(manifest.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise UnsignedIpaFailure("unsigned IPA manifest is malformed") from error
    if not isinstance(document, dict) or set(document) != {
        "schema",
        "artifact",
        "build",
        "contentPolicy",
    }:
        raise UnsignedIpaFailure("unsigned IPA manifest has unexpected top-level fields")
    if document["schema"] != SCHEMA:
        raise UnsignedIpaFailure("unsigned IPA manifest schema is unsupported")
    artifact = document["artifact"]
    build = document["build"]
    policy = document["contentPolicy"]
    if not isinstance(artifact, dict) or set(artifact) != {
        "file",
        "sha256",
        "sizeBytes",
        "signed",
        "installableWithoutLocalSigning",
    }:
        raise UnsignedIpaFailure("unsigned IPA manifest artifact fields are invalid")
    if not isinstance(build, dict) or set(build) != {
        "sourceSha",
        "workflowRunId",
        "runnerImage",
        "xcodeVersion",
        "compiler",
        "sdkVersion",
        "configuration",
        "platform",
        "architecture",
        "minimumOS",
        "bundleIdentifier",
        "dependencyLockSha256",
    }:
        raise UnsignedIpaFailure("unsigned IPA manifest build fields are invalid")
    if not isinstance(policy, dict) or set(policy) != {
        "dataLess",
        "originalGameAssetsIncluded",
        "privateHdTexturesIncluded",
        "signingMaterialIncluded",
    }:
        raise UnsignedIpaFailure("unsigned IPA manifest policy fields are invalid")
    if (
        artifact["file"] != ipa.name
        or artifact["sha256"] != sha256_file(ipa)
        or type(artifact["sizeBytes"]) is not int
        or artifact["sizeBytes"] != ipa.stat().st_size
        or artifact["signed"] is not False
        or artifact["installableWithoutLocalSigning"] is not False
    ):
        raise UnsignedIpaFailure("unsigned IPA manifest does not match the artifact")
    source_sha = build.get("sourceSha")
    if not isinstance(source_sha, str) or not SOURCE_SHA_PATTERN.fullmatch(source_sha):
        raise UnsignedIpaFailure("unsigned IPA manifest source SHA is invalid")
    if expected_source_sha is not None and source_sha != expected_source_sha:
        raise UnsignedIpaFailure("unsigned IPA manifest source SHA is unexpected")
    if artifact["file"] != f"AirfixDogfighter-{source_sha}-unsigned.ipa":
        raise UnsignedIpaFailure("unsigned IPA filename does not match its source SHA")
    if (
        not isinstance(build.get("workflowRunId"), str)
        or not RUN_ID_PATTERN.fullmatch(build["workflowRunId"])
        or build.get("configuration") != "Release"
        or build.get("platform") != "IOS"
        or build.get("architecture") != "arm64"
        or build.get("minimumOS") != MINIMUM_IOS
        or build.get("bundleIdentifier") != DEFAULT_BUNDLE_ID
        or not isinstance(build.get("dependencyLockSha256"), str)
        or not re.fullmatch(r"[0-9a-f]{64}", build["dependencyLockSha256"])
    ):
        raise UnsignedIpaFailure("unsigned IPA manifest build identity is invalid")
    for field in ("runnerImage", "xcodeVersion", "compiler", "sdkVersion"):
        value = build.get(field)
        if not isinstance(value, str):
            raise UnsignedIpaFailure("unsigned IPA manifest metadata is invalid")
        require_safe_metadata(value, field)
    if expected_dependency_lock is not None:
        expected_lock_sha256 = sha256_normalized_text_file(
            expected_dependency_lock, "dependency lock"
        )
        if build["dependencyLockSha256"] != expected_lock_sha256:
            raise UnsignedIpaFailure("unsigned IPA dependency lock is unexpected")
    if any(type(policy[key]) is not bool for key in policy) or policy != {
        "dataLess": True,
        "originalGameAssetsIncluded": False,
        "privateHdTexturesIncluded": False,
        "signingMaterialIncluded": False,
    }:
        raise UnsignedIpaFailure("unsigned IPA manifest content policy is invalid")
    return document


def package_command(args: argparse.Namespace) -> None:
    payloads = collect_source_app(args.app, DEFAULT_BUNDLE_ID)
    write_deterministic_ipa(payloads, args.ipa_output)
    validate_unsigned_ipa(args.ipa_output, DEFAULT_BUNDLE_ID)
    write_manifest(
        output=args.manifest_output,
        ipa=args.ipa_output,
        source_sha=args.source_sha,
        workflow_run_id=args.workflow_run_id,
        runner_image=args.runner_image,
        xcode_version=args.xcode_version,
        compiler=args.compiler,
        sdk_version=args.sdk_version,
        dependency_lock=args.dependency_lock,
        bundle_id=DEFAULT_BUNDLE_ID,
    )
    load_and_validate_manifest(
        args.manifest_output,
        args.ipa_output,
        args.source_sha,
        args.dependency_lock,
    )


def verify_command(args: argparse.Namespace) -> None:
    document = load_and_validate_manifest(
        args.manifest,
        args.ipa,
        args.expected_source_sha,
        args.dependency_lock,
    )
    build = document["build"]
    assert isinstance(build, dict)
    bundle_id = build["bundleIdentifier"]
    assert isinstance(bundle_id, str)
    validate_unsigned_ipa(args.ipa, bundle_id)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    package = commands.add_parser("package")
    package.add_argument("--app", type=Path, required=True)
    package.add_argument("--ipa-output", type=Path, required=True)
    package.add_argument("--manifest-output", type=Path, required=True)
    package.add_argument("--source-sha", required=True)
    package.add_argument("--workflow-run-id", required=True)
    package.add_argument("--runner-image", required=True)
    package.add_argument("--xcode-version", required=True)
    package.add_argument("--compiler", required=True)
    package.add_argument("--sdk-version", required=True)
    package.add_argument("--dependency-lock", type=Path, required=True)
    package.set_defaults(function=package_command)

    verify = commands.add_parser("verify")
    verify.add_argument("--ipa", type=Path, required=True)
    verify.add_argument("--manifest", type=Path, required=True)
    verify.add_argument("--expected-source-sha")
    verify.add_argument(
        "--dependency-lock",
        type=Path,
        default=Path(__file__).resolve().parents[2] / "docs" / "toolchain" / "LOCK.md",
    )
    verify.set_defaults(function=verify_command)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        args.function(args)
    except UnsignedIpaFailure as error:
        print(f"unsigned iOS package rejected: {error}", file=sys.stderr)
        return 1
    print(f"unsigned iOS package {args.command} verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
