#include <Ludus/Editor/Pch.hpp>

#include <Ludus/Engine/Core/CommandLineManager.hpp>
#include <Ludus/Engine/Core/Container/Array.hpp>
#include <Ludus/Engine/Core/Container/String.hpp>

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

void PrintWin32Error() noexcept;

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

	WNDCLASSEX windowClassEx
	{
		.cbSize        	= sizeof(WNDCLASSEX),
		.style		 	= CS_HREDRAW | CS_VREDRAW,
		.lpfnWndProc   	= WindowProc,
		.cbClsExtra   	= 0,
		.cbWndExtra   	= 0,
		.hInstance     	= instance,
		.hIcon        	= LoadIcon(NULL, IDI_APPLICATION),
		.hCursor      	= LoadCursor(NULL, IDC_ARROW),
		.hbrBackground	= reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
		.lpszMenuName  	= NULL,
		.lpszClassName 	= EDITOR_WINDOW_CLASS_NAME,
		.hIconSm      	= LoadIcon(NULL, IDI_APPLICATION)
	};

	if( RegisterClassEx(&windowClassEx) == 0 )
	{
		PrintWin32Error();
		return 0;
	}

	HWND hwnd = CreateWindowEx(
		0,
		EDITOR_WINDOW_CLASS_NAME,
		EDITOR_WINDOW_TITLE,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		NULL,
		NULL,
		instance,
		nullptr
	);
	if (hwnd == NULL)
	{
		PrintWin32Error();
		return 0;
	}

	ShowWindow(hwnd, commandShowFlag);

	MSG msg = {};
	while (GetMessage(&msg, NULL, 0, 0) > 0)
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return 0;
}


LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void PrintWin32Error() noexcept
{
	DWORD errorCode = GetLastError();
	LPWSTR messageBuffer = nullptr;
	
	FormatMessageW(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr,
		errorCode,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		reinterpret_cast<LPWSTR>(&messageBuffer),
		0,
		nullptr
	);
	
	if (messageBuffer != nullptr)
	{
		OutputDebugStringW(messageBuffer);
		LocalFree(messageBuffer);
	}
}