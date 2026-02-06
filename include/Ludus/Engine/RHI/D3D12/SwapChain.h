#pragma once

#include <Ludus/Engine/RHI/Instance.h>

#if defined(LUDUS_GRAPHICS_D3D12)
#include <Ludus/Engine/Core/SmartPtr.hpp>

#include <Ludus/Engine/RHI/D3D12/Common.h>

#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace ludus::rhi
{
    struct SwapChainMemberVariablesD3D12 final : public SwapChainMemberVariablesBase
    {
        ComPtr<LudusDxgiSwapChain> DxgiSwapChain = nullptr;
    };
};
#endif  // defined(LUDUS_GRAPHICS_D3D12)