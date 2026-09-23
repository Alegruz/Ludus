// The SDK's policy remains the same even across consumer TUs with different
// NDEBUG settings. Never undefine Ludus's generated policy macros.
#if defined(NDEBUG)
#    undef NDEBUG
#endif
#include <ludus/foundation/base/assert.hpp>

static_assert(LUDUS_BUILD_FLAVOR_ID == EXPECTED_FLAVOR);
static_assert(LUDUS_ENABLE_ASSERTS == EXPECTED_ASSERTS);
static_assert(LUDUS_BREAK_ON_CHECK == EXPECTED_CHECK_BREAK);

int PolicyWithoutNdebug()
{
    LUDUS_REQUIRE(true);
#if !LUDUS_ENABLE_ASSERTS
    LUDUS_ASSERT(undeclared_installed_sdk_condition, undeclared_installed_sdk_message);
#endif
    return LUDUS_ENABLE_ASSERTS;
}
