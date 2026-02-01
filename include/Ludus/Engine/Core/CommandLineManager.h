#pragma once

#include <Ludus/Engine/Core/Common.h>
#include <Ludus/Engine/Core/Container/String.h>

namespace ludus::core
{
    // Concept for manager types that can handle command line arguments
    template<typename Manager, typename CharT>
    concept CommandLineManager_HandleArgument = requires(Manager & manager, const BasicString<CharT>& arg) {
        { manager.template HandleArgument<CharT>(arg) } noexcept;
    };

    template<StringCharType CharT>
    class CommandLineManager final
    {
    public:
        static CommandLineManager Create(int32_t argc, CharT** argv) noexcept;
        static CommandLineManager Create(CharT* commandLine) noexcept;
    
    public:
        explicit CommandLineManager(DynamicArray<BasicString<CharT>>&& arguments) noexcept;
        CommandLineManager(const CommandLineManager&) noexcept;
        CommandLineManager(CommandLineManager&&) noexcept;
        ~CommandLineManager() noexcept;
        
        CommandLineManager& operator=(const CommandLineManager&) noexcept;
        CommandLineManager& operator=(CommandLineManager&&) noexcept;
        
        template<typename Manager>
            requires CommandLineManager_HandleArgument<Manager, CharT>
        void ParseCommandLine(Manager& manager) noexcept;

        [[nodiscard]] const DynamicArray<BasicString<CharT>>& GetArguments() const noexcept;
        
        // Indexed access with lookahead support for paired arguments
        [[nodiscard]] bool TryGetArgument(size_t index, BasicString<CharT>& outArg) const noexcept;
        [[nodiscard]] bool TryGetArgumentPair(size_t index, BasicString<CharT>& outKey, BasicString<CharT>& outValue) const noexcept;
        [[nodiscard]] size_t GetArgumentCount() const noexcept;
    
    private:
        DynamicArray<BasicString<CharT>> mArguments;
    };
}   // namespace ludus::core