import importlib.machinery
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "examples/audio-content"))
from generate_fixtures import generate
loader = importlib.machinery.SourceFileLoader("package_audio", str(ROOT / "scripts/package-audio-content"))
spec = importlib.util.spec_from_loader(loader.name, loader)
pack = importlib.util.module_from_spec(spec)
loader.exec_module(pack)
VALIDATOR = sys.argv.pop(1)


class PackageTests(unittest.TestCase):
    def test_closure_reproducibility_and_last_good(self):
        with tempfile.TemporaryDirectory() as temporary:
            content = Path(temporary) / "content"
            output = Path(temporary) / "release"
            generate(content)
            (content / "unused.wav").write_bytes(b"unused asset")
            roots = ["sound/impact", "music/theme"]
            first = pack.package(VALIDATOR, content, output, roots)
            pointer = (output / "current.json").read_bytes()
            self.assertFalse((first / "unused.wav").exists())
            self.assertEqual(first, pack.package(VALIDATOR, content, output, list(reversed(roots))))
            subprocess.run([str(Path(VALIDATOR).parent.parent / "audio_content_demo/ludus_audio_content_demo"), "--offline", str(first)], check=True)
            (content / "theme.wav").unlink()
            with self.assertRaises(subprocess.CalledProcessError):
                pack.package(VALIDATOR, content, output, roots)
            self.assertEqual(pointer, (output / "current.json").read_bytes())
            self.assertTrue((first / "theme.wav").exists())

    def test_source_edit_and_interrupted_publish_preserve_last_good(self):
        with tempfile.TemporaryDirectory() as temporary:
            content = Path(temporary) / "content"
            output = Path(temporary) / "release"
            generate(content)
            roots = ["music/theme"]
            first = pack.package(VALIDATOR, content, output, roots)
            pointer = (output / "current.json").read_bytes()
            original = pack.fingerprint
            def changed(root_fd, path, destination=None):
                if destination and path == "theme.wav":
                    with open(content / path, "ab") as target:
                        target.write(b"changed during packaging")
                return original(root_fd, path, destination)
            with patch.object(pack, "fingerprint", changed):
                with self.assertRaises(ValueError):
                    pack.package(VALIDATOR, content, output, roots)
            self.assertEqual(pointer, (output / "current.json").read_bytes())
            generate(content)
            with patch.object(pack.os, "replace", side_effect=OSError("interrupted publication")):
                with self.assertRaises(OSError):
                    pack.package(VALIDATOR, content, output, roots)
            self.assertEqual(pointer, (output / "current.json").read_bytes())
            self.assertTrue((first / "theme.wav").exists())

    def test_symlink_and_unsupported_definition_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            content = Path(temporary) / "content"
            generate(content)
            (content / "theme.wav").unlink()
            (content / "theme.wav").symlink_to("/etc/passwd")
            with self.assertRaises(subprocess.CalledProcessError):
                pack.package(VALIDATOR, content, Path(temporary) / "release", ["music/theme"])
            document = json.loads((content / "impact.json").read_text())
            document["version"] = 2
            (content / "impact.json").write_text(json.dumps(document))
            with self.assertRaises(subprocess.CalledProcessError):
                pack.package(VALIDATOR, content, Path(temporary) / "release", ["sound/impact"])


if __name__ == "__main__":
    unittest.main()
