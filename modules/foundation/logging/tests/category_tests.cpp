// This translation unit deliberately raises the compiled log level to Error so
// that TRACE/DEBUG/INFO/WARN are compiled out entirely. That lets us assert the
// spec section 7 / section 39 guarantee: arguments of a compiled-out log
// statement are never evaluated. The define must precede the header include.
#if defined(LUDUS_COMPILED_LOG_LEVEL)
#    undef LUDUS_COMPILED_LOG_LEVEL
#endif
#define LUDUS_COMPILED_LOG_LEVEL 4 // Error+

#include <ludus/foundation/logging/log.hpp>
#include <ludus/foundation/logging/log_format.hpp>
#include <ludus/foundation/logging/log_system.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

namespace
{

using namespace ludus::foundation::logging;

inline constexpr LogCategory LogCatTest{"CatTest"};

int gSideEffectCounter = 0;

int incrementAndReturn() noexcept
{
    ++gSideEffectCounter;
    return gSideEffectCounter;
}

} // namespace

TEST_CASE("constexpr category hashing is stable and order-sensitive", "[logging][category]")
{
    STATIC_REQUIRE(HashLogCategory("Platform") == HashLogCategory("Platform"));
    STATIC_REQUIRE(HashLogCategory("Platform") != HashLogCategory("Rendering"));
    // The id stored in a category matches the standalone hash of its name.
    STATIC_REQUIRE(LogCatTest.Id == HashLogCategory("CatTest"));
    CHECK(LogCatTest.Name == std::string_view{"CatTest"});
}

TEST_CASE("compiled-out log statements do not evaluate their arguments", "[logging][category][compiletime]")
{
    gSideEffectCounter = 0;

    // Sanity: the function is live and does have an observable side effect when
    // actually called. This also anchors the function as "used" so its later
    // elision inside the macros is what keeps the counter at zero, not dead-code
    // removal of the function itself.
    REQUIRE(incrementAndReturn() == 1);
    gSideEffectCounter = 0;

    // TRACE/DEBUG/INFO/WARN are below the compiled level (Error) in this TU, so
    // these must expand to ((void)0) and never call incrementAndReturn().
    LUDUS_LOG_TRACE(LogCatTest, "{}", incrementAndReturn());
    LUDUS_LOG_DEBUG(LogCatTest, "{}", incrementAndReturn());
    LUDUS_LOG_INFO(LogCatTest, "{}", incrementAndReturn());
    LUDUS_LOG_WARN(LogCatTest, "{}", incrementAndReturn());

    CHECK(gSideEffectCounter == 0);
}

TEST_CASE("per-category runtime override changes effective threshold", "[logging][category][filtering]")
{
    LogConfig config{};
    config.GlobalLevel = LogLevel::Warning;
    config.EnableConsole = false;
    config.EnableDebugger = false;
    config.EnableFile = false;
    LogSystem::Initialize(config);

    // Global is Warning, so Info is filtered for an unconfigured category.
    CHECK_FALSE(ShouldLog(LogLevel::Info, LogCatTest));

    // Lower this category to Trace: now Info passes.
    LogSystem::SetCategoryLevel(LogCatTest, LogLevel::Trace);
    CHECK(ShouldLog(LogLevel::Info, LogCatTest));

    // Other categories still follow the global level.
    CHECK_FALSE(ShouldLog(LogLevel::Info, LOG_CORE));

    // Fatal is always eligible regardless of thresholds.
    CHECK(ShouldLog(LogLevel::Fatal, LogCatTest));

    LogSystem::ClearCategoryLevels();
    CHECK_FALSE(ShouldLog(LogLevel::Info, LogCatTest));

    LogSystem::Shutdown();
}
