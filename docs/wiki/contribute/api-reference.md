# Document public APIs

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

## Build the combined site

Prepare the wiki venv as described in [Improve this wiki](wiki.md), then run:

```bash
out/wiki-venv/bin/python -m mkdocs build --strict
out/wiki-venv/bin/python scripts/build-api --bootstrap
out/wiki-venv/bin/python scripts/check-wiki
```

The bootstrap explicitly downloads a hash-pinned official Linux x64 Doxygen
binary into ignored `out/doxygen-tools/`. On another host, install the pinned
version and pass `--doxygen /path/to/doxygen` instead. There is no native engine
build, compiler dependency or global installation for documentation generation.

Run the API generator after the initial MkDocs build. Doxygen extracts XML,
the generator creates ignored Markdown pages, and MkDocs rebuilds the combined
artifact with its normal theme and shared search. Publish through the existing
Pages workflow; relative links are compatible with `/Ludus/`.
Plain MkDocs builds/previews reuse the last generated API pages without rebuilding
Doxygen. Regenerate after changing headers or comments.

```bash
out/wiki-venv/bin/python -m http.server 8000 --directory out/wiki
```

Open `http://localhost:8000/reference/` and follow the generated API link. Check
sidebar navigation, symbol search, light/dark modes and a narrow viewport on a
symbol page as well as the API landing page.

## Keep the backlog shrinking

`docs/api-undocumented.json` is the reviewed initial list of missing descriptions.
The XML-based check rejects new missing symbols and removed existing descriptions,
including enum values. Changing an undocumented signature requires documenting
it rather than adding its new key to the baseline.

The report in `out/api-reference/coverage.json` lists gaps and resolved/removed
entries. Remove resolved baseline entries in the change that documents them, so later
removal of those descriptions also fails the gate. The explicit
`--update-baseline` switch is for a reviewed migration, never a fix for failing CI.
Brief/detail description coverage is not proof that every parameter, unit or
ownership guarantee is correct; reviewers still check the contract.

Read the [tooling contract](../../development/api-reference.md)
for public-header inventory limits and generated configuration templates.
