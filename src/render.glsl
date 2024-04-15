#version 450

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

void TraceMesh(Ray ray, uint rootNodeIndex, inout Hit hit)
{
    uint stack[32];
    uint depth;

    stack[0] = rootNodeIndex;
    depth = 1;

    while (depth > 0) {
        MeshNode node = meshNodes[stack[--depth]];

        // Compute ray time to the axis-aligned planes at the node bounding
        // box minimum and maximum corners.
        vec3 minimumTime = (node.minimum - ray.origin) / ray.direction;
        vec3 maximumTime = (node.maximum - ray.origin) / ray.direction;

        // For each coordinate axis, sort out which of the two coordinate
        // planes (at bounding box min/max points) comes earlier in time and
        // which one comes later.
        vec3 earlierTime = min(minimumTime, maximumTime);
        vec3 laterTime = max(minimumTime, maximumTime);

        // Compute the ray entry and exit times.  The ray enters the box when
        // it has crossed all of the entry planes, so we take the maximum.
        // Likewise, the ray has exit the box when it has exit at least one
        // of the exit planes, so we take the minimum.
        float entryTime = max(max(earlierTime.x, earlierTime.y), earlierTime.z);
        float exitTime = min(min(laterTime.x, laterTime.y), laterTime.z);

        // If the exit time is greater than the entry time, then the ray has
        // missed the box altogether.
        if (exitTime < entryTime) continue;

        // If the exit time is less than 0, then the box is behind the eye.
        if (exitTime <= 0) continue;

        // If the entry time is greater than previous hit time, then the box
        // is occluded.
        if (entryTime >= hit.time) continue;

        if (node.faceEndIndex > 0) {
            for (uint faceIndex = node.faceBeginOrNodeIndex; faceIndex < node.faceEndIndex; faceIndex++)
                TraceMeshFace(ray, faceIndex, hit);
        }
        else {
            stack[depth++] = node.faceBeginOrNodeIndex+1;
            stack[depth++] = node.faceBeginOrNodeIndex;
        }
    }
}

vec4 Trace(Ray ray)
{
    Hit hit;
    hit.time = 1e30;

    TraceMesh(ray, 0, hit);
    if (hit.time < 1e30) {
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
