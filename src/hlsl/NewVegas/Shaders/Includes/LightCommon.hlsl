// UNOFFICIAL 2026-10-29 LightCommon (docs/next/ROOT-ASSUMPTIONS-PLAN.md, C; first user: ManyLampsOnePass with [Main.Main.Misc]
// LightCommonLamps, MANY_LAMPS_ON 2): the lamps live once per frame in a lamp table (core/ManyLamps.h BuildTable), a float texture of
// 1024 lamps x 2 texels: texel 2i = the lamp's world position + reach, texel 2i + 1 = its colour (as the game computes it, already
// linear with linear lighting on). A draw gets only its list of table indices and its own world -> object transform (c37-c44), so a
// shader lights any number of lamps in a REAL loop: ps_3_0 cannot index constant registers with a loop counter (D3DX copies the whole
// array into temp registers, X4505), so every lamp array in constants was unrolled, lamp by lamp, into the shader. Measured on the GTX
// 1070 (tests/lamp_loop_cost.cpp): a looped lamp costs ~1.4x an unrolled one, and the shader stays ~100 instruction slots at any count
// (unrolled 24 lamps: ~700, and minutes of D3DX compile time in the worst templates).
#ifndef NVR_LIGHT_COMMON_HLSL
#define NVR_LIGHT_COMMON_HLSL

#define LIGHT_COMMON_TEXELS 2048.0f		// table width (2 texels a lamp, 1024 lamps: Vault 21 has 447 lights)
#define LIGHT_COMMON_NONE 4095.0f		// "no lamp here" (a slot kept for its shadow-slot order)
#ifndef LIGHT_COMMON_ROWS
#define LIGHT_COMMON_ROWS 1.0f			// UNOFFICIAL 2026-10-30 ClusteredLamps: 31 (row 0 the lamps, rows 1-30 the froxel lists: ManyLamps::TableRows)
#endif

sampler2D NVR_LampTable : register(s3);	// no sampler_state: the DLL binds it point / clamp (free in these object shaders)

// Lamp k (0-23) of the draw's list: two indices per float (i0 + 4096 i1, exact in a float), eight per float4. Only divisions by powers
// of two (exact on the GPU) and floors of (n + 0.5) / m: a GPU divides through a reciprocal, so 3 x (1 / 3) is just under 1 and a plain
// floor(j / 3) put lamps 3, 6 and 9 in the wrong place (2026-10-29 bench: the first version read the wrong table entries).
float LightCommonIndex(float4 packedA, float4 packedB, float4 packedC, float k) {
    const float group = floor((k + 0.5f) / 8.0f);
    const float4 v = group < 0.5f ? packedA : (group < 1.5f ? packedB : packedC);
    const float j = k - group * 8.0f;
    const float component = floor((j + 0.5f) / 2.0f);
    const float f = dot(v, float4(component == 0.0f, component == 1.0f, component == 2.0f, component == 3.0f));
    const float high = floor(f / 4096.0f);
    return j - component * 2.0f < 0.5f ? f - high * 4096.0f : high;
}

// Table lamp i in the draw's own space: position + reach (reach x the draw's reach scale), colour.
// offset: the draw's lighting offset (first person); r0-r2: the world -> object transform's rows (x = dot(float4(world, 1), r0)).
void LightCommonLamp(float i, float4 offset, float4 r0, float4 r1, float4 r2, float reachScale, out float4 positionReach, out float3 color) {
    const float x = (i * 2.0f + 0.5f) / LIGHT_COMMON_TEXELS;
    const float4 p = tex2Dlod(NVR_LampTable, float4(x, 0.5f / LIGHT_COMMON_ROWS, 0.0f, 0.0f));
    const float4 c = tex2Dlod(NVR_LampTable, float4(x + 1.0f / LIGHT_COMMON_TEXELS, 0.5f / LIGHT_COMMON_ROWS, 0.0f, 0.0f));
    const float4 w = float4(p.xyz + offset.xyz, 1.0f);
    positionReach = float4(dot(w, r0), dot(w, r1), dot(w, r2), p.w * reachScale);
    color = c.rgb;
}

// UNOFFICIAL 2026-10-30 ClusteredLamps (core/ManyLamps.h BinFroxels): texel t of the froxel lists (row 1 on, LIGHT_COMMON_TEXELS a row).
// A froxel's list is 8 texels from froxel x 8: texel 0 = (count, lamp 0, 1, 2), then four lamp indices a texel (31 at most).
float4 LightCommonListTexel(float t) {
    const float row = floor(t / LIGHT_COMMON_TEXELS);	// (a power of two: exact)
    const float col = t - row * LIGHT_COMMON_TEXELS;
    return tex2Dlod(NVR_LampTable, float4((col + 0.5f) / LIGHT_COMMON_TEXELS, (row + 1.5f) / LIGHT_COMMON_ROWS, 0.0f, 0.0f));
}

#endif
