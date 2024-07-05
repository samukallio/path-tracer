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

// Result of tracing a ray against the geometry of a scene.
struct hit
{
    float               Time;                       // Time of hit along the ray vector.
    uint                ShapeIndex;                 // Index (into the shape array) of shape that was hit.
    vec3                Position;                   // World space position of the hit.
    vec3                Normal;                     // World space normal of the hit surface.
    vec3                TangentX;                   // World space tangent of the hit surface.
    uint                MaterialIndex;              // Surface material index.
    vec2                UV;                         // Surface texture map coordinates.

    vec3                TangentY;                   // World space bitangent of the hit surface.

    uint                ShapeType;                  // Type of shape that was hit.
    vec3                PrimitiveCoordinates;       // Shape type-dependent primitive coordinates (e.g. barycentric coordinates).
    uint                PrimitiveIndex;             // Index of primitive within the shape (e.g. mesh face) that was hit.
    uint                SceneComplexity;            // Total number of shape nodes traversed while tracing.
    uint                MeshComplexity;             // Total number of mesh nodes traversed while tracing.
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

/* --- Scene Bindings ------------------------------------------------------ */

#ifdef BIND_SCENE

const uint SHAPE_TYPE_MESH_INSTANCE         = 0;
const uint SHAPE_TYPE_PLANE                 = 1;
const uint SHAPE_TYPE_SPHERE                = 2;
const uint SHAPE_TYPE_CUBE                  = 3;

const uint TEXTURE_TYPE_RAW                 = 0;
const uint TEXTURE_TYPE_REFLECTANCE_WITH_ALPHA = 1;
const uint TEXTURE_TYPE_RADIANCE            = 2;

const uint TEXTURE_FLAG_FILTER_NEAREST      = 1 << 0;

struct packed_texture
{
    vec2                AtlasPlacementMinimum;
    vec2                AtlasPlacementMaximum;
    uint                AtlasImageIndex;
    uint                Type;
    uint                Flags;
    uint                Dummy2;
};

struct packed_shape
{
    uint                Type;
    uint                MaterialIndex;
    uint                MeshRootNodeIndex;
    packed_transform    Transform;
};

struct packed_shape_node
{
    vec3                Minimum;
    uint                ChildNodeIndices;
    vec3                Maximum;
    uint                ShapeIndex;
};

struct packed_mesh_face
{
    vec3                Position0;
    uint                VertexIndex0;
    vec3                Position1;
    uint                VertexIndex1;
    vec3                Position2;
    uint                VertexIndex2;
};

struct packed_mesh_vertex
{
    uint                PackedNormal;
    uint                PackedUV;
};

struct packed_mesh_node
{
    vec3                Minimum;
    uint                FaceBeginOrNodeIndex;
    vec3                Maximum;
    uint                FaceEndIndex;
};

struct packed_scene_globals
{
    mat3                SkyboxDistributionFrame;
    float               SkyboxDistributionConcentration;
    float               SkyboxBrightness;
    uint                SkyboxTextureIndex;
    uint                ShapeCount;
    float               SceneScatterRate;
};

layout(set=BIND_SCENE, binding=0)
uniform SceneUBO
{
    packed_scene_globals Scene;
};

layout(set=BIND_SCENE, binding=1)
uniform sampler2DArray TextureArrayNearest;

layout(set=BIND_SCENE, binding=2)
uniform sampler2DArray TextureArrayLinear;

layout(set=BIND_SCENE, binding=3, std430)
readonly buffer TextureSSBO
{
    packed_texture Textures[];
};

layout(set=BIND_SCENE, binding=4, std430)
readonly buffer MaterialSSBO
{
    uint MaterialData[];
};

layout(set=BIND_SCENE, binding=5, std430)
readonly buffer ShapeSSBO
{
    packed_shape Shapes[];
};

layout(set=BIND_SCENE, binding=6, std430)
readonly buffer ShapeNodeSSBO
{
    packed_shape_node ShapeNodes[];
};

layout(set=BIND_SCENE, binding=7, std430)
readonly buffer MeshFaceSSBO
{
    packed_mesh_face MeshFaces[];
};

layout(set=BIND_SCENE, binding=8, std430)
readonly buffer MeshVertexSSBO
{
    packed_mesh_vertex MeshVertices[];
};

layout(set=BIND_SCENE, binding=9, std430)
readonly buffer MeshNodeSSBO
{
    packed_mesh_node MeshNodes[];
};

uint MaterialUint(uint MaterialSlotIndex, uint AttributeIndex)
{
    return MaterialData[32*MaterialSlotIndex + AttributeIndex];
}

float MaterialFloat(uint MaterialSlotIndex, uint AttributeIndex)
{
    return uintBitsToFloat(MaterialUint(MaterialSlotIndex, AttributeIndex));
}

vec3 MaterialVec3(uint MaterialSlotIndex, uint AttributeIndex)
{
    return vec3(
        MaterialFloat(MaterialSlotIndex, AttributeIndex+0),
        MaterialFloat(MaterialSlotIndex, AttributeIndex+1),
        MaterialFloat(MaterialSlotIndex, AttributeIndex+2));
}

vec4 SampleTexture(uint Index, vec2 UV)
{
    packed_texture Texture = Textures[Index];

    float U = mix(
        Texture.AtlasPlacementMinimum.x,
        Texture.AtlasPlacementMaximum.x,
        fract(UV.x));

    float V = mix(
        Texture.AtlasPlacementMinimum.y,
        Texture.AtlasPlacementMaximum.y,
        fract(UV.y));

    vec3 UVW = vec3(U, V, Texture.AtlasImageIndex);

    if ((Texture.Flags & TEXTURE_FLAG_FILTER_NEAREST) != 0)
        return textureLod(TextureArrayNearest, UVW, 0);
    else
        return textureLod(TextureArrayLinear, UVW, 0);
}

// Sample the parametric coefficients of the emission spectrum
// of the environment map in the given direction.
vec4 SampleSkyboxSpectrum(vec3 Direction)
{
    if (Scene.SkyboxTextureIndex == TEXTURE_INDEX_NONE)
        return vec4(0, 0, 100, 1);

    float Phi = atan(Direction.y, Direction.x);
    float Theta = asin(Direction.z);

    float U = 0.5 + Phi / TAU;
    float V = 0.5 + Theta / PI;

    return SampleTexture(Scene.SkyboxTextureIndex, vec2(U, V));
}

// Sample radiances at given wavelengths of the emission spectrum
// of the environment map in the given direction.
vec4 SampleSkyboxRadiance(vec3 Direction, vec4 Lambda)
{
    vec4 Spectrum = SampleSkyboxSpectrum(Direction);
    return SampleParametricSpectrum(Spectrum, Lambda) * Scene.SkyboxBrightness;
}

void IntersectMeshFace(ray Ray, uint MeshFaceIndex, inout hit Hit)
{
    packed_mesh_face Face = MeshFaces[MeshFaceIndex];

    vec3 Edge1 = Face.Position1 - Face.Position0;
    vec3 Edge2 = Face.Position2 - Face.Position0;

    vec3 RayCrossEdge2 = cross(Ray.Velocity, Edge2);
    float Det = dot(Edge1, RayCrossEdge2);

    if (abs(Det) < EPSILON) return;

    float InvDet = 1.0 / Det;

    vec3 S = Ray.Origin - Face.Position0;
    float U = InvDet * dot(S, RayCrossEdge2);
    if (U < 0 || U > 1) return;

    vec3 SCrossEdge1 = cross(S, Edge1);
    float V = InvDet * dot(Ray.Velocity, SCrossEdge1);
    if (V < 0 || U + V > 1) return;

    float T = InvDet * dot(Edge2, SCrossEdge1);
    if (T < 0 || T > Hit.Time) return;

    Hit.Time = T;
    Hit.ShapeType = SHAPE_TYPE_MESH_INSTANCE;
    Hit.ShapeIndex = 0xFFFFFFFE;
    Hit.PrimitiveIndex = MeshFaceIndex;
    Hit.PrimitiveCoordinates = vec3(1 - U - V, U, V);
}

void IntersectMeshNode(ray Ray, uint MeshNodeIndex, inout hit Hit)
{
    uint Stack[32];
    uint Depth = 0;

    packed_mesh_node Node = MeshNodes[MeshNodeIndex];

    while (true) {
        Hit.MeshComplexity++;

        // Leaf node or internal?
        if (Node.FaceEndIndex > 0) {
            // Leaf node, trace all geometry within.
            for (uint FaceIndex = Node.FaceBeginOrNodeIndex; FaceIndex < Node.FaceEndIndex; FaceIndex++)
                IntersectMeshFace(Ray, FaceIndex, Hit);
        }
        else {
            // Internal node.
            // Load the first subnode as the node to be processed next.
            uint Index = Node.FaceBeginOrNodeIndex;
            Node = MeshNodes[Index];
            float Time = IntersectBoundingBox(Ray, Hit.Time, Node.Minimum, Node.Maximum);

            // Also load the second subnode to see if it is closer.
            uint IndexB = Index + 1;
            packed_mesh_node NodeB = MeshNodes[IndexB];
            float TimeB = IntersectBoundingBox(Ray, Hit.Time, NodeB.Minimum, NodeB.Maximum);

            // If the second subnode is strictly closer than the first one,
            // then it was definitely hit, so process it next.
            if (Time > TimeB) {
                // If the first subnode was also hit, then set it aside for later.
                if (Time < INFINITY) Stack[Depth++] = Index;
                Node = NodeB;
                continue;
            }

            // The first subnode is at least as close as the second one.
            // If the second subnode was hit, then both of them were,
            // and we should set the second one aside for later.
            if (TimeB < INFINITY) {
                Stack[Depth++] = IndexB;
                continue;
            }

            // The first subnode is at least as close as the second one,
            // and the second subnode was not Hit.  If the first one was
            // hit, then process it next.
            if (Time < INFINITY) continue;
        }

        // Just processed a leaf node or an internal node with no intersecting
        // subnodes.  If the stack is also empty, then we are done.
        if (Depth == 0) break;

        // Pull a node from the stack.
        Node = MeshNodes[Stack[--Depth]];
    }
}

void IntersectShape(ray Ray, uint ShapeIndex, inout hit Hit)
{
    packed_shape Shape = Shapes[ShapeIndex];

    Ray = InverseTransformRay(Ray, Shape.Transform);

    if (Shape.Type == SHAPE_TYPE_MESH_INSTANCE) {
        IntersectMeshNode(Ray, Shape.MeshRootNodeIndex, Hit);
        if (Hit.ShapeIndex == 0xFFFFFFFE)
            Hit.ShapeIndex = ShapeIndex;
    }
    else if (Shape.Type == SHAPE_TYPE_PLANE) {
        float T = -Ray.Origin.z / Ray.Velocity.z;
        if (T < 0 || T > Hit.Time) return;

        Hit.Time = T;
        Hit.ShapeType = SHAPE_TYPE_PLANE;
        Hit.ShapeIndex = ShapeIndex;
        Hit.PrimitiveIndex = 0;
        Hit.PrimitiveCoordinates = Ray.Origin + Ray.Velocity * T;
    }
    else if (Shape.Type == SHAPE_TYPE_SPHERE) {
        float V = dot(Ray.Velocity, Ray.Velocity);
        float P = dot(Ray.Origin, Ray.Velocity);
        float Q = dot(Ray.Origin, Ray.Origin) - 1.0;
        float D2 = P * P - Q * V;
        if (D2 < 0) return;

        float D = sqrt(D2);
        if (D < P) return;

        float S0 = -P - D;
        float S1 = -P + D;
        float S = S0 < 0 ? S1 : S0;
        if (S < 0 || S > V * Hit.Time) return;

        Hit.Time = S / V;
        Hit.ShapeType = SHAPE_TYPE_SPHERE;
        Hit.ShapeIndex = ShapeIndex;
        Hit.PrimitiveIndex = 0;
        Hit.PrimitiveCoordinates = Ray.Origin + Ray.Velocity * Hit.Time;
    }
    else if (Shape.Type == SHAPE_TYPE_CUBE) {
        vec3 Minimum = (vec3(-1,-1,-1) - Ray.Origin) / Ray.Velocity;
        vec3 Maximum = (vec3(+1,+1,+1) - Ray.Origin) / Ray.Velocity;
        vec3 Earlier = min(Minimum, Maximum);
        vec3 Later = max(Minimum, Maximum);
        float T0 = max(max(Earlier.x, Earlier.y), Earlier.z);
        float T1 = min(min(Later.x, Later.y), Later.z);
        if (T1 < T0) return;
        if (T1 <= 0) return;

        float T = T0 < 0 ? T1 : T0;
        if (T >= Hit.Time) return;

        Hit.Time = T;
        Hit.ShapeType = SHAPE_TYPE_CUBE;
        Hit.ShapeIndex = ShapeIndex;
        Hit.PrimitiveIndex = 0;
        Hit.PrimitiveCoordinates = Ray.Origin + Ray.Velocity * T;
    }
}

void Intersect(ray Ray, inout hit Hit)
{
    if (Scene.ShapeCount == 0) return;

    uint Stack[32];
    uint Depth = 0;

    packed_shape_node NodeA = ShapeNodes[0];
    packed_shape_node NodeB;

    while (true) {
        Hit.SceneComplexity++;

        // Leaf node or internal?
        if (NodeA.ChildNodeIndices == 0) {
            // Leaf node, intersect object.
            IntersectShape(Ray, NodeA.ShapeIndex, Hit);
        }
        else {
            // Internal node.
            uint IndexA = NodeA.ChildNodeIndices & 0xFFFF;
            uint IndexB = NodeA.ChildNodeIndices >> 16;

            NodeA = ShapeNodes[IndexA];
            NodeB = ShapeNodes[IndexB];

            float TimeA = IntersectBoundingBox(Ray, Hit.Time, NodeA.Minimum, NodeA.Maximum);
            float TimeB = IntersectBoundingBox(Ray, Hit.Time, NodeB.Minimum, NodeB.Maximum);

            if (TimeA > TimeB) {
                if (TimeA < INFINITY) Stack[Depth++] = IndexA;
                NodeA = NodeB;
                continue;
            }

            if (TimeB < INFINITY) {
                Stack[Depth++] = IndexB;
                continue;
            }

            if (TimeA < INFINITY) continue;
        }

        if (Depth == 0) break;

        NodeA = ShapeNodes[Stack[--Depth]];
    }
}

hit Trace(ray Ray)
{
    hit Hit;
    Hit.ShapeIndex = SHAPE_INDEX_NONE;
    Hit.Time = Ray.Duration;
    Hit.MeshComplexity = 0;
    Hit.SceneComplexity = 0;

    Intersect(Ray, Hit);

    if (Hit.ShapeIndex == SHAPE_INDEX_NONE)
        return Hit;

    packed_shape Shape = Shapes[Hit.ShapeIndex];

    Hit.MaterialIndex = Shape.MaterialIndex;

    if (Hit.ShapeType == SHAPE_TYPE_MESH_INSTANCE) {
        packed_mesh_face Face = MeshFaces[Hit.PrimitiveIndex];

        packed_mesh_vertex Vertex0 = MeshVertices[Face.VertexIndex0];
        packed_mesh_vertex Vertex1 = MeshVertices[Face.VertexIndex1];
        packed_mesh_vertex Vertex2 = MeshVertices[Face.VertexIndex2];

        vec3 Normal = SafeNormalize(
            UnpackUnitVector(Vertex0.PackedNormal) * Hit.PrimitiveCoordinates.x +
            UnpackUnitVector(Vertex1.PackedNormal) * Hit.PrimitiveCoordinates.y +
            UnpackUnitVector(Vertex2.PackedNormal) * Hit.PrimitiveCoordinates.z);

        Hit.Normal = TransformNormal(Normal, Shape.Transform);
        Hit.TangentX = ComputeTangentVector(Hit.Normal);
        Hit.UV = unpackHalf2x16(Vertex0.PackedUV) * Hit.PrimitiveCoordinates.x
                + unpackHalf2x16(Vertex1.PackedUV) * Hit.PrimitiveCoordinates.y
                + unpackHalf2x16(Vertex2.PackedUV) * Hit.PrimitiveCoordinates.z;
    }
    else if (Hit.ShapeType == SHAPE_TYPE_PLANE) {
        Hit.Normal = TransformNormal(vec3(0, 0, 1), Shape.Transform);
        Hit.TangentX = TransformDirection(vec3(1, 0, 0), Shape.Transform);
        Hit.UV = fract(Hit.PrimitiveCoordinates.xy);
    }
    else if (Hit.ShapeType == SHAPE_TYPE_SPHERE) {
        vec3 P = Hit.PrimitiveCoordinates;
        float U = (atan(P.y, P.x) + PI) / TAU;
        float V = (P.z + 1.0) / 2.0;
        
        Hit.Normal = TransformNormal(P, Shape.Transform);
        Hit.TangentX = TransformDirection(cross(P, vec3(-P.y, P.x, 0)), Shape.Transform);
        Hit.UV = vec2(U, V);
    }
    else if (Hit.ShapeType == SHAPE_TYPE_CUBE) {
        vec3 P = Hit.PrimitiveCoordinates;
        vec3 Q = abs(P);

        vec3 Normal;
        vec3 TangentX;

        if (Q.x >= Q.y && Q.x >= Q.z) {
            float S = sign(P.x);
            Normal = vec3(S, 0, 0);
            TangentX = vec3(0, S, 0);
            Hit.UV = 0.5 * (1.0 + P.yz);
        }
        else if (Q.y >= Q.x && Q.y >= Q.z) {
            float S = sign(P.y);
            Normal = vec3(0, S, 0);
            TangentX = vec3(0, 0, S);
            Hit.UV = 0.5 * (1.0 + P.xz);
        }
        else {
            float S = sign(P.z);
            Normal = vec3(0, 0, S);
            TangentX = vec3(S, 0, 0);
            Hit.UV = 0.5 * (1.0 + P.xy);
        }

        Hit.Normal = TransformNormal(Normal, Shape.Transform);
        Hit.TangentX = TransformDirection(TangentX, Shape.Transform);
    }

    return Hit;
}

#endif

/* --- Ray Tracing --------------------------------------------------------- */

#ifdef BIND_TRACE

const int TRACE_COUNT = 2048*1024;

struct trace_buffer
{
    // Query data.
    float   OriginX                 [TRACE_COUNT];
    float   OriginY                 [TRACE_COUNT];
    float   OriginZ                 [TRACE_COUNT];
    uint    PackedVelocity          [TRACE_COUNT];
    float   Duration                [TRACE_COUNT];

    // Result data.
    float   Time                    [TRACE_COUNT];
    uint    ShapeAndMaterialIndex   [TRACE_COUNT];
    uint    PackedNormal            [TRACE_COUNT];
    uint    PackedTangentX          [TRACE_COUNT];
    float   TextureU                [TRACE_COUNT];
    float   TextureV                [TRACE_COUNT];

    // Result data stored only for specific rendering modes.
    uint    PrimitiveIndex          [TRACE_COUNT];
    uint    SceneComplexity         [TRACE_COUNT];
    uint    MeshComplexity          [TRACE_COUNT];
};

layout(set=BIND_TRACE, binding=0, std430) buffer TraceSSBO
{
    trace_buffer TraceBuffer;
};

ray LoadTraceRay(uint Index)
{
    ray Ray;
    Ray.Origin.x = TraceBuffer.OriginX  [Index];
    Ray.Origin.y = TraceBuffer.OriginY  [Index];
    Ray.Origin.z = TraceBuffer.OriginZ  [Index];
    Ray.Velocity = UnpackUnitVector(TraceBuffer.PackedVelocity[Index]);
    Ray.Duration = TraceBuffer.Duration [Index];

    return Ray;
}

void StoreTraceRay(uint Index, ray Ray)
{
    TraceBuffer.OriginX        [Index] = Ray.Origin.x;
    TraceBuffer.OriginY        [Index] = Ray.Origin.y;
    TraceBuffer.OriginZ        [Index] = Ray.Origin.z;
    TraceBuffer.PackedVelocity [Index] = PackUnitVector(Ray.Velocity);
    TraceBuffer.Duration       [Index] = Ray.Duration;
}

hit LoadTraceHit(uint Index)
{
    hit Hit;

    //if (RenderMode == RENDER_MODE_SCENE_COMPLEXITY)
        Hit.SceneComplexity    = TraceBuffer.SceneComplexity [Index];
    //if (RenderMode == RENDER_MODE_MESH_COMPLEXITY)
        Hit.MeshComplexity     = TraceBuffer.MeshComplexity  [Index];
    //if (RenderMode == RENDER_MODE_PRIMITIVE_INDEX)
        Hit.PrimitiveIndex     = TraceBuffer.PrimitiveIndex  [Index];

    uint ShapeAndMaterialIndex = TraceBuffer.ShapeAndMaterialIndex[Index];

    if (ShapeAndMaterialIndex == 0xFFFFFFFF) {
        Hit.ShapeIndex = SHAPE_INDEX_NONE;
        Hit.Time = HIT_TIME_LIMIT;
        return Hit;
    }

    Hit.ShapeIndex             = ShapeAndMaterialIndex >> 16;
    Hit.MaterialIndex          = ShapeAndMaterialIndex & 0xFFFF;

    Hit.Time                   = TraceBuffer.Time            [Index];

    Hit.Normal                 = UnpackUnitVector(TraceBuffer.PackedNormal   [Index]);
    Hit.TangentX               = UnpackUnitVector(TraceBuffer.PackedTangentX [Index]);
    Hit.TangentY               = cross(Hit.Normal, Hit.TangentX);

    Hit.UV.x                   = TraceBuffer.TextureU        [Index];
    Hit.UV.y                   = TraceBuffer.TextureV        [Index];
    return Hit;
}

void StoreTraceHit(uint Index, hit Hit)
{
    //if (RenderMode == RENDER_MODE_SCENE_COMPLEXITY)
        TraceBuffer.SceneComplexity [Index] = Hit.SceneComplexity;
    //if (RenderMode == RENDER_MODE_MESH_COMPLEXITY)
        TraceBuffer.MeshComplexity  [Index] = Hit.MeshComplexity;
    //if (RenderMode == RENDER_MODE_PRIMITIVE_INDEX)
        TraceBuffer.PrimitiveIndex  [Index] = Hit.PrimitiveIndex;

    if (Hit.ShapeIndex == SHAPE_INDEX_NONE) {
        TraceBuffer.ShapeAndMaterialIndex[Index] = 0xFFFFFFFF;
        return;
    }

    TraceBuffer.ShapeAndMaterialIndex [Index] = (Hit.ShapeIndex << 16) | Hit.MaterialIndex;

    TraceBuffer.Time            [Index] = Hit.Time;
    TraceBuffer.PackedNormal    [Index] = PackUnitVector(Hit.Normal);
    TraceBuffer.PackedTangentX  [Index] = PackUnitVector(Hit.TangentX);
    TraceBuffer.TextureU        [Index] = Hit.UV.x;
    TraceBuffer.TextureV        [Index] = Hit.UV.y;
}

#endif // TRACE_BUFFER_BINDING

/* --- Path Tracer Compute Shader Bindings --------------------------------- */

#ifdef BIND_PATH

const int PATH_COUNT = 2048*1024;
const int ACTIVE_SHAPE_LIMIT = 4;

struct path_buffer
{
    int                 ImagePosition       [PATH_COUNT];
    float               NormalizedLambda0   [PATH_COUNT];
    float               Throughput0         [PATH_COUNT];
    float               Throughput1         [PATH_COUNT];
    float               Throughput2         [PATH_COUNT];
    float               Throughput3         [PATH_COUNT];
    float               Weight0             [PATH_COUNT];
    float               Weight1             [PATH_COUNT];
    float               Weight2             [PATH_COUNT];
    float               Weight3             [PATH_COUNT];
    float               SampleR             [PATH_COUNT];
    float               SampleG             [PATH_COUNT];
    float               SampleB             [PATH_COUNT];

    uint                ActiveShapeIndex01  [PATH_COUNT];
    uint                ActiveShapeIndex23  [PATH_COUNT];
};

layout(set=BIND_PATH, binding=0)
uniform PathUBO
{
    camera  Camera;
    uint    RenderMode;
    uint    RenderFlags;
    uint    RenderSampleBlockSize;
    uint    RenderBounceLimit;
    float   RenderTerminationProbability;
};

layout(set=BIND_PATH, binding=1, rgba32f)
uniform image2D SampleAccumulatorImage;

layout(set=BIND_PATH, binding=2, std430)
buffer PathSSBO
{
    path_buffer PathBuffer;
};

path LoadPath(uint Index)
{
    path Path;

    int ImageWidth = imageSize(SampleAccumulatorImage).x;
    Path.ImagePosition.x    = PathBuffer.ImagePosition      [Index] % ImageWidth;
    Path.ImagePosition.y    = PathBuffer.ImagePosition      [Index] / ImageWidth;

    Path.NormalizedLambda0  = PathBuffer.NormalizedLambda0  [Index];

    Path.Throughput.x       = PathBuffer.Throughput0        [Index];
    Path.Throughput.y       = PathBuffer.Throughput1        [Index];
    Path.Throughput.z       = PathBuffer.Throughput2        [Index];
    Path.Throughput.w       = PathBuffer.Throughput3        [Index];

    Path.Weight.x           = PathBuffer.Weight0            [Index];
    Path.Weight.y           = PathBuffer.Weight1            [Index];
    Path.Weight.w           = PathBuffer.Weight2            [Index];
    Path.Weight.z           = PathBuffer.Weight3            [Index];

    Path.Sample.r           = PathBuffer.SampleR            [Index];
    Path.Sample.g           = PathBuffer.SampleG            [Index];
    Path.Sample.b           = PathBuffer.SampleB            [Index];

    uint ActiveShapeIndex01 = PathBuffer.ActiveShapeIndex01[Index];
    Path.ActiveShapeIndex[0] = ActiveShapeIndex01 & 0xFFFF;
    Path.ActiveShapeIndex[1] = ActiveShapeIndex01 >> 16;

    uint ActiveShapeIndex23 = PathBuffer.ActiveShapeIndex23[Index];
    Path.ActiveShapeIndex[2] = ActiveShapeIndex23 & 0xFFFF;
    Path.ActiveShapeIndex[3] = ActiveShapeIndex23 >> 16;

    for (int I = 0; I < 4; I++) {
        if (Path.ActiveShapeIndex[I] == 0xFFFF)
            Path.ActiveShapeIndex[I] = SHAPE_INDEX_NONE;
    }

    return Path;
}

void StorePathVertexData(uint Index, path Path)
{
    PathBuffer.Throughput0          [Index] = Path.Throughput.x;
    PathBuffer.Throughput1          [Index] = Path.Throughput.y;
    PathBuffer.Throughput2          [Index] = Path.Throughput.z;
    PathBuffer.Throughput3          [Index] = Path.Throughput.w;
    PathBuffer.Weight0              [Index] = Path.Weight.x;
    PathBuffer.Weight1              [Index] = Path.Weight.y;
    PathBuffer.Weight2              [Index] = Path.Weight.w;
    PathBuffer.Weight3              [Index] = Path.Weight.z;
    PathBuffer.SampleR              [Index] = Path.Sample.r;
    PathBuffer.SampleG              [Index] = Path.Sample.g;
    PathBuffer.SampleB              [Index] = Path.Sample.b;

    PathBuffer.ActiveShapeIndex01   [Index] = (Path.ActiveShapeIndex[1] << 16) | Path.ActiveShapeIndex[0];
    PathBuffer.ActiveShapeIndex23   [Index] = (Path.ActiveShapeIndex[3] << 16) | Path.ActiveShapeIndex[2];
}

void StorePath(uint Index, path Path)
{
    int ImageWidth = imageSize(SampleAccumulatorImage).x;
    PathBuffer.ImagePosition     [Index] = Path.ImagePosition.y * ImageWidth + Path.ImagePosition.x;
    PathBuffer.NormalizedLambda0 [Index] = Path.NormalizedLambda0;

    StorePathVertexData(Index, Path);
}

#endif

#endif // COMMON_GLSL_INC
