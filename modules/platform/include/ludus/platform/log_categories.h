#pragma once

#include <ludus/foundation/logging/category.hpp>

namespace ludus::platform
{
// Logging category for the platform layer (windowing, display server
// integration). Engine modules declare their own categories at namespace
// scope so the id is computed at compile time (see FoundationLogging).
inline constexpr ludus::foundation::logging::LogCategory LOG_PLATFORM{"Platform"};
} // namespace ludus::platform
