// UNOFFICIAL 2026-10-27 displacement shadows ([Shaders.Terrain.Displacement] CastShadows, core/DisplaceShadows.h; plan step 2 of
// docs/next/FORK2-DISPLACE.md). Loaded only with [Main.Main.Performance] NvrDxvkDisplacement on at startup and the NVR DXVK
// tessellation interface present.
//
// The land drawn into the sun's shadow cascades through the land's own tessellation (Compute/Tess/TerrainDisplace.tesc and the
// SHADOW_CASTER variants of TerrainDisplace.tese), so the maps hold the same bumps the camera sees. The outputs are TerrainTemplate's
// vertex shader's, at the same places (the TCS hands them on as they are), with these differences:
//  - TEXCOORD6 (and the position) is the shadow map's clip position, pancaked as ShadowMap.vso does;
//  - TEXCOORD7 is the MAIN camera's eye in object space (the TCS measures the splitting, the distance fade and the quadrant borders
//    from it, so they are the main view's, point for point, and not the light's);
//  - the free w of TEXCOORD3/4/5/7 hold the shadow map's ModelViewProj times the normal (the TES moves the clip position by it);
//  - c0-c3 is last frame's main-camera ModelViewProj (ObjectBlend reads last frame's depth copy with it; the TES finds it through the
//    registration's constant indices, so this shader must use all four rows: they go out in TEXCOORD0.zw, TEXCOORD1.w, TEXCOORD2.w,
//    which nothing reads).
// No TESR_ constants: core/DisplaceShadows.h sets c0-c8 itself for every land piece.

row_major float4x4 NvrMainModelViewProj : register(c0);	// last frame's main camera (rows: the clip position's components)
row_major float4x4 NvrShadowModelViewProj : register(c4);	// this cascade (rows: the clip position's components)
float4 NvrMainEye : register(c8);							// the main camera, object space (xyz)

struct VS_INPUT {
    float4 position : POSITION;
    float3 tangent : TANGENT;
    float3 binormal : BINORMAL;
    float3 normal : NORMAL;
    float4 uv : TEXCOORD0;
    float4 vertex_color : COLOR0;
    float4 blend_0 : TEXCOORD1;
    float4 blend_1 : TEXCOORD2;
};

struct VS_OUTPUT {
    float4 blend_0 : COLOR0;
    float4 blend_1 : COLOR1;
    float4 position : POSITION;
    float4 uv : TEXCOORD0;
    float4 vertex_color : TEXCOORD1;
    float4 lPosition : TEXCOORD2;
    float4 tangent : TEXCOORD3;
    float4 binormal : TEXCOORD4;
    float4 normal : TEXCOORD5;
    float4 projectionPosition : TEXCOORD6;
    float4 viewPosition : TEXCOORD7;
};

VS_OUTPUT main(VS_INPUT IN) {
    VS_OUTPUT OUT;

    const float4x4 mainMvp = float4x4(NvrMainModelViewProj[0], NvrMainModelViewProj[1], NvrMainModelViewProj[2], NvrMainModelViewProj[3]);
    const float4x4 shadowMvp = float4x4(NvrShadowModelViewProj[0], NvrShadowModelViewProj[1], NvrShadowModelViewProj[2], NvrShadowModelViewProj[3]);
    const float4 mainClip = mul(mainMvp, IN.position);
    float4 shadowClip = mul(shadowMvp, IN.position);
    shadowClip.z = max(shadowClip.z, 0.0f);	// pancaking, as ShadowMap.vso
    const float4 shadowNormal = float4(dot(shadowMvp[0].xyz, IN.normal), dot(shadowMvp[1].xyz, IN.normal),
        dot(shadowMvp[2].xyz, IN.normal), dot(shadowMvp[3].xyz, IN.normal));

    OUT.blend_0 = IN.blend_0;
    OUT.blend_1 = IN.blend_1;
    OUT.position = shadowClip;
    OUT.uv = float4(IN.uv.xy, mainClip.xy);
    OUT.vertex_color = float4(saturate(IN.vertex_color.rgb), mainClip.z);
    OUT.lPosition = float4(IN.position.xyz, mainClip.w);
    OUT.tangent = float4(IN.tangent, shadowNormal.x);
    OUT.binormal = float4(IN.binormal, shadowNormal.y);
    OUT.normal = float4(IN.normal, shadowNormal.z);
    OUT.projectionPosition = shadowClip;
    OUT.viewPosition = float4(NvrMainEye.xyz, shadowNormal.w);
    return OUT;
}
