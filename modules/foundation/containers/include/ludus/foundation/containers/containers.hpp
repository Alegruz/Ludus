#pragma once

// Convenience umbrella for the Ludus foundational contiguous containers.
// Prefer including the specific header you need (static_array.hpp for the
// fixed-size StaticArray<T, N>, array.hpp for the dynamic Array<T>) to keep
// translation-unit include cost minimal; this aggregate exists for call sites
// that legitimately use several.

#include <ludus/foundation/containers/array.hpp>
#include <ludus/foundation/containers/relocation.hpp>
#include <ludus/foundation/containers/static_array.hpp>
