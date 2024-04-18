#version 450

const float INFINITY = 1e30f;
const float EPSILON = 1e-9f;
const float PI = 3.141592653f;
const float TAU = 6.283185306f;

const uint MESH = 0;
const uint PLANE = 1;
const uint SPHERE = 2;

const uint CAMERA_PINHOLE = 0;
const uint CAMERA_THIN_LENS = 1;

struct Material
{
    vec3    albedoColor;
    uint    albedoTextureIndex;
    vec4    specularColor;
    vec3    emissiveColor;
    uint    emissiveTextureIndex;
    float   roughness;
    float   specularProbability;
    float   refractProbability;
    float   refractIndex;
    uvec2   albedoTextureSize;
};

struct Object
{
    vec3    origin;
    uint    type;
    vec3    scale;
    uint    materialIndex;
    uint    meshRootNodeIndex;
};

struct MeshFace
{
    vec3    position;
    uint    materialIndex;
    vec4    plane;
    vec3    base1;
    vec3    base2;
    vec3    normals[3];
    vec2    uvs[3];
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
    float       time;
    uint        objectType;
    uint        objectIndex;
    uint        elementIndex;
    vec3        data;

    // Populated by ResolveHit()
    vec3        position;
    vec3        normal;
    vec2        uv;
    Material    material;
};

layout(binding = 0)
uniform SceneUniformBuffer
{
    mat4    viewMatrixInverse;
    vec2    nearPlaneSize;
    uint    frameIndex;
    uint    objectCount;
    uint    clearFrame;

    uint    cameraType;
    float   cameraFocalLength;
    float   cameraApertureRadius;
};

layout(binding = 1, rgba32f)
uniform readonly image2D inputImage;

layout(binding = 2, rgba32f)
uniform writeonly image2D outputImage;

layout(binding = 3)
uniform sampler2D skyboxImage;

layout(binding = 4)
uniform sampler2DArray textureArray;

layout(binding = 5, std140)
readonly buffer MaterialBuffer
{
    Material materials[];
};

layout(binding = 6, std140)
readonly buffer ObjectBuffer
{
    Object objects[];
};

layout(binding = 7, std430)
readonly buffer MeshFaceBuffer
{
    MeshFace meshFaces[];
};

layout(binding = 8, std140)
readonly buffer MeshNodeBuffer
{
    MeshNode meshNodes[];
};

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

vec2 RandomPointOnDisk()
{
    float r = sqrt(Random0To1());
    float theta = Random0To1() * TAU;
    return r * vec2(cos(theta), sin(theta));
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

void ResolveHit(Ray ray, inout Hit hit)
{
    hit.position = ray.origin + ray.direction * hit.time;

    if (hit.objectType == MESH) {
        MeshFace face = meshFaces[hit.elementIndex];
            
        hit.normal = face.normals[0] * hit.data.x
                   + face.normals[1] * hit.data.y
                   + face.normals[2] * hit.data.z;

        hit.uv = face.uvs[0] * hit.data.x
               + face.uvs[1] * hit.data.y
               + face.uvs[2] * hit.data.z;

        hit.material = materials[face.materialIndex];

        vec2 uv = fract(hit.uv) * hit.material.albedoTextureSize / vec2(2048, 2048);
        vec3 uvw = vec3(uv, hit.material.albedoTextureIndex);

        hit.material.albedoColor *= textureLod(textureArray, uvw, 0).rgb;
    }

    if (hit.objectType == PLANE) {
        Object object = objects[hit.objectIndex];

        hit.normal = vec3(0, 0, 1);

        hit.material = materials[object.materialIndex];

        if ((hit.data.x > 0.5 && hit.data.y > 0.5) || (hit.data.x <= 0.5 && hit.data.y <= 0.5))
            hit.material.albedoColor *= vec3(1.0, 1.0, 1.0);
        else
            hit.material.albedoColor *= vec3(0.5, 0.5, 0.5);
    }

    if (hit.objectType == SPHERE) {
        Object object = objects[hit.objectIndex];
        hit.normal = normalize(hit.position - object.origin);
        hit.material = materials[object.materialIndex];
    }
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
    hit.objectType = MESH;
    hit.objectIndex = 0xFFFFFFFF;
    hit.elementIndex = meshFaceIndex;
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

    if (object.type == MESH)
        TraceMesh(ray, object, hit);

    if (object.type == PLANE) {
        float t = (object.origin.z - ray.origin.z) / ray.direction.z;
        if (t < 0 || t > hit.time) return;

        hit.time = t;
        hit.objectType = PLANE;
        hit.objectIndex = objectIndex;
        hit.data = vec3(fract(ray.origin.xy + ray.direction.xy * t), 0);
    }

    if (object.type == SPHERE) {
        vec3 vector = object.origin - ray.origin;
        float tm = dot(ray.direction, vector);
        float td2 = tm * tm - dot(vector, vector) + object.scale.x * object.scale.x;
        if (td2 < 0) return;

        float td = sqrt(td2);
        float t0 = tm - td;
        float t1 = tm + td;
        float t = min(t0, t1);
        if (t < 0 || t > hit.time) return;

        hit.time = t;
        hit.objectType = SPHERE;
        hit.objectIndex = objectIndex;
    }
}

vec3 SampleSkybox(Ray ray)
{
    float r = length(ray.direction.xy);
    float phi = atan(ray.direction.x, ray.direction.y);
    float theta = atan(-ray.direction.z, r);

    vec2 uv = 0.5 + vec2(phi / TAU, theta / PI);
    return textureLod(skyboxImage, uv, 0).rgb;
}

vec4 Trace(Ray ray)
{
    Hit hit;
    hit.time = INFINITY;

    vec3 outputColor = vec3(0, 0, 0);
    vec3 filterColor = vec3(1, 1, 1);

    for (uint bounce = 0; bounce < 5; bounce++) {

        for (uint objectIndex = 0; objectIndex < objectCount; objectIndex++) {
            TraceObject(ray, objectIndex, hit);
            if (hit.objectIndex == 0xFFFFFFFF)
                hit.objectIndex = objectIndex;
        }

        float fogFactor = 1.0;
//        if (hit.time == INFINITY)
//            fogFactor = 0.1;
//        else
//            fogFactor = 0.1 + 0.99 * exp(-hit.time / 1.0);
//

        if (hit.time == INFINITY) {
            outputColor += fogFactor * filterColor * SampleSkybox(ray);
            break;
        }

        ResolveHit(ray, hit);

        vec3 diffuseDirection = normalize(hit.normal + RandomDirection());
        vec3 specularDirection = reflect(ray.direction, hit.normal);

        ray.direction = normalize(mix(specularDirection, diffuseDirection, hit.material.roughness));
        ray.origin = hit.position + 1e-3 * ray.direction;

        outputColor += hit.material.emissiveColor * filterColor;
        filterColor *= hit.material.albedoColor * fogFactor;

        hit.time = INFINITY;
    }

    return vec4(outputColor.rgb, 1);
}

const vec3 COLORS[20] = vec3[20](
    vec3(0.902, 0.098, 0.294),
    vec3(0.235, 0.706, 0.294),
    vec3(1.000, 0.882, 0.098),
    vec3(0.263, 0.388, 0.847),
    vec3(0.961, 0.510, 0.192),
    vec3(0.569, 0.118, 0.706),
    vec3(0.275, 0.941, 0.941),
    vec3(0.941, 0.196, 0.902),
    vec3(0.737, 0.965, 0.047),
    vec3(0.980, 0.745, 0.745),
    vec3(0.000, 0.502, 0.502),
    vec3(0.902, 0.745, 1.000),
    vec3(0.604, 0.388, 0.141),
    vec3(1.000, 0.980, 0.784),
    vec3(0.502, 0.000, 0.000),
    vec3(0.667, 1.000, 0.765),
    vec3(0.502, 0.502, 0.000),
    vec3(1.000, 0.847, 0.694),
    vec3(0.000, 0.000, 0.459),
    vec3(0.502, 0.502, 0.502)
);

vec4 TraceAlbedo(Ray ray)
{
    Hit hit;
    hit.time = INFINITY;

    for (uint objectIndex = 0; objectIndex < objectCount; objectIndex++)
        TraceObject(ray, objectIndex, hit);

    if (hit.time == INFINITY) {
        return vec4(SampleSkybox(ray), 1);
    }

    ResolveHit(ray, hit);

    return vec4(hit.material.albedoColor, 1);
}

void main()
{
    InitializeRandom();

    // Image pixel coordinate: (0,0) to (canvasSizeX, canvasSizeY).
    ivec2 imagePosition = ivec2(gl_GlobalInvocationID.xy);
    ivec2 imageSize_ = imageSize(outputImage);

    // Invocation may be outside the image if the image dimensions are not
    // multiples of 16 (since a workgroup is 16-by-16).  If that happens,
    // just exit immediately.
    if (imagePosition.x >= imageSize_.x) return;
    if (imagePosition.y >= imageSize_.y) return;

    vec2 samplePosition = imagePosition + vec2(Random0To1(), Random0To1());
    vec2 samplePositionNormalized = samplePosition / imageSize_;

    //
    vec3 sensorPosition = vec3(
        -0.032 * (samplePositionNormalized.x - 0.5),
        -0.018 * (0.5 - samplePositionNormalized.y),
        0.0202);

    float focalLength = 0.020;
    float apertureRadius = 0.040;

    vec3 objectPosition = -sensorPosition * focalLength / (sensorPosition.z - focalLength);
    vec3 aperturePosition = vec3(apertureRadius * RandomPointOnDisk(), 0);
    vec3 rayVector = objectPosition - aperturePosition;

    Ray ray;
    ray.origin = (viewMatrixInverse * vec4(aperturePosition, 1)).xyz;
    ray.direction = normalize(viewMatrixInverse * vec4(rayVector, 0)).xyz;

//    //
//    vec2 samplePosition = imagePosition + vec2(Random0To1(), Random0To1());
//    vec2 samplePositionNormalized = samplePosition / imageSize_;
//
//    // Point on the "virtual near plane" through which the ray passes.
//    vec2 nearPlanePositionNormalized = vec2(
//        samplePositionNormalized.x - 0.5,
//        0.5 - samplePositionNormalized.y);
//
//    vec2 nearPlanePosition = nearPlaneSize * nearPlanePositionNormalized;
//    //vec2 dxy = -2.0f * nearPlanePositionNormalized;
//
    // Trace.
//    Ray ray;
    //ray.origin = (viewMatrixInverse * vec4(0, 0, 0, 1)).xyz;
    //ray.direction = (viewMatrixInverse * normalize(vec4(nearPlanePosition, -1, 0))).xyz;

    vec4 outputValue = Trace(ray);

    if (clearFrame == 0) {
        outputValue += imageLoad(inputImage, imagePosition);
    }

    imageStore(outputImage, imagePosition, outputValue);
}
