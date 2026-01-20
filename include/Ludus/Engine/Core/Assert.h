#pragma once

#include <Ludus/Engine/Core/Common.h>

#include <cstdio>

#if defined(_MSC_VER)
    #define LUDUS_DEBUGBREAK() __debugbreak()
#elif defined(__GNUC__) || defined(__clang__)
    #define LUDUS_DEBUGBREAK() __builtin_trap()
#else
    #define LUDUS_DEBUGBREAK() ((void)0)
#endif

namespace ludus::core
{
    struct AssertInfo final
    {
        const char* expression;
        const char* message;
        const char* file;
        int line;
    };

    using AssertHandler = bool (*)(const AssertInfo& info, bool* ignoreAlways);

    LUDUS_INLINE bool DefaultAssertHandler(const AssertInfo& info, bool* ignoreAlways) noexcept
    {
        const char* message = info.message ? info.message : "(no message)";
        std::fprintf(stderr,
            "Assertion failed!\n  Expression: %s\n  Message: %s\n  File: %s\n  Line: %d\n",
            info.expression,
            message,
            info.file,
            info.line);
        (void)ignoreAlways;
        return true;
    }

    LUDUS_INLINE AssertHandler& GetAssertHandler() noexcept
    {
        static AssertHandler handler = DefaultAssertHandler;
        return handler;
    }

    LUDUS_INLINE void SetAssertHandler(AssertHandler handler) noexcept
    {
        GetAssertHandler() = handler ? handler : DefaultAssertHandler;
    }

    LUDUS_INLINE bool HandleAssert(const AssertInfo& info, bool* ignoreAlways) noexcept
    {
        return GetAssertHandler()(info, ignoreAlways);
    }
} // namespace ludus::core

#if defined(LUDUS_DEBUG)
    #define LUDUS_ASSERT_MSG(expr, message)                                                         \
        do                                                                                          \
        {                                                                                           \
            static bool ignoreAlways = false;                                                       \
            if (!ignoreAlways && !(expr))                                                           \
            {                                                                                       \
                if (::ludus::core::HandleAssert({#expr, message, __FILE__, __LINE__}, &ignoreAlways)) \
                {                                                                                   \
                    LUDUS_DEBUGBREAK();                                                             \
                }                                                                                   \
            }                                                                                       \
        } while (false)

    #define LUDUS_ASSERT(expr) LUDUS_ASSERT_MSG(expr, nullptr)
#else
    #define LUDUS_ASSERT_MSG(expr, message)                                                          \
        do                                                                                           \
        {                                                                                            \
            (void)sizeof(expr);                                                                      \
            (void)sizeof(message);                                                                   \
        } while (false)

    #define LUDUS_ASSERT(expr)                                                                        \
        do                                                                                            \
        {                                                                                             \
            (void)sizeof(expr);                                                                       \
        } while (false)
#endif

#define LUDUS_VERIFY_MSG(expr, message)                                                               \
    do                                                                                               \
    {                                                                                                \
        if (!(expr))                                                                                 \
        {                                                                                            \
            if (::ludus::core::HandleAssert({#expr, message, __FILE__, __LINE__}, nullptr))           \
            {                                                                                        \
                LUDUS_DEBUGBREAK();                                                                  \
            }                                                                                        \
        }                                                                                            \
    } while (false)

#define LUDUS_VERIFY(expr) LUDUS_VERIFY_MSG(expr, nullptr)
