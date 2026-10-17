#pragma once

#include <ludus/foundation/base/types.h>
#include <ludus/foundation/profiling/profiling.hpp>
#include <ludus/foundation/threading/jobs.hpp>

/// @file
/// @brief Forwards JobSystem traces to FoundationProfiling (target `Ludus::FoundationThreadingProfiling`).
///
/// The scheduler kernel (`jobs.hpp`) knows nothing about the profiler. This
/// adapter supplies JobSystemHooks that register each worker as a named profiler
/// thread and, while a capture is active, record every submission as Perfetto
/// slices, flows and a ready-queue counter. The trace layout is described in
/// the "Execution traces" section of docs/modules/foundation/threading.md.

namespace ludus::foundation::threading
{
/// @brief Records a job label's profiler site so exported traces show its name.
///
/// Used by LUDUS_JOB_LABEL; call at most once per descriptor.
/// @param descriptor Site metadata with static storage duration.
/// @return The descriptor's site ID, so the call can initialize a function-local static.
[[nodiscard]] uint32 RegisterJobLabelSite(const profiling::ZoneDescriptor& descriptor) noexcept;

/// @brief Returns hooks that connect a JobSystem to the profiler.
///
/// Pass the result as JobSystemConfig::Hooks. Each worker registers with
/// profiling::RegisterThreadForTrace as "<prefix> <index>" during Initialize and
/// unregisters, publishing its partial trace chunk, during Shutdown. Each Submit
/// is traced only while a capture is active, and is then traced completely.
/// Builds with profiling compiled out return empty hooks, so the scheduler does
/// no tracing work.
/// @param workerNamePrefixOrNull Static NUL-terminated thread-name prefix; null selects "Job worker".
///        Names are truncated to the profiler's thread-name capacity.
/// @return Hooks whose context is the prefix pointer; it must outlive the system.
/// @note Worker partial chunks reach an export only when a chunk fills or the
///       worker unregisters, so shut the system down before exporting a capture
///       that must include every worker event.
[[nodiscard]] JobSystemHooks MakeProfilerJobHooks(const char* workerNamePrefixOrNull = nullptr) noexcept;
} // namespace ludus::foundation::threading

// LUDUS_JOB_LABEL(name): an expression yielding a JobLabel for the bare identifier
// `name`. Its site ID is the compile-time hash used by LUDUS_PROFILE_SCOPE, so the
// job's slices export under `name`; with profiling compiled in, the first
// evaluation registers that site. Evaluating it never allocates. (Plain comment:
// the API reference does not preprocess macros.)
#if LUDUS_PROFILING_ENABLED
#    define LUDUS_JOB_LABEL(name)                                                                                      \
        ([]() noexcept -> ::ludus::foundation::threading::JobLabel {                                                   \
            static constexpr ::ludus::foundation::profiling::ZoneDescriptor DESCRIPTOR{::std::string_view{#name}};     \
            static const ::ludus::foundation::uint32 SITE_ID =                                                         \
                ::ludus::foundation::threading::RegisterJobLabelSite(DESCRIPTOR);                                      \
            return {.NameOrNull = #name, .SiteId = SITE_ID};                                                           \
        }())
#else
#    define LUDUS_JOB_LABEL(name)                                                                                      \
        (::ludus::foundation::threading::JobLabel{                                                                     \
            .NameOrNull = #name,                                                                                       \
            .SiteId = ::ludus::foundation::profiling::HashZoneName(::std::string_view{#name}),                         \
        })
#endif
