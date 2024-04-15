#define TINYOBJLOADER_IMPLEMENTATION 
#include "tiny_obj_loader.h"

#include "scene.h"

static void BuildMeshNode(Scene* scene, uint32_t index, std::vector<glm::vec3>& centroids)
{
    MeshNode& node = scene->meshNodes[index];

    // Compute node bounds.
    node.minimum = glm::vec3(+1e30f, +1e30f, +1e30f);
    node.maximum = glm::vec3(-1e30f, -1e30f, -1e30f);
    for (uint32_t index = node.faceBeginOrNodeIndex; index < node.faceEndIndex; index++) {
        for (int j = 0; j < 3; j++) {
            auto const& position = scene->meshFaces[index].positions[j];
            node.minimum = glm::min(node.minimum, position);
            node.maximum = glm::max(node.maximum, position);
        }
    }

    if (node.faceEndIndex - node.faceBeginOrNodeIndex <= 2) return;

    //
    glm::vec3 extent = node.maximum - node.minimum;
    int axis = 0;
    if (extent.y > extent.x) axis = 1;
    if (extent.z > extent[axis]) axis = 2;
    float split = 0.5f * (node.minimum[axis] + node.maximum[axis]);

    //
    uint32_t beginIndex = node.faceBeginOrNodeIndex;
    uint32_t endIndex = node.faceEndIndex;
    uint32_t splitIndex = beginIndex;
    uint32_t swapIndex = endIndex - 1;
    while (splitIndex < swapIndex) {
        if (centroids[splitIndex][axis] < split) {
            splitIndex++;
        }
        else {
            std::swap(scene->meshFaces[splitIndex], scene->meshFaces[swapIndex]);
            std::swap(centroids[splitIndex], centroids[swapIndex]);
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

    BuildMeshNode(scene, leftNodeIndex, centroids);
    BuildMeshNode(scene, rightNodeIndex, centroids);
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

    for (auto const& shape : shapes) {
        size_t n = shape.mesh.indices.size();
        scene->meshFaces.resize(n / 3);
        for (size_t i = 0; i < n; i += 3) {
            MeshFace& face = scene->meshFaces[i / 3];
            for (int j = 0; j < 3; j++) {
                tinyobj::index_t const& index = shape.mesh.indices[i+j];
                face.positions[j].x = attrib.vertices[3*index.vertex_index+0];
                face.positions[j].y = attrib.vertices[3*index.vertex_index+1];
                face.positions[j].z = attrib.vertices[3*index.vertex_index+2];
                face.normals[j].x = attrib.normals[3*index.normal_index+0];
                face.normals[j].y = attrib.normals[3*index.normal_index+1];
                face.normals[j].z = attrib.normals[3*index.normal_index+2];
            }
        }
    }

    scene->meshNodes.clear();

    MeshNode root;
    root.faceBeginOrNodeIndex = 0;
    root.faceEndIndex = static_cast<uint32_t>(scene->meshFaces.size());
    scene->meshNodes.push_back(root);

    std::vector<glm::vec3> centroids;
    centroids.reserve(scene->meshFaces.size());
    for (MeshFace const& face : scene->meshFaces) {
        glm::vec3 centroid = 0.333333f * (
            face.positions[0] +
            face.positions[1] +
            face.positions[2]);
        centroids.push_back(centroid);
    }

    BuildMeshNode(scene, 0, centroids);

    return true;
}