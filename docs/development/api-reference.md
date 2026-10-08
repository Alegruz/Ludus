# Generated public SDK reference

Doxygen extracts XML from Ludus's public headers; MkDocs renders the API pages
with the same Material theme and search as the rest of the wiki. It is documentation
tooling only: no engine link dependency or native SDK/toolchain configuration is
required. `config/doxygen_toolchain.json` pins the official executable version,
Linux x64 and macOS Apple Silicon/Intel archives from the official GitHub release
and SHA-256 checksums published by Doxygen.
Downloads identify the documentation client with an explicit User-Agent; the
checksum is verified before extracting or executing the binary.

On macOS 15 or later, `python scripts/build-api --bootstrap` automatically selects
Apple Silicon (`arm64`) or Intel (`x86_64`) using the Python process architecture.
An Intel Python running under Rosetta selects the Intel tool. Build MkDocs first
as described below; no Homebrew, disk-image mounting or system-wide installation
is needed. Archives and the executable stay in ignored `out/doxygen-tools/`.
Repeat runs verify and reuse the cached archive and restore the CLI from it,
including when its installed copy is missing or damaged. Failed downloads or
extraction leave an existing executable intact. Only the CLI member is extracted;
the GUI and search helpers are not installed. Other hosts may use
`python scripts/build-api --doxygen /path/to/doxygen` with the pinned version.
The release binaries target macOS 15, despite the upstream download page's
macOS 13+ label; older systems fail with a setup message before downloading or
replacing tools. Use an explicit compatible build of the pinned Doxygen version
on those systems. Plain previews never download Doxygen.

## Write caller contracts

Write descriptions beside public declarations in `///` or `/** ... */` comments.
Doxygen extracts the signature; explain the contract a caller needs.

| Item | Useful description |
| --- | --- |
| Class or struct | Purpose, ownership, lifetime and thread-affinity rules |
| Function or method | Inputs/units, return/status meanings, failure effects and preconditions |
| Field, enum value or constant | Meaning, units, valid bounds and sentinel behavior |
| Alias or macro | Intended use and any evaluation/build limitations |

Start multiline comments with `@brief` for the summary shown in member tables.
Use `@param`, `@return`, `@pre`, `@note` and `@warning` where appropriate.
Engine errors are explicit; do not add exception contracts. Preserve existing
source attribution and link lengthy explanations to their architecture owner.
For a concrete example, read
[`AllocationDomain`](https://github.com/Alegruz/Ludus/blob/main/modules/foundation/memory/include/ludus/foundation/memory/allocation_domain.hpp).

## Inputs and boundaries

`scripts/build-api` reads literal `PUBLIC`/`INTERFACE FILE_SET public_headers`
lists from module CMake files. It fails on unsupported file-set expressions,
missing headers or include-tree headers absent from the declared file sets.
This intentionally narrow reader is not a general CMake evaluator. Changing
header registration to globs, new generated inputs or complex expressions requires
updating it with regressions. It never scans source/private/third-party directories
as Doxygen inputs and excludes installed `detail`/`internal` helper headers.

Three generated Foundation headers are represented by their checked-in templates:
`build_metadata.hpp.in`, `assert_config.hpp.in`, and `profiling_config.hpp.in`.
They expose vocabulary, not concrete configured values. Their `@...@` placeholders
are explicitly identified on the reference landing page. Preprocessing is disabled
to inventory native/browser declaration branches together; this is source reference,
not a compiler-verified symbol set for a particular SDK profile. The renderer
excludes private/protected members; `detail`/`internal` namespaces are excluded
by Doxygen configuration. Whole-header source listings and private members are
never rendered from the XML; public declaration initializers remain visible.

## Build and publication

Use [the documentation build commands](wiki.md#build-and-check): build MkDocs,
run `scripts/build-api` with the pinned Doxygen, and check/package the combined
documentation. Publication adds the separately validated editor payload before
the full `scripts/check-wiki` gate.
The generator writes `out/api-reference/{xml,markdown,coverage.json}`, then rebuilds
MkDocs with those generated pages through `scripts/python/wiki_api_hook.py`.
Only the resulting site in `out/wiki/` is published. XML, generated Markdown,
machine-local inputs and the baseline report are not part of the public site.
Relative URLs make the combined artifact usable under GitHub Pages' `/Ludus/`
project path without a separate deployment.

The wiki's API overview links to the generated reference using a relative URL,
so it also works in a locally served combined artifact. `scripts/build-api`
renders XML into Markdown with escaped HTML for structured descriptions, parameter
tables, return/precondition/warning callouts and linked declarations. API pages
use the wiki's header, sidebar, table of contents, code highlighting and light/dark
mode. The API navigation section provides namespace, type, concept and header
indexes. Compound filenames and member/enum-value anchors retain their existing
Doxygen URLs. The combined artifact checker requires return links on every API
page and a forward
link from the wiki overview; broken or removed navigation fails publication.

The existing Pages workflow performs the build, regression checks and combined
link validation before uploading `out/wiki/`. PRs validate without publishing;
`main` publishes the wiki and API reference together. One local search indexes
guides and API symbols. Generated pages link to declaring public headers and omit
edit links to fictional wiki source files. No generated HTML is committed.

Plain MkDocs builds and previews reuse the last generated Markdown when available;
they do not run Doxygen or download tools. Run `scripts/build-api` again after
header/comment changes. Without that output, MkDocs builds just the authored wiki;
the combined artifact checker rejects the missing reference before publication.
The renderer fails on unsupported documentation XML elements rather than silently
discarding part of a contract; extend its presentation and regression tests when
introducing a new structured documentation construct.

## Coverage and validation

`EXTRACT_ALL=YES` ensures undocumented declarations remain visible. Doxygen's
undocumented warning mode cannot be combined with that setting, so a separate
XML gate compares missing descriptions against `docs/api-undocumented.json`.
Keys use qualified names/kinds/signatures rather than generated Doxygen IDs.
Classes/structs/unions/concepts, public members, aliases/macros/constants and enum values
are covered. Namespace/file descriptions are not required.

CI rejects new gaps, changed undocumented signatures and removal of previously
documented descriptions. Existing gaps are visible in the coverage report;
Resolved entries must be removed in the change that documents them; reviewed
baseline maintenance also removes deleted symbols. Parameter
documentation errors are handled by Doxygen warnings-as-errors; this description
gate does not prove semantic completeness or automatically require every parameter.
No blanket conversion of plain comments or invented contracts is performed.

`out/api-reference/coverage.json` reports gaps and resolved/removed entries.
Remove resolved baseline entries with the change that documents them. The
explicit `--update-baseline` switch is for a reviewed migration; do not use it
to make failing coverage checks pass.

The combined HTML checker checks API local links/assets/anchors and requires
MkDocs search coverage for every wiki and API page.
Tests inject missing headers, unknown generated inputs, removed descriptions,
undocumented enum values, private members, overload/enum-value anchors,
structured contracts and broken/unindexed generated pages.

## Consulted references

Thanks to **Dimitri van Heesch / Doxygen**, [Configuration](https://www.doxygen.nl/manual/config.html)
and [Documenting the code](https://www.doxygen.nl/manual/docblocks.html), for the
extraction, structured comment, XML and warning contracts adopted here.
Its [Customizing the output: Using the XML output](https://www.doxygen.nl/manual/customize.html#xml)
informs the separation between declaration extraction and site presentation.
Thanks to **MkDocs**, [Plugins](https://www.mkdocs.org/dev-guide/plugins/)
and [API Reference](https://www.mkdocs.org/dev-guide/api/#mkdocs.structure.files.File.generated),
for the hooks and generated-file interfaces used to join the normal site build.
The [official download page](https://www.doxygen.nl/download.html) supplies the
pinned archive checksums. No upstream implementation code is copied.
