#include <Ludus/Editor/Pch.hpp>

#include <Ludus/Engine/Core/Assert.h>
#include <Ludus/Engine/Core/CommandLineManager.hpp>
#include <Ludus/Engine/Core/Container/Array.hpp>
#include <Ludus/Engine/Core/Container/String.hpp>

#include <Ludus/Engine/Platform/Window.hpp>

#undef CreateWindow

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, [[maybe_unused]] int commandShowFlag)
{
	ludus::core::CommandLineManager<wchar_t> commandLineManager = ludus::core::CommandLineManager<wchar_t>::Create(commandLine);
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

	ludus::platform::Window<ludus::platform::CURRENT_PLATFORM_TYPE>::CreateInfo createInfo
	{
		.Title = ludus::core::ConvertWStringToString(ludus::core::WString(EDITOR_WINDOW_TITLE)),
		.Instance = instance
	};

	const ludus::platform::Window<ludus::platform::CURRENT_PLATFORM_TYPE> window = windowManager.CreateWindow(createInfo);
	window.Show(commandShowFlag);

	MSG msg = {};
	while (GetMessage(&msg, NULL, 0, 0) > 0)
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return 0;
}