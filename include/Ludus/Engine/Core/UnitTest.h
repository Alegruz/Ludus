#pragma once

#include <Ludus/Engine/Core/Common.h>
#include <Ludus/Engine/Core/Container/Array.hpp>
#include <Ludus/Engine/Core/Container/String.hpp>

namespace ludus::core
{
	// Forward declarations
	class UnitTestRegistry;
	class UnitTest;

	// Test result tracking
	struct TestResult
	{
		String TestName;
		String Message;
		bool Passed;
	};

	// Base class for unit tests
	class UnitTest
	{
	public:
		explicit UnitTest(const char* name);
		virtual ~UnitTest() = default;

		virtual void Run() = 0;
		[[nodiscard]] const char* GetName() const noexcept { return m_Name; }
		[[nodiscard]] bool HasFailed() const noexcept { return m_Failed; }
		[[nodiscard]] const String& GetFailureMessage() const noexcept { return m_FailureMessage; }

	protected:
		void Fail(const char* message);

	private:
		const char* m_Name;
		bool m_Failed = false;
		String m_FailureMessage;
	};

	// Registry for all unit tests
	class UnitTestRegistry
	{
	public:
		static UnitTestRegistry& GetInstance();

		void RegisterTest(UnitTest* test);
		DynamicArray<TestResult> RunAllTests();

	private:
		UnitTestRegistry() = default;
		DynamicArray<UnitTest*> m_Tests;
	};

	// Helper class to auto-register tests
	class UnitTestRegistrar
	{
	public:
		explicit UnitTestRegistrar(UnitTest* test)
		{
			UnitTestRegistry::GetInstance().RegisterTest(test);
		}
	};
}	// namespace ludus::core

// Test macros
#define LUDUS_TEST(TestName)                                                                       \
	class Test_##TestName : public ludus::core::UnitTest                                           \
	{                                                                                              \
	public:                                                                                        \
		Test_##TestName() : UnitTest(#TestName) {}                                                 \
		void Run() override;                                                                       \
	};                                                                                             \
	/* NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) */                      \
	static Test_##TestName g_Test_##TestName##_Instance;                                           \
	/* NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) */                      \
	static ludus::core::UnitTestRegistrar g_Test_##TestName##_Registrar(&g_Test_##TestName##_Instance); \
	void Test_##TestName::Run()

// Assertion macros for tests
#define LUDUS_TEST_ASSERT(condition)                                                               \
	do                                                                                             \
	{                                                                                              \
		if (!(condition))                                                                          \
		{                                                                                          \
			ludus::core::String msg = "Assertion failed: ";                                        \
			const char* condStr = #condition;                                                      \
			msg.Append(condStr, static_cast<uint32_t>(strlen(condStr)));                          \
			Fail(msg.GetData());                                                                   \
			return;                                                                                \
		}                                                                                          \
	} while (0)

#define LUDUS_TEST_ASSERT_MSG(condition, message)                                                  \
	do                                                                                             \
	{                                                                                              \
		if (!(condition))                                                                          \
		{                                                                                          \
			ludus::core::String msg = "Assertion failed: ";                                        \
			const char* condStr = #condition;                                                      \
			msg.Append(condStr, static_cast<uint32_t>(strlen(condStr)));                          \
			const char* sepStr = " - ";                                                           \
			msg.Append(sepStr, 3);                                                                 \
			msg.Append(message, static_cast<uint32_t>(strlen(message)));                          \
			Fail(msg.GetData());                                                                   \
			return;                                                                                \
		}                                                                                          \
	} while (0)

#define LUDUS_TEST_ASSERT_EQ(expected, actual)                                                     \
	do                                                                                             \
	{                                                                                              \
		if (!((expected) == (actual)))                                                             \
		{                                                                                          \
			ludus::core::String msg = "Assertion failed: ";                                        \
			const char* expStr = #expected;                                                        \
			msg.Append(expStr, static_cast<uint32_t>(strlen(expStr)));                            \
			const char* eqStr = " == ";                                                           \
			msg.Append(eqStr, 4);                                                                  \
			const char* actStr = #actual;                                                          \
			msg.Append(actStr, static_cast<uint32_t>(strlen(actStr)));                            \
			Fail(msg.GetData());                                                                   \
			return;                                                                                \
		}                                                                                          \
	} while (0)

#define LUDUS_TEST_ASSERT_NE(expected, actual)                                                     \
	do                                                                                             \
	{                                                                                              \
		if (!((expected) != (actual)))                                                             \
		{                                                                                          \
			ludus::core::String msg = "Assertion failed: ";                                        \
			const char* expStr = #expected;                                                        \
			msg.Append(expStr, static_cast<uint32_t>(strlen(expStr)));                            \
			const char* neStr = " != ";                                                           \
			msg.Append(neStr, 4);                                                                  \
			const char* actStr = #actual;                                                          \
			msg.Append(actStr, static_cast<uint32_t>(strlen(actStr)));                            \
			Fail(msg.GetData());                                                                   \
			return;                                                                                \
		}                                                                                          \
	} while (0)

#define LUDUS_TEST_ASSERT_NEAR(expected, actual, epsilon)                                          \
	do                                                                                             \
	{                                                                                              \
		auto diff = (expected) > (actual) ? (expected) - (actual) : (actual) - (expected);         \
		if (diff > (epsilon))                                                                      \
		{                                                                                          \
			ludus::core::String msg = "Assertion failed: ";                                        \
			const char* expStr = #expected;                                                        \
			msg.Append(expStr, static_cast<uint32_t>(strlen(expStr)));                            \
			const char* nearStr = " ~= ";                                                         \
			msg.Append(nearStr, 4);                                                                \
			const char* actStr = #actual;                                                          \
			msg.Append(actStr, static_cast<uint32_t>(strlen(actStr)));                            \
			const char* withinStr = " (within ";                                                  \
			msg.Append(withinStr, 9);                                                              \
			const char* epsStr = #epsilon;                                                         \
			msg.Append(epsStr, static_cast<uint32_t>(strlen(epsStr)));                            \
			const char* closeStr = ")";                                                            \
			msg.Append(closeStr, 1);                                                               \
			Fail(msg.GetData());                                                                   \
			return;                                                                                \
		}                                                                                          \
	} while (0)
