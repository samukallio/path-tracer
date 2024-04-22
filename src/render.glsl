#version 450

#include "common.glsl.inc"

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

layout(binding = 1, rgba32f)
uniform readonly image2D inputImage;

layout(binding = 2, rgba32f)
uniform writeonly image2D outputImage;

layout(binding = 3)
uniform sampler2D skyboxImage;

layout(binding = 4)
uniform sampler2DArray textureArray;

layout(binding = 5, std430)
readonly buffer MaterialBuffer
{
    Material materials[];
};

layout(binding = 6, std430)
readonly buffer ObjectBuffer
{
    Object objects[];
};

layout(binding = 7, std430)
readonly buffer MeshFaceBuffer
{
    MeshFace meshFaces[];
};

layout(binding = 8, std430)
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
    randomState = xy.y * imageSize(inputImage).x + xy.x + frameRandomSeed * 277803737u;
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

vec3 SampleSkybox(Ray ray)
{
    float r = length(ray.direction.xy);
    float phi = atan(ray.direction.x, ray.direction.y);
    float theta = atan(-ray.direction.z, r);

    vec2 uv = 0.5 + vec2(phi / TAU, theta / PI);
    return textureLod(skyboxImage, uv, 0).rgb;
}

void IntersectMeshFace(Ray ray, uint meshFaceIndex, inout Hit hit)
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
    hit.objectType = OBJECT_TYPE_MESH_INSTANCE;
    hit.objectIndex = 0xFFFFFFFF;
    hit.primitiveIndex = meshFaceIndex;
    hit.primitiveCoordinates = vec3(1 - beta - gamma, beta, gamma);
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

void IntersectMesh(Ray ray, Object object, inout Hit hit)
{
    uint stack[32];
    uint depth = 0;

    MeshNode node = meshNodes[object.meshRootNodeIndex];

    while (true) {
        // Leaf node or internal?
        if (node.faceEndIndex > 0) {
            // Leaf node, trace all geometry within.
            for (uint faceIndex = node.faceBeginOrNodeIndex; faceIndex < node.faceEndIndex; faceIndex++)
                IntersectMeshFace(ray, faceIndex, hit);
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

void IntersectObject(Ray ray, uint objectIndex, inout Hit hit)
{
    Object object = objects[objectIndex];

    if (object.type == OBJECT_TYPE_MESH_INSTANCE) {
        IntersectMesh(ray, object, hit);
        if (hit.objectIndex == 0xFFFFFFFF)
                hit.objectIndex = objectIndex;
    }

    if (object.type == OBJECT_TYPE_PLANE) {
        float t = - ray.origin.z / ray.direction.z;
        if (t < 0 || t > hit.time) return;

        hit.time = t;
        hit.objectType = OBJECT_TYPE_PLANE;
        hit.objectIndex = objectIndex;
        hit.primitiveIndex = 0;
        hit.primitiveCoordinates = vec3(fract(ray.origin.xy + ray.direction.xy * t), 0);
    }

    if (object.type == OBJECT_TYPE_SPHERE) {
        float tm = dot(ray.direction, -ray.origin);
        float td2 = tm * tm - dot(ray.origin, ray.origin) + 1.0f;
        if (td2 < 0) return;

        float td = sqrt(td2);
        float t0 = tm - td;
        float t1 = tm + td;
        float t = min(t0, t1);
        if (t < 0 || t > hit.time) return;

        hit.time = t;
        hit.objectType = OBJECT_TYPE_SPHERE;
        hit.objectIndex = objectIndex;
    }
}

void Intersect(Ray ray, inout Hit hit)
{
    for (uint objectIndex = 0; objectIndex < sceneObjectCount; objectIndex++) {
        Object object = objects[objectIndex];
        Ray objectRay = TransformRay(ray, object.worldToObjectMatrix);
        IntersectObject(objectRay, objectIndex, hit);
    }
}

void ResolveHit(Ray ray, inout Hit hit)
{
    Object object = objects[hit.objectIndex];
    Ray objectRay = TransformRay(ray, object.worldToObjectMatrix);

    vec3 position = objectRay.origin + objectRay.direction * hit.time;
    vec3 normal = vec3(0, 0, 1);

    if (hit.objectType == OBJECT_TYPE_MESH_INSTANCE) {
        MeshFace face = meshFaces[hit.primitiveIndex];

        normal = face.normals[0] * hit.primitiveCoordinates.x
               + face.normals[1] * hit.primitiveCoordinates.y
               + face.normals[2] * hit.primitiveCoordinates.z;

        hit.uv = face.uvs[0] * hit.primitiveCoordinates.x
               + face.uvs[1] * hit.primitiveCoordinates.y
               + face.uvs[2] * hit.primitiveCoordinates.z;

        hit.materialIndex = face.materialIndex;
        hit.material = materials[face.materialIndex];

        if ((hit.material.flags & MATERIAL_FLAG_BASE_COLOR_TEXTURE) != 0) {
            float u = mix(
                hit.material.baseColorTextureMinimum.x,
                hit.material.baseColorTextureMaximum.x,
                fract(hit.uv.x));

            float v = mix(
                hit.material.baseColorTextureMinimum.y,
                hit.material.baseColorTextureMaximum.y,
                fract(hit.uv.y));

            vec3 uvw = vec3(u, v, hit.material.baseColorTextureIndex);
            hit.material.baseColor *= textureLod(textureArray, uvw, 0).rgb;
        }
    }

    if (hit.objectType == OBJECT_TYPE_PLANE) {
        Object object = objects[hit.objectIndex];

        hit.primitiveIndex = 0;

        normal = vec3(0, 0, 1);

        hit.materialIndex = object.materialIndex;
        hit.material = materials[object.materialIndex];

        vec3 pc = hit.primitiveCoordinates;

        if ((pc.x > 0.5 && pc.y > 0.5) || (pc.x <= 0.5 && pc.y <= 0.5))
            hit.material.baseColor *= vec3(1.0, 1.0, 1.0);
        else
            hit.material.baseColor *= vec3(0.5, 0.5, 0.5);
    }

    if (hit.objectType == OBJECT_TYPE_SPHERE) {
        Object object = objects[hit.objectIndex];
        normal = normalize(position);
        hit.materialIndex = object.materialIndex;
        hit.primitiveIndex = 0;
        hit.material = materials[object.materialIndex];
    }

    hit.position = (object.objectToWorldMatrix * vec4(position, 1)).xyz;
    hit.normal = (object.objectToWorldMatrix * vec4(normal, 0)).xyz;
}

void IntersectAndResolve(Ray ray, inout Hit hit)
{
    Intersect(ray, hit);

    if (hit.time == INFINITY)
        return;

    ResolveHit(ray, hit);
}

vec4 Trace(Ray ray)
{
    vec3 outputColor = vec3(0, 0, 0);
    vec3 filterColor = vec3(1, 1, 1);

    for (uint bounce = 0; bounce <= renderBounceLimit; bounce++) {
        float scatterTime = INFINITY;

        if (sceneScatterRate > 0) {
            scatterTime = -log(Random0To1()) / sceneScatterRate;
        }

        Hit hit;
        hit.time = scatterTime;

        Intersect(ray, hit);

        if (hit.time == scatterTime) {
            if (scatterTime < INFINITY) {
                ray.origin = ray.origin + ray.direction * scatterTime;
                ray.direction = RandomDirection();
                continue;
            }
            else {
                outputColor += filterColor * SampleSkybox(ray);
                break;
            }
        }

        ResolveHit(ray, hit);

        if (Random0To1() < hit.material.refraction) {
            vec3 refractionDirection;

            if (dot(ray.direction, hit.normal) < 0) {
                // Ray is exiting the material.
                refractionDirection = refract(ray.direction, hit.normal, 1.0f / hit.material.refractionIndex);
            }
            else {
                // Ray is entering the material.
                refractionDirection = refract(ray.direction, -hit.normal, hit.material.refractionIndex);
            }

            if (dot(refractionDirection, refractionDirection) > 0) {
                // Normal refraction.
                ray.direction = refractionDirection;
            }
            else {
                // Total internal reflection.
                ray.direction = reflect(ray.direction, hit.normal);
            }
        }
        else {
            vec3 diffuseDirection = normalize(hit.normal + RandomDirection());
            vec3 specularDirection = reflect(ray.direction, hit.normal);

            ray.direction = normalize(mix(specularDirection, diffuseDirection, hit.material.roughness));

            outputColor += hit.material.emissionColor * filterColor;
            filterColor *= hit.material.baseColor;
        }

        ray.origin = hit.position + 1e-3 * ray.direction;
    }

    return vec4(outputColor.rgb, 1);
}

vec4 TraceBaseColor(Ray ray)
{
    Hit hit;
    hit.time = INFINITY;
    IntersectAndResolve(ray, hit);
    if (hit.time == INFINITY)
        return vec4(SampleSkybox(ray), 1);
    return vec4(hit.material.baseColor, 1);
}

vec4 TraceNormal(Ray ray)
{
    Hit hit;
    hit.time = INFINITY;
    IntersectAndResolve(ray, hit);
    if (hit.time == INFINITY)
        return vec4(0.5 * (1 - ray.direction), 1);
    return vec4(0.5 * (hit.normal + 1), 1);
}

vec4 TraceMaterialIndex(Ray ray)
{
    Hit hit;
    hit.time = INFINITY;
    IntersectAndResolve(ray, hit);
    if (hit.time == INFINITY)
        return vec4(0, 0, 0, 1);
    return vec4(COLORS[hit.materialIndex % 20], 1);
}

vec4 TracePrimitiveIndex(Ray ray)
{
    Hit hit;
    hit.time = INFINITY;
    IntersectAndResolve(ray, hit);
    if (hit.time == INFINITY)
        return vec4(0, 0, 0, 1);
    return vec4(COLORS[hit.primitiveIndex % 20], 1);
}

vec4 TraceEdit(Ray ray)
{
    Hit hit;
    hit.time = INFINITY;
    IntersectAndResolve(ray, hit);
    if (hit.time == INFINITY)
        return vec4(0, 0, 0, 1);

    float shading = dot(hit.normal, -ray.direction);
    if (hit.objectIndex == highlightObjectIndex)
        return vec4((hit.material.baseColor + vec3(1,0,0)) * shading, 1.0);
    else
        return vec4(hit.material.baseColor * shading, 1.0);
}

void main()
{
    InitializeRandom();

    // Image pixel coordinate: (0,0) to (canvasSizeX, canvasSizeY).
    vec2 samplePosition = gl_GlobalInvocationID.xy;

    if ((renderFlags & RENDER_FLAG_SAMPLE_JITTER) != 0)
        samplePosition += vec2(Random0To1(), Random0To1());
    else
        samplePosition += vec2(0.5, 0.5);

    samplePosition *= renderSampleBlockSize;

    ivec2 imagePosition = ivec2(floor(samplePosition));
    ivec2 imageSize_ = imageSize(outputImage);

    // Invocation may be outside the image if the image dimensions are not
    // multiples of 16 (since a workgroup is 16-by-16).  If that happens,
    // just exit immediately.
    if (imagePosition.x >= imageSize_.x) return;
    if (imagePosition.y >= imageSize_.y) return;

    vec2 samplePositionNormalized = samplePosition / imageSize_;

    Ray ray;

    if (cameraModel == CAMERA_MODEL_PINHOLE) {
        vec3 sensorPositionNormalized = vec3(
            -cameraSensorSize.x * (samplePositionNormalized.x - 0.5),
            -cameraSensorSize.y * (0.5 - samplePositionNormalized.y),
            cameraSensorDistance);

        ray.origin = vec3(0, 0, 0);
        ray.direction = normalize(-sensorPositionNormalized);
    }

    if (cameraModel == CAMERA_MODEL_THIN_LENS) {
        vec3 sensorPosition = vec3(
            -cameraSensorSize.x * (samplePositionNormalized.x - 0.5),
            -cameraSensorSize.y * (0.5 - samplePositionNormalized.y),
            cameraSensorDistance);

        vec3 objectPosition = -sensorPosition * cameraFocalLength / (sensorPosition.z - cameraFocalLength);

        ray.origin = vec3(cameraApertureRadius * RandomPointOnDisk(), 0);
        ray.direction = normalize(objectPosition - ray.origin);
    }

    if (cameraModel == CAMERA_MODEL_360) {
        float phi = (samplePositionNormalized.x - 0.5f) * TAU;
        float theta = (0.5f - samplePositionNormalized.y) * PI;

        ray.origin = vec3(0, 0, 0);
        ray.direction = vec3(cos(theta) * sin(phi), sin(theta), -cos(theta) * cos(phi));
    }

    ray = TransformRay(ray, cameraWorldMatrix);

    vec4 sampleValue;

    if (renderMode == RENDER_MODE_PATH_TRACE)
        sampleValue = Trace(ray);

    if (renderMode == RENDER_MODE_BASE_COLOR)
        sampleValue = TraceBaseColor(ray);

    if (renderMode == RENDER_MODE_NORMAL)
        sampleValue = TraceNormal(ray);

    if (renderMode == RENDER_MODE_MATERIAL_INDEX)
        sampleValue = TraceMaterialIndex(ray);

    if (renderMode == RENDER_MODE_PRIMITIVE_INDEX)
        sampleValue = TracePrimitiveIndex(ray);

    if (renderMode == RENDER_MODE_EDIT)
        sampleValue = TraceEdit(ray);

    ivec2 xy = ivec2(gl_GlobalInvocationID.xy * renderSampleBlockSize);
    for (int i = 0; i < renderSampleBlockSize; i++) {
        for (int j = 0; j < renderSampleBlockSize; j++) {
            ivec2 transferPosition = xy + ivec2(i,j);
            if (transferPosition.x >= imageSize_.x) break;
            if (transferPosition.y >= imageSize_.y) return;
            vec4 transferValue = vec4(0,0,0,0);
            if ((renderFlags & RENDER_FLAG_ACCUMULATE) != 0)
                transferValue = imageLoad(inputImage, transferPosition);
            if (transferPosition == imagePosition)
                transferValue += sampleValue;
            imageStore(outputImage, transferPosition, transferValue);
        }
    }
}
