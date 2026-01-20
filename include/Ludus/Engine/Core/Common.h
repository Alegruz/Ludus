#pragma once

#include <Ludus/Engine/Platform/Platform.h>

#include <concepts>
#include <cstdint>
#include <cstring>
#include <cwchar>
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
    template<typename CharT>
    concept StringCharType = std::is_same_v<CharT, char> || std::is_same_v<CharT, wchar_t>;
    
    constexpr StringCharType auto STRING_NULL_CHAR(char);
    constexpr StringCharType auto STRING_NULL_CHAR(wchar_t);

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