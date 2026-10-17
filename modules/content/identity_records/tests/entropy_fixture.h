#pragma once

#include <ludus/foundation/base/core.h>

#include <ludus/foundation/identity/uuid.hpp>

namespace ludus::content::identity_records::tests
{
using namespace ludus::foundation;

/// Controlled test entropy: each call fills bytes from a counter, optionally failing after a number of calls.
struct CountingEntropy final
{
    /// Value mixed into the next fill; advanced once per call.
    uint64 NextValue = 1;
    /// Calls that succeed before the source reports unavailable entropy; never fails when zero.
    usize CallsBeforeFailure = 0;
    /// Calls served so far.
    usize CallCount = 0;
    /// Fill every call with the same bytes, to force UUID collisions.
    bool IsConstant = false;

    static bool Fill(void* contextOrNull, uint8* outBytes, usize size) noexcept
    {
        CountingEntropy& self = *static_cast<CountingEntropy*>(contextOrNull);
        if (self.CallsBeforeFailure != 0 && self.CallCount == self.CallsBeforeFailure)
        {
            return false;
        }
        ++self.CallCount;
        for (usize i = 0; i < size; ++i)
        {
            outBytes[i] = static_cast<uint8>((self.NextValue >> ((i % 8) * 8)) ^ (i * 17));
        }
        if (self.IsConstant == false)
        {
            ++self.NextValue;
        }
        return true;
    }

    [[nodiscard]] identity::EntropySource GetSource() noexcept
    {
        return { .FillBytesOrNull = &Fill, .ContextOrNull = this };
    }
};

/// An entropy source with no fill function: always unavailable.
[[nodiscard]] inline identity::EntropySource MakeUnavailableEntropy() noexcept
{
    return {};
}
} // namespace ludus::content::identity_records::tests
