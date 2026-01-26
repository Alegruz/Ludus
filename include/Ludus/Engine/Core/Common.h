#pragma once

#include <Ludus/Engine/Platform/Platform.h>

#include <climits>
#include <codecvt>
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <locale>
#include <utility>
#include <variant>

// MACROS
#if defined(_MSC_VER)
    #define LUDUS_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
    #define LUDUS_INLINE __attribute__((always_inline)) inline
#else
    #define LUDUS_INLINE inline
#endif

namespace ludus::core
{
    // Constants
    constexpr uint32_t POINTER_SIZE_64BIT = 8U;
    constexpr uint32_t POINTER_SIZE_32BIT = 4U;

    template<typename CharT>
    concept StringCharType = std::is_same_v<CharT, char> || std::is_same_v<CharT, wchar_t>;
    
    // NOLINTNEXTLINE(readability-identifier-naming) - Macro-style naming is intentional for cross-char helper.
    LUDUS_INLINE constexpr char STRING_NULL_CHAR(char unusedChar) noexcept { (void)unusedChar; return '\0'; }
    // NOLINTNEXTLINE(readability-identifier-naming) - Macro-style naming is intentional for cross-char helper.
    LUDUS_INLINE constexpr wchar_t STRING_NULL_CHAR(wchar_t unusedChar) noexcept { (void)unusedChar; return L'\0'; }

    template<StringCharType CharT>
    LUDUS_INLINE constexpr uint32_t GetStringLength(const CharT* str) noexcept
    {
        if constexpr (std::is_same_v<CharT, char>)
        {
            return static_cast<uint32_t>(strlen(str));
        }
        else if constexpr (std::is_same_v<CharT, wchar_t>)
        {
            return static_cast<uint32_t>(wcslen(str));
        }
    }
    
    template<typename T>
    concept Arithmetic = std::is_arithmetic_v<T>;
}   // namespace ludus::core
