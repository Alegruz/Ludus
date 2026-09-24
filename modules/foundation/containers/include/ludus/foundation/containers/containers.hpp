#pragma once

// Convenience umbrella for the Ludus foundational contiguous containers.
// Prefer including the specific header you need (array.hpp / vector.hpp) to keep
// translation-unit include cost minimal; this aggregate exists for call sites
// that legitimately use several.

#include <ludus/foundation/containers/array.hpp>
#include <ludus/foundation/containers/relocation.hpp>
#include <ludus/foundation/containers/vector.hpp>
