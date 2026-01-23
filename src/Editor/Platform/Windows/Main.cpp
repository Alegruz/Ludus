#include <Ludus/Editor/Pch.hpp>

#include <Ludus/Engine/Core/Assert.h>
#include <Ludus/Engine/Core/CommandLineManager.hpp>
#include <Ludus/Engine/Core/Container/Array.hpp>
#include <Ludus/Engine/Core/Container/String.hpp>
#include <Ludus/Engine/Core/LeakDetection.h>

#include <Ludus/Engine/Core/Math/Trigonometry.hpp>

#include <Ludus/Engine/Platform/Window.hpp>

#undef CreateWindow

void Main(HINSTANCE instance, PWSTR lpCmdLine, int nShowCmd);

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

void Main(HINSTANCE instance, PWSTR lpCmdLine, int nShowCmd)
{
	using namespace ludus;
	using namespace ludus::core;
	using namespace ludus::platform;

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

	const float angle = Pi<float>() / 4.0f; // 45 degrees in radians
	const float sine = Sin(angle);
	const float cosine = Cos(angle);
	LUDUS_ASSERT_MSG(sine > 0.7071f && sine < 0.7072f, "Sine calculation is incorrect");	// NOLINT(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
	LUDUS_ASSERT_MSG(cosine > 0.7071f && cosine < 0.7072f, "Cosine calculation is incorrect");	// NOLINT(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

	WindowManager<CURRENT_PLATFORM_TYPE> windowManager;
	commandLineManager.ParseCommandLine(windowManager);

	Window<CURRENT_PLATFORM_TYPE>::CreateInfo createInfo
	{
		.Title = ConvertWStringToString(WString(EDITOR_WINDOW_TITLE)),
		.Instance = instance
	};
	
	const Window<CURRENT_PLATFORM_TYPE> window = windowManager.CreateWindow(createInfo);
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