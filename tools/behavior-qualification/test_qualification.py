"""Qualification evidence must reject fabricated outcomes or profile/target drift."""
import copy
import json
from pathlib import Path
import tempfile
import unittest

import qualify


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.profile = qualify.profile(qualify.ROOT / 'config/behavior_qualification.json')
        self.target = {'os': 'Linux', 'arch': 'x86_64', 'pointer_bits': 64}
        cases = []
        for name in qualify.CASES:
            ready = 0 if name.endswith('.dormant') else 100 if name.endswith('.encounter') else 1
            cases.append({'name': name, 'ready': ready, 'dormant': 1100 - ready,
                          'commands': 300 * ready if name.endswith('.operation') else 3 * ready if name.endswith('.encounter') else 0,
                          'interactions': 100 * ready if name.endswith(('.scalar', '.encounter')) else 0,
                          'opened': ready if name.endswith('.encounter') else 0, 'entity_checks': ready * 300,
                          'aligned_requests': 0, 'aligned_requested_bytes': 0, 'boundary_live_max_bytes': 0,
                          'samples_ns': list(range(300)), 'p50_ns': 149, 'p95_ns': 284, 'p99_ns': 296, 'worst_ns': 299})
        self.value = {'version': 1, 'instrumented': False, 'target': self.target, 'samples': 300, 'warmup': 30,
                      'active': 100, 'inactive': 1000, 'contract': 'a' * 64, 'package': 'b' * 64, 'cases': cases,
                      'equivalence': True, 'live_after_close': 0}

    def test_complete_evidence_and_counting_pair(self):
        qualify.measurements(self.value, self.profile, False, self.target)
        counting = copy.deepcopy(self.value)
        counting['instrumented'] = True
        counting['cases'][1]['aligned_requests'] = 10
        counting['cases'][1]['aligned_requested_bytes'] = 160
        qualify.measurements(counting, self.profile, True, self.target)
        qualify.paired(self.value, counting)

    def test_missing_duplicate_unknown_and_wrong_target_rejected(self):
        variants = []
        value = copy.deepcopy(self.value); value['cases'].pop(); variants.append(value)
        value = copy.deepcopy(self.value); value['cases'][1] = value['cases'][0]; variants.append(value)
        value = copy.deepcopy(self.value); value['cases'][0]['name'] = 'managed.encounter'; variants.append(value)
        value = copy.deepcopy(self.value); value['target'] = {'os': 'Linux', 'arch': 'arm64', 'pointer_bits': 64}; variants.append(value)
        for value in variants:
            with self.subTest(value=value['target']), self.assertRaises(ValueError):
                qualify.measurements(value, self.profile, False, self.target)

    def test_samples_percentiles_semantics_and_retirement_rejected(self):
        for name, change in [('p99_ns', 1), ('samples_ns', [1]), ('commands', 1), ('aligned_requests', 1),
                             ('boundary_live_max_bytes', self.profile['heap_limit_bytes'] + 1)]:
            value = copy.deepcopy(self.value); value['cases'][0][name] = change
            with self.subTest(name=name), self.assertRaises(ValueError):
                qualify.measurements(value, self.profile, False, self.target)
        value = copy.deepcopy(self.value); value['live_after_close'] = 1
        with self.assertRaises(ValueError): qualify.measurements(value, self.profile, False, self.target)
        value = copy.deepcopy(self.value); value['cases'][-1]['entity_checks'] = 1
        with self.assertRaises(ValueError): qualify.measurements(value, self.profile, False, self.target)

    def test_counting_mismatch_and_duplicate_json_rejected(self):
        counting = copy.deepcopy(self.value); counting['cases'][1]['entity_checks'] += 1
        with self.assertRaises(ValueError): qualify.paired(self.value, counting)
        with self.assertRaises(ValueError): qualify.parse('{"version":1,"version":1}')
        with self.assertRaises(ValueError): qualify.parse('{"value":NaN}')

    def test_cook_pointer_rejects_path_and_schema_drift(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            valid = {'version': 1, 'key': 'a' * 64, 'directory': 'a' * 64}
            (root / 'current.json').write_text(json.dumps(valid))
            self.assertEqual(qualify.cook_pointer(root), 'a' * 64)
            for key, value in [('directory', '../outside'), ('version', True), ('key', '../outside')]:
                broken = dict(valid); broken[key] = value
                (root / 'current.json').write_text(json.dumps(broken))
                with self.subTest(key=key), self.assertRaises(ValueError):
                    qualify.cook_pointer(root)

    def test_profile_rejects_fictional_capacity_or_product_budget(self):
        for name, value in [('active_encounters', True), ('samples', 0), ('inactive_encounters', 10000),
                            ('timing_policy', 'mandatory-game-budget'), ('unqualified', [])]:
            changed = copy.deepcopy(self.profile); changed[name] = value
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / 'profile.json'; path.write_text(json.dumps(changed))
                with self.subTest(name=name), self.assertRaises(ValueError): qualify.profile(path)


if __name__ == '__main__':
    unittest.main()
