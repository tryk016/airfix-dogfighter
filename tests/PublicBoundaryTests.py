#!/usr/bin/env python3
"""Synthetic tests for the public-source boundary scanner."""

from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import tempfile


REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
SCANNER = REPOSITORY_ROOT / "tools" / "ci" / "check_public_boundary.py"


def run(*args: str, cwd: Path, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        args,
        cwd=cwd,
        check=check,
        capture_output=True,
        text=True,
    )


def scan(root: Path) -> subprocess.CompletedProcess[str]:
    return run(sys.executable, str(SCANNER), str(root), cwd=REPOSITORY_ROOT, check=False)


def assert_blocked(root: Path, relative: str, expected: str) -> None:
    target = root / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(b"synthetic test only\n")
    run("git", "add", "-f", "--", relative, cwd=root)
    result = scan(root)
    combined = result.stdout + result.stderr
    if result.returncode != 1 or expected not in combined:
        raise AssertionError(
            f"{relative!r} was not blocked as expected\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    run("git", "rm", "--cached", "--", relative, cwd=root)


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="airfix-public-boundary-") as raw:
        root = Path(raw)
        run("git", "init", "-q", cwd=root)
        (root / "README.md").write_text("synthetic repository\n", encoding="utf-8")
        run("git", "add", "--", "README.md", cwd=root)

        clean = scan(root)
        if clean.returncode != 0:
            raise AssertionError(
                f"clean synthetic repository failed\n{clean.stdout}\n{clean.stderr}"
            )

        assert_blocked(
            root,
            "analysis/tools/vendor.bin",
            "forbidden local-analysis directory",
        )
        assert_blocked(root, "notes/function.bndb", "forbidden file type")
        assert_blocked(root, "notes/session.trace32", "forbidden file type")
        assert_blocked(root, "notes/session.dd32.bak", "forbidden file type")

    print("Public boundary tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
