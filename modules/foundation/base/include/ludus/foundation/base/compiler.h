#pragma once

// -----------------------------------------------------------------------------
// Ludus Band 0 compiler abstraction: compiler identity plus the codegen
// attribute and hint macros the engine uses (inlining, cold/noinline, branch
// prediction, debug break, unreachable). Macros only — no code, free to parse.
//
// Pulled in by <ludus/foundation/base/core.h>; include core.h (or this header
// directly) rather than relying on it arriving transitively. This is the single
// home for "how do I ask this compiler to do X?" so the rest of the engine never
// hand-writes __attribute__/__declspec spellings.
//
// See docs/architecture/foundational-headers.md (§8) and ADR 0007.
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Compiler identity.
// -----------------------------------------------------------------------------
#if defined(__clang__)
#    define LUDUS_COMPILER_CLANG 1
#elif defined(__GNUC__)
#    define LUDUS_COMPILER_GCC 1
#elif defined(_MSC_VER)
#    define LUDUS_COMPILER_MSVC 1
#endif

// -----------------------------------------------------------------------------
// Inlining and codegen attributes.
//
//   LUDUS_INLINE   force inline (best effort; strongest available hint).
//   LUDUS_NOINLINE never inline.
//   LUDUS_COLD     rarely executed; keep off the hot path (e.g. failure sinks).
// -----------------------------------------------------------------------------
#if defined(_MSC_VER)
#    define LUDUS_INLINE __forceinline
#    define LUDUS_NOINLINE __declspec(noinline)
#    define LUDUS_COLD
#elif defined(__clang__) || defined(__GNUC__)
#    define LUDUS_INLINE inline __attribute__((__always_inline__))
#    define LUDUS_NOINLINE __attribute__((noinline))
#    define LUDUS_COLD __attribute__((cold))
#else
#    define LUDUS_INLINE inline
#    define LUDUS_NOINLINE
#    define LUDUS_COLD
#endif

// -----------------------------------------------------------------------------
// Branch-prediction hints and low-level codegen intrinsics.
//
// LUDUS_LIKELY/LUDUS_UNLIKELY give a named, greppable engine spelling for use in
// C-style conditions and macros where the C++ [[likely]]/[[unlikely]]
// attributes cannot appear. Prefer the standard attributes in ordinary C++
// control flow; use these in the low-level diagnostic/assertion plumbing.
//
// LUDUS_DEBUG_BREAK traps into an attached debugger; LUDUS_UNREACHABLE marks a
// path the compiler may assume is never taken.
// -----------------------------------------------------------------------------
#if defined(__clang__) || defined(__GNUC__)
#    define LUDUS_LIKELY(expression) (__builtin_expect(!!(expression), 1))
#    define LUDUS_UNLIKELY(expression) (__builtin_expect(!!(expression), 0))
#    define LUDUS_UNREACHABLE() __builtin_unreachable()
#else
#    define LUDUS_LIKELY(expression) (!!(expression))
#    define LUDUS_UNLIKELY(expression) (!!(expression))
#    define LUDUS_UNREACHABLE() ((void)0)
#endif

#if defined(__clang__)
#    define LUDUS_DEBUG_BREAK() __builtin_debugtrap()
#elif defined(__GNUC__)
#    define LUDUS_DEBUG_BREAK() __builtin_trap()
#elif defined(_MSC_VER)
#    define LUDUS_DEBUG_BREAK() __debugbreak()
#else
#    define LUDUS_DEBUG_BREAK() ((void)0)
#endif
