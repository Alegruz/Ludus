#include <Ludus/Engine/RHI/Common.h>

#if defined(LUDUS_GRAPHICS_D3D12)
#define LUDUS_DXGI_VERSION_1_6  (16)
#define LUDUS_DXGI_VERSION_1_5  (15)
#define LUDUS_DXGI_VERSION_1_4  (14)
#define LUDUS_DXGI_VERSION_1_3  (13)
#define LUDUS_DXGI_VERSION_1_2  (12)
#define LUDUS_DXGI_VERSION_1_1  (11)
#define LUDUS_DXGI_VERSION_1_0  (10)

#if __has_include(<dxgi1_6.h>)
    #define LUDUS_DXGI_VERSION  (16)
    #include <dxgi1_6.h>
#elif __has_include(<dxgi1_5.h>)
    #define LUDUS_DXGI_VERSION  (15)
    #include <dxgi1_5.h>
#elif __has_include(<dxgi1_4.h>)
    #define LUDUS_DXGI_VERSION  (14)
    #include <dxgi1_4.h>
#elif __has_include(<dxgi1_3.h>)
    #define LUDUS_DXGI_VERSION  (13)
    #include <dxgi1_3.h>
#elif __has_include(<dxgi1_2.h>)
    #define LUDUS_DXGI_VERSION  (12)
    #include <dxgi1_2.h>
#elif __has_include(<dxgi1_1.h>)
    #define LUDUS_DXGI_VERSION  (11)
    #include <dxgi1_1.h>
#elif __has_include(<dxgi.h>)
    #define LUDUS_DXGI_VERSION  (10)
    #include <dxgi.h>
#else
    #error "DXGI header not found."
#endif  // __has_include(<dxgi1_6.h>)
#include <wrl/client.h>

namespace ludus::rhi
{
#if LUDUS_DXGI_VERSION >= LUDUS_DXGI_VERSION_1_6
    using LudusDxgiFactory = IDXGIFactory7;
#elif LUDUS_DXGI_VERSION == LUDUS_DXGI_VERSION_1_5
    using LudusDxgiFactory = IDXGIFactory5;
#elif LUDUS_DXGI_VERSION == LUDUS_DXGI_VERSION_1_4
    using LudusDxgiFactory = IDXGIFactory4;
#elif LUDUS_DXGI_VERSION == LUDUS_DXGI_VERSION_1_3
    using LudusDxgiFactory = IDXGIFactory3;
#elif LUDUS_DXGI_VERSION == LUDUS_DXGI_VERSION_1_2
    using LudusDxgiFactory = IDXGIFactory2;
#elif LUDUS_DXGI_VERSION >= LUDUS_DXGI_VERSION_1_0
    using LudusDxgiFactory = IDXGIFactory1;
#endif  // LUDUS_DXGI_VERSION >= LUDUS_DXGI_VERSION_1_6

#if LUDUS_DXGI_VERSION >= LUDUS_DXGI_VERSION_1_3
    using LudusDxgiCreatingFactory = IDXGIFactory2;
#else
    using LudusDxgiCreatingFactory = IDXGIFactory1;
#endif  // LUDUS_DXGI_VERSION < LUDUS_DXGI_VERSION_1_3

#if LUDUS_DXGI_VERSION >= LUDUS_DXGI_VERSION_1_5
    using LudusDxgiSwapChain = IDXGISwapChain4;
#elif LUDUS_DXGI_VERSION == LUDUS_DXGI_VERSION_1_4
    using LudusDxgiSwapChain = IDXGISwapChain3;
#elif LUDUS_DXGI_VERSION == LUDUS_DXGI_VERSION_1_3
    using LudusDxgiSwapChain = IDXGISwapChain2;
#elif LUDUS_DXGI_VERSION >= LUDUS_DXGI_VERSION_1_2
    using LudusDxgiSwapChain = IDXGISwapChain1;
#else
    using LudusDxgiSwapChain = IDXGISwapChain;
#endif  // LUDUS_DXGI_VERSION >= LUDUS_DXGI_VERSION_1_5
}   // namespace ludus::rhi 
#endif  // defined(LUDUS_GRAPHICS_D3D12)