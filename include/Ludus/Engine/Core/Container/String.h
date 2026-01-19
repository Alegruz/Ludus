#pragma once

#include <Ludus/Engine/Core/Container/Array.h>

namespace ludus::core
{
    using String    = DynamicArray<char>;
    using WString   = DynamicArray<wchar_t>;
    
    template<StringCharType CharT>
    using BasicString = std::conditional_t<std::is_same_v<CharT, char>, String, WString>;

    template<typename T>
    concept StringType = std::is_same_v<T, String> || std::is_same_v<T, WString>;
}   // namespace ludus::core