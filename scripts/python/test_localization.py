from copy import deepcopy
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

from localization import (CookError, MAX_JSON_BYTES, MAX_MESSAGES, PROFILE, bindings, cook, document,
                          export_review, main, read_json, source_digest, write_atomic)


def source() -> dict:
    return {"version": 1, "domain": "game/ui", "source_locale": "en-US", "profile": PROFILE,
            "messages": [{"key": "menu/play", "source_revision": 1, "text": "Play", "context": "Start game button"},
                         {"key": "menu/quit", "source_revision": 2, "text": "Quit", "context": "Leave game button"}]}


def approved(value: dict) -> dict:
    result = export_review(value, "fr-FR")
    for message in result["messages"]:
        message["status"] = "approved"
        message["text"] = {"menu/play": "Jouer", "menu/quit": "Quitter"}[message["key"]]
    return result


class LocalizationTests(unittest.TestCase):
    def test_deterministic_cook_and_schema_only_bindings(self):
        value = source()
        translation = approved(value)
        binary = cook(value, translation)
        source_header = bindings(value, "game::ui")
        reversed_value, reversed_translation = deepcopy(value), deepcopy(translation)
        reversed_value["messages"].reverse()
        reversed_translation["messages"].reverse()
        self.assertEqual(binary, cook(reversed_value, reversed_translation))
        self.assertEqual(source_header, bindings(reversed_value, "game::ui"))
        value["messages"][0]["text"] = "Launch game"
        self.assertEqual(source_header, bindings(value, "game::ui"))
        header = struct.unpack_from("<12I", binary)
        self.assertEqual(header[:4], (0x434f4c4c, 1, len(binary), 2))
        self.assertEqual(header[7:], (20, 1, 0, 0, 0))
        self.assertIn(b"Jouer", binary)
        self.assertIn(b"menu/play", binary)
        self.assertNotIn(b"Start game button", binary)

    def test_literal_utf8_preserves_whitespace_braces_apostrophes_and_empty(self):
        value = source()
        literal = "\tالعربية 日本語 😀 e\u0301 ' {name}\n"
        value["messages"][0]["text"] = literal
        value["messages"][1]["text"] = ""
        binary = cook(value)
        self.assertIn(literal.encode("utf-8"), binary)
        self.assertNotIn(b"Quit", binary)

    def test_export_is_unapproved_and_contains_context_and_review_receipts(self):
        value = source()
        review = export_review(value, "fr-FR")
        message = review["messages"][0]
        self.assertEqual(message["status"], "needs-review")
        self.assertEqual(message["text"], "")
        self.assertEqual(message["source_text"], "Play")
        self.assertEqual(message["context"], "Start game button")
        self.assertEqual(message["reviewed_source_digest"], source_digest(value, value["messages"][0]))
        with self.assertRaisesRegex(CookError, "missing approved"):
            cook(value, review)

    def test_fallback_is_explicit_and_keeps_provenance(self):
        value = source()
        review = export_review(value, "fr-FR")
        binary = cook(value, review, allow_source_fallback=True)
        header = struct.unpack_from("<12I", binary)
        records = 48 + sum(header[4:7])
        self.assertEqual(struct.unpack_from("<5I", binary, records)[4], 2)
        review["messages"] = review["messages"][:1]
        with self.assertRaisesRegex(CookError, "missing approved"):
            cook(value, review)
        self.assertEqual(binary, cook(value, review, allow_source_fallback=True))

    def test_text_context_revision_and_source_identity_edits_invalidate_review(self):
        for field, replacement in (("text", "Launch"), ("context", "Main menu button"), ("source_revision", 3)):
            value = source()
            review = approved(value)
            value["messages"][0][field] = replacement
            for fallback in (False, True):
                with self.subTest(field=field, fallback=fallback), self.assertRaisesRegex(CookError, "stale"):
                    cook(value, review, allow_source_fallback=fallback)
        for field, replacement in (("domain", "other"), ("source_locale", "en-GB")):
            value = source()
            review = approved(value)
            value[field] = replacement
            with self.assertRaisesRegex(CookError, "disagrees"):
                cook(value, review)

    def test_invalid_keys_duplicates_unknown_fields_types_and_versions(self):
        cases = []
        for key in ("", "/a", "a/", "a//b", "Upper", "a.b", "a_b", "é", "a" * 129):
            value = source()
            value["messages"][0]["key"] = key
            cases.append(value)
        for field, replacement in (("version", True), ("version", 2), ("profile", "ludus-icu-mf1-v1"),
                                   ("messages", {}), ("locale", "en-US")):
            value = source()
            value[field] = replacement
            cases.append(value)
        for field, replacement in (("source_revision", True), ("source_revision", 0), ("source_revision", 2**32),
                                   ("text", None), ("context", "  "), ("arguments", []),
                                   ("text", "zero\0"), ("text", "\ud800"), ("text", "é" * 32769)):
            value = source()
            value["messages"][0][field] = replacement
            cases.append(value)
        value = source()
        value["messages"].append(value["messages"][0])
        cases.append(value)
        for value in cases:
            with self.subTest(value=value), self.assertRaises(CookError):
                cook(value)

    def test_translation_unknown_key_duplicate_bad_status_and_bad_digest(self):
        for field, replacement in (("key", "unknown"), ("reviewed_source_digest", "x" * 64),
                                   ("status", "draft"), ("status", []), ("locale", "en-US"),
                                   ("source_text", "Old text"), ("context", "Old context")):
            value = source()
            review = approved(value)
            if field == "locale":
                review[field] = replacement
            else:
                review["messages"][0][field] = replacement
            with self.subTest(field=field), self.assertRaises(CookError):
                cook(value, review)
        value = source()
        review = approved(value)
        review["messages"].append(review["messages"][0])
        with self.assertRaisesRegex(CookError, "duplicate"):
            cook(value, review)

    def test_locale_shape_is_checked_without_canonicalization(self):
        for tag in ("en", "en-US", "zh-Hant-TW", "es-419", "und", "en-us"):
            value = source()
            value["source_locale"] = tag
            self.assertIn(tag.encode("ascii"), cook(value))
        for tag in ("e", "", "en_US", "en-", "-en", "en--US", "abcdefghi", "en-abcdefghi", "é-US"):
            value = source()
            value["source_locale"] = tag
            with self.subTest(tag=tag), self.assertRaises(CookError):
                cook(value)

    def test_json_rejects_duplicates_constants_bad_utf8_oversize_and_deep_nesting(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "input.json"
            for data in (b'{"key":1,"key":2}', b'{"x":NaN}', b'{"x":Infinity}', b'"string"', b'{"x":"\xff"}',
                         b"[" * 33 + b"0" + b"]" * 33, b" " * (MAX_JSON_BYTES + 1)):
                path.write_bytes(data)
                with self.subTest(prefix=data[:32]), self.assertRaises(CookError):
                    read_json(path)
            path.write_text('{"x":"[[[\\\"{{{{"}', encoding="utf-8")
            self.assertEqual(read_json(path), {"x": '[[["{{{{'})

    def test_message_and_catalog_limits(self):
        value = source()
        value["messages"] = [value["messages"][0]] * (MAX_MESSAGES + 1)
        with self.assertRaisesRegex(CookError, "at most"):
            cook(value)
        value = source()
        value["messages"] = [{"key": f"m-{index:05}", "source_revision": 1, "text": "x" * 65536,
                              "context": "Limit test"} for index in range(513)]
        with self.assertRaisesRegex(CookError, "catalog exceeds"):
            cook(value)

    def test_bindings_detect_collisions_and_invalid_namespaces(self):
        value = source()
        value["messages"][0]["key"] = "menu-play"
        value["messages"][1]["key"] = "menu/play"
        with self.assertRaisesRegex(CookError, "collision"):
            bindings(value, "game::ui")
        value["messages"][0]["symbol"] = "OtherPlay"
        self.assertIn("kOtherPlay", bindings(value, "game::ui"))
        for namespace in ("game;bad", "game::class", "game::__reserved", "namespace", "game::xor"):
            with self.subTest(namespace=namespace), self.assertRaises(CookError):
                bindings(value, namespace)

    def test_cli_preserves_inputs_and_existing_output_on_validation_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path, output, header = root / "source.json", root / "catalog.loc", root / "keys.hpp"
            path.write_text(json.dumps(source()), encoding="utf-8")
            args = ["cook", "--source", str(path), "--output", str(output), "--header", str(header),
                    "--namespace", "game::ui"]
            self.assertEqual(main(args), 0)
            initial = output.read_bytes()
            timestamp = output.stat().st_mtime_ns
            self.assertEqual(main(args), 0)
            self.assertEqual(output.stat().st_mtime_ns, timestamp)
            value = source()
            value["profile"] = "unsupported"
            path.write_text(json.dumps(value), encoding="utf-8")
            process = subprocess.run([sys.executable, str(Path(__file__).with_name("localization.py")), *args],
                                     capture_output=True, text=True, timeout=30)
            self.assertEqual(process.returncode, 1)
            self.assertIn("unsupported", process.stderr)
            self.assertNotIn("Traceback", process.stderr)
            self.assertEqual(output.read_bytes(), initial)
            before = path.read_bytes()
            self.assertEqual(main(["cook", "--source", str(path), "--output", str(path)]), 1)
            self.assertEqual(path.read_bytes(), before)
            write_atomic(output, initial)
            self.assertEqual(output.stat().st_mtime_ns, timestamp)

    def test_empty_catalog_is_supported(self):
        value = source()
        value["messages"] = []
        binary = cook(value)
        self.assertEqual(struct.unpack_from("<I", binary, 12)[0], 0)
        self.assertEqual(len(binary), 48 + len("game/ui") + 2 * len("en-US"))


if __name__ == "__main__":
    unittest.main()
