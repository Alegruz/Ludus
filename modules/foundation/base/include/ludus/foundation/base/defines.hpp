#pragma once

#include <cstddef>

#if defined(_MSC_VER)
#define LUDUS_INLINE __forceinline
#elif defined(__clang__) || defined(__GNUC__)
#define LUDUS_INLINE inline __attribute__((__always_inline__))
#else
#define LUDUS_INLINE inline
#endif

#if !defined(LUDUS_BUILD_DEBUG) && !defined(LUDUS_BUILD_DEVELOPMENT) && !defined(LUDUS_BUILD_PROFILE) && !defined(LUDUS_BUILD_RELEASE)
    #if defined(_DEBUG) || defined(DEBUG)
        #define LUDUS_BUILD_DEBUG 1
    #elif defined(NDEBUG)
        #define LUDUS_BUILD_RELEASE 1
    #else
        #define LUDUS_BUILD_DEVELOPMENT 1
    #endif
#endif

#if defined(LUDUS_BUILD_DEBUG)
    #define LUDUS_IS_DEBUG_BUILD 1
#endif

#if defined(LUDUS_BUILD_DEVELOPMENT)
    #define LUDUS_IS_DEVELOPMENT_BUILD 1
#endif

#if defined(LUDUS_BUILD_PROFILE)
    #define LUDUS_IS_PROFILE_BUILD 1
#endif

#if defined(LUDUS_BUILD_RELEASE)
    #define LUDUS_IS_RELEASE_BUILD 1
#endif

#if defined(LUDUS_BUILD_SANITIZED)
    #define LUDUS_IS_SANITIZED_BUILD 1
#endif

namespace ludus::foundation::core
{
    using nullptr_t = std::nullptr_t;
}

namespace ludus::foundation
{
    using nullptr_t = core::nullptr_t;
}
