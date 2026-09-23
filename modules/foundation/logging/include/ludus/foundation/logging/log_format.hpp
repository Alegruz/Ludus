#pragma once

// -----------------------------------------------------------------------------
// Opt-in typed formatting surface for the LUDUS_LOG_* severity macros.
//
// Depends on <ludus/foundation/logging/log.hpp> for the gate/severity/category
// vocabulary; log.hpp never includes this header, so translation units that
// only submit raw text (LUDUS_LOG_TEXT) never pay the formatting-header cost
// (requirements R21).
//
// This surface uses the bounded, no-heap typed formatter selected by the
// Phase-2 measurement (see .kiro/specs/logging-redesign/design.md section 5 and
// the decision log). It does NOT include <format>: call sites pack their
// arguments into a small FormatArg array and the parsing/conversion happen once
// in the compiled engine. The result carries explicit truncation/error status;
// a bad or oversized format degrades to a bounded marker, never a throw, an
// allocation, or std::terminate (requirements R19, R25, R38).
//
// The legacy std::format-based behavior lives in the explicitly-temporary
// <ludus/foundation/logging/log_compat_format.hpp> adapter for un-migrated call
// sites (requirements R53); it is excluded from the lightweight-core budget.
//
// Escaped braces are handled consistently for every argument count, including
// zero arguments: "escaped {{braces}}" renders "escaped {braces}" (R17).
// -----------------------------------------------------------------------------

#include <ludus/foundation/base/types.h>
#include <ludus/foundation/logging/log.hpp>

#include <source_location>
#include <span>
#include <string_view>

namespace ludus::foundation::logging
{

// Argument tag for the narrow typed grammar. Kept in the public header because
// packing happens at the call site; the values are lightweight scalars/views.
enum class ArgTag : uint8
{
    StrView,
    Bool,
    I64,
    U64,
    Ptr,
    F64,
};

// Small POD carried on the producer stack. Strings are BORROWED only for the
// duration of the submit call; the engine copies formatted bytes into a bounded
// buffer before returning, so nothing borrowed escapes (requirements R24).
struct FormatArg
{
    ArgTag Tag;
    union
    {
        std::string_view S;
        bool B;
        int64 I;
        uint64 U;
        const void* P;
        float64 D;
    };

    FormatArg(std::string_view s) noexcept : Tag(ArgTag::StrView), S(s) {}
    FormatArg(const char* s) noexcept : Tag(ArgTag::StrView), S(s == nullptr ? std::string_view{} : std::string_view{s})
    {
    }
    FormatArg(bool b) noexcept : Tag(ArgTag::Bool), B(b) {}
    FormatArg(int8 v) noexcept : Tag(ArgTag::I64), I(v) {}
    FormatArg(int16 v) noexcept : Tag(ArgTag::I64), I(v) {}
    FormatArg(int32 v) noexcept : Tag(ArgTag::I64), I(v) {}
    FormatArg(int64 v) noexcept : Tag(ArgTag::I64), I(v) {}
    FormatArg(uint8 v) noexcept : Tag(ArgTag::U64), U(v) {}
    FormatArg(uint16 v) noexcept : Tag(ArgTag::U64), U(v) {}
    FormatArg(uint32 v) noexcept : Tag(ArgTag::U64), U(v) {}
    FormatArg(uint64 v) noexcept : Tag(ArgTag::U64), U(v) {}
    FormatArg(const void* p) noexcept : Tag(ArgTag::Ptr), P(p) {}
    FormatArg(float32 v) noexcept : Tag(ArgTag::F64), D(static_cast<float64>(v)) {}
    FormatArg(float64 v) noexcept : Tag(ArgTag::F64), D(v) {}
};

} // namespace ludus::foundation::logging

namespace ludus::foundation::logging::detail
{

// Admission-checked format-and-submit boundary, defined once in logger.cpp. Not
// a public unfiltered dispatch: reached only after the gate macro admits the
// record (requirements R20).
void SubmitFormat(LogLevel level,
                  LogCategory category,
                  const std::source_location& location,
                  std::string_view format,
                  std::span<const FormatArg> args) noexcept;

} // namespace ludus::foundation::logging::detail

// -----------------------------------------------------------------------------
// Public typed-format macros. Each resolves its category exactly once and gates
// twice (compile-time via LUDUS_COMPILED_LOG_LEVEL, run-time via ShouldLog).
// Below the compiled level a macro becomes ((void)0): neither the category nor
// the arguments are evaluated (requirements R12). The arguments are packed into
// a fixed FormatArg array on the caller's stack; there is no per-site <format>
// instantiation.
// -----------------------------------------------------------------------------
// The format string is the first variadic token; separate it from the args so a
// zero-argument call still compiles (an empty FormatArg[] is ill-formed, so the
// zero-argument path uses an empty span).
#define LUDUS_LOG_DISPATCH_(level_name, category, fmt, ...)                                                            \
    LUDUS_LOG_GATE_(::ludus::foundation::logging::LogLevel::level_name, (category), {                                  \
        ::ludus::foundation::logging::detail::LudusLogFormat_(ludus_lvl_,                                              \
                                                              ludus_cat_,                                              \
                                                              ::std::source_location::current(),                       \
                                                              (fmt)__VA_OPT__(, ) __VA_ARGS__);                        \
    })

namespace ludus::foundation::logging::detail
{

// Variadic helper that packs into a fixed stack array and forwards to the
// non-template SubmitFormat. Kept tiny so per-call-site code stays small; the
// parsing/conversion machinery is compiled once in the engine.
template <typename... Args>
inline void LudusLogFormat_(LogLevel level,
                            LogCategory category,
                            const std::source_location& location,
                            std::string_view format,
                            Args&&... args) noexcept
{
    if constexpr (sizeof...(Args) == 0)
    {
        SubmitFormat(level, category, location, format, std::span<const FormatArg>{});
    }
    else
    {
        const FormatArg packed[] = {FormatArg(static_cast<Args&&>(args))...};
        SubmitFormat(level, category, location, format, std::span<const FormatArg>(packed, sizeof...(Args)));
    }
}

} // namespace ludus::foundation::logging::detail

#if LUDUS_COMPILED_LOG_LEVEL <= LUDUS_LOG_LEVEL_TRACE
#    define LUDUS_LOG_TRACE(category, ...) LUDUS_LOG_DISPATCH_(Trace, (category), __VA_ARGS__)
#else
#    define LUDUS_LOG_TRACE(category, ...) ((void)0)
#endif

#if LUDUS_COMPILED_LOG_LEVEL <= LUDUS_LOG_LEVEL_DEBUG
#    define LUDUS_LOG_DEBUG(category, ...) LUDUS_LOG_DISPATCH_(Debug, (category), __VA_ARGS__)
#else
#    define LUDUS_LOG_DEBUG(category, ...) ((void)0)
#endif

#if LUDUS_COMPILED_LOG_LEVEL <= LUDUS_LOG_LEVEL_INFO
#    define LUDUS_LOG_INFO(category, ...) LUDUS_LOG_DISPATCH_(Info, (category), __VA_ARGS__)
#else
#    define LUDUS_LOG_INFO(category, ...) ((void)0)
#endif

#if LUDUS_COMPILED_LOG_LEVEL <= LUDUS_LOG_LEVEL_WARNING
#    define LUDUS_LOG_WARN(category, ...) LUDUS_LOG_DISPATCH_(Warning, (category), __VA_ARGS__)
#else
#    define LUDUS_LOG_WARN(category, ...) ((void)0)
#endif

#if LUDUS_COMPILED_LOG_LEVEL <= LUDUS_LOG_LEVEL_ERROR
#    define LUDUS_LOG_ERROR(category, ...) LUDUS_LOG_DISPATCH_(Error, (category), __VA_ARGS__)
#else
#    define LUDUS_LOG_ERROR(category, ...) ((void)0)
#endif

// Fatal is never compiled out (requirements R14). Fatal REPORTS; it does not
// terminate the process (requirements R30).
#define LUDUS_LOG_FATAL(category, ...) LUDUS_LOG_DISPATCH_(Fatal, (category), __VA_ARGS__)
