#pragma once

// Project descriptor version 1 and its draft/saved value types.
//
// Private editor header (not installed). Holds the in-memory representation of
// `ludus.project.json` and the result/error vocabulary shared by parsing,
// validation and the UI. See .kiro/specs/editor-workspace/design.md section 4.
//
// All error handling is explicit (status enums / returned structs); this header
// introduces no exception-dependent path (AGENTS.md "no C++ exceptions").

#include <ludus/foundation/base/types.h>

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace ludus::editor
{
using foundation::uint64;
using foundation::usize;

// Supported descriptor providers. There is no auto-detection: the file names
// exactly one of these (design section 4).
enum class Provider : foundation::uint8
{
    Ludus, // engine checkout; reuses bootstrap/managed tools
    Cmake, // external project with its own CMakePresets.json
};

// Stable result/error codes. These mirror the protocol result codes in design
// section 8 so a parse/validation failure and a tool failure speak the same
// vocabulary. User-facing messages are produced separately and stay readable
// independently of the code name.
enum class ResultCode : foundation::uint8
{
    Ok,
    InvalidProject,
    UnsupportedVersion,
    Conflict,
    MissingTools,
    MissingDebugger,
    DebuggerFailed,
    BootstrapStale,
    Busy,
    ConfigureFailed,
    TargetInvalid,
    ReplyInvalid,
    BuildFailed,
    ArtifactInvalid,
    SpawnFailed,
    RuntimeFailed,
    RuntimeSignaled,
    ProtocolError,
    Cancelled,
    ReleaseFailed,
    CleanupUnknown,
};

// A stable, user-readable name for a result code. Independent of any particular
// message so logs and Copy Job Details can label a result unambiguously.
[[nodiscard]] const char* ResultCodeName(ResultCode code) noexcept;

// The committed engine requirement (version-2, provider `cmake` only). Mirrors
// ludus_tools.descriptor.EngineRequirement. `Version` is an exact release token,
// never a floating range. Only populated when Version == 2 and provider cmake.
struct EngineRequirement
{
    QString Version;        // exact release token, <= 128 bytes
    QStringList Components; // requested public modules
    QStringList Features;   // required feature flags

    [[nodiscard]] friend bool operator==(const EngineRequirement&, const EngineRequirement&) = default;
};

// Identifies the generation inputs (version-2). Mirrors
// ludus_tools.descriptor.TemplateRef.
struct TemplateRef
{
    QString Id;                     // bounded identifier token
    foundation::uint64 Version = 0; // positive integer

    [[nodiscard]] friend bool operator==(const TemplateRef&, const TemplateRef&) = default;
};

// The documented, persisted fields of a descriptor. Version 1 keeps exactly the
// original fields; version 2 additionally carries `engine` (provider cmake only)
// and `template`. Only these are serialized, in a stable order (design section
// 4). Paths are stored exactly as written in the file (relative); resolution/
// canonicalization happens later and never relative to the editor launch
// directory.
struct ProjectDescriptor
{
    foundation::uint32 Version = 1; // 1 or 2
    QString Name;                   // nonempty, <= 128 UTF-8 bytes
    Provider ProviderKind = Provider::Ludus;
    QString SourceDir;   // relative, <= 4096 UTF-8 bytes
    QString Preset;      // v1: linux-clang-{debug,development}; v2 adds -release
    QString Target;      // ASCII [A-Za-z0-9_][A-Za-z0-9_.+-]*, <= 256 bytes
    QString RunCwd;      // relative, <= 4096 UTF-8 bytes
    QStringList RunArgs; // <= 64 items, each <= 4096 bytes, total <= 32 KiB; empties valid

    bool HasEngine = false;   // true only for a version-2 cmake project
    EngineRequirement Engine; // valid only when HasEngine
    bool HasTemplate = false; // optional version-2 field
    TemplateRef Template;     // valid only when HasTemplate

    [[nodiscard]] friend bool operator==(const ProjectDescriptor& lhs, const ProjectDescriptor& rhs) = default;
};

// Outcome of parsing/validating descriptor bytes. On success Code == Ok and
// Descriptor is populated; otherwise Message explains the problem and names the
// offending field where possible. Message is bounded by the caller for display.
struct ParseOutcome
{
    ResultCode Code = ResultCode::InvalidProject;
    ProjectDescriptor Descriptor;
    QString Message;

    [[nodiscard]] bool Ok() const noexcept
    {
        return Code == ResultCode::Ok;
    }
};

// Documented size limits (design section 4). Centralized so C++ and Python
// validation cannot drift (both consume the shared fixtures).
namespace limits
{
inline constexpr usize MaxFileBytes = usize{64} * 1024u;
inline constexpr usize MaxNameBytes = 128u;
inline constexpr usize MaxPathBytes = 4096u;
inline constexpr usize MaxTargetBytes = 256u;
inline constexpr usize MaxArgCount = 64u;
inline constexpr usize MaxArgBytes = 4096u;
inline constexpr usize MaxArgsTotalBytes = usize{32} * 1024u;
// Version-2 additions (kept equal to ludus_tools.descriptor).
inline constexpr usize MaxVersionBytes = 128u;
inline constexpr usize MaxComponentCount = 64u;
inline constexpr usize MaxComponentBytes = 128u;
inline constexpr usize MaxFeatureCount = 64u;
inline constexpr usize MaxFeatureBytes = 128u;
inline constexpr usize MaxTemplateIdBytes = 128u;
} // namespace limits

} // namespace ludus::editor
