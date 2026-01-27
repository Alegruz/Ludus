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

#include <Ludus/Engine/Renderer/Renderer.hpp>

#undef CreateWindow

void Main(HINSTANCE instance, PWSTR lpCmdLine, int nShowCmd);
bool WindowProcedure(const ludus::platform::Window<ludus::platform::CURRENT_PLATFORM_TYPE>::ProcedureParams<ludus::platform::CURRENT_PLATFORM_TYPE>& params);

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

static ludus::renderer::Renderer<ludus::rhi::CURRENT_GRAPHICS_API>* gRenderer = nullptr;	// TODO: Remove global, NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
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
		.WindowProcedureOrNull = WindowProcedure,
		.Instance = instance,
	};
	
	Window<CURRENT_PLATFORM_TYPE>& window = windowManager.CreateWindow(createInfo);

	const Renderer<CURRENT_GRAPHICS_API>::CreateInfo rendererCreateInfo
	{
		.Width = window.GetWidth(),
		.Height = window.GetHeight(),
		.Format = TextureFormat::RGBA8_UNORM,
	};
	Renderer<CURRENT_GRAPHICS_API> renderer(rendererCreateInfo);
	gRenderer = &renderer;

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

bool WindowProcedure(const ludus::platform::Window<ludus::platform::CURRENT_PLATFORM_TYPE>::ProcedureParams<ludus::platform::CURRENT_PLATFORM_TYPE>& params)
{
	using namespace ludus;
	using namespace ludus::core;
	using namespace ludus::platform;
	using namespace ludus::rhi;
	using namespace ludus::renderer;

	switch (params.Message)
	{
		case WM_PAINT:
		{
#if defined(LUDUS_GRAPHICS_CPU)
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(params.WindowHandle, &ps);

			gRenderer->RenderFrame();
			const Texture<CURRENT_GRAPHICS_API>& backBufferTexture = gRenderer->GetBackBufferTexture();
			const TextureFormat format = backBufferTexture.GetFormat();
			const uint32_t width = backBufferTexture.GetWidth();
			const uint32_t height = backBufferTexture.GetHeight();

			DWORD bitmapInfoCompression = BI_RGB;
			switch(format)
			{
				case TextureFormat::RGBA8_UNORM:
					[[fallthrough]];
				case TextureFormat::BGRA8_UNORM:
					bitmapInfoCompression = BI_RGB;
					break;
				case TextureFormat::RGBA16_FLOAT:
					[[fallthrough]];
				case TextureFormat::RGBA32_FLOAT:
					// For simplicity, treat float formats as RGB for bitmap creation
					bitmapInfoCompression = BI_RGB;
					break;
				default:
					LUDUS_ASSERT_MSG(false, "Unsupported texture format for bitmap creation");
					break;
			}
			
			const BITMAPINFO bitmapInfo =
			{
				.bmiHeader =
				{
					.biSize		  	 = sizeof(BITMAPINFOHEADER),
					.biWidth         = static_cast<LONG>(width),
					.biHeight        = -static_cast<LONG>(height), // Negative height for top-down bitmap
					.biPlanes        = 1,
					.biBitCount      = static_cast<WORD>(GetBytesPerPixel(format) * 8),
					.biCompression   = bitmapInfoCompression,
					.biSizeImage     = 0,
					.biXPelsPerMeter = 0,
					.biYPelsPerMeter = 0,
					.biClrUsed       = 0,
					.biClrImportant  = 0,
				},
			};

			void* bits = nullptr;
			HBITMAP hBitmap = CreateDIBSection(hdc, &bitmapInfo, DIB_RGB_COLORS, &bits, NULL, 0);
			if (hBitmap != NULL && bits != nullptr)
			{
				// Copy texture data to bitmap
				const core::DynamicArray<uint8_t>& textureData = backBufferTexture.GetData();

				// Assuming format is RGBA8_UNORM for simplicity
				memcpy(bits, textureData.GetData(), static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(GetBytesPerPixel(format))); // 4 bytes per pixel

				HDC memDC = CreateCompatibleDC(hdc);
				HGDIOBJ oldBitmap = SelectObject(memDC, hBitmap);

				BitBlt(hdc, 0, 0, static_cast<int>(width), static_cast<int>(height), memDC, 0, 0, SRCCOPY);

				SelectObject(memDC, oldBitmap);
				DeleteDC(memDC);
				DeleteObject(hBitmap);
			}

			EndPaint(params.WindowHandle, &ps);
			return true; // Message was processed
#else	// defined(LUDUS_GRAPHICS_CPU)
			return false; // Message was not processed
#endif	// defined(LUDUS_GRAPHICS_CPU)
		}
		default:
			return false; // Message was not processed
	}
}
