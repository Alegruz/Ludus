#include <ludus/foundation/base/compiler.h>
#include <ludus/foundation/base/version.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string_view>
#include <type_traits>

LUDUS_INLINE constexpr int ludus_inline_test() noexcept
{
    return 7;
}

static_assert(ludus_inline_test() == 7);

TEST_CASE("semantic version values are exposed", "[foundation][version]")
{
    const auto value = ludus::foundation::version();

    CHECK(value.major == 0);
    CHECK(value.minor == 1);
    CHECK(value.patch == 0);
}

TEST_CASE("version string is stable semantic text", "[foundation][version]")
{
    CHECK(ludus::foundation::version_string() == std::string_view{"0.1.0"});
}

TEST_CASE("git revision API always returns useful text", "[foundation][version]")
{
    const auto revision = ludus::foundation::git_revision();

    CHECK_FALSE(revision.empty());
}

TEST_CASE("public version API is noexcept and allocation-free", "[foundation][version]")
{
    static_assert(noexcept(ludus::foundation::version()));
    static_assert(noexcept(ludus::foundation::version_string()));
    static_assert(noexcept(ludus::foundation::git_revision()));
    static_assert(noexcept(ludus::foundation::compiler_identity()));
    static_assert(std::is_same_v<decltype(ludus::foundation::version_string()), std::string_view>);

    CHECK_FALSE(ludus::foundation::compiler_identity().empty());
}
