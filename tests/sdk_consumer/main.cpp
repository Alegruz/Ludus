#include <ludus/foundation/base/assert_config.hpp>
#include <ludus/foundation/base/assert_format.hpp>
#include <ludus/foundation/base/build_metadata.hpp>
#include <ludus/foundation/base/version.hpp>
#include <ludus/graphics/rhi/rhi.h>

#include <iostream>
#include <ludus/foundation/base/diagnostic_output.hpp>
#include <sys/socket.h>
#include <unistd.h>

static_assert(LUDUS_BUILD_FLAVOR_ID == EXPECTED_FLAVOR);
static_assert(LUDUS_ENABLE_ASSERTS == EXPECTED_ASSERTS);
static_assert(LUDUS_BREAK_ON_CHECK == EXPECTED_CHECK_BREAK);
int PolicyWithoutNdebug();

int main()
{
    // Verify the lifecycle API and static link without requiring Vulkan on CI.
    static_assert(noexcept(ludus::graphics::rhi::Initialize({})));
    ludus::graphics::rhi::Shutdown();
    int endpoints[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, endpoints) != 0 ||
        !ludus::foundation::diagnostics::ConfigureEmergencySocket(endpoints[0]))
    {
        return 4;
    }
    (void)close(endpoints[0]);
    LUDUS_ASSERT(true);
    LUDUS_REQUIRE(true);
    if (!LUDUS_CHECK(true) || LUDUS_CHECK_F(false, "installed SDK recovery probe {}", 23))
    {
        return 3;
    }
    char report[2048];
    const auto received = recv(endpoints[1], report, sizeof(report), MSG_DONTWAIT);
    (void)close(endpoints[1]);
    if (received <= 0)
    {
        return 5;
    }
    std::cout.write(report, received);
    if (PolicyWithoutNdebug() != LUDUS_ENABLE_ASSERTS)
    {
        return 2;
    }
    const auto value = ludus::foundation::version();
    if (value.major != ludus::foundation::build_metadata::version_major)
    {
        return 1;
    }

    std::cout << "SDK consumer linked Ludus " << ludus::foundation::version_string() << '\n';
    return 0;
}
