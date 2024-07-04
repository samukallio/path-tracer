#version 450

#define BIND_SCENE 1

#include "common.glsl.inc"
#include "openpbr.glsl.inc"

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

layout(set=0, binding=0, rgba32f)
uniform image2D SampleAccumulatorImage;

layout(push_constant)
uniform PreviewPushConstantBuffer
{
    camera  Camera;
    uint    RenderMode;
    uint    RandomSeed;
    uint    SelectedShapeIndex;
};

layout(local_size_x=16, local_size_y=16, local_size_z=1) in;

void main()
{
    // Initialize random number generator.
    RandomState
        = gl_GlobalInvocationID.y * 65537
        + gl_GlobalInvocationID.x
        + RandomSeed * 277803737u;

    ivec2 ImagePosition = ivec2(gl_GlobalInvocationID.xy);
    ivec2 ImageSize = imageSize(SampleAccumulatorImage);

    if (ImagePosition.x >= 2048) return;
    if (ImagePosition.y >= 1024) return;

    // Compute the position of the sample we are going to produce in image
    // coordinates from (0, 0) to (ImageSizeX, ImageSizeY).
    vec2 SamplePosition = ImagePosition;

    //SamplePosition += vec2(Random0To1(), Random0To1());

    // Compute normalized sample position from (0, 0) to (1, 1).
    vec2 NormalizedSamplePosition = SamplePosition / ImageSize;

    ray Ray = GenerateCameraRay(Camera, NormalizedSamplePosition);
    hit Hit = Trace(Ray);

    vec3 Color = vec3(0.0);

    switch (RenderMode) {
        case RENDER_MODE_BASE_COLOR:
        case RENDER_MODE_BASE_COLOR_SHADED:
            if (Hit.ShapeIndex == SHAPE_INDEX_NONE) {
                // We hit the skybox.  Generate a color sample from the skybox radiance
                // spectrum by integrating against the standard observer.
                vec4 Spectrum = SampleSkyboxSpectrum(Ray.Velocity);
                Color = ObserveParametricSpectrumSRGB(Spectrum);
            }
            else {
                // We hit a surface.  Resolve the base color sample from the reflectance
                // spectrum by integrating against the standard observer.
                Color = OpenPBRBaseColor(Hit);

                if (RenderMode == RENDER_MODE_BASE_COLOR_SHADED) {
                    float Shading = dot(Hit.Normal, -Ray.Velocity); 
                    if (Hit.ShapeIndex == SelectedShapeIndex)
                        Color.r += 1.0;
                    Color *= Shading;
                }
            }
            break;

        case RENDER_MODE_NORMAL:
            if (Hit.ShapeIndex == SHAPE_INDEX_NONE)
                Color = 0.5 * (1 - Ray.Velocity);
            else
                Color = 0.5 * (Hit.Normal + 1);
            break;

        case RENDER_MODE_MATERIAL_INDEX:
            if (Hit.ShapeIndex != SHAPE_INDEX_NONE)
                Color = COLORS[Hit.MaterialIndex % 20];
            break;

        case RENDER_MODE_PRIMITIVE_INDEX:
            if (Hit.ShapeIndex != SHAPE_INDEX_NONE)
                Color = COLORS[Hit.PrimitiveIndex % 20];
            break;

        case RENDER_MODE_MESH_COMPLEXITY:
            Color = vec3(0,1,0) * Hit.MeshComplexity / 256.0;
            break;

        case RENDER_MODE_SCENE_COMPLEXITY:
            Color = vec3(0,1,0) * Hit.SceneComplexity / 256.0;
            break;
    }

    imageStore(SampleAccumulatorImage, ImagePosition, vec4(Color, 1.0));
}
