#pragma once
#include <ludus/foundation/base/core.h>

namespace ludus::runtime::scripting
{
using namespace foundation;
enum class ResumeMode : uint8
{
    Continue,
    Into,
    Over,
    Out
};
enum class DebugKind : uint8
{
    Nil,
    Boolean,
    Number,
    StringBytes,
    Opaque,
    Nonfinite
};
struct BreakpointSpec
{
    uint64 Asset = 0;
    int32 Line = 0;
    bool Enabled = true;
};
struct DebugValue
{
    char Name[48] = {};
    DebugKind Kind = DebugKind::Opaque;
    float64 Number = 0;
    bool Boolean = false;
    char Bytes[129] = {}; // At most 64 string bytes, hex; no VM conversion/getter.
    bool Truncated = false;
};
struct DebugFrame
{
    char Source[96] = {};
    char Function[48] = {};
    int32 Line = 0;
};
struct DebugSnapshot
{
    DebugFrame Frames[8];
    DebugValue Locals[16];
    uint32 FrameCount = 0;
    uint32 LocalCount = 0;
    int32 Depth = 0;
    uint64 Stop = 0;
    bool Truncated = false;
};
// Trusted immutable catalog entries. The cooker bounds and authenticates the
// acquisition separately; hashes alone never admit arbitrary bytecode.
struct Program
{
    uint64 Asset = 0;
    uint64 Revision = 0;
    const uint8* Code = nullptr;
    usize Bytes = 0;
    const char* Source = nullptr;
    uint64 Dependencies[8] = {};
    uint8 DependencyCount = 0;
    bool Entrypoint = true;
};
} // namespace ludus::runtime::scripting
