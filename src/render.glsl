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
uniform sampler2DArray textureArrayNearest;

layout(binding = 5)
uniform sampler2DArray textureArrayLinear;

layout(binding = 6, std430)
readonly buffer MaterialBuffer
{
    PackedMaterial materials[];
};

layout(binding = 7, std430)
readonly buffer ObjectBuffer
{
    PackedSceneObject objects[];
};

layout(binding = 8, std430)
readonly buffer SceneNodeBuffer
{
    PackedSceneNode sceneNodes[];
};

layout(binding = 9, std430)
readonly buffer MeshFaceBuffer
{
    PackedMeshFace meshFaces[];
};

layout(binding = 10, std430)
readonly buffer MeshNodeBuffer
{
    PackedMeshNode meshNodes[];
};

layout(
    local_size_x = 16,
    local_size_y = 16,
    local_size_z = 1)
    in;

uint randomState;

uint Random()
{
    randomState = randomState * 747796405u + 2891336453u;
    uint s = randomState;
    uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}

// Generate a random number in the range [0,1).
float Random0To1()
{
    return Random() / 4294967296.0f;
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
    float r = length(ray.vector.xy);
    float phi = atan(ray.vector.x, ray.vector.y);
    float theta = atan(-ray.vector.z, r);

    vec2 uv = 0.5 + vec2(phi / TAU, theta / PI);
    return textureLod(skyboxImage, uv, 0).rgb;
}

float IntersectBoundingBox(Ray ray, float reach, vec3 minimum, vec3 maximum)
{
    // Compute ray time to the axis-aligned planes at the node bounding
    // box minimum and maximum corners.
    vec3 minimumT = (minimum - ray.origin) / ray.vector;
    vec3 maximumT = (maximum - ray.origin) / ray.vector;

    // For each coordinate axis, sort out which of the two coordinate
    // planes (at bounding box min/max points) comes earlier in time and
    // which one comes later.
    vec3 earlierT = min(minimumT, maximumT);
    vec3 laterT = max(minimumT, maximumT);

    // Compute the ray entry and exit times.  The ray enters the box when
    // it has crossed all of the entry planes, so we take the maximum.
    // Likewise, the ray has exit the box when it has exit at least one
    // of the exit planes, so we take the minimum.
    float entryT = max(max(earlierT.x, earlierT.y), earlierT.z);
    float exitT = min(min(laterT.x, laterT.y), laterT.z);

    // If the exit time is greater than the entry time, then the ray has
    // missed the box altogether.
    if (exitT < entryT) return INFINITY;

    // If the exit time is less than 0, then the box is behind the eye.
    if (exitT <= 0) return INFINITY;

    // If the entry time is greater than previous hit time, then the box
    // is occluded.
    if (entryT >= reach) return INFINITY;

    return entryT;
}

void IntersectMeshFace(Ray ray, uint meshFaceIndex, inout Hit hit)
{
    PackedMeshFace face = meshFaces[meshFaceIndex];

    float r = dot(face.plane.xyz, ray.vector);
    if (r > -EPSILON && r < +EPSILON) return;

    float t = -(dot(face.plane.xyz, ray.origin) + face.plane.w) / r;
    if (t < 0 || t > hit.time) return;

    vec3 v = ray.origin + ray.vector * t - face.position;
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

void IntersectMeshNode(Ray ray, uint meshNodeIndex, inout Hit hit)
{
    uint stack[32];
    uint depth = 0;

    PackedMeshNode node = meshNodes[meshNodeIndex];

    while (true) {
        hit.meshComplexity++;

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
            float time = IntersectBoundingBox(ray, hit.time, node.minimum, node.maximum);

            // Also load the second subnode to see if it is closer.
            uint indexB = index + 1;
            PackedMeshNode nodeB = meshNodes[indexB];
            float timeB = IntersectBoundingBox(ray, hit.time, nodeB.minimum, nodeB.maximum);

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
    PackedSceneObject object = objects[objectIndex];

    ray = InverseTransformRay(ray, object.transform);

    if (object.type == OBJECT_TYPE_MESH_INSTANCE) {
        IntersectMeshNode(ray, object.meshRootNodeIndex, hit);
        if (hit.objectIndex == 0xFFFFFFFF)
            hit.objectIndex = objectIndex;
    }
    else if (object.type == OBJECT_TYPE_PLANE) {
        float t = - ray.origin.z / ray.vector.z;
        if (t < 0 || t > hit.time) return;

        hit.time = t;
        hit.objectType = OBJECT_TYPE_PLANE;
        hit.objectIndex = objectIndex;
        hit.primitiveIndex = 0;
        hit.primitiveCoordinates = ray.origin + ray.vector * t;
    }
    else if (object.type == OBJECT_TYPE_SPHERE) {
        float v = dot(ray.vector, ray.vector);
        float p = dot(ray.origin, ray.vector);
        float q = dot(ray.origin, ray.origin) - 1.0;
        float d2 = p * p - q * v;
        if (d2 < 0) return;

        float d = sqrt(d2);
        if (d < p) return;

        float s0 = -p - d;
        float s1 = -p + d;
        float s = s0 < 0 ? s1 : s0;
        if (s < 0 || s > v * hit.time) return;

        hit.time = s / v;
        hit.objectType = OBJECT_TYPE_SPHERE;
        hit.objectIndex = objectIndex;
        hit.primitiveIndex = 0;
        hit.primitiveCoordinates = ray.origin + ray.vector * hit.time;
    }
    else if (object.type == OBJECT_TYPE_CUBE) {
        vec3 minimum = (vec3(-1,-1,-1) - ray.origin) / ray.vector;
        vec3 maximum = (vec3(+1,+1,+1) - ray.origin) / ray.vector;
        vec3 earlier = min(minimum, maximum);
        vec3 later = max(minimum, maximum);
        float t0 = max(max(earlier.x, earlier.y), earlier.z);
        float t1 = min(min(later.x, later.y), later.z);
        if (t1 < t0) return;
        if (t1 <= 0) return;
        if (t0 >= hit.time) return;

        float t = t0 < 0 ? t1 : t0;

        hit.time = t;
        hit.objectType = OBJECT_TYPE_CUBE;
        hit.objectIndex = objectIndex;
        hit.primitiveIndex = 0;
        hit.primitiveCoordinates = ray.origin + ray.vector * t;
    }
}

void Intersect(Ray ray, inout Hit hit)
{
    uint stack[32];
    uint depth = 0;

    PackedSceneNode nodeA = sceneNodes[0];
    PackedSceneNode nodeB;

    while (true) {
        hit.sceneComplexity++;

        // Leaf node or internal?
        if (nodeA.childNodeIndices == 0) {
            // Leaf node, intersect object.
            IntersectObject(ray, nodeA.objectIndex, hit);
        }
        else {
            // Internal node.
            uint indexA = nodeA.childNodeIndices & 0xFFFF;
            uint indexB = nodeA.childNodeIndices >> 16;

            nodeA = sceneNodes[indexA];
            nodeB = sceneNodes[indexB];

            float timeA = IntersectBoundingBox(ray, hit.time, nodeA.minimum, nodeA.maximum);
            float timeB = IntersectBoundingBox(ray, hit.time, nodeB.minimum, nodeB.maximum);

            if (timeA > timeB) {
                if (timeA < INFINITY) stack[depth++] = indexA;
                nodeA = nodeB;
                continue;
            }

            if (timeB < INFINITY) {
                stack[depth++] = indexB;
                continue;
            }

            if (timeA < INFINITY) continue;
        }

        if (depth == 0) break;

        nodeA = sceneNodes[stack[--depth]];
    }
}

void ResolveHit(Ray ray, inout Hit hit)
{
    PackedSceneObject object = objects[hit.objectIndex];
    Ray objectRay = InverseTransformRay(ray, object.transform);

    vec3 position = objectRay.origin + objectRay.vector * hit.time;
    vec3 normal = vec3(0, 0, 1);

    if (hit.objectType == OBJECT_TYPE_MESH_INSTANCE) {
        PackedMeshFace face = meshFaces[hit.primitiveIndex];

        normal = face.normals[0] * hit.primitiveCoordinates.x
               + face.normals[1] * hit.primitiveCoordinates.y
               + face.normals[2] * hit.primitiveCoordinates.z;

        hit.uv = face.uvs[0] * hit.primitiveCoordinates.x
               + face.uvs[1] * hit.primitiveCoordinates.y
               + face.uvs[2] * hit.primitiveCoordinates.z;

        hit.materialIndex = face.materialIndex;
        hit.material = materials[face.materialIndex];
    }
    else if (hit.objectType == OBJECT_TYPE_PLANE) {
        PackedSceneObject object = objects[hit.objectIndex];

        normal = vec3(0, 0, 1);

        hit.primitiveIndex = 0;
        hit.materialIndex = object.materialIndex;
        hit.material = materials[object.materialIndex];
        hit.uv = fract(hit.primitiveCoordinates.xy);
    }
    else if (hit.objectType == OBJECT_TYPE_SPHERE) {
        PackedSceneObject object = objects[hit.objectIndex];

        hit.primitiveIndex = 0;
        hit.materialIndex = object.materialIndex;
        hit.material = materials[object.materialIndex];

        vec3 p = hit.primitiveCoordinates;
        float u = (atan(p.y, p.x) + PI) / TAU;
        float v = (p.z + 1.0) / 2.0;
        hit.uv = vec2(u, v);

        normal = position;
    }
    else if (hit.objectType == OBJECT_TYPE_CUBE) {
        PackedSceneObject object = objects[hit.objectIndex];

        hit.materialIndex = object.materialIndex;
        hit.material = materials[object.materialIndex];
        hit.primitiveIndex = 0;

        vec3 p = hit.primitiveCoordinates;
        vec3 q = abs(p);

        if (q.x >= q.y && q.x >= q.z) {
            normal = vec3(sign(p.x), 0, 0);
            hit.uv = 0.5 * (1.0 + p.yz);
        }
        else if (q.y >= q.x && q.y >= q.z) {
            normal = vec3(0, sign(p.y), 0);
            hit.uv = 0.5 * (1.0 + p.xz);
        }
        else {
            normal = vec3(0, 0, sign(p.z));
            hit.uv = 0.5 * (1.0 + p.xy);
        }
    }

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

        vec4 value;
        if ((hit.material.flags & MATERIAL_FLAG_BASE_COLOR_TEXTURE_FILTER_NEAREST) != 0)
            value = textureLod(textureArrayNearest, uvw, 0);
        else
            value = textureLod(textureArrayLinear, uvw, 0);

        hit.material.baseColor *= value.rgb;
        hit.opacity = value.a;
    }
    else {
        hit.opacity = 1.0;
    }

    hit.position = TransformPosition(position, object.transform);
    hit.normal = TransformNormal(normal, object.transform);
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
                ray.origin = ray.origin + ray.vector * scatterTime;
                ray.vector = RandomDirection();
                continue;
            }
            else {
                outputColor += filterColor * SampleSkybox(ray);
                break;
            }
        }

        ResolveHit(ray, hit);

        if (Random0To1() > hit.opacity) {
            // Pass through.
        }
        else if (Random0To1() < hit.material.refraction) {
            vec3 refractionNormal;
            float refractionRatio;

            if (dot(ray.vector, hit.normal) < 0) {
                // Ray is entering the material.
                refractionNormal = -hit.normal;
                refractionRatio = 1.0f / hit.material.refractionIndex;
            }
            else {
                // Ray is exiting the material.
                refractionNormal = hit.normal;
                refractionRatio = hit.material.refractionIndex;
            }

            float cosTheta = dot(refractionNormal, ray.vector);
            float cosThetaPrimeSquared = 1 - refractionRatio * refractionRatio * (1 - cosTheta * cosTheta);
            bool totalInternalReflection = cosThetaPrimeSquared < 0;

            float schlickR0 = pow((1 - refractionRatio) / (1 + refractionRatio), 2);
            float schlickReflectance = schlickR0 + (1 - schlickR0) * pow(1 - cosTheta, 5);
            bool schlickReflection = Random0To1() < schlickReflectance;

            if (totalInternalReflection || schlickReflection) {
                // Reflection.
                ray.vector = reflect(ray.vector, -refractionNormal);
            }
            else {
                // Refraction.
                float cosThetaPrime = sqrt(cosThetaPrimeSquared);
                ray.vector = refractionRatio * ray.vector + (cosThetaPrime - refractionRatio * cosTheta) * refractionNormal;
            }
        }
        else {
            vec3 diffuseDirection = normalize(hit.normal + RandomDirection());
            vec3 specularDirection = reflect(ray.vector, hit.normal);

            ray.vector = normalize(mix(specularDirection, diffuseDirection, hit.material.roughness));

            outputColor += hit.material.emissionColor * filterColor;
            filterColor *= hit.material.baseColor;
        }

        ray.origin = hit.position + 1e-3 * ray.vector;
    }

    return vec4(outputColor.rgb, 1);
}

Hit IntersectAndResolve(Ray ray)
{
    Hit hit;
    hit.time = INFINITY;
    Intersect(ray, hit);
    if (hit.time != INFINITY)
        ResolveHit(ray, hit);
    return hit;
}

vec4 TraceBaseColor(Ray ray, bool shaded)
{
    Hit hit = IntersectAndResolve(ray);

    if (hit.time == INFINITY)
        return vec4(SampleSkybox(ray), 1);

    if (shaded) {
        float shading = dot(hit.normal, -ray.vector);
        if (hit.objectIndex == highlightObjectIndex)
            return vec4((hit.material.baseColor + vec3(1,0,0)) * shading, 1.0);
        else
            return vec4(hit.material.baseColor * shading, 1.0);
    }
    else {
        return vec4(hit.material.baseColor, 1);
    }
}

vec4 TraceNormal(Ray ray)
{
    Hit hit = IntersectAndResolve(ray);
    if (hit.time == INFINITY)
        return vec4(0.5 * (1 - ray.vector), 1);
    return vec4(0.5 * (hit.normal + 1), 1);
}

vec4 TraceMaterialIndex(Ray ray)
{
    Hit hit = IntersectAndResolve(ray);
    if (hit.time == INFINITY)
        return vec4(0, 0, 0, 1);
    return vec4(COLORS[hit.materialIndex % 20], 1);
}

vec4 TracePrimitiveIndex(Ray ray)
{
    Hit hit = IntersectAndResolve(ray);
    if (hit.time == INFINITY)
        return vec4(0, 0, 0, 1);
    return vec4(COLORS[hit.primitiveIndex % 20], 1);
}

vec4 TraceMeshComplexity(Ray ray)
{
    Hit hit;
    hit.time = INFINITY;
    hit.meshComplexity = 0;
    Intersect(ray, hit);

    float alpha = min(hit.meshComplexity / float(renderMeshComplexityScale), 1.0);
    vec3 color = mix(vec3(0,0,0), vec3(0,1,0), alpha);
    return vec4(color, 1);
}

vec4 TraceSceneComplexity(Ray ray)
{
    Hit hit;
    hit.time = INFINITY;
    hit.sceneComplexity = 0;
    Intersect(ray, hit);

    float alpha = min(hit.sceneComplexity / float(renderSceneComplexityScale), 1.0);
    vec3 color = mix(vec3(0,0,0), vec3(0,1,0), alpha);
    return vec4(color, 1);
}

void main()
{
    // Initialize random number generator.
    randomState
        = gl_GlobalInvocationID.y * 65537
        + gl_GlobalInvocationID.x
        + frameRandomSeed * 277803737u;

    // Compute the position of the sample we are going to produce in image
    // coordinates from (0, 0) to (ImageSizeX, ImageSizeY).
    vec2 samplePosition = gl_GlobalInvocationID.xy;

    if ((renderFlags & RENDER_FLAG_SAMPLE_JITTER) != 0)
        samplePosition += vec2(Random0To1(), Random0To1());
    else
        samplePosition += vec2(0.5, 0.5);

    samplePosition *= renderSampleBlockSize;

    // Get the integer position of the pixel that contains the chosen sample.
    ivec2 samplePixelPosition = ivec2(floor(samplePosition));

    // This position can be outside the target image if the image size is not
    // a multiple of the region size (16 * renderSampleBlockSize) handled by
    // one invocation.  If that happens, just exit.
    ivec2 imageSizeInPixels = imageSize(outputImage);
    if (samplePixelPosition.x >= imageSizeInPixels.x) return;
    if (samplePixelPosition.y >= imageSizeInPixels.y) return;

    // Compute normalized sample position from (0, 0) to (1, 1).
    vec2 sampleNormalizedPosition = samplePosition / imageSizeInPixels;

    Ray ray;

    if (cameraModel == CAMERA_MODEL_PINHOLE) {
        vec3 sensorPosition = vec3(
            -cameraSensorSize.x * (sampleNormalizedPosition.x - 0.5),
            -cameraSensorSize.y * (0.5 - sampleNormalizedPosition.y),
            cameraSensorDistance);

        ray.origin = vec3(cameraApertureRadius * RandomPointOnDisk(), 0);
        ray.vector = normalize(ray.origin - sensorPosition);
    }

    else if (cameraModel == CAMERA_MODEL_THIN_LENS) {
        vec3 sensorPosition = vec3(
            -cameraSensorSize.x * (sampleNormalizedPosition.x - 0.5),
            -cameraSensorSize.y * (0.5 - sampleNormalizedPosition.y),
            cameraSensorDistance);

        vec3 objectPosition = -sensorPosition * cameraFocalLength / (sensorPosition.z - cameraFocalLength);

        ray.origin = vec3(cameraApertureRadius * RandomPointOnDisk(), 0);
        ray.vector = normalize(objectPosition - ray.origin);
    }

    else if (cameraModel == CAMERA_MODEL_360) {
        float phi = (sampleNormalizedPosition.x - 0.5f) * TAU;
        float theta = (0.5f - sampleNormalizedPosition.y) * PI;

        ray.origin = vec3(0, 0, 0);
        ray.vector = vec3(cos(theta) * sin(phi), sin(theta), -cos(theta) * cos(phi));
    }

    ray = TransformRay(ray, cameraTransform);

    vec4 sampleValue;

    if (renderMode == RENDER_MODE_PATH_TRACE)
        sampleValue = Trace(ray);
    else if (renderMode == RENDER_MODE_BASE_COLOR)
        sampleValue = TraceBaseColor(ray, false);
    else if (renderMode == RENDER_MODE_BASE_COLOR_SHADED)
        sampleValue = TraceBaseColor(ray, true);
    else if (renderMode == RENDER_MODE_NORMAL)
        sampleValue = TraceNormal(ray);
    else if (renderMode == RENDER_MODE_MATERIAL_INDEX)
        sampleValue = TraceMaterialIndex(ray);
    else if (renderMode == RENDER_MODE_PRIMITIVE_INDEX)
        sampleValue = TracePrimitiveIndex(ray);
    else if (renderMode == RENDER_MODE_MESH_COMPLEXITY)
        sampleValue = TraceMeshComplexity(ray);
    else if (renderMode == RENDER_MODE_SCENE_COMPLEXITY)
        sampleValue = TraceSceneComplexity(ray);

    // Transfer the sample block from the input image to the output image,
    // adding the sample value that we produced at the relevant pixel
    // position.
    ivec2 xy = ivec2(gl_GlobalInvocationID.xy * renderSampleBlockSize);
    for (int i = 0; i < renderSampleBlockSize; i++) {
        for (int j = 0; j < renderSampleBlockSize; j++) {
            ivec2 transferPixelPosition = xy + ivec2(i,j);
            if (transferPixelPosition.x >= imageSizeInPixels.x) break;
            if (transferPixelPosition.y >= imageSizeInPixels.y) return;
            vec4 outputValue = vec4(0,0,0,0);
            if ((renderFlags & RENDER_FLAG_ACCUMULATE) != 0)
                outputValue = imageLoad(inputImage, transferPixelPosition);
            if (transferPixelPosition == samplePixelPosition)
                outputValue += sampleValue;
            imageStore(outputImage, transferPixelPosition, outputValue);
        }
    }
}
