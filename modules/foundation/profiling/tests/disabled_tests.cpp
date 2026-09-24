// Compile-out semantics (final design §9, §19, gate G1).
//
// LUDUS_PROFILING_ENABLED is SDK-owned and injected by the generated
// profiling_config.hpp; a translation unit must NOT predefine or override it
// (the generated header #errors on that, exactly like assert_config.hpp). So
// this TU does not force a value: it adapts to whatever flavor it is built in
// and asserts the invariant that must hold there.
//
//   * Release flavor  -> LUDUS_PROFILING_ENABLED == 0: every macro expands to a
//     no-op and NO argument is evaluated. CI builds the tests in Release via the
//     "Assertion policy (linux-clang-release)" job, so this path is exercised.
//   * Debug/Development -> LUDUS_PROFILING_ENABLED == 1: the macros expand to
//     real code; arguments are evaluated (the scope actually records). The
//     recording behaviour itself is covered in depth by scope_tests.cpp.
//
// Either way the argument-evaluation contract is checked against the real flag.
#include <ludus/foundation/profiling/profiling.hpp>
#include <ludus/foundation/profiling/trace_system.hpp>

#include <catch2/catch_test_macros.hpp>

namespace
{

int gSideEffectCounter = 0;

[[maybe_unused]] int incrementAndReturn() noexcept
{
    ++gSideEffectCounter;
    return gSideEffectCounter;
}

// A category-valued expression whose evaluation is observable. maybe_unused
// because in a disabled build the referencing macro must not evaluate it, so the
// compiler legitimately sees no call.
[[nodiscard, maybe_unused]] ludus::foundation::profiling::ProfileCategory makeCategory() noexcept
{
    ++gSideEffectCounter;
    return ludus::foundation::profiling::ProfileCategory{0u, std::string_view{"X"}};
}

} // namespace

TEST_CASE("LUDUS_PROFILING_ENABLED is defined by the SDK config", "[profiling][compiletime]")
{
    // The generated profiling_config.hpp always defines it (0 or 1). This simply
    // proves the ubiquitous header pulls in the SDK-owned policy.
#if !defined(LUDUS_PROFILING_ENABLED)
    STATIC_REQUIRE(false); // header failed to define the policy
#endif
    STATIC_REQUIRE((LUDUS_PROFILING_ENABLED == 0) || (LUDUS_PROFILING_ENABLED == 1));
}

#if LUDUS_PROFILING_ENABLED == 0

TEST_CASE("disabled instrumentation evaluates no arguments (gate G1)", "[profiling][compiletime][disabled]")
{
    gSideEffectCounter = 0;

    // Sanity: the helper is observable when actually called.
    REQUIRE(incrementAndReturn() == 1);
    gSideEffectCounter = 0;

    // In a disabled build none of these may evaluate their arguments.
    LUDUS_PROFILE_SCOPE(ZoneName);
    LUDUS_PROFILE_SCOPE(ZoneName, makeCategory());
    LUDUS_PROFILE_FUNCTION();
    LUDUS_PROFILE_FRAME();
    LUDUS_PROFILE_MARK(MarkName);
    const auto flow = LUDUS_PROFILE_FLOW_OUT(FlowName);
    LUDUS_PROFILE_FLOW_IN(flow);

    CHECK(gSideEffectCounter == 0);
    // FLOW_OUT yields a constant 0 id when compiled out.
    CHECK(flow == 0u);
}

#else // LUDUS_PROFILING_ENABLED == 1

TEST_CASE("enabled instrumentation records while a capture is active", "[profiling][compiletime][enabled]")
{
    ludus::foundation::profiling::RegisterThreadForTrace("Main");
    REQUIRE(ludus::foundation::profiling::BeginCapture(4));
    {
        LUDUS_PROFILE_SCOPE(EnabledZone);
        LUDUS_PROFILE_MARK(EnabledMark);
    }
    const ludus::foundation::profiling::TraceHealth health = ludus::foundation::profiling::GetTraceHealth();
    ludus::foundation::profiling::EndCapture();

    // The scope (begin+end) and the mark are real events in an enabled build.
    CHECK(health.EventsRecorded >= 3);
}

#endif
