// Compile this minimal consumer in every native profile so an unused framework
// include cannot silently reintroduce the standard formatting graph.
#include <catch2/catch_test_macros.hpp>

#if defined(_GLIBCXX_FORMAT) || defined(_LIBCPP_FORMAT) || defined(_FORMAT_)
#    error Catch assertion headers must not include standard formatting machinery
#endif
#if defined(_GLIBCXX_CHRONO) || defined(_LIBCPP_CHRONO)
#    error Catch assertion headers must not include the full chrono facade
#endif
