"""R7 qualification sample validation; execute the real benchmark parser."""
import importlib.machinery
import importlib.util
from pathlib import Path
import unittest

SOURCE = Path(__file__).resolve().parents[1] / "benchmark-rhi"
LOADER = importlib.machinery.SourceFileLoader("rhi_benchmark", str(SOURCE))
SPEC = importlib.util.spec_from_loader(LOADER.name, LOADER)
BENCHMARK = importlib.util.module_from_spec(SPEC)
LOADER.exec_module(BENCHMARK)


def samples(state=3):
    return "\n".join(f"R7,{layers},{iteration},{merge},{state},0,0,0,100,1,128,128,10,20,0,0,0"
                     for layers in (1, 8, 16, 31) for iteration in range(128) for merge in (0, 1))


class Measurements(unittest.TestCase):
    def test_quantized_zero_is_available(self):
        rows = BENCHMARK.parse_measurements(samples())
        self.assertEqual(len(rows), 1024)
        self.assertEqual(BENCHMARK.distribution([row[4] for row in rows]), {"p50": 0, "p95": 0, "p99": 0})

    def test_unavailable_disjoint_failure_have_no_duration(self):
        for state in (4, 5, 6):
            self.assertEqual(len(BENCHMARK.parse_measurements(samples(state))), 1024)
            with self.assertRaises(ValueError):
                BENCHMARK.parse_measurements(samples(state).replace(f",{state},0,0,0,100", f",{state},1,0,0,100", 1))

    def test_incomplete_duplicate_nonfinite_pending_and_opaque_reject(self):
        text = samples()
        rows = text.splitlines()
        for invalid in ("\n".join(rows[:-1]), "\n".join([rows[1]] + rows[1:]),
                        text.replace(",3,0,0,0,100", ",2,0,0,0,100", 1),
                        text.replace(",3,0,0,0,100", ",3,nan,0,0,100", 1),
                        text.replace(",100,1,128,128", ",100,0,128,128", 1),
                        text.replace("R7,1,0,0,3", "R7,1,0.5,0,3", 1)):
            with self.subTest(invalid=invalid[:80]), self.assertRaises(ValueError):
                BENCHMARK.parse_measurements(invalid)


if __name__ == "__main__":
    unittest.main()
