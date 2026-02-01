#include <Ludus/Engine/Platform/Window.hpp>

#include <Ludus/Engine/Core/SmartPtr.hpp>
#include <Ludus/Engine/Core/Container/String.hpp>
#include <Ludus/Engine/Core/Container/Array.hpp>

#if defined(LUDUS_WINDOWS)
namespace ludus::platform
{
    template class Window<PlatformType::WINDOWS>;
    template class WindowManager<PlatformType::WINDOWS>;
}   // namespace ludus::platform
#endif // defined(LUDUS_WINDOWS)
