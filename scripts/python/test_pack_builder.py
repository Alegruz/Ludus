import binascii
import ctypes
import ctypes.util
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest

from pack_builder import (BLOCK, ENTRY, HEADER, PackError, build_bytes, build_stream,
                          canonical_paths, encode_lz4, main, portable_path)


class PackBuilderTests(unittest.TestCase):
    def test_reproducible_sorted_index_and_independent_layout(self):
        entries = [("z", b"zz"), ("a", b"A" * 70000), ("empty", b"")]
        canonical = build_bytes(entries)
        self.assertEqual(canonical, build_bytes(list(reversed(entries))))
        reordered = build_bytes(entries, layout={"version": 1, "paths": ["z"]})
        self.assertNotEqual(canonical, reordered)
        def read_index(blob):
            position = HEADER.size
            keys = []
            for _ in range(HEADER.unpack_from(blob)[5]):
                length, first, count, _, decoded, stored = ENTRY.unpack_from(blob, position)
                position += ENTRY.size
                key = blob[position:position + length]
                position += length
                keys.append((key, first, count, decoded, stored))
            return keys, position
        before, at = read_index(canonical)
        after, other_at = read_index(reordered)
        self.assertEqual(before, after)
        self.assertEqual([row[0] for row in before], [b"a", b"empty", b"z"])
        self.assertEqual(at, other_at)
        header = HEADER.unpack_from(canonical)
        self.assertEqual(header[10], len(canonical))
        header_bytes = bytearray(canonical[:HEADER.size])
        struct.pack_into("<I", header_bytes, 68, 0)
        self.assertEqual(header[12], binascii.crc32(header_bytes))
        self.assertEqual(header[11], binascii.crc32(canonical[80:header[9]]))
        raw = build_bytes(entries, compress=False)
        self.assertGreater(len(raw), len(canonical))

    def test_portable_reserved_and_collision_admission(self):
        for path in ["", "/a", "a//b", "../a", "a/.", "a\\b", "a:b", "a\0b", "a\x7f", "a.", "a ",
                     "CON", "a/nul.txt", "LPT¹.txt", "COM9", "CONIN$", "a?b", "a|b", "x" * 1025,
                     "a/" + "😀" * 128, "\ud800"]:
            with self.subTest(path=repr(path)), self.assertRaises(PackError):
                portable_path(path)
        for paths in [["data", "Data"], ["é", "e\u0301"], ["A/x", "a/y"], ["a", "a/b"], ["a", "a"]]:
            with self.subTest(paths=paths), self.assertRaises(PackError):
                canonical_paths(paths)
        self.assertEqual(canonical_paths(["z", "nested/音.wav"]), ["nested/音.wav", "z"])

    def test_layout_and_block_policy_admission(self):
        for layout in [{"version": 2, "paths": []}, {"version": 1, "paths": ["missing"]},
                       {"version": 1, "paths": ["a", "a"]}, {"version": 1, "paths": "a"}]:
            with self.assertRaises(PackError):
                build_bytes([("a", b"data")], layout=layout)
        for size in [0, 255, 257, 65537]:
            with self.assertRaises(PackError):
                build_bytes([], block_size=size)
        self.assertEqual(len(build_bytes([])), 80)

    def test_encoder_compatible_with_independent_system_lz4(self):
        library = ctypes.util.find_library("lz4")
        if not library:
            self.skipTest("system LZ4 oracle unavailable")
        decoder = ctypes.CDLL(library).LZ4_decompress_safe
        decoder.argtypes = [ctypes.c_char_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
        decoder.restype = ctypes.c_int
        for data in [b"", b"abc", b"A" * 65536, bytes(range(256)) * 256,
                     b"abcabcde" * 4000, bytes((i * 91 + i // 37) & 255 for i in range(4096))]:
            packed = encode_lz4(data)
            destination = ctypes.create_string_buffer(max(len(data), 1))
            self.assertEqual(decoder(packed, destination, len(packed), len(data)), len(data))
            self.assertEqual(destination.raw[:len(data)], data)

    def test_streaming_reads_are_bounded_and_failed_build_preserves_output(self):
        class Bounded(io.BytesIO):
            def read(self, size=-1):
                if size < 0 or size > 256:
                    raise AssertionError("unbounded source read")
                return super().read(size)
        output = io.BytesIO()
        build_stream(["a"], lambda _: Bounded(b"x" * 10000), output, block_size=256)
        self.assertGreater(len(output.getvalue()), 80)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "sources"
            source.mkdir()
            (source / "asset").write_bytes(b"bytes")
            archive = root / "result.lpk"
            archive.write_bytes(b"existing")
            self.assertEqual(main([str(source), str(archive), "--block-bytes", "257"]), 1)
            self.assertEqual(archive.read_bytes(), b"existing")
            self.assertEqual(list(root.glob("*.tmp")), [])
            self.assertEqual(main([str(source), str(archive)]), 0)
            before = archive.read_bytes()
            self.assertEqual(main([str(source), str(archive)]), 0)
            self.assertEqual(archive.read_bytes(), before)
            self.assertEqual(main([str(source), str(source / "pack")]), 1)
            if hasattr(Path, "symlink_to"):
                (source / "link").symlink_to(source / "asset")
                self.assertEqual(main([str(source), str(archive)]), 1)
                self.assertEqual(archive.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
