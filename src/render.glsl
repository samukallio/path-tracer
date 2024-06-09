#version 450

#include "common.glsl.inc"

const vec3 COLORS[20] = vec3[20](
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

layout(binding = 1, rgba32f)
uniform readonly image2D InputImage;

layout(binding = 2, rgba32f)
uniform writeonly image2D OutputImage;

layout(binding = 3)
uniform sampler2D SkyboxImage;

layout(binding = 4)
uniform sampler2DArray TextureArrayNearest;

layout(binding = 5)
uniform sampler2DArray TextureArrayLinear;

layout(binding = 6)
readonly buffer TextureBuffer
{
    packed_texture Textures[];
};

layout(binding = 7, std430)
readonly buffer MaterialBuffer
{
    packed_material Materials[];
};

layout(binding = 8, std430)
readonly buffer ObjectBuffer
{
    packed_shape Shapes[];
};

layout(binding = 9, std430)
readonly buffer ShapeNodeBuffer
{
    packed_shape_node ShapeNodes[];
};

layout(binding = 10, std430)
readonly buffer MeshFaceBuffer
{
    packed_mesh_face MeshFaces[];
};

layout(binding = 11, std430)
readonly buffer MeshFaceExtraBuffer
{
    packed_mesh_face_extra MeshFaceExtras[];
};

layout(binding = 12, std430)
readonly buffer MeshNodeBuffer
{
    packed_mesh_node MeshNodes[];
};

layout(
    local_size_x = 16,
    local_size_y = 16,
    local_size_z = 1)
    in;


// Generate a random direction from the skybox directional distribution such
// that the generated direction lies in the hemisphere corresponding to the
// given normal.  The strategy is to sample a random direction first, then
// flip the direction if it lies in the wrong hemisphere.  A more "correct"
// approach would be to do rejection sampling to sample from the probability
// distribution conditioned on the direction lying in the correct hemisphere,
// but that is less efficient and makes determining the resulting probability
// density difficult.
vec3 RandomHemisphereSkyboxDirection(vec3 Normal)
{
    vec3 Direction = SkyboxDistributionFrame * RandomVonMisesFisher(SkyboxDistributionConcentration);
    return dot(Direction, Normal) < 0 ? -Direction : Direction;
}

// Return the probability density of a direction generated by the
// RandomHemisphereSkyboxDirection function.
float HemisphereSkyboxDirectionPDF(vec3 Direction)
{
    float Kappa = SkyboxDistributionConcentration;
    vec3 Mu = SkyboxDistributionFrame[2];

    // The probability density is the sum of the von Mises-Fisher densities
    // of the given direction and its opposite, since both map to the same
    // output direction as per the procedure outlined above.
    float C = Kappa / (2 * TAU * sinh(Kappa));
    float Z = Kappa * dot(Mu, Direction);
    return C * (exp(Z) + exp(-Z));
}

vec3 SampleSkybox(ray Ray)
{
//    mat3 frame = skyboxDistributionFrame;
//    float x = (1 + dot(Ray.Vector, frame[0])) / 2.0;
//    float y = (1 + dot(Ray.Vector, frame[1])) / 2.0;
//    float z = (1 + dot(Ray.Vector, frame[2])) / 2.0;
//    return vec3(x, y, z);

    if (SkyboxWhiteFurnace != 0)
        return vec3(1, 1, 1);

    float Phi = atan(Ray.Vector.y, Ray.Vector.x);
    float Theta = asin(Ray.Vector.z);

    float U = 0.5 + Phi / TAU;
    float V = 0.5 - Theta / PI;

    vec3 Color = textureLod(SkyboxImage, vec2(U, V), 0).rgb;

    return Color * SkyboxBrightness;
}

float SampleSpectralSkybox(ray Ray, float Lambda)
{
    float Phi = atan(Ray.Vector.y, Ray.Vector.x);
    float Theta = asin(Ray.Vector.z);

    float U = 0.5 + Phi / TAU;
    float V = 0.5 - Theta / PI;

    vec4 Value = textureLod(SkyboxImage, vec2(U, V), 0);

    return SampleParametricSpectrum(Value.xyz, Lambda) * Value.a;
}

vec4 SampleTexture(uint Index, vec2 UV)
{
    packed_texture Texture = Textures[Index];

    float U = mix(
        Texture.AtlasPlacementMinimum.x,
        Texture.AtlasPlacementMaximum.x,
        fract(UV.x));

    float V = mix(
        Texture.AtlasPlacementMinimum.y,
        Texture.AtlasPlacementMaximum.y,
        fract(UV.y));

    vec3 UVW = vec3(U, V, Texture.AtlasImageIndex);

    if ((Texture.Flags & TEXTURE_FLAG_FILTER_NEAREST) != 0)
        return textureLod(TextureArrayNearest, UVW, 0);
    else
        return textureLod(TextureArrayLinear, UVW, 0);
}

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

void IntersectShape(ray Ray, uint ShapeIndex, inout hit Hit)
{
    packed_shape Shape = Shapes[ShapeIndex];

    Ray = InverseTransformRay(Ray, Shape.Transform);

    if (Shape.Type == SHAPE_TYPE_MESH_INSTANCE) {
        IntersectMeshNode(Ray, Shape.MeshRootNodeIndex, Hit);
        if (Hit.ShapeIndex == SHAPE_INDEX_NONE)
            Hit.ShapeIndex = ShapeIndex;
    }
    else if (Shape.Type == SHAPE_TYPE_PLANE) {
        float T = -Ray.Origin.z / Ray.Vector.z;
        if (T < 0 || T > Hit.Time) return;

        Hit.Time = T;
        Hit.ShapeType = SHAPE_TYPE_PLANE;
        Hit.ShapeIndex = ShapeIndex;
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
        Hit.PrimitiveIndex = 0;
        Hit.PrimitiveCoordinates = Ray.Origin + Ray.Vector * T;
    }
}

void Intersect(ray Ray, inout hit Hit)
{
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

void ResolveHitSurfaceData(ray Ray, inout hit Hit)
{
    packed_shape Shape = Shapes[Hit.ShapeIndex];
    ray ShapeRay = InverseTransformRay(Ray, Shape.Transform);

    vec3 Position = ShapeRay.Origin + ShapeRay.Vector * Hit.Time;
    vec3 Normal = vec3(0, 0, 1);
    vec3 TangentX = vec3(1, 0, 0);
    vec3 TangentY = vec3(0, 1, 0);

    if (Hit.ShapeType == SHAPE_TYPE_MESH_INSTANCE) {
        packed_mesh_face Face = MeshFaces[Hit.PrimitiveIndex];
        packed_mesh_face_extra Extra = MeshFaceExtras[Hit.PrimitiveIndex];

        Normal = Extra.Normals[0] * Hit.PrimitiveCoordinates.x
               + Extra.Normals[1] * Hit.PrimitiveCoordinates.y
               + Extra.Normals[2] * Hit.PrimitiveCoordinates.z;

        ComputeTangentVectors(Normal, TangentX, TangentY);

        Hit.UV = Extra.UVs[0] * Hit.PrimitiveCoordinates.x
               + Extra.UVs[1] * Hit.PrimitiveCoordinates.y
               + Extra.UVs[2] * Hit.PrimitiveCoordinates.z;

        Hit.MaterialIndex = Extra.MaterialIndex;
        Hit.Material = Materials[Extra.MaterialIndex];
    }
    else if (Hit.ShapeType == SHAPE_TYPE_PLANE) {
        packed_shape Shape = Shapes[Hit.ShapeIndex];

        Hit.PrimitiveIndex = 0;
        Hit.MaterialIndex = Shape.MaterialIndex;
        Hit.Material = Materials[Shape.MaterialIndex];
        Hit.UV = fract(Hit.PrimitiveCoordinates.xy);
    }
    else if (Hit.ShapeType == SHAPE_TYPE_SPHERE) {
        packed_shape Shape = Shapes[Hit.ShapeIndex];

        Hit.PrimitiveIndex = 0;
        Hit.MaterialIndex = Shape.MaterialIndex;
        Hit.Material = Materials[Shape.MaterialIndex];

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
        Hit.Material = Materials[Shape.MaterialIndex];
        Hit.PrimitiveIndex = 0;

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

    if (Hit.Material.BaseColorTextureIndex != TEXTURE_INDEX_NONE) {
        vec4 Value = SampleTexture(Hit.Material.BaseColorTextureIndex, Hit.UV);
        Hit.Material.BaseColor = Value.rgb;
        Hit.Material.Opacity *= Value.a;
    }

    if (Hit.Material.SpecularRoughnessTextureIndex != TEXTURE_INDEX_NONE) {
        vec4 Value = SampleTexture(Hit.Material.SpecularRoughnessTextureIndex, Hit.UV);
        Hit.Material.SpecularRoughness *= Value.r;
    }

    Hit.ShapePriority = Shape.Priority;

    Hit.Position = TransformPosition(Position, Shape.Transform);
    Hit.Normal = TransformNormal(Normal, Shape.Transform);
    Hit.TangentX = TransformDirection(TangentX, Shape.Transform);
    Hit.TangentY = TransformDirection(TangentY, Shape.Transform);
}

vec4 TracePath(ray Ray)
{
    float Lambda = mix(CIE_LAMBDA_MIN, CIE_LAMBDA_MAX, Random0To1());

    float Output = 0.0;
    float Filter = 1.0; //* (CIE_LAMBDA_MAX - CIE_LAMBDA_MIN);

    const int MAX_MEDIUM_COUNT = 8;

    medium Mediums[MAX_MEDIUM_COUNT];
    for (int I = 0; I < MAX_MEDIUM_COUNT; I++)
        Mediums[I].ShapeIndex = SHAPE_INDEX_NONE;

    medium AmbientMedium;
    AmbientMedium.ShapeIndex = SHAPE_INDEX_NONE;
    AmbientMedium.ShapePriority = 0;
    AmbientMedium.IOR = 1.0f;
    AmbientMedium.ScatteringRate = SceneScatterRate;

    medium CurrentMedium = AmbientMedium;

    for (uint Bounce = 0; Bounce <= RenderBounceLimit; Bounce++) {
        float ScatteringTime = INFINITY;

        if (CurrentMedium.ScatteringRate > 0) {
            ScatteringTime = -log(Random0To1()) / CurrentMedium.ScatteringRate;
        }

        hit Hit;
        Hit.Time = ScatteringTime;

        Intersect(Ray, Hit);

        if (Hit.Time == ScatteringTime) {
            if (ScatteringTime < INFINITY) {
                Ray.Origin = Ray.Origin + Ray.Vector * ScatteringTime;
                Ray.Vector = RandomDirection();
                continue;
            }
            else {
                Output += Filter * SampleSpectralSkybox(Ray, Lambda);
                break;
            }
        }

        // We hit a surface.  Resolve the surface details.
        ResolveHitSurfaceData(Ray, Hit);
        packed_material Material = Hit.Material;

        // Incoming ray direction in normal/tangent space.
        vec3 Incoming;

        // Outgoing ray direction in normal/tangent space.
        vec3 Outgoing = -vec3(
            dot(Ray.Vector, Hit.TangentX),
            dot(Ray.Vector, Hit.TangentY),
            dot(Ray.Vector, Hit.Normal));

        bool IsFrontFace = Outgoing.z > 0;

        // Determines if the surface we hit is a virtual surface.  A surface
        // is virtual if it belongs to a shape with a lower priority than the
        // highest priority shape that we are currently traversing through.
        // In that case the medium associated with the higher priority shape
        // supersedes the lower priority one, and it is as if an interface
        // does not exist at that point at all.
        bool IsVirtualSurface;

        // Relative index of refraction between the current medium and the
        // medium at the other side of the surface.
        float RelativeIOR = 1.0;

        if (IsFrontFace) {
            // If we are hitting the exterior side of a shape, then the surface
            // interface should affect the ray if the surface belongs to
            // a higher priority shape than we are currently traversing through.
            IsVirtualSurface = CurrentMedium.ShapePriority >= Hit.ShapePriority;

            if (!IsVirtualSurface) {
                float AbbeNumber = Material.TransmissionDispersionAbbeNumber / Material.TransmissionDispersionScale;
                float IOR = CauchyEmpiricalIOR(Material.SpecularIOR, AbbeNumber, Lambda);
                RelativeIOR = CurrentMedium.IOR / IOR;
            }
        }
        else {
            Outgoing.z = -Outgoing.z;

            // If we are hitting the interior side of a shape, then the surface
            // interface should affect the ray only if the surface belongs to
            // the highest-priority shape that we are currently traversing
            // through.
            IsVirtualSurface = CurrentMedium.ShapeIndex != Hit.ShapeIndex;

            // If the surface is real, then we need to determine the relative IOR.
            if (!IsVirtualSurface) {
                // The medium beyond the surface will be that of the highest
                // priority shape other than the one we are currently in.
                medium ExteriorMedium = AmbientMedium;
                for (int I = 0; I < MAX_MEDIUM_COUNT; I++) {
                    if (Mediums[I].ShapeIndex == SHAPE_INDEX_NONE)
                        continue;
                    if (Mediums[I].ShapeIndex == CurrentMedium.ShapeIndex)
                        continue;
                    if (Mediums[I].ShapePriority < ExteriorMedium.ShapePriority)
                        continue;
                    ExteriorMedium = Mediums[I];
                }
                RelativeIOR = CurrentMedium.IOR / ExteriorMedium.IOR;
            }
        }

        vec2 SpecularRoughnessAlpha = ComputeRoughnessAlphaGGX(
            Material.SpecularRoughness,
            Material.SpecularRoughnessAnisotropy);

        // Sample a microsurface normal.
        vec3 SpecularNormal = SampleVisibleNormalGGX(Outgoing, SpecularRoughnessAlpha, Random0To1(), Random0To1());
        // Compute cosine between microsurface normal and outgoing direction.
        float SpecularCosine = dot(SpecularNormal, Outgoing);

        // Pass through the surface if embedded in a higher-priority
        // medium, or probabilistically based on geometric opacity.
        if (Random0To1() > Material.Opacity || IsVirtualSurface) {
            Incoming = -Outgoing;
        }
        // Metal base.
        else if (Random0To1() < Material.BaseMetalness) {
            // Compute incoming direction.  If the incoming ray appears to be
            // coming from within the macrosurface, then it is shadowed, and we
            // terminate.
            Incoming = 2 * SpecularCosine * SpecularNormal - Outgoing;
            if (Incoming.z <= 0) break;

            float Base = SampleParametricSpectrum(Material.BaseColor, Lambda);
            float Specular = SampleParametricSpectrum(Material.SpecularColor, Lambda);

            // Compute Fresnel term.
            float F0 = Material.BaseWeight * Base;
            float F = Material.SpecularWeight * SchlickFresnelMetal(Base, Specular, SpecularCosine);

            // Compute shadowing term.
            float G = SmithG1(Incoming, SpecularRoughnessAlpha);

            Filter *= F * G;
        }
        // Translucent dielectric base.
        else if (Random0To1() < Hit.Material.TransmissionWeight) {
            float RefractedCosineSquared = 1 - RelativeIOR * RelativeIOR * (1 - SpecularCosine * SpecularCosine);

            // Compute reflectance (Fresnel).
            float F;
            if (RefractedCosineSquared < 0) {
                // Total internal reflection.
                F = 1.0;
            }
            else {
                float F0 = Material.SpecularWeight * pow((1 - RelativeIOR) / (1 + RelativeIOR), 2);
                F = SchlickFresnelDielectric(F0, SpecularCosine);
            }

            // Determine incoming direction.
            float IncomingCosine;
            if (Random0To1() < F) {
                // Reflection.
                IncomingCosine = SpecularCosine;
                Incoming = 2 * SpecularCosine * SpecularNormal - Outgoing;

                if (IsFrontFace) {
                    // Per the OpenPBR specification: the specular color material
                    // parameter modulates the Fresnel factor of the dielectric,
                    // but only for reflections from above (and not below).
                    Filter *= SampleParametricSpectrum(Material.SpecularColor, Lambda);
                }
            }
            else {
                // Refraction.
                IncomingCosine = -sqrt(RefractedCosineSquared);
                Incoming = (RelativeIOR * SpecularCosine + IncomingCosine) * SpecularNormal - RelativeIOR * Outgoing;
            }

            // If the incoming direction is in the wrong hemisphere (depending on
            // whether we have a reflection or a refraction), then it is shadowed,
            // and we terminate.
            if (IncomingCosine * Incoming.z <= 0) break;

            float G = SmithG1(Incoming, SpecularRoughnessAlpha);

            Filter *= G;
        }
        // Glossy-diffuse dielectric base.
        else {
            float F0 = Material.SpecularWeight * pow((1 - RelativeIOR) / (1 + RelativeIOR), 2);
            float F = SchlickFresnelDielectric(F0, SpecularCosine);

            if (Random0To1() < F) {
                // Specular reflection.

                // Compute incoming direction.  If the incoming ray appears to be
                // coming from within the macrosurface, then it is shadowed, and we
                // terminate.
                Incoming = 2 * SpecularCosine * SpecularNormal - Outgoing;
                if (Incoming.z <= 0) break;

                if (IsFrontFace) {
                    // Per the OpenPBR specification: the specular color material
                    // parameter modulates the Fresnel factor of the dielectric,
                    // but only for reflections from above (and not below).
                    Filter *= SampleParametricSpectrum(Material.SpecularColor, Lambda);
                }

                float G = SmithG1(Incoming, SpecularRoughnessAlpha);

                Filter *= G;
            }
            else {
                // Diffuse reflection.
                Incoming = normalize(RandomDirection() + vec3(0, 0, 1));

                float S = dot(Incoming, Outgoing) - Incoming.z * Outgoing.z;
                float T = S > 0 ? max(Incoming.z, Outgoing.z) : 1.0;

                float BaseColorSample = SampleParametricSpectrum(Material.BaseColor, Lambda);

                float SigmaSq = Material.BaseDiffuseRoughness * Material.BaseDiffuseRoughness;
                float A = 1 - 0.5 * SigmaSq / (SigmaSq + 0.33) + 0.17 * BaseColorSample * SigmaSq / (SigmaSq + 0.13);
                float B = 0.45 * SigmaSq / (SigmaSq + 0.09);

                Filter *= BaseColorSample * (A + B * S / T);
            }
        }

        // If the incoming and outgoing directions are within opposite hemispheres,
        // then the ray is crossing the material interface boundary.  We need to
        // perform bookkeeping to determine the current Medium.
        if (Incoming.z < 0) {
            if (IsFrontFace) {
                // We are tracing into the object, so add a medium entry
                // associated with the object.
                for (int I = 0; I < MAX_MEDIUM_COUNT; I++) {
                    if (Mediums[I].ShapeIndex == SHAPE_INDEX_NONE) {
                        Mediums[I].ShapeIndex     = Hit.ShapeIndex;
                        Mediums[I].ShapePriority  = Hit.ShapePriority;
                        Mediums[I].IOR            = Material.SpecularIOR;
                        Mediums[I].ScatteringRate = Material.ScatteringRate;
                        break;
                    }
                }
            }
            else {
                // We are tracing out of the object, so remove the medium
                // entry associated with the object.
                for (int I = 0; I < MAX_MEDIUM_COUNT; I++) {
                    if (Mediums[I].ShapeIndex == Hit.ShapeIndex) {
                        Mediums[I].ShapeIndex = SHAPE_INDEX_NONE;
                        break;
                    }
                }
            }

            // Determine the highest-priority medium that we are currently
            // in, and set it as the active one.
            CurrentMedium = AmbientMedium;
            for (int I = 0; I < MAX_MEDIUM_COUNT; I++) {
                if (Mediums[I].ShapeIndex == SHAPE_INDEX_NONE)
                    continue;
                if (Mediums[I].ShapePriority < CurrentMedium.ShapePriority)
                    continue;
                CurrentMedium = Mediums[I];
            }
        }

        if (!IsFrontFace) {
            Incoming.z = -Incoming.z;
        }

        Ray.Vector = Incoming.x * Hit.TangentX
                   + Incoming.y * Hit.TangentY
                   + Incoming.z * Hit.Normal;

        Ray.Origin = Hit.Position + 1e-3 * Ray.Vector;

        if (Random0To1() < RenderTerminationProbability)
            break;

        Filter /= 1.0 - RenderTerminationProbability;
    }

    vec3 OutputColor = StandardObserverSRGB(Lambda) * Output;

    return vec4(OutputColor, 1);
}

hit IntersectAndResolve(ray Ray)
{
    hit Hit;
    Hit.Time = INFINITY;
    Intersect(Ray, Hit);
    if (Hit.Time != INFINITY)
        ResolveHitSurfaceData(Ray, Hit);
    return Hit;
}

vec4 TraceBaseColor(ray Ray, bool IsShaded)
{
    hit Hit = IntersectAndResolve(Ray);

    if (Hit.Time == INFINITY)
        return vec4(SampleSkybox(Ray), 1);

    if (IsShaded) {
        float Shading = dot(Hit.Normal, -Ray.Vector);
        if (Hit.ShapeIndex == SelectedShapeIndex)
            return vec4((Hit.Material.BaseColor + vec3(1,0,0)) * Shading, 1.0);
        else
            return vec4(Hit.Material.BaseColor * Shading, 1.0);
    }
    else {
        return vec4(Hit.Material.BaseColor, 1);
    }
}

vec4 TraceNormal(ray Ray)
{
    hit Hit = IntersectAndResolve(Ray);
    if (Hit.Time == INFINITY)
        return vec4(0.5 * (1 - Ray.Vector), 1);
    return vec4(0.5 * (Hit.Normal + 1), 1);
}

vec4 TraceMaterialIndex(ray Ray)
{
    hit Hit = IntersectAndResolve(Ray);
    if (Hit.Time == INFINITY)
        return vec4(0, 0, 0, 1);
    return vec4(COLORS[Hit.MaterialIndex % 20], 1);
}

vec4 TracePrimitiveIndex(ray Ray)
{
    hit Hit = IntersectAndResolve(Ray);
    if (Hit.Time == INFINITY)
        return vec4(0, 0, 0, 1);
    return vec4(COLORS[Hit.PrimitiveIndex % 20], 1);
}

vec4 TraceMeshComplexity(ray Ray)
{
    hit Hit;
    Hit.Time = INFINITY;
    Hit.MeshComplexity = 0;
    Intersect(Ray, Hit);

    float Alpha = min(Hit.MeshComplexity / float(RenderMeshComplexityScale), 1.0);
    vec3 Color = mix(vec3(0,0,0), vec3(0,1,0), Alpha);
    return vec4(Color, 1);
}

vec4 TraceSceneComplexity(ray Ray)
{
    hit Hit;
    Hit.Time = INFINITY;
    Hit.SceneComplexity = 0;
    Intersect(Ray, Hit);

    float Alpha = min(Hit.SceneComplexity / float(RenderSceneComplexityScale), 1.0);
    vec3 Color = mix(vec3(0,0,0), vec3(0,1,0), Alpha);
    return vec4(Color, 1);
}

void main()
{
    // Initialize random number generator.
    RandomState
        = gl_GlobalInvocationID.y * 65537
        + gl_GlobalInvocationID.x
        + FrameRandomSeed * 277803737u;

    // Compute the position of the sample we are going to produce in image
    // coordinates from (0, 0) to (ImageSizeX, ImageSizeY).
    vec2 SamplePosition = gl_GlobalInvocationID.xy;

    if ((RenderFlags & RENDER_FLAG_SAMPLE_JITTER) != 0)
        SamplePosition += vec2(Random0To1(), Random0To1());
    else
        SamplePosition += vec2(0.5, 0.5);

    SamplePosition *= RenderSampleBlockSize;

    // Get the integer position of the pixel that contains the chosen sample.
    ivec2 SamplePixelPosition = ivec2(floor(SamplePosition));

    // This position can be outside the target image if the image size is not
    // a multiple of the region size (16 * renderSampleBlockSize) handled by
    // one invocation.  If that happens, just exit.
    ivec2 ImageSizeInPixels = imageSize(OutputImage);
    if (SamplePixelPosition.x >= ImageSizeInPixels.x) return;
    if (SamplePixelPosition.y >= ImageSizeInPixels.y) return;

    // Compute normalized sample position from (0, 0) to (1, 1).
    vec2 SampleNormalizedPosition = SamplePosition / ImageSizeInPixels;

    ray Ray;

    if (CameraModel == CAMERA_MODEL_PINHOLE) {
        vec3 SensorPosition = vec3(
            -CameraSensorSize.x * (SampleNormalizedPosition.x - 0.5),
            -CameraSensorSize.y * (0.5 - SampleNormalizedPosition.y),
            CameraSensorDistance);

        Ray.Origin = vec3(CameraApertureRadius * RandomPointOnDisk(), 0);
        Ray.Vector = normalize(Ray.Origin - SensorPosition);
    }

    else if (CameraModel == CAMERA_MODEL_THIN_LENS) {
        vec3 SensorPosition = vec3(
            -CameraSensorSize.x * (SampleNormalizedPosition.x - 0.5),
            -CameraSensorSize.y * (0.5 - SampleNormalizedPosition.y),
            CameraSensorDistance);

        vec3 ObjectPosition = -SensorPosition * CameraFocalLength / (SensorPosition.z - CameraFocalLength);

        Ray.Origin = vec3(CameraApertureRadius * RandomPointOnDisk(), 0);
        Ray.Vector = normalize(ObjectPosition - Ray.Origin);
    }

    else if (CameraModel == CAMERA_MODEL_360) {
        float Phi = (SampleNormalizedPosition.x - 0.5f) * TAU;
        float Theta = (0.5f - SampleNormalizedPosition.y) * PI;

        Ray.Origin = vec3(0, 0, 0);
        Ray.Vector = vec3(cos(Theta) * sin(Phi), sin(Theta), -cos(Theta) * cos(Phi));
    }

    Ray = TransformRay(Ray, CameraTransform);

    vec4 SampleValue;

    if (RenderMode == RENDER_MODE_PATH_TRACE)
        SampleValue = TracePath(Ray);
    else if (RenderMode == RENDER_MODE_BASE_COLOR)
        SampleValue = TraceBaseColor(Ray, false);
    else if (RenderMode == RENDER_MODE_BASE_COLOR_SHADED)
        SampleValue = TraceBaseColor(Ray, true);
    else if (RenderMode == RENDER_MODE_NORMAL)
        SampleValue = TraceNormal(Ray);
    else if (RenderMode == RENDER_MODE_MATERIAL_INDEX)
        SampleValue = TraceMaterialIndex(Ray);
    else if (RenderMode == RENDER_MODE_PRIMITIVE_INDEX)
        SampleValue = TracePrimitiveIndex(Ray);
    else if (RenderMode == RENDER_MODE_MESH_COMPLEXITY)
        SampleValue = TraceMeshComplexity(Ray);
    else if (RenderMode == RENDER_MODE_SCENE_COMPLEXITY)
        SampleValue = TraceSceneComplexity(Ray);

    // Transfer the sample block from the input image to the output image,
    // adding the sample value that we produced at the relevant pixel
    // position.
    ivec2 XY = ivec2(gl_GlobalInvocationID.xy * RenderSampleBlockSize);
    for (int I = 0; I < RenderSampleBlockSize; I++) {
        for (int J = 0; J < RenderSampleBlockSize; J++) {
            ivec2 TransferPixelPosition = XY + ivec2(I,J);
            if (TransferPixelPosition.x >= ImageSizeInPixels.x) break;
            if (TransferPixelPosition.y >= ImageSizeInPixels.y) return;
            vec4 OutputValue = vec4(0,0,0,0);
            if ((RenderFlags & RENDER_FLAG_ACCUMULATE) != 0)
                OutputValue = imageLoad(InputImage, TransferPixelPosition);
            if (TransferPixelPosition == SamplePixelPosition)
                OutputValue += SampleValue;
            imageStore(OutputImage, TransferPixelPosition, OutputValue);
        }
    }
}
