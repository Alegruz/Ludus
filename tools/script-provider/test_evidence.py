"""Reject incomplete or contradictory S6 execution evidence."""
import unittest
from verify_safety import CASES, inspect_output


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.output = 'S6 TARGET os=Linux arch=x86_64 pointer_bits=64\n'
        self.output += ''.join(f'S6 PASS {case}\n' for case in CASES)
        self.output += ''.join(f'S6 METRIC {name}-allocation-points=3\n' for name in ('invoke', 'debug', 'replacement'))

    def test_completed_run_identifies_actual_target_and_sweeps(self):
        record = inspect_output(self.output)
        self.assertEqual(record['system'], 'Linux')
        self.assertEqual(record['allocation_points']['debug'], 3)
        wasm = self.output.replace('os=Linux arch=x86_64 pointer_bits=64', 'os=Emscripten arch=wasm32 pointer_bits=32')
        self.assertEqual(inspect_output(wasm)['pointer_bits'], 32)

    def test_missing_duplicate_unknown_cases_and_failure_are_rejected(self):
        line = f'S6 PASS {CASES[0]}\n'
        for output in (self.output.replace(line, ''), self.output + line,
                       self.output + 'S6 PASS unknown\n', self.output + 'S6 FAIL fault\n',
                       self.output.replace(line, line.rstrip() + '-suffix\n')):
            with self.subTest(output=output), self.assertRaises(ValueError):
                inspect_output(output)

    def test_unexecuted_or_ambiguous_sweeps_are_rejected(self):
        for output in (self.output.replace('debug-allocation-points=3', 'debug-allocation-points=0'),
                       self.output.replace('S6 METRIC debug-allocation-points=3\n', ''),
                       self.output + 'S6 METRIC debug-allocation-points=3\n'):
            with self.subTest(output=output), self.assertRaises(ValueError):
                inspect_output(output)

    def test_unadmitted_or_ambiguous_target_is_rejected(self):
        marker = 'S6 TARGET os=Linux arch=x86_64 pointer_bits=64\n'
        for output in (self.output.replace(marker, ''), self.output + marker,
                       self.output.replace('os=Linux', 'os=Android'),
                       self.output.replace('pointer_bits=64', 'pointer_bits=32')):
            with self.subTest(output=output), self.assertRaises(ValueError):
                inspect_output(output)


if __name__ == '__main__':
    unittest.main()
