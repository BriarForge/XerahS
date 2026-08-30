#!/usr/bin/env python3
"""Validate XerahS Product Contract and scoped governance structure."""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


SEMVER = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")
RULE_ID = re.compile(r"\*\*([A-Z][A-Z0-9]*(?:-[A-Z0-9]+)+-[0-9]{3})\*\*")
REQUIREMENT = re.compile(r"\*\*([A-Z][A-Z0-9]*-[0-9]{3}):\*\*")
LEDGER_ID = re.compile(r'^  - id: "([A-Z0-9]+(?:-[A-Z0-9]+)+-[0-9]{3})"$', re.MULTILINE)
PINNED_COMMIT = "5c7e36dea77ab131fe0f5e2101e5d578ccde0306"
REQUIRED_LEDGERS = (
    "capability-ledger.yaml",
    "settings-ledger.yaml",
    "workflow-ledger.yaml",
    "interface-ledger.yaml",
    "compatibility-ledger.yaml",
    "baseline-deltas.yaml",
)


@dataclass
class Capability:
    identifier: str
    path: str
    version: str
    status: str
    requirements: list[str]


class Findings:
    def __init__(self) -> None:
        self.items: list[str] = []

    def add(self, path: Path | str, message: str) -> None:
        self.items.append(f"{str(path).replace(chr(92), '/')}: {message}")

    def require(self, condition: bool, path: Path | str, message: str) -> None:
        if not condition:
            self.add(path, message)


def relative(path: Path, repo: Path) -> str:
    try:
        return path.relative_to(repo).as_posix()
    except ValueError:
        return path.as_posix()


def unquote(value: str) -> str:
    value = value.strip()
    if value.startswith('"') and value.endswith('"'):
        return json.loads(value)
    return value


def parse_inline_list(value: str) -> list[str]:
    value = value.strip()
    if value == "[]":
        return []
    if not value.startswith("[") or not value.endswith("]"):
        raise ValueError("expected inline YAML list")
    return [unquote(item.strip()) for item in value[1:-1].split(",") if item.strip()]


def parse_manifest(path: Path, findings: Findings) -> tuple[str, str, list[Capability]]:
    if not path.exists():
        findings.add(path, "missing manifest")
        return "", "", []
    contract_version = ""
    status = ""
    capabilities: list[Capability] = []
    current: dict[str, str] | None = None
    for number, raw in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), start=1):
        line = raw.strip()
        if not line or line.startswith("#") or line == "capabilities:":
            continue
        if raw.startswith("contract_version:"):
            contract_version = unquote(raw.split(":", 1)[1])
        elif raw.startswith("status:"):
            status = unquote(raw.split(":", 1)[1])
        elif raw.startswith("  - id:"):
            if current is not None:
                capabilities.append(capability_from(current, path, findings))
            current = {"id": unquote(raw.split(":", 1)[1])}
        elif current is not None and raw.startswith("    ") and ":" in line:
            key, value = line.split(":", 1)
            current[key] = value.strip()
        else:
            findings.add(path, f"unsupported manifest syntax at line {number}")
    if current is not None:
        capabilities.append(capability_from(current, path, findings))
    return contract_version, status, capabilities


def capability_from(values: dict[str, str], path: Path, findings: Findings) -> Capability:
    required = ("id", "path", "version", "status", "requirements")
    for key in required:
        findings.require(key in values, path, f"capability entry missing {key}")
    try:
        requirements = parse_inline_list(values.get("requirements", "[]"))
    except ValueError as error:
        findings.add(path, f"{values.get('id', '<unknown>')} requirements: {error}")
        requirements = []
    return Capability(
        unquote(values.get("id", "")),
        unquote(values.get("path", "")),
        unquote(values.get("version", "")),
        unquote(values.get("status", "")),
        requirements,
    )


def lint_agents(repo: Path, findings: Findings) -> None:
    rule_owners: dict[str, str] = {}
    fixtures_root = (repo / "tools" / "contract-linter" / "fixtures").resolve()
    agents_files = sorted(
        path for path in repo.rglob("AGENTS.md")
        if ".git" not in path.parts and fixtures_root not in path.resolve().parents
    )
    root_file = repo / "AGENTS.md"
    findings.require(root_file in agents_files, root_file, "missing root constitution")
    for path in agents_files:
        text = path.read_text(encoding="utf-8-sig")
        for rule in RULE_ID.findall(text):
            if rule in rule_owners:
                findings.add(path, f"duplicate rule ID {rule}; first declared in {rule_owners[rule]}")
            else:
                rule_owners[rule] = relative(path, repo)
        if path == root_file:
            continue
        parent_match = re.search(r"^Parent:\s*(.+?)\s*$", text, re.MULTILINE)
        if not parent_match:
            findings.add(path, "missing Parent declaration")
            continue
        parent = (path.parent / parent_match.group(1).strip()).resolve()
        findings.require(parent.exists(), path, f"parent does not exist: {parent_match.group(1).strip()}")
        depth = 1
        seen = {path.resolve()}
        while parent.exists() and parent != root_file.resolve():
            if parent in seen:
                findings.add(path, "parent cycle detected")
                break
            seen.add(parent)
            parent_text = parent.read_text(encoding="utf-8-sig")
            match = re.search(r"^Parent:\s*(.+?)\s*$", parent_text, re.MULTILINE)
            if not match:
                findings.add(path, f"ancestor lacks Parent declaration: {relative(parent, repo)}")
                break
            parent = (parent.parent / match.group(1).strip()).resolve()
            depth += 1
        findings.require(depth <= 4, path, f"instruction hierarchy depth {depth} exceeds D-AGT-001 maximum 4")


def lint_manifest(repo: Path, findings: Findings) -> set[str]:
    contract_root = repo / "product-contract"
    version, status, capabilities = parse_manifest(contract_root / "manifest.yaml", findings)
    findings.require(bool(SEMVER.fullmatch(version)), contract_root / "manifest.yaml", f"invalid contract SemVer: {version!r}")
    findings.require(status in {"seed", "draft", "active", "retired"}, contract_root / "manifest.yaml", f"invalid manifest status: {status!r}")
    capability_ids: set[str] = set()
    global_requirements: set[str] = set()
    for capability in capabilities:
        if capability.identifier in capability_ids:
            findings.add(contract_root / "manifest.yaml", f"duplicate capability ID {capability.identifier}")
        capability_ids.add(capability.identifier)
        findings.require(bool(SEMVER.fullmatch(capability.version)), contract_root / "manifest.yaml", f"{capability.identifier} has invalid SemVer {capability.version!r}")
        findings.require(capability.status in {"planned", "draft", "approved", "active", "retired"}, contract_root / "manifest.yaml", f"{capability.identifier} has invalid status {capability.status!r}")
        package = contract_root / capability.path
        findings.require(package.is_dir(), package, f"manifest path missing for {capability.identifier}")
        findings.require((package / "README.md").is_file(), package, "missing README.md")
        if capability.status in {"draft", "approved", "active"}:
            spec = package / "SPEC.md"
            scenarios = package / "SCENARIOS.feature"
            findings.require(spec.is_file(), package, "draft or active capability missing SPEC.md")
            findings.require(scenarios.is_file(), package, "draft or active capability missing SCENARIOS.feature")
            spec_requirements = REQUIREMENT.findall(spec.read_text(encoding="utf-8-sig")) if spec.exists() else []
            if len(spec_requirements) != len(set(spec_requirements)):
                findings.add(spec, "duplicate requirement ID in SPEC.md")
            if set(spec_requirements) != set(capability.requirements):
                missing = sorted(set(capability.requirements) - set(spec_requirements))
                extra = sorted(set(spec_requirements) - set(capability.requirements))
                findings.add(spec, f"manifest/spec requirement mismatch; missing={missing}, extra={extra}")
        for requirement in capability.requirements:
            if requirement in global_requirements:
                findings.add(contract_root / "manifest.yaml", f"duplicate requirement ID {requirement}")
            global_requirements.add(requirement)
    return capability_ids


def lint_json(repo: Path, findings: Findings) -> None:
    root = repo / "product-contract"
    for path in sorted(root.rglob("*.json")):
        try:
            value = json.loads(path.read_text(encoding="utf-8-sig"))
        except (json.JSONDecodeError, UnicodeDecodeError) as error:
            findings.add(path, f"invalid JSON: {error}")
            continue
        if path.name.endswith(".schema.json"):
            findings.require(isinstance(value, dict) and "$schema" in value, path, "JSON Schema missing $schema")


def ledger_blocks(text: str) -> list[str]:
    starts = [match.start() for match in re.finditer(r'^  - id: ', text, re.MULTILINE)]
    return [text[start : starts[index + 1] if index + 1 < len(starts) else len(text)] for index, start in enumerate(starts)]


def lint_parity(repo: Path, findings: Findings) -> None:
    parity = repo / "product-contract" / "parity"
    global_ids: set[str] = set()
    for name in REQUIRED_LEDGERS:
        path = parity / name
        findings.require(path.is_file(), path, "missing required parity artifact")
        if not path.exists() or name == "baseline-deltas.yaml":
            continue
        text = path.read_text(encoding="utf-8-sig")
        ids = LEDGER_ID.findall(text)
        blocks = ledger_blocks(text)
        findings.require(len(ids) == len(blocks) and bool(ids), path, "could not parse ledger rows")
        for identifier, block in zip(ids, blocks):
            if identifier in global_ids:
                findings.add(path, f"duplicate ledger ID {identifier}")
            global_ids.add(identifier)
            for field in ("domain", "user_outcome", "classification", "source_evidence", "baseline_commit", "contract", "platforms", "evidence", "compatibility", "status"):
                findings.require(re.search(rf"^    {re.escape(field)}:", block, re.MULTILINE) is not None, path, f"{identifier} missing {field}")
            findings.require(PINNED_COMMIT in block, path, f"{identifier} has wrong baseline commit")
            for platform in ("windows", "macos", "linux"):
                findings.require(re.search(rf'^      {platform}: "(?:required|equivalent|degraded|unavailable|not-applicable)"$', block, re.MULTILINE) is not None, path, f"{identifier} missing or invalid {platform} disposition")
            evidence_path = re.search(r'^      - path: "([^"]+)"$', block, re.MULTILINE)
            findings.require(evidence_path is not None, path, f"{identifier} missing source evidence path")
            contract = re.search(r'^    contract: (.+)$', block, re.MULTILINE)
            if contract and contract.group(1).strip() != "null":
                contract_path = repo / unquote(contract.group(1))
                findings.require(contract_path.exists(), path, f"{identifier} contract path does not exist: {contract_path}")
    baseline = repo / "product-contract" / "reference-baselines" / "kova-0.29.0.yaml"
    findings.require(baseline.is_file(), baseline, "missing pinned baseline descriptor")
    if baseline.exists():
        text = baseline.read_text(encoding="utf-8-sig")
        findings.require(f'commit: "{PINNED_COMMIT}"' in text, baseline, "pinned baseline commit mismatch")
        findings.require('census_status: "open"' in text or 'census_status: "closed"' in text, baseline, "missing census status")
    census = repo / "product-contract" / "reference-baselines" / "provenance" / "kova-0.29.0-census.json"
    findings.require(census.is_file(), census, "missing generated census evidence")


def lint_repository(repo: Path) -> list[str]:
    findings = Findings()
    lint_agents(repo, findings)
    lint_manifest(repo, findings)
    lint_json(repo, findings)
    lint_parity(repo, findings)
    return findings.items


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args(list(argv) if argv is not None else None)
    repo = args.repo.resolve()
    findings = lint_repository(repo)
    if findings:
        print(f"Contract lint failed with {len(findings)} finding(s):")
        for finding in findings:
            print(f"- {finding}")
        return 1
    print("Contract lint passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
