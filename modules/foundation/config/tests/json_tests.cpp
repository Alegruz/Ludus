#include <ludus/foundation/config/json.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace ludus::foundation;
using namespace ludus::foundation::config;
TEST_CASE("Config bundle validates full layer before publication and writes sparse overrides")
{
    const Descriptor schema[]{
        { .Name = "a", .Default = Value::FromUint64(5), .Help = {}, .Units = {}, .Choices = {}, .Persistent = true },
        { .Name = "b", .Default = Value::FromBool(false), .Help = {}, .Units = {}, .Choices = {}, .Persistent = true }};
    Context context;
    Diagnostic error;
    const auto& domain = GetSystemAllocationDomain();
    REQUIRE(context.Initialize(schema, domain, error) == Status::Ok);
    const auto input =
        R"({"version":1,"schema":"test.v1","layer":"preference","values":[{"key":"a","type":"uint64","value":"18446744073709551615","source":"player","line":12}]})";
    REQUIRE(PrepareJson(context, input, Layer::Preference, "test.v1", context.Revision(), domain, error) == Status::Ok);
    REQUIRE(context.Commit(context.Revision(), error) == Status::Ok);
    uint8 buffer[4096]{};
    std::span<const uint8> output;
    REQUIRE(WriteLayer(context, Layer::Preference, "test.v1", buffer, output) == Status::Ok);
    const std::string_view text{reinterpret_cast<const char*>(output.data()), output.size()};
    CHECK(text.find("18446744073709551615") != std::string_view::npos);
    CHECK(text.find("\"key\":\"b\"") == std::string_view::npos);
    Context roundTrip;
    REQUIRE(roundTrip.Initialize(schema, domain, error) == Status::Ok);
    REQUIRE(PrepareJson(roundTrip, text, Layer::Preference, "test.v1", roundTrip.Revision(), domain, error) ==
            Status::Ok);
    REQUIRE(roundTrip.Commit(roundTrip.Revision(), error) == Status::Ok);
    CHECK(PrepareJson(context, input, Layer::Preference, "wrong.v1", context.Revision(), domain, error) ==
          Status::InvalidBundle);
    CHECK(PrepareJson(context, input, Layer::Project, "test.v1", context.Revision(), domain, error) ==
          Status::InvalidBundle);
    CHECK(PrepareJson(context,
                      R"({"version":1,"version":1})",
                      Layer::Preference,
                      "test.v1",
                      context.Revision(),
                      domain,
                      error) == Status::InvalidBundle);
    const auto preserved = output;
    uint8 tiny[2]{};
    CHECK(WriteLayer(context, Layer::Preference, "test.v1", tiny, output) == Status::LimitExceeded);
    CHECK(output.data() == preserved.data());
}
TEST_CASE("Config integer decoding rejects coercion, overflow, noncanonical strings and preserves outputs")
{
    parsing::JsonDocument document;
    parsing::ParseError error;
    const std::string_view invalid[]{"1", "\"01\"", "\"-0\"", "\"+1\"", "\"18446744073709551616\"", "true", "1.0"};
    for (auto input : invalid)
    {
        REQUIRE(document.Read(input, error) == parsing::ParseStatus::Ok);
        Value value = Value::FromBool(true);
        CHECK(DecodeValue(document.Root(), Type::Uint64, value) == Status::InvalidValue);
        CHECK(value.Kind == Type::Bool);
    }
    REQUIRE(document.Read("\"-9223372036854775808\"", error) == parsing::ParseStatus::Ok);
    Value value;
    REQUIRE(DecodeValue(document.Root(), Type::Int64, value) == Status::Ok);
    CHECK(value.Signed == (-9223372036854775807LL - 1));
}

TEST_CASE("Config JSON adapter rejects wrong shapes, unknown keys, source positions and resource failures")
{
    const Descriptor schema[]{
        { .Name = "a", .Default = Value::FromBool(false), .Help = {}, .Units = {}, .Choices = {}, .Persistent = true }};
    Context context;
    Diagnostic error;
    const auto& domain = GetSystemAllocationDomain();
    REQUIRE(context.Initialize(schema, domain, error) == Status::Ok);
    const std::string_view malformed[]{
        R"({"version":2,"schema":"s","layer":"project","values":[]})",
        R"({"version":1,"schema":"s","layer":"project","values":[],"extra":true})",
        R"({"version":1,"schema":"s","layer":"project","values":[{"key":"z","type":"bool","value":true,"source":"x","line":0}]})",
        R"({"version":1,"schema":"s","layer":"project","values":[{"key":"a","type":"bool","value":true,"source":"x","line":4294967296}]})",
        R"({"version":1,"schema":"s","layer":"project","values":[{"key":"a","type":"bool","value":1,"source":"x","line":0}]})",
    };
    const auto revision = context.Revision();
    for (auto input : malformed)
    {
        CHECK(PrepareJson(context, input, Layer::Project, "s", revision, domain, error) != Status::Ok);
        CHECK(context.Revision() == revision);
        CHECK(context.Commit(revision, error) == Status::InvalidState);
    }
    const AllocationDomain failure(
        nullptr,
        [](void*, usize, usize) noexcept -> void* { return nullptr; },
        [](void*, void*, usize, usize) noexcept {});
    CHECK(PrepareJson(context, "{}", Layer::Project, "s", revision, failure, error) == Status::OutOfMemory);
}
