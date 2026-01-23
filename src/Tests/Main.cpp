#include <Ludus/Engine/Core/Pch.hpp>

#include <Ludus/Engine/Core/UnitTest.hpp>
#include <Ludus/Engine/Core/CommandLineManager.hpp>
#include <Ludus/Engine/Core/LeakDetection.h>

#include <iostream>

int main(int argc, char** argv)
{
	// Scoped memory leak detector - automatically handles snapshots and reporting
	LUDUS_LEAK_DETECTOR();

	// Parse command line arguments (for future extensibility)
	ludus::core::CommandLineManager<char> commandLineManager = ludus::core::CommandLineManager<char>::Create(argc, argv);

	// Run all registered unit tests
	const ludus::core::DynamicArray<ludus::core::TestResult> results = ludus::core::UnitTestRegistry::GetInstance().RunAllTests();

	// Return non-zero if any tests failed
	for (const ludus::core::TestResult& result : results)
	{
		if (!result.Passed)
		{
			return 1;
		}
	}

	return 0;
}
