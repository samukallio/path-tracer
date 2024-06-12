#pragma once

#include "common.h"
#include "spectral.h"

struct texture
{
    std::string                     Name                    = "New Texture";
    texture_type                    Type                    = TEXTURE_TYPE_RAW;
    uint32_t                        Width                   = 0;
    uint32_t                        Height                  = 0;
    glm::vec4 const*                Pixels                  = nullptr;

    bool                            EnableNearestFiltering  = false;

    uint32_t                        PackedTextureIndex      = 0;
};

struct material
{
    std::string                     Name                    = "New Material";
    uint32_t                        Flags                   = 0;

    float                           Opacity                 = 1.0f;

    float                           BaseWeight              = 1.0f;
    glm::vec3                       BaseColor               = glm::vec3(1, 1, 1);
    texture*                        BaseColorTexture        = nullptr;
    float                           BaseMetalness           = 0.0f;
    float                           BaseDiffuseRoughness    = 0.0f;

    float                           SpecularWeight          = 1.0f;
    glm::vec3                       SpecularColor           = glm::vec3(1, 1, 1);
    float                           SpecularRoughness       = 0.3f;
    texture*                        SpecularRoughnessTexture = nullptr;
    float                           SpecularRoughnessAnisotropy = 0.0f;
    float                           SpecularIOR             = 1.5f;

    float                           TransmissionWeight      = 0.0f;
    glm::vec3                       TransmissionColor       = glm::vec3(1, 1, 1);
    float                           TransmissionDepth       = 0.0f;
    glm::vec3                       TransmissionScatter     = glm::vec3(0, 0, 0);
    float                           TransmissionScatterAnisotropy = 0.0f;
    float                           TransmissionDispersionScale = 0.0f;
    float                           TransmissionDispersionAbbeNumber = 20.0f;

    float                           CoatWeight              = 0.0f;
    glm::vec3                       CoatColor               = glm::vec3(1, 1, 1);
    float                           CoatRoughness           = 0.0f;
    float                           CoatRoughnessAnisotropy = 0.0f;
    float                           CoatIOR                 = 1.6f;
    float                           CoatDarkening           = 1.0f;

    float                           EmissionLuminance       = 0.0f;
    glm::vec3                       EmissionColor           = glm::vec3(0, 0, 0);
    texture*                        EmissionColorTexture    = nullptr;

    float                           ScatteringRate          = 0.0f;

    int                             LayerBounceLimit        = 16;

    uint32_t                        PackedMaterialIndex     = 0;
};

struct mesh_face
{
    glm::vec3                       Vertices[3];
    glm::vec3                       Centroid;
    glm::vec3                       Normals[3];
    glm::vec2                       UVs[3];
    uint32_t                        MaterialIndex;
};

struct mesh_node
{
    bounds                          Bounds;
    uint32_t                        FaceBeginIndex;
    uint32_t                        FaceEndIndex;
    uint32_t                        ChildNodeIndex;
};

struct mesh
{
    std::string                     Name;
    std::vector<mesh_face>          Faces;
    std::vector<mesh_node>          Nodes;
    uint32_t                        Depth;
    uint32_t                        PackedRootNodeIndex;
    std::vector<material*>          Materials;
};

enum entity_type
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

struct entity
{
    std::string                     Name                    = "Entity";
    entity_type                     Type                    = ENTITY_TYPE_ROOT;
    bool                            Active                  = true;
    transform                       Transform               = {};
    std::vector<entity*>            Children                = {};
    uint32_t                        PackedShapeIndex        = SHAPE_INDEX_NONE;
};

struct root : entity
{
    float                           ScatterRate             = 0.0f;
    float                           SkyboxBrightness        = 1.0f;
    texture*                        SkyboxTexture           = nullptr;

    root() { Type = ENTITY_TYPE_ROOT; }
};

struct container : entity
{
    container() { Type = ENTITY_TYPE_CONTAINER; }
};

struct camera_pinhole
{
    float                           FieldOfViewInDegrees    = 90.000f;
    float                           ApertureDiameterInMM    = 0.0f;
};

struct camera_thin_lens
{
    glm::vec2                       SensorSizeInMM          = { 32.0f, 18.0f };
    float                           FocalLengthInMM         = 20.0f;
    float                           ApertureDiameterInMM    = 10.0f;
    float                           FocusDistance           = 1.0f;
};

struct camera : entity
{
    render_mode                     RenderMode              = RENDER_MODE_PATH_TRACE;
    uint32_t                        RenderFlags             = 0;
    uint32_t                        RenderBounceLimit       = 5;
    uint32_t                        RenderMeshComplexityScale = 32;
    uint32_t                        RenderSceneComplexityScale = 32;
    uint32_t                        RenderSampleBlockSizeLog2 = 0;
    float                           RenderTerminationProbability = 0.0f;

    float                           Brightness              = 1.0f;
    tone_mapping_mode               ToneMappingMode         = TONE_MAPPING_MODE_CLAMP;
    float                           ToneMappingWhiteLevel   = 1.0f;

    camera_model                    CameraModel             = CAMERA_MODEL_PINHOLE;
    camera_pinhole                  Pinhole                 = {};
    camera_thin_lens                ThinLens                = {};

    glm::vec3                       Velocity                = { 0, 0, 0 };

    camera() { ((entity*)this)->Type = ENTITY_TYPE_CAMERA; }
};

struct mesh_instance : entity
{
    mesh*                           Mesh = nullptr;

    mesh_instance() { Type = ENTITY_TYPE_MESH_INSTANCE; }
};

struct plane : entity
{
    material*                       Material = nullptr;

    plane() { Type = ENTITY_TYPE_PLANE; }
};

struct sphere : entity
{
    material*                       Material = nullptr;

    sphere() { Type = ENTITY_TYPE_SPHERE; }
};

struct cube : entity
{
    material*                       Material = nullptr;

    cube() { Type = ENTITY_TYPE_CUBE; }
};

struct prefab
{
    entity*                         Entity                  = nullptr;
};

enum scene_dirty_flag
{
    SCENE_DIRTY_TEXTURES            = 1 << 0,
    SCENE_DIRTY_MATERIALS           = 1 << 1,
    SCENE_DIRTY_SHAPES              = 1 << 2,
    SCENE_DIRTY_MESHES              = 1 << 3,
    SCENE_DIRTY_CAMERAS             = 1 << 4,
    SCENE_DIRTY_ALL                 = 0xFFFFFFFF,
};

struct scene
{
    // Source description of the scene entities and assets.
    root                                Root;
    std::vector<mesh*>                  Meshes;
    std::vector<material*>              Materials;
    std::vector<texture*>               Textures;
    std::vector<prefab*>                Prefabs;
    parametric_spectrum_table*          RGBSpectrumTable;

    // Data derived from the source data, packed and optimized
    // for rendering on the GPU.  Generated by PackSceneData().
    std::vector<image>                  Images;
    std::vector<packed_texture>         TexturePack;
    std::vector<packed_shape>           ShapePack;
    std::vector<packed_shape_node>      ShapeNodePack;
    std::vector<packed_material>        MaterialPack;
    std::vector<packed_mesh_face>       MeshFacePack;
    std::vector<packed_mesh_face_extra> MeshFaceExtraPack;
    std::vector<packed_mesh_node>       MeshNodePack;

    glm::mat3                           SkyboxDistributionFrame;
    float                               SkyboxDistributionConcentration;

    // Flags that track which portion of the source description has
    // changed relative to the packed data since the last call to
    // PackSceneData().
    uint32_t                            DirtyFlags;
};

struct load_model_options
{
    char const*     Name                        = nullptr;
    material*       DefaultMaterial             = nullptr;
    std::string     DirectoryPath               = ".";
    glm::mat4       VertexTransform             = glm::mat4(1);
    glm::mat4       NormalTransform             = glm::mat4(1);
    glm::mat3       TextureCoordinateTransform  = glm::mat3(1);
    bool            MergeIntoSingleMesh         = false;
};

inline uint32_t GetPackedTextureIndex(texture* Texture)
{
    if (!Texture) return TEXTURE_INDEX_NONE;
    return Texture->PackedTextureIndex;
}

char const* EntityTypeName(entity_type Type);

entity*     CreateEntity(scene* Scene, entity_type Type, entity* Parent = nullptr);
entity*     CreateEntity(scene* Scene, entity* Source, entity* Parent = nullptr);
entity*     CreateEntity(scene* Scene, prefab* Prefab, entity* Parent = nullptr);
material*   CreateMaterial(scene* Scene, char const* Name);
texture*    CreateCheckerTexture(scene* Scene, char const* Name, glm::vec4 const& ColorA, glm::vec4 const& ColorB);
texture*    LoadTexture(scene* Scene, char const* Path, texture_type Type, char const* Name = nullptr);
prefab*     LoadModelAsPrefab(scene* Scene, char const* Path, load_model_options* Options = nullptr);

uint32_t    PackSceneData(scene* Scene);
entity*     FindEntityByPackedShapeIndex(scene* Scene, uint32_t PackedShapeIndex);

bool        Trace(scene* Scene, ray const& Ray, hit& Hit);