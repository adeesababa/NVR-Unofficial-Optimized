float3 DiffuseDir   : register(c0);
float3 DiffuseColor : register(c1);
float3 ScaleMask    : register(c2);
float4 WindData     : register(c4);
float4 AlphaParam   : register(c5);
float4 AmbientColor : register(c6);
float4 AddlParams   : register(c7);
row_major float4x4 ModelViewProj : register(c9);
float4 FogColor     : register(c14);
float4 FogParam     : register(c15);

#ifndef GRASS_INSTANCE_COUNT
    #define GRASS_INSTANCE_COUNT 228
#endif
float4 InstanceData[GRASS_INSTANCE_COUNT] : register(c20);

float4 NG_Lod   : register(c248);
float4 NG_Shape : register(c249);
float4 NG_Misc  : register(c250);
float4 NG_Var   : register(c251);
float4 NG_Prof  : register(c254);
float4 NG_Prof2 : register(c255);

struct VS_INPUT {
    float4 packed : TEXCOORD0;
};

struct VS_OUTPUT {
    float4 position : POSITION;
    float4 clip     : TEXCOORD0;
    float4 params   : TEXCOORD1;
    float4 normal   : TEXCOORD2;
    float4 sunDir   : TEXCOORD3;
    float4 ambient  : TEXCOORD4;
    float4 sun      : TEXCOORD5;
    float4 up       : TEXCOORD6;
    float4 fog      : COLOR0;
};

float Hash(float2 p) {
    return frac(sin(dot(p, float2(12.9898f, 78.233f))) * 43758.5453f);
}

VS_OUTPUT main(VS_INPUT IN) {
    VS_OUTPUT OUT;

    int idx = (int)(IN.packed.x + 0.5f);
    float4 inst = InstanceData[idx];
    float blade = IN.packed.y;
    float t = IN.packed.z * NG_Var.x;
    float side = IN.packed.w * 2.0f - 1.0f;

    float4 rootClip = mul(ModelViewProj, float4(inst.xyz, 1.0f));
    float depth = rootClip.w;
    float instDist = length(float3(rootClip.xy, rootClip.w));

    float budget = lerp(NG_Misc.y, NG_Prof2.y > 0.5f ? 0.0f : 1.0f, saturate((depth - NG_Lod.x) * NG_Lod.y));
    float keep = saturate(budget - blade);
    bool gone = IN.packed.x >= NG_Misc.w || keep <= 0.0f || (NG_Var.y > 0.5f && instDist > AlphaParam.z);
    [branch] if (gone) {
        VS_OUTPUT none = (VS_OUTPUT)0;
        none.position = float4(2.0f, 2.0f, 0.5f, 1.0f);
        return none;
    }

    float4 f = frac(inst);
    float lightScale = f.w * 0.75f + 0.25f;
    float3 orient = normalize((f.xyz - 0.5f) * 2.0f);
    float3 up = NG_Misc.x > 0.5f ? normalize(float3(0.0f, 0.0f, 1.0f) + orient) : float3(0.0f, 0.0f, 1.0f);

    float2 key = inst.xy * 0.03125f + blade * float2(7.13f, 2.3f);
    float h1 = Hash(key);
    float h2 = Hash(key + 17.17f);
    float h3 = Hash(key.yx + 17.17f);
    float h4 = Hash(key.yx * 1.5f + 7.13f);

    float3 scale = (0.01f * inst.w) * ScaleMask.xyz + 1.0f;
    float plantH = NG_Shape.x * scale.z * NG_Misc.z;
    float halfW = NG_Shape.y * scale.x;
    float len = lerp(NG_Var.w, 1.0f, h1);
    float height = plantH * len * keep;
    float hb = len * 4.0f - 0.5f;
    float rsil = hb < 1.0f ? lerp(NG_Prof.x, NG_Prof.y, saturate(hb)) : hb < 2.0f ? lerp(NG_Prof.y, NG_Prof.z, hb - 1.0f) : lerp(NG_Prof.z, NG_Prof.w, saturate(hb - 2.0f));
    float rTip = halfW * rsil * NG_Prof2.z * (0.5f + 0.5f * h3);
    float ang = h2 * 6.2831853f;
    float3 away = float3(cos(ang), sin(ang), 0.0f);
    float3 outDir = normalize(away - up * dot(away, up));
    float3 base = inst.xyz + outDir * (halfW * NG_Prof2.x * h4);
    float3 sideVec = cross(up, outDir);

    float phase = (inst.x + inst.y) * 0.0078125f + WindData.w;
    phase = frac(phase * 0.159154937f + 0.5f) * 6.28318548f - 3.14159274f;
    float swing = sin(phase + h1 * 1.5f);
    float3 wind = float3(WindData.xy * (swing * WindData.z), 0.0f)
                + sideVec * (sin(phase * 2.3f + h2 * 6.2831853f) * WindData.z * NG_Var.z);

    float lean = rTip / max(height, 1.0f);
    float droop = NG_Lod.w * saturate(lean - 0.5f) * height;
    float3 p0 = base;
    float3 p2 = base + up * (height - droop) + outDir * rTip + wind;
    float3 p1 = base + up * (height * 0.6f) + outDir * (rTip * NG_Shape.w) + wind * 0.3f;
    float u = 1.0f - t;
    float3 pos = (u * u) * p0 + (2.0f * u * t) * p1 + (t * t) * p2;
    float3 tangent = normalize(2.0f * u * (p1 - p0) + 2.0f * t * (p2 - p1));

    float width = plantH * NG_Shape.z * (0.6f + 0.6f * h4) * (1.0f + depth * NG_Lod.z) * (1.0f - t * t) * keep;
    pos += sideVec * (side * 0.5f * width + NG_Prof2.w * width * sin(t * 9.0f + h2 * 6.2831853f) * (0.5f + 0.5f * h3));

    float3 normal = normalize(cross(sideVec, tangent));
    normal = normalize(normal + sideVec * (side * 0.3f));

    OUT.position = mul(ModelViewProj, float4(pos, 1.0f));
    OUT.clip = OUT.position;
    OUT.params = float4(t * len, IN.packed.w, h4, lightScale);
    OUT.normal = float4(normal, AddlParams.x);
    OUT.sunDir = float4(DiffuseDir, 0.0f);
    OUT.ambient = float4(AmbientColor.rgb, 1.0f);
    OUT.sun = float4(DiffuseColor, 1.0f);
    OUT.up = float4(orient, 0.0f);

    float fogDist = length(float3(OUT.position.xy, OUT.position.w - OUT.position.z));
    float fogStrength = 1.0f - saturate((FogParam.x - fogDist) / FogParam.y);
    OUT.fog.rgb = FogColor.rgb;
    OUT.fog.w = pow(fogStrength, FogParam.z);

    return OUT;
};
