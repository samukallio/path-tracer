#pragma once

#include "common.h"

struct parametric_spectrum_table
{
    static constexpr int SCALE_BINS = 64;
    static constexpr int COLOR_BINS = 64;

    vec3 Coefficients[3][SCALE_BINS][COLOR_BINS][COLOR_BINS];
};

void    BuildParametricSpectrumTableForSRGB(parametric_spectrum_table* Table);
bool    SaveParametricSpectrumTable(parametric_spectrum_table const* Table, char const* Path);
bool    LoadParametricSpectrumTable(parametric_spectrum_table* Table, char const* Path);
vec3    GetParametricSpectrumCoefficients(parametric_spectrum_table const* Table, glm::vec3 const& Color);
float   SampleParametricSpectrum(glm::vec3 const& Beta, float Lambda);
