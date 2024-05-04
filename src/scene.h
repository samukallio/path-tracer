#pragma once

#include "common.h"

struct Texture
{
    std::string                     name;
    uint32_t                        width;
    uint32_t                        height;
    uint32_t const*                 pixels;

    uint32_t                        atlasImageIndex;
    glm::vec2                       atlasPlacementMinimum;
    glm::vec2                       atlasPlacementMaximum;
};

struct Material
{
    std::string                     name                    = "New Material";
    uint32_t                        flags                   = 0;
    glm::vec3                       baseColor               = glm::vec3(1, 1, 1);
    Texture*                        baseColorTexture        = nullptr;
    bool                            baseColorTextureFilterNearest = false;
    glm::vec3                       emissionColor           = glm::vec3(0, 0, 0);
    Texture*                        emissionColorTexture    = nullptr;
    bool                            emissionColorFilterNearest = false;
    float                           emissionPower           = 0.0f;
    float                           metallic                = 0.0f;
    Texture*                        metallicTexture         = nullptr;
    bool                            metallicTextureFilterNearest = false;
    float                           roughness               = 1.0f;
    Texture*                        roughnessTexture        = nullptr;
    bool                            roughnessTextureFilterNearest = false;
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
    ENTITY_TYPE_CONTAINER           = 1,
    ENTITY_TYPE_CAMERA              = 2,
    ENTITY_TYPE_MESH_INSTANCE       = 3,
    ENTITY_TYPE_PLANE               = 4,
    ENTITY_TYPE_SPHERE              = 5,
    ENTITY_TYPE_CUBE                = 6,

    ENTITY_TYPE__COUNT              = 7,
};

struct Entity
{
    std::string                     name                    = "Entity";
    EntityType                      type                    = ENTITY_TYPE_ROOT;
    bool                            active                  = true;
    Transform                       transform               = {};
    std::vector<Entity*>            children                = {};
    uint32_t                        packedObjectIndex       = 0xFFFFFFFF;
};

struct Root : Entity
{
    float                           scatterRate             = 0.0f;

    Root() { type = ENTITY_TYPE_ROOT; }
};

struct Container : Entity
{
    Container() { type = ENTITY_TYPE_CONTAINER; }
};

struct CameraParametersPinhole
{
    float                           fieldOfViewInDegrees    = 90.000f;
    float                           apertureDiameterInMM    = 0.0f;
};

struct CameraParametersThinLens
{
    glm::vec2                       sensorSizeInMM          = { 32.0f, 18.0f };
    float                           focalLengthInMM         = 20.0f;
    float                           apertureDiameterInMM    = 10.0f;
    float                           focusDistance           = 1.0f;
};

struct Camera : Entity
{
    RenderMode                      renderMode              = RENDER_MODE_PATH_TRACE;
    uint32_t                        renderFlags             = 0;
    uint32_t                        renderBounceLimit       = 5;
    uint32_t                        renderMeshComplexityScale = 32;
    uint32_t                        renderSceneComplexityScale = 32;
    uint32_t                        renderSampleBlockSizeLog2 = 0;

    float                           brightness              = 1.0f;
    ToneMappingMode                 toneMappingMode         = TONE_MAPPING_MODE_CLAMP;
    float                           toneMappingWhiteLevel   = 1.0f;

    CameraModel                     cameraModel             = CAMERA_MODEL_PINHOLE;
    CameraParametersPinhole         pinhole                 = {};
    CameraParametersThinLens        thinLens                = {};

    glm::vec3                       velocity                = { 0, 0, 0 };

    Camera() { ((Entity*)this)->type = ENTITY_TYPE_CAMERA; }
};

struct MeshInstance : Entity
{
    Mesh*                           mesh = nullptr;

    MeshInstance() { type = ENTITY_TYPE_MESH_INSTANCE; }
};

struct Plane : Entity
{
    Material*                       material = nullptr;

    Plane() { type = ENTITY_TYPE_PLANE; }
};

struct Sphere : Entity
{
    Material*                       material = nullptr;

    Sphere() { type = ENTITY_TYPE_SPHERE; }
};

struct Cube : Entity
{
    Material*                       material = nullptr;

    Cube() { type = ENTITY_TYPE_CUBE; }
};

struct Prefab
{
    Entity*                         entity                  = nullptr;
};

struct Scene
{
    Root                            root;
    std::vector<Mesh*>              meshes;
    std::vector<Material*>          materials;
    std::vector<Texture*>           textures;
    std::vector<Prefab*>            prefabs;

    std::vector<Image>              images;
    std::vector<PackedSceneObject>  sceneObjectPack;
    std::vector<PackedSceneNode>    sceneNodePack;
    std::vector<PackedMaterial>     materialPack;
    std::vector<PackedMeshFace>     meshFacePack;
    std::vector<PackedMeshNode>     meshNodePack;

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
    SCENE_DIRTY_CAMERAS             = 1 << 5,
    SCENE_DIRTY_ALL                 = 0xFFFFFFFF,
};

struct LoadModelOptions
{
    char const*     name                        = nullptr;
    Material*       defaultMaterial             = nullptr;
    std::string     directoryPath               = ".";
    glm::mat4       vertexTransform             = glm::mat4(1);
    glm::mat4       normalTransform             = glm::mat4(1);
    glm::mat3       textureCoordinateTransform  = glm::mat3(1);
    bool            mergeIntoSingleMesh         = false;
};

char const* EntityTypeName(EntityType type);

Entity* CreateEntity(Scene* scene, EntityType type, Entity* parent = nullptr);
Entity* CreateEntity(Scene* scene, Entity* source, Entity* parent = nullptr);
Entity* CreateEntity(Scene* scene, Prefab* prefab, Entity* parent = nullptr);
Material* CreateMaterial(Scene* scene, char const* name);
Texture* LoadTexture(Scene* scene, char const* path, char const* name = nullptr);
Prefab* LoadModelAsPrefab(Scene* scene, char const* path, LoadModelOptions* options = nullptr);
bool LoadSkybox(Scene* scene, char const* path);

uint32_t PackSceneData(Scene* scene);

bool Trace(Scene* scene, Ray const& ray, Hit& hit);