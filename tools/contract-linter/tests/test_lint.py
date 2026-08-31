from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).resolve().parents[1] / "lint.py"
SPEC = importlib.util.spec_from_file_location("contract_lint", MODULE_PATH)
assert SPEC and SPEC.loader
LINT = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = LINT
SPEC.loader.exec_module(LINT)


class ContractLintTests(unittest.TestCase):
    def test_repository_fixture_is_known_good(self) -> None:
        repo = Path(__file__).resolve().parents[3]
        self.assertEqual([], LINT.lint_repository(repo))

    def test_bad_fixture_reports_duplicate_root_rule(self) -> None:
        bad = Path(__file__).resolve().parents[1] / "fixtures" / "bad"
        findings = LINT.Findings()
        LINT.lint_agents(bad, findings)
        self.assertTrue(any("duplicate rule ID ROOT-TEST-001" in item for item in findings.items))

    def test_good_instruction_fixture_has_no_findings(self) -> None:
        good = Path(__file__).resolve().parents[1] / "fixtures" / "good"
        findings = LINT.Findings()
        LINT.lint_agents(good, findings)
        self.assertEqual([], findings.items)

    def test_manifest_parser_reads_inline_requirements(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.yaml"
            path.write_text(
                "contract_version: 1.2.3\nstatus: draft\ncapabilities:\n"
                "  - id: SAMPLE-CAPABILITY-001\n"
                "    path: capabilities/SAMPLE-CAPABILITY-001\n"
                "    version: 1.0.0\n    status: draft\n"
                "    requirements: [SAM-001, SAM-002]\n",
                encoding="utf-8",
            )
            findings = LINT.Findings()
            version, status, capabilities = LINT.parse_manifest(path, findings)
            self.assertEqual("1.2.3", version)
            self.assertEqual("draft", status)
            self.assertEqual(["SAM-001", "SAM-002"], capabilities[0].requirements)
            self.assertEqual([], findings.items)

    def test_approved_capability_requires_human_approval_record(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            contract = repo / "product-contract"
            contract.mkdir()
            (contract / "manifest.yaml").write_text(
                "contract_version: 1.2.3\nstatus: draft\ncapabilities:\n"
                "  - id: SAMPLE-CAPABILITY-001\n"
                "    path: capabilities/SAMPLE-CAPABILITY-001\n"
                "    version: 1.0.0\n    status: approved\n"
                "    requirements: [SAM-001]\n",
                encoding="utf-8",
            )
            findings = LINT.Findings()
            LINT.lint_approval_records(repo, findings)
            self.assertTrue(
                any(
                    "approved capability lacks human product-owner approval record: SAMPLE-CAPABILITY-001"
                    in item
                    for item in findings.items
                )
            )

    def test_image_editor_catalogs_match_pinned_census(self) -> None:
        repo = Path(__file__).resolve().parents[3]
        findings = LINT.Findings()
        LINT.lint_editor_catalogs(repo, findings)
        self.assertEqual([], findings.items)

    def test_image_editor_catalog_drift_is_reported(self) -> None:
        source = Path(__file__).resolve().parents[3] / "product-contract"
        relatives = [
            Path("reference-baselines/provenance/kova-0.29.0-census.json"),
            Path("capabilities/EDITOR-ANNOTATIONS-001/annotation-tools.json"),
            Path("capabilities/EDITOR-EFFECTS-001/effect-catalog-selection.json"),
            Path("capabilities/EDITOR-UTILITIES-001/utility-catalog.json"),
        ]
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            for relative in relatives:
                destination = repo / "product-contract" / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes((source / relative).read_bytes())
            tools_path = repo / "product-contract" / relatives[1]
            tools = json.loads(tools_path.read_text(encoding="utf-8-sig"))
            tools["tools"].pop()
            tools_path.write_text(json.dumps(tools), encoding="utf-8")
            findings = LINT.Findings()
            LINT.lint_editor_catalogs(repo, findings)
            self.assertTrue(any("tool selection differs" in item for item in findings.items))


if __name__ == "__main__":
    unittest.main()
