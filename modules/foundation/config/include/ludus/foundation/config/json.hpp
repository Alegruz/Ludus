#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/foundation/config/config.hpp>
#include <ludus/foundation/parsing/json.hpp>

namespace ludus::foundation::config
{
// V1 cooked layer: exact fields {version,schema,layer,values}; each record has
// {key,type,value,source,line}. 64-bit integers use canonical decimal strings to
// survive tools with binary64 JSON numbers. Unknown fields/types fail closed.
// The owner bumps schema identity when descriptor contracts change. Input is
// bounded to 256 KiB. This adapter owns no filesystem or schema registration.
inline constexpr usize MAX_BUNDLE_BYTES = 262144;
[[nodiscard]] Status DecodeValue(parsing::JsonValue input, Type type, Value& output) noexcept;
void EncodeValue(parsing::JsonWriter& writer, const Value& value) noexcept;
// On success leaves ONE replacement prepared; caller commits at its boundary.
// Failure never publishes a partial layer. Caller-supplied domain outlives call.
[[nodiscard]] Status PrepareJson(Context& context,
                                 std::string_view input,
                                 Layer expectedLayer,
                                 std::string_view schemaIdentity,
                                 uint64 expectedRevision,
                                 const AllocationDomain& domain,
                                 Diagnostic& error) noexcept;
// Sparse explicit assignments only (including explicit values equal to defaults).
// Output view is preserved on failure; caller buffer may contain a partial prefix.
[[nodiscard]] Status WriteLayer(const Context& context,
                                Layer layer,
                                std::string_view schemaIdentity,
                                std::span<uint8> buffer,
                                std::span<const uint8>& output) noexcept;
// Tool/Editor metadata uses the same descriptors as runtime validation.
[[nodiscard]] Status WriteSchema(const Context& context,
                                 std::string_view schemaIdentity,
                                 std::span<uint8> buffer,
                                 std::span<const uint8>& output) noexcept;
} // namespace ludus::foundation::config
