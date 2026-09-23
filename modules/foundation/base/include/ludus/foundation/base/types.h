#pragma once

#include <cstddef>
#include <cstdint>

// Ludus fixed-width primitive types. Engine code uses these spellings
// (uint32, int64, usize, f32, ...) instead of std::uint32_t / std::size_t /
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

// Floating point. f32/f64 make the width explicit at call sites.
using f32 = float;
using f64 = double;
} // namespace ludus::foundation::core

// Re-export into ludus::foundation so most engine code can use the short-scoped
// names, mirroring how nullptr_t is surfaced from defines.h.
namespace ludus::foundation
{
using core::f32;
using core::f64;
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
