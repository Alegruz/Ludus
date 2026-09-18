// This translation unit deliberately raises the compiled log level to Error so
// that TRACE/DEBUG/INFO/WARN are compiled out entirely. That lets us assert the
// spec section 7 / section 39 guarantee: arguments of a compiled-out log
// statement are never evaluated. The define must precede the header include.
#if defined(LUDUS_COMPILED_LOG_LEVEL)
#    undef LUDUS_COMPILED_LOG_LEVEL
#endif
#define LUDUS_COMPILED_LOG_LEVEL 4 // Error+

#include <ludus/foundation/logging/log.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

namespace {

using namespace ludus::foundation::logging;

inline constexpr LogCategory LogCatTest{"CatTest"};

int g_side_effect_counter = 0;

int increment_and_return() noexcept
{
    ++g_side_effect_counter;
    return g_side_effect_counter;
}

} // namespace

TEST_CASE("constexpr category hashing is stable and order-sensitive", "[logging][category]")
{
    STATIC_REQUIRE(hash_log_category("Platform") == hash_log_category("Platform"));
    STATIC_REQUIRE(hash_log_category("Platform") != hash_log_category("Rendering"));
    // The id stored in a category matches the standalone hash of its name.
    STATIC_REQUIRE(LogCatTest.id == hash_log_category("CatTest"));
    CHECK(LogCatTest.name == std::string_view{"CatTest"});
}

TEST_CASE("compiled-out log statements do not evaluate their arguments", "[logging][category][compiletime]")
{
    g_side_effect_counter = 0;

    // Sanity: the function is live and does have an observable side effect when
    // actually called. This also anchors the function as "used" so its later
    // elision inside the macros is what keeps the counter at zero, not dead-code
    // removal of the function itself.
    REQUIRE(increment_and_return() == 1);
    g_side_effect_counter = 0;

    // TRACE/DEBUG/INFO/WARN are below the compiled level (Error) in this TU, so
    // these must expand to ((void)0) and never call increment_and_return().
    LUDUS_LOG_TRACE(LogCatTest, "{}", increment_and_return());
    LUDUS_LOG_DEBUG(LogCatTest, "{}", increment_and_return());
    LUDUS_LOG_INFO(LogCatTest, "{}", increment_and_return());
    LUDUS_LOG_WARN(LogCatTest, "{}", increment_and_return());

    CHECK(g_side_effect_counter == 0);
}

TEST_CASE("per-category runtime override changes effective threshold", "[logging][category][filtering]")
{
    LogConfig config{};
    config.global_level = LogLevel::Warning;
    config.enable_console = false;
    config.enable_debugger = false;
    config.enable_file = false;
    LogSystem::initialize(config);

    // Global is Warning, so Info is filtered for an unconfigured category.
    CHECK_FALSE(LogSystem::should_log(LogLevel::Info, LogCatTest));

    // Lower this category to Trace: now Info passes.
    LogSystem::set_category_level(LogCatTest, LogLevel::Trace);
    CHECK(LogSystem::should_log(LogLevel::Info, LogCatTest));

    // Other categories still follow the global level.
    CHECK_FALSE(LogSystem::should_log(LogLevel::Info, LogCore));

    // Fatal is always eligible regardless of thresholds.
    CHECK(LogSystem::should_log(LogLevel::Fatal, LogCatTest));

    LogSystem::clear_category_levels();
    CHECK_FALSE(LogSystem::should_log(LogLevel::Info, LogCatTest));

    LogSystem::shutdown();
}
