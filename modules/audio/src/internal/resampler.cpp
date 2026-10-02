#include "internal/resampler.hpp"

#include "miniaudio.h"

#include <new>

namespace ludus::audio::internal
{
namespace
{
[[nodiscard]] ma_resampler* AsResampler(void* p) noexcept
{
    return static_cast<ma_resampler*>(p);
}
} // namespace

Resampler::~Resampler() noexcept
{
    Uninit();
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): named rate args.
bool Resampler::Init(uint32 channels, uint32 rateIn, uint32 rateOut, uint32 lpfOrder) noexcept
{
    if (mBackend != nullptr)
    {
        return true; // already initialized
    }
    if ((channels != 1 && channels != 2) || rateIn == 0 || rateOut == 0)
    {
        return false;
    }

    ma_resampler_config config =
        ma_resampler_config_init(ma_format_f32, channels, rateIn, rateOut, ma_resample_algorithm_linear);
    config.linear.lpfOrder = lpfOrder > MA_MAX_FILTER_ORDER ? MA_MAX_FILTER_ORDER : lpfOrder;

    // Allocate the resampler object and its external heap once, outside the
    // render path. We query the heap size, allocate it, then init in place.
    auto* resampler = new (std::nothrow) ma_resampler{};
    if (resampler == nullptr)
    {
        return false;
    }

    size_t heapSize = 0;
    if (ma_resampler_get_heap_size(&config, &heapSize) != MA_SUCCESS)
    {
        delete resampler;
        return false;
    }
    void* heap = nullptr;
    if (heapSize != 0)
    {
        heap = ::operator new(heapSize, std::nothrow);
        if (heap == nullptr)
        {
            delete resampler;
            return false;
        }
    }

    if (ma_resampler_init_preallocated(&config, heap, resampler) != MA_SUCCESS)
    {
        ::operator delete(heap, std::nothrow);
        delete resampler;
        return false;
    }

    mBackend = resampler;
    mHeap = heap;
    mChannels = channels;
    return true;
}

void Resampler::Uninit() noexcept
{
    if (mBackend != nullptr)
    {
        ma_resampler_uninit(AsResampler(mBackend), nullptr);
        delete AsResampler(mBackend);
        mBackend = nullptr;
    }
    if (mHeap != nullptr)
    {
        ::operator delete(mHeap, std::nothrow);
        mHeap = nullptr;
    }
    mChannels = 0;
}

bool Resampler::SetRate(uint32 rateIn, uint32 rateOut) noexcept
{
    if (mBackend == nullptr || rateIn == 0 || rateOut == 0)
    {
        return false;
    }
    return ma_resampler_set_rate(AsResampler(mBackend), rateIn, rateOut) == MA_SUCCESS;
}

void Resampler::Reset() noexcept
{
    if (mBackend != nullptr)
    {
        ma_resampler_reset(AsResampler(mBackend));
    }
}

bool Resampler::Process(const float32* in, uint64* frameCountIn, float32* out, uint64* frameCountOut) noexcept
{
    if (mBackend == nullptr)
    {
        return false;
    }
    ma_uint64 inCount = frameCountIn != nullptr ? *frameCountIn : 0;
    ma_uint64 outCount = frameCountOut != nullptr ? *frameCountOut : 0;
    const ma_result r = ma_resampler_process_pcm_frames(AsResampler(mBackend), in, &inCount, out, &outCount);
    if (frameCountIn != nullptr)
    {
        *frameCountIn = inCount;
    }
    if (frameCountOut != nullptr)
    {
        *frameCountOut = outCount;
    }
    return r == MA_SUCCESS;
}

uint64 Resampler::RequiredInputFrames(uint64 outputFrames) const noexcept
{
    if (mBackend == nullptr)
    {
        return 0;
    }
    ma_uint64 required = 0;
    if (ma_resampler_get_required_input_frame_count(AsResampler(mBackend), outputFrames, &required) != MA_SUCCESS)
    {
        return 0;
    }
    return required;
}

} // namespace ludus::audio::internal
