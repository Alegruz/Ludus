import hashlib
import unittest
from streaming import make_bytes, make_trace


class StreamingFixtureTests(unittest.TestCase):
    def test_trace_families_preserve_bounds_and_cadence(self):
        for family in ("startup", "transition", "background"):
            rows = make_trace(family)
            self.assertEqual(rows, make_trace(family))
            self.assertEqual(len(rows), 256)
            self.assertEqual([row[0] for row in rows], sorted(row[0] for row in rows))
            self.assertEqual(any(row[5] for row in rows), family == "transition")
            for release, file_index, offset, length, priority, cancel in rows:
                self.assertLess(file_index, 72)
                self.assertLessEqual(offset + length, 4096 if file_index < 64 else 2 * 1024 * 1024)
        self.assertEqual({row[0] for row in make_trace("startup")}, {0})
        self.assertEqual(make_trace("background")[1][0], 1_000_000)

    def test_payload_is_position_sensitive_and_spans_compressibility_classes(self):
        compressible = make_bytes(0, 8192)
        mixed = make_bytes(2, 8192)
        random = make_bytes(1, 8192)
        self.assertEqual(compressible[:4096], compressible[4096:])
        self.assertNotEqual(random[:4096], random[4096:])
        self.assertNotEqual(mixed[:4096], mixed[4096:])
        # Pinned digest detects fixture changes, including interior bytes that
        # the old first/last-byte benchmark could not detect.
        self.assertEqual(hashlib.sha256(random).hexdigest(), "7cf31c25dfca048e6f4d644e3f183516a18ae4288d15e6de79a0eec61ac62384")


if __name__ == "__main__":
    unittest.main()
