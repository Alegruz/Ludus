#include "raster_bindings.slang"

struct Settings
{
    uint4 Value;
};
LUDUS_SHADER_BINDING(0) StructuredBuffer<uint> Primary : register(t0);
LUDUS_SHADER_BINDING(1) StructuredBuffer<uint> Secondary : register(t1);
LUDUS_SHADER_BINDING(2) StructuredBuffer<uint> Third : register(t2);
LUDUS_SHADER_BINDING(3) StructuredBuffer<uint> Fourth : register(t3);
LUDUS_SHADER_BINDING(4) RWStructuredBuffer<uint> Output : register(u4);
LUDUS_SHADER_BINDING(5) ConstantBuffer<Settings> FirstSettings : register(b5);
LUDUS_SHADER_BINDING(6) ConstantBuffer<Settings> SecondSettings : register(b6);
LUDUS_SHADER_BINDING(7) ConstantBuffer<Settings> ThirdSettings : register(b7);
LUDUS_SHADER_BINDING(8) ConstantBuffer<Settings> FourthSettings : register(b8);

[shader("compute")][numthreads(1, 1, 1)] void ComputeMain() {
    Output[0] = Primary[FirstSettings.Value.x] + Secondary[0] + Third[0] + Fourth[0] + FirstSettings.Value.y +
                SecondSettings.Value.x + ThirdSettings.Value.x + FourthSettings.Value.x;
}
