"""Self-tests for the NuGet freshness gate (standard library only)."""
import datetime as dt
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check  # noqa: E402

NOW = dt.datetime(2026, 11, 3, tzinfo=dt.timezone.utc)
CSPROJ = '<Project><ItemGroup><PackageReference Include="CommunityToolkit.Mvvm" Version="8.2.0" /></ItemGroup></Project>'


def record(**overrides):
    base = {"package": "CommunityToolkit.Mvvm", "manifest": "app/App.csproj",
            "source": "https://api.nuget.org/v3/index.json", "checked_at": "2026-11-01T00:00:00Z",
            "previous_version": "8.2.0", "latest_stable": "8.4.0", "selected_version": "8.4.0",
            "verification": "passed"}
    base.update(overrides)
    return base


class NugetFreshnessTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.root = Path(self.dir.name)
        self.records = self.root / "records.json"

    def tearDown(self):
        self.dir.cleanup()

    def run_check(self, records=None):
        if records is not None:
            self.records.write_text(json.dumps({"checks": records}))
        return check.check(self.root, self.records, NOW)

    def add_manifest(self, rel="app/App.csproj", text=CSPROJ):
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def test_no_manifests_passes(self):
        (self.root / "CMakeLists.txt").write_text("project(x)")
        self.assertEqual(self.run_check(), [])

    def test_unrecorded_package_fails(self):
        self.add_manifest()
        self.assertTrue(any("no NuGet check record" in e for e in self.run_check([])))

    def test_fresh_complete_record_passes(self):
        self.add_manifest()
        self.assertEqual(self.run_check([record()]), [])

    def test_stale_record_fails(self):
        self.add_manifest()
        self.assertTrue(any("older than 30 days" in e for e in self.run_check([record(checked_at="2026-09-01T00:00:00Z")])))

    def test_missing_fields_fail(self):
        self.add_manifest()
        self.assertTrue(any("missing" in e for e in self.run_check([record(source="", verification="")])))

    def test_failed_verification_fails(self):
        self.add_manifest()
        self.assertTrue(any("verification" in e for e in self.run_check([record(verification="failed")])))

    def test_retention_below_latest_needs_complete_bounded_block(self):
        self.add_manifest()
        self.assertTrue(any("retention" in e for e in self.run_check([record(selected_version="8.2.0")])))
        bounded = {"rejected_version": "8.4.0", "blocker": "toolchain", "evidence": "build log", "owner": "Mike",
                   "remediation": "raise SDK", "next_review": "2026-11-20"}
        self.assertEqual(self.run_check([record(selected_version="8.2.0", retention=bounded)]), [])
        late = dict(bounded, next_review="2027-01-15")
        self.assertTrue(any("within 30 days" in e for e in self.run_check([record(selected_version="8.2.0", retention=late)])))

    def test_central_versions_and_lock_files_are_found(self):
        self.add_manifest("Directory.Packages.props",
                          '<Project><ItemGroup><PackageVersion Include="Serilog" Version="4.0.0" /></ItemGroup></Project>')
        self.add_manifest("app/packages.lock.json", json.dumps({"dependencies": {"net8.0": {"Polly": {}}}}))
        errors = self.run_check([])
        self.assertTrue(any("Serilog" in e for e in errors))
        self.assertTrue(any("Polly" in e for e in errors))

    def test_build_and_worktree_directories_are_ignored(self):
        self.add_manifest("build/x/App.csproj")
        self.add_manifest("worktrees/other/App.csproj")
        self.assertEqual(self.run_check([]), [])


if __name__ == "__main__":
    unittest.main()
