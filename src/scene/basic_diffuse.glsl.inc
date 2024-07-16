const uint BASIC_DIFFUSE_BASE_SPECTRUM = 1;

vec3 BasicDiffuse_BaseColor(uint MaterialIndex, vec2 TextureUV)
{
    return MaterialTexturableColor(MaterialIndex, BASIC_DIFFUSE_BASE_SPECTRUM, TextureUV);
}

bool BasicDiffuse_HasDiracBSDF(bsdf_parameters Parameters)
{
    return false;
}

bool BasicDiffuse_EvaluateBSDF
(
    in  bsdf_parameters Parameters,
    in  vec3 Out,
    out vec3 In,
    out vec4 Throughput,
    out vec4 Probability
)
{
    vec4 Reflectance = MaterialTexturableReflectance(Parameters, BASIC_DIFFUSE_BASE_SPECTRUM);
    Probability = vec4(In.z / PI);
    Throughput = Probability * Reflectance;
    return true;
}

bool BasicDiffuse_SampleBSDF
(
    in  bsdf_parameters Parameters,
    in  vec3 Out,
    out vec3 In,
    out vec4 Throughput,
    out vec4 Probability
)
{
    In = SafeNormalize(RandomDirection() + vec3(0, 0, 1));
    return BasicDiffuse_EvaluateBSDF(Parameters, Out, In, Throughput, Probability);
}
