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
    std::string                     name                    = "New Material";
    uint32_t                        flags                   = 0;
    glm::vec3                       baseColor               = glm::vec3(1, 1, 1);
    Texture*                        baseColorTexture        = nullptr;
    glm::vec3                       emissionColor           = glm::vec3(0, 0, 0);
    Texture*                        emissionColorTexture    = nullptr;
    float                           emissionPower           = 0.0f;
    float                           metallic                = 0.0f;
    Texture*                        metallicTexture         = nullptr;
    float                           roughness               = 1.0f;
    Texture*                        roughnessTexture        = nullptr;
    float                           refraction              = 0.0f;
    float                           refractionIndex         = 1.0f;
    uint32_t                        packedMaterialIndex     = 0;
};

struct MeshFace
{
    glm::vec3                       vertices[3];
    glm::vec3                       centroid;
    glm::vec3                       normals[3];
    glm::vec2                       uvs[3];
    uint32_t                        materialIndex;
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
    uint32_t                        packedRootNodeIndex;
    std::vector<Material*>          materials;
};

enum EntityType
{
    ENTITY_TYPE_ROOT                = 0,
    ENTITY_TYPE_CAMERA              = 1,
    ENTITY_TYPE_MESH_INSTANCE       = 2,
    ENTITY_TYPE_PLANE               = 3,
    ENTITY_TYPE_SPHERE              = 4,
};

struct Entity
{
    std::string                     name;
    EntityType                      type;
    Transform                       transform;
    Material*                       material;
    Mesh*                           mesh;
    uint32_t                        packedObjectIndex;
    std::vector<Entity*>            children;
};

struct Root : Entity
{
};

struct Camera : Entity
{
    RenderMode      renderMode;
    uint32_t        bounceLimit;

    ToneMappingMode toneMappingMode;
    float           toneMappingWhiteLevel;

    CameraType      type;
    glm::vec3       velocity;
    float           focalLengthInMM;
    float           apertureRadiusInMM;
    float           focusDistance;
};

struct Scene
{
    Root                            root;
    std::vector<Mesh*>              meshes;
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

    uint32_t                        dirtyFlags;
};

enum SceneDirtyFlag
{
    SCENE_DIRTY_SKYBOX              = 1 << 0,
    SCENE_DIRTY_TEXTURES            = 1 << 1,
    SCENE_DIRTY_MATERIALS           = 1 << 2,
    SCENE_DIRTY_OBJECTS             = 1 << 3,
    SCENE_DIRTY_MESHES              = 1 << 4,
    SCENE_DIRTY_ALL                 = 0xFFFFFFFF,
};

Texture* LoadTexture(Scene* scene, char const* path);

struct LoadModelOptions
{
    Material*       defaultMaterial             = nullptr;
    std::string     directoryPath               = ".";
    glm::mat4       vertexTransform             = glm::mat4(1);
    glm::mat4       normalTransform             = glm::mat4(1);
    glm::mat3       textureCoordinateTransform  = glm::mat3(1);
};

Mesh* LoadModel(Scene* scene, char const* path, LoadModelOptions* options = nullptr);

bool LoadSkybox(Scene* scene, char const* path);

Material* CreateMaterial(Scene* scene, char const* name);

uint32_t BakeSceneData(Scene* scene);

bool Trace(Scene* scene, Ray const& ray, Hit& hit);