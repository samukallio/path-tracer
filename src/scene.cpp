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

Texture* LoadTexture(Scene* scene, char const* path)
{
    int width, height, channelsInFile;
    stbi_uc* pixels = stbi_load(path, &width, &height, &channelsInFile, 4);
    if (!pixels) return nullptr;

    auto texture = new Texture {
        .name = path,
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

Mesh* LoadModel(Scene* scene, char const* path, LoadModelOptions* options)
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

    auto mesh = new Mesh;
    mesh->name = path;

    // Map from in-file texture name to scene texture.
    std::unordered_map<std::string, Texture*> textureMap;

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

        material->roughness = 1.0f;
        material->refraction = 0.0f;
        material->refractionIndex = 0.0f;

        std::pair<std::string, Texture**> textures[] = {
            { fileMaterial.diffuse_texname, &material->baseColorTexture },
            { fileMaterial.emissive_texname, &material->emissionColorTexture },
        };

        for (auto const& [name, ptexture] : textures) {
            if (!name.empty()) {
                if (!textureMap.contains(name)) {
                    std::string path = std::format("{}/{}", options->directoryPath, name);
                    textureMap[name] = LoadTexture(scene, path.c_str());
                }
                *ptexture = textureMap[name];
            }
            else {
                *ptexture = nullptr;
            }
        }

        mesh->materials.push_back(material);
    }

    uint32_t defaultMaterialIndex = static_cast<uint32_t>(mesh->materials.size());
    bool defaultMaterialNeeded = false;

    size_t faceCount = 0;
    for (auto const& shape : shapes)
        faceCount += shape.mesh.indices.size() / 3;

    mesh->faces.resize(faceCount);

    size_t faceIndex = 0;
    for (tinyobj::shape_t const& shape : shapes) {
        size_t shapeIndexCount = shape.mesh.indices.size();

        for (size_t i = 0; i < shapeIndexCount; i += 3) {
            MeshFace& face = mesh->faces[faceIndex];

            for (int j = 0; j < 3; j++) {
                tinyobj::index_t const& index = shape.mesh.indices[i+j];

                face.vertices[j] = options->vertexTransform * glm::vec4(
                    attrib.vertices[3*index.vertex_index+0],
                    attrib.vertices[3*index.vertex_index+1],
                    attrib.vertices[3*index.vertex_index+2],
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
            if (materialId >= 0) {
                face.materialIndex = static_cast<uint32_t>(materialId);
            }
            else {
                face.materialIndex = defaultMaterialIndex;
                defaultMaterialNeeded = true;
            }

            face.centroid = (face.vertices[0] + face.vertices[1] + face.vertices[2]) / 3.0f;

            faceIndex++;
        }
    }

    if (defaultMaterialNeeded) {
        mesh->materials.push_back(options->defaultMaterial);
    }

    mesh->nodes.reserve(2 * mesh->faces.size());

    MeshNode root = {
        .faceBeginIndex = 0,
        .faceEndIndex = static_cast<uint32_t>(mesh->faces.size()),
        .childNodeIndex = 0,
    };

    mesh->nodes.push_back(root);
    BuildMeshNode(mesh, 0, 0);

    scene->meshes.push_back(mesh);

    scene->dirtyFlags |= SCENE_DIRTY_MATERIALS;
    scene->dirtyFlags |= SCENE_DIRTY_MESHES;

    return mesh;
}

bool LoadSkybox(Scene* scene, char const* path)
{
    int width, height, components;

    scene->skyboxPixels = stbi_loadf(path, &width, &height, &components, STBI_rgb_alpha);
    scene->skyboxWidth = static_cast<uint32_t>(width);
    scene->skyboxHeight = static_cast<uint32_t>(height);

    scene->dirtyFlags |= SCENE_DIRTY_SKYBOX;

    return true;
}

uint32_t BakeSceneData(Scene* scene)
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

        scene->packedImages.clear();

        while (!rects.empty()) {
            stbrp_context context;
            stbrp_init_target(&context, 4096, 4096, nodes.data(), static_cast<int>(nodes.size()));
            stbrp_pack_rects(&context, rects.data(), static_cast<int>(rects.size()));

            uint32_t* pixels = new uint32_t[4096 * 4096];

            uint32_t packedImageIndex = static_cast<uint32_t>(scene->packedImages.size());

            for (stbrp_rect& rect : rects) {
                if (!rect.was_packed)
                    continue;

                Texture* texture = scene->textures[rect.id];
                assert(texture->width == rect.w);
                assert(texture->height == rect.h);

                texture->packedImageIndex = packedImageIndex;
                texture->packedImageMinimum = {
                    rect.x / float(ATLAS_WIDTH),
                    (rect.y + rect.h) / float(ATLAS_HEIGHT),
                };
                texture->packedImageMaximum = {
                    (rect.x + rect.w) / float(ATLAS_WIDTH),
                    rect.y / float(ATLAS_HEIGHT),
                };

                for (uint32_t y = 0; y < texture->height; y++) {
                    uint32_t const* src = texture->pixels + y * texture->width;
                    uint32_t* dst = pixels + (rect.y + y) * ATLAS_WIDTH + rect.x;
                    memcpy(dst, src, texture->width * sizeof(uint32_t));
                }
            }

            PackedImage packed = {
                .width = ATLAS_WIDTH,
                .height = ATLAS_HEIGHT,
                .pixels = pixels,
            };
            scene->packedImages.push_back(packed);

            std::erase_if(rects, [](stbrp_rect& r) { return r.was_packed; });
        }

        dirtyFlags |= SCENE_DIRTY_MATERIALS;
    }

    // Pack materials.
    if (dirtyFlags & SCENE_DIRTY_MATERIALS) {
        scene->packedMaterials.clear();

        // Fallback material.
        {
            PackedMaterial packed = {
                .baseColor = glm::vec3(1, 1, 1),
                .emissionColor = glm::vec3(0, 0, 0),
                .metallic = 0.0f,
                .roughness = 1.0f,
                .refraction = 0.0f,
                .refractionIndex = 1.0f,
                .flags = 0
            };
            scene->packedMaterials.push_back(packed);
        }

        for (Material* material : scene->materials) {
            PackedMaterial packed;

            packed.flags = 0;

            material->packedMaterialIndex = static_cast<uint32_t>(scene->packedMaterials.size());

            if (Texture* texture = material->baseColorTexture; texture) {
                packed.baseColorTextureIndex = texture->packedImageIndex;
                packed.baseColorTextureMinimum = texture->packedImageMinimum;
                packed.baseColorTextureMaximum = texture->packedImageMaximum;
                packed.flags |= MATERIAL_FLAG_BASE_COLOR_TEXTURE;
            }

            packed.baseColor = material->baseColor;
            packed.emissionColor = material->emissionColor * material->emissionPower;
            packed.metallic = material->metallic;
            packed.roughness = material->roughness;
            packed.refraction = material->refraction;
            packed.refractionIndex = material->refractionIndex;

            scene->packedMaterials.push_back(packed);
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

        scene->packedMeshFaces.clear();
        scene->packedMeshFaces.reserve(faceCount);
        scene->packedMeshNodes.clear();
        scene->packedMeshNodes.reserve(nodeCount);

        for (Mesh* mesh : scene->meshes) {
            uint32_t faceIndexBase = static_cast<uint32_t>(scene->packedMeshFaces.size());
            uint32_t nodeIndexBase = static_cast<uint32_t>(scene->packedMeshNodes.size());

            // Build the packed mesh faces.
            for (MeshFace const& face : mesh->faces) {
                PackedMeshFace packed;

                packed.position = face.vertices[0];

                Material* material = mesh->materials[face.materialIndex];
                packed.materialIndex = material ? material->packedMaterialIndex : 0;

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

                packed.normals[0] = face.normals[0];
                packed.normals[1] = face.normals[1];
                packed.normals[2] = face.normals[2];

                packed.uvs[0] = face.uvs[0];
                packed.uvs[1] = face.uvs[1];
                packed.uvs[2] = face.uvs[2];

                scene->packedMeshFaces.push_back(packed);
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

                scene->packedMeshNodes.push_back(packed);
            }

            mesh->packedRootNodeIndex = nodeIndexBase;
        }

        dirtyFlags |= SCENE_DIRTY_OBJECTS;
    }

    // Pack object data.
    if (dirtyFlags & SCENE_DIRTY_OBJECTS) {
        scene->packedObjects.clear();

        for (Entity* entity : scene->root.children) {
            PackedSceneObject packed;

            packed.materialIndex = 0;

            switch (entity->type) {
                case ENTITY_TYPE_MESH_INSTANCE: {
                    if (!entity->mesh) continue;
                    packed.meshRootNodeIndex = entity->mesh->packedRootNodeIndex;
                    packed.type = OBJECT_TYPE_MESH_INSTANCE;
                    break;
                }
                case ENTITY_TYPE_PLANE: {
                    if (entity->material)
                        packed.materialIndex = entity->material->packedMaterialIndex;
                    packed.type = OBJECT_TYPE_PLANE;
                    break;
                }
                case ENTITY_TYPE_SPHERE: {
                    if (entity->material)
                        packed.materialIndex = entity->material->packedMaterialIndex;
                    packed.type = OBJECT_TYPE_SPHERE;
                    break;
                }
                default: {
                    continue;
                }
            }

            packed.objectToWorldMatrix
                = glm::translate(glm::mat4(1), entity->transform.position)
                * glm::orientate4(entity->transform.rotation)
                * glm::scale(glm::mat4(1), entity->transform.scale);

            packed.worldToObjectMatrix = glm::inverse(packed.objectToWorldMatrix);

            entity->packedObjectIndex = static_cast<uint32_t>(scene->packedObjects.size());

            scene->packedObjects.push_back(packed);
        }
    }

    scene->dirtyFlags = 0;

    return dirtyFlags;
}

static void IntersectMeshFace(Scene* scene, Ray ray, uint32_t meshFaceIndex, Hit& hit)
{
    PackedMeshFace face = scene->packedMeshFaces[meshFaceIndex];

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
    //hit.data = vec3(1 - beta - gamma, beta, gamma);
    hit.objectType = OBJECT_TYPE_MESH_INSTANCE;
    hit.objectIndex = 0xFFFFFFFF;
    hit.primitiveIndex = meshFaceIndex;
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

static void IntersectMesh(Scene* scene, Ray const& ray, PackedSceneObject object, Hit& hit)
{
    uint32_t stack[32];
    uint32_t depth = 0;

    PackedMeshNode node = scene->packedMeshNodes[object.meshRootNodeIndex];

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
            node = scene->packedMeshNodes[index];
            float time = IntersectMeshNodeBounds(ray, hit.time, node);

            // Also load the second subnode to see if it is closer.
            uint32_t indexB = index + 1;
            PackedMeshNode nodeB = scene->packedMeshNodes[indexB];
            float timeB = IntersectMeshNodeBounds(ray, hit.time, nodeB);

            // If the second subnode is strictly closer than the first one,
            // then it was definitely hit, so process it next.
            if (time > timeB) {
                // If the first subnode was also hit, then set it aside for later.
                if (time < INFINITY) stack[depth++] = index;
                node = nodeB;
                continue;
            }

            // The first subnode is at least as close as the second one.
            // If the second subnode was hit, then both of them were,
            // and we should set the second one aside for later.
            if (timeB < INFINITY) {
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
        node = scene->packedMeshNodes[stack[--depth]];
    }
}

static void IntersectObject(Scene* scene, Ray const& ray, uint32_t objectIndex, Hit& hit)
{
    PackedSceneObject object = scene->packedObjects[objectIndex];

    if (object.type == OBJECT_TYPE_MESH_INSTANCE) {
        IntersectMesh(scene, ray, object, hit);
        if (hit.objectIndex == 0xFFFFFFFF)
            hit.objectIndex = objectIndex;
    }

    if (object.type == OBJECT_TYPE_PLANE) {
        float t = -ray.origin.z / ray.direction.z;
        if (t < 0 || t > hit.time) return;

        hit.time = t;
        hit.objectType = OBJECT_TYPE_PLANE;
        hit.objectIndex = objectIndex;
        //hit.data = glm::vec3(fract(ray.origin.xy + ray.direction.xy * t), 0);
    }

    if (object.type == OBJECT_TYPE_SPHERE) {
        float tm = glm::dot(ray.direction, -ray.origin);
        float td2 = tm * tm - glm::dot(-ray.origin, -ray.origin) + 1.0f;
        if (td2 < 0) return;

        float td = sqrt(td2);
        float t0 = tm - td;
        float t1 = tm + td;
        float t = glm::min(t0, t1);
        if (t < 0 || t > hit.time) return;

        hit.time = t;
        hit.objectType = OBJECT_TYPE_SPHERE;
        hit.objectIndex = objectIndex;
    }
}

static void Intersect(Scene* scene, Ray const& ray, Hit& hit)
{
    for (uint32_t objectIndex = 0; objectIndex < scene->packedObjects.size(); objectIndex++) {
        PackedSceneObject& object = scene->packedObjects[objectIndex];
        Ray objectRay = TransformRay(ray, object.worldToObjectMatrix);
        IntersectObject(scene, objectRay, objectIndex, hit);
    }
}

bool Trace(Scene* scene, Ray const& ray, Hit& hit)
{
    hit.time = INFINITY;
    Intersect(scene, ray, hit);
    return hit.time < INFINITY;
}
