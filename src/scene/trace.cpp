#include "core/common.h"
#include "scene/scene.h"

static void IntersectMeshFace(scene* Scene, ray Ray, uint32_t MeshFaceIndex, hit& Hit)
{
    packed_mesh_face Face = Scene->MeshFacePack[MeshFaceIndex];

    vec3 Edge1 = Face.Position1 - Face.Position0;
    vec3 Edge2 = Face.Position2 - Face.Position0;

    vec3 RayCrossEdge2 = cross(Ray.Vector, Edge2);
    float Det = dot(Edge1, RayCrossEdge2);
    if (abs(Det) < EPSILON) return;

    float InvDet = 1.0f / Det;

    vec3 S = Ray.Origin - Face.Position0;
    float U = InvDet * dot(S, RayCrossEdge2);
    if (U < 0 || U > 1) return;

    vec3 SCrossEdge1 = cross(S, Edge1);
    float V = InvDet * dot(Ray.Vector, SCrossEdge1);
    if (V < 0 || U + V > 1) return;

    float T = InvDet * dot(Edge2, SCrossEdge1);
    if (T < 0 || T > Hit.Time) return;

    Hit.Time = T;
    Hit.ShapeType = SHAPE_TYPE_MESH_INSTANCE;
    Hit.ShapeIndex = SHAPE_INDEX_NONE;
    Hit.PrimitiveIndex = MeshFaceIndex;
    Hit.PrimitiveCoordinates = glm::vec3(1 - U - V, U, V);
}

static float IntersectMeshNodeBounds(ray Ray, float Reach, packed_mesh_node const& Node)
{
    // Compute ray time to the axis-aligned planes at the node bounding
    // box minimum and maximum corners.
    glm::vec3 Minimum = (Node.Minimum - Ray.Origin) / Ray.Vector;
    glm::vec3 Maximum = (Node.Maximum - Ray.Origin) / Ray.Vector;

    // For each coordinate axis, sort out which of the two coordinate
    // planes (at bounding box min/max points) comes earlier in time and
    // which one comes later.
    glm::vec3 Earlier = min(Minimum, Maximum);
    glm::vec3 Later = max(Minimum, Maximum);

    // Compute the ray entry and exit times.  The ray enters the box when
    // it has crossed all of the entry planes, so we take the maximum.
    // Likewise, the ray has exit the box when it has exit at least one
    // of the exit planes, so we take the minimum.
    float Entry = glm::max(glm::max(Earlier.x, Earlier.y), Earlier.z);
    float Exit = glm::min(glm::min(Later.x, Later.y), Later.z);

    // If the exit time is greater than the entry time, then the ray has
    // missed the box altogether.
    if (Exit < Entry) return INFINITY;

    // If the exit time is less than 0, then the box is behind the eye.
    if (Exit <= 0) return INFINITY;

    // If the entry time is greater than previous hit time, then the box
    // is occluded.
    if (Entry >= Reach) return INFINITY;

    return Entry;
}

static void IntersectMesh(scene* Scene, ray const& Ray, uint32_t RootNodeIndex, hit& Hit)
{
    uint32_t Stack[32];
    uint32_t Depth = 0;

    packed_mesh_node Node = Scene->MeshNodePack[RootNodeIndex];

    while (true) {
        // Leaf node or internal?
        if (Node.FaceEndIndex > 0) {
            // Leaf node, trace all geometry within.
            for (uint32_t FaceIndex = Node.FaceBeginOrNodeIndex; FaceIndex < Node.FaceEndIndex; FaceIndex++)
                IntersectMeshFace(Scene, Ray, FaceIndex, Hit);
        }
        else {
            // Internal node.
            // Load the first subnode as the node to be processed next.
            uint32_t Index = Node.FaceBeginOrNodeIndex;
            Node = Scene->MeshNodePack[Index];
            float Time = IntersectMeshNodeBounds(Ray, Hit.Time, Node);

            // Also load the second subnode to see if it is closer.
            uint32_t IndexB = Index + 1;
            packed_mesh_node NodeB = Scene->MeshNodePack[IndexB];
            float TimeB = IntersectMeshNodeBounds(Ray, Hit.Time, NodeB);

            // If the second subnode is strictly closer than the first one,
            // then it was definitely hit, so process it next.
            if (Time > TimeB) {
                // If the first subnode was also hit, then set it aside for later.
                if (Time < INFINITY) {
                    assert(Depth < std::size(Stack));
                    Stack[Depth++] = Index;
                }
                Node = NodeB;
                continue;
            }

            // The first subnode is at least as close as the second one.
            // If the second subnode was hit, then both of them were,
            // and we should set the second one aside for later.
            if (TimeB < INFINITY) {
                assert(Depth < std::size(Stack));
                Stack[Depth++] = IndexB;
                continue;
            }

            // The first subnode is at least as close as the second one,
            // and the second subnode was not hit.  If the first one was
            // hit, then process it next.
            if (Time < INFINITY) continue;
        }

        // Just processed a leaf node or an internal node with no intersecting
        // subnodes.  If the stack is also empty, then we are done.
        if (Depth == 0) break;

        // Pull a node from the stack.
        Node = Scene->MeshNodePack[Stack[--Depth]];
    }
}

static void IntersectShape(scene* Scene, ray const& WorldRay, uint32_t ShapeIndex, hit& Hit)
{
    packed_shape Shape = Scene->ShapePack[ShapeIndex];

    ray Ray = TransformRay(WorldRay, Shape.Transform.From);

    if (Shape.Type == SHAPE_TYPE_MESH_INSTANCE) {
        IntersectMesh(Scene, Ray, Shape.MeshRootNodeIndex, Hit);
        if (Hit.ShapeIndex == SHAPE_INDEX_NONE)
            Hit.ShapeIndex = ShapeIndex;
    }

    if (Shape.Type == SHAPE_TYPE_PLANE) {
        float T = -Ray.Origin.z / Ray.Vector.z;
        if (T < 0 || T > Hit.Time) return;

        Hit.Time = T;
        Hit.ShapeType = SHAPE_TYPE_PLANE;
        Hit.ShapeIndex = ShapeIndex;
        Hit.PrimitiveIndex = 0;
        Hit.PrimitiveCoordinates = glm::vec3(glm::fract(Ray.Origin.xy() + Ray.Vector.xy() * T), 0);
    }

    if (Shape.Type == SHAPE_TYPE_SPHERE) {
        float V = glm::dot(Ray.Vector, Ray.Vector);
        float P = glm::dot(Ray.Origin, Ray.Vector);
        float Q = glm::dot(Ray.Origin, Ray.Origin) - 1.0f;
        float D2 = P * P - Q * V;
        if (D2 < 0) return;

        float D = glm::sqrt(D2);
        if (D < P) return;

        float S0 = -P - D;
        float S1 = -P + D;
        float S = S0 < 0 ? S1 : S0;
        if (S < 0 || S > V * Hit.Time) return;

        Hit.Time = S / V;
        Hit.ShapeType = SHAPE_TYPE_SPHERE;
        Hit.ShapeIndex = ShapeIndex;
        Hit.PrimitiveIndex = 0;
        Hit.PrimitiveCoordinates = Ray.Origin + Ray.Vector * Hit.Time;
    }

    if (Shape.Type == SHAPE_TYPE_CUBE) {
        glm::vec3 Minimum = (glm::vec3(-1,-1,-1) - Ray.Origin) / Ray.Vector;
        glm::vec3 Maximum = (glm::vec3(+1,+1,+1) - Ray.Origin) / Ray.Vector;
        glm::vec3 Earlier = min(Minimum, Maximum);
        glm::vec3 Later = max(Minimum, Maximum);
        float T0 = glm::max(glm::max(Earlier.x, Earlier.y), Earlier.z);
        float T1 = glm::min(glm::min(Later.x, Later.y), Later.z);
        if (T1 < T0) return;
        if (T1 <= 0) return;
        if (T0 >= Hit.Time) return;

        float T = T0 < 0 ? T1 : T0;

        Hit.Time = T;
        Hit.ShapeType = SHAPE_TYPE_CUBE;
        Hit.ShapeIndex = ShapeIndex;
        Hit.PrimitiveIndex = 0;
        Hit.PrimitiveCoordinates = Ray.Origin + Ray.Vector * T;
    }
}

static void Intersect(scene* Scene, ray const& WorldRay, hit& Hit)
{
    for (uint32_t ShapeIndex = 0; ShapeIndex < Scene->ShapePack.size(); ShapeIndex++)
        IntersectShape(Scene, WorldRay, ShapeIndex, Hit);
}

bool Trace(scene* Scene, ray const& Ray, hit& Hit)
{
    Hit.Time = INFINITY;
    Intersect(Scene, Ray, Hit);
    return Hit.Time < INFINITY;
}
