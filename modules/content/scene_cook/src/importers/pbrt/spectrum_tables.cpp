// Conductor colours: normal-incidence reflectance from the published
// "Physically Based" material charts (Dontnod / Unreal Engine 4 tables of F0
// in linear sRGB), used as a stand-in for pbrt's tabulated eta/k spectra.
// Blackbody: Tanner Helland, "How to Convert Temperature (K) to RGB" (2012),
// https://tannerhelland.com/2012/09/18/convert-temperature-rgb-algorithm-code.html,
// a curve fit adopted here and then linearised.

#include "spectrum_tables.h"

#include <ludus/foundation/math/scalar.hpp>

namespace ludus::content::scene_cook::importers
{
namespace
{
namespace math = foundation::math;

struct NamedColour final
{
    std::string_view Metal;
    float64 Colour[3];
};

constexpr NamedColour CONDUCTORS[] = {
    {"Ag", {.972, .960, .915}},
    {"Al", {.913, .922, .924}},
    {"Au", {1.0, .766, .336}},
    {"Cu", {.955, .638, .538}},
    {"CuZn", {.910, .778, .423}},
    {"MgO", {.95, .95, .95}},
    {"TiO2", {.95, .95, .95}},
};

[[nodiscard]] bool FindConductor(std::string_view metal, float64 (&outColour)[3]) noexcept
{
    for (const NamedColour& entry : CONDUCTORS)
    {
        if (entry.Metal == metal)
        {
            for (int k = 0; k < 3; ++k)
            {
                outColour[k] = entry.Colour[k];
            }
            return true;
        }
    }
    return false;
}
} // namespace

bool LookUpConductorColour(std::string_view spectrumName, float64 (&outColour)[3]) noexcept
{
    if (spectrumName.substr(0, 6) != "metal-")
    {
        return false;
    }
    std::string_view metal = spectrumName.substr(6);
    const foundation::core::usize dash = metal.rfind('-');
    if (dash != std::string_view::npos)
    {
        metal = metal.substr(0, dash);
    }
    return FindConductor(metal, outColour);
}

bool LookUpNamedSpectrum(std::string_view spectrumName, float64 (&outColour)[3]) noexcept
{
    if (spectrumName.substr(0, 9) == "stdillum-")
    {
        outColour[0] = outColour[1] = outColour[2] = 1.0;
        if (spectrumName == "stdillum-A")
        {
            ConvertBlackbodyToRgb(2856.0, outColour);
        }
        return true;
    }
    return LookUpConductorColour(spectrumName, outColour);
}

void ConvertBlackbodyToRgb(float64 kelvin, float64 (&outColour)[3]) noexcept
{
    const float64 t = math::Clamp(kelvin, 1000.0, 40000.0) / 100.0;
    float64 red = 255.0;
    float64 green = 0.0;
    float64 blue = 255.0;
    if (t > 66.0)
    {
        red = 329.698727446 * math::Pow(t - 60.0, -0.1332047592);
        green = 288.1221695283 * math::Pow(t - 60.0, -0.0755148492);
    }
    else
    {
        green = 99.4708025861 * math::Log(t) - 161.1195681661;
        if (t <= 19.0)
        {
            blue = 0.0;
        }
        else if (t < 66.0)
        {
            blue = 138.5177312231 * math::Log(t - 10.0) - 305.0447927307;
        }
    }
    const float64 encoded[3] = {red, green, blue};
    float64 maximum = 0.0;
    for (int k = 0; k < 3; ++k)
    {
        outColour[k] = static_cast<float64>(
            math::DecodeSrgb(static_cast<foundation::core::float32>(math::Clamp(encoded[k], 0.0, 255.0) / 255.0)));
        maximum = math::Max(maximum, outColour[k]);
    }
    for (int k = 0; k < 3 && maximum > 0.0; ++k)
    {
        outColour[k] /= maximum;
    }
}
} // namespace ludus::content::scene_cook::importers
