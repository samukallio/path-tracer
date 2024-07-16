const uint BASIC_DIFFUSE_BASE_SPECTRUM = 1;

vec3 BasicDiffuse_BaseColor(uint MaterialIndex, vec2 TextureUV)
{
    return MaterialTexturableColor(MaterialIndex, BASIC_DIFFUSE_BASE_SPECTRUM, TextureUV);
}

bool BasicDiffuse_HasDiracBSDF(bsdf_parameters Parameters)
{
    return false;
}

// Evaluate the BSDF for a given incident and scattered direction.
bool BasicDiffuse_EvaluateBSDF
(
    // Inputs.
    in  bsdf_parameters Parameters,
    in  vec3 In,
    in  vec3 Out,
    // Outputs.
    out vec4 Throughput,
    out vec4 Probability
)
{
    vec4 Reflectance = MaterialTexturableReflectance(Parameters, BASIC_DIFFUSE_BASE_SPECTRUM);
    Probability = vec4(In.z / PI);
    Throughput = Probability * Reflectance;
    return true;
}

// Importance sample a scattered direction and evaluate the BSDF using it.
bool BasicDiffuse_SampleBSDF
(
    // Inputs.
    in  bsdf_parameters Parameters,
    in  vec3 In,
    // Outputs.
    out vec3 Out,
    out vec4 Throughput,
    out vec4 Probability
)
{
    Out = SafeNormalize(RandomDirection() + vec3(0, 0, 1));
    return BasicDiffuse_EvaluateBSDF(Parameters, In, Out, Throughput, Probability);
}
