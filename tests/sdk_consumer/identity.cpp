#include <ludus/foundation/identity/handle.hpp>
#include <ludus/foundation/identity/lifetime.hpp>
#include <ludus/foundation/identity/uuid.hpp>

#include <string_view>

namespace
{
struct ConsumerTag;
} // namespace

int ExerciseInstalledIdentity() noexcept
{
    using namespace ludus::foundation;
    using namespace ludus::foundation::identity;
    uint64 owner = 0;
    if (TryIssueLifetimeId(owner) != LifetimeIdStatus::Success || owner == 0)
    {
        return 92;
    }
    uint32 generation = FIRST_GENERATION;
    const Handle<ConsumerTag> handle{owner, 0, generation};
    if (handle.IsNull() == true || AdvanceGeneration(generation) != GenerationStatus::Advanced)
    {
        return 92;
    }
    constexpr std::string_view TEXT = "919108f7-52d1-4320-9bac-f847db4148a8";
    Uuid value;
    char formatted[UUID_TEXT_BUFFER_BYTES]{};
    if (TryParseUuid(TEXT.data(), TEXT.size(), UuidPolicy::RequireVersion4, value) != UuidStatus::Success ||
        FormatUuid(value, formatted, sizeof(formatted)) != UuidStatus::Success || std::string_view(formatted) != TEXT ||
        AssetId{value}.IsNil() == true)
    {
        return 92;
    }
    return 0;
}
