#pragma once

#include "core/common.hpp"
#include "core/spectrum.hpp"
#include "core/vulkan.hpp"

uint const SHAPE_INDEX_NONE   = 0xFFFFFFFF;
uint const TEXTURE_INDEX_NONE = 0xFFFFFFFF;

enum texture_type
{
    TEXTURE_TYPE_RAW                    = 0,
    TEXTURE_TYPE_REFLECTANCE_WITH_ALPHA = 1,
    TEXTURE_TYPE_RADIANCE               = 2,
    TEXTURE_TYPE__COUNT                 = 3,
};

enum texture_flag : uint
{
    TEXTURE_FLAG_FILTER_NEAREST = 1 << 0,
};

enum material_type
{
    MATERIAL_TYPE_OPENPBR = 0,
    MATERIAL_TYPE__COUNT  = 1,
};

enum shape_type : int32_t
{
    SHAPE_TYPE_MESH_INSTANCE = 0,
    SHAPE_TYPE_PLANE         = 1,
    SHAPE_TYPE_SPHERE        = 2,
    SHAPE_TYPE_CUBE          = 3,
};

enum camera_model : int32_t
{
    CAMERA_MODEL_PINHOLE   = 0,
    CAMERA_MODEL_THIN_LENS = 1,
    CAMERA_MODEL_360       = 2,
    CAMERA_MODEL__COUNT    = 3,
};

inline char const* TextureTypeName(texture_type Type)
{
    switch (Type)
    {
        case TEXTURE_TYPE_RAW:
            return "Raw";
        case TEXTURE_TYPE_REFLECTANCE_WITH_ALPHA:
            return "Reflectance (with alpha)";
        case TEXTURE_TYPE_RADIANCE:
            return "Radiance";
    }
    assert(false);
    return nullptr;
}

inline char const* CameraModelName(camera_model Model)
{
    switch (Model)
    {
        case CAMERA_MODEL_PINHOLE:
            return "Pinhole";
        case CAMERA_MODEL_THIN_LENS:
            return "Thin Lens";
        case CAMERA_MODEL_360:
            return "360";
    }
    assert(false);
    return nullptr;
}

/* --- Low-Level Scene Representation ---------------------------------------- */

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct alignas(16) packed_transform
{
    aligned_mat4 To = mat4(1);
    aligned_mat4 From = mat4(1);
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct alignas(16) packed_texture
{
    vec2 AtlasPlacementMinimum;
    vec2 AtlasPlacementMaximum;
    uint AtlasImageIndex;
    uint Type;
    uint Flags;
    uint Unused0;
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct alignas(16) packed_shape
{
    shape_type Type;
    uint MaterialIndex;
    uint MeshRootNodeIndex;
    packed_transform Transform;
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct alignas(16) packed_shape_node
{
    vec3 Minimum;
    uint ChildNodeIndices;
    vec3 Maximum;
    uint ShapeIndex;
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct alignas(16) packed_mesh_face
{
    vec3 Position0;
    uint VertexIndex0;
    vec3 Position1;
    uint VertexIndex1;
    vec3 Position2;
    uint VertexIndex2;
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct alignas(4) packed_mesh_vertex
{
    uint PackedNormal;
    uint PackedUV;
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct alignas(16) packed_mesh_node
{
    vec3 Minimum;
    uint FaceBeginOrNodeIndex;
    vec3 Maximum;
    uint FaceEndIndex;
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct alignas(16) packed_scene_globals
{
    aligned_mat3 SkyboxDistributionFrame = {};
    float        SkyboxDistributionConcentration = 1.0f;
    float        SkyboxBrightness = 1.0f;
    uint         SkyboxTextureIndex = TEXTURE_INDEX_NONE;
    uint         ShapeCount = 0;
    float        SceneScatterRate = 0.0f;
};

// This structure is shared between CPU and GPU,
// and must follow std430 layout rules.
struct alignas(16) packed_camera
{
    uint             Model;
    float            FocalLength;
    float            ApertureRadius;
    float            SensorDistance;
    vec2             SensorSize;
    packed_transform Transform;
};

/* --- High-Level Scene Representation --------------------------------------- */

struct texture
{
    std::string      Name = "New Texture";
    texture_type     Type = TEXTURE_TYPE_RAW;
    bool             EnableNearestFiltering = false;
    uint32_t         Width = 0;
    uint32_t         Height = 0;
    glm::vec4 const* Pixels = nullptr;
    uint32_t         PackedTextureIndex = 0;
};

struct material
{
    material_type Type = {};
    std::string   Name = "New Material";
    uint32_t      Flags = 0;
    float         Opacity = 1.0f;
    uint32_t      PackedMaterialIndex = 0;

    virtual ~material() {}
};

struct mesh_face
{
    uint VertexIndex[3];
};

struct mesh_vertex
{
    vec3 Position = {};
    vec3 Normal = {};
    vec2 UV = {};
};

struct mesh_node
{
    bounds   Bounds;
    uint32_t FaceBeginIndex;
    uint32_t FaceEndIndex;
    uint32_t ChildNodeIndex;
};

struct mesh
{
    std::string              Name;
    std::vector<mesh_vertex> Vertices;
    std::vector<mesh_face>   Faces;
    std::vector<mesh_node>   Nodes;
    uint32_t                 Depth;
    uint32_t                 PackedRootNodeIndex;
};

enum entity_type
{
    ENTITY_TYPE_ROOT = 0,
    ENTITY_TYPE_CONTAINER = 1,
    ENTITY_TYPE_CAMERA = 2,
    ENTITY_TYPE_MESH_INSTANCE = 3,
    ENTITY_TYPE_PLANE = 4,
    ENTITY_TYPE_SPHERE = 5,
    ENTITY_TYPE_CUBE = 6,

    ENTITY_TYPE__COUNT = 7,
};

struct entity
{
    std::string          Name = "Entity";
    entity_type          Type = ENTITY_TYPE_ROOT;
    bool                 Active = true;
    transform            Transform = {};
    entity*              Parent = nullptr;
    std::vector<entity*> Children = {};
    uint32_t             PackedShapeIndex = SHAPE_INDEX_NONE;

    virtual ~entity() {}
};

struct root_entity : entity
{
    float    ScatterRate = 0.0f;
    float    SkyboxBrightness = 1.0f;
    texture* SkyboxTexture = nullptr;

    root_entity() { Type = ENTITY_TYPE_ROOT; }
};

struct container_entity : entity
{
    container_entity() { Type = ENTITY_TYPE_CONTAINER; }
};

struct camera_pinhole
{
    float FieldOfViewInDegrees = 90.000f;
    float ApertureDiameterInMM = 0.0f;
};

struct camera_thin_lens
{
    vec2  SensorSizeInMM = { 32.0f, 18.0f };
    float FocalLengthInMM = 20.0f;
    float ApertureDiameterInMM = 10.0f;
    float FocusDistance = 1.0f;
};

struct camera_entity : entity
{
    camera_model     CameraModel = CAMERA_MODEL_PINHOLE;
    camera_pinhole   Pinhole = {};
    camera_thin_lens ThinLens = {};
    vec3             Velocity = { 0, 0, 0 };
    uint             PackedCameraIndex = 0;

    camera_entity() { ((entity*)this)->Type = ENTITY_TYPE_CAMERA; }
};

struct mesh_entity : entity
{
    mesh*     Mesh = nullptr;
    material* Material = nullptr;

    mesh_entity() { Type = ENTITY_TYPE_MESH_INSTANCE; }
};

struct plane_entity : entity
{
    material* Material = nullptr;

    plane_entity() { Type = ENTITY_TYPE_PLANE; }
};

struct sphere_entity : entity
{
    material* Material = nullptr;

    sphere_entity() { Type = ENTITY_TYPE_SPHERE; }
};

struct cube_entity : entity
{
    material* Material = nullptr;

    cube_entity() { Type = ENTITY_TYPE_CUBE; }
};

struct prefab
{
    entity* Entity = nullptr;
};

enum scene_dirty_flag
{
    SCENE_DIRTY_GLOBALS   = 1 << 0,
    SCENE_DIRTY_TEXTURES  = 1 << 1,
    SCENE_DIRTY_MATERIALS = 1 << 2,
    SCENE_DIRTY_SHAPES    = 1 << 3,
    SCENE_DIRTY_MESHES    = 1 << 4,
    SCENE_DIRTY_CAMERAS   = 1 << 5,
    SCENE_DIRTY_ALL       = 0xFFFFFFFF,
};

struct scene
{
    // Source description of the scene entities and assets.
    root_entity                Root;
    std::vector<mesh*>         Meshes;
    std::vector<material*>     Materials;
    std::vector<texture*>      Textures;
    std::vector<prefab*>       Prefabs;
    parametric_spectrum_table* RGBSpectrumTable;
    mat3                       SkyboxDistributionFrame;
    float                      SkyboxDistributionConcentration;

    // Data derived from the source data, packed and optimized
    // for rendering on the GPU. Generated by PackSceneData().
    std::vector<image>              Images;
    std::vector<packed_texture>     TexturePack;
    std::vector<packed_shape>       ShapePack;
    std::vector<packed_shape_node>  ShapeNodePack;
    std::vector<uint>               MaterialAttributePack;
    std::vector<packed_mesh_face>   MeshFacePack;
    std::vector<packed_mesh_vertex> MeshVertexPack;
    std::vector<packed_mesh_node>   MeshNodePack;
    std::vector<packed_camera>      CameraPack;
    packed_scene_globals            Globals;

    // Flags that track which portion of the source description has
    // changed relative to the packed data since the last call to
    // PackSceneData().
    uint32_t DirtyFlags;
};

// Vulkan resources associated with a scene.
struct vulkan_scene
{
    VkDescriptorSetLayout DescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet       DescriptorSet       = VK_NULL_HANDLE;
    vulkan_buffer         UniformBuffer       = {};
    vulkan_image          ImageArray          = {};
    vulkan_buffer         TextureBuffer       = {};
    vulkan_buffer         MaterialBuffer      = {};
    vulkan_buffer         ShapeBuffer         = {};
    vulkan_buffer         ShapeNodeBuffer     = {};
    vulkan_buffer         MeshFaceBuffer      = {};
    vulkan_buffer         MeshVertexBuffer    = {};
    vulkan_buffer         MeshNodeBuffer      = {};
    vulkan_buffer         CameraBuffer        = {};
};

struct load_model_options
{
    char const* Name = nullptr;
    material*   DefaultMaterial = nullptr;
    std::string DirectoryPath = ".";
    mat4        VertexTransform = mat4(1);
    mat4        NormalTransform = mat4(1);
    mat3        TextureCoordinateTransform = mat3(1);
};

inline uint32_t GetPackedTextureIndex(texture* Texture)
{
    if (!Texture) return TEXTURE_INDEX_NONE;
    return Texture->PackedTextureIndex;
}

inline uint32_t GetPackedMaterialIndex(material* Material)
{
    if (!Material) return 0;
    return Material->PackedMaterialIndex;
}

inline packed_transform PackTransform(mat4 const& Matrix)
{
    return { .To = Matrix, .From = glm::inverse(Matrix) };
}

// --- scene.cpp --------------------------------------------------------------

char const* EntityTypeName(entity_type Type);

entity* CreateEntityRaw(entity_type Type);
entity* CreateEntity(scene* Scene, entity_type Type, entity* Parent = nullptr);
entity* CreateEntity(scene* Scene, entity* Source, entity* Parent = nullptr);
entity* CreateEntity(scene* Scene, prefab* Prefab, entity* Parent = nullptr);
void DestroyEntity(scene* Scene, entity* Entity);

material* CreateMaterial(scene* Scene, material_type Type, char const* Name);
void DestroyMaterial(scene* Scene, material* Material);

texture* CreateCheckerTexture(scene* Scene, char const* Name, texture_type Type, glm::vec4 const& ColorA, glm::vec4 const& ColorB);
texture* LoadTexture(scene* Scene, char const* Path, texture_type Type, char const* Name = nullptr);
void DestroyTexture(scene* Scene, texture* Texture);

void DestroyMesh(scene* Scene, mesh* Mesh);

prefab* LoadModelAsPrefab(scene* Scene, char const* Path, load_model_options* Options = nullptr);
void DestroyPrefab(scene* Scene, prefab* Prefab);

scene* CreateScene();
scene* LoadScene(char const* Path);
void SaveScene(char const* Path, scene* Scene);
void DestroyScene(scene* Scene);

uint32_t PackSceneData(scene* Scene);

entity* FindEntityByPackedShapeIndex(scene* Scene, uint32_t PackedShapeIndex);

vulkan_scene* CreateVulkanScene(vulkan* Vulkan);
void UpdateVulkanScene(vulkan* Vulkan, vulkan_scene* VulkanScene, scene* Scene, uint32_t Flags);
void DestroyVulkanScene(vulkan* Vulkan, vulkan_scene* VulkanScene);

/* --- Material Types -------------------------------------------------------- */

#include "scene/openpbr.hpp"

inline char const* MaterialTypeName(material_type Type)
{
    switch (Type)
    {
        case MATERIAL_TYPE_OPENPBR:
            return "OpenPBR";
    }
    assert(false);
    return nullptr;
}

inline uint MaterialTypePackedSize(material_type Type)
{
    switch (Type)
    {
        case MATERIAL_TYPE_OPENPBR:
            return 64;
    }
    assert(false);
    return 0;
}

template<typename function_type>
inline void ForEachMaterialTexture(scene* Scene, material* Material, function_type&& Function)
{
    if (Material->Type == MATERIAL_TYPE_OPENPBR)
        OpenPBR_ForEachTexture(Scene, static_cast<openpbr_material*>(Material), std::forward<function_type>(Function));
}

inline void PackMaterialData(scene* Scene, material* Material, uint* AttributeData)
{
    if (Material->Type == MATERIAL_TYPE_OPENPBR)
        OpenPBR_PackData(Scene, static_cast<openpbr_material*>(Material), AttributeData);
}
