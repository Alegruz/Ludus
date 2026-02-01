#pragma once

// C++ Standard Library MUST come before any custom headers
#include <concepts>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <type_traits>
#include <utility>
#include <variant>

// Core infrastructure (platform detection and compiler macros)
#include <Ludus/Engine/Core/Compiler.h>
#include <Ludus/Engine/Core/PlatformDetection.h>

// Core headers
#include <Ludus/Engine/Core/Assert.h>
#include <Ludus/Engine/Core/Container/Array.hpp>
#include <Ludus/Engine/Core/Math/Bit.hpp>
#include <Ludus/Engine/Core/Container/String.hpp>