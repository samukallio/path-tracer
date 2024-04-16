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
    glm::aligned_vec3           positions[3];
    glm::aligned_vec3           normals[3];
};

struct Scene
{
    std::vector<MeshFace>       meshFaces;
    std::vector<MeshNode>       meshNodes;
};

bool LoadScene(Scene* scene, char const* path);
