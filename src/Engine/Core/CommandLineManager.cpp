#include <Ludus/Engine/Pch.hpp>

#include <Ludus/Engine/Core/CommandLineManager.hpp>

namespace ludus::core
{
    // Explicit template instantiations
    template class CommandLineManager<char>;
    template class CommandLineManager<wchar_t>;
} // namespace ludus::core
