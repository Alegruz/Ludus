"""Release contracts and real ELF/CMake round trips; no live account uploads."""
from __future__ import annotations

import json
import os
import shutil
import stat
import subprocess
import tempfile
import unittest
import zipfile
from dataclasses import asdict
from pathlib import Path
from unittest.mock import patch

from ludus_tools import cli, create, release, release_model as model
from ludus_tools.errors import ToolingError
from ludus_tools.package_native import executable_identity, validate_native, POLICY
from ludus_tools.package_verify import extract_archive, file_digest, inventory, verify_package
from ludus_tools.release_template import release_files
from ludus_tools.sdkstore import SdkStore
from ludus_tools.identity import SdkIdentity
from test_ludus_tools import _manifest_json, _write_sdk_prefix


def config(target="game"):
    return json.loads(release_files(target, "tester/mygame")[0].content)


class ReleaseModelTests(unittest.TestCase):
    def load(self, obj):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "ludus.release.json").write_text(json.dumps(obj))
            return model.load_release(root)

    def test_profile_and_destination(self):
        parsed = self.load(config())
        self.assertEqual(parsed.profiles["linux-release"].entry_point, "bin/game")
        self.assertEqual(parsed.destinations["linux"], ("linux-release", "linux-stable"))

    def test_optional_itch_and_automation(self):
        obj = config()
        del obj["itch"]
        obj["automation"] = {"provider": "github-actions", "releaseTags": False}
        self.assertIsNone(self.load(obj).itch_target)

    def test_reject_invalid_configuration(self):
        mutations = [
            lambda o: o.update(schemaVersion=True),
            lambda o: o.update(schemaVersion=2),
            lambda o: o.update(unknown="x"),
            lambda o: o["profiles"]["linux-release"].update(entryPoint="../game"),
            lambda o: o["profiles"]["linux-release"].update(entryPoint="bin\\game"),
            lambda o: o["profiles"]["linux-release"].update(target="x; touch /tmp/pwn"),
            lambda o: o["profiles"]["linux-release"].update(buildProfile="debug"),
            lambda o: o["profiles"]["linux-release"].update(targetPlatform="wasm32"),
            lambda o: o["itch"].update(target="User/game"),
            lambda o: o["itch"]["channels"]["linux"].update(packageProfile="missing"),
            lambda o: o["itch"]["channels"].update(other={"packageProfile": "linux-release", "channel": "linux-stable"}),
        ]
        for mutate in mutations:
            with self.subTest(mutate=mutate):
                obj = config()
                mutate(obj)
                with self.assertRaises(ToolingError):
                    self.load(obj)

    def test_duplicate_fields_nonfinite_and_bounds(self):
        for data in (b'{"schemaVersion": 1, "schemaVersion": 1}', b'{"x": NaN}',
                     b"x" * (model.MAX_JSON_BYTES + 1), b"[" * 2000):
            with self.assertRaises(ToolingError):
                model.json_bytes(data, "fixture")

    def test_creation_opt_in_and_invalid_target_leave_nothing(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            create.create_project(root / "plain", name="Plain", template_id="minimal", engine_version="0.1.0")
            self.assertFalse((root / "plain/ludus.release.json").exists())
            create.create_project(root / "release", name="My Game", template_id="minimal", engine_version="0.1.0",
                                  release=True, itch_target="tester/mygame")
            parsed = model.load_release(root / "release")
            self.assertEqual(parsed.profiles["linux-release"].target, "My_Game")
            self.assertIn("include(cmake/GameRelease.cmake)", (root / "release/CMakeLists.txt").read_text())
            self.assertNotIn("printf", (root / "release/src/main.cpp").read_text())
            with self.assertRaises(ToolingError):
                create.create_project(root / "bad", name="Bad", template_id="minimal", engine_version="0.1.0",
                                      release=True, itch_target="bad; target")
            self.assertFalse((root / "bad").exists())


@unittest.skipUnless(shutil.which("clang++-18") and shutil.which("readelf"), "requires Clang 18 and readelf")
class ArchiveTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.binary = Path(cls.temp.name) / "game"
        cpp = Path(cls.temp.name) / "main.cpp"
        cpp.write_text("int main() { return 0; }\n")
        subprocess.run(["clang++-18", "-std=c++23", "-fno-exceptions", "-g0", str(cpp), "-o", str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def make_package(self, root):
        payload = root / "payload"
        payload.mkdir()
        (payload / "bin").mkdir()
        shutil.copy2(self.binary, payload / "bin/game")
        (payload / "NOTICE.txt").write_text("Fixture licensing notice")
        (payload / "licenses").mkdir()
        (payload / "licenses/test.txt").write_text("Fixture test license")
        external = validate_native(payload, "bin/game")
        records = inventory(payload)
        manifest = {"schemaVersion": 1, "profile": "linux-release", "targetPlatform": "linux-x64",
                    "entryPoint": "bin/game", "version": "0.1.0", "source": {"revision": "unknown", "dirty": True},
                    "engineLockSha256": "a" * 64, "releaseConfigSha256": "b" * 64,
                    "sdk": asdict(SdkIdentity.from_json(_manifest_json(build_flavor="Release"))),
                    "localInputs": True, "policy": POLICY, "systemLibraries": external, "files": records}
        package = root / "package"
        package.mkdir()
        release._archive(payload, package / "game.zip", records, manifest)
        digest = file_digest(package / "game.zip")
        (package / "package.json").write_bytes(model.canonical({"schemaVersion": 1, "archiveSha256": digest,
            "manifestSha256": model.sha256(model.canonical(manifest)), "profile": "linux-release", "version": "0.1.0",
            "releaseConfigSha256": "b" * 64, "toolVersion": "0.1.0"}))
        (package / "validation.json").write_bytes(model.canonical({"schemaVersion": 1, "archiveSha256": digest,
            "policy": POLICY, "toolVersion": "0.1.0", "checks": ["payload", "native", "clean-extraction"]}))
        return package, manifest, payload

    def test_roundtrip_and_executable_permissions(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            package, manifest, _ = self.make_package(root)
            verified = verify_package(package)
            self.assertEqual(verified["manifest"], manifest)
            extracted = root / "extracted"
            extracted.mkdir()
            extract_archive(package / "game.zip", extracted)
            self.assertEqual(subprocess.run([str(extracted / "bin/game")], check=False).returncode, 0)
            self.assertEqual(stat.S_IMODE((extracted / "bin/game").stat().st_mode), 0o755)

    def test_same_payload_yields_same_archive(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            package, manifest, payload = self.make_package(root)
            release._archive(payload, root / "other.zip", manifest["files"], manifest)
            self.assertEqual(file_digest(root / "other.zip"), file_digest(package / "game.zip"))

    def test_payload_changes_fail_even_if_zip_digest_is_recomputed(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            package, manifest, payload = self.make_package(root)
            (payload / "NOTICE.txt").write_text("tampered")
            release._archive(payload, package / "game.zip", manifest["files"], manifest)
            for filename in ("package.json", "validation.json"):
                obj = json.loads((package / filename).read_text())
                obj["archiveSha256"] = file_digest(package / "game.zip")
                (package / filename).write_text(json.dumps(obj))
            with self.assertRaises(ToolingError):
                verify_package(package)

    def test_mismatched_sidecar_and_report(self):
        for filename, field in (("package.json", "manifestSha256"), ("validation.json", "archiveSha256")):
            with self.subTest(filename=filename), tempfile.TemporaryDirectory() as temp:
                package, _, _ = self.make_package(Path(temp))
                obj = json.loads((package / filename).read_text())
                obj[field] = "0" * 64
                (package / filename).write_text(json.dumps(obj))
                with self.assertRaises(ToolingError):
                    verify_package(package)

    def test_staged_symlinks_debug_and_case_collisions(self):
        for kind in ("symlink", "debug", "case"):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as temp:
                _, _, payload = self.make_package(Path(temp))
                if kind == "symlink":
                    (payload / "link").symlink_to(self.binary)
                elif kind == "debug":
                    (payload / "game.pdb").write_bytes(b"symbols")
                else:
                    (payload / "notice.TXT").write_text("collision")
                with self.assertRaises(ToolingError):
                    inventory(payload)

    def test_archive_paths_types_duplicates_and_limits(self):
        bad_entries = ["../escape", "/absolute", "bin\\game", "bin/../game", "bin//game", "C:drive", "game.h"]
        for name in bad_entries + ["symlink", "duplicate", "parents", "case-directory", "too-many", "too-large"]:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                archive = root / "bad.zip"
                with zipfile.ZipFile(archive, "w") as writer:
                    def write(entry, mode=0o100644):
                        info = zipfile.ZipInfo(entry)
                        info.create_system = 3
                        info.external_attr = mode << 16
                        info.compress_type = zipfile.ZIP_DEFLATED
                        writer.writestr(info, b"payload")
                    if name == "duplicate":
                        write("a")
                        write("A")
                    elif name == "parents":
                        write("a")
                        write("a/b")
                    elif name == "case-directory":
                        write("Dir/a")
                        write("dir/b")
                    elif name == "symlink":
                        write("link", 0o120777)
                    else:
                        write("a" if name in ("too-many", "too-large") else name)
                extraction = root / "extract"
                extraction.mkdir()
                with patch("ludus_tools.package_verify.MAX_ENTRIES", 0 if name == "too-many" else 10000), \
                     patch("ludus_tools.package_verify.MAX_EXPANDED", 0 if name == "too-large" else 30000000000):
                    with self.assertRaises(ToolingError):
                        extract_archive(archive, extraction)
                self.assertFalse((root / "escape").exists())
                self.assertEqual(list(extraction.iterdir()), [])

    def test_missing_notice_or_license_and_unexecutable_entry(self):
        for kind in ("notice", "license", "executable"):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as temp:
                _, _, payload = self.make_package(Path(temp))
                if kind == "notice":
                    (payload / "NOTICE.txt").unlink()
                elif kind == "license":
                    shutil.rmtree(payload / "licenses")
                else:
                    (payload / "bin/game").chmod(0o644)
                with self.assertRaises(ToolingError):
                    validate_native(payload, "bin/game")

    def test_unknown_dependency_and_absolute_loader_path(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            _, _, payload = self.make_package(root)
            cpp = root / "dependency.cpp"
            cpp.write_text('extern "C" int value() { return 0; }')
            subprocess.run(["clang++-18", "-shared", "-fPIC", str(cpp), "-o", str(root / "libfixture.so")], check=True)
            cpp.write_text('extern "C" int value(); int main() { return value(); }')
            for loader in ([], [f"-Wl,-rpath,{root}"]):
                subprocess.run(["clang++-18", str(cpp), "-L", str(root), "-lfixture", *loader,
                                "-o", str(payload / "bin/game")], check=True)
                with self.assertRaises(ToolingError):
                    validate_native(payload, "bin/game")

    def test_relative_bundled_dependency_is_valid(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            _, _, payload = self.make_package(root)
            library = payload / "lib"
            library.mkdir()
            cpp = root / "fixture.cpp"
            cpp.write_text('extern "C" int value() { return 0; }')
            subprocess.run(["clang++-18", "-fno-exceptions", "-shared", "-fPIC", str(cpp),
                            "-Wl,-soname,libfixture.so", "-o", str(library / "libfixture.so")], check=True)
            cpp.write_text('extern "C" int value(); int main() { return value(); }')
            subprocess.run(["clang++-18", "-fno-exceptions", str(cpp), "-L", str(library), "-lfixture",
                            "-Wl,-rpath,$ORIGIN/../lib", "-o", str(payload / "bin/game")], check=True)
            validate_native(payload, "bin/game")
            self.assertEqual(subprocess.run([str(payload / "bin/game")], check=False).returncode, 0)
            (library / "libfixture.so").write_text("a text file is not a shared library")
            with self.assertRaises(ToolingError):
                validate_native(payload, "bin/game")

    def test_atomic_no_replace_preserves_existing_directory(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            staging, destination = root / "staging", root / "existing"
            staging.mkdir()
            destination.mkdir()
            with self.assertRaises(ToolingError):
                release._publish_directory(staging, destination)
            self.assertTrue(staging.is_dir())
            self.assertEqual(list(destination.iterdir()), [])


@unittest.skipUnless(shutil.which("clang++-18") and shutil.which("readelf") and shutil.which("cmake"),
                     "requires Clang 18, readelf, CMake")
class ProjectRoundtripTests(unittest.TestCase):
    def setup_project(self, root):
        compiler = subprocess.run(["clang++-18", "--version"], capture_output=True, text=True, check=True).stdout
        import re
        manifest = _manifest_json(build_flavor="Release", compiler_version=re.search(r"\d+\.\d+\.\d+", compiler)[0],
                                  target_triple="x86_64-linux-gnu")
        sdk = _write_sdk_prefix(root / "sdk", manifest)
        # A fixture SDK keeps the release tests independent of graphics/system
        # dependencies; it does not certify real SDK relocation or license closure.
        headers = sdk / "include/ludus/foundation/base"
        headers.mkdir(parents=True)
        (headers / "version.hpp").write_text('#pragma once\n#include <string_view>\nnamespace ludus::foundation {\n'
            'inline std::string_view version_string() noexcept { return "fixture"; }\n}\n')
        cmake = sdk / "lib/cmake/Ludus"
        cmake.mkdir(parents=True)
        (cmake / "LudusConfig.cmake").write_text('get_filename_component(_prefix "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)\n'
            'add_library(Ludus::FoundationBase INTERFACE IMPORTED)\n'
            'set_target_properties(Ludus::FoundationBase PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${_prefix}/include")\n'
            'set(Ludus_SDK_MANIFEST "${_prefix}/share/Ludus/LudusSdkManifest.json")\n'
            'function(ludus_apply_app_policy target)\n'
            'target_compile_options(${target} PRIVATE -fno-exceptions -Wall -Wextra -Werror)\nendfunction()\n')
        licenses = sdk / "share/Ludus/licenses"
        licenses.mkdir()
        (licenses / "LICENSE").write_text("Fixture SDK license")
        project = root / "My Game 日本語"
        create.create_project(project, name="MyGame", template_id="minimal", engine_version="0.1.0",
                              local_sdk_prefix=sdk, release=True, itch_target="tester/mygame")
        return project, sdk

    def test_build_extract_plan_and_failed_build_preserves_package(self):
        with tempfile.TemporaryDirectory() as temp, patch.dict(os.environ, {"CXX": "clang++-18", "BUTLER_API_KEY": "fixture-secret"}):
            root = Path(temp)
            project, sdk = self.setup_project(root)
            package = release.package_project(project, profile="linux-release", version="0.1.0", store=SdkStore(root / "store"))
            verified = verify_package(package)
            self.assertEqual(verified["manifest"]["sdk"]["flavor"], "Release")
            self.assertTrue(verified["manifest"]["localInputs"])
            with self.assertRaises(ToolingError):
                release.publish_plan(project, package, destination="linux")
            plan = release.publish_plan(project, package, destination="linux", allow_local_inputs=True)
            self.assertEqual(plan["target"], "tester/mygame")
            self.assertNotIn("fixture-secret", json.dumps(plan))
            # Planning is offline and does not need the SDK or a compiler.
            shutil.rmtree(sdk)
            self.assertEqual(release.publish_plan(project, package, destination="linux", allow_local_inputs=True), plan)
            extracted = root / "extracted"
            extracted.mkdir()
            extract_archive(package / "game.zip", extracted)
            self.assertEqual(subprocess.run([str(extracted / "bin/MyGame")], check=False).returncode, 0)
            import io
            from contextlib import redirect_stdout
            with redirect_stdout(io.StringIO()) as output:
                self.assertEqual(cli.main(["--json", "project", "package", "verify", str(package)]), 0)
            self.assertEqual(json.loads(output.getvalue())["verified"]["archiveSha256"], package.name)

    def test_cmake_install_loader_rewrite_preserves_program_identity(self):
        with tempfile.TemporaryDirectory() as temp, patch.dict(os.environ, {"CXX": "clang++-18"}):
            root = Path(temp)
            project, _ = self.setup_project(root)
            cmake = project / "CMakeLists.txt"
            cmake.write_text(cmake.read_text() + '\nset_target_properties(MyGame PROPERTIES '
                             'BUILD_RPATH "${CMAKE_CURRENT_SOURCE_DIR}/runtime" INSTALL_RPATH "$ORIGIN/../lib")\n')
            package = release.package_project(project, profile="linux-release", version="0.1.0", store=SdkStore(root / "store"))
            extracted = root / "extracted"
            extracted.mkdir()
            extract_archive(package / "game.zip", extracted)
            artifact = project / "out/build/linux-clang-release/MyGame"
            self.assertNotEqual(file_digest(artifact), file_digest(extracted / "bin/MyGame"))
            self.assertEqual(executable_identity(artifact), executable_identity(extracted / "bin/MyGame"))
            self.assertEqual(subprocess.run([str(extracted / "bin/MyGame")], check=False).returncode, 0)
            cpp = root / "different.cpp"
            cpp.write_text("int main() { return 7; }")
            subprocess.run(["clang++-18", "-fno-exceptions", str(cpp), "-o", str(root / "different")], check=True)
            self.assertNotEqual(executable_identity(artifact), executable_identity(root / "different"))

    def test_failed_build_cannot_package_old_executable(self):
        with tempfile.TemporaryDirectory() as temp, patch.dict(os.environ, {"CXX": "clang++-18"}):
            root = Path(temp)
            project, _ = self.setup_project(root)
            store = SdkStore(root / "store")
            package = release.package_project(project, profile="linux-release", version="0.1.0", store=store)
            (project / "src/main.cpp").write_text("this cannot compile;")
            with self.assertRaises(ToolingError) as failure:
                release.package_project(project, profile="linux-release", version="0.2.0", store=store)
            self.assertEqual(failure.exception.code, "BuildFailed")
            verify_package(package)
            self.assertEqual([p.name for p in package.parent.iterdir() if p.name != ".cmake"], [package.name])

    def test_cancel_and_lock_contention_never_publish_partial_package(self):
        from ludus_tools.buildlock import BuildTreeLock
        with tempfile.TemporaryDirectory() as temp, patch.dict(os.environ, {"CXX": "clang++-18"}):
            root = Path(temp)
            project, _ = self.setup_project(root)
            store = SdkStore(root / "store")
            build = project / "out/build/linux-clang-release"
            with BuildTreeLock(build), self.assertRaises(ToolingError) as error:
                release.package_project(project, profile="linux-release", version="0.1.0", store=store)
            self.assertEqual(error.exception.code, "Busy")
            with patch.object(release.operations, "_run", side_effect=KeyboardInterrupt), self.assertRaises(KeyboardInterrupt):
                release.package_project(project, profile="linux-release", version="0.1.0", store=store)
            self.assertEqual(list((project / "out/packages/linux-release").iterdir()), [])

    def test_sdk_changes_mid_build_fail_without_package(self):
        with tempfile.TemporaryDirectory() as temp, patch.dict(os.environ, {"CXX": "clang++-18"}):
            root = Path(temp)
            project, sdk = self.setup_project(root)
            def run(argv, **kwargs):
                (sdk / "lib/libludus_foundation_base.a").write_bytes(b"changed")
                return 0
            with patch.object(release.operations, "_run", side_effect=run), self.assertRaises(ToolingError) as error:
                release.package_project(project, profile="linux-release", version="0.1.0", store=SdkStore(root / "store"))
            self.assertEqual(error.exception.code, "StampChanged")
            self.assertEqual(list((project / "out/packages/linux-release").iterdir()), [])

    def test_metadata_changes_conflict_and_build_environment_has_no_secret(self):
        with tempfile.TemporaryDirectory() as temp, patch.dict(os.environ, {"CXX": "clang++-18", "BUTLER_API_KEY": "secret"}):
            root = Path(temp)
            project, sdk = self.setup_project(root)
            original_run = release.operations._run
            def run(argv, **kwargs):
                self.assertNotIn("BUTLER_API_KEY", kwargs["env"])
                return original_run(argv, **kwargs)
            with patch.object(release.operations, "_run", side_effect=run):
                package = release.package_project(project, profile="linux-release", version="0.1.0", store=SdkStore(root / "store"))
            path = project / "ludus.release.json"
            path.write_text(path.read_text() + "\n")
            with self.assertRaises(ToolingError) as failure:
                release.publish_plan(project, package, destination="linux", allow_local_inputs=True)
            self.assertEqual(failure.exception.code, "Conflict")


if __name__ == "__main__":
    unittest.main()
