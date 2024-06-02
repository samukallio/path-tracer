#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <format>

#define GLM_FORCE_SWIZZLE
#define GLM_FORCE_ALIGNED_GENTYPES
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/type_aligned.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>

float const EPSILON     = 1e-9f;
float const PI          = 3.141592653f;
float const TAU         = 6.283185306f;

uint32_t const TEXTURE_INDEX_NONE = 0xFFFFFFFF;

enum RenderMode : int32_t
{
    RENDER_MODE_PATH_TRACE              = 0,
    RENDER_MODE_BASE_COLOR              = 1,
    RENDER_MODE_BASE_COLOR_SHADED       = 2,
    RENDER_MODE_NORMAL                  = 3,
    RENDER_MODE_MATERIAL_INDEX          = 4,
    RENDER_MODE_PRIMITIVE_INDEX         = 5,
    RENDER_MODE_MESH_COMPLEXITY         = 6,
    RENDER_MODE_SCENE_COMPLEXITY        = 7,
    RENDER_MODE__COUNT                  = 8,
};

inline char const* RenderModeName(RenderMode mode)
{
    switch (mode) {
        case RENDER_MODE_PATH_TRACE:
            return "Path Trace";
        case RENDER_MODE_BASE_COLOR:
            return "Base Color";
        case RENDER_MODE_BASE_COLOR_SHADED:
            return "Base Color (Shaded)";
        case RENDER_MODE_NORMAL:
            return "Normal";
        case RENDER_MODE_MATERIAL_INDEX:
            return "Material ID";
        case RENDER_MODE_PRIMITIVE_INDEX:
            return "Primitive ID";
        case RENDER_MODE_MESH_COMPLEXITY:
            return "Mesh Complexity";
        case RENDER_MODE_SCENE_COMPLEXITY:
            return "Scene Complexity";
    }
    assert(false);
    return nullptr;
}

enum RenderFlag : uint32_t
{
    RENDER_FLAG_ACCUMULATE              = 1 << 0,
    RENDER_FLAG_SAMPLE_JITTER           = 1 << 1,
};

enum ToneMappingMode : int32_t
{
    TONE_MAPPING_MODE_CLAMP             = 0,
    TONE_MAPPING_MODE_REINHARD          = 1,
    TONE_MAPPING_MODE_HABLE             = 2,
    TONE_MAPPING_MODE_ACES              = 3,
    TONE_MAPPING_MODE__COUNT            = 4,
};

inline char const* ToneMappingModeName(ToneMappingMode mode)
{
    switch (mode) {
        case TONE_MAPPING_MODE_CLAMP:
            return "Clamp";
        case TONE_MAPPING_MODE_REINHARD:
            return "Reinhard";
        case TONE_MAPPING_MODE_HABLE:
            return "Hable";
        case TONE_MAPPING_MODE_ACES:
            return "ACES";
    }
    assert(false);
    return nullptr;
}

enum CameraModel : int32_t
{
    CAMERA_MODEL_PINHOLE                = 0,
    CAMERA_MODEL_THIN_LENS              = 1,
    CAMERA_MODEL_360                    = 2,
    CAMERA_MODEL__COUNT                 = 3,
};

inline char const* CameraModelName(CameraModel model)
{
    switch (model) {
        case CAMERA_MODEL_PINHOLE:
            return "Pinhole";
        case CAMERA_MODEL_THIN_LENS:
            return "Thin Lens";
        case CAMERA_MODEL_360:
            return "360";
    }
    assert(false);
    return nullptr;
}

enum ObjectType : int32_t
{
    OBJECT_TYPE_MESH_INSTANCE           = 0,
    OBJECT_TYPE_PLANE                   = 1,
    OBJECT_TYPE_SPHERE                  = 2,
    OBJECT_TYPE_CUBE                    = 3,
};

enum TextureFlag : uint32_t
{
    TEXTURE_FLAG_FILTER_NEAREST         = 1 << 0,
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct alignas(16) PackedTransform
{
    glm::aligned_mat4           to      = glm::mat4(1);
    glm::aligned_mat4           from    = glm::mat4(1);
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct alignas(16) PackedTexture
{
    glm::vec2                   atlasPlacementMinimum;
    glm::vec2                   atlasPlacementMaximum;
    uint32_t                    atlasImageIndex;
    uint32_t                    flags;
    uint32_t                    dummy1;
    uint32_t                    dummy2;
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct alignas(16) PackedMaterial
{
    glm::vec3                   baseColor;
    float                       baseWeight;
    glm::vec3                   specularColor;
    float                       specularWeight;
    glm::vec3                   transmissionColor;
    float                       transmissionWeight;
    glm::vec3                   transmissionScatter;
    float                       transmissionScatterAnisotropy;
    glm::vec3                   emissionColor;
    float                       emissionLuminance;

    glm::vec2                   specularRoughnessAlpha;

    float                       opacity;
    float                       baseMetalness;
    float                       baseDiffuseRoughness;
    float                       specularIOR;
    float                       transmissionDepth;
    float                       scatteringRate;

    uint32_t                    baseColorTextureIndex;
    uint32_t                    emissionColorTextureIndex;
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct alignas(16) PackedSceneObject
{
    ObjectType                  type;
    uint32_t                    materialIndex;
    uint32_t                    meshRootNodeIndex;
    int32_t                     priority;
    PackedTransform             transform;
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct alignas(16) PackedSceneNode
{
    glm::vec3                   minimum;
    uint32_t                    childNodeIndices;
    glm::vec3                   maximum;
    uint32_t                    objectIndex;
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct alignas(16) PackedMeshFace
{
    alignas(16) glm::vec3       position;
    alignas(16) glm::vec4       plane;
    alignas(16) glm::vec3       base1;
    alignas(16) glm::vec3       base2;
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct alignas(16) PackedMeshFaceExtra
{
    glm::aligned_vec3           normals[3];
    glm::aligned_vec2           uvs[3];
    uint32_t                    materialIndex;
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct alignas(16) PackedMeshNode
{

    glm::vec3                   minimum;
    uint32_t                    faceBeginOrNodeIndex;
    glm::vec3                   maximum;
    uint32_t                    faceEndIndex;
};

struct Image
{
    uint32_t                    width;
    uint32_t                    height;
    uint32_t const*             pixels;
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct FrameUniformBuffer
{
    uint32_t                    frameRandomSeed             = 0;
    uint32_t                    sceneObjectCount            = 0;
    float                       sceneScatterRate            = 0.0f;
    CameraModel                 cameraModel                 = CAMERA_MODEL_THIN_LENS;
    float                       cameraFocalLength           = 0.020f;
    float                       cameraApertureRadius        = 0.040f;
    float                       cameraSensorDistance        = 0.0202f;
    glm::aligned_vec2           cameraSensorSize            = { 0.032f, 0.018f };
    PackedTransform             cameraTransform             = {};
    RenderMode                  renderMode                  = RENDER_MODE_PATH_TRACE;
    uint32_t                    renderFlags                 = 0;
    uint32_t                    renderSampleBlockSize       = 1;
    uint32_t                    renderBounceLimit           = 0;
    uint32_t                    renderMeshComplexityScale   = 32;
    uint32_t                    renderSceneComplexityScale  = 32;
    uint32_t                    highlightObjectIndex        = 0xFFFFFFFF;
    float                       brightness                  = 1.0f;
    ToneMappingMode             toneMappingMode             = TONE_MAPPING_MODE_CLAMP;
    float                       toneMappingWhiteLevel       = 1.0f;
    glm::aligned_mat3           skyboxDistributionFrame     = {};
    float                       skyboxDistributionConcentration = 1.0f;
    float                       skyboxBrightness            = 1.0f;
    uint32_t                    skyboxWhiteFurnace          = 0;
};

struct Transform
{
    glm::vec3                   position                    = glm::vec3(0, 0, 0);
    glm::vec3                   rotation                    = glm::vec3(0, 0, 0);
    glm::vec3                   scale                       = glm::vec3(1, 1, 1);
    bool                        scaleIsUniform              = true;
};

struct Ray
{
    glm::vec3                   origin;
    glm::vec3                   direction;
};

inline Ray TransformRay(Ray const& ray, glm::mat4 const& matrix)
{
    return {
        .origin = (matrix * glm::vec4(ray.origin, 1)).xyz(),
        .direction = (matrix * glm::vec4(ray.direction, 0)).xyz(),
    };
}

struct Hit
{
    float                       time;
    ObjectType                  objectType;
    uint32_t                    objectIndex;
    uint32_t                    primitiveIndex;
    glm::vec3                   primitiveCoordinates;
};

inline float RepeatRange(float value, float min, float max)
{
    float range = max - min;
    return min + range * glm::fract((value + min) / range);
}
