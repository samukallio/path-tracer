#ifndef TRACE_GLSL_INC
#define TRACE_GLSL_INC

const int TRACE_COUNT = 2048*1024;

struct trace_buffer
{
    // Query data.
    float   OriginX                 [TRACE_COUNT];
    float   OriginY                 [TRACE_COUNT];
    float   OriginZ                 [TRACE_COUNT];
    uint    PackedVelocity          [TRACE_COUNT];
    float   Duration                [TRACE_COUNT];

    // Result data.
    float   Time                    [TRACE_COUNT];
    uint    ShapeAndMaterialIndex   [TRACE_COUNT];
    uint    PackedNormal            [TRACE_COUNT];
    uint    PackedTangentX          [TRACE_COUNT];
    float   TextureU                [TRACE_COUNT];
    float   TextureV                [TRACE_COUNT];

    // Result data stored only for specific rendering modes.
    uint    PrimitiveIndex          [TRACE_COUNT];
    uint    SceneComplexity         [TRACE_COUNT];
    uint    MeshComplexity          [TRACE_COUNT];
};

layout(set=BIND_TRACE, binding=0, std430) buffer TraceSSBO
{
    trace_buffer TraceBuffer;
};

ray LoadTraceRay(uint Index)
{
    ray Ray;
    Ray.Origin.x = TraceBuffer.OriginX  [Index];
    Ray.Origin.y = TraceBuffer.OriginY  [Index];
    Ray.Origin.z = TraceBuffer.OriginZ  [Index];
    Ray.Velocity = UnpackUnitVector(TraceBuffer.PackedVelocity[Index]);
    Ray.Duration = TraceBuffer.Duration [Index];

    return Ray;
}

void StoreTraceRay(uint Index, ray Ray)
{
    TraceBuffer.OriginX        [Index] = Ray.Origin.x;
    TraceBuffer.OriginY        [Index] = Ray.Origin.y;
    TraceBuffer.OriginZ        [Index] = Ray.Origin.z;
    TraceBuffer.PackedVelocity [Index] = PackUnitVector(Ray.Velocity);
    TraceBuffer.Duration       [Index] = Ray.Duration;
}

hit LoadTraceHit(uint Index)
{
    hit Hit;

    //if (RenderMode == RENDER_MODE_SCENE_COMPLEXITY)
        Hit.SceneComplexity    = TraceBuffer.SceneComplexity [Index];
    //if (RenderMode == RENDER_MODE_MESH_COMPLEXITY)
        Hit.MeshComplexity     = TraceBuffer.MeshComplexity  [Index];
    //if (RenderMode == RENDER_MODE_PRIMITIVE_INDEX)
        Hit.PrimitiveIndex     = TraceBuffer.PrimitiveIndex  [Index];

    uint ShapeAndMaterialIndex = TraceBuffer.ShapeAndMaterialIndex[Index];

    if (ShapeAndMaterialIndex == 0xFFFFFFFF) {
        Hit.ShapeIndex = SHAPE_INDEX_NONE;
        Hit.Time = HIT_TIME_LIMIT;
        return Hit;
    }

    Hit.ShapeIndex             = ShapeAndMaterialIndex >> 16;
    Hit.MaterialIndex          = ShapeAndMaterialIndex & 0xFFFF;

    Hit.Time                   = TraceBuffer.Time            [Index];

    Hit.Normal                 = UnpackUnitVector(TraceBuffer.PackedNormal   [Index]);
    Hit.TangentX               = UnpackUnitVector(TraceBuffer.PackedTangentX [Index]);
    Hit.TangentY               = cross(Hit.Normal, Hit.TangentX);

    Hit.UV.x                   = TraceBuffer.TextureU        [Index];
    Hit.UV.y                   = TraceBuffer.TextureV        [Index];
    return Hit;
}

void StoreTraceHit(uint Index, hit Hit)
{
    //if (RenderMode == RENDER_MODE_SCENE_COMPLEXITY)
        TraceBuffer.SceneComplexity [Index] = Hit.SceneComplexity;
    //if (RenderMode == RENDER_MODE_MESH_COMPLEXITY)
        TraceBuffer.MeshComplexity  [Index] = Hit.MeshComplexity;
    //if (RenderMode == RENDER_MODE_PRIMITIVE_INDEX)
        TraceBuffer.PrimitiveIndex  [Index] = Hit.PrimitiveIndex;

    if (Hit.ShapeIndex == SHAPE_INDEX_NONE) {
        TraceBuffer.ShapeAndMaterialIndex[Index] = 0xFFFFFFFF;
        return;
    }

    TraceBuffer.ShapeAndMaterialIndex [Index] = (Hit.ShapeIndex << 16) | Hit.MaterialIndex;

    TraceBuffer.Time            [Index] = Hit.Time;
    TraceBuffer.PackedNormal    [Index] = PackUnitVector(Hit.Normal);
    TraceBuffer.PackedTangentX  [Index] = PackUnitVector(Hit.TangentX);
    TraceBuffer.TextureU        [Index] = Hit.UV.x;
    TraceBuffer.TextureV        [Index] = Hit.UV.y;
}

#endif
