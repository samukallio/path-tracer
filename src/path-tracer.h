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
