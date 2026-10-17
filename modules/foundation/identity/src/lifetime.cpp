#include <ludus/foundation/identity/lifetime.hpp>

#include "internal/lifetime_source.hpp"

#include <atomic>

namespace ludus::foundation::identity
{
namespace
{
/// Next process lifetime token. Zero is never issued.
std::atomic<uint64> gNextLifetimeId{1};
} // namespace

LifetimeIdStatus TryIssueLifetimeId(uint64& outLifetimeId) noexcept
{
    return internal::TryIssueLifetimeIdFrom(gNextLifetimeId, outLifetimeId);
}
} // namespace ludus::foundation::identity
