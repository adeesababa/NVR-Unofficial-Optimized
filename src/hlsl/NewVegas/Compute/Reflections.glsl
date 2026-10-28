#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;
layout(set = 1, binding = 1) uniform texture2D t_depth;
layout(set = 1, binding = 2) uniform texture2D t_normals;
layout(set = 1, binding = 3) uniform texture2D t_viewmodel;
layout(set = 1, binding = 4) uniform texture2D t_trace;
layout(set = 1, binding = 5) uniform texture2D t_buffer;
layout(set = 1, binding = 6) uniform texture2D t_temp;
layout(set = 1, binding = 7) uniform texture2D t_history;
layout(set = 1, binding = 8) uniform textureCube t_capture;
layout(set = 1, binding = 9) uniform textureCube t_captureNew;
layout(set = 1, binding = 10) uniform textureCube t_env;
layout(set = 1, binding = 11) uniform texture2D t_ortho;
layout(set = 1, binding = 12) uniform texture2D t_gridLight;
layout(set = 1, binding = 13) uniform texture2D t_gridColor;
layout(set = 1, binding = 14) uniform texture2D t_skyVis;
layout(set = 1, binding = 15) uniform texture2D t_material;

layout(set = 2, binding = 0, std140) uniform Constants {
    mat4 projectionTransform;
    mat4 viewTransform;
    mat4 shadowOrtho;
    mat4 reproject;
    vec4 reciprocalResolution;
    vec4 skyColor;
    vec4 horizonColor;
    vec4 waterSettings;
    vec4 data;
    vec4 data2;
    vec4 data3;
    vec4 data4;
    vec4 layoutData;
    vec4 cube2;
    vec4 grid;
    vec4 grid2;
    vec4 grid3;
    vec4 material;
    vec4 depthConstants;
    vec4 cameraData;
    vec4 cameraPosition;
    vec4 sky;
    vec4 flags;
} cb;

layout(push_constant) uniform Push {
    vec4 cube;
    uint linearIndex;
    uint pointIndex;
    uint mipIndex;
    uint pass;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

#define LINEAR_S NVR_SAMPLER(pc.linearIndex)
#define POINT_S NVR_SAMPLER(pc.pointIndex)
#define MIP_S NVR_SAMPLER(pc.mipIndex)

#define MaterialOn (cb.flags.x > 0.5)
#define AddOn (cb.flags.y > 0.5)
#define SpecOccOn (cb.flags.z > 0.5)

#define DryReflect cb.data.x
#define Wetness cb.data.y
#define DryRoughness cb.data.z
#define WetRoughness cb.data.w
#define RayLength cb.data2.x
#define Steps cb.data2.y
#define ThicknessScale cb.data2.z
#define NoiseFrame cb.data2.w
#define HistoryWeight cb.data3.x
#define BlurRadius cb.data3.y
#define DebugView cb.data3.z
#define SkyFallback cb.data3.w
#define DrawDistance cb.data4.x
#define RoofMaskOn cb.data4.y
#define LowTexel cb.layoutData.xy
#define CubeFace pc.cube.x
#define CubeSize pc.cube.y
#define CubeMip pc.cube.z
#define CubeReset pc.cube.w
#define CaptureBlend cb.cube2.x
#define CubeFade cb.cube2.y
#define CubeLastMip cb.cube2.z
#define CubeOn cb.cube2.w
#define GridOn cb.grid2.x
const int RefineSteps = 5;
const float BrightestReflection = 16.0;

#define nearZ cb.cameraData.x
#define farZ cb.cameraData.y

float readDepthLod(vec2 coord) { return textureLod(sampler2D(t_depth, POINT_S), coord, 0.0).x * farZ; }

vec3 toWorld(vec2 tex) {
    vec3 v = vec3(cb.viewTransform[0][2], cb.viewTransform[1][2], cb.viewTransform[2][2]);
    v += (1.0 / cb.projectionTransform[0][0] * (2.0 * tex.x - 1.0)) * vec3(cb.viewTransform[0][0], cb.viewTransform[1][0], cb.viewTransform[2][0]);
    v += (-1.0 / cb.projectionTransform[1][1] * (2.0 * tex.y - 1.0)) * vec3(cb.viewTransform[0][1], cb.viewTransform[1][1], cb.viewTransform[2][1]);
    return v;
}

float fmodHlsl(float x, float y) { return x - y * trunc(x / y); }

float GradientNoise(vec2 pixel) {
    pixel += NoiseFrame * 5.588238;
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

vec3 ViewPosition(vec2 uv, float depth) {
    return vec3((uv.x * 2.0 - 1.0) / cb.projectionTransform[0][0], (1.0 - uv.y * 2.0) / cb.projectionTransform[1][1], 1.0) * depth;
}

vec2 ToScreen(vec3 view) {
    return vec2(view.x * cb.projectionTransform[0][0] / view.z * 0.5 + 0.5, 0.5 - view.y * cb.projectionTransform[1][1] / view.z * 0.5);
}

bool IsWeapon(vec2 uv) {
    float viewmodelDepth = textureLod(sampler2D(t_viewmodel, POINT_S), uv, 0.0).x;
    return cb.depthConstants.z == 0.0 ? viewmodelDepth < 0.9 : viewmodelDepth > 0.01;
}

vec3 ViewNormal(vec2 uv) { return normalize(textureLod(sampler2D(t_normals, POINT_S), uv, 0.0).xyz * 2.0 - 1.0); }

vec3 ViewToWorldDir(vec3 v) { return v * mat3(cb.viewTransform); }

const float RoofMargin = 40.0;
float OpenAbove(vec2 uv, float depth) {
    vec4 p = cb.shadowOrtho * vec4(toWorld(uv) * depth, 1.0);
    p.xyz /= p.w;
    if (abs(p.x) > 1.0 || abs(p.y) > 1.0 || p.z < 0.0 || p.z > 1.0) return 1.0;
    float top = textureLod(sampler2D(t_ortho, POINT_S), vec2(p.x * 0.5 + 0.5, p.y * -0.5 + 0.5), 0.0).r;
    vec3 depthAxis = vec3(cb.shadowOrtho[0][2], cb.shadowOrtho[1][2], cb.shadowOrtho[2][2]);
    float perUnit = length(depthAxis) / max(abs(cb.shadowOrtho[3][3]), 1.0e-6);
    return top > p.z - max(RoofMargin * perUnit, 0.0001) ? 1.0 : 0.0;
}

float SurfaceMetal = 0.0;
vec4 MaterialAt(vec2 uv) { return cb.material.x > 0.5 ? textureLod(sampler2D(t_material, POINT_S), uv, 0.0) : vec4(0.0); }

float MaterialReflectivity(vec2 uv, float depth, float floorShare, vec4 material, out float roughness) {
    float worldZ = cb.cameraPosition.z + toWorld(uv).z * depth;
    roughness = max(material.r, 0.02);
    if (floorShare > 0.0 && abs(worldZ - cb.waterSettings.x) < max(2.0, depth * 0.002)) return 0.0;
    float maxRough = cb.material.y;
    float dry = cb.material.z * (1.0 - smoothstep(0.6 * maxRough, maxRough, material.r));
    float wetness = Wetness * floorShare;
    if (wetness > 0.0 && RoofMaskOn > 0.5) wetness *= OpenAbove(uv, depth);
    float amount = dry + wetness;
    roughness = mix(roughness, WetRoughness, wetness / max(amount, 1.0e-4));
    SurfaceMetal = material.g;
    float fade = 1.0 - smoothstep(0.7 * DrawDistance, DrawDistance, depth);
    return min(amount, 2.0) * fade;
}

float Reflectivity(vec2 uv, float depth, vec3 viewNormal, out float roughness) {
    roughness = DryRoughness;
    if (MaterialOn) SurfaceMetal = 0.0;
    if (depth >= DrawDistance || depth >= farZ * 0.98 || IsWeapon(uv)) return 0.0;
    vec3 worldNormal = ViewToWorldDir(viewNormal);
    float floorShare = saturate((worldNormal.z - 0.75) / 0.2);
    if (MaterialOn) {
        vec4 material = MaterialAt(uv);
        if (material.b > 0.5) return MaterialReflectivity(uv, depth, floorShare, material, roughness);
    }
    if (floorShare <= 0.0) return 0.0;
    float worldZ = cb.cameraPosition.z + toWorld(uv).z * depth;
    if (abs(worldZ - cb.waterSettings.x) < max(2.0, depth * 0.002)) return 0.0;
    float wetness = Wetness;
    if (wetness > 0.0 && RoofMaskOn > 0.5) wetness *= OpenAbove(uv, depth);
    float amount = DryReflect + wetness;
    roughness = mix(DryRoughness, WetRoughness, wetness / max(amount, 1.0e-4));
    float fade = 1.0 - smoothstep(0.7 * DrawDistance, DrawDistance, depth);
    return min(amount, 2.0) * floorShare * fade;
}

float EnvironmentFresnel(float roughness, float NdotV) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
    return 0.04 * ab.x + ab.y;
}

vec3 EnvironmentFresnelF0(float roughness, float NdotV, vec3 F0) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
    return F0 * ab.x + ab.y;
}

vec3 SkyAlong(vec3 worldDir) {
    float up = saturate(worldDir.z);
    return mix(linearize(cb.horizonColor.rgb), linearize(cb.skyColor.rgb), sqrt(up)) * smoothstep(0.0, 0.15, worldDir.z);
}

vec3 Tame(vec3 c) { return c / (1.0 + dot(c, vec3(0.2126, 0.7152, 0.0722))); }
vec3 Untame(vec3 t) { return t / max(1.0 - dot(t, vec3(0.2126, 0.7152, 0.0722)), 1.0e-3); }

vec3 FaceDirection(float face, vec2 st) {
    if (face < 0.5) return vec3(1.0, -st.y, -st.x);
    if (face < 1.5) return vec3(-1.0, -st.y, st.x);
    if (face < 2.5) return vec3(st.x, 1.0, st.y);
    if (face < 3.5) return vec3(st.x, -1.0, -st.y);
    if (face < 4.5) return vec3(st.x, -st.y, 1.0);
    return vec3(-st.x, -st.y, -1.0);
}

vec3 WorldToViewDir(vec3 w) { return (cb.viewTransform * vec4(w, 0.0)).xyz; }

vec4 EnvAlong(vec3 worldDir, float roughness) {
    float lod = roughness * CubeLastMip;
    vec4 sharp = textureLod(samplerCube(t_env, MIP_S), worldDir, lod);
    vec4 coarse = textureLod(samplerCube(t_env, MIP_S), worldDir, min(lod + 2.0, CubeLastMip));
    vec4 m = sharp + coarse * (1.0 - saturate(sharp.a));
    return vec4(m.rgb / max(m.a, 1.0e-4), saturate(m.a));
}

const float GridX = 32.0, GridY = 32.0, GridZ = 16.0;
const float GridAlbedo = 0.4;
const float GridIrradianceScale = 1.5;
vec4 GridShBasis(vec3 d) { return vec4(0.282095, 0.488603 * d); }
vec2 GridCellUV(vec3 c) { return (vec2(fmodHlsl(c.z, 4.0), floor(c.z / 4.0)) * 32.0 + c.xy + 0.5) / vec2(256.0, 128.0); }

float CoveredAt(vec3 p) {
    vec4 m = cb.shadowOrtho * vec4(p, 1.0);
    m.xyz /= m.w;
    if (abs(m.x) > 1.0 || abs(m.y) > 1.0 || m.z < 0.0 || m.z > 1.0) return 0.0;
    float top = textureLod(sampler2D(t_ortho, POINT_S), vec2(m.x * 0.5 + 0.5, m.y * -0.5 + 0.5), 0.0).r;
    vec3 depthAxis = vec3(cb.shadowOrtho[0][2], cb.shadowOrtho[1][2], cb.shadowOrtho[2][2]);
    float perUnit = length(depthAxis) / max(abs(cb.shadowOrtho[3][3]), 1.0e-6);
    return top < m.z - max(RoofMargin * perUnit, 0.0001) ? 1.0 : 0.0;
}

vec4 GridAlong(vec3 start, vec3 worldR) {
    float size = cb.grid.w;
    vec3 dims = vec3(GridX, GridY, GridZ);
    vec4 toward = GridShBasis(-worldR);
    float light = 0.0, weight = 0.0, inside = 0.0, covered = 0.0;
    vec4 colour = vec4(0.0);
    bool open = true;
    for (float k = 0.0; k < 3.5; k += 1.0) {
        float t = size * (k < 0.5 ? 0.75 : exp2(k));
        vec3 p = start + worldR * t;
        vec3 g = (p - cb.grid.xyz) / size - 0.5;
        vec3 edge = min(g + 0.5, dims - 0.5 - g);
        float fade = saturate(min(min(edge.x, edge.y), edge.z) / 2.0);
        if (open && fade > 0.0) {
            g = clamp(g, vec3(0.0), dims - 1.0);
            float z0 = floor(g.z), z1 = min(z0 + 1.0, GridZ - 1.0);
            vec4 sh = mix(textureLod(sampler2D(t_gridLight, LINEAR_S), GridCellUV(vec3(g.xy, z0)), 0.0),
                textureLod(sampler2D(t_gridLight, LINEAR_S), GridCellUV(vec3(g.xy, z1)), 0.0), g.z - z0);
            light += max(dot(sh, toward), 0.0) * fade;
            if (cb.grid2.z > 0.5) colour += textureLod(sampler2D(t_gridColor, LINEAR_S), GridCellUV(vec3(g.xy, floor(g.z + 0.5))), 0.0) * fade;
            weight += fade;
            inside = max(inside, fade);
            if (RoofMaskOn > 0.5) {
                float c = CoveredAt(p);
                covered = max(covered, c * fade);
                open = c < 0.5;
            }
        }
    }
    if (weight <= 0.0) return vec4(0.0);
    vec3 hue = vec3(1.0);
    if (colour.a > 1.0e-5) {
        hue = max(colour.rgb / colour.a, 0.0);
        hue = min(hue / max(luma(hue), 1.0e-4), 3.0);
    }
    vec3 radiance = (light / weight * GridIrradianceScale + cb.grid3.rgb * GridAlbedo) * hue * cb.grid2.y;
    float share = inside;
    if (SkyFallback > 0.0) share *= max(covered, 1.0 - smoothstep(0.0, 0.15, worldR.z));
    return vec4(radiance, share);
}
