#include <ludus/foundation/base/version.hpp>
#include <ludus/foundation/base/pointer.hpp>
#include <ludus/platform/base/window.h>

#include <iostream>

int main()
{
    std::cout << "Ludus " << ludus::foundation::version_string() << '\n';
    std::cout << "Revision: " << ludus::foundation::git_revision() << '\n';
    std::cout << "Compiler: " << ludus::foundation::compiler_identity() << '\n';

    ludus::platform::WindowManager windowManager;
    constexpr ludus::platform::WindowManager::InitializeInfo info =
    {
    };
    std::cout << std::boolalpha << windowManager.Initialize(info) << std::endl;
    const ludus::platform::Window::CreateInfo createInfo =
    {
        .Name = std::string("Test Window"),
    };

    ludus::foundation::core::UniquePtr<ludus::platform::Window> window = nullptr;
    if (!windowManager.CreateWindow(createInfo, window))
    {
        std::cerr << "Failed to create window" << std::endl;
        return 1;
    }

    while (window->HandleEvent({}))
    {
        // Main loop
    }

    return 0;
}
