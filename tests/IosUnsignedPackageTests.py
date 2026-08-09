#!/usr/bin/env python3
"""Synthetic tests for the public unsigned iOS package boundary."""

from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path
import plistlib
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import warnings
import zipfile


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = REPOSITORY_ROOT / "tools" / "ci" / "package_ios_unsigned_ipa.py"
SPEC = importlib.util.spec_from_file_location("airfix_unsigned_ipa", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load unsigned iOS package module")
ipa = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ipa)

SOURCE_SHA = "a" * 40
BUNDLE_ID = "com.tryk016.airfixdogfighter"


def macho(
    *,
    cpu_type: int = ipa.CPU_TYPE_ARM64,
    platform: int = ipa.PLATFORM_IOS,
    minimum_os: int = ipa.MINIMUM_IOS_PACKED,
    build_command_size: int = 24,
    build_tool_count: int = 0,
    extra_command: bytes = b"",
) -> bytes:
    build = struct.pack(
        "<6I",
        ipa.LC_BUILD_VERSION,
        build_command_size,
        platform,
        minimum_os,
        minimum_os,
        build_tool_count,
    ) + b"\0" * (build_command_size - 24)
    commands = build + extra_command
    command_count = 1 + (1 if extra_command else 0)
    header = struct.pack(
        "<8I",
        ipa.MACHO_64_MAGIC,
        cpu_type,
        0,
        ipa.MH_EXECUTE,
        command_count,
        len(commands),
        0,
        0,
    )
    return header + commands + b"synthetic-public-code"


def populate_app(app: Path, executable: bytes | None = None) -> None:
    (app / "third-party-licenses").mkdir(parents=True)
    document = {
        "CFBundleExecutable": "AirfixDogfighter",
        "CFBundleIdentifier": BUNDLE_ID,
        "CFBundlePackageType": "APPL",
        "MinimumOSVersion": "16.4",
        "LSRequiresIPhoneOS": True,
        "LSSupportsOpeningDocumentsInPlace": True,
        "UIFileSharingEnabled": True,
    }
    (app / "Info.plist").write_bytes(plistlib.dumps(document, sort_keys=True))
    (app / "AirfixDogfighter").write_bytes(executable or macho())
    (app / "default.metallib").write_bytes(b"synthetic-public-metal")
    (app / "third-party-licenses" / "LodePNG.txt").write_text(
        "synthetic license", encoding="utf-8"
    )


def package(root: Path, app: Path, suffix: str = "") -> tuple[Path, Path]:
    case_root = root / (suffix.strip("-") or "default")
    case_root.mkdir(parents=True, exist_ok=True)
    output = case_root / f"AirfixDogfighter-{SOURCE_SHA}-unsigned.ipa"
    manifest = case_root / "build-manifest.json"
    payloads = ipa.collect_source_app(app, BUNDLE_ID)
    ipa.write_deterministic_ipa(payloads, output)
    ipa.validate_unsigned_ipa(output, BUNDLE_ID)
    ipa.write_manifest(
        output=manifest,
        ipa=output,
        source_sha=SOURCE_SHA,
        workflow_run_id="12345",
        runner_image="macos-26/20260801.1",
        xcode_version="Xcode 26.6 Build version 17G86",
        compiler="Apple clang version 18.0.0",
        sdk_version="26.6",
        dependency_lock=REPOSITORY_ROOT / "docs" / "toolchain" / "LOCK.md",
        bundle_id=BUNDLE_ID,
    )
    ipa.load_and_validate_manifest(manifest, output, SOURCE_SHA)
    return output, manifest


def expect_failure(function, fragment: str) -> None:
    try:
        function()
    except ipa.UnsignedIpaFailure as error:
        if fragment not in str(error):
            raise AssertionError(f"unexpected failure: {error}") from error
        return
    raise AssertionError(f"expected unsigned IPA failure containing {fragment!r}")


def rewrite_archive_info(source: Path, output: Path, mutation) -> None:
    with zipfile.ZipFile(source) as original, zipfile.ZipFile(output, "w") as changed:
        for info in original.infolist():
            data = b"" if info.is_dir() else original.read(info)
            replacement = copy.copy(info)
            mutation(replacement)
            changed.writestr(replacement, data)


def test_positive_and_deterministic(root: Path) -> None:
    app = root / "source" / "AirfixDogfighter.app"
    populate_app(app)
    first, first_manifest = package(root, app, "-first")
    second, _ = package(root, app, "-second")
    if first.read_bytes() != second.read_bytes():
        raise AssertionError("unsigned IPA package is not deterministic")
    document = json.loads(first_manifest.read_text(encoding="utf-8"))
    if document["contentPolicy"] != {
        "dataLess": True,
        "originalGameAssetsIncluded": False,
        "privateHdTexturesIncluded": False,
        "signingMaterialIncluded": False,
    }:
        raise AssertionError("unsigned IPA manifest content policy is incorrect")
    if document["artifact"]["signed"] is not False:
        raise AssertionError("unsigned IPA manifest claims a signature")

    (app / "PkgInfo").write_bytes(b"APPL????")
    package(root, app, "-pkginfo")
    (app / "PkgInfo").write_bytes(b"BAD!????")
    expect_failure(lambda: ipa.collect_source_app(app, BUNDLE_ID), "PkgInfo")


def test_source_boundary(root: Path) -> None:
    app = root / "boundary" / "AirfixDogfighter.app"
    populate_app(app)

    try:
        ipa.scan_public_bytes(
            b"prefix /Users/alice/work/airfix/source.mm\x00suffix", "executable"
        )
    except ipa.UnsignedIpaFailure as error:
        message = str(error)
        if "<host-root>/work/airfix/source.mm" not in message:
            raise AssertionError("host-path diagnostic lost its useful suffix")
        if "alice" in message:
            raise AssertionError("host-path diagnostic exposed the host user")
    else:
        raise AssertionError("host-path diagnostic fixture was accepted")

    # A bare home-root prefix is neither a file nor a source/build path. It can
    # occur as an SDK/runtime string and must not create a binary false alarm.
    ipa.scan_public_bytes(b"prefix /Users/runner/\x01suffix", "executable")

    (app / "private-notes.txt").write_text("not allowed", encoding="utf-8")
    expect_failure(
        lambda: ipa.collect_source_app(app, BUNDLE_ID), "missing or unexpected files"
    )
    (app / "private-notes.txt").unlink()

    (app / "default.metallib").write_bytes(
        b"C:\\Users\\owner\\roms\\Airfix Dogfighter\\private"
    )
    expect_failure(lambda: ipa.collect_source_app(app, BUNDLE_ID), "local host path")
    (app / "default.metallib").write_bytes(ipa.SIMULATOR_MARKER)
    expect_failure(
        lambda: ipa.collect_source_app(app, BUNDLE_ID), "simulator-only harness"
    )

    invalid_plist = {
        "CFBundleExecutable": "AirfixDogfighter",
        "CFBundleIdentifier": BUNDLE_ID,
        "CFBundlePackageType": "APPL",
        "MinimumOSVersion": "16.4",
        "LSRequiresIPhoneOS": 1,
        "LSSupportsOpeningDocumentsInPlace": True,
        "UIFileSharingEnabled": True,
    }
    expect_failure(
        lambda: ipa.validate_info_plist(
            plistlib.dumps(invalid_plist, sort_keys=True), BUNDLE_ID
        ),
        "LSRequiresIPhoneOS",
    )
    missing_workspace_key = dict(invalid_plist)
    missing_workspace_key["LSRequiresIPhoneOS"] = True
    del missing_workspace_key["UIFileSharingEnabled"]
    expect_failure(
        lambda: ipa.validate_info_plist(
            plistlib.dumps(missing_workspace_key, sort_keys=True), BUNDLE_ID
        ),
        "UIFileSharingEnabled",
    )


def test_macho_contract(root: Path) -> None:
    ipa.validate_macho(macho(build_command_size=32, build_tool_count=1))
    signature = struct.pack("<4I", ipa.LC_CODE_SIGNATURE, 16, 0, 0)
    cases = (
        (macho(cpu_type=7), "not thin ARM64"),
        (macho(platform=7), "platform or minimum iOS"),
        (macho(minimum_os=(17 << 16)), "platform or minimum iOS"),
        (macho(build_command_size=25), "load command size"),
        (
            macho(build_command_size=24, build_tool_count=1),
            "build-version command",
        ),
        (
            macho(build_command_size=32, build_tool_count=0),
            "build-version command",
        ),
        (macho(extra_command=signature), "code signature"),
    )
    for index, (executable, message) in enumerate(cases):
        case = root / "macho" / str(index) / "AirfixDogfighter.app"
        populate_app(case, executable)
        expect_failure(lambda case=case: ipa.collect_source_app(case, BUNDLE_ID), message)


def test_archive_boundary(root: Path) -> None:
    app = root / "archive" / "AirfixDogfighter.app"
    populate_app(app)
    valid, _ = package(root, app, "-archive")

    extra = root / "extra.ipa"
    with zipfile.ZipFile(valid) as source, zipfile.ZipFile(extra, "w") as output:
        for info in source.infolist():
            output.writestr(info, b"" if info.is_dir() else source.read(info))
        unexpected = zipfile.ZipInfo("Payload/private-notes.txt")
        unexpected.create_system = 3
        unexpected.compress_type = zipfile.ZIP_DEFLATED
        unexpected.external_attr = (stat.S_IFREG | 0o644) << 16
        output.writestr(unexpected, b"private")
    expect_failure(
        lambda: ipa.validate_unsigned_ipa(extra, BUNDLE_ID),
        "outside the application",
    )

    duplicate = root / "duplicate.ipa"
    duplicate.write_bytes(valid.read_bytes())
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", UserWarning)
        with zipfile.ZipFile(duplicate, "a") as archive:
            original = archive.getinfo("Payload/AirfixDogfighter.app/Info.plist")
            archive.writestr(original, archive.read(original))
    expect_failure(
        lambda: ipa.validate_unsigned_ipa(duplicate, BUNDLE_ID), "duplicate member"
    )

    symlink = root / "symlink.ipa"
    symlink.write_bytes(valid.read_bytes())
    link = zipfile.ZipInfo("Payload/AirfixDogfighter.app/link")
    link.create_system = 3
    link.external_attr = (stat.S_IFLNK | 0o777) << 16
    with zipfile.ZipFile(symlink, "a") as archive:
        archive.writestr(link, b"target")
    expect_failure(
        lambda: ipa.validate_unsigned_ipa(symlink, BUNDLE_ID), "non-regular member"
    )

    for name, replacement in (
        ("dot-alias", "Payload/./AirfixDogfighter.app/Info.plist"),
        ("double-slash-alias", "Payload//AirfixDogfighter.app/Info.plist"),
    ):
        candidate = root / f"{name}.ipa"
        rewrite_archive_info(
            valid,
            candidate,
            lambda info, replacement=replacement: setattr(
                info,
                "filename",
                replacement
                if info.filename == "Payload/AirfixDogfighter.app/Info.plist"
                else info.filename,
            ),
        )
        expect_failure(
            lambda candidate=candidate: ipa.validate_unsigned_ipa(candidate, BUNDLE_ID),
            "member",
        )

    metadata_mutations = (
        ("dos-attribute", lambda info: setattr(info, "external_attr", info.external_attr | 1)),
        ("internal-attribute", lambda info: setattr(info, "internal_attr", 1)),
        ("create-version", lambda info: setattr(info, "create_version", 21)),
    )
    for name, mutate in metadata_mutations:
        candidate = root / f"{name}.ipa"

        def mutate_executable(info, mutate=mutate) -> None:
            if info.filename == "Payload/AirfixDogfighter.app/AirfixDogfighter":
                mutate(info)

        rewrite_archive_info(valid, candidate, mutate_executable)
        expect_failure(
            lambda candidate=candidate: ipa.validate_unsigned_ipa(candidate, BUNDLE_ID),
            "ZIP metadata",
        )


def test_manifest_boundary(root: Path) -> None:
    app = root / "manifest" / "AirfixDogfighter.app"
    populate_app(app)
    artifact, manifest = package(root, app, "-manifest")
    document = json.loads(manifest.read_text(encoding="utf-8"))

    def rejected(mutator, fragment: str) -> None:
        candidate = copy.deepcopy(document)
        mutator(candidate)
        path = root / (fragment.replace(" ", "-") + ".json")
        path.write_text(json.dumps(candidate), encoding="utf-8")
        expect_failure(
            lambda: ipa.load_and_validate_manifest(path, artifact, SOURCE_SHA), fragment
        )

    rejected(lambda value: value.update({"extra": 1}), "top-level fields")
    rejected(lambda value: value["artifact"].update({"signed": True}), "does not match")
    rejected(lambda value: value["contentPolicy"].update({"dataLess": False}), "content policy")
    rejected(lambda value: value["contentPolicy"].update({"dataLess": 1}), "content policy")
    rejected(lambda value: value["build"].update({"sourceSha": "b" * 40}), "unexpected")

    oversized = root / "oversized-manifest.json"
    oversized.write_bytes(b" " * (ipa.MAX_MANIFEST_BYTES + 1))
    expect_failure(
        lambda: ipa.load_and_validate_manifest(oversized, artifact, SOURCE_SHA),
        "empty or oversized",
    )
    manifest_directory = root / "manifest-directory"
    manifest_directory.mkdir()
    expect_failure(
        lambda: ipa.load_and_validate_manifest(manifest_directory, artifact, SOURCE_SHA),
        "not a regular file",
    )
    ipa_directory = root / "ipa-directory"
    ipa_directory.mkdir()
    expect_failure(
        lambda: ipa.load_and_validate_manifest(manifest, ipa_directory, SOURCE_SHA),
        "not a regular file",
    )

    lf_lock = root / "lock-lf.md"
    crlf_lock = root / "lock-crlf.md"
    lf_lock.write_bytes(b"first\nsecond\n")
    crlf_lock.write_bytes(b"first\r\nsecond\r\n")
    if ipa.sha256_normalized_text_file(
        lf_lock, "dependency lock"
    ) != ipa.sha256_normalized_text_file(crlf_lock, "dependency lock"):
        raise AssertionError("dependency lock identity depends on checkout line endings")
    bare_cr_lock = root / "lock-bare-cr.md"
    bare_cr_lock.write_bytes(b"first\rsecond\r")
    expect_failure(
        lambda: ipa.sha256_normalized_text_file(bare_cr_lock, "dependency lock"),
        "non-canonical line ending",
    )
    oversized_lock = root / "lock-oversized.md"
    oversized_lock.write_bytes(b"x" * (ipa.MAX_DEPENDENCY_LOCK_BYTES + 1))
    expect_failure(
        lambda: ipa.sha256_normalized_text_file(oversized_lock, "dependency lock"),
        "empty or oversized",
    )
    invalid_utf8_lock = root / "lock-invalid-utf8.md"
    invalid_utf8_lock.write_bytes(b"valid\n\xff\n")
    expect_failure(
        lambda: ipa.sha256_normalized_text_file(
            invalid_utf8_lock, "dependency lock"
        ),
        "not valid UTF-8",
    )


def require_text(path: Path, fragments: tuple[str, ...]) -> str:
    text = path.read_text(encoding="utf-8")
    for fragment in fragments:
        if fragment not in text:
            raise AssertionError(f"{path.name} is missing required policy: {fragment}")
    return text


def test_repository_policy() -> None:
    workflow = require_text(
        REPOSITORY_ROOT / ".github" / "workflows" / "ios-unsigned.yml",
        (
            "configuration=Release",
            "package_ios_unsigned_ipa.py package",
            "xcrun --sdk iphoneos strip -S -x",
            "if: matrix.sdk == 'iphoneos'",
            "github.event_name != 'pull_request'",
            "github.ref == 'refs/heads/main'",
            "AirfixDogfighter-unsigned-${{ github.sha }}",
            "xcrun --sdk iphoneos clang --version",
            "retention-days: 7",
            "compression-level: 0",
            "actions/upload-artifact@043fb46d1a93c77aae656e7c1c64a875d1fc6a0a",
        ),
    )
    if "BUILD_CERTIFICATE" in workflow or "mobileprovision" in workflow:
        raise AssertionError("public unsigned workflow references signing material")
    for obsolete in (
        REPOSITORY_ROOT / ".github" / "workflows" / "ios-private-ipa.yml",
        REPOSITORY_ROOT / "tools" / "ci" / "build_ios_private_ipa.sh",
    ):
        if obsolete.exists():
            raise AssertionError(f"obsolete hosted signing path remains: {obsolete.name}")

    root_cmake = require_text(
        REPOSITORY_ROOT / "CMakeLists.txt",
        (
            "AIRFIX_IOS_ENABLE_LOCAL_SIGNING",
            "AIRFIX_IOS_LOCAL_DEVELOPMENT_TEAM",
            "must be a 10-character Apple team ID",
            "-ffile-prefix-map=${CMAKE_BINARY_DIR}=airfix-build",
            "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=airfix-source",
            "-fdebug-compilation-dir=airfix-build",
        ),
    )
    if "AIRFIX_IOS_ENABLE_SIGNING" in root_cmake:
        raise AssertionError("obsolete hosted-signing CMake option remains")
    require_text(
        REPOSITORY_ROOT / "apps" / "airfix-ios" / "CMakeLists.txt",
        (
            "XCODE_ATTRIBUTE_CODE_SIGN_STYLE",
            "XCODE_ATTRIBUTE_DEVELOPMENT_TEAM",
            "AIRFIX_IOS_LOCAL_DEVELOPMENT_TEAM",
        ),
    )
    require_text(
        REPOSITORY_ROOT / "tools" / "ios" / "configure-local-xcode.sh",
        (
            "--source-sha",
            "tracked source changes must be committed or removed",
            "AIRFIX_IOS_ENABLE_LOCAL_SIGNING=ON",
            "AIRFIX_IOS_INITIAL_SETUP_LOGICAL_PATH_BASE64=",
            "Product > Run",
        ),
    )


def test_cmake_local_signing_gate() -> None:
    compiler = shutil.which("g++") or shutil.which("clang++")
    if compiler is None:
        raise AssertionError("no C++ compiler is available for the CMake gate test")
    with tempfile.TemporaryDirectory(prefix="airfix-ios-local-signing-cmake-") as raw:
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
                "-DAIRFIX_IOS_ENABLE_LOCAL_SIGNING=ON",
            ],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=30,
        )
    if result.returncode == 0:
        raise AssertionError("CMake accepted local signing without the iOS app")
    expected = "AIRFIX_IOS_ENABLE_LOCAL_SIGNING requires AIRFIX_BUILD_IOS_APP=ON"
    if expected not in result.stdout:
        raise AssertionError("CMake local-signing failure was not the fail-closed gate")


def test_command_line_round_trip(root: Path) -> None:
    app = root / "cli-source" / "AirfixDogfighter.app"
    populate_app(app)
    output_root = root / "cli-output"
    artifact = output_root / f"AirfixDogfighter-{SOURCE_SHA}-unsigned.ipa"
    manifest = output_root / "build-manifest.json"
    package_result = subprocess.run(
        [
            sys.executable,
            str(MODULE_PATH),
            "package",
            "--app",
            str(app),
            "--ipa-output",
            str(artifact),
            "--manifest-output",
            str(manifest),
            "--source-sha",
            SOURCE_SHA,
            "--workflow-run-id",
            "12345",
            "--runner-image",
            "macos-26/20260801.1",
            "--xcode-version",
            "Xcode 26.6 Build version 17G86",
            "--compiler",
            "Apple clang version 18.0.0",
            "--sdk-version",
            "26.6",
            "--dependency-lock",
            str(REPOSITORY_ROOT / "docs" / "toolchain" / "LOCK.md"),
        ],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=30,
    )
    if package_result.returncode != 0:
        raise AssertionError(f"package CLI failed: {package_result.stdout}")
    verify_result = subprocess.run(
        [
            sys.executable,
            str(MODULE_PATH),
            "verify",
            "--ipa",
            str(artifact),
            "--manifest",
            str(manifest),
            "--expected-source-sha",
            SOURCE_SHA,
            "--dependency-lock",
            str(REPOSITORY_ROOT / "docs" / "toolchain" / "LOCK.md"),
        ],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=30,
    )
    if verify_result.returncode != 0:
        raise AssertionError(f"verify CLI failed: {verify_result.stdout}")


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="airfix-ios-unsigned-tests-") as raw:
        root = Path(raw)
        test_positive_and_deterministic(root)
        test_source_boundary(root)
        test_macho_contract(root)
        test_archive_boundary(root)
        test_manifest_boundary(root)
        test_command_line_round_trip(root)
    test_repository_policy()
    test_cmake_local_signing_gate()
    print("iOS unsigned package tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
