#include <ludus/runtime/property_binding/scalars.hpp>

#include "reflection_test_record_schema.hpp"
#include "sample_game_reflection_schema.hpp"

#include <limits>
#include <string>

#include <catch2/catch_test_macros.hpp>

using namespace ludus::foundation;
using namespace ludus::foundation::reflection;
using namespace ludus::reflection_test;
namespace binding = ludus::runtime::property_binding;
namespace api = ludus::runtime::game_api;

TEST_CASE("Generated scalar JSON preserves widths and unreflected native storage", "[reflection][serialization]")
{
    Record source;
    source.Unreflected = 123;
    uint8 storage[4096]{};
    std::span<const uint8> output;
    Diagnostic error;
    REQUIRE(WriteRecordJson(source, storage, output, error) == Status::Ok);
    const std::string_view text{reinterpret_cast<const char*>(output.data()), output.size()};
    CHECK(text.find("-9223372036854775808") != std::string_view::npos);
    CHECK(text.find("18446744073709551615") != std::string_view::npos);
    CHECK(text.find("runtime") == std::string_view::npos);
    Record result;
    result.Signed = 0;
    result.Unsigned = 0;
    result.Runtime = 27;
    result.Unreflected = 71;
    REQUIRE(ReadRecordJson(text, result, GetSystemAllocationDomain(), error) == Status::Ok);
    CHECK(result.Signed == source.Signed);
    CHECK(result.Unsigned == source.Unsigned);
    CHECK(result.Rate == source.Rate);
    CHECK(result.Time == source.Time);
    CHECK(result.Runtime == 27);
    CHECK(result.Unreflected == 71);
    uint8 second[4096]{};
    std::span<const uint8> next;
    REQUIRE(WriteRecordJson(result, second, next, error) == Status::Ok);
    CHECK(std::string_view{reinterpret_cast<const char*>(next.data()), next.size()} == text);
    const auto published = next;
    CHECK(WriteRecordJson(result, std::span<uint8>{second, 1}, next, error) == Status::LimitExceeded);
    CHECK(next.data() == published.data());
    CHECK(next.size() == published.size());
}

TEST_CASE("Strict schema admission never publishes a partially decoded candidate", "[reflection][serialization]")
{
    constexpr std::string_view prefix = R"({"schema":"fc79a65b-2783-4412-9303-64c7bc5e18f9","version":1,"values":)";
    const std::string_view invalid[] = {
        R"({"rate":2,"smallunsigned":-1}})",
        R"({"rate":2,"signed":-9223372036854775809}})",
        R"({"rate":2,"unsigned":18446744073709551616}})",
        R"({"rate":2,"smallunsigned":4294967296}})",
        R"({"rate":2,"smallsigned":2147483648}})",
        R"({"rate":2,"signed":1.5}})",
        R"({"rate":2,"unsigned":"7"}})",
        R"({"rate":2,"enabled":1}})",
        R"({"rate":2,"unknown":7}})",
        R"({"rate":2,"runtime":7}})",
        R"({"rate":2,"rate":3}})",
        R"({"rate":2,"\u0072ate":3}})",
        R"({"rate":1e100}})",
        R"({"rate":2,"time":1e999}})",
        R"({"rate":2}} trailing)",
    };
    Diagnostic error;
    Record result;
    result.Rate = 7;
    result.Unsigned = 17;
    for (auto tail : invalid)
    {
        INFO(tail);
        const std::string input = std::string(prefix) + std::string(tail);
        REQUIRE(ReadRecordJson(input, result, GetSystemAllocationDomain(), error) != Status::Ok);
        CHECK(result.Rate == 7);
        CHECK(result.Unsigned == 17);
    }
    const std::string defaults = std::string(prefix) + "{}}";
    REQUIRE(ReadRecordJson(defaults, result, GetSystemAllocationDomain(), error) == Status::Ok);
    CHECK(result.Rate == 1.25F);
    CHECK(result.Unsigned == std::numeric_limits<uint64>::max());
    std::string wrongVersion = defaults;
    wrongVersion[wrongVersion.find("\"version\":1") + 10] = '2';
    CHECK(ReadRecordJson(wrongVersion, result, GetSystemAllocationDomain(), error) == Status::SchemaChanged);
    std::string wrongId = defaults;
    wrongId[12] = '0';
    CHECK(ReadRecordJson(wrongId, result, GetSystemAllocationDomain(), error) == Status::SchemaChanged);
    CHECK(ReadRecordJson(defaults + "\xff", result, GetSystemAllocationDomain(), error) != Status::Ok);
}

TEST_CASE("Scalar edits stage all-or-none and reject duplicate IDs and bad widths", "[reflection]")
{
    Record source;
    Record candidate;
    candidate.Rate = 7;
    const Edit invalid[]{
        { .FieldId = 6, .Data = { .Type = Kind::Float32, .Real = 2 } },
        { .FieldId = 3, .Data = { .Type = Kind::Uint32, .Unsigned = 1ULL << 32U } },
    };
    Diagnostic error;
    REQUIRE(PrepareRecord(source, invalid, candidate, error) == Status::InvalidValue);
    CHECK(error.FieldId == 3);
    CHECK(candidate.Rate == 7);
    Edit valid[]{invalid[0], { .FieldId = 3, .Data = { .Type = Kind::Uint32, .Unsigned = 12 } }};
    REQUIRE(PrepareRecord(source, valid, candidate, error) == Status::Ok);
    CHECK(candidate.Rate == 2);
    CHECK(candidate.SmallUnsigned == 12);
    CHECK(source.Rate == 1.25F);
    CHECK(candidate.Unreflected == source.Unreflected);
    valid[1] = valid[0];
    CHECK(PrepareRecord(source, valid, candidate, error) == Status::DuplicateField);
    valid[1].FieldId = 8;
    CHECK(PrepareRecord(source, valid, candidate, error) == Status::ReadOnly);
    valid[1].FieldId = 99;
    CHECK(PrepareRecord(source, valid, candidate, error) == Status::UnknownField);
    CHECK(PrepareRecord(source, {}, candidate, error) == Status::InvalidArgument);
    valid[1].FieldId = 6;
    valid[0].Data.Real = std::numeric_limits<float64>::quiet_NaN();
    CHECK(PrepareRecord(source, valid, candidate, error) == Status::InvalidValue);
    valid[0].Data.Real = std::numeric_limits<float64>::infinity();
    CHECK(PrepareRecord(source, {valid, 1}, candidate, error) == Status::InvalidValue);
    valid[0].Data.Real = 1.1; // Not a representable native float32 snapshot.
    CHECK(PrepareRecord(source, {valid, 1}, candidate, error) == Status::InvalidValue);
    valid[0].Data.Real = 3;
    REQUIRE(PrepareRecord(source, {valid, 1}, source, error) == Status::Ok);
    CHECK(source.Rate == 3);
}

TEST_CASE("OOM and document limits preserve the native destination", "[reflection][serialization]")
{
    AllocationDomain denied(
        nullptr,
        [](void*, usize, usize) noexcept -> void* { return nullptr; },
        [](void*, void*, usize, usize) noexcept {});
    Record result;
    result.Rate = 7;
    Diagnostic error;
    constexpr std::string_view input = R"({"schema":"fc79a65b-2783-4412-9303-64c7bc5e18f9","version":1,"values":{}})";
    CHECK(ReadRecordJson(input, result, denied, error) == Status::OutOfMemory);
    CHECK(result.Rate == 7);
    CHECK(ReadRecordJson(std::string(serialization::MAX_DOCUMENT_BYTES + 1, ' '),
                         result,
                         GetSystemAllocationDomain(),
                         error) == Status::LimitExceeded);
    CHECK(result.Rate == 7);
    for (usize length = 0; length < input.size(); ++length)
    {
        REQUIRE(ReadRecordJson(input.substr(0, length), result, GetSystemAllocationDomain(), error) != Status::Ok);
        CHECK(result.Rate == 7);
    }
}

TEST_CASE("Schema invariants and copied editor projection are checked in shipping", "[reflection][editor-binding]")
{
    Diagnostic error;
    auto schema = ludus::sample::BodySchema();
    Field fields[2]{schema.Fields[0], schema.Fields[1]};
    schema.Fields = fields;
    fields[1].Id = fields[0].Id;
    CHECK(ValidateSchema(schema, error) == Status::InvalidSchema);
    fields[1] = ludus::sample::BodySchema().Fields[1];
    fields[0].Max.Real = -1;
    CHECK(ValidateSchema(schema, error) == Status::InvalidSchema);
    fields[0] = ludus::sample::BodySchema().Fields[0];
    fields[0].Label = "\xff";
    CHECK(ValidateSchema(schema, error) == Status::InvalidSchema);
    fields[0] = ludus::sample::BodySchema().Fields[0];
    api::PropertyDescriptor descriptors[2]{};
    REQUIRE(binding::Describe(schema, 0xA001, descriptors, error) == Status::Ok);
    CHECK(descriptors[0].PropertyId == 0xB001);
    CHECK(descriptors[0].MinFloat == 0);
    CHECK(descriptors[0].MaxFloat == 8);
    CHECK(descriptors[0].Scope == static_cast<uint32>(api::PropertyScope::Persistable));
    CHECK(descriptors[1].Writable == 0);
    CHECK(descriptors[1].Scope == static_cast<uint32>(api::PropertyScope::ReadOnly));
    CHECK(binding::Describe(schema, 0xA001, {descriptors, 1}, error) == Status::BufferTooSmall);
    CHECK(binding::Describe(RecordSchema(), 1, descriptors, error) == Status::UnsupportedKind);
    CHECK(descriptors[0].ObjectId == 0xA001);
    api::PropertyEdit edits[2]{};
    edits[0].ObjectId = 0xA001;
    edits[0].PropertyId = 0xB001;
    edits[0].ExpectedRevision = 3;
    edits[0].Kind = static_cast<uint32>(api::PropertyKind::Float32);
    edits[0].Float = 4;
    Edit decoded[2]{};
    REQUIRE(binding::DecodeEdits(schema, {edits, 1}, 0xA001, 3, decoded, error) == Status::Ok);
    CHECK(decoded[0].Data.Real == 4);
    edits[0].Float = std::numeric_limits<float32>::quiet_NaN();
    CHECK(binding::DecodeEdits(schema, {edits, 1}, 0xA001, 3, decoded, error) == Status::InvalidValue);
    CHECK(decoded[0].Data.Real == 4);
    edits[0].Float = 5;
    edits[1] = edits[0];
    CHECK(binding::DecodeEdits(schema, edits, 0xA001, 3, decoded, error) == Status::DuplicateField);
    CHECK(binding::DecodeEdits(schema, {edits, 1}, 0xA001, 4, decoded, error) == Status::InvalidArgument);
    edits[0].PropertyId = 0x10000B001ULL;
    CHECK(binding::DecodeEdits(schema, {edits, 1}, 0xA001, 3, decoded, error) == Status::InvalidArgument);
}

TEST_CASE("Typed JSON validates preserved runtime fields and pre-rounding bounds", "[reflection][serialization]")
{
    ludus::sample::Body record;
    record.Speed = 7;
    record.Bounces = -1;
    Diagnostic error;
    constexpr std::string_view input =
        R"({"schema":"f2e2dbbb-709c-48ad-b87b-61d0d86ea35d","version":1,"values":{"speed":2}})";
    CHECK(ludus::sample::ReadBodyJson(input, record, GetSystemAllocationDomain(), error) == Status::InvalidValue);
    CHECK(error.FieldId == 0xB002);
    CHECK(record.Speed == 7);
    record.Bounces = 0;
    constexpr std::string_view over =
        R"({"schema":"f2e2dbbb-709c-48ad-b87b-61d0d86ea35d","version":1,"values":{"speed":8.00000001}})";
    CHECK(ludus::sample::ReadBodyJson(over, record, GetSystemAllocationDomain(), error) == Status::InvalidValue);
    CHECK(record.Speed == 7);
}
