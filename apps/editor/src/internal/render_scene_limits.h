#pragma once

#include <ludus/foundation/base/types.h>

namespace ludus::editor
{
/// Inclusive encoded JSON file size for the preview manifest and mesh sources.
inline constexpr foundation::usize MAX_PREVIEW_DOCUMENT_BYTES = foundation::usize{1024} * 1024;
/// Inclusive encoded PNG file size; decoded textures have separate SceneFormat caps.
inline constexpr foundation::usize MAX_PREVIEW_TEXTURE_FILE_BYTES = foundation::usize{4} * 1024 * 1024;
} // namespace ludus::editor
