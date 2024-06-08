#include "spectral.h"

// Spectrum of the CIE standard illuminant D65, 1 nm wavelength steps.
float const CIE_ILLUMINANT_D65[] =
{
     46.638f,  47.183f,  47.728f,  48.273f,  48.819f, // 360-364 nm
     49.364f,  49.909f,  50.454f,  50.999f,  51.544f, // 365-369 nm
     52.089f,  51.878f,  51.666f,  51.455f,  51.244f, // 370-374 nm
     51.032f,  50.821f,  50.610f,  50.398f,  50.187f, // 375-379 nm
     49.975f,  50.443f,  50.910f,  51.377f,  51.845f, // 380-384 nm
     52.312f,  52.779f,  53.246f,  53.714f,  54.181f, // 385-389 nm
     54.648f,  57.459f,  60.270f,  63.080f,  65.891f, // 390-394 nm
     68.701f,  71.512f,  74.323f,  77.134f,  79.944f, // 395-399 nm
     82.755f,  83.628f,  84.501f,  85.374f,  86.247f, // 400-404 nm
     87.120f,  87.994f,  88.867f,  89.740f,  90.613f, // 405-409 nm
     91.486f,  91.681f,  91.875f,  92.070f,  92.264f, // 410-414 nm
     92.459f,  92.653f,  92.848f,  93.043f,  93.237f, // 415-419 nm
     93.432f,  92.757f,  92.082f,  91.407f,  90.732f, // 420-424 nm
     90.057f,  89.382f,  88.707f,  88.032f,  87.357f, // 425-429 nm
     86.682f,  88.501f,  90.319f,  92.137f,  93.955f, // 430-434 nm
     95.774f,  97.592f,  99.410f, 101.228f, 103.047f, // 435-439 nm
    104.865f, 106.079f, 107.294f, 108.508f, 109.722f, // 440-444 nm
    110.936f, 112.151f, 113.365f, 114.579f, 115.794f, // 445-449 nm
    117.008f, 117.088f, 117.169f, 117.249f, 117.330f, // 450-454 nm
    117.410f, 117.490f, 117.571f, 117.651f, 117.732f, // 455-459 nm
    117.812f, 117.517f, 117.222f, 116.927f, 116.632f, // 460-464 nm
    116.336f, 116.041f, 115.746f, 115.451f, 115.156f, // 465-469 nm
    114.861f, 114.967f, 115.073f, 115.180f, 115.286f, // 470-474 nm
    115.392f, 115.498f, 115.604f, 115.711f, 115.817f, // 475-479 nm
    115.923f, 115.212f, 114.501f, 113.789f, 113.078f, // 480-484 nm
    112.367f, 111.656f, 110.945f, 110.233f, 109.522f, // 485-489 nm
    108.811f, 108.865f, 108.920f, 108.974f, 109.028f, // 490-494 nm
    109.082f, 109.137f, 109.191f, 109.245f, 109.300f, // 495-499 nm
    109.354f, 109.199f, 109.044f, 108.888f, 108.733f, // 500-504 nm
    108.578f, 108.423f, 108.268f, 108.112f, 107.957f, // 505-509 nm
    107.802f, 107.501f, 107.200f, 106.898f, 106.597f, // 510-514 nm
    106.296f, 105.995f, 105.694f, 105.392f, 105.091f, // 515-519 nm
    104.790f, 105.080f, 105.370f, 105.660f, 105.950f, // 520-524 nm
    106.239f, 106.529f, 106.819f, 107.109f, 107.399f, // 525-529 nm
    107.689f, 107.361f, 107.032f, 106.704f, 106.375f, // 530-534 nm
    106.047f, 105.719f, 105.390f, 105.062f, 104.733f, // 535-539 nm
    104.405f, 104.369f, 104.333f, 104.297f, 104.261f, // 540-544 nm
    104.225f, 104.190f, 104.154f, 104.118f, 104.082f, // 545-549 nm
    104.046f, 103.641f, 103.237f, 102.832f, 102.428f, // 550-554 nm
    102.023f, 101.618f, 101.214f, 100.809f, 100.405f, // 555-559 nm
    100.000f,  99.633f,  99.267f,  98.900f,  98.534f, // 560-564 nm
     98.167f,  97.800f,  97.434f,  97.067f,  96.701f, // 565-569 nm
     96.334f,  96.280f,  96.225f,  96.170f,  96.116f, // 570-574 nm
     96.061f,  96.007f,  95.952f,  95.897f,  95.843f, // 575-579 nm
     95.788f,  95.078f,  94.368f,  93.657f,  92.947f, // 580-584 nm
     92.237f,  91.527f,  90.816f,  90.106f,  89.396f, // 585-589 nm
     88.686f,  88.818f,  88.950f,  89.082f,  89.214f, // 590-594 nm
     89.346f,  89.478f,  89.610f,  89.742f,  89.874f, // 595-599 nm
     90.006f,  89.966f,  89.925f,  89.884f,  89.843f, // 600-604 nm
     89.803f,  89.762f,  89.721f,  89.680f,  89.640f, // 605-609 nm
     89.599f,  89.409f,  89.219f,  89.029f,  88.839f, // 610-614 nm
     88.649f,  88.459f,  88.269f,  88.079f,  87.889f, // 615-619 nm
     87.699f,  87.258f,  86.817f,  86.376f,  85.935f, // 620-624 nm
     85.494f,  85.053f,  84.612f,  84.171f,  83.730f, // 625-629 nm
     83.289f,  83.330f,  83.371f,  83.412f,  83.453f, // 630-634 nm
     83.494f,  83.535f,  83.576f,  83.617f,  83.658f, // 635-639 nm
     83.699f,  83.332f,  82.965f,  82.597f,  82.230f, // 640-644 nm
     81.863f,  81.496f,  81.129f,  80.761f,  80.394f, // 645-649 nm
     80.027f,  80.046f,  80.064f,  80.083f,  80.102f, // 650-654 nm
     80.121f,  80.139f,  80.158f,  80.177f,  80.196f, // 655-659 nm
     80.215f,  80.421f,  80.627f,  80.834f,  81.040f, // 660-664 nm
     81.246f,  81.453f,  81.659f,  81.865f,  82.072f, // 665-669 nm
     82.278f,  81.878f,  81.479f,  81.080f,  80.680f, // 670-674 nm
     80.281f,  79.882f,  79.482f,  79.083f,  78.684f, // 675-679 nm
     78.284f,  77.428f,  76.572f,  75.715f,  74.859f, // 680-684 nm
     74.003f,  73.147f,  72.290f,  71.434f,  70.578f, // 685-689 nm
     69.721f,  69.910f,  70.099f,  70.288f,  70.476f, // 690-694 nm
     70.665f,  70.854f,  71.043f,  71.231f,  71.420f, // 695-699 nm
     71.609f,  71.883f,  72.157f,  72.431f,  72.705f, // 700-704 nm
     72.979f,  73.253f,  73.527f,  73.801f,  74.075f, // 705-709 nm
     74.349f,  73.075f,  71.800f,  70.525f,  69.251f, // 710-714 nm
     67.977f,  66.702f,  65.427f,  64.153f,  62.879f, // 715-719 nm
     61.604f,  62.432f,  63.260f,  64.088f,  64.917f, // 720-724 nm
     65.745f,  66.573f,  67.401f,  68.229f,  69.057f, // 725-729 nm
     69.886f,  70.406f,  70.926f,  71.446f,  71.966f, // 730-734 nm
     72.486f,  73.006f,  73.527f,  74.047f,  74.567f, // 735-739 nm
     75.087f,  73.938f,  72.788f,  71.639f,  70.489f, // 740-744 nm
     69.340f,  68.190f,  67.041f,  65.892f,  64.742f, // 745-749 nm
     63.593f,  61.875f,  60.158f,  58.440f,  56.723f, // 750-754 nm
     55.005f,  53.288f,  51.571f,  49.853f,  48.136f, // 755-759 nm
     46.418f,  48.457f,  50.496f,  52.534f,  54.573f, // 760-764 nm
     56.612f,  58.651f,  60.689f,  62.728f,  64.767f, // 765-769 nm
     66.805f,  66.463f,  66.121f,  65.779f,  65.436f, // 770-774 nm
     65.094f,  64.752f,  64.410f,  64.067f,  63.725f, // 775-779 nm
     63.383f,  63.475f,  63.567f,  63.659f,  63.751f, // 780-784 nm
     63.843f,  63.935f,  64.028f,  64.120f,  64.212f, // 785-789 nm
     64.304f,  63.819f,  63.334f,  62.848f,  62.363f, // 790-794 nm
     61.878f,  61.393f,  60.907f,  60.422f,  59.937f, // 795-799 nm
     59.452f,  58.703f,  57.953f,  57.204f,  56.455f, // 800-804 nm
     55.705f,  54.956f,  54.207f,  53.458f,  52.708f, // 805-809 nm
     51.959f,  52.507f,  53.055f,  53.603f,  54.152f, // 810-814 nm
     54.700f,  55.248f,  55.796f,  56.344f,  56.892f, // 815-819 nm
     57.441f,  57.728f,  58.015f,  58.302f,  58.589f, // 820-824 nm
     58.877f,  59.164f,  59.451f,  59.738f,  60.025f, // 825-829 nm
     60.312f,                                         // 830 nm
};

// Return an interpolated sample of the spectrum of the CIE standard
// illuminant D65 at the given wavelength.  Takes a normalized wavelength
// in the range [0,1] corresponding to physical wavelengths in the range
// [CIE_LAMBDA_MIN, CIE_LAMBDA_MAX].
static double SampleD65(double NormalizedLambda)
{
    constexpr int N = static_cast<int>(std::size(CIE_ILLUMINANT_D65));
    double Offset = NormalizedLambda * (N - 1);
    int Index = glm::clamp(int(Offset), 0, N - 2);
    return glm::mix(CIE_ILLUMINANT_D65[Index], CIE_ILLUMINANT_D65[Index+1], Offset - Index);
}

// Compute the CIE XYZ tristimulus values of a single-wavelength Dirac spectrum
// using the multi-lobe piecewise Gaussian fit of the CIE 1931 standard observer
// presented in the paper "Simple Analytic Approximations to the CIE XYZ Color
// Matching Functions" by Chris Wyman et al.  Takes a normalized wavelength
// in the range [0,1] corresponding to physical wavelengths in the range
// [CIE_LAMBDA_MIN, CIE_LAMBDA_MAX].
static glm::dvec3 SampleObserver(double NormalizedLambda)
{
    float Lambda = glm::mix(CIE_LAMBDA_MIN, CIE_LAMBDA_MAX, NormalizedLambda);

    glm::vec3 Result;
    {
        float T1 = (Lambda - 442.0f) * (Lambda < 442.0f ? 0.0624f : 0.0374f);
        float T2 = (Lambda - 599.8f) * (Lambda < 599.8f ? 0.0264f : 0.0323f);
        float T3 = (Lambda - 501.1f) * (Lambda < 501.1f ? 0.0490f : 0.0382f);
        Result.x = 0.362f * exp(-0.5f * T1 * T1)
                 + 1.056f * exp(-0.5f * T2 * T2)
                 - 0.065f * exp(-0.5f * T3 * T3);
    }
    {
        float T1 = (Lambda - 568.8f) * (Lambda < 568.8f ? 0.0213f : 0.0247f);
        float T2 = (Lambda - 530.9f) * (Lambda < 530.9f ? 0.0613f : 0.0322f);
        Result.y = 0.821f * exp(-0.5f * T1 * T1)
                 + 0.286f * exp(-0.5f * T2 * T2);
    }
    {
        float T1 = (Lambda - 437.0f) * (Lambda < 437.0f ? 0.0845f : 0.0278f);
        float T2 = (Lambda - 459.0f) * (Lambda < 459.0f ? 0.0385f : 0.0725f);
        Result.z = 1.217f * exp(-0.5f * T1 * T1)
                 + 0.681f * exp(-0.5f * T2 * T2);
    }
    return Result;
}

// Sample a reflectance spectrum parameterized using the method outlined in the
// paper "A Low-Dimensional Function Space for Efficient Spectral Upsampling"
// by W. Jakob and J. Hanika.  Takes normalized spectrum coefficients and
// a wavelength in the range [0,1] corresponding to physical wavelengths in
// the range [CIE_LAMBDA_MIN, CIE_LAMBDA_MAX].
double SampleSpectrum(glm::dvec3 NormalizedBeta, double NormalizedLambda)
{
    double X = (NormalizedBeta.x * NormalizedLambda + NormalizedBeta.y) * NormalizedLambda + NormalizedBeta.z;
    return 0.5 + X / (2.0 * glm::sqrt(1.0 + X * X));
}

// Compute the CIE XYZ tristimulus response of a reflectance spectrum
// parameterized by coefficients Beta when lit by the CIE standard
// illuminant D65.  Takes normalized coefficients.
static glm::dvec3 ObserveSpectrumUnderD65(glm::dvec3 NormalizedBeta)
{
    int const SampleCount = 471;
    double const DeltaLambda = (CIE_LAMBDA_MAX - CIE_LAMBDA_MIN + 1) / SampleCount;

    glm::dvec3 XYZ = {};

    for (int I = 0; I < SampleCount; I++) {
        double NormalizedLambda = I / double(SampleCount - 1);
        double W = SampleD65(NormalizedLambda) / 10566.864005;
        double S = SampleSpectrum(NormalizedBeta, NormalizedLambda);
        XYZ += SampleObserver(NormalizedLambda) * W * S * DeltaLambda;
    }

    return XYZ;
}

// Convert from the CIEXYZ space to the CIELAB space.
// Distance in the CIELAB space is a useful heuristic
// for the perceptual difference between two colors.
static glm::dvec3 XYZToLab(glm::dvec3 XYZ)
{
    auto F = [](double T) -> double {
        double const Delta = 6/29.0;
        if (T > Delta * Delta * Delta)
            return glm::pow(T, 1/3.0);
        else
            return T / (3 * Delta * Delta) + 4/29.0;
    };

    double FX = F(XYZ.x / 0.950489);
    double FY = F(XYZ.y);
    double FZ = F(XYZ.z / 1.088840);

    double L = 116.0 * FX - 16.0;
    double A = 500.0 * (FX - FY);
    double B = 200.0 * (FY - FZ);

    return { L, A, B };
}

// Optimize the normalized coefficients of a parametric reflectance spectrum
// to match the given CIE XYZ tristimulus values when observed under the
// standard illuminant D65.  The residual is calculated in the CIELAB space
// for a good perceptual match.  The optimization procedure uses Gauss-Newton
// iteration with a numerically approximated residual Jacobian.
static glm::vec3 OptimizeSpectrum(
    glm::dvec3 NormalizedBeta,
    glm::dvec3 const& TargetXYZ,
    int IterationCount = 15)
{
    double const Epsilon = 1e-5;

    double Error = 0.0;

    for (int I = 0; I < IterationCount; I++) {
        // Compute the CIELAB difference between target XYZ and observed XYZ response of the spectrum.
        glm::dvec3 ObservedXYZ = ObserveSpectrumUnderD65(NormalizedBeta);
        glm::dvec3 Residual = XYZToLab(ObservedXYZ) - XYZToLab(TargetXYZ);

        Error = glm::length(Residual);
        if (Error < 1e-3) break;

        // Compute the Jacobian of the residual with respect to change in the coefficient.
        glm::dmat3 Jacobian = {};
        for (int I = 0; I < 3; I++) {
            glm::dvec3 Beta0 = NormalizedBeta; Beta0[I] -= Epsilon;
            glm::dvec3 XYZ0 = ObserveSpectrumUnderD65(Beta0);
            glm::dvec3 Lab0 = XYZToLab(XYZ0);

            glm::dvec3 Beta1 = NormalizedBeta; Beta1[I] += Epsilon;
            glm::dvec3 XYZ1 = ObserveSpectrumUnderD65(Beta1);
            glm::dvec3 Lab1 = XYZToLab(XYZ1);

            Jacobian[I] = (Lab1 - Lab0) / (2 * Epsilon);
        }

        if (glm::abs(glm::determinant(Jacobian)) < 1e-15) {
            // The Jacobian is degenerate, so we are probably
            // very close to a local optimum.  Stop iterating.
            printf("Degenerate Jacobian: TargetXYZ=(%.5f,%.5f,%.5f), Iteration=%d, Error=%e\n",
                TargetXYZ.x, TargetXYZ.y, TargetXYZ.z, I, Error);
            break;
        }

        NormalizedBeta -= glm::inverse(Jacobian) * Residual;

        double Max = glm::max(glm::max(NormalizedBeta.x, NormalizedBeta.y), NormalizedBeta.z);
        if (Max > 200.0) {
            NormalizedBeta *= 200.0 / Max;
        }
    }

    // Report a poor fit.  A CIELAB distance of 2.3 is a "just noticeable difference".
    if (Error > 2) {
        glm::dvec3 FitXYZ = ObserveSpectrumUnderD65(NormalizedBeta);
        printf("Poor fit: TargetXYZ=(%.5f,%.5f,%.5f), FitXYZ=(%.5f,%.5f,%.5f), Beta=(%.3f,%.3f,%.3f), Error=%e\n",
            TargetXYZ.x, TargetXYZ.y, TargetXYZ.z,
            FitXYZ.x, FitXYZ.y, FitXYZ.z,
            NormalizedBeta.x, NormalizedBeta.y, NormalizedBeta.z,
            Error);
    }

    return NormalizedBeta;
}

static float IndexToScale(int K)
{
    constexpr int M = parametric_spectrum_table::SCALE_BINS;
    float R = K / float(M - 1);
    float S = R * R * (3.f - 2.f * R);
    float T = S * S * (3.f - 2.f * S);
    return T;
}

static int ScaleToIndex(float Scale)
{
    constexpr int M = parametric_spectrum_table::SCALE_BINS;
    int K0 = 0, K1 = M;
    while (K1 - K0 > 1) {
        int K = (K0 + K1) / 2;
        (Scale > IndexToScale(K) ? K0 : K1) = K;
    }
    return K0;
}

static glm::vec3 IndexToColor(int I, int J, int K, int L)
{
    constexpr int N = parametric_spectrum_table::COLOR_BINS;
    glm::vec3 Color;
    Color[L] = 1.0f;
    Color[(L + 1) % 3] = I / float(N - 1);
    Color[(L + 2) % 3] = J / float(N - 1);
    return Color * IndexToScale(K);
}

static std::pair<glm::ivec4, glm::vec3> ColorToIndex(glm::vec3 const& Color)
{
    constexpr int N = parametric_spectrum_table::COLOR_BINS;
    constexpr int M = parametric_spectrum_table::SCALE_BINS;

    int L = 0;
    for (int I = 1; I < 3; I++)
        if (Color[I] >= Color[L])
            L = I;

    float Scale = glm::max(Color[L], 1e-6f);

    float X = (N - 1) * Color[(L + 1) % 3] / Scale;
    float Y = (N - 1) * Color[(L + 2) % 3] / Scale;

    int I = std::min(int(X), N - 2);
    int J = std::min(int(Y), N - 2);
    int K = std::min(ScaleToIndex(Scale), M - 2);

    glm::ivec4 Index = { I, J, K, L };

    float S0 = IndexToScale(K);
    float S1 = IndexToScale(K+1);

    glm::vec3 Alpha = { X - I, Y - J, (Scale - S0) / (S1 - S0) };

    return { Index, Alpha };
}

void BuildParametricSpectrumTableForSRGB(
    parametric_spectrum_table* Table)
{
    constexpr int N = parametric_spectrum_table::COLOR_BINS;
    constexpr int M = parametric_spectrum_table::SCALE_BINS;

    auto DenormalizeBeta = [](glm::dvec3 const& NormalizedBeta) -> glm::vec3 {
        constexpr float C0 = CIE_LAMBDA_MIN;
        constexpr float C1 = 1.f / (CIE_LAMBDA_MAX - CIE_LAMBDA_MIN);
        auto B = NormalizedBeta;
        return {
            B[0] * C1 * C1,
            B[1] * C1 - 2 * B[0] * C0 * C1 * C1,
            B[2] - B[1] * C0 * C1 + B[0] * C0 * C0 * C1 * C1
        };
    };

    glm::dvec3 NormalizedBeta = {};

    for (int L = 0; L < 3; L++) {
        for (int J = 0; J < N; J++) {
            for (int I = 0; I < N; I++) {
                // Light colors.
                NormalizedBeta = {};
                for (int K = M/5; K < M; K++) {
                    auto TargetXYZ = SRGBToXYZ(IndexToColor(I, J, K, L));
                    NormalizedBeta = OptimizeSpectrum(NormalizedBeta, TargetXYZ, 15);
                    Table->Coefficients[L][K][J][I] = DenormalizeBeta(NormalizedBeta);
                }

                // Dark colors.
                NormalizedBeta = {};
                for (int K = M/5; K >= 0; K--) {
                    auto TargetXYZ = SRGBToXYZ(IndexToColor(I, J, K, L));
                    NormalizedBeta = OptimizeSpectrum(NormalizedBeta, TargetXYZ, 15);
                    Table->Coefficients[L][K][J][I] = DenormalizeBeta(NormalizedBeta);
                }
            }
        }
    }
}

bool SaveParametricSpectrumTable(
    parametric_spectrum_table const* Table,
    char const* Path)
{
    FILE* File = nullptr;
    fopen_s(&File, Path, "wb");
    if (File) {
        fwrite(Table->Coefficients, sizeof(Table->Coefficients), 1, File);
        fclose(File);
        return true;
    }
    return false;
}

bool LoadParametricSpectrumTable(
    parametric_spectrum_table* Table,
    char const* Path)
{
    FILE* File = nullptr;
    fopen_s(&File, Path, "rb");
    if (File) {
        fread(Table->Coefficients, sizeof(Table->Coefficients), 1, File);
        fclose(File);
        return true;
    }
    return false;
}

glm::vec3 GetParametricSpectrumCoefficients(
    parametric_spectrum_table const* Table,
    glm::vec3 const& InColor)
{
    glm::vec3 Color = glm::clamp(InColor, glm::vec3(0), glm::vec3(1));

    glm::ivec4 Index;
    glm::vec3 Alpha;
    std::tie(Index, Alpha) = ColorToIndex(Color);

    glm::vec3 Beta00 = glm::mix(
        Table->Coefficients[Index.w][Index.z+0][Index.y+0][Index.x+0],
        Table->Coefficients[Index.w][Index.z+0][Index.y+0][Index.x+1],
        Alpha.x);

    glm::vec3 Beta01 = glm::mix(
        Table->Coefficients[Index.w][Index.z+0][Index.y+1][Index.x+0],
        Table->Coefficients[Index.w][Index.z+0][Index.y+1][Index.x+1],
        Alpha.x);

    glm::vec3 Beta10 = glm::mix(
        Table->Coefficients[Index.w][Index.z+1][Index.y+0][Index.x+0],
        Table->Coefficients[Index.w][Index.z+1][Index.y+0][Index.x+1],
        Alpha.x);

    glm::vec3 Beta11 = glm::mix(
        Table->Coefficients[Index.w][Index.z+1][Index.y+1][Index.x+0],
        Table->Coefficients[Index.w][Index.z+1][Index.y+1][Index.x+1],
        Alpha.x);

    glm::vec3 Beta0 = glm::mix(Beta00, Beta01, Alpha.y);
    glm::vec3 Beta1 = glm::mix(Beta10, Beta11, Alpha.y);

    return glm::mix(Beta0, Beta1, Alpha.z);
}

float SampleParametricSpectrum(
    glm::vec3 const& Beta,
    float Lambda)
{
    float X = (Beta.x * Lambda + Beta.y) * Lambda + Beta.z;
    return 0.5f + X / (2.0f * glm::sqrt(1.0f + X * X));
}
