// UNOFFICIAL 2026-10-29 LightCommon (docs/next/ROOT-ASSUMPTIONS-PLAN.md, C; first user: ManyLampsOnePass with [Main.Main.Misc]
// LightCommonLamps, MANY_LAMPS_ON 2): the lamps live once per frame in a lamp table (core/ManyLamps.h BuildTable), a float texture of
// 128 lamps x 2 texels: texel 2i = the lamp's world position + reach, texel 2i + 1 = its colour (as the game computes it, already
// linear with linear lighting on). A draw gets only its list of table indices and its own world -> object transform (c37-c43), so a
// shader lights any number of lamps in a REAL loop: ps_3_0 cannot index constant registers with a loop counter (D3DX copies the whole
// array into temp registers, X4505), so every lamp array in constants was unrolled, lamp by lamp, into the shader. Measured on the GTX
// 1070 (tests/lamp_loop_cost.cpp): a looped lamp costs ~1.4x an unrolled one, and the shader stays ~100 instruction slots at any count
// (unrolled 24 lamps: ~700, and minutes of D3DX compile time in the worst templates).
#ifndef NVR_LIGHT_COMMON_HLSL
#define NVR_LIGHT_COMMON_HLSL

#define LIGHT_COMMON_TEXELS 256.0f		// table width (2 texels a lamp)
#define LIGHT_COMMON_NONE 255.0f		// "no lamp here" (a slot kept for its shadow-slot order)

sampler2D NVR_LampTable : register(s3);	// no sampler_state: the DLL binds it point / clamp (free in these object shaders)

// Lamp k (0-23) of the draw's list: three indices per float (i0 + 256 i1 + 65536 i2, exact in a float), twelve per float4.
float LightCommonIndex(float4 packedA, float4 packedB, float k) {
    const float4 v = k < 12.0f ? packedA : packedB;
    const float j = k < 12.0f ? k : k - 12.0f;
    const float component = floor(j / 3.0f);
    const float digit = j - component * 3.0f;
    const float f = dot(v, float4(component == 0.0f, component == 1.0f, component == 2.0f, component == 3.0f));
    const float scale = digit < 0.5f ? 1.0f : (digit < 1.5f ? 256.0f : 65536.0f);
    return fmod(floor(f / scale), 256.0f);
}

// Table lamp i in the draw's own space: position + reach (reach x the draw's reach scale), colour.
// offset: the draw's lighting offset (first person); r0-r2: the world -> object transform's rows (x = dot(float4(world, 1), r0)).
void LightCommonLamp(float i, float4 offset, float4 r0, float4 r1, float4 r2, float reachScale, out float4 positionReach, out float3 color) {
    const float x = (i * 2.0f + 0.5f) / LIGHT_COMMON_TEXELS;
    const float4 p = tex2Dlod(NVR_LampTable, float4(x, 0.5f, 0.0f, 0.0f));
    const float4 c = tex2Dlod(NVR_LampTable, float4(x + 1.0f / LIGHT_COMMON_TEXELS, 0.5f, 0.0f, 0.0f));
    const float4 w = float4(p.xyz + offset.xyz, 1.0f);
    positionReach = float4(dot(w, r0), dot(w, r1), dot(w, r2), p.w * reachScale);
    color = c.rgb;
}

#endif
