import copy
import importlib.util
import sys
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).resolve().parents[1] / "tracker.py"
SPEC = importlib.util.spec_from_file_location("contract_backlog_tracker", MODULE_PATH)
assert SPEC and SPEC.loader
tracker = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = tracker
SPEC.loader.exec_module(tracker)


class ContractBacklogTrackerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.backlog = tracker.load_backlog()
        cls.rows = tracker.load_rows()

    def test_repository_backlog_is_valid(self):
        self.assertEqual([], tracker.validate(self.backlog, self.rows))

    def test_every_current_open_row_is_assigned(self):
        open_rows = sum(
            row.contract is None
            for ledger_rows in self.rows.values()
            for row in ledger_rows
        )
        assigned = tracker.assignments(self.backlog, self.rows)
        self.assertEqual(open_rows, sum(len(rows) for rows in assigned.values()))

    def test_linked_rows_remain_visible_to_their_review_package(self):
        routed = tracker.routed_rows(self.backlog, self.rows, open_only=False)
        settings = routed["PC-SETTINGS-CATALOG-001"]
        self.assertEqual(922, len(settings))
        self.assertTrue(all(row.contract is not None for row in settings))

    def test_missing_domain_route_is_reported(self):
        backlog = copy.deepcopy(self.backlog)
        del backlog["routing"]["interface"]["values"]["gui"]
        rows = copy.deepcopy(self.rows)
        rows["interface"].append(
            tracker.LedgerRow("interface", "IF-GUI-SYNTHETIC-999", "gui", None)
        )
        findings = tracker.validate(backlog, rows)
        self.assertTrue(any("unassigned open row interface:" in item for item in findings))

    def test_dependency_cycle_is_reported(self):
        backlog = copy.deepcopy(self.backlog)
        governance = next(
            package for package in backlog["packages"]
            if package["id"] == "PC-GOVERNANCE-001"
        )
        governance["depends_on"] = ["PC-CONFORMANCE-001"]
        findings = tracker.validate(backlog, self.rows)
        self.assertTrue(any(item.startswith("dependency cycle:") for item in findings))

    def test_completed_package_cannot_own_open_rows(self):
        backlog = copy.deepcopy(self.backlog)
        rows = copy.deepcopy(self.rows)
        rows["capability"].append(
            tracker.LedgerRow(
                "capability", "CAPTURE-SYNTHETIC-999", "capture", None
            )
        )
        capture = next(
            package for package in backlog["packages"]
            if package["id"] == "PC-CAPTURE-001"
        )
        capture["status"] = "complete"
        governance = next(
            package for package in backlog["packages"]
            if package["id"] == "PC-GOVERNANCE-001"
        )
        governance["status"] = "complete"
        findings = tracker.validate(backlog, rows)
        self.assertIn(
            "completed package PC-CAPTURE-001 still owns 1 open rows",
            findings,
        )

    def test_progress_cannot_skip_incomplete_dependencies(self):
        backlog = copy.deepcopy(self.backlog)
        app_shell = next(
            package for package in backlog["packages"]
            if package["id"] == "PC-APP-SHELL-001"
        )
        app_shell["status"] = "in_progress"
        findings = tracker.validate(backlog, self.rows)
        self.assertIn(
            "in_progress package PC-APP-SHELL-001 has incomplete dependency PC-GOVERNANCE-001",
            findings,
        )

    def test_governance_completion_requires_closed_census(self):
        backlog = copy.deepcopy(self.backlog)
        governance = next(
            package for package in backlog["packages"]
            if package["id"] == "PC-GOVERNANCE-001"
        )
        governance["status"] = "complete"
        findings = tracker.validate(backlog, self.rows)
        self.assertIn(
            "completed governance package requires a closed baseline census",
            findings,
        )

    def test_status_report_is_deterministic_and_complete(self):
        first = tracker.render_status(self.backlog, self.rows)
        second = tracker.render_status(self.backlog, self.rows)
        self.assertEqual(first, second)
        total = sum(len(ledger_rows) for ledger_rows in self.rows.values())
        open_rows = sum(
            row.contract is None
            for ledger_rows in self.rows.values()
            for row in ledger_rows
        )
        self.assertIn(f"{total - open_rows:,} of {total:,} parity rows linked", first)
        self.assertIn("PC-CONFORMANCE-001", first)
        expected_mark = "x" if open_rows == 0 else " "
        self.assertIn(
            f"- [{expected_mark}] Every parity-ledger row links to a stable Product Contract requirement",
            first,
        )


if __name__ == "__main__":
    unittest.main()
