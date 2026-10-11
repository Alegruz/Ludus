#include "catch_stream.hpp"

#include <ostream>

#include <catch2/catch_tostring.hpp>

namespace ludus::test
{
void Insert(std::ostream& stream, const std::string& value)
{
    stream << value;
}
void Insert(std::ostream& stream, const char* value)
{
    stream << value;
}
void Insert(std::ostream& stream, char value)
{
    stream << value;
}
void Insert(std::ostream& stream, signed char value)
{
    stream << value;
}
void Insert(std::ostream& stream, unsigned char value)
{
    stream << value;
}
void Insert(std::ostream& stream, bool value)
{
    stream << value;
}
void Insert(std::ostream& stream, long long value)
{
    stream << value;
}
void Insert(std::ostream& stream, unsigned long long value)
{
    stream << value;
}
void Insert(std::ostream& stream, double value)
{
    stream << value;
}
void Insert(std::ostream& stream, long double value)
{
    stream << value;
}
} // namespace ludus::test

std::string Catch::StringMaker<long double>::convert(long double value)
{
    Catch::ReusableStringStream stream;
    stream << value;
    return stream.str();
}
