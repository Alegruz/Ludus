// Tests for the trivial-relocation fast path: a relocatable/trivially-copyable
// type must be moved during reallocation with memcpy (ZERO element move ctors),
// while a non-relocatable type uses per-element move+destroy.

#include "lifetime_type.hpp"

#include <ludus/foundation/containers/relocation.hpp>
#include <ludus/foundation/containers/vector.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <type_traits>

using ludus::foundation::IsTriviallyRelocatable;
using ludus::foundation::usize;
using ludus::foundation::Vector;
namespace tst = ludus::containers::testing;

TEST_CASE("IsTriviallyRelocatable defaults to trivially copyable", "[relocation][trait]")
{
    STATIC_REQUIRE(IsTriviallyRelocatable<int>);
    STATIC_REQUIRE(IsTriviallyRelocatable<double>);
    STATIC_REQUIRE(IsTriviallyRelocatable<void*>);
    struct Pod
    {
        int a;
        float b;
    };
    STATIC_REQUIRE(IsTriviallyRelocatable<Pod>);

    // A type with a user destructor is NOT trivially copyable, so not relocatable
    // by default.
    STATIC_REQUIRE_FALSE(IsTriviallyRelocatable<tst::Tracked>);
    STATIC_REQUIRE_FALSE(IsTriviallyRelocatable<std::string>);
}

TEST_CASE("Opt-in LUDUS_TRIVIALLY_RELOCATABLE enables the trait", "[relocation][trait]")
{
    STATIC_REQUIRE(IsTriviallyRelocatable<tst::Relocatable>);
}

TEST_CASE("Relocatable existing elements relocate via memcpy (bulk moves stay tiny)", "[relocation]")
{
    tst::LifetimeLedger led;
    Vector<tst::Relocatable> v;
    // Force many reallocations.
    for (int i = 0; i < 300; ++i)
    {
        v.EmplaceBack(&led, i);
    }
    // The Relocatable move ctor increments MoveCtor. The EXISTING elements are
    // relocated with memcpy on each growth (never move-constructed), which is the
    // property that keeps reallocation O(bytes) not O(n) constructor calls. The
    // newly-emplaced element is materialized into a local and moved into place
    // once per growth (self-reference safety, see EmplaceBack) — so total moves
    // are bounded by the number of reallocations (O(log n)), NOT the element
    // count. If the memcpy relocation of existing elements regressed to
    // per-element move-construction, this would be ~hundreds instead of ~dozen.
    const long growths = 32; // generous upper bound on reallocations for n=300
    REQUIRE(led.MoveCtor <= growths);
    for (int i = 0; i < 300; ++i)
    {
        REQUIRE(v[static_cast<usize>(i)].Value == i);
    }
}

TEST_CASE("Relocatable bulk relocation performs zero moves when pushes do not grow", "[relocation]")
{
    // With capacity reserved up-front there is no reallocation, so no relocation
    // of existing elements and no grow-path local: EmplaceBack constructs the
    // element directly in place. Zero move constructions confirms the direct
    // (non-grow) fast path does not spuriously move.
    tst::LifetimeLedger led;
    Vector<tst::Relocatable> v;
    v.Reserve(300);
    for (int i = 0; i < 300; ++i)
    {
        v.EmplaceBack(&led, i);
    }
    REQUIRE(led.MoveCtor == 0);
}

TEST_CASE("Non-relocatable type reallocates via move+destroy", "[relocation]")
{
    tst::LifetimeLedger led;
    Vector<tst::Tracked> v;
    v.Reserve(2);
    v.EmplaceBack(&led, 1);
    v.EmplaceBack(&led, 2);
    const long movesBefore = led.MoveCtor;
    const long dtorsBefore = led.Dtor;
    v.PushBack(tst::Tracked(&led, 3)); // triggers reallocation of the 2 existing
    // The two existing elements were move-constructed into new storage and the
    // old ones destroyed.
    REQUIRE(led.MoveCtor > movesBefore);
    REQUIRE(led.Dtor > dtorsBefore);
    REQUIRE(v.Size() == 3);
    REQUIRE(v[0].Value() == 1);
    REQUIRE(v[2].Value() == 3);
}

TEST_CASE("Relocatable correctness after growth", "[relocation]")
{
    Vector<tst::Relocatable> v;
    for (int i = 0; i < 1000; ++i)
    {
        v.EmplaceBack(nullptr, i * 2);
    }
    for (int i = 0; i < 1000; ++i)
    {
        REQUIRE(v[static_cast<usize>(i)].Value == i * 2);
    }
}
