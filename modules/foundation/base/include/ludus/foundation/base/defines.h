#pragma once

#include <cstddef>
#include <type_traits>

#if defined(_MSC_VER)
#define LUDUS_INLINE __forceinline
#elif defined(__clang__) || defined(__GNUC__)
#define LUDUS_INLINE inline __attribute__((__always_inline__))
#else
#define LUDUS_INLINE inline
#endif

namespace ludus::foundation::core
{
    using nullptr_t = std::nullptr_t;

    template<typename T, typename U>
    concept DerivedFrom = std::is_base_of_v<U, T> && !std::is_same_v<T, U>;
}

namespace ludus::foundation
{
    using nullptr_t = core::nullptr_t;
}

