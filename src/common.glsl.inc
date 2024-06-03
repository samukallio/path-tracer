const float INFINITY    = 1e30f;
const float EPSILON     = 1e-9f;
const float PI          = 3.141592653f;
const float TAU         = 6.283185306f;

const uint OBJECT_INDEX_NONE                = 0xFFFFFFFF;
const uint TEXTURE_INDEX_NONE               = 0xFFFFFFFF;

const uint RENDER_MODE_PATH_TRACE           = 0;
const uint RENDER_MODE_PATH_TRACE_SPECTRAL  = 1;
const uint RENDER_MODE_BASE_COLOR           = 2;
const uint RENDER_MODE_BASE_COLOR_SHADED    = 3;
const uint RENDER_MODE_NORMAL               = 4;
const uint RENDER_MODE_MATERIAL_INDEX       = 5;
const uint RENDER_MODE_PRIMITIVE_INDEX      = 6;
const uint RENDER_MODE_MESH_COMPLEXITY      = 7;
const uint RENDER_MODE_SCENE_COMPLEXITY     = 8;

const uint RENDER_FLAG_ACCUMULATE           = 1 << 0;
const uint RENDER_FLAG_SAMPLE_JITTER        = 1 << 1;

const uint TONE_MAPPING_MODE_CLAMP          = 0;
const uint TONE_MAPPING_MODE_REINHARD       = 1;
const uint TONE_MAPPING_MODE_HABLE          = 2;
const uint TONE_MAPPING_MODE_ACES           = 3;

const uint CAMERA_MODEL_PINHOLE             = 0;
const uint CAMERA_MODEL_THIN_LENS           = 1;
const uint CAMERA_MODEL_360                 = 2;

const uint OBJECT_TYPE_MESH_INSTANCE        = 0;
const uint OBJECT_TYPE_PLANE                = 1;
const uint OBJECT_TYPE_SPHERE               = 2;
const uint OBJECT_TYPE_CUBE                 = 3;

const uint TEXTURE_FLAG_FILTER_NEAREST      = 1 << 0;

struct packed_transform
{
    mat4            To;
    mat4            From;
};

struct packed_texture
{
    vec2            AtlasPlacementMinimum;
    vec2            AtlasPlacementMaximum;
    uint            AtlasImageIndex;
    uint            Flags;
    uint            Dummy1;
    uint            Dummy2;
};

struct packed_material
{
    vec3            BaseColor;
    float           BaseWeight;
    vec3            SpecularColor;
    float           SpecularWeight;
    vec3            TransmissionColor;
    float           TransmissionWeight;
    vec3            TransmissionScatter;
    float           TransmissionScatterAnisotropy;
    vec3            EmissionColor;
    float           EmissionLuminance;

    vec2            SpecularRoughnessAlpha;

    float           Opacity;
    float           BaseMetalness;
    float           BaseDiffuseRoughness;
    float           SpecularIOR;
    float           TransmissionDepth;
    float           ScatteringRate;

    uint            BaseColorTextureIndex;
    uint            EmissionColorTextureIndex;
};

struct packed_scene_object
{
    uint                Type;
    uint                MaterialIndex;
    uint                MeshRootNodeIndex;
    uint                Priority;
    packed_transform    Transform;
};

struct packed_scene_node
{
    vec3            Minimum;
    uint            ChildNodeIndices;
    vec3            Maximum;
    uint            ObjectIndex;
};

struct packed_mesh_face
{
    vec3            Position;
    vec4            Plane;
    vec3            Base1;
    vec3            Base2;
};

struct packed_mesh_face_extra
{
    vec3            Normals[3];
    vec2            UVs[3];
    uint            MaterialIndex;
};

struct packed_mesh_node
{
    vec3            Minimum;
    uint            FaceBeginOrNodeIndex;
    vec3            Maximum;
    uint            FaceEndIndex;
};

struct medium
{
    uint            ObjectIndex;
    uint            ObjectPriority;
    float           SpecularIOR;
    float           ScatteringRate;
};

struct hit
{
    // Populated by the Intersect*() routines.
    float           Time;
    uint            ObjectType;
    uint            ObjectIndex;
    uint            PrimitiveIndex;
    vec3            PrimitiveCoordinates;
    uint            SceneComplexity;
    uint            MeshComplexity;

    // Populated by ResolveHit().
    uint            ObjectPriority;
    vec3            Position;
    vec3            Normal;
    vec3            TangentX;
    vec3            TangentY;
    vec2            UV;
    uint            MaterialIndex;
    packed_material Material;
};

struct ray
{
    vec3            Origin;
    vec3            Vector;
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

// Compute an arbitrary tangent space basis for a given normal vector.
void ComputeTangentVectors(vec3 Normal, out vec3 TangentX, out vec3 TangentY)
{
    vec3 NonParallel = abs(Normal.x) < 0.9 ? vec3(1, 0, 0) : vec3(0, 1, 0);
    TangentX = normalize(cross(NonParallel, Normal));
    TangentY = cross(TangentX, Normal);
}

// Sample a normal from the GGX distribution that is visible from the given
// direction according to the procedure outlined in the paper "Sampling the
// GGX Distribution of Visible Normals" by Eric Heitz.
vec3 SampleVisibleNormalGGX(vec3 Direction, vec2 Roughness, float U1, float U2)
{
    vec3 vz = normalize(vec3(
        Roughness.x * Direction.x,
        Roughness.y * Direction.y,
        Direction.z));

    float lensq = dot(vz.xy, vz.xy);
    vec3 vx = lensq > 0 ? vec3(-vz.y, vz.x, 0) / sqrt(lensq) : vec3(1, 0, 0);
    vec3 vy = cross(vz, vx);

    float r = sqrt(U1);
    float phi = TAU * U2;
    float s = 0.5 * (1.0 + vz.z);

    float tx = r * cos(phi);
    float ty = (1.0 - s) * sqrt(1.0 - tx * tx) + s * r * sin(phi);
    float tz = sqrt(max(0.0, 1.0 - tx * tx - ty * ty));

    vec3 Normal = tx * vx + ty * vy + tz * vz;

    return normalize(vec3(
        Roughness.x * Normal.x,
        Roughness.y * Normal.y,
        max(0.0, Normal.z)));
}

float SchlickFresnelDielectric(float F0, float CosTheta)
{
    return F0 + (1 - F0) * pow(1 - CosTheta, 5);
}

vec3 SchlickFresnelMetal(vec3 F0, float CosTheta)
{
    return F0 + (1 - F0) * pow(1 - CosTheta, 5);
}

// Computes the Fresnel term for a metallic surface using the "F82-tint"
// formulation presented in the paper "Novel aspects of the Adobe Standard
// Material" by Kutz et al.
vec3 SchlickFresnelMetalWithTint(vec3 F0, vec3 F82Tint, float CosTheta)
{
    const float CosThetaMax = 1 / 7.0f;
    const float Denominator = CosThetaMax * pow(1 - CosThetaMax, 6);
    float Nominator = CosTheta * pow(1 - CosTheta, 6);
    float Factor = Nominator / Denominator;
    vec3 Base = SchlickFresnelMetal(F0, CosTheta);
    vec3 Offset = Factor * (1 - F82Tint) * SchlickFresnelMetal(F0, CosThetaMax);
    return Base - Offset;
}

// One half of the uncorrelated Smith shadowing-masking function for the
// anisotropic GGX distribution of normals as presented in the paper
// "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"
// by Eric Heitz.
float SmithG1(vec3 Direction, vec2 Roughness)
{
    vec2 RoughnessSq = Roughness * Roughness;
    float CosPhiSq = Direction.x * Direction.x;
    float SinPhiSq = Direction.y * Direction.y;
    float CosThetaSq = Direction.z * Direction.z;
    float TanThetaSq = (1 - CosThetaSq) / CosThetaSq;
    float AlphaSq = (CosPhiSq * RoughnessSq.x + SinPhiSq * RoughnessSq.y) / (CosPhiSq + SinPhiSq);
    return 2.0 / (1.0 + sqrt(1.0 + AlphaSq * TanThetaSq));
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
    uint                SceneObjectCount;
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
    uint                HighlightObjectIndex;
    float               Brightness;
    uint                ToneMappingMode;
    float               ToneMappingWhiteLevel;
    mat3                SkyboxDistributionFrame;
    float               SkyboxDistributionConcentration;
    float               SkyboxBrightness;
    uint                SkyboxWhiteFurnace;
};
