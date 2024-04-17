#pragma once

#include "common.h"

enum ObjectType : uint32_t
{
    OBJECT_MESH     = 0,
    OBJECT_PLANE    = 1,
    OBJECT_SPHERE   = 2,
};

struct Image
{
    uint32_t                    width;
    uint32_t                    height;
    void const*                 pixels;
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

struct Scene
{
    std::vector<Image>          textures;
    std::vector<Material>       materials;
    std::vector<Object>         objects;
    std::vector<MeshFace>       meshFaces;
    std::vector<MeshNode>       meshNodes;

    uint32_t                    skyboxWidth;
    uint32_t                    skyboxHeight;
    float*                      skyboxPixels;
};

uint32_t AddTextureFromFile(Scene* scene, char const* path);

bool LoadMesh(Scene* scene, char const* path, float scale = 1.0f);
bool LoadSkybox(Scene* scene, char const* path);
void AddMesh(Scene* scene, glm::vec3 origin, uint32_t rootNodeIndex);
void AddPlane(Scene* scene, glm::vec3 origin);
void AddSphere(Scene* scene, glm::vec3 origin, float radius);