"""Pinned host selection, acquisition failures and installed-CLI contracts; no network."""
import hashlib
import io
import json
import os
import stat
import tempfile
import unittest
import urllib.error
import zipfile
from contextlib import redirect_stdout
from dataclasses import replace
from pathlib import Path
from unittest.mock import Mock, patch

from ludus_tools import butler, cli, itch
from ludus_tools.errors import ToolingError, EXIT_OK, EXIT_UNAVAILABLE


class Response(io.BytesIO):
    def geturl(self):
        return 'https://downloads.itch.io/fixture.zip'


def fixture(entries=None):
    binary = b'fixture butler executable'
    output = io.BytesIO()
    with zipfile.ZipFile(output, 'w') as archive:
        for name, data in entries if entries is not None else [('butler', binary), ('7z.so', b'ignored')]:
            archive.writestr(name, data)
    data = output.getvalue()
    artifact = butler.ButlerArtifact('darwin-arm64', hashlib.sha256(data).hexdigest(), hashlib.sha256(binary).hexdigest())
    return data, binary, artifact


class ButlerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.destination = self.root / 'tools with spaces'
        self.archive, self.binary, self.artifact = fixture()
        self.opener = Mock()
        self.opener.open.side_effect = lambda *args, **kwargs: Response(self.archive)
        for target, value in [('host_artifact', self.artifact), ('urllib.request.build_opener', self.opener)]:
            mock = patch('ludus_tools.butler.' + target, return_value=value)
            mock.start()
            self.addCleanup(mock.stop)

    def test_verified_install_ignores_optional_files_and_preserves_neighbors(self):
        self.destination.mkdir()
        (self.destination / 'custom').write_text('keep')
        result = butler.install_butler(self.destination)
        self.assertEqual(result.read_bytes(), self.binary)
        self.assertEqual(stat.S_IMODE(result.stat().st_mode), 0o700)
        self.assertEqual(sorted(p.name for p in self.destination.iterdir()), ['butler', 'custom'])
        self.opener.open.assert_called_once_with(self.artifact.url, timeout=30)
        self.assertIs(itch.install_butler, butler.install_butler)

    def test_archive_mismatch_leaves_no_destination(self):
        self.archive = b'wrong archive'
        with self.assertRaisesRegex(ToolingError, 'archive digest mismatch'):
            butler.install_butler(self.destination)
        self.assertFalse(self.destination.exists())

    def test_binary_mismatch_preserves_existing_directory(self):
        self.destination.mkdir()
        (self.destination / 'custom').write_text('keep')
        with patch.object(butler, 'host_artifact', return_value=replace(self.artifact, binary_sha256='0'*64)):
            with self.assertRaisesRegex(ToolingError, 'executable digest mismatch'):
                butler.install_butler(self.destination)
        self.assertEqual([p.name for p in self.destination.iterdir()], ['custom'])

    def test_existing_file_or_link_fails_before_download(self):
        for linked in (False, True):
            with self.subTest(linked=linked):
                self.destination.mkdir(exist_ok=True)
                target = self.destination / 'butler'
                if linked: target.symlink_to(self.root / 'missing')
                else: target.write_text('existing')
                with self.assertRaisesRegex(ToolingError, 'already exists'):
                    butler.install_butler(self.destination)
                self.opener.open.assert_not_called()
                if not linked: self.assertEqual(target.read_text(), 'existing')
                target.unlink()

    def test_destination_race_never_replaces_winner(self):
        link = os.link
        def race(source, destination):
            destination.write_text('winner')
            link(source, destination)
        with patch.object(butler.os, 'link', side_effect=race), self.assertRaisesRegex(ToolingError, 'appeared'):
            butler.install_butler(self.destination)
        self.assertEqual((self.destination / 'butler').read_text(), 'winner')
        self.assertEqual([p.name for p in self.destination.iterdir()], ['butler'])

    def test_download_error_and_interrupt_clean_staging(self):
        for error in (urllib.error.URLError('offline'), KeyboardInterrupt()):
            with self.subTest(error=type(error).__name__):
                self.opener.open.side_effect = error
                with self.assertRaises((ToolingError, KeyboardInterrupt)):
                    butler.install_butler(self.destination)
                self.assertFalse(self.destination.exists())

    def test_interruption_after_download_bytes_removes_partial_files(self):
        class InterruptedResponse(Response):
            started = False
            def read(self, count):
                if self.started:
                    raise KeyboardInterrupt()
                self.started = True
                return super().read(8)
        self.opener.open.side_effect = lambda *args, **kwargs: InterruptedResponse(self.archive)
        with self.assertRaises(KeyboardInterrupt):
            butler.install_butler(self.destination)
        self.assertFalse(self.destination.exists())

    def test_reject_missing_duplicate_linked_and_bad_zip_entries(self):
        link = zipfile.ZipInfo('butler'); link.external_attr = (stat.S_IFLNK | 0o777) << 16
        for entries in ([], [('butler', self.binary), ('butler', self.binary)], [(link, b'target')], [('butler/', b'')]):
            with self.subTest(entries=entries):
                import warnings
                with warnings.catch_warnings():
                    warnings.simplefilter('ignore', UserWarning)
                    self.archive, _, pin = fixture(entries)
                with patch.object(butler, 'host_artifact', return_value=pin), self.assertRaises(ToolingError):
                    butler.install_butler(self.destination)
                self.assertFalse(self.destination.exists())
        self.archive = b'not a ZIP'
        pin = replace(self.artifact, archive_sha256=hashlib.sha256(self.archive).hexdigest())
        with patch.object(butler, 'host_artifact', return_value=pin), self.assertRaisesRegex(ToolingError, 'installation failed'):
            butler.install_butler(self.destination)
        self.assertFalse(self.destination.exists())

    def test_download_and_extraction_are_bounded(self):
        with patch.object(butler, 'MAX_DOWNLOAD', 8), self.assertRaisesRegex(ToolingError, 'size limit'):
            butler.install_butler(self.destination)
        self.assertFalse(self.destination.exists())
        output = io.BytesIO()
        with patch.object(butler, 'MAX_DOWNLOAD', 8), self.assertRaises(ToolingError):
            butler._copy_bounded(io.BytesIO(b'123456789'), output)
        self.assertEqual(output.getvalue(), b'')

    def test_reject_directory_symlink_and_file_without_download(self):
        for linked in (False, True):
            with self.subTest(linked=linked):
                if linked: self.destination.symlink_to(self.root, target_is_directory=True)
                else: self.destination.write_text('keep')
                with self.assertRaises(ToolingError): butler.install_butler(self.destination)
                self.opener.open.assert_not_called()
                self.destination.unlink()

    def test_cli_json_and_no_implicit_execution_or_credentials(self):
        output = io.StringIO()
        with patch.dict(os.environ, {'BUTLER_API_KEY': 'must-not-use'}), patch.object(itch, '_run_upload') as runner, redirect_stdout(output):
            code = cli.main(['--json', 'tools', 'install-butler', str(self.destination)])
        self.assertEqual(code, EXIT_OK)
        result = json.loads(output.getvalue())
        self.assertEqual(result['platform'], 'darwin-arm64')
        self.assertEqual(result['version'], '15.31.0')
        self.assertEqual(Path(result['executable']).read_bytes(), self.binary)
        self.assertNotIn('must-not-use', output.getvalue())
        runner.assert_not_called()

    def test_cli_acquisition_failure_is_structured(self):
        self.opener.open.side_effect = urllib.error.URLError('offline')
        output = io.StringIO()
        with redirect_stdout(output):
            code = cli.main(['--json', 'tools', 'install-butler', str(self.destination)])
        self.assertEqual(code, EXIT_UNAVAILABLE)
        self.assertEqual(json.loads(output.getvalue())['error']['code'], 'MissingTools')


class HostSelectionTests(unittest.TestCase):
    def test_platform_and_architecture_pins(self):
        for system, machine, channel in [('Linux', 'x86_64', 'linux-amd64'), ('Linux', 'AMD64', 'linux-amd64'), ('Darwin', 'arm64', 'darwin-arm64'), ('Darwin', 'aarch64', 'darwin-arm64'), ('Darwin', 'x86_64', 'darwin-amd64')]:
            with self.subTest(system=system, machine=machine), patch.object(butler.platform, 'system', return_value=system), patch.object(butler.platform, 'machine', return_value=machine):
                pin = butler.host_artifact()
                self.assertEqual(pin.channel, channel)
                self.assertIn('/15.31.0/', pin.url)
                self.assertEqual(len(pin.archive_sha256), 64)
                self.assertEqual(len(pin.binary_sha256), 64)

    def test_unsupported_host_fails_without_disk_or_network(self):
        for system, machine in [('Linux', 'arm64'), ('Windows', 'AMD64'), ('Darwin', 'i386')]:
            with tempfile.TemporaryDirectory() as temp, patch.object(butler.platform, 'system', return_value=system), patch.object(butler.platform, 'machine', return_value=machine), patch.object(butler.urllib.request, 'build_opener') as network:
                destination = Path(temp) / 'missing'
                with self.assertRaises(ToolingError): butler.install_butler(destination)
                self.assertFalse(destination.exists())
                network.assert_not_called()

    def test_reject_downgrade_before_following_redirect(self):
        handler = butler._HttpsRedirect()
        request = butler.urllib.request.Request('https://broth.itch.zone/fixture')
        with self.assertRaises(ToolingError):
            handler.redirect_request(request, None, 302, 'Found', {}, 'http://example.org/unsafe')
        result = handler.redirect_request(request, None, 302, 'Found', {}, 'https://example.org/safe')
        self.assertEqual(result.full_url, 'https://example.org/safe')
