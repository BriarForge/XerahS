#!/usr/bin/env python3
"""Validate and render the progressive Product Contract completion backlog."""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass
from datetime import date
from pathlib import Path
from typing import Any


TOOL_VERSION = "1.0.0"
ROOT = Path(__file__).resolve().parents[2]
BACKLOG_PATH = ROOT / "product-contract" / "backlog" / "backlog.json"
STATUS_PATH = ROOT / "product-contract" / "backlog" / "STATUS.md"
MANIFEST_PATH = ROOT / "product-contract" / "manifest.yaml"
BASELINE_PATH = ROOT / "product-contract" / "reference-baselines" / "kova-0.29.0.yaml"
LEDGER_DIR = ROOT / "product-contract" / "parity"
LEDGERS = ("capability", "settings", "workflow", "interface", "compatibility")


@dataclass(frozen=True)
class LedgerRow:
    ledger: str
    row_id: str
    domain: str
    contract: str | None


def _yaml_scalar(raw: str) -> str | None:
    value = raw.strip()
    if value == "null":
        return None
    if len(value) >= 2 and value[0] == value[-1] == '"':
        return json.loads(value)
    return value


def parse_ledger(path: Path, ledger: str) -> list[LedgerRow]:
    """Parse only stable top-level row fields without adding a YAML dependency."""
    rows: list[LedgerRow] = []
    current: dict[str, str | None] | None = None
    for line in path.read_text(encoding="utf-8").splitlines():
        match = re.match(r'^  - id: (.+)$', line)
        if match:
            if current is not None:
                rows.append(_finish_row(current, ledger, path))
            current = {"id": _yaml_scalar(match.group(1))}
            continue
        if current is None:
            continue
        match = re.match(r'^    (domain|contract): (.+)$', line)
        if match:
            current[match.group(1)] = _yaml_scalar(match.group(2))
    if current is not None:
        rows.append(_finish_row(current, ledger, path))
    return rows


def _finish_row(values: dict[str, str | None], ledger: str, path: Path) -> LedgerRow:
    missing = [field for field in ("id", "domain", "contract") if field not in values]
    if missing:
        raise ValueError(f"{path}: ledger row is missing {', '.join(missing)}")
    row_id = values["id"]
    domain = values["domain"]
    if not isinstance(row_id, str) or not isinstance(domain, str):
        raise ValueError(f"{path}: ledger row id and domain must be strings")
    return LedgerRow(ledger, row_id, domain, values["contract"])


def load_rows(root: Path = ROOT) -> dict[str, list[LedgerRow]]:
    ledger_dir = root / "product-contract" / "parity"
    return {
        ledger: parse_ledger(ledger_dir / f"{ledger}-ledger.yaml", ledger)
        for ledger in LEDGERS
    }


def load_backlog(path: Path = BACKLOG_PATH) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def route_row(backlog: dict[str, Any], row: LedgerRow) -> str | None:
    route = backlog.get("routing", {}).get(row.ledger, {})
    values = route.get("values", {})
    return values.get(row.domain, route.get("default"))


def routed_rows(
    backlog: dict[str, Any],
    rows: dict[str, list[LedgerRow]],
    *,
    open_only: bool,
) -> dict[str, list[LedgerRow]]:
    result: dict[str, list[LedgerRow]] = defaultdict(list)
    for ledger_rows in rows.values():
        for row in ledger_rows:
            if open_only and row.contract is not None:
                continue
            package_id = route_row(backlog, row)
            if package_id is not None:
                result[package_id].append(row)
    return dict(result)


def assignments(backlog: dict[str, Any], rows: dict[str, list[LedgerRow]]) -> dict[str, list[LedgerRow]]:
    return routed_rows(backlog, rows, open_only=True)


def validate(
    backlog: dict[str, Any],
    rows: dict[str, list[LedgerRow]],
    root: Path = ROOT,
) -> list[str]:
    findings: list[str] = []
    if backlog.get("schema_version") != 1:
        findings.append("backlog schema_version must be 1")
    if backlog.get("tracker_version") != TOOL_VERSION:
        findings.append(
            f"backlog tracker_version must match tool version {TOOL_VERSION}"
        )

    waves = backlog.get("waves", [])
    packages = backlog.get("packages", [])
    wave_ids = [wave.get("id") for wave in waves]
    package_ids = [package.get("id") for package in packages]
    for label, identifiers in (("wave", wave_ids), ("package", package_ids)):
        duplicates = sorted(key for key, count in Counter(identifiers).items() if count > 1)
        if duplicates:
            findings.append(f"duplicate {label} ids: {', '.join(duplicates)}")

    wave_by_id = {wave.get("id"): wave for wave in waves}
    package_by_id = {package.get("id"): package for package in packages}
    sequences = [wave.get("sequence") for wave in waves]
    if sequences != list(range(1, len(waves) + 1)):
        findings.append("wave sequences must be ordered, unique, and contiguous from 1")
    previous_date: date | None = None
    memberships: Counter[str] = Counter()
    for wave in waves:
        try:
            target = date.fromisoformat(wave.get("target_date", ""))
            if previous_date is not None and target < previous_date:
                findings.append("wave target dates must not move backwards")
            previous_date = target
        except (TypeError, ValueError):
            findings.append(f"wave {wave.get('id')} has an invalid target_date")
        for package_id in wave.get("package_ids", []):
            memberships[package_id] += 1
            if package_id not in package_by_id:
                findings.append(f"wave {wave.get('id')} references unknown package {package_id}")

    for package in packages:
        package_id = package.get("id")
        wave_id = package.get("wave")
        if wave_id not in wave_by_id:
            findings.append(f"package {package_id} references unknown wave {wave_id}")
        elif package_id not in wave_by_id[wave_id].get("package_ids", []):
            findings.append(f"package {package_id} is not listed by its wave {wave_id}")
        if memberships[package_id] != 1:
            findings.append(f"package {package_id} must occur in exactly one wave")
        for dependency in package.get("depends_on", []):
            if dependency not in package_by_id:
                findings.append(f"package {package_id} depends on unknown package {dependency}")
            elif dependency == package_id:
                findings.append(f"package {package_id} cannot depend on itself")
            elif package.get("status") in {"in_progress", "complete"} and package_by_id[dependency].get("status") != "complete":
                findings.append(
                    f"{package.get('status')} package {package_id} has incomplete dependency {dependency}"
                )
        if not package.get("deliverables"):
            findings.append(f"package {package_id} must define deliverables")
        if not package.get("exit_criteria"):
            findings.append(f"package {package_id} must define exit criteria")

    findings.extend(_dependency_cycle_findings(package_by_id))

    for ledger in LEDGERS:
        route = backlog.get("routing", {}).get(ledger)
        if not isinstance(route, dict):
            findings.append(f"missing routing rule for {ledger} ledger")
            continue
        if route.get("field") != "domain":
            findings.append(f"{ledger} routing field must be domain")
        route_targets = list(route.get("values", {}).values())
        if route.get("default") is not None:
            route_targets.append(route["default"])
        for package_id in route_targets:
            if package_id not in package_by_id:
                findings.append(f"{ledger} routing references unknown package {package_id}")
            elif package_by_id[package_id].get("kind") != "ledger":
                findings.append(f"{ledger} routing references non-ledger package {package_id}")

    assigned = assignments(backlog, rows)
    seen_row_ids: set[str] = set()
    for ledger in LEDGERS:
        for row in rows.get(ledger, []):
            if row.row_id in seen_row_ids:
                findings.append(f"duplicate parity row id {row.row_id}")
            seen_row_ids.add(row.row_id)
            if row.contract is None and route_row(backlog, row) is None:
                findings.append(
                    f"unassigned open row {ledger}:{row.row_id} (domain {row.domain})"
                )
    for package_id, package_rows in assigned.items():
        package = package_by_id.get(package_id)
        if package and package.get("status") == "complete" and package_rows:
            findings.append(
                f"completed package {package_id} still owns {len(package_rows)} open rows"
            )

    census_status = _read_scalar(
        root / "product-contract" / "reference-baselines" / "kova-0.29.0.yaml",
        "census_status",
    )
    governance = package_by_id.get("PC-GOVERNANCE-001", {})
    if governance.get("status") == "complete" and census_status != "closed":
        findings.append("completed governance package requires a closed baseline census")
    conformance = package_by_id.get("PC-CONFORMANCE-001", {})
    if conformance.get("status") == "complete":
        manifest_status, capability_statuses = _manifest_summary(
            root / "product-contract" / "manifest.yaml"
        )
        if any(row.contract is None for ledger_rows in rows.values() for row in ledger_rows):
            findings.append("completed conformance package requires zero open parity rows")
        if census_status != "closed":
            findings.append("completed conformance package requires a closed baseline census")
        if manifest_status != "active":
            findings.append("completed conformance package requires an active manifest")
        disallowed = sorted(status for status in capability_statuses if status not in {"approved", "active"})
        if disallowed:
            findings.append(
                "completed conformance package requires every capability to be approved or active; found "
                + ", ".join(disallowed)
            )
    return sorted(set(findings))


def _dependency_cycle_findings(package_by_id: dict[str, dict[str, Any]]) -> list[str]:
    findings: list[str] = []
    visiting: set[str] = set()
    visited: set[str] = set()

    def visit(package_id: str, path: list[str]) -> None:
        if package_id in visiting:
            start = path.index(package_id)
            findings.append("dependency cycle: " + " -> ".join(path[start:] + [package_id]))
            return
        if package_id in visited or package_id not in package_by_id:
            return
        visiting.add(package_id)
        for dependency in package_by_id[package_id].get("depends_on", []):
            visit(dependency, path + [package_id])
        visiting.remove(package_id)
        visited.add(package_id)

    for identifier in package_by_id:
        visit(identifier, [])
    return findings


def _read_scalar(path: Path, field: str) -> str:
    match = re.search(rf"^{re.escape(field)}: [\"']?([^\"'\r\n]+)", path.read_text(encoding="utf-8"), re.MULTILINE)
    return match.group(1).strip() if match else "unknown"


def _manifest_summary(path: Path) -> tuple[str, Counter[str]]:
    text = path.read_text(encoding="utf-8")
    overall = _read_scalar(path, "status")
    statuses = Counter(re.findall(r"^    status: ([a-z_]+)$", text, re.MULTILINE))
    return overall, statuses


def _escape(value: Any) -> str:
    return str(value).replace("|", "\\|").replace("\n", " ")


def render_status(
    backlog: dict[str, Any],
    rows: dict[str, list[LedgerRow]],
    root: Path = ROOT,
) -> str:
    assigned = assignments(backlog, rows)
    routed = routed_rows(backlog, rows, open_only=False)
    packages = backlog["packages"]
    package_by_id = {package["id"]: package for package in packages}
    overall_status, manifest_statuses = _manifest_summary(root / "product-contract" / "manifest.yaml")
    census_status = _read_scalar(
        root / "product-contract" / "reference-baselines" / "kova-0.29.0.yaml",
        "census_status",
    )
    total = sum(len(ledger_rows) for ledger_rows in rows.values())
    open_count = sum(1 for ledger_rows in rows.values() for row in ledger_rows if row.contract is None)
    linked = total - open_count

    lines = [
        "# Product Contract completion status",
        "",
        "<!-- Generated by tools/contract-backlog/tracker.py; do not edit. -->",
        "",
        f"Baseline: `{backlog['baseline']}` · Tracker: `{backlog['tracker_version']}` · Census: `{census_status}` · Manifest: `{overall_status}`",
        "",
        f"**{linked:,} of {total:,} parity rows linked ({linked / total:.1%}); {open_count:,} remain.**",
        "",
        "## Ledger coverage",
        "",
        "| Ledger | Total | Linked | Open | Coverage |",
        "|---|---:|---:|---:|---:|",
    ]
    for ledger in LEDGERS:
        ledger_total = len(rows[ledger])
        ledger_open = sum(row.contract is None for row in rows[ledger])
        ledger_linked = ledger_total - ledger_open
        coverage = ledger_linked / ledger_total if ledger_total else 1.0
        lines.append(f"| {ledger} | {ledger_total:,} | {ledger_linked:,} | {ledger_open:,} | {coverage:.1%} |")

    status_text = ", ".join(
        f"{status} {count}" for status, count in sorted(manifest_statuses.items())
    ) or "none"
    lines.extend([
        "",
        "## Governance gates",
        "",
        f"- Baseline census: `{census_status}`",
        f"- Manifest lifecycle: `{overall_status}` ({status_text})",
        f"- Open parity rows with an assigned package: `{sum(len(value) for value in assigned.values()):,}` of `{open_count:,}`",
        "",
        "## Daily waves",
        "",
        "| Wave | Target | Status | Open rows | Goal |",
        "|---|---|---|---:|---|",
    ])
    for wave in backlog["waves"]:
        wave_packages = [package_by_id[identifier] for identifier in wave["package_ids"]]
        statuses = {package["status"] for package in wave_packages}
        if statuses == {"complete"}:
            wave_status = "complete"
        elif "blocked" in statuses:
            wave_status = "blocked"
        elif "in_progress" in statuses:
            wave_status = "in_progress"
        else:
            wave_status = "planned"
        wave_open = sum(len(assigned.get(identifier, [])) for identifier in wave["package_ids"])
        lines.append(
            f"| {wave['id']} — {_escape(wave['title'])} | {wave['target_date']} | `{wave_status}` | {wave_open:,} | {_escape(wave['goal'])} |"
        )

    lines.extend([
        "",
        "## Work packages",
        "",
        "| Package | Wave | Priority | Maintained status | Linked / routed | Open | Approval | Owner role |",
        "|---|---|---|---|---:|---:|---|---|",
    ])
    wave_by_id = {wave["id"]: wave for wave in backlog["waves"]}
    for package in packages:
        wave = wave_by_id[package["wave"]]
        package_rows = routed.get(package["id"], [])
        package_open = assigned.get(package["id"], [])
        package_linked = len(package_rows) - len(package_open)
        lines.append(
            f"| `{package['id']}` — {_escape(package['title'])} | {package['wave']} / {wave['target_date']} | {package['priority']} | `{package['status']}` | {package_linked:,} / {len(package_rows):,} | {len(package_open):,} | {package['approval']} | {package['owner_role']} |"
        )

    first_open_wave = next(
        (wave for wave in backlog["waves"] if any(package_by_id[identifier]["status"] != "complete" for identifier in wave["package_ids"])),
        None,
    )
    lines.extend(["", "## Next actions", ""])
    if first_open_wave:
        lines.append(
            f"Advance **{first_open_wave['id']} — {_escape(first_open_wave['title'])}** (target {first_open_wave['target_date']}):"
        )
        lines.append("")
        for identifier in first_open_wave["package_ids"]:
            package = package_by_id[identifier]
            if package["status"] != "complete":
                package_rows = routed.get(identifier, [])
                package_open = assigned.get(identifier, [])
                lines.append(
                    f"- `{identifier}`: {package['title']} — {len(package_rows) - len(package_open):,} of {len(package_rows):,} routed rows linked; exit when "
                    + "; ".join(package["exit_criteria"])
                    + "."
                )
    else:
        lines.append("All maintained packages are complete; verify every completion gate before closure.")

    lines.extend(["", "## Completion definition", ""])
    capability_lifecycle_complete = bool(manifest_statuses) and all(
        status in {"approved", "active"} for status in manifest_statuses
    )
    machine_checks = [
        census_status == "closed",
        open_count == 0,
        capability_lifecycle_complete,
    ]
    for index, criterion in enumerate(backlog["completion_definition"]):
        checked = index < len(machine_checks) and machine_checks[index]
        lines.append(f"- [{'x' if checked else ' '}] {criterion}")
    lines.extend([
        "",
        "Use `python tools/contract-backlog/tracker.py --package PC-…-001` to list a package's deliverables, dependencies, exit criteria, and currently assigned parity rows.",
        "",
    ])
    return "\n".join(lines)


def render_package(backlog: dict[str, Any], rows: dict[str, list[LedgerRow]], package_id: str) -> str:
    package_by_id = {package["id"]: package for package in backlog["packages"]}
    package = package_by_id.get(package_id)
    if package is None:
        raise KeyError(package_id)
    owned = sorted(
        routed_rows(backlog, rows, open_only=False).get(package_id, []),
        key=lambda row: (row.ledger, row.row_id),
    )
    open_count = sum(row.contract is None for row in owned)
    lines = [
        f"{package_id}: {package['title']}",
        f"wave={package['wave']} priority={package['priority']} status={package['status']} owner={package['owner_role']} approval={package['approval']}",
        "dependencies=" + (", ".join(package["depends_on"]) or "none"),
        "deliverables:",
        *[f"  - {item}" for item in package["deliverables"]],
        "exit criteria:",
        *[f"  - {item}" for item in package["exit_criteria"]],
        f"routed rows ({len(owned) - open_count} linked, {open_count} open):",
        *[
            f"  - {row.ledger}:{row.row_id} [{row.domain}] -> {row.contract or 'OPEN'}"
            for row in owned
        ],
    ]
    return "\n".join(lines) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true", help="validate and require STATUS.md to be current")
    mode.add_argument("--write", action="store_true", help="validate and regenerate STATUS.md")
    mode.add_argument("--package", metavar="ID", help="show one package and its current row assignments")
    args = parser.parse_args(argv)

    try:
        backlog = load_backlog()
        rows = load_rows()
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    findings = validate(backlog, rows)
    if findings:
        for finding in findings:
            print(f"ERROR: {finding}", file=sys.stderr)
        return 1

    if args.package:
        try:
            print(render_package(backlog, rows, args.package), end="")
        except KeyError:
            print(f"ERROR: unknown package {args.package}", file=sys.stderr)
            return 1
        return 0

    report = render_status(backlog, rows)
    if args.write:
        STATUS_PATH.write_text(report, encoding="utf-8", newline="\n")
        print(f"Wrote {STATUS_PATH.relative_to(ROOT)}")
        return 0
    if args.check:
        existing = STATUS_PATH.read_text(encoding="utf-8") if STATUS_PATH.exists() else ""
        if existing != report:
            print("ERROR: product-contract/backlog/STATUS.md is stale; run tracker.py --write", file=sys.stderr)
            return 1
        print("Product Contract backlog is valid and STATUS.md is current.")
        return 0
    print(report, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
