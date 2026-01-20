#include <Ludus/Engine/Pch.hpp>

#include <Ludus/Engine/Core/Container/Array.hpp>
#include <Ludus/Engine/Core/Container/String.hpp>

namespace ludus::core
{
    // Explicit template instantiations for the underlying array types
    template class ArrayImpl<char, ArrayType::Dynamic, 0, ArrayResizePolicy::Default>;
    template class ArrayImpl<wchar_t, ArrayType::Dynamic, 0, ArrayResizePolicy::Default>;
    template class ArrayImplBase<char, ArrayType::Dynamic, 0, ArrayResizePolicy::Default>;
    template class ArrayImplBase<wchar_t, ArrayType::Dynamic, 0, ArrayResizePolicy::Default>;
} // namespace ludus::core
