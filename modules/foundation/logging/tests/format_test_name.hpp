#pragma once

#include "internal/format_engine.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <type_traits>

// Filesystem fixtures use the same bounded formatter as the production logger.
// Convert owning fixture strings to borrowed views before argument packing.
template <typename T>
auto TestNameArg(const T& value)
{
    if constexpr (std::is_same_v<T, std::string>)
    {
        return ludus::foundation::logging::FormatArg(std::string_view(value));
    }
    else
    {
        return ludus::foundation::logging::FormatArg(value);
    }
}

template <typename... Args>
std::string FormatTestName(std::string_view format, const Args&... args)
{
    char buffer[256];
    const ludus::foundation::logging::FormatArg packed[] = {TestNameArg(args)...};
    const auto result = ludus::foundation::logging::internal::FormatInto(buffer, format, packed);
    REQUIRE_FALSE(result.Truncated);
    REQUIRE_FALSE(result.FormatError);
    return {buffer, result.BytesWritten};
}
