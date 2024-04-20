#pragma once

#include "common.h"

struct Transform
{
    glm::vec3                   position;
    glm::vec3                   rotation;
};

struct Scene
{
    std::vector<Image>          textures;
    std::vector<Transform>      objectTransforms;

    // GPU shared data.
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

bool Trace(Scene* scene, Ray const& ray, Hit& hit);