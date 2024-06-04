#define TINYOBJLOADER_IMPLEMENTATION 
#include "tiny_obj_loader.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_RECT_PACK_IMPLEMENTATION
#include "stb_rect_pack.h"

#include "scene.h"

#include <unordered_map>
#include <format>
#include <stdio.h>

static void Grow(glm::vec3& Minimum, glm::vec3& Maximum, glm::vec3 Point)
{
    Minimum = glm::min(Minimum, Point);
    Maximum = glm::max(Maximum, Point);
}

static void Grow(bounds& Bounds, glm::vec3 Point)
{
    Bounds.Minimum = glm::min(Bounds.Minimum, Point);
    Bounds.Maximum = glm::max(Bounds.Maximum, Point);
}

static void Grow(bounds& Bounds, bounds const& Other)
{
    Bounds.Minimum = glm::min(Bounds.Minimum, Other.Minimum);
    Bounds.Maximum = glm::max(Bounds.Maximum, Other.Maximum);
}

static float HalfArea(glm::vec3 Minimum, glm::vec3 Maximum)
{
    glm::vec3 E = Maximum - Minimum;
    return E.x * E.y + E.y * E.z + E.z * E.x;
}

static float HalfArea(bounds const& Box)
{
    glm::vec3 E = Box.Maximum - Box.Minimum;
    return E.x * E.y + E.y * E.z + E.z * E.x;
}

static glm::vec3 OrthogonalVector(glm::vec3 const& V)
{
    int Axis = 0;
    if (glm::abs(V.y) > glm::abs(V.x)) Axis = 1;
    if (glm::abs(V.z) > glm::abs(V[Axis])) Axis = 2;
    glm::vec3 W = {};
    W[(Axis + 1) % 3] = 1.0f;
    return glm::normalize(glm::cross(V, W));
}

static uint8_t ToSRGB(float Value)
{
    Value = glm::clamp(Value, 0.0f, 1.0f);
    if (Value <= 0.0031308f)
        Value *= 12.92f;
    else
        Value = 1.055f * glm::pow(Value, 1.0f / 2.4f) - 0.055f;
    return static_cast<uint8_t>(Value * 255);
}

static uint32_t ToSRGB(glm::vec4 const& Color)
{
    return ToSRGB(Color.r)
         | ToSRGB(Color.g) << 8
         | ToSRGB(Color.b) << 16
         | static_cast<uint8_t>(255 * Color.a) << 24;
}

char const* EntityTypeName(entity_type Type)
{
    switch (Type) {
        case ENTITY_TYPE_ROOT:          return "Root";
        case ENTITY_TYPE_CONTAINER:     return "Container";
        case ENTITY_TYPE_CAMERA:        return "Camera";
        case ENTITY_TYPE_MESH_INSTANCE: return "Mesh Instance";
        case ENTITY_TYPE_PLANE:         return "Plane";
        case ENTITY_TYPE_SPHERE:        return "Sphere";
        case ENTITY_TYPE_CUBE:          return "Cube";
    }
    return "Entity";
}

entity* CreateEntity(scene* Scene, entity_type Type, entity* Parent)
{
    entity* Entity = nullptr;

    switch (Type) {
        case ENTITY_TYPE_ROOT:
            Entity = new root;
            break;
        case ENTITY_TYPE_CONTAINER:
            Entity = new container;
            break;
        case ENTITY_TYPE_CAMERA:
            Entity = new camera;
            break;
        case ENTITY_TYPE_MESH_INSTANCE:
            Entity = new mesh_instance;
            break;
        case ENTITY_TYPE_PLANE:
            Entity = new plane;
            break;
        case ENTITY_TYPE_SPHERE:
            Entity = new sphere;
            break;
        case ENTITY_TYPE_CUBE:
            Entity = new cube;
            break;
        default:
            assert(false);
            break;
    }

    if (!Parent) Parent = &Scene->Root;

    Parent->Children.push_back(Entity);

    return Entity;
}

entity* CreateEntity(scene* Scene, entity* Source, entity* Parent)
{
    entity* Entity = nullptr;

    switch (Source->Type) {
        case ENTITY_TYPE_ROOT:
            Entity = new root(*static_cast<root*>(Source));
            break;
        case ENTITY_TYPE_CONTAINER:
            Entity = new container(*static_cast<container*>(Source));
            break;
        case ENTITY_TYPE_CAMERA:
            Entity = new camera(*static_cast<camera*>(Source));
            break;
        case ENTITY_TYPE_MESH_INSTANCE:
            Entity = new mesh_instance(*static_cast<mesh_instance*>(Source));
            break;
        case ENTITY_TYPE_PLANE:
            Entity = new plane(*static_cast<plane*>(Source));
            break;
        case ENTITY_TYPE_SPHERE:
            Entity = new sphere(*static_cast<sphere*>(Source));
            break;
        case ENTITY_TYPE_CUBE:
            Entity = new cube(*static_cast<cube*>(Source));
            break;
        default:
            assert(false);
            break;
    }

    if (!Parent) Parent = &Scene->Root;

    Parent->Children.push_back(Entity);

    std::vector<entity*> Children = std::move(Entity->Children);
    Entity->Children.clear();
    for (entity* Child : Children)
        CreateEntity(Scene, Child, Entity);

    return Entity;
}

entity* CreateEntity(scene* Scene, prefab* Prefab, entity* Parent)
{
    return CreateEntity(Scene, Prefab->Entity, Parent);
}

texture* CreateCheckerTexture(scene* Scene, char const* Name, glm::vec4 const& ColorA, glm::vec4 const& ColorB)
{
    auto Pixels = new uint32_t[4];

    Pixels[0] = ToSRGB(ColorA);
    Pixels[1] = ToSRGB(ColorB);
    Pixels[2] = ToSRGB(ColorB);
    Pixels[3] = ToSRGB(ColorA);

    auto Texture = new texture {
        .Name = Name,
        .Width = 2,
        .Height = 2,
        .Pixels = Pixels,
    };
    Scene->Textures.push_back(Texture);

    Scene->DirtyFlags |= SCENE_DIRTY_TEXTURES;

    return Texture;
}

texture* LoadTexture(scene* Scene, char const* Path, char const* Name)
{
    int Width, Height, ChannelsInFile;
    stbi_uc* Pixels = stbi_load(Path, &Width, &Height, &ChannelsInFile, 4);
    if (!Pixels) return nullptr;

    auto Texture = new texture {
        .Name = Name ? Name : Path,
        .Width = static_cast<uint32_t>(Width),
        .Height = static_cast<uint32_t>(Height),
        .Pixels = reinterpret_cast<uint32_t*>(Pixels),
    };
    Scene->Textures.push_back(Texture);

    Scene->DirtyFlags |= SCENE_DIRTY_TEXTURES;

    return Texture;
}

material* CreateMaterial(scene* Scene, char const* Name)
{
    auto Material = new material;
    Material->Name = Name;
    Scene->Materials.push_back(Material);

    Scene->DirtyFlags |= SCENE_DIRTY_MATERIALS;

    return Material;
}

static void BuildMeshNode(mesh* Mesh, uint32_t NodeIndex, uint32_t Depth)
{
    mesh_node& Node = Mesh->Nodes[NodeIndex];

    uint32_t FaceCount = Node.FaceEndIndex - Node.FaceBeginIndex;

    // Compute node bounds.
    Node.Bounds = {};
    for (uint32_t Index = Node.FaceBeginIndex; Index < Node.FaceEndIndex; Index++) {
        for (int J = 0; J < 3; J++)
            Grow(Node.Bounds, Mesh->Faces[Index].Vertices[J]);
    }

    int SplitAxis = 0;
    float SplitPosition = 0;
    float SplitCost = +INF;

    for (int Axis = 0; Axis < 3; Axis++) {
        // Compute centroid-based bounds for the current node.
        float Minimum = +INF, Maximum = -INF;
        for (uint32_t I = Node.FaceBeginIndex; I < Node.FaceEndIndex; I++) {
            float Centroid = Mesh->Faces[I].Centroid[Axis];
            Minimum = std::min(Minimum, Centroid);
            Maximum = std::max(Maximum, Centroid);
        }

        if (Minimum == Maximum) continue;

        // Bin the faces by their centroid points.
        constexpr uint32_t BINS = 32;

        struct bin {
            bounds Bounds;
            uint32_t FaceCount = 0;
        };
        bin Bins[BINS];

        float BinIndexPerUnit = float(BINS) / (Maximum - Minimum);

        for (uint32_t I = Node.FaceBeginIndex; I < Node.FaceEndIndex; I++) {
            // Compute bin index of the face centroid.
            float Centroid = Mesh->Faces[I].Centroid[Axis];
            uint32_t BinIndexUnclamped = static_cast<uint32_t>(BinIndexPerUnit * (Centroid - Minimum));
            uint32_t BinIndex = std::min(BinIndexUnclamped, BINS - 1);

            // Grow the bin to accommodate the new face.
            bin& Bin = Bins[BinIndex];
            Grow(Bin.Bounds, Mesh->Faces[I].Vertices[0]);
            Grow(Bin.Bounds, Mesh->Faces[I].Vertices[1]);
            Grow(Bin.Bounds, Mesh->Faces[I].Vertices[2]);
            Bin.FaceCount++;
        }

        // Calculate details of each possible split.
        struct split {
            float LeftArea = 0.0f;
            uint32_t LeftCount = 0;
            float RightArea = 0.0f;
            uint32_t RightCount = 0;
        };
        split Splits[BINS-1];

        bounds LeftBounds;
        bounds RightBounds;
        uint32_t LeftCountSum = 0;
        uint32_t RightCountSum = 0;

        for (uint32_t I = 0; I < BINS-1; I++) {
            uint32_t J = BINS - 2 - I;

            bin const& LeftBin = Bins[I];
            if (LeftBin.FaceCount > 0) {
                LeftCountSum += LeftBin.FaceCount;
                Grow(LeftBounds, LeftBin.Bounds);
            }
            Splits[I].LeftCount = LeftCountSum;
            Splits[I].LeftArea = HalfArea(LeftBounds);

            bin const& RightBin = Bins[J+1];
            if (RightBin.FaceCount > 0) {
                RightCountSum += RightBin.FaceCount;
                Grow(RightBounds, RightBin.Bounds);
            }
            Splits[J].RightCount = RightCountSum;
            Splits[J].RightArea = HalfArea(RightBounds);
        }

        // Find the best split.
        float Interval = (Maximum - Minimum) / float(BINS);
        float Position = Minimum + Interval;

        for (uint32_t I = 0; I < BINS - 1; I++) {
            split const& Split = Splits[I];
            float Cost = Split.LeftCount * Split.LeftArea + Split.RightCount * Split.RightArea;
            if (Cost < SplitCost) {
                SplitCost = Cost;
                SplitAxis = Axis;
                SplitPosition = Position;
            }
            Position += Interval;
        }
    }

    // If splitting is more costly than not splitting, then leave this node as a leaf.
    float NoSplitCost = FaceCount * HalfArea(Node.Bounds);
    if (SplitCost >= NoSplitCost) return;

    // Partition the faces within the node by the chosen split plane.
    uint32_t BeginIndex = Node.FaceBeginIndex;
    uint32_t EndIndex = Node.FaceEndIndex;
    uint32_t SplitIndex = BeginIndex;
    uint32_t SwapIndex = EndIndex - 1;
    while (SplitIndex < SwapIndex) {
        if (Mesh->Faces[SplitIndex].Centroid[SplitAxis] < SplitPosition) {
            SplitIndex++;
        }
        else {
            std::swap(Mesh->Faces[SplitIndex], Mesh->Faces[SwapIndex]);
            SwapIndex--;
        }
    }

    if (SplitIndex == BeginIndex || SplitIndex == EndIndex)
        return;

    uint32_t LeftNodeIndex = static_cast<uint32_t>(Mesh->Nodes.size());
    uint32_t RightNodeIndex = LeftNodeIndex + 1;

    Node.ChildNodeIndex = LeftNodeIndex;

    Mesh->Nodes.push_back({
        .FaceBeginIndex = BeginIndex,
        .FaceEndIndex = SplitIndex,
    });

    Mesh->Nodes.push_back({
        .FaceBeginIndex = SplitIndex,
        .FaceEndIndex = EndIndex,
    });

    Mesh->Depth = std::max(Mesh->Depth, Depth+1);

    BuildMeshNode(Mesh, LeftNodeIndex, Depth+1);
    BuildMeshNode(Mesh, RightNodeIndex, Depth+1);
}

prefab* LoadModelAsPrefab(scene* Scene, char const* Path, load_model_options* Options)
{
    load_model_options DefaultOptions {};
    if (!Options) Options = &DefaultOptions;

    tinyobj::attrib_t Attrib;
    std::vector<tinyobj::shape_t> Shapes;
    std::vector<tinyobj::material_t> FileMaterials;
    std::string Warnings;
    std::string Errors;

    if (!tinyobj::LoadObj(&Attrib, &Shapes, &FileMaterials, &Warnings, &Errors, Path, Options->DirectoryPath.c_str()))
        return nullptr;

    if (Attrib.normals.empty()) {
        auto& Normals = Attrib.normals;

        Normals.resize(Attrib.vertices.size());

        for (tinyobj::shape_t& Shape : Shapes) {
            size_t n = Shape.mesh.indices.size();
            for (size_t i = 0; i < n; i += 3) {
                glm::vec3 vertices[3];
                for (size_t j = 0; j < 3; j++) {
                    tinyobj::index_t const& Index = Shape.mesh.indices[i+j];
                    vertices[j] = glm::vec3(
                        Attrib.vertices[3*Index.vertex_index+0],
                        Attrib.vertices[3*Index.vertex_index+1],
                        Attrib.vertices[3*Index.vertex_index+2]);
                }
                glm::vec3 normal = glm::normalize(glm::cross(vertices[1] - vertices[0], vertices[2] - vertices[0]));
                for (size_t j = 0; j < 3; j++) {
                    tinyobj::index_t& Index = Shape.mesh.indices[i+j];
                    Index.normal_index = Index.vertex_index;
                    Normals[3*Index.normal_index+0] += normal.x;
                    Normals[3*Index.normal_index+1] += normal.y;
                    Normals[3*Index.normal_index+2] += normal.z;
                }
            }
        }

        size_t n = Normals.size();
        for (size_t i = 0; i < n; i += 3) {
            float length = glm::length(glm::vec3(Normals[i+0], Normals[i+1], Normals[i+2]));
            Normals[i+0] /= length;
            Normals[i+1] /= length;
            Normals[i+2] /= length;
        }
    }

    // Map from in-file texture name to scene texture.
    std::unordered_map<std::string, texture*> TextureMap;
    std::vector<material*> Materials;

    // Scan the material definitions and build scene materials.
    for (int MaterialId = 0; MaterialId < FileMaterials.size(); MaterialId++) {
        tinyobj::material_t const& FileMaterial = FileMaterials[MaterialId];

        auto Material = CreateMaterial(Scene, FileMaterial.name.c_str());

        Material->BaseColor = glm::vec4(
            FileMaterial.diffuse[0],
            FileMaterial.diffuse[1],
            FileMaterial.diffuse[2],
            1.0);

        Material->EmissionColor = glm::vec4(
            FileMaterial.emission[0],
            FileMaterial.emission[1],
            FileMaterial.emission[2],
            1.0);

        Material->SpecularRoughness = 1.0f;
        Material->SpecularIOR = 0.0f;
        Material->TransmissionWeight = 0.0f;

        std::pair<std::string, texture**> Textures[] = {
            { FileMaterial.diffuse_texname, &Material->BaseColorTexture },
            { FileMaterial.emissive_texname, &Material->EmissionColorTexture },
        };

        for (auto const& [Name, TexturePtr] : Textures) {
            if (!Name.empty()) {
                if (!TextureMap.contains(Name)) {
                    std::string Path = std::format("{}/{}", Options->DirectoryPath, Name);
                    TextureMap[Name] = LoadTexture(Scene, Path.c_str(), Name.c_str());
                }
                *TexturePtr = TextureMap[Name];
            }
            else {
                *TexturePtr = nullptr;
            }
        }

        Materials.push_back(Material);
    }

    // Import meshes.
    std::vector<mesh*> Meshes;
    std::vector<glm::vec3> Origins;
    {
        mesh* Mesh = nullptr;

        std::unordered_map<int, uint32_t> MeshMaterialIndices;
        std::vector<material*> MeshMaterials;

        if (Options->MergeIntoSingleMesh) {
            Mesh = new mesh;
            Mesh->Name = Options->Name ? Options->Name : Path;
            Mesh->Materials = Materials;
            Meshes.push_back(Mesh);
            Origins.push_back(glm::vec3());

            size_t FaceCount = 0;
            for (auto const& Shape : Shapes)
                FaceCount += Shape.mesh.indices.size() / 3;
            Mesh->Faces.reserve(FaceCount);
        }

        glm::vec3 Origin {};

        for (tinyobj::shape_t const& Shape : Shapes) {
            size_t ShapeIndexCount = Shape.mesh.indices.size();

            if (!Options->MergeIntoSingleMesh) {
                Mesh = new mesh;
                Mesh->Name = !Shape.name.empty() ? Shape.name : "Shape";
                Mesh->Faces.reserve(ShapeIndexCount / 3);
                Meshes.push_back(Mesh);

                MeshMaterialIndices.clear();
                MeshMaterials.clear();

                glm::vec3 Minimum = glm::vec3(+INFINITY, +INFINITY, +INFINITY);
                glm::vec3 Maximum = glm::vec3(-INFINITY, -INFINITY, -INFINITY);
                for (size_t I = 0; I < ShapeIndexCount; I++) {
                    tinyobj::index_t const& Index = Shape.mesh.indices[I];
                    glm::vec3 Position = glm::vec3(
                        Attrib.vertices[3*Index.vertex_index+0],
                        Attrib.vertices[3*Index.vertex_index+1],
                        Attrib.vertices[3*Index.vertex_index+2]);
                    Minimum = glm::min(Minimum, Position);
                    Maximum = glm::max(Maximum, Position);
                }
                Origin = (Minimum + Maximum) / 2.0f;
                Origins.push_back(Origin);
            }

            for (size_t I = 0; I < ShapeIndexCount; I += 3) {
                auto Face = mesh_face {};

                for (int J = 0; J < 3; J++) {
                    tinyobj::index_t const& Index = Shape.mesh.indices[I+J];

                    Face.Vertices[J] = Options->VertexTransform * glm::vec4(
                        Attrib.vertices[3*Index.vertex_index+0] - Origin.x,
                        Attrib.vertices[3*Index.vertex_index+1] - Origin.y,
                        Attrib.vertices[3*Index.vertex_index+2] - Origin.z,
                        1.0f);

                    if (Index.normal_index >= 0) {
                        Face.Normals[J] = Options->NormalTransform * glm::vec4(
                            Attrib.normals[3*Index.normal_index+0],
                            Attrib.normals[3*Index.normal_index+1],
                            Attrib.normals[3*Index.normal_index+2],
                            1.0f);
                    }

                    if (Index.texcoord_index >= 0) {
                        Face.UVs[J] = Options->TextureCoordinateTransform * glm::vec3(
                            Attrib.texcoords[2*Index.texcoord_index+0],
                            Attrib.texcoords[2*Index.texcoord_index+1],
                            1.0f);
                    }
                }

                int MaterialId = Shape.mesh.material_ids[I/3];

                if (!MeshMaterialIndices.contains(MaterialId)) {
                    uint32_t MaterialIndex = static_cast<uint32_t>(MeshMaterials.size());
                    MeshMaterialIndices[MaterialId] = MaterialIndex;

                    if (MaterialId >= 0)
                        MeshMaterials.push_back(Materials[MaterialId]);
                    else
                        MeshMaterials.push_back(Options->DefaultMaterial);
                }

                Face.MaterialIndex = MeshMaterialIndices[MaterialId];

                Face.Centroid = (Face.Vertices[0] + Face.Vertices[1] + Face.Vertices[2]) / 3.0f;

                Mesh->Faces.push_back(Face);
            }

            if (!Options->MergeIntoSingleMesh) {
                Mesh->Materials = std::move(MeshMaterials);
            }
        }

        if (Options->MergeIntoSingleMesh) {
            Mesh->Materials = std::move(MeshMaterials);
        }
    }

    for (mesh* Mesh : Meshes) {
        Mesh->Nodes.reserve(2 * Mesh->Faces.size());

        auto Root = mesh_node {
            .FaceBeginIndex = 0,
            .FaceEndIndex = static_cast<uint32_t>(Mesh->Faces.size()),
            .ChildNodeIndex = 0,
        };

        Mesh->Nodes.push_back(Root);
        BuildMeshNode(Mesh, 0, 0);

        Scene->Meshes.push_back(Mesh);
    }

    Scene->DirtyFlags |= SCENE_DIRTY_MATERIALS;
    Scene->DirtyFlags |= SCENE_DIRTY_MESHES;

    auto Prefab = new prefab;

    if (Options->MergeIntoSingleMesh) {
        auto Instance = new mesh_instance;
        Instance->Name = Meshes[0]->Name;
        Instance->Mesh = Meshes[0];
        Prefab->Entity = Instance;
    }
    else {
        auto Container = new container;
        Container->Name = Options->Name ? Options->Name : Path;
        for (size_t I = 0; I < Meshes.size(); I++) {
            mesh* Mesh = Meshes[I];
            glm::vec3 const& Origin = Origins[I];

            auto Instance = new mesh_instance;
            Instance->Name = Mesh->Name;
            Instance->Mesh = Mesh;
            Instance->Transform.Position = Options->VertexTransform * glm::vec4(Origin, 1);
            Container->Children.push_back(Instance);
        }
        Prefab->Entity = Container;
    }

    return Prefab;
}

bool LoadSkybox(scene* Scene, char const* Path)
{
    int Width, Height, Components;

    Scene->SkyboxPixels = stbi_loadf(Path, &Width, &Height, &Components, STBI_rgb_alpha);
    Scene->SkyboxWidth = static_cast<uint32_t>(Width);
    Scene->SkyboxHeight = static_cast<uint32_t>(Height);

    Scene->DirtyFlags |= SCENE_DIRTY_SKYBOX;

    float* Pixels = Scene->SkyboxPixels;

    glm::vec3 Mean = {};
    float WeightSum = 0.0f;
    for (int Y = 0; Y < Height; Y++) {
        float Theta = (0.5f - (Y + 0.5f) / Height) * PI;
        for (int X = 0; X < Width; X++) {
            float Phi = ((X + 0.5f) / Width - 0.5f) * TAU;

            int Index = (Y * Width + X) * 4;
            float R = Pixels[Index+0];
            float G = Pixels[Index+1];
            float B = Pixels[Index+2];
            float Luminance = 0.2126f * R + 0.7152f * G + 0.0722f * B;
            float Area = glm::cos(Theta);

            float Weight = Area * Luminance * Luminance;

            glm::vec3 Direction = {
                glm::cos(Theta) * glm::cos(Phi),
                glm::cos(Theta) * glm::sin(Phi),
                glm::sin(Theta),
            };

            Mean += Weight * Direction;
            WeightSum += Weight;
        }
    }
    Mean /= WeightSum;

    float MeanLength = glm::length(Mean);

    glm::vec3 FrameZ = Mean / MeanLength;
    glm::vec3 FrameX = OrthogonalVector(FrameZ);
    glm::vec3 FrameY = glm::cross(FrameX, FrameZ);

    float Concentration = MeanLength * (3.0f - MeanLength * MeanLength) / (1 - MeanLength * MeanLength);

    Scene->SkyboxDistributionFrame = glm::mat3(FrameX, FrameY, FrameZ);
    Scene->SkyboxDistributionConcentration = Concentration;

    return true;
}

static void PackSceneObject(scene* Scene, glm::mat4 const& OuterTransform, entity* Entity, uint32_t& Priority)
{
    if (!Entity->Active)
        return;

    glm::mat4 InnerTransform
        = OuterTransform
        * glm::translate(glm::mat4(1), Entity->Transform.Position)
        * glm::orientate4(Entity->Transform.Rotation)
        * glm::scale(glm::mat4(1), Entity->Transform.Scale);

    for (entity* Child : Entity->Children)
        PackSceneObject(Scene, InnerTransform, Child, Priority);

    packed_scene_object Packed;

    Packed.MaterialIndex = 0;
    Packed.Priority = Priority++;

    switch (Entity->Type) {
        case ENTITY_TYPE_MESH_INSTANCE: {
            auto Instance = static_cast<mesh_instance*>(Entity);
            if (!Instance->Mesh) return;
            Packed.MeshRootNodeIndex = Instance->Mesh->PackedRootNodeIndex;
            Packed.Type = OBJECT_TYPE_MESH_INSTANCE;
            break;
        }
        case ENTITY_TYPE_PLANE: {
            auto Plane = static_cast<plane*>(Entity);
            if (Plane->Material)
                Packed.MaterialIndex = Plane->Material->PackedMaterialIndex;
            Packed.Type = OBJECT_TYPE_PLANE;
            break;
        }
        case ENTITY_TYPE_SPHERE: {
            auto Sphere = static_cast<sphere*>(Entity);
            if (Sphere->Material)
                Packed.MaterialIndex = Sphere->Material->PackedMaterialIndex;
            Packed.Type = OBJECT_TYPE_SPHERE;
            break;
        }
        case ENTITY_TYPE_CUBE: {
            auto Cube = static_cast<cube*>(Entity);
            if (Cube->Material)
                Packed.MaterialIndex = Cube->Material->PackedMaterialIndex;
            Packed.Type = OBJECT_TYPE_CUBE;
            break;
        }
        default: {
            return;
        }
    }

    Packed.Transform.To = InnerTransform;
    Packed.Transform.From = glm::inverse(InnerTransform);

    Entity->PackedObjectIndex = static_cast<uint32_t>(Scene->SceneObjectPack.size());

    Scene->SceneObjectPack.push_back(Packed);
}

static bounds SceneObjectBounds(scene const* Scene, packed_scene_object const& Object)
{
    glm::vec4 Corners[8] = {};

    switch (Object.Type) {
        case OBJECT_TYPE_MESH_INSTANCE: {
            glm::vec3 MeshMin = Scene->MeshNodePack[Object.MeshRootNodeIndex].Minimum;
            glm::vec3 MeshMax = Scene->MeshNodePack[Object.MeshRootNodeIndex].Maximum;

            Corners[0] = { MeshMin.x, MeshMin.y, MeshMin.z, 1 };
            Corners[1] = { MeshMin.x, MeshMin.y, MeshMax.z, 1 };
            Corners[2] = { MeshMin.x, MeshMax.y, MeshMin.z, 1 };
            Corners[3] = { MeshMin.x, MeshMax.y, MeshMax.z, 1 };
            Corners[4] = { MeshMax.x, MeshMin.y, MeshMin.z, 1 };
            Corners[5] = { MeshMax.x, MeshMin.y, MeshMax.z, 1 };
            Corners[6] = { MeshMax.x, MeshMax.y, MeshMin.z, 1 };
            Corners[7] = { MeshMax.x, MeshMax.y, MeshMax.z, 1 };

            break;
        }
        case OBJECT_TYPE_PLANE: {
            Corners[0] = { -1e9f, -1e9f, -EPSILON, 1 };
            Corners[1] = { +1e9f, -1e9f, -EPSILON, 1 };
            Corners[2] = { -1e9f, +1e9f, -EPSILON, 1 };
            Corners[3] = { +1e9f, +1e9f, -EPSILON, 1 };
            Corners[4] = { -1e9f, -1e9f, +EPSILON, 1 };
            Corners[5] = { +1e9f, -1e9f, +EPSILON, 1 };
            Corners[6] = { -1e9f, +1e9f, +EPSILON, 1 };
            Corners[7] = { +1e9f, +1e9f, +EPSILON, 1 };

            break;
        }
        case OBJECT_TYPE_SPHERE:
        case OBJECT_TYPE_CUBE: {
            Corners[0] = { -1, -1, -1, 1 };
            Corners[1] = { +1, -1, -1, 1 };
            Corners[2] = { -1, +1, -1, 1 };
            Corners[3] = { +1, +1, -1, 1 };
            Corners[4] = { -1, -1, +1, 1 };
            Corners[5] = { +1, -1, +1, 1 };
            Corners[6] = { -1, +1, +1, 1 };
            Corners[7] = { +1, +1, +1, 1 };

            break;
        }
    }

    glm::vec3 WorldMin = glm::vec3(+INFINITY);
    glm::vec3 WorldMax = glm::vec3(-INFINITY);

    for (glm::vec4 const& Corner : Corners) {
        glm::vec3 WorldCorner = (Object.Transform.To * Corner).xyz();
        WorldMin = glm::min(WorldMin, WorldCorner.xyz());
        WorldMax = glm::max(WorldMax, WorldCorner.xyz());
    }

    return { WorldMin, WorldMax };
}

void PrintSceneNode(scene* Scene, uint16_t Index, int Depth)
{
    packed_scene_node const& Node = Scene->SceneNodePack[Index];

    for (int I = 0; I < Depth; I++) printf("  ");

    if (Node.ChildNodeIndices > 0) {
        uint16_t IndexA = Node.ChildNodeIndices & 0xFFFF;
        uint16_t IndexB = Node.ChildNodeIndices >> 16;
        printf("Node %u\n", Index);
        PrintSceneNode(Scene, IndexA, Depth+1);
        PrintSceneNode(Scene, IndexB, Depth+1);
    }
    else {
        printf("Leaf %u (object %lu)\n", Index, Node.ObjectIndex);
    }
}

static uint32_t GetPackedTextureIndex(texture* Texture)
{
    if (!Texture) return TEXTURE_INDEX_NONE;
    return Texture->PackedTextureIndex;
}

uint32_t PackSceneData(scene* Scene)
{
    uint32_t DirtyFlags = Scene->DirtyFlags;

    // Pack textures.
    if (DirtyFlags & SCENE_DIRTY_TEXTURES) {
        constexpr int ATLAS_WIDTH = 4096;
        constexpr int ATLAS_HEIGHT = 4096;

        std::vector<stbrp_node> Nodes(ATLAS_WIDTH);
        std::vector<stbrp_rect> Rects(Scene->Textures.size());
        
        for (int I = 0; I < Rects.size(); I++) {
            texture* Texture = Scene->Textures[I];
            Rects[I] = {
                .id = I,
                .w = static_cast<int>(Texture->Width),
                .h = static_cast<int>(Texture->Height),
                .was_packed = 0,
            };
        }

        Scene->Images.clear();
        Scene->TexturePack.clear();

        while (!Rects.empty()) {
            stbrp_context Context;
            stbrp_init_target(&Context, 4096, 4096, Nodes.data(), static_cast<int>(Nodes.size()));
            stbrp_pack_rects(&Context, Rects.data(), static_cast<int>(Rects.size()));

            uint32_t* Pixels = new uint32_t[4096 * 4096];

            uint32_t ImageIndex = static_cast<uint32_t>(Scene->Images.size());

            for (stbrp_rect& Rect : Rects) {
                if (!Rect.was_packed)
                    continue;

                texture* Texture = Scene->Textures[Rect.id];
                assert(Texture->Width == Rect.w);
                assert(Texture->Height == Rect.h);

                Texture->PackedTextureIndex = static_cast<uint32_t>(Scene->TexturePack.size());

                packed_texture Packed;

                Packed.Flags = 0;
                Packed.AtlasImageIndex = ImageIndex;
                Packed.AtlasPlacementMinimum = {
                    (Rect.x + 0.5f) / float(ATLAS_WIDTH),
                    (Rect.y + Rect.h - 0.5f) / float(ATLAS_HEIGHT),
                };
                Packed.AtlasPlacementMaximum = {
                    (Rect.x + Rect.w - 0.5f) / float(ATLAS_WIDTH),
                    (Rect.y + 0.5f) / float(ATLAS_HEIGHT),
                };

                for (uint32_t Y = 0; Y < Texture->Height; Y++) {
                    uint32_t const* Src = Texture->Pixels + Y * Texture->Width;
                    uint32_t* Dst = Pixels + (Rect.y + Y) * ATLAS_WIDTH + Rect.x;
                    memcpy(Dst, Src, Texture->Width * sizeof(uint32_t));
                }

                if (Texture->EnableNearestFiltering)
                    Packed.Flags |= TEXTURE_FLAG_FILTER_NEAREST;

                Scene->TexturePack.push_back(Packed);
            }

            auto Atlas = image {
                .Width = ATLAS_WIDTH,
                .Height = ATLAS_HEIGHT,
                .Pixels = Pixels,
            };
            Scene->Images.push_back(Atlas);

            std::erase_if(Rects, [](stbrp_rect& R) { return R.was_packed; });
        }

        DirtyFlags |= SCENE_DIRTY_MATERIALS;
    }

    // Pack materials.
    if (DirtyFlags & SCENE_DIRTY_MATERIALS) {
        Scene->MaterialPack.clear();

        // Fallback material.
        {
            packed_material Packed = {
                .BaseColor = glm::vec3(1, 1, 1),
                .BaseWeight = 1.0f,
                .SpecularWeight = 0.0f,
                .TransmissionWeight = 0.0f,
                .EmissionColor = glm::vec3(0, 0, 0),
                .EmissionLuminance = 0.0f,
                .SpecularRoughnessAlpha = glm::vec2(1, 1),
                .BaseMetalness = 0.0f,
                .BaseDiffuseRoughness = 1.0f,
                .SpecularIOR = 1.5f,
                .TransmissionDepth = 0.0f,
            };
            Scene->MaterialPack.push_back(Packed);
        }

        for (material* Material : Scene->Materials) {
            packed_material Packed;

            Material->PackedMaterialIndex = static_cast<uint32_t>(Scene->MaterialPack.size());

            Packed.Opacity = Material->Opacity;

            Packed.BaseColor = Material->BaseColor;
            Packed.BaseWeight = Material->BaseWeight;
            Packed.BaseMetalness = Material->BaseMetalness;
            Packed.BaseDiffuseRoughness = Material->BaseDiffuseRoughness;

            Packed.SpecularColor = Material->SpecularColor;
            Packed.SpecularWeight = Material->SpecularWeight;

            // Precompute the specular roughness alpha parameter
            // from the roughness and anisotropy parameters.
            {
                float R = Material->SpecularRoughness;
                float S = 1 - Material->SpecularRoughnessAnisotropy;
                float AlphaX = R * R * glm::sqrt(2 / (1 + S * S));
                float AlphaY = S * AlphaX;
                Packed.SpecularRoughnessAlpha = { AlphaX, AlphaY };
            }

            Packed.SpecularIOR = Material->SpecularIOR;

            Packed.TransmissionColor = Material->TransmissionColor;
            Packed.TransmissionWeight = Material->TransmissionWeight;
            Packed.TransmissionScatter = Material->TransmissionScatter;
            Packed.TransmissionScatterAnisotropy = Material->TransmissionScatterAnisotropy;

            Packed.EmissionColor = Material->EmissionColor;
            Packed.EmissionLuminance = Material->EmissionLuminance;

            Packed.ScatteringRate = Material->ScatteringRate;

            Packed.BaseColorTextureIndex = GetPackedTextureIndex(Material->BaseColorTexture);

            Scene->MaterialPack.push_back(Packed);
        }

        DirtyFlags |= SCENE_DIRTY_MESHES;
        DirtyFlags |= SCENE_DIRTY_OBJECTS;
    }

    // Pack mesh face and node data.
    if (DirtyFlags & SCENE_DIRTY_MESHES) {
        uint32_t FaceCount = 0;
        uint32_t NodeCount = 0;
        for (mesh* Mesh : Scene->Meshes) {
            FaceCount += static_cast<uint32_t>(Mesh->Faces.size());
            NodeCount += static_cast<uint32_t>(Mesh->Nodes.size());
        }

        Scene->MeshFacePack.clear();
        Scene->MeshFacePack.reserve(FaceCount);
        Scene->MeshNodePack.clear();
        Scene->MeshNodePack.reserve(NodeCount);
        Scene->MeshFaceExtraPack.clear();
        Scene->MeshFaceExtraPack.reserve(NodeCount);

        for (mesh* Mesh : Scene->Meshes) {
            uint32_t FaceIndexBase = static_cast<uint32_t>(Scene->MeshFacePack.size());
            uint32_t NodeIndexBase = static_cast<uint32_t>(Scene->MeshNodePack.size());

            // Build the packed mesh faces.
            for (mesh_face const& Face : Mesh->Faces) {
                packed_mesh_face Packed;
                packed_mesh_face_extra PackedX;

                Packed.Position = Face.Vertices[0];

                material* Material = Mesh->Materials[Face.MaterialIndex];
                PackedX.MaterialIndex = Material ? Material->PackedMaterialIndex : 0;

                glm::vec3 AB = Face.Vertices[1] - Face.Vertices[0];
                glm::vec3 AC = Face.Vertices[2] - Face.Vertices[0];
                glm::vec3 Normal = glm::normalize(glm::cross(AB, AC));
                float D = -glm::dot(Normal, glm::vec3(Packed.Position));
                Packed.Plane = glm::vec4(Normal, D);

                float BB = glm::dot(AB, AB);
                float BC = glm::dot(AB, AC);
                float CC = glm::dot(AC, AC);
                float InvDet = 1.0f / (BB * CC - BC * BC);
                Packed.Base1 = (AB * CC - AC * BC) * InvDet;
                Packed.Base2 = (AC * BB - AB * BC) * InvDet;

                PackedX.Normals[0] = Face.Normals[0];
                PackedX.Normals[1] = Face.Normals[1];
                PackedX.Normals[2] = Face.Normals[2];

                PackedX.UVs[0] = Face.UVs[0];
                PackedX.UVs[1] = Face.UVs[1];
                PackedX.UVs[2] = Face.UVs[2];

                Scene->MeshFacePack.push_back(Packed);
                Scene->MeshFaceExtraPack.push_back(PackedX);
            }

            // Build the packed mesh nodes.
            for (mesh_node const& Node : Mesh->Nodes) {
                packed_mesh_node Packed;

                Packed.Minimum = Node.Bounds.Minimum;
                Packed.Maximum = Node.Bounds.Maximum;

                if (Node.ChildNodeIndex > 0) {
                    Packed.FaceBeginOrNodeIndex = NodeIndexBase + Node.ChildNodeIndex;
                    Packed.FaceEndIndex = 0;
                }
                else {
                    Packed.FaceBeginOrNodeIndex = FaceIndexBase + Node.FaceBeginIndex;
                    Packed.FaceEndIndex = FaceIndexBase + Node.FaceEndIndex;
                }

                Scene->MeshNodePack.push_back(Packed);
            }

            Mesh->PackedRootNodeIndex = NodeIndexBase;
        }

        DirtyFlags |= SCENE_DIRTY_OBJECTS;
    }

    // Pack object data.
    if (DirtyFlags & SCENE_DIRTY_OBJECTS) {
        Scene->SceneObjectPack.clear();
        Scene->SceneNodePack.resize(1);

        glm::mat4 const& OuterTransform = glm::mat4(1);

        uint32_t Priority = 0;
        for (entity* Entity : Scene->Root.Children)
            PackSceneObject(Scene, OuterTransform, Entity, Priority);

        std::vector<uint16_t> Map;

        for (uint32_t ObjectIndex = 0; ObjectIndex < Scene->SceneObjectPack.size(); ObjectIndex++) {
            packed_scene_object const& Object = Scene->SceneObjectPack[ObjectIndex];
            bounds Bounds = SceneObjectBounds(Scene, Object);

            uint16_t NodeIndex = static_cast<uint16_t>(Scene->SceneNodePack.size());
            Map.push_back(NodeIndex);

            packed_scene_node Node = {
                .Minimum = Bounds.Minimum,
                .ChildNodeIndices = 0,
                .Maximum = Bounds.Maximum,
                .ObjectIndex = ObjectIndex,
            };
            Scene->SceneNodePack.push_back(Node);
        }

        auto FindBestMatch = [](
            scene const* Scene,
            std::vector<uint16_t> const& Map,
            uint16_t IndexA
        ) -> uint16_t {
            glm::vec3 MinA = Scene->SceneNodePack[Map[IndexA]].Minimum;
            glm::vec3 MaxA = Scene->SceneNodePack[Map[IndexA]].Maximum;

            float BestArea = INFINITY;
            uint16_t BestIndexB = 0xFFFF;

            for (uint16_t IndexB = 0; IndexB < Map.size(); IndexB++) {
                if (IndexA == IndexB) continue;

                glm::vec3 MinB = Scene->SceneNodePack[Map[IndexB]].Minimum;
                glm::vec3 MaxB = Scene->SceneNodePack[Map[IndexB]].Maximum;
                glm::vec3 Size = glm::max(MaxA, MaxB) - glm::min(MinA, MinB);
                float Area = Size.x * Size.y + Size.y * Size.z + Size.z * Size.z;
                if (Area <= BestArea) {
                    BestArea = Area;
                    BestIndexB = IndexB;
                }
            }

            return BestIndexB;
        };

        uint16_t IndexA = 0;
        uint16_t IndexB = FindBestMatch(Scene, Map, IndexA);

        while (Map.size() > 1) {
            uint16_t IndexC = FindBestMatch(Scene, Map, IndexB);
            if (IndexA == IndexC) {
                uint16_t NodeIndexA = Map[IndexA];
                packed_scene_node const& NodeA = Scene->SceneNodePack[NodeIndexA];
                uint16_t NodeIndexB = Map[IndexB];
                packed_scene_node const& NodeB = Scene->SceneNodePack[NodeIndexB];

                packed_scene_node Node = {
                    .Minimum = glm::min(NodeA.Minimum, NodeB.Minimum),
                    .ChildNodeIndices = uint32_t(NodeIndexA) | uint32_t(NodeIndexB) << 16,
                    .Maximum = glm::max(NodeA.Maximum, NodeB.Maximum),
                    .ObjectIndex = OBJECT_INDEX_NONE,
                };

                Map[IndexA] = static_cast<uint16_t>(Scene->SceneNodePack.size());
                Map[IndexB] = Map.back();
                Map.pop_back();

                if (IndexA == Map.size())
                    IndexA = IndexB;

                Scene->SceneNodePack.push_back(Node);

                IndexB = FindBestMatch(Scene, Map, IndexA);
            }
            else {
                IndexA = IndexB;
                IndexB = IndexC;
            }
        }

        Scene->SceneNodePack[0] = Scene->SceneNodePack[Map[IndexA]];
        Scene->SceneNodePack[Map[IndexA]] = Scene->SceneNodePack.back();
        Scene->SceneNodePack.pop_back();

        //PrintSceneNode(scene, 0, 0);
    }

    Scene->DirtyFlags = 0;

    return DirtyFlags;
}

static void IntersectMeshFace(scene* Scene, ray Ray, uint32_t MeshFaceIndex, hit& Hit)
{
    packed_mesh_face Face = Scene->MeshFacePack[MeshFaceIndex];

    float R = glm::dot(Face.Plane.xyz(), Ray.Direction);
    if (R > -EPSILON && R < +EPSILON) return;

    float T = -(glm::dot(Face.Plane.xyz(), Ray.Origin) + Face.Plane.w) / R;
    if (T < 0 || T > Hit.Time) return;

    glm::vec3 V = Ray.Origin + Ray.Direction * T - Face.Position;
    float Beta = glm::dot(glm::vec3(Face.Base1), V);
    if (Beta < 0 || Beta > 1) return;
    float Gamma = glm::dot(glm::vec3(Face.Base2), V);
    if (Gamma < 0 || Beta + Gamma > 1) return;

    Hit.Time = T;
    Hit.ObjectType = OBJECT_TYPE_MESH_INSTANCE;
    Hit.ObjectIndex = OBJECT_INDEX_NONE;
    Hit.PrimitiveIndex = MeshFaceIndex;
    Hit.PrimitiveCoordinates = glm::vec3(1 - Beta - Gamma, Beta, Gamma);
}

static float IntersectMeshNodeBounds(ray Ray, float Reach, packed_mesh_node const& Node)
{
    // Compute ray time to the axis-aligned planes at the node bounding
    // box minimum and maximum corners.
    glm::vec3 Minimum = (Node.Minimum - Ray.Origin) / Ray.Direction;
    glm::vec3 Maximum = (Node.Maximum - Ray.Origin) / Ray.Direction;

    // For each coordinate axis, sort out which of the two coordinate
    // planes (at bounding box min/max points) comes earlier in time and
    // which one comes later.
    glm::vec3 Earlier = min(Minimum, Maximum);
    glm::vec3 Later = max(Minimum, Maximum);

    // Compute the ray entry and exit times.  The ray enters the box when
    // it has crossed all of the entry planes, so we take the maximum.
    // Likewise, the ray has exit the box when it has exit at least one
    // of the exit planes, so we take the minimum.
    float Entry = glm::max(glm::max(Earlier.x, Earlier.y), Earlier.z);
    float Exit = glm::min(glm::min(Later.x, Later.y), Later.z);

    // If the exit time is greater than the entry time, then the ray has
    // missed the box altogether.
    if (Exit < Entry) return INFINITY;

    // If the exit time is less than 0, then the box is behind the eye.
    if (Exit <= 0) return INFINITY;

    // If the entry time is greater than previous hit time, then the box
    // is occluded.
    if (Entry >= Reach) return INFINITY;

    return Entry;
}

static void IntersectMesh(scene* Scene, ray const& Ray, uint32_t RootNodeIndex, hit& Hit)
{
    uint32_t Stack[32];
    uint32_t Depth = 0;

    packed_mesh_node Node = Scene->MeshNodePack[RootNodeIndex];

    while (true) {
        // Leaf node or internal?
        if (Node.FaceEndIndex > 0) {
            // Leaf node, trace all geometry within.
            for (uint32_t FaceIndex = Node.FaceBeginOrNodeIndex; FaceIndex < Node.FaceEndIndex; FaceIndex++)
                IntersectMeshFace(Scene, Ray, FaceIndex, Hit);
        }
        else {
            // Internal node.
            // Load the first subnode as the node to be processed next.
            uint32_t Index = Node.FaceBeginOrNodeIndex;
            Node = Scene->MeshNodePack[Index];
            float Time = IntersectMeshNodeBounds(Ray, Hit.Time, Node);

            // Also load the second subnode to see if it is closer.
            uint32_t IndexB = Index + 1;
            packed_mesh_node NodeB = Scene->MeshNodePack[IndexB];
            float TimeB = IntersectMeshNodeBounds(Ray, Hit.Time, NodeB);

            // If the second subnode is strictly closer than the first one,
            // then it was definitely hit, so process it next.
            if (Time > TimeB) {
                // If the first subnode was also hit, then set it aside for later.
                if (Time < INFINITY) {
                    assert(Depth < std::size(Stack));
                    Stack[Depth++] = Index;
                }
                Node = NodeB;
                continue;
            }

            // The first subnode is at least as close as the second one.
            // If the second subnode was hit, then both of them were,
            // and we should set the second one aside for later.
            if (TimeB < INFINITY) {
                assert(Depth < std::size(Stack));
                Stack[Depth++] = IndexB;
                continue;
            }

            // The first subnode is at least as close as the second one,
            // and the second subnode was not hit.  If the first one was
            // hit, then process it next.
            if (Time < INFINITY) continue;
        }

        // Just processed a leaf node or an internal node with no intersecting
        // subnodes.  If the stack is also empty, then we are done.
        if (Depth == 0) break;

        // Pull a node from the stack.
        Node = Scene->MeshNodePack[Stack[--Depth]];
    }
}

static void IntersectObject(scene* Scene, ray const& WorldRay, uint32_t ObjectIndex, hit& Hit)
{
    packed_scene_object Object = Scene->SceneObjectPack[ObjectIndex];

    ray Ray = TransformRay(WorldRay, Object.Transform.From);

    if (Object.Type == OBJECT_TYPE_MESH_INSTANCE) {
        IntersectMesh(Scene, Ray, Object.MeshRootNodeIndex, Hit);
        if (Hit.ObjectIndex == OBJECT_INDEX_NONE)
            Hit.ObjectIndex = ObjectIndex;
    }

    if (Object.Type == OBJECT_TYPE_PLANE) {
        float T = -Ray.Origin.z / Ray.Direction.z;
        if (T < 0 || T > Hit.Time) return;

        Hit.Time = T;
        Hit.ObjectType = OBJECT_TYPE_PLANE;
        Hit.ObjectIndex = ObjectIndex;
        Hit.PrimitiveIndex = 0;
        Hit.PrimitiveCoordinates = glm::vec3(glm::fract(Ray.Origin.xy() + Ray.Direction.xy() * T), 0);
    }

    if (Object.Type == OBJECT_TYPE_SPHERE) {
        float V = glm::dot(Ray.Direction, Ray.Direction);
        float P = glm::dot(Ray.Origin, Ray.Direction);
        float Q = glm::dot(Ray.Origin, Ray.Origin) - 1.0f;
        float D2 = P * P - Q * V;
        if (D2 < 0) return;

        float D = glm::sqrt(D2);
        if (D < P) return;

        float S0 = -P - D;
        float S1 = -P + D;
        float S = S0 < 0 ? S1 : S0;
        if (S < 0 || S > V * Hit.Time) return;

        Hit.Time = S / V;
        Hit.ObjectType = OBJECT_TYPE_SPHERE;
        Hit.ObjectIndex = ObjectIndex;
    }

    if (Object.Type == OBJECT_TYPE_CUBE) {
        glm::vec3 Minimum = (glm::vec3(-1,-1,-1) - Ray.Origin) / Ray.Direction;
        glm::vec3 Maximum = (glm::vec3(+1,+1,+1) - Ray.Origin) / Ray.Direction;
        glm::vec3 Earlier = min(Minimum, Maximum);
        glm::vec3 Later = max(Minimum, Maximum);
        float T0 = glm::max(glm::max(Earlier.x, Earlier.y), Earlier.z);
        float T1 = glm::min(glm::min(Later.x, Later.y), Later.z);
        if (T1 < T0) return;
        if (T1 <= 0) return;
        if (T0 >= Hit.Time) return;

        float T = T0 < 0 ? T1 : T0;

        Hit.Time = T;
        Hit.ObjectType = OBJECT_TYPE_CUBE;
        Hit.ObjectIndex = ObjectIndex;
    }
}

static void Intersect(scene* Scene, ray const& WorldRay, hit& Hit)
{
    for (uint32_t ObjectIndex = 0; ObjectIndex < Scene->SceneObjectPack.size(); ObjectIndex++)
        IntersectObject(Scene, WorldRay, ObjectIndex, Hit);
}

bool Trace(scene* Scene, ray const& Ray, hit& Hit)
{
    Hit.Time = INFINITY;
    Intersect(Scene, Ray, Hit);
    return Hit.Time < INFINITY;
}
