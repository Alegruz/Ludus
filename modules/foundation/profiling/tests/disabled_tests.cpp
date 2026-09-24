// Gate G1 (final design §9, §19): when profiling is compiled out, every macro
// expands to `((void)0)` (or a constant for FLOW_OUT) and NO argument is
// evaluated. This TU forces LUDUS_PROFILING_ENABLED=0 BEFORE including the
// header, mirroring the logging category test's compiled-out-argument proof.
#if defined(LUDUS_PROFILING_ENABLED)
#    undef LUDUS_PROFILING_ENABLED
#endif
#define LUDUS_PROFILING_ENABLED 0

#include <ludus/foundation/profiling/profiling.hpp>

#include <catch2/catch_test_macros.hpp>

namespace
{

int gSideEffectCounter = 0;

int incrementAndReturn() noexcept
{
    ++gSideEffectCounter;
    return gSideEffectCounter;
}

// A category expression whose evaluation would also be observable. Marked
// maybe_unused because, in a disabled build, the macro that references it must
// NOT evaluate it — so the compiler legitimately sees no call and would warn.
[[nodiscard, maybe_unused]] ludus::foundation::profiling::ProfileCategory makeCategory() noexcept
{
    ++gSideEffectCounter;
    return ludus::foundation::profiling::ProfileCategory{0u, std::string_view{"X"}};
}

} // namespace

TEST_CASE("disabled build compiles the macros out entirely", "[profiling][disabled][compiletime]")
{
    STATIC_REQUIRE(LUDUS_PROFILING_ENABLED == 0);
}

TEST_CASE("disabled instrumentation does not evaluate its arguments", "[profiling][disabled][compiletime]")
{
    gSideEffectCounter = 0;

    // Sanity: the helper is live and observable when actually called.
    REQUIRE(incrementAndReturn() == 1);
    gSideEffectCounter = 0;

    // None of these may evaluate their arguments in a disabled build.
    LUDUS_PROFILE_SCOPE(ZoneName);
    LUDUS_PROFILE_SCOPE(ZoneName, makeCategory());
    LUDUS_PROFILE_FUNCTION();
    LUDUS_PROFILE_FRAME();
    LUDUS_PROFILE_MARK(MarkName);
    const auto flow = LUDUS_PROFILE_FLOW_OUT(FlowName);
    LUDUS_PROFILE_FLOW_IN(flow);

    CHECK(gSideEffectCounter == 0);
    // FLOW_OUT yields a constant 0 id in a disabled build.
    CHECK(flow == 0u);
}
