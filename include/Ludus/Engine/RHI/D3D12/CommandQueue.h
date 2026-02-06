#pragma once

#include <Ludus/Engine/RHI/CommandQueue.h>

#if defined(LUDUS_GRAPHICS_D3D12)
#include <Ludus/Engine/RHI/D3D12/Common.h>

#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace ludus::rhi
{
    struct CommandQueueMemberVariablesD3D12 final : public CommandQueueMemberVariablesBase
    {
        ComPtr<ID3D12CommandQueue> CommandQueue = nullptr;
    };
};
#endif  // defined(LUDUS_GRAPHICS_D3D12)