#!/usr/bin/env python3
"""Repository-wide documentation, experiment, and function-ledger checks."""

from __future__ import annotations

from collections import Counter
import csv
from pathlib import Path, PurePosixPath
import re
import subprocess
from urllib.parse import unquote, urlsplit


REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
EXPERIMENTS_ROOT = REPOSITORY_ROOT / "docs" / "experiments"
LOG_PATH = REPOSITORY_ROOT / "docs" / "progress" / "LOG.md"
FUNCTION_CATALOG = REPOSITORY_ROOT / "docs" / "re" / "FUNCTION-CATALOG.csv"

EXPERIMENT_ID = re.compile(r"^(EXP-[0-9]{8}-[0-9]{3})(?:-|\.md)")
EVIDENCE_ID = re.compile(r"^EV-[0-9]{8}-[0-9]{3}$")
EVIDENCE_REFERENCE = re.compile(r"EV-[0-9]{8}-[0-9]{3}")
INLINE_LINK = re.compile(r"(?<!!)\[[^\]\n]+\]\(([^)\n]+)\)")
REFERENCE_LINK = re.compile(r"^\s*\[[^\]]+\]:\s*(\S+)")
SCHEME = re.compile(r"^[A-Za-z][A-Za-z0-9+.-]*:")

EXPECTED_FUNCTION_COLUMNS = [
    "function_id",
    "module",
    "rva",
    "original_symbol",
    "working_name",
    "system",
    "state",
    "confidence",
    "calls",
    "called_by",
    "evidence_ids",
    "scenario_ids",
    "source_path",
    "notes",
]

# These two unresolved catalogue references predate this gate and remain
# explicit research backlog. No new unresolved reference is accepted.
ALLOWED_MISSING_FUNCTION_IDS = {
    "FN-CC-00021080",
    "FN-CC-00021150",
}


def markdown_link_targets(path: Path) -> list[tuple[int, str]]:
    targets: list[tuple[int, str]] = []
    in_fence = False
    for line_number, line in enumerate(
        path.read_text(encoding="utf-8").splitlines(), start=1
    ):
        stripped = line.lstrip()
        if stripped.startswith("```") or stripped.startswith("~~~"):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        targets.extend((line_number, match.group(1)) for match in INLINE_LINK.finditer(line))
        reference = REFERENCE_LINK.match(line)
        if reference:
            targets.append((line_number, reference.group(1)))
    return targets


def local_link_target(raw: str) -> str | None:
    target = raw.strip()
    if target.startswith("<") and ">" in target:
        target = target[1 : target.index(">")]
    else:
        target = target.split(maxsplit=1)[0]
    if not target or target.startswith("#") or SCHEME.match(target):
        return None
    parsed = urlsplit(target)
    if parsed.scheme or parsed.netloc:
        return None
    return unquote(parsed.path)


def exact_case_exists(path: Path) -> bool:
    try:
        relative = path.relative_to(REPOSITORY_ROOT)
    except ValueError:
        return False
    current = REPOSITORY_ROOT
    for component in relative.parts:
        try:
            names = {entry.name for entry in current.iterdir()}
        except OSError:
            return False
        if component not in names:
            return False
        current /= component
    return current.exists()


def check_markdown_links() -> list[str]:
    issues: list[str] = []
    result = subprocess.run(
        ["git", "-C", str(REPOSITORY_ROOT), "ls-files", "-z", "*.md"],
        check=True,
        capture_output=True,
    )
    markdown_files = [
        REPOSITORY_ROOT / raw.decode("utf-8", errors="surrogateescape")
        for raw in result.stdout.split(b"\0")
        if raw
    ]
    for markdown in sorted(markdown_files):
        for line_number, raw_target in markdown_link_targets(markdown):
            target = local_link_target(raw_target)
            if target is None:
                continue
            candidate = (markdown.parent / target).resolve()
            if not exact_case_exists(candidate):
                relative = markdown.relative_to(REPOSITORY_ROOT).as_posix()
                issues.append(f"{relative}:{line_number}: missing local link {target!r}")
    return issues


def check_experiment_ids() -> list[str]:
    issues: list[str] = []
    ids: list[str] = []
    for report in sorted(EXPERIMENTS_ROOT.glob("EXP-*.md")):
        match = EXPERIMENT_ID.match(report.name)
        if not match:
            issues.append(f"malformed experiment filename: {report.name}")
            continue
        experiment_id = match.group(1)
        ids.append(experiment_id)
    for experiment_id, count in Counter(ids).items():
        if count != 1:
            issues.append(f"duplicate experiment ID {experiment_id}: {count} files")
    return issues


def split_ids(raw: str) -> list[str]:
    return [entry for entry in raw.split(";") if entry]


def check_function_catalog() -> list[str]:
    issues: list[str] = []
    with FUNCTION_CATALOG.open("r", encoding="utf-8", newline="") as source:
        reader = csv.DictReader(source)
        if reader.fieldnames != EXPECTED_FUNCTION_COLUMNS:
            return ["FUNCTION-CATALOG.csv has an unexpected 14-column header"]
        rows = list(reader)

    function_ids = [row["function_id"] for row in rows]
    function_id_set = set(function_ids)
    for function_id, count in Counter(function_ids).items():
        if count != 1:
            issues.append(f"duplicate function ID {function_id}: {count} rows")

    locations = [(row["module"], row["rva"].upper()) for row in rows]
    for location, count in Counter(locations).items():
        if count != 1:
            issues.append(
                f"duplicate module/RVA {location[0]}:{location[1]}: {count} rows"
            )

    evidence_text = LOG_PATH.read_text(encoding="utf-8")
    for report in EXPERIMENTS_ROOT.glob("EXP-*.md"):
        evidence_text += "\n" + report.read_text(encoding="utf-8")
    documented_evidence = set(EVIDENCE_REFERENCE.findall(evidence_text))
    unresolved: set[str] = set()
    for row_number, row in enumerate(rows, start=2):
        if None in row or any(row[column] is None for column in EXPECTED_FUNCTION_COLUMNS):
            issues.append(
                f"FUNCTION-CATALOG.csv:{row_number}: row does not have 14 columns"
            )
            continue
        function_id = row["function_id"]
        if not function_id or not row["module"] or not row["rva"]:
            issues.append(f"FUNCTION-CATALOG.csv:{row_number}: incomplete identity")
        if not re.fullmatch(r"[0-9A-Fa-f]{8}", row["rva"]):
            issues.append(f"FUNCTION-CATALOG.csv:{row_number}: invalid RVA")
        if row["confidence"] not in {"1", "2", "3"}:
            issues.append(f"FUNCTION-CATALOG.csv:{row_number}: invalid confidence")
        for relation_column in ("calls", "called_by"):
            for related in split_ids(row[relation_column]):
                if related not in function_id_set:
                    unresolved.add(related)
        for evidence in split_ids(row["evidence_ids"]):
            if not EVIDENCE_ID.fullmatch(evidence):
                issues.append(
                    f"FUNCTION-CATALOG.csv:{row_number}: malformed evidence ID"
                )
            elif evidence not in documented_evidence:
                issues.append(
                    f"FUNCTION-CATALOG.csv:{row_number}: {evidence} has no report or log entry"
                )

    unexpected = unresolved - ALLOWED_MISSING_FUNCTION_IDS
    stale_allowlist = ALLOWED_MISSING_FUNCTION_IDS - unresolved
    if unexpected:
        issues.append(f"new unresolved function references: {sorted(unexpected)!r}")
    if stale_allowlist:
        issues.append(
            "resolved function references remain in the CI allowlist: "
            f"{sorted(stale_allowlist)!r}"
        )
    return issues


def main() -> int:
    issues = check_markdown_links()
    issues.extend(check_experiment_ids())
    issues.extend(check_function_catalog())
    if issues:
        for issue in issues:
            print(f"documentation-integrity: {issue}")
        return 1
    report_count = len(list(EXPERIMENTS_ROOT.glob("EXP-*.md")))
    with FUNCTION_CATALOG.open("r", encoding="utf-8", newline="") as source:
        function_count = sum(1 for _ in csv.DictReader(source))
    print(
        "Documentation integrity passed: "
        f"{report_count} experiments, {function_count} catalogue rows."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
