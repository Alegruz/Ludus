#pragma once

#include <ludus/foundation/base/types.h>
#include <ludus/foundation/profiling/zone.hpp>

/// @file
/// @brief Trace emission for runtime systems that identify their work at run time.
///
/// The macros in profiling.hpp give each lexical site a static descriptor. A
/// scheduler or other integration instead receives site IDs and correlation IDs
/// as data, so this header exposes the same per-thread recorder through plain
/// functions. Ordinary code keeps using the macros. Every function is a no-op
/// that evaluates nothing beyond its arguments while no capture is active; it
/// never allocates, never blocks and drops (and counts) an event when the capture
/// pool is exhausted. The calling thread should be registered with
/// RegisterThreadForTrace so that its events are attributed and flushed.
/// See docs/modules/foundation/profiling.md.

namespace ludus::foundation::profiling
{
/// @brief Set in every caller-supplied flow ID passed to StartFlow and FinishFlow.
///
/// LUDUS_PROFILE_FLOW_OUT allocates IDs from a counter that starts at one and
/// never reaches this bit, so the two ID spaces cannot collide. The caller owns
/// uniqueness within its own space for the duration of a capture.
inline constexpr uint64 CALLER_FLOW_ID_BIT = uint64{1} << 63;

/// @brief Whether a capture session is active. One relaxed atomic load; safe before BeginCapture.
[[nodiscard]] bool IsCaptureActive() noexcept;

/// @brief Records a site's name and source location for export.
///
/// Call at most once per descriptor, off the hot path (for example from a
/// function-local static initializer). The registry is bounded; a site beyond its
/// capacity still records events but exports without a name.
/// @param descriptor Site metadata whose Name and File have static storage duration.
void RegisterSite(const ZoneDescriptor& descriptor) noexcept;

/// @brief Opens a slice of a registered site on the calling thread.
///
/// Pair it with EndSite on the same thread, in last-opened first-closed order.
/// @param siteId ZoneDescriptor::SiteId of a site registered with RegisterSite.
void BeginSite(uint32 siteId) noexcept;

/// @brief Closes the most recently opened slice on the calling thread.
/// @param siteId The site ID given to the matching BeginSite.
void EndSite(uint32 siteId) noexcept;

/// @brief Starts a flow from the calling thread's innermost open slice.
///
/// Exported as a Chrome trace flow start ("s"). Emit it inside an open slice
/// and before the matching FinishFlow can be emitted on any thread.
/// @param descriptor Registered site naming the flow; FinishFlow must use the same site.
/// @param flowId Caller-unique ID with CALLER_FLOW_ID_BIT set.
void StartFlow(const ZoneDescriptor& descriptor, uint64 flowId) noexcept;

/// @brief Finishes a flow at the calling thread's innermost open slice.
///
/// Exported as a Chrome trace flow end ("f") bound to the enclosing slice.
/// @param descriptor The site given to the matching StartFlow.
/// @param flowId The ID given to the matching StartFlow.
void FinishFlow(const ZoneDescriptor& descriptor, uint64 flowId) noexcept;

/// @brief Records a timestamped sample of a counter on the trace timeline.
///
/// Exported as a Chrome trace counter event ("C") named by the site. This is a
/// timeline sample in the trace stream, not the planned per-frame aggregated
/// counters and gauges of profiling Phase 2.
/// @param descriptor Registered site naming the counter.
/// @param value Sample value.
void SampleCounter(const ZoneDescriptor& descriptor, uint64 value) noexcept;
} // namespace ludus::foundation::profiling
