#pragma once

#include <Ludus/Engine/Core/CommandLineManager.h>

namespace ludus::core
{
    template<StringCharType CharT>
    CommandLineManager<CharT> CommandLineManager<CharT>::Create(int32_t argc, CharT** argv) noexcept
    {
        DynamicArray<BasicString<CharT>> arguments;
        for(int32_t i = 0; i < argc; ++i)
        {
            arguments.PushBack( BasicString<CharT>(argv[i]) );
        }
        return CommandLineManager( std::move(arguments) );
    }

    template<StringCharType CharT>
    CommandLineManager<CharT> CommandLineManager<CharT>::Create(CharT* commandLine) noexcept
    {
        DynamicArray<BasicString<CharT>> arguments;
        CharT* current = commandLine;
        while(*current != '\0')
        {
            // Skip leading spaces
            while(*current == ' ')
            {
                ++current;
            }

            if(*current == '\0')
            {
                break;
            }

            // Find the end of the argument
            CharT* start = current;
            while(*current != ' ' && *current != '\0')
            {
                ++current;
            }

            // Extract the argument
            const uint32_t length = static_cast<uint32_t>(current - start);
            BasicString<CharT> argument(start, length);
            arguments.PushBack( std::move(argument) );
        }
        return CommandLineManager( std::move(arguments) );
    }

    template<StringCharType CharT>
    CommandLineManager<CharT>::CommandLineManager( DynamicArray<BasicString<CharT>>&& arguments ) noexcept
        : mArguments( std::move(arguments) )
    {
    }

    template<StringCharType CharT>
    [[nodiscard]] const DynamicArray<BasicString<CharT>>& CommandLineManager<CharT>::GetArguments() const noexcept
    {
        return mArguments;
    }
}   // namespace ludus::core