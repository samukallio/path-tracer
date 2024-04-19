const float INFINITY    = 1e30f;
const float EPSILON     = 1e-9f;
const float PI          = 3.141592653f;
const float TAU         = 6.283185306f;

const uint RENDER_MODE_PATH_TRACE       = 0;
const uint RENDER_MODE_ALBEDO           = 1;
const uint RENDER_MODE_NORMAL           = 2;
const uint RENDER_MODE_MATERIAL_INDEX   = 3;
const uint RENDER_MODE_PRIMITIVE_INDEX  = 4;

const uint TONE_MAPPING_MODE_CLAMP      = 0;
const uint TONE_MAPPING_MODE_REINHARD   = 1;
const uint TONE_MAPPING_MODE_HABLE      = 2;
const uint TONE_MAPPING_MODE_ACES       = 3;

struct ToneMapping
{
    uint            mode;
    float           whiteLevel;
};

const uint CAMERA_TYPE_PINHOLE = 0;
const uint CAMERA_TYPE_THIN_LENS = 1;

struct Camera
{
    uint            type;
    float           focalLength;
    float           apertureRadius;
    float           sensorDistance;
    vec2            sensorSize;
    mat4            worldMatrix;
};

layout(binding = 0)
uniform FrameUniformBuffer
{
    uint            frameIndex;
    uint            objectCount;
    uint            clearFrame;
    uint            renderMode;
    uint            bounceLimit;

    ToneMapping     toneMapping;
    Camera          camera;
};