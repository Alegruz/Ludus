#include "native_interop_fixture.h"

#include <ludus/foundation/base/core.h>

#include <cstring>
#include <new>

#import <Metal/Metal.h>

namespace native_fixture
{
namespace
{
using namespace ludus::foundation;
struct Source final
{
    id<MTLDevice> Device = nil;
    id<MTLCommandQueue> Queue = nil;
    id<MTLCommandBuffer> Sample = nil;
    id<MTLBuffer> Output = nil;
    usize Retained = 0;
    usize Released = 0;
    static bool Retain(void* context) noexcept
    {
        ++static_cast<Source*>(context)->Retained;
        return true;
    }
    static void Release(void* context) noexcept
    {
        ++static_cast<Source*>(context)->Released;
    }
};
} // namespace
bool CreateForeign(Foreign& fixture) noexcept
{
    auto* source = new (std::nothrow) Source;
    if (source == nullptr)
    {
        return false;
    }
    fixture.Context = source;
    source->Device = MTLCreateSystemDefaultDevice();
    source->Queue = [source->Device newCommandQueue];
    if (source->Device == nil || source->Queue == nil)
    {
        return false;
    }
    fixture.Description =
    {
        .Identity =
        {
            .Api = ludus::graphics::rhi::Backend::Metal,
            .Device = ludus::graphics::rhi::NativeLogicalDevice{(__bridge void*)source->Device},
            .CommandQueue = ludus::graphics::rhi::NativeCommandQueue{(__bridge void*)source->Queue},
        },
        .Lifetime = { .Context = source, .Retain = Source::Retain, .Release = Source::Release },
        .SerializedOwnerQueue = true,
    };
    return true;
}
bool AllowProducer(Foreign&) noexcept
{
    return true;
}
bool RetireConsumers(Foreign& fixture) noexcept
{
    auto* source = static_cast<Source*>(fixture.Context);
    if (source != nullptr && source->Sample != nil)
    {
        if (source->Sample.status >= MTLCommandBufferStatusCommitted)
        {
            [source->Sample waitUntilCompleted];
        }
        source->Sample = nil;
    }
    return true;
}
void DestroyForeign(Foreign& fixture) noexcept
{
    delete static_cast<Source*>(fixture.Context);
    fixture = {};
}
bool SubmitSample(Foreign& fixture, const ludus::graphics::rhi::NativeTextureInfo& image) noexcept
{
    auto& source = *static_cast<Source*>(fixture.Context);
    NSString* text =
        @"#include <metal_stdlib>\nusing namespace metal;\nkernel void sample_native(texture2d<float,access::read> "
        @"image [[texture(0)]], device uchar4* output [[buffer(0)]], uint2 p [[thread_position_in_grid]]) { "
        @"output[p.y*4+p.x]=uchar4(image.read(p)*255.0f); }";
    NSError* error = nil;
    auto library = [source.Device newLibraryWithSource:text options:nil error:&error];
    auto function = [library newFunctionWithName:@"sample_native"];
    auto pipeline = [source.Device newComputePipelineStateWithFunction:function error:&error];
    if (pipeline == nil || error != nil)
    {
        return false;
    }
    source.Output = [source.Device newBufferWithLength:64 options:MTLResourceStorageModeShared];
    source.Sample = [source.Queue commandBuffer];
    auto encoder = [source.Sample computeCommandEncoder];
    if (encoder == nil || source.Output == nil)
    {
        return false;
    }
    [encoder setComputePipelineState:pipeline];
    [encoder setTexture:(__bridge id<MTLTexture>)image.MetalTexture.GetObject() atIndex:0];
    [encoder setBuffer:source.Output offset:0 atIndex:0];
    [encoder dispatchThreads:MTLSizeMake(4, 4, 1) threadsPerThreadgroup:MTLSizeMake(4, 4, 1)];
    [encoder endEncoding];
    [source.Sample commit];
    return true;
}
bool ReadSample(Foreign& fixture, ludus::foundation::uint8 (&pixels)[64]) noexcept
{
    auto& source = *static_cast<Source*>(fixture.Context);
    [source.Sample waitUntilCompleted];
    if (source.Sample.status != MTLCommandBufferStatusCompleted)
    {
        return false;
    }
    std::memcpy(pixels, source.Output.contents, sizeof(pixels));
    // Retained command buffers own their encoded resource references until here.
    source.Sample = nil;
    return true;
}
ludus::foundation::usize Retains(const Foreign& fixture) noexcept
{
    return static_cast<const Source*>(fixture.Context)->Retained;
}
ludus::foundation::usize Releases(const Foreign& fixture) noexcept
{
    return static_cast<const Source*>(fixture.Context)->Released;
}
} // namespace native_fixture
