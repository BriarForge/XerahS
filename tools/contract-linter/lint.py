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


MARKDOWN_LINK = re.compile(r"\]\(([^)\s]+)\)")
PROTECTED_PREFIX = "ROOT-"


def instruction_files(repo: Path) -> list[Path]:
    """Return governed AGENTS.md files, excluding linter fixture repositories."""
    fixtures_root = (repo / "tools" / "contract-linter" / "fixtures").resolve()
    return sorted(
        path for path in repo.rglob("AGENTS.md")
        if ".git" not in path.relative_to(repo).parts and fixtures_root not in path.resolve().parents
    )


def nearest_ancestor_instructions(path: Path, repo: Path, governed: set[Path]) -> Path | None:
    directory = path.parent.parent
    while True:
        candidate = (directory / "AGENTS.md").resolve()
        if candidate in governed:
            return candidate
        if directory == repo or directory == directory.parent:
            return None
        directory = directory.parent


def lint_agents(repo: Path, findings: Findings) -> None:
    rule_owners: dict[str, str] = {}
    repo = repo.resolve()
    agents_files = [path.resolve() for path in instruction_files(repo)]
    governed = set(agents_files)
    root_file = (repo / "AGENTS.md").resolve()
    findings.require(root_file in governed, root_file, "missing root constitution")
    for path in agents_files:
        text = path.read_text(encoding="utf-8-sig")
        for rule in RULE_ID.findall(text):
            if rule in rule_owners:
                findings.add(path, f"duplicate rule ID {rule}; first declared in {rule_owners[rule]}")
            else:
                rule_owners[rule] = relative(path, repo)
            if path != root_file and rule.startswith(PROTECTED_PREFIX):
                findings.add(path, f"child declares protected root rule ID {rule}")
        for target in MARKDOWN_LINK.findall(text):
            if re.match(r"^[a-z][a-z0-9+.-]*:", target) or target.startswith("#"):
                continue
            linked = (path.parent / target.split("#", 1)[0]).resolve()
            findings.require(linked.exists(), path, f"broken relative link: {target}")
        if path == root_file:
            continue
        scope_match = re.search(r"^Applies to:\s*(.+?)\s*$", text, re.MULTILINE)
        expected_scope = f"{relative(path.parent, repo)}/**"
        if not scope_match:
            findings.add(path, "missing Applies to declaration")
        else:
            findings.require(
                scope_match.group(1) == expected_scope,
                path,
                f"Applies to {scope_match.group(1)!r} does not match directory scope {expected_scope!r}",
            )
        parent_match = re.search(r"^Parent:\s*(.+?)\s*$", text, re.MULTILINE)
        if not parent_match:
            findings.add(path, "missing Parent declaration")
            continue
        parent = (path.parent / parent_match.group(1).strip()).resolve()
        findings.require(parent.exists(), path, f"parent does not exist: {parent_match.group(1).strip()}")
        nearest = nearest_ancestor_instructions(path, repo, governed)
        if parent.exists() and nearest is not None and parent != nearest:
            findings.add(path, f"Parent must be the nearest ancestor instruction file: {relative(nearest, repo)}")
        if parent.exists():
            child_link = Path(path).relative_to(parent.parent).as_posix() if path.is_relative_to(parent.parent) else ""
            parent_links = {
                link.split("#", 1)[0] for link in MARKDOWN_LINK.findall(parent.read_text(encoding="utf-8-sig"))
            }
            findings.require(
                child_link in parent_links,
                path,
                f"undeclared scope: {relative(parent, repo)} does not index {child_link or relative(path, repo)}",
            )
        depth = 1
        seen = {path}
        while parent.exists() and parent != root_file:
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


def effective_chain(repo: Path, target: str, governed: list[Path]) -> list[Path]:
    """Return the root-to-leaf AGENTS.md files that govern a repository path."""
    repo = repo.resolve()
    governed_set = {path.resolve() for path in governed}
    parts = Path(target.replace("\\", "/")).parts
    chain: list[Path] = []
    for index in range(len(parts) + 1):
        candidate = (repo.joinpath(*parts[:index]) / "AGENTS.md").resolve()
        if candidate in governed_set and candidate not in chain:
            chain.append(candidate)
    return chain


def effective_report(repo: Path, targets: Iterable[str]) -> str:
    """Render a Markdown effective-instructions report grouped by governing chain (TOOL-EFFECTIVE-001)."""
    repo = repo.resolve()
    governed = instruction_files(repo)
    groups: dict[tuple[Path, ...], list[str]] = {}
    for target in sorted({item.strip().replace("\\", "/") for item in targets if item.strip()}):
        groups.setdefault(tuple(effective_chain(repo, target, governed)), []).append(target)
    lines = ["# Effective instructions", ""]
    if not groups:
        lines.append("No changed paths.")
        return "\n".join(lines) + "\n"
    for chain, paths in sorted(groups.items(), key=lambda item: [relative(p, repo) for p in item[0]]):
        heading = " → ".join(relative(path, repo) for path in chain) or "(no governing AGENTS.md)"
        lines.append(f"## {heading}")
        lines.append("")
        for path in chain:
            rules = RULE_ID.findall(path.read_text(encoding="utf-8-sig"))
            lines.append(f"- `{relative(path, repo)}`: {', '.join(rules) if rules else 'no rule IDs'}")
        lines.append("")
        lines.append(f"Governed paths ({len(paths)}):")
        lines.append("")
        lines.extend(f"- `{path}`" for path in paths)
        lines.append("")
    return "\n".join(lines)


def changed_paths(repo: Path, base: str) -> list[str]:
    """List paths changed since base, using the merge base when one exists."""
    import subprocess

    def diff(*revisions: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            ["git", "-C", str(repo), "diff", "--name-only", *revisions],
            capture_output=True,
            text=True,
        )

    result = diff(f"{base}...HEAD")
    if result.returncode != 0:
        result = diff(base, "HEAD")
    if result.returncode != 0:
        raise SystemExit(f"cannot list changes since {base}: {result.stderr.strip()}")
    return [line for line in result.stdout.splitlines() if line.strip()]


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


VECTOR_ID = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
OPERATION = re.compile(r"^[a-z][a-z0-9-]*$")


def lint_vectors(repo: Path, findings: Findings) -> None:
    """Validate capability vector files against product-contract/VECTORS.md."""
    contract_root = repo / "product-contract"
    _, _, capabilities = parse_manifest(contract_root / "manifest.yaml", Findings())
    requirements = {capability.identifier: set(capability.requirements) for capability in capabilities}
    for path in sorted((contract_root / "capabilities").glob("*/test-vectors.json")):
        try:
            data = json.loads(path.read_text(encoding="utf-8-sig"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            continue  # reported by lint_json
        capability = path.parent.name
        if not isinstance(data, dict) or data.get("schema_version") != 2:
            findings.add(path, "vector file must use schema_version 2 (product-contract/VECTORS.md)")
            continue
        findings.require(data.get("capability") == capability, path, f"vector capability must be {capability}")
        findings.require(data.get("comparison") in {"exact", "subset"}, path, "vector comparison must be exact or subset")
        allowed = requirements.get(capability, set())
        for requirement in data.get("requirement_ids", []):
            findings.require(requirement in allowed, path, f"requirement_ids names unknown requirement {requirement}")
        default_operation = data.get("operation")
        vectors = data.get("vectors")
        if not isinstance(vectors, list) or not vectors:
            findings.add(path, "vector file must contain vectors")
            continue
        seen: set[str] = set()
        for vector in vectors:
            if not isinstance(vector, dict):
                findings.add(path, "vector must be an object")
                continue
            vector_id = vector.get("id", "")
            findings.require(bool(VECTOR_ID.fullmatch(str(vector_id))), path, f"invalid vector id {vector_id!r}")
            findings.require(vector_id not in seen, path, f"duplicate vector id {vector_id}")
            seen.add(vector_id)
            extra = sorted(set(vector) - {"id", "operation", "requirements", "input", "expected"})
            findings.require(not extra, path, f"{vector_id}: unsupported vector fields {extra}")
            operation = vector.get("operation", default_operation)
            findings.require(
                isinstance(operation, str) and bool(OPERATION.fullmatch(operation)),
                path,
                f"{vector_id}: vector has no valid operation",
            )
            findings.require(isinstance(vector.get("input"), dict), path, f"{vector_id}: input must be an object")
            findings.require(isinstance(vector.get("expected"), dict), path, f"{vector_id}: expected must be an object")
            for requirement in vector.get("requirements", []):
                findings.require(requirement in allowed, path, f"{vector_id}: unknown requirement {requirement}")


def lint_approval_records(repo: Path, findings: Findings) -> None:
    contract_root = repo / "product-contract"
    manifest_path = contract_root / "manifest.yaml"
    contract_version, _, capabilities = parse_manifest(manifest_path, findings)
    capability_by_id = {capability.identifier: capability for capability in capabilities}
    requires_approval = {
        capability.identifier
        for capability in capabilities
        if capability.status in {"approved", "active"}
    }
    covered: set[str] = set()
    for path in sorted((contract_root / "reviews").glob("APPROVAL-*.json")):
        try:
            record = json.loads(path.read_text(encoding="utf-8-sig"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            continue
        if record.get("decision") != "approved":
            continue
        findings.require(record.get("schema_version") == 1, path, "approval record schema_version must be 1")
        authority = record.get("authority", {})
        findings.require(authority.get("role") == "human-product-owner", path, "approval authority must be human-product-owner")
        scope = record.get("scope", {})
        if scope.get("contract_version") != contract_version:
            # Records for earlier contract versions are retained history; only
            # current-version records can make a capability approved.
            continue
        effects = record.get("effects", {})
        findings.require(effects.get("normative_contract") is True, path, "approved record must make its contract scope normative")
        packages = scope.get("packages", [])
        findings.require(isinstance(packages, list), path, "approval scope packages must be a list")
        for package_id in packages if isinstance(packages, list) else []:
            if package_id not in capability_by_id:
                findings.add(path, f"approval references unknown capability {package_id}")
            else:
                covered.add(package_id)
    for package_id in sorted(requires_approval - covered):
        findings.add(manifest_path, f"approved capability lacks human product-owner approval record: {package_id}")


def lint_editor_catalogs(repo: Path, findings: Findings) -> None:
    contract = repo / "product-contract"
    paths = {
        "census": contract / "reference-baselines" / "provenance" / "kova-0.29.0-census.json",
        "annotations": contract / "capabilities" / "EDITOR-ANNOTATIONS-001" / "annotation-tools.json",
        "effects": contract / "capabilities" / "EDITOR-EFFECTS-001" / "effect-catalog-selection.json",
        "utilities": contract / "capabilities" / "EDITOR-UTILITIES-001" / "utility-catalog.json",
    }
    values: dict[str, dict] = {}
    for name, path in paths.items():
        if not path.is_file():
            findings.add(path, f"missing ImageEditor {name} evidence")
            return
        try:
            value = json.loads(path.read_text(encoding="utf-8-sig"))
        except (json.JSONDecodeError, UnicodeDecodeError) as error:
            findings.add(path, f"cannot validate ImageEditor catalog: {error}")
            return
        if not isinstance(value, dict):
            findings.add(path, "ImageEditor catalog root must be an object")
            return
        values[name] = value

    try:
        inventory = values["census"]["inventory"]
        editor = inventory["image_editor"]
        selected_tools = [item["id"] for item in values["annotations"]["tools"]]
        findings.require(selected_tools == editor["editor_tools"], paths["annotations"], "tool selection differs from pinned census")

        effects = editor["effect_types"]
        categories: dict[str, int] = {}
        modes: dict[str, int] = {}
        for effect in effects:
            categories[effect["category"]] = categories.get(effect["category"], 0) + 1
            modes[effect["execution_mode"]] = modes.get(effect["execution_mode"], 0) + 1
        expected = values["effects"]["expected"]
        findings.require(expected["total"] == len(effects), paths["effects"], "effect total differs from pinned census")
        findings.require(expected["categories"] == categories, paths["effects"], "effect category counts differ from pinned census")
        findings.require(expected["execution_modes"] == modes, paths["effects"], "effect execution-mode counts differ from pinned census")
        findings.require(expected["declared_parameter_controls"] == sum(len(effect["parameters"]) for effect in effects), paths["effects"], "effect parameter count differs from pinned census")

        utility_by_id = {item["id"]: item for item in values["utilities"]["utilities"]}
        enum_by_name = {
            item["name"]: item["members"]
            for item in inventory["public_enums"]
            if item["path"].startswith("ShareX.ImageEditor/")
        }
        exact_enums = {
            ("qr-code", "scan_modes"): "QrCodeScanMode",
            ("hash-checker", "algorithms"): "HashCheckerAlgorithm",
            ("icon-converter", "bit_depths"): "IconBitDepth",
            ("background-remover", "devices"): "BackgroundRemovalDevice",
            ("video-converter", "codecs"): "VideoConverterCodec",
        }
        for (utility_id, field), enum_name in exact_enums.items():
            findings.require(set(utility_by_id[utility_id][field]) == set(enum_by_name[enum_name]), paths["utilities"], f"{utility_id}.{field} differs from pinned census")
    except (KeyError, TypeError) as error:
        findings.add(contract, f"malformed ImageEditor catalog evidence: missing or invalid {error}")


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
    lint_approval_records(repo, findings)
    lint_vectors(repo, findings)
    lint_editor_catalogs(repo, findings)
    lint_parity(repo, findings)
    return findings.items


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    report = parser.add_mutually_exclusive_group()
    report.add_argument("--effective", nargs="+", metavar="PATH", help="print effective instructions for paths")
    report.add_argument("--changed-since", metavar="REF", help="print effective instructions for paths changed since REF")
    args = parser.parse_args(list(argv) if argv is not None else None)
    repo = args.repo.resolve()
    if args.effective or args.changed_since:
        targets = args.effective or changed_paths(repo, args.changed_since)
        print(effective_report(repo, targets), end="")
        return 0
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
