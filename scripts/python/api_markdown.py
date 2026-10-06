"""Present Doxygen's public declarations and contracts as native MkDocs pages.

Thanks to Dimitri van Heesch / Doxygen, "Customizing the output", "Using the
XML output": use the indexed compound/member model as presentation data while
retaining Doxygen as the declaration extractor. No upstream renderer is copied.
https://www.doxygen.nl/manual/customize.html#xml
"""
from collections import defaultdict
from html import escape
import json
from pathlib import Path
import re
from urllib.parse import quote
import xml.etree.ElementTree as ET


KINDS = {"class", "struct", "union", "concept", "namespace", "file"}
SECTIONS = {
    "public-type": "Types", "public-func": "Methods",
    "public-static-func": "Static methods", "public-attrib": "Fields",
    "public-static-attrib": "Static fields", "func": "Functions",
    "typedef": "Aliases", "enum": "Enumerations", "var": "Constants and variables",
    "define": "Macros", "user-defined": "Members",
}


def text(node):
    if node is None:
        return ""
    if node.tag == "sp":
        return " "
    return (node.text or "") + "".join(text(child) + (child.tail or "") for child in node)


def heading(value):
    return re.sub(r"([\\`*_{}\[\]<>#!|])", r"\\\1", value)


def link(url, label):
    return f'<a href="{escape(url, quote=True)}">{escape(label)}</a>'


def fence(value):
    # A declaration or documentation example may itself contain backticks.
    delimiter = "`" * max(3, max((len(s) for s in re.findall(r"`+", value)), default=0) + 1)
    return f"\n{delimiter}cpp\n{value.strip()}\n{delimiter}\n"


class Description:
    """Translate structured contracts without treating their text as raw HTML."""
    def __init__(self, references):
        self.references = references

    def content(self, node):
        return escape(node.text or "") + "".join(self.render(child) + escape(child.tail or "")
                                                  for child in node)

    def render(self, node):
        tag = node.tag
        if tag in {"briefdescription", "detaileddescription", "inbodydescription",
                   "parameterdescription", "description", "highlight"}:
            return self.content(node)
        if tag == "para":
            return f'<div class="api-paragraph">{self.content(node)}</div>'
        if tag in {"bold", "emphasis", "computeroutput", "subscript", "superscript"}:
            html_tag = {"bold": "strong", "emphasis": "em", "computeroutput": "code",
                        "subscript": "sub", "superscript": "sup"}[tag]
            return f"<{html_tag}>{self.content(node)}</{html_tag}>"
        if tag == "ref":
            url = self.references.get(node.get("refid"))
            return link(url, text(node)) if url else escape(text(node))
        if tag == "ulink":
            return link(node.get("url", ""), text(node))
        if tag == "anchor":
            return f'<span id="{escape(node.get("id", ""), quote=True)}"></span>'
        if tag in {"linebreak", "sp", "nonbreakablespace"}:
            return {"linebreak": "<br />", "sp": " ", "nonbreakablespace": "&#160;"}[tag]
        if tag in {"itemizedlist", "orderedlist", "listitem"}:
            html_tag = {"itemizedlist": "ul", "orderedlist": "ol", "listitem": "li"}[tag]
            return f"<{html_tag}>{self.content(node)}</{html_tag}>"
        if tag == "parameterlist":
            label = {"param": "Parameter", "templateparam": "Template parameter",
                     "retval": "Return value"}.get(node.get("kind"), "Parameter")
            rows = []
            for item in node.findall("parameteritem"):
                names = ", ".join(escape(text(name)) for name in item.findall("parameternamelist/parametername"))
                description = item.find("parameterdescription")
                rows.append(f"<tr><td><code>{names}</code></td><td>{self.content(description)}</td></tr>")
            return (f"<table><thead><tr><th>{label}</th><th>Description</th></tr></thead>"
                    f"<tbody>{''.join(rows)}</tbody></table>")
        if tag == "simplesect":
            kind = node.get("kind", "note")
            title = {"return": "Returns", "pre": "Precondition", "post": "Postcondition",
                     "see": "See also"}.get(kind, kind.capitalize())
            style = "warning" if kind == "warning" else "note"
            return (f'<div class="admonition {style}"><p class="admonition-title">{escape(title)}</p>'
                    f'{self.content(node)}</div>')
        if tag == "programlisting":
            code = "\n".join(text(line) for line in node.findall("codeline"))
            return f"<pre><code>{escape(code)}</code></pre>"
        if tag == "verbatim":
            return f"<pre><code>{escape(text(node))}</code></pre>"
        if tag in {"sect1", "sect2", "sect3", "sect4"}:
            level = min(6, int(tag[-1]) + 2)
            title = node.findtext("title", "")
            body = "".join(self.render(child) for child in node if child.tag != "title")
            return f'<h{level} id="{escape(node.get("id", ""), quote=True)}">{escape(title)}</h{level}>{body}'
        if tag in {"table", "row", "entry"}:
            html_tag = {"table": "table", "row": "tr",
                        "entry": "th" if node.get("thead") == "yes" else "td"}[tag]
            return f"<{html_tag}>{self.content(node)}</{html_tag}>"
        # A new Doxygen construct must get a reviewed presentation; never silently
        # erase a warning, parameter contract, example or other structured content.
        raise ValueError(f"Unsupported Doxygen documentation element: {tag}")

    def descriptions(self, node):
        return "\n".join(self.render(child) for child in node
                         if child.tag in {"briefdescription", "detaileddescription", "inbodydescription"}
                         and text(child).strip())


def template_prefix(node):
    parameters = node.find("templateparamlist")
    if parameters is None:
        return ""
    values = []
    for param in parameters.findall("param"):
        value = text(param.find("type"))
        if param.findtext("declname"):
            value += " " + param.findtext("declname")
        if param.find("defval") is not None:
            value += " = " + text(param.find("defval"))
        values.append(value)
    return "template <" + ", ".join(values) + ">\n"


def signature(member):
    declaration = member.findtext("definition", "") + member.findtext("argsstring", "")
    if member.get("kind") == "enum":
        declaration = "enum " + ("class " if member.get("strong") == "yes" else "") + member.findtext("name", "")
        if text(member.find("type")).strip():
            declaration += " : " + text(member.find("type"))
    if not declaration:
        declaration = text(member.find("type")) + " " + member.findtext("name", "")
    if member.get("kind") == "define":
        declaration = "#define " + member.findtext("name", "")
        parameters = member.findall("param")
        if parameters:
            declaration += "(" + ", ".join(param.findtext("defname", "") for param in parameters) + ")"
    for keyword, attribute in (("constexpr", "constexpr"), ("static", "static"), ("explicit", "explicit")):
        if member.get(attribute) == "yes" and not re.search(rf"\b{keyword}\b", declaration):
            declaration = keyword + " " + declaration
    declaration = template_prefix(member) + declaration
    requires = text(member.find("requiresclause")).strip()
    if requires and "requires" not in declaration:
        declaration += "\nrequires " + requires
    if member.find("bitfield") is not None:
        declaration += " : " + text(member.find("bitfield"))
    initializer = text(member.find("initializer")).strip()
    return declaration + (" " + initializer if initializer else "")


def render_reference(root, xml, output, inputs):
    """Generate only public compound/member pages; retain published compound URLs."""
    compounds = {}
    for entry in ET.parse(xml / "index.xml").getroot().findall("compound"):
        if entry.get("kind") in KINDS:
            compound = ET.parse(xml / (entry.get("refid") + ".xml")).getroot().find("compounddef")
            if compound.get("prot") in {None, "public"}:
                compounds[entry.get("refid")] = compound
    references = {refid: refid + ".html" for refid in compounds}
    for refid, compound in compounds.items():
        for member in compound.findall("sectiondef/memberdef"):
            if member.get("prot") not in {None, "public"}:
                continue
            for node in [member, *member.findall("enumvalue")]:
                identifier = node.get("id")
                owner, _, anchor = identifier.rpartition("_1")
                if owner in compounds:
                    references[identifier] = owner + ".html#" + anchor
        for anchor in compound.iter("anchor"):
            references[anchor.get("id")] = refid + ".html#" + anchor.get("id")
    descriptions = Description(references)
    public_files = {path.relative_to(root).as_posix() for path in inputs}
    directory = output / "api"
    directory.mkdir(parents=True, exist_ok=True)

    def source(node):
        location = node.find("location")
        if location is None or location.get("file") not in public_files:
            return ""
        path = location.get("file")
        line = location.get("line")
        url = "https://github.com/Alegruz/Ludus/blob/main/" + quote(path)
        if line:
            url += "#L" + line
        return "\n" + link(url, "Declared in " + path) + "\n"

    def save(name, title, body):
        navigation = (link("../index.html", "Ludus Wiki") + " · " +
                      link("../reference/index.html", "API overview") + " · " +
                      link("index.html", "Browse API"))
        page = ("---\ntitle: " + json.dumps(title) + "\n---\n\n# " + heading(title) +
                "\n\n" + navigation + "\n\n" + body + "\n")
        (directory / (name + ".md")).write_text(page, encoding="utf-8")

    for refid, compound in compounds.items():
        name = compound.findtext("compoundname", refid)
        body = f"**{compound.get('kind').capitalize()}**\n\n" + descriptions.descriptions(compound)
        body += source(compound)
        includes = compound.find("includes")
        if includes is not None:
            include = text(includes)
            location = compound.find("location")
            if location is not None and "/include/" in location.get("file", ""):
                include = location.get("file").split("/include/", 1)[1]
            body += fence("#include <" + include + ">")
        kind = compound.get("kind")
        if kind in {"class", "struct", "union"}:
            declaration = kind + " " + name
            if compound.get("final") == "yes":
                declaration += " final"
            bases = [("virtual " if base.get("virt") == "virtual" else "") +
                     base.get("prot", "public") + " " + text(base)
                     for base in compound.findall("basecompoundref")]
            if bases:
                declaration += " : " + ", ".join(bases)
            body += fence(template_prefix(compound) + declaration)
        elif kind == "concept":
            body += fence(text(compound.find("initializer")))
        relationships = [(child, child.get("refid")) for child in compound
                         if child.tag in {"innerclass", "innernamespace", "basecompoundref", "derivedcompoundref"}
                         and child.get("refid") in references]
        if relationships:
            body += "\n## Related declarations\n\n" + "\n".join(
                "- " + link(references[target], text(child)) for child, target in relationships) + "\n"
        seen = set()
        for section in compound.findall("sectiondef"):
            members = [member for member in section.findall("memberdef")
                       if member.get("prot") in {None, "public"} and member.get("id") not in seen]
            if not members:
                continue
            title = section.findtext("header") or SECTIONS.get(section.get("kind"), "Members")
            body += "\n## " + heading(title) + "\n\n"
            for member in members:
                seen.add(member.get("id"))
                anchor = member.get("id").rpartition("_1")[2]
                body += "\n### " + heading(member.findtext("name", "Member")) + " { #" + anchor + " }\n"
                body += fence(signature(member))
                body += descriptions.descriptions(member) + "\n"
                type_links = {link(references[node.get("refid")], text(node))
                              for node in member.findall(".//type/ref") if node.get("refid") in references}
                if type_links:
                    body += "\nRelated types: " + " · ".join(sorted(type_links)) + "\n"
                for value in member.findall("enumvalue"):
                    value_anchor = value.get("id").rpartition("_1")[2]
                    body += "\n#### " + heading(value.findtext("name", "Value")) + " { #" + value_anchor + " }\n"
                    body += fence(value.findtext("name", "") + " " + text(value.find("initializer")))
                    body += descriptions.descriptions(value) + "\n"
                body += source(member)
        save(refid, name, body)

    categories = [("namespaces", "Namespaces", {"namespace"}),
                  ("annotated", "Classes and structs", {"class", "struct", "union"}),
                  ("concepts", "Concepts", {"concept"}), ("files", "Header files", {"file"})]
    for filename, title, kinds in categories:
        groups = defaultdict(list)
        for refid, compound in compounds.items():
            if compound.get("kind") in kinds:
                name = compound.findtext("compoundname", refid)
                location = compound.find("location")
                group = "::".join(name.split("::")[:2]) if "::" in name else "SDK"
                if compound.get("kind") == "file" and location is not None:
                    group = "/".join(location.get("file", "").split("/")[:3])
                groups[group].append((name, refid))
        body = ""
        for group, values in sorted(groups.items()):
            body += "\n## " + heading(group) + "\n\n"
            body += "\n".join("- " + link(references[refid], name) for name, refid in sorted(values)) + "\n"
        save(filename, title, body or "No public declarations of this kind.\n")
    landing = (root / "docs/api-main.md").read_text(encoding="utf-8")
    # The authored landing content keeps its contract; the generated index adds
    # destinations derived from the same XML inventory as the symbol pages.
    landing = landing.split("\n", 1)[1]
    landing += "\n## Browse declarations\n\n" + "\n".join(
        "- " + link(filename + ".html", title) for filename, title, _ in categories)
    save("index", "Ludus SDK API reference", landing)
