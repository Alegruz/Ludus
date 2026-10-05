#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/foundation/memory/allocation_domain.hpp>
#include <ludus/foundation/reflection/schema.hpp>

#include <span>
#include <string_view>

namespace ludus::foundation::serialization
{
inline constexpr usize MAX_DOCUMENT_BYTES = 65536;

// Strict scalar authored envelope: schema UUID, exact version, values object.
// Unknown/nonpersistent/duplicate fields fail; absent persistent fields use
// that exact schema version's declared defaults. No migrations are implicit.
// Decode uses one bounded fallible parser pool and bounded stack staging.
// Failure leaves output unchanged. Nonpersistent slots contain schema defaults;
// generated typed decoders preserve the corresponding native members instead.
[[nodiscard]] reflection::Status ReadJson(const reflection::Schema& schema,
                                          std::string_view input,
                                          std::span<reflection::Value> output,
                                          const AllocationDomain& domain,
                                          reflection::Diagnostic& error) noexcept;

// Writes every persistent field in declaration order, exact integers and
// finite floats. Storage can contain a failed prefix, but output is published
// only on success. Caller must not alias storage with the values/metadata.
[[nodiscard]] reflection::Status WriteJson(const reflection::Schema& schema,
                                           std::span<const reflection::Value> values,
                                           std::span<uint8> storage,
                                           std::span<const uint8>& output,
                                           reflection::Diagnostic& error) noexcept;
} // namespace ludus::foundation::serialization
