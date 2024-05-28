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

constexpr float INF = std::numeric_limits<float>::infinity();

struct Bounds
{
    glm::vec3   minimum = { +INF, +INF, +INF };
    glm::vec3   maximum = { -INF, -INF, -INF };
};

static void Grow(glm::vec3& minimum, glm::vec3& maximum, glm::vec3 point)
{
    minimum = glm::min(minimum, point);
    maximum = glm::max(maximum, point);
}

static void Grow(Bounds& bounds, glm::vec3 point)
{
    bounds.minimum = glm::min(bounds.minimum, point);
    bounds.maximum = glm::max(bounds.maximum, point);
}

static void Grow(Bounds& bounds, Bounds const& other)
{
    bounds.minimum = glm::min(bounds.minimum, other.minimum);
    bounds.maximum = glm::max(bounds.maximum, other.maximum);
}

static float HalfArea(glm::vec3 minimum, glm::vec3 maximum)
{
    glm::vec3 e = maximum - minimum;
    return e.x * e.y + e.y * e.z + e.z * e.x;
}

static float HalfArea(Bounds const& box)
{
    glm::vec3 e = box.maximum - box.minimum;
    return e.x * e.y + e.y * e.z + e.z * e.x;
}

static glm::vec3 OrthogonalVector(glm::vec3 const& v)
{
    int axis = 0;
    if (glm::abs(v.y) > glm::abs(v.x)) axis = 1;
    if (glm::abs(v.z) > glm::abs(v[axis])) axis = 2;
    glm::vec3 w = {};
    w[(axis + 1) % 3] = 1.0f;
    return glm::normalize(glm::cross(v, w));
}

static uint8_t ToSRGB(float value)
{
    value = glm::clamp(value, 0.0f, 1.0f);
    if (value <= 0.0031308f)
        value *= 12.92f;
    else
        value = 1.055f * glm::pow(value, 1.0f / 2.4f) - 0.055f;
    return static_cast<uint8_t>(value * 255);
}

static uint32_t ToSRGB(glm::vec4 const& color)
{
    return ToSRGB(color.r)
         | ToSRGB(color.g) << 8
         | ToSRGB(color.b) << 16
         | static_cast<uint8_t>(255 * color.a) << 24;
}

char const* EntityTypeName(EntityType type)
{
    switch (type) {
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

Entity* CreateEntity(Scene* scene, EntityType type, Entity* parent)
{
    Entity* entity = nullptr;

    switch (type) {
        case ENTITY_TYPE_ROOT:
            entity = new Root;
            break;
        case ENTITY_TYPE_CONTAINER:
            entity = new Container;
            break;
        case ENTITY_TYPE_CAMERA:
            entity = new Camera;
            break;
        case ENTITY_TYPE_MESH_INSTANCE:
            entity = new MeshInstance;
            break;
        case ENTITY_TYPE_PLANE:
            entity = new Plane;
            break;
        case ENTITY_TYPE_SPHERE:
            entity = new Sphere;
            break;
        case ENTITY_TYPE_CUBE:
            entity = new Cube;
            break;
        default:
            assert(false);
            break;
    }

    if (!parent) parent = &scene->root;

    parent->children.push_back(entity);

    return entity;
}

Entity* CreateEntity(Scene* scene, Entity* source, Entity* parent)
{
    Entity* entity = nullptr;

    switch (source->type) {
        case ENTITY_TYPE_ROOT:
            entity = new Root(*static_cast<Root*>(source));
            break;
        case ENTITY_TYPE_CONTAINER:
            entity = new Container(*static_cast<Container*>(source));
            break;
        case ENTITY_TYPE_CAMERA:
            entity = new Camera(*static_cast<Camera*>(source));
            break;
        case ENTITY_TYPE_MESH_INSTANCE:
            entity = new MeshInstance(*static_cast<MeshInstance*>(source));
            break;
        case ENTITY_TYPE_PLANE:
            entity = new Plane(*static_cast<Plane*>(source));
            break;
        case ENTITY_TYPE_SPHERE:
            entity = new Sphere(*static_cast<Sphere*>(source));
            break;
        case ENTITY_TYPE_CUBE:
            entity = new Cube(*static_cast<Cube*>(source));
            break;
        default:
            assert(false);
            break;
    }

    if (!parent) parent = &scene->root;

    parent->children.push_back(entity);

    std::vector<Entity*> children = std::move(entity->children);
    entity->children.clear();
    for (Entity* child : children)
        CreateEntity(scene, child, entity);

    return entity;
}

Entity* CreateEntity(Scene* scene, Prefab* prefab, Entity* parent)
{
    return CreateEntity(scene, prefab->entity, parent);
}

Texture* CreateCheckerTexture(Scene* scene, char const* name, glm::vec4 const& colorA, glm::vec4 const& colorB)
{
    auto pixels = new uint32_t[4];

    pixels[0] = ToSRGB(colorA);
    pixels[1] = ToSRGB(colorB);
    pixels[2] = ToSRGB(colorB);
    pixels[3] = ToSRGB(colorA);

    auto texture = new Texture{
        .name = name,
        .width = 2,
        .height = 2,
        .pixels = pixels,
    };
    scene->textures.push_back(texture);

    scene->dirtyFlags |= SCENE_DIRTY_TEXTURES;

    return texture;
}

Texture* LoadTexture(Scene* scene, char const* path, char const* name)
{
    int width, height, channelsInFile;
    stbi_uc* pixels = stbi_load(path, &width, &height, &channelsInFile, 4);
    if (!pixels) return nullptr;

    auto texture = new Texture {
        .name = name ? name : path,
        .width = static_cast<uint32_t>(width),
        .height = static_cast<uint32_t>(height),
        .pixels = reinterpret_cast<uint32_t*>(pixels),
    };
    scene->textures.push_back(texture);

    scene->dirtyFlags |= SCENE_DIRTY_TEXTURES;

    return texture;
}

Material* CreateMaterial(Scene* scene, char const* name)
{
    auto material = new Material;
    material->name = name;
    scene->materials.push_back(material);

    scene->dirtyFlags |= SCENE_DIRTY_MATERIALS;

    return material;
}

static void BuildMeshNode(Mesh* mesh, uint32_t nodeIndex, uint32_t depth)
{
    MeshNode& node = mesh->nodes[nodeIndex];

    uint32_t faceCount = node.faceEndIndex - node.faceBeginIndex;

    // Compute node bounds.
    node.minimum = { +INF, +INF, +INF };
    node.maximum = { -INF, -INF, -INF };
    for (uint32_t index = node.faceBeginIndex; index < node.faceEndIndex; index++) {
        for (int j = 0; j < 3; j++) {
            glm::vec3 const& position = mesh->faces[index].vertices[j];
            node.minimum = glm::min(node.minimum, position);
            node.maximum = glm::max(node.maximum, position);
        }
    }

    int splitAxis = 0;
    float splitPosition = 0;
    float splitCost = +INF;

    for (int axis = 0; axis < 3; axis++) {
        // Compute centroid-based bounds for the current node.
        float minimum = +INF, maximum = -INF;
        for (uint32_t i = node.faceBeginIndex; i < node.faceEndIndex; i++) {
            float centroid = mesh->faces[i].centroid[axis];
            minimum = std::min(minimum, centroid);
            maximum = std::max(maximum, centroid);
        }

        if (minimum == maximum) continue;

        // Bin the faces by their centroid points.
        constexpr uint32_t BINS = 32;

        struct Bin {
            Bounds bounds;
            uint32_t faceCount = 0;
        };
        Bin bins[BINS];

        float binIndexPerUnit = float(BINS) / (maximum - minimum);

        for (uint32_t i = node.faceBeginIndex; i < node.faceEndIndex; i++) {
            // Compute bin index of the face centroid.
            float centroid = mesh->faces[i].centroid[axis];
            uint32_t binIndexUnclamped = static_cast<uint32_t>(binIndexPerUnit * (centroid - minimum));
            uint32_t binIndex = std::min(binIndexUnclamped, BINS - 1);

            // Grow the bin to accommodate the new face.
            Bin& bin = bins[binIndex];
            Grow(bin.bounds, mesh->faces[i].vertices[0]);
            Grow(bin.bounds, mesh->faces[i].vertices[1]);
            Grow(bin.bounds, mesh->faces[i].vertices[2]);
            bin.faceCount++;
        }

        // Calculate details of each possible split.
        struct Split {
            float leftArea = 0.0f;
            uint32_t leftCount = 0;
            float rightArea = 0.0f;
            uint32_t rightCount = 0;
        };
        Split splits[BINS-1];

        Bounds leftBounds;
        Bounds rightBounds;
        uint32_t leftCountSum = 0;
        uint32_t rightCountSum = 0;

        for (uint32_t i = 0; i < BINS - 1; i++) {
            uint32_t j = BINS - 2 - i;

            Bin const& leftBin = bins[i];
            if (leftBin.faceCount > 0) {
                leftCountSum += leftBin.faceCount;
                Grow(leftBounds, leftBin.bounds);
            }
            splits[i].leftCount = leftCountSum;
            splits[i].leftArea = HalfArea(leftBounds);

            Bin const& rightBin = bins[j+1];
            if (rightBin.faceCount > 0) {
                rightCountSum += rightBin.faceCount;
                Grow(rightBounds, rightBin.bounds);
            }
            splits[j].rightCount = rightCountSum;
            splits[j].rightArea = HalfArea(rightBounds);
        }

        // Find the best split.
        float interval = (maximum - minimum) / float(BINS);
        float position = minimum + interval;

        for (uint32_t i = 0; i < BINS - 1; i++) {
            Split const& split = splits[i];
            float cost = split.leftCount * split.leftArea + split.rightCount * split.rightArea;
            if (cost < splitCost) {
                splitCost = cost;
                splitAxis = axis;
                splitPosition = position;
            }
            position += interval;
        }
    }

    // If splitting is more costly than not splitting, then leave this node as a leaf.
    float unsplitCost = faceCount * HalfArea(node.minimum, node.maximum);
    if (splitCost >= unsplitCost) return;

    // Partition the faces within the node by the chosen split plane.
    uint32_t beginIndex = node.faceBeginIndex;
    uint32_t endIndex = node.faceEndIndex;
    uint32_t splitIndex = beginIndex;
    uint32_t swapIndex = endIndex - 1;
    while (splitIndex < swapIndex) {
        if (mesh->faces[splitIndex].centroid[splitAxis] < splitPosition) {
            splitIndex++;
        }
        else {
            std::swap(mesh->faces[splitIndex], mesh->faces[swapIndex]);
            swapIndex--;
        }
    }

    if (splitIndex == beginIndex || splitIndex == endIndex)
        return;

    uint32_t leftNodeIndex = static_cast<uint32_t>(mesh->nodes.size());
    uint32_t rightNodeIndex = leftNodeIndex + 1;

    node.childNodeIndex = leftNodeIndex;

    mesh->nodes.push_back({
        .faceBeginIndex = beginIndex,
        .faceEndIndex = splitIndex,
    });

    mesh->nodes.push_back({
        .faceBeginIndex = splitIndex,
        .faceEndIndex = endIndex,
    });

    mesh->depth = std::max(mesh->depth, depth+1);

    BuildMeshNode(mesh, leftNodeIndex, depth+1);
    BuildMeshNode(mesh, rightNodeIndex, depth+1);
}

Prefab* LoadModelAsPrefab(Scene* scene, char const* path, LoadModelOptions* options)
{
    LoadModelOptions defaultOptions {};
    if (!options) options = &defaultOptions;

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> fileMaterials;
    std::string warn;
    std::string err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &fileMaterials, &warn, &err, path, options->directoryPath.c_str()))
        return nullptr;

    // Map from in-file texture name to scene texture.
    std::unordered_map<std::string, Texture*> textureMap;
    std::vector<Material*> materials;

    // Scan the material definitions and build scene materials.
    for (int materialId = 0; materialId < fileMaterials.size(); materialId++) {
        tinyobj::material_t const& fileMaterial = fileMaterials[materialId];

        auto material = CreateMaterial(scene, fileMaterial.name.c_str());

        material->baseColor = glm::vec4(
            fileMaterial.diffuse[0],
            fileMaterial.diffuse[1],
            fileMaterial.diffuse[2],
            1.0);

        material->emissionColor = glm::vec4(
            fileMaterial.emission[0],
            fileMaterial.emission[1],
            fileMaterial.emission[2],
            1.0);

        material->specularRoughness = 1.0f;
        material->specularIOR = 0.0f;
        material->transmissionWeight = 0.0f;

        std::pair<std::string, Texture**> textures[] = {
            { fileMaterial.diffuse_texname, &material->baseColorTexture },
            { fileMaterial.emissive_texname, &material->emissionColorTexture },
        };

        for (auto const& [name, ptexture] : textures) {
            if (!name.empty()) {
                if (!textureMap.contains(name)) {
                    std::string path = std::format("{}/{}", options->directoryPath, name);
                    textureMap[name] = LoadTexture(scene, path.c_str(), name.c_str());
                }
                *ptexture = textureMap[name];
            }
            else {
                *ptexture = nullptr;
            }
        }

        materials.push_back(material);
    }

    // Import meshes.
    std::vector<Mesh*> meshes;
    std::vector<glm::vec3> origins;
    {
        Mesh* mesh = nullptr;

        std::unordered_map<int, uint32_t> meshMaterialIndices;
        std::vector<Material*> meshMaterials;

        if (options->mergeIntoSingleMesh) {
            mesh = new Mesh;
            mesh->name = options->name ? options->name : path;
            mesh->materials = materials;
            meshes.push_back(mesh);
            origins.push_back(glm::vec3());

            size_t faceCount = 0;
            for (auto const& shape : shapes)
                faceCount += shape.mesh.indices.size() / 3;
            mesh->faces.reserve(faceCount);
        }

        glm::vec3 origin {};

        for (tinyobj::shape_t const& shape : shapes) {
            size_t shapeIndexCount = shape.mesh.indices.size();

            if (!options->mergeIntoSingleMesh) {
                mesh = new Mesh;
                mesh->name = !shape.name.empty() ? shape.name : "Shape";
                mesh->faces.reserve(shapeIndexCount / 3);
                meshes.push_back(mesh);

                meshMaterialIndices.clear();
                meshMaterials.clear();

                glm::vec3 minimum = glm::vec3(+INFINITY, +INFINITY, +INFINITY);
                glm::vec3 maximum = glm::vec3(-INFINITY, -INFINITY, -INFINITY);
                for (size_t i = 0; i < shapeIndexCount; i++) {
                    tinyobj::index_t const& index = shape.mesh.indices[i];
                    glm::vec3 position = glm::vec3(
                        attrib.vertices[3*index.vertex_index+0],
                        attrib.vertices[3*index.vertex_index+1],
                        attrib.vertices[3*index.vertex_index+2]);
                    minimum = glm::min(minimum, position);
                    maximum = glm::max(maximum, position);
                }
                origin = (minimum + maximum) / 2.0f;
                origins.push_back(origin);
            }

            for (size_t i = 0; i < shapeIndexCount; i += 3) {
                auto face = MeshFace {};

                for (int j = 0; j < 3; j++) {
                    tinyobj::index_t const& index = shape.mesh.indices[i+j];

                    face.vertices[j] = options->vertexTransform * glm::vec4(
                        attrib.vertices[3*index.vertex_index+0] - origin.x,
                        attrib.vertices[3*index.vertex_index+1] - origin.y,
                        attrib.vertices[3*index.vertex_index+2] - origin.z,
                        1.0f);

                    if (index.normal_index >= 0) {
                        face.normals[j] = options->normalTransform * glm::vec4(
                            attrib.normals[3*index.normal_index+0],
                            attrib.normals[3*index.normal_index+1],
                            attrib.normals[3*index.normal_index+2],
                            1.0f);
                    }

                    if (index.texcoord_index >= 0) {
                        face.uvs[j] = options->textureCoordinateTransform * glm::vec3(
                            attrib.texcoords[2*index.texcoord_index+0],
                            attrib.texcoords[2*index.texcoord_index+1],
                            1.0f);
                    }
                }

                int materialId = shape.mesh.material_ids[i/3];

                if (!meshMaterialIndices.contains(materialId)) {
                    uint32_t materialIndex = static_cast<uint32_t>(meshMaterials.size());
                    meshMaterialIndices[materialId] = materialIndex;

                    if (materialId >= 0)
                        meshMaterials.push_back(materials[materialId]);
                    else
                        meshMaterials.push_back(options->defaultMaterial);
                }

                face.materialIndex = meshMaterialIndices[materialId];

                face.centroid = (face.vertices[0] + face.vertices[1] + face.vertices[2]) / 3.0f;

                mesh->faces.push_back(face);
            }

            if (!options->mergeIntoSingleMesh) {
                mesh->materials = std::move(meshMaterials);
            }
        }

        if (options->mergeIntoSingleMesh) {
            mesh->materials = std::move(meshMaterials);
        }
    }

    for (Mesh* mesh : meshes) {
        mesh->nodes.reserve(2 * mesh->faces.size());

        auto root = MeshNode {
            .faceBeginIndex = 0,
            .faceEndIndex = static_cast<uint32_t>(mesh->faces.size()),
            .childNodeIndex = 0,
        };

        mesh->nodes.push_back(root);
        BuildMeshNode(mesh, 0, 0);

        scene->meshes.push_back(mesh);
    }

    scene->dirtyFlags |= SCENE_DIRTY_MATERIALS;
    scene->dirtyFlags |= SCENE_DIRTY_MESHES;

    auto prefab = new Prefab;

    if (options->mergeIntoSingleMesh) {
        auto instance = new MeshInstance;
        instance->name = meshes[0]->name;
        instance->mesh = meshes[0];
        prefab->entity = instance;
    }
    else {
        auto container = new Container;
        container->name = options->name ? options->name : path;
        for (size_t k = 0; k < meshes.size(); k++) {
            Mesh* mesh = meshes[k];
            glm::vec3 const& origin = origins[k];

            auto instance = new MeshInstance;
            instance->name = mesh->name;
            instance->mesh = mesh;
            instance->transform.position = options->vertexTransform * glm::vec4(origin, 1);
            container->children.push_back(instance);
        }
        prefab->entity = container;
    }

    return prefab;
}

bool LoadSkybox(Scene* scene, char const* path)
{
    int width, height, components;

    scene->skyboxPixels = stbi_loadf(path, &width, &height, &components, STBI_rgb_alpha);
    scene->skyboxWidth = static_cast<uint32_t>(width);
    scene->skyboxHeight = static_cast<uint32_t>(height);

    scene->dirtyFlags |= SCENE_DIRTY_SKYBOX;


    float* pixels = scene->skyboxPixels;

    glm::vec3 mean = {};
    float weightSum = 0.0f;
    for (int y = 0; y < height; y++) {
        float theta = (0.5f - (y + 0.5f) / height) * PI;
        for (int x = 0; x < width; x++) {
            float phi = ((x + 0.5f) / width - 0.5f) * TAU;

            int index = (y * width + x) * 4;
            float r = pixels[index+0];
            float g = pixels[index+1];
            float b = pixels[index+2];
            float luminance = 0.2126f * r + 0.7152f * g + 0.0722f * b;
            float area = glm::cos(theta);

            float weight = area * luminance * luminance;

            glm::vec3 direction = {
                glm::cos(theta) * glm::cos(phi),
                glm::cos(theta) * glm::sin(phi),
                glm::sin(theta),
            };

            mean += weight * direction;
            weightSum += weight;
        }
    }
    mean /= weightSum;

    float meanLength = glm::length(mean);

    glm::vec3 mu = mean / meanLength;
    float kappa = meanLength * (3.0f - meanLength * meanLength) / (1 - meanLength * meanLength);

    glm::vec3 e1 = OrthogonalVector(mu);
    glm::vec3 e2 = glm::cross(e1, mu);

    scene->skyboxDistributionFrame = glm::mat3(e1, e2, mu);
    scene->skyboxDistributionConcentration = kappa;

    return true;
}

static void PackSceneObject(Scene* scene, glm::mat4 const& outerTransform, Entity* entity, uint32_t& priority)
{
    if (!entity->active)
        return;

    glm::mat4 innerTransform
        = outerTransform
        * glm::translate(glm::mat4(1), entity->transform.position)
        * glm::orientate4(entity->transform.rotation)
        * glm::scale(glm::mat4(1), entity->transform.scale);

    for (Entity* child : entity->children)
        PackSceneObject(scene, innerTransform, child, priority);

    PackedSceneObject packed;

    packed.materialIndex = 0;
    packed.priority = priority++;

    switch (entity->type) {
        case ENTITY_TYPE_MESH_INSTANCE: {
            auto instance = static_cast<MeshInstance*>(entity);
            if (!instance->mesh) return;
            packed.meshRootNodeIndex = instance->mesh->packedRootNodeIndex;
            packed.type = OBJECT_TYPE_MESH_INSTANCE;
            break;
        }
        case ENTITY_TYPE_PLANE: {
            auto plane = static_cast<Plane*>(entity);
            if (plane->material)
                packed.materialIndex = plane->material->packedMaterialIndex;
            packed.type = OBJECT_TYPE_PLANE;
            break;
        }
        case ENTITY_TYPE_SPHERE: {
            auto sphere = static_cast<Sphere*>(entity);
            if (sphere->material)
                packed.materialIndex = sphere->material->packedMaterialIndex;
            packed.type = OBJECT_TYPE_SPHERE;
            break;
        }
        case ENTITY_TYPE_CUBE: {
            auto cube = static_cast<Cube*>(entity);
            if (cube->material)
                packed.materialIndex = cube->material->packedMaterialIndex;
            packed.type = OBJECT_TYPE_CUBE;
            break;
        }
        default: {
            return;
        }
    }

    packed.transform.to = innerTransform;
    packed.transform.from = glm::inverse(innerTransform);

    entity->packedObjectIndex = static_cast<uint32_t>(scene->sceneObjectPack.size());

    scene->sceneObjectPack.push_back(packed);
}

static Bounds SceneObjectBounds(Scene const* scene, PackedSceneObject const& object)
{
    glm::vec4 objectCorners[8] = {};

    switch (object.type) {
        case OBJECT_TYPE_MESH_INSTANCE: {
            glm::vec3 meshMin = scene->meshNodePack[object.meshRootNodeIndex].minimum;
            glm::vec3 meshMax = scene->meshNodePack[object.meshRootNodeIndex].maximum;

            objectCorners[0] = { meshMin.x, meshMin.y, meshMin.z, 1 };
            objectCorners[1] = { meshMin.x, meshMin.y, meshMax.z, 1 };
            objectCorners[2] = { meshMin.x, meshMax.y, meshMin.z, 1 };
            objectCorners[3] = { meshMin.x, meshMax.y, meshMax.z, 1 };
            objectCorners[4] = { meshMax.x, meshMin.y, meshMin.z, 1 };
            objectCorners[5] = { meshMax.x, meshMin.y, meshMax.z, 1 };
            objectCorners[6] = { meshMax.x, meshMax.y, meshMin.z, 1 };
            objectCorners[7] = { meshMax.x, meshMax.y, meshMax.z, 1 };

            break;
        }
        case OBJECT_TYPE_PLANE: {
            objectCorners[0] = { -1e9f, -1e9f, -EPSILON, 1 };
            objectCorners[1] = { +1e9f, -1e9f, -EPSILON, 1 };
            objectCorners[2] = { -1e9f, +1e9f, -EPSILON, 1 };
            objectCorners[3] = { +1e9f, +1e9f, -EPSILON, 1 };
            objectCorners[4] = { -1e9f, -1e9f, +EPSILON, 1 };
            objectCorners[5] = { +1e9f, -1e9f, +EPSILON, 1 };
            objectCorners[6] = { -1e9f, +1e9f, +EPSILON, 1 };
            objectCorners[7] = { +1e9f, +1e9f, +EPSILON, 1 };

            break;
        }
        case OBJECT_TYPE_SPHERE:
        case OBJECT_TYPE_CUBE: {
            objectCorners[0] = { -1, -1, -1, 1 };
            objectCorners[1] = { +1, -1, -1, 1 };
            objectCorners[2] = { -1, +1, -1, 1 };
            objectCorners[3] = { +1, +1, -1, 1 };
            objectCorners[4] = { -1, -1, +1, 1 };
            objectCorners[5] = { +1, -1, +1, 1 };
            objectCorners[6] = { -1, +1, +1, 1 };
            objectCorners[7] = { +1, +1, +1, 1 };

            break;
        }
    }

    glm::vec3 worldMin = glm::vec3(+INFINITY);
    glm::vec3 worldMax = glm::vec3(-INFINITY);

    for (glm::vec4 const& objectCorner : objectCorners) {
        glm::vec3 worldCorner = (object.transform.to * objectCorner).xyz();
        worldMin = glm::min(worldMin, worldCorner.xyz());
        worldMax = glm::max(worldMax, worldCorner.xyz());
    }

    return { worldMin, worldMax };
}

void PrintSceneNode(Scene* scene, uint16_t index, int depth)
{
    PackedSceneNode const& node = scene->sceneNodePack[index];

    for (int k = 0; k < depth; k++) printf("  ");

    if (node.childNodeIndices > 0) {
        uint16_t indexA = node.childNodeIndices & 0xFFFF;
        uint16_t indexB = node.childNodeIndices >> 16;
        printf("Node %u\n", index);
        PrintSceneNode(scene, indexA, depth+1);
        PrintSceneNode(scene, indexB, depth+1);
    }
    else {
        printf("Leaf %u (object %lu)\n", index, node.objectIndex);
    }
}

uint32_t PackSceneData(Scene* scene)
{
    uint32_t dirtyFlags = scene->dirtyFlags;

    // Pack textures.
    if (dirtyFlags & SCENE_DIRTY_TEXTURES) {
        constexpr int ATLAS_WIDTH = 4096;
        constexpr int ATLAS_HEIGHT = 4096;

        std::vector<stbrp_node> nodes(ATLAS_WIDTH);
        std::vector<stbrp_rect> rects(scene->textures.size());
        
        for (int k = 0; k < rects.size(); k++) {
            Texture* texture = scene->textures[k];
            rects[k] = {
                .id = k,
                .w = static_cast<int>(texture->width),
                .h = static_cast<int>(texture->height),
                .was_packed = 0,
            };
        }

        scene->images.clear();

        while (!rects.empty()) {
            stbrp_context context;
            stbrp_init_target(&context, 4096, 4096, nodes.data(), static_cast<int>(nodes.size()));
            stbrp_pack_rects(&context, rects.data(), static_cast<int>(rects.size()));

            uint32_t* pixels = new uint32_t[4096 * 4096];

            uint32_t imageIndex = static_cast<uint32_t>(scene->images.size());

            for (stbrp_rect& rect : rects) {
                if (!rect.was_packed)
                    continue;

                Texture* texture = scene->textures[rect.id];
                assert(texture->width == rect.w);
                assert(texture->height == rect.h);

                texture->atlasImageIndex = imageIndex;
                texture->atlasPlacementMinimum = {
                    (rect.x + 0.5f) / float(ATLAS_WIDTH),
                    (rect.y + rect.h - 0.5f) / float(ATLAS_HEIGHT),
                };
                texture->atlasPlacementMaximum = {
                    (rect.x + rect.w - 0.5f) / float(ATLAS_WIDTH),
                    (rect.y + 0.5f) / float(ATLAS_HEIGHT),
                };

                for (uint32_t y = 0; y < texture->height; y++) {
                    uint32_t const* src = texture->pixels + y * texture->width;
                    uint32_t* dst = pixels + (rect.y + y) * ATLAS_WIDTH + rect.x;
                    memcpy(dst, src, texture->width * sizeof(uint32_t));
                }
            }

            auto atlas = Image {
                .width = ATLAS_WIDTH,
                .height = ATLAS_HEIGHT,
                .pixels = pixels,
            };
            scene->images.push_back(atlas);

            std::erase_if(rects, [](stbrp_rect& r) { return r.was_packed; });
        }

        dirtyFlags |= SCENE_DIRTY_MATERIALS;
    }

    // Pack materials.
    if (dirtyFlags & SCENE_DIRTY_MATERIALS) {
        scene->materialPack.clear();

        // Fallback material.
        {
            PackedMaterial packed = {
                .baseColor = glm::vec3(1, 1, 1),
                .baseWeight = 1.0f,
                .specularWeight = 0.0f,
                .transmissionWeight = 0.0f,
                .emissionColor = glm::vec3(0, 0, 0),
                .emissionLuminance = 0.0f,
                .baseMetalness = 0.0f,
                .baseDiffuseRoughness = 1.0f,
                .specularRoughness = 1.0f,
                .specularRoughnessAnisotropy = 0.0f,
                .specularIOR = 1.5f,
                .transmissionDepth = 0.0f,
                .flags = 0
            };
            scene->materialPack.push_back(packed);
        }

        for (Material* material : scene->materials) {
            PackedMaterial packed;

            packed.flags = 0;

            material->packedMaterialIndex = static_cast<uint32_t>(scene->materialPack.size());

            packed.baseColor = material->baseColor;
            packed.baseWeight = material->baseWeight;
            packed.baseMetalness = material->baseMetalness;
            packed.baseDiffuseRoughness = material->baseDiffuseRoughness;

            packed.specularColor = material->specularColor;
            packed.specularWeight = material->specularWeight;
            packed.specularRoughness = material->specularRoughness;
            packed.specularRoughnessAnisotropy = material->specularRoughnessAnisotropy;
            packed.specularIOR = material->specularIOR;

            packed.transmissionColor = material->transmissionColor;
            packed.transmissionWeight = material->transmissionWeight;
            packed.transmissionScatter = material->transmissionScatter;
            packed.transmissionScatterAnisotropy = material->transmissionScatterAnisotropy;

            packed.emissionColor = material->emissionColor;
            packed.emissionLuminance = material->emissionLuminance;

            if (Texture* texture = material->baseColorTexture; texture) {
                packed.baseColorTextureIndex = texture->atlasImageIndex;
                packed.baseColorTextureMinimum = texture->atlasPlacementMinimum;
                packed.baseColorTextureMaximum = texture->atlasPlacementMaximum;
                packed.flags |= MATERIAL_FLAG_BASE_COLOR_TEXTURE;
                if (material->baseColorTextureFilterNearest)
                    packed.flags |= MATERIAL_FLAG_BASE_COLOR_TEXTURE_FILTER_NEAREST;
            }

            scene->materialPack.push_back(packed);
        }

        dirtyFlags |= SCENE_DIRTY_MESHES;
        dirtyFlags |= SCENE_DIRTY_OBJECTS;
    }

    // Pack mesh face and node data.
    if (dirtyFlags & SCENE_DIRTY_MESHES) {
        uint32_t faceCount = 0;
        uint32_t nodeCount = 0;
        for (Mesh* mesh : scene->meshes) {
            faceCount += static_cast<uint32_t>(mesh->faces.size());
            nodeCount += static_cast<uint32_t>(mesh->nodes.size());
        }

        scene->meshFacePack.clear();
        scene->meshFacePack.reserve(faceCount);
        scene->meshNodePack.clear();
        scene->meshNodePack.reserve(nodeCount);

        for (Mesh* mesh : scene->meshes) {
            uint32_t faceIndexBase = static_cast<uint32_t>(scene->meshFacePack.size());
            uint32_t nodeIndexBase = static_cast<uint32_t>(scene->meshNodePack.size());

            // Build the packed mesh faces.
            for (MeshFace const& face : mesh->faces) {
                PackedMeshFace packed;
                PackedMeshFaceExtra packedx;

                packed.position = face.vertices[0];

                Material* material = mesh->materials[face.materialIndex];
                packedx.materialIndex = material ? material->packedMaterialIndex : 0;

                glm::vec3 ab = face.vertices[1] - face.vertices[0];
                glm::vec3 ac = face.vertices[2] - face.vertices[0];
                glm::vec3 normal = glm::normalize(glm::cross(ab, ac));
                float d = -glm::dot(normal, glm::vec3(packed.position));
                packed.plane = glm::vec4(normal, d);

                float bb = glm::dot(ab, ab);
                float bc = glm::dot(ab, ac);
                float cc = glm::dot(ac, ac);
                float idet = 1.0f / (bb * cc - bc * bc);
                packed.base1 = (ab * cc - ac * bc) * idet;
                packed.base2 = (ac * bb - ab * bc) * idet;

                packedx.normals[0] = face.normals[0];
                packedx.normals[1] = face.normals[1];
                packedx.normals[2] = face.normals[2];

                packedx.uvs[0] = face.uvs[0];
                packedx.uvs[1] = face.uvs[1];
                packedx.uvs[2] = face.uvs[2];

                scene->meshFacePack.push_back(packed);
                scene->meshFaceExtraPack.push_back(packedx);
            }

            // Build the packed mesh nodes.
            for (MeshNode const& node : mesh->nodes) {
                PackedMeshNode packed;

                packed.minimum = node.minimum;
                packed.maximum = node.maximum;

                if (node.childNodeIndex > 0) {
                    packed.faceBeginOrNodeIndex = nodeIndexBase + node.childNodeIndex;
                    packed.faceEndIndex = 0;
                }
                else {
                    packed.faceBeginOrNodeIndex = faceIndexBase + node.faceBeginIndex;
                    packed.faceEndIndex = faceIndexBase + node.faceEndIndex;
                }

                scene->meshNodePack.push_back(packed);
            }

            mesh->packedRootNodeIndex = nodeIndexBase;
        }

        dirtyFlags |= SCENE_DIRTY_OBJECTS;
    }

    // Pack object data.
    if (dirtyFlags & SCENE_DIRTY_OBJECTS) {
        scene->sceneObjectPack.clear();
        scene->sceneNodePack.resize(1);

        glm::mat4 const& outerTransform = glm::mat4(1);

        uint32_t priority = 0;
        for (Entity* entity : scene->root.children)
            PackSceneObject(scene, outerTransform, entity, priority);

        std::vector<uint16_t> map;

        for (uint32_t objectIndex = 0; objectIndex < scene->sceneObjectPack.size(); objectIndex++) {
            PackedSceneObject const& object = scene->sceneObjectPack[objectIndex];
            Bounds bounds = SceneObjectBounds(scene, object);

            uint16_t nodeIndex = static_cast<uint16_t>(scene->sceneNodePack.size());
            map.push_back(nodeIndex);

            PackedSceneNode node = {
                .minimum = bounds.minimum,
                .childNodeIndices = 0,
                .maximum = bounds.maximum,
                .objectIndex = objectIndex,
            };
            scene->sceneNodePack.push_back(node);
        }

        auto FindBestMatch = [](
            Scene const* scene,
            std::vector<uint16_t> const& map,
            uint16_t indexA
        ) -> uint16_t {
            glm::vec3 minA = scene->sceneNodePack[map[indexA]].minimum;
            glm::vec3 maxA = scene->sceneNodePack[map[indexA]].maximum;

            float bestArea = INFINITY;
            uint16_t bestIndexB = 0xFFFF;

            for (uint16_t indexB = 0; indexB < map.size(); indexB++) {
                if (indexA == indexB) continue;

                glm::vec3 minB = scene->sceneNodePack[map[indexB]].minimum;
                glm::vec3 maxB = scene->sceneNodePack[map[indexB]].maximum;
                glm::vec3 size = glm::max(maxA, maxB) - glm::min(minA, minB);
                float area = size.x * size.y + size.y * size.z + size.z * size.z;
                if (area <= bestArea) {
                    bestArea = area;
                    bestIndexB = indexB;
                }
            }

            return bestIndexB;
        };

        uint16_t indexA = 0;
        uint16_t indexB = FindBestMatch(scene, map, indexA);

        while (map.size() > 1) {
            uint16_t indexC = FindBestMatch(scene, map, indexB);
            if (indexA == indexC) {
                uint16_t nodeIndexA = map[indexA];
                PackedSceneNode const& nodeA = scene->sceneNodePack[nodeIndexA];
                uint16_t nodeIndexB = map[indexB];
                PackedSceneNode const& nodeB = scene->sceneNodePack[nodeIndexB];

                PackedSceneNode node = {
                    .minimum = glm::min(nodeA.minimum, nodeB.minimum),
                    .childNodeIndices = uint32_t(nodeIndexA) | uint32_t(nodeIndexB) << 16,
                    .maximum = glm::max(nodeA.maximum, nodeB.maximum),
                    .objectIndex = 0xFFFFFFFF,
                };

                map[indexA] = static_cast<uint16_t>(scene->sceneNodePack.size());
                map[indexB] = map.back();
                map.pop_back();

                if (indexA == map.size())
                    indexA = indexB;

                scene->sceneNodePack.push_back(node);

                indexB = FindBestMatch(scene, map, indexA);
            }
            else {
                indexA = indexB;
                indexB = indexC;
            }
        }

        scene->sceneNodePack[0] = scene->sceneNodePack[map[indexA]];
        scene->sceneNodePack[map[indexA]] = scene->sceneNodePack.back();
        scene->sceneNodePack.pop_back();

        //PrintSceneNode(scene, 0, 0);
    }

    scene->dirtyFlags = 0;

    return dirtyFlags;
}

static void IntersectMeshFace(Scene* scene, Ray ray, uint32_t meshFaceIndex, Hit& hit)
{
    PackedMeshFace face = scene->meshFacePack[meshFaceIndex];

    float r = glm::dot(face.plane.xyz(), ray.direction);
    if (r > -EPSILON && r < +EPSILON) return;

    float t = -(glm::dot(face.plane.xyz(), ray.origin) + face.plane.w) / r;
    if (t < 0 || t > hit.time) return;

    glm::vec3 v = ray.origin + ray.direction * t - face.position;
    float beta = glm::dot(glm::vec3(face.base1), v);
    if (beta < 0 || beta > 1) return;
    float gamma = glm::dot(glm::vec3(face.base2), v);
    if (gamma < 0 || beta + gamma > 1) return;

    hit.time = t;
    hit.objectType = OBJECT_TYPE_MESH_INSTANCE;
    hit.objectIndex = 0xFFFFFFFF;
    hit.primitiveIndex = meshFaceIndex;
    hit.primitiveCoordinates = glm::vec3(1 - beta - gamma, beta, gamma);
}

static float IntersectMeshNodeBounds(Ray ray, float reach, PackedMeshNode const& node)
{
    // Compute ray time to the axis-aligned planes at the node bounding
    // box minimum and maximum corners.
    glm::vec3 minimum = (node.minimum - ray.origin) / ray.direction;
    glm::vec3 maximum = (node.maximum - ray.origin) / ray.direction;

    // For each coordinate axis, sort out which of the two coordinate
    // planes (at bounding box min/max points) comes earlier in time and
    // which one comes later.
    glm::vec3 earlier = min(minimum, maximum);
    glm::vec3 later = max(minimum, maximum);

    // Compute the ray entry and exit times.  The ray enters the box when
    // it has crossed all of the entry planes, so we take the maximum.
    // Likewise, the ray has exit the box when it has exit at least one
    // of the exit planes, so we take the minimum.
    float entry = glm::max(glm::max(earlier.x, earlier.y), earlier.z);
    float exit = glm::min(glm::min(later.x, later.y), later.z);

    // If the exit time is greater than the entry time, then the ray has
    // missed the box altogether.
    if (exit < entry) return INFINITY;

    // If the exit time is less than 0, then the box is behind the eye.
    if (exit <= 0) return INFINITY;

    // If the entry time is greater than previous hit time, then the box
    // is occluded.
    if (entry >= reach) return INFINITY;

    return entry;
}

static void IntersectMesh(Scene* scene, Ray const& ray, uint32_t rootNodeIndex, Hit& hit)
{
    uint32_t stack[32];
    uint32_t depth = 0;

    PackedMeshNode node = scene->meshNodePack[rootNodeIndex];

    while (true) {
        // Leaf node or internal?
        if (node.faceEndIndex > 0) {
            // Leaf node, trace all geometry within.
            for (uint32_t faceIndex = node.faceBeginOrNodeIndex; faceIndex < node.faceEndIndex; faceIndex++)
                IntersectMeshFace(scene, ray, faceIndex, hit);
        }
        else {
            // Internal node.
            // Load the first subnode as the node to be processed next.
            uint32_t index = node.faceBeginOrNodeIndex;
            node = scene->meshNodePack[index];
            float time = IntersectMeshNodeBounds(ray, hit.time, node);

            // Also load the second subnode to see if it is closer.
            uint32_t indexB = index + 1;
            PackedMeshNode nodeB = scene->meshNodePack[indexB];
            float timeB = IntersectMeshNodeBounds(ray, hit.time, nodeB);

            // If the second subnode is strictly closer than the first one,
            // then it was definitely hit, so process it next.
            if (time > timeB) {
                // If the first subnode was also hit, then set it aside for later.
                if (time < INFINITY) {
                    assert(depth < std::size(stack));
                    stack[depth++] = index;
                }
                node = nodeB;
                continue;
            }

            // The first subnode is at least as close as the second one.
            // If the second subnode was hit, then both of them were,
            // and we should set the second one aside for later.
            if (timeB < INFINITY) {
                assert(depth < std::size(stack));
                stack[depth++] = indexB;
                continue;
            }

            // The first subnode is at least as close as the second one,
            // and the second subnode was not hit.  If the first one was
            // hit, then process it next.
            if (time < INFINITY) continue;
        }

        // Just processed a leaf node or an internal node with no intersecting
        // subnodes.  If the stack is also empty, then we are done.
        if (depth == 0) break;

        // Pull a node from the stack.
        node = scene->meshNodePack[stack[--depth]];
    }
}

static void IntersectObject(Scene* scene, Ray const& worldRay, uint32_t objectIndex, Hit& hit)
{
    PackedSceneObject object = scene->sceneObjectPack[objectIndex];

    Ray ray = TransformRay(worldRay, object.transform.from);

    if (object.type == OBJECT_TYPE_MESH_INSTANCE) {
        IntersectMesh(scene, ray, object.meshRootNodeIndex, hit);
        if (hit.objectIndex == 0xFFFFFFFF)
            hit.objectIndex = objectIndex;
    }

    if (object.type == OBJECT_TYPE_PLANE) {
        float t = -ray.origin.z / ray.direction.z;
        if (t < 0 || t > hit.time) return;

        hit.time = t;
        hit.objectType = OBJECT_TYPE_PLANE;
        hit.objectIndex = objectIndex;
        hit.primitiveIndex = 0;
        hit.primitiveCoordinates = glm::vec3(glm::fract(ray.origin.xy() + ray.direction.xy() * t), 0);
    }

    if (object.type == OBJECT_TYPE_SPHERE) {
        float v = glm::dot(ray.direction, ray.direction);
        float p = glm::dot(ray.origin, ray.direction);
        float q = glm::dot(ray.origin, ray.origin) - 1.0f;
        float d2 = p * p - q * v;
        if (d2 < 0) return;

        float d = glm::sqrt(d2);
        if (d < p) return;

        float s0 = -p - d;
        float s1 = -p + d;
        float s = s0 < 0 ? s1 : s0;
        if (s < 0 || s > v * hit.time) return;

        hit.time = s / v;
        hit.objectType = OBJECT_TYPE_SPHERE;
        hit.objectIndex = objectIndex;
    }

    if (object.type == OBJECT_TYPE_CUBE) {
        glm::vec3 minimum = (glm::vec3(-1,-1,-1) - ray.origin) / ray.direction;
        glm::vec3 maximum = (glm::vec3(+1,+1,+1) - ray.origin) / ray.direction;
        glm::vec3 earlier = min(minimum, maximum);
        glm::vec3 later = max(minimum, maximum);
        float t0 = glm::max(glm::max(earlier.x, earlier.y), earlier.z);
        float t1 = glm::min(glm::min(later.x, later.y), later.z);
        if (t1 < t0) return;
        if (t1 <= 0) return;
        if (t0 >= hit.time) return;

        float t = t0 < 0 ? t1 : t0;

        hit.time = t;
        hit.objectType = OBJECT_TYPE_CUBE;
        hit.objectIndex = objectIndex;
    }
}

static void Intersect(Scene* scene, Ray const& worldRay, Hit& hit)
{
    for (uint32_t objectIndex = 0; objectIndex < scene->sceneObjectPack.size(); objectIndex++)
        IntersectObject(scene, worldRay, objectIndex, hit);
}

bool Trace(Scene* scene, Ray const& ray, Hit& hit)
{
    hit.time = INFINITY;
    Intersect(scene, ray, hit);
    return hit.time < INFINITY;
}
