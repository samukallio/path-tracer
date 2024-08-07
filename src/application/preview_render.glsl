#version 450

#define BIND_SCENE 0

#include "core/common.glsl.inc"
#include "scene/scene.glsl.inc"

const uint PREVIEW_RENDER_MODE_BASE_COLOR = 0;
const uint PREVIEW_RENDER_MODE_BASE_COLOR_SHADED = 1;
const uint PREVIEW_RENDER_MODE_NORMAL = 2;
const uint PREVIEW_RENDER_MODE_MATERIAL_INDEX = 3;
const uint PREVIEW_RENDER_MODE_PRIMITIVE_INDEX = 4;
const uint PREVIEW_RENDER_MODE_MESH_COMPLEXITY = 5;
const uint PREVIEW_RENDER_MODE_SCENE_COMPLEXITY = 6;

const vec3 COLORS[20] = vec3[20]
(
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

layout(set=1, binding=0, std430)
buffer QuerySSBO
{
    uint HitShapeIndex;
};

layout(push_constant)
uniform PreviewPushConstantBuffer
{
    packed_transform CameraTransform;
    uint RenderMode;
    float Brightness;
    uint SelectedShapeIndex;

    uint RenderSizeX;
    uint RenderSizeY;
    uint MouseX;
    uint MouseY;
};

#if VERTEX

vec2 Positions[6] = vec2[]
(
    vec2(-1.0, -1.0),
    vec2( 1.0, -1.0),
    vec2( 1.0, 1.0),
    vec2(-1.0, -1.0),
    vec2( 1.0, 1.0),
    vec2(-1.0, 1.0)
);

vec2 ScreenXYs[6] = vec2[]
(
	vec2(0.0, 0.0),
	vec2(1.0, 0.0),
	vec2(1.0, 1.0),
	vec2(0.0, 0.0),
	vec2(1.0, 1.0),
	vec2(0.0, 1.0)
);

layout(location = 0) out vec2 ScreenXY;

void main()
{
    gl_Position = vec4(Positions[gl_VertexIndex], 0.0, 1.0);
    ScreenXY = ScreenXYs[gl_VertexIndex];
}

#elif FRAGMENT

layout(location = 0) in vec2 ScreenXY;

layout(location = 0) out vec4 OutColor;

void main()
{
    float AspectRatio = float(RenderSizeX) / float(RenderSizeY);
    float NearX = (ScreenXY.x - 0.5) * AspectRatio;
    float NearY = 0.5 - ScreenXY.y;

    ray Ray;
    Ray.Origin = vec3(0, 0, 0);
    Ray.Velocity = normalize(vec3(NearX, NearY, -1.0));
    Ray.Duration = HIT_TIME_LIMIT;
    Ray = TransformRay(Ray, CameraTransform);

    hit Hit = Trace(Ray);

    vec3 Color = vec3(0.0);

    switch (RenderMode)
    {
        case PREVIEW_RENDER_MODE_BASE_COLOR:
        case PREVIEW_RENDER_MODE_BASE_COLOR_SHADED:
        {
            if (Hit.ShapeIndex == SHAPE_INDEX_NONE)
            {
                // We hit the skybox. Generate a color sample from the skybox radiance
                // spectrum by integrating against the standard observer.
                vec4 Spectrum = SampleSkyboxSpectrum(Ray.Velocity);
                Color = CIE_XYZ_TO_SRGB * ObserveParametricSpectrumUnderD65(Spectrum);
            }
            else {
                // We hit a surface. Resolve the base color sample from the reflectance
                // spectrum by integrating against the standard observer.
                Color = CIE_XYZ_TO_SRGB * MaterialBaseColor(Hit.MaterialIndex, Hit.UV);

                if (RenderMode == PREVIEW_RENDER_MODE_BASE_COLOR_SHADED)
                    Color *= dot(Hit.Normal, -Ray.Velocity);
            }
            break;
        }
        case PREVIEW_RENDER_MODE_NORMAL:
        {
            if (Hit.ShapeIndex == SHAPE_INDEX_NONE)
                Color = 0.5 * (1 - Ray.Velocity);
            else
                Color = 0.5 * (Hit.Normal + 1);
            break;
        }
        case PREVIEW_RENDER_MODE_MATERIAL_INDEX:
        {
            if (Hit.ShapeIndex != SHAPE_INDEX_NONE)
                Color = COLORS[Hit.MaterialIndex % 20];
            break;
        }
        case PREVIEW_RENDER_MODE_PRIMITIVE_INDEX:
        {
            if (Hit.ShapeIndex != SHAPE_INDEX_NONE)
                Color = COLORS[Hit.PrimitiveIndex % 20];
            break;
        }
        case PREVIEW_RENDER_MODE_MESH_COMPLEXITY:
        {
            Color = vec3(0,1,0) * Hit.MeshComplexity / 256.0;
            break;
        }
        case PREVIEW_RENDER_MODE_SCENE_COMPLEXITY:
        {
            Color = vec3(0,1,0) * Hit.SceneComplexity / 256.0;
            break;
        }
    }

    if (Hit.ShapeIndex == SelectedShapeIndex)
        Color *= vec3(1.0, 0.5, 0.5);

    Color *= Brightness;

    uint PixelX = uint(floor(ScreenXY.x * RenderSizeX));
    uint PixelY = uint(floor(ScreenXY.y * RenderSizeY));

    if (PixelX == MouseX && PixelY == MouseY)
        HitShapeIndex = Hit.ShapeIndex;

    OutColor = vec4(Color, 1.0);
}

#endif