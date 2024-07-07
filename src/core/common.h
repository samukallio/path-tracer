#pragma once

#include <algorithm>
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <vector>

#define GLM_FORCE_SWIZZLE
#define GLM_FORCE_ALIGNED_GENTYPES
#define GLM_ENABLE_EXPERIMENTAL

#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>
#include <glm/gtc/type_aligned.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>

constexpr float EPSILON     = 1e-9f;
constexpr float PI          = 3.141592653f;
constexpr float TAU         = 6.283185306f;
constexpr float INF         = std::numeric_limits<float>::infinity();

constexpr float CIE_LAMBDA_MIN = 360.0f;
constexpr float CIE_LAMBDA_MAX = 830.0f;

using uint = uint32_t;
using glm::vec2;
using glm::vec3;
using glm::vec4;
using glm::mat3;
using glm::mat4;
using glm::aligned_vec2;
using glm::aligned_vec3;
using glm::aligned_vec4;
using glm::aligned_mat3;
using glm::aligned_mat4;

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
};

enum tone_mapping_mode : int32_t
{
    TONE_MAPPING_MODE_CLAMP             = 0,
    TONE_MAPPING_MODE_REINHARD          = 1,
    TONE_MAPPING_MODE_HABLE             = 2,
    TONE_MAPPING_MODE_ACES              = 3,
    TONE_MAPPING_MODE__COUNT            = 4,
};

struct image
{
    uint        Width;
    uint        Height;
    vec4 const* Pixels;
};

struct transform
{
    vec3        Position        = vec3(0, 0, 0);
    vec3        Rotation        = vec3(0, 0, 0);
    vec3        Scale           = vec3(1, 1, 1);
    bool        ScaleIsUniform  = true;
};

struct bounds
{
    vec3        Minimum         = { +INF, +INF, +INF };
    vec3        Maximum         = { -INF, -INF, -INF };
};

struct ray
{
    vec3        Origin;
    vec3        Vector;
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

inline ray TransformRay(ray const& Ray, mat4 const& Matrix)
{
    return {
        .Origin = (Matrix * vec4(Ray.Origin, 1)).xyz(),
        .Vector = (Matrix * vec4(Ray.Vector, 0)).xyz(),
    };
}

inline float RepeatRange(float Value, float Min, float Max)
{
    float Range = Max - Min;
    return Min + Range * glm::fract((Value + Min) / Range);
}

inline vec2 SignNotZero(vec2 V)
{
    return vec2(
        V.x >= 0.0f ? +1.0f : -1.0f,
        V.y >= 0.0f ? +1.0f : -1.0f);
}

// Packs a unit vector into a single 32-bit value.
inline uint PackUnitVector(vec3 V)
{
    vec2 P = V.xy * (1.0f / (abs(V.x) + abs(V.y) + abs(V.z)));
    if (V.z <= 0.0f) P = (1.0f - glm::abs(P.yx())) * SignNotZero(P);
    return glm::packSnorm2x16(P);
}

// Unpacks a unit vector from a single 32-bit value.
inline vec3 UnpackUnitVector(uint PackedV)
{
    vec2 P = glm::unpackSnorm2x16(PackedV);
    float Z = 1.0f - glm::abs(P.x) - glm::abs(P.y);
    if (Z < 0.0f) P = (1.0f - glm::abs(P.yx())) * SignNotZero(P);
    return glm::normalize(glm::vec3(P, Z));
}