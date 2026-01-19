#pragma once

#include <Ludus/Engine/Pch.hpp>

namespace ludus::core
{
    template<StringCharType CharT>
    class CommandLineManager final
    {
    public:
        static CommandLineManager Create(int32_t argc, CharT** argv) noexcept;
        static CommandLineManager Create(CharT* commandLine) noexcept;
    
    public:
        explicit CommandLineManager(DynamicArray<BasicString<CharT>>&& arguments) noexcept;
        ~CommandLineManager() noexcept = default;

        [[nodiscard]] const DynamicArray<BasicString<CharT>>& GetArguments() const noexcept;
    
    private:
        DynamicArray<BasicString<CharT>> mArguments;
    };
}   // namespace ludus::core