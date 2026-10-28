
struct Particle {
    vec4 a;
    vec4 b;
};

#define GP_FRAME_BLOCK \
    mat4 viewProj; \
    mat4 orthoProj; \
    vec4 origin; \
    vec4 camDelta; \
    vec4 box; \
    vec4 sim; \
    vec4 look; \
    vec4 screen; \
    vec4 light; \
    vec4 camRight; \
    vec4 camUp; \
    vec4 camFwd;

const float GP_SPLASHES = 1.0;
const float GP_ORTHO = 2.0;
const float GP_RESET = 4.0;

bool gpFlag(float flags, float bit) { return mod(floor(flags / bit), 2.0) > 0.5; }

uint gpHash(uint x) {
    x = x * 747796405u + 2891336453u;
    uint w = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
    return (w >> 22u) ^ w;
}
float gpRand(inout uint state) { state = gpHash(state); return float(state & 0x00FFFFFFu) / 16777216.0; }
