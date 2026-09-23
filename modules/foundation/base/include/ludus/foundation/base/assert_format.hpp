#pragma once

#include <ludus/foundation/base/assert.hpp>

namespace ludus::foundation::diagnostics
{
struct DiagnosticAddressValue
{
    const void* Value;
};
struct DiagnosticCStringValue
{
    const char* Value;
};
[[nodiscard]] constexpr DiagnosticAddressValue DiagnosticAddress(const void* value) noexcept
{
    return {value};
}
[[nodiscard]] constexpr DiagnosticCStringValue DiagnosticCString(const char* value) noexcept
{
    return {value};
}

namespace detail
{
enum class DiagnosticArgKind : uint8
{
    Signed,
    Unsigned,
    Floating,
    Boolean,
    Text,
    Address
};
struct DiagnosticArg
{
    DiagnosticArgKind Kind;
    union
    {
        int64 Signed;
        uint64 Unsigned;
        float64 Floating;
        bool Boolean;
        DiagnosticText Text;
        const void* Address;
    } Value;
    bool Truncated = false;
};
constexpr DiagnosticArg MakeDiagnosticArg(int8 value) noexcept
{
    return {DiagnosticArgKind::Signed, {.Signed = value}};
}
constexpr DiagnosticArg MakeDiagnosticArg(int16 value) noexcept
{
    return {DiagnosticArgKind::Signed, {.Signed = value}};
}
constexpr DiagnosticArg MakeDiagnosticArg(int32 value) noexcept
{
    return {DiagnosticArgKind::Signed, {.Signed = value}};
}
constexpr DiagnosticArg MakeDiagnosticArg(int64 value) noexcept
{
    return {DiagnosticArgKind::Signed, {.Signed = value}};
}
constexpr DiagnosticArg MakeDiagnosticArg(uint8 value) noexcept
{
    return {DiagnosticArgKind::Unsigned, {.Unsigned = value}};
}
constexpr DiagnosticArg MakeDiagnosticArg(uint16 value) noexcept
{
    return {DiagnosticArgKind::Unsigned, {.Unsigned = value}};
}
constexpr DiagnosticArg MakeDiagnosticArg(uint32 value) noexcept
{
    return {DiagnosticArgKind::Unsigned, {.Unsigned = value}};
}
constexpr DiagnosticArg MakeDiagnosticArg(uint64 value) noexcept
{
    return {DiagnosticArgKind::Unsigned, {.Unsigned = value}};
}
constexpr DiagnosticArg MakeDiagnosticArg(float32 value) noexcept
{
    return {DiagnosticArgKind::Floating, {.Floating = static_cast<float64>(value)}};
}
constexpr DiagnosticArg MakeDiagnosticArg(float64 value) noexcept
{
    return {DiagnosticArgKind::Floating, {.Floating = value}};
}
constexpr DiagnosticArg MakeDiagnosticArg(bool value) noexcept
{
    return {DiagnosticArgKind::Boolean, {.Boolean = value}};
}
constexpr DiagnosticArg MakeDiagnosticArg(DiagnosticText value) noexcept
{
    return {DiagnosticArgKind::Text, {.Text = value}};
}
constexpr DiagnosticArg MakeDiagnosticArg(DiagnosticAddressValue value) noexcept
{
    return {DiagnosticArgKind::Address, {.Address = value.Value}};
}
DiagnosticArg MakeCStringArg(const char* value, usize bound) noexcept;
inline DiagnosticArg MakeDiagnosticArg(DiagnosticCStringValue value) noexcept
{
    return MakeCStringArg(value.Value, 1024);
}
template <usize N>
DiagnosticArg MakeDiagnosticArg(const char (&value)[N]) noexcept
{
    return MakeCStringArg(value, N);
}
template <typename T>
DiagnosticArg MakeDiagnosticArg(const T&) = delete;

[[noreturn]] LUDUS_COLD void FinishFatalArgs(DiagnosticText format, const DiagnosticArg* args, usize count) noexcept;
LUDUS_COLD bool FinishCheckArgs(DiagnosticText format, const DiagnosticArg* args, usize count) noexcept;

// Fixed-array input, not a claim that C++ can require a literal here. The runtime
// validates its terminator, extent, grammar, and argument count. All borrows are
// consumed synchronously before temporary argument lifetimes end.
template <usize N, typename... Args>
[[noreturn]] void FinishFatalFormatted(const char (&format)[N], const Args&... args) noexcept
{
    static_assert(sizeof...(Args) <= 8, "Assertions accept at most eight diagnostic arguments");
    if constexpr (sizeof...(Args) == 0)
    {
        FinishFatalArgs({format, N}, nullptr, 0);
    }
    else
    {
        const DiagnosticArg packed[] = {MakeDiagnosticArg(args)...};
        FinishFatalArgs({format, N}, packed, sizeof...(Args));
    }
}
template <usize N, typename... Args>
bool FinishCheckFormatted(const char (&format)[N], const Args&... args) noexcept
{
    static_assert(sizeof...(Args) <= 8, "Assertions accept at most eight diagnostic arguments");
    if constexpr (sizeof...(Args) == 0)
    {
        return FinishCheckArgs({format, N}, nullptr, 0);
    }
    else
    {
        const DiagnosticArg packed[] = {MakeDiagnosticArg(args)...};
        return FinishCheckArgs({format, N}, packed, sizeof...(Args));
    }
}
} // namespace detail
} // namespace ludus::foundation::diagnostics

#if LUDUS_ENABLE_ASSERTS
#    define LUDUS_ASSERT_F(condition, ...)                                                                             \
        do                                                                                                             \
        {                                                                                                              \
            if (!static_cast<bool>(condition)) [[unlikely]]                                                            \
            {                                                                                                          \
                ::ludus::foundation::diagnostics::detail::BeginFatal(                                                  \
                    ::ludus::foundation::diagnostics::FailureKind::Assert,                                             \
                    LUDUS_DETAIL_ASSERT_SITE(#condition));                                                             \
                ::ludus::foundation::diagnostics::detail::FinishFatalFormatted(__VA_ARGS__);                           \
            }                                                                                                          \
        } while (false)
#else
#    define LUDUS_ASSERT_F(...) ((void)0)
#endif
#define LUDUS_REQUIRE_F(condition, ...)                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!static_cast<bool>(condition)) [[unlikely]]                                                                \
        {                                                                                                              \
            ::ludus::foundation::diagnostics::detail::BeginFatal(                                                      \
                ::ludus::foundation::diagnostics::FailureKind::Require,                                                \
                LUDUS_DETAIL_ASSERT_SITE(#condition));                                                                 \
            ::ludus::foundation::diagnostics::detail::FinishFatalFormatted(__VA_ARGS__);                               \
        }                                                                                                              \
    } while (false)
#define LUDUS_CHECK_F(condition, ...)                                                                                  \
    (static_cast<bool>(condition)                                                                                      \
         ? true                                                                                                        \
         : (::ludus::foundation::diagnostics::detail::BeginCheck(LUDUS_DETAIL_ASSERT_SITE(#condition))                 \
                ? ::ludus::foundation::diagnostics::detail::FinishCheckFormatted(__VA_ARGS__)                          \
                : false))
#define LUDUS_FATAL_F(...)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        ::ludus::foundation::diagnostics::detail::BeginFatal(::ludus::foundation::diagnostics::FailureKind::Fatal,     \
                                                             LUDUS_DETAIL_ASSERT_SITE(nullptr));                       \
        ::ludus::foundation::diagnostics::detail::FinishFatalFormatted(__VA_ARGS__);                                   \
    } while (false)
