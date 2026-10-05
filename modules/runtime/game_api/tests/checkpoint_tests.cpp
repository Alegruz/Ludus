#include <ludus/runtime/game_api/authored.h>
#include <ludus/runtime/game_api/checkpoint.h>

#include <catch2/catch_test_macros.hpp>

using namespace ludus::foundation;
using namespace ludus::runtime::game_api;

TEST_CASE("checkpoint integer writes fit every width without partial failure", "[primitive][checkpoint]")
{
    for (usize width = 1; width <= 8; ++width)
    {
        uint8 bytes[10] = {0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5};
        const uint64 limit = width == 8 ? ~uint64{0} : (uint64{1} << (width * 8)) - 1;
        if (width < 8)
        {
            REQUIRE_FALSE(WriteCheckpointUint({bytes + 1, width}, limit + 1));
            for (const auto byte : bytes)
            {
                REQUIRE(byte == 0xA5);
            }
        }
        const uint64 value = 0xFEDCBA9876543210 & limit;
        REQUIRE(WriteCheckpointUint({bytes + 1, width}, value));
        REQUIRE(ReadCheckpointUint(bytes + 1, width) == value);
        for (usize i = 0; i < width; ++i)
        {
            REQUIRE(bytes[i + 1] == static_cast<uint8>(value >> (i * 8)));
        }
        for (usize i = width + 1; i < sizeof(bytes); ++i)
        {
            REQUIRE(bytes[i] == 0xA5);
        }
        REQUIRE(bytes[0] == 0xA5);
    }
    uint8 byte = 42;
    REQUIRE_FALSE(WriteCheckpointUint({&byte, 0}, 1));
    REQUIRE_FALSE(WriteCheckpointUint({&byte, 9}, 1));
    REQUIRE_FALSE(WriteCheckpointUint({nullptr, 1}, 1));
    REQUIRE(byte == 42);
}

TEST_CASE("checkpoint reader failure preserves output and can retry a repaired extent", "[primitive][checkpoint]")
{
    const uint8 bytes[] = {1, 0, 2, 0, 0xAA, 0xBB};
    CheckpointReader reader{{bytes, 5}};
    uint32 id = 99;
    ByteView payload{bytes, 1};
    REQUIRE_FALSE(reader.Next(id, payload));
    REQUIRE(id == 99);
    REQUIRE(payload.Data == bytes);
    REQUIRE(payload.Size == 1);
    REQUIRE(reader.Offset == 0);
    reader.Buffer.Size = sizeof(bytes);
    REQUIRE(reader.Next(id, payload));
    REQUIRE(id == 1);
    REQUIRE(payload.Data == bytes + 4);
    REQUIRE(payload.Size == 2);
    REQUIRE(reader.Offset == 6);
    REQUIRE_FALSE(reader.Next(id, payload));
    REQUIRE(id == 1);
    const uint8 invalid[] = {0, 0, 0, 0};
    reader = {{invalid, sizeof(invalid)}};
    REQUIRE_FALSE(reader.Next(id, payload));
    REQUIRE(id == 1);
    REQUIRE(payload.Data == bytes + 4);
}

TEST_CASE("authored reader publishes only validated records", "[primitive][checkpoint]")
{
    uint8 bytes[46]{};
    bytes[0] = 'L';
    bytes[1] = 'T';
    bytes[2] = 'U';
    bytes[3] = 'N';
    REQUIRE(WriteCheckpointUint({bytes + 4, 4}, 1));
    REQUIRE(WriteCheckpointUint({bytes + 8, 8}, 7));
    REQUIRE(WriteCheckpointUint({bytes + 16, 4}, 1));
    REQUIRE(WriteCheckpointUint({bytes + 20, 8}, 1));
    REQUIRE(WriteCheckpointUint({bytes + 28, 8}, 2));
    REQUIRE(WriteCheckpointUint({bytes + 36, 4}, 4));
    REQUIRE(WriteCheckpointUint({bytes + 40, 4}, 3));
    AuthoredReader reader;
    REQUIRE(reader.Start({bytes, sizeof(bytes)}, 7));
    AuthoredRecord record{99, 98, 97, {bytes, 1}};
    REQUIRE_FALSE(reader.Next(record));
    REQUIRE(record.Object == 99);
    REQUIRE(record.Property == 98);
    REQUIRE(record.Kind == 97);
    REQUIRE(record.Value.Data == bytes);
    REQUIRE(record.Value.Size == 1);
    REQUIRE_FALSE(reader.Complete());
    REQUIRE(WriteCheckpointUint({bytes + 40, 4}, 2));
    REQUIRE(reader.Next(record));
    REQUIRE(record.Object == 1);
    REQUIRE(record.Property == 2);
    REQUIRE(record.Kind == 4);
    REQUIRE(record.Value.Data == bytes + 44);
    REQUIRE(record.Value.Size == 2);
    REQUIRE(reader.Complete());
}
