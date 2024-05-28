const float INFINITY    = 1e30f;
const float EPSILON     = 1e-9f;
const float PI          = 3.141592653f;
const float TAU         = 6.283185306f;

const uint OBJECT_INDEX_NONE                = 0xFFFFFFFF;

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

const uint OBJECT_TYPE_MESH_INSTANCE        = 0;
const uint OBJECT_TYPE_PLANE                = 1;
const uint OBJECT_TYPE_SPHERE               = 2;
const uint OBJECT_TYPE_CUBE                 = 3;

const uint MATERIAL_FLAG_BASE_COLOR_TEXTURE                 = 1 << 0;
const uint MATERIAL_FLAG_BASE_COLOR_TEXTURE_FILTER_NEAREST  = 1 << 1;
const uint MATERIAL_FLAG_EMISSION_TEXTURE                   = 1 << 2;
const uint MATERIAL_FLAG_EMISSION_TEXTURE_FILTER_NEAREST    = 1 << 3;
const uint MATERIAL_FLAG_METALLIC_TEXTURE                   = 1 << 4;
const uint MATERIAL_FLAG_METALLIC_TEXTURE_FILTER_NEAREST    = 1 << 5;
const uint MATERIAL_FLAG_ROUGHNESS_TEXTURE                  = 1 << 6;
const uint MATERIAL_FLAG_ROUGHNESS_TEXTURE_FILTER_NEAREST   = 1 << 7;


struct PackedTransform
{
    mat4            to;
    mat4            from;
};

struct PackedMaterial
{
    vec3            baseColor;
    float           baseWeight;
    vec3            specularColor;
    float           specularWeight;
    vec3            transmissionColor;
    float           transmissionWeight;
    vec3            transmissionScatter;
    float           transmissionScatterAnisotropy;
    vec3            emissionColor;
    float           emissionLuminance;

    float           opacity;
    float           baseMetalness;
    float           baseDiffuseRoughness;
    float           specularRoughness;
    float           specularRoughnessAnisotropy;
    float           specularIOR;
    float           transmissionDepth;
    float           scatteringRate;

    uint            baseColorTextureIndex;
    uint            emissionColorTextureIndex;

    vec2            baseColorTextureMinimum;
    vec2            baseColorTextureMaximum;

    uint            flags;
};

struct PackedSceneObject
{
    uint            type;
    uint            materialIndex;
    uint            meshRootNodeIndex;
    uint            priority;
    PackedTransform transform;
};

struct PackedSceneNode
{
    vec3            minimum;
    uint            childNodeIndices;
    vec3            maximum;
    uint            objectIndex;
};

struct PackedMeshFace
{
    vec3            position;
    vec4            plane;
    vec3            base1;
    vec3            base2;
};

struct PackedMeshFaceExtra
{
    vec3            normals[3];
    vec2            uvs[3];
    uint            materialIndex;
};

struct PackedMeshNode
{
    vec3            minimum;
    uint            faceBeginOrNodeIndex;
    vec3            maximum;
    uint            faceEndIndex;
};

struct Medium
{
    uint            objectIndex;
    uint            objectPriority;
    float           specularIOR;
    float           scatteringRate;
};

struct Hit
{
    // Populated by the Intersect*() routines.
    float           time;
    uint            objectType;
    uint            objectIndex;
    uint            primitiveIndex;
    vec3            primitiveCoordinates;
    uint            sceneComplexity;
    uint            meshComplexity;

    // Populated by ResolveHit().
    uint            objectPriority;
    vec3            position;
    vec3            normal;
    vec3            tangentX;
    vec3            tangentY;
    vec2            uv;
    uint            materialIndex;
    PackedMaterial  material;
};

struct Ray
{
    vec3            origin;
    vec3            vector;
};

vec3 TransformPosition(vec3 position, PackedTransform transform)
{
    return (transform.to * vec4(position, 1)).xyz;
}

vec3 TransformVector(vec3 vector, PackedTransform transform)
{
    return (transform.to * vec4(vector, 0)).xyz;
}

vec3 TransformNormal(vec3 normal, PackedTransform transform)
{
    return normalize((vec4(normal, 0) * transform.from).xyz);
}

Ray TransformRay(Ray ray, PackedTransform transform)
{
    Ray r;
    r.origin = TransformPosition(ray.origin, transform);
    r.vector = TransformVector(ray.vector, transform);
    return r;
}

vec3 InverseTransformPosition(vec3 position, PackedTransform transform)
{
    return (transform.from * vec4(position, 1)).xyz;
}

vec3 InverseTransformVector(vec3 vector, PackedTransform transform)
{
    return (transform.from * vec4(vector, 0)).xyz;
}

vec3 InverseTransformNormal(vec3 normal, PackedTransform transform)
{
    return normalize((vec4(normal, 0) * transform.to).xyz);
}

Ray InverseTransformRay(Ray ray, PackedTransform transform)
{
    Ray r;
    r.origin = InverseTransformPosition(ray.origin, transform);
    r.vector = InverseTransformVector(ray.vector, transform);
    return r;
}

// Compute an arbitrary tangent space basis for a given normal vector.
void ComputeTangentVectors(vec3 normal, out vec3 tangentX, out vec3 tangentY)
{
    vec3 nonparallel = abs(normal.x) < 0.9 ? vec3(1, 0, 0) : vec3(0, 1, 0);
    tangentX = normalize(cross(nonparallel, normal));
    tangentY = cross(tangentX, normal);
}

// Sample a microsurface normal from the GGX distribution.
vec3 SampleNormalGGX(float alpha, float xi0, float xi1)
{
    float theta = atan(alpha * sqrt(xi0) / (1 - sqrt(xi0)));
    float phi = TAU * xi1;
    return vec3(
        sin(theta) * cos(phi),
        sin(theta) * sin(phi),
        cos(theta));
}

float SchlickFresnelDielectric(float relativeIOR, float cosTheta)
{
    float r0 = pow((1 - relativeIOR) / (1 + relativeIOR), 2);
    return r0 + (1 - r0) * pow(1 - cosTheta, 5);
}

vec3 SchlickFresnelMetal(vec3 f0, float cosTheta)
{
    return f0 + (1 - f0) * pow(1 - cosTheta, 5);
}

// Computes the Fresnel term for a metallic surface using the "F82-tint"
// formulation presented in the paper "Novel aspects of the Adobe Standard
// Material" by Kutz et al.
vec3 SchlickFresnelMetalWithTint(vec3 f0, vec3 f82tint, float cosTheta)
{
    const float cosThetaMax = 1 / 7.0f;
    const float denominator = cosThetaMax * pow(1 - cosThetaMax, 6);
    float nominator = cosTheta * pow(1 - cosTheta, 6);
    float factor = nominator / denominator;
    vec3 base = SchlickFresnelMetal(f0, cosTheta);
    vec3 offset = factor * (1 - f82tint) * SchlickFresnelMetal(f0, cosThetaMax);
    return base - offset;
}

// One half of the Smith masking-shadowing function.
float SmithG1(vec3 omega, float alpha)
{
    float cosThetaSq = omega.z * omega.z;
    float tanThetaSq = (1 - cosThetaSq) / cosThetaSq;
    //float cosPhiSq = omega.x * omega.x;
    //float sinPhiSq = omega.y * omega.y;
    //float alphaSq = (cosPhiSq * alphaSq0.x + sinPhiSq * alphaSq0.y) / (cosPhiSq + sinPhiSq);
    float alphaSq = alpha * alpha;
    return 2.0 / (1.0 + sqrt(1.0 + alphaSq * tanThetaSq));
}

float SmithG(vec3 omegaIn, vec3 omegaOut, float alpha)
{
    return SmithG1(omegaIn, alpha) * SmithG1(omegaOut, alpha);
}

/* --- Random -------------------------------------------------------------- */

uint randomState;

uint Random()
{
    randomState = randomState * 747796405u + 2891336453u;
    uint s = randomState;
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
    float r = sqrt(Random0To1());
    float theta = Random0To1() * TAU;
    return r * vec2(cos(theta), sin(theta));
}

vec3 RandomDirection()
{
    float z = 2 * Random0To1() - 1;
    float r = sqrt(1 - z * z);
    float phi = TAU * Random0To1();
    return vec3(r * cos(phi), r * sin(phi), z);
}

vec3 RandomHemisphereDirection(vec3 normal)
{
    vec3 direction = RandomDirection();
    return direction * sign(dot(normal, direction));
}

// Generate a random direction from a von Mises-Fisher distribution on
// a sphere, with concentration parameter kappa and mean direction (0,0,1).
vec3 RandomVonMisesFisher(float kappa)
{
    float xi = Random0To1();
    float z = 1 + (1 / kappa) * log(xi - exp(-2 * kappa) * (xi - 1));

    float r = sqrt(1 - z * z);
    float phi = Random0To1() * TAU;
    float x = r * cos(phi);
    float y = r * sin(phi);

    return vec3(x, y, z);
}

/* --- Shader Per-Frame Uniforms ------------------------------------------- */

layout(binding = 0)
uniform FrameUniformBuffer
{
    uint            frameRandomSeed;
    uint            sceneObjectCount;
    float           sceneScatterRate;
    uint            cameraModel;
    float           cameraFocalLength;
    float           cameraApertureRadius;
    float           cameraSensorDistance;
    vec2            cameraSensorSize;
    PackedTransform cameraTransform;
    uint            renderMode;
    uint            renderFlags;
    uint            renderSampleBlockSize;
    uint            renderBounceLimit;
    uint            renderMeshComplexityScale;
    uint            renderSceneComplexityScale;
    uint            highlightObjectIndex;
    float           brightness;
    uint            toneMappingMode;
    float           toneMappingWhiteLevel;
    mat3            skyboxDistributionFrame;
    float           skyboxDistributionConcentration;
    float           skyboxBrightness;
};
