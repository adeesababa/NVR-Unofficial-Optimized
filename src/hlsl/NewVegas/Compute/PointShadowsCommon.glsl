#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 2, binding = 0, std140) uniform Constants {
    vec4 shadowLightPosition[12];
    vec4 lightPosition[12];
    vec4 lightColor[24];
    vec4 extraLights[16];
    vec4 shadowFade;
    vec4 pointShadowNear;
    vec4 spotLightPosition;
    vec4 spotLightDirection;
    vec4 spotLightColor;
    vec4 lampContact;
    vec4 lampContactFade;
    vec4 lampContactBias;
    vec4 lampContactNoise;
    vec4 reciprocalResolution;
    vec4 cameraPosition;
    vec4 cameraData;
    mat4 viewTransform;
    mat4 projectionTransform;
    vec4 shadowLightPre[12];
    vec4 lightPre[12];
    vec4 extraPre[16];
    vec4 nearPre;
    vec4 spotPre;
} cb;

const float BIAS = 0.018;

float hlslSmoothstep(float a, float b, float x) {
    float t = clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

float smooth01(float t) {
    t = clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

float readDepthAt(texture2D depthTex, sampler samp, vec2 uv) {
    return textureLod(sampler2D(depthTex, samp), uv, 0.0).x * cb.cameraData.y;
}

vec3 toWorld(vec2 tex) {
    vec3 v = vec3(cb.viewTransform[0][2], cb.viewTransform[1][2], cb.viewTransform[2][2]);
    v += (1.0 / cb.projectionTransform[0][0] * (2.0 * tex.x - 1.0)) * vec3(cb.viewTransform[0][0], cb.viewTransform[1][0], cb.viewTransform[2][0]);
    v += (-1.0 / cb.projectionTransform[1][1] * (2.0 * tex.y - 1.0)) * vec3(cb.viewTransform[0][1], cb.viewTransform[1][1], cb.viewTransform[2][1]);
    return v;
}

vec3 worldNormalAt(texture2D normalsTex, sampler samp, vec2 uv) {
    vec4 normal = vec4(textureLod(sampler2D(normalsTex, samp), uv, 0.0).xyz * 2.0 - 1.0, 1.0);
    return (normal * cb.viewTransform).xyz;
}

float pointLightAmountValueLod(textureCube cubeTex, sampler samp, vec3 lightDir, float dn, float invNearFade) {
    if (cb.shadowFade.z == 0.0) return 1.0;
#ifdef PS_TEST_NO_CUBES
    return 1.0;
#endif

    float lightDepth = textureLod(samplerCube(cubeTex, samp), lightDir, 0.0).r;
    float shadow = lightDepth + BIAS * dn > dn ? 1.0 : 0.0;
    float blocker = invNearFade > 0.0 ? smooth01(lightDepth * invNearFade) : 1.0;
    return mix(1.0, shadow, float(lightDepth > 0.0 && lightDepth < 1.0) * blocker);
}

float edgeWindow(float dn) {
    return cb.pointShadowNear.y > 0.0 ? 1.0 - smooth01((dn - (1.0 - cb.pointShadowNear.y)) * cb.nearPre.x) : 1.0;
}

float lampReach(float dn) {
    return 1.0 - smooth01((dn - cb.pointShadowNear.z) * cb.nearPre.y);
}

float getPointLightAtten(vec3 lightDir, float dn, vec3 n) {
    float s = clamp(dn * dn, 0.0, 1.0);
    float atten = clamp(((1.0 - s) * (1.0 - s)) / (1.0 + 5.0 * s), 0.0, 1.0);

    vec3 l = normalize(lightDir);
    float diffuse = mix(1.0, dot(l, n), smooth01(dn * 5.0));

    return clamp(diffuse * atten, 0.0, 1.0);
}

float lampAmount(vec3 lightDir, float dn, vec3 n) {
    if (cb.pointShadowNear.z < 0.0) return getPointLightAtten(lightDir, dn, normalize(lightDir));
    return cb.pointShadowNear.z > 0.0 ? lampReach(dn) : getPointLightAtten(lightDir, dn, n);
}

float shadowedLight(textureCube cubeTex, sampler samp, vec3 worldPos, vec4 lightPos, vec4 pre, vec3 n) {
    vec3 lightDir = lightPos.xyz - worldPos;
    float dn = length(lightDir) * pre.x;
    float weight = pre.y;
    if (!(dn < 1.0) || weight == 0.0) return 0.0;

    float amount = pointLightAmountValueLod(cubeTex, samp, lightDir * vec3(-1.0, -1.0, 1.0), dn, pre.z) * lampAmount(lightDir, dn, n);
    return clamp(clamp(amount, 0.0, 1.0) * weight, 0.0, 1.0) * edgeWindow(dn);
}

vec4 pointLightDistance(vec3 worldPos, vec3 lightPos, float invRadius) {
    vec3 lightDir = lightPos - worldPos;
    return vec4(lightDir, length(lightDir) * invRadius);
}

float spotLightAmount(vec3 worldPos, vec4 spotPos, vec4 spotDir, vec3 n) {
    vec3 lightToWorld = spotPos.rgb - worldPos;
    vec3 lightVector = normalize(lightToWorld);

    float dn = length(lightToWorld) * cb.nearPre.z;
    float s = clamp(dn * dn, 0.0, 1.0);
    float atten = clamp(((1.0 - s) * (1.0 - s)) / (1.0 + 5.0 * s), 0.0, 1.0);

    float cone = clamp((clamp(dot(spotDir.xyz, lightVector * -1.0), 0.0, 1.0) - cb.spotPre.x) * cb.spotPre.y, 0.0, 1.0);
    cone *= cone;

    float diffuse = max(dot(lightVector, n), 0.0);
    return diffuse * cone * atten;
}
