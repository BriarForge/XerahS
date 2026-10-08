import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from xerahs_conformance import compare, deep_merge  # noqa: E402


class DeepMergeTests(unittest.TestCase):
    def test_vector_value_wins_and_objects_merge(self):
        defaults = {"options": {"a": 1, "b": 2}, "outcomes": {}}
        merged = deep_merge(defaults, {"options": {"b": 3}, "extra": True})
        self.assertEqual(merged, {"options": {"a": 1, "b": 3}, "outcomes": {}, "extra": True})

    def test_non_object_override_replaces(self):
        self.assertEqual(deep_merge({"a": {"x": 1}}, {"a": [1]}), {"a": [1]})


class CompareTests(unittest.TestCase):
    def test_exact_rejects_extra_keys(self):
        self.assertTrue(compare({"a": 1}, {"a": 1, "b": 2}, "exact"))
        self.assertFalse(compare({"a": 1}, {"a": 1, "b": 2}, "subset"))

    def test_subset_arrays_must_match_length(self):
        self.assertTrue(compare([{"a": 1}], [{"a": 1}, {"a": 2}], "subset"))
        self.assertFalse(compare([{"a": 1}], [{"a": 1, "b": 2}], "subset"))

    def test_types_are_strict(self):
        self.assertTrue(compare(1, 1.0, "exact"))
        self.assertTrue(compare(1, True, "exact"))
        self.assertTrue(compare(None, 0, "exact"))

    def test_null_requires_present_key(self):
        self.assertTrue(compare({"token": None}, {}, "subset"))
        self.assertFalse(compare({"token": None}, {"token": None}, "exact"))

    def test_int64_bounds(self):
        self.assertFalse(compare(2**63 - 1, 2**63 - 1, "exact"))
        self.assertTrue(compare(2**63, 2**63, "exact"))


if __name__ == "__main__":
    unittest.main()
