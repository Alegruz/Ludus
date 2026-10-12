#pragma once
// Thanks to Khronos WebGL Working Group, EXT_disjoint_timer_query_webgl2, rev. 4:
// https://registry.khronos.org/webgl/extensions/EXT_disjoint_timer_query_webgl2/
// Reject every pending result when the extension reports GPU_DISJOINT_EXT.
namespace ludus::graphics::rhi::LUDUS_RHI_WEBGL_NAMESPACE
{
namespace
{
GpuTiming gTiming;
// clang-format off
EM_JS(void, TimingGlAdapter, (char* name, uint32 capacity), {
    const gl = GL.currentContext?.GLctx;
    if (!gl) return;
    const ext = gl.getExtension('WEBGL_debug_renderer_info');
    stringToUTF8(ext ? gl.getParameter(ext.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER), name, capacity);
});
EM_JS(int32, TimingGlArm, (), {
    const gl = GL.currentContext?.GLctx;
    if (!gl) return 0;
    const ext = gl.getExtension('EXT_disjoint_timer_query_webgl2');
    if (!ext) return 0;
    const bits = gl.getQuery(ext.TIME_ELAPSED_EXT, ext.QUERY_COUNTER_BITS_EXT);
    if (!bits) return 0;
    const query = gl.createQuery();
    if (!query) return -1;
    if (Module.ludusTiming) Module.ludusTiming.gl.deleteQuery(Module.ludusTiming.query);
    // Clear an earlier disjoint notification before beginning this sample.
    gl.getParameter(ext.GPU_DISJOINT_EXT);
    Module.ludusTiming = {gl, ext, query, open:false}; return bits;
});
EM_JS(void, TimingGlBegin, (), {
    const t = Module.ludusTiming; t.gl.beginQuery(t.ext.TIME_ELAPSED_EXT, t.query); t.open=true;
});
EM_JS(void, TimingGlEnd, (), {
    const t = Module.ludusTiming; t.gl.endQuery(t.ext.TIME_ELAPSED_EXT); t.open=false;
});
EM_JS(int32, TimingGlPoll, (foundation::float64* duration), {
    const t = Module.ludusTiming;
    if (!t || t.gl.isContextLost()) return 3;
    if (t.gl.getParameter(t.ext.GPU_DISJOINT_EXT)) return 2;
    if (!t.gl.getQueryParameter(t.query, t.gl.QUERY_RESULT_AVAILABLE)) return 0;
    const value = t.gl.getQueryParameter(t.query, t.gl.QUERY_RESULT);
    if (!Number.isSafeInteger(value) || value < 0) return 2;
    HEAPF64[duration/8] = value; return 1;
});
EM_JS(void, TimingGlReset, (), {
    const t = Module.ludusTiming;
    if (t) { if(t.open && !t.gl.isContextLost()) t.gl.endQuery(t.ext.TIME_ELAPSED_EXT); t.gl.deleteQuery(t.query); }
    delete Module.ludusTiming;
});
// clang-format on
} // namespace
bool AllocatorBlockBytes(uint64& bytes) noexcept
{
    bytes = 0;
    return false;
}
MeasurementAdapter TimingAdapter() noexcept
{
    MeasurementAdapter output;
    TimingGlAdapter(output.Name, sizeof(output.Name));
    return output;
}
void TimingReset() noexcept
{
    TimingGlReset();
    gTiming = {};
}
GpuTiming TimingPoll() noexcept
{
    if (gTiming.State == GpuTimingState::Pending)
    {
        const auto result = TimingGlPoll(&gTiming.DurationNanoseconds);
        if (result == 1)
        {
            gTiming.State = GpuTimingState::Available;
        }
        else if (result == 2)
        {
            gTiming.State = GpuTimingState::Disjoint;
        }
        else if (result == 3)
        {
            gTiming.State = GpuTimingState::Failed;
        }
    }
    return gTiming;
}
RasterStatus TimingArm() noexcept
{
    const auto state = TimingPoll().State;
    if (state == GpuTimingState::Armed || state == GpuTimingState::Pending)
    {
        return RasterStatus::InvalidState;
    }
    const auto result = TimingGlArm();
    if (result < 0)
    {
        return RasterStatus::OutOfMemory;
    }
    gTiming = { .State = result > 0 ? GpuTimingState::Armed : GpuTimingState::Unavailable };
    if (result > 0)
    {
        gTiming.ValidBits = static_cast<uint32>(result);
    }
    return result > 0 ? RasterStatus::Ready : result == 0 ? RasterStatus::Unsupported : RasterStatus::OutOfMemory;
}
void TimingBegin() noexcept
{
    if (gTiming.State == GpuTimingState::Armed)
    {
        TimingGlBegin();
    }
}
void TimingEnd() noexcept
{
    if (gTiming.State == GpuTimingState::Armed)
    {
        TimingGlEnd();
        gTiming.State = GpuTimingState::Pending;
    }
}
void TimingSubmitted() noexcept {}
bool RasterAllocationBytes(internal::RasterKind, usize, uint64& bytes) noexcept
{
    bytes = 0;
    return false;
}
} // namespace ludus::graphics::rhi::LUDUS_RHI_WEBGL_NAMESPACE
