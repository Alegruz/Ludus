// ABI layout acceptance (requirement L04, design section 6).
//
// Proves the gameplay ABI records are standard-layout POD with the measured
// size/alignment/offsets this target was built with. A silent layout change
// (field reorder, padding shift, type width change) breaks these assertions,
// which is the point: the host rejects a module whose layout disagrees.

#include <ludus/runtime/game_api/abi.h>
#include <ludus/runtime/game_api/api.h>
#include <ludus/runtime/game_api/properties.h>
#include <ludus/runtime/game_api/services.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <type_traits>

using namespace ludus::runtime::game_api;

namespace
{
// Every boundary record must be standard-layout and trivially copyable so its
// bytes can cross the ABI and be snapshotted without running constructors.
template <typename T>
constexpr bool IsAbiPod()
{
    return std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T>;
}
} // namespace

TEST_CASE("boundary records are standard-layout trivially-copyable POD", "[abi]")
{
    STATIC_REQUIRE(IsAbiPod<FrameInput>());
    STATIC_REQUIRE(IsAbiPod<RenderParams>());
    STATIC_REQUIRE(IsAbiPod<HostServices>());
    STATIC_REQUIRE(IsAbiPod<GameMetadata>());
    STATIC_REQUIRE(IsAbiPod<CheckpointHeader>());
    STATIC_REQUIRE(IsAbiPod<CreateInfo>());
    STATIC_REQUIRE(IsAbiPod<GameApiTable>());
    STATIC_REQUIRE(IsAbiPod<ByteView>());
    STATIC_REQUIRE(IsAbiPod<ByteSpan>());
    STATIC_REQUIRE(IsAbiPod<PropertyDescriptor>());
    STATIC_REQUIRE(IsAbiPod<PropertyValue>());
    STATIC_REQUIRE(IsAbiPod<PropertyEdit>());
    STATIC_REQUIRE(IsAbiPod<EditBatchHeader>());
}

TEST_CASE("enum representations are the fixed ABI width", "[abi]")
{
    STATIC_REQUIRE(sizeof(Status) == 4);
    STATIC_REQUIRE(sizeof(Capability) == 4);
    STATIC_REQUIRE(sizeof(LogSeverity) == 4);
    STATIC_REQUIRE(sizeof(PropertyKind) == 4);
    STATIC_REQUIRE(sizeof(PropertyScope) == 4);
    STATIC_REQUIRE(static_cast<ludus::foundation::uint32>(Status::Internal) == 11);
}

TEST_CASE("measured sizes and offsets on this target", "[abi]")
{
    // FrameInput: 2x u64 + 2x f64 + 3x u32 + 4x u8, 8-byte aligned.
    STATIC_REQUIRE(sizeof(FrameInput) == 40);
    STATIC_REQUIRE(alignof(FrameInput) == 8);
    STATIC_REQUIRE(offsetof(FrameInput, FrameIndex) == 0);
    STATIC_REQUIRE(offsetof(FrameInput, DeltaSeconds) == 8);
    STATIC_REQUIRE(offsetof(FrameInput, ElapsedSeconds) == 16);
    STATIC_REQUIRE(offsetof(FrameInput, Width) == 24);
    STATIC_REQUIRE(offsetof(FrameInput, Height) == 28);
    STATIC_REQUIRE(offsetof(FrameInput, HeldActions) == 32);
    STATIC_REQUIRE(offsetof(FrameInput, Paused) == 36);

    STATIC_REQUIRE(sizeof(RenderParams) == 32);
    STATIC_REQUIRE(offsetof(RenderParams, ClearRed) == 0);
    STATIC_REQUIRE(offsetof(RenderParams, Uniform0) == 16);

    STATIC_REQUIRE(sizeof(GameMetadata) == 32 + kIdentityMax);
    STATIC_REQUIRE(sizeof(GameMetadata) == 288);
    STATIC_REQUIRE(offsetof(GameMetadata, StructSize) == 0);
    STATIC_REQUIRE(offsetof(GameMetadata, AbiMajor) == 4);
    STATIC_REQUIRE(offsetof(GameMetadata, Identity) == 32);

    STATIC_REQUIRE(sizeof(CheckpointHeader) == 48);
    STATIC_REQUIRE(offsetof(CheckpointHeader, FormatVersion) == 0);
    STATIC_REQUIRE(offsetof(CheckpointHeader, BodyLength) == 32);
    STATIC_REQUIRE(offsetof(CheckpointHeader, BodyDigest) == 40);

    // ByteView/ByteSpan are a pointer plus a usize.
    STATIC_REQUIRE(sizeof(ByteView) == 2 * sizeof(void*));
    STATIC_REQUIRE(sizeof(ByteSpan) == 2 * sizeof(void*));

    STATIC_REQUIRE(sizeof(HostServices) == 48);
    STATIC_REQUIRE(offsetof(HostServices, StructSize) == 0);
    STATIC_REQUIRE(offsetof(HostServices, Context) == 8);
    STATIC_REQUIRE(offsetof(HostServices, Log) == 16);

    STATIC_REQUIRE(sizeof(CreateInfo) == 56);
    STATIC_REQUIRE(sizeof(GameApiTable) == 168);
    STATIC_REQUIRE(sizeof(PropertyDescriptor) == 112);
    STATIC_REQUIRE(sizeof(PropertyValue) == 296);
    STATIC_REQUIRE(sizeof(PropertyEdit) == 296);
    STATIC_REQUIRE(sizeof(EditBatchHeader) == 16);
}

TEST_CASE("abi version constants", "[abi]")
{
    STATIC_REQUIRE(kAbiMajor == 1);
    STATIC_REQUIRE(kAbiMinor == 0);
    STATIC_REQUIRE(kIdentityMax == 256);
}

TEST_CASE("capability bit helpers", "[abi]")
{
    const auto bits = static_cast<ludus::foundation::uint32>(Capability::Reload | Capability::Properties);
    REQUIRE(HasCapability(bits, Capability::Reload));
    REQUIRE(HasCapability(bits, Capability::Properties));
    REQUIRE_FALSE(HasCapability(bits, Capability::AssetReload));
}
