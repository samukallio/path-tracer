#version 450

struct Ray
{
    vec3 origin;
    vec3 direction;
};

struct Sphere
{
    vec3 center;
    float radius;
};

layout(binding = 0) uniform SceneUniformBuffer
{
    int frame;
    vec4 color;
} scene;

layout(binding = 1, rgba32f) uniform readonly image2D inputImage;

layout(binding = 2, rgba32f) uniform writeonly image2D outputImage;

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

bool IntersectRaySphere(Ray ray, Sphere sphere, out float t)
{
    vec3 vector = sphere.center - ray.origin;
    float tm = dot(ray.direction, vector);
    float td2 = tm * tm - dot(vector, vector) + sphere.radius * sphere.radius;
    if (td2 >= 0) {
        float td = sqrt(td2);
        float t0 = tm - td;
        float t1 = tm + td;
        t = min(t0, t1);
        return true;
    }
    else {
        t = 0.0;
        return false;
    }
}

vec4 Trace(Ray ray)
{
    Sphere sphere;
    sphere.center = vec3(0, 0, 10);
    sphere.radius = 3.0;

    float time;

    if (IntersectRaySphere(ray, sphere, time)) {
        vec3 position = ray.origin + time * ray.direction;
        vec3 normal = position - sphere.center;

        vec3 lightPosition = vec3(5, 5, 5);

        vec3 lightDir = normalize(lightPosition - position);
        float intensity = clamp(dot(lightDir, normal), 0, 1);

        vec3 color = intensity * vec3(1, 1, 1);

        return vec4(color, 1);
    }

    return scene.color;
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
    vec3 nearPoint = vec3(
        imagePositionNormalized.x - 0.5,
        0.5 - imagePositionNormalized.y,
        1.0
    );

    // Trace.
    Ray ray;
    ray.origin = vec3(0, 0, 0);
    ray.direction = normalize(nearPoint);
    imageStore(outputImage, imagePosition, Trace(ray));
}
