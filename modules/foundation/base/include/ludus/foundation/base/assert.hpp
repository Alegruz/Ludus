#pragma once

#include <ludus/foundation/base/assert_config.hpp>
#include <ludus/foundation/base/compiler.hpp>
#include <ludus/foundation/base/types.h>

namespace ludus::foundation::diagnostics
{

struct DiagnosticText
{
    const char* Data;
    usize Size;
};

struct AssertionSite
{
    const char* File;
    const char* Function;
    const char* Expression; // nullptr for unconditional fatal
    uint32 Line;
};

enum class FailureKind : uint8
{
    Assert,
    Require,
    Check,
    Fatal
};

namespace detail
{
// Macro implementation only. Begin/Finish must pair on one thread; no exceptions,
// cancellation, longjmp or suspension may cross this region. Begin guards and
// copies the site BEFORE the macro evaluates optional diagnostic expressions.
//
// REQUIRE/FATAL use the terminal Begin/Finish pair: FinishFatal never returns,
// including after a debugger continues. ASSERT/ASSERT_F use the separate
// resumable pair: FinishAssert may RETURN when a developer explicitly continues
// (debugger continue, or the external-helper Continue-once dialog) in an
// eligible local non-CI Debug build; otherwise it terminates like a fatal. CHECK
// is unchanged: it reports best-effort and returns false.
LUDUS_COLD LUDUS_NOINLINE void BeginFatal(FailureKind kind, const AssertionSite& site) noexcept;
LUDUS_COLD LUDUS_NOINLINE void BeginAssert(const AssertionSite& site) noexcept;
LUDUS_COLD LUDUS_NOINLINE bool BeginCheck(const AssertionSite& site) noexcept;
[[noreturn]] LUDUS_COLD LUDUS_NOINLINE void FinishFatal(const char* message = nullptr) noexcept;
LUDUS_COLD LUDUS_NOINLINE void FinishAssert(const char* message = nullptr) noexcept;
LUDUS_COLD LUDUS_NOINLINE bool FinishCheck(const char* message = nullptr) noexcept;
} // namespace detail
} // namespace ludus::foundation::diagnostics

// Function-body use only. Parenthesize conditions containing template/initializer
// commas. Messages are borrowed C strings, observed only for an unsuppressed
// failure; keep them safe and free of required side effects.
#define LUDUS_DETAIL_ASSERT_SITE(expression_text)                                                                      \
    (::ludus::foundation::diagnostics::AssertionSite{__FILE__,                                                         \
                                                     __func__,                                                         \
                                                     (expression_text),                                                \
                                                     static_cast<::ludus::foundation::uint32>(__LINE__)})

#if LUDUS_ENABLE_ASSERTS
#    define LUDUS_ASSERT(condition, ...)                                                                               \
        do                                                                                                             \
        {                                                                                                              \
            if (!static_cast<bool>(condition)) [[unlikely]]                                                            \
            {                                                                                                          \
                ::ludus::foundation::diagnostics::detail::BeginAssert(LUDUS_DETAIL_ASSERT_SITE(#condition));           \
                ::ludus::foundation::diagnostics::detail::FinishAssert(__VA_ARGS__);                                   \
            }                                                                                                          \
        } while (false)
#else
#    define LUDUS_ASSERT(...) ((void)0)
#endif

#define LUDUS_REQUIRE(condition, ...)                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!static_cast<bool>(condition)) [[unlikely]]                                                                \
        {                                                                                                              \
            ::ludus::foundation::diagnostics::detail::BeginFatal(                                                      \
                ::ludus::foundation::diagnostics::FailureKind::Require,                                                \
                LUDUS_DETAIL_ASSERT_SITE(#condition));                                                                 \
            ::ludus::foundation::diagnostics::detail::FinishFatal(__VA_ARGS__);                                        \
        }                                                                                                              \
    } while (false)

// Consume the result in a recovery branch. No lambda: __func__ remains the
// caller's short function name, and explicit operator bool is accepted.
#define LUDUS_CHECK(condition, ...)                                                                                    \
    (static_cast<bool>(condition)                                                                                      \
         ? true                                                                                                        \
         : (::ludus::foundation::diagnostics::detail::BeginCheck(LUDUS_DETAIL_ASSERT_SITE(#condition))                 \
                ? ::ludus::foundation::diagnostics::detail::FinishCheck(__VA_ARGS__)                                   \
                : false))

#define LUDUS_FATAL(message)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        ::ludus::foundation::diagnostics::detail::BeginFatal(::ludus::foundation::diagnostics::FailureKind::Fatal,     \
                                                             LUDUS_DETAIL_ASSERT_SITE(nullptr));                       \
        ::ludus::foundation::diagnostics::detail::FinishFatal((message));                                              \
    } while (false)
