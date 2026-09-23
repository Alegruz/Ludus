#pragma once

#if defined(__clang__) || defined(__GNUC__)
#    define LUDUS_NOINLINE __attribute__((noinline))
#    define LUDUS_COLD __attribute__((cold))
#elif defined(_MSC_VER)
#    define LUDUS_NOINLINE __declspec(noinline)
#    define LUDUS_COLD
#else
#    define LUDUS_NOINLINE
#    define LUDUS_COLD
#endif
