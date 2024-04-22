const float INFINITY    = 1e30f;
const float EPSILON     = 1e-9f;
const float PI          = 3.141592653f;
const float TAU         = 6.283185306f;

const uint RENDER_MODE_PATH_TRACE           = 0;
const uint RENDER_MODE_BASE_COLOR           = 1;
const uint RENDER_MODE_NORMAL               = 2;
const uint RENDER_MODE_MATERIAL_INDEX       = 3;
const uint RENDER_MODE_PRIMITIVE_INDEX      = 4;
const uint RENDER_MODE_EDIT                 = 1000;

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

const uint MATERIAL_FLAG_BASE_COLOR_TEXTURE = 1 << 0;
const uint MATERIAL_FLAG_EMISSION_TEXTURE   = 1 << 1;
const uint MATERIAL_FLAG_METALLIC_TEXTURE   = 1 << 2;
const uint MATERIAL_FLAG_ROUGHNESS_TEXTURE  = 1 << 3;

layout(binding = 0)
uniform FrameUniformBuffer
{
    uint            frameRandomSeed;
    uint            sceneObjectCount;
    uint            cameraModel;
    float           cameraFocalLength;
    float           cameraApertureRadius;
    float           cameraSensorDistance;
    vec2            cameraSensorSize;
    mat4            cameraWorldMatrix;
    uint            renderMode;
    uint            renderFlags;
    uint            renderBounceLimit;
    uint            highlightObjectIndex;
    uint            toneMappingMode;
    float           toneMappingWhiteLevel;
};

struct Material
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

struct Object
{
    uint            type;
    uint            materialIndex;
    uint            meshRootNodeIndex;
    mat4            worldToObjectMatrix;
    mat4            objectToWorldMatrix;
};

struct MeshFace
{
    vec3            position;
    uint            materialIndex;
    vec4            plane;
    vec3            base1;
    vec3            base2;
    vec3            normals[3];
    vec2            uvs[3];
};

struct MeshNode
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

    // Populated by ResolveHit()
    vec3            position;
    vec3            normal;
    vec2            uv;
    uint            materialIndex;
    Material        material;
};

struct Ray
{
    vec3            origin;
    vec3            direction;
};

Ray TransformRay(Ray ray, mat4 matrix)
{
    Ray r;
    r.origin = (matrix * vec4(ray.origin, 1)).xyz;
    r.direction = (matrix * vec4(ray.direction, 0)).xyz;
    return r;
}