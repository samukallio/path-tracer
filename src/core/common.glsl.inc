#ifndef COMMON_GLSL_INC
#define COMMON_GLSL_INC

const float INFINITY        = 1e30f;
const float EPSILON         = 1e-9f;
const float PI              = 3.141592653f;
const float TAU             = 6.283185306f;

const float HIT_TIME_LIMIT  = 1048576.0;

const float CIE_LAMBDA_MIN  = 360.0;
const float CIE_LAMBDA_MAX  = 830.0;

const uint SHAPE_INDEX_NONE                 = 0xFFFFFFFF;
const uint TEXTURE_INDEX_NONE               = 0xFFFFFFFF;

const uint RENDER_MODE_PATH_TRACE           = 0;
const uint RENDER_MODE_BASE_COLOR           = 1;
const uint RENDER_MODE_BASE_COLOR_SHADED    = 2;
const uint RENDER_MODE_NORMAL               = 3;
const uint RENDER_MODE_MATERIAL_INDEX       = 4;
const uint RENDER_MODE_PRIMITIVE_INDEX      = 5;
const uint RENDER_MODE_MESH_COMPLEXITY      = 6;
const uint RENDER_MODE_SCENE_COMPLEXITY     = 7;

const uint RENDER_FLAG_ACCUMULATE           = 1 << 0;
const uint RENDER_FLAG_SAMPLE_JITTER        = 1 << 1;

const uint TONE_MAPPING_MODE_CLAMP          = 0;
const uint TONE_MAPPING_MODE_REINHARD       = 1;
const uint TONE_MAPPING_MODE_HABLE          = 2;
const uint TONE_MAPPING_MODE_ACES           = 3;

struct packed_transform
{
    mat4                To;
    mat4                From;
};

struct ray
{
    vec3                Origin;
    vec3                Velocity;
    float               Duration;
};

// Volumetric material properties of a participating medium.
struct medium
{
    uint                Priority;                   // Priority of the medium when multiple media overlap.
    vec4                IOR;                        // Absolute index of refraction at each of the 4 sampling wavelengths.
    vec4                AbsorptionRate;             // Absorption rate at each of the 4 sampling wavelengths.
    vec4                ScatteringRate;             // Scattering rate at each of the 4 sampling wavelengths.
    float               ScatteringAnisotropy;       // Scattering phase function anisotropy.
};

struct path
{
    float               NormalizedLambda0;
    vec4                Throughput;                 // Path throughput at each of the 4 sampling wavelengths.
    vec4                Weight;                     // Path weight at each of the 4 sampling wavelengths.
    vec3                Sample;
    ivec2               ImagePosition;
    uint                ActiveShapeIndex[4];
};

vec3 TransformPosition(vec3 P, packed_transform Transform)
{
    return (Transform.To * vec4(P, 1)).xyz;
}

vec3 TransformVector(vec3 V, packed_transform Transform)
{
    return (Transform.To * vec4(V, 0)).xyz;
}

vec3 TransformNormal(vec3 N, packed_transform Transform)
{
    return normalize((vec4(N, 0) * Transform.From).xyz);
}

vec3 TransformDirection(vec3 D, packed_transform Transform)
{
    return normalize(TransformVector(D, Transform));
}

ray TransformRay(ray R, packed_transform Transform)
{
    ray OutR;
    OutR.Origin = TransformPosition(R.Origin, Transform);
    OutR.Velocity = TransformVector(R.Velocity, Transform);
    OutR.Duration = R.Duration;
    return OutR;
}

vec3 InverseTransformPosition(vec3 P, packed_transform Transform)
{
    return (Transform.From * vec4(P, 1)).xyz;
}

vec3 InverseTransformVector(vec3 V, packed_transform Transform)
{
    return (Transform.From * vec4(V, 0)).xyz;
}

vec3 InverseTransformNormal(vec3 N, packed_transform Transform)
{
    return normalize((vec4(N, 0) * Transform.To).xyz);
}

ray InverseTransformRay(ray R, packed_transform Transform)
{
    ray OutR;
    OutR.Origin = InverseTransformPosition(R.Origin, Transform);
    OutR.Velocity = InverseTransformVector(R.Velocity, Transform);
    OutR.Duration = R.Duration;
    return OutR;
}

vec3 SafeNormalize(vec3 V)
{
    float LenSq = dot(V, V);
    if (LenSq < 1e-12)
        return vec3(0, 0, 1);
    else
        return V / sqrt(LenSq);
}

float max4(vec4 V)
{
    return max(max(V.x, V.y), max(V.z, V.w));
}

// Compute an arbitrary tangent vector for a given normal.
vec3 ComputeTangentVector(vec3 Normal)
{
    vec3 V = abs(Normal.x) < 0.9 ? vec3(1, 0, 0) : vec3(0, 1, 0);
    return normalize(cross(V, Normal));
}

// Compute an arbitrary 3D coordinate frame given one vector.
void ComputeCoordinateFrame(vec3 Z, out vec3 X, out vec3 Y)
{
    vec3 V = abs(Z.x) < 0.9 ? vec3(1, 0, 0) : vec3(0, 1, 0);
    X = normalize(cross(V, Z));
    Y = cross(X, Z);
}

vec2 SignNotZero(vec2 V)
{
    return vec2(
        V.x >= 0.0 ? +1.0 : -1.0,
        V.y >= 0.0 ? +1.0 : -1.0);
}

// Packs a unit vector into a single 32-bit value.
uint PackUnitVector(vec3 V)
{
    vec2 P = V.xy * (1.0 / (abs(V.x) + abs(V.y) + abs(V.z)));
    if (V.z <= 0.0) P = (1.0 - abs(P.yx)) * SignNotZero(P);
    return packSnorm2x16(P);
}

// Unpacks a unit vector from a single 32-bit value.
vec3 UnpackUnitVector(uint PackedV)
{
    vec2 P = unpackSnorm2x16(PackedV);
    float Z = 1.0 - abs(P.x) - abs(P.y);
    if (Z < 0.0) P = (1.0 - abs(P.yx)) * SignNotZero(P);
    return normalize(vec3(P, Z));
}

float IntersectBoundingBox(ray Ray, float Reach, vec3 Min, vec3 Max)
{
    // Compute ray time to the axis-aligned planes at the node bounding
    // box minimum and maximum corners.
    vec3 MinT = (Min - Ray.Origin) / Ray.Velocity;
    vec3 MaxT = (Max - Ray.Origin) / Ray.Velocity;

    // For each coordinate axis, sort out which of the two coordinate
    // planes (at bounding box min/max points) comes earlier in time and
    // which one comes later.
    vec3 EarlierT = min(MinT, MaxT);
    vec3 LaterT = max(MinT, MaxT);

    // Compute the ray entry and exit times.  The ray enters the box when
    // it has crossed all of the entry planes, so we take the maximum.
    // Likewise, the ray has exit the box when it has exit at least one
    // of the exit planes, so we take the minimum.
    float EntryT = max(max(EarlierT.x, EarlierT.y), EarlierT.z);
    float ExitT = min(min(LaterT.x, LaterT.y), LaterT.z);

    // If the exit time is greater than the entry time, then the ray has
    // missed the box altogether.
    if (ExitT < EntryT) return INFINITY;

    // If the exit time is less than 0, then the box is behind the eye.
    if (ExitT <= 0) return INFINITY;

    // If the entry time is greater than previous hit time, then the box
    // is occluded.
    if (EntryT >= Reach) return INFINITY;

    return EntryT;
}

/* --- Random Number Generator --------------------------------------------- */

uint RandomState;

uint Random()
{
    RandomState = RandomState * 747796405u + 2891336453u;
    uint s = RandomState;
    uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}

// Generate a random number in the range [0,1).
float Random0To1()
{
    return Random() / 4294967296.0f;
}

vec2 RandomPointOnDisk()
{
    float R = sqrt(Random0To1());
    float Theta = Random0To1() * TAU;
    return R * vec2(cos(Theta), sin(Theta));
}

vec3 RandomDirection()
{
    float Z = 2 * Random0To1() - 1;
    float R = sqrt(1 - Z * Z);
    float Phi = TAU * Random0To1();
    return vec3(R * cos(Phi), R * sin(Phi), Z);
}

vec3 RandomHemisphereDirection(vec3 Normal)
{
    vec3 Direction = RandomDirection();
    return Direction * sign(dot(Normal, Direction));
}

// Generate a random direction from a von Mises-Fisher distribution on
// a sphere, with concentration parameter Kappa and mean direction (0,0,1).
vec3 RandomVonMisesFisher(float Kappa)
{
    float Xi = Random0To1();
    float Z = 1 + (1 / Kappa) * log(Xi - exp(-2 * Kappa) * (Xi - 1));

    float R = sqrt(1 - Z * Z);
    float Phi = Random0To1() * TAU;
    float X = R * cos(Phi);
    float Y = R * sin(Phi);

    return vec3(X, Y, Z);
}

/* --- Camera -------------------------------------------------------------- */

const uint CAMERA_MODEL_PINHOLE             = 0;
const uint CAMERA_MODEL_THIN_LENS           = 1;
const uint CAMERA_MODEL_360                 = 2;

struct camera
{
    uint                Model;
    float               FocalLength;
    float               ApertureRadius;
    float               SensorDistance;
    vec2                SensorSize;
    packed_transform    Transform;
};

ray GenerateCameraRay(camera Camera, vec2 NormalizedSamplePosition)
{
    ray Ray;

    Ray.Duration = HIT_TIME_LIMIT;

    if (Camera.Model == CAMERA_MODEL_PINHOLE) {
        vec3 SensorPosition = vec3(
            -Camera.SensorSize.x * (NormalizedSamplePosition.x - 0.5),
            -Camera.SensorSize.y * (0.5 - NormalizedSamplePosition.y),
            Camera.SensorDistance);

        Ray.Origin = vec3(Camera.ApertureRadius * RandomPointOnDisk(), 0);
        Ray.Velocity = normalize(Ray.Origin - SensorPosition);
    }

    else if (Camera.Model == CAMERA_MODEL_THIN_LENS) {
        vec3 SensorPosition = vec3(
            -Camera.SensorSize.x * (NormalizedSamplePosition.x - 0.5),
            -Camera.SensorSize.y * (0.5 - NormalizedSamplePosition.y),
            Camera.SensorDistance);

        vec3 ObjectPosition = -SensorPosition * Camera.FocalLength / (SensorPosition.z - Camera.FocalLength);

        Ray.Origin = vec3(Camera.ApertureRadius * RandomPointOnDisk(), 0);
        Ray.Velocity = normalize(ObjectPosition - Ray.Origin);
    }

    else if (Camera.Model == CAMERA_MODEL_360) {
        float Phi = (NormalizedSamplePosition.x - 0.5f) * TAU;
        float Theta = (0.5f - NormalizedSamplePosition.y) * PI;

        Ray.Origin = vec3(0, 0, 0);
        Ray.Velocity = vec3(cos(Theta) * sin(Phi), sin(Theta), -cos(Theta) * cos(Phi));
    }

    return TransformRay(Ray, Camera.Transform);
}

/* --- Refraction Utilities ------------------------------------------------ */

// Computes a wavelength-dependent refractive index according to
// the Cauchy empirical formula.
vec4 CauchyEmpiricalIOR(float BaseIOR, float AbbeNumber, vec4 Lambda)
{
    // Fraunhofer C, d, and F spectral lines.
    const float LC = 656.3;
    const float Ld = 587.6;
    const float LF = 486.1;

    float B = (BaseIOR - 1) / (AbbeNumber * (1.0 / (LF * LF) - 1.0 / (LC * LC)));
    float A = BaseIOR - B / (Ld * Ld);

    return A + B / (Lambda * Lambda);
}

/* --- Henyey-Greenstein Phase Function ------------------------------------ */

// Sample a direction from the Henyey-Greenstein phase function.
vec3 SampleDirectionHG(float Anisotropy, float U1, float U2)
{
    float Z;
    if (abs(Anisotropy) < 1e-3) {
        // The distribution becomes spherical as the anisotropy approaches zero.
        Z = 1 - 2 * U1;
    }
    else {
        float G = Anisotropy;
        float S = (1 - G * G) / (1 + G - 2 * G * U1);
        Z = -(1 + G * G - S * S) / (2 * G);
    }
    float R = sqrt(1 - Z * Z);
    float Phi = U2 * TAU;
    return vec3(R * cos(Phi), R * sin(Phi), Z);
}

/* --- GGX Microfacet Model with Anisotropic Roughness --------------------- */

// Compute the 2D roughness alpha parameter for the GGX distribution of normals.
vec2 GGXRoughnessAlpha(float Roughness, float Anisotropy)
{
    float R = Roughness;
    float S = 1 - Anisotropy;
    float AlphaX = R * R * sqrt(2 / (1 + S * S));
    float AlphaY = S * AlphaX;
    return vec2(AlphaX, AlphaY);
}

// One half of the uncorrelated Smith shadowing-masking function for the
// anisotropic GGX distribution of normals as presented in the paper
// "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"
// by Eric Heitz.
float GGXSmithG1(vec3 Direction, vec2 RoughnessAlpha)
{
    vec3 DirectionSq = Direction * Direction;
    if (DirectionSq.z < EPSILON) return 0.0;
    vec2 RoughnessAlphaSq = RoughnessAlpha * RoughnessAlpha;
    float AlphaSqByTanThetaSq = dot(RoughnessAlphaSq, DirectionSq.xy) / DirectionSq.z;
    return 2.0 / (1.0 + sqrt(1.0 + AlphaSqByTanThetaSq));
}

// Sample a normal from the GGX distribution that is visible from the given
// direction according to the procedure outlined in the paper "Sampling the
// GGX Distribution of Visible Normals" by Eric Heitz.
vec3 GGXVisibleNormal(vec3 Direction, vec2 RoughnessAlpha, float U1, float U2)
{
    // Transform direction to the hemisphere space.
    vec3 Vz = SafeNormalize(vec3(
        RoughnessAlpha.x * Direction.x,
        RoughnessAlpha.y * Direction.y,
        Direction.z));

    // Compute orthogonal frame in hemisphere space.
    float LengthSq = dot(Vz.xy, Vz.xy);
    vec3 Vx = LengthSq > 0 ? vec3(-Vz.y, Vz.x, 0) / sqrt(LengthSq) : vec3(1, 0, 0);
    vec3 Vy = cross(Vz, Vx);

    // Uniformly sample the projected area of the hemisphere.
    float R = sqrt(U1);
    float Phi = TAU * U2;
    float S = 0.5 * (1.0 + Vz.z);

    float Tx = R * cos(Phi);
    float Ty = (1.0 - S) * sqrt(1.0 - Tx * Tx) + S * R * sin(Phi);
    float Tz = sqrt(max(0.0, 1.0 - Tx * Tx - Ty * Ty));

    // Determine normal corresponding to the sampled projected point.
    vec3 Normal = Tx * Vx + Ty * Vy + Tz * Vz;

    // Transform normal to the ellipsoid space.
    return SafeNormalize(vec3(
        RoughnessAlpha.x * Normal.x,
        RoughnessAlpha.y * Normal.y,
        max(0.0, Normal.z)));
}

// This is the anisotropic GGX distribution of normals D(m).
float GGXDistribution(vec3 Normal, vec2 RoughnessAlpha)
{
    vec2 A = 1.0 / RoughnessAlpha;
    float B = dot(Normal * Normal, vec3(A * A, 1.0));
    return 1.0 / (PI * RoughnessAlpha.x * RoughnessAlpha.y * B * B);
}

/* --- Fresnel ------------------------------------------------------------- */

// Compute surface reflectance using the Fresnel equations,
// assuming equal proportions of s and p polarized light.
float FresnelDielectric(float CosThetaIn, float CosThetaOut, float RelativeIOR)
{
    float A = CosThetaIn;
    float Bs = CosThetaOut * RelativeIOR;
    float Bp = CosThetaOut / RelativeIOR;
    float Rs = (A + Bs) / (A - Bs);
    float Rp = (A + Bp) / (A - Bp);
    return 0.5 * (Rs * Rs + Rp * Rp);
}

// Like above, but computes the Fresnel coefficient for 4 IOR values at the same time.
vec4 FresnelDielectric(float CosThetaIn, float CosThetaOut, vec4 RelativeIOR)
{
    float A = CosThetaIn;
    vec4 Bs = CosThetaOut * RelativeIOR;
    vec4 Bp = CosThetaOut / RelativeIOR;
    vec4 Rs = (A + Bs) / (A - Bs);
    vec4 Rp = (A + Bp) / (A - Bp);
    return 0.5 * (Rs * Rs + Rp * Rp);
}

// Computes the Fresnel term for a metallic surface using the "F82-tint"
// formulation presented in the paper "Novel aspects of the Adobe Standard
// Material" by Kutz et al, adapted for spectral rendering.
vec4 SchlickFresnelMetal(vec4 Base, vec4 Specular, float CosTheta)
{
    const float CosThetaMax = 1 / 7.0f;

    vec4 FSchlick = Base + (1 - Base) * pow(1 - CosTheta, 5);
    vec4 FSchlickMax = Base + (1 - Base) * pow(1 - CosThetaMax, 5);
    vec4 FMax = Specular * FSchlickMax;

    const float Denominator = CosThetaMax * pow(1 - CosThetaMax, 6);
    float Nominator = CosTheta * pow(1 - CosTheta, 6);
    return FSchlick - (Nominator / Denominator) * (FSchlickMax - FMax);
}

/* --- Spectrum Utilities -------------------------------------------------- */

// Compute the CIE XYZ tristimulus values of a single-wavelength Dirac spectrum
// using the multi-lobe piecewise Gaussian fit of the CIE 1931 standard observer
// presented in the paper "Simple Analytic Approximations to the CIE XYZ Color
// Matching Functions" by Chris Wyman et al.
vec3 SampleStandardObserverXYZ(float Lambda)
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

mat4x3 SampleStandardObserverXYZ(vec4 Lambda)
{
    return mat4x3(
        SampleStandardObserverXYZ(Lambda.x),
        SampleStandardObserverXYZ(Lambda.y),
        SampleStandardObserverXYZ(Lambda.z),
        SampleStandardObserverXYZ(Lambda.w)
    );
}

// Conversion from CIE XYZ to (linear) sRGB tristimulus values.
const mat3 CIE_XYZ_TO_SRGB = mat3(
    +3.2406, -0.9689, +0.0557,
    -1.5372, +1.8758, -0.2040,
    -0.4986, +0.0415, +1.0570
);

// Like StandardObserverXYZ(), but gives linear sRGB values.
vec3 SampleStandardObserverSRGB(float Lambda)
{
    return CIE_XYZ_TO_SRGB * SampleStandardObserverXYZ(Lambda);
}

mat4x3 SampleStandardObserverSRGB(vec4 Lambda)
{
    return CIE_XYZ_TO_SRGB * SampleStandardObserverXYZ(Lambda);
}

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

// Compute the SRGB tristimulus response of a parametric spectrum under
// an illuminant with a constant power spectrum.  This is useful to retrieve
// the SRGB color corresponding to a parametric spectrum for simple surface
// color visualization.
vec3 ObserveParametricSpectrumSRGB(vec4 BetaAndIntensity)
{
    const int SampleCount = 16;
    const float DeltaLambda = (CIE_LAMBDA_MAX - CIE_LAMBDA_MIN) / SampleCount;
    vec3 Color = vec3(0, 0, 0);
    for (int I = 0; I < SampleCount; I++) {
        float Lambda = mix(CIE_LAMBDA_MIN, CIE_LAMBDA_MAX, I / float(SampleCount - 1));
        Color += SampleParametricSpectrum(BetaAndIntensity, Lambda) * SampleStandardObserverSRGB(Lambda) * DeltaLambda;
    }
    return Color / 50.0;
}

vec3 ObserveParametricSpectrumSRGB(vec3 Beta)
{
    return ObserveParametricSpectrumSRGB(vec4(Beta, 1));
}

#endif // COMMON_GLSL_INC
