#pragma once

#include "common.h"

struct MeshNode
{
    glm::aligned_vec3           minimum;
    glm::aligned_vec3           maximum;
    uint32_t                    faceBeginOrNodeIndex;
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
