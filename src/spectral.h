#pragma once

#include "common.h"

constexpr float CIE_LAMBDA_MIN = 360.0f;
constexpr float CIE_LAMBDA_MAX = 830.0f;

// Conversion from CIE XYZ to (linear) sRGB tristimulus values.
const glm::mat3 CIE_XYZ_TO_SRGB =
{
    +3.2406f, -0.9689f, +0.0557f,
    -1.5372f, +1.8758f, -0.2040f,
    -0.4986f, +0.0415f, +1.0570f,
};

const glm::mat3 CIE_SRGB_TO_XYZ =
{
    +0.4124f, +0.2126f, +0.0193f,
    +0.3576f, +0.7152f, +0.1192f,
    +0.1805f, +0.0722f, +0.9505f,
};

inline glm::vec3 XYZToSRGB(glm::vec3 XYZ)
{
    return CIE_XYZ_TO_SRGB * XYZ;
}

inline glm::vec3 SRGBToXYZ(glm::vec3 SRGB)
{
    return CIE_SRGB_TO_XYZ * SRGB;
}

struct parametric_spectrum_table
{
    static constexpr int SCALE_BINS = 64;
    static constexpr int COLOR_BINS = 64;

    glm::vec3 Coefficients[3][SCALE_BINS][COLOR_BINS][COLOR_BINS];
};

void BuildParametricSpectrumTableForSRGB(
    parametric_spectrum_table* Table);

bool SaveParametricSpectrumTable(
    parametric_spectrum_table const* Table,
    char const* Path);

bool LoadParametricSpectrumTable(
    parametric_spectrum_table* Table,
    char const* Path);

glm::vec3 GetParametricSpectrumCoefficients(
    parametric_spectrum_table const* Table,
    glm::vec3 const& Color);

float SampleParametricSpectrum(
    glm::vec3 const& Beta,
    float Lambda);
