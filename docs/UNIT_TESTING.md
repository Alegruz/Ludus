# Ludus Unit Test Framework

A simple, lightweight unit test framework for the Ludus game engine core systems.

## Overview

This framework provides basic unit testing capabilities without being overly complex. It's designed to validate core systems like math, containers, and low-level utilities.

## Running Tests

There are two ways to run the unit tests:

### 1. Runtime Arguments (Dynamic Branching)

Run the executable with one of the following arguments:
```bash
LudusEditor.exe --run-tests
LudusEditor.exe --test
LudusEditor.exe --tests
```

### 2. Compile-Time Macro (Conditional Compilation)

Define the `LUDUS_RUN_TESTS` macro during compilation to automatically run tests at startup:

**CMake:**
```cmake
target_compile_definitions(YourTarget PRIVATE LUDUS_RUN_TESTS)
```

**Visual Studio:**
Add `LUDUS_RUN_TESTS` to Project Properties → C/C++ → Preprocessor → Preprocessor Definitions

**Command Line:**
```bash
cmake -DCMAKE_CXX_FLAGS="-DLUDUS_RUN_TESTS" ..
```

### 3. CMake Presets (Recommended)

Use the dedicated test presets for your platform:

**Windows (MSVC):**
```bash
cmake --preset test_msvc
cmake --build --preset test_msvc
out/build/test_msvc/bin/LudusEditor.exe
```

**Linux (Clang):**
```bash
cmake --preset test_clang
cmake --build --preset test_clang
./out/build/test_clang/bin/LudusEditor
```

**Linux (GCC):**
```bash
cmake --preset test_gcc
cmake --build --preset test_gcc
./out/build/test_gcc/bin/LudusEditor
```

## Continuous Integration

Unit tests run automatically on every push and pull request via GitHub Actions.

The workflow tests on multiple platforms and compilers:
- Windows with MSVC
- Linux with Clang
- Linux with GCC

View test results in the **Actions** tab of the GitHub repository. The workflow:
1. Builds the project with `LUDUS_RUN_TESTS` enabled
2. Executes the test suite
3. Reports pass/fail status
4. Uploads build artifacts for debugging

To see CI status, check the badge at the top of the README or visit:
`https://github.com/YourOrg/Ludus/actions/workflows/unit-tests.yml`

## Writing Tests

### Basic Test Structure

Use the `LUDUS_TEST()` macro to define a test:

```cpp
#include <Ludus/Engine/Core/UnitTest.hpp>
#include <Ludus/Engine/Core/Math/Vector.hpp>

using namespace ludus::core;

LUDUS_TEST(MyTestName)
{
    Vector2<float> v(3.0f, 4.0f);
    LUDUS_TEST_ASSERT_EQ(v.X, 3.0f);
    LUDUS_TEST_ASSERT_EQ(v.Y, 4.0f);
}
```

### Available Assertion Macros

- **`LUDUS_TEST_ASSERT(condition)`** - Assert that a condition is true
- **`LUDUS_TEST_ASSERT_MSG(condition, message)`** - Assert with custom message
- **`LUDUS_TEST_ASSERT_EQ(expected, actual)`** - Assert equality
- **`LUDUS_TEST_ASSERT_NE(expected, actual)`** - Assert inequality
- **`LUDUS_TEST_ASSERT_NEAR(expected, actual, epsilon)`** - Assert floating-point near-equality

### Example Tests

```cpp
LUDUS_TEST(Vector2_Addition)
{
    Vector2<float> v1(1.0f, 2.0f);
    Vector2<float> v2(3.0f, 4.0f);
    Vector2<float> result = v1 + v2;

    LUDUS_TEST_ASSERT_EQ(result.X, 4.0f);
    LUDUS_TEST_ASSERT_EQ(result.Y, 6.0f);
}

LUDUS_TEST(Vector2_Length)
{
    Vector2<float> v(3.0f, 4.0f);
    float length = v.Length();
    
    LUDUS_TEST_ASSERT_NEAR(length, 5.0f, 0.0001f);
}

LUDUS_TEST(String_Construction)
{
    String str("Hello");
    LUDUS_TEST_ASSERT_EQ(str.GetSize(), 5u);
    LUDUS_TEST_ASSERT(str[0] == 'H');
}
```

## Test Organization

Tests are automatically registered at static initialization time. Simply include the test file in your build, and tests will be discovered automatically.

**Current test locations:**
- `src/Engine/Core/CoreTests.cpp` - Core system tests (Vector, Math, Containers)

## Output Format

When tests run, you'll see output like:

```
========================================
Running Unit Tests
========================================

Running test: Vector2_Construction... PASSED
Running test: Vector2_Addition... PASSED
Running test: Vector2_DotProduct... PASSED
Running test: Trigonometry_Sin... FAILED
  Error: Assertion failed: sine ~= 0.5 (within 0.0001)

========================================
Test Results
========================================
Total tests: 20
Passed: 19
Failed: 1
========================================
```

## Architecture Notes

- **Simple & Lightweight**: No external dependencies, uses STL and engine containers
- **Auto-Registration**: Tests register themselves using static initialization
- **Header-Only Implementation**: Framework is entirely in headers for easy integration
- **No TDD Overhead**: Focused on validation, not test-driven development workflows
- **Separated from Main Loop**: Tests can run before or instead of the main application loop

## Design Philosophy

This isn't a TDD framework. It's designed to:
- Validate core math and utility functions
- Catch regressions in low-level systems
- Provide quick sanity checks during development
- Be easy to use without excessive boilerplate
