"""Regressions for accidental publication and documentation coverage loss."""
import hashlib
import io
from pathlib import Path
import tarfile
import tempfile
import unittest
import stat
import zipfile
import warnings
from unittest.mock import patch

from api_reference import bootstrap, check_coverage, check_extracted_files, coverage, public_inputs


class ApiReferenceTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def header(self, name):
        path = self.root / "modules/example" / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("struct Example {};\n")
        return path

    def cmake(self, files):
        path = self.root / "modules/example/CMakeLists.txt"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("target_sources(example PUBLIC FILE_SET public_headers TYPE HEADERS "
                        "BASE_DIRS include FILES " + files + ")\n")

    def test_only_registered_public_headers_are_inputs(self):
        public = self.header("include/ludus/example.h")
        self.header("src/internal/private.h")
        self.header("tests/test.h")
        self.header("include/ludus/detail/helper.h")
        self.cmake("include/ludus/example.h include/ludus/detail/helper.h")
        self.assertEqual(public_inputs(self.root), [public])

    def test_unregistered_and_missing_headers_fail(self):
        self.header("include/ludus/example.h")
        self.header("include/ludus/unregistered.h")
        self.cmake("include/ludus/example.h")
        with self.assertRaisesRegex(ValueError, "missing from"):
            public_inputs(self.root)
        self.cmake("include/ludus/missing.h")
        with self.assertRaisesRegex(ValueError, "Missing"):
            public_inputs(self.root)

    def test_unknown_generated_header_and_glob_fail_closed(self):
        self.cmake('"${GENERATED}/new_policy.hpp"')
        with self.assertRaisesRegex(ValueError, "Unknown generated"):
            public_inputs(self.root)
        self.cmake("${PUBLIC_HEADERS}")
        with self.assertRaises(ValueError):
            public_inputs(self.root)

    def test_removed_description_or_changed_signature_is_new_gap(self):
        baseline = ["function ns::Old()"]
        self.assertEqual(check_coverage({"function ns::Old()"}, baseline), [])
        self.assertEqual(check_coverage({"function ns::Old(int)"}, baseline), ["function ns::Old(int)"])
        self.assertEqual(check_coverage({"function ns::Documented()"}, baseline), ["function ns::Documented()"])

    def test_extraction_cannot_omit_or_add_input_files(self):
        public = self.root / "public.h"
        self.root.joinpath("index.xml").write_text(
            '<doxygenindex><compound refid="file" kind="file"/></doxygenindex>')
        self.root.joinpath("file.xml").write_text(
            '<doxygen><compounddef><location file="private.h"/></compounddef></doxygen>')
        with self.assertRaisesRegex(ValueError, "inventory mismatch"):
            check_extracted_files(self.root, self.root, [public])
        self.root.joinpath("file.xml").write_text(
            '<doxygen><compounddef><location file="public.h"/></compounddef></doxygen>')
        check_extracted_files(self.root, self.root, [public])

    def test_xml_includes_enum_values_and_excludes_private_members(self):
        self.root.joinpath("index.xml").write_text(
            '<doxygenindex><compound refid="sample" kind="struct"/></doxygenindex>')
        self.root.joinpath("sample.xml").write_text('''<doxygen><compounddef kind="struct">
          <compoundname>ns::Sample</compoundname>
          <briefdescription><para>Public state.</para></briefdescription>
          <sectiondef><memberdef kind="function" prot="private"><name>Secret</name></memberdef>
          <memberdef kind="enum" prot="public"><name>Mode</name><qualifiedname>ns::Sample::Mode</qualifiedname>
            <briefdescription><para>Modes.</para></briefdescription>
            <enumvalue><name>Ready</name><briefdescription><para>Ready to use.</para></briefdescription></enumvalue>
            <enumvalue><name>Pending</name></enumvalue>
          </memberdef></sectiondef></compounddef></doxygen>''')
        symbols, missing = coverage(self.root)
        self.assertIn("struct ns::Sample", symbols)
        self.assertEqual(missing, {"enumvalue ns::Sample::Mode::Pending"})
        self.assertFalse(any("Secret" in key for key in symbols))

    def bootstrap_archive(self):
        data = io.BytesIO()
        with tarfile.open(fileobj=data, mode="w:gz") as archive:
            entry = tarfile.TarInfo("doxygen/bin/doxygen")
            binary = b"pinned binary"
            entry.size = len(binary)
            archive.addfile(entry, io.BytesIO(binary))
        content = data.getvalue()
        pin = {"linux_x64_url": "https://example.test/doxygen.tar.gz",
               "sha256": hashlib.sha256(content).hexdigest(),
               "archive_binary": "doxygen/bin/doxygen"}
        return content, pin

    @patch("api_reference.platform.machine", return_value="x86_64")
    @patch("api_reference.platform.system", return_value="Linux")
    def test_bootstrap_identifies_client_and_verifies_download(self, _system, _machine):
        content, pin = self.bootstrap_archive()
        with patch("api_reference.urllib.request.urlopen", return_value=io.BytesIO(content)) as download:
            binary = Path(bootstrap(self.root, pin))
        request = download.call_args.args[0]
        self.assertEqual(request.full_url, pin["linux_x64_url"])
        self.assertEqual(request.get_header("User-agent"), "Ludus-docs/1.0")
        self.assertEqual(binary.read_bytes(), b"pinned binary")
        self.assertTrue(binary.stat().st_mode & 0o111)
        with patch("api_reference.urllib.request.urlopen") as download:
            bootstrap(self.root, pin)
            download.assert_not_called()

    @patch("api_reference.platform.machine", return_value="x86_64")
    @patch("api_reference.platform.system", return_value="Linux")
    def test_bootstrap_rejects_corrupt_download_before_extraction(self, _system, _machine):
        _, pin = self.bootstrap_archive()
        with patch("api_reference.urllib.request.urlopen", return_value=io.BytesIO(b"corrupt")):
            with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                bootstrap(self.root, pin)
        self.assertFalse((self.root / "out/doxygen-tools/bin/doxygen").exists())

    def mac_archive(self, kind="regular", duplicate=False):
        data = io.BytesIO()
        with zipfile.ZipFile(data, "w") as archive:
            entry = zipfile.ZipInfo("doxygen/doxygen")
            entry.create_system = 3
            entry.external_attr = ((stat.S_IFLNK if kind == "symlink" else stat.S_IFREG) | 0o755) << 16
            archive.writestr(entry, b"native binary")
            archive.writestr("../../unexpected", b"never extract")
            if duplicate:
                archive.writestr(entry, b"duplicate")
        content = data.getvalue()
        selected = {"url": "https://example.test/doxygen.zip",
                    "sha256": hashlib.sha256(content).hexdigest(),
                    "archive_binary": "doxygen/doxygen"}
        return content, {"macos_arm64": selected, "macos_x64": selected}

    def test_mac_architectures_and_cached_binary_repair(self):
        content, pin = self.mac_archive()
        for machine in ("arm64", "aarch64", "x86_64", "AMD64"):
            with self.subTest(machine=machine), patch("api_reference.platform.system", return_value="Darwin"), \
                    patch("api_reference.platform.machine", return_value=machine):
                with patch("api_reference.urllib.request.urlopen", return_value=io.BytesIO(content)):
                    binary = Path(bootstrap(self.root, pin))
                self.assertEqual(binary.read_bytes(), b"native binary")
                binary.write_bytes(b"damaged")
                with patch("api_reference.urllib.request.urlopen", side_effect=AssertionError("network")):
                    bootstrap(self.root, pin)
                self.assertEqual(binary.read_bytes(), b"native binary")
                self.assertFalse((self.root / "unexpected").exists())

    @patch("api_reference.platform.machine", return_value="arm64")
    @patch("api_reference.platform.system", return_value="Darwin")
    def test_mac_rejects_symlink_and_preserves_existing_binary(self, _system, _machine):
        content, pin = self.mac_archive("symlink")
        binary = self.root / "out/doxygen-tools/bin/doxygen"
        binary.parent.mkdir(parents=True)
        binary.write_bytes(b"existing tool")
        with patch("api_reference.urllib.request.urlopen", return_value=io.BytesIO(content)):
            with self.assertRaisesRegex(ValueError, "regular archive member"):
                bootstrap(self.root, pin)
        self.assertEqual(binary.read_bytes(), b"existing tool")
        self.assertFalse(list(binary.parents[1].glob("staging-*")))

    @patch("api_reference.platform.machine", return_value="x86_64")
    @patch("api_reference.platform.system", return_value="Darwin")
    def test_failed_mac_download_preserves_existing_archive_and_binary(self, _system, _machine):
        content, pin = self.mac_archive()
        with patch("api_reference.urllib.request.urlopen", return_value=io.BytesIO(content)):
            binary = Path(bootstrap(self.root, pin))
        pin["macos_x64"]["sha256"] = "0" * 64
        with patch("api_reference.urllib.request.urlopen", return_value=io.BytesIO(b"bad")):
            with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                bootstrap(self.root, pin)
        self.assertEqual(binary.read_bytes(), b"native binary")
        self.assertEqual((binary.parents[1] / "doxygen-macos-x64.zip").read_bytes(), content)

    @patch("api_reference.platform.machine", return_value="arm64")
    @patch("api_reference.platform.system", return_value="Darwin")
    def test_mac_duplicate_and_missing_cli_members_fail(self, _system, _machine):
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", UserWarning)
            content, pin = self.mac_archive(duplicate=True)
        for member in ("doxygen/doxygen", "missing"):
            with self.subTest(member=member):
                pin["macos_arm64"]["archive_binary"] = member
                with patch("api_reference.urllib.request.urlopen", return_value=io.BytesIO(content)):
                    with self.assertRaisesRegex(ValueError, "exactly one CLI binary"):
                        bootstrap(self.root, pin)
                self.assertFalse((self.root / "out/doxygen-tools/bin/doxygen").exists())

    def test_unsupported_host_fails_before_network_or_output(self):
        for system, machine in (("Windows", "AMD64"), ("Linux", "aarch64"), ("Darwin", "ppc")):
            with self.subTest(system=system, machine=machine), \
                    patch("api_reference.platform.system", return_value=system), \
                    patch("api_reference.platform.machine", return_value=machine), \
                    patch("api_reference.urllib.request.urlopen") as download:
                with self.assertRaisesRegex(ValueError, "use --doxygen"):
                    bootstrap(self.root, {})
                download.assert_not_called()
                self.assertFalse((self.root / "out").exists())

    def test_concepts_are_subject_to_the_coverage_gate(self):
        self.root.joinpath("index.xml").write_text(
            '<doxygenindex><compound refid="concept" kind="concept"/></doxygenindex>')
        self.root.joinpath("concept.xml").write_text(
            '<doxygen><compounddef kind="concept"><compoundname>ns::Valid</compoundname>'
            '</compounddef></doxygen>')
        symbols, missing = coverage(self.root)
        self.assertEqual(symbols, {"concept ns::Valid"})
        self.assertEqual(missing, symbols)


if __name__ == "__main__":
    unittest.main()
