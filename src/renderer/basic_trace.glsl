#version 450

#include "renderer/basic.glsl.inc"

layout(local_size_x=256, local_size_y=1, local_size_z=1) in;

void main()
{
    uint Index = gl_GlobalInvocationID.x;

    if (Index >= TRACE_COUNT) return;

    ray Ray = LoadTraceRay(Index);
    hit Hit = Trace(Ray);
    StoreTraceHit(Index, Hit);
}
