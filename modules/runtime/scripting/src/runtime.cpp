// Thanks to Roblox Corporation, Luau's VM protected-call contract at
// 1eca9fda3e4753a1592000f6cfdf659aaa778b7d (VM/src/ldo.cpp, lapi.cpp),
// https://github.com/luau-lang/luau/tree/1eca9fda3e4753a1592000f6cfdf659aaa778b7d
// and "Embedding a sandboxed Luau virtual machine", https://luau.org/sandbox/.
// We use POD trampolines, a bounded allocator, frozen globals and an explicit
// library allowlist. The reviewed loader ownership adjustment is documented in
// docs/architecture/luau-s0.md; this is not a general untrusted-mod sandbox.
#include <ludus/foundation/base/core.h>
#include <ludus/foundation/memory/allocation_domain.hpp>

#include <cstring>
#include <type_traits>

#include <lua.h>
#include <lualib.h>

#include "internal/runtime.h"

#if !LUA_USE_LONGJMP || defined(__cpp_exceptions)
#    error S1 requires the reviewed exception-free Luau interpreter profile
#endif

namespace ludus::runtime::scripting
{
namespace
{
static_assert(std::is_trivially_destructible_v<CallContext>);
static_assert(std::is_trivially_destructible_v<Identity>);

// Luau fixes its C allocator parameter order. Distinct wrapper types cannot
// change this external signature.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void* Allocate(void* user, void* block, usize old_size, usize new_size) noexcept
{
    auto* memory = static_cast<Memory*>(user);
    const auto& domain = foundation::GetSystemAllocationDomain();
    constexpr usize ALIGNMENT = 16;
    if (new_size == 0)
    {
        memory->Live -= old_size;
        domain.Free(block, old_size, ALIGNMENT);
        return nullptr;
    }
    ++memory->Attempts;
    // FoundationMemory has allocate/free, not realloc. Bound the actual temporary
    // old+new footprint before copying. Rejection preserves the original block.
    if ((memory->FailAt != 0 && memory->Attempts >= memory->FailAt) || memory->Live > memory->Limit ||
        new_size > memory->Limit - memory->Live)
    {
        ++memory->Denied;
        return nullptr;
    }
    void* output = domain.TryAllocate(new_size, ALIGNMENT);
    if (output != nullptr)
    {
        const usize peak = memory->Live + new_size;
        if (peak > memory->Peak)
        {
            memory->Peak = peak;
        }
        if (block != nullptr)
        {
            std::memcpy(output, block, old_size < new_size ? old_size : new_size);
            domain.Free(block, old_size, ALIGNMENT);
        }
        memory->Live = memory->Live - old_size + new_size;
    }
    return output;
}

void Interrupt(lua_State* state, int32 gc) noexcept
{
    CallContext& context = GetCallContext(state);
    if (gc < 0 && ++context.Safepoints >= context.SafepointLimit)
    {
        context.Interrupted = true;
        luaL_error(state, "scripting safepoint limit exceeded");
    }
}

void CopyError(lua_State* state, Diagnostic& diagnostic) noexcept
{
    if (lua_type(state, -1) == LUA_TSTRING)
    {
        usize bytes = 0;
        const char* message = lua_tolstring(state, -1, &bytes);
        if (bytes >= sizeof(diagnostic.Message))
        {
            bytes = sizeof(diagnostic.Message) - 1;
        }
        std::memcpy(diagnostic.Message, message, bytes);
        diagnostic.Message[bytes] = '\0';
    }
}
} // namespace

CallContext& GetCallContext(lua_State* state) noexcept
{
    return *static_cast<CallContext*>(lua_callbacks(state)->userdata);
}

void Reject(lua_State* state, uint32 reason, const char* message) noexcept
{
    GetCallContext(state).NativeStatus = reason;
    luaL_error(state, "%s", message);
}

int32 Runtime::Initialize(lua_State* state) noexcept
{
    auto* runtime = static_cast<Runtime*>(lua_touserdata(state, 1));
    luaopen_base(state);
    lua_settop(state, 0);
    // Remove environment replacement, dynamic authoring, host I/O, GC controls
    // and script protection that could catch our cooperative interruption.
    const char* denied[] = {"print",
                            "loadstring",
                            "getfenv",
                            "setfenv",
                            "newproxy",
                            "collectgarbage",
                            "gcinfo",
                            "pcall",
                            "xpcall",
                            "rawset",
                            "setmetatable"};
    for (const char* name : denied)
    {
        lua_pushnil(state);
        lua_setglobal(state, name);
    }
    if (runtime->mExecution.Installer != nullptr)
    {
        runtime->mExecution.Installer(state);
    }
    luaL_sandbox(state);
    if (luau_load(state,
                  runtime->mExecution.Source,
                  reinterpret_cast<const char*>(runtime->mExecution.Bytecode),
                  runtime->mExecution.Bytes,
                  0) != 0)
    {
        lua_error(state);
    }
    lua_call(state, 0, 1);
    if (lua_type(state, -1) != LUA_TFUNCTION)
    {
        luaL_error(state, "program must return a handler function");
    }
    runtime->mExecution.Handler = lua_ref(state, -1);
    return 0;
}

int32 Runtime::Execute(lua_State* state) noexcept
{
    auto* runtime = static_cast<Runtime*>(lua_touserdata(state, 1));
    lua_settop(state, 0);
    lua_getref(state, runtime->mExecution.Handler);
    const int32 arguments = runtime->mExecution.Arguments(state);
    lua_call(state, arguments, 0);
    return 0;
}

Runtime::~Runtime() noexcept
{
    Close();
}

void Runtime::Close() noexcept
{
    if (mEntered)
    {
        return; // Never destroy an executing VM through a nested native call.
    }
    if (mState != nullptr)
    {
        lua_close(mState);
    }
    mState = nullptr;
    mContext.User = nullptr;
    mContext.Active = false;
}

Status Runtime::Load(const uint8* bytecode, usize bytes, const char* source, Setup installer) noexcept
{
    if (mEntered)
    {
        return Status::Reentrant;
    }
    Close();
    if (bytecode == nullptr || bytes == 0 || source == nullptr)
    {
        return Status::InvalidArtifact;
    }
    mExecution = { .Bytecode = bytecode, .Bytes = bytes, .Source = source, .Installer = installer };
    mContext.Safepoints = 0;
    mContext.Interrupted = false;
    mContext.NativeStatus = 0;
    mState = lua_newstate(Allocate, &mMemory);
    if (mState == nullptr)
    {
        return Status::AllocationFailure;
    }
    lua_callbacks(mState)->userdata = &mContext;
    lua_callbacks(mState)->interrupt = Interrupt;
    mEntered = true;
    const int32 status = lua_cpcall(mState, Initialize, this);
    mEntered = false;
    if (status == LUA_OK)
    {
        return Status::Completed;
    }
    const Status result = status == LUA_ERRMEM ? Status::AllocationFailure
                                               : (mContext.Interrupted ? Status::Interrupted : Status::ScriptFault);
    Close();
    return result;
}

Status Runtime::Invoke(Identity identity, void* user, Setup arguments, Diagnostic& diagnostic) noexcept
{
    if (mEntered)
    {
        return Status::Reentrant;
    }
    diagnostic = {};
    diagnostic.Source = identity;
    if (mState == nullptr || arguments == nullptr || user == nullptr || mContext.Epoch == ~uint64{0})
    {
        diagnostic.Code = Status::NotReady;
        return diagnostic.Code;
    }
    ++mContext.Epoch;
    mContext.User = user;
    mContext.Active = true;
    mContext.Safepoints = 0;
    mContext.Interrupted = false;
    mContext.Operation = 0;
    mContext.NativeStatus = 0;
    mExecution.Arguments = arguments;
    mEntered = true;
    const int32 status = lua_cpcall(mState, Execute, this);
    mEntered = false;
    mContext.Active = false;
    mContext.User = nullptr;
    diagnostic.Operation = mContext.Operation;
    diagnostic.NativeStatus = mContext.NativeStatus;
    if (status != LUA_OK)
    {
        diagnostic.Code =
            status == LUA_ERRMEM
                ? Status::AllocationFailure
                : (mContext.Interrupted ? Status::Interrupted
                                        : (mContext.NativeStatus != 0 ? Status::NativeRejected : Status::ScriptFault));
        CopyError(mState, diagnostic);
        Close();
    }
    return diagnostic.Code;
}
} // namespace ludus::runtime::scripting
