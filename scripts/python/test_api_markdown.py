"""Regressions for public XML presentation, contracts and stable symbol links."""
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET

from api_markdown import Description, render_reference, signature


class ApiMarkdownTests(unittest.TestCase):
    def test_contracts_preserve_parameters_returns_warnings_and_refs(self):
        xml = ET.fromstring('''<detaileddescription><para>Use
          <ref refid="classSample_1amethod">TryAllocate</ref> with <computeroutput>bytes</computeroutput>.
          <parameterlist kind="param"><parameteritem><parameternamelist>
            <parametername>bytes</parametername></parameternamelist>
            <parameterdescription><para>Allocation size in bytes.</para></parameterdescription>
          </parameteritem></parameterlist>
          <simplesect kind="return"><para>Null on failure; output is preserved.</para></simplesect>
          <simplesect kind="warning"><para>Borrowed memory &lt;must&gt; remain alive.</para></simplesect>
        </para></detaileddescription>''')
        rendered = Description({"classSample_1amethod": "classSample.html#amethod"}).render(xml)
        for expected in ('href="classSample.html#amethod"', "<code>bytes</code>",
                         "Allocation size in bytes.", "Returns", "output is preserved.",
                         'admonition warning', "&lt;must&gt;"):
            self.assertIn(expected, rendered)

    def test_unknown_contract_markup_fails_instead_of_disappearing(self):
        with self.assertRaisesRegex(ValueError, "Unsupported.*futurecontract"):
            Description({}).render(ET.fromstring("<futurecontract>Keep this.</futurecontract>"))

    def test_signature_preserves_templates_qualifiers_defaults_and_initializers(self):
        member = ET.fromstring('''<memberdef kind="function" static="yes" constexpr="yes">
          <templateparamlist><param><type>typename</type><declname>T</declname>
          <defval>uint32</defval></param></templateparamlist>
          <definition>T ns::Get</definition><argsstring>(T value = {}) noexcept</argsstring>
        </memberdef>''')
        self.assertEqual(signature(member),
                         "template <typename T = uint32>\nstatic constexpr T ns::Get(T value = {}) noexcept")
        enum = ET.fromstring('<memberdef kind="enum" strong="yes"><name>Mode</name><type>uint8</type></memberdef>')
        self.assertEqual(signature(enum), "enum class Mode : uint8")
        macro = ET.fromstring('''<memberdef kind="define"><name>LUDUS_CHECK</name>
          <param><defname>condition</defname></param><initializer>Check(condition)</initializer></memberdef>''')
        self.assertEqual(signature(macro), "#define LUDUS_CHECK(condition) Check(condition)")
        member.append(ET.fromstring('<requiresclause>std::is_integral_v&lt;T&gt;</requiresclause>'))
        self.assertIn("\nrequires std::is_integral_v<T>", signature(member))

    def test_renderer_excludes_private_members_and_retains_overload_enum_anchors(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            xml = root / "xml"
            xml.mkdir()
            (root / "docs").mkdir()
            (root / "docs/api-main.md").write_text("# API\n\nPublic contracts.\n")
            header = root / "modules/example/include/ludus/example.h"
            header.parent.mkdir(parents=True)
            header.write_text("public declarations")
            (xml / "index.xml").write_text(
                '<doxygenindex><compound kind="class" refid="classSample"/></doxygenindex>')
            (xml / "classSample.xml").write_text('''<doxygen><compounddef kind="class" id="classSample">
              <compoundname>ns::Sample</compoundname><briefdescription><para>Public sample.</para></briefdescription>
              <sectiondef kind="public-func">
                <memberdef kind="function" prot="public" id="classSample_1afirst"><name>Get</name>
                  <definition>uint32 ns::Sample::Get</definition><argsstring>() const noexcept</argsstring>
                  <location file="modules/example/include/ludus/example.h" line="12"/></memberdef>
                <memberdef kind="function" prot="public" id="classSample_1asecond"><name>Get</name>
                  <definition>uint32 ns::Sample::Get</definition><argsstring>(uint32 index) const noexcept</argsstring>
                </memberdef></sectiondef>
              <sectiondef kind="public-type"><memberdef kind="enum" prot="public" id="classSample_1amode">
                <name>Mode</name><enumvalue id="classSample_1aready"><name>Ready</name><initializer>= 1</initializer>
                <briefdescription><para>Ready to use.</para></briefdescription></enumvalue></memberdef></sectiondef>
              <sectiondef kind="private-attrib"><memberdef kind="variable" prot="private" id="classSample_1aprivate">
                <name>Secret</name></memberdef></sectiondef>
            </compounddef></doxygen>''')
            output = root / "markdown"
            render_reference(root, xml, output, [header])
            page = (output / "api/classSample.md").read_text()
            for expected in ("Public sample.", "#afirst", "#asecond", "#aready", "Ready to use.",
                             "Get(uint32 index) const noexcept", "example.h#L12", "../reference/index.html"):
                self.assertIn(expected, page.replace("# ", "#"))
            self.assertNotIn("Secret", page)
            self.assertIn("class ns::Sample", page)
            self.assertIn('href="classSample.html"', (output / "api/annotated.md").read_text())


if __name__ == "__main__":
    unittest.main()
