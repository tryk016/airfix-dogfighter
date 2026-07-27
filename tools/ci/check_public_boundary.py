#!/usr/bin/env python3
"""Fail when tracked or pending files cross the public-source boundary."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys


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
    ".ipa",
    ".p12",
    ".cer",
    ".mobileprovision",
    ".provisionprofile",
    ".bndb",
    ".rzdb",
    ".dd32",
    ".dd64",
    ".trace32",
    ".trace64",
}

BLOCKED_PATH_PREFIXES = {
    "analysis/tool-cache/",
    "analysis/tools/",
    "analysis/work-copies/",
    "analysis/ghidra-appdata/",
    "analysis/rizin-projects/",
    "analysis/cutter-projects/",
    "analysis/binary-ninja-projects/",
    "analysis/x64dbg-workspace/",
}

BLOCKED_FILE_ENDINGS = {
    ".dd32.bak",
    ".dd64.bak",
}

BLOCKED_DIRECTORY_NAMES = {
    "artifacts",
    "original",
    "private-fixtures",
    "ghidra-projects",
    "dumps",
    "traces",
    "screenshots",
    "signing",
}

MAX_PUBLIC_FILE_BYTES = 5 * 1024 * 1024


def repository_paths(root: Path) -> list[str]:
    result = subprocess.run(
        [
            "git",
            "-C",
            os.fspath(root),
            "ls-files",
            "--cached",
            "--others",
            "--exclude-standard",
            "-z",
        ],
        check=True,
        capture_output=True,
    )
    return [
        raw.decode("utf-8", errors="surrogateescape")
        for raw in result.stdout.split(b"\0")
        if raw
    ]


def inspect(root: Path) -> list[str]:
    issues: list[str] = []
    resolved_root = root.resolve()
    for relative in repository_paths(root):
        normalized = relative.replace("\\", "/")
        lowered = normalized.lower()
        parts = [part.lower() for part in normalized.split("/")]
        suffix = Path(normalized).suffix.lower()

        if suffix in BLOCKED_SUFFIXES:
            issues.append(f"forbidden file type: {normalized}")
        if any(lowered.endswith(ending) for ending in BLOCKED_FILE_ENDINGS):
            issues.append(f"forbidden file type: {normalized}")
        if any(lowered.startswith(prefix) for prefix in BLOCKED_PATH_PREFIXES):
            issues.append(f"forbidden local-analysis directory: {normalized}")
        if any(part in BLOCKED_DIRECTORY_NAMES for part in parts[:-1]):
            issues.append(f"forbidden source directory: {normalized}")

        candidate = root / relative
        try:
            resolved_candidate = candidate.resolve(strict=True)
            resolved_candidate.relative_to(resolved_root)
        except (FileNotFoundError, RuntimeError, ValueError):
            issues.append(f"missing, cyclic, or external file: {normalized}")
            continue

        if not candidate.is_file():
            issues.append(f"non-regular repository entry: {normalized}")
            continue
        size = candidate.stat().st_size
        if size > MAX_PUBLIC_FILE_BYTES:
            issues.append(
                f"public file exceeds {MAX_PUBLIC_FILE_BYTES} bytes: "
                f"{normalized} ({size})"
            )
    return issues


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    if not (root / ".git").exists():
        print(f"public-boundary: not a Git worktree: {root}", file=sys.stderr)
        return 2
    issues = inspect(root)
    if issues:
        for issue in issues:
            print(f"public-boundary: {issue}", file=sys.stderr)
        return 1
    print(f"public-boundary: OK ({len(repository_paths(root))} files checked)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
