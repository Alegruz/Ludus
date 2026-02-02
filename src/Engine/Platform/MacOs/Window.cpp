#include <Ludus/Engine/Platform/Window.h>

#if defined(LUDUS_MAC)
namespace ludus::platform
{
    struct WindowMemberVariablesMac final : public WindowMemberVariablesBase
    {
    };

#define mMemberVariablesMac (*static_cast<WindowMemberVariablesMac*>(mMemberVariables.Get()))   // NOLINT(cppcoreguidelines-pro-type-static-cast-downcast,-warnings-as-errors)

    template<PlatformType PLATFORM_TYPE>
    Window<PLATFORM_TYPE>::Window() noexcept requires (PLATFORM_TYPE == PlatformType::MAC)
        : mMemberVariables(core::MakeUnique<WindowMemberVariablesMac>())
    {
    }

    template<PlatformType PLATFORM_TYPE>
    bool Window<PLATFORM_TYPE>::initialize([[maybe_unused]] const CreateInfo& createInfo) noexcept requires (PLATFORM_TYPE == PlatformType::MAC)
    {
        LUDUS_ASSERT_MSG(false, "Mac platform windowing not yet implemented.");
        return false;
    }
    
    template<PlatformType PLATFORM_TYPE>
    void Window<PLATFORM_TYPE>::show([[maybe_unused]] const int32_t commandShowFlag) const noexcept requires (PLATFORM_TYPE == PlatformType::MAC)
    {
        LUDUS_ASSERT_MSG(false, "Mac platform windowing not yet implemented.");
    }

    template<PlatformType PLATFORM_TYPE>
    uint64_t Window<PLATFORM_TYPE>::getPlatformHandle() const noexcept requires (PLATFORM_TYPE == PlatformType::MAC)
    {
        LUDUS_ASSERT_MSG(false, "Mac platform windowing not yet implemented.");
        return 0;
    }

    template class Window<PlatformType::MAC>;
    template class WindowManager<PlatformType::MAC>;
}   // namespace ludus::platform
#endif  // defined(LUDUS_MAC)