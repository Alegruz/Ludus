// Play-session protocol codec acceptance (design 10, requirement L13).
//
// Exercises the bounded framing + flat-JSON codec: round-trip of typed fields,
// hex id precision, frame length-prefix encode/decode across split reads,
// rejection of oversize frames and malformed JSON, and backlog bounds. A slow
// reader scenario is modeled by feeding bytes incrementally.

#include "internal/protocol_codec.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace ludus::runtime::game_host::protocol;

TEST_CASE("message round-trips typed fields", "[protocol]")
{
    Message m;
    m.SetString("type", "Hello");
    m.SetUint("protocol", kProtocolVersion);
    m.SetHexId("session", 0xDEADBEEF12345678ULL);
    m.SetInt("delta", -42);
    m.SetBool("paused", true);

    const std::string json = m.Serialize();
    Message parsed;
    REQUIRE(Message::Parse(json, parsed));

    std::string type;
    uint64 proto = 0;
    uint64 session = 0;
    int64 delta = 0;
    bool paused = false;
    REQUIRE(parsed.GetString("type", type));
    REQUIRE(type == "Hello");
    REQUIRE(parsed.GetUint("protocol", proto));
    REQUIRE(proto == kProtocolVersion);
    REQUIRE(parsed.GetHexId("session", session));
    REQUIRE(session == 0xDEADBEEF12345678ULL);
    REQUIRE(parsed.GetInt("delta", delta));
    REQUIRE(delta == -42);
    REQUIRE(parsed.GetBool("paused", paused));
    REQUIRE(paused == true);
}

TEST_CASE("hex id survives full 64-bit precision", "[protocol]")
{
    const uint64 id = 0xFFFFFFFFFFFFFFFFULL;
    Message m;
    m.SetHexId("id", id);
    Message parsed;
    REQUIRE(Message::Parse(m.Serialize(), parsed));
    uint64 out = 0;
    REQUIRE(parsed.GetHexId("id", out));
    REQUIRE(out == id);
}

TEST_CASE("type mismatch on read fails cleanly", "[protocol]")
{
    Message m;
    m.SetString("n", "notnumber");
    uint64 out = 0;
    REQUIRE_FALSE(m.GetUint("n", out));
}

TEST_CASE("malformed json is rejected", "[protocol]")
{
    Message parsed;
    REQUIRE_FALSE(Message::Parse("{\"a\":}", parsed));
    REQUIRE_FALSE(Message::Parse("not json", parsed));
    REQUIRE_FALSE(Message::Parse("{\"a\":[1,2]}", parsed)); // nested unsupported
    REQUIRE_FALSE(Message::Parse("{\"a\":null}", parsed));
    REQUIRE(Message::Parse("{}", parsed));
}

TEST_CASE("frame encode/decode across split reads", "[protocol]")
{
    Message m;
    m.SetString("type", "Status");
    const std::string payload = m.Serialize();

    std::vector<ludus::foundation::uint8> frame;
    REQUIRE(EncodeFrame(payload, frame));
    REQUIRE(frame.size() == payload.size() + 4);

    // Feed the frame one byte at a time (slow reader): Next yields nothing until
    // the whole frame has arrived.
    FrameReader reader;
    std::string out;
    for (std::size_t i = 0; i + 1 < frame.size(); ++i)
    {
        reader.Append(&frame[i], 1);
        REQUIRE_FALSE(reader.Next(out));
    }
    reader.Append(&frame.back(), 1);
    REQUIRE(reader.Next(out));
    REQUIRE(out == payload);
    REQUIRE_FALSE(reader.Next(out));
    REQUIRE_FALSE(reader.Failed());
}

TEST_CASE("two frames back to back decode in order", "[protocol]")
{
    Message a;
    a.SetString("type", "Pause");
    Message b;
    b.SetString("type", "Resume");
    std::vector<ludus::foundation::uint8> fa;
    std::vector<ludus::foundation::uint8> fb;
    REQUIRE(EncodeFrame(a.Serialize(), fa));
    REQUIRE(EncodeFrame(b.Serialize(), fb));

    FrameReader reader;
    reader.Append(fa.data(), fa.size());
    reader.Append(fb.data(), fb.size());
    std::string out;
    REQUIRE(reader.Next(out));
    REQUIRE(out == a.Serialize());
    REQUIRE(reader.Next(out));
    REQUIRE(out == b.Serialize());
    REQUIRE_FALSE(reader.Next(out));
}

TEST_CASE("oversize frame length is a protocol failure", "[protocol]")
{
    // Hand-craft a length prefix exceeding the control-frame bound.
    std::vector<ludus::foundation::uint8> bad = {0xFF, 0xFF, 0xFF, 0x7F}; // ~2 GiB
    FrameReader reader;
    reader.Append(bad.data(), bad.size());
    std::string out;
    REQUIRE_FALSE(reader.Next(out));
    REQUIRE(reader.Failed());
}

TEST_CASE("payload above frame bound cannot be encoded", "[protocol]")
{
    const std::string huge(kMaxControlFrameBytes + 1, 'x');
    std::vector<ludus::foundation::uint8> frame;
    REQUIRE_FALSE(EncodeFrame(huge, frame));
}
