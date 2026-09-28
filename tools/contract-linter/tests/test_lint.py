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

    def test_approval_for_earlier_contract_version_does_not_cover(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            contract = repo / "product-contract"
            (contract / "reviews").mkdir(parents=True)
            (contract / "manifest.yaml").write_text(
                "contract_version: 1.3.0\nstatus: draft\ncapabilities:\n"
                "  - id: SAMPLE-CAPABILITY-001\n"
                "    path: capabilities/SAMPLE-CAPABILITY-001\n"
                "    version: 1.0.0\n    status: approved\n"
                "    requirements: [SAM-001]\n",
                encoding="utf-8",
            )
            (contract / "reviews" / "APPROVAL-2026-01-01-OLD.json").write_text(
                json.dumps({
                    "schema_version": 1,
                    "decision": "approved",
                    "authority": {"role": "human-product-owner", "identity": "owner"},
                    "scope": {"contract_version": "1.2.3", "packages": ["SAMPLE-CAPABILITY-001"]},
                    "effects": {"normative_contract": True},
                }),
                encoding="utf-8",
            )
            findings = LINT.Findings()
            LINT.lint_approval_records(repo, findings)
            self.assertEqual(1, len(findings.items))
            self.assertIn("lacks human product-owner approval record: SAMPLE-CAPABILITY-001", findings.items[0])

    def test_vector_file_structure_is_validated(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            contract = repo / "product-contract"
            package = contract / "capabilities" / "SAMPLE-CAPABILITY-001"
            package.mkdir(parents=True)
            (contract / "manifest.yaml").write_text(
                "contract_version: 1.2.3\nstatus: draft\ncapabilities:\n"
                "  - id: SAMPLE-CAPABILITY-001\n"
                "    path: capabilities/SAMPLE-CAPABILITY-001\n"
                "    version: 1.0.0\n    status: draft\n"
                "    requirements: [SAM-001]\n",
                encoding="utf-8",
            )
            (package / "test-vectors.json").write_text(
                json.dumps({
                    "schema_version": 2,
                    "capability": "SAMPLE-CAPABILITY-001",
                    "comparison": "subset",
                    "vectors": [
                        {"id": "one", "operation": "run", "requirements": ["SAM-001"], "input": {}, "expected": {}},
                        {"id": "one", "requirements": ["SAM-999"], "input": {}, "expected": {}},
                    ],
                }),
                encoding="utf-8",
            )
            findings = LINT.Findings()
            LINT.lint_vectors(repo, findings)
            joined = "\n".join(findings.items)
            self.assertIn("duplicate vector id one", joined)
            self.assertIn("one: vector has no valid operation", joined)
            self.assertIn("one: unknown requirement SAM-999", joined)

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

    def _write(self, root: Path, relative_path: str, text: str) -> None:
        path = root / relative_path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def _hierarchy(self, root: Path, child_text: str, root_index: str = "[child](child/AGENTS.md)") -> None:
        self._write(root, "AGENTS.md", f"# Root\n\n- **ROOT-TEST-001** Root rule.\n\n{root_index}\n")
        self._write(root, "child/AGENTS.md", child_text)

    def test_valid_child_hierarchy_has_no_findings(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self._hierarchy(root, "Applies to: child/**\nParent: ../AGENTS.md\n\n- **CHILD-TEST-001** Rule.\n")
            findings = LINT.Findings()
            LINT.lint_agents(root, findings)
            self.assertEqual([], findings.items)

    def test_child_cannot_declare_protected_root_rule(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self._hierarchy(root, "Applies to: child/**\nParent: ../AGENTS.md\n\n- **ROOT-CHILD-001** Override.\n")
            findings = LINT.Findings()
            LINT.lint_agents(root, findings)
            self.assertTrue(any("protected root rule ID ROOT-CHILD-001" in item for item in findings.items))

    def test_unindexed_child_scope_is_reported(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self._hierarchy(root, "Applies to: child/**\nParent: ../AGENTS.md\n", root_index="No children.")
            findings = LINT.Findings()
            LINT.lint_agents(root, findings)
            self.assertTrue(any("undeclared scope" in item for item in findings.items))

    def test_scope_parent_and_link_mismatches_are_reported(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self._hierarchy(root, "Applies to: child/**\nParent: ../AGENTS.md\n")
            self._write(
                root,
                "child/grand/AGENTS.md",
                "Applies to: elsewhere/**\nParent: ../../AGENTS.md\n\n[missing](missing/)\n",
            )
            findings = LINT.Findings()
            LINT.lint_agents(root, findings)
            joined = "\n".join(findings.items)
            self.assertIn("does not match directory scope 'child/grand/**'", joined)
            self.assertIn("nearest ancestor instruction file: child/AGENTS.md", joined)
            self.assertIn("broken relative link: missing/", joined)

    def test_effective_report_lists_root_to_leaf_chain(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self._hierarchy(root, "Applies to: child/**\nParent: ../AGENTS.md\n\n- **CHILD-TEST-001** Rule.\n")
            report = LINT.effective_report(root, ["child/deep/file.txt", "top.txt"])
            self.assertIn("## AGENTS.md → child/AGENTS.md", report)
            self.assertIn("`child/AGENTS.md`: CHILD-TEST-001", report)
            self.assertIn("- `child/deep/file.txt`", report)
            self.assertIn("- `top.txt`", report)

    def test_repository_paths_resolve_effective_chain(self) -> None:
        repo = Path(__file__).resolve().parents[3]
        chain = LINT.effective_chain(repo, "tools/contract-linter/lint.py", LINT.instruction_files(repo))
        self.assertEqual(
            ["AGENTS.md", "tools/AGENTS.md", "tools/contract-linter/AGENTS.md"],
            [LINT.relative(path, repo.resolve()) for path in chain],
        )


if __name__ == "__main__":
    unittest.main()
