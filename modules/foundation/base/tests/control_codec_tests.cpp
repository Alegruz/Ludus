#include <ludus/foundation/base/diagnostic_output.hpp>

#include <initializer_list>

#include <catch2/catch_test_macros.hpp>

using namespace ludus::foundation;
using namespace ludus::foundation::diagnostics;

TEST_CASE("diagnostic control header preserves its exact little-endian wire bytes", "[primitive][control]")
{
    char bytes[19]{};
    bytes[0] = 'x';
    bytes[18] = 'y';
    REQUIRE(EncodeControlHeader(bytes + 1, 17, ControlMessageType::DecisionReply, 0xFEDCBA98, 1) == 16);
    const uint8 expected[] = {0x43, 0x44, 0x55, 0x4C, 1, 0, 4, 0, 0x98, 0xBA, 0xDC, 0xFE, 1, 0, 0, 0};
    for (usize i = 0; i < sizeof(expected); ++i)
    {
        REQUIRE(static_cast<uint8>(bytes[i + 1]) == expected[i]);
    }
    REQUIRE(bytes[0] == 'x');
    REQUIRE(bytes[18] == 'y');
    ControlHeader header;
    REQUIRE(DecodeControlHeader(bytes + 1, 17, header));
    REQUIRE(header.Kind == 4);
    REQUIRE(header.IncidentId == 0xFEDCBA98);
    REQUIRE(header.Length == 1);
}

TEST_CASE("diagnostic control failures preserve caller buffers and header", "[primitive][control]")
{
    char bytes[16]{};
    REQUIRE(EncodeControlHeader(bytes, sizeof(bytes), ControlMessageType::Hello, 0x12345678, 0) == 16);
    for (usize size = 0; size < sizeof(bytes); ++size)
    {
        REQUIRE(EncodeControlHeader(bytes, size, ControlMessageType::Hello, 0, 0) == 0);
        ControlHeader header{9, 10, 11};
        REQUIRE_FALSE(DecodeControlHeader(bytes, size, header));
        REQUIRE(header.Kind == 9);
        REQUIRE(header.IncidentId == 10);
        REQUIRE(header.Length == 11);
    }
    REQUIRE(EncodeControlHeader(bytes, sizeof(bytes), ControlMessageType::Hello, 0, 2049) == 0);
    ControlHeader header;
    REQUIRE(DecodeControlHeader(bytes, sizeof(bytes), header));
    REQUIRE(header.IncidentId == 0x12345678);
    for (const usize offset : {usize{0}, usize{4}, usize{6}, usize{12}})
    {
        const char original = bytes[offset];
        bytes[offset] = static_cast<char>(0xFF);
        ControlHeader rejected{9, 10, 11};
        REQUIRE_FALSE(DecodeControlHeader(bytes, sizeof(bytes), rejected));
        REQUIRE(rejected.Kind == 9);
        REQUIRE(rejected.IncidentId == 10);
        REQUIRE(rejected.Length == 11);
        bytes[offset] = original;
    }
    REQUIRE(EncodeControlHeader(nullptr, 16, ControlMessageType::Hello, 0, 0) == 0);
    REQUIRE_FALSE(DecodeControlHeader(nullptr, 16, header));
}
