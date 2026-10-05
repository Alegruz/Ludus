#pragma once

// Thanks to ISO/IEC JTC1/SC22/WG21, C++ Working Draft [basic.fundamental], for
// the native scalar representation contract: https://eel.is/c++draft/basic.fundamental
// We retain native aliases and verify Ludus's narrower supported representations.
// See docs/architecture/primitive-types.md; no reference implementation was copied.

#include <cstddef>
#include <cstdint>

// Ludus fixed-width primitive types. Engine code uses these spellings
// (uint32, int64, usize, float32, ...) instead of std::uint32_t / std::size_t /
// float so that widths are explicit and consistent across the codebase.
//
// These are aliases of the standard <cstdint> / <cstddef> types, not
// hand-rolled typedefs: that keeps them correct on every platform and ABI and
// interoperable with the standard library (sizeof, container sizes, memcpy,
// etc.). Prefer these aliases over the std:: spellings in all new code.

namespace ludus::foundation::core
{
// Unsigned integers.
using uint8 = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;

// Signed integers.
using int8 = std::int8_t;
using int16 = std::int16_t;
using int32 = std::int32_t;
using int64 = std::int64_t;

// Size and pointer-difference types. usize is what sizeof, array indexing, and
// container .size() return; isize is its signed counterpart (ptrdiff_t). Kept
// as the platform types so the codebase never fights the language on widths.
using usize = std::size_t;
using isize = std::ptrdiff_t;

// Floating point. IEEE binary32/binary64 representation is verified once in
// src/types.cpp; aliases do not set rounding, trap, or denormal handling policy.
using float32 = float;
using float64 = double;

// Cheap storage checks belong here; numeric representation checks compile once
// in src/types.cpp. Neither check changes the native aliases or their ABI.
static_assert(sizeof(uint8) == 1 && sizeof(int8) == 1);
static_assert(sizeof(uint16) == 2 && sizeof(int16) == 2);
static_assert(sizeof(uint32) == 4 && sizeof(int32) == 4);
static_assert(sizeof(uint64) == 8 && sizeof(int64) == 8);
static_assert(sizeof(float32) == 4 && sizeof(float64) == 8);
} // namespace ludus::foundation::core

// Re-export into ludus::foundation so most engine code can use the short-scoped
// names, mirroring how nullptr_t is surfaced from defines.h.
namespace ludus::foundation
{
using core::float32;
using core::float64;
using core::int16;
using core::int32;
using core::int64;
using core::int8;
using core::isize;
using core::uint16;
using core::uint32;
using core::uint64;
using core::uint8;
using core::usize;
} // namespace ludus::foundation
