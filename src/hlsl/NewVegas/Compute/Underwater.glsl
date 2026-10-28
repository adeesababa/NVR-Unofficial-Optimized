
layout(set = 2, binding = 0, std140) uniform Constants {
    vec4 reciprocalResolution;
    vec4 sunDirection;
    vec4 gameTime;
    vec4 waterCoefficients;
    vec4 sunColor;
    vec4 waterFog;
    vec4 waterVolume;
    vec4 waveParams;
    vec4 waterSettings;
    vec4 waterShallowColor;
    vec4 waterDeepColor;
    vec4 horizonColor;
    vec4 skyColor;
    vec4 cameraPosition;
    vec4 cameraData;
    mat4 projectionTransform;
    mat4 viewTransform;
} cb;

const vec3 up = vec3(0.0, 0.0, 1.0);

float shade(vec3 n, vec3 l) { return max(dot(n, l), 0.0); }
float shades(vec3 n, vec3 l) { return clamp(dot(n, l), 0.0, 1.0); }
float compressF(float v) { return (v * 0.5) + 0.5; }
float invlerp(float a, float b, float t) { return (t - a) / (b - a); }

float hsmooth(float a, float b, float x) {
    float t = clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

float fmodHLSL(float x, float y) { return x - y * trunc(x / y); }

float sunLuma() { return luma(pows(cb.sunColor.rgb, 2.2)); }

vec3 toWorld(vec2 tex) {
    vec3 v = vec3(cb.viewTransform[0][2], cb.viewTransform[1][2], cb.viewTransform[2][2]);
    v += (1.0 / cb.projectionTransform[0][0] * (2.0 * tex.x - 1.0)) * vec3(cb.viewTransform[0][0], cb.viewTransform[1][0], cb.viewTransform[2][0]);
    v += (-1.0 / cb.projectionTransform[1][1] * (2.0 * tex.y - 1.0)) * vec3(cb.viewTransform[0][1], cb.viewTransform[1][1], cb.viewTransform[2][1]);
    return v;
}

float readDepth(vec2 coord, texture2D depth, uint samplerIndex) {
    return texture(sampler2D(depth, NVR_SAMPLER(samplerIndex)), coord).x * cb.cameraData.y;
}

vec3 getWorldNormal(vec2 uv, texture2D normals, uint samplerIndex) {
    vec4 normal = vec4(texture(sampler2D(normals, NVR_SAMPLER(samplerIndex)), uv).xyz * 2.0 - 1.0, 1.0);
    return (normal * cb.viewTransform).xyz;
}

float getCaustics(vec3 pos, texture2D caustics, uint samplerIndex) {
    float depth = cb.waterSettings.z - pos.z;
    pos += (depth / cb.sunDirection.z) * cb.sunDirection.xyz;

    float speed = cb.gameTime.x * 0.01 * cb.waveParams.z;
    float scale = 0.002;

    float layer1 = texture(sampler2D(caustics, NVR_SAMPLER(samplerIndex)), pos.xy * scale + speed * normalize(vec2(-1.2, -2.5))).r;
    float layer2 = texture(sampler2D(caustics, NVR_SAMPLER(samplerIndex)), pos.xy * scale * 1.2 + speed * normalize(vec2(0.5, 2.0))).r;

    return pows(min(layer1, layer2), 1.3) * 4.0;
}

vec3 getSkyColor(vec3 eyeDirection, vec3 sunColor) {
    vec3 skyColor = mix(linearize(cb.horizonColor).rgb, linearize(cb.skyColor).rgb, pow(shade(eyeDirection, up), 0.5));
    skyColor += sunColor * pows(shades(eyeDirection, cb.sunDirection.xyz), 12.0);
    return skyColor;
}

bool aboveWater(float eyeHeight, float pointHeight, float waterHeight) {
    if (eyeHeight < waterHeight - 4.0) return false;
    return eyeHeight > (waterHeight + 4.0) || pointHeight > (waterHeight - 0.6);
}
