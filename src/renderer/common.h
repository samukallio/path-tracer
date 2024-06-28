#pragma once

#include "path-tracer.h"

uint const SHAPE_INDEX_NONE     = 0xFFFFFFFF;
uint const TEXTURE_INDEX_NONE   = 0xFFFFFFFF;

enum render_mode : int32_t
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

enum render_flag : uint
{
    RENDER_FLAG_ACCUMULATE              = 1 << 0,
    RENDER_FLAG_SAMPLE_JITTER           = 1 << 1,
    RENDER_FLAG_RESET                   = 1 << 2,
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

enum texture_type
{
    TEXTURE_TYPE_RAW                    = 0,
    TEXTURE_TYPE_REFLECTANCE_WITH_ALPHA = 1,
    TEXTURE_TYPE_RADIANCE               = 2,
    TEXTURE_TYPE__COUNT                 = 3,
};

enum texture_flag : uint
{
    TEXTURE_FLAG_FILTER_NEAREST         = 1 << 0,
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct alignas(16) packed_transform
{
    aligned_mat4        To      = mat4(1);
    aligned_mat4        From    = mat4(1);
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct alignas(16) packed_texture
{
    vec2                AtlasPlacementMinimum;
    vec2                AtlasPlacementMaximum;
    uint                AtlasImageIndex;
    uint                Type;
    uint                Flags;
    uint                Unused0;
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct alignas(16) packed_material
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

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct alignas(16) packed_shape
{
    shape_type          Type;
    uint                MaterialIndex;
    uint                MeshRootNodeIndex;
    int                 Priority;
    packed_transform    Transform;
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct alignas(16) packed_shape_node
{
    vec3                Minimum;
    uint                ChildNodeIndices;
    vec3                Maximum;
    uint                ShapeIndex;
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct alignas(16) packed_mesh_face
{
    alignas(16) vec3    Position;
    alignas(16) vec4    Plane;
    alignas(16) vec3    Base1;
    alignas(16) vec3    Base2;
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct alignas(16) packed_mesh_face_extra
{
    aligned_vec3        Normals[3];
    aligned_vec2        UVs[3];
    uint                MaterialIndex;
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct alignas(16) packed_mesh_node
{

    vec3                Minimum;
    uint                FaceBeginOrNodeIndex;
    vec3                Maximum;
    uint                FaceEndIndex;
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct frame_uniform_buffer
{
    uint                FrameRandomSeed                 = 0;
    uint                ShapeCount                      = 0;
    float               SceneScatterRate                = 0.0f;
    camera_model        CameraModel                     = CAMERA_MODEL_THIN_LENS;
    float               CameraFocalLength               = 0.020f;
    float               CameraApertureRadius            = 0.040f;
    float               CameraSensorDistance            = 0.0202f;
    aligned_vec2        CameraSensorSize                = { 0.032f, 0.018f };
    packed_transform    CameraTransform                 = {};
    render_mode         RenderMode                      = RENDER_MODE_PATH_TRACE;
    uint                RenderFlags                     = 0;
    uint                RenderSampleBlockSize           = 1;
    uint                RenderBounceLimit               = 0;
    float               RenderTerminationProbability    = 0.0f;
    uint                RenderMeshComplexityScale       = 32;
    uint                RenderSceneComplexityScale      = 32;
    uint                SelectedShapeIndex              = SHAPE_INDEX_NONE;
    float               Brightness                      = 1.0f;
    tone_mapping_mode   ToneMappingMode                 = TONE_MAPPING_MODE_CLAMP;
    float               ToneMappingWhiteLevel           = 1.0f;
    aligned_mat3        SkyboxDistributionFrame         = {};
    float               SkyboxDistributionConcentration = 1.0f;
    float               SkyboxBrightness                = 1.0f;
    uint                SkyboxTextureIndex              = TEXTURE_INDEX_NONE;
};

struct image
{
    uint                Width;
    uint                Height;
    vec4 const*         Pixels;
};

struct transform
{
    vec3                Position                        = vec3(0, 0, 0);
    vec3                Rotation                        = vec3(0, 0, 0);
    vec3                Scale                           = vec3(1, 1, 1);
    bool                ScaleIsUniform                  = true;
};

struct bounds
{
    vec3                Minimum                         = { +INF, +INF, +INF };
    vec3                Maximum                         = { -INF, -INF, -INF };
};

struct ray
{
    vec3                Origin;
    vec3                Vector;
};

struct hit
{
    float               Time;
    shape_type          ShapeType;
    uint                ShapeIndex;
    uint                PrimitiveIndex;
    vec3                PrimitiveCoordinates;
};

inline char const* RenderModeName(render_mode Mode)
{
    switch (Mode) {
    case RENDER_MODE_PATH_TRACE:            return "Path Trace";
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

inline char const* TextureTypeName(texture_type Type)
{
    switch (Type) {
    case TEXTURE_TYPE_RAW:                  return "Raw";
    case TEXTURE_TYPE_REFLECTANCE_WITH_ALPHA: return "Reflectance (with alpha)";
    case TEXTURE_TYPE_RADIANCE:             return "Radiance";
    }
    assert(false);
    return nullptr;
}

inline ray TransformRay(ray const& Ray, mat4 const& Matrix)
{
    return {
        .Origin = (Matrix * vec4     (Ray.Origin, 1)).xyz(),
        .Vector = (Matrix * vec4     (Ray.Vector, 0)).xyz(),
    };
}

inline float RepeatRange(float Value, float Min, float Max)
{
    float Range = Max - Min;
    return Min + Range * glm::fract((Value + Min) / Range);
}
