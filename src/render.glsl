#version 450

layout(binding = 0) uniform SceneUniformBuffer
{
    int frame;
    vec4 color;
} scene;

layout(binding = 1, rgba8) uniform readonly image2D inputImage;

layout(binding = 2, rgba8) uniform writeonly image2D outputImage;

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

void main()
{
    ivec2 xy = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(outputImage);

    if (xy.x < size.x && xy.y < size.y) {
        imageStore(outputImage, xy, scene.color);
    }
}
