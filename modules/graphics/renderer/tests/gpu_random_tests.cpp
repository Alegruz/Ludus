#include "random_parity.h"
#include "random_parity_layout.h"

#include <ludus/foundation/containers/array.hpp>
#include <ludus/foundation/math/addressed_random.hpp>
#include <ludus/foundation/time/time.hpp>
#include <ludus/graphics/rhi/compute.h>
#include <ludus/graphics/rhi/device.h>
#include <ludus/graphics/rhi/graph.h>
#include <ludus/graphics/rhi/lifetime.h>

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <span>

// GPU evidence for FoundationMath's shader port of addressed random v1
// (modules/foundation/math/shaders/math.slang); see
// docs/architecture/randomness.md#gpu-addressed-sampling. Every expected value
// comes from the CPU reference or a pinned known answer, never from the GPU.
namespace
{
using namespace ludus::foundation;
namespace math = ludus::foundation::math;
namespace rhi = ludus::graphics::rhi;
using S = rhi::RasterStatus;

constexpr uint64 TIMEOUT_NANOSECONDS = 10 * time::NANOSECONDS_PER_SECOND;
constexpr usize MAX_REPORTED_MISMATCHES = 16;
// Per-request readback limit; well below the default 256 KiB transfer profile.
constexpr usize READBACK_CHUNK_BYTES = usize{64} * 1024;

struct ParityCase final
{
    math::RandomBlock Header;
    math::RandomBlock Operand;
};
static_assert(sizeof(ParityCase) == LUDUS_RANDOM_PARITY_WORDS_PER_CASE * sizeof(math::RandomBlock));

// One kernel case with the result the GPU must reproduce; named fields keep the
// operand and expected words from being exchanged.
struct ParityEntry final
{
    uint32 Kind = LUDUS_RANDOM_PARITY_CASE_PADDING;
    math::RandomKey Key;
    uint32 Bound = 0;
    math::RandomBlock Operand;
    math::RandomBlock Expected;
};

class ParityCorpus final
{
public:
    void Add(const ParityEntry& entry)
    {
        const ParityCase item
        {
            .Header = {{entry.Kind,
                        static_cast<uint32>(entry.Key.Value),
                        static_cast<uint32>(entry.Key.Value >> 32u),
                        entry.Bound}},
            .Operand = entry.Operand,
        };
        if (mCases.TryAdd(item) == false || mExpected.TryAdd(entry.Expected) == false)
        {
            mHasFailedAllocation = true;
        }
    }
    void AddSample(math::RandomKey key, uint64 scope, uint32 event, uint32 dimension, uint32 bound)
    {
        // An out-of-range dimension must alias dimension % 65536, as CPU uint16
        // narrowing does, and never a retry attempt; bounded sampling rejects it.
        const bool isInRange = dimension <= math::RandomMaxDimension;
        const math::RandomAddress address{scope, event, static_cast<uint16>(dimension)};
        const uint32 raw = math::SampleUInt32(key, address);
        const float32 sample = math::SampleFloat01(key, address);
        uint32 sampleBits = 0;
        std::memcpy(&sampleBits, &sample, sizeof(sampleBits));
        uint32 bounded = 0;
        uint32 flags = isInRange == true ? LUDUS_RANDOM_PARITY_IS_IN_RANGE : 0u;
        if (isInRange == true && math::TrySampleBounded(key, address, bound, bounded) == math::MathStatus::Success)
        {
            flags |= LUDUS_RANDOM_PARITY_IS_SAMPLED;
        }
        else
        {
            bounded = 0;
        }
        Add(
        {
            .Kind = LUDUS_RANDOM_PARITY_CASE_SAMPLE,
            .Key = key,
            .Bound = bound,
            .Operand = {{static_cast<uint32>(scope), static_cast<uint32>(scope >> 32u), event, dimension}},
            .Expected = {{raw, sampleBits, bounded, flags}},
        });
    }
    void AddMultiply(uint32 a, uint32 b)
    {
        const uint64 product = static_cast<uint64>(a) * b;
        Add(
        {
            .Kind = LUDUS_RANDOM_PARITY_CASE_MULTIPLY,
            .Key = {},
            .Bound = 0,
            .Operand = {{a, b, 0, 0}},
            .Expected = {{static_cast<uint32>(product), static_cast<uint32>(product >> 32u), 0, 0}},
        });
    }
    void PadToGroup()
    {
        while (mCases.GetSize() % LUDUS_RANDOM_PARITY_GROUP_SIZE != 0)
        {
            Add({ .Kind = LUDUS_RANDOM_PARITY_CASE_PADDING, .Key = {}, .Bound = 0, .Operand = {}, .Expected = {} });
        }
    }
    [[nodiscard]] bool HasFailedAllocation() const noexcept
    {
        return mHasFailedAllocation;
    }
    [[nodiscard]] const Array<ParityCase>& GetCases() const noexcept
    {
        return mCases;
    }
    [[nodiscard]] const Array<math::RandomBlock>& GetExpected() const noexcept
    {
        return mExpected;
    }

private:
    Array<ParityCase> mCases;
    Array<math::RandomBlock> mExpected;
    bool mHasFailedAllocation = false;
};

template <typename Handle>
void WaitReady(rhi::DeviceHandle device, Handle handle)
{
    const uint64 start = time::NowTicks();
    S status = rhi::GetStatus(device, handle);
    while (status == S::Pending && time::NowTicks() - start < TIMEOUT_NANOSECONDS)
    {
        REQUIRE(rhi::PollLifetime(device) == S::Ready);
        status = rhi::GetStatus(device, handle);
    }
    REQUIRE(status == S::Ready);
}

bool IsAccepted(S status) noexcept
{
    return status == S::Ready || status == S::Pending;
}

void BuildCorpus(ParityCorpus& outCorpus)
{
    // Random123 tests/kat_vectors, revision 9545ff6413f258be2f04c1d319d99aaef7521150, the same
    // pinned zero, all-ones and mixed vectors as FoundationMath's addressed_random_tests.cpp.
    // https://github.com/DEShawResearch/random123/blob/9545ff6413f258be2f04c1d319d99aaef7521150/tests/kat_vectors
    outCorpus.Add(
    {
        .Kind = LUDUS_RANDOM_PARITY_CASE_PHILOX,
        .Key = {0},
        .Bound = 0,
        .Operand = {{0, 0, 0, 0}},
        .Expected = {{0x6627e8d5u, 0xe169c58du, 0xbc57ac4cu, 0x9b00dbd8u}},
    });
    outCorpus.Add(
    {
        .Kind = LUDUS_RANDOM_PARITY_CASE_PHILOX,
        .Key = {0xffffffffffffffffULL},
        .Bound = 0,
        .Operand = {{0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu}},
        .Expected = {{0x408f276du, 0x41c83b0eu, 0xa20bc7c6u, 0x6d5451fdu}},
    });
    outCorpus.Add(
    {
        .Kind = LUDUS_RANDOM_PARITY_CASE_PHILOX,
        .Key = {0x299f31d0a4093822ULL},
        .Bound = 0,
        .Operand = {{0x243f6a88u, 0x85a308d3u, 0x13198a2eu, 0x03707344u}},
        .Expected = {{0xd16cfe09u, 0x94fdccebu, 0x5001e420u, 0x24126ea1u}},
    });

    outCorpus.Add(
    {
        .Kind = LUDUS_RANDOM_PARITY_CASE_CONSTANTS,
        .Key = {},
        .Bound = 0,
        .Operand = {},
        .Expected = {{math::RandomAddressLayoutVersion,
                      math::RandomBoundedMappingVersion,
                      math::RandomMaxDimension,
                      math::RandomBoundedAttemptCount}},
    });

    // Retry loop boundaries through the production loop with injected draws.
    // Bound 0x80000001 has threshold 0x7fffffff, so a zero draw always rejects.
    constexpr uint32 HALF_REJECTING_BOUND = 0x80000001u;
    outCorpus.Add(
    {
        .Kind = LUDUS_RANDOM_PARITY_CASE_FORCED_EXHAUSTION,
        .Key = {},
        .Bound = HALF_REJECTING_BOUND,
        .Operand = {},
        .Expected = {{0, 0, 0, 0}},
    });
    outCorpus.Add(
    {
        .Kind = LUDUS_RANDOM_PARITY_CASE_FORCED_EXHAUSTION,
        .Key = {},
        .Bound = 0,
        .Operand = {},
        .Expected = {{0, 0, 0, 0}},
    });
    outCorpus.Add(
    {
        .Kind = LUDUS_RANDOM_PARITY_CASE_FORCED_LAST_ATTEMPT,
        .Key = {},
        .Bound = HALF_REJECTING_BOUND,
        .Operand = {},
        .Expected = {{0, LUDUS_RANDOM_PARITY_IS_SAMPLED, 0, 0}},
    });
    outCorpus.Add(
    {
        .Kind = LUDUS_RANDOM_PARITY_CASE_FORCED_CONSTANT,
        .Key = {},
        .Bound = 1,
        .Operand = {{0xffffffffu, 0, 0, 0}},
        .Expected = {{0, LUDUS_RANDOM_PARITY_IS_SAMPLED, 0, 0}},
    });

    // Endpoint floats pinned independently of the CPU mapping code.
    const uint32 floatBits[] = {0u, 255u, 256u, 0x80000000u, 0xffffffffu};
    const float32 floatValues[] = {0.0f, 0.0f, 0x1p-24f, 0.5f, 1.0f - 0x1p-24f};
    static_assert(sizeof(floatBits) / sizeof(floatBits[0]) == sizeof(floatValues) / sizeof(floatValues[0]));
    for (usize i = 0; i < sizeof(floatBits) / sizeof(floatBits[0]); ++i)
    {
        uint32 expectedBits = 0;
        std::memcpy(&expectedBits, &floatValues[i], sizeof(expectedBits));
        outCorpus.Add(
        {
            .Kind = LUDUS_RANDOM_PARITY_CASE_FLOAT,
            .Key = {},
            .Bound = 0,
            .Operand = {{floatBits[i], 0, 0, 0}},
            .Expected = {{expectedBits, 0, 0, 0}},
        });
    }

    // Ludus v1 golden answers from addressed_random_tests.cpp, pinned so the GPU
    // is checked against frozen values as well as the live CPU reference.
    math::RandomKey key;
    REQUIRE(math::TryMakeRandomKey(42, 7, key) == math::MathStatus::Success);
    REQUIRE(key.Value == 0xccf635ee9e9e2fa4ULL);
    const uint32 sampled = LUDUS_RANDOM_PARITY_IS_SAMPLED | LUDUS_RANDOM_PARITY_IS_IN_RANGE;
    const float32 golden = 0.5788143277168274f;
    uint32 goldenBits = 0;
    std::memcpy(&goldenBits, &golden, sizeof(goldenBits));
    outCorpus.Add(
    {
        .Kind = LUDUS_RANDOM_PARITY_CASE_SAMPLE,
        .Key = key,
        .Bound = 1000,
        .Operand = {{0x89abcdefu, 0x01234567u, 99, 1234}},
        .Expected = {{0x942d2d40u, goldenBits, 578, sampled}},
    });
    // Attempts 0 and 1 reject at this address; attempt 2 is accepted.
    math::RandomBlock forcedRejection = {{0xa3a6ca03u, 0, 0x3c0cb600u, sampled}};
    const float32 forcedSample = math::SampleFloat01(key, {});
    std::memcpy(&forcedRejection.Values[1], &forcedSample, sizeof(uint32));
    outCorpus.Add(
    {
        .Kind = LUDUS_RANDOM_PARITY_CASE_SAMPLE,
        .Key = key,
        .Bound = HALF_REJECTING_BOUND,
        .Operand = {},
        .Expected = forcedRejection,
    });
    // The aligned block holding dimensions 1232-1235 (TrySampleBlock's golden answer).
    outCorpus.Add(
    {
        .Kind = LUDUS_RANDOM_PARITY_CASE_PHILOX,
        .Key = key,
        .Bound = 0,
        .Operand = {{0x89abcdefu, 0x01234567u, 99, 1232u >> 2u}},
        .Expected = {{0x991146b3u, 0x1aa17390u, 0x942d2d40u, 0x487e565du}},
    });

    // Live CPU parity across key words, both scope words, event extremes,
    // lane boundaries 0-3/4-7, the top of the dimension range, out-of-range
    // dimensions and bounds that force rejection (0x80000001), exceed 16 bits
    // or are invalid (0).
    math::RandomKey derived;
    REQUIRE(math::TryMakeRandomKey(0xffffffffffffffffULL, 0xffffffffu, derived) == math::MathStatus::Success);
    const math::RandomKey keys[] = {{0}, {0xffffffffffffffffULL}, key, derived, {0x0123456789abcdefULL}};
    const uint64 scopes[] = {0, 1, 0xffffffffULL, 0x100000000ULL, 0x0123456789abcdefULL, 0xffffffffffffffffULL};
    const uint32 events[] = {0, 1, 99, 0x80000000u, 0xffffffffu};
    const uint32 dimensions[] =
        {0, 1, 2, 3, 4, 5, 6, 7, 8, 1232, 1235, 65531, 65532, 65535, 0x10000, 0x10000 + 1234, 0x20003, 0xffffffffu};
    const uint32 bounds[] =
        {1, 2, 3, 1000, 65535, 65536, 65537, 0x7fffffffu, 0x80000000u, HALF_REJECTING_BOUND, 0xffffffffu, 0};
    for (const math::RandomKey& sampleKey : keys)
    {
        for (const uint64 scope : scopes)
        {
            for (const uint32 event : events)
            {
                for (const uint32 dimension : dimensions)
                {
                    for (const uint32 bound : bounds)
                    {
                        outCorpus.AddSample(sampleKey, scope, event, dimension, bound);
                    }
                }
            }
        }
    }

    // MultiplyWide: every pair of 16-bit limb edge values, then exhaustive sweeps
    // of one limb against an all-ones operand where every carry is maximal.
    const uint32 edges[] = {0u,          1u,          2u,          0x7fffu,     0x8000u,     0xfffeu,
                            0xffffu,     0x10000u,    0x10001u,    0x1ffffu,    0x7fffffffu, 0x80000000u,
                            0x80000001u, 0xfffeffffu, 0xffff0000u, 0xffff0001u, 0xfffffffeu, 0xffffffffu,
                            0xd2511f53u, 0xcd9e8d57u, 0x9e3779b9u, 0xbb67ae85u};
    for (const uint32 a : edges)
    {
        for (const uint32 b : edges)
        {
            outCorpus.AddMultiply(a, b);
        }
    }
    constexpr uint32 LIMB_VALUES = 0x10000u;
    for (uint32 limb = 0; limb < LIMB_VALUES; ++limb)
    {
        outCorpus.AddMultiply(0xffffffffu, 0xffff0000u | limb);
        outCorpus.AddMultiply((limb << 16u) | 0xffffu, 0xffffffffu);
    }
    outCorpus.PadToGroup();
}
} // namespace

TEST_CASE("GPU addressed random matches the CPU v1 reference and Random123 known answers", "[renderer][gpu][random]")
{
    rhi::Shutdown();
    struct Guard
    {
        ~Guard()
        {
            rhi::Shutdown();
        }
    } guard;
    ParityCorpus corpus;
    BuildCorpus(corpus);
    REQUIRE(corpus.HasFailedAllocation() == false);
    const Array<ParityCase>& cases = corpus.GetCases();
    const usize caseBytes = cases.GetSize() * sizeof(ParityCase);
    const usize resultBytes = cases.GetSize() * sizeof(math::RandomBlock);

    rhi::DeviceHandle device;
    rhi::SurfaceHandle surface;
    rhi::DeviceDescription description;
    description.Required.Compute = true;
    description.RequiredProfile.Raster.MinBufferSize = caseBytes;
    description.RequiredProfile.Compute.MinStorageRange = caseBytes;
    const rhi::DeviceStatus started = rhi::CreateDevice({}, { .Width = 8, .Height = 8 }, description, device, surface);
#if defined(LUDUS_TEST_METAL)
    if (started != rhi::DeviceStatus::Ready && rhi::GetStartup().Error == rhi::StartupError::AdapterUnavailable)
    {
        SKIP("No Metal adapter");
    }
#endif
    REQUIRE(started == rhi::DeviceStatus::Ready);

    rhi::BufferHandle caseBuffer;
    rhi::BufferHandle resultBuffer;
    Array<uint8> zeros;
    REQUIRE(zeros.TryResize(resultBytes, uint8{0}) == true);
    const std::span<const uint8> caseSource{reinterpret_cast<const uint8*>(cases.GetData()), caseBytes};
    REQUIRE(IsAccepted(rhi::CreateBuffer(device,
                                         {
                                             .Role = rhi::BufferRole::Storage,
                                             .Size = caseBytes,
                                         },
                                         caseSource,
                                         caseBuffer)) == true);
    REQUIRE(IsAccepted(rhi::CreateBuffer(device,
                                         { .Role = rhi::BufferRole::Storage, .Size = resultBytes },
                                         std::span<const uint8>{zeros.GetData(), zeros.GetSize()},
                                         resultBuffer)) == true);
    const rhi::RasterShaderDescription shader = ludus::shaders::random_parity::Compute();
    rhi::RasterShaderHandle shaderHandle;
    rhi::BindingLayoutHandle layout;
    REQUIRE(IsAccepted(rhi::CreateRasterShader(device, shader, shaderHandle)) == true);
    REQUIRE(IsAccepted(rhi::CreateBindingLayout(device, shader.Bindings, layout)) == true);
    WaitReady(device, caseBuffer);
    WaitReady(device, resultBuffer);
    WaitReady(device, shaderHandle);
    WaitReady(device, layout);

    const rhi::RasterBindingResource resources[] = {
        { .Binding = 0, .Buffer = caseBuffer, .Size = caseBytes },
        { .Binding = 1, .Buffer = resultBuffer, .Size = resultBytes },
    };
    rhi::BindingSetHandle set;
    rhi::ComputePipelineHandle pipeline;
    REQUIRE(IsAccepted(rhi::CreateBindingSet(device, layout, resources, set)) == true);
    REQUIRE(IsAccepted(rhi::CreateComputePipeline(device, { .Shader = shaderHandle, .Layout = layout }, pipeline)) ==
            true);
    WaitReady(device, set);
    WaitReady(device, pipeline);

    rhi::OrderedGraph graph;
    REQUIRE(rhi::CreateOrderedGraph(device, graph) == S::Ready);
    rhi::GraphVersion caseVersion;
    rhi::GraphVersion initialResults;
    rhi::GraphVersion results;
    REQUIRE(rhi::ImportGraphBuffer(device, graph, caseBuffer, caseVersion) == S::Ready);
    REQUIRE(rhi::ImportGraphBuffer(device, graph, resultBuffer, initialResults) == S::Ready);
    REQUIRE(rhi::NextGraphVersion(device, graph, initialResults, results) == S::Ready);
    const rhi::GraphUse uses[] = {
        {caseVersion, rhi::GraphAccessMode::StorageRead, 0, caseBytes, rhi::RasterVisibility::Compute},
        {results, rhi::GraphAccessMode::StorageReadWrite, 0, resultBytes, rhi::RasterVisibility::Compute},
    };
    const rhi::ComputeDispatch dispatch
    {
        .Pipeline = pipeline,
        .Bindings = set,
        .Groups = {static_cast<uint32>(cases.GetSize() / LUDUS_RANDOM_PARITY_GROUP_SIZE), 1, 1},
    };
    rhi::GraphPassDescription pass;
    pass.Name = "addressed random parity";
    pass.Uses = uses;
    pass.UseCount = 2;
    pass.SideEffect = true;
    pass.Dispatch = &dispatch;
    REQUIRE(rhi::AddGraphPass(device, graph, pass) == S::Ready);
    rhi::GraphReport report;
    REQUIRE(rhi::CompileOrderedGraph(device, graph, report) == S::Ready);
    REQUIRE(rhi::SetFrameTarget(device, surface, {16, 16, 0, 0, 0, 1}) == rhi::DeviceStatus::Ready);
    rhi::SubmissionToken completion;
    const uint64 start = time::NowTicks();
    S executed = rhi::ExecuteOrderedGraph(device, surface, graph, completion);
    while (executed == S::NotReady && time::NowTicks() - start < TIMEOUT_NANOSECONDS)
    {
        executed = rhi::ExecuteOrderedGraph(device, surface, graph, completion);
    }
    REQUIRE(executed == S::Ready);

    Array<math::RandomBlock> actual;
    REQUIRE(actual.TryResize(cases.GetSize()) == true);
    uint8* actualBytes = reinterpret_cast<uint8*>(actual.GetData());
    for (usize offset = 0; offset < resultBytes; offset += READBACK_CHUNK_BYTES)
    {
        const usize size = resultBytes - offset < READBACK_CHUNK_BYTES ? resultBytes - offset : READBACK_CHUNK_BYTES;
        rhi::ReadbackTicket ticket;
        S readback = rhi::RequestBufferReadback(device, resultBuffer, offset, size, ticket);
        const uint64 readbackStart = time::NowTicks();
        while (readback == S::Pending && time::NowTicks() - readbackStart < TIMEOUT_NANOSECONDS)
        {
            REQUIRE(rhi::PollLifetime(device) == S::Ready);
            readback = rhi::GetStatus(device, ticket);
        }
        REQUIRE(readback == S::Ready);
        REQUIRE(rhi::CopyReadback(device, ticket, actualBytes + offset, size) == S::Ready);
        REQUIRE(rhi::Release(device, ticket) == S::Ready);
    }

    const Array<math::RandomBlock>& expected = corpus.GetExpected();
    usize mismatches = 0;
    for (usize index = 0; index < cases.GetSize() && mismatches < MAX_REPORTED_MISMATCHES; ++index)
    {
        for (usize lane = 0; lane < 4; ++lane)
        {
            if (actual[index].Values[lane] != expected[index].Values[lane])
            {
                const uint32 kind = cases[index].Header.Values[0];
                CAPTURE(index, kind, lane);
                CHECK(actual[index].Values[lane] == expected[index].Values[lane]);
                ++mismatches;
            }
        }
    }
    REQUIRE(mismatches == 0);
}
