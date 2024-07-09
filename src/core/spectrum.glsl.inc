#ifndef SPECTRUM_GLSL_INC
#define SPECTRUM_GLSL_INC

/* --- CIE XYZ Standard Observer ------------------------------------------- */

// Compute the CIE XYZ tristimulus values of a single-wavelength Dirac spectrum
// using the multi-lobe piecewise Gaussian fit of the CIE 1931 standard observer
// presented in the paper "Simple Analytic Approximations to the CIE XYZ Color
// Matching Functions" by Chris Wyman et al.
vec3 SampleStandardObserver(float Lambda)
{
    vec3 Result;
    {
        float T1 = (Lambda - 442.0) * (Lambda < 442.0 ? 0.0624 : 0.0374);
        float T2 = (Lambda - 599.8) * (Lambda < 599.8 ? 0.0264 : 0.0323);
        float T3 = (Lambda - 501.1) * (Lambda < 501.1 ? 0.0490 : 0.0382);
        Result.x = 0.362 * exp(-0.5 * T1 * T1)
                 + 1.056 * exp(-0.5 * T2 * T2)
                 - 0.065 * exp(-0.5 * T3 * T3);
    }
    {
        float T1 = (Lambda - 568.8) * (Lambda < 568.8 ? 0.0213 : 0.0247);
        float T2 = (Lambda - 530.9) * (Lambda < 530.9 ? 0.0613 : 0.0322);
        Result.y = 0.821 * exp(-0.5 * T1 * T1)
                 + 0.286 * exp(-0.5 * T2 * T2);
    }
    {
        float T1 = (Lambda - 437.0) * (Lambda < 437.0 ? 0.0845 : 0.0278);
        float T2 = (Lambda - 459.0) * (Lambda < 459.0 ? 0.0385 : 0.0725);
        Result.z = 1.217 * exp(-0.5 * T1 * T1)
                 + 0.681 * exp(-0.5 * T2 * T2);
    }
    return Result;
}

// As above, but sample for 4 wavelengths simultaneously, producing 4 sets
// of tristimulus values.
mat4x3 SampleStandardObserver(vec4 Lambda)
{
    return mat4x3
    (
        SampleStandardObserver(Lambda.x),
        SampleStandardObserver(Lambda.y),
        SampleStandardObserver(Lambda.z),
        SampleStandardObserver(Lambda.w)
    );
}

// Conversion from CIE XYZ to (linear) sRGB tristimulus values.
const mat3 CIE_XYZ_TO_SRGB = mat3
(
    +3.2406, -0.9689, +0.0557,
    -1.5372, +1.8758, -0.2040,
    -0.4986, +0.0415, +1.0570
);

/* --- CIE Illuminant D65 -------------------------------------------------- */

// Spectrum of the CIE standard illuminant D65, 1 nm wavelength steps.
const float CIE_ILLUMINANT_D65[] =
{
     46.638, 47.183, 47.728, 48.273, 48.819, // 360-364 nm
     49.364, 49.909, 50.454, 50.999, 51.544, // 365-369 nm
     52.089, 51.878, 51.666, 51.455, 51.244, // 370-374 nm
     51.032, 50.821, 50.610, 50.398, 50.187, // 375-379 nm
     49.975, 50.443, 50.910, 51.377, 51.845, // 380-384 nm
     52.312, 52.779, 53.246, 53.714, 54.181, // 385-389 nm
     54.648, 57.459, 60.270, 63.080, 65.891, // 390-394 nm
     68.701, 71.512, 74.323, 77.134, 79.944, // 395-399 nm
     82.755, 83.628, 84.501, 85.374, 86.247, // 400-404 nm
     87.120, 87.994, 88.867, 89.740, 90.613, // 405-409 nm
     91.486, 91.681, 91.875, 92.070, 92.264, // 410-414 nm
     92.459, 92.653, 92.848, 93.043, 93.237, // 415-419 nm
     93.432, 92.757, 92.082, 91.407, 90.732, // 420-424 nm
     90.057, 89.382, 88.707, 88.032, 87.357, // 425-429 nm
     86.682, 88.501, 90.319, 92.137, 93.955, // 430-434 nm
     95.774, 97.592, 99.410, 101.228, 103.047, // 435-439 nm
    104.865, 106.079, 107.294, 108.508, 109.722, // 440-444 nm
    110.936, 112.151, 113.365, 114.579, 115.794, // 445-449 nm
    117.008, 117.088, 117.169, 117.249, 117.330, // 450-454 nm
    117.410, 117.490, 117.571, 117.651, 117.732, // 455-459 nm
    117.812, 117.517, 117.222, 116.927, 116.632, // 460-464 nm
    116.336, 116.041, 115.746, 115.451, 115.156, // 465-469 nm
    114.861, 114.967, 115.073, 115.180, 115.286, // 470-474 nm
    115.392, 115.498, 115.604, 115.711, 115.817, // 475-479 nm
    115.923, 115.212, 114.501, 113.789, 113.078, // 480-484 nm
    112.367, 111.656, 110.945, 110.233, 109.522, // 485-489 nm
    108.811, 108.865, 108.920, 108.974, 109.028, // 490-494 nm
    109.082, 109.137, 109.191, 109.245, 109.300, // 495-499 nm
    109.354, 109.199, 109.044, 108.888, 108.733, // 500-504 nm
    108.578, 108.423, 108.268, 108.112, 107.957, // 505-509 nm
    107.802, 107.501, 107.200, 106.898, 106.597, // 510-514 nm
    106.296, 105.995, 105.694, 105.392, 105.091, // 515-519 nm
    104.790, 105.080, 105.370, 105.660, 105.950, // 520-524 nm
    106.239, 106.529, 106.819, 107.109, 107.399, // 525-529 nm
    107.689, 107.361, 107.032, 106.704, 106.375, // 530-534 nm
    106.047, 105.719, 105.390, 105.062, 104.733, // 535-539 nm
    104.405, 104.369, 104.333, 104.297, 104.261, // 540-544 nm
    104.225, 104.190, 104.154, 104.118, 104.082, // 545-549 nm
    104.046, 103.641, 103.237, 102.832, 102.428, // 550-554 nm
    102.023, 101.618, 101.214, 100.809, 100.405, // 555-559 nm
    100.000, 99.633, 99.267, 98.900, 98.534, // 560-564 nm
     98.167, 97.800, 97.434, 97.067, 96.701, // 565-569 nm
     96.334, 96.280, 96.225, 96.170, 96.116, // 570-574 nm
     96.061, 96.007, 95.952, 95.897, 95.843, // 575-579 nm
     95.788, 95.078, 94.368, 93.657, 92.947, // 580-584 nm
     92.237, 91.527, 90.816, 90.106, 89.396, // 585-589 nm
     88.686, 88.818, 88.950, 89.082, 89.214, // 590-594 nm
     89.346, 89.478, 89.610, 89.742, 89.874, // 595-599 nm
     90.006, 89.966, 89.925, 89.884, 89.843, // 600-604 nm
     89.803, 89.762, 89.721, 89.680, 89.640, // 605-609 nm
     89.599, 89.409, 89.219, 89.029, 88.839, // 610-614 nm
     88.649, 88.459, 88.269, 88.079, 87.889, // 615-619 nm
     87.699, 87.258, 86.817, 86.376, 85.935, // 620-624 nm
     85.494, 85.053, 84.612, 84.171, 83.730, // 625-629 nm
     83.289, 83.330, 83.371, 83.412, 83.453, // 630-634 nm
     83.494, 83.535, 83.576, 83.617, 83.658, // 635-639 nm
     83.699, 83.332, 82.965, 82.597, 82.230, // 640-644 nm
     81.863, 81.496, 81.129, 80.761, 80.394, // 645-649 nm
     80.027, 80.046, 80.064, 80.083, 80.102, // 650-654 nm
     80.121, 80.139, 80.158, 80.177, 80.196, // 655-659 nm
     80.215, 80.421, 80.627, 80.834, 81.040, // 660-664 nm
     81.246, 81.453, 81.659, 81.865, 82.072, // 665-669 nm
     82.278, 81.878, 81.479, 81.080, 80.680, // 670-674 nm
     80.281, 79.882, 79.482, 79.083, 78.684, // 675-679 nm
     78.284, 77.428, 76.572, 75.715, 74.859, // 680-684 nm
     74.003, 73.147, 72.290, 71.434, 70.578, // 685-689 nm
     69.721, 69.910, 70.099, 70.288, 70.476, // 690-694 nm
     70.665, 70.854, 71.043, 71.231, 71.420, // 695-699 nm
     71.609, 71.883, 72.157, 72.431, 72.705, // 700-704 nm
     72.979, 73.253, 73.527, 73.801, 74.075, // 705-709 nm
     74.349, 73.075, 71.800, 70.525, 69.251, // 710-714 nm
     67.977, 66.702, 65.427, 64.153, 62.879, // 715-719 nm
     61.604, 62.432, 63.260, 64.088, 64.917, // 720-724 nm
     65.745, 66.573, 67.401, 68.229, 69.057, // 725-729 nm
     69.886, 70.406, 70.926, 71.446, 71.966, // 730-734 nm
     72.486, 73.006, 73.527, 74.047, 74.567, // 735-739 nm
     75.087, 73.938, 72.788, 71.639, 70.489, // 740-744 nm
     69.340, 68.190, 67.041, 65.892, 64.742, // 745-749 nm
     63.593, 61.875, 60.158, 58.440, 56.723, // 750-754 nm
     55.005, 53.288, 51.571, 49.853, 48.136, // 755-759 nm
     46.418, 48.457, 50.496, 52.534, 54.573, // 760-764 nm
     56.612, 58.651, 60.689, 62.728, 64.767, // 765-769 nm
     66.805, 66.463, 66.121, 65.779, 65.436, // 770-774 nm
     65.094, 64.752, 64.410, 64.067, 63.725, // 775-779 nm
     63.383, 63.475, 63.567, 63.659, 63.751, // 780-784 nm
     63.843, 63.935, 64.028, 64.120, 64.212, // 785-789 nm
     64.304, 63.819, 63.334, 62.848, 62.363, // 790-794 nm
     61.878, 61.393, 60.907, 60.422, 59.937, // 795-799 nm
     59.452, 58.703, 57.953, 57.204, 56.455, // 800-804 nm
     55.705, 54.956, 54.207, 53.458, 52.708, // 805-809 nm
     51.959, 52.507, 53.055, 53.603, 54.152, // 810-814 nm
     54.700, 55.248, 55.796, 56.344, 56.892, // 815-819 nm
     57.441, 57.728, 58.015, 58.302, 58.589, // 820-824 nm
     58.877, 59.164, 59.451, 59.738, 60.025, // 825-829 nm
     60.312, // 830 nm
};

float SampleIlluminantD65(float NormalizedLambda)
{
    float Offset = NormalizedLambda * 470;
    int Index = clamp(int(Offset), 0, 469);
    return mix(CIE_ILLUMINANT_D65[Index], CIE_ILLUMINANT_D65[Index+1], Offset - Index);
}

/* --- Parametric Spectrum Representation ---------------------------------- */

// Sample a parametric reflectance spectrum at a given wavelength.
float SampleParametricSpectrum(vec3 Beta, float Lambda)
{
    float X = (Beta.x * Lambda + Beta.y) * Lambda + Beta.z;
    return 0.5 + X / (2.0 * sqrt(1.0 + X * X));
}

// Sample a parametric reflectance spectrum at a given cluster of wavelengths.
vec4 SampleParametricSpectrum(vec3 Beta, vec4 Lambdas)
{
    vec4 X = (Beta.x * Lambdas + Beta.y) * Lambdas + Beta.z;
    return 0.5 + X / (2.0 * sqrt(1.0 + X * X));
}

// Sample a parametric spectrum at a given wavelength.
float SampleParametricSpectrum(vec4 BetaAndIntensity, float Lambda)
{
    return BetaAndIntensity.w * SampleParametricSpectrum(BetaAndIntensity.xyz, Lambda);
}

// Sample a parametric spectrum at a given cluster of wavelengths.
vec4 SampleParametricSpectrum(vec4 BetaAndIntensity, vec4 Lambdas)
{
    return BetaAndIntensity.w * SampleParametricSpectrum(BetaAndIntensity.xyz, Lambdas);
}

// Compute the XYZ tristimulus response of a parametric spectrum under
// illuminant D65. This is useful to retrieve the XYZ color corresponding
// to a parametric spectrum for simple surface color visualization.
vec3 ObserveParametricSpectrumUnderD65(vec4 BetaAndIntensity)
{
    const int SampleCount = 16;
    const float DeltaLambda = (CIE_LAMBDA_MAX - CIE_LAMBDA_MIN) / SampleCount;
    vec3 Color = vec3(0, 0, 0);
    for (int I = 0; I < SampleCount; I++)
    {
        float NormalizedLambda = I / float(SampleCount - 1);
        float D65 = SampleIlluminantD65(NormalizedLambda) / 10566.864005;
        float Lambda = mix(CIE_LAMBDA_MIN, CIE_LAMBDA_MAX, NormalizedLambda);
        Color += SampleParametricSpectrum(BetaAndIntensity, Lambda) * D65 * SampleStandardObserver(Lambda) * DeltaLambda;
    }
    return Color;
}

vec3 ObserveParametricSpectrumUnderD65(vec3 Beta)
{
    return ObserveParametricSpectrumUnderD65(vec4(Beta, 1));
}

#endif // SPECTRUM_GLSL_INC