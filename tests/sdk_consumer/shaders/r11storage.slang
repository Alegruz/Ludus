#include "raster_bindings.slang"

LUDUS_SHADER_BINDING(0) RWStructuredBuffer<uint> Outputs[2] : register(u0);
[shader("compute")][numthreads(1, 1, 1)] void ComputeMain() {
    Outputs[0][0] = 0x12345678U;
    Outputs[1][0] = 0x9abcdef0U;
}
