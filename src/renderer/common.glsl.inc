const float INFINITY    = 1e30f;
const float EPSILON     = 1e-9f;
const float PI          = 3.141592653f;
const float TAU         = 6.283185306f;

const float CIE_LAMBDA_MIN = 360.0;
const float CIE_LAMBDA_MAX = 830.0;

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

const uint CAMERA_MODEL_PINHOLE             = 0;
const uint CAMERA_MODEL_THIN_LENS           = 1;
const uint CAMERA_MODEL_360                 = 2;

const uint SHAPE_TYPE_MESH_INSTANCE         = 0;
const uint SHAPE_TYPE_PLANE                 = 1;
const uint SHAPE_TYPE_SPHERE                = 2;
const uint SHAPE_TYPE_CUBE                  = 3;

const uint TEXTURE_TYPE_RAW                 = 0;
const uint TEXTURE_TYPE_REFLECTANCE_WITH_ALPHA = 1;
const uint TEXTURE_TYPE_RADIANCE            = 2;

const uint TEXTURE_FLAG_FILTER_NEAREST      = 1 << 0;

struct packed_transform
{
    mat4                To;
    mat4                From;
};

struct packed_texture
{
    vec2                AtlasPlacementMinimum;
    vec2                AtlasPlacementMaximum;
    uint                AtlasImageIndex;
    uint                Type;
    uint                Flags;
    uint                Dummy2;
};

struct packed_material
{
    vec3                BaseSpectrum;
    float               BaseWeight;
    vec3                SpecularSpectrum;
    float               SpecularWeight;
    vec3                TransmissionSpectrum;
    float               TransmissionWeight;
    vec3                TransmissionScatterSpectrum;
    float               TransmissionScatterAnisotropy;
    vec3                EmissionSpectrum;
    float               EmissionLuminance;
    vec3                CoatColorSpectrum;
    float               CoatWeight;
    float               Opacity;
    float               BaseMetalness;
    float               BaseDiffuseRoughness;
    float               CoatIOR;
    float               CoatRoughness;
    float               CoatRoughnessAnisotropy;
    float               CoatDarkening;
    float               SpecularIOR;
    float               SpecularRoughness;
    float               SpecularRoughnessAnisotropy;
    float               TransmissionDepth;
    float               TransmissionDispersionScale;
    float               TransmissionDispersionAbbeNumber;
    uint                BaseSpectrumTextureIndex;
    uint                SpecularRoughnessTextureIndex;
    uint                EmissionSpectrumTextureIndex;
    uint                LayerBounceLimit;
};

struct packed_shape
{
    uint                Type;
    uint                MaterialIndex;
    uint                MeshRootNodeIndex;
    uint                Priority;
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
    vec3                Position;
    vec4                Plane;
    vec3                Base1;
    vec3                Base2;
};

struct packed_mesh_face_extra
{
    vec3                Normals[3];
    vec2                UVs[3];
    uint                MaterialIndex;
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

struct ray
{
    vec3                Origin;
    vec3                Vector;
    float               Duration;
};

// Result of tracing a ray against the geometry of a scene.
struct hit
{
    float               Time;                       // Time of hit along the ray vector.
    uint                ShapeIndex;                 // Index (into the shape array) of shape that was hit.
    uint                ShapePriority;              // Priority of the shape that was hit, for resolving overlapping shapes.
    vec3                Position;                   // World space position of the hit.
    vec3                Normal;                     // World space normal of the hit surface.
    vec3                TangentX;                   // World space tangent of the hit surface.
    vec3                TangentY;                   // World space bitangent of the hit surface.
    uint                MaterialIndex;              // Surface material index.
    vec2                UV;                         // Surface texture map coordinates.

    uint                ShapeType;                  // Type of shape that was hit.
    vec3                PrimitiveCoordinates;       // Shape type-dependent primitive coordinates (e.g. barycentric coordinates).
    uint                PrimitiveIndex;             // Index of primitive within the shape (e.g. mesh face) that was hit.
    uint                SceneComplexity;            // Total number of shape nodes traversed while tracing.
    uint                MeshComplexity;             // Total number of mesh nodes traversed while tracing.
};

// Local surface material properties for the OpenPBR BSDF.
struct surface
{
    float               Opacity;                    // Opacity of the surface geometry.
    uint                LayerBounceLimit;           // Maximum number of BSDF layer bounces.
    bool                BaseIsMetal;                // Is the base substrate layer in the BSDF stack metal?
    bool                BaseIsTranslucent;          // Is the base substrate layer in the BSDF stack a translucent dielectric?
    vec4                BaseReflectance;            // Base reflectance value at each of the 4 sampling wavelengths.
    float               BaseDiffuseRoughness;       // Oren-Nayar roughness for the glossy-diffuse base substrate layer.
    bool                CoatIsPresent;              // Is there a dielectric coat layer over the base substrate?
    vec4                CoatRelativeIOR;            // Relative IOR of the coat layer at each of the 4 sampling wavelengths.
    vec4                CoatTransmittance;          // Transmittance of the coat layer at each of the 4 sampling wavelengths.
    vec2                CoatRoughnessAlpha;         // Coat GGX roughness alpha (X and Y).
    float               SpecularWeight;             // Artistic specular weight parameter.
    vec4                SpecularRelativeIOR;        // Relative IOR of a dielectric base substrate at each of the 4 sampling wavelengths.
    vec4                SpecularReflectance;        // 82-degree reflectance of the metal base substrate at each of the 4 sampling wavelengths.
    vec2                SpecularRoughnessAlpha;     // Base substrate GGX roughness alpha (X and Y).
    vec4                Emission;                   // Base substrate emission radiance at each of the 4 sampling wavelengths.
};

// Volumetric material properties of a participating medium.
struct medium
{
    uint                ShapeIndex;                 // Shape associated with this medium.
    uint                ShapePriority;              // Priority of the shape associated with the medium.
    vec4                IOR;                        // Absolute index of refraction at each of the 4 sampling wavelengths.
    vec4                AbsorptionRate;             // Absorption rate at each of the 4 sampling wavelengths.
    vec4                ScatteringRate;             // Scattering rate at each of the 4 sampling wavelengths.
    float               ScatteringAnisotropy;       // Scattering phase function anisotropy.
};

const uint MAX_MEDIUM_COUNT = 2;

struct path
{
    vec4                Lambda;
    vec4                Throughput;                 // Path throughput at each of the 4 sampling wavelengths.
    vec4                Weight;                     // Path weight at each of the 4 sampling wavelengths.
    vec3                Sample;
    //medium            Mediums[MAX_MEDIUM_COUNT];
    ivec2               ImagePosition;
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
    OutR.Vector = TransformVector(R.Vector, Transform);
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
    OutR.Vector = InverseTransformVector(R.Vector, Transform);
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

// Compute an arbitrary 3D coordinate frame given one vector.
void ComputeCoordinateFrame(vec3 Z, out vec3 X, out vec3 Y)
{
    vec3 V = abs(Z.x) < 0.9 ? vec3(1, 0, 0) : vec3(0, 1, 0);
    X = normalize(cross(V, Z));
    Y = cross(X, Z);
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

/* --- Multi-Wavelength Sampling ------------------------------------------- */

void AddEmission(inout path Path, vec4 Emission)
{
    float ClusterPDF = Path.Weight.x + Path.Weight.y + Path.Weight.z + Path.Weight.w;
    Path.Sample += SampleStandardObserverSRGB(Path.Lambda) * (Emission * Path.Throughput) / ClusterPDF;
}

/* --- Utilities ----------------------------------------------------------- */

// Packs a unit vector into a single 32-bit value.
uint PackUnitVector(vec3 Vector)
{
    float Phi;
    if (abs(Vector.x) < EPSILON)
        Phi = Vector.y < 0.0 ? -PI/2 : +PI/2;
    else if (abs(Vector.y) < EPSILON)
        Phi = Vector.x < 0.0 ? PI : 0.0;
    else
        Phi = atan(Vector.y, Vector.x);
    return packHalf2x16(vec2(Phi, Vector.z));
}

// Unpacks a unit vector from a single 32-bit value.
vec3 UnpackUnitVector(uint PackedVector)
{
    vec2 PhiZ = unpackHalf2x16(PackedVector);
    float R = sqrt(max(0.0, 1 - PhiZ.y * PhiZ.y));
    vec2 XY = R * vec2(cos(PhiZ.x), sin(PhiZ.x));
    return vec3(XY, PhiZ.y);
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

const int PATH_COUNT = 2048*1024;

struct trace_buffer
{
    // Query data.
    float               OriginX             [PATH_COUNT];
    float               OriginY             [PATH_COUNT];
    float               OriginZ             [PATH_COUNT];
    float               VectorX             [PATH_COUNT];
    float               VectorY             [PATH_COUNT];
    float               VectorZ             [PATH_COUNT];
    float               Duration            [PATH_COUNT];

    // Result data.
    float               Time                [PATH_COUNT];
    uint                ShapeIndex          [PATH_COUNT];
    uint                ShapePriority       [PATH_COUNT];
    uint                MaterialIndex       [PATH_COUNT];
    uint                PackedNormal        [PATH_COUNT];
    uint                PackedTangentX      [PATH_COUNT];
    float               TextureU            [PATH_COUNT];
    float               TextureV            [PATH_COUNT];

    // Result data stored only for specific rendering modes.
    uint                PrimitiveIndex      [PATH_COUNT];
    uint                SceneComplexity     [PATH_COUNT];
    uint                MeshComplexity      [PATH_COUNT];
};

struct path_buffer
{
    int                 ImagePosition       [PATH_COUNT];
    float               Lambda0             [PATH_COUNT];
    float               Lambda1             [PATH_COUNT];
    float               Lambda2             [PATH_COUNT];
    float               Lambda3             [PATH_COUNT];
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
};

/* --- Bindings ------------------------------------------------------------ */

#ifdef DECLARE_FRAME_UBO_BINDING

layout(binding=0)
uniform frame_uniform_buffer
{
    uint                CameraModel;
    float               CameraFocalLength;
    float               CameraApertureRadius;
    float               CameraSensorDistance;
    vec2                CameraSensorSize;
    packed_transform    CameraTransform;
    uint                RenderMode;
    uint                RenderFlags;
    uint                RenderSampleBlockSize;
    uint                RenderBounceLimit;
    float               RenderTerminationProbability;
    uint                RenderMeshComplexityScale;
    uint                RenderSceneComplexityScale;
    uint                SelectedShapeIndex;
    float               Brightness;
    uint                ToneMappingMode;
    float               ToneMappingWhiteLevel;
};

#endif

#ifdef DECLARE_SCENE_BINDINGS

layout(set=1, binding=0) uniform SceneUBO { packed_scene_globals Scene; };

layout(set=1, binding=1) uniform sampler2DArray TextureArrayNearest;
layout(set=1, binding=2) uniform sampler2DArray TextureArrayLinear;

layout(set=1, binding=3, std430) readonly buffer TextureSSBO       { packed_texture         Textures[];       };
layout(set=1, binding=4, std430) readonly buffer MaterialSSBO      { packed_material        Materials[];      };
layout(set=1, binding=5, std430) readonly buffer ShapeSSBO         { packed_shape           Shapes[];         };
layout(set=1, binding=6, std430) readonly buffer ShapeNodeSSBO     { packed_shape_node      ShapeNodes[];     };
layout(set=1, binding=7, std430) readonly buffer MeshFaceSSBO      { packed_mesh_face       MeshFaces[];      };
layout(set=1, binding=8, std430) readonly buffer MeshFaceExtraSSBO { packed_mesh_face_extra MeshFaceExtras[]; };
layout(set=1, binding=9, std430) readonly buffer MeshNodeSSBO      { packed_mesh_node       MeshNodes[];      };

#endif

#ifdef DECLARE_COMPUTE_BINDINGS

layout(set=0, binding=1, rgba32f) uniform image2D SampleAccumulatorImage;

layout(set=0, binding=2,  std430) buffer TraceSSBO { trace_buffer  TraceBuffer;  };
layout(set=0, binding=3,  std430) buffer PathSSBO  { path_buffer   PathBuffer;   };

layout(push_constant) uniform ComputePushConstantBuffer
{
    uint RandomSeed;
    uint Restart;
};

ray LoadTraceRay(uint Index)
{
    ray Ray;
    Ray.Origin.x = TraceBuffer.OriginX  [Index];
    Ray.Origin.y = TraceBuffer.OriginY  [Index];
    Ray.Origin.z = TraceBuffer.OriginZ  [Index];
    Ray.Vector.x = TraceBuffer.VectorX  [Index];
    Ray.Vector.y = TraceBuffer.VectorY  [Index];
    Ray.Vector.z = TraceBuffer.VectorZ  [Index];
    Ray.Duration = TraceBuffer.Duration [Index];
    return Ray;
}

void StoreTraceRay(uint Index, ray Ray)
{
    TraceBuffer.OriginX  [Index] = Ray.Origin.x;
    TraceBuffer.OriginY  [Index] = Ray.Origin.y;
    TraceBuffer.OriginZ  [Index] = Ray.Origin.z;
    TraceBuffer.VectorX  [Index] = Ray.Vector.x;
    TraceBuffer.VectorY  [Index] = Ray.Vector.y;
    TraceBuffer.VectorZ  [Index] = Ray.Vector.z;
    TraceBuffer.Duration [Index] = Ray.Duration;
}

hit LoadTraceHit(uint Index)
{
    hit Hit;
    Hit.Time                   = TraceBuffer.Time            [Index];

    if (RenderMode == RENDER_MODE_SCENE_COMPLEXITY)
        Hit.SceneComplexity    = TraceBuffer.SceneComplexity [Index];
    if (RenderMode == RENDER_MODE_MESH_COMPLEXITY)
        Hit.MeshComplexity     = TraceBuffer.MeshComplexity  [Index];

    if (Hit.Time == INFINITY) return Hit;

    if (RenderMode == RENDER_MODE_PRIMITIVE_INDEX)
        Hit.PrimitiveIndex     = TraceBuffer.PrimitiveIndex  [Index];

    Hit.ShapeIndex             = TraceBuffer.ShapeIndex      [Index];
    Hit.ShapePriority          = TraceBuffer.ShapePriority   [Index];
    Hit.MaterialIndex          = TraceBuffer.MaterialIndex   [Index];

    Hit.Normal                 = UnpackUnitVector(TraceBuffer.PackedNormal   [Index]);
    Hit.TangentX               = UnpackUnitVector(TraceBuffer.PackedTangentX [Index]);
    Hit.TangentY               = cross(Hit.Normal, Hit.TangentX);

    Hit.UV.x                   = TraceBuffer.TextureU        [Index];
    Hit.UV.y                   = TraceBuffer.TextureV        [Index];
    return Hit;
}

void StoreTraceHit(uint Index, hit Hit)
{
    TraceBuffer.Time            [Index] = Hit.Time;

    if (RenderMode == RENDER_MODE_SCENE_COMPLEXITY)
        TraceBuffer.SceneComplexity [Index] = Hit.SceneComplexity;
    if (RenderMode == RENDER_MODE_MESH_COMPLEXITY)
        TraceBuffer.MeshComplexity  [Index] = Hit.MeshComplexity;

    if (Hit.Time == INFINITY) return;

    if (RenderMode == RENDER_MODE_PRIMITIVE_INDEX)
        TraceBuffer.PrimitiveIndex  [Index] = Hit.PrimitiveIndex;

    TraceBuffer.ShapeIndex      [Index] = Hit.ShapeIndex;
    TraceBuffer.ShapePriority   [Index] = Hit.ShapePriority;
    TraceBuffer.MaterialIndex   [Index] = Hit.MaterialIndex;
    TraceBuffer.PackedNormal    [Index] = PackUnitVector(Hit.Normal);
    TraceBuffer.PackedTangentX  [Index] = PackUnitVector(Hit.TangentX);
    TraceBuffer.TextureU        [Index] = Hit.UV.x;
    TraceBuffer.TextureV        [Index] = Hit.UV.y;
}

path LoadPath(uint Index)
{
    path Path;
    Path.ImagePosition.x = PathBuffer.ImagePosition  [Index] % 1920;
    Path.ImagePosition.y = PathBuffer.ImagePosition  [Index] / 1920;
    Path.Lambda.x        = PathBuffer.Lambda0        [Index];
    Path.Lambda.y        = PathBuffer.Lambda1        [Index];
    Path.Lambda.z        = PathBuffer.Lambda2        [Index];
    Path.Lambda.w        = PathBuffer.Lambda3        [Index];
    Path.Throughput.x    = PathBuffer.Throughput0    [Index];
    Path.Throughput.y    = PathBuffer.Throughput1    [Index];
    Path.Throughput.z    = PathBuffer.Throughput2    [Index];
    Path.Throughput.w    = PathBuffer.Throughput3    [Index];
    Path.Weight.x        = PathBuffer.Weight0        [Index];
    Path.Weight.y        = PathBuffer.Weight1        [Index];
    Path.Weight.w        = PathBuffer.Weight2        [Index];
    Path.Weight.z        = PathBuffer.Weight3        [Index];
    Path.Sample.r        = PathBuffer.SampleR        [Index];
    Path.Sample.g        = PathBuffer.SampleG        [Index];
    Path.Sample.b        = PathBuffer.SampleB        [Index];
    return Path;
}

void StorePathVertexData(uint Index, path Path)
{
    PathBuffer.Throughput0    [Index] = Path.Throughput.x;
    PathBuffer.Throughput1    [Index] = Path.Throughput.y;
    PathBuffer.Throughput2    [Index] = Path.Throughput.z;
    PathBuffer.Throughput3    [Index] = Path.Throughput.w;
    PathBuffer.Weight0        [Index] = Path.Weight.x;
    PathBuffer.Weight1        [Index] = Path.Weight.y;
    PathBuffer.Weight2        [Index] = Path.Weight.w;
    PathBuffer.Weight3        [Index] = Path.Weight.z;
    PathBuffer.SampleR        [Index] = Path.Sample.r;
    PathBuffer.SampleG        [Index] = Path.Sample.g;
    PathBuffer.SampleB        [Index] = Path.Sample.b;
}

void StorePath(uint Index, path Path)
{
    PathBuffer.ImagePosition  [Index] = Path.ImagePosition.y * 1920 + Path.ImagePosition.x;
    PathBuffer.Lambda0        [Index] = Path.Lambda.x;
    PathBuffer.Lambda1        [Index] = Path.Lambda.y;
    PathBuffer.Lambda2        [Index] = Path.Lambda.z;
    PathBuffer.Lambda3        [Index] = Path.Lambda.w;

    StorePathVertexData(Index, Path);
}

#endif