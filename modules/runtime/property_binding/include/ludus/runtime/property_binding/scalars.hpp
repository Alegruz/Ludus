#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/foundation/reflection/schema.hpp>
#include <ludus/runtime/game_api/properties.h>

#include <span>

namespace ludus::runtime::property_binding
{
// Optional module-side projection into the EXISTING copied GameApi records.
// Only bool/int32/float32 fit the current inspector ABI. Other schema kinds
// fail explicitly; do not narrow wide integers or rewrite the public ABI.
// No descriptor, native address, or callback crosses the module boundary.
// Object identity, module lifetime, revisions, safe-point commit and undo remain
// the owner's responsibility. All output spans remain unchanged on failure.
[[nodiscard]] foundation::reflection::Status Describe(const foundation::reflection::Schema& schema,
                                                      foundation::uint64 objectId,
                                                      std::span<game_api::PropertyDescriptor> output,
                                                      foundation::reflection::Diagnostic& error) noexcept;
[[nodiscard]] foundation::reflection::Status Snapshot(const foundation::reflection::Schema& schema,
                                                      std::span<const foundation::reflection::Value> values,
                                                      foundation::uint64 objectId,
                                                      foundation::uint64 revision,
                                                      std::span<game_api::PropertyValue> output,
                                                      foundation::reflection::Diagnostic& error) noexcept;
[[nodiscard]] foundation::reflection::Status DecodeEdits(const foundation::reflection::Schema& schema,
                                                         std::span<const game_api::PropertyEdit> input,
                                                         foundation::uint64 objectId,
                                                         foundation::uint64 revision,
                                                         std::span<foundation::reflection::Edit> output,
                                                         foundation::reflection::Diagnostic& error) noexcept;
} // namespace ludus::runtime::property_binding
