#pragma once

// Thanks to K. Davis, B. Peabody and P. Leach, "Universally Unique IDentifiers
// (UUIDs)", RFC 9562, sections 4 (format and network byte order), 5.4 (UUIDv4)
// and 6.9 (unguessability), https://www.rfc-editor.org/rfc/rfc9562.html. Ludus
// adopts the 16-byte network-order layout, the version/variant fields and the
// 8-4-4-4-12 text form. Requiring v4 for durable identity, lowercase output and
// rejecting braces/URN prefixes are Ludus schema choices. Review:
// docs/architecture/object-handles-uuid-reference-review.md.
#include <ludus/foundation/base/compiler.h>
#include <ludus/foundation/base/types.h>

namespace ludus::foundation::identity
{
/// Number of bytes in a UUID's canonical binary form.
inline constexpr usize UUID_BYTE_COUNT = 16;
/// Number of characters in canonical `8-4-4-4-12` UUID text, without a terminator.
inline constexpr usize UUID_TEXT_LENGTH = 36;
/// Buffer size that holds canonical UUID text and a terminating zero.
inline constexpr usize UUID_TEXT_BUFFER_BYTES = UUID_TEXT_LENGTH + 1;
/// RFC 9562 version number of random UUIDs, the only version accepted for durable identity.
inline constexpr uint8 UUID_VERSION_RANDOM = 4;
/// Index of the byte whose high nibble holds the RFC 9562 version field.
inline constexpr usize UUID_VERSION_BYTE_INDEX = 6;
/// Index of the byte whose high bits hold the RFC 9562 variant field.
inline constexpr usize UUID_VARIANT_BYTE_INDEX = 8;
/// Mask selecting the two variant bits of the variant byte.
inline constexpr uint8 UUID_VARIANT_MASK = 0xC0;
/// Variant bits `10` of RFC 9562 UUIDs, positioned within UUID_VARIANT_MASK.
inline constexpr uint8 UUID_STANDARD_VARIANT_BITS = 0x80;

/// @brief A 128-bit UUID in canonical (RFC 9562 network) byte order.
///
/// Byte 0 is the first byte of the text form. The value is a plain byte array:
/// never reinterpret it as native-endian integers or a platform GUID structure.
/// Store exactly these 16 bytes in versioned binary formats. The all-zero nil
/// value is an explicit absent-reference sentinel and is never a valid identity.
/// Hash the bytes with FoundationHash when a table needs an accelerator; full-byte
/// equality, not a hash, decides identity.
struct Uuid final
{
    /// Canonical bytes. A raw array keeps this value header free of container
    /// headers (FoundationIdentity depends only on FoundationBase).
    uint8 Bytes[UUID_BYTE_COUNT] = {};

    /// Compare all 16 bytes exactly.
    [[nodiscard]] constexpr bool operator==(const Uuid&) const noexcept = default;

    /// Report whether every byte is zero (the nil UUID).
    [[nodiscard]] LUDUS_INLINE constexpr bool IsNil() const noexcept
    {
        for (const uint8 byte : Bytes)
        {
            if (byte != 0)
            {
                return false;
            }
        }
        return true;
    }

    /// Return the RFC 9562 version field (the high four bits of the version byte).
    /// Meaningful only when HasStandardVariant() is true.
    [[nodiscard]] LUDUS_INLINE constexpr uint8 GetVersion() const noexcept
    {
        return static_cast<uint8>(Bytes[UUID_VERSION_BYTE_INDEX] >> 4);
    }

    /// Report whether the variant field (the high two bits of the variant byte) is the RFC 9562 variant `10`.
    [[nodiscard]] LUDUS_INLINE constexpr bool HasStandardVariant() const noexcept
    {
        return (Bytes[UUID_VARIANT_BYTE_INDEX] & UUID_VARIANT_MASK) == UUID_STANDARD_VARIANT_BITS;
    }
};
static_assert(sizeof(Uuid) == UUID_BYTE_COUNT);

/// @brief A UUID tagged with its semantic domain.
///
/// Distinct tags produce distinct types, so an asset identity cannot be passed
/// where a source-object identity is expected. Construct one explicitly from a
/// validated Uuid (`AssetId{uuid}`) at a serialization boundary and read `Value`
/// to write it back. Equality compares the complete UUID.
/// @tparam Tag Domain marker type; it is never instantiated.
template <typename Tag>
struct TypedUuid final
{
    /// The underlying UUID; nil only for an absent optional reference.
    Uuid Value;

    /// Compare the complete UUID.
    [[nodiscard]] constexpr bool operator==(const TypedUuid&) const noexcept = default;

    /// Report whether this is the nil absent-reference sentinel.
    [[nodiscard]] LUDUS_INLINE constexpr bool IsNil() const noexcept
    {
        return Value.IsNil();
    }
};

/// Domain marker for AssetId.
struct AssetTag final
{
};
/// Domain marker for SourceObjectId.
struct SourceObjectTag final
{
};
/// Domain marker for InstanceId.
struct InstanceTag final
{
};

/// One logical authored asset. Survives moving the file or changing its contents.
using AssetId = TypedUuid<AssetTag>;
/// One authored object in a level or reusable source document, retained through edits and renames.
using SourceObjectId = TypedUuid<SourceObjectTag>;
/// One placement or saved instance scope; distinguishes repeated instances of one source.
using InstanceId = TypedUuid<InstanceTag>;

/// @brief Durable identity of one object within one instance scope.
///
/// Used in saves, undo records and durable object links. Ten placements of one
/// prefab share SourceObjectId values but have different InstanceId values, so
/// each key names a different object.
struct PersistentObjectKey final
{
    /// The placement or saved instance scope that contains the object.
    InstanceId Instance;
    /// The authored object within that scope.
    SourceObjectId Object;

    /// Compare both UUIDs exactly.
    [[nodiscard]] constexpr bool operator==(const PersistentObjectKey&) const noexcept = default;
};
static_assert(sizeof(PersistentObjectKey) == 2 * UUID_BYTE_COUNT);

/// Which UUID values an operation accepts.
enum class UuidPolicy : uint8
{
    /// Accept only a nonnil RFC 9562 version-4 UUID: an actual durable identity.
    RequireVersion4,
    /// Also accept the nil UUID, the explicit sentinel of an absent optional reference.
    AllowNilOrVersion4,
};

/// Result of a UUID validation, codec or construction operation.
enum class UuidStatus : uint8
{
    /// The operation succeeded and its output is available.
    Success,
    /// Text is not exactly canonical `8-4-4-4-12` hexadecimal, or an input has the wrong size.
    MalformedInput,
    /// The value is nil but the policy requires an actual identity.
    NilIdentity,
    /// The variant is RFC 9562 but the version is not 4.
    UnsupportedVersion,
    /// The variant field is not the RFC 9562 variant.
    UnsupportedVariant,
    /// The caller's buffer cannot hold UUID_TEXT_LENGTH characters.
    InsufficientSpace,
    /// The entropy source is missing or could not supply bytes.
    EntropyUnavailable,
};

/// @brief Check a decoded value against a policy.
///
/// Use this after reading 16 canonical bytes from a versioned binary format.
/// @return Success, NilIdentity, UnsupportedVariant or UnsupportedVersion.
[[nodiscard]] UuidStatus ValidateUuid(const Uuid& value, UuidPolicy policy) noexcept;

/// @brief Parse canonical UUID text.
///
/// The complete input must be exactly 36 characters: hexadecimal digits in
/// groups of 8-4-4-4-12 separated by `-`. Digits are case-insensitive. Whitespace,
/// braces, `urn:uuid:` prefixes, missing or extra separators and terminators
/// inside `size` are rejected. The decoded value is then checked with
/// ValidateUuid(). No allocation; uses no shared state.
/// @param text Input characters; may be null only when size is zero.
/// @param size Number of characters to parse.
/// @param policy Which decoded values are accepted.
/// @param outValue Receives the value on Success; unchanged on any failure.
/// @return Success, MalformedInput, or a ValidateUuid() failure.
[[nodiscard]] UuidStatus TryParseUuid(const char* text, usize size, UuidPolicy policy, Uuid& outValue) noexcept;

/// @brief Format a UUID as canonical lowercase `8-4-4-4-12` text.
///
/// Writes exactly UUID_TEXT_LENGTH characters. When capacity is at least
/// UUID_TEXT_BUFFER_BYTES, a terminating zero follows them. Any 16 bytes format;
/// formatting does not validate the version. No allocation.
/// @param value UUID to format.
/// @param text Destination; may be null only when capacity is zero.
/// @param capacity Size of the destination in bytes.
/// @return Success, or InsufficientSpace with the destination untouched.
[[nodiscard]] UuidStatus FormatUuid(const Uuid& value, char* text, usize capacity) noexcept;

/// @brief Build a version-4 UUID from exactly 16 random bytes.
///
/// Pure construction: sets the version and variant fields and keeps the other
/// 122 bits. The caller is responsible for the bytes coming from a
/// cryptographically secure source; never pass wall time, addresses or a
/// gameplay PRNG. Production code uses TryGenerateUuidV4().
/// @param randomBytes Input bytes; may be null only when size is zero.
/// @param size Number of bytes; must equal UUID_BYTE_COUNT.
/// @param outValue Receives the UUID on Success; unchanged on failure.
/// @return Success, or MalformedInput for a wrong size.
[[nodiscard]] UuidStatus TryMakeUuidV4(const uint8* randomBytes, usize size, Uuid& outValue) noexcept;

/// @brief Caller-supplied source of cryptographically secure random bytes.
///
/// Platform and tool owners provide adapters over the operating system's entropy
/// interface; tests inject controlled bytes. FoundationIdentity performs no
/// entropy acquisition of its own and has no fallback source.
struct EntropySource final
{
    /// Fill `size` bytes at `outBytes`; return false when entropy is unavailable.
    /// Called on the generating thread; must not throw. Null means unavailable.
    bool (*FillBytesOrNull)(void* contextOrNull, uint8* outBytes, usize size) noexcept = nullptr;
    /// Opaque state passed to FillBytesOrNull; owned by the caller and kept alive across the call.
    void* ContextOrNull = nullptr;
};

/// @brief Generate a fresh version-4 UUID from an entropy source.
///
/// Obtains 16 bytes with one call to the source, then applies TryMakeUuidV4().
/// Global uniqueness is probabilistic: owners of a project, save or package index
/// must still check the new value against that index before publishing it.
/// @param source Entropy provider; a null FillBytesOrNull is reported as unavailable.
/// @param outValue Receives the UUID on Success; unchanged when entropy fails, with no partial output.
/// @return Success or EntropyUnavailable.
[[nodiscard]] UuidStatus TryGenerateUuidV4(const EntropySource& source, Uuid& outValue) noexcept;
} // namespace ludus::foundation::identity
