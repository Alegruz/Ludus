#include <Ludus/Engine/RHI/Texture.h>

namespace ludus::rhi
{
    template<GraphicsApi TApi>
    Texture<TApi>::Texture(const CreateInfo& createInfo)
        requires(TApi == GraphicsApi::CPU)
        : mWidth(createInfo.Width)
        , mHeight(createInfo.Height)
        , mFormat(createInfo.Format)
        , mData(createInfo.Width * createInfo.Height * GetBytesPerPixel(createInfo.Format), 0u)
    {
    }

    // Explicitly instantiate the CPU texture so the symbol is emitted from the shared library
    template class Texture<GraphicsApi::CPU>;
}   // namespace ludus::rhi