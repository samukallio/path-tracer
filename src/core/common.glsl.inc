#ifndef COMMON_GLSL_INC
#define COMMON_GLSL_INC

const float INFINITY                    = 1e30f;
const float EPSILON                     = 1e-9f;
const float PI                          = 3.141592653f;
const float TAU                         = 6.283185306f;

const float HIT_TIME_LIMIT              = 1048576.0;

const float CIE_LAMBDA_MIN              = 360.0;
const float CIE_LAMBDA_MAX              = 830.0;

const uint RENDER_FLAG_ACCUMULATE       = 1 << 0;
const uint RENDER_FLAG_SAMPLE_JITTER    = 1 << 1;

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
    return mat4x3(
        SampleStandardObserver(Lambda.x),
        SampleStandardObserver(Lambda.y),
        SampleStandardObserver(Lambda.z),
        SampleStandardObserver(Lambda.w)
    );
}

// Conversion from CIE XYZ to (linear) sRGB tristimulus values.
const mat3 CIE_XYZ_TO_SRGB = mat3(
    +3.2406, -0.9689, +0.0557,
    -1.5372, +1.8758, -0.2040,
    -0.4986, +0.0415, +1.0570
);

/* --- CIE Illuminant D65 -------------------------------------------------- */

// Spectrum of the CIE standard illuminant D65, 1 nm wavelength steps.
const float CIE_ILLUMINANT_D65[] =
{
     46.638,  47.183,  47.728,  48.273,  48.819, // 360-364 nm
     49.364,  49.909,  50.454,  50.999,  51.544, // 365-369 nm
     52.089,  51.878,  51.666,  51.455,  51.244, // 370-374 nm
     51.032,  50.821,  50.610,  50.398,  50.187, // 375-379 nm
     49.975,  50.443,  50.910,  51.377,  51.845, // 380-384 nm
     52.312,  52.779,  53.246,  53.714,  54.181, // 385-389 nm
     54.648,  57.459,  60.270,  63.080,  65.891, // 390-394 nm
     68.701,  71.512,  74.323,  77.134,  79.944, // 395-399 nm
     82.755,  83.628,  84.501,  85.374,  86.247, // 400-404 nm
     87.120,  87.994,  88.867,  89.740,  90.613, // 405-409 nm
     91.486,  91.681,  91.875,  92.070,  92.264, // 410-414 nm
     92.459,  92.653,  92.848,  93.043,  93.237, // 415-419 nm
     93.432,  92.757,  92.082,  91.407,  90.732, // 420-424 nm
     90.057,  89.382,  88.707,  88.032,  87.357, // 425-429 nm
     86.682,  88.501,  90.319,  92.137,  93.955, // 430-434 nm
     95.774,  97.592,  99.410, 101.228, 103.047, // 435-439 nm
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
    100.000,  99.633,  99.267,  98.900,  98.534, // 560-564 nm
     98.167,  97.800,  97.434,  97.067,  96.701, // 565-569 nm
     96.334,  96.280,  96.225,  96.170,  96.116, // 570-574 nm
     96.061,  96.007,  95.952,  95.897,  95.843, // 575-579 nm
     95.788,  95.078,  94.368,  93.657,  92.947, // 580-584 nm
     92.237,  91.527,  90.816,  90.106,  89.396, // 585-589 nm
     88.686,  88.818,  88.950,  89.082,  89.214, // 590-594 nm
     89.346,  89.478,  89.610,  89.742,  89.874, // 595-599 nm
     90.006,  89.966,  89.925,  89.884,  89.843, // 600-604 nm
     89.803,  89.762,  89.721,  89.680,  89.640, // 605-609 nm
     89.599,  89.409,  89.219,  89.029,  88.839, // 610-614 nm
     88.649,  88.459,  88.269,  88.079,  87.889, // 615-619 nm
     87.699,  87.258,  86.817,  86.376,  85.935, // 620-624 nm
     85.494,  85.053,  84.612,  84.171,  83.730, // 625-629 nm
     83.289,  83.330,  83.371,  83.412,  83.453, // 630-634 nm
     83.494,  83.535,  83.576,  83.617,  83.658, // 635-639 nm
     83.699,  83.332,  82.965,  82.597,  82.230, // 640-644 nm
     81.863,  81.496,  81.129,  80.761,  80.394, // 645-649 nm
     80.027,  80.046,  80.064,  80.083,  80.102, // 650-654 nm
     80.121,  80.139,  80.158,  80.177,  80.196, // 655-659 nm
     80.215,  80.421,  80.627,  80.834,  81.040, // 660-664 nm
     81.246,  81.453,  81.659,  81.865,  82.072, // 665-669 nm
     82.278,  81.878,  81.479,  81.080,  80.680, // 670-674 nm
     80.281,  79.882,  79.482,  79.083,  78.684, // 675-679 nm
     78.284,  77.428,  76.572,  75.715,  74.859, // 680-684 nm
     74.003,  73.147,  72.290,  71.434,  70.578, // 685-689 nm
     69.721,  69.910,  70.099,  70.288,  70.476, // 690-694 nm
     70.665,  70.854,  71.043,  71.231,  71.420, // 695-699 nm
     71.609,  71.883,  72.157,  72.431,  72.705, // 700-704 nm
     72.979,  73.253,  73.527,  73.801,  74.075, // 705-709 nm
     74.349,  73.075,  71.800,  70.525,  69.251, // 710-714 nm
     67.977,  66.702,  65.427,  64.153,  62.879, // 715-719 nm
     61.604,  62.432,  63.260,  64.088,  64.917, // 720-724 nm
     65.745,  66.573,  67.401,  68.229,  69.057, // 725-729 nm
     69.886,  70.406,  70.926,  71.446,  71.966, // 730-734 nm
     72.486,  73.006,  73.527,  74.047,  74.567, // 735-739 nm
     75.087,  73.938,  72.788,  71.639,  70.489, // 740-744 nm
     69.340,  68.190,  67.041,  65.892,  64.742, // 745-749 nm
     63.593,  61.875,  60.158,  58.440,  56.723, // 750-754 nm
     55.005,  53.288,  51.571,  49.853,  48.136, // 755-759 nm
     46.418,  48.457,  50.496,  52.534,  54.573, // 760-764 nm
     56.612,  58.651,  60.689,  62.728,  64.767, // 765-769 nm
     66.805,  66.463,  66.121,  65.779,  65.436, // 770-774 nm
     65.094,  64.752,  64.410,  64.067,  63.725, // 775-779 nm
     63.383,  63.475,  63.567,  63.659,  63.751, // 780-784 nm
     63.843,  63.935,  64.028,  64.120,  64.212, // 785-789 nm
     64.304,  63.819,  63.334,  62.848,  62.363, // 790-794 nm
     61.878,  61.393,  60.907,  60.422,  59.937, // 795-799 nm
     59.452,  58.703,  57.953,  57.204,  56.455, // 800-804 nm
     55.705,  54.956,  54.207,  53.458,  52.708, // 805-809 nm
     51.959,  52.507,  53.055,  53.603,  54.152, // 810-814 nm
     54.700,  55.248,  55.796,  56.344,  56.892, // 815-819 nm
     57.441,  57.728,  58.015,  58.302,  58.589, // 820-824 nm
     58.877,  59.164,  59.451,  59.738,  60.025, // 825-829 nm
     60.312,                                     // 830 nm
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

/* --- Parametric Spectrum Representation ---------------------------------- */

// Compute the SRGB tristimulus response of a parametric spectrum under
// illuminant D65.  This is useful to retrieve the SRGB color corresponding
// to a parametric spectrum for simple surface color visualization.
vec3 ObserveParametricSpectrumUnderD65(vec4 BetaAndIntensity)
{
    const int SampleCount = 16;
    const float DeltaLambda = (CIE_LAMBDA_MAX - CIE_LAMBDA_MIN) / SampleCount;
    vec3 Color = vec3(0, 0, 0);
    for (int I = 0; I < SampleCount; I++) {
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

#endif // COMMON_GLSL_INC
