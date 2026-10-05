"""Creation engine precedence, freshness and cancellation boundaries."""
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from ludus_tools import creation_engine as engines
from ludus_tools.creation_engine import select_creation_sdk, prepare_creation_sdk
from ludus_tools.errors import ToolingError
from test_ludus_tools import _manifest_json, _write_sdk_prefix


class CreationEngineTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "CMakeLists.txt").write_text("project(Ludus VERSION 0.1.0 LANGUAGES CXX)\n")
        self.prefix = self.root / "out/install/linux-clang-development"
        self.calls = []
        revision = patch("ludus_tools.creation_engine.source_revision", return_value="current")
        revision.start()
        self.addCleanup(revision.stop)

    def select(self, **kwargs):
        return select_creation_sdk(self.root, environ={}, **kwargs)

    def prepare(self, prefix):
        self.calls.append(prefix)
        _write_sdk_prefix(prefix, _manifest_json(source_revision=engines.source_revision(self.root)))

    def test_editor_install_is_reused_only_at_current_source_revision(self):
        _write_sdk_prefix(self.prefix, _manifest_json(source_revision="current"))
        with patch("ludus_tools.creation_engine.source_revision", return_value="current"):
            self.assertEqual(self.select(prepare=self.prepare), self.prefix)
            self.assertEqual(self.calls, [])
        with patch("ludus_tools.creation_engine.source_revision", return_value="next"):
            self.assertEqual(self.select(prepare=self.prepare), self.prefix)
            self.assertEqual(self.calls, [self.prefix])

    def test_missing_or_invalid_editor_install_is_prepared(self):
        for manifest in (None, _manifest_json(build_flavor="Release")):
            if manifest:
                _write_sdk_prefix(self.prefix, manifest)
            self.assertEqual(self.select(prepare=self.prepare), self.prefix)
        self.assertEqual(self.calls, [self.prefix, self.prefix])

    def test_explicit_selection_precedes_environment_and_does_not_prepare(self):
        custom = _write_sdk_prefix(self.root / "custom", _manifest_json())
        self.assertEqual(select_creation_sdk(self.root, explicit=custom, prepare=self.prepare,
                                            environ={"LUDUS_SDK_PREFIX": "/missing"}), custom)
        self.assertEqual(select_creation_sdk(self.root, prepare=self.prepare,
                                            environ={"LUDUS_SDK_PREFIX": str(custom)}), custom)
        self.assertEqual(self.calls, [])

    def test_invalid_overrides_never_fall_back_or_create_files(self):
        for value in ("relative", str(self.root / "missing")):
            with self.assertRaises(ToolingError):
                select_creation_sdk(self.root, prepare=self.prepare,
                                    environ={"LUDUS_SDK_PREFIX": value})
        self.assertEqual(self.calls, [])
        self.assertFalse(self.prefix.exists())

    def test_preparation_cannot_publish_a_different_engine_identity(self):
        def wrong(prefix):
            _write_sdk_prefix(prefix, _manifest_json(version="0.0.1"))
        with self.assertRaisesRegex(ToolingError, "does not match"):
            self.select(prepare=wrong)

    def test_discovery_without_preparation_is_read_only(self):
        with self.assertRaises(ToolingError):
            self.select()
        self.assertEqual(list(self.root.iterdir()), [self.root / "CMakeLists.txt"])

    def test_preparation_preserves_legacy_sdk_and_reuses_separate_install(self):
        legacy = _manifest_json(sdk_variant="assert-v1", source_revision="old")
        _write_sdk_prefix(self.prefix, legacy)
        manifest = self.prefix / "share/Ludus/LudusSdkManifest.json"
        original = manifest.read_bytes()
        current = _manifest_json(source_revision="current")

        def runner(argv, **kwargs):
            self.calls.append(argv)
            if Path(argv[0]).name == "build":
                generated = self.root / "out/build" / engines.PROFILE / "cmake/LudusSdkManifest.json"
                generated.parent.mkdir(parents=True)
                generated.write_text(json.dumps(current))
            if "--install" in argv:
                destination = Path(argv[argv.index("--prefix") + 1])
                self.assertNotEqual(destination, self.prefix)
                _write_sdk_prefix(destination, current)

        selected = self.select(prepare=lambda prefix: prepare_creation_sdk(self.root, prefix, runner=runner))
        self.assertEqual(selected, engines.prepared_sdk_prefix(self.root))
        self.assertEqual(manifest.read_bytes(), original)
        self.assertEqual(len(self.calls), 3)
        self.calls.clear()
        self.assertEqual(self.select(prepare=self.prepare), selected)
        self.assertEqual(self.calls, [])

    def test_cancel_after_build_prevents_install(self):
        def runner(argv, **kwargs):
            self.calls.append(argv)
        def cancel():
            if len(self.calls) == 2:
                raise ToolingError("Cancelled", "cancelled")
        with self.assertRaisesRegex(ToolingError, "cancelled"):
            prepare_creation_sdk(self.root, self.prefix, runner=runner, cancel_check=cancel)
        self.assertEqual(len(self.calls), 2)
        self.assertFalse(self.prefix.exists())

    def test_cancelled_preparation_stops_before_next_command(self):
        def runner(argv, **kwargs):
            self.calls.append(argv)
        def cancel():
            if self.calls:
                raise ToolingError("Cancelled", "cancelled")
        with self.assertRaisesRegex(ToolingError, "cancelled"):
            prepare_creation_sdk(self.root, self.prefix, runner=runner, cancel_check=cancel)
        self.assertEqual(len(self.calls), 1)
        self.assertIn("--no-system-install", self.calls[0])
        self.assertFalse(self.prefix.exists())


if __name__ == "__main__":
    unittest.main()
