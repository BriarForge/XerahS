#!/usr/bin/env python3
"""NuGet freshness gate (ROOT-NUGET-001/002, CORE-031, CORE-032).

Finds every repository-owned NuGet manifest and requires a check record for each
package it declares in tools/nuget-freshness/records.json:

  {"checks": [{"package": "...", "manifest": "path", "source": "https://api.nuget.org/v3/index.json",
               "checked_at": "2026-11-03T00:00:00Z", "previous_version": "...",
               "latest_stable": "...", "selected_version": "...", "verification": "passed",
               "retention": {"rejected_version": "...", "blocker": "...", "evidence": "...",
                             "owner": "...", "remediation": "...", "next_review": "2026-12-01"}}]}

A record is stale after 30 days. A selected version below latest_stable needs a
complete retention block whose next_review is at most 30 days after checked_at.
With no NuGet manifests in the repository the gate passes and says so.
Standard library only.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import re
import sys
from pathlib import Path

MANIFEST_NAMES = {"directory.packages.props", "packages.config", "packages.lock.json", "nuget.config"}
MANIFEST_SUFFIXES = (".csproj", ".fsproj", ".vbproj", ".nuspec", ".props", ".targets")
SKIP_DIRS = {".git", "build", "worktrees", "node_modules", "graphify-out", "bin", "obj"}
REQUIRED = ("package", "manifest", "source", "checked_at", "previous_version", "latest_stable",
            "selected_version", "verification")
RETENTION = ("rejected_version", "blocker", "evidence", "owner", "remediation", "next_review")
MAX_AGE = dt.timedelta(days=30)
PACKAGE_REF = re.compile(r'<(?:PackageReference|PackageVersion|package)\b[^>]*?(?:Include|id)="([^"]+)"', re.I)


def manifests(root: Path) -> list[Path]:
    found = []
    for path in root.rglob("*"):
        if any(part in SKIP_DIRS for part in path.relative_to(root).parts[:-1]) or not path.is_file():
            continue
        name = path.name.lower()
        if name in MANIFEST_NAMES or (name.endswith(MANIFEST_SUFFIXES) and name != "cmakelists.txt"):
            if name.endswith((".props", ".targets")) and "PackageReference" not in path.read_text(errors="ignore") \
                    and "PackageVersion" not in path.read_text(errors="ignore"):
                continue
            found.append(path)
    return sorted(found)


def packages(path: Path) -> set[str]:
    text = path.read_text(errors="ignore")
    if path.name.lower() == "packages.lock.json":
        try:
            data = json.loads(text)
        except json.JSONDecodeError:
            return {"<unreadable lock file>"}
        return {name for deps in data.get("dependencies", {}).values() for name in deps}
    return set(PACKAGE_REF.findall(text))


def parse_time(value: str) -> dt.datetime | None:
    try:
        parsed = dt.datetime.fromisoformat(value.replace("Z", "+00:00"))
    except (TypeError, ValueError):
        return None
    return parsed if parsed.tzinfo else parsed.replace(tzinfo=dt.timezone.utc)


def version_key(value: str) -> tuple:
    return tuple(int(p) if p.isdigit() else p for p in re.split(r"[.\-+]", value))


def check(root: Path, records_path: Path, now: dt.datetime) -> list[str]:
    errors: list[str] = []
    records = []
    if records_path.exists():
        try:
            records = json.loads(records_path.read_text())["checks"]
        except (json.JSONDecodeError, KeyError, TypeError):
            return [f"{records_path}: not a valid records file"]
    by_package = {}
    for record in records:
        missing = [k for k in REQUIRED if not record.get(k)]
        if missing:
            errors.append(f"record {record.get('package', '?')}: missing {', '.join(missing)}")
            continue
        checked = parse_time(record["checked_at"])
        if checked is None:
            errors.append(f"record {record['package']}: checked_at is not ISO 8601")
            continue
        if now - checked > MAX_AGE:
            errors.append(f"record {record['package']}: last check {record['checked_at']} is older than 30 days")
        if record["verification"] != "passed":
            errors.append(f"record {record['package']}: verification is {record['verification']!r}, not 'passed'")
        if version_key(record["selected_version"]) < version_key(record["latest_stable"]):
            retention = record.get("retention") or {}
            gaps = [k for k in RETENTION if not retention.get(k)]
            review = parse_time(retention.get("next_review", ""))
            if gaps:
                errors.append(f"record {record['package']}: below latest stable without retention fields {', '.join(gaps)}")
            elif review is None or review - checked > MAX_AGE:
                errors.append(f"record {record['package']}: retention next_review must be within 30 days of the check")
        by_package[record["package"].lower()] = record
    found = manifests(root)
    for manifest in found:
        for package in sorted(packages(manifest)):
            if package.lower() not in by_package:
                errors.append(f"{manifest.relative_to(root)}: {package} has no NuGet check record")
    if not found:
        print("nuget-freshness: no repository-owned NuGet manifests; nothing to check")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--records", type=Path)
    parser.add_argument("--now", help="ISO time override for tests")
    args = parser.parse_args()
    records = args.records or args.root / "tools/nuget-freshness/records.json"
    now = parse_time(args.now) if args.now else dt.datetime.now(dt.timezone.utc)
    errors = check(args.root, records, now)
    for error in errors:
        print(f"nuget-freshness: {error}", file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
