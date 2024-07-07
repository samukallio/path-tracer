#include "core/common.h"
#include "core/json.hpp"
#include "core/miniz.h"
#include "scene/scene.h"

#include <fstream>
#include <filesystem>

using nlohmann::json;

void WriteCompressed(std::ostream& Out, void const* Data, size_t Size)
{
    mz_ulong CompressedSize = static_cast<mz_ulong>(std::max(Size, 1024ull));
    auto CompressedData = std::vector<unsigned char>(CompressedSize);
    int Result = mz_compress(
        CompressedData.data(), &CompressedSize,
        static_cast<unsigned char const*>(Data),
        static_cast<mz_ulong>(Size));
    assert(Result == MZ_OK);
    Out.write((char const*)&CompressedSize, sizeof(mz_ulong));
    Out.write((char const*)CompressedData.data(), CompressedSize);
}

void ReadCompressed(std::istream& In, void* Data, size_t Size)
{
    mz_ulong CompressedSize = 0;
    In.read((char*)&CompressedSize, sizeof(mz_ulong));
    auto CompressedData = std::vector<unsigned char>(CompressedSize);
    In.read((char*)CompressedData.data(), CompressedSize);
    mz_ulong Size_ = static_cast<mz_ulong>(Size);
    int Result = mz_uncompress(
        (unsigned char*)Data, &Size_,
        (unsigned char const*)CompressedData.data(),
        CompressedSize);
    assert(Result == MZ_OK);
}

struct serializer
{
    std::filesystem::path SceneFilePath;
    std::filesystem::path DirectoryPath;

    std::unordered_map<texture*, uint>      TextureIndexMap;
    std::unordered_map<mesh*, uint>         MeshIndexMap;
    std::unordered_map<material*, uint>     MaterialIndexMap;
    std::unordered_map<prefab*, uint>       PrefabIndexMap;

    scene* Scene;
    bool IsWriting;
};

std::string MakeFileName(std::string const& Name, char const* Extension)
{
    std::string Str = Name;
    for (char& Ch : Str)
        if (!std::isalnum(Ch))
            Ch = '_';

    Str.erase(Str.begin(), std::find_if(Str.begin(), Str.end(), [](unsigned char Ch) { return !std::isspace(Ch); }));

    Str += ".";
    Str += Extension;

    return Str;
}

template<typename type, int N>
void SerializeField(serializer& S, json& JSON, type (&Array)[N])
{
    if (S.IsWriting) {
        for (uint I = 0; I < N; I++) {
            json ValueJSON;
            Serialize(S, ValueJSON, Array[I]);
            JSON.push_back(ValueJSON);
        }
    }
    else {
        for (uint I = 0; I < N; I++)
            Serialize(S, JSON[I], Array[I]);
    }
}

template<typename type>
void SerializeField(serializer& S, json& JSON, std::vector<type>& Array)
{
    if (S.IsWriting) {
        for (uint I = 0; I < Array.size(); I++) {
            json ValueJSON;
            SerializeField(S, ValueJSON, Array[I]);
            JSON.push_back(ValueJSON);
        }
    }
    else {
        Array.resize(JSON.size());
        for (uint I = 0; I < Array.size(); I++)
            SerializeField(S, JSON[I], Array[I]);
    }
}

template<typename type>
void SerializeField(serializer& S, json& JSON, type& Value)
{
    if (S.IsWriting) JSON = Value; else Value = JSON;
}

void SerializeField(serializer& S, json& JSON, vec2& Value)
{
    if (S.IsWriting) {
        JSON = { Value.x, Value.y };
    }
    else {
        Value.x = JSON[0];
        Value.y = JSON[1];
    }
}

void SerializeField(serializer& S, json& JSON, vec3& Value)
{
    if (S.IsWriting) {
        JSON = { Value.x, Value.y, Value.z };
    }
    else {
        Value.x = JSON[0];
        Value.y = JSON[1];
        Value.z = JSON[2];
    }
}

void SerializeField(serializer& S, json& JSON, vec4& Value)
{
    if (S.IsWriting) {
        JSON = { Value.x, Value.y, Value.z, Value.w };
    }
    else {
        Value.x = JSON[0];
        Value.y = JSON[1];
        Value.z = JSON[2];
        Value.w = JSON[3];
    }
}

#define F(NAME) SerializeField(S, JSON[#NAME], Object.NAME);
#define F2(NAME1, NAME2) SerializeField(S, JSON[#NAME1][#NAME2], Object.NAME1.NAME2);

template<typename type>
void SerializeObject(serializer& S, json& JSON, std::vector<type>& Array)
{
    if (S.IsWriting) {
        for (uint I = 0; I < Array.size(); I++) {
            json ValueJSON;
            SerializeObject(S, ValueJSON, Array[I]);
            JSON.push_back(ValueJSON);
        }
    }
    else {
        Array.resize(JSON.size());
        for (uint I = 0; I < Array.size(); I++)
            SerializeObject(S, JSON[I], Array[I]);
    }
}

void SerializeObject(serializer& S, json& JSON, texture& Object)
{
    F(Name);
    F(Type);
    F(EnableNearestFiltering);

    struct texture_header
    {
        uint32_t Magic;
        uint32_t Version;
        uint32_t Width;
        uint32_t Height;
    };

    auto FilePath = S.DirectoryPath / MakeFileName(Object.Name, "texture");

    texture_header Header;

    if (S.IsWriting) {
        Header.Magic = 'TEX ';
        Header.Version = 0;
        Header.Width = Object.Width;
        Header.Height = Object.Height;

        auto File = std::ofstream(FilePath, std::ios::binary);
        File.write(
            reinterpret_cast<char*>(&Header),
            sizeof(texture_header));
        WriteCompressed(File,
            Object.Pixels,
            sizeof(vec4) * Object.Width * Object.Height);
    }
    else {
        auto File = std::ifstream(FilePath, std::ios::binary);

        File.read(
            reinterpret_cast<char*>(&Header),
            sizeof(texture_header));

        Object.Width = Header.Width;
        Object.Height = Header.Height;

        vec4* Pixels = new vec4[Object.Width * Object.Height];
        ReadCompressed(File, Pixels, sizeof(vec4) * Object.Width * Object.Height);

        Object.Pixels = Pixels;
    }
}

void SerializeField(serializer& S, json& JSON, texture*& Pointer)
{
    if (S.IsWriting) {
        if (S.TextureIndexMap.contains(Pointer))
            JSON = S.TextureIndexMap[Pointer];
        else
            JSON = -1;
    }
    else {
        int Index = JSON.get<int>();
        if (Index >= 0)
            Pointer = S.Scene->Textures[Index];
        else
            Pointer = nullptr;
    }
}

void SerializeObject(serializer& S, json& JSON, material*& Material)
{
    if (Material->Type == MATERIAL_TYPE_OPENPBR) {
        material_openpbr& Object = *static_cast<material_openpbr*>(Material);

        F(Name);
        F(Flags);
        F(Opacity);
        F(BaseWeight);
        F(BaseColor);
        F(BaseColorTexture);
        F(BaseMetalness);
        F(BaseDiffuseRoughness);
        F(SpecularWeight);
        F(SpecularColor);
        F(SpecularRoughness);
        F(SpecularRoughnessTexture);
        F(SpecularRoughnessAnisotropy);
        F(SpecularIOR);
        F(TransmissionWeight);
        F(TransmissionColor);
        F(TransmissionDepth);
        F(TransmissionScatter);
        F(TransmissionScatterAnisotropy);
        F(TransmissionDispersionScale);
        F(TransmissionDispersionAbbeNumber);
        F(CoatWeight);
        F(CoatColor);
        F(CoatRoughness);
        F(CoatRoughnessAnisotropy);
        F(CoatIOR);
        F(CoatDarkening);
        F(EmissionLuminance);
        F(EmissionColor);
        F(EmissionColorTexture);
        F(LayerBounceLimit);
    }
}

void SerializeField(serializer& S, json& JSON, material*& Pointer)
{
    if (S.IsWriting) {
        if (S.MaterialIndexMap.contains(Pointer))
            JSON = S.MaterialIndexMap[Pointer];
        else
            JSON = -1;
    }
    else {
        int Index = JSON.get<int>();
        if (Index >= 0)
            Pointer = S.Scene->Materials[Index];
        else
            Pointer = nullptr;
    }
}

void SerializeObject(serializer& S, json& JSON, mesh& Object)
{
    F(Name);

    struct mesh_header
    {
        uint32_t Magic;
        uint32_t Version;
        uint32_t FaceCount;
        uint32_t NodeCount;
    };

    auto FilePath = S.DirectoryPath / MakeFileName(Object.Name, "mesh");

    mesh_header Header;

    if (S.IsWriting) {
        Header.Magic = 'MESH';
        Header.Version = 0;
        Header.FaceCount = static_cast<uint>(Object.Faces.size());
        Header.NodeCount = static_cast<uint>(Object.Nodes.size());

        auto File = std::ofstream(FilePath, std::ios::binary);
        File.write(
            reinterpret_cast<char*>(&Header),
            sizeof(mesh_header));
        WriteCompressed(File,
            Object.Faces.data(),
            sizeof(mesh_face) * Object.Faces.size());
        WriteCompressed(File,
            Object.Nodes.data(),
            sizeof(mesh_node) * Object.Nodes.size());
    }
    else {
        auto File = std::ifstream(FilePath, std::ios::binary);

        File.read(
            reinterpret_cast<char*>(&Header),
            sizeof(mesh_header));

        Object.Faces.resize(Header.FaceCount);
        Object.Nodes.resize(Header.NodeCount);

        ReadCompressed(File,
            Object.Faces.data(),
            sizeof(mesh_face) * Object.Faces.size());
        ReadCompressed(File,
            Object.Nodes.data(),
            sizeof(mesh_node) * Object.Nodes.size());
    }
}

void SerializeField(serializer& S, json& JSON, mesh*& Pointer)
{
    if (S.IsWriting) {
        if (S.MeshIndexMap.contains(Pointer))
            JSON = S.MeshIndexMap[Pointer];
        else
            JSON = -1;
    }
    else {
        int Index = JSON.get<int>();
        if (Index >= 0)
            Pointer = S.Scene->Meshes[Index];
        else
            Pointer = nullptr;
    }
}

void SerializeObject(serializer& S, json& JSON, entity*& Entity)
{
    if (!S.IsWriting) {
        auto EntityType = static_cast<entity_type>(JSON["Type"].get<int>());
        Entity = CreateEntityRaw(EntityType);
    }
    else {
        JSON["Type"] = Entity->Type;
    }

    SerializeField(S, JSON["Position"], Entity->Transform.Position);
    SerializeField(S, JSON["Rotation"], Entity->Transform.Rotation);
    SerializeField(S, JSON["Scale"], Entity->Transform.Scale);
    SerializeField(S, JSON["Name"], Entity->Name);
    SerializeField(S, JSON["Active"], Entity->Active);
    SerializeObject(S, JSON["Children"], Entity->Children);

    if (!S.IsWriting) {
        for (entity* Child : Entity->Children)
            Child->Parent = Entity;
    }

    switch (Entity->Type) {
        case ENTITY_TYPE_CAMERA: {
            camera_entity& Object = *static_cast<camera_entity*>(Entity);
            F(RenderMode);
            F(RenderFlags);
            F(RenderBounceLimit);
            F(RenderSampleBlockSizeLog2);
            F(RenderTerminationProbability);
            F(Brightness);
            F(ToneMappingMode);
            F(ToneMappingWhiteLevel);
            F(CameraModel);
            F2(Pinhole, FieldOfViewInDegrees);
            F2(Pinhole, ApertureDiameterInMM);
            F2(ThinLens, SensorSizeInMM);
            F2(ThinLens, FocalLengthInMM);
            F2(ThinLens, ApertureDiameterInMM);
            F2(ThinLens, FocusDistance);
            break;
        }
        case ENTITY_TYPE_MESH_INSTANCE: {
            mesh_entity& Object = *static_cast<mesh_entity*>(Entity);
            F(Mesh);
            F(Material);
            break;
        }
        case ENTITY_TYPE_PLANE: {
            plane_entity& Object = *static_cast<plane_entity*>(Entity);
            F(Material);
            break;
        }
        case ENTITY_TYPE_SPHERE: {
            sphere_entity& Object = *static_cast<sphere_entity*>(Entity);
            F(Material);
            break;
        }
        case ENTITY_TYPE_CUBE: {
            cube_entity& Object = *static_cast<cube_entity*>(Entity);
            F(Material);
            break;
        }
    }
}

void SerializeObject(serializer& S, json& JSON, root_entity& Object)
{
    F(ScatterRate);
    F(SkyboxBrightness);
    F(SkyboxTexture);

    SerializeObject(S, JSON["Children"], Object.Children);

    if (!S.IsWriting) {
        for (entity* Child : Object.Children)
            Child->Parent = &Object;
    }
}

void SerializeObject(serializer& S, scene& Scene)
{
    // Serialize assets and entities.
    {
        json JSON;

        if (!S.IsWriting) {
            auto SceneFile = std::ifstream(S.SceneFilePath);
            JSON = json::parse(SceneFile);
        }

        if (!S.IsWriting) {
            for (uint I = 0; I < JSON["Textures"].size(); I++)
                Scene.Textures.push_back(new texture);
            for (uint I = 0; I < JSON["Materials"].size(); I++)
                Scene.Materials.push_back(new material);
            for (uint I = 0; I < JSON["Meshes"].size(); I++)
                Scene.Meshes.push_back(new mesh);
            for (uint I = 0; I < JSON["Prefabs"].size(); I++)
                Scene.Prefabs.push_back(new prefab);
        }

        for (uint Index = 0; Index < Scene.Textures.size(); Index++) {
            texture* Texture = Scene.Textures[Index];
            S.TextureIndexMap[Texture] = Index;
            SerializeObject(S, JSON["Textures"][Index], *Texture);
        }

        for (uint Index = 0; Index < Scene.Materials.size(); Index++) {
            material* Material = Scene.Materials[Index];
            S.MaterialIndexMap[Material] = Index;
            SerializeObject(S, JSON["Materials"][Index], Material);
        }

        for (uint Index = 0; Index < Scene.Meshes.size(); Index++) {
            mesh* Mesh = Scene.Meshes[Index];
            S.MeshIndexMap[Mesh] = Index;
            SerializeObject(S, JSON["Meshes"][Index], *Mesh);
        }

        for (uint Index = 0; Index < Scene.Prefabs.size(); Index++) {
            prefab* Prefab = Scene.Prefabs[Index];
            S.PrefabIndexMap[Prefab] = Index;
            SerializeObject(S, JSON["Prefabs"][Index], Prefab->Entity);
        }

        SerializeObject(S, JSON["Root"], Scene.Root);

        if (S.IsWriting) {
            auto SceneFile = std::ofstream(S.SceneFilePath);
            SceneFile << JSON.dump(4);
        }
    }

    // Serialize the RGB spectrum coefficient table.
    {
        struct spectrum_table_header
        {
            uint32_t Magic;
            uint32_t Version;
        };

        auto FilePath = S.DirectoryPath / "spectrum.dat";

        spectrum_table_header Header;

        if (S.IsWriting) {
            Header.Magic = 'SPEC';
            Header.Version = 0;

            auto File = std::ofstream(FilePath, std::ios::binary);
            File.write(
                reinterpret_cast<char*>(&Header),
                sizeof(spectrum_table_header));

            WriteCompressed(File,
                &Scene.RGBSpectrumTable->Coefficients,
                sizeof(parametric_spectrum_table::Coefficients));
        }
        else {
            auto File = std::ifstream(FilePath, std::ios::binary);

            File.read(
                reinterpret_cast<char*>(&Header),
                sizeof(spectrum_table_header));

            Scene.RGBSpectrumTable = new parametric_spectrum_table;

            ReadCompressed(File,
                &Scene.RGBSpectrumTable->Coefficients,
                sizeof(parametric_spectrum_table::Coefficients));
        }
    }
}

scene* LoadScene(char const* Path)
{
    std::filesystem::path SceneFilePath = Path;

    serializer S;
    S.SceneFilePath = Path;
    S.DirectoryPath = S.SceneFilePath.parent_path();
    S.IsWriting = false;
    S.Scene = new scene;

    SerializeObject(S, *S.Scene);

    S.Scene->DirtyFlags = SCENE_DIRTY_ALL;

    return S.Scene;
}

void SaveScene(char const* Path, scene* Scene)
{
    serializer S;
    S.SceneFilePath = Path;
    S.DirectoryPath = S.SceneFilePath.parent_path();
    S.IsWriting = true;
    S.Scene = Scene;

    std::filesystem::create_directory(S.DirectoryPath);

    SerializeObject(S, *Scene);
}
