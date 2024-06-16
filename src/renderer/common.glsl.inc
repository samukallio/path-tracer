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

struct medium
{
    uint                ShapeIndex;
    uint                ShapePriority;
    float               IOR;
    float               AbsorptionRate;
    float               ScatteringRate;
    float               ScatteringAnisotropy;
};

struct hit
{
    float               Time;
    uint                ShapeType;
    uint                ShapeIndex;
    uint                ShapePriority;
    uint                PrimitiveIndex;
    vec3                PrimitiveCoordinates;
    uint                SceneComplexity;
    uint                MeshComplexity;
    vec3                Position;
    vec3                Normal;
    vec3                TangentX;
    vec3                TangentY;
    vec2                UV;
    uint                MaterialIndex;
};

struct surface
{
    float               Opacity;
    uint                LayerBounceLimit;
    bool                BaseIsMetal;
    bool                BaseIsTranslucent;
    float               BaseReflectance;
    float               BaseDiffuseRoughness;
    bool                CoatIsPresent;
    float               CoatRelativeIOR;
    float               CoatTransmittance;
    vec2                CoatRoughnessAlpha;
    float               SpecularWeight;
    float               SpecularRelativeIOR;
    float               SpecularReflectance;
    vec2                SpecularRoughnessAlpha;
    float               Emission;
};

struct ray
{
    vec3                Origin;
    vec3                Vector;
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

// Compute an arbitrary 3D coordinate frame given one vector.
void ComputeCoordinateFrame(vec3 Z, out vec3 X, out vec3 Y)
{
    vec3 V = abs(Z.x) < 0.9 ? vec3(1, 0, 0) : vec3(0, 1, 0);
    X = normalize(cross(V, Z));
    Y = cross(X, Z);
}

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

// Compute the 2D roughness alpha parameter for the GGX distribution of normals.
vec2 ComputeRoughnessAlphaGGX(float Roughness, float Anisotropy)
{
    float R = Roughness;
    float S = 1 - Anisotropy;
    float AlphaX = R * R * sqrt(2 / (1 + S * S));
    float AlphaY = S * AlphaX;
    return vec2(AlphaX, AlphaY);
}

// Sample a normal from the GGX distribution that is visible from the given
// direction according to the procedure outlined in the paper "Sampling the
// GGX Distribution of Visible Normals" by Eric Heitz.
vec3 SampleVisibleNormalGGX(vec3 Direction, vec2 RoughnessAlpha, float U1, float U2)
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

// Computes the Fresnel term for a metallic surface using the "F82-tint"
// formulation presented in the paper "Novel aspects of the Adobe Standard
// Material" by Kutz et al, adapted for spectral rendering.
float SchlickFresnelMetal(float Base, float Specular, float CosTheta)
{
    const float CosThetaMax = 1 / 7.0f;

    float FSchlick = Base + (1 - Base) * pow(1 - CosTheta, 5);
    float FSchlickMax = Base + (1 - Base) * pow(1 - CosThetaMax, 5);
    float FMax = Specular * FSchlickMax;

    const float Denominator = CosThetaMax * pow(1 - CosThetaMax, 6);
    float Nominator = CosTheta * pow(1 - CosTheta, 6);
    return FSchlick - (Nominator / Denominator) * (FSchlickMax - FMax);
}

// Computes a wavelength-dependent refractive index according to
// the Cauchy empirical formula.
float CauchyEmpiricalIOR(float BaseIOR, float AbbeNumber, float Lambda)
{
    // Fraunhofer C, d, and F spectral lines.
    const float LC = 656.3;
    const float Ld = 587.6;
    const float LF = 486.1;

    float B = (BaseIOR - 1) / (AbbeNumber * (1.0 / (LF * LF) - 1.0 / (LC * LC)));
    float A = BaseIOR - B / (Ld * Ld);

    return A + B / (Lambda * Lambda);
}

// One half of the uncorrelated Smith shadowing-masking function for the
// anisotropic GGX distribution of normals as presented in the paper
// "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"
// by Eric Heitz.
float SmithG1(vec3 Direction, vec2 Roughness)
{
    vec3 DirectionSq = Direction * Direction;
    if (DirectionSq.z < EPSILON) return 0.0;
    vec2 RoughnessSq = Roughness * Roughness;
    float AlphaSqByTanThetaSq = dot(RoughnessSq, DirectionSq.xy) / DirectionSq.z;
    return 2.0 / (1.0 + sqrt(1.0 + AlphaSqByTanThetaSq));
}

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

// Sample a parametric reflectance spectrum at a given wavelength.
float SampleParametricSpectrum(vec3 Beta, float Lambda)
{
    float X = (Beta.x * Lambda + Beta.y) * Lambda + Beta.z;
    return 0.5 + X / (2.0 * sqrt(1.0 + X * X));
}

// Sample a parametric spectrum at a given wavelength.
float SampleParametricSpectrum(vec4 BetaAndIntensity, float Lambda)
{
    return BetaAndIntensity.w * SampleParametricSpectrum(BetaAndIntensity.xyz, Lambda);
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

/* --- Random -------------------------------------------------------------- */

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

/* --- Shader Per-Frame Uniforms ------------------------------------------- */

layout(binding = 0)
uniform frame_uniform_buffer
{
    uint                FrameRandomSeed;
    uint                ShapeCount;
    float               SceneScatterRate;
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
    mat3                SkyboxDistributionFrame;
    float               SkyboxDistributionConcentration;
    float               SkyboxBrightness;
    uint                SkyboxTextureIndex;
};