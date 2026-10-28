#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 2, binding = 0, std140) uniform Constants {
    mat4 projectionTransform;
    mat4 invProjectionTransform;
    mat4 viewTransform;
    mat4 reproject;
    mat4 gridSunToGrid;
    mat4 gridToSun;
    vec4 data;
    vec4 data2;
    vec4 reciprocalResolution;
    vec4 alLayout;
    vec4 alFlags;
    vec4 alInterior;
    vec4 alScene;
    vec4 alTemporal;
    vec4 alEdges;
    vec4 waterSettings;
    vec4 shadowData;
    vec4 shadowFade;
    vec4 skyColor;
    vec4 sunAmbient;
    vec4 sunDirection;
    vec4 aoSky5;
    vec4 aoData;
    vec4 depthConstants;
    vec4 cameraData;
    vec4 cameraPosition;
    vec4 sunFarFlags;
    vec4 gridOrigin;
    vec4 gridOrigin1;
    vec4 gridData;
    vec4 gridLight;
    vec4 gridStep;
    vec4 gridShift;
    vec4 gridScreen;
    vec4 gridWalls;
    vec4 gridLampInfo;
    vec4 gridLamps[32];
    vec4 gridLampColors[32];
} cb;

layout(push_constant) uniform Push {
    vec4 frame;
    uint pointClamp;
    uint linearClamp;
    uint pointWrap;
    uint variant;
#ifdef NVR_GI
    vec4 gi;
#endif
} pc;

#define POINT_S  NVR_SAMPLER(pc.pointClamp)
#define LINEAR_S NVR_SAMPLER(pc.linearClamp)

vec2 frameUV() { return floor(gl_FragCoord.xy) * pc.frame.xy + pc.frame.zw; }

vec4 pointLod(texture2D t, vec2 uv) { return textureLod(sampler2D(t, POINT_S), uv, 0.0); }
vec4 linearLod(texture2D t, vec2 uv) { return textureLod(sampler2D(t, LINEAR_S), uv, 0.0); }

float hlslFmod(float x, float y) { return x - y * trunc(x / y); }

#define Radius            max(cb.data.x, 1.0)
#define OcclusionStrength (cb.data.y)
#define BounceStrength    (cb.data.z)
#define DrawDistance      max(cb.data.w, 100.0)
#define DarkestAO         saturate(cb.data2.x)
#define BrightFade        (cb.data2.y)
#define BlurStep          max(cb.data2.z, 0.5)
#define DebugView         (cb.data2.w)
const int SLICES = 2;
const int STEPS = 4;
const float BounceScale = 2.5;
const float NearFade = 40.0;
const float MaxScreenRadius = 0.08;
const float endFade = 8000.0;

#define nearZ (cb.cameraData.x)
#define farZ  (cb.cameraData.y)

float projectedDepthFromLinear(float linear01) {
    float viewZ = max(linear01 * farZ, 1.0e-6);
    return nearZ * (farZ - viewZ) / (viewZ * (farZ - nearZ));
}

float readDepthLod(texture2D depthTex, vec2 coord) { return pointLod(depthTex, coord).x * farZ; }

vec3 projectPosition(vec3 position) {
    vec4 projection = cb.projectionTransform * vec4(position, 1.0);
    projection.xyz /= projection.w;
    projection.x = projection.x * 0.5 + 0.5;
    projection.y = 0.5 - 0.5 * projection.y;
    return projection.xyz;
}

vec3 toWorld(vec2 tex) {
    vec3 v = vec3(cb.viewTransform[0][2], cb.viewTransform[1][2], cb.viewTransform[2][2]);
    v += (1.0 / cb.projectionTransform[0][0] * (2.0 * tex.x - 1.0)) * vec3(cb.viewTransform[0][0], cb.viewTransform[1][0], cb.viewTransform[2][0]);
    v += (-1.0 / cb.projectionTransform[1][1] * (2.0 * tex.y - 1.0)) * vec3(cb.viewTransform[0][1], cb.viewTransform[1][1], cb.viewTransform[2][1]);
    return v;
}

vec3 GetWorldNormalLod(texture2D normalsTex, vec2 uv) {
    vec4 normal = vec4(pointLod(normalsTex, uv).xyz * 2.0 - 1.0, 1.0);
    return (normal * cb.viewTransform).xyz;
}

vec3 SkyTake(vec3 linearColor, float factor, float visibility, float cap, float noLimit) {
    float take = min(1.0 - factor, ((1.0 - visibility) * cap + noLimit) / max(luma(linearColor), 1.0e-6));
    return linearColor * (1.0 - take);
}

bool IsViewModel(texture2D viewModelTex, vec2 uv) {
    float d = pointLod(viewModelTex, uv).x;
    return (cb.depthConstants.z == 0.0 && d < 0.9) || (cb.depthConstants.z > 0.0 && d > 0.01);
}

vec3 PositionFromLinear(vec2 uv, float linear01) {
    float z = projectedDepthFromLinear(linear01);
    vec4 viewSpace = cb.invProjectionTransform * vec4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, z, 1.0);
    return viewSpace.xyz / viewSpace.w;
}

vec3 PositionFromViewZ(vec2 uv, float z) {
    return vec3((uv.x * 2.0 - 1.0) / cb.projectionTransform[0][0], (1.0 - uv.y * 2.0) / cb.projectionTransform[1][1], 1.0) * z;
}

float Pack2(vec2 v) { v = floor(clamp(v, 0.0, 1.0) * 4095.0 + 0.5); return v.x * 4096.0 + v.y; }
vec2 Unpack2(float p) { float high = floor(p / 4096.0); return vec2(high, p - high * 4096.0) / 4095.0; }
vec2 OctEncode(vec3 n) {
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    vec2 s = vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    vec2 e = n.z >= 0.0 ? n.xy : (1.0 - abs(n.yx)) * s;
    return e * 0.5 + 0.5;
}
vec3 OctDecode(vec2 e) {
    e = e * 2.0 - 1.0;
    vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
    float t = saturate(-n.z);
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
}

const vec3 GridGroundColor = vec3(0.55, 0.42, 0.30);
const float GridX = 32.0, GridY = 32.0, GridZ = 16.0;
const float AtlasSize = 128.0;
const vec2 AtlasDims = vec2(256.0, 128.0);
const float SunMapSize = 256.0;
const int GridLampsMax = 32;
const float GridLampReflectance = 0.4;

vec4 CosineLobe(vec3 n) { return vec4(0.886227, 1.023328 * n); }
vec4 ShBasis(vec3 d) { return vec4(0.282095, 0.488603 * d); }

vec2 CellUV(vec3 c, float level) {
    float tile = c.z;
    vec2 t = vec2(hlslFmod(tile, 4.0), floor(tile / 4.0));
    return (t * 32.0 + c.xy + 0.5 + vec2(level * AtlasSize, 0.0)) / AtlasDims;
}
vec2 CellUV(vec3 c) { return CellUV(c, 0.0); }

vec3 AtlasCell(vec2 uv) {
    vec2 texel = floor(uv * AtlasDims);
    texel.x -= texel.x >= AtlasSize ? AtlasSize : 0.0;
    vec2 t = floor(texel / 32.0);
    return vec3(texel - t * 32.0, t.y * 4.0 + t.x);
}

vec4 GridLevel(texture2D accumTex, texture2D accumColorTex, vec3 cameraRelative, vec3 normal, vec4 origin, float level, float edgeCells,
    out vec4 colourLight, out float fade) {
    float size = origin.w;
    vec3 g = (cameraRelative - origin.xyz + normal * (0.5 * size)) / size - 0.5;
    vec3 dims = vec3(GridX, GridY, GridZ);
    vec3 edge = min(g + 0.5, dims - 0.5 - g);
    fade = saturate(min(min(edge.x, edge.y), edge.z) / edgeCells);
    colourLight = vec4(0.0);
    if (fade <= 0.0) return vec4(0.0);
    g = clamp(g, vec3(0.0), dims - 1.0);
    float z0 = floor(g.z);
    float z1 = min(z0 + 1.0, GridZ - 1.0);
    float fz = g.z - z0;
    vec4 a = linearLod(accumTex, CellUV(vec3(g.xy, z0), level));
    vec4 b = linearLod(accumTex, CellUV(vec3(g.xy, z1), level));
    if (cb.gridData.w > 0.5) {
        vec4 ca = linearLod(accumColorTex, CellUV(vec3(g.xy, z0), level));
        vec4 cb_ = linearLod(accumColorTex, CellUV(vec3(g.xy, z1), level));
        colourLight = mix(ca, cb_, fz);
    }
    return mix(a, b, fz);
}

vec3 GridBounce(texture2D accumTex, texture2D accumColorTex, vec3 cameraRelative, vec3 normal) {
    vec4 c, cFine;
    float fade, fadeFine;
    vec4 sh = GridLevel(accumTex, accumColorTex, cameraRelative, normal, cb.gridOrigin, 0.0, 2.0, c, fade);
    if (cb.gridOrigin1.w > 0.0) {
        vec4 shFine = GridLevel(accumTex, accumColorTex, cameraRelative, normal, cb.gridOrigin1, 1.0, 4.0, cFine, fadeFine);
        sh = mix(sh, shFine, fadeFine);
        c = mix(c, cFine, fadeFine);
        fade = max(fade, fadeFine);
    }
    if (fade <= 0.0) return vec3(0.0);
    vec3 colour = vec3(1.0);
    if (cb.gridData.w > 0.5) {
        colour = c.a > 1e-5 ? mix(vec3(1.0), c.rgb / c.a, cb.alScene.w) : vec3(1.0);
    }
    float irradiance = max(dot(sh, CosineLobe(-normal)), 0.0) * 1.5;
    return irradiance * colour * fade;
}

const float MaxReflectance = 0.6;
vec3 SurfaceColour(vec3 lin) {
    vec3 ambient = max(cb.alScene.rgb, vec3(0.25 * luma(cb.alScene.rgb) + 1.0e-4));
    vec3 raw = max(lin, vec3(0.0)) / ambient;
    float level = luma(raw);
    vec3 hue = raw / max(level, 1.0e-4);
    float trust = saturate(MaxReflectance / max(level, 1.0e-4));
    return min(level, MaxReflectance) * min(mix(vec3(1.0), hue, trust), vec3(3.0));
}
