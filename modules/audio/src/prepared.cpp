#include "internal/prepared.hpp"
#include <new>
namespace ludus::audio
{
PreparedClip::Impl::~Impl() noexcept
{
    ::operator delete[](Decoded.Pcm, std::nothrow);
}
PreparedClip::~PreparedClip() noexcept
{
    delete mImpl;
}
uint64 PreparedClip::Bytes() const noexcept
{
    return mImpl != nullptr ? mImpl->Decoded.Frames * mImpl->Decoded.Channels * sizeof(float32) : 0;
}
Status PreparedClip::Decode(std::span<const uint8> encoded, const ClipDescriptor& descriptor, uint32 rate) noexcept
{
    if (encoded.empty() || rate == 0 || (descriptor.SourceLoopEnd == 0 && descriptor.SourceLoopBegin != 0) ||
        (descriptor.SourceLoopEnd != 0 && descriptor.SourceLoopBegin >= descriptor.SourceLoopEnd))
    {
        return Status::InvalidArgument;
    }
    auto* next = new (std::nothrow) Impl();
    if (next == nullptr)
    {
        return Status::OutOfMemory;
    }
    next->Decoded = internal::DecodeResidentClip(encoded, descriptor.Format, rate);
    next->Rate = rate;
    next->AssetHash = descriptor.AssetHash;
    auto& decoded = next->Decoded;
    if (decoded.Result != Status::Ok)
    {
        const auto status = decoded.Result;
        delete next;
        return status;
    }
    if (descriptor.SourceLoopEnd != 0)
    {
        const auto convert = [&](uint64 frame, uint64& result) noexcept {
            const uint64 whole = frame / decoded.SourceRate, remainder = frame % decoded.SourceRate;
            if (whole > (static_cast<uint64>(-1) - rate) / rate)
            {
                return false;
            }
            result = whole * rate + (remainder * rate + decoded.SourceRate / 2) / decoded.SourceRate;
            return true;
        };
        if (descriptor.SourceLoopEnd > decoded.SourceFrames || !convert(descriptor.SourceLoopBegin, next->LoopBegin) ||
            !convert(descriptor.SourceLoopEnd, next->LoopEnd) || next->LoopBegin >= next->LoopEnd ||
            next->LoopEnd > decoded.Frames)
        {
            delete next;
            return Status::InvalidArgument;
        }
    }
    delete mImpl;
    mImpl = next;
    return Status::Ok;
}
} // namespace ludus::audio
