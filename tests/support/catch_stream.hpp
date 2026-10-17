#pragma once

#include <iosfwd>
#include <string>
#include <string_view>
#include <type_traits>

// Test-only stream boundary. Catch2's runtime still owns its diagnostic stream;
// ordinary assertions do not need to parse <ostream>/<chrono>/<format>.
namespace ludus::test
{
void Insert(std::ostream& stream, const std::string& value);
void Insert(std::ostream& stream, std::string_view value);
void Insert(std::ostream& stream, const char* value);
void Insert(std::ostream& stream, char value);
void Insert(std::ostream& stream, signed char value);
void Insert(std::ostream& stream, unsigned char value);
void Insert(std::ostream& stream, bool value);
void Insert(std::ostream& stream, long long value);
void Insert(std::ostream& stream, unsigned long long value);
void Insert(std::ostream& stream, double value);
void Insert(std::ostream& stream, long double value);

template <typename T>
    requires std::is_integral_v<T> && (!std::is_same_v<T, char>) && (!std::is_same_v<T, bool>)
void Insert(std::ostream& stream, T value)
{
    if constexpr (std::is_signed_v<T>)
    {
        Insert(stream, static_cast<long long>(value));
    }
    else
    {
        Insert(stream, static_cast<unsigned long long>(value));
    }
}

// User-defined stream printers remain available by ADL. Their owners explicitly
// include <ostream>; scalar/range diagnostics use the compiled overloads above.
template <typename T>
    requires(!std::is_arithmetic_v<T>) && (!std::is_convertible_v<const T&, const char*>) &&
            (!std::is_same_v<T, std::string>)
void Insert(std::ostream& stream, const T& value)
{
    stream << value;
}
} // namespace ludus::test
