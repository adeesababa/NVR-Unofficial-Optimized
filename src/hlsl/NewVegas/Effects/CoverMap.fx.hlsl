
float4 TESR_CoverMapUpdate;
float4 TESR_CoverMapOrigin;
float4 TESR_CoverMapShift;
float4 TESR_CoverMapRates;
float4 TESR_CoverMapSnowRates;
float4 TESR_CoverMapSun;
float4 TESR_CoverMapShade;
float4 TESR_CoverMapStart;
float4 TESR_CoverMapFootA;
float4 TESR_CoverMapFootB;
float4 TESR_CoverMapDebug;
float4x4 TESR_CoverMapOrthoInverse;
float4x4 TESR_ShadowCameraToLightTransformOrtho;
float4 TESR_ReciprocalResolution;

sampler2D TESR_CoverMapBuffer : register(s0) = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_OrthoMapBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_RenderedBuffer : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_DepthBuffer : register(s3) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_CoverMapAux : register(s4) = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };

#include "Includes/Helpers.hlsl"
#include "Includes/Depth.hlsl"
#include "Includes/CoverMap.hlsl"

struct VSOUT
{
	float4 vertPos : POSITION;
	float2 UVCoord : TEXCOORD0;
};

struct VSIN
{
	float4 vertPos : POSITION0;
	float2 UVCoord : TEXCOORD0;
};

VSOUT FrameVS(VSIN IN)
{
	VSOUT OUT = (VSOUT)0.0f;
	OUT.vertPos = IN.vertPos;
	OUT.UVCoord = IN.UVCoord;
	return OUT;
}

static const float HeightRange = 65536.0f;
static const float HeightNone = 1.0f / 65535.0f;

float EncodeHeight(float h) { return clamp((h + TESR_CoverMapShade.z) / HeightRange + 0.5f, 2.0f * HeightNone, 1.0f); }
float DecodeHeight(float a) { return (a - 0.5f) * HeightRange - TESR_CoverMapShade.z; }
float HeightKnown(float a) { return a > HeightNone ? 1.0f : 0.0f; }

float2 WindowPos(float2 texel, float2 origin)
{
	return texel - origin + TESR_CoverMapUpdate.x * (1.0f - step(origin, texel));
}

float KeptTexel(float2 texel)
{
	float2 now = WindowPos(texel, TESR_CoverMapOrigin.xy);
	float2 before = WindowPos(texel, TESR_CoverMapOrigin.zw);
	float2 miss = abs(TESR_CoverMapShift.xy - (before - now));
	return TESR_CoverMapUpdate.w * step(max(miss.x, miss.y), 0.25f);
}

float2 WrapTexel(float2 texel)
{
	return texel - TESR_CoverMapUpdate.x * floor((texel + 0.5f) * TESR_CoverMapUpdate.y);
}

float4 FetchTexel(float2 texel)
{
	return tex2Dlod(TESR_CoverMapBuffer, float4((texel + 0.5f) * TESR_CoverMapUpdate.y, 0.0f, 0.0f));
}

float4 FetchAux(float2 texel)
{
	return tex2Dlod(TESR_CoverMapAux, float4((texel + 0.5f) * TESR_CoverMapUpdate.y, 0.0f, 0.0f));
}

float NeighbourHeight(float2 texel, float2 pos, float2 offset, float none)
{
	float2 npos = pos + offset;
	float2 ntexel = WrapTexel(texel + offset);
	float a = FetchAux(ntexel).r;
	float inside = step(0.0f, min(npos.x, npos.y)) * step(max(npos.x, npos.y), TESR_CoverMapUpdate.x - 1.0f);
	return inside * KeptTexel(ntexel) * HeightKnown(a) > 0.5f ? DecodeHeight(a) : none;
}

float OrthoTop(float2 rel, float guess, out float found)
{
	float4 o = mul(float4(rel, guess, 1.0f), TESR_ShadowCameraToLightTransformOrtho);
	o.xyz /= o.w;
	float2 uv = float2(o.x * 0.5f + 0.5f, o.y * -0.5f + 0.5f);
	float d = tex2Dlod(TESR_OrthoMapBuffer, float4(uv, 0.0f, 0.0f)).r;
	found = step(0.002f, min(uv.x, uv.y)) * step(max(uv.x, uv.y), 0.998f) * step(1e-6f, d) * step(d, 0.99999f);
	float4 p = mul(float4(o.x, o.y, d, 1.0f), TESR_CoverMapOrthoInverse);
	return p.z / p.w;
}

float3 ScreenPoint(float2 uv)
{
	return toWorld(uv) * readDepthLod(uv);
}

float ScreenTop(float2 rel, float guess, out float found)
{
	found = 0.0f;
	float z = guess;
	float3 p = float3(rel, guess);
	float2 uv = float2(0.5f, 0.5f);
	float onScreen = 0.0f;
	[unroll]
	for (int i = 0; i < 3; i++)
	{
		float3 view = mul(float4(rel, z, 0.0f), TESR_ViewTransform).xyz;
		float3 projected = projectPosition(view);
		uv = projected.xy;
		onScreen = step(nearZ + 1.0f, view.z) * step(0.002f, min(uv.x, uv.y)) * step(max(uv.x, uv.y), 0.998f);
		p = ScreenPoint(saturate(uv));
		z = p.z;
	}
	float depth = length(p);
	float2 pixel = TESR_ReciprocalResolution.xy;
	float3 px = ScreenPoint(saturate(uv + float2(pixel.x, 0.0f))) - p;
	float3 py = ScreenPoint(saturate(uv + float2(0.0f, pixel.y))) - p;
	float3 n = cross(px, py);
	n *= dot(n, -p) < 0.0f ? -1.0f : 1.0f;
	float up = n.z / max(length(n), 1e-6f);
	float match = length(p.xy - rel) < 1.5f * TESR_CoverMapUpdate.z ? 1.0f : 0.0f;
	found = onScreen * match * step(0.7f, up) * step(60.0f, depth) * step(readDepthLod(saturate(uv)), farZ * 0.98f);
	return z;
}

struct UpdateOut
{
	float4 main : COLOR0;
	float4 aux : COLOR1;
};

UpdateOut UpdatePS(VSOUT IN, float2 vpos : VPOS)
{
	const float texelSize = TESR_CoverMapUpdate.z;
	float2 texel = floor(vpos);
	float kept = KeptTexel(texel);
	float4 state = kept > 0.5f ? FetchTexel(texel) : float4(TESR_CoverMapStart.xyz, 0.0f);
	float4 aux = kept > 0.5f ? FetchAux(texel) : float4(0.0f, 1.0f, 0.0f, 0.0f);
	float2 pos = WindowPos(texel, TESR_CoverMapOrigin.xy);
	float2 rel = TESR_CoverMapShift.zw + (pos + 0.5f) * texelSize;

	float known = HeightKnown(aux.r);
	float height = known > 0.5f ? DecodeHeight(aux.r) : TESR_CoverMapFootB.z;
	float source = aux.b;
	float orthoFound = 0.0f;
	[branch]
	if (TESR_CoverMapShade.w > 0.5f)
	{
		float found1, found2;
		float first = OrthoTop(rel, height, found1);
		float second = OrthoTop(rel, first, found2);
		orthoFound = found1 * found2;
		if (orthoFound > 0.5f)
		{
			height = second;
			known = 1.0f;
			source = 1.0f;
		}
	}
	[branch]
	if (orthoFound < 0.5f && source < 0.75f && length(rel) < 4000.0f)
	{
		float seen;
		float top = ScreenTop(rel, height, seen);
		if (seen > 0.5f)
		{
			height = top;
			known = 1.0f;
			source = 0.5f;
		}
	}

	float sunlit = 1.0f;
	float2 sunFlat = TESR_CoverMapSun.xy;
	float sunFlatLength = length(sunFlat);
	[branch]
	if (known > 0.5f && TESR_CoverMapSun.w > 0.0f && TESR_CoverMapSun.z > 0.0f && sunFlatLength > 0.02f)
	{
		float2 towards = sunFlat / sunFlatLength;
		float rise = TESR_CoverMapSun.z / sunFlatLength;
		[unroll]
		for (int k = 0; k < 6; k++)
		{
			float dist = 12.0f * exp2((float)k);
			float2 offset = floor(towards * (dist / texelSize) + 0.5f);
			float other = NeighbourHeight(texel, pos, offset, -1e9f);
			sunlit *= other > height + dist * rise + 16.0f ? 0.0f : 1.0f;
		}
	}

	float drift = 1.0f;
	[branch]
	if (known > 0.5f && TESR_CoverMapSnowRates.x > 0.0f && TESR_CoverMapSnowRates.z > 0.0f)
	{
		float reach = max(floor(32.0f / texelSize + 0.5f), 1.0f);
		float h0 = NeighbourHeight(texel, pos, float2(reach, 0.0f), 1e9f);
		float h1 = NeighbourHeight(texel, pos, float2(-reach, 0.0f), 1e9f);
		float h2 = NeighbourHeight(texel, pos, float2(0.0f, reach), 1e9f);
		float h3 = NeighbourHeight(texel, pos, float2(0.0f, -reach), 1e9f);
		float4 h = float4(h0, h1, h2, h3);
		float4 valid = step(h, 1e8f);
		float count = dot(valid, 1.0f);
		float mean = dot(h * valid, 1.0f) / max(count, 1.0f);
		drift = count > 0.5f ? 1.0f + TESR_CoverMapSnowRates.z * clamp((mean - height) / 32.0f, -1.0f, 1.0f) : 1.0f;
	}

	float sunWet = lerp(TESR_CoverMapShade.x, 1.0f, sunlit * TESR_CoverMapSun.w);
	float sunSnow = lerp(TESR_CoverMapShade.y, 1.0f, sunlit * TESR_CoverMapSun.w);
	float wet = saturate(state.r + TESR_CoverMapRates.x - TESR_CoverMapRates.y * sunWet);
	float puddle = saturate(state.g + TESR_CoverMapRates.z - TESR_CoverMapRates.w * sunWet);
	float snow = saturate(state.b + TESR_CoverMapSnowRates.x * drift - TESR_CoverMapSnowRates.y * sunSnow);

	float trodden = state.a;
	[branch]
	if (TESR_CoverMapSnowRates.w > 0.0f)
	{
		float2 a = TESR_CoverMapFootA.xy;
		float2 ab = TESR_CoverMapFootB.xy - a;
		float t = saturate(dot(rel - a, ab) / max(dot(ab, ab), 0.0001f));
		float2 q = a + ab * t;
		float feetZ = lerp(TESR_CoverMapFootA.z, TESR_CoverMapFootB.z, t);
		float stroke = saturate(1.0f - length(rel - q) / TESR_CoverMapFootB.w);
		stroke *= known > 0.5f ? 1.0f - smoothstep(40.0f, 80.0f, abs(height - feetZ)) : 1.0f;
		trodden = max(trodden, stroke);
	}
	trodden = saturate(trodden - 3.0f * TESR_CoverMapSnowRates.x) * saturate(snow * 20.0f);

	UpdateOut OUT;
	OUT.main = float4(wet, puddle, snow, trodden);
	OUT.aux = float4(known > 0.5f ? EncodeHeight(height) : 0.0f, sunlit, known > 0.5f ? source : 0.0f, 0.0f);
	return OUT;
}

float3 DebugColour(float4 c, float3 base)
{
	float amount = saturate(max(c.r, max(c.g, c.b)));
	float3 tint = c.r * float3(0.05f, 0.30f, 1.0f) + c.g * float3(0.0f, 0.55f, 0.55f) + c.b * float3(0.9f, 0.9f, 0.9f);
	float3 shown = base * (1.0f - 0.75f * amount) + min(tint, 1.0f) * 0.85f;
	return lerp(shown, float3(0.55f, 0.32f, 0.12f), 0.85f * saturate(c.a));
}

float4 DebugPS(VSOUT IN) : COLOR0
{
	float4 color = tex2D(TESR_RenderedBuffer, IN.UVCoord);
	float2 pixel = IN.UVCoord / TESR_ReciprocalResolution.xy;
	float side = TESR_CoverMapDebug.y / TESR_ReciprocalResolution.y;
	float2 local = (pixel - 16.0f) / side;
	if (TESR_CoverMapDebug.x > 0.5f && TESR_CoverMapDebug.x < 1.5f && local.x >= 0.0f && local.y >= 0.0f && local.x <= 1.0f && local.y <= 1.0f)
	{
		float span = TESR_CoverMapWindow.z;
		float2 rel = float2(local.x - 0.5f, 0.5f - local.y) * span;
		float2 uv = rel * TESR_CoverMapData.x + TESR_CoverMapData.yz;
		float4 c = tex2Dlod(TESR_CoverMapBuffer, float4(uv, 0.0f, 0.0f));
		c.a = CoverMapTrodden(c.a);
		float4 aux = tex2Dlod(TESR_CoverMapAux, float4(uv, 0.0f, 0.0f));
		float known = HeightKnown(aux.r);
		float3 shown = DebugColour(c, float3(0.22f, 0.22f, 0.22f));
		shown *= known > 0.5f ? lerp(0.6f, 1.0f, aux.g) : 1.0f;
		float contour = known > 0.5f && frac(aux.r * HeightRange / 256.0f) < 0.2f ? 1.0f : 0.0f;
		shown *= 1.0f - 0.45f * contour;
		float hatch = frac((pixel.x + pixel.y) / 10.0f) < 0.5f ? 1.0f : 0.0f;
		shown *= known > 0.5f ? 1.0f : 1.0f - 0.35f * hatch;
		float2 fromCorner = rel - TESR_CoverMapWindow.xy;
		float edge = min(min(fromCorner.x, span - fromCorner.x), min(fromCorner.y, span - fromCorner.y));
		float pixelUnits = span / side;
		shown = abs(edge) < 2.0f * pixelUnits ? float3(1.0f, 0.85f, 0.0f) : shown;
		shown = edge < -2.0f * pixelUnits ? shown * 0.35f : shown;
		shown = length((local - 0.5f) * side) < 4.0f ? float3(1.0f, 0.0f, 0.0f) : shown;
		shown = TESR_CoverMapGlobal.w > 0.5f ? shown : float3(0.5f, 0.0f, 0.5f);
		return float4(shown, 1.0f);
	}
	if (TESR_CoverMapDebug.x > 1.5f)
	{
		float depth = readDepthLod(IN.UVCoord);
		if (depth < farZ * 0.99f)
		{
			float3 rel = toWorld(IN.UVCoord) * depth;
			float inside;
			float4 c = CoverMapReadAll(rel.xy, inside);
			c.a = CoverMapTrodden(c.a);
			float3 shown = DebugColour(c, color.rgb);
			shown = lerp(shown, shown * float3(1.0f, 0.55f, 0.55f), 1.0f - inside);
			color.rgb = lerp(color.rgb, shown, 0.75f);
		}
	}
	return color;
}

technique UpdateTechnique
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 UpdatePS();
	}
}

technique DebugTechnique
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 DebugPS();
	}
}
