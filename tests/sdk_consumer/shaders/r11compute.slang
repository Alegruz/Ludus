#include "raster_bindings.slang"

LUDUS_SHADER_BINDING(0) RWStructuredBuffer<uint> Values : register(u0);
[shader("compute")][numthreads(1, 1, 1)] void ComputeMain(uint3 dispatchThreadId
                                                          : SV_DispatchThreadID) {
    if (dispatchThreadId.x == 0)
    {
        Values[0] = 0x12345678U;
    }
}
