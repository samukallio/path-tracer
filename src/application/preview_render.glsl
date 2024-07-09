#version 450
#define BIND_SCENE 0
#include "core/common.glsl.inc"
const uint PREVIEW_RENDER_MODE_BASE_COLOR = 0;
const vec3 COLORS[20] = vec3[20]
layout(set=1, binding=0, std430)
layout(push_constant)
    uint RenderSizeX;
#if VERTEX
vec2 Positions[6] = vec2[]
vec2 ScreenXYs[6] = vec2[]
layout(location = 0) out vec2 ScreenXY;
void main()
#elif FRAGMENT
layout(location = 0) in vec2 ScreenXY;
layout(location = 0) out vec4 OutColor;
void main()
    ray Ray;
    hit Hit = Trace(Ray);
    vec3 Color = vec3(0.0);
    switch (RenderMode)
                if (RenderMode == PREVIEW_RENDER_MODE_BASE_COLOR_SHADED)
    if (Hit.ShapeIndex == SelectedShapeIndex)
    Color *= Brightness;
    uint PixelX = uint(floor(ScreenXY.x * RenderSizeX));
    if (PixelX == MouseX && PixelY == MouseY)
    OutColor = vec4(Color, 1.0);
#endif