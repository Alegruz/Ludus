#pragma once

#include <ludus/foundation/base/types.h>

#include <span>

#include <sys/stat.h>
#include <sys/types.h>

// Compile-time seam for a separate, unexported test backend. Production builds
// call POSIX directly and contain no mutable fault state or hook dispatch.
namespace ludus::foundation::filesystem::test
{
int OpenAt(int parent, const char* name, int flags) noexcept;
int Duplicate(int descriptor) noexcept;
int Metadata(int descriptor, struct stat* info) noexcept;
isize Read(int descriptor, std::span<uint8> buffer, off_t offset) noexcept;
int Close(int descriptor) noexcept;
bool AllocationAllowed() noexcept;
} // namespace ludus::foundation::filesystem::test
