#pragma once

#include <ludus/foundation/base/types.h>

#include <span>
#include <sys/stat.h>

namespace ludus::foundation::filesystem::persistence_test
{
bool AllocationAllowed() noexcept;
int OpenAt(int parent, const char* name, int flags, mode_t mode) noexcept;
int MetadataAt(int parent, const char* name, struct stat* info, int flags) noexcept;
int Lock(int descriptor, int operation) noexcept;
isize Write(int descriptor, std::span<const uint8> bytes) noexcept;
int Sync(int descriptor) noexcept;
int Replace(int parent, const char* source, const char* target) noexcept;
int Unlink(int parent, const char* name) noexcept;
int Close(int descriptor) noexcept;
} // namespace ludus::foundation::filesystem::persistence_test
