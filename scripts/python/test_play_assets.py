import json
from pathlib import Path
import stat
import struct
import tempfile
import unittest

from play_assets import artifact_digest, cook_frame_clear, publish_frame_clear
from play_documents import DocumentError


class AssetTests(unittest.TestCase):
    def test_bounded_versioned_configuration_and_invalid_imports(self):
        cooked = cook_frame_clear(b'{"version":1,"kind":"frame_clear","rgba":[0.2,0.4,0.6,1]}')
        self.assertEqual(cooked[:8], b"LCLR\x01\x00\x00\x00")
        self.assertEqual(len(cooked), 24)
        self.assertAlmostEqual(struct.unpack_from("<f", cooked, 8)[0], 0.2)
        for obj in [dict(version=2, kind="frame_clear", rgba=[0,0,0,1]),
                    dict(version=1, kind="texture", rgba=[0,0,0,1]),
                    dict(version=1, kind="frame_clear", rgba=[True,0,0,1]),
                    dict(version=1, kind="frame_clear", rgba=[0,0,float("nan"),1]),
                    dict(version=1, kind="frame_clear", rgba=[0,0,2,1])]:
            with self.assertRaises(DocumentError):
                cook_frame_clear(json.dumps(obj).encode())

    def test_publication_is_immutable_and_source_digest_is_distinct(self):
        with tempfile.TemporaryDirectory() as td:
            project = Path(td)
            source = project / "clear.json"
            source.write_text('{"version":1,"kind":"frame_clear","rgba":[0,0,0,1]}')
            first = publish_frame_clear(project, "clear.json")
            source.write_text('{ "version":1,"kind":"frame_clear","rgba":[0,0,0,1]}')
            second = publish_frame_clear(project, "clear.json")
            self.assertNotEqual(first["source_sha256"], second["source_sha256"])
            self.assertEqual(first["cooked_sha256"], second["cooked_sha256"])
            artifact = project / ".ludus/assets/frame-clear" / (first["cooked_sha256"]+".bin")
            self.assertEqual(artifact_digest(artifact.read_bytes()), first["digest"])
            self.assertEqual(stat.S_IMODE(artifact.stat().st_mode), 0o444)
            source.write_text('{"version":1,"kind":"texture","rgba":[0,0,0,1]}')
            with self.assertRaises(DocumentError):
                publish_frame_clear(project, "clear.json")
            self.assertTrue(artifact.is_file())
            with self.assertRaises(DocumentError):
                publish_frame_clear(project, "../clear.json")


if __name__ == "__main__":
    unittest.main()
