#include <Ludus/Editor/Pch.hpp>

#include <Ludus/Engine/Core/Assert.h>
#include <Ludus/Engine/Core/CommandLineManager.hpp>
#include <Ludus/Engine/Core/Container/Array.hpp>
#include <Ludus/Engine/Core/Container/String.hpp>
#include <Ludus/Engine/Core/LeakDetection.h>

#include <Ludus/Engine/Core/Math/Trigonometry.hpp>

#include <Ludus/Engine/RHI/Common.h>
#include <Ludus/Engine/RHI/Texture.h>
#include <Ludus/Engine/Renderer/Renderer.hpp>

// Window-related code is omitted for MacOS for now

void Main(int argc, char** argv);

int main(int argc, char** argv)
{
    // Scoped memory leak detector - automatically handles snapshots and reporting
    LUDUS_LEAK_DETECTOR();

    // Uncomment to break on specific allocation number from leak report:
    // LUDUS_BREAK_ON_ALLOC(253);

    Main(argc, argv);

    // Leak detection and reporting happens automatically when scope exits
    return 0;
}

static ludus::renderer::Renderer<ludus::rhi::CURRENT_GRAPHICS_API>* gRenderer = nullptr; // TODO: Remove global, NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
void Main(int argc, char** argv)
{
    using namespace ludus;
    using namespace ludus::core;
    using namespace ludus::rhi;
    using namespace ludus::renderer;

    CommandLineManager<char> commandLineManager = CommandLineManager<char>::Create(argc, argv);
    const DynamicArray<String>& arguments = commandLineManager.GetArguments();
    bool quickExit = false;

    for (const String& arg : arguments)
    {
        // For demonstration purposes, output each argument
        printf("Argument: %s\n", arg.GetData());
        if (arg == "--quick-exit" || arg == "--exit")
        {
            quickExit = true;
        }
    }

    const float angle = Pi<float>() / 4.0f; // 45 degrees in radians
    const float sine = Sin(angle);
    const float cosine = Cos(angle);
    LUDUS_ASSERT_MSG(sine > 0.7071f && sine < 0.7072f, "Sine calculation is incorrect");
    LUDUS_ASSERT_MSG(cosine > 0.7071f && cosine < 0.7072f, "Cosine calculation is incorrect");

    // Window creation and rendering loop omitted for MacOS for now

    // Exit early if quick exit flag is set
    if (quickExit)
    {
        return;
    }

    // Placeholder for main loop
    // while (running) { /* ... */ }
}
