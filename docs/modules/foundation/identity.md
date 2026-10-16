# FoundationIdentity

Typed runtime handle values, persistent UUID values and their codecs, slot
generation advancement, and the process lifetime-token source. Link
`Ludus::FoundationIdentity`; it depends only on FoundationBase and builds for
native and browser targets. Engine code is exception-free and allocation-free.
See the [architecture](../../architecture/object-handles-uuid.md) for the full
identity and lifetime contract.

```cpp
#include <ludus/foundation/identity/handle.hpp>
#include <ludus/foundation/identity/lifetime.hpp>
#include <ludus/foundation/identity/uuid.hpp>

namespace identity = ludus::foundation::identity;

struct LightTag;
using LightHandle = identity::Handle<LightTag>;

bool ReadAssetId(const char* text, ludus::foundation::usize size, identity::AssetId& output) noexcept
{
    identity::Uuid value;
    if (identity::TryParseUuid(text, size, identity::UuidPolicy::RequireVersion4, value) !=
        identity::UuidStatus::Success)
    {
        return false; // output is unchanged
    }
    output = identity::AssetId{value};
    return true;
}

bool StartRegistry(ludus::foundation::uint64& owner) noexcept
{
    return identity::TryIssueLifetimeId(owner) == identity::LifetimeIdStatus::Success;
}
```

## Runtime handles

`Handle<Tag>` holds `Owner / Slot / Generation` as `uint64 / uint32 / uint32`,
the same layout as `EntityId`. Different tags are different types with no
conversion; the owner token distinguishes two registries of the same domain.
All-zero is null. Equality and `IsNull()` never say whether a handle is live:
only the issuing registry validates it. A handle does not own or pin anything.

Registries own their slot tables and use the shared rules: generations start at
`FIRST_GENERATION`, and `AdvanceGeneration` increments a released slot's
generation or reports `Retired` at `TERMINAL_GENERATION`, never wrapping.
Retired slots stay out of reuse for that owner and count against capacity.

`TryIssueLifetimeId` issues nonzero, strictly increasing `uint64` owner tokens
from a relaxed atomic counter, once per registry lifetime, and reports
`IdentityExhausted` instead of wrapping. A rebuilt registry obtains a new token.
The counter lives in this static library: an executable or shared module that
links it privately gets its own counter. Reloadable gameplay modules must get
tokens from the host instead of calling it themselves. Tokens are process-scoped
and are never serialized.

## Persistent UUIDs

`Uuid` stores 16 bytes in RFC 9562 network order. Never reinterpret it as native
integers or a platform GUID. `AssetId`, `SourceObjectId` and `InstanceId` wrap a
`Uuid` with separate domain types; `PersistentObjectKey` pairs an instance with
a source object (32 bytes). Construct a wrapper explicitly at a serialization
boundary and read its `Value` to write it back.

| Operation | Contract |
| --- | --- |
| `TryParseUuid(text, size, policy, output)` | Exactly 36 characters in `8-4-4-4-12` groups, case-insensitive hex. Rejects whitespace, braces, URN prefixes and misplaced separators. Output unchanged on failure. |
| `FormatUuid(value, text, capacity)` | Writes 36 lowercase characters; adds a terminator when `capacity >= UUID_TEXT_BUFFER_BYTES`. `InsufficientSpace` writes nothing. |
| `ValidateUuid(value, policy)` | Checks bytes read from a binary format. |
| `TryMakeUuidV4(bytes, size, output)` | Pure construction from exactly 16 caller-supplied random bytes. |
| `TryGenerateUuidV4(source, output)` | Calls the injected `EntropySource` once; `EntropyUnavailable` leaves no partial output. |

`UuidPolicy::RequireVersion4` accepts only a nonnil version-4 UUID, the only form
of durable identity. `AllowNilOrVersion4` also accepts nil, which marks an
absent optional reference. Other versions or variants are rejected with
`UnsupportedVersion` or `UnsupportedVariant`.

FoundationIdentity performs no entropy acquisition and has no fallback source.
Platform or tool owners supply an `EntropySource` over operating-system
entropy; tests inject controlled bytes. Uniqueness is probabilistic. An owner of
a project, import, save or package index must check new values against that
index and reject duplicates supplied by content. For a hash table, hash the
canonical bytes with FoundationHash; exact byte equality decides identity.

### Generating UUIDs with platform entropy

`Ludus::PlatformEntropy` provides the production source in
`<ludus/platform/entropy.h>`. It links only FoundationBase and
FoundationIdentity (publicly), so tools can use it without Platform's window
backends; `Ludus::Platform` re-exports it. Link either target and pass
`GetSystemEntropySource()` to `TryGenerateUuidV4`:

```cpp
#include <ludus/foundation/identity/uuid.hpp>
#include <ludus/platform/entropy.h>

namespace identity = ludus::foundation::identity;

bool MintAssetId(identity::AssetId& output) noexcept
{
    identity::Uuid value;
    if (identity::TryGenerateUuidV4(ludus::platform::GetSystemEntropySource(), value) !=
        identity::UuidStatus::Success)
    {
        return false; // EntropyUnavailable; output is unchanged
    }
    output = identity::AssetId{value};
    return true;
}
```

| Target | Source | Failure |
| --- | --- | --- |
| macOS, Linux | `getentropy(3)`, in requests of at most 256 bytes | The call reports an error; `TryGenerateUuidV4` returns `EntropyUnavailable`. |
| Browser (Emscripten) | Web Crypto `crypto.getRandomValues`, in requests of at most 256 bytes, called through a guarded bridge (Node's global Web Crypto object under Node) | A missing or throwing Web Crypto object fails the request instead of raising a JavaScript exception through wasm. |
| Other targets | None yet | Every nonempty request fails. |

The source is stateless and thread-safe; copy it freely. No target falls back to
wall time, addresses, `rand` or a gameplay PRNG. `TryFillSystemEntropy(bytes,
size)` exposes the same bytes for other security-sensitive callers; its
destination is unspecified after a failure. Linux may block briefly during early
boot until the kernel pool is initialized, so generate identities in tools,
import and save paths rather than inside frame-critical work.

## Status

This is the first stage of the
[implementation sequence](../../architecture/object-handles-uuid.md#implementation-sequence).
Existing `EntityId`, audio and RHI handles, and current content formats, are
unchanged. The native and browser platform entropy adapters are implemented in
`Ludus::PlatformEntropy`. SceneCook generates its UUIDv4 values with
`TryGenerateUuidV4` from an entropy source its callers inject
(`AssetOptions::EntropyOrNull`; see the
[architecture decision](../../architecture/object-handles-uuid.md#implementation-sequence)).
Registry alignment and UUID authoring records are later stages.
