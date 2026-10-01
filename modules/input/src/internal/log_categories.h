#pragma once

#include <ludus/foundation/logging/category.hpp>

namespace ludus::input
{
// Logging category for the input layer. Lifecycle/failure summaries only; never
// per-key formatting on the hot path (K10, K13).
inline constexpr ludus::foundation::logging::LogCategory LOG_INPUT{"Input"};
} // namespace ludus::input
