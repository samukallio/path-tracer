#include "utility/tiny_obj_loader.h"
#include "utility/stb_image.h"
#include "utility/stb_rect_pack.h"

#include "scene/scene.h"

#include <unordered_map>
#include <format>
#include <filesystem>
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

glm::vec4 ColorToSpectrum(scene* Scene, glm::vec4 const& Color)
{
    glm::vec3 Beta = GetParametricSpectrumCoefficients(Scene->RGBSpectrumTable, Color.xyz());
    return glm::vec4(Beta, Color.a);
}

entity* CreateEntityRaw(entity_type Type)
{
    switch (Type) {
        case ENTITY_TYPE_ROOT:
            return new root;
            break;
        case ENTITY_TYPE_CONTAINER:
            return new container;
            break;
        case ENTITY_TYPE_CAMERA:
            return new camera;
            break;
        case ENTITY_TYPE_MESH_INSTANCE:
            return new mesh_instance;
            break;
        case ENTITY_TYPE_PLANE:
            return new plane;
            break;
        case ENTITY_TYPE_SPHERE:
            return new sphere;
            break;
        case ENTITY_TYPE_CUBE:
            return new cube;
            break;
        default:
            assert(false);
            break;
    }

    return nullptr;
}

entity* CreateEntity(scene* Scene, entity_type Type, entity* Parent)
{
    entity* Entity = CreateEntityRaw(Type);

    if (!Parent) Parent = &Scene->Root;

    Entity->Parent = Parent;
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

    Entity->Parent = Parent;
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

void DestroyEntity(scene* Scene, entity* Entity)
{
    entity* Parent = Entity->Parent;
    if (Parent) {
        std::erase(Parent->Children, Entity);
    }

    for (entity* Child : Entity->Children)
        DestroyEntity(Scene, Child);

    delete Entity;
}

template<typename callback>
static void ForEachEntity(entity* Entity, callback&& Callback)
{
    Callback(Entity);
    for (entity* Child : Entity->Children)
        ForEachEntity(Child, Callback);
}

texture* CreateCheckerTexture(scene* Scene, char const* Name, texture_type Type, glm::vec4 const& ColorA, glm::vec4 const& ColorB)
{
    auto Pixels = new glm::vec4[4];

    Pixels[0] = ColorA;
    Pixels[1] = ColorB;
    Pixels[2] = ColorB;
    Pixels[3] = ColorA;

    auto Texture = new texture {
        .Name = Name,
        .Type = Type,
        .Width = 2,
        .Height = 2,
        .Pixels = Pixels,
    };
    Scene->Textures.push_back(Texture);

    Scene->DirtyFlags |= SCENE_DIRTY_TEXTURES;

    return Texture;
}

texture* LoadTexture(scene* Scene, char const* Path, texture_type Type, char const* Name)
{
    int Width, Height, ChannelsInFile;
    glm::vec4* Pixels = reinterpret_cast<glm::vec4*>(stbi_loadf(Path, &Width, &Height, &ChannelsInFile, 4));
    if (!Pixels) return nullptr;

    auto Texture = new texture {
        .Name = Name ? Name : std::filesystem::path(Path).filename().string(),
        .Type = Type,
        .Width = static_cast<uint32_t>(Width),
        .Height = static_cast<uint32_t>(Height),
        .Pixels = Pixels,
    };
    Scene->Textures.push_back(Texture);

    Scene->DirtyFlags |= SCENE_DIRTY_TEXTURES;

    return Texture;
}

void DestroyTexture(scene* Scene, texture* Texture)
{
    bool MaterialsDirty = false;
    for (material* Material : Scene->Materials) {
        if (Material->BaseColorTexture == Texture) {
            Material->BaseColorTexture = nullptr;
            MaterialsDirty = true;
        }
        if (Material->SpecularRoughnessTexture == Texture) {
            Material->SpecularRoughnessTexture = nullptr;
            MaterialsDirty = true;
        }
        if (Material->EmissionColorTexture == Texture) {
            Material->EmissionColorTexture = nullptr;
            MaterialsDirty = true;
        }
    }

    if (MaterialsDirty) Scene->DirtyFlags |= SCENE_DIRTY_MATERIALS;

    std::erase(Scene->Textures, Texture);
    Scene->DirtyFlags |= SCENE_DIRTY_TEXTURES;

    free((void*)Texture->Pixels);
    delete Texture;
}

void DestroyMesh(scene* Scene, mesh* Mesh)
{
    ForEachEntity(&Scene->Root, [Scene, Mesh](entity* Entity) {
        if (Entity->Type == ENTITY_TYPE_MESH_INSTANCE) {
            auto MeshInstance = static_cast<mesh_instance*>(Entity);
            if (MeshInstance->Mesh == Mesh) {
                MeshInstance->Mesh = nullptr;
                Scene->DirtyFlags |= SCENE_DIRTY_SHAPES;
            }
        }
    });

    for (prefab* Prefab : Scene->Prefabs) {
        ForEachEntity(Prefab->Entity, [Scene, Mesh](entity* Entity) {
            if (Entity->Type == ENTITY_TYPE_MESH_INSTANCE) {
                auto MeshInstance = static_cast<mesh_instance*>(Entity);
                if (MeshInstance->Mesh == Mesh)
                    MeshInstance->Mesh = nullptr;
            }
        });
    }

    std::erase(Scene->Meshes, Mesh);
    Scene->DirtyFlags |= SCENE_DIRTY_MESHES;

    delete Mesh;
}

material* CreateMaterial(scene* Scene, char const* Name)
{
    auto Material = new material;
    Material->Name = Name;
    Scene->Materials.push_back(Material);

    Scene->DirtyFlags |= SCENE_DIRTY_MATERIALS;

    return Material;
}

void DestroyMaterial(scene* Scene, material* Material)
{
    ForEachEntity(&Scene->Root, [Scene, Material](entity* Entity) {
        switch (Entity->Type) {
            case ENTITY_TYPE_MESH_INSTANCE: {
                auto MeshInstance = static_cast<mesh_instance*>(Entity);
                if (MeshInstance->Material == Material) {
                    MeshInstance->Material = nullptr;
                    Scene->DirtyFlags |= SCENE_DIRTY_SHAPES;
                }
                break;
            }
            case ENTITY_TYPE_PLANE: {
                auto Plane = static_cast<plane*>(Entity);
                if (Plane->Material == Material) {
                    Plane->Material = nullptr;
                    Scene->DirtyFlags |= SCENE_DIRTY_SHAPES;
                }
                break;
            }
            case ENTITY_TYPE_SPHERE: {
                auto Sphere = static_cast<sphere*>(Entity);
                if (Sphere->Material == Material) {
                    Sphere->Material = nullptr;
                    Scene->DirtyFlags |= SCENE_DIRTY_SHAPES;
                }
                break;
            }
            case ENTITY_TYPE_CUBE: {
                auto Cube = static_cast<cube*>(Entity);
                if (Cube->Material == Material) {
                    Cube->Material = nullptr;
                    Scene->DirtyFlags |= SCENE_DIRTY_SHAPES;
                }
                break;
            }
        }
    });

    std::erase(Scene->Materials, Material);
    Scene->DirtyFlags |= SCENE_DIRTY_MATERIALS;
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
            if (length > EPSILON) {
                Normals[i+0] /= length;
                Normals[i+1] /= length;
                Normals[i+2] /= length;
            }
            else {
                Normals[i+0] = 0;
                Normals[i+1] = 0;
                Normals[i+2] = 1;
            }
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

        std::tuple<std::string, texture_type, texture**> Textures[] = {
            {
                FileMaterial.diffuse_texname,
                TEXTURE_TYPE_REFLECTANCE_WITH_ALPHA,
                &Material->BaseColorTexture,
            },
            {
                FileMaterial.emissive_texname,
                TEXTURE_TYPE_RADIANCE,
                &Material->EmissionColorTexture,
            },
        };

        for (auto const& [Name, Type, TexturePtr] : Textures) {
            if (!Name.empty()) {
                if (!TextureMap.contains(Name)) {
                    std::string Path = std::format("{}/{}", Options->DirectoryPath, Name);
                    TextureMap[Name] = LoadTexture(Scene, Path.c_str(), Type, Name.c_str());
                }
                *TexturePtr = TextureMap[Name];
            }
            else {
                *TexturePtr = nullptr;
            }
        }

        Materials.push_back(Material);
    }

    std::string ModelName;
    if (Options->Name) {
        ModelName = Options->Name;
    }
    else {
        std::filesystem::path P = Path;
        ModelName = P.stem().string();
    }

    // Import meshes.
    std::vector<mesh*> Meshes;
    std::unordered_map<mesh*, material*> MeshMaterials;
    std::vector<glm::vec3> Origins;
    {
        mesh* Mesh = nullptr;
        material* MeshMaterial = nullptr;

        if (Options->MergeIntoSingleMesh) {
            Mesh = new mesh;
            Mesh->Name = ModelName;
            Meshes.push_back(Mesh);
            Origins.push_back(glm::vec3());

            size_t FaceCount = 0;
            for (auto const& Shape : Shapes)
                FaceCount += Shape.mesh.indices.size() / 3;
            Mesh->Faces.reserve(FaceCount);
        }

        glm::vec3 Origin {};

        for (size_t ShapeIndex = 0; ShapeIndex < Shapes.size(); ShapeIndex++) {
            tinyobj::shape_t const& Shape = Shapes[ShapeIndex];
            size_t ShapeIndexCount = Shape.mesh.indices.size();

            if (ShapeIndexCount == 0)
                continue;

            if (!Options->MergeIntoSingleMesh) {
                Mesh = new mesh;
                Mesh->Name = !Shape.name.empty() ? Shape.name : std::format("{} {}", ModelName, ShapeIndex);
                Mesh->Faces.reserve(ShapeIndexCount / 3);
                Meshes.push_back(Mesh);

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

                if (MaterialId >= 0)
                    MeshMaterial = Materials[MaterialId];

                Face.Centroid = (Face.Vertices[0] + Face.Vertices[1] + Face.Vertices[2]) / 3.0f;

                Mesh->Faces.push_back(Face);
            }

            MeshMaterials[Mesh] = MeshMaterial;
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

    if (Meshes.size() == 1) {
        auto Instance = new mesh_instance;
        Instance->Name = Meshes[0]->Name;
        Instance->Mesh = Meshes[0];
        Instance->Material = MeshMaterials[Meshes[0]];
        Prefab->Entity = Instance;
    }
    else {
        auto Container = new container;
        Container->Name = ModelName;
        for (size_t I = 0; I < Meshes.size(); I++) {
            mesh* Mesh = Meshes[I];
            glm::vec3 const& Origin = Origins[I];

            auto Instance = new mesh_instance;
            Instance->Name = Mesh->Name;
            Instance->Mesh = Mesh;
            Instance->Material = MeshMaterials[Mesh];
            Instance->Transform.Position = Options->VertexTransform * glm::vec4(Origin, 1);
            Container->Children.push_back(Instance);
        }
        Prefab->Entity = Container;
    }

    Scene->Prefabs.push_back(Prefab);

    return Prefab;
}

void DestroyPrefab(scene* Scene, prefab* Prefab)
{
    DestroyEntity(Scene, Prefab->Entity);
    std::erase(Scene->Prefabs, Prefab);
    delete Prefab;
}

scene* CreateScene()
{
    auto Scene = new scene;

    Scene->Root.Name = "Scene";

    char const* SRGB_SPECTRUM_TABLE_FILE = "sRGBSpectrumTable.dat";

    Scene->RGBSpectrumTable = new parametric_spectrum_table;
    if (!LoadParametricSpectrumTable(Scene->RGBSpectrumTable, SRGB_SPECTRUM_TABLE_FILE)) {
        printf("%s not found, generating it.\n", SRGB_SPECTRUM_TABLE_FILE);
        printf("This will probably take a few minutes...\n");
        BuildParametricSpectrumTableForSRGB(Scene->RGBSpectrumTable);
        SaveParametricSpectrumTable(Scene->RGBSpectrumTable, SRGB_SPECTRUM_TABLE_FILE);
    }

    plane* Plane = (plane*)CreateEntity(Scene, ENTITY_TYPE_PLANE);
    Plane->Name = "Plane";
    Plane->Material = CreateMaterial(Scene, "Plane Material");
    Plane->Material->BaseColorTexture = CreateCheckerTexture(Scene, "Plane Texture", TEXTURE_TYPE_REFLECTANCE_WITH_ALPHA, glm::vec4(1,1,1,1), glm::vec4(0.5,0.5,0.5,1));
    Plane->Material->BaseColorTexture->EnableNearestFiltering = true;

    camera* Camera = (camera*)CreateEntity(Scene, ENTITY_TYPE_CAMERA);
    Camera->Name = "Camera";
    Camera->Transform.Position = { 0, 0, 1 };

    Scene->DirtyFlags = SCENE_DIRTY_ALL;

    return Scene;
}

void DestroyScene(scene* Scene)
{
    while (!Scene->Root.Children.empty())
        DestroyEntity(Scene, Scene->Root.Children[0]);
    for (prefab* Prefab : Scene->Prefabs)
        delete Prefab;
    for (mesh* Mesh : Scene->Meshes)
        delete Mesh;
    for (material* Material : Scene->Materials)
        delete Material;
    for (texture* Texture : Scene->Textures)
        delete Texture;
    delete Scene->RGBSpectrumTable;
    delete Scene;
}

//bool LoadSkybox(scene* Scene, char const* Path)
//{
    //int Width, Height, Components;

    //Scene->SkyboxPixels = reinterpret_cast<glm::vec4*>(stbi_loadf(Path, &Width, &Height, &Components, STBI_rgb_alpha));
    //Scene->SkyboxWidth = static_cast<uint32_t>(Width);
    //Scene->SkyboxHeight = static_cast<uint32_t>(Height);

    //for (int Y = 0; Y < Height; Y++) {
    //    for (int X = 0; X < Width; X++) {
    //        int Index = Y * Width + X;

    //        glm::vec3 Color = Scene->SkyboxPixels[Index].rgb();
    //        float Intensity = 2 * glm::max(glm::max(Color.r, Color.g), Color.b);
    //        glm::vec3 Beta = GetParametricSpectrumCoefficients(Scene->RGBSpectrumTable, Color / Intensity);
    //        Scene->SkyboxPixels[Index] = glm::vec4(Beta, Intensity);
    //    }
    //}

    //Scene->DirtyFlags |= SCENE_DIRTY_SKYBOX;

    //float* Pixels = Scene->SkyboxPixels;

    //glm::vec3 Mean = {};
    //float WeightSum = 0.0f;
    //for (int Y = 0; Y < Height; Y++) {
    //    float Theta = (0.5f - (Y + 0.5f) / Height) * PI;
    //    for (int X = 0; X < Width; X++) {
    //        float Phi = ((X + 0.5f) / Width - 0.5f) * TAU;

    //        int Index = (Y * Width + X) * 4;
    //        float R = Pixels[Index+0];
    //        float G = Pixels[Index+1];
    //        float B = Pixels[Index+2];
    //        float Luminance = 0.2126f * R + 0.7152f * G + 0.0722f * B;
    //        float Area = glm::cos(Theta);

    //        float Weight = Area * Luminance * Luminance;

    //        glm::vec3 Direction = {
    //            glm::cos(Theta) * glm::cos(Phi),
    //            glm::cos(Theta) * glm::sin(Phi),
    //            glm::sin(Theta),
    //        };

    //        Mean += Weight * Direction;
    //        WeightSum += Weight;
    //    }
    //}
    //Mean /= WeightSum;

    //float MeanLength = glm::length(Mean);

    //glm::vec3 FrameZ = Mean / MeanLength;
    //glm::vec3 FrameX = OrthogonalVector(FrameZ);
    //glm::vec3 FrameY = glm::cross(FrameX, FrameZ);

    //float Concentration = MeanLength * (3.0f - MeanLength * MeanLength) / (1 - MeanLength * MeanLength);

    //Scene->SkyboxDistributionFrame = glm::mat3(FrameX, FrameY, FrameZ);
    //Scene->SkyboxDistributionConcentration = Concentration;

//    return true;
//}

static void PackShape(scene* Scene, glm::mat4 const& OuterTransform, entity* Entity)
{
    if (!Entity->Active)
        return;

    glm::mat4 InnerTransform
        = OuterTransform
        * glm::translate(glm::mat4(1), Entity->Transform.Position)
        * glm::orientate4(Entity->Transform.Rotation)
        * glm::scale(glm::mat4(1), Entity->Transform.Scale);

    for (entity* Child : Entity->Children)
        PackShape(Scene, InnerTransform, Child);

    packed_shape Packed;

    Packed.MaterialIndex = 0;

    switch (Entity->Type) {
        case ENTITY_TYPE_MESH_INSTANCE: {
            auto Instance = static_cast<mesh_instance*>(Entity);
            if (!Instance->Mesh) return;
            Packed.MaterialIndex = GetPackedMaterialIndex(Instance->Material);
            Packed.MeshRootNodeIndex = Instance->Mesh->PackedRootNodeIndex;
            Packed.Type = SHAPE_TYPE_MESH_INSTANCE;
            break;
        }
        case ENTITY_TYPE_PLANE: {
            auto Plane = static_cast<plane*>(Entity);
            Packed.MaterialIndex = GetPackedMaterialIndex(Plane->Material);
            Packed.Type = SHAPE_TYPE_PLANE;
            break;
        }
        case ENTITY_TYPE_SPHERE: {
            auto Sphere = static_cast<sphere*>(Entity);
            Packed.MaterialIndex = GetPackedMaterialIndex(Sphere->Material);
            Packed.Type = SHAPE_TYPE_SPHERE;
            break;
        }
        case ENTITY_TYPE_CUBE: {
            auto Cube = static_cast<cube*>(Entity);
            Packed.MaterialIndex = GetPackedMaterialIndex(Cube->Material);
            Packed.Type = SHAPE_TYPE_CUBE;
            break;
        }
        default: {
            return;
        }
    }

    Packed.Transform.To = InnerTransform;
    Packed.Transform.From = glm::inverse(InnerTransform);

    Entity->PackedShapeIndex = static_cast<uint32_t>(Scene->ShapePack.size());

    Scene->ShapePack.push_back(Packed);
}

static bounds ShapeBounds(scene const* Scene, packed_shape const& Object)
{
    glm::vec4 Corners[8] = {};

    switch (Object.Type) {
        case SHAPE_TYPE_MESH_INSTANCE: {
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
        case SHAPE_TYPE_PLANE: {
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
        case SHAPE_TYPE_SPHERE:
        case SHAPE_TYPE_CUBE: {
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

void PrintShapeNode(scene* Scene, uint16_t Index, int Depth)
{
    packed_shape_node const& Node = Scene->ShapeNodePack[Index];

    for (int I = 0; I < Depth; I++) printf("  ");

    if (Node.ChildNodeIndices > 0) {
        uint16_t IndexA = Node.ChildNodeIndices & 0xFFFF;
        uint16_t IndexB = Node.ChildNodeIndices >> 16;
        printf("Node %u\n", Index);
        PrintShapeNode(Scene, IndexA, Depth+1);
        PrintShapeNode(Scene, IndexB, Depth+1);
    }
    else {
        printf("Leaf %u (object %lu)\n", Index, Node.ShapeIndex);
    }
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

            glm::vec4* Pixels = new glm::vec4[4096 * 4096];

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
                    glm::vec4 const* Src = Texture->Pixels + Y * Texture->Width;
                    glm::vec4* Dst = Pixels + (Rect.y + Y) * ATLAS_WIDTH + Rect.x;
                    if (Texture->Type == TEXTURE_TYPE_RAW) {
                        memcpy(Dst, Src, Texture->Width * sizeof(glm::vec4));
                    }
                    else if (Texture->Type == TEXTURE_TYPE_REFLECTANCE_WITH_ALPHA) {
                        for (uint32_t X = 0; X < Texture->Width; X++) {
                            glm::vec4 Value = *Src++;
                            glm::vec3 Beta = GetParametricSpectrumCoefficients(Scene->RGBSpectrumTable, Value.xyz());
                            *Dst++ = glm::vec4(Beta, Value.a);
                        }
                    }
                    else if (Texture->Type == TEXTURE_TYPE_RADIANCE) {
                        for (uint32_t X = 0; X < Texture->Width; X++) {
                            glm::vec4 Color = *Src++;
                            float Intensity = 2 * glm::max(glm::max(Color.r, Color.g), Color.b);
                            if (Intensity > 1e-6f) {
                                glm::vec3 Beta = GetParametricSpectrumCoefficients(Scene->RGBSpectrumTable, Color / Intensity);
                                *Dst++ = glm::vec4(Beta, Intensity);
                            }
                            else {
                                *Dst++ = glm::vec4(0, 0, 0, 0);
                            }
                        }
                    }
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
                .BaseSpectrum                   = GetParametricSpectrumCoefficients(Scene->RGBSpectrumTable, glm::vec3(1, 1, 1)),
                .BaseWeight                     = 1.0f,
                .SpecularWeight                 = 0.0f,
                .TransmissionWeight             = 0.0f,
                .EmissionSpectrum               = GetParametricSpectrumCoefficients(Scene->RGBSpectrumTable, glm::vec3(0, 0, 0)),
                .EmissionLuminance              = 0.0f,
                .CoatWeight                     = 0.0f,
                .BaseMetalness                  = 0.0f,
                .BaseDiffuseRoughness           = 1.0f,
                .SpecularIOR                    = 1.5f,
                .SpecularRoughness              = 0.0f,
                .SpecularRoughnessAnisotropy    = 0.0f,
                .TransmissionDepth              = 0.0f,
                .BaseSpectrumTextureIndex       = TEXTURE_INDEX_NONE,
                .SpecularRoughnessTextureIndex  = TEXTURE_INDEX_NONE,
                .EmissionSpectrumTextureIndex   = TEXTURE_INDEX_NONE,
                .LayerBounceLimit               = 8,
            };
            Scene->MaterialPack.push_back(Packed);
        }

        for (material* Material : Scene->Materials) {
            packed_material Packed;

            Material->PackedMaterialIndex = static_cast<uint32_t>(Scene->MaterialPack.size());

            Packed.Opacity = Material->Opacity;

            Packed.BaseSpectrum = GetParametricSpectrumCoefficients(Scene->RGBSpectrumTable, Material->BaseColor);
            Packed.BaseWeight = Material->BaseWeight;
            Packed.BaseMetalness = Material->BaseMetalness;
            Packed.BaseDiffuseRoughness = Material->BaseDiffuseRoughness;

            Packed.SpecularSpectrum = GetParametricSpectrumCoefficients(Scene->RGBSpectrumTable, Material->SpecularColor);
            Packed.SpecularWeight = Material->SpecularWeight;

            Packed.SpecularIOR = Material->SpecularIOR;
            Packed.SpecularRoughness = Material->SpecularRoughness;
            Packed.SpecularRoughnessAnisotropy = Material->SpecularRoughnessAnisotropy;

            Packed.TransmissionWeight = Material->TransmissionWeight;
            Packed.TransmissionSpectrum = GetParametricSpectrumCoefficients(Scene->RGBSpectrumTable, Material->TransmissionColor);
            Packed.TransmissionDepth = Material->TransmissionDepth;
            Packed.TransmissionScatterSpectrum = GetParametricSpectrumCoefficients(Scene->RGBSpectrumTable, Material->TransmissionScatter);
            Packed.TransmissionScatterAnisotropy = Material->TransmissionScatterAnisotropy;
            Packed.TransmissionDispersionScale = Material->TransmissionDispersionScale;
            Packed.TransmissionDispersionAbbeNumber = Material->TransmissionDispersionAbbeNumber;

            Packed.CoatWeight = Material->CoatWeight;
            Packed.CoatColorSpectrum = GetParametricSpectrumCoefficients(Scene->RGBSpectrumTable, Material->CoatColor);
            Packed.CoatRoughness = Material->CoatRoughness;
            Packed.CoatRoughnessAnisotropy = Material->CoatRoughnessAnisotropy;
            Packed.CoatIOR = Material->CoatIOR;
            Packed.CoatDarkening = Material->CoatDarkening;

            Packed.EmissionSpectrum = GetParametricSpectrumCoefficients(Scene->RGBSpectrumTable, Material->EmissionColor);
            Packed.EmissionLuminance = Material->EmissionLuminance;

            Packed.BaseSpectrumTextureIndex = GetPackedTextureIndex(Material->BaseColorTexture);
            Packed.SpecularRoughnessTextureIndex = GetPackedTextureIndex(Material->SpecularRoughnessTexture);
            Packed.EmissionSpectrumTextureIndex = GetPackedTextureIndex(Material->EmissionColorTexture);

            Packed.LayerBounceLimit = static_cast<uint32_t>(Material->LayerBounceLimit);

            Scene->MaterialPack.push_back(Packed);
        }

        DirtyFlags |= SCENE_DIRTY_SHAPES;
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

                Packed.Position0 = Face.Vertices[0];
                Packed.Position1 = Face.Vertices[1];
                Packed.Position2 = Face.Vertices[2];

                PackedX.PackedNormals[0] = PackUnitVector(Face.Normals[0]);
                PackedX.PackedNormals[1] = PackUnitVector(Face.Normals[1]);
                PackedX.PackedNormals[2] = PackUnitVector(Face.Normals[2]);

                PackedX.PackedUVs[0] = glm::packHalf2x16(Face.UVs[0]);
                PackedX.PackedUVs[1] = glm::packHalf2x16(Face.UVs[1]);
                PackedX.PackedUVs[2] = glm::packHalf2x16(Face.UVs[2]);

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

        DirtyFlags |= SCENE_DIRTY_SHAPES;
    }

    // Pack object data.
    if (DirtyFlags & SCENE_DIRTY_SHAPES) {
        Scene->ShapePack.clear();
        Scene->ShapeNodePack.resize(1);

        glm::mat4 const& OuterTransform = glm::mat4(1);

        for (entity* Entity : Scene->Root.Children)
            PackShape(Scene, OuterTransform, Entity);

        std::vector<uint16_t> Map;

        for (uint32_t ShapeIndex = 0; ShapeIndex < Scene->ShapePack.size(); ShapeIndex++) {
            packed_shape const& Object = Scene->ShapePack[ShapeIndex];
            bounds Bounds = ShapeBounds(Scene, Object);

            uint16_t NodeIndex = static_cast<uint16_t>(Scene->ShapeNodePack.size());
            Map.push_back(NodeIndex);

            packed_shape_node Node = {
                .Minimum = Bounds.Minimum,
                .ChildNodeIndices = 0,
                .Maximum = Bounds.Maximum,
                .ShapeIndex = ShapeIndex,
            };
            Scene->ShapeNodePack.push_back(Node);
        }

        auto FindBestMatch = [](
            scene const* Scene,
            std::vector<uint16_t> const& Map,
            uint16_t IndexA
        ) -> uint16_t {
            glm::vec3 MinA = Scene->ShapeNodePack[Map[IndexA]].Minimum;
            glm::vec3 MaxA = Scene->ShapeNodePack[Map[IndexA]].Maximum;

            float BestArea = INFINITY;
            uint16_t BestIndexB = 0xFFFF;

            for (uint16_t IndexB = 0; IndexB < Map.size(); IndexB++) {
                if (IndexA == IndexB) continue;

                glm::vec3 MinB = Scene->ShapeNodePack[Map[IndexB]].Minimum;
                glm::vec3 MaxB = Scene->ShapeNodePack[Map[IndexB]].Maximum;
                glm::vec3 Size = glm::max(MaxA, MaxB) - glm::min(MinA, MinB);
                float Area = Size.x * Size.y + Size.y * Size.z + Size.z * Size.z;
                if (Area <= BestArea) {
                    BestArea = Area;
                    BestIndexB = IndexB;
                }
            }

            return BestIndexB;
        };

        if (!Scene->ShapePack.empty()) {
            uint16_t IndexA = 0;
            uint16_t IndexB = FindBestMatch(Scene, Map, IndexA);

            while (Map.size() > 1) {
                uint16_t IndexC = FindBestMatch(Scene, Map, IndexB);
                if (IndexA == IndexC) {
                    uint16_t NodeIndexA = Map[IndexA];
                    packed_shape_node const& NodeA = Scene->ShapeNodePack[NodeIndexA];
                    uint16_t NodeIndexB = Map[IndexB];
                    packed_shape_node const& NodeB = Scene->ShapeNodePack[NodeIndexB];

                    packed_shape_node Node = {
                        .Minimum = glm::min(NodeA.Minimum, NodeB.Minimum),
                        .ChildNodeIndices = uint32_t(NodeIndexA) | uint32_t(NodeIndexB) << 16,
                        .Maximum = glm::max(NodeA.Maximum, NodeB.Maximum),
                        .ShapeIndex = SHAPE_INDEX_NONE,
                    };

                    Map[IndexA] = static_cast<uint16_t>(Scene->ShapeNodePack.size());
                    Map[IndexB] = Map.back();
                    Map.pop_back();

                    if (IndexA == Map.size())
                        IndexA = IndexB;

                    Scene->ShapeNodePack.push_back(Node);

                    IndexB = FindBestMatch(Scene, Map, IndexA);
                }
                else {
                    IndexA = IndexB;
                    IndexB = IndexC;
                }
            }

            Scene->ShapeNodePack[0] = Scene->ShapeNodePack[Map[IndexA]];
            Scene->ShapeNodePack[Map[IndexA]] = Scene->ShapeNodePack.back();
            Scene->ShapeNodePack.pop_back();
        }

        // To update the ShapeCount.
        DirtyFlags |= SCENE_DIRTY_GLOBALS;

        //PrintShapeNode(scene, 0, 0);
    }

    // Pack scene global data.
    if (DirtyFlags & SCENE_DIRTY_GLOBALS) {
        packed_scene_globals* G = &Scene->Globals;

        G->SkyboxDistributionFrame = Scene->SkyboxDistributionFrame;
        G->SkyboxDistributionConcentration = Scene->SkyboxDistributionConcentration;
        G->SkyboxBrightness = Scene->Root.SkyboxBrightness;
        G->SkyboxTextureIndex = GetPackedTextureIndex(Scene->Root.SkyboxTexture);
        G->SceneScatterRate = Scene->Root.ScatterRate;
        G->ShapeCount = static_cast<uint>(Scene->ShapePack.size());
    }

    Scene->DirtyFlags = 0;

    return DirtyFlags;
}

static entity* FindEntityByPackedShapeIndexRecursive(entity* Entity, uint32_t PackedShapeIndex)
{
    if (Entity->Active) {
        if (Entity->PackedShapeIndex == PackedShapeIndex)
            return Entity;
        for (entity* Child : Entity->Children)
            if (entity* Found = FindEntityByPackedShapeIndexRecursive(Child, PackedShapeIndex); Found)
                return Found;
    }
    return nullptr;
};

entity* FindEntityByPackedShapeIndex(scene* Scene, uint32_t PackedShapeIndex)
{
    return FindEntityByPackedShapeIndexRecursive(&Scene->Root, PackedShapeIndex);
}
