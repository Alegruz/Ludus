"""Render the last generated Doxygen XML reference in the wiki's native theme.

Thanks to MkDocs, "Plugins", on_config/on_files/on_nav/on_pre_page, and
"API Reference", File.generated: generated sources join normal navigation,
rendering and search without copying them into the checked-in wiki tree.
https://www.mkdocs.org/dev-guide/plugins/
https://www.mkdocs.org/dev-guide/api/#mkdocs.structure.files.File.generated
"""
from pathlib import Path

from mkdocs.structure.files import File, InclusionLevel
from mkdocs.structure.nav import Section


def generated_directory(config):
    return Path(config.config_file_path).parent / "out/api-reference/markdown"


def on_config(config):
    if not (generated_directory(config) / "api/index.md").is_file():
        return config
    for item in config.nav:
        if item.get("API reference") == "reference/index.md":
            item["API reference"] = [
                {"Overview": "reference/index.md"},
                {"Browse API": "api/index.md"},
                {"Namespaces": "api/namespaces.md"},
                {"Classes and structs": "api/annotated.md"},
                {"Concepts": "api/concepts.md"},
                {"Header files": "api/files.md"},
            ]
    return config


def on_files(files, config):
    directory = generated_directory(config)
    if not (directory / "api/index.md").is_file():
        return files
    for path in sorted(directory.rglob("*.md")):
        uri = path.relative_to(directory).as_posix()
        if files.get_file_from_path(uri) is not None:
            raise ValueError(f"Generated API page conflicts with wiki source: {uri}")
        file = File.generated(config, uri, abs_src_path=str(path), inclusion=InclusionLevel.NOT_IN_NAV)
        # Retain the published Doxygen compound URLs, including deep links.
        file.use_directory_urls = False
        files.append(file)
    return files


def on_nav(nav, config, files):
    section = next((item for item in nav.items
                    if isinstance(item, Section) and item.title == "API reference"), None)
    for file in files.documentation_pages():
        if file.src_uri.startswith("api/") and file.page.parent is None:
            file.page.parent = section
    return nav


def on_pre_page(page, config, files):
    if page.file.src_uri.startswith("api/"):
        # Generated pages link to their declaring headers; no fictional edit path.
        page.edit_url = None
    return page
