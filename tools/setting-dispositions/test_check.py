#!/usr/bin/env python3
"""Self-tests for tools/setting-dispositions/check.py.

Each test builds a miniature product-contract tree in a temporary directory
with a two-row ledger and one capability, then mutates one part at a time to
prove every rule fires and that the known-good fixture passes clean.
"""
from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check  # noqa: E402

SCHEMA = {
    "$schema": "https://json-schema.org/draft/2020-12/schema",
    "$id": "https://xerahs.com/schemas/setting-disposition-v1.json",
    "type": "object",
    "additionalProperties": False,
    "required": ["ledger_id", "name", "value_family", "default", "validation", "scope", "privacy",
                 "migration", "effect", "platforms", "status"],
    "properties": {
        "ledger_id": {"type": "string", "pattern": "^SET-[A-Z0-9-]+-[0-9]{3}$"},
        "name": {"type": "string", "minLength": 1},
        "value_family": {"enum": ["boolean", "integer", "number", "string", "enum", "color", "geometry",
                                  "duration", "path", "url", "secret-reference", "collection", "dictionary",
                                  "object", "runtime-only"]},
        "default": {},
        "validation": {"type": "string", "minLength": 1},
        "scope": {"enum": ["global", "profile", "workflow", "task", "provider", "device", "window", "runtime"]},
        "privacy": {"enum": ["public-preference", "private-local", "personal-data", "secret"]},
        "migration": {"type": "string", "minLength": 1},
        "effect": {"type": "string", "minLength": 1},
        "platforms": {"type": "object", "additionalProperties": False,
                      "required": ["windows", "macos", "linux"],
                      "properties": {"windows": {"$ref": "#/$defs/disposition"},
                                     "macos": {"$ref": "#/$defs/disposition"},
                                     "linux": {"$ref": "#/$defs/disposition"}}},
        "status": {"enum": ["draft", "approved", "retired"]},
    },
    "$defs": {"disposition": {"enum": ["required", "equivalent", "degraded", "unavailable",
                                       "not-applicable"]}},
}

RECORD_BOOL = {
    "ledger_id": "SET-TEST-OPTIONS-SHADOW-001", "name": "Shadow", "value_family": "boolean",
    "default": False, "validation": "Boolean only.", "scope": "global",
    "privacy": "public-preference", "migration": "Absent loads the default.",
    "effect": "Creation default shadow enablement.",
    "platforms": {"windows": "required", "macos": "required", "linux": "required"}, "status": "approved",
}
RECORD_ALIAS = {
    "ledger_id": "SET-TEST-OPTIONS-COLOR-001", "name": "BorderColor", "value_family": "runtime-only",
    "default": "#FFFF0000", "validation": "Never loaded or saved.", "scope": "global",
    "privacy": "public-preference", "migration": "Absent loads the default.",
    "effect": "Derived convenience colour.",
    "platforms": {"windows": "required", "macos": "required", "linux": "required"}, "status": "approved",
}
LEDGER = """schema_version: 1
baseline: "test"
ledger: "settings"
items:
  - id: "SET-TEST-OPTIONS-SHADOW-001"
    domain: "settings"
    user_outcome: "Shadow default."
    classification: "preserve"
    contract: "product-contract/capabilities/TEST-CAPABILITY-001"
    status: "inventoried"
  - id: "SET-TEST-OPTIONS-COLOR-001"
    domain: "settings"
    user_outcome: "Border colour alias."
    classification: "preserve"
    contract: "product-contract/capabilities/TEST-CAPABILITY-001"
    status: "inventoried"
"""
VECTORS = {
    "schema_version": 2, "capability": "TEST-CAPABILITY-001", "comparison": "subset",
    "vectors": [
        {"id": "defaults-boolean", "operation": "load-settings", "input": {"stored": {}},
         "expected": {"Shadow": False}},
        {"id": "invalid-boolean", "operation": "load-settings",
         "input": {"stored": {"Shadow": "yes"}},
         "expected": {"Shadow": False, "diagnostic": "invalid-Shadow"}},
    ],
}


def build_repo(records=None, ledger=LEDGER, vectors=VECTORS, decision=True, decision_name="decisions/test.md"):
    root = Path(tempfile.mkdtemp(prefix="setting-dispositions-"))
    capability = root / "product-contract" / "capabilities" / "TEST-CAPABILITY-001"
    schema_dir = root / "product-contract" / "capabilities" / "SETTINGS-CATALOG-001"
    schema_dir.mkdir(parents=True)
    (schema_dir / "setting-disposition.schema.json").write_text(json.dumps(SCHEMA), encoding="utf-8")
    parity = root / "product-contract" / "parity"
    parity.mkdir(parents=True)
    (parity / "settings-ledger.yaml").write_text(ledger, encoding="utf-8")
    capability.mkdir(parents=True)
    if records is None:
        records = [RECORD_BOOL, RECORD_ALIAS]
    document = {"schema": check.SCHEMA_URL, "capability": "TEST-CAPABILITY-001",
                "decision_record": decision_name, "records": records}
    (capability / "setting-dispositions.json").write_text(json.dumps(document, indent=2), encoding="utf-8")
    (capability / "test-vectors.json").write_text(json.dumps(vectors), encoding="utf-8")
    if decision:
        decision_path = root / "product-contract" / decision_name
        decision_path.parent.mkdir(parents=True, exist_ok=True)
        decision_path.write_text("# decision\n", encoding="utf-8")
    return root


def run(root: Path):
    problems = check.Problems()
    rows = check.ledger_rows(root / check.LEDGER_PATH)
    for path in sorted(root.glob(check.DISPOSITIONS_GLOB)):
        check.check_file(path, root, rows, problems)
    return problems


class SettingDispositionCheckTest(unittest.TestCase):
    def test_known_good_fixture_passes(self):
        problems = run(build_repo())
        self.assertEqual(list(problems), [])

    def test_schema_violations_are_reported(self):
        bad = dict(RECORD_BOOL, value_family="maybe", status="blessed")
        problems = run(build_repo(records=[bad, RECORD_ALIAS]))
        text = "\n".join(problems)
        self.assertIn("invalid value_family", text)
        self.assertIn("invalid status", text)

    def test_secret_record_without_credential_migration_fails(self):
        bad = dict(RECORD_BOOL, privacy="secret")
        problems = run(build_repo(records=[bad, RECORD_ALIAS]))
        self.assertIn("must record its credential-service migration", "\n".join(problems))

    def test_secret_record_with_credential_migration_passes(self):
        good = dict(RECORD_BOOL, privacy="secret",
                    migration="Never a JSON setting; the value migrates into the CredentialStore and the "
                              "row retires.")
        problems = run(build_repo(records=[good, RECORD_ALIAS]))
        self.assertEqual(list(problems), [])

    def test_ledger_row_without_record_fails(self):
        ledger = LEDGER + """  - id: "SET-TEST-OPTIONS-UNCOVERED-001"
    domain: "settings"
    user_outcome: "Uncovered."
    classification: "preserve"
    contract: "product-contract/capabilities/TEST-CAPABILITY-001"
    status: "inventoried"
"""
        problems = run(build_repo(ledger=ledger))
        self.assertIn("SET-TEST-OPTIONS-UNCOVERED-001", "\n".join(problems))

    def test_record_without_ledger_row_fails(self):
        orphan = dict(RECORD_BOOL, ledger_id="SET-TEST-OPTIONS-GHOST-001")
        problems = run(build_repo(records=[RECORD_BOOL, RECORD_ALIAS, orphan]))
        self.assertIn("SET-TEST-OPTIONS-GHOST-001", "\n".join(problems))

    def test_duplicate_ledger_ids_fail(self):
        twin = dict(RECORD_ALIAS, name="BorderColorTwin")
        problems = run(build_repo(records=[RECORD_BOOL, RECORD_ALIAS, twin]))
        self.assertIn("duplicate ledger_id", "\n".join(problems))

    def test_missing_family_vectors_fail(self):
        vectors = {"schema_version": 2, "capability": "TEST-CAPABILITY-001", "comparison": "subset",
                   "vectors": []}
        problems = run(build_repo(vectors=vectors))
        self.assertIn("approved records need defaults-boolean and invalid-boolean vectors",
                      "\n".join(problems))

    def test_defaults_vector_disagreeing_with_the_decided_default_fails(self):
        vectors = json.loads(json.dumps(VECTORS))
        vectors["vectors"][0]["expected"]["Shadow"] = True
        problems = run(build_repo(vectors=vectors))
        self.assertIn("must equal the decided default False", "\n".join(problems))

    def test_invalid_vector_without_an_approved_target_fails(self):
        vectors = json.loads(json.dumps(VECTORS))
        vectors["vectors"][1]["input"]["stored"] = {"Unknown": "yes"}
        problems = run(build_repo(vectors=vectors))
        self.assertIn("stored input must include an approved boolean field", "\n".join(problems))

    def test_missing_decision_record_fails(self):
        problems = run(build_repo(decision=False))
        self.assertIn("decision_record must name an existing decisions/ document", "\n".join(problems))

    def test_wrong_schema_pointer_fails(self):
        root = build_repo()
        path = root / "product-contract" / "capabilities" / "TEST-CAPABILITY-001" / "setting-dispositions.json"
        document = json.loads(path.read_text())
        document["schema"] = "https://example.com/other.json"
        path.write_text(json.dumps(document), encoding="utf-8")
        problems = run(root)
        self.assertIn("schema must be", "\n".join(problems))

    def test_draft_records_do_not_demand_vectors(self):
        draft = dict(RECORD_BOOL, status="draft")
        vectors = {"schema_version": 2, "capability": "TEST-CAPABILITY-001", "comparison": "subset",
                   "vectors": []}
        problems = run(build_repo(records=[draft, RECORD_ALIAS], vectors=vectors))
        self.assertEqual(list(problems), [])

    def test_runtime_only_family_is_exempt_from_vectors(self):
        records = [RECORD_ALIAS]
        vectors = {"schema_version": 2, "capability": "TEST-CAPABILITY-001", "comparison": "subset",
                   "vectors": []}
        ledger = LEDGER.replace("""  - id: "SET-TEST-OPTIONS-SHADOW-001"
    domain: "settings"
    user_outcome: "Shadow default."
    classification: "preserve"
    contract: "product-contract/capabilities/TEST-CAPABILITY-001"
    status: "inventoried"
""", "")
        problems = run(build_repo(records=records, ledger=ledger, vectors=vectors))
        self.assertEqual(list(problems), [])


if __name__ == "__main__":
    unittest.main()
