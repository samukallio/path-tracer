#ifndef SCENE_GLSL_INC
#define SCENE_GLSL_INC

#include "core/common.glsl.inc"
#include "core/spectrum.glsl.inc"

const uint SHAPE_INDEX_NONE   = 0xFFFFFFFF;
const uint TEXTURE_INDEX_NONE = 0xFFFFFFFF;

const uint SHAPE_TYPE_MESH_INSTANCE = 0;
const uint SHAPE_TYPE_PLANE         = 1;
const uint SHAPE_TYPE_SPHERE        = 2;
const uint SHAPE_TYPE_CUBE          = 3;

const uint TEXTURE_TYPE_RAW                    = 0;
const uint TEXTURE_TYPE_REFLECTANCE_WITH_ALPHA = 1;
const uint TEXTURE_TYPE_RADIANCE               = 2;

const uint TEXTURE_FLAG_FILTER_NEAREST = 1 << 0;

const uint MATERIAL_TYPE_BASIC_DIFFUSE     = 0;
const uint MATERIAL_TYPE_BASIC_METAL       = 1;
const uint MATERIAL_TYPE_BASIC_TRANSLUCENT = 2;
const uint MATERIAL_TYPE_OPENPBR           = 3;

const uint CAMERA_MODEL_PINHOLE   = 0;
const uint CAMERA_MODEL_THIN_LENS = 1;
const uint CAMERA_MODEL_360       = 2;

struct packed_texture
{
    vec2 AtlasPlacementMinimum;
    vec2 AtlasPlacementMaximum;
    uint AtlasImageIndex;
    uint Type;
    uint Flags;
    uint Dummy2;
};

struct packed_shape
{
    uint Type;
    uint MaterialIndex;
    uint MeshRootNodeIndex;
    packed_transform Transform;
};

struct packed_shape_node
{
    vec3 Minimum;
    uint ChildNodeIndices;
    vec3 Maximum;
    uint ShapeIndex;
};

struct packed_mesh_face
{
    vec3 Position0;
    uint VertexIndex0;
    vec3 Position1;
    uint VertexIndex1;
    vec3 Position2;
    uint VertexIndex2;
};

struct packed_mesh_vertex
{
    uint PackedNormal;
    uint PackedUV;
};

struct packed_mesh_node
{
    vec3 Minimum;
    uint FaceBeginOrNodeIndex;
    vec3 Maximum;
    uint FaceEndIndex;
};

struct packed_camera
{
    uint  Model;
    float FocalLength;
    float ApertureRadius;
    float SensorDistance;
    vec2  SensorSize;
    packed_transform Transform;
};

struct packed_scene_globals
{
    vec3  SkyboxMeanDirection;
    float SkyboxConcentration;
    float SkyboxSamplingProbability;
    float SkyboxBrightness;
    uint  SkyboxTextureIndex;
    uint  ShapeCount;
    float SceneScatterRate;
};

// Result of tracing a ray against the geometry of a scene.
struct hit
{
    float Time;                 // Time of hit along the ray vector.
    uint  ShapeIndex;           // Index (into the shape array) of shape that was hit.
    vec3  Position;             // World space position of the hit.
    vec3  Normal;               // World space normal of the hit surface.
    vec3  TangentX;             // World space tangent of the hit surface.
    uint  MaterialIndex;        // Surface material index.
    vec2  UV;                   // Surface texture map coordinates.

    vec3  TangentY;             // World space bitangent of the hit surface.

    uint  ShapeType;            // Type of shape that was hit.
    vec3  PrimitiveCoordinates; // Shape type-dependent primitive coordinates (e.g. barycentric coordinates).
    uint  PrimitiveIndex;       // Index of primitive within the shape (e.g. mesh face) that was hit.
    uint  SceneComplexity;      // Total number of shape nodes traversed while tracing.
    uint  MeshComplexity;       // Total number of mesh nodes traversed while tracing.
};

layout(set=BIND_SCENE, binding=0)
uniform SceneUBO
{
    packed_scene_globals Scene;
};

layout(set=BIND_SCENE, binding=1)
uniform sampler2DArray TextureArrayNearest;

layout(set=BIND_SCENE, binding=2)
uniform sampler2DArray TextureArrayLinear;

layout(set=BIND_SCENE, binding=3, std430)
readonly buffer TextureSSBO
{
    packed_texture Textures[];
};

layout(set=BIND_SCENE, binding=4, std430)
readonly buffer MaterialSSBO
{
    uint MaterialData[];
};

layout(set=BIND_SCENE, binding=5, std430)
readonly buffer ShapeSSBO
{
    packed_shape Shapes[];
};

layout(set=BIND_SCENE, binding=6, std430)
readonly buffer ShapeNodeSSBO
{
    packed_shape_node ShapeNodes[];
};

layout(set=BIND_SCENE, binding=7, std430)
readonly buffer MeshFaceSSBO
{
    packed_mesh_face MeshFaces[];
};

layout(set=BIND_SCENE, binding=8, std430)
readonly buffer MeshVertexSSBO
{
    packed_mesh_vertex MeshVertices[];
};

layout(set=BIND_SCENE, binding=9, std430)
readonly buffer MeshNodeSSBO
{
    packed_mesh_node MeshNodes[];
};

layout(set=BIND_SCENE, binding=10, std430)
readonly buffer CameraSSBO
{
    packed_camera Cameras[];
};

vec4 SampleTexture(uint Index, vec2 UV)
{
    packed_texture Texture = Textures[Index];

    float U = mix
    (
        Texture.AtlasPlacementMinimum.x,
        Texture.AtlasPlacementMaximum.x,
        fract(UV.x)
    );

    float V = mix
    (
        Texture.AtlasPlacementMinimum.y,
        Texture.AtlasPlacementMaximum.y,
        fract(UV.y)
    );

    vec3 UVW = vec3(U, V, Texture.AtlasImageIndex);

    if ((Texture.Flags & TEXTURE_FLAG_FILTER_NEAREST) != 0)
        return textureLod(TextureArrayNearest, UVW, 0);
    else
        return textureLod(TextureArrayLinear, UVW, 0);
}

// Sample the parametric coefficients of the emission spectrum
// of the environment map in the given direction.
vec4 SampleSkyboxSpectrum(vec3 Direction)
{
    if (Scene.SkyboxTextureIndex == TEXTURE_INDEX_NONE)
        return vec4(0, 0, 100, 1);

    float Phi = atan(Direction.y, Direction.x);
    float Theta = asin(Direction.z);

    float U = 0.5 + Phi / TAU;
    float V = 0.5 + Theta / PI;

    return SampleTexture(Scene.SkyboxTextureIndex, vec2(U, V));
}

// Sample radiances at given wavelengths of the emission spectrum
// of the environment map in the given direction.
vec4 SampleSkyboxRadiance(vec3 Direction, vec4 Lambda)
{
    vec4 Spectrum = SampleSkyboxSpectrum(Direction);
    return SampleParametricSpectrum(Spectrum, Lambda) * Scene.SkyboxBrightness;
}

uint MaterialType(uint MaterialSlotIndex)
{
    return MaterialData[32*MaterialSlotIndex];
}

uint MaterialUint(uint MaterialSlotIndex, uint AttributeIndex)
{
    return MaterialData[32*MaterialSlotIndex + AttributeIndex];
}

float MaterialFloat(uint MaterialSlotIndex, uint AttributeIndex)
{
    return uintBitsToFloat(MaterialUint(MaterialSlotIndex, AttributeIndex));
}

vec3 MaterialVec3(uint MaterialSlotIndex, uint AttributeIndex)
{
    return vec3(
        MaterialFloat(MaterialSlotIndex, AttributeIndex+0),
        MaterialFloat(MaterialSlotIndex, AttributeIndex+1),
        MaterialFloat(MaterialSlotIndex, AttributeIndex+2));
}

vec3 MaterialColor(uint MaterialIndex, uint AttributeIndex)
{
    vec3 Beta = MaterialVec3(MaterialIndex, AttributeIndex);
    return ObserveParametricSpectrumUnderD65(Beta);
}

vec3 MaterialTexturableColor(uint MaterialIndex, uint AttributeIndex, vec2 TextureUV)
{
    vec3 Beta = MaterialVec3(MaterialIndex, AttributeIndex+0);
    vec3 Color = ObserveParametricSpectrumUnderD65(Beta);

    uint TextureIndex = MaterialUint(MaterialIndex, AttributeIndex+3);

    if (TextureIndex != TEXTURE_INDEX_NONE)
    {
        vec3 TextureBeta = SampleTexture(TextureIndex, TextureUV).xyz;
        Color *= ObserveParametricSpectrumUnderD65(TextureBeta);
    }

    return Color;
}

vec4 MaterialTexturableReflectance(uint MaterialIndex, uint AttributeIndex, vec4 Lambda, vec2 TextureUV)
{
    vec3 Beta = MaterialVec3(MaterialIndex, AttributeIndex+0);
    vec4 Value = SampleParametricSpectrum(Beta, Lambda);

    uint TextureIndex = MaterialUint(MaterialIndex, AttributeIndex+3);

    if (TextureIndex != TEXTURE_INDEX_NONE)
    {
        vec3 TextureBeta = SampleTexture(TextureIndex, TextureUV).xyz;
        Value *= SampleParametricSpectrum(TextureBeta, Lambda);
    }

    return Value;
}

float MaterialTexturableValue(uint MaterialIndex, uint AttributeIndex, vec2 TextureUV)
{
    float Value = MaterialFloat(MaterialIndex, AttributeIndex+0);

    uint TextureIndex = MaterialUint(MaterialIndex, AttributeIndex+1);

    if (TextureIndex != TEXTURE_INDEX_NONE)
        Value *= SampleTexture(TextureIndex, TextureUV).r;

    return Value;
}

void IntersectMeshFace(ray Ray, uint MeshFaceIndex, inout hit Hit)
{
    packed_mesh_face Face = MeshFaces[MeshFaceIndex];

    vec3 Edge1 = Face.Position1 - Face.Position0;
    vec3 Edge2 = Face.Position2 - Face.Position0;

    vec3 RayCrossEdge2 = cross(Ray.Velocity, Edge2);
    float Det = dot(Edge1, RayCrossEdge2);

    if (abs(Det) < EPSILON) return;

    float InvDet = 1.0 / Det;

    vec3 S = Ray.Origin - Face.Position0;
    float U = InvDet * dot(S, RayCrossEdge2);
    if (U < 0 || U > 1) return;

    vec3 SCrossEdge1 = cross(S, Edge1);
    float V = InvDet * dot(Ray.Velocity, SCrossEdge1);
    if (V < 0 || U + V > 1) return;

    float T = InvDet * dot(Edge2, SCrossEdge1);
    if (T < 0 || T > Hit.Time) return;

    Hit.Time = T;
    Hit.ShapeType = SHAPE_TYPE_MESH_INSTANCE;
    Hit.ShapeIndex = 0xFFFFFFFE;
    Hit.PrimitiveIndex = MeshFaceIndex;
    Hit.PrimitiveCoordinates = vec3(1 - U - V, U, V);
}

void IntersectMeshNode(ray Ray, uint MeshNodeIndex, inout hit Hit)
{
    uint Stack[32];
    uint Depth = 0;

    packed_mesh_node Node = MeshNodes[MeshNodeIndex];

    while (true)
    {
        Hit.MeshComplexity++;

        // Leaf node or internal?
        if (Node.FaceEndIndex > 0)
        {
            // Leaf node, trace all geometry within.
            for (uint FaceIndex = Node.FaceBeginOrNodeIndex; FaceIndex < Node.FaceEndIndex; FaceIndex++)
                IntersectMeshFace(Ray, FaceIndex, Hit);
        }
        else
        {
            // Internal node.
            // Load the first subnode as the node to be processed next.
            uint Index = Node.FaceBeginOrNodeIndex;
            Node = MeshNodes[Index];
            float Time = IntersectBoundingBox(Ray, Hit.Time, Node.Minimum, Node.Maximum);

            // Also load the second subnode to see if it is closer.
            uint IndexB = Index + 1;
            packed_mesh_node NodeB = MeshNodes[IndexB];
            float TimeB = IntersectBoundingBox(Ray, Hit.Time, NodeB.Minimum, NodeB.Maximum);

            // If the second subnode is strictly closer than the first one,
            // then it was definitely hit, so process it next.
            if (Time > TimeB)
            {
                // If the first subnode was also hit, then set it aside for later.
                if (Time < INFINITY) Stack[Depth++] = Index;
                Node = NodeB;
                continue;
            }

            // The first subnode is at least as close as the second one.
            // If the second subnode was hit, then both of them were,
            // and we should set the second one aside for later.
            if (TimeB < INFINITY)
            {
                Stack[Depth++] = IndexB;
                continue;
            }

            // The first subnode is at least as close as the second one,
            // and the second subnode was not Hit. If the first one was
            // hit, then process it next.
            if (Time < INFINITY) continue;
        }

        // Just processed a leaf node or an internal node with no intersecting
        // subnodes. If the stack is also empty, then we are done.
        if (Depth == 0) break;

        // Pull a node from the stack.
        Node = MeshNodes[Stack[--Depth]];
    }
}

void IntersectShape(ray Ray, uint ShapeIndex, inout hit Hit)
{
    packed_shape Shape = Shapes[ShapeIndex];

    Ray = InverseTransformRay(Ray, Shape.Transform);

    if (Shape.Type == SHAPE_TYPE_MESH_INSTANCE)
    {
        IntersectMeshNode(Ray, Shape.MeshRootNodeIndex, Hit);
        if (Hit.ShapeIndex == 0xFFFFFFFE)
            Hit.ShapeIndex = ShapeIndex;
    }
    else if (Shape.Type == SHAPE_TYPE_PLANE)
    {
        float T = -Ray.Origin.z / Ray.Velocity.z;
        if (T < 0 || T > Hit.Time) return;

        Hit.Time = T;
        Hit.ShapeType = SHAPE_TYPE_PLANE;
        Hit.ShapeIndex = ShapeIndex;
        Hit.PrimitiveIndex = 0;
        Hit.PrimitiveCoordinates = Ray.Origin + Ray.Velocity * T;
    }
    else if (Shape.Type == SHAPE_TYPE_SPHERE)
    {
        float V = dot(Ray.Velocity, Ray.Velocity);
        float P = dot(Ray.Origin, Ray.Velocity);
        float Q = dot(Ray.Origin, Ray.Origin) - 1.0;
        float D2 = P * P - Q * V;
        if (D2 < 0) return;

        float D = sqrt(D2);
        if (D < P) return;

        float S0 = -P - D;
        float S1 = -P + D;
        float S = S0 < 0 ? S1 : S0;
        if (S < 0 || S > V * Hit.Time) return;

        Hit.Time = S / V;
        Hit.ShapeType = SHAPE_TYPE_SPHERE;
        Hit.ShapeIndex = ShapeIndex;
        Hit.PrimitiveIndex = 0;
        Hit.PrimitiveCoordinates = Ray.Origin + Ray.Velocity * Hit.Time;
    }
    else if (Shape.Type == SHAPE_TYPE_CUBE)
    {
        vec3 Minimum = (vec3(-1,-1,-1) - Ray.Origin) / Ray.Velocity;
        vec3 Maximum = (vec3(+1,+1,+1) - Ray.Origin) / Ray.Velocity;
        vec3 Earlier = min(Minimum, Maximum);
        vec3 Later = max(Minimum, Maximum);
        float T0 = max(max(Earlier.x, Earlier.y), Earlier.z);
        float T1 = min(min(Later.x, Later.y), Later.z);
        if (T1 < T0) return;
        if (T1 <= 0) return;

        float T = T0 < 0 ? T1 : T0;
        if (T >= Hit.Time) return;

        Hit.Time = T;
        Hit.ShapeType = SHAPE_TYPE_CUBE;
        Hit.ShapeIndex = ShapeIndex;
        Hit.PrimitiveIndex = 0;
        Hit.PrimitiveCoordinates = Ray.Origin + Ray.Velocity * T;
    }
}

void Intersect(ray Ray, inout hit Hit)
{
    if (Scene.ShapeCount == 0) return;

    uint Stack[32];
    uint Depth = 0;

    packed_shape_node NodeA = ShapeNodes[0];
    packed_shape_node NodeB;

    while (true)
    {
        Hit.SceneComplexity++;

        // Leaf node or internal?
        if (NodeA.ChildNodeIndices == 0)
        {
            // Leaf node, intersect object.
            IntersectShape(Ray, NodeA.ShapeIndex, Hit);
        }
        else
        {
            // Internal node.
            uint IndexA = NodeA.ChildNodeIndices & 0xFFFF;
            uint IndexB = NodeA.ChildNodeIndices >> 16;

            NodeA = ShapeNodes[IndexA];
            NodeB = ShapeNodes[IndexB];

            float TimeA = IntersectBoundingBox(Ray, Hit.Time, NodeA.Minimum, NodeA.Maximum);
            float TimeB = IntersectBoundingBox(Ray, Hit.Time, NodeB.Minimum, NodeB.Maximum);

            if (TimeA > TimeB)
            {
                if (TimeA < INFINITY) Stack[Depth++] = IndexA;
                NodeA = NodeB;
                continue;
            }

            if (TimeB < INFINITY)
            {
                Stack[Depth++] = IndexB;
                continue;
            }

            if (TimeA < INFINITY) continue;
        }

        if (Depth == 0) break;

        NodeA = ShapeNodes[Stack[--Depth]];
    }
}

hit Trace(ray Ray)
{
    hit Hit;
    Hit.ShapeIndex = SHAPE_INDEX_NONE;
    Hit.Time = Ray.Duration;
    Hit.MeshComplexity = 0;
    Hit.SceneComplexity = 0;

    Intersect(Ray, Hit);

    if (Hit.ShapeIndex == SHAPE_INDEX_NONE)
        return Hit;

    packed_shape Shape = Shapes[Hit.ShapeIndex];

    Hit.MaterialIndex = Shape.MaterialIndex;

    if (Hit.ShapeType == SHAPE_TYPE_MESH_INSTANCE)
    {
        packed_mesh_face Face = MeshFaces[Hit.PrimitiveIndex];

        packed_mesh_vertex Vertex0 = MeshVertices[Face.VertexIndex0];
        packed_mesh_vertex Vertex1 = MeshVertices[Face.VertexIndex1];
        packed_mesh_vertex Vertex2 = MeshVertices[Face.VertexIndex2];

        vec3 Normal = SafeNormalize
        (
            UnpackUnitVector(Vertex0.PackedNormal) * Hit.PrimitiveCoordinates.x +
            UnpackUnitVector(Vertex1.PackedNormal) * Hit.PrimitiveCoordinates.y +
            UnpackUnitVector(Vertex2.PackedNormal) * Hit.PrimitiveCoordinates.z
        );

        Hit.Normal = TransformNormal(Normal, Shape.Transform);
        Hit.TangentX = ComputeTangentVector(Hit.Normal);
        Hit.UV = unpackHalf2x16(Vertex0.PackedUV) * Hit.PrimitiveCoordinates.x
               + unpackHalf2x16(Vertex1.PackedUV) * Hit.PrimitiveCoordinates.y
               + unpackHalf2x16(Vertex2.PackedUV) * Hit.PrimitiveCoordinates.z;
    }
    else if (Hit.ShapeType == SHAPE_TYPE_PLANE)
    {
        Hit.Normal = TransformNormal(vec3(0, 0, 1), Shape.Transform);
        Hit.TangentX = TransformDirection(vec3(1, 0, 0), Shape.Transform);
        Hit.UV = fract(Hit.PrimitiveCoordinates.xy);
    }
    else if (Hit.ShapeType == SHAPE_TYPE_SPHERE)
    {
        vec3 P = Hit.PrimitiveCoordinates;
        float U = (atan(P.y, P.x) + PI) / TAU;
        float V = (P.z + 1.0) / 2.0;
        
        Hit.Normal = TransformNormal(P, Shape.Transform);
        Hit.TangentX = TransformDirection(cross(P, vec3(-P.y, P.x, 0)), Shape.Transform);
        Hit.UV = vec2(U, V);
    }
    else if (Hit.ShapeType == SHAPE_TYPE_CUBE)
    {
        vec3 P = Hit.PrimitiveCoordinates;
        vec3 Q = abs(P);

        vec3 Normal;
        vec3 TangentX;

        if (Q.x >= Q.y && Q.x >= Q.z)
        {
            float S = sign(P.x);
            Normal = vec3(S, 0, 0);
            TangentX = vec3(0, S, 0);
            Hit.UV = 0.5 * (1.0 + P.yz);
        }
        else if (Q.y >= Q.x && Q.y >= Q.z)
        {
            float S = sign(P.y);
            Normal = vec3(0, S, 0);
            TangentX = vec3(0, 0, S);
            Hit.UV = 0.5 * (1.0 + P.xz);
        }
        else
        {
            float S = sign(P.z);
            Normal = vec3(0, 0, S);
            TangentX = vec3(S, 0, 0);
            Hit.UV = 0.5 * (1.0 + P.xy);
        }

        Hit.Normal = TransformNormal(Normal, Shape.Transform);
        Hit.TangentX = TransformDirection(TangentX, Shape.Transform);
    }

    return Hit;
}

ray GenerateCameraRay(packed_camera Camera, vec2 NormalizedSamplePosition)
{
    ray Ray;

    Ray.Duration = HIT_TIME_LIMIT;

    if (Camera.Model == CAMERA_MODEL_PINHOLE)
    {
        vec3 SensorPosition = vec3
        (
            -Camera.SensorSize.x * (NormalizedSamplePosition.x - 0.5),
            -Camera.SensorSize.y * (0.5 - NormalizedSamplePosition.y),
            Camera.SensorDistance
        );

        Ray.Origin = vec3(Camera.ApertureRadius * RandomPointOnDisk(), 0);
        Ray.Velocity = normalize(Ray.Origin - SensorPosition);
    }
    else if (Camera.Model == CAMERA_MODEL_THIN_LENS)
    {
        vec3 SensorPosition = vec3
        (
            -Camera.SensorSize.x * (NormalizedSamplePosition.x - 0.5),
            -Camera.SensorSize.y * (0.5 - NormalizedSamplePosition.y),
            Camera.SensorDistance
        );

        vec3 ObjectPosition = -SensorPosition * Camera.FocalLength / (SensorPosition.z - Camera.FocalLength);

        Ray.Origin = vec3(Camera.ApertureRadius * RandomPointOnDisk(), 0);
        Ray.Velocity = normalize(ObjectPosition - Ray.Origin);
    }
    else if (Camera.Model == CAMERA_MODEL_360)
    {
        float Phi = (NormalizedSamplePosition.x - 0.5f) * TAU;
        float Theta = (0.5f - NormalizedSamplePosition.y) * PI;

        Ray.Origin = vec3(0, 0, 0);
        Ray.Velocity = vec3(cos(Theta) * sin(Phi), sin(Theta), -cos(Theta) * cos(Phi));
    }

    return TransformRay(Ray, Camera.Transform);
}

/* --- Materials ----------------------------------------------------------- */

struct bsdf_parameters
{
    uint MaterialIndex;
    vec2 TextureUV;
    vec4 Lambda;
    vec4 ExteriorIOR;
};

float MaterialTexturableValue(bsdf_parameters Parameters, uint AttributeIndex)
{
    return MaterialTexturableValue(Parameters.MaterialIndex, AttributeIndex, Parameters.TextureUV);
}

vec3 MaterialTexturableColor(bsdf_parameters Parameters, uint AttributeIndex)
{
    return MaterialTexturableColor(Parameters.MaterialIndex, AttributeIndex, Parameters.TextureUV);
}

vec4 MaterialTexturableReflectance(bsdf_parameters Parameters, uint AttributeIndex)
{
    return MaterialTexturableReflectance(Parameters.MaterialIndex, AttributeIndex, Parameters.Lambda, Parameters.TextureUV);
}

#include "scene/basic_diffuse.glsl.inc"
#include "scene/basic_metal.glsl.inc"
#include "scene/basic_translucent.glsl.inc"
//#include "scene/openpbr.glsl.inc"

#define DISPATCH_MATERIAL(NAME, ARGUMENTS) \
    if (Type == MATERIAL_TYPE_BASIC_DIFFUSE) \
        return BasicDiffuse_##NAME ARGUMENTS; \
    if (Type == MATERIAL_TYPE_BASIC_METAL) \
        return BasicMetal_##NAME ARGUMENTS; \
    if (Type == MATERIAL_TYPE_BASIC_TRANSLUCENT) \
        return BasicTranslucent_##NAME ARGUMENTS; \

vec3 MaterialBaseColor(uint MaterialIndex, vec2 TextureUV)
{
    uint Type = MaterialType(MaterialIndex);
    DISPATCH_MATERIAL(BaseColor, (MaterialIndex, TextureUV));
    return vec3(0,0,0);
}

medium MaterialMedium(uint MaterialIndex, vec4 Lambda)
{
    medium Medium;
    uint Type = MaterialType(MaterialIndex);
    if (Type == MATERIAL_TYPE_BASIC_TRANSLUCENT)
        Medium = BasicTranslucent_Medium(MaterialIndex, Lambda);
    return Medium;
}

bool MaterialHasDiracBSDF(bsdf_parameters Parameters)
{
    uint Type = MaterialType(Parameters.MaterialIndex);
    DISPATCH_MATERIAL(HasDiracBSDF, (Parameters));
    return false;
}

bool MaterialSampleBSDF(bsdf_parameters Parameters, vec3 Out, out vec3 In, out vec4 Throughput, out vec4 InPDF)
{
    uint Type = MaterialType(Parameters.MaterialIndex);
    DISPATCH_MATERIAL(SampleBSDF, (Parameters, Out, In, Throughput, InPDF));
    return false;
}

bool MaterialEvaluateBSDF(bsdf_parameters Parameters, vec3 Out, vec3 In, out vec4 Throughput, out vec4 InPDF)
{
    uint Type = MaterialType(Parameters.MaterialIndex);
    DISPATCH_MATERIAL(EvaluateBSDF, (Parameters, Out, In, Throughput, InPDF));
    return false;
}

#undef DISPATCH_MATERIAL

#endif // SCENE_GLSL_INC
