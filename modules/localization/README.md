# Localization: static UTF-8 catalogs

`Ludus::Localization` builds on native and Emscripten. This first implementation
delivers literal text, exact authored identities, offline translation validation,
immutable catalog leases and a portable cooked format. Dynamic ICU messages,
locale negotiation, schema-preserving language switches and Text paragraph
integration remain later phases of the [architecture](../../docs/architecture/localization.md).

No formatting grammar is interpreted: `{player}`, apostrophes, combining marks,
and whitespace remain literal bytes. Passing an ICU-profile source to this cooker
fails explicitly. UTF-8 validity does not imply font coverage, bidi or rendering.

## Author and cook

Source JSON uses profile `ludus-static-utf8-v1`. Each record contains a stable
Content-style key, positive `source_revision`, literal `text`, and nonempty
translator `context`. See [the example](tests/source.json). Keep whole labels or
sentences in one record; localize complete messages rather than joining words.

```sh
./scripts/localization export-review --source modules/localization/tests/source.json \
    --locale fr-FR --output out/localization/fr-FR.json
```

The export carries source wording, context and a SHA-256 review receipt. A
translator edits `text` and deliberately changes `status` from `needs-review` to
`approved`. Approval certifies this exact source text, context and revision.
Regenerating an export resets approval. The cooker never automatically approves
or copies source wording into translated text.

```sh
./scripts/localization cook --source modules/localization/tests/source.json \
    --translation out/localization/fr-FR.json --output out/localization/fr-FR.loc \
    --header out/localization/ui_keys.hpp --namespace game::l10n::ui
```

Without `--translation`, cook the source locale. Every translated key must have
current source provenance and approved text by default. `--allow-source-fallback`
explicitly allows missing/unapproved entries to use source text. It never accepts
stale receipts or malformed translation records. Each resolved entry reports
`Source`, `Translation` or `SourceFallback`; the catalog retains requested and
source locale labels. Fallback is baked offline, with no runtime fallback search.

The generated header contains only exact `MessageKey` constants. Translation,
context and wording edits leave it unchanged; schema/key edits rebuild consumers.
Default names derive from keys, e.g. `menu/play` becomes `kMenuPlay`. Ambiguous
names fail generation; provide a distinct PascalCase `symbol` suffix on the source
record. Use a distinct explicit C++ namespace for each domain's header.

Unknown fields, duplicate JSON members/keys, unpaired surrogates, embedded NUL,
excess nesting and limits fail with a diagnostic. Input files are never overwritten.
Outputs are deterministic, individually replaced atomically, and left untouched
when bytes are unchanged. All content/header validation completes before writes;
an I/O failure between two output replacements may leave different generations.
Treat the command's successful exit as the publication boundary.

Locale labels are project-configured ASCII labels with the shape
`[A-Za-z]{2,8}(-[A-Za-z0-9]{1,8})*`, bounded to 128 bytes. They are case-sensitive;
the codec does not canonicalize, validate the IANA registry, accept every BCP47
grandfathered/private tag, negotiate a language or consult OS preferences.

## Runtime use

Link `Ludus::Localization` and load bytes through the application's existing
Content/file owner. The runtime does no I/O, parsing of authoring JSON or logging.
Failures are explicit statuses plus cooked-byte offsets; presentation policy
chooses a visible missing-text marker and rate-limited diagnostics.

```cpp
#include <ludus/localization/catalog.hpp>

using namespace ludus::localization;
Catalog active;
Diagnostic diagnostic;
// `bytes` is a borrowed std::span<const ludus::foundation::uint8>.
const auto status = PrepareCatalog(bytes, ludus::foundation::GetSystemAllocationDomain(),
                                   active, diagnostic);
if (status == Status::Ok)
{
    Catalog lease = active;
    MessageBinding play;
    ResolvedText text;
    if (lease.BindMessage({"game/ui", "menu/play"}, play) == Status::Ok &&
        lease.ResolveStatic(play, text) == Status::Ok)
    {
        // Consume text.Text while `lease` remains alive.
    }
}
```

`PrepareCatalog` validates bounds, exact ordering, UTF-8 and provenance before
making one fallible shared allocation through `AllocationDomain`. It owns a copy
of the bytes and preserves the previous output on any failure. The domain/context
must outlive all leases. Copying/acquiring a lease allocates nothing; separate
copies may be read concurrently after synchronized publication. Access to the
same mutable handle requires caller synchronization.

Binding performs an exact domain comparison and binary search over readable keys.
`ResolveStatic` checks a non-reused process token and accesses one record in O(1),
with no allocation, hash table, formatting or locks. Keep bindings and leases in
the consumer and resolve when presentation changes. This is a complexity and
allocator-tested contract, not a throughput benchmark.

Each successful load has a fresh token, including identical files. Rebind after
replacement; cross-catalog handles fail with `WrongCatalog` and preserve the
result. Old leases retain their token and text. This deliberately conservative
first phase does **not** implement the full design's schema-compatible binding
reuse, context manager or visible-view language-switch transaction. An application
can prepare a candidate, bind all its visible labels, and swap its own state at a
safe UI boundary. Catalog copies are the lifetime mechanism, not a global service.

Borrowed strings are counted views and are not NUL-terminated. Keep at least one
lease alive through consumption. Public view-producing operations reject temporary
catalogs, but C++ cannot detect a view retained after destruction of the last lease.
Moved-from catalogs are invalid. Every failing binder/resolver preserves its output.

## Cooked wire format, v1

All integer fields are unsigned little-endian uint32, decoded with FoundationBase
bounded byte codecs. There are no native structs, pointers, alignment assumptions,
hash identities, compressed blocks or private SDK headers.

| Header byte offset | Field |
| --- | --- |
| 0 | Magic `LLOC` (`0x434f4c4c`) |
| 4 | File version, 1 |
| 8 | Exact total byte count |
| 12 | Message count, at most 65,536 |
| 16, 20, 24 | Domain, selected-locale, source-locale byte lengths, each at most 128 |
| 28 | Record width, 20 |
| 32 | Literal UTF-8 profile ID, 1 |
| 36, 40, 44 | Reserved, zero |

Immediately after the 48-byte header are the three counted ASCII labels, then
`count` records. Each record contains absolute key offset, key length, absolute
text offset, text length, and origin (`0` source, `1` translation, `2` source
fallback). Records are sorted strictly by exact ASCII key bytes. A source-locale
catalog accepts origin 0 only; other catalogs accept 1 or 2 only.

Payload follows the record directory. Each key and text is packed consecutively
in record order. The loader requires exact offsets, no overlapping slices/gaps,
and no trailing bytes; empty text is valid. Keys are at most 128 bytes, text at
most 64 KiB, and a package at most 32 MiB. Authoring JSON is capped at 8 MiB and
32 nested containers. Cooked text preserves scalar values without normalization.

The file is structurally validated, not authenticated. Approved-source SHA-256
receipts belong to the offline workflow and are not a runtime signature. Content
transport/package integrity remains the acquiring owner's responsibility. Future
pattern codecs must use a new profile/version and validate their own schema.

## Validation

Native Catch2 tests use production-cooker fixtures: source/translation/fallback,
exact binding, default/moved owners, transactional OOM, one-allocation preparation,
zero-allocation leases/resolution, concurrent retained readers, unaligned bytes,
every truncation, corrupt offsets/metadata/UTF-8 and a byte-mutation corpus.
Python tests validate source receipts, strict authoring, deterministic outputs,
limits, generated bindings and CLI failures. The same cooker fixtures and runtime
execute under wasm32 through the pinned SDK Node emulator. The installed SDK
consumer checks the public API and static link closure.

```sh
python3 -m unittest discover -s scripts/python -p test_localization.py -v
./scripts/test linux-clang-development
./scripts/test linux-clang-asan-ubsan
./scripts/check linux-clang-development --all
./scripts/test web-emscripten-development
./scripts/install-sdk linux-clang-development
```
