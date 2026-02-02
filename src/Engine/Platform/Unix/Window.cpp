#include <Ludus/Engine/Platform/Window.h>

#if defined(LUDUS_LINUX)
#include <Ludus/Engine/Core/Math/Rect.hpp>

namespace ludus::platform
{
    struct WindowMemberVariablesLinux final : public WindowMemberVariablesBase
    {
    };

#define mMemberVariablesLinux (*static_cast<WindowMemberVariablesLinux*>(mMemberVariables.Get()))   // NOLINT(cppcoreguidelines-pro-type-static-cast-downcast,-warnings-as-errors)

    template<PlatformType PLATFORM_TYPE>
    Window<PLATFORM_TYPE>::Window() noexcept requires (PLATFORM_TYPE == PlatformType::LINUX)
        : mMemberVariables(core::MakeUnique<WindowMemberVariablesLinux>())
    {
    }

    template<PlatformType PLATFORM_TYPE>
    bool Window<PLATFORM_TYPE>::initialize(const CreateInfo& createInfo) noexcept requires (PLATFORM_TYPE == PlatformType::LINUX)
    {
        LUDUS_ASSERT_MSG(false, "Linux platform windowing not yet implemented.");
        return false;
    }
    
    template<PlatformType PLATFORM_TYPE>
    void Window<PLATFORM_TYPE>::show(const int32_t commandShowFlag) const noexcept requires (PLATFORM_TYPE == PlatformType::LINUX)
    {
        LUDUS_ASSERT_MSG(false, "Linux platform windowing not yet implemented.");
    }

    template<PlatformType PLATFORM_TYPE>
    uint64_t Window<PLATFORM_TYPE>::getPlatformHandle() const noexcept requires (PLATFORM_TYPE == PlatformType::LINUX)
    {
        LUDUS_ASSERT_MSG(false, "Linux platform windowing not yet implemented.");
        return 0;
    }

    template class Window<PlatformType::LINUX>;
    template class WindowManager<PlatformType::LINUX>;
}   // namespace ludus::platform
#endif  // defined(LUDUS_LINUX)