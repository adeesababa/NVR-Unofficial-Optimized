#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;
layout(set = 1, binding = 1) uniform texture2D t_text;
layout(set = 1, binding = 2) uniform texture2D t_hold;
layout(set = 1, binding = 3) uniform texture2D t_exposure;
layout(set = 1, binding = 4) uniform texture2D t_depth;

layout(push_constant) uniform Push {
    vec4 recip;
    vec4 lensC;
    vec4 imageC;
    vec4 noiseC;
    vec4 textC;
    vec4 frameC;
    vec4 motionC;
    vec4 gradeC;
    vec4 videoC;
    vec4 adaptC;
    vec4 bodyC;
    vec4 flashC;
    vec4 glitchC;
    uint linearIndex;
    uint pointIndex;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

const vec2 TextTextureSize = vec2(256.0, 12.0);

float Hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float ClipWhiteAmount(float m) { return clamp((m - 1.0) * 0.8, 0.0, 1.0); }

vec3 ClipToWhite(vec3 c) {
    c = max(c, 0.0);
    float m = max(c.r, max(c.g, c.b));
    return m <= 1.0 ? c : mix(c / m, vec3(1.0), ClipWhiteAmount(m));
}

float ValueNoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(Hash(i), Hash(i + vec2(1.0, 0.0)), f.x), mix(Hash(i + vec2(0.0, 1.0)), Hash(i + vec2(1.0, 1.0)), f.x), f.y);
}

float LensDirt(vec2 pos) {
    vec2 p = pos * vec2(pc.recip.z, 1.0);
    float blotch = smoothstep(0.55, 0.85, ValueNoise(p * 3.0 + 11.0) * 0.65 + ValueNoise(p * 7.0 + 3.0) * 0.35);
    vec2 cell = floor(p * 24.0);
    vec2 inCell = fract(p * 24.0) - 0.5;
    float speck = Hash(cell + 41.0) > 0.93 ? 1.0 - smoothstep(0.1, 0.35, length(inCell)) : 0.0;
    return clamp(blotch * 0.7 + speck * 0.6, 0.0, 1.0);
}

vec2 Bend(vec2 pos, float scale) {
    float aspect = pc.recip.z;
    vec2 p = (pos * 2.0 - 1.0) * vec2(aspect, 1.0);
    float roll = pc.bodyC.z, rs = sin(roll), rc = cos(roll);
    p = vec2(p.x * rc - p.y * rs, p.x * rs + p.y * rc) / max(pc.bodyC.w, 1.0) + pc.bodyC.xy * 2.0 * vec2(aspect, 1.0);
    float r2 = dot(p, p);
    p *= (1.0 + pc.lensC.x * r2) * scale / pc.lensC.y;
    return p / vec2(aspect, 1.0) * 0.5 + 0.5;
}

vec3 Read(vec2 pos) {
    return textureLod(sampler2D(t_rendered, NVR_SAMPLER(pc.linearIndex)), pos, 0.0).rgb;
}

vec3 Smeared(vec2 pos, bool moving) {
    if (!moving) return Read(pos);
    vec3 c = vec3(0.0);
    for (int i = 0; i < 7; i++) c += Read(pos + pc.motionC.xy * (float(i) / 6.0 - 0.5));
    return c / 7.0;
}

vec4 Picture() {
    vec2 px = pc.recip.xy;
    if (pc.videoC.w > 0.5) return textureLod(sampler2D(t_hold, NVR_SAMPLER(pc.pointIndex)), uv, 0.0);
    float scale1080 = 1.0 / (px.y * 1080.0);

    vec2 blockUV = (floor(uv / (px * 8.0)) + 0.5) * px * 8.0;
    vec2 shutter = pc.videoC.yz * (uv.y - 0.5);
    vec2 at = Bend(uv, 1.0) + shutter;
    float edge = length((uv * 2.0 - 1.0) * vec2(pc.recip.z, 1.0));

    float fringe = pc.lensC.z * scale1080 * px.y * edge * edge * 0.5;
    vec2 outward = normalize(at - 0.5 + 1e-5);
    bool moving = length(pc.motionC.xy / px) > 0.75;
    vec3 colour;
    colour.r = Smeared(at + outward * fringe, moving).r;
    colour.g = Smeared(at, moving).g;
    colour.b = Smeared(at - outward * fringe, moving).b;

    float blurRadius = (pc.frameC.y * edge * edge * 2.0 + pc.gradeC.y * 2.5) * px.y * scale1080;
    if (pc.glitchC.z > 0.0 && pc.adaptC.w > 0.5) {
        vec2 focus = textureLod(sampler2D(t_exposure, NVR_SAMPLER(pc.pointIndex)), vec2(0.5), 0.0).ba;
        blurRadius += pc.glitchC.z * clamp(abs(focus.x - focus.y) * 0.7, 0.0, 1.0) * 6.0 * px.y * scale1080;
    }
    if (blurRadius > px.y * 0.5) {
        vec2 r = blurRadius * vec2(px.x / px.y, 1.0);
        vec3 ring = vec3(0.0);
        float turn = fract(52.9829189 * fract(dot(uv / px, vec2(0.06711056, 0.00583715)))) * 1.570796;
        for (int k = 0; k < 4; k++) {
            float a = float(k) * 1.570796 + turn;
            ring += Read(at + r * vec2(cos(a), sin(a)) * 0.5);
            ring += Read(at + r * vec2(cos(a + 0.785398), sin(a + 0.785398)));
        }
        colour = colour * 0.2 + ring * 0.1;
    }

    vec2 stepUV = px * 1.25;
    vec3 around = (Read(at + vec2(stepUV.x, 0.0)) + Read(at - vec2(stepUV.x, 0.0)) + Read(at + vec2(0.0, stepUV.y)) + Read(at - vec2(0.0, stepUV.y))) * 0.25;
    colour += (colour - around) * pc.imageC.x;

    vec3 glow = vec3(0.0);
    for (int i = 0; i < 8; i++) {
        float a = float(i) * 0.785398;
        vec2 way = vec2(cos(a), sin(a)) * vec2(px.x / px.y, 1.0);
        vec2 way2 = vec2(cos(a + 0.3927), sin(a + 0.3927)) * vec2(px.x / px.y, 1.0);
        glow += max(Read(at + way * 0.012) - 0.65, 0.0) * 0.65;
        glow += max(Read(at + way2 * 0.035) - 0.65, 0.0) * 0.35;
    }
    colour += glow * 0.125 * pc.imageC.w * 2.0;
    if (pc.flashC.w > 0.0) {
        float jitter = fract(52.9829189 * fract(dot(uv / px, vec2(0.06711056, 0.00583715))));
        vec3 streak = vec3(0.0);
        for (int i = 1; i <= 6; i++) {
            float o = (float(i) - 0.5 + jitter) * 0.045;
            float weight = 1.0 - float(i) / 7.0;
            streak += (max(Read(at + vec2(0.0, o)) - 0.85, 0.0) + max(Read(at - vec2(0.0, o)) - 0.85, 0.0)) * weight;
        }
        colour += streak * 0.35 * pc.flashC.w * (1.0 + pc.flashC.z);
    }
    colour += glow * 0.5 * pc.flashC.x;

    vec3 blockColour = Read(Bend(blockUV, 1.0) + shutter);
    float flatness = 1.0 - clamp(length(colour - around) * 8.0, 0.0, 1.0);
    float dark = 1.0 - clamp(luma(blockColour) * 2.5, 0.0, 1.0);
    colour = mix(colour, blockColour, pc.noiseC.y * flatness * dark);
    if (pc.videoC.x > 0.01) {
        float breakup = pc.videoC.x;
        vec2 bigUV = (floor(uv / (px * 16.0)) + 0.5) * px * 16.0;
        vec3 big = Read(Bend(bigUV, 1.0) + shutter);
        vec2 hugeUV = (floor(uv / (px * 32.0)) + 0.5) * px * 32.0;
        vec3 huge = Read(Bend(hugeUV, 1.0) + shutter);
        colour = mix(colour, blockColour, clamp(breakup * 0.35 * (0.4 + 0.6 * flatness), 0.0, 1.0));
        colour = mix(colour, big, clamp((breakup - 0.3) * 0.45 * (0.3 + 0.7 * flatness), 0.0, 1.0));
        colour = mix(colour, huge, clamp((breakup - 1.2) * 0.4 * (0.2 + 0.8 * flatness), 0.0, 1.0));
        float levels = max(48.0 - breakup * 12.0, 8.0);
        colour = mix(colour, floor(colour * levels + 0.5) / levels, clamp(breakup * 0.3, 0.0, 1.0));
    }

    float dirt = LensDirt(uv);
    float bright = luma(glow) * 0.125 + 0.2 * luma(colour) * luma(colour);
    colour += dirt * bright * pc.frameC.w;

    float gain = 1.0;
    if (pc.adaptC.x > 0.0 && pc.adaptC.w > 0.5) {
        vec2 measured = textureLod(sampler2D(t_exposure, NVR_SAMPLER(pc.pointIndex)), vec2(0.5), 0.0).rg;
        gain = clamp(pow(max(measured.x, 1e-4) / max(measured.y, 1e-4), pc.adaptC.x / 2.2), 0.2, 5.0);
    }
    gain *= (1.0 + pc.flashC.x * 1.5) * (1.0 - pc.flashC.y * 0.4);
    colour *= pc.frameC.z * gain;
    colour = pow(ClipToWhite(colour), vec3(1.0 / (1.0 + pc.motionC.w)));
    colour = ClipToWhite((colour - 0.5) * pc.imageC.y + 0.5);
    colour *= mix(vec3(1.0), vec3(0.93, 0.98, 1.12) / 0.9795, pc.gradeC.x);
    float lum = luma(colour);
    colour = mix(vec3(lum), colour, pc.imageC.z);
    colour *= mix(vec3(1.0), vec3(0.94, 1.0, 0.95), pc.noiseC.w);
    if (pc.flashC.z > 0.0) {
        float l = luma(colour);
        colour = mix(colour, vec3(0.96, 1.0, 0.97) * pow(max(l, 0.0), 0.85), pc.flashC.z * 0.9);
    }
    colour = ClipToWhite(colour);
    colour = (1.0 - exp(-max(colour, 0.0) * 1.15)) / (1.0 - exp(-1.15));
    colour = clamp(colour, 0.0, 1.0);

    float frameNo = pc.noiseC.z;
    vec2 cell = floor(uv / px);
    float n = Hash(cell + frameNo * 17.31) - 0.5;
    vec3 chroma = vec3(Hash(cell * 1.3 + frameNo * 3.7), Hash(cell * 0.7 + frameNo * 5.1), Hash(cell * 1.9 + frameNo * 7.3)) - 0.5;
    float amount = pc.noiseC.x * 0.06 * mix(1.0, 0.35, clamp(luma(colour) * 1.5, 0.0, 1.0)) * (1.0 + pc.flashC.z * 1.5);
    colour += (n + chroma * 0.4) * amount;

    colour *= 1.0 - pc.lensC.w * smoothstep(0.6, 1.9, edge);
    float oval = length(uv * 2.0 - 1.0);
    float ovalStart = mix(1.6, 0.75, pc.frameC.x);
    colour *= 1.0 - smoothstep(ovalStart, ovalStart + 0.35, oval);
    colour *= (all(greaterThanEqual(at, vec2(0.0))) && all(lessThanEqual(at, vec2(1.0)))) ? 1.0 : 0.0;

    if (pc.glitchC.x > 0.0) {
        vec2 blockId = floor(uv / (px * 16.0));
        float h = Hash(blockId + pc.glitchC.y);
        float row = Hash(vec2(blockId.y, pc.glitchC.y * 0.37));
        if (h < pc.glitchC.x * 0.35 || row < pc.glitchC.x * 0.08) {
            vec2 shift = vec2((Hash(blockId * 1.7 + pc.glitchC.y) - 0.5) * 0.08, 0.0);
            vec3 stale = textureLod(sampler2D(t_hold, NVR_SAMPLER(pc.pointIndex)), uv + shift, 0.0).rgb;
            vec3 smear = Read(Bend((blockId + 0.5) * px * 16.0, 1.0));
            colour = Hash(blockId + 7.0) > 0.5 ? stale : smear;
            if (Hash(blockId + 3.0) > 0.6) colour.rb = colour.br;
        }
    }

    float lit = 0.0, shadow = 0.0;
    float texel = max(floor(pc.textC.y * scale1080 + 0.5), 1.0);
    vec2 corner = vec2(1.0 / px.x - pc.textC.z * texel - 40.0 * scale1080, 34.0 * scale1080);
    vec2 pixel = uv / px;
    vec2 t = (pixel - corner) / texel;
    if (pc.textC.x > 0.0 && all(greaterThan(t, vec2(-1.0))) && all(lessThan(t, pc.textC.zw + 1.0))) {
        lit = textureLod(sampler2D(t_text, NVR_SAMPLER(pc.pointIndex)), (floor(t) + 0.5) / TextTextureSize, 0.0).a;
        vec2 back = (pixel - corner - max(floor(texel * 0.35), 1.0)) / texel;
        shadow = textureLod(sampler2D(t_text, NVR_SAMPLER(pc.pointIndex)), (floor(back) + 0.5) / TextTextureSize, 0.0).a;
    }
    colour = mix(colour, colour * 0.25, shadow * pc.textC.x);
    colour = mix(colour, vec3(0.93, 0.93, 0.9), lit * pc.textC.x);
    return vec4(clamp(colour, 0.0, 1.0), 1.0);
}

void main() {
    o_color = Picture();
}
