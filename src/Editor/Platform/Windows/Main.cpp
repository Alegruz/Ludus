#include <Ludus/Editor/Pch.hpp>

#include <Ludus/Engine/Core/Assert.h>
#include <Ludus/Engine/Core/CommandLineManager.hpp>
#include <Ludus/Engine/Core/Container/Array.hpp>
#include <Ludus/Engine/Core/Container/String.hpp>
#include <Ludus/Engine/Core/LeakDetection.h>

#include <Ludus/Engine/Platform/Window.hpp>

#undef CreateWindow

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE /*hPrevInstance*/, PWSTR lpCmdLine, int nShowCmd)
{
#if defined(LUDUS_DEBUG)
	// Enable CRT memory leak detection in Debug mode
	ludus::core::debug::InitializeLeakDetection();
#endif

	ludus::core::CommandLineManager<wchar_t> commandLineManager = ludus::core::CommandLineManager<wchar_t>::Create(lpCmdLine);
	const ludus::core::DynamicArray<ludus::core::WString>& arguments = commandLineManager.GetArguments();
	for (const ludus::core::WString& arg : arguments)
	{
		// For demonstration purposes, output each argument to the debug console
		ludus::core::WString debugOutput = L"Argument: ";
		debugOutput.Append(arg.GetData(), arg.GetSize());
		debugOutput.PushBack(L'\n');
		OutputDebugStringW(debugOutput.GetData());
	}

	ludus::platform::WindowManager<ludus::platform::CURRENT_PLATFORM_TYPE> windowManager;
	commandLineManager.ParseCommandLine(windowManager);

	const std::wstring_view windowTitle(EDITOR_WINDOW_TITLE);
	ludus::platform::Window<ludus::platform::CURRENT_PLATFORM_TYPE>::CreateInfo createInfo
	{
		.Title = ludus::core::ConvertWStringToString(ludus::core::WString(windowTitle.data(), static_cast<uint32_t>(windowTitle.size()))),
		.Instance = instance
	};
	
	const ludus::platform::Window<ludus::platform::CURRENT_PLATFORM_TYPE> window = windowManager.CreateWindow(createInfo);
	window.Show(nShowCmd);

	MSG msg = {};
	while (GetMessage(&msg, NULL, 0, 0) > 0)
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

#if defined(LUDUS_DEBUG)
	// Dump all memory leaks to the debug output
	ludus::core::debug::DumpMemoryLeaks();
#endif

	return 0;
}