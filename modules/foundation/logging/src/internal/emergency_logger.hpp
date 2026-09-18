#pragma once

#include <ludus/foundation/logging/category.hpp>
#include <ludus/foundation/logging/level.hpp>

#include <source_location>
#include <string_view>

namespace ludus::foundation::logging::internal {

// The emergency path (spec sections 25, 26). It exists to make logging safe in
// three windows where the normal backend cannot be trusted:
//   * before LogSystem::initialize()  (pre-init)
//   * after  LogSystem::shutdown()    (post-shutdown)
//   * during a Fatal / catastrophic failure
//
// Properties it deliberately upholds: no heap allocation, no async queue, no
// dependency on the logging worker, minimal locking, and a direct write to
// stderr (and the debugger where available). It never throws.
//
// `message` is already-formatted text; the emergency path performs no
// std::format work of its own so it stays allocation-free.
void EmergencyLog(LogLevel level,
                  LogCategory category,
                  std::string_view message,
                  const std::source_location& location) noexcept;

// Convenience wrapper for internal diagnostics that do not have a meaningful
// call site (e.g. "dropped N records"). Uses the current source location.
void EmergencyNote(std::string_view message,
                   const std::source_location& location = std::source_location::current()) noexcept;

} // namespace ludus::foundation::logging::internal
