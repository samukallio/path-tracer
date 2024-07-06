#version 450

#define BIND_SCENE 0    // Scene data in descriptor set 0.
#define BIND_TRACE 1    // Trace buffer in descriptor set 1.

#include "core/common.glsl.inc"
#include "scene/scene.glsl.inc"
#include "scene/trace.glsl.inc"

layout(local_size_x=256, local_size_y=1, local_size_z=1) in;

void main()
{
    uint Index = gl_GlobalInvocationID.x;

    if (Index >= 2048*1024) return;

    ray Ray = LoadTraceRay(Index);
    hit Hit = Trace(Ray);
    StoreTraceHit(Index, Hit);
}
