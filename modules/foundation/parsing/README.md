# FoundationParsing

Link `Ludus::FoundationParsing` for checked byte cursors, UTF-8 validation,
byte-based source locations, and bounded error records. Link the optional
`Ludus::FoundationParsingJson` for strict whole-document JSON over the pinned
private yyjson backend. Engine code is exception-free.

```cpp
#include <ludus/foundation/parsing/json.hpp>

namespace parsing = ludus::foundation::parsing;

bool ReadCount(std::string_view source, ludus::foundation::uint64& output) noexcept
{
    parsing::JsonDocument document;
    parsing::ParseError error;
    if (document.Read(source, error) != parsing::ParseStatus::Ok ||
        !document.Root().Fields({"count"}))
    {
        return false;
    }
    return document.Root().Get("count").Integer(output);
}
```

Document nodes and strings are borrowed. Read invalidates old views on every
attempt and exposes no root after failure. Input stays alive through Read and
cannot refer to the document's old strings. Typed consumers copy surviving
fields into an owned candidate and publish it only after domain validation.
No gameplay, asset loading, logging, or I/O occurs inside node accessors.

A document owns one AllocationDomain pool. The domain outlives the document.
Reset retains capacity, repeated Read reuses it, and Release returns it. Growth
frees old storage before allocating, bounding peak workspace; allocation failure
returns OutOfMemory. WorkspaceBytes reports retained bytes. Separate documents
can run concurrently; a single document needs external synchronization.

JsonLimits distinguishes input/workspace admission from post-DOM structural
validation. Container nesting is scanned before allocation with strings and
escapes respected. The root has depth zero. Limits apply to values excluding
keys, decoded string bytes including keys, array length, and object member
count. Depth 64 and 256 members are hard safety ceilings; defaults preserve
Content's depth 32 and 32 members. DOM-validation errors have no exact source
location and use UNKNOWN_BYTE_OFFSET. Syntax/encoding errors use byte offsets.

JsonWriter supplies primitives for domain-owned field ordering. Raw accepts
trusted syntax. String escapes controls and rejects invalid UTF-8; Number uses
the same pinned spelling and rejects nonfinite values. Finish appends one
newline, is idempotent, and leaves its output view unchanged on failure. The
caller owns its buffer and must not write into it through overlapping input.

See the [architecture](../../../docs/architecture/parsing.md) and
[Gems review](../../../docs/architecture/parsing-gems-review.md) for the broader
pipeline and staged follow-ups. There is no generic grammar framework, editor
AST, streaming reader, or cooker in this milestone.
