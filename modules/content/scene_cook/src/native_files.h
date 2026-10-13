#pragma once

// Private POSIX helpers for the scene pipeline: path resolution, directory
// creation and atomic whole-file replacement of arbitrary size.

#include <ludus/content/content.h>
#include <ludus/foundation/strings/string.hpp>

#include <span>
#include <string_view>

namespace ludus::content::scene_cook::detail
{
/// realpath(3) of an existing directory.
[[nodiscard]] Status ResolveDirectory(std::string_view path, foundation::String& outPath) noexcept;
/// Final component of a resolved path.
[[nodiscard]] std::string_view GetBaseName(std::string_view path) noexcept;
/// mkdir -p with mode 0777 (the process umask applies).
[[nodiscard]] Status MakeDirectories(std::string_view path) noexcept;
/// Replace directory/name with data via a same-directory temporary and rename.
[[nodiscard]] Status
WriteFileAtomic(std::string_view directory, std::string_view name, std::span<const uint8> data) noexcept;
} // namespace ludus::content::scene_cook::detail
