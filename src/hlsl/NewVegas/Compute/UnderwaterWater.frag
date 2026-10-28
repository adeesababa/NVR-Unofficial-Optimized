#version 450
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_samplerless_texture_functions : enable
#include "Common.glsl"
#include "Underwater.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;
layout(set = 1, binding = 1) uniform texture2D t_source;
layout(set = 1, binding = 2) uniform texture2D t_depth;
layout(set = 1, binding = 3) uniform texture2D t_normals;
layout(set = 1, binding = 4) uniform texture2D t_caustics;
layout(set = 1, binding = 5) uniform texture2D t_shadow;

layout(push_constant) uniform Push {
    uint linearIndex;
    uint pointIndex;
    uint anisoIndex;
    uint pad;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

const float ditherMat[16] = float[16](
    0.0588, 0.5294, 0.1765, 0.6471,
    0.7647, 0.2941, 0.8824, 0.4118,
    0.2353, 0.7059, 0.1176, 0.5882,
    0.9412, 0.4706, 0.8235, 0.3259);

void main() {
    float depth = readDepth(uv, t_depth, pc.linearIndex);
    vec3 eyeVector = toWorld(uv);
    vec3 eyeDirection = normalize(eyeVector);
    vec3 worldPos = cb.cameraPosition.xyz + eyeVector * depth;
    float fogDepth = length(eyeVector * depth);
    float waterDepth = cb.waterSettings.x - worldPos.z;
    vec3 floorNormal = getWorldNormal(uv, t_normals, pc.pointIndex);

    vec4 color = texture(sampler2D(t_source, NVR_SAMPLER(pc.linearIndex)), uv);
    if (aboveWater(cb.cameraPosition.z, worldPos.z, cb.waterSettings.x)) {
        o_color = color;
        return;
    }

    color = linearize(color);

    float density = 0.08;
    float falloff = 0.00225;
    float fogAmount = clamp((density / falloff) * exp(-cb.cameraPosition.z * falloff) * (1.0 - exp(-fogDepth * eyeDirection.z * falloff)) / eyeDirection.z, 0.0, 1.0);

    vec3 waterDeepColor = linearize(cb.waterDeepColor).rgb;
    vec3 waterShallowColor = linearize(cb.waterShallowColor).rgb;
    vec3 sunColor = linearize(cb.sunColor).rgb;
    float sunL = sunLuma();
    float causticsStrength = cb.waterVolume.x;
    float depthDarkness = cb.waterSettings.y;
    vec3 extCoeff = cb.waterCoefficients.xyz;
    float scattCoeff = cb.waterCoefficients.w;
    float turbidity = cb.waterVolume.z;
    float godraysStrength = cb.waterVolume.w;

    vec3 skyColor = getSkyColor(eyeDirection, sunColor);

    float verticalFade = clamp(compressF(dot(eyeDirection, up) * 1.5), 0.0, 1.0);
    float sunDirFade = compressF(dot(eyeDirection.xyz, cb.sunDirection.xyz));

    vec3 fogColor = mix(waterDeepColor, waterShallowColor + skyColor * 0.2, verticalFade);
    fogColor *= sunL * (0.4 + (1.0 - 0.4) * sunDirFade) * extCoeff;
    vec3 scattering = (1.0 - fogColor) * scattCoeff;
    float linearFog = clamp(invlerp(cb.waterFog.x, cb.waterFog.y, fogDepth), 0.0, 1.0);

    float floorAngle = (smoothstep(0.5, 1.0, dot(floorNormal, up)) + smoothstep(0.8, 1.0, dot(floorNormal, cb.sunDirection.xyz))) / 2.0;
    float depthFade = hsmooth(800.0, 0.0, waterDepth);
    float distanceFade = invlerp(6000.0, 0.0, fogDepth);

    vec2 shadows = texture(sampler2D(t_shadow, NVR_SAMPLER(pc.anisoIndex)), uv).rg;
    float caustics = getCaustics(worldPos, t_caustics, pc.anisoIndex) * shadows.r;

    color.rgb += color.rgb * pows(caustics, 2.0) * sunColor * depthFade * floorAngle * distanceFade * sunL * causticsStrength * 100.0;

    color.rgb = mix(color.rgb, fogColor, clamp(pow(invlerp(0.0, 5000.0, fogDepth), 10.0), 0.0, 1.0));

    color.rgb *= mix(vec3(1.0), waterDeepColor, clamp(fogAmount * pows(depthDarkness, 0.5), 0.0, 1.0) * (1.0 - shadows.g * distanceFade));

    float fogThickness = clamp(2.0 - pows(turbidity, 0.5) * 2.0, 0.0, 1.0);
    float turbidPower = (turbidity * 3.0) / (1.0 + 3.0 * turbidity);
    color.rgb = mix(color.rgb, fogColor, clamp(scattering * pows(linearFog, fogThickness) * turbidPower, 0.0, 1.0));

    color.rgb += texture(sampler2D(t_rendered, NVR_SAMPLER(pc.linearIndex)), uv / 2.0).r * sunColor * godraysStrength * color.rgb;

    vec2 pixel = uv / cb.reciprocalResolution.xy;
    color = delinearize(color);
    int row = int(fmodHLSL(pixel.x, 4.0));
    int column = int(fmodHLSL(pixel.y, 4.0));
    color.rgb += ditherMat[row * 4 + column] / 255.0;

    o_color = vec4(color.rgb, 1.0);
}
