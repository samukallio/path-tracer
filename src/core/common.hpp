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

constexpr float EPSILON = 1e-9f;
constexpr float PI = 3.141592653f;
constexpr float TAU = 6.283185306f;
constexpr float INF = std::numeric_limits<float>::infinity();

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

struct image
{
    uint Width;
    uint Height;
    vec4 const* Pixels;
};

struct transform
{
    vec3 Position = vec3(0, 0, 0);
    vec3 Rotation = vec3(0, 0, 0);
    vec3 Scale = vec3(1, 1, 1);
    bool ScaleIsUniform = true;
};

struct bounds
{
    vec3 Minimum = { +INF, +INF, +INF };
    vec3 Maximum = { -INF, -INF, -INF };
};

inline mat4 MakeTransformMatrix
(
    vec3 const& Position,
    vec3 const& Rotation
)
{
    return glm::translate(glm::mat4(1.0f), Position)
         * glm::eulerAngleZYX(Rotation.z, Rotation.y, Rotation.x);
}

inline mat4 MakeTransformMatrix
(
    vec3 const& Position,
    vec3 const& Rotation,
    vec3 const& Scale
)
{
    return glm::translate(glm::mat4(1.0f), Position)
         * glm::eulerAngleZYX(Rotation.z, Rotation.y, Rotation.x)
         * glm::scale(glm::mat4(1.0f), Scale);
}

inline float RepeatRange(float Value, float Min, float Max)
{
    float Range = Max - Min;
    return Min + Range * glm::fract((Value + Min) / Range);
}

inline vec2 SignNotZero(vec2 V)
{
    return vec2
    (
        V.x >= 0.0f ? +1.0f : -1.0f,
        V.y >= 0.0f ? +1.0f : -1.0f
    );
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
