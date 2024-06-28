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
    vec3 MinT = (Min - Ray.Origin) / Ray.Vector;
    vec3 MaxT = (Max - Ray.Origin) / Ray.Vector;

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

// Compute ray intersection against a single mesh face (triangle).
void IntersectMeshFace(ray Ray, uint MeshFaceIndex, inout hit Hit)
{
    packed_mesh_face Face = MeshFaces[MeshFaceIndex];

    float R = dot(Face.Plane.xyz, Ray.Vector);
    if (R > -EPSILON && R < +EPSILON) return;

    float T = -(dot(Face.Plane.xyz, Ray.Origin) + Face.Plane.w) / R;
    if (T < 0 || T > Hit.Time) return;

    vec3 V = Ray.Origin + Ray.Vector * T - Face.Position;
    float Beta = dot(Face.Base1, V);
    if (Beta < 0 || Beta > 1) return;
    float Gamma = dot(Face.Base2, V);
    if (Gamma < 0 || Beta + Gamma > 1) return;

    Hit.Time = T;
    Hit.ShapeType = SHAPE_TYPE_MESH_INSTANCE;
    Hit.ShapeIndex = SHAPE_INDEX_NONE;
    Hit.PrimitiveIndex = MeshFaceIndex;
    Hit.PrimitiveCoordinates = vec3(1 - Beta - Gamma, Beta, Gamma);
}

// Compute ray intersection against a mesh BVH node.
void IntersectMeshNode(ray Ray, uint MeshNodeIndex, inout hit Hit)
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
                IntersectMeshFace(Ray, FaceIndex, Hit);
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
void IntersectShape(ray Ray, uint ShapeIndex, inout hit Hit)
{
    packed_shape Shape = Shapes[ShapeIndex];

    Ray = InverseTransformRay(Ray, Shape.Transform);

    if (Shape.Type == SHAPE_TYPE_MESH_INSTANCE) {
        IntersectMeshNode(Ray, Shape.MeshRootNodeIndex, Hit);
        if (Hit.ShapeIndex == SHAPE_INDEX_NONE) {
            Hit.ShapeIndex = ShapeIndex;
            Hit.ShapePriority = Shape.Priority;
        }
    }
    else if (Shape.Type == SHAPE_TYPE_PLANE) {
        float T = -Ray.Origin.z / Ray.Vector.z;
        if (T < 0 || T > Hit.Time) return;

        Hit.Time = T;
        Hit.ShapeType = SHAPE_TYPE_PLANE;
        Hit.ShapeIndex = ShapeIndex;
        Hit.ShapePriority = Shape.Priority;
        Hit.PrimitiveIndex = 0;
        Hit.PrimitiveCoordinates = Ray.Origin + Ray.Vector * T;
    }
    else if (Shape.Type == SHAPE_TYPE_SPHERE) {
        float V = dot(Ray.Vector, Ray.Vector);
        float P = dot(Ray.Origin, Ray.Vector);
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
        Hit.ShapePriority = Shape.Priority;
        Hit.PrimitiveIndex = 0;
        Hit.PrimitiveCoordinates = Ray.Origin + Ray.Vector * Hit.Time;
    }
    else if (Shape.Type == SHAPE_TYPE_CUBE) {
        vec3 Minimum = (vec3(-1,-1,-1) - Ray.Origin) / Ray.Vector;
        vec3 Maximum = (vec3(+1,+1,+1) - Ray.Origin) / Ray.Vector;
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
        Hit.ShapePriority = Shape.Priority;
        Hit.PrimitiveIndex = 0;
        Hit.PrimitiveCoordinates = Ray.Origin + Ray.Vector * T;
    }
}

// Compute ray intersection against the whole scene.
void Intersect(ray Ray, inout hit Hit)
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
            IntersectShape(Ray, NodeA.ShapeIndex, Hit);
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

// Trace a ray into the scene and return information about the hit.
hit Trace(ray Ray)
{
    hit Hit;
    Hit.Time = Ray.Duration;
    Hit.MeshComplexity = 0;
    Hit.SceneComplexity = 0;

    Intersect(Ray, Hit);

    // If we didn't hit any shape, exit.
    if (Hit.Time == Ray.Duration)
        return Hit;

    packed_shape Shape = Shapes[Hit.ShapeIndex];

    // Ray in shape-local space.
    ray ShapeRay = InverseTransformRay(Ray, Shape.Transform);

    // Hit position and tangent space basis in shape-local space.
    vec3 Position = ShapeRay.Origin + ShapeRay.Vector * Hit.Time;
    vec3 Normal = vec3(0, 0, 1);
    vec3 TangentX = vec3(1, 0, 0);
    vec3 TangentY = vec3(0, 1, 0);

    if (Hit.ShapeType == SHAPE_TYPE_MESH_INSTANCE) {
        packed_mesh_face Face = MeshFaces[Hit.PrimitiveIndex];
        packed_mesh_face_extra Extra = MeshFaceExtras[Hit.PrimitiveIndex];

        Normal = SafeNormalize(
            Extra.Normals[0] * Hit.PrimitiveCoordinates.x +
            Extra.Normals[1] * Hit.PrimitiveCoordinates.y +
            Extra.Normals[2] * Hit.PrimitiveCoordinates.z);

        ComputeCoordinateFrame(Normal, TangentX, TangentY);

        Hit.UV = Extra.UVs[0] * Hit.PrimitiveCoordinates.x
               + Extra.UVs[1] * Hit.PrimitiveCoordinates.y
               + Extra.UVs[2] * Hit.PrimitiveCoordinates.z;

        Hit.MaterialIndex = Extra.MaterialIndex;
    }
    else if (Hit.ShapeType == SHAPE_TYPE_PLANE) {
        packed_shape Shape = Shapes[Hit.ShapeIndex];

        Hit.MaterialIndex = Shape.MaterialIndex;
        Hit.UV = fract(Hit.PrimitiveCoordinates.xy);
    }
    else if (Hit.ShapeType == SHAPE_TYPE_SPHERE) {
        packed_shape Shape = Shapes[Hit.ShapeIndex];

        Hit.MaterialIndex = Shape.MaterialIndex;

        vec3 P = Hit.PrimitiveCoordinates;
        float U = (atan(P.y, P.x) + PI) / TAU;
        float V = (P.z + 1.0) / 2.0;
        Hit.UV = vec2(U, V);

        Normal = Position;
        TangentX = normalize(cross(Normal, vec3(-Normal.y, Normal.x, 0)));
        TangentY = cross(Normal, TangentX);
    }
    else if (Hit.ShapeType == SHAPE_TYPE_CUBE) {
        packed_shape Shape = Shapes[Hit.ShapeIndex];

        Hit.MaterialIndex = Shape.MaterialIndex;

        vec3 P = Hit.PrimitiveCoordinates;
        vec3 Q = abs(P);

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

        TangentY = cross(Normal, TangentX);
    }

    // Transform the hit position and tangent space basis to world space.
    Hit.Position = TransformPosition(Position, Shape.Transform);
    Hit.Normal = TransformNormal(Normal, Shape.Transform);
    Hit.TangentX = TransformDirection(TangentX, Shape.Transform);
    Hit.TangentY = TransformDirection(TangentY, Shape.Transform);

    return Hit;
}


void main()
{
    // Initialize random number generator.
    RandomState
        = gl_GlobalInvocationID.y * 65537
        + gl_GlobalInvocationID.x
        + RandomSeed * 277803737u;

    uint Index = gl_GlobalInvocationID.x;

    //if (Index >= Rays.Count) return;
    if (Index >= 2048*1024) return;

    ray Ray = LoadTraceRay(Index);
    hit Hit = Trace(Ray);
    StoreTraceHit(Index, Hit);
}
