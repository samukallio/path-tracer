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

constexpr float EPSILON     = 1e-9f;
constexpr float PI          = 3.141592653f;
constexpr float TAU         = 6.283185306f;
constexpr float INF         = std::numeric_limits<float>::infinity();

uint32_t const SHAPE_INDEX_NONE     = 0xFFFFFFFF;
uint32_t const TEXTURE_INDEX_NONE   = 0xFFFFFFFF;

enum render_mode : int32_t
{
    RENDER_MODE_PATH_TRACE              = 0,
    RENDER_MODE_PATH_TRACE_SPECTRAL     = 1,
    RENDER_MODE_BASE_COLOR              = 2,
    RENDER_MODE_BASE_COLOR_SHADED       = 3,
    RENDER_MODE_NORMAL                  = 4,
    RENDER_MODE_MATERIAL_INDEX          = 5,
    RENDER_MODE_PRIMITIVE_INDEX         = 6,
    RENDER_MODE_MESH_COMPLEXITY         = 7,
    RENDER_MODE_SCENE_COMPLEXITY        = 8,
    RENDER_MODE__COUNT                  = 9,
};

enum render_flag : uint32_t
{
    RENDER_FLAG_ACCUMULATE              = 1 << 0,
    RENDER_FLAG_SAMPLE_JITTER           = 1 << 1,
};

enum tone_mapping_mode : int32_t
{
    TONE_MAPPING_MODE_CLAMP             = 0,
    TONE_MAPPING_MODE_REINHARD          = 1,
    TONE_MAPPING_MODE_HABLE             = 2,
    TONE_MAPPING_MODE_ACES              = 3,
    TONE_MAPPING_MODE__COUNT            = 4,
};

enum camera_model : int32_t
{
    CAMERA_MODEL_PINHOLE                = 0,
    CAMERA_MODEL_THIN_LENS              = 1,
    CAMERA_MODEL_360                    = 2,
    CAMERA_MODEL__COUNT                 = 3,
};

enum shape_type : int32_t
{
    SHAPE_TYPE_MESH_INSTANCE            = 0,
    SHAPE_TYPE_PLANE                    = 1,
    SHAPE_TYPE_SPHERE                   = 2,
    SHAPE_TYPE_CUBE                     = 3,
};

enum texture_flag : uint32_t
{
    TEXTURE_FLAG_FILTER_NEAREST         = 1 << 0,
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct alignas(16) packed_transform
{
    glm::aligned_mat4           To      = glm::mat4(1);
    glm::aligned_mat4           From    = glm::mat4(1);
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct alignas(16) packed_texture
{
    glm::vec2                   AtlasPlacementMinimum;
    glm::vec2                   AtlasPlacementMaximum;
    uint32_t                    AtlasImageIndex;
    uint32_t                    Flags;
    uint32_t                    Dummy1;
    uint32_t                    Dummy2;
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct alignas(16) packed_material
{
    glm::vec3                   BaseColor;
    float                       BaseWeight;
    glm::vec3                   SpecularColor;
    float                       SpecularWeight;
    glm::vec3                   TransmissionColor;
    float                       TransmissionWeight;
    glm::vec3                   TransmissionScatter;
    float                       TransmissionScatterAnisotropy;
    glm::vec3                   EmissionColor;
    float                       EmissionLuminance;

    float                       Opacity;
    float                       BaseMetalness;
    float                       BaseDiffuseRoughness;
    float                       SpecularIOR;
    float                       SpecularRoughness;
    float                       SpecularRoughnessAnisotropy;
    float                       TransmissionDepth;
    float                       ScatteringRate;

    uint32_t                    BaseColorTextureIndex;
    uint32_t                    SpecularRoughnessTextureIndex;
    uint32_t                    EmissionColorTextureIndex;
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct alignas(16) packed_shape
{
    shape_type                  Type;
    uint32_t                    MaterialIndex;
    uint32_t                    MeshRootNodeIndex;
    int32_t                     Priority;
    packed_transform            Transform;
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct alignas(16) packed_shape_node
{
    glm::vec3                   Minimum;
    uint32_t                    ChildNodeIndices;
    glm::vec3                   Maximum;
    uint32_t                    ShapeIndex;
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct alignas(16) packed_mesh_face
{
    alignas(16) glm::vec3       Position;
    alignas(16) glm::vec4       Plane;
    alignas(16) glm::vec3       Base1;
    alignas(16) glm::vec3       Base2;
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct alignas(16) packed_mesh_face_extra
{
    glm::quat                   Normals[3];
    glm::aligned_vec2           UVs[3];
    uint32_t                    MaterialIndex;
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct alignas(16) packed_mesh_node
{

    glm::vec3                   Minimum;
    uint32_t                    FaceBeginOrNodeIndex;
    glm::vec3                   Maximum;
    uint32_t                    FaceEndIndex;
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct frame_uniform_buffer
{
    uint32_t                    FrameRandomSeed             = 0;
    uint32_t                    ShapeCount                  = 0;
    float                       SceneScatterRate            = 0.0f;
    camera_model                CameraModel                 = CAMERA_MODEL_THIN_LENS;
    float                       CameraFocalLength           = 0.020f;
    float                       CameraApertureRadius        = 0.040f;
    float                       CameraSensorDistance        = 0.0202f;
    glm::aligned_vec2           CameraSensorSize            = { 0.032f, 0.018f };
    packed_transform            CameraTransform             = {};
    render_mode                 RenderMode                  = RENDER_MODE_PATH_TRACE;
    uint32_t                    RenderFlags                 = 0;
    uint32_t                    RenderSampleBlockSize       = 1;
    uint32_t                    RenderBounceLimit           = 0;
    float                       RenderTerminationProbability = 0.0f;
    uint32_t                    RenderMeshComplexityScale   = 32;
    uint32_t                    RenderSceneComplexityScale  = 32;
    uint32_t                    SelectedShapeIndex          = SHAPE_INDEX_NONE;
    float                       Brightness                  = 1.0f;
    tone_mapping_mode           ToneMappingMode             = TONE_MAPPING_MODE_CLAMP;
    float                       ToneMappingWhiteLevel       = 1.0f;
    glm::aligned_mat3           SkyboxDistributionFrame     = {};
    float                       SkyboxDistributionConcentration = 1.0f;
    float                       SkyboxBrightness            = 1.0f;
    uint32_t                    SkyboxWhiteFurnace          = 0;
};

struct image
{
    uint32_t                    Width;
    uint32_t                    Height;
    uint32_t const*             Pixels;
};

struct transform
{
    glm::vec3                   Position                    = glm::vec3(0, 0, 0);
    glm::vec3                   Rotation                    = glm::vec3(0, 0, 0);
    glm::vec3                   Scale                       = glm::vec3(1, 1, 1);
    bool                        ScaleIsUniform              = true;
};

struct bounds
{
    glm::vec3                   Minimum                     = { +INF, +INF, +INF };
    glm::vec3                   Maximum                     = { -INF, -INF, -INF };
};

struct ray
{
    glm::vec3                   Origin;
    glm::vec3                   Vector;
};

struct hit
{
    float                       Time;
    shape_type                  ShapeType;
    uint32_t                    ShapeIndex;
    uint32_t                    PrimitiveIndex;
    glm::vec3                   PrimitiveCoordinates;
};

inline char const* RenderModeName(render_mode Mode)
{
    switch (Mode) {
    case RENDER_MODE_PATH_TRACE:            return "Path Trace";
    case RENDER_MODE_PATH_TRACE_SPECTRAL:   return "Path Trace (Spectral)";
    case RENDER_MODE_BASE_COLOR:            return "Base Color";
    case RENDER_MODE_BASE_COLOR_SHADED:     return "Base Color (Shaded)";
    case RENDER_MODE_NORMAL:                return "Normal";
    case RENDER_MODE_MATERIAL_INDEX:        return "Material ID";
    case RENDER_MODE_PRIMITIVE_INDEX:       return "Primitive ID";
    case RENDER_MODE_MESH_COMPLEXITY:       return "Mesh Complexity";
    case RENDER_MODE_SCENE_COMPLEXITY:      return "Scene Complexity";
    }
    assert(false);
    return nullptr;
}

inline char const* ToneMappingModeName(tone_mapping_mode Mode)
{
    switch (Mode) {
    case TONE_MAPPING_MODE_CLAMP:           return "Clamp";
    case TONE_MAPPING_MODE_REINHARD:        return "Reinhard";
    case TONE_MAPPING_MODE_HABLE:           return "Hable";
    case TONE_MAPPING_MODE_ACES:            return "ACES";
    }
    assert(false);
    return nullptr;
}

inline char const* CameraModelName(camera_model Model)
{
    switch (Model) {
    case CAMERA_MODEL_PINHOLE:              return "Pinhole";
    case CAMERA_MODEL_THIN_LENS:            return "Thin Lens";
    case CAMERA_MODEL_360:                  return "360";
    }
    assert(false);
    return nullptr;
}

inline ray TransformRay(ray const& Ray, glm::mat4 const& Matrix)
{
    return {
        .Origin = (Matrix * glm::vec4(Ray.Origin, 1)).xyz(),
        .Vector = (Matrix * glm::vec4(Ray.Vector, 0)).xyz(),
    };
}

inline float RepeatRange(float Value, float Min, float Max)
{
    float Range = Max - Min;
    return Min + Range * glm::fract((Value + Min) / Range);
}
