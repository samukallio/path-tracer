#version 450

#include "integrator/basic.glsl.inc"

layout(local_size_x=16, local_size_y=16, local_size_z=1) in;

void GenerateNewPath(uint Index, ivec2 ImagePosition)
{
    ivec2 ImageSize = imageSize(SampleAccumulatorImage);

    // Compute the position of the sample we are going to produce in image
    // coordinates from (0, 0) to (ImageSizeX, ImageSizeY).
    vec2 SamplePosition = ImagePosition;

    if ((RenderFlags & RENDER_FLAG_SAMPLE_JITTER) != 0)
        SamplePosition += vec2(Random0To1(), Random0To1());
    else
        SamplePosition += vec2(0.5, 0.5);

    // Compute normalized sample position from (0, 0) to (1, 1).
    vec2 NormalizedSamplePosition = SamplePosition / ImageSize;

    packed_camera Camera = Cameras[CameraIndex];

    ray Ray = GenerateCameraRay(Camera, NormalizedSamplePosition);

    // Write initial ray.
    StoreTraceRay(Index, Ray);

    // Write new path.
    path Path;
    Path.ImagePosition = ImagePosition;
    Path.NormalizedLambda0 = Random0To1();
    Path.Throughput = vec4(1.0);
    Path.Weight = vec4(1.0);
    Path.Sample = vec3(0.0);

    for (int I = 0; I < 4; I++)
        Path.ActiveShapeIndex[I] = SHAPE_INDEX_NONE;

    StorePath(Index, Path);
}

medium ResolveMedium(uint ShapeIndex, vec4 Lambda)
{
    medium Medium;

    if (true) //ShapeIndex == SHAPE_INDEX_NONE)
    {
        Medium.Priority = 0xFFFFFFFF;
        Medium.IOR = vec4(1.0);
        Medium.AbsorptionRate = vec4(0.0);
        Medium.ScatteringRate = vec4(Scene.SceneScatterRate);
        Medium.ScatteringAnisotropy = 0.0;
    }
    else
    {
        packed_shape Shape = Shapes[ShapeIndex];
        Medium = MaterialMedium(Shape.MaterialIndex, Lambda);
    }

    Medium.Priority = ShapeIndex;
    return Medium;
}

bool Sample
(
    in  hit Hit,
    in  bsdf_parameters Parameters,
    in  vec3 Out,
    out vec3 In,
    out vec4 Throughput,
    out vec4 InPDF
)
{
    float LightProbability = MaterialHasDiracBSDF(Parameters) ? 0.0 : Scene.SkyboxSamplingProbability;

    vec4 MaterialPDF;

    vec3 SkyboxMeanDirection = vec3
    (
        dot(Scene.SkyboxMeanDirection, Hit.TangentX),
        dot(Scene.SkyboxMeanDirection, Hit.TangentY),
        dot(Scene.SkyboxMeanDirection, Hit.Normal)
    );

    if (Random0To1() < LightProbability)
    {
        In = RandomVonMisesFisher(Scene.SkyboxConcentration, SkyboxMeanDirection);

        if (In.z < 0.0)
            return false;

        if (!MaterialEvaluateBSDF(Parameters, Out, In, Throughput, MaterialPDF))
            return false;
    }
    else
    {
        if (!MaterialSampleBSDF(Parameters, Out, In, Throughput, MaterialPDF))
            return false;
    }

    vec4 SkyboxPDF = vec4(VonMisesFisherPDF(Scene.SkyboxConcentration, SkyboxMeanDirection, In));

    InPDF = LightProbability * SkyboxPDF + (1 - LightProbability) * MaterialPDF;
    return true;
}

void RenderPathTrace(inout path Path, inout ray Ray, hit Hit)
{
    vec4 Lambda = vec4
    (
        mix(CIE_LAMBDA_MIN, CIE_LAMBDA_MAX, Path.NormalizedLambda0 ),
        mix(CIE_LAMBDA_MIN, CIE_LAMBDA_MAX, fract(Path.NormalizedLambda0 + 0.25)),
        mix(CIE_LAMBDA_MIN, CIE_LAMBDA_MAX, fract(Path.NormalizedLambda0 + 0.50)),
        mix(CIE_LAMBDA_MIN, CIE_LAMBDA_MAX, fract(Path.NormalizedLambda0 + 0.75))
    );

    // Determine the highest priority shape that we are currently in.
    uint ActiveShapeIndex = SHAPE_INDEX_NONE;
    for (int I = 0; I < ACTIVE_SHAPE_LIMIT; I++)
        ActiveShapeIndex = min(ActiveShapeIndex, Path.ActiveShapeIndex[I]);

    // Determine the properties of the incident participating medium.
    medium Medium = ResolveMedium(ActiveShapeIndex, Lambda);

    // Apply attenuation due to absorption.
    Path.Throughput *= exp(-Medium.AbsorptionRate * Hit.Time);

    // Compute scattering event time based on the scattering rate for the primary wavelength.
    float ScatteringTime = HIT_TIME_LIMIT;
    if (Medium.ScatteringRate.x > 0.0)
        ScatteringTime = -log(Random0To1()) / Medium.ScatteringRate.x;

    if (Hit.Time >= ScatteringTime)
    {
        if (ScatteringTime < HIT_TIME_LIMIT)
        {
            Ray.Origin += Ray.Velocity * ScatteringTime;

            // Compute a local coordinate frame for the scattering event.
            vec3 X, Y, Z = Ray.Velocity;
            ComputeCoordinateFrame(Z, X, Y);

            // Sample a random scattering direction in the local frame.
            float U1 = Random0To1();
            float U2 = Random0To1();
            vec3 Scattered = SampleDirectionHG(Medium.ScatteringAnisotropy, U1, U2);

            // Compute and apply per-wavelength probability of scattering at this point.
            vec4 Density = Medium.ScatteringRate * exp(-Medium.ScatteringRate * ScatteringTime);
            Density /= max(EPSILON, max4(Density));
            Path.Throughput *= Density;
            Path.Weight *= Density;

            // Transform the scattered ray into world space and set it as the extension ray.
            Ray.Velocity = normalize(X * Scattered.x + Y * Scattered.y + Z * Scattered.z);
            Ray.Duration = HIT_TIME_LIMIT;
        }
        // Otherwise, we hit the skybox.
        else
        {
            vec4 Emission = SampleSkyboxRadiance(Ray.Velocity, Lambda);
            float ClusterPDF = Path.Weight.x + Path.Weight.y + Path.Weight.z + Path.Weight.w;
            Path.Sample += SampleStandardObserver(Lambda) * (Emission * Path.Throughput) / ClusterPDF;
            Path.Weight = vec4(0.0);
        }

        return;
    }

    // Incoming ray direction in normal/tangent space.
    vec3 In;

    // Outgoing ray direction in normal/tangent space.
    vec3 Out = -vec3
    (
        dot(Ray.Velocity, Hit.TangentX),
        dot(Ray.Velocity, Hit.TangentY),
        dot(Ray.Velocity, Hit.Normal)
    );

    // This flag determines if the surface will scatter the ray.
    // It is always true, except when two or more objects overlap
    // and we hit a surface of a lower priority object while inside
    // a higher priority one. In that case the interior of the
    // higher priority shape supersedes the lower priority one, and
    // it is as if there is no surface at all.
    bool IsRealSurface = true;

    // Refractive indices (at each sampling wavelength) of the medium
    // outside the surface (positive normal direction).
    vec4 ExteriorIOR = vec4(1.0);

    // Priority of the interior of the current shape.
    uint ShapePriority = Hit.ShapeIndex;

    if (Out.z > 0)
    {
        // We hit the exterior surface of a shape. The ray should be
        // scattered if the priority of the shape is higher than the
        // priority of the current medium. Otherwise, the shape and
        // its surface are superseded by the current medium, and the
        // ray will pass through.
        IsRealSurface = Medium.Priority > ShapePriority;

        if (IsRealSurface) ExteriorIOR = Medium.IOR;
    }
    else
    {
        // We hit the interior surface of a shape. The ray should be
        // scattered if this surface belongs to the shape whose interior
        // we are currently traversing. In that case the priority of
        // the active medium and the surface are the same.
        IsRealSurface = Medium.Priority == ShapePriority;

        // If the surface is real, then we need to determine the IOR
        // on the other (exterior) side of the shape.
        if (IsRealSurface)
        {
            // Determine the highest priority shape outside the current shape.
            uint ExteriorShapeIndex = SHAPE_INDEX_NONE;
            for (int I = 0; I < ACTIVE_SHAPE_LIMIT; I++)
            {
                if (Path.ActiveShapeIndex[I] == ActiveShapeIndex)
                    continue;
                ExteriorShapeIndex = min(ExteriorShapeIndex, Path.ActiveShapeIndex[I]);
            }

            medium Exterior = ResolveMedium(ExteriorShapeIndex, Lambda);

            ExteriorIOR = Exterior.IOR;
        }
    }

    if (IsRealSurface)
    {
        bsdf_parameters Parameters;
        Parameters.MaterialIndex = Hit.MaterialIndex;
        Parameters.TextureUV = Hit.UV;
        Parameters.Lambda = Lambda;
        Parameters.ExteriorIOR = ExteriorIOR;

        vec4 Throughput;
        vec4 InPDF;
        if (!Sample(Hit, Parameters, Out, In, Throughput, InPDF))
        {
            Path.Weight = vec4(0.0);
            return;
        }

        float C = max4(InPDF);
        if (C > EPSILON)
        {
            Throughput /= C;
            InPDF /= C;
        }

        Path.Throughput *= Throughput;
        Path.Weight *= InPDF;
    }
    else
    {
        In = -Out;
    }

    if (max4(Path.Weight) < EPSILON) return;

    // If the incoming and outgoing directions are within opposite hemispheres,
    // then the ray is crossing the material interface boundary. We need to
    // perform bookkeeping to determine the current medium.
    if (In.z * Out.z < 0)
    {
        if (Out.z > 0)
        {
            for (int I = 0; I < ACTIVE_SHAPE_LIMIT; I++)
            {
                if (Path.ActiveShapeIndex[I] == SHAPE_INDEX_NONE)
                {
                    Path.ActiveShapeIndex[I] = Hit.ShapeIndex;
                    break;
                }
            }
        }
        else
        {
            for (int I = 0; I < ACTIVE_SHAPE_LIMIT; I++)
            {
                if (Path.ActiveShapeIndex[I] == Hit.ShapeIndex)
                {
                    Path.ActiveShapeIndex[I] = SHAPE_INDEX_NONE;
                    break;
                }
            }
        }
    }

    // Handle probabilistic termination.
    if (Random0To1() < PathTerminationProbability)
    {
        Path.Weight = vec4(0.0);
        return;
    }

    Path.Weight *= 1.0 - PathTerminationProbability;

    // Prepare the extension ray.
    Ray.Velocity = In.x * Hit.TangentX
                 + In.y * Hit.TangentY
                 + In.z * Hit.Normal;

    Ray.Origin = Hit.Position + 1e-3 * Ray.Velocity;

    Ray.Duration = HIT_TIME_LIMIT;
}

void main()
{
    // Initialize random number generator.
    RandomState
        = gl_GlobalInvocationID.y * 65537
        + gl_GlobalInvocationID.x
        + RandomSeed * 277803737u;

    uvec2 ImageSize = imageSize(SampleAccumulatorImage);
    if (gl_GlobalInvocationID.x >= ImageSize.x) return;
    if (gl_GlobalInvocationID.y >= ImageSize.y) return;

    uint Index
        = gl_WorkGroupID.y * 16 * 16 * (ImageSize.x / 16)
        + gl_WorkGroupID.x * 16 * 16
        + gl_LocalInvocationID.y * 16
        + gl_LocalInvocationID.x;

    if (Restart != 0)
    {
        ivec2 ImagePosition = ivec2(gl_GlobalInvocationID.xy);
        GenerateNewPath(Index, ImagePosition);
        imageStore(SampleAccumulatorImage, ImagePosition, vec4(0.0));
        return;
    }

    ray Ray = LoadTraceRay(Index);
    hit Hit = LoadTraceHit(Index);

    Hit.Position = Ray.Origin + Hit.Time * Ray.Velocity;

    path Path = LoadPath(Index);

    RenderPathTrace(Path, Ray, Hit);

    if (max4(Path.Weight) < EPSILON)
    {
        ivec2 ImagePosition = Path.ImagePosition;

        vec4 ImageValue = vec4(Path.Sample, 1.0);

        if ((RenderFlags & RENDER_FLAG_ACCUMULATE) != 0)
            ImageValue += imageLoad(SampleAccumulatorImage, ImagePosition);

        imageStore(SampleAccumulatorImage, ImagePosition, ImageValue);

        GenerateNewPath(Index, ImagePosition);
    }
    else
    {
        StoreTraceRay(Index, Ray);
        StorePathVertexData(Index, Path);
    }
}
