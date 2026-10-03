"""Existing-project setup, browser archives and explicit upload isolation."""
from __future__ import annotations

import json
import os
import shutil
import tempfile
import unittest
from dataclasses import asdict
from pathlib import Path
from unittest.mock import patch

from ludus_tools import create, release, release_model, itch
from ludus_tools.errors import ToolingError
from ludus_tools.identity import SdkIdentity
from ludus_tools.package_web import WEB_POLICY, validate_web
from ludus_tools.package_verify import inventory, file_digest, verify_package
from ludus_tools.release_setup import setup_release, WORKFLOW
from ludus_tools.sdkstore import SdkStore
from test_ludus_tools import _manifest_json


def project(root: Path):
    create.create_project(root, name="game", template_id="minimal", engine_version="0.1.0")
    return root


def web_package(root: Path):
    payload = root / "payload"
    payload.mkdir()
    (payload / "index.html").write_text('<script src="index.js"></script>')
    (payload / "index.js").write_text('console.log("fixture");')
    (payload / "index.wasm").write_bytes(b'\0asm\x01\0\0\0')
    (payload / "NOTICE.txt").write_text('fixture notice')
    (payload / "licenses").mkdir()
    (payload / "licenses/LICENSE").write_text('fixture license')
    config = release_model.load_release(root)
    manifest = {"schemaVersion": 1, "profile": "web-release", "targetPlatform": "web", "entryPoint": "index.html",
        "version": "0.1.0", "source": {"revision": "unknown", "dirty": True},
        "engineLockSha256": file_digest(root / "ludus.lock.json"), "releaseConfigSha256": config.digest,
        "sdk": asdict(SdkIdentity.from_json(_manifest_json(build_flavor='Release', target_triple='wasm32-unknown-emscripten'))),
        "localInputs": True, "policy": WEB_POLICY, "systemLibraries": [], "files": inventory(payload)}
    package = root / 'package'
    package.mkdir()
    release._archive(payload, package / 'game.zip', manifest['files'], manifest)
    digest = file_digest(package / 'game.zip')
    (package / 'package.json').write_bytes(release_model.canonical({"schemaVersion": 1, "archiveSha256": digest,
        "manifestSha256": release_model.sha256(release_model.canonical(manifest)), "profile": "web-release",
        "version": "0.1.0", "releaseConfigSha256": config.digest, "toolVersion": "0.1.0"}))
    (package / 'validation.json').write_bytes(release_model.canonical({"schemaVersion": 1, "archiveSha256": digest,
        "policy": WEB_POLICY, "toolVersion": "0.1.0", "checks": ['payload', 'web', 'clean-extraction']}))
    return package, payload


class SetupTests(unittest.TestCase):
    def test_existing_project_setup_preserves_code_and_generates_workflow(self):
        with tempfile.TemporaryDirectory() as temp:
            root = project(Path(temp) / 'game')
            source = (root / 'src/main.cpp').read_bytes()
            descriptor = (root / 'ludus.project.json').read_bytes()
            setup_release(root, platform='web', itch_target='tester/game', tools_ref='a' * 40,
                          build_command=['./init.sh', '--with-web', '--web-release'])
            self.assertEqual(source, (root / 'src/main.cpp').read_bytes())
            self.assertEqual(descriptor, (root / 'ludus.project.json').read_bytes())
            self.assertEqual(release_model.load_release(root).destinations['web'], ('web-release', 'html5'))
            self.assertIn('web-release', (root / '.github/workflows/itch-release.yml').read_text())
            before = (root / 'CMakeLists.txt').read_bytes()
            with self.assertRaises(ToolingError):
                setup_release(root, platform='web', itch_target='tester/other', tools_ref='a' * 40)
            self.assertEqual(before, (root / 'CMakeLists.txt').read_bytes())

    def test_existing_notice_is_not_overwritten_or_partial_setup(self):
        with tempfile.TemporaryDirectory() as temp:
            root = project(Path(temp) / 'game')
            (root / 'NOTICE.txt').write_text('authored')
            original = (root / 'CMakeLists.txt').read_bytes()
            with self.assertRaises(ToolingError):
                setup_release(root)
            self.assertEqual((root / 'NOTICE.txt').read_text(), 'authored')
            self.assertEqual((root / 'CMakeLists.txt').read_bytes(), original)
            self.assertFalse((root / 'cmake/GameRelease.cmake').exists())

    def test_cancel_before_commit_leaves_project_unchanged(self):
        with tempfile.TemporaryDirectory() as temp:
            root = project(Path(temp) / 'game')
            original = (root / 'CMakeLists.txt').read_bytes()
            def cancel():
                raise ToolingError('Cancelled', 'test')
            with self.assertRaises(ToolingError):
                setup_release(root, cancel_check=cancel)
            self.assertFalse((root / 'ludus.release.json').exists())
            self.assertEqual(original, (root / 'CMakeLists.txt').read_bytes())

    def test_descriptor_conflict_and_invalid_tools_ref(self):
        with tempfile.TemporaryDirectory() as temp:
            root = project(Path(temp) / 'game')
            for kwargs in ({'expected_descriptor_digest': 'b' * 64}, {'itch_target': 'tester/game'},
                           {'tools_ref': 'main'}, {'itch_target': 'bad;target', 'tools_ref': 'a' * 40}):
                with self.subTest(kwargs=kwargs), self.assertRaises(ToolingError):
                    setup_release(root, **kwargs)
            self.assertFalse((root / 'ludus.release.json').exists())

    def test_workflow_secret_is_scoped_to_upload_step_and_python_compiles(self):
        self.assertEqual(WORKFLOW.count('secrets.BUTLER_API_KEY'), 1)
        self.assertIn('needs: package', WORKFLOW)
        self.assertNotIn('pull_request_target', WORKFLOW)
        self.assertIn('cancel-in-progress: false', WORKFLOW)
        self.assertIn('--expected-digest', WORKFLOW)
        import textwrap
        for block in WORKFLOW.split("python3 - <<'PYTHON'\n")[1:]:
            code = block.split('          PYTHON')[0]
            compile(textwrap.dedent(code), '<workflow>', 'exec')


class BrowserTests(unittest.TestCase):
    def test_roundtrip_offline_plan_and_no_native_inspection(self):
        with tempfile.TemporaryDirectory() as temp:
            root = project(Path(temp) / 'game')
            setup_release(root, platform='web', itch_target='tester/game', tools_ref='a' * 40)
            package, _ = web_package(root)
            with patch('ludus_tools.package_verify.validate_native', side_effect=AssertionError('native tool invoked')):
                self.assertEqual(verify_package(package)['manifest']['targetPlatform'], 'web')
            plan = release.publish_plan(root, package, destination='web', allow_local_inputs=True)
            self.assertEqual(plan['channel'], 'html5')
            with self.assertRaises(ToolingError):
                release.publish_plan(root, package, destination='web')

    def test_reject_missing_remote_runtime_asset_and_wasm_debug(self):
        with tempfile.TemporaryDirectory() as temp:
            root = project(Path(temp) / 'game')
            setup_release(root, platform='web')
            _, payload = web_package(root)
            for html in ('<script src="missing.js"></script>', '<script src="https://example.org/a.js"></script>',
                         '<script src="../index.js"></script>'):
                (payload / 'index.html').write_text(html)
                with self.assertRaises(ToolingError):
                    validate_web(payload, 'index.html')
            (payload / 'index.html').write_text('<script src="index.js"></script>')
            for wasm in (b'bad', b'\0asm\x01\0\0\0\0\x05\x04name', b'\0asm\x01\0\0\0\x01\xff\xff\xff\xff\xff'):
                (payload / 'index.wasm').write_bytes(wasm)
                with self.assertRaises(ToolingError):
                    validate_web(payload, 'index.html')

    @unittest.skipUnless(shutil.which('clang++-18') and shutil.which('cmake'), 'requires pinned native fixture tools')
    def test_cmake_build_install_failed_build_and_wrong_sdk(self):
        with tempfile.TemporaryDirectory() as temp:
            root = project(Path(temp) / 'game')
            setup_release(root, platform='web')
            sdk = Path(temp) / 'sdk'
            (sdk / 'lib/cmake/Ludus').mkdir(parents=True)
            (sdk / 'share/Ludus/licenses').mkdir(parents=True)
            (sdk / 'share/Ludus/licenses/LICENSE').write_text('Fixture SDK license')
            manifest = _manifest_json(build_flavor='Release', target_triple='wasm32-unknown-emscripten')
            (sdk / 'share/Ludus/LudusSdkManifest.json').write_text(json.dumps(manifest))
            (sdk / 'lib/cmake/Ludus/LudusConfig.cmake').write_text(f'set(Ludus_SDK_MANIFEST "{sdk}/share/Ludus/LudusSdkManifest.json")\n')
            # Compile a real CMake executable and replace the test artifact in a
            # post-build command with tiny valid browser fixture bytes. This
            # checks build/install identity and failures, not Emscripten codegen.
            (root / 'src/main.cpp').write_text('int main() { return 0; }')
            (root / 'emit.py').write_text('from pathlib import Path\nimport sys\np=Path(sys.argv[1])\np.write_text("console.log(1);")\np.with_suffix(".wasm").write_bytes(b"\\0asm\\x01\\0\\0\\0")\n')
            (root / 'index.html').write_text('<script src="index.js"></script>')
            (root / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.29)
project(web_fixture LANGUAGES CXX)
find_package(Ludus CONFIG REQUIRED)
add_executable(game src/main.cpp)
set_target_properties(game PROPERTIES OUTPUT_NAME index SUFFIX ".js")
add_custom_command(TARGET game POST_BUILD COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/emit.py" "$<TARGET_FILE:game>")
configure_file(index.html index.html COPYONLY)
set(EMSCRIPTEN TRUE)
include(cmake/GameRelease.cmake)
''')
            (root / 'CMakePresets.json').write_text(json.dumps({'version': 6, 'configurePresets': [{
                'name': 'web-emscripten-release', 'generator': 'Ninja', 'binaryDir': '${sourceDir}/out/build/web-emscripten-release',
                'cacheVariables': {'CMAKE_BUILD_TYPE': 'Release', 'CMAKE_CXX_COMPILER': 'clang++-18', 'CMAKE_PREFIX_PATH': str(sdk)}}]}))
            result = release.package_project(root, profile='web-release', version='0.1.0', store=SdkStore(Path(temp) / 'store'))
            self.assertEqual(verify_package(result)['manifest']['policy'], WEB_POLICY)
            (root / 'src/main.cpp').write_text('does not compile')
            with self.assertRaises(ToolingError) as failed:
                release.package_project(root, profile='web-release', version='0.1.1', store=SdkStore(Path(temp) / 'store'))
            self.assertEqual(failed.exception.code, 'BuildFailed')
            manifest['build_flavor'] = 'Development'
            (sdk / 'share/Ludus/LudusSdkManifest.json').write_text(json.dumps(manifest))
            with self.assertRaises(ToolingError) as failed:
                release.package_project(root, profile='web-release', version='0.1.2', store=SdkStore(Path(temp) / 'store'))
            self.assertEqual(failed.exception.code, 'SdkIncompatible')


class EditorReleaseAdapterTests(unittest.TestCase):
    def test_setup_through_editor_protocol_and_digest_conflict(self):
        import hashlib
        import editor_tool
        from types import SimpleNamespace
        from test_editor_tool import StubEngineError
        with tempfile.TemporaryDirectory() as temp:
            root = project(Path(temp) / 'game')
            descriptor = root / 'ludus.project.json'
            context = editor_tool.ToolContext(Path(__file__).resolve().parents[2], SimpleNamespace(EngineError=StubEngineError), None)
            read_fd, write_fd = os.pipe()
            os.set_blocking(read_fd, False)
            try:
                outputs = []
                writer = editor_tool.ProtocolWriter('0' * 16, outputs.append)
                options = {'platform': 'web', 'itch_target': ''}
                bad = editor_tool.Operation(context, writer, '0' * 16, 'release_init', descriptor, 'a' * 64, read_fd, options)
                result = bad.execute()
                self.assertEqual(result.outcome, 'failed')
                self.assertFalse((root / 'ludus.release.json').exists())
                good = editor_tool.Operation(context, writer, '0' * 16, 'release_init', descriptor,
                    hashlib.sha256(descriptor.read_bytes()).hexdigest(), read_fd, options)
                result = good.execute()
                self.assertEqual(result.outcome, 'success')
                self.assertTrue((root / '.github/workflows/itch-release.yml').is_file())
                self.assertIn(b'release files', b''.join(outputs))
            finally:
                os.close(read_fd)
                os.close(write_fd)

    def test_release_fields_are_bounded_before_operation(self):
        import editor_tool
        base = {'protocol': 1, 'job': '0' * 16, 'operation': 'package', 'project': '/tmp/game/ludus.project.json',
                'expected_sha256': 'a' * 64, 'profile': 'web-release', 'version': '0.1.0', 'sdk': ''}
        editor_tool._validate_request(base)
        for key, value in (('profile', 'x' * 65), ('version', 'a\n'), ('sdk', 1)):
            with self.subTest(key=key), self.assertRaises(editor_tool.ProtocolError):
                editor_tool._validate_request({**base, key: value})


class UploadTests(unittest.TestCase):
    def test_private_snapshot_exact_digest_credentials_and_receipt(self):
        with tempfile.TemporaryDirectory() as temp:
            root = project(Path(temp) / 'game')
            setup_release(root, platform='web', itch_target='tester/game', tools_ref='a' * 40)
            package, _ = web_package(root)
            approved = file_digest(package / 'game.zip')
            snapshot_paths = []
            def run(argv, env, secret):
                snapshot_paths.append(Path(argv[2]))
                self.assertEqual(secret, 'test-secret')
                self.assertEqual(env['BUTLER_API_KEY'], 'test-secret')
                self.assertEqual(argv[3], 'tester/game:html5')
                self.assertNotIn('test-secret', ' '.join(argv))
                (package / 'game.zip').write_bytes(b'mutated after verification')
                self.assertEqual(file_digest(Path(argv[2])), approved)
                return 0
            with patch.dict(os.environ, {'BUTLER_API_KEY': 'test-secret'}), patch.object(itch, 'install_butler', return_value=Path('/fixture/butler')), patch.object(itch, '_run_upload', side_effect=run):
                result = itch.upload(root, package, destination='web', allow_local_inputs=True, expected_digest=approved)
            self.assertEqual(result['outcome'], 'upload-submitted')
            self.assertNotIn('test-secret', Path(result['receipt']).read_text())
            self.assertFalse(snapshot_paths[0].exists())

    def test_corrupt_or_unapproved_package_never_reaches_transport(self):
        with tempfile.TemporaryDirectory() as temp:
            root = project(Path(temp) / 'game')
            setup_release(root, platform='web', itch_target='tester/game', tools_ref='a' * 40)
            package, _ = web_package(root)
            with patch.object(itch, 'install_butler') as installer:
                with self.assertRaises(ToolingError):
                    itch.upload(root, package, destination='web', allow_local_inputs=True, expected_digest='0' * 64)
                (package / 'game.zip').write_bytes(b'corrupt')
                with self.assertRaises(ToolingError):
                    itch.upload(root, package, destination='web', allow_local_inputs=True)
                installer.assert_not_called()

    def test_transport_redacts_secret_across_write_boundaries(self):
        import io, sys
        from types import SimpleNamespace
        output = io.BytesIO()
        program = "import os,sys,time; k=os.environ['BUTLER_API_KEY']; sys.stdout.write('before:'+k[:3]); sys.stdout.flush(); time.sleep(.02); sys.stdout.write(k[3:]+':after')"
        with patch.object(sys, 'stdout', SimpleNamespace(buffer=output)):
            code = itch._run_upload([sys.executable, '-c', program], {'BUTLER_API_KEY': 'secret-token', 'PATH': os.defpath}, 'secret-token')
        self.assertEqual(code, 0)
        self.assertEqual(output.getvalue(), b'before:[redacted]:after')

    def test_interrupted_transport_records_unknown_outcome(self):
        with tempfile.TemporaryDirectory() as temp:
            root = project(Path(temp) / 'game')
            setup_release(root, platform='web', itch_target='tester/game', tools_ref='a' * 40)
            package, _ = web_package(root)
            with patch.dict(os.environ, {'BUTLER_API_KEY': 'test-secret'}), patch.object(itch, 'install_butler', return_value=Path('/fixture/butler')), patch.object(itch, '_run_upload', side_effect=KeyboardInterrupt):
                with self.assertRaises(KeyboardInterrupt):
                    itch.upload(root, package, destination='web', allow_local_inputs=True)
            receipt = next((root / 'out/publish-receipts').glob('*.json'))
            self.assertEqual(json.loads(receipt.read_text())['outcome'], 'upload-outcome-unknown')

    def test_failed_upload_has_failure_receipt_no_retry(self):
        with tempfile.TemporaryDirectory() as temp:
            root = project(Path(temp) / 'game')
            setup_release(root, platform='web', itch_target='tester/game', tools_ref='a' * 40)
            package, _ = web_package(root)
            with patch.dict(os.environ, {'BUTLER_API_KEY': 'test-secret'}), patch.object(itch, 'install_butler', return_value=Path('/fixture/butler')), patch.object(itch, '_run_upload', return_value=1) as runner:
                with self.assertRaises(ToolingError):
                    itch.upload(root, package, destination='web', allow_local_inputs=True)
                self.assertEqual(runner.call_count, 1)
            receipt = next((root / 'out/publish-receipts').glob('*.json'))
            self.assertEqual(json.loads(receipt.read_text())['outcome'], 'upload-failed')


if __name__ == '__main__':
    unittest.main()
