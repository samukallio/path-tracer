#pragma once

#define GLM_FORCE_ALIGNED_GENTYPES
#include <glm/glm.hpp>
#include <glm/gtc/type_aligned.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

enum RenderMode : int32_t
{
    RENDER_MODE_PATH_TRACE      = 0,
    RENDER_MODE_ALBEDO          = 1,
    RENDER_MODE_NORMAL          = 2,
    RENDER_MODE_MATERIAL_INDEX  = 3,
    RENDER_MODE_PRIMITIVE_INDEX = 4,
};

enum ToneMappingMode : int32_t
{
    TONE_MAPPING_MODE_CLAMP     = 0,
    TONE_MAPPING_MODE_REINHARD  = 1,
    TONE_MAPPING_MODE_HABLE     = 2,
    TONE_MAPPING_MODE_ACES      = 3,
};

enum CameraType : int32_t
{
    CAMERA_TYPE_PINHOLE         = 0,
    CAMERA_TYPE_THIN_LENS       = 1,
    CAMERA_TYPE_360             = 2,
};

enum ObjectType : int32_t
{
    OBJECT_TYPE_MESH            = 0,
    OBJECT_TYPE_PLANE           = 1,
    OBJECT_TYPE_SPHERE          = 2,
};

struct Camera
{
    CameraType                  type                        = CAMERA_TYPE_THIN_LENS;
    float                       focalLength                 = 0.020f;
    float                       apertureRadius              = 0.040f;
    float                       sensorDistance              = 0.0202f;
    glm::vec2                   sensorSize                  = { 0.032f, 0.018f };
    glm::aligned_mat4           worldMatrix                 = {};
};

struct ToneMapping
{
    ToneMappingMode             mode                        = TONE_MAPPING_MODE_CLAMP;
    float                       whiteLevel                  = 1.0f;
};

struct Material
{
    glm::vec3                   albedoColor;
    uint32_t                    albedoTextureIndex;
    glm::vec4                   specularColor;
    glm::vec3                   emissiveColor;
    uint32_t                    emissiveTextureIndex;
    float                       roughness;
    float                       specularProbability;
    float                       refractProbability;
    float                       refractIndex;
    glm::uvec2                  albedoTextureSize;
    glm::uvec2                  padding;
};

struct Object
{
    glm::vec3                   origin;
    uint32_t                    type;
    glm::vec3                   scale;
    uint32_t                    materialIndex;
    uint32_t                    meshRootNodeIndex;
    uint32_t                    dummy[3];
};

struct MeshFace
{
    glm::vec3                   position;
    uint32_t                    materialIndex;
    glm::vec4                   plane;
    glm::aligned_vec3           base1;
    glm::aligned_vec3           base2;
    glm::aligned_vec3           normals[3];
    glm::aligned_vec2           uvs[3];
};

struct MeshNode
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
    void const*                 pixels;
};

struct FrameUniformBuffer
{
    uint32_t                    frameIndex                  = 0;
    uint32_t                    objectCount                 = {};
    uint32_t                    clearFrame                  = 0;
    RenderMode                  renderMode                  = RENDER_MODE_PATH_TRACE;
    uint32_t                    bounceLimit                 = 0;

    alignas(16) ToneMapping     toneMapping                 = {};
    alignas(16) Camera          camera                      = {};
};
