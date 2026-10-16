#pragma once

#include <ludus/foundation/base/types.h>

// Private benchmark entry points; no installed SDK surface.
int RunStreamingBenchmark(int argumentCount, char** arguments);
void SetBenchmarkAllocationTracking(bool shouldTrack) noexcept;
ludus::foundation::uint64 GetBenchmarkAllocationCount() noexcept;
