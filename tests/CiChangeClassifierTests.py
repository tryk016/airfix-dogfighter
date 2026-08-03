#!/usr/bin/env python3
"""Synthetic tests for the conservative CI change classifier."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import subprocess
import tempfile


REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
CLASSIFIER_PATH = REPOSITORY_ROOT / "tools" / "ci" / "classify_ci_changes.py"
SPEC = importlib.util.spec_from_file_location("classify_ci_changes", CLASSIFIER_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("unable to import CI change classifier")
CLASSIFIER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CLASSIFIER)


def run(*args: str, cwd: Path) -> None:
    subprocess.run(args, cwd=cwd, check=True, capture_output=True)


def assert_policy(paths: list[str], expected_docs_only: bool) -> None:
    docs_only, _ = CLASSIFIER.classify_paths(paths)
    if docs_only != expected_docs_only:
        raise AssertionError(
            f"unexpected classification for {paths!r}: docs_only={docs_only}"
        )


def main() -> int:
    assert_policy(["README.md", "docs/adr/0020-example.md"], True)
    assert_policy(["LICENSE.md", "LICENSE.txt"], True)
    assert_policy(["LICENSE.cpp"], False)
    assert_policy(["docs/re/FUNCTION-CATALOG.csv", "docs/evidence/a.sha256"], True)
    assert_policy([".github/ISSUE_TEMPLATE/research.md"], True)
    assert_policy(["src/airfix/game/Flight.cpp"], False)
    assert_policy(["tests/FlightTests.cpp"], False)
    assert_policy(["CMakeLists.txt"], False)
    assert_policy([".github/workflows/portable-ci.yml"], False)
    assert_policy(["docs/schema/runtime.json"], False)
    assert_policy(["docs/../src/Flight.cpp"], False)
    assert_policy(["docs/report.md\nfull_build=false"], False)
    assert_policy([], True)

    with tempfile.TemporaryDirectory(prefix="airfix-ci-policy-") as raw:
        repository = Path(raw)
        run("git", "init", "-q", cwd=repository)
        run("git", "config", "user.email", "synthetic@example.invalid", cwd=repository)
        run("git", "config", "user.name", "Synthetic Test", cwd=repository)
        (repository / "README.md").write_text("initial\n", encoding="utf-8")
        run("git", "add", "--", "README.md", cwd=repository)
        run("git", "commit", "-qm", "initial", cwd=repository)
        base = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=repository,
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
        (repository / "README.md").write_text("updated\n", encoding="utf-8")
        run("git", "commit", "-qam", "docs", cwd=repository)
        head = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=repository,
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
        paths = CLASSIFIER.changed_paths(repository, base, head, False)
        if paths != ["README.md"]:
            raise AssertionError(f"unexpected git range result: {paths!r}")

        source = repository / "src" / "Flight.cpp"
        source.parent.mkdir()
        source.write_text("// synthetic\n", encoding="utf-8")
        run("git", "add", "--", "src/Flight.cpp", cwd=repository)
        run("git", "commit", "-qm", "source", cwd=repository)
        rename_base = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=repository,
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
        (repository / "docs").mkdir()
        run(
            "git",
            "mv",
            "--",
            "src/Flight.cpp",
            "docs/Flight.md",
            cwd=repository,
        )
        run("git", "commit", "-qm", "rename source to docs", cwd=repository)
        rename_head = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=repository,
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
        renamed_paths = CLASSIFIER.changed_paths(
            repository, rename_base, rename_head, False
        )
        if CLASSIFIER.classify_paths(renamed_paths)[0]:
            raise AssertionError(
                f"source-to-doc rename bypassed the full build: {renamed_paths!r}"
            )

        output = repository / "github-output.txt"
        CLASSIFIER.append_github_outputs(
            output, False, "unsafe\nfull_build=false\rstill unsafe"
        )
        output_lines = output.read_text(encoding="utf-8").splitlines()
        if output_lines != [
            "docs_only=false",
            "full_build=true",
            "reason=unsafe?full_build=false?still unsafe",
        ]:
            raise AssertionError(f"unsafe GitHub output encoding: {output_lines!r}")

    print("CI change classifier tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
