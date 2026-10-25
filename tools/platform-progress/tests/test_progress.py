import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('platform_progress', Path(__file__).parents[1] / 'update.py')
progress = importlib.util.module_from_spec(spec)
spec.loader.exec_module(progress)


class ProgressTests(unittest.TestCase):
    def test_partial_and_missing_requirements_do_not_earn_credit(self):
        counts = progress.count(['A-001', 'A-002', 'A-003'], {
            'A-001': {'status': 'implemented'}, 'A-002': {'status': 'partial'}})
        self.assertEqual(progress.percent(counts), '33.3%')
        self.assertEqual(counts['unassessed'], 1)

    def test_explicit_mobile_requirement_does_not_inflate_desktop_scope(self):
        counts = progress.count(['CORE-023', 'CORE-029'], {'CORE-023': {'status': 'implemented'}})
        self.assertEqual(progress.percent(counts), '100.0%')

    def test_unknown_ids_and_silent_desktop_exclusions_fail(self):
        with self.assertRaises(ValueError):
            progress.count(['A-001'], {'A-002': {'status': 'implemented'}})
        with self.assertRaises(ValueError):
            progress.count(['A-001'], {'A-001': {'status': 'not-applicable'}})

    def test_overall_is_weighted_by_requirement_pairs(self):
        one = progress.count(['A-001'], {'A-001': {'status': 'implemented'}})
        three = progress.count(['A-001', 'A-002', 'A-003'], {})
        self.assertEqual(progress.percent(one + three), '25.0%')

    def test_repository_reports_cover_every_current_desktop_capability(self):
        _, capabilities = progress.contract(progress.ROOT)
        reports = progress.render(progress.ROOT)
        self.assertEqual(len(reports), 4)
        for report in reports.values():
            for capability in capabilities:
                self.assertIn(capability.identifier, report)
            self.assertIn('platform-requirement pairs', report)
            self.assertIn('not release readiness', report)


if __name__ == '__main__':
    unittest.main()
