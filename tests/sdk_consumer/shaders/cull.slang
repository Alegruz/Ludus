// One deterministic invocation compacts visible instances and publishes a
// bounded indexed command. The CPU variant in ordered_frame.h uses the same
// authored candidates and predicate; no atomics or cross-workgroup order needed.
[[vk::binding(0, 0)]] StructuredBuffer<float2> candidates : register(t0);
[[vk::binding(1, 0)]] RWStructuredBuffer<float2> visible : register(u1);
[[vk::binding(2, 0)]] RWStructuredBuffer<uint> arguments : register(u2);

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain()
{
    uint count = 0;
    for (uint i = 0; i < 4; ++i)
    {
        float2 offset = candidates[i];
        if (offset.x >= -0.75 && offset.x <= 0.75)
            visible[count++] = offset;
    }
    arguments[0] = 6;
    arguments[1] = count;
    arguments[2] = 0;
    arguments[3] = 0;
    arguments[4] = 0;
}
