#include <Ludus/Editor/Pch.hpp>

#include <Ludus/Engine/Core/CommandLineManager.hpp>
#include <Ludus/Engine/Core/Container/Array.hpp>
#include <Ludus/Engine/Core/Container/String.hpp>

#include <iostream>

int main(int argc, char** argv)
{
	ludus::core::CommandLineManager<char> commandLineManager = ludus::core::CommandLineManager<char>::Create(argc, argv);
	const ludus::core::DynamicArray<ludus::core::String>& arguments = commandLineManager.GetArguments();
	bool quickExit = false;
	
	for (const ludus::core::String& arg : arguments)
	{
		// For demonstration purposes, output each argument to the debug console
		ludus::core::String debugOutput = "Argument: ";
		debugOutput.Append(arg.GetData(), arg.GetSize());
		debugOutput.PushBack('\n');
		std::cout << debugOutput.GetData();
		
		// Check for quick exit flag
		if (arg == "--quick-exit" || arg == "--exit")
		{
			quickExit = true;
		}
	}

	// Exit early if quick exit flag is set
	if (quickExit)
	{
		return 0;
	}

	return 0;
}