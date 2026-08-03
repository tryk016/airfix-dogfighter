#!/usr/bin/env python3
"""Classify a revision range for the conservative documentation-only CI path."""

from __future__ import annotations

import argparse
from pathlib import Path, PurePosixPath
import subprocess
import sys


DOCUMENT_EXTENSIONS = {
    ".csv",
    ".md",
    ".mmd",
    ".sha256",
    ".txt",
}
ROOT_DOCUMENTS = {
    "CODE_OF_CONDUCT.md",
    "CONTRIBUTING.md",
    "LICENSE",
    "README.md",
    "SECURITY.md",
}


def normalize_repository_path(raw: str) -> str | None:
    if any(not character.isprintable() for character in raw):
        return None
    normalized = raw.replace("\\", "/")
    path = PurePosixPath(normalized)
    if not normalized or path.is_absolute() or ".." in path.parts:
        return None
    return path.as_posix()


def is_documentation_only_path(raw: str) -> bool:
    normalized = normalize_repository_path(raw)
    if normalized is None:
        return False
    path = PurePosixPath(normalized)
    if normalized in ROOT_DOCUMENTS:
        return True
    if len(path.parts) == 1 and path.stem == "LICENSE":
        return path.suffix.lower() in {".md", ".txt"}
    if path.parts and path.parts[0] == "docs":
        return path.suffix.lower() in DOCUMENT_EXTENSIONS
    if len(path.parts) >= 2 and path.parts[0] == ".github":
        return path.suffix.lower() == ".md"
    return False


def classify_paths(paths: list[str]) -> tuple[bool, list[str]]:
    full_build_paths = sorted(
        path for path in paths if not is_documentation_only_path(path)
    )
    return (not full_build_paths, full_build_paths)


def changed_paths(
    repository: Path, base: str, head: str, three_dot: bool
) -> list[str]:
    separator = "..." if three_dot else ".."
    revision_range = f"{base}{separator}{head}"
    result = subprocess.run(
        [
            "git",
            "-C",
            str(repository),
            "diff",
            "--no-renames",
            "--name-only",
            "--diff-filter=ACDMRTUXB",
            "-z",
            revision_range,
        ],
        check=True,
        capture_output=True,
    )
    return [
        entry.decode("utf-8", errors="surrogateescape")
        for entry in result.stdout.split(b"\0")
        if entry
    ]


def append_github_outputs(path: Path, docs_only: bool, reason: str) -> None:
    safe_reason = "".join(
        character if character.isprintable() else "?" for character in reason
    )
    safe_reason = safe_reason.encode("utf-8", errors="replace").decode("utf-8")[:500]
    with path.open("a", encoding="utf-8", newline="\n") as output:
        output.write(f"docs_only={'true' if docs_only else 'false'}\n")
        output.write(f"full_build={'false' if docs_only else 'true'}\n")
        output.write(f"reason={safe_reason}\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repository", type=Path, default=Path("."))
    parser.add_argument("--base")
    parser.add_argument("--head")
    parser.add_argument("--three-dot", action="store_true")
    parser.add_argument("--force-full", action="store_true")
    parser.add_argument("--github-output", type=Path)
    args = parser.parse_args()

    if args.force_full:
        if args.base or args.head or args.three_dot:
            parser.error("--force-full cannot be combined with revision arguments")
        docs_only = False
        reason = "non-PR event requires the complete build matrix"
    else:
        if not args.base or not args.head:
            parser.error("--base and --head are required unless --force-full is used")
        try:
            paths = changed_paths(
                args.repository.resolve(), args.base, args.head, args.three_dot
            )
        except subprocess.CalledProcessError as error:
            print(
                "ci-change-policy: revision classification failed; requesting "
                "the complete build matrix",
                file=sys.stderr,
            )
            print(error.stderr.decode("utf-8", errors="replace"), file=sys.stderr)
            docs_only = False
            reason = "revision classification failed closed"
        else:
            docs_only, full_build_paths = classify_paths(paths)
            if not paths:
                docs_only = False
                reason = "empty revision range failed closed"
            elif docs_only:
                reason = f"all {len(paths)} changed paths are documentation-only"
            else:
                preview = ", ".join(full_build_paths[:3])
                if len(full_build_paths) > 3:
                    preview += f", and {len(full_build_paths) - 3} more"
                reason = f"build-affecting paths: {preview}"

    print(
        "ci-change-policy: "
        f"docs_only={'true' if docs_only else 'false'}; {reason}"
    )
    if args.github_output:
        append_github_outputs(args.github_output, docs_only, reason)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
