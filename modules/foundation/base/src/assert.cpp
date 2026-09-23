#include <ludus/foundation/base/assert.hpp>

#include <ludus/foundation/base/build_metadata.hpp>
#include <ludus/foundation/base/diagnostic_output.hpp>

#include "internal/diagnostic_finish.hpp"
#include "internal/diagnostic_platform.hpp"
#include "internal/diagnostic_record.hpp"
#include "internal/diagnostic_text.hpp"

#include <atomic>

namespace ludus::foundation::diagnostics::internal
{
constinit FatalPacket gFatalPacket{};
} // namespace ludus::foundation::diagnostics::internal

namespace ludus::foundation::diagnostics
{
namespace
{
using internal::TextWriter;
struct Entry
{
    AssertionSite Site{};
    uint64 ThreadId = 0;
    FailureKind Kind = FailureKind::Fatal;
    bool Active = false;
    bool LastCheck = false;
};

constinit thread_local Entry gEntry{};
constinit std::atomic_flag gOwner = ATOMIC_FLAG_INIT;
constinit uint32 gCheckReports = 0; // Access only while owning gOwner.
constexpr uint32 CHECK_REPORT_LIMIT = 64;

const char* KindName(FailureKind kind) noexcept
{
    switch (kind)
    {
        case FailureKind::Assert:
            return "ASSERT";
        case FailureKind::Require:
            return "REQUIRE";
        case FailureKind::Check:
            return "CHECK";
        case FailureKind::Fatal:
            return "FATAL";
    }
    return "invalid-kind";
}

const char* FlavorName() noexcept
{
    switch (LUDUS_BUILD_FLAVOR_ID)
    {
        case 1:
            return "Debug";
        case 2:
            return "Development";
        case 3:
            return "Profile";
        case 4:
            return "Release";
        default:
            return "unknown";
    }
}

void SiteText(TextWriter& writer, const Entry& entry, bool minimal = false) noexcept
{
    writer.Raw("[LUDUS:");
    writer.Raw(KindName(entry.Kind));
    writer.Raw("] thread=");
    writer.Number(entry.ThreadId);
    writer.Raw(" site=");
    writer.Text(entry.Site.File, minimal ? 160 : 480);
    writer.Byte(':');
    writer.Number(entry.Site.Line);
    writer.Raw(" expression=");
    writer.Text(entry.Site.Expression == nullptr ? "<unconditional>" : entry.Site.Expression, minimal ? 160 : 480);
}

usize CompleteReport(char* buffer, usize capacity, const char* message, const DiagnosticText* rendered) noexcept
{
    TextWriter writer(buffer, capacity);
    writer.Raw("[LUDUS assertion report v1] revision=");
    writer.Text(build_metadata::git_revision.data(), 64);
    writer.Raw(" flavor=");
    writer.Raw(FlavorName());
    writer.Raw(" asserts=");
    writer.Number(LUDUS_ENABLE_ASSERTS);
    writer.Raw(" policy-version=");
    writer.Number(LUDUS_ASSERT_POLICY_VERSION);
    writer.Raw(" check-break=");
    writer.Number(LUDUS_BREAK_ON_CHECK);
    writer.Byte('\n');
    SiteText(writer, gEntry);
    writer.Raw("\nfunction=");
    writer.Text(gEntry.Site.Function, 128);
    writer.Raw("\ncapture=stack-unavailable,artifact-id-unavailable; delivery=nonblocking-best-effort");
    if (gEntry.LastCheck)
    {
        writer.Raw("\n[check report limit reached; later detail suppressed]");
    }
    writer.Raw("\nmessage=");
    if (rendered != nullptr)
    {
        writer.Rendered(rendered->Data, rendered->Size);
    }
    else if (message != nullptr)
    {
        writer.Text(message);
    }
    return writer.Finish();
}

[[noreturn]] void Compromised(const char* marker, usize size) noexcept
{
    (void)TryWriteEmergencyBytes(marker, size);
    internal::TerminateImmediately();
}

void InspectIfAttached() noexcept
{
    if (internal::QueryDebugger() == internal::DebuggerState::Attached)
    {
        internal::BreakForDebugger();
    }
}
} // namespace

namespace detail
{
void BeginFatal(FailureKind kind, const AssertionSite& site) noexcept
{
    if (gEntry.Active)
    {
        constexpr char marker[] = "[LUDUS recursive fatal; immediate termination]\n";
        Compromised(marker, sizeof(marker) - 1);
    }
    gEntry.Active = true;
    gEntry.Kind = kind;
    gEntry.Site = site;
    gEntry.ThreadId = internal::NativeThreadId();
    if (gOwner.test_and_set(std::memory_order_acquire))
    {
        char secondary[512];
        TextWriter writer(secondary, sizeof(secondary));
        writer.Raw("[secondary fatal; immediate termination] ");
        SiteText(writer, gEntry, true);
        Compromised(secondary, writer.Finish());
    }
    auto& packet = internal::gFatalPacket;
    TextWriter writer(packet.Minimal, sizeof(packet.Minimal));
    SiteText(writer, gEntry, true);
    packet.MinimalSize = writer.Finish();
    packet.MinimalReady.store(true, std::memory_order_release);
    packet.MinimalDelivery = TryWriteEmergencyBytes(packet.Minimal, packet.MinimalSize);
}

bool BeginCheck(const AssertionSite& site) noexcept
{
    if (gEntry.Active)
    {
        return false; // Do not clear the enclosing failure's state.
    }
    gEntry.Active = true;
    if (gOwner.test_and_set(std::memory_order_acquire))
    {
        gEntry.Active = false;
        return false;
    }
    if (gCheckReports == CHECK_REPORT_LIMIT)
    {
        gEntry.Active = false;
        gOwner.clear(std::memory_order_release);
        return false;
    }
    gEntry.Site = site;
    gEntry.Kind = FailureKind::Check;
    gEntry.ThreadId = internal::NativeThreadId();
    gEntry.LastCheck = ++gCheckReports == CHECK_REPORT_LIMIT;
    return true;
}

[[noreturn]] static void FinishFatalImpl(const char* message, const DiagnosticText* rendered) noexcept
{
    if (!gEntry.Active || gEntry.Kind == FailureKind::Check)
    {
        constexpr char marker[] = "[LUDUS invalid fatal protocol]\n";
        Compromised(marker, sizeof(marker) - 1);
    }
    auto& packet = internal::gFatalPacket;
    packet.CompleteSize = CompleteReport(packet.Complete, sizeof(packet.Complete), message, rendered);
    packet.CompleteReady.store(true, std::memory_order_release);
    packet.CompleteDelivery = TryWriteEmergencyBytes(packet.Complete, packet.CompleteSize);
    packet.DeliveryReady.store(true, std::memory_order_release);
    InspectIfAttached();
    internal::TerminateForAssertion(); // Debugger continuation never escapes.
}

static bool FinishCheckImpl(const char* message, const DiagnosticText* rendered) noexcept
{
    if (!gEntry.Active || gEntry.Kind != FailureKind::Check)
    {
        constexpr char marker[] = "[LUDUS invalid Check protocol]\n";
        Compromised(marker, sizeof(marker) - 1);
    }
    char report[2048];
    const usize size = CompleteReport(report, sizeof(report), message, rendered);
    (void)TryWriteEmergencyBytes(report, size);
    if (LUDUS_BREAK_ON_CHECK != 0)
    {
        InspectIfAttached();
    }
    gEntry = {};
    gOwner.clear(std::memory_order_release);
    return false;
}
[[noreturn]] void FinishFatal(const char* message) noexcept
{
    FinishFatalImpl(message, nullptr);
}
bool FinishCheck(const char* message) noexcept
{
    return FinishCheckImpl(message, nullptr);
}
[[noreturn]] void FinishFatalRendered(DiagnosticText message) noexcept
{
    FinishFatalImpl(nullptr, &message);
}
bool FinishCheckRendered(DiagnosticText message) noexcept
{
    return FinishCheckImpl(nullptr, &message);
}
} // namespace detail
} // namespace ludus::foundation::diagnostics
