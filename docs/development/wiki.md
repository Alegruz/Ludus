# Ludus Wiki: development and publication

Ludus Wiki is a static user documentation site: tutorials/how-to guides,
explanations, terminology and references. It is an additional pre-S2 increment,
not a browser engine/editor runtime. The expected project-site address is
`https://alegruz.github.io/Ludus/` once Pages deploys successfully.

## Source and tooling

- `docs/wiki/`: only public wiki content and small local assets.
- `mkdocs.yml`: explicit navigation, theme, search and strict validation.
- `docs/wiki-requirements.txt`: pinned documentation dependencies.
- `scripts/check-wiki`: generated artifact/source-link checks.
- `out/wiki/`: ignored build output and the sole Pages artifact root.

MkDocs/Material are documentation-only Python dependencies. They do not enter
the native tooling venv, Conan recipe, engine modules or installed SDK. System
fonts and local search avoid a runtime dependency on external font/search APIs.
The maintained Material layout supplies navigation, responsive behavior,
light/dark reading, code-copy controls and source-edit links. Small stylesheet
overrides establish a professional reading surface without choosing the final
editor art direction.

```bash
python3 -m venv out/wiki-venv
out/wiki-venv/bin/python -m pip install -r docs/wiki-requirements.txt
out/wiki-venv/bin/python -m mkdocs build --strict
out/wiki-venv/bin/python scripts/build-api --bootstrap
out/wiki-venv/bin/python scripts/check-wiki
out/wiki-venv/bin/python -m mkdocs serve
```

## Deployment contract

`.github/workflows/wiki.yml` builds on pull requests, `main` pushes and manual
dispatch. It runs strict Markdown/anchor checks, validates local rendered links,
assets, repository source targets and the search index, then uploads `out/wiki`.
Only a non-PR run on `refs/heads/main` enters the Pages deployment job. The build
has read-only repository permissions; Pages/OIDC write permissions belong only
to deployment. Actions are pinned to verified official release commits.
Superseded PR runs can cancel; production runs are serialized without cancelling
an active deployment.

The artifact contains no repository-wide sweep, engine binaries, local reference
PDFs or generated native state. Existing engine docs remain engineering sources;
the wiki explains user tasks and links back to those owners instead of copying
every architecture/evidence file into a public navigation tree.

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

The [public SDK reference](api-reference.md) is generated with pinned Doxygen
after MkDocs and copied into `out/wiki/api/` before combined validation/upload.
It uses its own symbol search; wiki search continues indexing MkDocs content.
The API coverage gate tracks legacy gaps and rejects new undocumented symbols.

The HTML checker understands the `/Ludus/` project path and rejects missing local
assets/anchors, escapes outside the site, unexpected symlinks and a missing/empty
search index or unindexed content page. It also checks GitHub `blob/tree/edit/main`
links against the local checkout. External pages and GitHub-rendered source
anchors are not crawled.
Browser acceptance should cover navigation, a useful search, keyboard focus,
light/dark modes and a narrow viewport under the project-site base path.

Update guides when shared CLI/Editor behavior changes. Requirements are pinned
including transitive packages; dependency updates are reviewed separately and
must rebuild/check/preview the site and refresh the copied theme/icon notices in
`docs/wiki/assets/` from the installed distribution. Code examples should be
checked against current command help, and design/evidence claims should retain
their stated scope.

## Consulted implementation references

Thanks to **Martin Donath / Material for MkDocs**, [Getting started](https://squidfunk.github.io/mkdocs-material/getting-started/),
[Setting up site search](https://squidfunk.github.io/mkdocs-material/setup/setting-up-site-search/),
[Changing the fonts](https://squidfunk.github.io/mkdocs-material/setup/changing-the-fonts/)
and [Customization](https://squidfunk.github.io/mkdocs-material/customization/):
we use its maintained Markdown theme, local index, documented system-font option
and a small variable-based stylesheet; no theme implementation was copied.
Thanks to **MkDocs**, [Configuration: validation](https://www.mkdocs.org/user-guide/configuration/#validation),
for strict document/anchor diagnostics.
Thanks to **GitHub**, [Using custom workflows with GitHub Pages](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages),
for the separate artifact/deployment contract adapted by the workflow.
