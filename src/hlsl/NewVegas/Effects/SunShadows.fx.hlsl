// Image space shadows shader for Oblivion Reloaded

float4x4 TESR_WorldViewProjectionTransform;
float4x4 TESR_ShadowCameraToLightTransformNear;
float4x4 TESR_ShadowCameraToLightTransformMiddle;
float4x4 TESR_ShadowCameraToLightTransformFar;
float4x4 TESR_ShadowCameraToLightTransformLod;
float4 TESR_ReciprocalResolution;
float4 TESR_SmoothedSunDir;
float4 TESR_ViewSpaceLightDir;
float4 TESR_ShadowData; // x: quality, y: darkness, z: texel size
float4 TESR_ShadowFormatData; // x: mode, y: format bits per pixels
float4 TESR_ShadowScreenSpaceData; // x: Enabled, y: blurRadius, z: renderDistance, w: intensity
float4 TESR_SunAmbient;
float4 TESR_ShadowFade; // x: sunset attenuation, y: shadows maps active, z: point lights shadows active
// Injected as a D3DXMACRO by EffectRecord from [Shaders.ShadowsExteriors.Main] ForwardShadows,
// exactly as it is for the game shaders -- so the two halves cannot disagree.
// 1 = the object/terrain/parallax shaders evaluate the sun cascades themselves, so this
//     effect must not also apply them. 0 = stock deferred behaviour.
#ifndef FORWARD_SHADOWS
    #define FORWARD_SHADOWS 0
#endif
float4 TESR_ShadowBlur; // x: 1 / atlas resolution, y: whether the lod cascade was updated
float4 TESR_ShadowForwardData; // x: 1 when the forward path is SUPPRESSED, w: HandoverBlend (world units; Shaders/Includes/Shadow.hlsl)
float4 TESR_ShadowNearCenter; // x,y,z: center (world space), w: radius
float4 TESR_ShadowMiddleCenter; // x,y,z: center (world space), w: radius
float4 TESR_ShadowFarCenter; // x,y,z: center (world space), w: radius
float4 TESR_ShadowLodCenter; // x,y,z: center (world space), w: radius

sampler2D TESR_DepthBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_ShadowAtlas : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_NormalsBuffer : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_PointShadowBuffer : register(s3)  = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_NoiseSampler : register(s4) < string ResourceName = "Effects\bluenoise256.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
// UNOFFICIAL 2026-10-23 CharacterShadowMap (Shaders/Includes/Shadow.hlsl CharacterMapShadow has the forward twin and the notes): the
// player's own small sun shadow map, read by the deferred lookup too (the path with ForwardShadows off). Compiled in only when on at
// startup (CHARACTER_SHADOW_MAP from EffectRecord), so that off this file is exactly what it was.
#ifndef CHARACTER_SHADOW_MAP
    #define CHARACTER_SHADOW_MAP 0
#endif
#if CHARACTER_SHADOW_MAP
float4x4 TESR_CharShadowCameraToLight; // camera-relative world -> the map's clip space (ortho)
float4 TESR_CharShadowCenter; // xyz: the box's centre (camera-relative world), w: its half-extent (world units)
float4 TESR_CharShadowData; // x: 1 / resolution, y: hand-over band (share of the half-extent), z: depth bias (map depth), w: 1 = live this frame
sampler2D TESR_CharShadowMap : register(s5) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
#endif

#define SSS_STEPNUM 5

static const float DARKNESS = 1-TESR_ShadowData.y;
static const float SSS_DIST = 2000;
static const float SSS_THICKNESS = 20;
static const float SSS_MAXDEPTH = TESR_ShadowScreenSpaceData.z * TESR_ShadowScreenSpaceData.x;

static const float Mode = TESR_ShadowFormatData.x;
static const float FormatBits = TESR_ShadowFormatData.y;

// Normal-offset bias in shadow map TEXELS, not world units -- a fixed world-space offset is
// several texels in the Near cascade and a fraction of one in the Lod cascade. See the note in
// Shaders/Includes/Shadow.hlsl. Keep these three in step with the SHADOW_NORMAL_BIAS_TEXELS /
// SHADOW_SLOPE_BIAS / SHADOW_FILTER_TAPS defaults there, or the two paths disagree.
static const float NormalBiasTexels = 2.5f;
static const float SlopeBias = 1.0f;
#define SHADOW_FILTER_TAPS 1
#define SHADOW_FILTER_SPREAD 1.0f

struct VSOUT
{
	float4 vertPos : POSITION;
	float4 normal : TEXCOORD1;
	float2 UVCoord : TEXCOORD0;
};

struct VSIN
{
	float4 vertPos : POSITION0;
	float2 UVCoord : TEXCOORD0;
};

#include "Includes/Helpers.hlsl"
#include "Includes/Depth.hlsl"
#include "Includes/Shadows.hlsl"
#include "Includes/Normals.hlsl"
#include "Includes/BlurDepth.hlsl"


VSOUT FrameVS(VSIN IN)
{
	VSOUT OUT = (VSOUT)0.0f;
	OUT.vertPos = IN.vertPos;
	OUT.UVCoord = IN.UVCoord;
	return OUT;
}

float4 ScreenCoordToTexCoord(float4 coord){
	// apply perspective (perspective division) and convert from -1/1 to range to 0/1 (shadowMap range);
	coord.xyz /= coord.w;
	coord.x = coord.x * 0.5f + 0.5f;
	coord.y = coord.y * -0.5f + 0.5f;

	return coord;
}

// Moments for one cascade, optionally averaged over several taps.
//
// Averaging the MOMENTS and evaluating Chebyshev once is the correct order -- moments are
// linearly filterable, which is the entire reason variance shadow maps exist. Averaging
// four separate Chebyshev results would be both wrong and slower.
// tex2Dlod, not tex2D: with forward compiled in, the deferred lookup sits inside a dynamic
// branch, and a gradient-taking sample there is illegal (X3528). The atlas has no mipmaps, so
// an explicit LOD 0 is exactly equivalent -- the forward path samples it the same way.
float4 SampleShadowMoments(float2 uv, float2 quadrantOffset) {
#if SHADOW_FILTER_TAPS <= 1
    return tex2Dlod(TESR_ShadowAtlas, float4(uv, 0.0f, 0.0f));
#else
	// Taps must stay inside their own quadrant: the atlas packs four unrelated cascades into
	// one texture, so a tap crossing a quadrant border reads another cascade's depths as if
	// they belonged to this one. ShadowMapBlur.pso clamps for the same reason.
    float texel = TESR_ShadowBlur.x;
    float2 lo = quadrantOffset + texel * 0.5f;
    float2 hi = quadrantOffset + 0.5f - texel * 0.5f;

    float2 d = texel * SHADOW_FILTER_SPREAD;
    float4 m;
    m  = tex2Dlod(TESR_ShadowAtlas, float4(clamp(uv + d * float2( 1.0f,  0.5f), lo, hi), 0.0f, 0.0f));
    m += tex2Dlod(TESR_ShadowAtlas, float4(clamp(uv + d * float2(-0.5f,  1.0f), lo, hi), 0.0f, 0.0f));
    m += tex2Dlod(TESR_ShadowAtlas, float4(clamp(uv + d * float2(-1.0f, -0.5f), lo, hi), 0.0f, 0.0f));
    m += tex2Dlod(TESR_ShadowAtlas, float4(clamp(uv + d * float2( 0.5f, -1.0f), lo, hi), 0.0f, 0.0f));
    return m * 0.25f;
#endif
}

float GetLightAmountValue(float4x4 lightTransform, float4 coord, float offsetX, float offsetY, float bias, float bleedReduction) {
    float4 LightSpaceCoord = ScreenCoordToTexCoord(mul(coord, lightTransform));

	// Offset to the correct position in the atlas.
    LightSpaceCoord.xy *= 0.5;
    LightSpaceCoord.x += offsetX;
    LightSpaceCoord.y += offsetY;

    float4 shadowBufferValue = SampleShadowMoments(LightSpaceCoord.xy, float2(offsetX, offsetY));

    float shadow;
	
	[branch]
    if (Mode == 0.0f)
        shadow = GetLightAmountValueVSM(shadowBufferValue.xy, LightSpaceCoord.z, bias, bleedReduction);
    else if (Mode == 1.0f)
        shadow = GetLightAmountValueEVSM2(shadowBufferValue.xy, LightSpaceCoord.z, bias, bleedReduction, FormatBits);
	else
        shadow = GetLightAmountValueEVSM4(shadowBufferValue, LightSpaceCoord.z, bias, bleedReduction, FormatBits);
	// UNOFFICIAL PCF (Mode 3): blended in without one more branch (this file is at ps_3_0's 16 constant branches). The
	// deferred path only; with forward shadows (the default) the object shaders do the lookup.
	shadow = lerp(shadow, ShadowPCF2x2(TESR_ShadowAtlas, LightSpaceCoord.xy, LightSpaceCoord.z - 0.0002f, TESR_ShadowBlur.x), saturate(Mode - 2.0f));
	
    return shadow;
}

float GetLightAmountCascades(float4 positionWS, float3 normal)
{
	// Normal offset.
    float NdotL = dot(normal, TESR_SmoothedSunDir.xyz);
    float offsetScale = saturate(1 - NdotL);

	// World size of one shadow map texel, per cascade. GetCascadeViewProj builds each cascade
	// as [-radius, +radius], so a texel is 2*radius/cascadeResolution; the atlas is two
	// cascades wide, so cascadeResolution = 0.5 / TESR_ShadowBlur.x and the texel works out
	// to 4 * radius * TESR_ShadowBlur.x.
    float4 radii = {
        TESR_ShadowNearCenter.w,
        TESR_ShadowMiddleCenter.w,
        TESR_ShadowFarCenter.w,
        TESR_ShadowLodCenter.w,
    };
    float4 texelWorld = 4.0f * radii * max(TESR_ShadowBlur.x, 1.0f / 16384.0f);
    float4 offsetDistance = offsetScale * NormalBiasTexels * texelWorld;

	// Slope-scaled variance floor: a grazing texel spans a long run of receiver depth and
	// needs more slack before Chebyshev calls it occluded.
    float bias = (Mode == 0.0f ? 0.00001f : 0.01f) * (1.0f + SlopeBias * offsetScale);

	// UNOFFICIAL 2026-10-21 HandoverBlend (the same rule as Shaders/Includes/Shadow.hlsl GetSunShadow; P82 tester: the hair's
	// shadow on the brow popping as the third-person camera zooms): the cross-fade into the next cascade starts a fixed number
	// of world units (TESR_ShadowForwardData.w) inside each cascade's edge, never less than the old tenth of the radius and never
	// more than the radius; and inside the band both cascades look up the SAME point, the normal offset going smoothly from the
	// inner cascade's to the outer's, so only the weights change between two readings of one place.
    float4 bands = max(0.1f * radii, min(TESR_ShadowForwardData.w, radii));
    float4 starts = radii - bands;

	// Each cascade is offset in its OWN texel scale -- one shared samplePos cannot suit all
	// four when their texels differ by more than an order of magnitude (blended across the bands).
    // Evaluate only the selected cascade(s), as the forward path already does.
    // Sampling uses explicit LOD, so it is legal inside ps_3_0 dynamic branches.
#define TAP_NEAR(o) GetLightAmountValue(TESR_ShadowCameraToLightTransformNear, float4(positionWS.xyz + (o) * normal, 1), 0, 0, bias, 0.1f)
#define TAP_MIDDLE(o) GetLightAmountValue(TESR_ShadowCameraToLightTransformMiddle, float4(positionWS.xyz + (o) * normal, 1), 0.5, 0, bias, 0.2f)
#define TAP_FAR(o) GetLightAmountValue(TESR_ShadowCameraToLightTransformFar, float4(positionWS.xyz + (o) * normal, 1), 0, 0.5, bias, 0.6f)
#define TAP_LOD(o) GetLightAmountValue(TESR_ShadowCameraToLightTransformLod, float4(positionWS.xyz + (o) * normal, 1), 0.5, 0.5, bias, 0.8f)

    float4 distances = {
        length(positionWS.xyz - TESR_ShadowNearCenter.xyz),
        length(positionWS.xyz - TESR_ShadowMiddleCenter.xyz),
        length(positionWS.xyz - TESR_ShadowFarCenter.xyz),
        length(positionWS.xyz - TESR_ShadowLodCenter.xyz),
    };

    [branch] if (distances.x < radii.x) {
        float f = smoothstep(starts.x, radii.x, distances.x);
        float o = lerp(offsetDistance.x, offsetDistance.y, f);
        [branch] if (f <= 0.0f)
            return TAP_NEAR(o);

        return lerp(TAP_NEAR(o), TAP_MIDDLE(o), f);
    }
    else if (distances.y < radii.y) {
        float f = smoothstep(starts.y, radii.y, distances.y);
        float o = lerp(offsetDistance.y, offsetDistance.z, f);
        [branch] if (f <= 0.0f)
            return TAP_MIDDLE(o);

        return lerp(TAP_MIDDLE(o), TAP_FAR(o), f);
    }
    else if (distances.z < radii.z) {
        float f = smoothstep(starts.z, radii.z, distances.z);
        float o = lerp(offsetDistance.z, offsetDistance.w, f);
        [branch] if (f <= 0.0f)
            return TAP_FAR(o);

        return lerp(TAP_FAR(o), TAP_LOD(o), f);
    }
    else if (distances.w < radii.w) {
        float f = smoothstep(starts.w, radii.w, distances.w);
        [branch] if (f <= 0.0f)
            return TAP_LOD(offsetDistance.w);

        return lerp(TAP_LOD(offsetDistance.w), 1.0f, f);
    }
    else {
        return 1.0f;
    }
}

// returns a semi random float3 between 0 and 1 based on the given seed. (blue noise)
// tailored to return a different value for each uv coord of the screen.
#undef TAP_NEAR
#undef TAP_MIDDLE
#undef TAP_FAR
#undef TAP_LOD

// The cascades' light amount, and with CharacterShadowMap the darker of it and the player's own map's where the point is inside
// that map's box, fading to the cascades alone over the box's outer band: the same rule as the forward lookup (Shadow.hlsl
// CharacterMapShadow), with the deferred path's 2 x 2 PCF. The branch depends on the point (weight), not on constants alone, so the
// effect compiler keeps it a dynamic branch and takes none of ps_3_0's 16 constant-branch registers (this file is at that cap).
float GetLightAmount(float4 positionWS, float3 normal)
{
    float shadow = GetLightAmountCascades(positionWS, normal);
#if CHARACTER_SHADOW_MAP
    float texel = max(TESR_CharShadowData.x, 1.0f / 16384.0f);
    float halfExtent = max(TESR_CharShadowCenter.w, 1.0f);
    float NdotL = dot(normal, TESR_SmoothedSunDir.xyz);
    float offsetScale = saturate(1.0f - NdotL);
    float3 position = positionWS.xyz + normal * (offsetScale * 1.5f * 2.0f * halfExtent * texel);
    float4 lightSpace = mul(float4(position, 1.0f), TESR_CharShadowCameraToLight);
    float edge = max(abs(lightSpace.x), abs(lightSpace.y));
    float band = max(TESR_CharShadowData.y, 0.01f);
    float weight = (1.0f - smoothstep(1.0f - band, 1.0f, edge)) * TESR_CharShadowData.w;
    [branch] if (weight > 0.0f) {
        float2 uv = float2(lightSpace.x * 0.5f + 0.5f, lightSpace.y * -0.5f + 0.5f);
        float slope = min(sqrt(saturate(1.0f - NdotL * NdotL)) / max(NdotL, 0.05f), 5.0f);
        float depth = lightSpace.z - TESR_CharShadowData.z - texel * slope;
        float lit = ShadowPCF2x2(TESR_CharShadowMap, clamp(uv, texel, 1.0f - texel), depth, texel);
        shadow = lerp(shadow, min(shadow, lit), weight);
    }
#endif
    return shadow;
}
float3 random(float2 seed)
{
	return tex2D(TESR_NoiseSampler, (seed/256 + 0.5) / TESR_ReciprocalResolution.xy).xyz;
}

float4 ScreenSpaceShadow(VSOUT IN) : COLOR0
{	
	// calculates wether a point is in shadow based on screen depth
	float2 uv = IN.UVCoord;
	// clip((uv < 0.5) - 1);
	// uv *= 2;

    float4 color = tex2D(TESR_PointShadowBuffer, IN.UVCoord);
	if (!TESR_ShadowScreenSpaceData.x) return float4(1.0, color.g, 0, 1); // skip is screenspace shadows are disabled

	float3 pos = reconstructPosition(uv);// + expand(random3);

	if (pos.z > SSS_MAXDEPTH) return float4(1.0, color.g, 0, 1); // early out for pixels further away than the max render distance

	// Surfaces facing away from the sun receive no direct sun, so a contact shadow there can
	// only darken ambient light -- and it is exactly where the march runs INTO the receiving
	// surface. Bilinear linear-depth reads along that ray are slightly off the true plane, and
	// the error cycles with the sub-pixel sample phase, so a fixed tiny bias flips the test on
	// and off in bands perpendicular to the ray (horizontal lines under a high sun). Fade the
	// term out as the surface turns away from the light, and scale the self-intersection bias
	// with distance so grazing lit faces do not band either.
	float NdotL = dot(GetNormal(uv), normalize(TESR_ViewSpaceLightDir.xyz));
	float facing = saturate(NdotL * 8.0f);
	if (facing <= 0.0f) return float4(1.0, color.g, 0, 1);
	float bias = max(0.01f, pos.z * 0.002f);

    float3 random3 = random(uv);
    float rand = lerp(min(0.8f, pos.z / SSS_MAXDEPTH), 1.0f, random3.r); // some noise to vary the ray length

	// scale the step with distance, and randomize length
	float depth = getHomogenousDepth(uv) / farZ;
	float3 step = pows(depth, 0.6) * (SSS_DIST / SSS_STEPNUM) * TESR_ViewSpaceLightDir.xyz * rand;
	float thickness = pows(depth, 0.6) * SSS_THICKNESS;

	float occlusion = 0.0;
	float total = 0;

	// Doing two steps at once to optimize the depth march
	[unroll]
	for (float i = 1; i < SSS_STEPNUM; i+=2){
		float step1 = i;
		float step2 = i + 1;

		float3 pos1 = pos + step1 * step; // we move to the light with bigger steps each time
		float3 pos2 = pos1 + step2 * step; // we move to the light with bigger steps each time
		
		// if (screen_pos.x > 0 && screen_pos.x < 1.0 && screen_pos.y > 0 && screen_pos.y <1){
		float2 depth = {pos1.z, pos2.z};
		float2 depthCompare = {
			readDepth(projectPosition(pos1).xy),
			readDepth(projectPosition(pos2).xy),
		};

		float2 depthDelta = depth - depthCompare;

		occlusion += (depthDelta.x > bias && depthDelta.x < SSS_THICKNESS)/step1; // in Shadow
		occlusion += (depthDelta.y > bias && depthDelta.y < SSS_THICKNESS)/step2; // in Shadow
		pos = pos2; 
		total += 1/step1 + 1/step2; // weight samples inversely with distance
	}

    occlusion = pows(occlusion / total, 0.3) * facing; // get an average shading based on total weights
	

    // save result of SSS in red channel, and fade contribution with distance
    color.r = lerp(1.0f - occlusion, 1.0, smoothstep(SSS_MAXDEPTH * 0.8, SSS_MAXDEPTH, pos.z));
    return color;
}

// UNOFFICIAL 2026-10-17 FastPaths ([Shaders.ShadowsExteriors.ScreenSpace] FastPaths; SunShadows.cpp picks techniques 3 and 4,
// copies of 0 and ForwardContactShadows with this march pass): the same picture, the three early returns made real. The game's
// effect compiler (D3DX 9.43) cannot branch around reads that need screen derivatives (tex2D), so it had turned 'contact shadows
// off', 'farther than the render distance' and 'facing away from the sun' into selects at the end: every pixel, sky included,
// ran the whole march. Here every read at the pixel itself stays tex2D in uniform flow (the same reads: the shadow buffer, depth,
// normal and the mipmapped blue noise), and only the march's depth reads, behind the branches, are tex2Dlod level 0 on the
// single-level depth buffer (the same texels). Arithmetic is the original's, line for line.
float4 ScreenSpaceShadowFast(VSOUT IN) : COLOR0
{
	// (statements in the original's order, so the effect's constant layout and preshader stay the same as the original pass's)
	float2 uv = IN.UVCoord;
	float4 color = tex2D(TESR_PointShadowBuffer, IN.UVCoord);
	if (!TESR_ShadowScreenSpaceData.x) return float4(1.0, color.g, 0, 1);
	float3 pos = reconstructPosition(uv);
	bool far = pos.z > SSS_MAXDEPTH;
	float NdotL = dot(GetNormal(uv), normalize(TESR_ViewSpaceLightDir.xyz));
	float facing = saturate(NdotL * 8.0f);
	float bias = max(0.01f, pos.z * 0.002f);
	float3 random3 = random(uv);
	float rand = lerp(min(0.8f, pos.z / SSS_MAXDEPTH), 1.0f, random3.r);
	float depth = getHomogenousDepth(uv) / farZ;
	float3 step = pows(depth, 0.6) * (SSS_DIST / SSS_STEPNUM) * TESR_ViewSpaceLightDir.xyz * rand;
	float thickness = pows(depth, 0.6) * SSS_THICKNESS;
	float4 result = float4(1.0, color.g, 0, 1);
	// the far and facing-away returns give the same value: one real branch around the march
	[branch] if (!far && !(facing <= 0.0f)) {
		float occlusion = 0.0;
		float total = 0;
		[unroll]
		for (float i = 1; i < SSS_STEPNUM; i+=2){
			float step1 = i;
			float step2 = i + 1;
			float3 pos1 = pos + step1 * step;
			float3 pos2 = pos1 + step2 * step;
			float2 depth = {pos1.z, pos2.z};
			float2 depthCompare = {
				readDepthLod(projectPosition(pos1).xy),
				readDepthLod(projectPosition(pos2).xy),
			};
			float2 depthDelta = depth - depthCompare;
			occlusion += (depthDelta.x > bias && depthDelta.x < SSS_THICKNESS)/step1;
			occlusion += (depthDelta.y > bias && depthDelta.y < SSS_THICKNESS)/step2;
			pos = pos2;
			total += 1/step1 + 1/step2;
		}
		occlusion = pows(occlusion / total, 0.3) * facing;
		color.r = lerp(1.0f - occlusion, 1.0, smoothstep(SSS_MAXDEPTH * 0.8, SSS_MAXDEPTH, pos.z));
		result = color;
	}
	return result;
}

// returns a shadow value from darkness setting value (full shadow) to 1 (full light)
float4 Shadow(VSOUT IN) : COLOR0
{
	float2 uv = IN.UVCoord;

	// Sample Screen Space shadows
	float4 Shadow = tex2D(TESR_PointShadowBuffer, IN.UVCoord);
    Shadow = pow(Shadow, TESR_ShadowScreenSpaceData.w);
	if (!TESR_ShadowFade.y) return Shadow; // disable shadow maps if ShadowFade.y == 0 (setting for shadow map disabled)

	// Sample shadows from shadowmaps.
	//
	// Skipped when the forward path is doing the cascade lookup: ObjectTemplate.hlsl and
	// friends then apply the result to the sun term alone, which this screen-space composite
	// cannot do -- it can only scale the finished pixel, dimming ambient, emittance and
	// specular along with the sun.
	//
	// Screen-space contact shadows (already in Shadow.r) and point lights (Shadow.g) stay
	// deferred either way; the forward path only takes over the cascade lookup.
	//
	// FORWARD_SHADOWS decides whether the forward code was COMPILED INTO the game shaders;
	// TESR_ShadowForwardData.x decides whether it is RUNNING. When forward is compiled in we
	// must branch at runtime rather than compile this out, so that turning the setting off
	// mid-session hands the cascades back here in the same frame -- game shaders cannot be
	// recompiled at runtime, so a macro alone would leave neither path drawing shadows.
#if FORWARD_SHADOWS
	// Forward mode already evaluated the cascades per object. This coherent early return avoids
	// reconstructing world position and reading normals for the entire screen. Explicit-LOD
	// helpers keep the deferred fallback legal inside ps_3_0 dynamic flow control.
	[branch] if (!TESR_ShadowForwardData.x) return Shadow;
#else
	// Forward was compiled out entirely, so the cascades are always ours.
#endif

	float viewDepth;
	float4 worldPos = reconstructWorldPositionLod(uv, viewDepth);
	float3 normal = GetWorldNormalLod(uv);
	Shadow.r = min(Shadow.r, GetLightAmount(worldPos, normal)); // darkest of screenspace & sun

	return Shadow;
}

float4 FinalContactBlur(VSOUT IN) : COLOR0
{
	float4 shadow = DepthBlurKeep(IN, TESR_PointShadowBuffer, OffsetMaskV,
		TESR_ShadowScreenSpaceData.y, 3500, SSS_MAXDEPTH);
	// Beyond the contact-shadow range DepthBlur passes its input through; the in-place path
	// used to leave those texels untouched, so do not apply the intensity curve to them either.
	if (readDepthLod(IN.UVCoord) > SSS_MAXDEPTH) return shadow;
	return pow(shadow, TESR_ShadowScreenSpaceData.w);
}


// UNOFFICIAL 2026-10-14 sharp contact shadows ([Shaders.ShadowsExteriors.ScreenSpace] Sharp; technique ForwardContactSharp). The
// march above takes four randomly lengthened steps and leaves the grain to two full-resolution 12-tap blurs (two thirds of the
// cost). Here the ray toward the sun is followed across the screen in 16 even steps, no randomness, from the pixel to the
// same reach the old march had (perspective-correct depth along it); so there is no grain to blur, only a tiny smoothing of
// the steps' stair edges in the second pass (SharpContactFinish). The same tests and weighting as ScreenSpaceShadow.
#define SHARP_STEPS 16
float4 SharpContactShadow(VSOUT IN) : COLOR0
{
	float2 uv = IN.UVCoord;
	float4 color = tex2D(TESR_PointShadowBuffer, uv);
	if (!TESR_ShadowScreenSpaceData.x) return float4(1.0, color.g, 0, 1);
	float3 pos = reconstructPosition(uv);
	if (pos.z > SSS_MAXDEPTH) return float4(1.0, color.g, 0, 1);
	float3 light = normalize(TESR_ViewSpaceLightDir.xyz);
	float facing = saturate(dot(GetNormal(uv), light) * 8.0f);
	if (facing <= 0.0f) return float4(1.0, color.g, 0, 1);
	float bias = max(0.01f, pos.z * 0.002f);

	float rayLength = pows(getHomogenousDepth(uv) / farZ, 0.6) * SSS_DIST * 2.0f;
	if (light.z < 0.0f) rayLength = min(rayLength, max(pos.z - 2.0f * nearZ, 0.0f) / -light.z);   // stay in front of the camera
	float3 endPos = pos + light * rayLength;
	float2 endUV = projectPosition(endPos).xy;
	float invZ0 = 1.0f / pos.z, invZ1 = 1.0f / max(endPos.z, nearZ);

	float occlusion = 0.0f, total = 0.0f;
	[unroll] for (int i = 1; i <= SHARP_STEPS; i++) {
		float t = i / (float)SHARP_STEPS;
		float rayZ = 1.0f / lerp(invZ0, invZ1, t);
		float delta = rayZ - readDepthLod(lerp(uv, endUV, t));
		float w = 1.0f / (1.0f + t * 4.0f);                    // nearer steps count more, as the old 1 / step
		occlusion += (delta > bias && delta < SSS_THICKNESS) * w;
		total += w;
	}
	occlusion = pows(occlusion / total, 0.3) * facing;
	return float4(lerp(1.0f - occlusion, 1.0f, smoothstep(SSS_MAXDEPTH * 0.8, SSS_MAXDEPTH, pos.z)), color.g, 0, 1);
}

// The four neighbours averaged in with the centre where they lie at the same depth (only the stair edges change), then the
// intensity curve, as FinalContactBlur. The point shadows (g) pass through.
float4 SharpContactFinish(VSOUT IN) : COLOR0
{
	float4 center = tex2D(TESR_PointShadowBuffer, IN.UVCoord);
	float depth = readDepthLod(IN.UVCoord);
	if (depth > SSS_MAXDEPTH) return center;
	float sum = center.r * 2.0f, weights = 2.0f;
	float tolerance = max(depth * 0.02f, 1.0f);
	static const float2 offsets[4] = { float2(1, 0), float2(-1, 0), float2(0, 1), float2(0, -1) };
	[unroll] for (int i = 0; i < 4; i++) {
		float2 tapUV = IN.UVCoord + offsets[i] * TESR_ReciprocalResolution.xy;
		float w = abs(readDepthLod(tapUV) - depth) < tolerance ? 1.0f : 0.0f;
		sum += tex2D(TESR_PointShadowBuffer, tapUV).r * w;
		weights += w;
	}
	center.r = pow(sum / weights, TESR_ShadowScreenSpaceData.w);
	return center;
}

technique {

	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 ScreenSpaceShadow();
	}

	pass {
		VertexShader = compile vs_3_0 FrameVS();
	 	PixelShader = compile ps_3_0 DepthBlurKeep(TESR_PointShadowBuffer, OffsetMaskH, TESR_ShadowScreenSpaceData.y, 3500, SSS_MAXDEPTH);
	}

	pass {
		VertexShader = compile vs_3_0 FrameVS();
	 	PixelShader = compile ps_3_0 DepthBlurKeep(TESR_PointShadowBuffer, OffsetMaskV, TESR_ShadowScreenSpaceData.y, 3500, SSS_MAXDEPTH);
	}

    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 Shadow();
    }

}

// With forward cascades enabled, Shadow() only samples the vertical blur result and raises it
// to the configured intensity. Do that in the vertical pass and avoid a fourth full-screen draw
// and copy. The DLL selects this technique only for the forward path.
technique ForwardContactShadows {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 ScreenSpaceShadow();
	}

	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 DepthBlurKeep(TESR_PointShadowBuffer, OffsetMaskH, TESR_ShadowScreenSpaceData.y, 3500, SSS_MAXDEPTH);
	}

	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 FinalContactBlur();
	}
}

// UNOFFICIAL: the sharp contact shadows (above), forward path only. Third technique (ShaderManager checks the index).
technique ForwardContactSharp {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 SharpContactShadow();
	}

	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 SharpContactFinish();
	}
}

// UNOFFICIAL 2026-10-17 FastPaths: techniques 3 and 4 = techniques 0 and ForwardContactShadows with the real early returns in the
// march (ScreenSpaceShadowFast above); SunShadowsEffect::Render picks them by index with the setting on.
technique ContactFast {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 ScreenSpaceShadowFast();
	}

	pass {
		VertexShader = compile vs_3_0 FrameVS();
	 	PixelShader = compile ps_3_0 DepthBlurKeep(TESR_PointShadowBuffer, OffsetMaskH, TESR_ShadowScreenSpaceData.y, 3500, SSS_MAXDEPTH);
	}

	pass {
		VertexShader = compile vs_3_0 FrameVS();
	 	PixelShader = compile ps_3_0 DepthBlurKeep(TESR_PointShadowBuffer, OffsetMaskV, TESR_ShadowScreenSpaceData.y, 3500, SSS_MAXDEPTH);
	}

    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 Shadow();
    }
}

technique ForwardContactShadowsFast {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 ScreenSpaceShadowFast();
	}

	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 DepthBlurKeep(TESR_PointShadowBuffer, OffsetMaskH, TESR_ShadowScreenSpaceData.y, 3500, SSS_MAXDEPTH);
	}

	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 FinalContactBlur();
	}
}
