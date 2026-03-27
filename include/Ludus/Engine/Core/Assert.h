#pragma once

#include <Ludus/Engine/Core/Compiler.h>
#include <Ludus/Engine/Core/Logger.h>

#include <cstdio>

namespace ludus::core
{
    struct AssertInfo final
    {
        const char* Expression = nullptr;
        const char* Message = nullptr;
        const char* File = nullptr;
        int Line = 0;
    };

    using AssertHandler = bool (*)(const AssertInfo& info, bool* ignoreAlways);

    LUDUS_INLINE bool DefaultAssertHandler(const AssertInfo& info, bool* ignoreAlways) noexcept // NOLINT(readability-non-const-parameter)
    {
        const char* message = info.Message ? info.Message : "(no message)";
        const std::string formattedMessage = std::format(
            "Assertion failed: expr=`{}` message=`{}`",
            info.Expression,
            message);
        LogWrite(
            LogLevel::Fatal,
            "Assert",
            info.File,
            "Assert",
            static_cast<uint32_t>(info.Line),
            formattedMessage.c_str());
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

#define LUDUS_STATIC_ASSERT_MSG(expr, message) static_assert(expr, message)
#define LUDUS_STATIC_ASSERT(expr) static_assert((expr))

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
