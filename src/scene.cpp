#define TINYOBJLOADER_IMPLEMENTATION 
#include "tiny_obj_loader.h"

#include "scene.h"

#include <stdio.h>

constexpr float INF = std::numeric_limits<float>::infinity();

struct MeshFaceBuildData
{
    glm::vec3               vertices[3];
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
        constexpr uint32_t BINS = 8;

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

bool LoadScene(Scene* scene, char const* path)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn;
    std::string err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path))
        return false;

    MeshTreeBuildState state;
    state.scene = scene;

    for (auto const& shape : shapes) {
        size_t n = shape.mesh.indices.size();
        scene->meshFaces.resize(n / 3);
        state.meshFaceDatas.resize(n / 3);
        for (size_t i = 0; i < n; i += 3) {
            MeshFace& face = scene->meshFaces[i / 3];
            for (int j = 0; j < 3; j++) {
                tinyobj::index_t const& index = shape.mesh.indices[i+j];
                state.meshFaceDatas[i/3].vertices[j] = {
                    attrib.vertices[3*index.vertex_index+0],
                    attrib.vertices[3*index.vertex_index+1],
                    attrib.vertices[3*index.vertex_index+2],
                };

                face.normals[j].x = attrib.normals[3*index.normal_index+0];
                face.normals[j].y = attrib.normals[3*index.normal_index+1];
                face.normals[j].z = attrib.normals[3*index.normal_index+2];
            }

            glm::vec3 position0 = state.meshFaceDatas[i/3].vertices[0];
            glm::vec3 position1 = state.meshFaceDatas[i/3].vertices[1];
            glm::vec3 position2 = state.meshFaceDatas[i/3].vertices[2];

            face.position = position0;

            state.meshFaceDatas[i/3].centroid = (position0 + position1 + position2) / 3.0f;

            glm::vec3 ab = position1 - position0;
            glm::vec3 ac = position2 - position0;

            // Compute triangle plane.
            glm::vec3 normal = glm::normalize(glm::cross(ab, ac));
            float d = -glm::dot(normal, glm::vec3(face.position));
            face.plane = glm::vec4(normal, d);

            // Compute reciprocal tangent space.
            float bb = glm::dot(ab, ab);
            float bc = glm::dot(ab, ac);
            float cc = glm::dot(ac, ac);
            float idet = 1.0f / (bb * cc - bc * bc);
            face.base1 = (ab * cc - ac * bc) * idet;
            face.base2 = (ac * bb - ab * bc) * idet;
        }
    }

    scene->meshNodes.clear();

    MeshNode root;
    root.faceBeginOrNodeIndex = 0;
    root.faceEndIndex = static_cast<uint32_t>(scene->meshFaces.size());
    scene->meshNodes.push_back(root);

    BuildMeshNode(&state, 0, 0);

    printf("mesh faces: %llu\n", scene->meshFaces.size());
    printf("mesh nodes: %llu\n", scene->meshNodes.size());
    printf("max depth: %lu\n", state.depth);


    return true;
}