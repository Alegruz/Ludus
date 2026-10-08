"""Artifact regressions: a successful Markdown build is not the whole deployment."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

CHECK = Path(__file__).resolve().parents[1] / "check-wiki"


class WikiArtifactTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.site = self.root / "out/wiki"
        (self.site / "guide").mkdir(parents=True)
        (self.site / "search").mkdir()
        (self.site / "assets").mkdir()
        (self.root / "mkdocs.yml").write_text(
            "site_url: https://example.test/Ludus/\nrepo_url: https://github.com/Alegruz/Ludus\nsite_dir: out/wiki\n")
        (self.root / "source.md").write_text("source")
        (self.site / "assets/style.css").write_text("body {}")
        (self.site / "index.html").write_text(
            '<link href="assets/style.css"><a href="guide/#next">Guide</a>'
            '<a href="https://github.com/Alegruz/Ludus/blob/main/source.md">Source</a>')
        (self.site / "guide/index.html").write_text('<h1 id="next">Guide</h1><a href="../">Home</a>')
        self.search("guide/#next")

    def search(self, location):
        (self.site / "search/search_index.json").write_text(json.dumps({"docs": [
            {"location": ""}, {"location": location}]}))

    def api(self):
        api = self.site / "api"
        api.mkdir()
        reference = self.site / "reference"
        reference.mkdir()
        (reference / "index.html").write_text('<a href="../api/index.html">C++ API</a>')
        search = self.site / "search/search_index.json"
        data = json.loads(search.read_text())
        data["docs"].extend({"location": uri} for uri in ("reference/", "api/index.html", "api/symbol.html#method"))
        search.write_text(json.dumps(data))
        navigation = ('<a href="../index.html">Ludus Wiki</a>'
                      '<a href="../reference/index.html">API overview</a>')
        (api / "index.html").write_text(navigation + '<a href="symbol.html#method">Method</a>')
        (api / "symbol.html").write_text(navigation + '<h1 id="method">Method</h1>')
        return api, reference, navigation

    def run_check(self):
        return subprocess.run([sys.executable, str(CHECK), "--root", str(self.root)],
                              capture_output=True, text=True, check=False)

    def test_project_path_assets_anchors_and_source_links(self):
        self.assertEqual(self.run_check().returncode, 0)

    def test_missing_asset_and_source_are_rejected(self):
        (self.site / "assets/style.css").unlink()
        (self.root / "source.md").unlink()
        result = self.run_check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing local destination", result.stderr)
        self.assertIn("missing repository source", result.stderr)

    def test_broken_search_anchor_is_rejected(self):
        self.search("guide/#gone")
        result = self.run_check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing anchor", result.stderr)

    def test_api_links_and_shared_search_are_checked(self):
        api, _, navigation = self.api()
        self.assertEqual(self.run_check().returncode, 0)
        (api / "symbol.html").write_text(navigation + '<h1 id="other">Other</h1>')
        self.assertIn("missing anchor", self.run_check().stderr)

    def test_api_pages_must_appear_in_shared_search(self):
        self.api()
        search = self.site / "search/search_index.json"
        data = json.loads(search.read_text())
        data["docs"] = [item for item in data["docs"] if item["location"] != "api/symbol.html#method"]
        search.write_text(json.dumps(data))
        self.assertIn("Page missing from search index: api/symbol.html", self.run_check().stderr)

    def test_every_api_page_requires_wiki_and_overview_return_links(self):
        api, _, _ = self.api()
        (api / "symbol.html").write_text('<h1 id="method">Method</h1>')
        result = self.run_check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("api/symbol.html: missing API return link to index.html", result.stderr)
        self.assertIn("api/symbol.html: missing API return link to reference/index.html", result.stderr)

    def test_overview_requires_generated_api_and_api_output_cannot_be_omitted(self):
        api, reference, _ = self.api()
        (api / "index.html").unlink()
        self.assertIn("missing local destination ../api/index.html", self.run_check().stderr)
        (reference / "index.html").write_text('<h1>API overview</h1>')
        self.assertIn("missing link to generated API reference", self.run_check().stderr)

    def test_unindexed_page_and_invalid_destination_are_rejected(self):
        self.search(".")
        result = self.run_check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Page missing from search index: guide/index.html", result.stderr)
        self.search(None)
        result = self.run_check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Invalid search destination", result.stderr)

    def test_editor_application_assets_are_checked_without_search_indexing(self):
        editor = self.site / "editor"
        editor.mkdir()
        (editor / "index.html").write_text('<script src="editor.js"></script>')
        (editor / "editor.js").write_text("app")
        self.assertEqual(self.run_check().returncode, 0)
        (editor / "editor.js").unlink()
        self.assertIn("missing local destination", self.run_check().stderr)
        (editor / "other.html").write_text("other")
        self.assertIn("Page missing from search index: editor/other.html", self.run_check().stderr)

    def test_site_root_escape_is_rejected(self):
        (self.site / "guide/index.html").write_text('<a href="../../">Wrong root</a>')
        self.assertNotEqual(self.run_check().returncode, 0)

    def test_shipped_players_check_assets_without_search_indexing(self):
        player = self.site / "editor/players/cornell-box"
        player.mkdir(parents=True)
        (player / "index.html").write_text('<script src="player.js"></script>')
        self.assertIn("missing local destination player.js", self.run_check().stderr)
        (player / "player.js").write_text("app")
        self.assertEqual(self.run_check().returncode, 0)
        unexpected = self.site / "editor/players/unknown"
        unexpected.mkdir()
        (unexpected / "index.html").write_text("unexpected")
        self.assertIn("Page missing from search index: editor/players/unknown/index.html", self.run_check().stderr)

    def test_empty_search_and_symlink_are_rejected(self):
        (self.site / "search/search_index.json").write_text('{"docs":[]}')
        (self.site / "leak").symlink_to(self.root / "source.md")
        result = self.run_check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Symlink", result.stderr)
        self.assertIn("Empty", result.stderr)


if __name__ == "__main__":
    unittest.main()
