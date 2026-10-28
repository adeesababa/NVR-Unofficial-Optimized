
layout(set = 2, binding = 0, std140) uniform Constants {
    mat4 prevViewProj;
    mat4 viewTransform;
    mat4 projectionTransform;
    vec4 reciprocalResolution;
    vec4 sunDirection;
    vec4 sunColor;
    vec4 sunAmount;
    vec4 skyColor;
    vec4 horizonColor;
    vec4 shape;
    vec4 noise;
    vec4 light;
    vec4 wind;
    vec4 march;
    vec4 cloudLayout;
    vec4 blocks;
    vec4 look;
    vec4 detail;
    vec4 extra;
    vec4 motion;
    vec4 waterSettings;
    vec4 cameraData;
    vec4 cameraPosition;
    vec4 tint;
    vec4 tower;
    vec4 physSky;
    vec4 physSkySun;
} cb;

layout(push_constant) uniform Push {
    uint pointIndex;
    uint linearIndex;
    uint pointWrapIndex;
    uint mipIndex;
    uint fixedExit;
    uint physicalSky;
    uint towers;
    uint prefilter;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

#define POINT_LOD(t, coord) textureLod(sampler2D(t, NVR_SAMPLER(pc.pointIndex)), (coord), 0.0)
#define LINEAR_LOD(t, coord) textureLod(sampler2D(t, NVR_SAMPLER(pc.linearIndex)), (coord), 0.0)

#define Base (cb.shape.x)
#define Thickness max(cb.shape.y, 10.0)
#define Coverage saturate(cb.shape.z)
#define Density (cb.shape.w)
#define ShapeTile max(cb.noise.x, 1.0)
#define DetailTile max(cb.noise.y, 1.0)
#define Erosion saturate(cb.noise.z)
#define WeatherTile max(cb.noise.w, 1.0)
#define SunStrength (cb.light.x)
#define AmbientStrength (cb.light.y)
#define Forward (cb.light.z)
#define RainDark saturate(cb.light.w)
#define Steps max(cb.march.x, 4.0)
#define MaxDistance (cb.march.y)
#define HazeDistance max(cb.march.w, 1.0)
#define LowTexel (cb.cloudLayout.xy)
#define Sharpness max(cb.motion.w, 0.0)
#define Cumulus (saturate(cb.detail.w) * (1.0 - smoothstep(0.6, 0.9, saturate(cb.shape.z))))
#define TowerShare (cb.tower.x > 0.0 ? clamp(cb.tower.x, 0.05, 1.0) : 1.0)
#define InvTowerShare (1.0 / TowerShare)
#define TowerGrow (InvTowerShare - 1.0)
#define Towering saturate(cb.tower.y)
#define Anvil saturate(cb.tower.z)
#define HeapGrow min(TowerGrow, 1.5)
#define StormSpacing (1.5 * WeatherTile)
#define StormRadius (0.55 * Thickness)
#define BaseThickness (Thickness * TowerShare)

const float EarthRadius = 6371000.0;
const float MetresPerUnit = 1.0 / 70.0;
const float Extinction = 0.04;

#define ShadowStrength (cb.extra.x)
#define ShadowRange max(cb.extra.y, 1000.0)
#define ShadowMapSize max(cb.detail.z, 256.0)
#define farZ (cb.cameraData.y)

vec3 toWorld(vec2 tex) {
    vec3 v = vec3(cb.viewTransform[0][2], cb.viewTransform[1][2], cb.viewTransform[2][2]);
    v += (1.0 / cb.projectionTransform[0][0] * (2.0 * tex.x - 1.0)) * vec3(cb.viewTransform[0][0], cb.viewTransform[1][0], cb.viewTransform[2][0]);
    v += (-1.0 / cb.projectionTransform[1][1] * (2.0 * tex.y - 1.0)) * vec3(cb.viewTransform[0][1], cb.viewTransform[1][1], cb.viewTransform[2][1]);
    return v;
}

float Remap(float v, float lo, float hi, float newLo, float newHi) { return newLo + (v - lo) / max(hi - lo, 1e-5) * (newHi - newLo); }

float ShellDistance(float dz, float above) {
    return EarthRadius * (-dz + sqrt(dz * dz + 2.0 * above / EarthRadius));
}

vec2 ShadowCentre() {
    float texel = ShadowRange / ShadowMapSize;
    return floor(cb.cameraPosition.xy * MetresPerUnit / texel) * texel;
}

vec3 ShadowLight() {
    vec3 light = normalize(cb.sunDirection.xyz);
    light.z = max(light.z, 0.25);
    return normalize(light);
}

#ifdef CLOUDS_DENSITY
#define MIP_LOD2(t, coord, lod) textureLod(sampler2D(t, NVR_SAMPLER(pc.mipIndex)), (coord), (lod))
#define MIP_LOD3(t, coord, lod) textureLod(sampler3D(t, NVR_SAMPLER(pc.mipIndex)), (coord), (lod))

float HeightProfile(float h, float hb, float type) {
    float top = mix(0.3, 1.0, type);
    float bottom = mix(0.08, 0.12, type);
    return smoothstep(0.0, bottom, hb) * (1.0 - smoothstep(top * 0.6, top, h));
}

float CloudDensity(vec3 p, float h, float lod, bool cheap, bool towers) {
    if (h <= 0.0 || h >= 1.0) return 0.0;
    vec4 weather = MIP_LOD2(t_weather, p.xy / WeatherTile, 0.0);
    float local = saturate(Remap(weather.r, 1.0 - Coverage, 1.0, 0.0, 1.0));
    float type = saturate(weather.g + cb.wind.w);
    float tower = 0.0, hb = h, storm = 0.0;
    if (towers) {
        tower = saturate(local * type * 4.0 - 0.6) * Towering;
        hb = h * InvTowerShare;
        if (hb * Towering > 0.0) {
            vec2 cell = floor(p.xy / StormSpacing);
            vec4 r = MIP_LOD3(t_noise, vec3((cell + 0.5) / 64.0, 0.5 / 64.0), 0.0);
            float exists = saturate((0.75 * Towering - r.a) * 8.0);
            vec2 centre = (cell + 0.3 + 0.4 * r.gb) * StormSpacing;
            float radius = StormRadius * (0.75 + 0.5 * r.r);
            float flare = smoothstep(0.72, 0.97, h);
            flare *= flare * Anvil;
            vec2 shear = (r.bg - 0.5) * (2.0 * radius * flare);
            float width = radius * mix(1.0 - 0.1 * h, 2.2, flare);
            float d = length(p.xy - centre - shear) / width;
            if (d < 1.6) {
                vec4 n = MIP_LOD3(t_noise, p / (2.0 * width), 0.0);
                d += (0.5 - n.b) * 0.6 + (0.5 - n.g) * 0.4;
            }
            storm = saturate((1.0 - d) * 3.0) * exists * smoothstep(0.0, 0.03, hb);
        }
    }
    if (local <= 0.0 && storm <= 0.0) return 0.0;
    if (towers) {
        h = hb / (1.0 + tower * HeapGrow);
        if (h >= 1.0 && storm <= 0.0) return 0.0;
        type += tower * (1.0 - type);
    }
    vec3 s = p / ShapeTile;
    s += (MIP_LOD3(t_noise, s * 0.5, lod).rga - 0.5) * (0.15 * cb.look.z * mix(1.0, 0.4, Cumulus));
    float shape = MIP_LOD3(t_shape, s, lod + cb.look.y).r;
    float base = saturate(Remap(shape * HeightProfile(h, hb, type), 1.0 - local, 1.0, 0.0, 1.0)) * local;
    float stormCloud = towers ? saturate((storm - 0.5 * (1.0 - shape)) * 2.0) : 0.0;
    {
        float top = mix(0.45, 1.0, type);
        float rise = saturate(h / top);
        float threshold = mix(1.0 - local, 1.0, 0.9 * rise * (1.0 - 0.5 * tower));
        float core = saturate(Remap(shape, threshold, 1.0, 0.0, 1.0));
        core *= smoothstep(0.0, 0.03, hb) * (1.0 - smoothstep(0.85, 1.0, rise));
        core = saturate(Remap(core, 0.1, 1.0, 0.0, 1.0)) * local;
        base = mix(base, core, Cumulus);
    }
    if (towers) base = max(base, stormCloud);
    if (base <= 0.0) return 0.0;
    if (!cheap) {
        vec3 q = (p + vec3(0.0, 0.0, cb.wind.z)) / DetailTile;
        vec4 warp = MIP_LOD3(t_noise, q * 0.125, lod);
        q += (warp.rga - 0.5) * cb.look.z * mix(1.0, 0.5, Cumulus);
        float detail = mix(MIP_LOD3(t_detail, q, lod).r, warp.g, cb.look.w);
        if (cb.detail.x > 0.0) {
            vec3 f = (p + vec3(0.0, 0.0, 1.7 * cb.wind.z)) / max(cb.detail.y, 1.0) + (warp.gar - 0.5) * cb.look.z;
            float fine = MIP_LOD3(t_detail, f, lod).r;
            detail = mix(detail, detail * 0.65 + fine * 0.35, cb.detail.x);
        }
        float wisps = mix(detail, 1.0 - detail, saturate(hb * 6.0));
        float erosion = Erosion;
        wisps = mix(wisps, 1.0 - detail, Cumulus);
        erosion *= mix(1.0, mix(0.35, 1.0, smoothstep(0.0, 0.2, hb)), Cumulus);
        base = saturate(Remap(base, wisps * erosion, 1.0, 0.0, 1.0));
    }
    base = saturate(base * max(cb.look.x, 1.0) * (1.0 + 0.5 * Cumulus));
    return base * Density * mix(0.6, 1.0, weather.b);
}
#endif

#ifdef CLOUDS_HISTORY
vec4 CatmullRom(vec2 coord) {
    vec2 size = 1.0 / LowTexel;
    vec2 position = coord * size;
    vec2 centre = floor(position - 0.5) + 0.5;
    vec2 f = position - centre;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 offset12 = w2 / w12;
    vec2 t0 = (centre - 1.0) * LowTexel, t3 = (centre + 2.0) * LowTexel, t12 = (centre + offset12) * LowTexel;
    vec4 result = vec4(0.0);
    result += LINEAR_LOD(t_history, vec2(t0.x, t0.y)) * w0.x * w0.y;
    result += LINEAR_LOD(t_history, vec2(t12.x, t0.y)) * w12.x * w0.y;
    result += LINEAR_LOD(t_history, vec2(t3.x, t0.y)) * w3.x * w0.y;
    result += LINEAR_LOD(t_history, vec2(t0.x, t12.y)) * w0.x * w12.y;
    result += LINEAR_LOD(t_history, vec2(t12.x, t12.y)) * w12.x * w12.y;
    result += LINEAR_LOD(t_history, vec2(t3.x, t12.y)) * w3.x * w12.y;
    result += LINEAR_LOD(t_history, vec2(t0.x, t3.y)) * w0.x * w3.y;
    result += LINEAR_LOD(t_history, vec2(t12.x, t3.y)) * w12.x * w3.y;
    result += LINEAR_LOD(t_history, vec2(t3.x, t3.y)) * w3.x * w3.y;
    return vec4(max(result.rgb, vec3(0.0)), saturate(result.a));
}
#endif
