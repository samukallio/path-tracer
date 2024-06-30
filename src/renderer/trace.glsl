#version 450

#define DECLARE_FRAME_UBO_BINDING
#define DECLARE_COMPUTE_BINDINGS
#define DECLARE_SCENE_BINDINGS
#include "common.glsl.inc"

layout(local_size_x=256, local_size_y=1, local_size_z=1) in;

float IntersectBoundingBox(ray Ray, float Reach, vec3 Min, vec3 Max)
{
    // Compute ray time to the axis-aligned planes at the node bounding
    // box minimum and maximum corners.
    vec3 MinT = (Min - Ray.Origin) / Ray.Velocity;
    vec3 MaxT = (Max - Ray.Origin) / Ray.Velocity;

    // For each coordinate axis, sort out which of the two coordinate
    // planes (at bounding box min/max points) comes earlier in time and
    // which one comes later.
    vec3 EarlierT = min(MinT, MaxT);
    vec3 LaterT = max(MinT, MaxT);

    // Compute the ray entry and exit times.  The ray enters the box when
    // it has crossed all of the entry planes, so we take the maximum.
    // Likewise, the ray has exit the box when it has exit at least one
    // of the exit planes, so we take the minimum.
    float EntryT = max(max(EarlierT.x, EarlierT.y), EarlierT.z);
    float ExitT = min(min(LaterT.x, LaterT.y), LaterT.z);

    // If the exit time is greater than the entry time, then the ray has
    // missed the box altogether.
    if (ExitT < EntryT) return INFINITY;

    // If the exit time is less than 0, then the box is behind the eye.
    if (ExitT <= 0) return INFINITY;

    // If the entry time is greater than previous hit time, then the box
    // is occluded.
    if (EntryT >= Reach) return INFINITY;

    return EntryT;
}


void TraceMeshFace(ray Ray, uint MeshFaceIndex, inout hit Hit)
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

// Compute ray intersection against a mesh BVH node.
void TraceMeshNode(ray Ray, uint MeshNodeIndex, inout hit Hit)
{
    uint Stack[32];
    uint Depth = 0;

    packed_mesh_node Node = MeshNodes[MeshNodeIndex];

    while (true) {
        Hit.MeshComplexity++;

        // Leaf node or internal?
        if (Node.FaceEndIndex > 0) {
            // Leaf node, trace all geometry within.
            for (uint FaceIndex = Node.FaceBeginOrNodeIndex; FaceIndex < Node.FaceEndIndex; FaceIndex++)
                TraceMeshFace(Ray, FaceIndex, Hit);
        }
        else {
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
            if (Time > TimeB) {
                // If the first subnode was also hit, then set it aside for later.
                if (Time < INFINITY) Stack[Depth++] = Index;
                Node = NodeB;
                continue;
            }

            // The first subnode is at least as close as the second one.
            // If the second subnode was hit, then both of them were,
            // and we should set the second one aside for later.
            if (TimeB < INFINITY) {
                Stack[Depth++] = IndexB;
                continue;
            }

            // The first subnode is at least as close as the second one,
            // and the second subnode was not Hit.  If the first one was
            // hit, then process it next.
            if (Time < INFINITY) continue;
        }

        // Just processed a leaf node or an internal node with no intersecting
        // subnodes.  If the stack is also empty, then we are done.
        if (Depth == 0) break;

        // Pull a node from the stack.
        Node = MeshNodes[Stack[--Depth]];
    }
}

// Compute ray intersection against a shape.
void TraceShape(ray Ray, uint ShapeIndex, inout hit Hit)
{
    packed_shape Shape = Shapes[ShapeIndex];

    Ray = InverseTransformRay(Ray, Shape.Transform);

    if (Shape.Type == SHAPE_TYPE_MESH_INSTANCE) {
        TraceMeshNode(Ray, Shape.MeshRootNodeIndex, Hit);
        if (Hit.ShapeIndex == 0xFFFFFFFE)
            Hit.ShapeIndex = ShapeIndex;
    }
    else if (Shape.Type == SHAPE_TYPE_PLANE) {
        float T = -Ray.Origin.z / Ray.Velocity.z;
        if (T < 0 || T > Hit.Time) return;

        Hit.Time = T;
        Hit.ShapeType = SHAPE_TYPE_PLANE;
        Hit.ShapeIndex = ShapeIndex;
        Hit.PrimitiveIndex = 0;
        Hit.PrimitiveCoordinates = Ray.Origin + Ray.Velocity * T;
    }
    else if (Shape.Type == SHAPE_TYPE_SPHERE) {
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
    else if (Shape.Type == SHAPE_TYPE_CUBE) {
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

// Compute ray intersection against the whole scene.
void Trace(ray Ray, inout hit Hit)
{
    if (Scene.ShapeCount == 0) return;

    uint Stack[32];
    uint Depth = 0;

    packed_shape_node NodeA = ShapeNodes[0];
    packed_shape_node NodeB;

    while (true) {
        Hit.SceneComplexity++;

        // Leaf node or internal?
        if (NodeA.ChildNodeIndices == 0) {
            // Leaf node, intersect object.
            TraceShape(Ray, NodeA.ShapeIndex, Hit);
        }
        else {
            // Internal node.
            uint IndexA = NodeA.ChildNodeIndices & 0xFFFF;
            uint IndexB = NodeA.ChildNodeIndices >> 16;

            NodeA = ShapeNodes[IndexA];
            NodeB = ShapeNodes[IndexB];

            float TimeA = IntersectBoundingBox(Ray, Hit.Time, NodeA.Minimum, NodeA.Maximum);
            float TimeB = IntersectBoundingBox(Ray, Hit.Time, NodeB.Minimum, NodeB.Maximum);

            if (TimeA > TimeB) {
                if (TimeA < INFINITY) Stack[Depth++] = IndexA;
                NodeA = NodeB;
                continue;
            }

            if (TimeB < INFINITY) {
                Stack[Depth++] = IndexB;
                continue;
            }

            if (TimeA < INFINITY) continue;
        }

        if (Depth == 0) break;

        NodeA = ShapeNodes[Stack[--Depth]];
    }
}

void main()
{
    uint Index = gl_GlobalInvocationID.x;

    if (Index >= 2048*1024) return;

    // Initialize random number generator.
    RandomState
        = gl_GlobalInvocationID.y * 65537
        + gl_GlobalInvocationID.x
        + RandomSeed * 277803737u;

    ray Ray = LoadTraceRay(Index);

    hit Hit;
    Hit.ShapeIndex = SHAPE_INDEX_NONE;
    Hit.Time = Ray.Duration;
    Hit.MeshComplexity = 0;
    Hit.SceneComplexity = 0;

    Trace(Ray, Hit);

    if (Hit.ShapeIndex != SHAPE_INDEX_NONE) {
        packed_shape Shape = Shapes[Hit.ShapeIndex];

        Hit.MaterialIndex = Shape.MaterialIndex;

        if (Hit.ShapeType == SHAPE_TYPE_MESH_INSTANCE) {
            packed_mesh_face Face = MeshFaces[Hit.PrimitiveIndex];
            packed_mesh_face_extra Extra = MeshFaceExtras[Hit.PrimitiveIndex];

            vec3 Normal = SafeNormalize(
                UnpackUnitVector(Extra.PackedNormals[0]) * Hit.PrimitiveCoordinates.x +
                UnpackUnitVector(Extra.PackedNormals[1]) * Hit.PrimitiveCoordinates.y +
                UnpackUnitVector(Extra.PackedNormals[2]) * Hit.PrimitiveCoordinates.z);

            Hit.Normal = TransformNormal(Normal, Shape.Transform);
            Hit.TangentX = ComputeTangentVector(Hit.Normal);
            Hit.UV = unpackHalf2x16(Extra.PackedUVs[0]) * Hit.PrimitiveCoordinates.x
                   + unpackHalf2x16(Extra.PackedUVs[1]) * Hit.PrimitiveCoordinates.y
                   + unpackHalf2x16(Extra.PackedUVs[2]) * Hit.PrimitiveCoordinates.z;
        }
        else if (Hit.ShapeType == SHAPE_TYPE_PLANE) {
            Hit.Normal = TransformNormal(vec3(0, 0, 1), Shape.Transform);
            Hit.TangentX = TransformDirection(vec3(1, 0, 0), Shape.Transform);
            Hit.UV = fract(Hit.PrimitiveCoordinates.xy);
        }
        else if (Hit.ShapeType == SHAPE_TYPE_SPHERE) {
            vec3 P = Hit.PrimitiveCoordinates;
            float U = (atan(P.y, P.x) + PI) / TAU;
            float V = (P.z + 1.0) / 2.0;
        
            Hit.Normal = TransformNormal(P, Shape.Transform);
            Hit.TangentX = TransformDirection(cross(P, vec3(-P.y, P.x, 0)), Shape.Transform);
            Hit.UV = vec2(U, V);
        }
        else if (Hit.ShapeType == SHAPE_TYPE_CUBE) {
            vec3 P = Hit.PrimitiveCoordinates;
            vec3 Q = abs(P);

            vec3 Normal;
            vec3 TangentX;

            if (Q.x >= Q.y && Q.x >= Q.z) {
                float S = sign(P.x);
                Normal = vec3(S, 0, 0);
                TangentX = vec3(0, S, 0);
                Hit.UV = 0.5 * (1.0 + P.yz);
            }
            else if (Q.y >= Q.x && Q.y >= Q.z) {
                float S = sign(P.y);
                Normal = vec3(0, S, 0);
                TangentX = vec3(0, 0, S);
                Hit.UV = 0.5 * (1.0 + P.xz);
            }
            else {
                float S = sign(P.z);
                Normal = vec3(0, 0, S);
                TangentX = vec3(S, 0, 0);
                Hit.UV = 0.5 * (1.0 + P.xy);
            }

            Hit.Normal = TransformNormal(Normal, Shape.Transform);
            Hit.TangentX = TransformDirection(TangentX, Shape.Transform);
        }
    }

    StoreTraceHit(Index, Hit);
}
