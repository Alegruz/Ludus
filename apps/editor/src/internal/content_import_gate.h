#pragma once

#include <ludus/foundation/base/types.h>

#include <atomic>

namespace ludus::editor
{
// Private publication gate shared by native import entry points. Cancellation
// wins before publication or reports that publication has already begun.
class ContentImportGate final
{
public:
    [[nodiscard]] bool Cancel() noexcept
    {
        auto expected = Phase::Preparing;
        return Phase_.compare_exchange_strong(expected, Phase::Cancelled) || expected == Phase::Cancelled;
    }
    [[nodiscard]] bool Cancelled() const noexcept
    {
        return Phase_.load() == Phase::Cancelled;
    }
    [[nodiscard]] bool Publish() noexcept
    {
        auto expected = Phase::Preparing;
        return Phase_.compare_exchange_strong(expected, Phase::Publishing);
    }

private:
    enum class Phase : foundation::uint8
    {
        Preparing,
        Cancelled,
        Publishing
    };
    std::atomic<Phase> Phase_{Phase::Preparing};
};

} // namespace ludus::editor
