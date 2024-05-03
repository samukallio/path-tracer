const float INFINITY    = 1e30f;
const float EPSILON     = 1e-9f;
const float PI          = 3.141592653f;
const float TAU         = 6.283185306f;

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

const uint MATERIAL_FLAG_BASE_COLOR_TEXTURE = 1 << 0;
const uint MATERIAL_FLAG_EMISSION_TEXTURE   = 1 << 1;
const uint MATERIAL_FLAG_METALLIC_TEXTURE   = 1 << 2;
const uint MATERIAL_FLAG_ROUGHNESS_TEXTURE  = 1 << 3;

struct PackedTransform
{
    mat4            to;
    mat4            from;
};

struct PackedMaterial
{
    vec3            baseColor;
    uint            baseColorTextureIndex;
    vec3            emissionColor;
    uint            emissionColorTextureIndex;
    float           metallic;
    uint            metallicTextureIndex;
    float           roughness;
    uint            roughnessTextureIndex;
    float           refraction;
    float           refractionIndex;
    vec2            baseColorTextureMinimum;
    vec2            baseColorTextureMaximum;
    uint            flags;
};

struct PackedSceneObject
{
    uint            type;
    uint            materialIndex;
    uint            meshRootNodeIndex;
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
    uint            materialIndex;
    vec4            plane;
    vec3            base1;
    vec3            base2;
    vec3            normals[3];
    vec2            uvs[3];
};

struct PackedMeshNode
{
    vec3            minimum;
    uint            faceBeginOrNodeIndex;
    vec3            maximum;
    uint            faceEndIndex;
};

struct Hit
{
    float           time;
    uint            objectType;
    uint            objectIndex;
    uint            primitiveIndex;
    vec3            primitiveCoordinates;

    uint            sceneComplexity;
    uint            meshComplexity;

    // Populated by ResolveHit()
    vec3            position;
    vec3            normal;
    vec2            uv;
    uint            materialIndex;
    PackedMaterial  material;
    float           opacity;
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
};
