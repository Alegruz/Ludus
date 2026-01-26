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
        static constexpr uint32_t ARGUMENT_BUFFER_SIZE = 4096;
        
        DynamicArray<BasicString<CharT>> arguments;
        CharT buffer[ARGUMENT_BUFFER_SIZE]; // Temporary buffer for building arguments
        uint32_t bufferPos = 0;
        
        CharT* current = commandLine;
        bool inQuotes = false;
        CharT quoteChar = '\0';
        bool justClosedQuotes = false;

        while(*current != '\0')
        {
            // Handle escape sequences
            if(*current == '\\' && *(current + 1) != '\0')
            {
                CharT nextChar = *(current + 1);
                // Recognize common escape sequences
                switch(nextChar)
                {
                    case 'n':
                    case 't':
                    case 'r':
                    case '\\':
                    case '"':
                    case '\'':
                    case ' ':
                        if(bufferPos + 1 >= ARGUMENT_BUFFER_SIZE) {
                            buffer[bufferPos] = '\0';
                            arguments.PushBack( BasicString<CharT>(buffer) );
                            bufferPos = 0;
                        }
                        break;
                }
                switch(nextChar)
                {
                    case 'n':
                        buffer[bufferPos++] = '\n';
                        current += 2;
                        break;
                    case 't':
                        buffer[bufferPos++] = '\t';
                        current += 2;
                        break;
                    case 'r':
                        buffer[bufferPos++] = '\r';
                        current += 2;
                        break;
                    case '\\':
                        buffer[bufferPos++] = '\\';
                        current += 2;
                        break;
                    case '"':
                        buffer[bufferPos++] = '"';
                        current += 2;
                        break;
                    case '\'':
                        buffer[bufferPos++] = '\'';
                        current += 2;
                        break;
                    case ' ':
                        buffer[bufferPos++] = ' ';
                        current += 2;
                        break;
                    default:
                        if(bufferPos + 1 >= ARGUMENT_BUFFER_SIZE) 
                        {
                            LUDUS_ASSERT_MSG(false, "Argument buffer overflow");
                            buffer[bufferPos] = '\0';
                            arguments.PushBack( BasicString<CharT>(buffer) );
                            bufferPos = 0;
                        }
                        // Unknown escape sequence - keep the backslash
                        buffer[bufferPos++] = '\\';
                        ++current;
                        break;
                }
                justClosedQuotes = false;
                continue;
            }

            // Handle quote characters
            if((*current == '"' || *current == '\'') && !inQuotes)
            {
                inQuotes = true;
                quoteChar = *current;
                justClosedQuotes = false;
                ++current;
                continue;
            }

            if(*current == quoteChar && inQuotes)
            {
                inQuotes = false;
                quoteChar = '\0';
                justClosedQuotes = true;
                ++current;
                continue;
            }

            // Handle spaces (argument separator if not in quotes)
            if(*current == ' ' && !inQuotes)
            {
                // Add the argument if we have one or if we just closed quotes (even if empty)
                if(bufferPos > 0 || justClosedQuotes)
                {
                    buffer[bufferPos] = '\0';
                    arguments.PushBack( BasicString<CharT>(buffer) );
                    bufferPos = 0;
                    justClosedQuotes = false;
                }
                // Skip consecutive spaces
                while(*current == ' ')
                {
                    ++current;
                }
                continue;
            }

            // Regular character
            if(bufferPos + 1 >= ARGUMENT_BUFFER_SIZE)
            {
                LUDUS_ASSERT_MSG(false, "Command line argument too long: buffer overflow detected; argument will be ignored.");
                // Skip this token until next separator
                while(*current != ' ' && *current != '\0')
                {
                    ++current;
                }
                bufferPos = 0;
                justClosedQuotes = false;
                continue;
            }
            buffer[bufferPos++] = *current;
            justClosedQuotes = false;
            ++current;
        }

        // Add the final argument if we have one or if we just closed quotes
        if(bufferPos > 0 || justClosedQuotes)
        {
            buffer[bufferPos] = '\0';
            arguments.PushBack( BasicString<CharT>(buffer) );
        }

        return CommandLineManager( std::move(arguments) );
    }

    template<StringCharType CharT>
    CommandLineManager<CharT>::CommandLineManager( DynamicArray<BasicString<CharT>>&& arguments ) noexcept
        : mArguments( std::move(arguments) )
    {
    }

    template<StringCharType CharT>
    CommandLineManager<CharT>::CommandLineManager(const CommandLineManager& other) noexcept = default;

    template<StringCharType CharT>
    CommandLineManager<CharT>::CommandLineManager(CommandLineManager&& other) noexcept = default;

    template<StringCharType CharT>
    CommandLineManager<CharT>::~CommandLineManager() noexcept = default;

    template<StringCharType CharT>
    CommandLineManager<CharT>& CommandLineManager<CharT>::operator=(const CommandLineManager& other) noexcept = default;

    template<StringCharType CharT>
    CommandLineManager<CharT>& CommandLineManager<CharT>::operator=(CommandLineManager&& other) noexcept = default;

    template<StringCharType CharT>
    template<typename Manager>
        requires CommandLineManager_HandleArgument<Manager, CharT>
    void CommandLineManager<CharT>::ParseCommandLine(Manager& manager) noexcept
    {
        for (const BasicString<CharT>& argument : mArguments)
        {
            manager.template HandleArgument<CharT>(argument);
        }
    }

    template<StringCharType CharT>
    [[nodiscard]] const DynamicArray<BasicString<CharT>>& CommandLineManager<CharT>::GetArguments() const noexcept
    {
        return mArguments;
    }
}   // namespace ludus::core