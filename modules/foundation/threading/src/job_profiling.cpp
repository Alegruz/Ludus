#include <ludus/foundation/threading/job_profiling.hpp>

#include <ludus/foundation/profiling/integration.hpp>
#include <ludus/foundation/profiling/trace_system.hpp>

#include <string_view>

// Trace layout (docs/modules/foundation/threading.md#execution-traces):
// - "JobSystem::Submit" and "JobSystem::Wait" slices on the owner thread;
// - one slice per executed job, named by its label site (or "Job");
// - "JobDispatch" flows from Submit to each root and from each succeeded
//   prerequisite to the dependent that edge made ready;
// - a "JobReadyDepth" counter sampled at Submit and at each job start.
// Flow IDs are derived from the submission and a per-submission key, so the
// adapter keeps no state and the scheduler stores nothing per job for tracing.
namespace ludus::foundation::threading
{
#if LUDUS_PROFILING_ENABLED
namespace
{
constexpr std::string_view DEFAULT_WORKER_PREFIX = "Job worker";
constexpr usize MAX_THREAD_NAME_BYTES = 32;
constexpr uint32 DECIMAL_BASE = 10;
// Keys below 2^20 are edge indices (MaxDependencies <= 2^20); roots use
// ROOT_FLOW_KEY + their ready sequence (MaxJobs <= 2^20). The remaining bits
// below CALLER_FLOW_ID_BIT hold the low submission ID bits.
constexpr uint32 FLOW_KEY_BITS = 21;
constexpr uint32 ROOT_FLOW_KEY = uint32{1} << 20;
constexpr uint64 SUBMISSION_MASK = (uint64{1} << (63 - FLOW_KEY_BITS)) - 1;

constexpr profiling::ZoneDescriptor SUBMIT_SITE{std::string_view{"JobSystem::Submit"}};
constexpr profiling::ZoneDescriptor WAIT_SITE{std::string_view{"JobSystem::Wait"}};
constexpr profiling::ZoneDescriptor JOB_SITE{std::string_view{"Job"}};
constexpr profiling::ZoneDescriptor DISPATCH_SITE{std::string_view{"JobDispatch"}};
constexpr profiling::ZoneDescriptor READY_DEPTH_SITE{std::string_view{"JobReadyDepth"}};

[[nodiscard]] uint64 MakeFlowId(uint64 submissionId, uint32 key) noexcept
{
    return profiling::CALLER_FLOW_ID_BIT | ((submissionId & SUBMISSION_MASK) << FLOW_KEY_BITS) | key;
}

[[nodiscard]] uint32 GetJobSite(const JobTraceEvent& event) noexcept
{
    return event.Label.SiteId != 0 ? event.Label.SiteId : JOB_SITE.SiteId;
}

// Returns the number of registered sites, so a function-local static can run it once.
[[nodiscard]] uint32 RegisterAdapterSites() noexcept
{
    uint32 count = 0;
    for (const profiling::ZoneDescriptor& descriptor :
         {SUBMIT_SITE, WAIT_SITE, JOB_SITE, DISPATCH_SITE, READY_DEPTH_SITE})
    {
        profiling::RegisterSite(descriptor);
        ++count;
    }
    return count;
}

bool ShouldTrace(void*) noexcept
{
    return profiling::IsCaptureActive();
}

void StartWorker(void* contextOrNull, uint32 workerIndex) noexcept
{
    const std::string_view prefix =
        contextOrNull == nullptr ? DEFAULT_WORKER_PREFIX : std::string_view{static_cast<const char*>(contextOrNull)};
    char name[MAX_THREAD_NAME_BYTES]{};
    usize length = 0;
    for (const char character : prefix)
    {
        if (length == MAX_THREAD_NAME_BYTES)
        {
            break;
        }
        name[length++] = character;
    }
    char digits[DECIMAL_BASE]{};
    usize digitCount = 0;
    uint32 value = workerIndex;
    do
    {
        digits[digitCount++] = static_cast<char>('0' + value % DECIMAL_BASE);
        value /= DECIMAL_BASE;
    } while (value != 0);
    if (length < MAX_THREAD_NAME_BYTES)
    {
        name[length++] = ' ';
    }
    while (digitCount > 0 && length < MAX_THREAD_NAME_BYTES)
    {
        name[length++] = digits[--digitCount];
    }
    profiling::RegisterThreadForTrace(std::string_view{name, length});
}

void StopWorker(void*, uint32) noexcept
{
    profiling::UnregisterThreadForTrace();
}

void RecordTrace(void*, const JobTraceEvent& event) noexcept
{
    switch (event.Kind)
    {
        case JobTraceKind::Submitted:
            profiling::BeginSite(SUBMIT_SITE.SiteId);
            profiling::SampleCounter(READY_DEPTH_SITE, event.ReadyDepth);
            for (uint32 root = 0; root < event.ReadyDepth; ++root)
            {
                profiling::StartFlow(DISPATCH_SITE, MakeFlowId(event.SubmissionId, ROOT_FLOW_KEY + root));
            }
            profiling::EndSite(SUBMIT_SITE.SiteId);
            break;
        case JobTraceKind::Started: {
            const uint32 key = event.EdgeIndex == NO_JOB_EDGE ? ROOT_FLOW_KEY + event.Sequence : event.EdgeIndex;
            profiling::BeginSite(GetJobSite(event));
            profiling::FinishFlow(DISPATCH_SITE, MakeFlowId(event.SubmissionId, key));
            profiling::SampleCounter(READY_DEPTH_SITE, event.ReadyDepth);
            break;
        }
        case JobTraceKind::Signalled:
            profiling::StartFlow(DISPATCH_SITE, MakeFlowId(event.SubmissionId, event.EdgeIndex));
            break;
        case JobTraceKind::Finished:
            profiling::EndSite(GetJobSite(event));
            break;
        case JobTraceKind::WaitEntered:
            profiling::BeginSite(WAIT_SITE.SiteId);
            break;
        case JobTraceKind::WaitReturned:
            profiling::EndSite(WAIT_SITE.SiteId);
            break;
    }
}
} // namespace
#endif

uint32 RegisterJobLabelSite(const profiling::ZoneDescriptor& descriptor) noexcept
{
    profiling::RegisterSite(descriptor);
    return descriptor.SiteId;
}

JobSystemHooks MakeProfilerJobHooks(const char* workerNamePrefixOrNull) noexcept
{
#if LUDUS_PROFILING_ENABLED
    static const uint32 REGISTERED_SITE_COUNT = RegisterAdapterSites();
    static_cast<void>(REGISTERED_SITE_COUNT);
    return
    {
        .WorkerStartOrNull = StartWorker,
        .WorkerStopOrNull = StopWorker,
        .TraceOrNull = RecordTrace,
        .ShouldTraceOrNull = ShouldTrace,
        // The hooks only read the prefix; the hook signature carries a mutable pointer.
        .ContextOrNull = const_cast<char*>(workerNamePrefixOrNull),
    };
#else
    static_cast<void>(workerNamePrefixOrNull);
    return {};
#endif
}
} // namespace ludus::foundation::threading
