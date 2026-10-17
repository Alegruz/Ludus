#include <ludus/foundation/identity/handle.hpp>
#include <ludus/foundation/identity/lifetime.hpp>

#include <concepts>
#include <type_traits>
#include <utility>

#include <catch2/catch_test_macros.hpp>

using namespace ludus::foundation;
using namespace ludus::foundation::identity;

namespace
{
struct EntityTag;
struct TextureTag;
using EntityHandle = Handle<EntityTag>;
using TextureHandle = Handle<TextureTag>;
} // namespace

// Wrong-domain handles neither convert nor compare: compile-time tags separate domains.
static_assert(std::is_convertible_v<EntityHandle, TextureHandle> == false);
static_assert(std::is_constructible_v<TextureHandle, EntityHandle> == false);
static_assert(std::is_assignable_v<TextureHandle&, EntityHandle> == false);
static_assert(std::equality_comparable_with<EntityHandle, TextureHandle> == false);
// No implicit integer conversion in either direction.
static_assert(std::is_convertible_v<EntityHandle, uint64> == false);
static_assert(std::is_convertible_v<uint64, EntityHandle> == false);
// A plain 16-byte value with no pointer, refcount or virtual table.
static_assert(sizeof(EntityHandle) == 16);
static_assert(std::is_trivially_copyable_v<EntityHandle>);
static_assert(std::is_standard_layout_v<EntityHandle>);
static_assert(EntityHandle{}.IsNull() == true);
static_assert(noexcept(AdvanceGeneration(std::declval<uint32&>())));

TEST_CASE("null handle is all zero and slot zero is a valid slot", "[identity][handle]")
{
    const EntityHandle null;
    CHECK(null.IsNull() == true);
    CHECK(null == EntityHandle{0, 0, 0});
    const EntityHandle firstSlot{1, 0, FIRST_GENERATION};
    CHECK(firstSlot.IsNull() == false);
    CHECK(firstSlot != null);
    // Partially zero values are not null; the issuing registry rejects them.
    CHECK(EntityHandle{0, 3, 1}.IsNull() == false);
    CHECK(EntityHandle{1, 0, 0}.IsNull() == false);
}

TEST_CASE("equality compares owner, slot and generation", "[identity][handle]")
{
    const EntityHandle handle{7, 3, 2};
    CHECK(handle == EntityHandle{7, 3, 2});
    CHECK(handle != EntityHandle{8, 3, 2});
    CHECK(handle != EntityHandle{7, 4, 2});
    CHECK(handle != EntityHandle{7, 3, 3});
}

TEST_CASE("two owners issuing the same slot and generation produce distinct handles", "[identity][handle]")
{
    uint64 firstOwner = 0;
    uint64 secondOwner = 0;
    REQUIRE(TryIssueLifetimeId(firstOwner) == LifetimeIdStatus::Success);
    REQUIRE(TryIssueLifetimeId(secondOwner) == LifetimeIdStatus::Success);
    const EntityHandle first{firstOwner, 0, FIRST_GENERATION};
    const EntityHandle second{secondOwner, 0, FIRST_GENERATION};
    CHECK(first != second);
}

TEST_CASE("generations advance from one and retire at the terminal value", "[identity][handle]")
{
    uint32 generation = FIRST_GENERATION;
    CHECK(AdvanceGeneration(generation) == GenerationStatus::Advanced);
    CHECK(generation == 2);

    generation = TERMINAL_GENERATION - 1;
    CHECK(AdvanceGeneration(generation) == GenerationStatus::Advanced);
    CHECK(generation == TERMINAL_GENERATION);
    // Releasing the terminal incarnation retires the slot; the value never wraps.
    CHECK(AdvanceGeneration(generation) == GenerationStatus::Retired);
    CHECK(generation == TERMINAL_GENERATION);
    CHECK(AdvanceGeneration(generation) == GenerationStatus::Retired);
    CHECK(generation == TERMINAL_GENERATION);

    uint32 invalid = 0;
    CHECK(AdvanceGeneration(invalid) == GenerationStatus::InvalidGeneration);
    CHECK(invalid == 0);
}

TEST_CASE("a recycled slot never repeats a released handle", "[identity][handle]")
{
    // Simulate one slot cycling through incarnations under one owner.
    constexpr uint64 OWNER = 11;
    constexpr uint32 SLOT = 5;
    uint32 generation = FIRST_GENERATION;
    const EntityHandle released{OWNER, SLOT, generation};
    REQUIRE(AdvanceGeneration(generation) == GenerationStatus::Advanced);
    const EntityHandle reused{OWNER, SLOT, generation};
    CHECK(reused != released);
    CHECK(reused.Generation > released.Generation);
}
