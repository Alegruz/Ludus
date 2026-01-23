#pragma once

#include <Ludus/Engine/Core/UnitTest.h>

#include <iostream>

namespace ludus::core
{
	inline UnitTest::UnitTest(const char* name)
		: m_Name(name)
	{
	}

	inline void UnitTest::Fail(const char* message)
	{
		m_Failed = true;
		m_FailureMessage = message;
	}

	inline UnitTestRegistry& UnitTestRegistry::GetInstance()
	{
		static UnitTestRegistry instance;
		return instance;
	}

	inline void UnitTestRegistry::RegisterTest(UnitTest* test)
	{
		m_Tests.PushBack(test);
	}

	inline DynamicArray<TestResult> UnitTestRegistry::RunAllTests()
	{
		DynamicArray<TestResult> results;
		
		std::cout << "========================================\n";
		std::cout << "Running Unit Tests\n";
		std::cout << "========================================\n\n";

		uint32_t passed = 0;
		uint32_t failed = 0;

		for (UnitTest* test : m_Tests)
		{
			std::cout << "Running test: " << test->GetName() << "... ";
			
			test->Run();
			
			TestResult result;
			result.TestName = test->GetName();
			result.Passed = !test->HasFailed();
			
			if (test->HasFailed())
			{
				result.Message = test->GetFailureMessage();
				std::cout << "FAILED\n";
				std::cout << "  Error: " << result.Message.GetData() << "\n";
				failed++;
			}
			else
			{
				result.Message = "Test passed";
				std::cout << "PASSED\n";
				passed++;
			}
			
			results.PushBack(std::move(result));
		}

		std::cout << "\n========================================\n";
		std::cout << "Test Results\n";
		std::cout << "========================================\n";
		std::cout << "Total tests: " << (passed + failed) << "\n";
		std::cout << "Passed: " << passed << "\n";
		std::cout << "Failed: " << failed << "\n";
		std::cout << "========================================\n\n";

		return results;
	}
}	// namespace ludus::core
