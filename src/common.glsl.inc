const float INFINITY    = 1e30f;
const float EPSILON     = 1e-9f;
const float PI          = 3.141592653f;
const float TAU         = 6.283185306f;

const uint RENDER_MODE_PATH_TRACE       = 0;
const uint RENDER_MODE_ALBEDO           = 1;
const uint RENDER_MODE_NORMAL           = 2;
const uint RENDER_MODE_MATERIAL_INDEX   = 3;
const uint RENDER_MODE_PRIMITIVE_INDEX  = 4;

const uint TONE_MAPPING_MODE_CLAMP      = 0;
const uint TONE_MAPPING_MODE_REINHARD   = 1;
const uint TONE_MAPPING_MODE_HABLE      = 2;
const uint TONE_MAPPING_MODE_ACES       = 3;

const uint CAMERA_TYPE_PINHOLE          = 0;
const uint CAMERA_TYPE_THIN_LENS        = 1;

const uint OBJECT_TYPE_MESH             = 0;
const uint OBJECT_TYPE_PLANE            = 1;
const uint OBJECT_TYPE_SPHERE           = 2;

struct ToneMapping
{
    uint            mode;
    float           whiteLevel;
};

struct Camera
{
    uint            type;
    float           focalLength;
    float           apertureRadius;
    float           sensorDistance;
    vec2            sensorSize;
    mat4            worldMatrix;
};

layout(binding = 0)
uniform FrameUniformBuffer
{
    uint            frameIndex;
    uint            objectCount;
    uint            clearFrame;
    uint            renderMode;
    uint            bounceLimit;

    ToneMapping     toneMapping;
    Camera          camera;
};

struct Material
{
    vec3            albedoColor;
    uint            albedoTextureIndex;
    vec4            specularColor;
    vec3            emissiveColor;
    uint            emissiveTextureIndex;
    float           roughness;
    float           specularProbability;
    float           refractProbability;
    float           refractIndex;
    uvec2           albedoTextureSize;
};

struct Object
{
    vec3            origin;
    uint            type;
    vec3            scale;
    uint            materialIndex;
    uint            meshRootNodeIndex;
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

struct Ray
{
    vec3            origin;
    vec3            direction;
};

struct Hit
{
    float           time;
    uint            objectType;
    uint            objectIndex;
    uint            primitiveIndex;
    vec3            data;

    // Populated by ResolveHit()
    vec3            position;
    vec3            normal;
    vec2            uv;
    uint            materialIndex;
    Material        material;
};
