"""Render canonical docs directly, preserving the existing wiki URLs.

Thanks to Tom Christie and the MkDocs maintainers, "API Reference", File and
"Plugins", on_files/on_post_build: source files can keep their local link/edit
identity while using a different published path. No document body is copied.
https://www.mkdocs.org/dev-guide/api/#mkdocs.structure.files.File
https://www.mkdocs.org/dev-guide/plugins/
"""
import json
from pathlib import Path
import sys
from urllib.parse import quote, unquote, urlsplit

from markdown.extensions import Extension
from markdown.treeprocessors import Treeprocessor
from mkdocs.structure.files import File, InclusionLevel

sys.path.insert(0, str(Path(__file__).resolve().parent))
from documentation import digest, group, sources

authored_files = []
source_hashes = {}

class RepositoryLinks(Treeprocessor):
    def __init__(self, md, extension):
        super().__init__(md)
        self.extension = extension

    def run(self, element):
        source, root, repository = self.extension.context
        if source is None or not source.is_relative_to(root / "docs"):
            return
        for node in element.iter("a"):
            value = node.get("href", "")
            parts = urlsplit(value)
            if parts.scheme or parts.netloc or not parts.path:
                continue
            target = (source.parent / unquote(parts.path)).resolve()
            if target.is_relative_to(root / "docs") or not target.is_relative_to(root):
                continue
            if target.is_relative_to(root / "references"):
                # Keep the citation label/locator; private library files are
                # deliberately absent from the website and offline archive.
                node.tag = "span"
                node.attrib.pop("href", None)
                node.set("title", "Local reference copy (not distributed): " + unquote(value))
                continue
            if not target.exists():
                raise ValueError(f"{source.relative_to(root)}: missing repository target {value}")
            kind = "tree" if target.is_dir() else "blob"
            destination = repository.rstrip("/") + f"/{kind}/main/" + quote(target.relative_to(root).as_posix())
            if parts.fragment:
                destination += "#" + parts.fragment
            node.set("href", destination)


class RepositoryExtension(Extension):
    def extendMarkdown(self, md):
        # Run before MkDocs' priority-0 link resolver. Markdown has already
        # parsed inline/reference links; code examples remain ordinary text.
        # Thanks to the Python-Markdown authors, "Extension API: Treeprocessors",
        # for this parsed-tree boundary: https://python-markdown.github.io/extensions/api/
        md.treeprocessors.register(RepositoryLinks(md, self), "ludus-repository", 1)


def root_directory(config):
    return Path(config.config_file_path).parent


def on_config(config):
    def prefix(items):
        for item in items:
            for key, value in item.items():
                if isinstance(value, list):
                    prefix(value)
                elif (isinstance(value, str) and value != "README.md"
                      and not value.startswith(("wiki/", "api/", "https://", "http://"))
                      and not (Path(config.docs_dir) / value).is_file()):
                    item[key] = "wiki/" + value
    prefix(config.nav)
    if not any(item.get("All documents") == "README.md" for item in config.nav):
        config.nav.append({"All documents": "README.md"})
    if not any(isinstance(value, RepositoryExtension) for value in config.markdown_extensions):
        config.markdown_extensions.append(RepositoryExtension())
    return config


def publish(file, config):
    # Keep guide URLs stable; engineering docs get their own namespace to
    # avoid collisions with existing guide paths such as architecture/.
    uri = file.src_uri
    if uri.startswith("wiki/"):
        published = uri.removeprefix("wiki/")
    elif uri == "README.md":
        published = "documentation/index.md"
    else:
        published = "engineering/" + uri
    mirror = File(published, config.docs_dir, config.site_dir, config.use_directory_urls)
    file.dest_uri = mirror.dest_uri
    if (file.is_documentation_page() and uri != "README.md"
            and (not uri.startswith("wiki/") or group(Path(uri)) == "Earlier entry points")
            and file.inclusion.is_included()):
        file.inclusion = InclusionLevel.NOT_IN_NAV


def on_files(files, config):
    authored_files.clear()
    source_hashes.clear()
    destinations = set()
    for file in files:
        if file.src_dir is None or Path(file.src_dir) != Path(config.docs_dir):
            continue
        publish(file, config)
        if file.inclusion.is_included():
            if file.dest_uri in destinations:
                raise ValueError(f"Documentation output collision: {file.dest_uri}")
            destinations.add(file.dest_uri)
            authored_files.append(file)
            source_hashes[file.src_uri] = digest(Path(file.abs_src_path))
    return files


def on_page_markdown(markdown, page, config, files):
    root = root_directory(config)
    source = Path(page.file.abs_src_path) if page.file.abs_src_path else None
    extension = next(value for value in config.markdown_extensions if isinstance(value, RepositoryExtension))
    extension.context = (source, root, config.repo_url)
    return markdown


def on_post_build(config):
    root = root_directory(config)
    site = Path(config.site_dir)
    # Resolve with the same hook mapping as the build, and cover every authored
    # page, including pages reachable through the portable index instead of nav.
    entries = []
    for path in sources(root):
        uri = path.relative_to(root / "docs").as_posix()
        file = File(uri, config.docs_dir, config.site_dir, config.use_directory_urls)
        publish(file, config)
        output = site / file.dest_uri
        entries.append({"source": "docs/" + uri, "source_sha256": source_hashes[uri],
                        "output": file.dest_uri, "output_sha256": digest(output)})
    assets = [{"source": "docs/" + file.src_uri, "source_sha256": source_hashes[file.src_uri],
               "output": file.dest_uri, "output_sha256": digest(site / file.dest_uri)}
              for file in authored_files if not file.is_documentation_page()
              and not file.src_uri.startswith("wiki/editor/")]
    (site / "documentation-sources.json").write_text(
        json.dumps({"version": 1, "pages": entries, "assets": assets}, indent=2) + "\n", encoding="utf-8")
