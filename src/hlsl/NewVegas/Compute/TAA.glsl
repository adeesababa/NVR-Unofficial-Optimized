#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_source;
layout(set = 1, binding = 1) uniform texture2D t_history;
layout(set = 1, binding = 2) uniform texture2D t_depth;
layout(set = 1, binding = 3) uniform texture2D t_viewmodel;

layout(set = 2, binding = 0, std140) uniform Constants {
    mat4 projectionTransform;
    mat4 viewTransform;
    mat4 prevViewTransform;
    vec4 reciprocalResolution;
    vec4 cameraData;
    vec4 data;
    vec4 prevProjection;
    vec4 cameraDelta;
    vec4 weapon;
    vec4 sharpen;
} cb;

layout(push_constant) uniform Push {
    uint pointIndex;
    uint linearIndex;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

#define POINT_S NVR_SAMPLER(pc.pointIndex)
#define LINEAR_S NVR_SAMPLER(pc.linearIndex)

#define historyWeight cb.data.x
#define clipGamma cb.data.y
#define historyValid cb.data.z
#define debugView cb.data.w
#define weaponHistoryScale cb.weapon.x
#define farZ cb.cameraData.y

float readDepth(vec2 p) { return textureLod(sampler2D(t_depth, POINT_S), p, 0.0).x * farZ; }

vec3 toWorld(vec2 tex) {
    vec3 v = vec3(cb.viewTransform[0][2], cb.viewTransform[1][2], cb.viewTransform[2][2]);
    v += (1.0 / cb.projectionTransform[0][0] * (2.0 * tex.x - 1.0)) * vec3(cb.viewTransform[0][0], cb.viewTransform[1][0], cb.viewTransform[2][0]);
    v += (-1.0 / cb.projectionTransform[1][1] * (2.0 * tex.y - 1.0)) * vec3(cb.viewTransform[0][1], cb.viewTransform[1][1], cb.viewTransform[2][1]);
    return v;
}

vec3 RGBToYCoCg(vec3 c) {
    return vec3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b,
                0.5 * c.r - 0.5 * c.b,
                -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

vec3 YCoCgToRGB(vec3 c) {
    return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

vec2 Reproject(vec2 p, out float inFront) {
    vec3 cameraVector = toWorld(p) * readDepth(p);
    vec3 fromPrevCamera = cameraVector + cb.cameraDelta.xyz;

    vec3 prevRight   = vec3(cb.prevViewTransform[0][0], cb.prevViewTransform[1][0], cb.prevViewTransform[2][0]);
    vec3 prevUp      = vec3(cb.prevViewTransform[0][1], cb.prevViewTransform[1][1], cb.prevViewTransform[2][1]);
    vec3 prevForward = vec3(cb.prevViewTransform[0][2], cb.prevViewTransform[1][2], cb.prevViewTransform[2][2]);

    float viewZ = dot(fromPrevCamera, prevForward);
    inFront = viewZ > 0.0 ? 1.0 : 0.0;

    vec2 ndc = vec2(dot(fromPrevCamera, prevRight) * cb.prevProjection.x,
                    dot(fromPrevCamera, prevUp) * cb.prevProjection.y) / max(viewZ, 1e-4);
    return vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
}

vec3 SampleHistory(vec2 p) {
    vec2 texelSize = cb.reciprocalResolution.xy;
    vec2 samplePos = p / texelSize;
    vec2 texPos1 = floor(samplePos - 0.5) + 0.5;
    vec2 f = samplePos - texPos1;

    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);

    vec2 w12 = w1 + w2;
    vec2 offset12 = w2 / w12;

    vec2 texPos0 = (texPos1 - 1.0) * texelSize;
    vec2 texPos3 = (texPos1 + 2.0) * texelSize;
    vec2 texPos12 = (texPos1 + offset12) * texelSize;

    vec3 result = vec3(0.0);
    result += textureLod(sampler2D(t_history, LINEAR_S), vec2(texPos12.x, texPos0.y), 0.0).rgb * (w12.x * w0.y);
    result += textureLod(sampler2D(t_history, LINEAR_S), vec2(texPos0.x, texPos12.y), 0.0).rgb * (w0.x * w12.y);
    result += textureLod(sampler2D(t_history, LINEAR_S), vec2(texPos12.x, texPos12.y), 0.0).rgb * (w12.x * w12.y);
    result += textureLod(sampler2D(t_history, LINEAR_S), vec2(texPos3.x, texPos12.y), 0.0).rgb * (w3.x * w12.y);
    result += textureLod(sampler2D(t_history, LINEAR_S), vec2(texPos12.x, texPos3.y), 0.0).rgb * (w12.x * w3.y);
    float weights = w12.x * (w0.y + w12.y + w3.y) + w12.y * (w0.x + w3.x);

    return max(result / weights, vec3(0.0));
}

vec3 ClipToBox(vec3 history, vec3 boxMin, vec3 boxMax) {
    vec3 center = 0.5 * (boxMax + boxMin);
    vec3 extents = 0.5 * (boxMax - boxMin) + 1e-4;
    vec3 offset = history - center;
    vec3 units = abs(offset / extents);
    float maxUnit = max(units.x, max(units.y, units.z));
    return maxUnit > 1.0 ? center + offset / maxUnit : history;
}

struct TAAResult {
    vec3 color;
    vec3 current;
    vec3 historyRaw;
    vec2 motionPixels;
    float weight;
    float clipAmount;
    float isViewModel;
    float depth;
};

TAAResult ResolveCore(vec2 p) {
    TAAResult result;
    vec2 texelSize = cb.reciprocalResolution.xy;

    vec3 current = vec3(0.0);
    vec3 moment1 = vec3(0.0);
    vec3 moment2 = vec3(0.0);
    vec3 neighbourMin = vec3(1e5);
    vec3 neighbourMax = vec3(-1e5);
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec3 c = RGBToYCoCg(textureLod(sampler2D(t_source, POINT_S), p + vec2(x, y) * texelSize, 0.0).rgb);
            if (x == 0 && y == 0) current = c;
            moment1 += c;
            moment2 += c * c;
            neighbourMin = min(neighbourMin, c);
            neighbourMax = max(neighbourMax, c);
        }
    }
    moment1 /= 9.0;
    moment2 /= 9.0;
    vec3 sigma = sqrt(max(moment2 - moment1 * moment1, vec3(0.0)));

    vec3 boxMin = max(neighbourMin, moment1 - clipGamma * sigma);
    vec3 boxMax = min(neighbourMax, moment1 + clipGamma * sigma);

    float inFront;
    vec2 prevUV = Reproject(p, inFront);

    float isViewModel = textureLod(sampler2D(t_viewmodel, POINT_S), p, 0.0).x > 0.0 ? 1.0 : 0.0;
    prevUV = mix(prevUV, p, isViewModel);
    inFront = max(inFront, isViewModel);

    vec2 inside = step(vec2(0.0), prevUV) * step(prevUV, vec2(1.0));
    float onScreen = inside.x * inside.y;

    vec3 historyRaw = SampleHistory(prevUV);
    vec3 historyRawYCoCg = RGBToYCoCg(historyRaw);
    vec3 history = ClipToBox(historyRawYCoCg, boxMin, boxMax);

    float weight = historyWeight * historyValid * onScreen * inFront;

    weight *= mix(1.0, weaponHistoryScale, isViewModel);

    result.color = YCoCgToRGB(mix(current, history, weight));
    result.current = YCoCgToRGB(current);
    result.historyRaw = historyRaw;
    result.motionPixels = (prevUV - p) / texelSize;
    result.weight = weight;
    result.clipAmount = length(history - historyRawYCoCg);
    result.isViewModel = isViewModel;
    result.depth = readDepth(p);
    return result;
}
