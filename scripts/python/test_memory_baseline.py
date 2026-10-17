"""Regressions for the memory baseline runner's parsing and summaries."""
import unittest

from memory_baseline import parse_samples, percentile, summarize


class ParseSamplesTests(unittest.TestCase):
    def test_parses_integer_rows(self):
        rows = parse_samples("workload,sample,operations,nanoseconds,bytes,checksum\nidle,0,1,0,0,0\nidle,1,4,10,8,3\n")
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[1], {"workload": "idle", "sample": 1, "operations": 4, "nanoseconds": 10, "bytes": 8, "checksum": 3})

    def test_rejects_unexpected_header(self):
        with self.assertRaises(ValueError):
            parse_samples("workload,ns\nidle,1\n")

    def test_rejects_non_integer_values(self):
        with self.assertRaises(ValueError):
            parse_samples("workload,sample,operations,nanoseconds,bytes,checksum\nidle,0,1,1.5,0,0\n")


class SummaryTests(unittest.TestCase):
    def test_percentile_interpolates(self):
        self.assertEqual(percentile([4.0, 1.0, 3.0, 2.0], 0.5), 2.5)
        self.assertEqual(percentile([7.0], 0.25), 7.0)

    def test_summary_separates_workloads_and_skips_zero_operation_rows(self):
        samples = [
            {"workload": "a", "operations": 2, "nanoseconds": 10},
            {"workload": "a", "operations": 2, "nanoseconds": 30},
            {"workload": "b", "operations": 0, "nanoseconds": 0},
        ]
        runs = [
            {"workload": "a", "peak_rss_kib": 100},
            {"workload": "a", "peak_rss_kib": 300},
            {"workload": "b", "peak_rss_kib": 50},
        ]
        summary = summarize(samples, runs)
        self.assertEqual(summary["a"]["ns_per_operation_median"], 10.0)
        self.assertEqual(summary["a"]["peak_rss_kib_median"], 200.0)
        self.assertEqual(summary["b"]["samples"], 0)
        self.assertNotIn("ns_per_operation_median", summary["b"])
        self.assertEqual(summary["b"]["peak_rss_kib_max"], 50.0)


if __name__ == "__main__":
    unittest.main()
