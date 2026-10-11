#include <ludus/foundation/base/types.h>

#include <catch2/catch_chrono_stringmaker.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace ludus::foundation;

TEST_CASE("Test adapter preserves scalar, range and opt-in chrono diagnostics", "[test-diagnostics]")
{
    REQUIRE(Catch::Detail::stringify(int32(-42)) == "-42");
    REQUIRE(Catch::Detail::stringify(uint64(18446744073709551615ULL)) == "18446744073709551615 (0xffffffffffffffff)");
    REQUIRE(Catch::Detail::stringify(static_cast<long double>(1.25)) == "1.25");
    REQUIRE(Catch::Detail::stringify(true) == "true");
    REQUIRE(Catch::Detail::stringify(std::string("quoted")) == "\"quoted\"");
    REQUIRE(Catch::Detail::stringify(std::vector<int32>{1, 2, 3}) == "{ 1, 2, 3 }");
    REQUIRE(Catch::Detail::stringify(std::chrono::milliseconds(12)) == "12 ms");
    CAPTURE(int32(-42), uint64(42));
    INFO("diagnostic context " << int32(42));
    REQUIRE(true);
}

TEST_CASE("Test adapter failing diagnostics probe", "[.][test-diagnostics-probe]")
{
    int32 observed = 17;
    CAPTURE(observed);
    INFO("probe context " << observed);
    CHECK(observed == 23);
    CHECK_FALSE(observed);
    FAIL("probe failure message");
}
