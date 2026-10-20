
#include "../includes/Speedtree.hlsl"
// UNOFFICIAL 2026-10-16 cloth wind (../includes/ClothWind.hlsl): cloth's shadow moves with it (position only).
#ifndef CLOTH_WIND_ON
    #define CLOTH_WIND_ON 0
#endif
#if CLOTH_WIND_ON
    #include "../includes/ClothWind.hlsl"
#endif
// UNOFFICIAL 2026-10-20 plant wind (../includes/PlantWind.hlsl, core/PlantWind.h): the SpeedTree leaves' shadows move as the lit leaves
// (STLEAF001/003: the same gust boost, flutter and lean), and the static plants' as the plants (PLANT_WIND_ON, c248-c253). c131-c134:
// free (LeafBase ends at c130, the land LOD's constants start at c140).
float4 TESR_PlantLeafWind  : register(c131);
float4 TESR_PlantLeafWind2 : register(c132);
float4 TESR_PlantLeafWind3 : register(c133);
float4 TESR_CameraPosition : register(c134);
#include "../includes/PlantWind.hlsl"

row_major float4x4 TESR_ShadowWorldTransform : register(c0);
row_major float4x4 TESR_ShadowViewProjTransform : register(c4);
float4 TESR_ShadowData : register(c8);
float4 Bones[54] : register(c9);
float4 BillboardRight : register(c63);
float4 BillboardUp : register(c64);
float4 RockParams : register(c65);
float4 RustleParams : register(c66);
float4 WindMatrices[16] : register(c67);
float4 LeafBase[48] : register(c83);

// Terrain LOD params for hiding it under normal terrain.
row_major float4x4 WorldTranspose : register(c140);
float4 HighDetailRange : register(c144);
float4 LODLandParams : register(c145);

#define	weight(v)		dot(v, 1)

#if CLOTH_WIND_ON
// cloth mask 5 (2026-10-19 round 8): a bind-pose vector through the same blend of the bones' rotations as the position (w = 0 drops the
// translation); offset / bw as in main's skinned branch
float3 ClothBonesVector(float4 offset, float3 bw, float3 v) {
	const float4 vw = float4(v, 0);
	const float3 v8 = mul(float3x4(Bones[0 + offset.w].xyzw, Bones[1 + offset.w].xyzw, Bones[2 + offset.w].xyzw), vw);
	const float3 v6 = mul(float3x4(Bones[0 + offset.z].xyzw, Bones[1 + offset.z].xyzw, Bones[2 + offset.z].xyzw), vw);
	const float3 v5 = mul(float3x4(Bones[0 + offset.x].xyzw, Bones[1 + offset.x].xyzw, Bones[2 + offset.x].xyzw), vw);
	const float3 v4 = mul(float3x4(Bones[0 + offset.y].xyzw, Bones[1 + offset.y].xyzw, Bones[2 + offset.y].xyzw), vw);
	return ((1 - weight(bw)) * v8) + (bw.z * v6) + (bw.x * v5) + (v4 * bw.y);
}
#endif

struct VS_INPUT {
    float4 position : POSITION;
	float4 texcoord_0 : TEXCOORD0;
    float4 texcoord_1 : TEXCOORD1;  // Terrain LOD, no idea what it represents.
    float4 blendweight : BLENDWEIGHT;
    float4 blendindexes : BLENDINDICES;
#if CLOTH_WIND_ON
    float4 color : COLOR0;	// cloth wind mask (red)
    float3 normal : NORMAL;	// cloth wind (2026-10-17): the displacement has a part along the normal (billow, flutter): the shadow needs it too
    float clothWeight : TEXCOORD7;	// cloth wind mask 5 (2026-10-19): vertex stream 1, bound only for worn cloth (read only in mask 5)
#endif
};

struct VS_OUTPUT {
    float4 position : POSITION;
    float4 texcoord_0 : TEXCOORD0;
	float4 texcoord_1 : TEXCOORD1;
};

VS_OUTPUT main(VS_INPUT IN) {
    VS_OUTPUT OUT;

	const float4 const_0 = {1.0f, 765.01001f, 0.0f, 0.0f};
	const float4 const_4 = {(1.0 / (PI * 2)), 0.25, 0.5, 0};
	
	float4 offset;
    float4 q0;
	float4 q1;
	float4 q2;
	float4 q3;
    float4 q4;
    float4 q5;
    float4 q6;
    float4 q7;
    float4 q8;
	float4 q9;
	float4 q10;
	float4 q11;
	float4 q28;
	float4 q59;
	float4 r1;
	float4 r2;
	float4 r3;
	float4 r4;
	float4 r5;
	float4 r0 = IN.position;
	float plantLean = 0.0f;	// UNOFFICIAL 2026-10-20 plant wind: SpeedTree leaves' lean downwind (world units)
#if CLOTH_WIND_ON
	float3 clothNormal = IN.normal;	// the vertex's normal in the space r0 ends in (model space; skinned: the bones' space)
	float3 clothOut = 0.0f;			// mask 5's billow direction (skinned only), carried by the same bones
	float4 clothLegL = 0.0f, clothLegR = 0.0f;	// mask 5 (round 8): each leg (ClothLegBind), carried by the same bones
	float3 clothUp = 0.0f;				// ...and the bind pose's up (the legs' direction)
#endif
	
	if (TESR_ShadowData.x == 1.0f) { // Skinned (Actors)
		offset.xyzw = IN.blendindexes.zyxw * const_0.y;
		r0.w = 1;
		q0.xyzw = (IN.position.xyzx * const_0.xxxz) + const_0.zzzx;
		q8.xyz = mul(float3x4(Bones[0 + offset.w].xyzw, Bones[1 + offset.w].xyzw, Bones[2 + offset.w].xyzw), q0.xyzw);
		q6.xyz = mul(float3x4(Bones[0 + offset.z].xyzw, Bones[1 + offset.z].xyzw, Bones[2 + offset.z].xyzw), q0.xyzw);
		q5.xyz = mul(float3x4(Bones[0 + offset.x].xyzw, Bones[1 + offset.x].xyzw, Bones[2 + offset.x].xyzw), q0.xyzw);
		q4.xyz = mul(float3x4(Bones[0 + offset.y].xyzw, Bones[1 + offset.y].xyzw, Bones[2 + offset.y].xyzw), q0.xyzw);
		q7.xyz = (IN.blendweight.z * q6.xyz) + ((IN.blendweight.x * q5.xyz) + (q4.xyz * IN.blendweight.y));
		r0.xyz = ((1 - weight(IN.blendweight.xyz)) * q8.xyz) + q7.xyz;
#if CLOTH_WIND_ON
		{	// the same blend of the bones' rotations for the normal (the rows' xyz; w = 0 drops the translation)
			const float4 nw = float4(IN.normal, 0);
			const float3 n8 = mul(float3x4(Bones[0 + offset.w].xyzw, Bones[1 + offset.w].xyzw, Bones[2 + offset.w].xyzw), nw);
			const float3 n6 = mul(float3x4(Bones[0 + offset.z].xyzw, Bones[1 + offset.z].xyzw, Bones[2 + offset.z].xyzw), nw);
			const float3 n5 = mul(float3x4(Bones[0 + offset.x].xyzw, Bones[1 + offset.x].xyzw, Bones[2 + offset.x].xyzw), nw);
			const float3 n4 = mul(float3x4(Bones[0 + offset.y].xyzw, Bones[1 + offset.y].xyzw, Bones[2 + offset.y].xyzw), nw);
			clothNormal = ((1 - weight(IN.blendweight.xyz)) * n8) + (IN.blendweight.z * n6) + (IN.blendweight.x * n5) + (n4 * IN.blendweight.y);
		}
		[branch] if (ClothA.y > 4.5f) {	// mask 5 (2026-10-19): the same for the direction away from the wearer's upright axis
			const float4 ow = float4(ClothOutwardBind(IN.position.xyz), 0);
			const float3 o8 = mul(float3x4(Bones[0 + offset.w].xyzw, Bones[1 + offset.w].xyzw, Bones[2 + offset.w].xyzw), ow);
			const float3 o6 = mul(float3x4(Bones[0 + offset.z].xyzw, Bones[1 + offset.z].xyzw, Bones[2 + offset.z].xyzw), ow);
			const float3 o5 = mul(float3x4(Bones[0 + offset.x].xyzw, Bones[1 + offset.x].xyzw, Bones[2 + offset.x].xyzw), ow);
			const float3 o4 = mul(float3x4(Bones[0 + offset.y].xyzw, Bones[1 + offset.y].xyzw, Bones[2 + offset.y].xyzw), ow);
			clothOut = ((1 - weight(IN.blendweight.xyz)) * o8) + (IN.blendweight.z * o6) + (IN.blendweight.x * o5) + (o4 * IN.blendweight.y);
			const float4 legL = ClothLegBind(IN.position.xyz, ClothLimb[0], ClothLimb[1], ClothLimb[2]);
			const float4 legR = ClothLegBind(IN.position.xyz, ClothLimb[4], ClothLimb[5], ClothLimb[6]);
			clothLegL = float4(ClothBonesVector(offset, IN.blendweight.xyz, legL.xyz), legL.w);
			clothLegR = float4(ClothBonesVector(offset, IN.blendweight.xyz, legR.xyz), legR.w);
			clothUp = ClothBonesVector(offset, IN.blendweight.xyz, float3(0.0f, 0.0f, 1.0f));
		}
#endif
	}
	else if (TESR_ShadowData.x == 2.0f) { // Leaves (Speedtrees)
        // UNOFFICIAL 2026-10-20 plant wind: as the lit leaves (STLEAF001/003), a passing gust strengthens the game's swing (both swings x
        // boost: the swing is sin x params.x x params.z) and leans the leaves (plantLean, added in the world below); the gust is read at the
        // bush's base. (The lit leaves' small flutter is left out here: no way to add it through the game's parameters.)
        float4 rockParams = RockParams, rustleParams = RustleParams;
        [branch] if (TESR_PlantLeafWind.w > 0.0f) {
            const float2 base = float2(TESR_ShadowWorldTransform._m30, TESR_ShadowWorldTransform._m31) + TESR_CameraPosition.xy;
            const float gust = PlantLeafGust(base, TESR_PlantLeafWind.xy, TESR_PlantLeafWind.z);
            const float boost = 1.0f + TESR_PlantLeafWind2.w * gust;
            rockParams.x *= boost;
            rustleParams.x *= boost;
            const float rise = saturate(IN.position.z * TESR_PlantLeafWind2.z);
            plantLean = TESR_PlantLeafWind2.y * gust * rise * rise;
        }
        r0.xyzw = SpeedTreeTransform(BillboardUp, BillboardRight, rockParams, rustleParams, WindMatrices, LeafBase, IN.blendindexes, r0);
    }
    else if (TESR_ShadowData.x == 3.0f) { // Terrain LOD.
        float4 r1 = IN.position;
        r1.z = lerp(IN.texcoord_1.x, IN.position.z, LODLandParams.x);

        float q0 = (abs(dot(WorldTranspose[1].xyzw, r1.xyzw) - HighDetailRange.y) < HighDetailRange.w ? 1.0 : 0.0);
        float q1 = (abs(dot(WorldTranspose[0].xyzw, r1.xyzw) - HighDetailRange.x) < HighDetailRange.z ? 1.0 : 0.0);

        r0.z = r1.z - ((q0.x * q1.x) * LODLandParams.y);
    }
#if PLANT_WIND_ON
    [branch] if (TESR_ShadowData.x == 0.0f && PlantA.x < -0.5f) r0.xyz += PlantWindMove(r0.xyz);	// UNOFFICIAL 2026-10-20 plant wind (static plants)
#endif
#if CLOTH_WIND_ON
    if (TESR_ShadowData.x == 0.0f || TESR_ShadowData.x == 1.0f) r0.xyz = ClothWindPosition(r0.xyz, clothNormal, IN.color, IN.position.xyz, IN.clothWeight, clothOut, clothLegL, clothLegR, clothUp);	// UNOFFICIAL cloth wind (normal and skinned geometry)
#endif
    if (TESR_ShadowData.x != 1.0f) r0 = mul(r0, TESR_ShadowWorldTransform);
    [branch] if (plantLean != 0.0f) r0.xy += TESR_PlantLeafWind3.xy * plantLean;	// UNOFFICIAL 2026-10-20 plant wind: the leaves' lean (world)
	r0 = mul(r0, TESR_ShadowViewProjTransform);
	
	// Pancaking to ensure geometry outside of near plane is not culled.
    r0.z = max(r0.z, 0.0f);
	
	OUT.position = r0;
    OUT.texcoord_0 = r0;
	OUT.texcoord_1 = IN.texcoord_0;
    return OUT;
	
};