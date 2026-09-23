#pragma once

#include <ludus/foundation/logging/category.hpp>
#include <ludus/foundation/logging/config.hpp>
#include <ludus/foundation/logging/level.hpp>

#include <format>
#include <source_location>
#include <string_view>
#include <utility>

// -----------------------------------------------------------------------------
// Compiled log-level thresholds.
//
// LUDUS_LOG_LEVEL_* are stable integers matching the LogLevel enum ordering.
// LUDUS_COMPILED_LOG_LEVEL selects the lowest level that survives compilation;
// anything below it is replaced by `((void)0)` so its arguments are never even
// evaluated (spec section 7). CMake injects LUDUS_COMPILED_LOG_LEVEL per build;
// the fallback here keeps the header usable when compiled standalone.
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

// Strongly-typed logging entry point that the LUDUS_LOG_* macros forward into.
// The format string is validated at compile time against the argument types by
// std::format_string, and std::source_location is defaulted at the call site so
// callers never write __FILE__ / __LINE__ (spec sections 9, 10).
//
// This overload takes the source location as the leading runtime argument so it
// can precede the variadic pack; the macros supply it automatically.
template <typename... Args>
void Log(LogLevel level,
         LogCategory category,
         const std::source_location& location,
         std::format_string<Args...> format,
         Args&&... args);

// Lifecycle and control surface (spec section 32). Normal engine code should use
// the LUDUS_LOG_* macros almost exclusively; this class is for startup,
// diagnostics, and tools.
class LogSystem
{
public:
    static void Initialize(const LogConfig& config);
    static void Shutdown();

    static void Flush();

    static void SetMode(LogMode mode);
    static void SetGlobalLevel(LogLevel level);
    static void SetCategoryLevel(LogCategory category, LogLevel level);

    static void ClearCategoryLevels();

    [[nodiscard]] static LogStatistics Statistics();

    // True between a successful initialize() and shutdown(). Normal code never
    // needs to check this before logging (spec section 25); it exists for tests
    // and diagnostics.
    [[nodiscard]] static bool IsInitialized() noexcept;

    // Cheap predicate used by the macros to skip formatting when a record would
    // be filtered out at runtime. Safe to call before initialize().
    [[nodiscard]] static bool ShouldLog(LogLevel level, LogCategory category) noexcept;
};

// Assign a human-readable name to the calling thread (spec section 29). Log
// output prefers this name (e.g. "[Render]") over an opaque numeric id. Safe to
// call before initialize(); the name is stored in thread-local state.
void SetCurrentThreadName(std::string_view name);

} // namespace ludus::foundation::logging

// -----------------------------------------------------------------------------
// Public macros.
//
// Each macro is guarded twice:
//   1. Compile-time: if the level is below LUDUS_COMPILED_LOG_LEVEL the whole
//      statement becomes ((void)0) and the arguments are not evaluated.
//   2. Runtime: should_log() is checked before formatting so filtered-out
//      records cost only an id + integer comparison (spec sections 7, 8).
//
// The extra do/while-style wrapping is expressed with a short-circuit so the
// macro remains a single expression usable anywhere a statement is expected.
// -----------------------------------------------------------------------------
#define LUDUS_LOG_IMPL(level_enum, category, ...)                                                                      \
    (::ludus::foundation::logging::LogSystem::ShouldLog((level_enum), (category))                                      \
         ? ::ludus::foundation::logging::Log((level_enum), (category), ::std::source_location::current(), __VA_ARGS__) \
         : (void)0)

#if LUDUS_COMPILED_LOG_LEVEL <= LUDUS_LOG_LEVEL_TRACE
#    define LUDUS_LOG_TRACE(category, ...)                                                                             \
        LUDUS_LOG_IMPL(::ludus::foundation::logging::LogLevel::Trace, (category), __VA_ARGS__)
#else
#    define LUDUS_LOG_TRACE(category, ...) ((void)0)
#endif

#if LUDUS_COMPILED_LOG_LEVEL <= LUDUS_LOG_LEVEL_DEBUG
#    define LUDUS_LOG_DEBUG(category, ...)                                                                             \
        LUDUS_LOG_IMPL(::ludus::foundation::logging::LogLevel::Debug, (category), __VA_ARGS__)
#else
#    define LUDUS_LOG_DEBUG(category, ...) ((void)0)
#endif

#if LUDUS_COMPILED_LOG_LEVEL <= LUDUS_LOG_LEVEL_INFO
#    define LUDUS_LOG_INFO(category, ...)                                                                              \
        LUDUS_LOG_IMPL(::ludus::foundation::logging::LogLevel::Info, (category), __VA_ARGS__)
#else
#    define LUDUS_LOG_INFO(category, ...) ((void)0)
#endif

#if LUDUS_COMPILED_LOG_LEVEL <= LUDUS_LOG_LEVEL_WARNING
#    define LUDUS_LOG_WARN(category, ...)                                                                              \
        LUDUS_LOG_IMPL(::ludus::foundation::logging::LogLevel::Warning, (category), __VA_ARGS__)
#else
#    define LUDUS_LOG_WARN(category, ...) ((void)0)
#endif

#if LUDUS_COMPILED_LOG_LEVEL <= LUDUS_LOG_LEVEL_ERROR
#    define LUDUS_LOG_ERROR(category, ...)                                                                             \
        LUDUS_LOG_IMPL(::ludus::foundation::logging::LogLevel::Error, (category), __VA_ARGS__)
#else
#    define LUDUS_LOG_ERROR(category, ...) ((void)0)
#endif

// Fatal is never compiled out: catastrophic failures must always be reportable
// through the hardened emergency path (spec sections 26, 30).
#define LUDUS_LOG_FATAL(category, ...)                                                                                 \
    LUDUS_LOG_IMPL(::ludus::foundation::logging::LogLevel::Fatal, (category), __VA_ARGS__)

// -----------------------------------------------------------------------------
// Template implementation.
//
// Kept in the header (after the class definition) so callers get compile-time
// format checking. The heavy lifting - filtering re-check, source capture into
// the record, sink dispatch - lives in logger.cpp behind log_formatted().
// -----------------------------------------------------------------------------
namespace ludus::foundation::logging::detail
{

// Type-erased logging entry points, both defined once in logger.cpp.
//
// The whole point of this boundary is build time: std::vformat and the entire
// <format> instantiation machinery are compiled a SINGLE time here, instead of
// being re-instantiated in every translation unit that logs. Call sites only
// instantiate the trivial std::make_format_args pack expansion, which is cheap
// (see docs/decisions/0004-logging-format-type-erasure.md).

// Format-and-dispatch: performs std::vformat internally, then routes to sinks.
void VLog(LogLevel level,
          LogCategory category,
          const std::source_location& location,
          std::string_view format,
          std::format_args args);

// Fast path for messages with no arguments: no formatting, forward the literal.
void DispatchMessage(LogLevel level,
                     LogCategory category,
                     const std::source_location& location,
                     std::string_view message);

} // namespace ludus::foundation::logging::detail

namespace ludus::foundation::logging
{

template <typename... Args>
void Log(LogLevel level,
         LogCategory category,
         const std::source_location& location,
         std::format_string<Args...> format,
         Args&&... args)
{
    // Zero-argument calls carry a plain format string with no substitutions, so
    // forward the string as-is and skip std::format entirely for that common
    // "static message" case.
    if constexpr (sizeof...(Args) == 0)
    {
        detail::DispatchMessage(level, category, location, format.get());
    }
    else
    {
        // Erase the arguments to std::format_args HERE and hand them to the
        // non-template VLog. std::vformat itself is instantiated only inside
        // logger.cpp, so no per-call-site <format> machinery is generated.
        detail::VLog(level, category, location, format.get(), std::make_format_args(args...));
    }
}

} // namespace ludus::foundation::logging
