"""Regressions for one-source publication and portable offline distribution."""
import importlib.machinery
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

from documentation import digest, group, index_text

SCRIPTS = Path(__file__).resolve().parents[1]


def load_script(name):
    loader = importlib.machinery.SourceFileLoader(name.replace("-", "_"), str(SCRIPTS / name))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


CHECK = load_script("check-wiki")
CHECK_DOCS = load_script("check-docs")


class SourceParityTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.site = self.root / "out/wiki"
        self.site.mkdir(parents=True)
        (self.root / "docs").mkdir()
        self.source = self.root / "docs/example.md"
        self.source.write_text("# Example\n\nCanonical contract.\n")
        self.output = self.site / "example.html"
        self.output.write_text("<h1>Example</h1><p>Canonical contract.</p>")
        self.entry = {"source": "docs/example.md", "output": "example.html",
                      "source_sha256": digest(self.source), "output_sha256": digest(self.output)}
        self.manifest([self.entry])

    def manifest(self, entries):
        (self.site / "documentation-sources.json").write_text(json.dumps({"version": 1, "pages": entries}))

    def test_source_edits_and_output_edits_require_a_rebuild(self):
        self.assertEqual(CHECK.check_sources(self.root, self.site), [])
        self.source.write_text("# Changed contract\n")
        self.assertIn("Stale documentation source", " ".join(CHECK.check_sources(self.root, self.site)))
        self.output.write_text("altered")
        self.assertIn("altered documentation output", " ".join(CHECK.check_sources(self.root, self.site)))

    def test_new_removed_duplicate_and_escaping_pages_are_rejected(self):
        (self.root / "docs/new.md").write_text("# New page\n")
        self.assertIn("Documentation absent from wiki: docs/new.md", CHECK.check_sources(self.root, self.site))
        self.manifest([self.entry, self.entry])
        self.assertIn("Duplicate", " ".join(CHECK.check_sources(self.root, self.site)))
        self.entry["output"] = "../../outside.html"
        self.manifest([self.entry])
        self.assertIn("Invalid documentation source/output mapping", " ".join(CHECK.check_sources(self.root, self.site)))

    def test_missing_manifest_is_a_failure(self):
        (self.site / "documentation-sources.json").unlink()
        self.assertIn("missing documentation source manifest", " ".join(CHECK.check_sources(self.root, self.site)))

    def test_index_tracks_new_documents_without_copying_their_prose(self):
        self.assertIn("[Example](example.md)", index_text(self.root))
        self.assertNotIn("Canonical contract", index_text(self.root))
        self.assertTrue(CHECK_DOCS.check(self.root))
        self.assertEqual(CHECK_DOCS.check(self.root, fix=True), [])
        self.assertEqual(CHECK_DOCS.check(self.root), [])
        self.source.write_text("# Renamed page\n")
        self.assertIn("Stale docs/README.md", " ".join(CHECK_DOCS.check(self.root)))

    def test_local_reference_links_and_anchors_are_checked_but_code_is_not(self):
        self.source.write_text("# Example\n\n[Contract][local]\n\n[local]: example.md#missing\n"
                               "\n```sh\n[Not a link](missing.md)\n```\n")
        failures = CHECK_DOCS.check(self.root, fix=True)
        self.assertIn("missing local Markdown anchor", " ".join(failures))
        self.assertNotIn("missing.md", " ".join(failures))
        self.source.write_text("# Example\n\n[Local][doc]\n\n[doc]: https://github.com/Alegruz/Ludus/blob/main/docs/example.md\n")
        self.assertIn("use relative links", " ".join(CHECK_DOCS.check(self.root, fix=True)))
        self.source.write_text("# Example\n\n[Guide](https://github.com/Alegruz/Ludus/blob/main/modules/demo/README.md)\n")
        self.assertIn("use relative links", " ".join(CHECK_DOCS.check(self.root, fix=True)))

    def test_setup_examples_use_the_real_parser_without_running_setup(self):
        valid = "```sh\n./init.sh --cli linux-clang-development --preset-only --locked --with-tests\n```\n"
        self.assertEqual(CHECK_DOCS.check_init_examples(valid, "fixture", {"linux-clang-development"}), [])
        invalid = valid.replace("--cli linux", "--cli --preset linux")
        self.assertIn("invalid init.sh example", " ".join(CHECK_DOCS.check_init_examples(invalid, "fixture", set())))
        self.assertIn("unknown setup preset", " ".join(CHECK_DOCS.check_init_examples(valid, "fixture", {"another-preset"})))
        self.assertFalse((self.root / "out/host-tools").exists())

    def test_readmes_link_to_the_canonical_owner_and_roles_distinguish_baselines(self):
        readme = self.root / "modules/demo/README.md"
        readme.parent.mkdir(parents=True)
        readme.write_text("# Demo\n\nSeparate setup instructions.\n")
        self.assertIn("short link to its owner", " ".join(CHECK_DOCS.check(self.root, fix=True)))
        readme.write_text("# Demo\n\nRead the canonical documentation.\n")
        self.assertIn("link to a Markdown owner under docs/", " ".join(CHECK_DOCS.check(self.root)))
        (self.root / "modules/other.md").write_text("# Separate owner\n")
        readme.write_text("# Demo\n\nRead [the canonical documentation](../other.md).\n")
        self.assertIn("link to a Markdown owner under docs/", " ".join(CHECK_DOCS.check(self.root)))
        readme.write_text("# Demo\n\nRead [the canonical documentation](../../docs/example.md).\n")
        self.assertEqual(CHECK_DOCS.check(self.root), [])
        readme.write_text(readme.read_text().replace("example.md", "absent.md"))
        self.assertIn("missing canonical documentation", " ".join(CHECK_DOCS.check(self.root)))
        self.assertEqual(group(Path("architecture/profiling.md")), "Historical baselines")
        self.assertEqual(group(Path("modules/foundation/profiling.md")), "Module usage")


class DocumentationBuildTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.root = Path(cls.temporary.name)
        files = {
            "docs/wiki/index.md": (
                "# Welcome\n\n[Design](../architecture/design.md#contract)\n"
                "![Diagram](../architecture/images/diagram.svg)\n"
                "[Code](../../modules/demo.hpp)\n"
                "[Local book][book]\n\n[book]: ../../references/Private%20Book.pdf#page=2\n"
                "\n```text\n[Example](../../missing-example.hpp)\n```\n"),
            "docs/architecture/design.md": "# Design\n\n## Contract\n\nCanonical design text.\n\n[Guide](../wiki/index.md)\n",
            "docs/architecture/images/diagram.svg": '<svg xmlns="http://www.w3.org/2000/svg"></svg>',
            "docs/development/building.md": "# Building Ludus\n",
            "docs/development/wiki.md": "# Documentation tooling\n\n## Offline reading\n",
            "docs/wiki/contribute/wiki.md": "# Earlier entry\n\n[Canonical guide](../../development/building.md)\n",
            "docs/wiki/editor/index.html": '<script src="missing-editor.js"></script>',
            "modules/demo.hpp": "// Public sample\n",
            "references/Private Book.pdf": "private reference",
            "docs/api-main.md": "Doxygen input\n",
        }
        for relative, content in files.items():
            path = cls.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
        (cls.root / "docs/README.md").write_text(index_text(cls.root))
        (cls.root / "mkdocs.yml").write_text(
            "site_name: Test\nsite_url: https://example.test/Ludus/\n"
            "repo_url: https://github.com/Alegruz/Ludus\nedit_uri: edit/main/docs/\n"
            "docs_dir: docs\nsite_dir: out/wiki\nexclude_docs: /api-main.md\n"
            "theme:\n  name: material\n  font: false\n  features:\n    - content.action.edit\nplugins:\n  - search\n"
            "markdown_extensions:\n  - fenced_code\n  - toc\n"
            "validation:\n  nav:\n    omitted_files: warn\n  links:\n    anchors: warn\n"
            f"hooks:\n  - {SCRIPTS / 'python/wiki_docs_hook.py'}\nnav:\n  - Home: index.md\n  - Build: development/building.md\n")
        result = subprocess.run([sys.executable, "-m", "mkdocs", "build", "--strict"],
                                cwd=cls.root, capture_output=True, text=True, check=False)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        for name in ("check-docs", "check-wiki", "package-docs", "python/documentation.py"):
            target = cls.root / "scripts" / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(SCRIPTS / name, target)

    def command(self, script, *args):
        return subprocess.run([sys.executable, str(self.root / "scripts" / script),
                               "--root", str(self.root), *args], capture_output=True, text=True, check=False)

    def test_real_build_uses_original_content_links_assets_and_edit_paths(self):
        home = (self.root / "out/wiki/index.html").read_text()
        design = (self.root / "out/wiki/engineering/architecture/design/index.html").read_text()
        self.assertIn('href="engineering/architecture/design/#contract"', home)
        self.assertIn('src="engineering/architecture/images/diagram.svg"', home)
        self.assertIn("Canonical design text.", design)
        self.assertIn("edit/main/docs/architecture/design.md", design)
        self.assertIn('href="engineering/development/building/"', home)
        self.assertTrue((self.root / "out/wiki/contribute/wiki/index.html").is_file())
        self.assertIn("blob/main/modules/demo.hpp", home)
        self.assertIn("Local reference copy (not distributed)", home)
        self.assertIn("../../missing-example.hpp", home)
        self.assertFalse((self.root / "out/wiki/engineering/api-main/index.html").exists())
        self.assertEqual(CHECK.check_sources(self.root, self.root / "out/wiki"), [])
        result = self.command("check-wiki", "--documentation-only")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotEqual(self.command("check-wiki").returncode, 0)

    def test_offline_archive_preserves_sources_and_pages_and_omits_private_material(self):
        result = self.command("package-docs")
        self.assertEqual(result.returncode, 0, result.stderr)
        with zipfile.ZipFile(self.root / "out/ludus-docs-offline.zip") as archive:
            self.assertEqual(archive.read("docs/architecture/design.md"),
                             (self.root / "docs/architecture/design.md").read_bytes())
            self.assertEqual(archive.read("site/engineering/architecture/design/index.html"),
                             (self.root / "out/wiki/engineering/architecture/design/index.html").read_bytes())
            self.assertNotIn("docs/api-main.md", archive.namelist())
            self.assertFalse(any("Private Book" in name for name in archive.namelist()))
            self.assertIn("internet required", archive.read("site/editor/index.html").decode())

    def test_stale_sources_do_not_replace_a_previous_bundle(self):
        output = self.root / "out/preserved.zip"
        output.write_bytes(b"previous good bundle")
        source = self.root / "docs/architecture/design.md"
        previous = source.read_bytes()
        try:
            source.write_bytes(previous + b"\nChanged contract.\n")
            result = self.command("package-docs", "--output", str(output))
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(output.read_bytes(), b"previous good bundle")
        finally:
            source.write_bytes(previous)


if __name__ == "__main__":
    unittest.main()
