#pragma once

#include "common.h"

struct MeshNode
{
    glm::vec3                   minimum;
    uint32_t                    faceBeginOrNodeIndex;
    glm::vec3                   maximum;
    uint32_t                    faceEndIndex;
};

struct MeshFace
{
    glm::aligned_vec3           position;
    glm::aligned_vec4           plane;
    glm::aligned_vec3           base1;
    glm::aligned_vec3           base2;
    glm::aligned_vec3           normals[3];
};

struct Scene
{
    std::vector<MeshFace>       meshFaces;
    std::vector<MeshNode>       meshNodes;

    uint32_t                    skyboxWidth;
    uint32_t                    skyboxHeight;
    float*                      skyboxPixels;
};

bool LoadMesh(Scene* scene, char const* path);
bool LoadSkybox(Scene* scene, char const* path);
