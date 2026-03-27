#pragma once

#include <Ludus/Engine/RHI/Adapter.h>

#if defined(LUDUS_GRAPHICS_D3D12)
#include <Ludus/Engine/RHI/D3D12/Common.h>

#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace ludus::rhi
{
    struct AdapterMemberVariablesD3D12 final : public AdapterMemberVariablesBase<GraphicsApi::D3D12>
    {
        LUDUS_INLINE explicit AdapterMemberVariablesD3D12(const Adapter<GraphicsApi::D3D12>& rhiAdapter) noexcept
            : AdapterMemberVariablesBase<GraphicsApi::D3D12>(rhiAdapter)
        {
        }

        ComPtr<LudusDxgiAdapter> DxgiAdapter = nullptr;
        ComPtr<ID3D12Device> D3D12Device = nullptr;
    };
};
#endif  // defined(LUDUS_GRAPHICS_D3D12)