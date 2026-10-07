#pragma once
#include <ludus/foundation/base/core.h>

struct lua_State;

namespace ludus::runtime::scripting
{
using namespace foundation;

// Private S1 surface; owner-thread only. No VM/native pointer enters a saved
// gameplay record. See docs/architecture/luau-s1.md for ownership and limitations.
enum class Status : uint8
{
    Completed,
    InvalidArtifact,
    NotReady,
    Reentrant,
    ScriptFault,
    NativeRejected,
    AllocationFailure,
    Interrupted
};
struct Identity
{
    uint64 Asset = 0;
    uint64 Revision = 0;
    uint64 Execution = 0;
    uint64 Instance = 0;
    uint64 World = 0;
    uint64 Session = 0;
    uint64 Tick = 0;
    uint8 Phase = 0;
    uint32 EntitySlot = 0;
    uint32 EntityGeneration = 0;
    char Entrypoint[32] = "OnInteract";
};
struct Diagnostic
{
    Status Code = Status::Completed;
    Identity Source;
    uint32 Operation = 0;
    uint32 NativeStatus = 0;
    char Message[192] = {};
};
struct Memory
{
    usize Live = 0;
    usize Peak = 0;
    usize Attempts = 0;
    usize FailAt = 0;
    usize Denied = 0;
    usize Limit = usize{8} * 1024 * 1024;
};
// This record, every generated binding local, and every crossed VM frame must
// stay trivially destructible. Native services finish before the next VM call.
struct CallContext
{
    void* User = nullptr;
    uint64 Epoch = 0;
    bool Active = false;
    bool Interrupted = false;
    uint32 Safepoints = 0;
    uint32 SafepointLimit = 100000;
    uint32 Operation = 0;
    uint32 NativeStatus = 0;
};
using Setup = int32 (*)(lua_State*) noexcept;
[[nodiscard]] CallContext& GetCallContext(lua_State* state) noexcept;
// Records a contract violation before entering protected VM error recovery.
void Reject(lua_State* state, uint32 reason, const char* message) noexcept;

class Runtime final
{
public:
    Runtime() noexcept = default;
    ~Runtime() noexcept;
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    // Trusted bytecode from the paired cooker only. Loading closes any previous
    // program; immutable package replacement belongs to S2. Installer pushes no
    // return values. Program must return one non-yielding handler function.
    [[nodiscard]] Status Load(const uint8* bytecode, usize bytes, const char* source, Setup installer) noexcept;
    // Arguments pushes config/state/event/API. All effects live in User's POD
    // candidate. Fault destroys this VM and requires an explicit new Load.
    [[nodiscard]] Status Invoke(Identity identity, void* user, Setup arguments, Diagnostic& diagnostic) noexcept;
    void Close() noexcept;
    [[nodiscard]] const Memory& GetMemory() const noexcept
    {
        return mMemory;
    }
    // Private acceptance hooks, not a shipping control surface.
    void SetAllocationFailure(usize attempt) noexcept
    {
        mMemory.FailAt = attempt;
    }
    void SetHeapLimit(usize bytes) noexcept
    {
        mMemory.Limit = bytes;
    }
    void SetSafepointLimit(uint32 count) noexcept
    {
        mContext.SafepointLimit = count;
    }

private:
    struct Execution
    {
        const uint8* Bytecode = nullptr;
        usize Bytes = 0;
        const char* Source = nullptr;
        Setup Installer = nullptr;
        Setup Arguments = nullptr;
        int32 Handler = -1;
    };
    static int32 Initialize(lua_State* state) noexcept;
    static int32 Execute(lua_State* state) noexcept;
    lua_State* mState = nullptr;
    Memory mMemory;
    CallContext mContext;
    Execution mExecution;
    bool mEntered = false;
};
} // namespace ludus::runtime::scripting
