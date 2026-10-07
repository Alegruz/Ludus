# Ludus Wiki: development and publication

Ludus Wiki renders the Markdown documentation in `docs/`: tutorials/how-to
guides, engineering designs, decisions, evidence and references. Each document
has one editable source shared by the website and offline readers. The expected project-site address is
`https://alegruz.github.io/Ludus/` once Pages deploys successfully.

## Source and tooling

- `docs/`: canonical authored Markdown and local documentation assets.
- `docs/wiki/`: task guides, learning paths and small site assets within that tree.
- `docs/README.md`: generated portable navigation to every authored document.
- `mkdocs.yml`: explicit navigation, theme, search and strict validation.
- `docs/wiki-requirements.txt`: pinned documentation dependencies.
- `scripts/check-wiki`: generated artifact/source-link checks.
- `scripts/check-docs`: local index and portable documentation-link checks.
- `scripts/package-docs`: checked Markdown/HTML offline bundle; no downloads.
- `scripts/python/wiki_docs_hook.py`: render the source tree and record source/output hashes.
- `out/wiki/`: ignored build output and the sole Pages artifact root.

MkDocs/Material are documentation-only Python dependencies. They do not enter
the native tooling venv, Conan recipe, engine modules or installed SDK. System
fonts and local search avoid a runtime dependency on external font/search APIs.
The maintained Material layout supplies navigation, responsive behavior,
light/dark reading, code-copy controls and source-edit links. Small stylesheet
overrides establish a professional reading surface without choosing the final
editor art direction.

## Preview locally

No native build or Qt installation is needed. From the repository root, install
the pinned documentation tools in a dedicated venv once:

```bash
python3 -m venv out/wiki-venv
out/wiki-venv/bin/python -m pip install -r docs/wiki-requirements.txt
out/wiki-venv/bin/python -m mkdocs serve
```

Open the local URL printed by MkDocs. The preview reuses generated API pages
when they exist; regenerate them after changing headers/comments.

## Build and check

```bash
out/wiki-venv/bin/python scripts/check-docs --fix-index
out/wiki-venv/bin/python scripts/check-docs
out/wiki-venv/bin/python -m mkdocs build --strict
out/wiki-venv/bin/python scripts/build-api --bootstrap
out/wiki-venv/bin/python scripts/check-wiki --documentation-only
out/wiki-venv/bin/python scripts/package-docs
```

Run Doxygen after the initial MkDocs build. `--bootstrap` explicitly downloads
its pinned Linux x64 executable. On another host supply the pinned version with
`--doxygen /path/to/doxygen`; see [API documentation](api-reference.md).
The documentation check validates the site without the separate editor application.
For the complete application/site artifact, follow [the browser editor build](browser-editor.md)
and run `scripts/check-wiki` after adding its validated payload. Publication does
that full check in CI.

## Deployment contract

`.github/workflows/wiki.yml` builds on pull requests, `main` pushes and manual
dispatch. It runs strict Markdown/anchor checks, validates local rendered links,
assets, repository source targets and the search index, then uploads `out/wiki`.
Only a non-PR run on `refs/heads/main` enters the Pages deployment job. The build
has read-only repository permissions; Pages/OIDC write permissions belong only
to deployment. Actions are pinned to verified official release commits.
Superseded PR runs can cancel; production runs are serialized without cancelling
an active deployment.

The artifact renders every authored Markdown document directly. Engineering
documents appear under `engineering/`, existing guide URLs stay stable, and
the **All documents** index links to both. MkDocs edit links point to each
canonical source. `api-main.md` is a Doxygen input, rendered by the API generator;
the Doxygen configuration, coverage baseline and Python requirements are not
reader pages. These exclusions are tooling inputs, not alternative documentation.
Keep private material and local reference PDFs outside `docs/`.

The workflow also uploads `ludus-docs-offline.zip` as the **ludus-docs-offline**
Actions artifact. It contains the same sources, documentation assets and checked
HTML site. It omits the separate browser editor executable payload.

## First publication

1. Set repository **Settings → Pages → Source → GitHub Actions**.
2. Leave Custom domain blank and enable Enforce HTTPS when available.
3. Optionally restrict the `github-pages` environment's deployment branches to `main`.
4. Merge the reviewed wiki changes. Alternatively, manually run **Ludus Wiki**
   on `main` after the workflow is present there.
5. Confirm both build and deployment succeed. Use the deployment job's returned
   URL; a local build or uploaded artifact alone does not prove publication.

No additional publishing credentials or purchased domain are required by this
workflow. It depends on Pages being enabled for the repository and the account's
plan/permissions. `configure-pages` does not silently enable repository settings.
If a custom domain is added later, update `site_url`, configure/verify its DNS in
Pages and rerun the workflow so canonical metadata matches.

## Checks and maintenance

The [public SDK reference](api-reference.md) is extracted with pinned Doxygen
after the first MkDocs build, then rendered from XML through the same MkDocs
theme into `out/wiki/api/` before combined validation/upload. Guides and API
symbols share the navigation, light/dark mode and local search index.
The API coverage gate tracks legacy gaps and rejects new undocumented symbols.

The HTML checker understands the `/Ludus/` project path and rejects missing local
assets/anchors, escapes outside the site, unexpected symlinks and a missing/empty
search index or unindexed content page. It also checks GitHub `blob/tree/edit/main`
links against the local checkout. External pages and GitHub-rendered source
anchors are not crawled.
Browser acceptance should cover navigation, a useful search, keyboard focus,
light/dark modes and a narrow viewport under the project-site base path.

For a local documentation build without the separate browser editor payload,
run `python scripts/check-wiki --documentation-only`. The publication workflow
uses the full check after downloading the validated editor artifact.

The build writes `documentation-sources.json`, mapping every authored document
to its rendered page with SHA-256 hashes of both files. The checker rejects
missing pages, duplicate mappings, changed sources and altered outputs. A build
is tied to its checkout, not silently compared to a newer online revision.

Update guides when shared CLI/Editor behavior changes. Requirements are pinned
including transitive packages; dependency updates are reviewed separately and
must rebuild/check/preview the site and refresh the copied theme/icon notices in
`docs/wiki/assets/` from the installed distribution. Code examples should be
checked against current command help, and design/evidence claims should retain
their stated scope.

## Author once

Lead with the reader's goal and supported environment. Give working steps, the
expected result, a recovery path and links to the owner of deeper contracts.
Module, sample and tool READMEs are short links to their documentation owners
under `docs/`; their instructions are published and bundled from those files.
`check-docs` rejects separate README guides, broken local links/anchors and setup
examples that the actual launcher grammar rejects, without running setup.
The document index groups usage guides, designs, decisions, reviews, plans and
validation records so readers can distinguish instructions from historical scope.


Find and update the existing document that owns a topic. Guides may lead readers
to detailed design contracts; link to those contracts instead of repeating them.
Do not create a separate wiki version of a document already in `docs/`. The
site renders its complete content and indexes it in the same search as guides.

Use relative `.md` links within `docs/`, including links between `docs/wiki/`
and `docs/architecture/` or `docs/development/`. Keep essential images local.
Markdown must remain understandable without a build, custom include syntax,
network access or a reader-specific plugin. Site styling, metadata and optional
diagrams may enhance it without becoming the only explanation. Repository code
links outside `docs/` become GitHub source links during rendering; offline code
reading requires the engine checkout. External papers remain citations.

After adding, deleting or renaming a page, run:

```bash
out/wiki-venv/bin/python scripts/check-docs --fix-index
```

Commit the updated navigation index alongside the source. CI checks the index,
strict Markdown/anchor validation and source/output coverage; it does not ask
contributors to maintain synchronized copies of prose.

## Offline reading

For raw documentation, obtain the repository or extract the documentation
bundle once. Open `docs/README.md` with any text/Markdown editor. VS Code can
preview it; Obsidian can open `docs/` as a vault. Relative document links and
images remain within that tree. Neither reading nor local search in an editor
requires the documentation build tools.

For the same browser presentation and search, extract the checked bundle and
serve its `site/` directory with Python's standard library:

```bash
python3 -m http.server 8000 --bind 127.0.0.1 --directory site
# Windows: py -m http.server 8000 --bind 127.0.0.1 --directory site
```

Open `http://localhost:8000/`. This uses no internet, dependency installation or
documentation generation. Search, styles and fonts are local. A local server is
needed for browser search; opening the directory through `file://` is not a
supported reading mode. External citations, GitHub actions and the browser
editor launcher retain their online destinations.

To create a bundle from an already built/checked checkout:

```bash
out/wiki-venv/bin/python scripts/package-docs
```

The command verifies source parity and links before writing
`out/ludus-docs-offline.zip`; it does not build or download anything. The
application launcher is replaced with a small link to the hosted editor;
authored documentation pages remain identical to the checked site. Initial
tool installation and `build-api --bootstrap` need internet unless tools and
dependencies have already been supplied. A transferred prebuilt bundle needs
neither. API contracts are extracted from header comments into the same site;
the Markdown sources remain the authored prose, not a second API inventory.

## Site credits


The documentation layout uses [Material for MkDocs](https://squidfunk.github.io/mkdocs-material/)
by Martin Donath. The distributed theme retains its
[MIT license](../wiki/assets/material-license.txt), the
[Material Design icon notice](../wiki/assets/material-icons-license.txt), and the
[Font Awesome notices](../wiki/assets/fontawesome-license.txt). Generated JavaScript
and source maps retain their upstream notices. The Ludus mark and small theme
overrides are authored in this repository.

## Consulted implementation references

Thanks to **Martin Donath / Material for MkDocs**, [Getting started](https://squidfunk.github.io/mkdocs-material/getting-started/),
[Setting up site search](https://squidfunk.github.io/mkdocs-material/setup/setting-up-site-search/),
[Changing the fonts](https://squidfunk.github.io/mkdocs-material/setup/changing-the-fonts/)
and [Customization](https://squidfunk.github.io/mkdocs-material/customization/):
we use its maintained Markdown theme, local index, documented system-font option
and a small variable-based stylesheet; no theme implementation was copied.
Thanks to **MkDocs**, [Configuration: validation](https://www.mkdocs.org/user-guide/configuration/#validation),
for strict document/anchor diagnostics.
Thanks to **the Python-Markdown authors**, [Extension API: Treeprocessors](https://python-markdown.github.io/extensions/api/#treeprocessors),
for resolving parsed repository/reference links without rewriting examples or
document prose. Private reference copies retain their citation labels in the
site; the original source Markdown can still open those copies in a local checkout.
Thanks to **GitHub**, [Using custom workflows with GitHub Pages](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages),
for the separate artifact/deployment contract adapted by the workflow.
