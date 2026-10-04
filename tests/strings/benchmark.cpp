#include <ludus/foundation/base/types.h>

#include <ludus/foundation/hash/hash.hpp>
#include <ludus/foundation/strings/shared_string.hpp>
#include <ludus/foundation/strings/string.hpp>
#include <ludus/foundation/strings/string_table.hpp>

#include <chrono>
#include <iostream>
#include <span>
#include <string_view>

using namespace ludus::foundation;

namespace
{
constexpr usize kIterations = 200000;
uint64 gChecksum{};
const std::string_view kCorpus[]{"x",
                                 "player",
                                 "material_albedo",
                                 "characters/player/animation/run_forward",
                                 "content/environments/city/materials/concrete_weathered_roughness.texture"};

template <typename Work>
void Measure(std::string_view label, Work work)
{
    for (usize i = 0; i < 1000; ++i)
    {
        work(i);
    }
    const auto start = std::chrono::steady_clock::now();
    for (usize i = 0; i < kIterations; ++i)
    {
        work(i);
    }
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
    std::cout << label << ',' << elapsed / static_cast<int64>(kIterations) << '\n';
}
} // namespace

// Opt-in diagnostic benchmark, not a timing test or an engine dependency.
// No speed threshold: report corpus/toolchain/CPU with results before tuning.
int main()
{
    std::cout << "String_bytes," << sizeof(String) << "\nSharedString_bytes," << sizeof(SharedString)
              << "\ncase,ns_per_operation\n";
    bool good = true;
    Measure("unique_construct_assign", [&](usize i) {
        String value;
        good &= value.TryAssign(kCorpus[i % 5]) == StringStatus::Ok;
        gChecksum += value.GetSize();
    });
    String reusable;
    if (reusable.TryEnsureCapacity(128) != StringStatus::Ok)
    {
        return 1;
    }
    Measure("unique_reserved_assign", [&](usize i) {
        good &= reusable.TryAssign(kCorpus[i % 5]) == StringStatus::Ok;
        gChecksum += reusable.GetSize();
    });
    SharedString shared;
    if (CreateShared(kCorpus[4], GetSystemAllocationDomain(), shared) != StringStatus::Ok)
    {
        return 1;
    }
    Measure("shared_copy_release", [&](usize) {
        // NOLINTNEXTLINE(performance-unnecessary-copy-initialization): benchmark deliberately measures retain/release.
        const SharedString copy = shared;
        gChecksum += copy.GetSize();
    });
    Measure("xxh3_64", [&](usize i) {
        const auto bytes = kCorpus[i % 5];
        gChecksum ^= TableHash64({reinterpret_cast<const uint8*>(bytes.data()), bytes.size()});
    });
    Measure("fnv1a_64", [&](usize i) { gChecksum ^= SymbolFingerprint64(kCorpus[i % 5]); });
    Measure("siphash_2_4", [&](usize i) {
        const auto bytes = kCorpus[i % 5];
        gChecksum ^= KeyedTableHash64({reinterpret_cast<const uint8*>(bytes.data()), bytes.size()}, {123, 456});
    });
    StringTable table;
    if (CreateTable({}, GetSystemAllocationDomain(), table) != StringStatus::Ok)
    {
        return 1;
    }
    for (auto spelling : kCorpus)
    {
        NameId id;
        if (table.TryIntern(spelling, id) != StringStatus::Ok)
        {
            return 1;
        }
    }
    Measure("mutable_find", [&](usize i) {
        NameId id;
        good &= table.TryFind(kCorpus[i % 5], id) == StringStatus::Ok;
        gChecksum += id.Index.Value;
    });
    FrozenStringTable frozen;
    if (table.TryFreeze(frozen) != StringStatus::Ok)
    {
        return 1;
    }
    Measure("frozen_find", [&](usize i) {
        NameId id;
        good &= frozen.TryFind(kCorpus[i % 5], id) == StringStatus::Ok;
        gChecksum += id.Index.Value;
    });
    std::cout << "checksum," << gChecksum << '\n';
    return good ? 0 : 1;
}
