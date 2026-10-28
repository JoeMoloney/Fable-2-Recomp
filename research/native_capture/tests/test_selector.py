import copy
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from selector import matches, state_signature

ROOT = Path(__file__).resolve().parents[1]


class SelectorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.selector = json.loads((ROOT / "selector-standard-material-v1.json").read_text())

    def test_canonical_selector_record_hash_and_match(self):
        record = {
            "vs_hash": int(self.selector["vs_hash"], 16),
            "ps_hash": int(self.selector["ps_hash"], 16),
            **copy.deepcopy(self.selector["match"]),
        }
        self.assertEqual(f"{state_signature(record):016x}", self.selector["state_signature_fnv64"])
        self.assertTrue(matches(record, self.selector))

    def test_selector_rejects_pair_geometry_texture_and_rt_changes(self):
        base = {
            "vs_hash": int(self.selector["vs_hash"], 16),
            "ps_hash": int(self.selector["ps_hash"], 16),
            **copy.deepcopy(self.selector["match"]),
        }
        for mutation in (
            lambda row: row.__setitem__("vs_hash", row["vs_hash"] ^ 1),
            lambda row: row.__setitem__("ps_hash", row["ps_hash"] ^ 1),
            lambda row: row.__setitem__("index_count", row["index_count"] + 3),
            lambda row: row["vertex_fetches"][0]["words"].__setitem__(0, 0),
            lambda row: row["texture_fetches"][0]["words"].__setitem__(1, 0),
            lambda row: row["render_target_registers"].__setitem__(10, 0),
        ):
            row = copy.deepcopy(base)
            mutation(row)
            self.assertFalse(matches(row, self.selector))

    def test_selector_provenance_and_repeat_group_are_explicit(self):
        self.assertEqual(self.selector["source_capture_sha256"],
                         "a644e1c24cb93e72f95dd85b28ba0e96097f5414e145b8e1f5d15cedae8eabcf")
        self.assertEqual(self.selector["source_sample_equal_state_records"], 18)
        self.assertEqual(self.selector["source_sample_ordinal"], 1641)


if __name__ == "__main__":
    unittest.main(verbosity=2)
