
struct Particle {
    vec4 pos;
    vec4 vel;
    vec4 info;
    vec4 nrm;
};

const uint FlagContact = 1u;
const uint FlagTouched = 2u;
const uint FlagGround  = 4u;
const uint FlagFrozen  = 8u;
const uint FlagSettled = 16u;

const float DeadAge = -1e30;

uint Flags(float w) { return uint(w + 0.5); }

uint PackSlope(vec2 g) {
    ivec2 q = ivec2(round(clamp(g, -1.0, 1.0) * 64.0));
    return ((uint(q.x) & 255u) << 8) | ((uint(q.y) & 255u) << 16);
}
vec2 SlopeOf(uint flags) {
    ivec2 q = ivec2(int((flags >> 8) & 255u), int((flags >> 16) & 255u));
    q -= ivec2(greaterThan(q, ivec2(127))) * 256;
    return vec2(q) / 64.0;
}

struct DrawRec {
    vec4 a;
    vec4 b;
    vec4 c;
};

layout(set = 2, binding = 0, std140) uniform Frame {
    vec4  eyeRel;
    vec4  right;
    vec4  up;
    vec4  fwd;
    vec4  screen;
    vec4  sim;
    vec4  fluid;
    vec4  wall;
    vec4  settle;
    vec4  gridf;
    uvec4 counts;
    uvec4 counts2;
    vec4  look;
    vec4  look2;
    vec4  viewRot0;
    vec4  viewRot1;
    vec4  viewRot2;
    vec4  fresh;
    vec4  dryColor;
    vec4  match;
    vec4  depthConsts;
    vec4  sunDir;
    vec4  sunColor;
    vec4  sunAmbient;
    vec4  horizon;
    vec4  lightPos[12];
    vec4  shadowLightPos[12];
    vec4  lightColor[24];
    vec4  look3;
    vec4  rework;
    vec4  look4;
} F;

uint hashU(uint x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
float hash01(uint x) { return float(hashU(x) & 0xFFFFFFu) / 16777216.0; }

vec3 ProjectRel(vec3 rel) {
    float z = dot(rel, F.fwd.xyz);
    if (z <= 0.5) return vec3(-1.0, -1.0, z);
    float nx = dot(rel, F.right.xyz) * F.right.w / z;
    float ny = dot(rel, F.up.xyz) * F.up.w / z;
    return vec3((nx * 0.5 + 0.5) * F.screen.x, (0.5 - ny * 0.5) * F.screen.y, z);
}

vec3 RayAt(vec2 px) {
    vec2 uv = px * F.screen.zw;
    return F.fwd.xyz + F.right.xyz * ((2.0 * uv.x - 1.0) / F.right.w) - F.up.xyz * ((2.0 * uv.y - 1.0) / F.up.w);
}

vec3 linearize(vec3 c) {
    vec3 lo = c / 12.92;
    vec3 hi = pow(abs((c + 0.055) / 1.055), vec3(2.4));
    return mix(hi, lo, lessThanEqual(c, vec3(0.04045)));
}
vec3 delinearize(vec3 c) {
    vec3 lo = c * 12.92;
    vec3 hi = (pow(abs(c), vec3(1.0 / 2.4)) * 1.055) - 0.055;
    return mix(hi, lo, lessThanEqual(c, vec3(0.0031308)));
}
float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
float saturate(float x) { return clamp(x, 0.0, 1.0); }
vec3 saturate(vec3 x) { return clamp(x, vec3(0.0), vec3(1.0)); }

bool IsViewModel(float vm) {
    return (F.depthConsts.z == 0.0 && vm < 0.9) || (F.depthConsts.z > 0.0 && vm > 0.01);
}
