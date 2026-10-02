#pragma once

#include <ludus/foundation/logging/category.hpp>

namespace ludus::audio
{
// Logging category for the audio control owner. Used privately on the
// owner/control thread only (lifecycle/failure summaries). Never from the
// device/worklet callback or the decode worker (design section 1, research
// "Repository fit").
inline constexpr ludus::foundation::logging::LogCategory LOG_AUDIO{"Audio"};
} // namespace ludus::audio
