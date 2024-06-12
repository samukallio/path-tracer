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
uniform sampler2DArray TextureArrayNearest;

layout(binding = 4)
uniform sampler2DArray TextureArrayLinear;

layout(binding = 5)
readonly buffer TextureBuffer
{
    packed_texture Textures[];
};

layout(binding = 6, std430)
readonly buffer MaterialBuffer
{
    packed_material Materials[];
};

layout(binding = 7, std430)
readonly buffer ObjectBuffer
{
    packed_shape Shapes[];
};

layout(binding = 8, std430)
readonly buffer ShapeNodeBuffer
{
    packed_shape_node ShapeNodes[];
};

layout(binding = 9, std430)
readonly buffer MeshFaceBuffer
{
    packed_mesh_face MeshFaces[];
};

layout(binding = 10, std430)
readonly buffer MeshFaceExtraBuffer
{
    packed_mesh_face_extra MeshFaceExtras[];
};

layout(binding = 11, std430)
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

vec4 SampleSkyboxSpectrum(ray Ray)
{
//    mat3 frame = skyboxDistributionFrame;
//    float x = (1 + dot(Ray.Vector, frame[0])) / 2.0;
//    float y = (1 + dot(Ray.Vector, frame[1])) / 2.0;
//    float z = (1 + dot(Ray.Vector, frame[2])) / 2.0;
//    return vec3(x, y, z);

    if (SkyboxTextureIndex == TEXTURE_INDEX_NONE)
        return vec4(0, 0, 100, 1);

    float Phi = atan(Ray.Vector.y, Ray.Vector.x);
    float Theta = asin(Ray.Vector.z);

    float U = 0.5 + Phi / TAU;
    float V = 0.5 + Theta / PI;

    return SampleTexture(SkyboxTextureIndex, vec2(U, V));
}

float SampleSkyboxRadiance(ray Ray, float Lambda)
{
    vec4 Spectrum = SampleSkyboxSpectrum(Ray);
    return SampleParametricSpectrum(Spectrum, Lambda) * SkyboxBrightness;
}

/* --- Tracing ------------------------------------------------------------- */

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
hit Trace(ray Ray, float MaxTime)
{
    hit Hit;
    Hit.Time = MaxTime;
    Hit.MeshComplexity = 0;
    Hit.SceneComplexity = 0;

    Intersect(Ray, Hit);

    // If we didn't hit any shape, exit.
    if (Hit.Time == MaxTime)
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

        ComputeTangentVectors(Normal, TangentX, TangentY);

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

/* --- Material ------------------------------------------------------------ */

// Evaluate the surface properties of a hit surface at a given wavelength.
void ResolveSurfaceParameters(hit Hit, float Lambda, float AmbientIOR, out surface_parameters Surface)
{
    packed_material Material = Materials[Hit.MaterialIndex];

    // Geometric opacity.
    Surface.Opacity = Material.Opacity;

    // Surface composition.
    Surface.CoatIsPresent = Random0To1() < Material.CoatWeight;
    Surface.BaseIsMetal = Random0To1() < Material.BaseMetalness;
    Surface.BaseIsTranslucent = !Surface.BaseIsMetal && Random0To1() < Material.TransmissionWeight;

    // Base reflectance and opacity.
    Surface.BaseReflectance = Material.BaseWeight * SampleParametricSpectrum(Material.BaseSpectrum, Lambda);
    Surface.BaseDiffuseRoughness = Material.BaseDiffuseRoughness;

    if (Material.BaseSpectrumTextureIndex != TEXTURE_INDEX_NONE) {
        vec4 Value = SampleTexture(Material.BaseSpectrumTextureIndex, Hit.UV);
        Surface.BaseReflectance *= SampleParametricSpectrum(Value.xyz, Lambda);
        Surface.Opacity *= Value.a;
    }

    // Coat.
    if (Surface.CoatIsPresent) {
        Surface.CoatRelativeIOR = AmbientIOR / Material.CoatIOR;
        Surface.CoatTransmittance = SampleParametricSpectrum(Material.CoatColorSpectrum, Lambda);
        Surface.CoatRoughnessAlpha = ComputeRoughnessAlphaGGX(Material.CoatRoughness, Material.CoatRoughnessAnisotropy);
    }

    // Specular.
    Surface.SpecularWeight = Material.SpecularWeight;
    Surface.SpecularReflectance = SampleParametricSpectrum(Material.SpecularSpectrum, Lambda);

    float AbbeNumber = Material.TransmissionDispersionAbbeNumber / Material.TransmissionDispersionScale;
    float SpecularIOR = CauchyEmpiricalIOR(Material.SpecularIOR, AbbeNumber, Lambda);

    if (Surface.CoatIsPresent)
        Surface.SpecularRelativeIOR = Material.CoatIOR / Material.SpecularIOR;
    else
        Surface.SpecularRelativeIOR = AmbientIOR / Material.SpecularIOR;

    float SpecularRoughness = Material.SpecularRoughness;
    if (Material.SpecularRoughnessTextureIndex != TEXTURE_INDEX_NONE) {
        vec4 Value = SampleTexture(Material.SpecularRoughnessTextureIndex, Hit.UV);
        SpecularRoughness *= Value.r;
    }

    Surface.SpecularRoughnessAlpha = ComputeRoughnessAlphaGGX(
        Material.SpecularRoughness,
        Material.SpecularRoughnessAnisotropy);

    //
    Surface.Emission = SampleParametricSpectrum(Material.EmissionSpectrum, Lambda) * Material.EmissionLuminance;

    // Medium.
    Surface.MediumIOR = Material.SpecularIOR;
    Surface.MediumScatteringRate = Material.ScatteringRate;
}

/* --- BSDF ---------------------------------------------------------------- */

// OpenPBR coat BSDF.
void CoatBSDF(vec3 Out, out vec3 In, inout float Radiance, inout float Weight, surface_parameters Surface)
{
    if (!Surface.CoatIsPresent) {
        In = -Out;
        return;
    }

    // Sample a microsurface normal for coat scattering.
    float NormalU1 = Random0To1();
    float NormalU2 = Random0To1();
    vec3 Normal = SampleVisibleNormalGGX(Out * sign(Out.z), Surface.CoatRoughnessAlpha, NormalU1, NormalU2);
    float Cosine = dot(Normal, Out);

    // Substrate is a dielectric.
    float RelativeIOR = Surface.CoatRelativeIOR;
    if (Out.z < 0) RelativeIOR = 1.0 / RelativeIOR;

    // Compute the cosine of the angle between the refraction direction and
    // the microsurface normal.  The squared cosine is clamped to zero, the
    // boundary for total internal reflection (TIR).  When the cosine is zero,
    // the Fresnel formulas give a reflectivity of 1, producing a TIR without
    // the need for branches.
    float RefractedCosineSquared = 1 - RelativeIOR * RelativeIOR * (1 - Cosine * Cosine);
    float RefractedCosine = -sign(Out.z) * sqrt(max(RefractedCosineSquared, 0.0));

    // Compute dielectric reflectance.
    float Reflectance = FresnelDielectric(RefractedCosine, Cosine, RelativeIOR);

    // Specular reflection?
    if (Random0To1() < Reflectance) {
        // Compute reflected direction.
        In = 2 * Cosine * Normal - Out;

        // If the reflected direction is in the wrong hemisphere,
        // then it is shadowed and we terminate here.
        if (In.z * Out.z <= 0) {
            Weight = 0.0;
            return;
        }

        Weight *= SmithG1(In, Surface.CoatRoughnessAlpha);

        if (Out.z < 0) Weight *= Surface.CoatTransmittance;

        return;
    }

    // Compute refracted direction.
    In = (RelativeIOR * Cosine + RefractedCosine) * Normal - RelativeIOR * Out;

    // If the refracted direction is in the wrong hemisphere,
    // then it is shadowed and we terminate here.
    if (In.z * Out.z > 0) {
        Weight = 0.0;
        return;
    }

    Weight *= SmithG1(In, Surface.CoatRoughnessAlpha);
    Weight *= sqrt(Surface.CoatTransmittance);
}

// OpenPBR base substrate BSDF.
void BaseBSDF(vec3 Out, out vec3 In, inout float Radiance, inout float Weight, surface_parameters Surface)
{
    if (Out.z > 0) Radiance += Surface.Emission;

    // Sample a microsurface normal for specular scattering.
    float NormalU1 = Random0To1();
    float NormalU2 = Random0To1();
    vec3 Normal = SampleVisibleNormalGGX(Out * sign(Out.z), Surface.SpecularRoughnessAlpha, NormalU1, NormalU2);
    float Cosine = dot(Normal, Out);

    // Metallic reflection.
    if (Surface.BaseIsMetal) {
        // Compute reflected direction.
        In = 2 * Cosine * Normal - Out;

        // If the reflected direction is in the wrong hemisphere,
        // then it is shadowed and we terminate here.
        if (Out.z * In.z <= 0) {
            Weight = 0.0;
            return;
        }

        // Compute Fresnel term.
        float F0 = Surface.BaseReflectance;
        float F = Surface.SpecularWeight * SchlickFresnelMetal(F0, Surface.SpecularReflectance, abs(Cosine));

        // Compute shadowing term.
        float G = SmithG1(In, Surface.SpecularRoughnessAlpha);

        Weight *= F * G;
        return;
    }

    // Substrate is a dielectric.
    float RelativeIOR = Surface.SpecularRelativeIOR;
    if (Out.z < 0) RelativeIOR = 1.0 / RelativeIOR;

    // Modulation of the relative IOR by the specular weight parameter.
    if (Surface.SpecularWeight < 1.0) {
        float R = sqrt(Surface.SpecularWeight) * (1.0 - RelativeIOR) / (1.0 + RelativeIOR);
        RelativeIOR = (1.0 - R) / (1.0 + R);
    }

    // Compute the cosine of the angle between the refraction direction and
    // the microsurface normal.  The squared cosine is clamped to zero, the
    // boundary for total internal reflection (TIR).  When the cosine is zero,
    // the Fresnel formulas give a reflectivity of 1, producing a TIR without
    // the need for branches.
    float RefractedCosineSquared = 1 - RelativeIOR * RelativeIOR * (1 - Cosine * Cosine);
    float RefractedCosine = -sign(Out.z) * sqrt(max(RefractedCosineSquared, 0.0));

    // Compute dielectric reflectance.
    float Reflectance = FresnelDielectric(RefractedCosine, Cosine, RelativeIOR);

    // Specular reflection?
    if (Random0To1() < Reflectance) {
        // Compute reflected direction.
        In = 2 * Cosine * Normal - Out;

        // If the reflected direction is in the wrong hemisphere,
        // then it is shadowed and we terminate here.
        if (In.z * Out.z <= 0) {
            Weight = 0.0;
            return;
        }

        // Per the OpenPBR specification: the specular color material
        // parameter modulates the Fresnel factor of the dielectric,
        // but only for reflections from above (and not below).
        if (Out.z > 0) Weight *= Surface.SpecularReflectance;

        Weight *= SmithG1(In, Surface.SpecularRoughnessAlpha);
        return;
    }

    // Translucent substrate.
    if (Surface.BaseIsTranslucent) {
        // Compute refracted direction.
        In = (RelativeIOR * Cosine + RefractedCosine) * Normal - RelativeIOR * Out;

        // If the refracted direction is in the wrong hemisphere,
        // then it is shadowed and we terminate here.
        if (In.z * Out.z > 0) {
            Weight = 0.0;
            return;
        }

        Weight *= SmithG1(In, Surface.SpecularRoughnessAlpha);
        return;
    }

    // Glossy-diffuse substrate.
    if (true) {
        In = SafeNormalize(RandomDirection() + vec3(0, 0, 1));

        float S = dot(In, Out) - In.z * Out.z;
        float T = S > 0 ? max(In.z, Out.z) : 1.0;
        float SigmaSq = Surface.BaseDiffuseRoughness * Surface.BaseDiffuseRoughness;
        float A = 1 - 0.5 * SigmaSq / (SigmaSq + 0.33) + 0.17 * Surface.BaseReflectance * SigmaSq / (SigmaSq + 0.13);
        float B = 0.45 * SigmaSq / (SigmaSq + 0.09);

        Weight *= Surface.BaseReflectance * (A + B * S / T);
    }
}

void BSDF(vec3 Out, out vec3 In, inout float Radiance, inout float Weight, surface_parameters Surface)
{
    const int LAYER_COAT = 0;
    const int LAYER_BASE = 1;

    int Layer;
    
    if (Out.z > 0)
        Layer = LAYER_COAT;
    else
        Layer = LAYER_BASE;

    for (int I = 0; I < 10; I++) {
        if (Layer == LAYER_COAT) {
            CoatBSDF(Out, In, Radiance, Weight, Surface);

            if (In.z > 0) return;

            Out = -In;
            Layer = LAYER_BASE;
        }

        if (Layer == LAYER_BASE) {
            BaseBSDF(Out, In, Radiance, Weight, Surface);

            if (In.z < 0) return;

            Out = -In;
            Layer = LAYER_COAT;
        }
    }
}

/* --- Rendering ----------------------------------------------------------- */

vec4 RenderPath(ray Ray)
{
    // Wavelength of this sample.
    float Lambda = mix(CIE_LAMBDA_MIN, CIE_LAMBDA_MAX, Random0To1());

    // Accumulated radiance along the path.
    float Radiance = 0.0;

    // Cumulative importance at the current path vertex.
    float Weight = 1.0; //* (CIE_LAMBDA_MAX - CIE_LAMBDA_MIN);

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

        // Trace the ray against the scene geometry.
        hit Hit = Trace(Ray, ScatteringTime);

        // If we hit no geometry before the scattering time...
        if (Hit.Time == ScatteringTime) {
            // If the scattering time is finite, then it's a scattering event.
            if (ScatteringTime < INFINITY) {
                Ray.Origin = Ray.Origin + Ray.Vector * ScatteringTime;
                Ray.Vector = RandomDirection();
                continue;
            }
            // Otherwise, we hit the skybox.
            else {
                Radiance += Weight * SampleSkyboxRadiance(Ray, Lambda);
                break;
            }
        }

        // Incoming ray direction in normal/tangent space.
        vec3 In;

        // Outgoing ray direction in normal/tangent space.
        vec3 Out = -vec3(
            dot(Ray.Vector, Hit.TangentX),
            dot(Ray.Vector, Hit.TangentY),
            dot(Ray.Vector, Hit.Normal));

        // Determines if the surface we hit is a virtual surface.  A surface
        // is virtual if it belongs to a shape with a lower priority than the
        // highest priority shape that we are currently traversing through.
        // In that case the medium associated with the higher priority shape
        // supersedes the lower priority one, and it is as if an interface
        // does not exist at that point at all.
        bool IsVirtualSurface;

        // Index of refraction of the medium above the surface.
        float AmbientIOR;

        if (Out.z > 0) {
            // If we are hitting the exterior side of a shape, then the surface
            // interface should affect the ray if the surface belongs to
            // a higher priority shape than we are currently traversing through.
            IsVirtualSurface = CurrentMedium.ShapePriority >= Hit.ShapePriority;

            if (!IsVirtualSurface) {
                AmbientIOR = CurrentMedium.IOR;
            }
        }
        else {
            // If we are hitting the interior side of a shape, then the surface
            // interface should affect the ray only if the surface belongs to
            // the highest-priority shape that we are currently traversing
            // through.
            IsVirtualSurface = CurrentMedium.ShapeIndex != Hit.ShapeIndex;

            // If the surface is real, then we need to determine the ambient IOR.
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
                AmbientIOR = ExteriorMedium.IOR;
            }
        }

        // Resolve the surface details.
        surface_parameters Surface;
        ResolveSurfaceParameters(Hit, Lambda, AmbientIOR, Surface);

        // Pass through the surface if embedded in a higher-priority
        // medium, or probabilistically based on geometric opacity.
        if (Random0To1() > Surface.Opacity || IsVirtualSurface)
            In = -Out;
        else
            BSDF(Out, In, Radiance, Weight, Surface);

        if (Weight < EPSILON) break;

        // If the incoming and outgoing directions are within opposite hemispheres,
        // then the ray is crossing the material interface boundary.  We need to
        // perform bookkeeping to determine the current Medium.
        if (In.z * Out.z < 0) {
            if (Out.z > 0) {
                // We are tracing into the object, so add a medium entry
                // associated with the object.
                for (int I = 0; I < MAX_MEDIUM_COUNT; I++) {
                    if (Mediums[I].ShapeIndex == SHAPE_INDEX_NONE) {
                        Mediums[I].ShapeIndex     = Hit.ShapeIndex;
                        Mediums[I].ShapePriority  = Hit.ShapePriority;
                        Mediums[I].IOR            = Surface.MediumIOR;
                        Mediums[I].ScatteringRate = Surface.MediumScatteringRate;
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

        // Prepare the extension ray.
        Ray.Vector = In.x * Hit.TangentX
                   + In.y * Hit.TangentY
                   + In.z * Hit.Normal;

        Ray.Origin = Hit.Position + 1e-3 * Ray.Vector;

        // Handle probabilistic termination.
        if (Random0To1() < RenderTerminationProbability)
            break;

        Weight /= 1.0 - RenderTerminationProbability;
    }

    vec3 Color = SampleStandardObserverSRGB(Lambda) * Radiance;

    return vec4(Color, 1);
}

vec4 RenderBaseColor(ray Ray, bool IsShaded)
{
    hit Hit = Trace(Ray, INFINITY);

    if (Hit.Time == INFINITY) {
        // We hit the skybox.  Generate a color sample from the skybox radiance
        // spectrum by integrating against the standard observer.
        vec4 Spectrum = SampleSkyboxSpectrum(Ray);
        vec3 Color = ObserveParametricSpectrumSRGB(Spectrum);
        return vec4(Color, 1);
    }

    // We hit a surface.  Resolve the base color sample from the reflectance
    // spectrum by integrating against the standard observer.
    packed_material Material = Materials[Hit.MaterialIndex];
    vec3 BaseColor = ObserveParametricSpectrumSRGB(Material.BaseSpectrum);
    if (Material.BaseSpectrumTextureIndex != TEXTURE_INDEX_NONE) {
        vec4 Value = SampleTexture(Material.BaseSpectrumTextureIndex, Hit.UV);
        BaseColor *= ObserveParametricSpectrumSRGB(Value.xyz);
    }

    if (IsShaded) {
        float Shading = dot(Hit.Normal, -Ray.Vector); 
        if (Hit.ShapeIndex == SelectedShapeIndex)
            return vec4((BaseColor + vec3(1,0,0)) * Shading, 1.0);
        else
            return vec4(BaseColor * Shading, 1.0);
    }
    else {
        return vec4(BaseColor, 1);
    }
}

vec4 RenderNormal(ray Ray)
{
    hit Hit = Trace(Ray, INFINITY);
    if (Hit.Time == INFINITY)
        return vec4(0.5 * (1 - Ray.Vector), 1);
    return vec4(0.5 * (Hit.Normal + 1), 1);
}

vec4 RenderMaterialIndex(ray Ray)
{
    hit Hit = Trace(Ray, INFINITY);
    if (Hit.Time == INFINITY)
        return vec4(0, 0, 0, 1);
    return vec4(COLORS[Hit.MaterialIndex % 20], 1);
}

vec4 RenderPrimitiveIndex(ray Ray)
{
    hit Hit = Trace(Ray, INFINITY);
    if (Hit.Time == INFINITY)
        return vec4(0, 0, 0, 1);
    return vec4(COLORS[Hit.PrimitiveIndex % 20], 1);
}

vec4 RenderMeshComplexity(ray Ray)
{
    hit Hit = Trace(Ray, INFINITY);
    float Alpha = min(Hit.MeshComplexity / float(RenderMeshComplexityScale), 1.0);
    vec3 Color = mix(vec3(0,0,0), vec3(0,1,0), Alpha);
    return vec4(Color, 1);
}

vec4 RenderSceneComplexity(ray Ray)
{
    hit Hit = Trace(Ray, INFINITY);
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
    ivec2 SampleImagePosition = ivec2(floor(SamplePosition));

    // This position can be outside the target image if the image size is not
    // a multiple of the region size (16 * RenderSampleBlockSize) handled by
    // one invocation.  If that happens, just exit.
    ivec2 ImageSize = imageSize(OutputImage);
    if (SampleImagePosition.x >= ImageSize.x) return;
    if (SampleImagePosition.y >= ImageSize.y) return;

    // Compute normalized sample position from (0, 0) to (1, 1).
    vec2 NormalizedSamplePosition = SamplePosition / ImageSize;

    ray Ray;

    if (CameraModel == CAMERA_MODEL_PINHOLE) {
        vec3 SensorPosition = vec3(
            -CameraSensorSize.x * (NormalizedSamplePosition.x - 0.5),
            -CameraSensorSize.y * (0.5 - NormalizedSamplePosition.y),
            CameraSensorDistance);

        Ray.Origin = vec3(CameraApertureRadius * RandomPointOnDisk(), 0);
        Ray.Vector = normalize(Ray.Origin - SensorPosition);
    }

    else if (CameraModel == CAMERA_MODEL_THIN_LENS) {
        vec3 SensorPosition = vec3(
            -CameraSensorSize.x * (NormalizedSamplePosition.x - 0.5),
            -CameraSensorSize.y * (0.5 - NormalizedSamplePosition.y),
            CameraSensorDistance);

        vec3 ObjectPosition = -SensorPosition * CameraFocalLength / (SensorPosition.z - CameraFocalLength);

        Ray.Origin = vec3(CameraApertureRadius * RandomPointOnDisk(), 0);
        Ray.Vector = normalize(ObjectPosition - Ray.Origin);
    }

    else if (CameraModel == CAMERA_MODEL_360) {
        float Phi = (NormalizedSamplePosition.x - 0.5f) * TAU;
        float Theta = (0.5f - NormalizedSamplePosition.y) * PI;

        Ray.Origin = vec3(0, 0, 0);
        Ray.Vector = vec3(cos(Theta) * sin(Phi), sin(Theta), -cos(Theta) * cos(Phi));
    }

    Ray = TransformRay(Ray, CameraTransform);

    vec4 SampleRadiance;

    if (RenderMode == RENDER_MODE_PATH_TRACE)
        SampleRadiance = RenderPath(Ray);
    else if (RenderMode == RENDER_MODE_BASE_COLOR)
        SampleRadiance = RenderBaseColor(Ray, false);
    else if (RenderMode == RENDER_MODE_BASE_COLOR_SHADED)
        SampleRadiance = RenderBaseColor(Ray, true);
    else if (RenderMode == RENDER_MODE_NORMAL)
        SampleRadiance = RenderNormal(Ray);
    else if (RenderMode == RENDER_MODE_MATERIAL_INDEX)
        SampleRadiance = RenderMaterialIndex(Ray);
    else if (RenderMode == RENDER_MODE_PRIMITIVE_INDEX)
        SampleRadiance = RenderPrimitiveIndex(Ray);
    else if (RenderMode == RENDER_MODE_MESH_COMPLEXITY)
        SampleRadiance = RenderMeshComplexity(Ray);
    else if (RenderMode == RENDER_MODE_SCENE_COMPLEXITY)
        SampleRadiance = RenderSceneComplexity(Ray);

    // Transfer the sample block from the input image to the output image,
    // adding the sample value that we produced at the relevant pixel
    // position.
    ivec2 XY = ivec2(gl_GlobalInvocationID.xy * RenderSampleBlockSize);
    for (int I = 0; I < RenderSampleBlockSize; I++) {
        for (int J = 0; J < RenderSampleBlockSize; J++) {
            ivec2 TransferImagePosition = XY + ivec2(I,J);
            if (TransferImagePosition.x >= ImageSize.x) break;
            if (TransferImagePosition.y >= ImageSize.y) return;
            vec4 OutputRadiance = vec4(0,0,0,0);
            if ((RenderFlags & RENDER_FLAG_ACCUMULATE) != 0)
                OutputRadiance = imageLoad(InputImage, TransferImagePosition);
            if (TransferImagePosition == SampleImagePosition)
                OutputRadiance += SampleRadiance;
            imageStore(OutputImage, TransferImagePosition, OutputRadiance);
        }
    }
}
