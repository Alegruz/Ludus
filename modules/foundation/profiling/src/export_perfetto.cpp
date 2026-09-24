#include <ludus/foundation/profiling/trace_system.hpp>

#include "internal/recorder.hpp"
#include "internal/trace_chunk.hpp"
#include "internal/trace_event.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

// -----------------------------------------------------------------------------
// Perfetto / Chrome Trace Event JSON exporter (final design §16 primary path).
//
// This is the ANALYSIS + VISUALIZATION-export layer: it runs entirely off the
// hot path, after EndCapture(). It drains the recorder's chunks, orders each
// thread's events by timestamp, and emits the Chrome Trace Event format that
// ui.perfetto.dev and chrome://tracing consume directly. The hierarchical tree,
// inclusive/exclusive time, and calls-per-frame are RECONSTRUCTED by the viewer
// from the ordered B/E stream (C1: tree is a derived view, never stored).
//
// Mapping:
//   Begin  -> phase "B"   (viewer stacks these per thread -> the tree)
//   End    -> phase "E"
//   Instant-> phase "i"   (scope "t")
//   FrameMark -> phase "i" on a synthetic "Frames" name (a frame boundary; D7)
//   FlowOut/FlowIn -> phases "s"/"f" with a shared id (cross-context links; C5)
//
// Incomplete scopes (a Begin with no End when the capture ends, or a dropped
// chunk) are closed with a synthesized "E" carrying an "incomplete" arg so the
// duration is visibly untrustworthy rather than silently fabricated (§6, G13).
//
// std::string / std::vector / <cstdio> are used deliberately here: this is a
// cold, off-hot-path .cpp (never a public header), well within the STL policy
// (ADR 0003 allows these; they are not spread into instrumentation headers).
// -----------------------------------------------------------------------------

namespace ludus::foundation::profiling
{

namespace
{

using internal::TraceChunk;
using internal::TraceEvent;
using internal::TraceEventKind;
using internal::TraceRecorder;

struct FlatEvent
{
    uint64 Ticks;
    uint32 ThreadId;
    uint32 SiteId;
    uint16 Kind;
    uint16 Flags;
    uint64 Aux;
};

void AppendJsonEscaped(std::string& out, std::string_view text) noexcept
{
    for (const char c : text)
    {
        switch (c)
        {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\t':
                out += "\\t";
                break;
            case '\r':
                out += "\\r";
                break;
            default:
                out += c;
                break;
        }
    }
}

void AppendUint(std::string& out, uint64 value) noexcept
{
    char buffer[24];
    const int n = std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
    if (n > 0)
    {
        out.append(buffer, static_cast<usize>(n));
    }
}

// Chrome trace timestamps are microseconds (floating allowed). We keep integer
// ns internally and convert to a fixed-point microsecond string so sub-µs
// precision survives (ns / 1000 with three decimals).
void AppendMicros(std::string& out, uint64 ticksNs) noexcept
{
    const uint64 whole = ticksNs / 1000ull;
    const uint64 frac = ticksNs % 1000ull;
    char buffer[40];
    const int n = std::snprintf(buffer,
                                sizeof(buffer),
                                "%llu.%03llu",
                                static_cast<unsigned long long>(whole),
                                static_cast<unsigned long long>(frac));
    if (n > 0)
    {
        out.append(buffer, static_cast<usize>(n));
    }
}

// One Chrome trace-event object's data. Bundled into a struct (rather than a
// long list of same-typed scalars) so the numeric fields cannot be transposed
// at a call site.
struct ChromeEvent
{
    std::string_view Name;
    char Phase = 'i'; // "B"/"E"/"i"/"s"/"f"
    uint32 ThreadId = 0;
    uint64 TicksNs = 0;
    uint64 FlowId = 0;
    bool Incomplete = false;
};

void AppendEventObject(std::string& out, bool& first, const ChromeEvent& event) noexcept
{
    if (!first)
    {
        out += ",\n";
    }
    first = false;
    out += "  {\"pid\":1,\"tid\":";
    AppendUint(out, event.ThreadId);
    out += ",\"ts\":";
    AppendMicros(out, event.TicksNs);
    out += ",\"ph\":\"";
    out += event.Phase;
    out += "\",\"name\":\"";
    AppendJsonEscaped(out, event.Name.empty() ? std::string_view{"?"} : event.Name);
    out += "\"";
    if (event.Phase == 'i')
    {
        out += ",\"s\":\"t\"";
    }
    if (event.Phase == 's' || event.Phase == 'f')
    {
        out += ",\"cat\":\"flow\",\"id\":";
        AppendUint(out, event.FlowId);
        if (event.Phase == 'f')
        {
            out += ",\"bp\":\"e\"";
        }
    }
    if (event.Incomplete)
    {
        out += ",\"args\":{\"incomplete\":true}";
    }
    out += "}";
}

} // namespace

bool ExportPerfettoTrace(std::string_view path) noexcept
{
    TraceRecorder& recorder = TraceRecorder::Instance();

    // Drain every full chunk. Flatten to a single vector we can stable-sort by
    // (threadId, ticks) so each thread's B/E stream is monotonic — the viewer
    // relies on ordering to nest slices.
    std::vector<FlatEvent> events;
    TraceChunk* chunk = recorder.DrainFullChunks();
    std::vector<TraceChunk*> drained;
    while (chunk != nullptr)
    {
        TraceChunk* next = chunk->PoolNext;
        for (uint32 i = 0; i < chunk->Count; ++i)
        {
            const TraceEvent& e = chunk->Events[i];
            events.push_back(FlatEvent{e.Ticks, chunk->ThreadId, e.SiteId, e.Kind, e.Flags, e.Aux});
        }
        drained.push_back(chunk);
        chunk = next;
    }

    // Recycle chunks back to the pool now that we've copied their events.
    for (TraceChunk* c : drained)
    {
        recorder.RecycleChunk(c);
    }

    if (events.empty())
    {
        return false;
    }

    std::stable_sort(events.begin(), events.end(), [](const FlatEvent& a, const FlatEvent& b) noexcept {
        if (a.ThreadId != b.ThreadId)
        {
            return a.ThreadId < b.ThreadId;
        }
        return a.Ticks < b.Ticks;
    });

    std::string out;
    out.reserve(events.size() * 96 + 256);
    out += "{\"traceEvents\":[\n";
    bool first = true;

    // Per-thread stack depth to synthesize End events for scopes still open at
    // capture end (incomplete; §6/G13). We track open Begins per thread as we
    // walk the sorted stream and close leftovers at the end.
    // Thread ids are small and dense (assigned from 0). Use a simple map-free
    // approach: since events are grouped by thread after the sort, we detect
    // thread transitions and flush the pending stack.
    std::vector<uint64> openTicks; // stack of open-begin timestamps (unused for name; viewer pairs by order)
    std::vector<std::string> openNames;
    uint32 currentThread = events.front().ThreadId;

    uint64 lastTicks = events.front().Ticks;

    auto flushOpen = [&](uint32 threadId) noexcept {
        // Close any scopes left open on this thread with a synthesized,
        // incomplete End at the last-seen timestamp (§6/G13: labelled, never a
        // silently fabricated duration).
        while (!openNames.empty())
        {
            ChromeEvent synth;
            synth.Phase = 'E';
            synth.ThreadId = threadId;
            synth.TicksNs = lastTicks;
            synth.Incomplete = true;
            AppendEventObject(out, first, synth);
            openNames.pop_back();
            openTicks.pop_back();
        }
    };

    for (const FlatEvent& e : events)
    {
        if (e.ThreadId != currentThread)
        {
            flushOpen(currentThread);
            currentThread = e.ThreadId;
        }
        lastTicks = e.Ticks;
        const std::string_view name = recorder.SiteName(e.SiteId);

        ChromeEvent ce;
        ce.Name = name;
        ce.ThreadId = e.ThreadId;
        ce.TicksNs = e.Ticks;

        switch (static_cast<TraceEventKind>(e.Kind))
        {
            case TraceEventKind::Begin:
                ce.Phase = 'B';
                AppendEventObject(out, first, ce);
                openTicks.push_back(e.Ticks);
                openNames.emplace_back(name);
                break;
            case TraceEventKind::End:
                ce.Phase = 'E';
                AppendEventObject(out, first, ce);
                if (!openNames.empty())
                {
                    openNames.pop_back();
                    openTicks.pop_back();
                }
                break;
            case TraceEventKind::Instant:
                ce.Phase = 'i';
                AppendEventObject(out, first, ce);
                break;
            case TraceEventKind::FrameMark:
                ce.Name = std::string_view{"FrameMark"};
                ce.Phase = 'i';
                AppendEventObject(out, first, ce);
                break;
            case TraceEventKind::FlowOut:
                // Emit the flow start.
                ce.Phase = 's';
                ce.FlowId = e.Aux;
                AppendEventObject(out, first, ce);
                break;
            case TraceEventKind::FlowIn:
                ce.Phase = 'f';
                ce.FlowId = e.Aux;
                AppendEventObject(out, first, ce);
                break;
        }
    }
    flushOpen(currentThread);
    (void)lastTicks;

    out += "\n],\"displayTimeUnit\":\"ns\"}\n";

    std::FILE* file = std::fopen(std::string(path).c_str(), "wb");
    if (file == nullptr)
    {
        return false;
    }
    const usize written = std::fwrite(out.data(), 1, out.size(), file);
    std::fclose(file);
    return written == out.size();
}

} // namespace ludus::foundation::profiling
