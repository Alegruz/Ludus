#pragma once

// Fixed RGB approximations of pbrt's named spectra and blackbody emitters.

#include <ludus/foundation/base/core.h>

#include <string_view>

namespace ludus::content::scene_cook::importers
{
using foundation::core::float64;

/// Normal-incidence reflectance (linear RGB) of a pbrt named conductor
/// spectrum such as "metal-Cu-eta" or "metal-Au-k"; false when unknown.
[[nodiscard]] bool LookUpConductorColour(std::string_view spectrumName, float64 (&outColour)[3]) noexcept;
/// RGB for a named illuminant/reflectance spectrum (stdillum-*, metal-*); false when unknown.
[[nodiscard]] bool LookUpNamedSpectrum(std::string_view spectrumName, float64 (&outColour)[3]) noexcept;
/// Normalised linear RGB (max channel 1) of a blackbody at kelvin.
void ConvertBlackbodyToRgb(float64 kelvin, float64 (&outColour)[3]) noexcept;
} // namespace ludus::content::scene_cook::importers
