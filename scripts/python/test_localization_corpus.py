"""Regression tests for the L0 ludus-icu-mf1-v1 document checker and fixture generator."""
from __future__ import annotations

import copy
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import localization_corpus as corpus  # noqa: E402
from localization import CookError  # noqa: E402

CORPUS = Path(__file__).resolve().parents[2] / "modules/localization/corpus"


class CorpusTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        for path in CORPUS.glob("*.json"):
            shutil.copyfile(path, self.root / path.name)

    def tearDown(self) -> None:
        self.directory.cleanup()

    def load(self, name: str) -> dict:
        return json.loads((self.root / name).read_text())

    def save(self, name: str, value: dict) -> None:
        (self.root / name).write_text(json.dumps(value, ensure_ascii=False, indent=2))

    def release(self) -> dict:
        return corpus.load_release(self.root / "release.json")

    def test_checked_in_corpus_is_valid_and_generation_is_deterministic(self):
        first = corpus.probe_header(CORPUS / "release.json")
        self.assertEqual(first, corpus.probe_header(CORPUS / "release.json"))
        self.assertIn("inline constexpr Case CASES[]", first)
        release = corpus.load_release(CORPUS / "release.json")
        self.assertEqual(release["paths"]["fr-FR"], ["fr-FR", "en-US"])
        self.assertEqual(release["domains"][0]["resolved"][("ja-JP", "menu/subtitle")], ("", "ja-JP"))

    def test_source_edit_without_revision_makes_receipts_stale(self):
        source = self.load("game-ui.source.json")
        source["messages"][6]["context"] += " Edited."
        self.save("game-ui.source.json", source)
        with self.assertRaisesRegex(CookError, "menu/play: required message lacks an approved translation"):
            self.release()

    def test_pattern_change_with_reused_revision_is_detected(self):
        source = self.load("game-ui.source.json")
        states = corpus.validate_translation(self.load("game-ui.fr-FR.json"), corpus.validate_source(source))
        self.assertEqual(set(states.values()), {"approved"})
        source["messages"][3]["pattern"] = source["messages"][3]["pattern"].replace("items", "things")
        states = corpus.validate_translation(self.load("game-ui.fr-FR.json"), corpus.validate_source(source))
        self.assertEqual(states["inventory/item-count"], "stale")

    def test_receipts_ignore_release_policy_but_cover_argument_schemas(self):
        source = self.load("game-ui.source.json")
        message = source["messages"][3]
        digest = corpus.source_digest(source, message)
        changed = copy.deepcopy(message)
        changed["required"] = False
        self.assertEqual(digest, corpus.source_digest(source, changed))
        changed["arguments"][0]["max"] = 5
        self.assertNotEqual(digest, corpus.source_digest(source, changed))

    def test_missing_required_translation_needs_explicit_permission(self):
        translation = self.load("game-ui.ru-RU.json")
        translation["messages"] = [m for m in translation["messages"] if m["key"] != "hud/score"]
        self.save("game-ui.ru-RU.json", translation)
        with self.assertRaisesRegex(CookError, "ru-RU hud/score: required message lacks"):
            self.release()
        manifest = self.load("release.json")
        manifest["domains"][0]["allow_required_fallback"] = ["ru-RU"]
        self.save("release.json", manifest)
        self.assertEqual(self.release()["domains"][0]["resolved"][("ru-RU", "hud/score")][1], "en-US")

    def test_optional_messages_fall_back_and_unapproved_records_do_not_ship(self):
        translation = self.load("game-ui.fr-FR.json")
        for message in translation["messages"]:
            if message["key"] == "menu/subtitle":
                message["status"] = "reviewed"
        self.save("game-ui.fr-FR.json", translation)
        resolved = self.release()["domains"][0]["resolved"]
        self.assertEqual(resolved[("fr-FR", "menu/subtitle")], ("Definitive Edition", "en-US"))

    def test_fallback_graph_must_be_acyclic_bounded_and_rooted(self):
        cases = (
            ([{"locale": "en-US", "parent": None}, {"locale": "fr-FR", "parent": "fr-CA"},
              {"locale": "fr-CA", "parent": "fr-FR"}], "cycle"),
            ([{"locale": "en-US", "parent": None}, {"locale": "fr-FR", "parent": "de-DE"}], "undeclared"),
            ([{"locale": "en-US", "parent": "fr-FR"}, {"locale": "fr-FR", "parent": None}], "source locale"),
        )
        for locales, message in cases:
            with self.subTest(message=message), self.assertRaisesRegex(CookError, message):
                corpus.fallback_paths(locales, "en-US")
        chain = [{"locale": "en-US", "parent": None}]
        for i in range(8):
            chain.append({"locale": "x" + "abcdefgh"[i] + "-AA", "parent": chain[-1]["locale"]})
        with self.assertRaisesRegex(CookError, "exceeds 8"):
            corpus.fallback_paths(chain, "en-US")

    def test_argument_schema_rejections(self):
        cases = (
            ({"name": "n", "type": "count", "min": 0.0, "max": 3}, "integer"),
            ({"name": "n", "type": "count", "min": True, "max": 3}, "integer"),
            ({"name": "n", "type": "count", "min": 0, "max": 2_000_000_000}, "integer"),
            ({"name": "n", "type": "count", "min": 4, "max": 3}, "below min"),
            ({"name": "g", "type": "enum", "values": ["other"], "unknown": "other"}, "other than other"),
            ({"name": "g", "type": "enum", "values": ["a", "a"], "unknown": "other"}, "duplicate"),
            ({"name": "g", "type": "enum", "values": ["a"], "unknown": "maybe"}, "unknown policy"),
            ({"name": "t", "type": "text", "origin": "player"}, "origin"),
            ({"name": "t", "type": "text", "origin": "user", "max_bytes": 4097}, "max_bytes"),
            ({"name": "Bad", "type": "text", "origin": "user"}, "argument name"),
            ({"name": "d", "type": "date"}, "unsupported argument type"),
            ({"name": "t", "type": "text", "origin": "user", "style": "x"}, "unknown"),
        )
        for value, message in cases:
            with self.subTest(value=value), self.assertRaisesRegex(CookError, message):
                corpus.argument_schema(value, "argument")

    def test_document_rejections(self):
        source = self.load("game-ui.source.json")
        broken = copy.deepcopy(source)
        broken["messages"].append(copy.deepcopy(broken["messages"][0]))
        with self.assertRaisesRegex(CookError, "duplicate message key"):
            corpus.validate_source(broken)
        broken = copy.deepcopy(source)
        broken["profile"] = "ludus-static-utf8-v1"
        with self.assertRaisesRegex(CookError, "profile"):
            corpus.validate_source(broken)
        broken = copy.deepcopy(source)
        broken["messages"][0]["context"] = "  "
        with self.assertRaisesRegex(CookError, "context"):
            corpus.validate_source(broken)
        translation = self.load("game-ui.ar.json")
        translation["messages"][0]["status"] = "stale"
        with self.assertRaisesRegex(CookError, "status"):
            corpus.validate_translation(translation, source)
        translation = self.load("game-ui.ar.json")
        translation["messages"][0]["key"] = "menu/unknown"
        with self.assertRaisesRegex(CookError, "unknown translated key"):
            corpus.validate_translation(translation, source)

    def test_fixture_rejections(self):
        release = self.release()
        expectations = self.load("expectations.json")
        expectations["cases"][0]["status"] = "InvalidArguments"
        with self.assertRaisesRegex(CookError, "exactly one"):
            corpus.validate_expectations(expectations, release)
        rejections = self.load("profile-rejections.json")
        rejections["cases"][0]["reason"] = "Unknown"
        with self.assertRaisesRegex(CookError, "unknown reason"):
            corpus.validate_rejections(rejections, release)

    def test_literals_are_counted_and_escape_safely(self):
        self.assertEqual(corpus.literal('a"\\?1'), 'Bytes{"a\\042\\134\\0771", 5}')
        self.assertEqual(corpus.literal("é"), 'Bytes{"\\303\\251", 2}')
        self.assertEqual(corpus.literal(""), 'Bytes{"", 0}')


if __name__ == "__main__":
    unittest.main()
