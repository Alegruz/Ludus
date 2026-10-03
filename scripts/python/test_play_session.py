"""Tests for build-generation publication, leasing and the File API resolver
extension (project-live-reload L2, design 5)."""

import os
import sys
import tempfile
import unittest
import json
import subprocess
from unittest import mock
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import cmake_targets  # noqa: E402
import play_session  # noqa: E402


class _Engine:
    class EngineError(Exception):
        pass


class GenerationPublishTests(unittest.TestCase):
    def test_lease_is_reference_counted(self):
        leases = play_session.LeaseSet()
        leases.acquire("generation")
        leases.acquire("generation")
        leases.release("generation")
        self.assertTrue(leases.is_leased("generation"))
        leases.release("generation")
        self.assertFalse(leases.is_leased("generation"))

    def test_manifest_fields_are_typed_before_publication(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            module, host, _ = self._artifacts(root)
            for key, value in (("abi_major", True), ("abi_minor", "0"),
                               ("sdk_identity", 123), ("project_id", 1)):
                fields = self._fields()
                fields[key] = value
                with self.assertRaises(play_session.PublishError, msg=key):
                    play_session.publish_generation(
                        generations_root=root / "generations", module_artifact=module,
                        host_artifact=host, symbol_artifact=None, manifest_fields=fields,
                        declared_source_inputs=[module])

    def test_missing_declared_input_cannot_publish(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            module, host, _ = self._artifacts(root)
            with self.assertRaisesRegex(play_session.PublishError, "source input missing"):
                play_session.publish_generation(
                    generations_root=root / "generations", module_artifact=module,
                    host_artifact=host, symbol_artifact=None, manifest_fields=self._fields(),
                    declared_source_inputs=[root / "gone.cpp"])

    def test_clock_rollback_does_not_reorder_ids(self):
        with mock.patch.object(play_session.time, "time", return_value=2000):
            first = play_session.next_generation_id()
        with mock.patch.object(play_session.time, "time", return_value=1000):
            second = play_session.next_generation_id()
        self.assertLess(first, second)

    def test_change_during_copy_cannot_publish(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            module, host, _ = self._artifacts(root)
            source = root / "game.cpp"
            source.write_text("before")
            before = play_session.source_input_digest([source])
            real_copy = play_session.shutil.copy2
            def edit_during_copy(src, dest):
                result = real_copy(src, dest)
                source.write_text("after")
                return result
            with mock.patch.object(play_session.shutil, "copy2", side_effect=edit_during_copy):
                with self.assertRaisesRegex(play_session.PublishError, "Superseded"):
                    play_session.publish_generation(
                        generations_root=root / "generations", module_artifact=module,
                        host_artifact=host, symbol_artifact=None, manifest_fields=self._fields(),
                        declared_source_inputs=[source], pre_build_source_digest=before,
                    )
            self.assertFalse(any(p.is_dir() for p in (root / "generations").iterdir()))

    def test_manifest_rejects_unknown_schema_and_escaped_payload(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            module, host, _ = self._artifacts(root)
            gen = play_session.publish_generation(
                generations_root=root / "generations", module_artifact=module,
                host_artifact=host, symbol_artifact=None, manifest_fields=self._fields(),
                declared_source_inputs=[module],
            )
            path = gen / "manifest.json"
            original = play_session.read_manifest(gen)
            os.chmod(path, 0o644)
            for kind in ("schema", "escape", "missing", "symlink"):
                data = dict(original)
                data["files"] = dict(original["files"])
                if kind == "schema": data["schema"] = 999
                if kind == "escape":
                    data["files"]["../external"] = data["files"].pop(data["module_file"])
                    data["module_file"] = "../external"
                if kind == "missing": data["files"].pop(data["module_file"])
                if kind == "symlink":
                    (gen / data["module_file"]).unlink()
                    (gen / data["module_file"]).symlink_to(module)
                path.write_text(json.dumps(data))
                with self.assertRaises(play_session.PublishError, msg=kind):
                    play_session.read_manifest(gen)

    def test_other_process_lease_prevents_collection(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            module, host, _ = self._artifacts(root)
            gens = root / "generations"
            published = [play_session.publish_generation(
                generations_root=gens, module_artifact=module, host_artifact=host,
                symbol_artifact=None, manifest_fields=self._fields(),
                declared_source_inputs=[module]) for _ in range(5)]
            proc = subprocess.Popen(
                [sys.executable, "-u", "-c",
                 "from pathlib import Path; from play_session import GenerationLease; "
                 "import sys; lease=GenerationLease(Path(sys.argv[1])); "
                 "print('leased',flush=True); sys.stdin.readline(); lease.close()",
                 str(published[0])],
                cwd=Path(play_session.__file__).parent,
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
            try:
                self.assertEqual(proc.stdout.readline().strip(), "leased")
                deleted = play_session.collect_generations(gens, play_session.LeaseSet())
                self.assertNotIn(published[0].name, deleted)
                self.assertIn(published[1].name, deleted)
                proc.stdin.write("release\n")
                proc.stdin.flush()
                self.assertEqual(proc.wait(timeout=5), 0)
                self.assertIn(published[0].name,
                              play_session.collect_generations(gens, play_session.LeaseSet()))
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait()
                proc.stdin.close()
                proc.stdout.close()

    def _artifacts(self, root: Path):
        module = root / "sample_game.so"
        host = root / "sample_game_host"
        symbols = root / "sample_game.so.debug"
        module.write_bytes(b"MODULE-BYTES-A")
        host.write_bytes(b"HOST-BYTES")
        symbols.write_bytes(b"SYMBOLS")
        return module, host, symbols

    def _fields(self):
        return {
            "project_id": "0000000000000001",
            "game_id": "0000000000000002",
            "module_target": "sample_game_module",
            "host_target": "sample_game_host",
            "sdk_identity": "assert-v2-Debug|Clang 18|x-y|abi-1.0",
            "build_request_revision": "rev1",
            "abi_major": 1,
            "abi_minor": 0,
            "property_schema": 1,
            "checkpoint_schema": 1,
        }

    def test_publish_is_atomic_and_immutable(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            module, host, symbols = self._artifacts(root)
            gens = root / "generations"
            gen = play_session.publish_generation(
                generations_root=gens,
                module_artifact=module,
                host_artifact=host,
                symbol_artifact=symbols,
                manifest_fields=self._fields(),
                declared_source_inputs=[module],
            )
            self.assertTrue(gen.is_dir())
            self.assertTrue((gen / "manifest.json").is_file())
            self.assertTrue((gen / "sample_game.so").is_file())
            # Payload is read-only (an active/leased image is not overwritten).
            mode = (gen / "sample_game.so").stat().st_mode & 0o222
            self.assertEqual(mode, 0, "published payload must not be writable")
            # Manifest re-verification passes on a clean generation.
            data = play_session.read_manifest(gen)
            self.assertEqual(data["module_file"], "sample_game.so")
            self.assertEqual(data["abi_major"], 1)

    def test_superseded_source_change_is_not_published(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            module, host, _ = self._artifacts(root)
            gens = root / "generations"
            pre = play_session.source_input_digest([module])
            module.write_bytes(b"CHANGED-DURING-BUILD")
            with self.assertRaises(play_session.PublishError):
                play_session.publish_generation(
                    generations_root=gens,
                    module_artifact=module,
                    host_artifact=host,
                    symbol_artifact=None,
                    manifest_fields=self._fields(),
                    declared_source_inputs=[module],
                    pre_build_source_digest=pre,
                )
            # Nothing was published.
            self.assertFalse(gens.is_dir() and any(gens.iterdir()))

    def test_corrupt_generation_fails_verification(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            module, host, _ = self._artifacts(root)
            gens = root / "generations"
            gen = play_session.publish_generation(
                generations_root=gens,
                module_artifact=module,
                host_artifact=host,
                symbol_artifact=None,
                manifest_fields=self._fields(),
                declared_source_inputs=[module],
            )
            payload = gen / "sample_game.so"
            os.chmod(payload, 0o644)
            payload.write_bytes(b"TAMPERED")
            with self.assertRaises(play_session.PublishError):
                play_session.read_manifest(gen)

    def test_gc_keeps_active_and_leased_generations(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            module, host, _ = self._artifacts(root)
            gens = root / "generations"
            published = []
            for i in range(5):
                module.write_bytes(f"MODULE-{i}".encode())
                published.append(
                    play_session.publish_generation(
                        generations_root=gens,
                        module_artifact=module,
                        host_artifact=host,
                        symbol_artifact=None,
                        manifest_fields=self._fields(),
                        declared_source_inputs=[module],
                    ).name
                )
            leases = play_session.LeaseSet()
            # Lease the OLDEST generation (as if a debugger holds it).
            leases.acquire(published[0])
            deleted = play_session.collect_generations(gens, leases, retention=3)
            # The oldest is leased so it survives even though it is beyond retention.
            self.assertNotIn(published[0], deleted)
            self.assertTrue((gens / published[0]).is_dir())
            # The newest 3 are always retained.
            for keep in published[2:]:
                self.assertTrue((gens / keep).is_dir())


class ResolverArtifactTypeTests(unittest.TestCase):
    def _write_codemodel(self, build_dir: Path, target_name: str, target_type: str, artifact: str):
        api = build_dir / ".cmake" / "api" / "v1" / "reply"
        api.mkdir(parents=True, exist_ok=True)
        detail = f"target-{target_name}.json"
        (api / detail).write_text(
            '{"type":"%s","artifacts":[{"path":"%s"}]}' % (target_type, artifact), encoding="utf-8"
        )
        model = "codemodel-v2-1.json"
        (api / model).write_text(
            '{"version":{"major":2},"paths":{"source":"%s","build":"%s"},'
            '"configurations":[{"name":"RelWithDebInfo","targets":[{"name":"%s","jsonFile":"%s"}]}]}'
            % (build_dir, build_dir, target_name, detail),
            encoding="utf-8",
        )
        (api / "index-1.json").write_text(
            '{"reply":{"client-ludus-editor":{"codemodel-v2":{"jsonFile":"%s"}}}}' % model,
            encoding="utf-8",
        )

    def test_resolves_module_library_artifact(self):
        with tempfile.TemporaryDirectory() as td:
            build = Path(td)
            self._write_codemodel(build, "sample_game_module", "MODULE_LIBRARY", "sample_game.so")
            path = cmake_targets.target_artifact(
                build, "sample_game_module", _Engine, expected_type="MODULE_LIBRARY"
            )
            self.assertEqual(path, (build / "sample_game.so").resolve())

    def test_wrong_type_is_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            build = Path(td)
            self._write_codemodel(build, "sample_game_module", "MODULE_LIBRARY", "sample_game.so")
            with self.assertRaises(_Engine.EngineError):
                cmake_targets.target_artifact(
                    build, "sample_game_module", _Engine, expected_type="EXECUTABLE"
                )


if __name__ == "__main__":
    unittest.main()
