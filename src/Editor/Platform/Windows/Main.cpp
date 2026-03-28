#include <Ludus/Editor/Pch.hpp>

#include <Ludus/Engine/Core/Assert.h>
#include <Ludus/Engine/Core/CommandLineManager.hpp>
#include <Ludus/Engine/Core/Container/Array.hpp>
#include <Ludus/Engine/Core/Container/String.hpp>
#include <Ludus/Engine/Core/LeakDetection.h>

#include <Ludus/Engine/Core/Math/Trigonometry.hpp>

#include <Ludus/Engine/Platform/Window.hpp>

#include <Ludus/Engine/RHI/Common.h>
#include <Ludus/Engine/RHI/Texture.h>

#include <Ludus/Engine/Renderer/Renderer.h>

#include <Ludus/Engine/Common.h>

#undef CreateWindow

void Main(HINSTANCE instance, PWSTR lpCmdLine, int nShowCmd);
static LRESULT WindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept;

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE /*hPrevInstance*/, PWSTR lpCmdLine, int nShowCmd)
{
	// Scoped memory leak detector - automatically handles snapshots and reporting
	LUDUS_LEAK_DETECTOR();
	
	// Uncomment to break on specific allocation number from leak report:
	// LUDUS_BREAK_ON_ALLOC(253);

	Main(instance, lpCmdLine, nShowCmd);
	
	// Leak detection and reporting happens automatically when scope exits
	return 0;
}

static ludus::renderer::Renderer* gRenderer = nullptr;	// TODO: Remove global, NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
void Main(HINSTANCE instance, PWSTR lpCmdLine, int nShowCmd)
{
	using namespace ludus;
	using namespace ludus::core;
	using namespace ludus::platform;
	using namespace ludus::rhi;
	using namespace ludus::renderer;

	if (!PreflightPlatformOrNotify())
	{
		return;
	}

	CommandLineManager<wchar_t> commandLineManager = CommandLineManager<wchar_t>::Create(lpCmdLine);
	const DynamicArray<WString>& arguments = commandLineManager.GetArguments();
	bool quickExit = false;

	for (const WString& arg : arguments)
	{
		// For demonstration purposes, output each argument to the debug console
		WString debugOutput = L"Argument: ";
		debugOutput.Append(arg.GetData(), arg.GetSize());
		debugOutput.PushBack(L'\n');
		OutputDebugStringW(debugOutput.GetData());
		
		// Check for quick exit flag
		if (arg == L"--quick-exit" || arg == L"--exit")
		{
			quickExit = true;
		}
	}
	
	// Note: Using string literal for ProjectInfo.Name to avoid heap allocation
	const core::ProjectInfo appInfo  // NOLINT(readability-identifier-naming)
	{
		.Name = EDITOR_APP_TITLE,
		.Version = LUDUS_MAKE_API_VERSION(0, 0, 1, 0),
	};

	WindowManager<CURRENT_PLATFORM_TYPE> windowManager;
	windowManager.ParseCommandLineWithContext(commandLineManager);

	Window<CURRENT_PLATFORM_TYPE>::CreateInfo createInfo
	{
		.Title = ConvertWStringToString(WString(EDITOR_WINDOW_TITLE)),
		.Instance = instance,
	};

	core::UniquePtr<Window<CURRENT_PLATFORM_TYPE>>& pWindow = windowManager.CreateWindow(createInfo);
	if( pWindow == nullptr )
	{
		LUDUS_ASSERT_MSG(false, "Failed to create main window.");
		return;
	}
	Window<CURRENT_PLATFORM_TYPE>& window = *pWindow;
	window.SetWindowProcedure(WindowProcedure);

	const Renderer::CreateInfo rendererCreateInfo
	{
		.Width = window.GetWidth(),
		.Height = window.GetHeight(),
		.Format = TextureFormat::RGBA8_UNORM,
		.ApplicationInfo = appInfo,
		.EngineInfo = ENGINE_INFO,
		.Window = &window,
	};
	Renderer Renderer_obj{};  // NOLINT(readability-identifier-naming)
	if (Renderer_obj.Initialize(rendererCreateInfo) == false)
	{
		LUDUS_ASSERT_MSG(false, "Failed to initialize renderer");
		return;
	}
	gRenderer = &Renderer_obj;

	window.Show(nShowCmd);

	// Exit early if quick exit flag is set
	if (quickExit)
	{
		return;
	}

	MSG msg = {};
	while (GetMessage(&msg, NULL, 0, 0) > 0)
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}

static LRESULT WindowProcedure([[maybe_unused]] HWND hwnd, [[maybe_unused]] UINT message, [[maybe_unused]] WPARAM wParam, [[maybe_unused]] LPARAM lParam) noexcept	// NOLINT(bugprone-easily-swappable-parameters)
{
	using namespace ludus;
	using namespace ludus::core;
	using namespace ludus::platform;
	using namespace ludus::rhi;
	using namespace ludus::renderer;

	return false;
}
