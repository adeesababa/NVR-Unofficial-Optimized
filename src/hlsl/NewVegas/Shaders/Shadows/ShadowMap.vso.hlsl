
#include "../includes/Speedtree.hlsl"
#ifndef CLOTH_WIND_ON
    #define CLOTH_WIND_ON 0
#endif
#if CLOTH_WIND_ON
    #include "../includes/ClothWind.hlsl"
#endif
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
    float4 color : COLOR0;
    float3 normal : NORMAL;
    float3 clothWeight : TEXCOORD7;
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
	float plantLean = 0.0f;
#if CLOTH_WIND_ON
	float3 clothNormal = IN.normal;
	float3 clothOut = 0.0f;
	float4 clothLegL = 0.0f, clothLegR = 0.0f;
	float3 clothUp = 0.0f;
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
		{
			const float4 nw = float4(IN.normal, 0);
			const float3 n8 = mul(float3x4(Bones[0 + offset.w].xyzw, Bones[1 + offset.w].xyzw, Bones[2 + offset.w].xyzw), nw);
			const float3 n6 = mul(float3x4(Bones[0 + offset.z].xyzw, Bones[1 + offset.z].xyzw, Bones[2 + offset.z].xyzw), nw);
			const float3 n5 = mul(float3x4(Bones[0 + offset.x].xyzw, Bones[1 + offset.x].xyzw, Bones[2 + offset.x].xyzw), nw);
			const float3 n4 = mul(float3x4(Bones[0 + offset.y].xyzw, Bones[1 + offset.y].xyzw, Bones[2 + offset.y].xyzw), nw);
			clothNormal = ((1 - weight(IN.blendweight.xyz)) * n8) + (IN.blendweight.z * n6) + (IN.blendweight.x * n5) + (n4 * IN.blendweight.y);
		}
		[branch] if (ClothA.y > 4.5f && ClothWalk.x > 0.5f) {
			r0.xyz = lerp(r0.xyz, ClothPelvisPoint(IN.position.xyz), saturate(IN.clothWeight.x) * ClothWalk.y);
			clothOut = ClothPelvisVector(ClothOutwardBind(IN.position.xyz));
			clothUp = ClothPelvisVector(float3(0.0f, 0.0f, 1.0f));
			clothLegL = ClothLegBind(r0.xyz, ClothLegNow[0], ClothLegNow[1], ClothLegNow[2]);
			clothLegR = ClothLegBind(r0.xyz, ClothLegNow[3], ClothLegNow[4], ClothLegNow[5]);
		}
		else [branch] if (ClothA.y > 4.5f) {
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
    [branch] if (TESR_ShadowData.x == 0.0f && PlantA.x < -0.5f) r0.xyz += PlantWindMove(r0.xyz);
#endif
#if CLOTH_WIND_ON
    if (TESR_ShadowData.x == 0.0f || TESR_ShadowData.x == 1.0f) r0.xyz = ClothWindPosition(r0.xyz, clothNormal, IN.color, IN.position.xyz, IN.clothWeight.x, clothOut, clothLegL, clothLegR, clothUp);
    if (TESR_ShadowData.x == 1.0f && ClothA.y > 4.5f && ClothWalk.x > 0.5f) {
        r0.xyz += ClothRingAt(IN.clothWeight.y) * IN.clothWeight.z;
        r0.xyz += ClothLegPush(r0.xyz, clothOut);
    }
#endif
    if (TESR_ShadowData.x != 1.0f) r0 = mul(r0, TESR_ShadowWorldTransform);
    [branch] if (plantLean != 0.0f) r0.xy += TESR_PlantLeafWind3.xy * plantLean;
	r0 = mul(r0, TESR_ShadowViewProjTransform);
	
	// Pancaking to ensure geometry outside of near plane is not culled.
    r0.z = max(r0.z, 0.0f);
	
	OUT.position = r0;
    OUT.texcoord_0 = r0;
	OUT.texcoord_1 = IN.texcoord_0;
    return OUT;
	
};