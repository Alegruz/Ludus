#pragma once

// Compiler and debugging utilities
// This is Core infrastructure needed by Assert.h and other low-level code

#if defined(_MSC_VER)
    #define LUDUS_DEBUGBREAK() __debugbreak()
    #define LUDUS_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
    #define LUDUS_DEBUGBREAK() __builtin_trap()
    #define LUDUS_INLINE __attribute__((always_inline)) inline
#else
    #define LUDUS_DEBUGBREAK() ((void)0)
    #define LUDUS_INLINE inline
#endif
