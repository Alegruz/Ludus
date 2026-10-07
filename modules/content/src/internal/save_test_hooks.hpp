#pragma once

#include <ludus/foundation/base/types.h>

#include <span>

// Compile-time seam for a separate, unexported test archive. Production has no
// hook dispatch or mutable fault state; the real POSIX save algorithm is tested.
namespace ludus::content::test
{
isize Write(int descriptor, std::span<const uint8> data) noexcept;
int Sync(int descriptor) noexcept;
int Replace(int directory, const char* source, const char* destination) noexcept;
int Close(int descriptor) noexcept;
} // namespace ludus::content::test
