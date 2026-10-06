# Improve this wiki

The wiki is Markdown in `docs/wiki/`. Navigation lives in `mkdocs.yml`; the
generated HTML lives under ignored `out/wiki/`. The public site is built from
reviewed `main` changes. Every page has an edit link to its source on GitHub.

## Preview locally

No native engine build or Qt installation is needed to work on the wiki.
From the repository root:

```bash
python3 -m venv out/wiki-venv
out/wiki-venv/bin/python -m pip install -r docs/wiki-requirements.txt
out/wiki-venv/bin/python -m mkdocs serve
```

Open the local URL printed by MkDocs. Use the venv interpreter explicitly so the
wiki's dependencies stay separate from engine build tools and system Python.

## Build and check

```bash
out/wiki-venv/bin/python -m mkdocs build --strict
out/wiki-venv/bin/python scripts/build-api --bootstrap
out/wiki-venv/bin/python scripts/check-wiki
```

Strict builds reject missing Markdown destinations and anchors. The artifact
check validates generated local links/assets, search destinations and repository
source paths. It does not crawl third-party websites or prove remote pages will
remain available.

Build the API after MkDocs, which cleans the artifact directory. The explicit
bootstrap downloads a hash-pinned official Linux x64 Doxygen binary into `out/`;
use `--doxygen /path/to/doxygen` on other hosts. See
[Document public APIs](api-reference.md) for structured comments and coverage.

## Write around a task

Lead with the goal and supported environment. Give the smallest working steps,
the expected result, a recovery path and links to deeper references. Prefer
short explanations over API dumps. Update existing pages before creating a
duplicate workflow. Register every new page in navigation.

Use relative `.md` links between wiki pages. Link implementation/design files to
their repository paths. Keep local PDFs, machine paths, build artifacts and
credentials outside the published source directory.

## Review and publication

A pull request builds and checks the wiki without publishing. Pushes to `main`
publish the checked `out/wiki` artifact through GitHub Pages. Manual workflow
dispatch can rebuild/deploy `main`; dispatch on another branch only checks it.

Repository Pages settings should use **GitHub Actions**, no custom domain
initially, and **Enforce HTTPS**. An administrator can restrict the `github-pages`
environment to `main`. See the
[deployment/maintenance guide](https://github.com/Alegruz/Ludus/blob/main/docs/development/wiki.md)
for first publication and troubleshooting.

## Site credits

The documentation layout uses [Material for MkDocs](https://squidfunk.github.io/mkdocs-material/)
by Martin Donath. The distributed theme retains its
[MIT license](../assets/material-license.txt), the
[Material Design icon notice](../assets/material-icons-license.txt), and the
[Font Awesome notices](../assets/fontawesome-license.txt). Generated JavaScript
and source maps retain their upstream notices. The Ludus mark and small theme
overrides are authored in this repository.
