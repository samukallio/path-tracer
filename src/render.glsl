#version 450

const float INFINITY = 1e30f;
const float EPSILON = 1e-9f;
const float PI = 3.141592653f;
const float TAU = 6.283185306f;

const uint HIT_MESH_FACE = 0;
const uint HIT_PLANE = 1;
const uint HIT_SPHERE = 2;

const uint OBJECT_MESH = 0;
const uint OBJECT_PLANE = 1;
const uint OBJECT_SPHERE = 2;

struct Object
{
    vec3    origin;
    uint    type;
    uint    meshRootNodeIndex;
};

struct MeshFace
{
    vec3    position;
    vec4    plane;
    vec3    base1;
    vec3    base2;
    vec3    normals[3];
};

struct MeshNode
{
    vec3    minimum;
    uint    faceBeginOrNodeIndex;
    vec3    maximum;
    uint    faceEndIndex;
};

struct Ray
{
    vec3    origin;
    vec3    direction;
};

struct Hit
{
    float   time;
    uint    type;
    uint    index;
    vec3    data;
};

layout(binding = 0)
uniform SceneUniformBuffer
{
    mat4    viewMatrixInverse;
    uint    frameIndex;
    bool    clearFrame;
    uint    objectCount;
};

layout(binding = 1, rgba32f)
uniform readonly image2D inputImage;

layout(binding = 2, rgba32f)
uniform writeonly image2D outputImage;

layout(binding = 3, std140)
readonly buffer ObjectBuffer
{
    Object objects[];
};

layout(binding = 4, std140)
readonly buffer MeshFaceBuffer
{
    MeshFace meshFaces[];
};

layout(binding = 5, std140)
readonly buffer MeshNodeBuffer
{
    MeshNode meshNodes[];
};

layout(binding = 6, rgba32f)
uniform readonly image2D skyboxImage;

layout(
    local_size_x = 16,
    local_size_y = 16,
    local_size_z = 1)
    in;

uint randomState;

void InitializeRandom()
{
    uvec2 xy = gl_GlobalInvocationID.xy;
    randomState = xy.y * imageSize(inputImage).x + xy.x + frameIndex * 277803737u;
}

uint Random()
{
    randomState = randomState * 747796405u + 2891336453u;
    uint s = randomState;
    uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}

float Random0To1()
{
    return Random() / float(0xFFFFFFFFu);
}

vec3 RandomDirection()
{
    float z = 2 * Random0To1() - 1;
    float r = sqrt(1 - z * z);
    float phi = TAU * Random0To1();
    return vec3(r * cos(phi), r * sin(phi), z);
}

vec3 RandomHemisphereDirection(vec3 normal)
{
    vec3 direction = RandomDirection();
    return direction * sign(dot(normal, direction));
}

/*
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
*/

void TraceMeshFace(Ray ray, uint meshFaceIndex, inout Hit hit)
{
    MeshFace face = meshFaces[meshFaceIndex];

    float r = dot(face.plane.xyz, ray.direction);
    if (r > -EPSILON && r < +EPSILON) return;

    float t = -(dot(face.plane.xyz, ray.origin) + face.plane.w) / r;
    if (t < 0 || t > hit.time) return;

    vec3 v = ray.origin + ray.direction * t - face.position;
    float beta = dot(face.base1, v);
    if (beta < 0 || beta > 1) return;
    float gamma = dot(face.base2, v);
    if (gamma < 0 || beta + gamma > 1) return;

    hit.time = t;
    hit.data = vec3(1 - beta - gamma, beta, gamma);
    hit.type = HIT_MESH_FACE;
    hit.index = meshFaceIndex;
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

void TraceMesh(Ray ray, Object object, inout Hit hit)
{
    uint stack[32];
    uint depth = 0;

    MeshNode node = meshNodes[object.meshRootNodeIndex];

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

void TraceObject(Ray ray, uint objectIndex, inout Hit hit)
{
    Object object = objects[objectIndex];

    if (object.type == OBJECT_MESH)
        TraceMesh(ray, object, hit);

    if (object.type == OBJECT_PLANE) {
        float t = (object.origin.z - ray.origin.z) / ray.direction.z;
        if (t < 0 || t > hit.time) return;

        hit.time = t;
        hit.type = HIT_PLANE;
        hit.index = objectIndex;
        hit.data = vec3(fract(ray.origin.xy + ray.direction.xy * t), 0);
    }
}

vec4 SampleSkybox(Ray ray)
{
    float r = length(ray.direction.xy);

    float phi = atan(ray.direction.x, ray.direction.y);
    float theta = atan(-ray.direction.z, r);

    ivec2 size = imageSize(skyboxImage);

    float x = floor(size.x * (0.5 + phi / 6.28318));
    float y = floor(size.y * (0.5 + theta / 3.14159));
    ivec2 xy = ivec2(int(x), int(y));

    return imageLoad(skyboxImage, min(xy, size - 1));
}

vec4 Trace(Ray ray)
{
    Hit hit;
    hit.time = INFINITY;

    vec4 outputColor = vec4(0, 0, 0, 0);
    vec4 filterColor = vec4(1, 1, 1, 0);

    for (uint bounce = 0; bounce < 5; bounce++) {

        for (uint objectIndex = 0; objectIndex < objectCount; objectIndex++)
            TraceObject(ray, objectIndex, hit);

        if (hit.time == INFINITY) {
            outputColor += filterColor * SampleSkybox(ray);
            break;
        }

        vec3 normal = vec3(0, 0, 1);
        float smoothness = 0.0f;
        vec4 diffuseColor = vec4(0, 0, 0, 0);

        if (hit.type == HIT_MESH_FACE) {
            MeshFace face = meshFaces[hit.index];

            normal = face.normals[0] * hit.data.x
                   + face.normals[1] * hit.data.y
                   + face.normals[2] * hit.data.z;

            diffuseColor = vec4(1, 1, 1, 0);

            smoothness = 0.6f;
        }

        if (hit.type == HIT_PLANE) {
            normal = vec3(0, 0, 1);

            if ((hit.data.x > 0.5 && hit.data.y > 0.5) || (hit.data.x <= 0.5 && hit.data.y <= 0.5))
                diffuseColor = vec4(1.0, 1.0, 1.0, 0);
            else
                diffuseColor = vec4(0.5, 0.5, 0.5, 0);
        }

        vec3 diffuseDirection = normalize(normal + RandomDirection());
        vec3 specularDirection = reflect(ray.direction, normal);

        ray.origin = ray.origin + (hit.time - 1e-3) * ray.direction;
        ray.direction = mix(diffuseDirection, specularDirection, smoothness);

        filterColor *= diffuseColor;

        hit.time = INFINITY;
    }

    return vec4(outputColor.rgb, 1);
}

void main()
{
    InitializeRandom();

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

    vec4 outputValue = Trace(ray);

    if (!clearFrame) {
        outputValue += imageLoad(inputImage, imagePosition);
    }

    imageStore(outputImage, imagePosition, outputValue);
}
