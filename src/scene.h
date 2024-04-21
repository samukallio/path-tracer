#pragma once

#include "common.h"

struct Texture
{
    std::string                     name;
    uint32_t                        width;
    uint32_t                        height;
    uint32_t const*                 pixels;

    uint32_t                        packedImageIndex;
    glm::vec2                       packedImageMinimum;
    glm::vec2                       packedImageMaximum;
};

struct Material
{
    std::string                     name;
    uint32_t                        flags;
    glm::vec3                       baseColor;
    Texture*                        baseColorTexture;
    glm::vec3                       emissionColor;
    Texture*                        emissionColorTexture;
    float                           metallic;
    Texture*                        metallicTexture;
    float                           roughness;
    Texture*                        roughnessTexture;
    float                           refraction;
    float                           refractionIndex;
    uint32_t                        packedMaterialIndex;
};

struct SceneObject
{
    std::string                     name;
    SceneObject*                    parent;
    Transform                       transform;
    ObjectType                      type;
};

struct MeshFace
{
    glm::vec3                       vertices[3];
    glm::vec3                       centroid;
    glm::vec3                       normals[3];
    glm::vec2                       uvs[3];
    Material*                       material;
};

struct MeshNode
{
    glm::vec3                       minimum;
    glm::vec3                       maximum;
    uint32_t                        faceBeginIndex;
    uint32_t                        faceEndIndex;
    uint32_t                        childNodeIndex;
};

struct Mesh
{
    std::string                     name;
    std::vector<MeshFace>           faces;
    std::vector<MeshNode>           nodes;
    uint32_t                        depth;
};

struct Scene
{
    std::vector<Mesh*>              meshes;
    std::vector<SceneObject*>       objects;
    std::vector<Material*>          materials;
    std::vector<Texture*>           textures;

    std::vector<PackedImage>        packedImages;
    std::vector<PackedSceneObject>  packedObjects;
    std::vector<PackedMaterial>     packedMaterials;
    std::vector<PackedMeshFace>     packedMeshFaces;
    std::vector<PackedMeshNode>     packedMeshNodes;

    uint32_t                        skyboxWidth;
    uint32_t                        skyboxHeight;
    float*                          skyboxPixels;
};

Texture* LoadTexture(Scene* scene, char const* path);
Mesh* LoadModel(Scene* scene, char const* path, float scale = 1.0f);
bool LoadSkybox(Scene* scene, char const* path);

void BakeSceneData(Scene* scene);

bool Trace(Scene* scene, Ray const& ray, Hit& hit);