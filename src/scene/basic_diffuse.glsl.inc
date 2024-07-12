const uint BASIC_DIFFUSE_BASE_SPECTRUM = 1;

struct basic_diffuse_parameters
{
    vec4  Lambda;                 // Sampling wavelengths.
    vec4  BaseReflectance;        // Base reflectance value at each of the 4 sampling wavelengths.
};

vec3 BasicDiffuse_BaseColor(uint MaterialIndex, vec2 TextureUV)
{
    return MaterialTexturableColor(MaterialIndex, BASIC_DIFFUSE_BASE_SPECTRUM, TextureUV);
}

basic_diffuse_parameters BasicDiffuse_Parameters
(
    uint MaterialIndex,
    vec2 TextureUV,
    vec4 Lambda,
    vec4 ExteriorIOR
)
{
    basic_diffuse_parameters Parameters;

    Parameters.Lambda = Lambda;

    Parameters.BaseReflectance = MaterialTexturableReflectance
    (
        MaterialIndex,
        BASIC_DIFFUSE_BASE_SPECTRUM,
        Lambda, TextureUV
    );

    return Parameters;
}

void BasicDiffuse_Sample
(
    basic_diffuse_parameters Parameters,
    vec3 Out, out vec3 In,
    inout vec4 PathThroughput,
    inout vec4 PathDensity
)
{
    In = SafeNormalize(RandomDirection() + vec3(0, 0, 1));

    PathThroughput *= Parameters.BaseReflectance;
}
