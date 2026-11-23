#!/usr/bin/env python3
"""Setting disposition gate (SETCAT-001/002/020, ESET-001/015).

Validates every published product-contract/capabilities/*/setting-dispositions.json:

  1. each record satisfies the pinned setting-disposition schema
     (SETTINGS-CATALOG-001/setting-disposition.schema.json, v1);
  2. the records are an exact bijection with the settings ledger rows linked to
     that capability (no unlinked record, no undisposed row);
  3. every value family with an approved record (runtime-only exempt) has a
     defaults-<family> and an invalid-<family> vector in the capability's
     test-vectors.json, the defaults vector asserts only approved field names
     of that family with exactly the decided defaults, and the invalid vector
     recovers an approved field to its decided default with the field
     diagnostic;
  4. --report writes the generated coverage summary (approved/draft counts per
     family, the approved fields no defaults vector exercises, and the
     activation gate state) required by SETCAT-020.

Standard library only. Reports violations; never edits contracts.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

SCHEMA_URL = "https://xerahs.com/schemas/setting-disposition-v1.json"
SCHEMA_PATH = Path("product-contract") / "capabilities" / "SETTINGS-CATALOG-001" / "setting-disposition.schema.json"
LEDGER_PATH = Path("product-contract") / "parity" / "settings-ledger.yaml"
DISPOSITIONS_GLOB = str(Path("product-contract") / "capabilities" / "*" / "setting-dispositions.json")
FAMILIES = {"boolean", "integer", "number", "string", "enum", "color", "geometry", "duration", "path",
            "url", "secret-reference", "collection", "dictionary", "object", "runtime-only"}
SCOPES = {"global", "profile", "workflow", "task", "provider", "device", "window", "runtime"}
PRIVACY = {"public-preference", "private-local", "personal-data", "secret"}
STATUSES = {"draft", "approved", "retired"}
PLATFORM_DISPOSITIONS = {"required", "equivalent", "degraded", "unavailable", "not-applicable"}
LEDGER_ID = re.compile(r"^SET-[A-Z0-9-]+-[0-9]{3}$")
CONTRACT_PREFIX = "product-contract/capabilities/"


class Problems(list):
    def add(self, path: Path, message: str) -> None:
        self.append(f"{path}: {message}")


def ledger_rows(ledger: Path) -> dict[str, str]:
    """ledger id -> contract path for every settings-ledger row."""
    if not ledger.is_file():
        return {}
    text = ledger.read_text(encoding="utf-8-sig")
    starts = [match.start() for match in re.finditer(r"^  - id: ", text, re.MULTILINE)]
    rows: dict[str, str] = {}
    for index, start in enumerate(starts):
        block = text[start: starts[index + 1] if index + 1 < len(starts) else len(text)]
        identifier = re.search(r'- id: "([^"]+)"', block)
        contract = re.search(r"^    contract: \"?([^\"\n]+)\"?", block, re.MULTILINE)
        if identifier and contract:
            rows[identifier.group(1)] = contract.group(1).strip()
    return rows


def schema_rules(schema_path: Path) -> tuple[list[str], dict]:
    schema = json.loads(schema_path.read_text(encoding="utf-8-sig"))
    required = schema.get("required", [])
    properties = schema.get("properties", {})
    disposition_enum = schema.get("$defs", {}).get("disposition", {}).get("enum", sorted(PLATFORM_DISPOSITIONS))
    return required, {"properties": properties, "dispositions": disposition_enum}


def validate_record(record: object, rules: dict, path: Path, problems: Problems) -> None:
    required, meta = rules
    if not isinstance(record, dict):
        problems.add(path, "record must be an object")
        return
    for key in required:
        if key not in record:
            problems.add(path, "record missing required field " + key)
    for key in record:
        if key not in meta["properties"]:
            problems.add(path, f"record has unsupported field {key}")
    ledger_id = record.get("ledger_id", "")
    if not isinstance(ledger_id, str) or not LEDGER_ID.match(ledger_id):
        problems.add(path, f"invalid ledger_id {ledger_id!r}")
    for field in ("name", "validation", "migration", "effect"):
        value = record.get(field)
        if not isinstance(value, str) or not value.strip():
            problems.add(path, f"{ledger_id}: {field} must be a non-empty string")
    family = record.get("value_family")
    if family not in FAMILIES:
        problems.add(path, f"{ledger_id}: invalid value_family {family!r}")
    if record.get("scope") not in SCOPES:
        problems.add(path, f"{ledger_id}: invalid scope {record.get('scope')!r}")
    if record.get("privacy") not in PRIVACY:
        problems.add(path, f"{ledger_id}: invalid privacy {record.get('privacy')!r}")
    if record.get("status") not in STATUSES:
        problems.add(path, f"{ledger_id}: invalid status {record.get('status')!r}")
    if family == "secret-reference" or record.get("privacy") == "secret":
        migration = record.get("migration")
        if not isinstance(migration, str) or "credential" not in migration.lower():
            problems.add(path, f"{ledger_id}: secret material must record its credential-service migration "
                                f"(SETCAT-007)")
    platforms = record.get("platforms")
    if not isinstance(platforms, dict) or set(platforms) != {"windows", "macos", "linux"}:
        problems.add(path, f"{ledger_id}: platforms must list exactly windows, macos and linux")
    elif isinstance(platforms, dict):
        for platform, disposition in platforms.items():
            if disposition not in meta["dispositions"]:
                problems.add(path, f"{ledger_id}: invalid {platform} disposition {disposition!r}")


def vector_map(vectors_path: Path) -> dict[str, dict]:
    if not vectors_path.is_file():
        return {}
    try:
        document = json.loads(vectors_path.read_text(encoding="utf-8-sig"))
    except (json.JSONDecodeError, UnicodeDecodeError):
        return {}
    return {vector.get("id"): vector for vector in document.get("vectors", []) if isinstance(vector, dict)}


def check_coverage(records: list[dict], vectors: dict[str, dict], path: Path, problems: Problems) -> dict:
    approved = [r for r in records if isinstance(r, dict) and r.get("status") == "approved"
                and isinstance(r.get("name"), str) and "default" in r
                and r.get("value_family") in FAMILIES]
    families: dict[str, list[dict]] = {}
    for record in approved:
        if record.get("value_family") != "runtime-only":
            families.setdefault(record["value_family"], []).append(record)
    coverage = {"families": {}, "unexercised_approved_fields": []}
    exercised: set[str] = set()
    for family, members in sorted(families.items()):
        before = len(problems)
        names = {r["name"] for r in members}
        defaults = {r["name"]: r["default"] for r in members}
        defaults_vector = vectors.get(f"defaults-{family}")
        invalid_vector = vectors.get(f"invalid-{family}")
        if defaults_vector is None or invalid_vector is None:
            problems.add(path, f"family {family}: approved records need defaults-{family} and "
                                f"invalid-{family} vectors")
            coverage["families"][family] = False
            continue
        expected = defaults_vector.get("expected", {})
        if not isinstance(expected, dict) or not expected:
            problems.add(path, f"defaults-{family}: expected must be a non-empty object")
            coverage["families"][family] = False
            continue
        for key, value in expected.items():
            if key not in names:
                problems.add(path, f"defaults-{family}: {key!r} is not an approved {family} field")
            elif value != defaults[key]:
                problems.add(path, f"defaults-{family}: {key} must equal the decided default "
                                    f"{defaults[key]!r}, found {value!r}")
        exercised.update(k for k in expected if k in names)
        stored = invalid_vector.get("input", {}).get("stored", {})
        targets = [k for k in (stored or {}) if k in names] if isinstance(stored, dict) else []
        if not targets:
            problems.add(path, f"invalid-{family}: stored input must include an approved {family} field")
        else:
            for target in targets:
                expected_back = invalid_vector.get("expected", {})
                if not isinstance(expected_back, dict) or expected_back.get(target) != defaults[target]:
                    problems.add(path, f"invalid-{family}: expected {target} must recover to the decided "
                                        f"default {defaults[target]!r}")
                if not isinstance(expected_back, dict) or expected_back.get("diagnostic") != f"invalid-{target}":
                    problems.add(path, f"invalid-{family}: expected diagnostic must be invalid-{target}")
        coverage["families"][family] = len(problems) == before
    coverage["unexercised_approved_fields"] = sorted(
        r["name"] for r in approved if r.get("value_family") != "runtime-only" and r["name"] not in exercised)
    return coverage


def check_file(path: Path, repo: Path, rows: dict[str, str], problems: Problems) -> dict | None:
    capability = path.parent.name
    try:
        document = json.loads(path.read_text(encoding="utf-8-sig"))
    except (json.JSONDecodeError, UnicodeDecodeError) as error:
        problems.add(path, f"invalid JSON: {error}")
        return None
    if document.get("schema") != SCHEMA_URL:
        problems.add(path, f"schema must be {SCHEMA_URL}")
    if document.get("capability") != capability:
        problems.add(path, f"capability must be {capability}")
    decision = document.get("decision_record")
    if not isinstance(decision, str) or not (repo / "product-contract" / decision).is_file():
        problems.add(path, "decision_record must name an existing decisions/ document")
    schema_path = repo / SCHEMA_PATH
    if not schema_path.is_file():
        problems.add(schema_path, "missing pinned setting-disposition schema")
        return None
    rules = schema_rules(schema_path)
    records = document.get("records")
    if not isinstance(records, list) or not records:
        problems.add(path, "records must be a non-empty list")
        return None
    seen: set[str] = set()
    for record in records:
        validate_record(record, rules, path, problems)
        if isinstance(record, dict):
            identifier = record.get("ledger_id")
            if identifier in seen:
                problems.add(path, f"duplicate ledger_id {identifier}")
            seen.add(identifier)
    linked = {identifier for identifier, contract in rows.items()
              if contract == CONTRACT_PREFIX + capability}
    missing = sorted(linked - seen)
    extra = sorted(seen - linked)
    if missing:
        problems.add(path, f"ledger rows without a disposition record: {', '.join(missing)}")
    if extra:
        problems.add(path, f"records not linked to this capability by the ledger: {', '.join(extra)}")
    vectors = vector_map(path.parent / "test-vectors.json")
    coverage = check_coverage([r for r in records if isinstance(r, dict)], vectors, path, problems)
    counts = {"approved": 0, "draft": 0, "retired": 0}
    for record in records:
        if isinstance(record, dict) and record.get("status") in counts:
            counts[record["status"]] += 1
    return {"capability": capability, "counts": counts, "coverage": coverage}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Validate published setting dispositions against the "
                                                 "pinned schema, the settings ledger and the conformance vectors.")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--report", type=Path, help="write the generated coverage summary to this path")
    args = parser.parse_args(argv)
    problems = Problems()
    rows = ledger_rows(args.root / LEDGER_PATH)
    results = []
    for path in sorted(args.root.glob(DISPOSITIONS_GLOB)):
        result = check_file(path, args.root, rows, problems)
        if result:
            results.append(result)
    if args.report:
        report = {
            "schema": SCHEMA_URL,
            "ledger_rows": len(rows),
            "capabilities": results,
        }
        for result in results:
            draft = result["counts"]["draft"]
            result["activation_gate"] = "blocked" if draft else "ready-for-adapter-qualification"
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    if not results:
        print("no published setting-dispositions.json; nothing to check")
    for problem in problems:
        print(problem)
    if problems:
        print(f"\n{len(problems)} violation(s)")
        return 1
    for result in results:
        counts = result["counts"]
        print(f"OK {result['capability']}: {counts['approved']} approved / {counts['draft']} draft / "
              f"{counts['retired']} retired, families covered: "
              f"{sum(1 for ok in result['coverage']['families'].values() if ok)}/{len(result['coverage']['families'])}"
              + (f", unexercised approved fields: {', '.join(result['coverage']['unexercised_approved_fields'])}"
                 if result['coverage']['unexercised_approved_fields'] else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
