#define TINYOBJLOADER_IMPLEMENTATION 
#include "tiny_obj_loader.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "scene.h"

#include <unordered_map>
#include <format>
#include <stdio.h>

constexpr float INF = std::numeric_limits<float>::infinity();

struct MeshFaceBuildData
{
    glm::vec3               vertices[3];
    glm::vec3               normals[3];
    glm::vec2               uvs[3];
    glm::vec3               centroid;
};

struct MeshTreeBuildState
{
    Scene*                  scene;
    std::vector<MeshFaceBuildData> meshFaceDatas;
    uint32_t                depth = 0;
};

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

static void BuildMeshNode(MeshTreeBuildState* state, uint32_t index, uint32_t depth)
{
    Scene* scene = state->scene;
    MeshNode& node = scene->meshNodes[index];

    uint32_t faceCount = node.faceEndIndex - node.faceBeginOrNodeIndex;

    // Compute node bounds.
    node.minimum = { +INF, +INF, +INF };
    node.maximum = { -INF, -INF, -INF };
    for (uint32_t index = node.faceBeginOrNodeIndex; index < node.faceEndIndex; index++) {
        for (int j = 0; j < 3; j++) {
            glm::vec3 const& position = state->meshFaceDatas[index].vertices[j];
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
        for (uint32_t i = node.faceBeginOrNodeIndex; i < node.faceEndIndex; i++) {
            float centroid = state->meshFaceDatas[i].centroid[axis];
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

        for (uint32_t i = node.faceBeginOrNodeIndex; i < node.faceEndIndex; i++) {
            // Compute bin index of the face centroid.
            float centroid = state->meshFaceDatas[i].centroid[axis];
            uint32_t binIndexUnclamped = static_cast<uint32_t>(binIndexPerUnit * (centroid - minimum));
            uint32_t binIndex = std::min(binIndexUnclamped, BINS - 1);

            // Grow the bin to accommodate the new face.
            Bin& bin = bins[binIndex];
            MeshFace const& face = scene->meshFaces[i];
            Grow(bin.bounds, state->meshFaceDatas[i].vertices[0]);
            Grow(bin.bounds, state->meshFaceDatas[i].vertices[1]);
            Grow(bin.bounds, state->meshFaceDatas[i].vertices[2]);
            //Grow(bin.bounds, face.positions[0]);
            //Grow(bin.bounds, face.positions[1]);
            //Grow(bin.bounds, face.positions[2]);
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
    uint32_t beginIndex = node.faceBeginOrNodeIndex;
    uint32_t endIndex = node.faceEndIndex;
    uint32_t splitIndex = beginIndex;
    uint32_t swapIndex = endIndex - 1;
    while (splitIndex < swapIndex) {
        if (state->meshFaceDatas[splitIndex].centroid[splitAxis] < splitPosition) {
            splitIndex++;
        }
        else {
            std::swap(scene->meshFaces[splitIndex], scene->meshFaces[swapIndex]);
            std::swap(state->meshFaceDatas[splitIndex], state->meshFaceDatas[swapIndex]);
            swapIndex--;
        }
    }

    if (splitIndex == beginIndex || splitIndex == endIndex)
        return;

    uint32_t leftNodeIndex = static_cast<uint32_t>(scene->meshNodes.size());
    uint32_t rightNodeIndex = leftNodeIndex + 1;

    node.faceBeginOrNodeIndex = leftNodeIndex;
    node.faceEndIndex = 0;

    scene->meshNodes.push_back(MeshNode {
        .faceBeginOrNodeIndex = beginIndex,
        .faceEndIndex = splitIndex,
    });

    scene->meshNodes.push_back(MeshNode {
        .faceBeginOrNodeIndex = splitIndex,
        .faceEndIndex = endIndex,
    });

    state->depth = std::max(state->depth, depth+1);

    BuildMeshNode(state, leftNodeIndex, depth+1);
    BuildMeshNode(state, rightNodeIndex, depth+1);
}

uint32_t AddTextureFromFile(Scene* scene, char const* path)
{
    int width, height, channelsInFile;
    stbi_uc* pixels = stbi_load(path, &width, &height, &channelsInFile, 4);
    if (!pixels) return -1;

    size_t index = scene->textures.size();

    scene->textures.push_back({
        .width = static_cast<uint32_t>(width),
        .height = static_cast<uint32_t>(height),
        .pixels = pixels,
    });

    return static_cast<uint32_t>(index);
}

bool LoadMesh(Scene* scene, char const* path, float scale)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn;
    std::string err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path, "../scene/"))
        return false;

    MeshTreeBuildState state;
    state.scene = scene;

    size_t faceCount = 0;
    for (auto const& shape : shapes)
        faceCount += shape.mesh.indices.size() / 3;

    state.meshFaceDatas.reserve(faceCount);

    // Map from in-file texture name to scene texture index.
    std::unordered_map<std::string, uint32_t> textureIndexMap;
    // Map from in-file material IDs to scene material index.
    std::unordered_map<int, uint32_t> materialIndexMap;

    // Scan the material definitions and build scene materials.
    for (int materialId = 0; materialId < materials.size(); materialId++) {
        tinyobj::material_t const& material = materials[materialId];

        std::string textureNames[2] = {
            material.diffuse_texname,
            material.emissive_texname,
        };

        uint32_t textureIndices[2] = {};

        for (int i = 0; i < std::size(textureNames); i++) {
            std::string const& textureName = textureNames[i];
            if (!textureName.empty()) {
                if (!textureIndexMap.contains(textureName)) {
                    std::string texturePath = std::format("../scene/{}", textureName);
                    textureIndexMap[textureName] = AddTextureFromFile(scene, texturePath.c_str());
                }
                textureIndices[i] = textureIndexMap[textureName];
            }
            else {
                textureIndices[i] = 0;
            }
        }

        materialIndexMap[materialId] = static_cast<uint32_t>(scene->materials.size());

        scene->materials.push_back({
            .baseColor = glm::vec4(
                material.diffuse[0],
                material.diffuse[1],
                material.diffuse[2],
                1.0),
            .baseColorTextureIndex = textureIndices[0],
            .emissionColor = glm::vec4(
                material.emission[0],
                material.emission[1],
                material.emission[2],
                1.0),
            .emissionColorTextureIndex = textureIndices[1],
            .roughness = 1.0f, //material.roughness,
            .refraction = 0.0f,
            .refractionIndex = 0.0f,
            .baseColorTextureSize = glm::uvec2(
                scene->textures[textureIndices[0]].width,
                scene->textures[textureIndices[0]].height
            ),
        });
    }

    glm::mat4 normalTransform = {
        { 0, 1, 0, 0 },
        { 0, 0, 1, 0 },
        { 1, 0, 0, 0 },
        { 0, 0, 0, 1 },
    };

    //glm::mat4 normalTransform = {
    //    { 1, 0, 0, 0 },
    //    { 0, 1, 0, 0 },
    //    { 0, 0, 1, 0 },
    //    { 0, 0, 0, 1 },
    //};


    glm::mat4 vertexTransform = scale * normalTransform;

    glm::mat3x2 uvTransform = {
        { 1,  0 },
        { 0, -1 },
        { 0,  1 },
    };

    for (tinyobj::shape_t const& shape : shapes) {
        size_t shapeIndexCount = shape.mesh.indices.size();

        for (size_t i = 0; i < shapeIndexCount; i += 3) {
            MeshFace face;
            MeshFaceBuildData faceData;

            for (int j = 0; j < 3; j++) {
                tinyobj::index_t const& index = shape.mesh.indices[i+j];

                faceData.vertices[j] = vertexTransform * glm::vec4(
                    attrib.vertices[3*index.vertex_index+0],
                    attrib.vertices[3*index.vertex_index+1],
                    attrib.vertices[3*index.vertex_index+2],
                    1.0f);

                if (index.normal_index >= 0) {
                    face.normals[j] = normalTransform * glm::vec4(
                        attrib.normals[3*index.normal_index+0],
                        attrib.normals[3*index.normal_index+1],
                        attrib.normals[3*index.normal_index+2],
                        1.0f);
                }

                if (index.texcoord_index >= 0) {
                    face.uvs[j] = uvTransform * glm::vec3(
                        attrib.texcoords[2*index.texcoord_index+0],
                        attrib.texcoords[2*index.texcoord_index+1],
                        1.0f);
                }
            }

            face.position = faceData.vertices[0];
            face.materialIndex = materialIndexMap[shape.mesh.material_ids[i/3]];

            // Compute triangle plane.
            glm::vec3 ab = faceData.vertices[1] - faceData.vertices[0];
            glm::vec3 ac = faceData.vertices[2] - faceData.vertices[0];
            glm::vec3 normal = glm::normalize(glm::cross(ab, ac));
            float d = -glm::dot(normal, glm::vec3(face.position));
            face.plane = glm::vec4(normal, d);

            // Compute reciprocal basis for the tangent plane.
            float bb = glm::dot(ab, ab);
            float bc = glm::dot(ab, ac);
            float cc = glm::dot(ac, ac);
            float idet = 1.0f / (bb * cc - bc * bc);
            face.base1 = (ab * cc - ac * bc) * idet;
            face.base2 = (ac * bb - ab * bc) * idet;

            faceData.centroid = (faceData.vertices[0] + faceData.vertices[1] + faceData.vertices[2]) / 3.0f;

            scene->meshFaces.push_back(face);
            state.meshFaceDatas.push_back(faceData);
        }
    }

    MeshNode root;
    root.faceBeginOrNodeIndex = 0;
    root.faceEndIndex = static_cast<uint32_t>(scene->meshFaces.size());
    scene->meshNodes.clear();
    scene->meshNodes.push_back(root);

    BuildMeshNode(&state, 0, 0);

    printf("mesh faces: %llu\n", scene->meshFaces.size());
    printf("mesh nodes: %llu\n", scene->meshNodes.size());
    printf("max depth: %lu\n", state.depth);

    return true;
}

bool LoadSkybox(Scene* scene, char const* path)
{
    int width, height, components;

    scene->skyboxPixels = stbi_loadf(path, &width, &height, &components, STBI_rgb_alpha);
    scene->skyboxWidth = static_cast<uint32_t>(width);
    scene->skyboxHeight = static_cast<uint32_t>(height);

    return true;
}

void AddMesh(Scene* scene, glm::vec3 origin, uint32_t rootNodeIndex)
{
    scene->objects.push_back({
        .type = OBJECT_TYPE_MESH,
        .meshRootNodeIndex = rootNodeIndex,
    });
}

void AddPlane(Scene* scene, glm::vec3 origin)
{
    scene->objects.push_back({
        .type = OBJECT_TYPE_PLANE,
    });
}

void AddSphere(Scene* scene, glm::vec3 origin, float radius)
{
    scene->objects.push_back({
        .type = OBJECT_TYPE_SPHERE,
    });
}

// ----------------------------------------------------------------------------


static void IntersectMeshFace(Scene* scene, Ray ray, uint32_t meshFaceIndex, Hit& hit)
{
    MeshFace face = scene->meshFaces[meshFaceIndex];

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
    hit.objectType = OBJECT_TYPE_MESH;
    hit.objectIndex = 0xFFFFFFFF;
    hit.primitiveIndex = meshFaceIndex;
}

static float IntersectMeshNodeBounds(Ray ray, float reach, MeshNode const& node)
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

static void IntersectMesh(Scene* scene, Ray const& ray, Object object, Hit& hit)
{
    uint32_t stack[32];
    uint32_t depth = 0;

    MeshNode node = scene->meshNodes[object.meshRootNodeIndex];

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
            node = scene->meshNodes[index];
            float time = IntersectMeshNodeBounds(ray, hit.time, node);

            // Also load the second subnode to see if it is closer.
            uint32_t indexB = index + 1;
            MeshNode nodeB = scene->meshNodes[indexB];
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
        node = scene->meshNodes[stack[--depth]];
    }
}

static void IntersectObject(Scene* scene, Ray const& ray, uint32_t objectIndex, Hit& hit)
{
    Object object = scene->objects[objectIndex];

    if (object.type == OBJECT_TYPE_MESH) {
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
    for (uint32_t objectIndex = 0; objectIndex < scene->objects.size(); objectIndex++) {
        Object& object = scene->objects[objectIndex];
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
