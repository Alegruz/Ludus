#pragma once

// -----------------------------------------------------------------------------
// Common, lightweight logging call-site header.
//
// This header intentionally pulls in NO heavy standard-library machinery:
// no <format>, no <filesystem>, no <chrono>, no <iostream>. It provides the
// severity/category vocabulary, the compile- and run-time gates, the raw-text
// submission surface (LUDUS_LOG_TEXT), and source-location capture.
//
// Typed formatting (LUDUS_LOG_TRACE/DEBUG/INFO/WARN/ERROR/FATAL with "{}"
// substitutions) lives in the opt-in <ludus/foundation/logging/log_format.hpp>,
// which depends on this header, never the reverse. Lifecycle/configuration
// lives in <ludus/foundation/logging/log_system.hpp>.
//
// See .kiro/specs/logging-redesign/design.md sections 1-3.
// -----------------------------------------------------------------------------

#include <ludus/foundation/logging/category.hpp>
#include <ludus/foundation/logging/level.hpp>

#include <source_location>
#include <string_view>

// -----------------------------------------------------------------------------
// Compiled log-level thresholds.
//
// LUDUS_LOG_LEVEL_* are stable integers matching the LogLevel enum ordering.
// LUDUS_COMPILED_LOG_LEVEL selects the lowest level that survives compilation;
// anything below it is replaced by `((void)0)` so its arguments and category
// expression are never even evaluated (requirements R12). CMake injects
// LUDUS_COMPILED_LOG_LEVEL per build; the fallback here keeps the header usable
// when compiled standalone.
// -----------------------------------------------------------------------------
#define LUDUS_LOG_LEVEL_TRACE 0
#define LUDUS_LOG_LEVEL_DEBUG 1
#define LUDUS_LOG_LEVEL_INFO 2
#define LUDUS_LOG_LEVEL_WARNING 3
#define LUDUS_LOG_LEVEL_ERROR 4
#define LUDUS_LOG_LEVEL_FATAL 5

#if !defined(LUDUS_COMPILED_LOG_LEVEL)
#    define LUDUS_COMPILED_LOG_LEVEL LUDUS_LOG_LEVEL_INFO
#endif

namespace ludus::foundation::logging
{

// Assign a human-readable name to the calling thread. Log output prefers this
// name (e.g. "[Render]") over an opaque numeric id. Safe to call before
// Initialize(); the name is stored in thread-local state.
void SetCurrentThreadName(std::string_view name);

// Cheap, lock-free runtime predicate used by the macros to skip work when a
// record would be filtered out. It performs only scalar atomic loads: no lock,
// no allocation, no hash-map lookup, no timestamp, no argument evaluation
// (requirements R13). Safe to call before Initialize().
[[nodiscard]] bool ShouldLog(LogLevel level, LogCategory category) noexcept;

} // namespace ludus::foundation::logging

namespace ludus::foundation::logging
{

// Result of an explicit direct (debugger-critical) delivery. It reports whether
// the record reached a healthy direct output endpoint BEFORE the call returned,
// so a caller/tool can inspect it before hitting a breakpoint (requirements
// R27 path 2, R28). Delivered means "handed to a selected direct endpoint",
// never "a GUI painted it".
enum class DeliveryStatus : uint8
{
    Delivered,
    Filtered,
    NoEndpoint,
};

struct DeliveryResult
{
    DeliveryStatus Status = DeliveryStatus::NoEndpoint;
};

} // namespace ludus::foundation::logging

namespace ludus::foundation::logging::detail
{

// Single, non-template raw-text submission entry point. The caller has already
// been admitted by the gate; this routes an already-final, uninterpreted text
// payload (no format parsing) to the backend (requirements R16, R20).
void SubmitText(LogLevel level,
                LogCategory category,
                const std::source_location& location,
                std::string_view text) noexcept;

// Explicit DIRECT delivery of raw text to selected debugger/stderr endpoints,
// independent of the (future) asynchronous worker so an intentional breakpoint
// after the call can still observe the record even if the worker is parked
// (requirements R27 path 2). Returns whether it reached a direct endpoint.
DeliveryResult SubmitDirectText(LogLevel level,
                                LogCategory category,
                                const std::source_location& location,
                                std::string_view text) noexcept;

} // namespace ludus::foundation::logging::detail

// -----------------------------------------------------------------------------
// Gate + submission macro used by both the raw-text and typed-format surfaces.
//
// The category expression is resolved into a single hidden local BEFORE gating,
// so it is evaluated exactly once (requirements R15; fixes the F6 double
// evaluation). source_location::current() is captured only inside the taken
// branch, so a runtime-disabled call captures nothing. The do/while(false)
// wrapper keeps the macro a single statement.
// -----------------------------------------------------------------------------
#define LUDUS_LOG_GATE_(level_enum, category_expr, ludus_body_)                                                        \
    do                                                                                                                 \
    {                                                                                                                  \
        const ::ludus::foundation::logging::LogCategory ludus_cat_ = (category_expr);                                  \
        const ::ludus::foundation::logging::LogLevel ludus_lvl_ = (level_enum);                                        \
        if (::ludus::foundation::logging::ShouldLog(ludus_lvl_, ludus_cat_))                                           \
        {                                                                                                              \
            ludus_body_                                                                                                \
        }                                                                                                              \
    } while (false)

// Raw-text logging: `text` is treated as literal, uninterpreted UTF-8 and is
// NEVER parsed as a format string. Use this for dynamic/third-party strings
// (driver messages, errno text) and for static messages that contain brace
// characters (requirements R16). `level_name` is a bare LogLevel enumerator
// (Trace/Debug/Info/Warning/Error/Fatal).
#define LUDUS_LOG_TEXT(category, level_name, text)                                                                     \
    LUDUS_LOG_GATE_(::ludus::foundation::logging::LogLevel::level_name,                                                \
                    (category),                                                                                        \
                    ::ludus::foundation::logging::detail::SubmitText(ludus_lvl_,                                       \
                                                                     ludus_cat_,                                       \
                                                                     ::std::source_location::current(),                \
                                                                     (text));)

// Explicit DIRECT debugger-critical delivery of raw text at Debug severity. This
// is a delivery POLICY, not a new severity: it delivers synchronously to the
// selected debugger/stderr endpoints before returning, bypassing the (future)
// asynchronous worker so an intentional breakpoint right after it still sees the
// record (requirements R27 path 2). Evaluates to a DeliveryResult the caller can
// inspect. Assertion/fatal reporting must not depend on this optional Debug gate
// (requirements R30).
#if LUDUS_COMPILED_LOG_LEVEL <= LUDUS_LOG_LEVEL_DEBUG
#    define LUDUS_LOG_DEBUG_SYNC(category, text)                                                                       \
        (::ludus::foundation::logging::ShouldLog(::ludus::foundation::logging::LogLevel::Debug, (category))            \
             ? ::ludus::foundation::logging::detail::SubmitDirectText(::ludus::foundation::logging::LogLevel::Debug,   \
                                                                      (category),                                      \
                                                                      ::std::source_location::current(),               \
                                                                      (text))                                          \
             : ::ludus::foundation::logging::DeliveryResult{::ludus::foundation::logging::DeliveryStatus::Filtered})
#else
#    define LUDUS_LOG_DEBUG_SYNC(category, text)                                                                       \
        (::ludus::foundation::logging::DeliveryResult{::ludus::foundation::logging::DeliveryStatus::Filtered})
#endif
