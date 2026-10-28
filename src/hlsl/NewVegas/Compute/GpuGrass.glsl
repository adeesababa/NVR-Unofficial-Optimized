
struct GGDraw {
    vec4 shape;
    vec4 scaleMask;
    vec4 sun;
    vec4 ambient;
    vec4 ramp[4];
};

struct GGBlade {
    vec4 a;
    vec3 tip;
    uint packed;
    vec3 ctrl;
    uint light;
};

#define GG_FRAME_BLOCK \
    vec4 rows[4]; \
    vec4 cam; \
    vec4 sunDir; \
    vec4 wind; \
    vec4 fade; \
    vec4 fogParam; \
    vec4 fogColor; \
    vec4 lod; \
    vec4 shape; \
    vec4 shape2; \
    vec4 shape3; \
    vec4 lanes; \
    vec4 misc; \
    vec4 look; \
    vec4 planes[4];

uint ggHash(uint x) {
    x = x * 747796405u + 2891336453u;
    uint w = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
    return (w >> 22u) ^ w;
}
uint ggSeed(vec2 p, uint salt) { return ggHash(floatBitsToUint(p.x) ^ ggHash(floatBitsToUint(p.y) ^ ggHash(salt))); }
float ggRand(inout uint s) { s = ggHash(s); return float(s & 0x00FFFFFFu) / 16777216.0; }

vec3 ggWindMove(vec4 inst, vec4 lanes, float clock, float swing, vec4 windData, float weight, float height) {
    vec2 dir = lanes.xy * inversesqrt(dot(lanes.xy, lanes.xy));
    float gustPhase = clock * 0.25;
    float along = dot(inst.xy, lanes.xy);
    float front = 0.5 + 0.5 * sin(gustPhase - along);
    float patchWave = 0.75 + 0.25 * sin(gustPhase - 0.5 * (along + inst.y * lanes.x - inst.x * lanes.y));
    float fa = 2.0 * clock + inst.w + (inst.x - inst.y) * 0.0078125;
    vec2 flutter = vec2(sin(fa), cos(fa));
    float alongWind = swing * length(windData.xy) + lanes.z * front * front * patchWave + lanes.w * flutter.x;
    vec2 xy = (dir * alongWind + vec2(-dir.y, dir.x) * (lanes.w * 0.5 * flutter.y)) * (windData.z * weight);
    float h = max(height, 1e-6);
    return vec3(xy, -min(dot(xy, xy) * 0.5 / h, h));
}
