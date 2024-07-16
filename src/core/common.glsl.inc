#ifndef COMMON_GLSL_INC
#define COMMON_GLSL_INC

const float INFINITY = 1e30f;
const float EPSILON = 1e-9f;
const float PI = 3.141592653f;
const float TAU = 6.283185306f;

const float HIT_TIME_LIMIT = 1048576.0;

const float CIE_LAMBDA_MIN = 360.0;
const float CIE_LAMBDA_MAX = 830.0;

const uint RENDER_FLAG_ACCUMULATE = 1 << 0;
const uint RENDER_FLAG_SAMPLE_JITTER = 1 << 1;

struct packed_transform
{
    mat4 To;
    mat4 From;
};

struct ray
{
    vec3  Origin;
    vec3  Velocity;
    float Duration;
};

// Volumetric material properties of a participating medium.
struct medium
{
    uint  Priority;             // Priority of the medium when multiple media overlap.
    vec4  IOR;                  // Absolute index of refraction at each of the 4 sampling wavelengths.
    vec4  AbsorptionRate;       // Absorption rate at each of the 4 sampling wavelengths.
    vec4  ScatteringRate;       // Scattering rate at each of the 4 sampling wavelengths.
    float ScatteringAnisotropy; // Scattering phase function anisotropy.
};

vec3 TransformPosition(vec3 P, packed_transform Transform)
{
    return (Transform.To * vec4(P, 1)).xyz;
}

vec3 TransformVector(vec3 V, packed_transform Transform)
{
    return (Transform.To * vec4(V, 0)).xyz;
}

vec3 TransformNormal(vec3 N, packed_transform Transform)
{
    return normalize((vec4(N, 0) * Transform.From).xyz);
}

vec3 TransformDirection(vec3 D, packed_transform Transform)
{
    return normalize(TransformVector(D, Transform));
}

ray TransformRay(ray R, packed_transform Transform)
{
    ray OutR;
    OutR.Origin = TransformPosition(R.Origin, Transform);
    OutR.Velocity = TransformVector(R.Velocity, Transform);
    OutR.Duration = R.Duration;
    return OutR;
}

vec3 InverseTransformPosition(vec3 P, packed_transform Transform)
{
    return (Transform.From * vec4(P, 1)).xyz;
}

vec3 InverseTransformVector(vec3 V, packed_transform Transform)
{
    return (Transform.From * vec4(V, 0)).xyz;
}

vec3 InverseTransformNormal(vec3 N, packed_transform Transform)
{
    return normalize((vec4(N, 0) * Transform.To).xyz);
}

ray InverseTransformRay(ray R, packed_transform Transform)
{
    ray OutR;
    OutR.Origin = InverseTransformPosition(R.Origin, Transform);
    OutR.Velocity = InverseTransformVector(R.Velocity, Transform);
    OutR.Duration = R.Duration;
    return OutR;
}

vec3 SafeNormalize(vec3 V)
{
    float LenSq = dot(V, V);
    if (LenSq < 1e-12)
        return vec3(0, 0, 1);
    else
        return V / sqrt(LenSq);
}

vec4 Sqr(vec4 X)
{
    return X * X;
}

float max4(vec4 V)
{
    return max(max(V.x, V.y), max(V.z, V.w));
}

// Compute an arbitrary tangent vector for a given normal.
vec3 ComputeTangentVector(vec3 Normal)
{
    vec3 V = abs(Normal.x) < 0.9 ? vec3(1, 0, 0) : vec3(0, 1, 0);
    return normalize(cross(V, Normal));
}

// Compute an arbitrary 3D coordinate frame given one vector.
void ComputeCoordinateFrame(vec3 Z, out vec3 X, out vec3 Y)
{
    vec3 V = abs(Z.x) < 0.9 ? vec3(1, 0, 0) : vec3(0, 1, 0);
    X = normalize(cross(V, Z));
    Y = cross(X, Z);
}

vec2 SignNotZero(vec2 V)
{
    return vec2
    (
        V.x >= 0.0 ? +1.0 : -1.0,
        V.y >= 0.0 ? +1.0 : -1.0
    );
}

// Packs a unit vector into a single 32-bit value.
uint PackUnitVector(vec3 V)
{
    vec2 P = V.xy * (1.0 / (abs(V.x) + abs(V.y) + abs(V.z)));
    if (V.z <= 0.0) P = (1.0 - abs(P.yx)) * SignNotZero(P);
    return packSnorm2x16(P);
}

// Unpacks a unit vector from a single 32-bit value.
vec3 UnpackUnitVector(uint PackedV)
{
    vec2 P = unpackSnorm2x16(PackedV);
    float Z = 1.0 - abs(P.x) - abs(P.y);
    if (Z < 0.0) P = (1.0 - abs(P.yx)) * SignNotZero(P);
    return normalize(vec3(P, Z));
}

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

    // Compute the ray entry and exit times. The ray enters the box when
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

/* --- Random Number Generator --------------------------------------------- */

uint RandomState;

uint Random()
{
    RandomState = RandomState * 747796405u + 2891336453u;
    uint s = RandomState;
    uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}

// Generate a random number in the range [0,1).
float Random0To1()
{
    return Random() / 4294967296.0f;
}

vec2 RandomPointOnDisk()
{
    float R = sqrt(Random0To1());
    float Theta = Random0To1() * TAU;
    return R * vec2(cos(Theta), sin(Theta));
}

vec3 RandomDirection()
{
    float Z = 2 * Random0To1() - 1;
    float R = sqrt(1 - Z * Z);
    float Phi = TAU * Random0To1();
    return vec3(R * cos(Phi), R * sin(Phi), Z);
}

vec3 RandomHemisphereDirection(vec3 Normal)
{
    vec3 Direction = RandomDirection();
    return Direction * sign(dot(Normal, Direction));
}

// Generate a random direction from a von Mises-Fisher distribution on
// a sphere, with concentration parameter Kappa and mean direction (0,0,1).
vec3 RandomVonMisesFisher(float Kappa)
{
    float Xi = Random0To1();
    float Z = 1 + (1 / Kappa) * log(Xi + (1 - Xi) * exp(-2 * Kappa));

    float R = sqrt(1 - Z * Z);
    float Phi = Random0To1() * TAU;
    float X = R * cos(Phi);
    float Y = R * sin(Phi);

    return vec3(X, Y, Z);
}

vec3 RandomVonMisesFisher(float Kappa, vec3 Mu)
{
    vec3 V = RandomVonMisesFisher(Kappa);
    vec3 MuX, MuY;
    ComputeCoordinateFrame(Mu, MuX, MuY);
    return SafeNormalize(V.x * MuX + V.y * MuY + V.z * Mu);
}

float VonMisesFisherPDF(float Kappa, vec3 Mu, vec3 Direction)
{
    if (Kappa < EPSILON) return 1.0 / (4 * PI);
    float C = Kappa / (2 * PI * (1 - exp(-2 * Kappa)));
    return C * exp(Kappa * (dot(Mu, Direction) - 1.0));
}

/* --- Henyey-Greenstein Phase Function ------------------------------------ */

// Sample a direction from the Henyey-Greenstein phase function.
vec3 SampleDirectionHG(float Anisotropy, float U1, float U2)
{
    float Z;
    if (abs(Anisotropy) < 1e-3)
    {
        // The distribution becomes spherical as the anisotropy approaches zero.
        Z = 1 - 2 * U1;
    }
    else
    {
        float G = Anisotropy;
        float S = (1 - G * G) / (1 + G - 2 * G * U1);
        Z = -(1 + G * G - S * S) / (2 * G);
    }
    float R = sqrt(1 - Z * Z);
    float Phi = U2 * TAU;
    return vec3(R * cos(Phi), R * sin(Phi), Z);
}

/* --- GGX Microfacet Model with Anisotropic Roughness --------------------- */

// Compute the 2D roughness alpha parameter for the GGX distribution of normals.
vec2 GGXRoughnessAlpha(float Roughness, float Anisotropy)
{
    float R = Roughness;
    float S = 1 - Anisotropy;
    float AlphaX = R * R * sqrt(2 / (1 + S * S));
    float AlphaY = S * AlphaX;
    return vec2(AlphaX, AlphaY);
}

// One half of the uncorrelated Smith shadowing-masking function for the
// anisotropic GGX distribution of normals as presented in the paper
// "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"
// by Eric Heitz.
float GGXSmithG1(vec3 Direction, vec2 RoughnessAlpha)
{
    vec3 DirectionSq = Direction * Direction;
    if (DirectionSq.z < EPSILON) return 0.0;
    vec2 RoughnessAlphaSq = RoughnessAlpha * RoughnessAlpha;
    float AlphaSqByTanThetaSq = dot(RoughnessAlphaSq, DirectionSq.xy) / DirectionSq.z;
    return 2.0 / (1.0 + sqrt(1.0 + AlphaSqByTanThetaSq));
}

// Sample a normal from the GGX distribution that is visible from the given
// direction according to the procedure outlined in the paper "Sampling the
// GGX Distribution of Visible Normals" by Eric Heitz.
vec3 GGXVisibleNormal(vec3 Direction, vec2 RoughnessAlpha, float U1, float U2)
{
    // Transform direction to the hemisphere space.
    vec3 Vz = SafeNormalize
    (
        vec3
        (
            RoughnessAlpha.x * Direction.x,
            RoughnessAlpha.y * Direction.y,
            Direction.z
        )
    );

    // Compute orthogonal frame in hemisphere space.
    float LengthSq = dot(Vz.xy, Vz.xy);
    vec3 Vx = LengthSq > 0 ? vec3(-Vz.y, Vz.x, 0) / sqrt(LengthSq) : vec3(1, 0, 0);
    vec3 Vy = cross(Vz, Vx);

    // Uniformly sample the projected area of the hemisphere.
    float R = sqrt(U1);
    float Phi = TAU * U2;
    float S = 0.5 * (1.0 + Vz.z);

    float Tx = R * cos(Phi);
    float Ty = (1.0 - S) * sqrt(1.0 - Tx * Tx) + S * R * sin(Phi);
    float Tz = sqrt(max(0.0, 1.0 - Tx * Tx - Ty * Ty));

    // Determine normal corresponding to the sampled projected point.
    vec3 Normal = Tx * Vx + Ty * Vy + Tz * Vz;

    // Transform normal to the ellipsoid space.
    return SafeNormalize
    (
        vec3
        (
            RoughnessAlpha.x * Normal.x,
            RoughnessAlpha.y * Normal.y,
            max(0.0, Normal.z)
        )
    );
}

// This is the anisotropic GGX distribution of normals D(m).
float GGXDistribution(vec3 Normal, vec2 RoughnessAlpha)
{
    vec2 A = 1.0 / RoughnessAlpha;
    float B = dot(Normal * Normal, vec3(A * A, 1.0));
    return 1.0 / (PI * RoughnessAlpha.x * RoughnessAlpha.y * B * B);
}

/* --- Reflection and Refraction ------------------------------------------- */

// Computes a wavelength-dependent refractive index according to
// the Cauchy empirical formula.
vec4 CauchyEmpiricalIOR(float BaseIOR, float AbbeNumber, vec4 Lambda)
{
    // Fraunhofer C, d, and F spectral lines.
    const float LC = 656.3;
    const float Ld = 587.6;
    const float LF = 486.1;

    float B = (BaseIOR - 1) / (AbbeNumber * (1.0 / (LF * LF) - 1.0 / (LC * LC)));
    float A = BaseIOR - B / (Ld * Ld);

    return A + B / (Lambda * Lambda);
}

// Compute the cosine of the angle of refraction with respect to the surface
// normal, given the cosine of the angle of incidence with respect to the
// surface normal and the relative index of refraction at the interface
// (IOR of incident medium over that of the refracting medium).  Both angles
// are measured with respect to the same surface normal.  In case of total
// internal reflection, the return value is zero.
float ComputeCosThetaRefracted(float Eta, float CosTheta)
{
    float Cos2ThetaRefracted = 1 - Eta * Eta * (1 - CosTheta * CosTheta);
    return -sign(CosTheta) * sqrt(max(Cos2ThetaRefracted, 0.0));
}

// Like above, but for 4 quantities at the same time.
vec4 ComputeCosThetaRefracted(vec4 Eta, vec4 CosTheta)
{
    vec4 Cos2ThetaRefracted = 1 - Eta * Eta * (1 - CosTheta * CosTheta);
    return -sign(CosTheta) * sqrt(max(Cos2ThetaRefracted, 0.0));
}

// Compute surface reflectance using the Fresnel equations given a relative
// index of refraction and cosines of the angles of the incident and refracted
// rays with respect to the surface normal.  Both angles are measured with
// respect to the same surface normal.
float FresnelDielectric(float Eta, float CosTheta1, float CosTheta2)
{
    float Ks = Eta * CosTheta1;
    float SqrtRs = (Ks + CosTheta2) / (Ks - CosTheta2);
    float Kp = Eta * CosTheta2;
    float SqrtRp = (Kp + CosTheta1) / (Kp - CosTheta1);
    return 0.5 * (SqrtRs * SqrtRs + SqrtRp * SqrtRp);
}

// Like the float version, but computes 4 quantities at the same time.
vec4 FresnelDielectric(vec4 Eta, vec4 CosTheta1, vec4 CosTheta2)
{
    vec4 Ks = Eta * CosTheta1;
    vec4 SqrtRs = (Ks + CosTheta2) / (Ks - CosTheta2);
    vec4 Kp = Eta * CosTheta2;
    vec4 SqrtRp = (Kp + CosTheta1) / (Kp - CosTheta1);
    return 0.5 * (SqrtRs * SqrtRs + SqrtRp * SqrtRp);
}

// As above, but with CosTheta2 implicit from Eta and CosTheta1.
vec4 FresnelDielectric(vec4 Eta, vec4 CosTheta1)
{
    vec4 CosTheta2 = ComputeCosThetaRefracted(Eta, CosTheta1);
    return FresnelDielectric(Eta, CosTheta1, CosTheta2);
}

// Computes the Fresnel term for a metallic surface using the "F82-tint"
// formulation presented in the paper "Novel aspects of the Adobe Standard
// Material" by Kutz et al, adapted for spectral rendering.
vec4 SchlickFresnelMetal(vec4 Base, vec4 Specular, float CosTheta)
{
    const float CosThetaMax = 1 / 7.0f;

    vec4 FSchlick = Base + (1 - Base) * pow(1.0 - CosTheta, 5);
    vec4 FSchlickMax = Base + (1 - Base) * pow(1 - CosThetaMax, 5);
    vec4 FMax = Specular * FSchlickMax;

    const float Denominator = CosThetaMax * pow(1 - CosThetaMax, 6);
    float Nominator = CosTheta * pow(1.0 - CosTheta, 6);
    return FSchlick - (Nominator / Denominator) * (FSchlickMax - FMax);
}

#endif // COMMON_GLSL_INC
