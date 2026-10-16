#pragma once

#include "raster_bindings.slang"

// These slots are injected by the installed RHI shader cooker from its binding
// boundary. Native code uses the matched constants in rhi/profile.h; the shader
// reflection regressions check their correspondence. Consumers never bind this
// immutable buffer themselves. See fullscreen-rendering.md, R11c.
#if !defined(LUDUS_RUNTIME_BINDING_LENGTH_SLOT)
#    error Use the installed RUNTIME_BINDING_ARRAYS shader profile.
#endif
struct RuntimeBindingLength
{
    uint4 CountAndPadding;
};
#define LUDUS_RUNTIME_REGISTER_IMPL(prefix, slot) prefix##slot
#define LUDUS_RUNTIME_REGISTER(prefix, slot) LUDUS_RUNTIME_REGISTER_IMPL(prefix, slot)
LUDUS_SHADER_BINDING(LUDUS_RUNTIME_BINDING_LENGTH_SLOT)
ConstantBuffer<RuntimeBindingLength> RuntimeLength
    : register(LUDUS_RUNTIME_REGISTER(b, LUDUS_RUNTIME_BINDING_LENGTH_SLOT));

uint BoundRuntimeBindingIndex(uint index)
{
    return min(index, RuntimeLength.CountAndPadding.x - 1U);
}
