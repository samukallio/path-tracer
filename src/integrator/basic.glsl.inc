#ifndef BASIC_GLSL_INC
#define BASIC_GLSL_INC

#define BIND_SCENE 1

#include "core/common.glsl.inc"
#include "scene/scene.glsl.inc"

const int TRACE_COUNT = 2048*1024;
const int PATH_COUNT = 2048*1024;
const int ACTIVE_SHAPE_LIMIT = 4;

struct path
{
    float NormalizedLambda0;
    vec4  Throughput;          // Path throughput at each of the 4 sampling wavelengths.
    vec4  Weight;              // Path weight at each of the 4 sampling wavelengths.
    vec3  Sample;
    ivec2 ImagePosition;
    uint  ActiveShapeIndex[4];
};

struct trace_buffer
{
    // Query data.
    float OriginX        [TRACE_COUNT];
    float OriginY        [TRACE_COUNT];
    float OriginZ        [TRACE_COUNT];
    uint  PackedVelocity [TRACE_COUNT];
    float Duration       [TRACE_COUNT];

    // Result data.
    float Time                  [TRACE_COUNT];
    uint  ShapeAndMaterialIndex [TRACE_COUNT];
    uint  PackedNormal          [TRACE_COUNT];
    uint  PackedTangentX        [TRACE_COUNT];
    float TextureU              [TRACE_COUNT];
    float TextureV              [TRACE_COUNT];
};

struct path_buffer
{
    int   ImagePosition      [PATH_COUNT];
    float NormalizedLambda0  [PATH_COUNT];
    float Throughput0        [PATH_COUNT];
    float Throughput1        [PATH_COUNT];
    float Throughput2        [PATH_COUNT];
    float Throughput3        [PATH_COUNT];
    float Weight0            [PATH_COUNT];
    float Weight1            [PATH_COUNT];
    float Weight2            [PATH_COUNT];
    float Weight3            [PATH_COUNT];
    float SampleR            [PATH_COUNT];
    float SampleG            [PATH_COUNT];
    float SampleB            [PATH_COUNT];

    uint  ActiveShapeIndex01 [PATH_COUNT];
    uint  ActiveShapeIndex23 [PATH_COUNT];
};

layout(set=0, binding=0, rgba32f)
uniform image2D SampleAccumulatorImage;

layout(set=0, binding=1, std430)
buffer PathSSBO
{
    path_buffer PathBuffer;
};

layout(set=0, binding=2, std430)
buffer TraceSSBO
{
    trace_buffer TraceBuffer;
};

layout(push_constant)
uniform ComputePushConstantBuffer
{
    uint  CameraIndex;
    uint  RenderFlags;
    uint  PathLengthLimit;
    float PathTerminationProbability;
    uint  RandomSeed;
    uint  Restart;
};

ray LoadTraceRay(uint Index)
{
    ray Ray;
    Ray.Origin.x = TraceBuffer.OriginX [Index];
    Ray.Origin.y = TraceBuffer.OriginY [Index];
    Ray.Origin.z = TraceBuffer.OriginZ [Index];
    Ray.Velocity = UnpackUnitVector(TraceBuffer.PackedVelocity[Index]);
    Ray.Duration = TraceBuffer.Duration [Index];

    return Ray;
}

void StoreTraceRay(uint Index, ray Ray)
{
    TraceBuffer.OriginX [Index] = Ray.Origin.x;
    TraceBuffer.OriginY [Index] = Ray.Origin.y;
    TraceBuffer.OriginZ [Index] = Ray.Origin.z;
    TraceBuffer.PackedVelocity [Index] = PackUnitVector(Ray.Velocity);
    TraceBuffer.Duration [Index] = Ray.Duration;
}

hit LoadTraceHit(uint Index)
{
    hit Hit;

    uint ShapeAndMaterialIndex = TraceBuffer.ShapeAndMaterialIndex[Index];

    if (ShapeAndMaterialIndex == 0xFFFFFFFF)
    {
        Hit.ShapeIndex = SHAPE_INDEX_NONE;
        Hit.Time = HIT_TIME_LIMIT;
        return Hit;
    }

    Hit.ShapeIndex = ShapeAndMaterialIndex >> 16;
    Hit.MaterialIndex = ShapeAndMaterialIndex & 0xFFFF;

    Hit.Time = TraceBuffer.Time [Index];

    Hit.Normal = UnpackUnitVector(TraceBuffer.PackedNormal [Index]);
    Hit.TangentX = UnpackUnitVector(TraceBuffer.PackedTangentX [Index]);
    Hit.TangentY = cross(Hit.Normal, Hit.TangentX);

    Hit.UV.x = TraceBuffer.TextureU [Index];
    Hit.UV.y = TraceBuffer.TextureV [Index];
    return Hit;
}

void StoreTraceHit(uint Index, hit Hit)
{
    if (Hit.ShapeIndex == SHAPE_INDEX_NONE)
    {
        TraceBuffer.ShapeAndMaterialIndex[Index] = 0xFFFFFFFF;
        return;
    }

    TraceBuffer.ShapeAndMaterialIndex [Index] = (Hit.ShapeIndex << 16) | Hit.MaterialIndex;

    TraceBuffer.Time [Index] = Hit.Time;
    TraceBuffer.PackedNormal [Index] = PackUnitVector(Hit.Normal);
    TraceBuffer.PackedTangentX [Index] = PackUnitVector(Hit.TangentX);
    TraceBuffer.TextureU [Index] = Hit.UV.x;
    TraceBuffer.TextureV [Index] = Hit.UV.y;
}

path LoadPath(uint Index)
{
    path Path;

    int ImageWidth = imageSize(SampleAccumulatorImage).x;
    Path.ImagePosition.x = PathBuffer.ImagePosition [Index] % ImageWidth;
    Path.ImagePosition.y = PathBuffer.ImagePosition [Index] / ImageWidth;

    Path.NormalizedLambda0 = PathBuffer.NormalizedLambda0 [Index];

    Path.Throughput.x = PathBuffer.Throughput0 [Index];
    Path.Throughput.y = PathBuffer.Throughput1 [Index];
    Path.Throughput.z = PathBuffer.Throughput2 [Index];
    Path.Throughput.w = PathBuffer.Throughput3 [Index];

    Path.Weight.x = PathBuffer.Weight0 [Index];
    Path.Weight.y = PathBuffer.Weight1 [Index];
    Path.Weight.w = PathBuffer.Weight2 [Index];
    Path.Weight.z = PathBuffer.Weight3 [Index];

    Path.Sample.r = PathBuffer.SampleR [Index];
    Path.Sample.g = PathBuffer.SampleG [Index];
    Path.Sample.b = PathBuffer.SampleB [Index];

    uint ActiveShapeIndex01 = PathBuffer.ActiveShapeIndex01[Index];
    Path.ActiveShapeIndex[0] = ActiveShapeIndex01 & 0xFFFF;
    Path.ActiveShapeIndex[1] = ActiveShapeIndex01 >> 16;

    uint ActiveShapeIndex23 = PathBuffer.ActiveShapeIndex23[Index];
    Path.ActiveShapeIndex[2] = ActiveShapeIndex23 & 0xFFFF;
    Path.ActiveShapeIndex[3] = ActiveShapeIndex23 >> 16;

    for (int I = 0; I < 4; I++)
    {
        if (Path.ActiveShapeIndex[I] == 0xFFFF)
            Path.ActiveShapeIndex[I] = SHAPE_INDEX_NONE;
    }

    return Path;
}

void StorePathVertexData(uint Index, path Path)
{
    PathBuffer.Throughput0 [Index] = Path.Throughput.x;
    PathBuffer.Throughput1 [Index] = Path.Throughput.y;
    PathBuffer.Throughput2 [Index] = Path.Throughput.z;
    PathBuffer.Throughput3 [Index] = Path.Throughput.w;
    PathBuffer.Weight0 [Index] = Path.Weight.x;
    PathBuffer.Weight1 [Index] = Path.Weight.y;
    PathBuffer.Weight2 [Index] = Path.Weight.w;
    PathBuffer.Weight3 [Index] = Path.Weight.z;
    PathBuffer.SampleR [Index] = Path.Sample.r;
    PathBuffer.SampleG [Index] = Path.Sample.g;
    PathBuffer.SampleB [Index] = Path.Sample.b;

    PathBuffer.ActiveShapeIndex01 [Index] = (Path.ActiveShapeIndex[1] << 16) | Path.ActiveShapeIndex[0];
    PathBuffer.ActiveShapeIndex23 [Index] = (Path.ActiveShapeIndex[3] << 16) | Path.ActiveShapeIndex[2];
}

void StorePath(uint Index, path Path)
{
    int ImageWidth = imageSize(SampleAccumulatorImage).x;
    PathBuffer.ImagePosition [Index] = Path.ImagePosition.y * ImageWidth + Path.ImagePosition.x;
    PathBuffer.NormalizedLambda0 [Index] = Path.NormalizedLambda0;

    StorePathVertexData(Index, Path);
}

#endif
