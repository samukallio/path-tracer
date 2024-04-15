#version 450

const float INFINITY = 1e30f;

struct Ray
{
    vec3    origin;
    vec3    direction;
};

struct Hit
{
    float   time;
    uint    objectType;
    uint    objectIndex;
};

struct Sphere
{
    vec3    center;
    float   radius;
};

struct MeshFace
{
    vec3    position0;
    vec3    position1;
    vec3    position2;
    vec3    normal0;
    vec3    normal1;
    vec3    normal2;
};

struct MeshNode
{
    vec3    minimum;
    vec3    maximum;
    uint    dummyForAlignment;
    uint    faceBeginOrNodeIndex;
    uint    faceEndIndex;
};

layout(binding = 0)
uniform SceneUniformBuffer
{
    mat4    viewMatrixInverse;
    int     sceneFrame;
    vec4    sceneColor;
};

layout(binding = 1, rgba32f)
uniform readonly image2D inputImage;

layout(binding = 2, rgba32f)
uniform writeonly image2D outputImage;

layout(binding = 3, std140)
readonly buffer MeshFaceBuffer
{
    MeshFace meshFaces[];
};

layout(binding = 4, std140)
readonly buffer MeshNodeBuffer
{
    MeshNode meshNodes[];
};

layout(
    local_size_x = 16,
    local_size_y = 16,
    local_size_z = 1)
    in;

void TraceMeshFace(Ray ray, uint meshFaceIndex, inout Hit hit)
{
    MeshFace face = meshFaces[meshFaceIndex];
    vec3 edge1 = face.position1 - face.position0;
    vec3 edge2 = face.position2 - face.position0;
    vec3 h = cross(ray.direction, edge2);
    float a = dot(edge1, h);
    if (a > -1e-9 && a < +1e-9) return;
    float f = 1 / a;
    vec3 s = ray.origin - face.position0;
    float u = f * dot(s, h);
    if (u < 0 || u > 1) return;
    vec3 q = cross(s, edge1);
    float v = f * dot(ray.direction, q);
    if (v < 0 || u + v > 1) return;
    float t = f * dot(edge2, q);
    if (t <= 0.0001) return;
    if (t > hit.time) return;
    hit.time = t;
    hit.objectType = 0;
    hit.objectIndex = meshFaceIndex;
}

float IntersectMeshNodeBounds(Ray ray, float reach, MeshNode node)
{
    // Compute ray time to the axis-aligned planes at the node bounding
    // box minimum and maximum corners.
    vec3 minimum = (node.minimum - ray.origin) / ray.direction;
    vec3 maximum = (node.maximum - ray.origin) / ray.direction;

    // For each coordinate axis, sort out which of the two coordinate
    // planes (at bounding box min/max points) comes earlier in time and
    // which one comes later.
    vec3 earlier = min(minimum, maximum);
    vec3 later = max(minimum, maximum);

    // Compute the ray entry and exit times.  The ray enters the box when
    // it has crossed all of the entry planes, so we take the maximum.
    // Likewise, the ray has exit the box when it has exit at least one
    // of the exit planes, so we take the minimum.
    float entry = max(max(earlier.x, earlier.y), earlier.z);
    float exit = min(min(later.x, later.y), later.z);

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

void TraceMesh(Ray ray, uint rootNodeIndex, inout Hit hit)
{
    uint stack[32];
    uint depth = 0;

    MeshNode node = meshNodes[rootNodeIndex];

    while (true) {
        // Leaf node or internal?
        if (node.faceEndIndex > 0) {
            // Leaf node, trace all geometry within.
            for (uint faceIndex = node.faceBeginOrNodeIndex; faceIndex < node.faceEndIndex; faceIndex++)
                TraceMeshFace(ray, faceIndex, hit);
        }
        else {
            // Internal node.
            // Load the first subnode as the node to be processed next.
            uint index = node.faceBeginOrNodeIndex;
            node = meshNodes[index];
            float time = IntersectMeshNodeBounds(ray, hit.time, node);

            // Also load the second subnode to see if it is closer.
            uint indexB = index + 1;
            MeshNode nodeB = meshNodes[indexB];
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
        node = meshNodes[stack[--depth]];
    }
}

vec4 Trace(Ray ray)
{
    Hit hit;
    hit.time = INFINITY;

    TraceMesh(ray, 0, hit);
    if (hit.time < INFINITY) {
        MeshFace face = meshFaces[hit.objectIndex];

        vec3 position = ray.origin + hit.time * ray.direction;
        vec3 normal = face.normal0;

        vec3 lightPosition = vec3(5, 5, -5);
        vec3 lightDir = normalize(lightPosition - position);
        float intensity = clamp(dot(lightDir, normal), 0, 1);
        vec3 color = intensity * vec3(1, 1, 1);
        return vec4(color, 1);
    }

    return sceneColor;
}

void main()
{
    // Image pixel coordinate: (0,0) to (imageSizeX, imageSizeY).
    ivec2 imagePosition = ivec2(gl_GlobalInvocationID.xy);

    // Normalized image pixel coordinate: (0,0) to (1,1).
    vec2 imagePositionNormalized = imagePosition / vec2(imageSize(outputImage));

    // Invocation may be outside the image if the image dimensions are not
    // multiples of 16 (since a workgroup is 16-by-16).  If that happens,
    // just exit immediately.
    if (imagePositionNormalized.x >= 1 || imagePositionNormalized.y >= 1)
        return;

    // Point on the "virtual near plane" through which the ray passes.
    vec4 nearPoint = viewMatrixInverse * vec4(
        imagePositionNormalized.x - 0.5,
        0.5 - imagePositionNormalized.y,
        -1.0,
        0.0
    );

    vec4 originPoint = viewMatrixInverse * vec4(0, 0, 0, 1);

    // Trace.
    Ray ray;
    ray.origin = originPoint.xyz;
    ray.direction = normalize(nearPoint).xyz;
    imageStore(outputImage, imagePosition, Trace(ray));
}
