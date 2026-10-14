float4 TESR_ReciprocalResolution;
float4 TESR_BodycamLens;
float4 TESR_BodycamImage;
float4 TESR_BodycamNoise;
float4 TESR_BodycamText;
float4 TESR_BodycamFrame;
float4 TESR_BodycamMotion;
float4 TESR_BodycamGrade;
float4 TESR_BodycamVideo;
float4 TESR_BodycamAdapt;

sampler2D TESR_RenderedBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_BodycamTextBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_BodycamHoldBuffer : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_BodycamExposureBuffer : register(s3) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };

static const float2 TextTextureSize = float2(256.0, 12.0);

struct VSOUT {
	float4 vertPos : POSITION;
	float2 UVCoord : TEXCOORD0;
};

struct VSIN {
	float4 vertPos : POSITION0;
	float2 UVCoord : TEXCOORD0;
};

VSOUT FrameVS(VSIN IN) {
	VSOUT OUT = (VSOUT)0.0f;
	OUT.vertPos = IN.vertPos;
	OUT.UVCoord = IN.UVCoord;
	return OUT;
}

float Hash(float2 p) {
	float3 p3 = frac(float3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return frac((p3.x + p3.y) * p3.z);
}

float Luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

float3 ClipToWhite(float3 c) {
	c = max(c, 0.0);
	float m = max(c.r, max(c.g, c.b));
	return m <= 1.0 ? c : lerp(c / m, 1.0, saturate((m - 1.0) * 0.8));
}

float ValueNoise(float2 p) {
	float2 i = floor(p), f = frac(p);
	f = f * f * (3.0 - 2.0 * f);
	return lerp(lerp(Hash(i), Hash(i + float2(1, 0)), f.x), lerp(Hash(i + float2(0, 1)), Hash(i + float2(1, 1)), f.x), f.y);
}

float LensDirt(float2 uv) {
	float2 p = uv * float2(TESR_ReciprocalResolution.z, 1.0);
	float blotch = smoothstep(0.55, 0.85, ValueNoise(p * 3.0 + 11.0) * 0.65 + ValueNoise(p * 7.0 + 3.0) * 0.35);
	float2 cell = floor(p * 24.0);
	float2 inCell = frac(p * 24.0) - 0.5;
	float speck = Hash(cell + 41.0) > 0.93 ? smoothstep(0.35, 0.1, length(inCell)) : 0.0;
	return saturate(blotch * 0.7 + speck * 0.6);
}

float2 Bend(float2 uv, float scale) {
	float aspect = TESR_ReciprocalResolution.z;
	float2 p = (uv * 2.0 - 1.0) * float2(aspect, 1.0);
	float r2 = dot(p, p);
	p *= (1.0 + TESR_BodycamLens.x * r2) * scale / TESR_BodycamLens.y;
	return p / float2(aspect, 1.0) * 0.5 + 0.5;
}

float3 Read(float2 uv) {
	return tex2Dlod(TESR_RenderedBuffer, float4(uv, 0.0, 0.0)).rgb;
}

float3 Smeared(float2 uv, bool moving) {
	[branch] if (!moving) return Read(uv);
	float3 c = 0.0;
	[unroll] for (int i = 0; i < 7; i++) c += Read(uv + TESR_BodycamMotion.xy * (i / 6.0 - 0.5));
	return c / 7.0;
}

float4 MeasurePS(VSOUT IN) : COLOR0 {
	float sum = 0.0;
	[unroll] for (int y = 0; y < 8; y++) {
		[unroll] for (int x = 0; x < 8; x++) {
			float3 c = tex2Dlod(TESR_RenderedBuffer, float4((float2(x, y) + 0.5) / 8.0, 0.0, 0.0)).rgb;
			sum += log(max(Luma(pow(saturate(c), 2.2)), 1e-3));
		}
	}
	float now = sum / 64.0;
	float2 last = log(max(tex2Dlod(TESR_BodycamExposureBuffer, float4(0.5, 0.5, 0.0, 0.0)).rg, 1e-4));
	if (TESR_BodycamAdapt.w < 0.5) last = now.xx;
	float fast = lerp(last.x, now, 1.0 - exp(-TESR_BodycamAdapt.y / 0.08));
	float slow = lerp(last.y, fast, 1.0 - exp(-TESR_BodycamAdapt.y / max(TESR_BodycamAdapt.z, 0.05)));
	return float4(exp(fast), exp(slow), 0.0, 1.0);
}

float4 BodycamPS(VSOUT IN) : COLOR0 {
	float2 px = TESR_ReciprocalResolution.xy;
	float2 uv = IN.UVCoord;
	[branch] if (TESR_BodycamVideo.w > 0.5) return tex2Dlod(TESR_BodycamHoldBuffer, float4(uv, 0.0, 0.0));
	float scale1080 = 1.0 / (px.y * 1080.0);

	float2 blockUV = (floor(uv / (px * 8.0)) + 0.5) * px * 8.0;
	float2 shutter = TESR_BodycamVideo.yz * (uv.y - 0.5);
	float2 at = Bend(uv, 1.0) + shutter;
	float edge = length((uv * 2.0 - 1.0) * float2(TESR_ReciprocalResolution.z, 1.0));

	float fringe = TESR_BodycamLens.z * scale1080 * px.y * edge * edge * 0.5;
	float2 outward = normalize(at - 0.5 + 1e-5);
	bool moving = length(TESR_BodycamMotion.xy / px) > 0.75;
	float3 colour;
	colour.r = Smeared(at + outward * fringe, moving).r;
	colour.g = Smeared(at, moving).g;
	colour.b = Smeared(at - outward * fringe, moving).b;

	float blurRadius = (TESR_BodycamFrame.y * edge * edge * 2.0 + TESR_BodycamGrade.y * 2.5) * px.y * scale1080;
	[branch] if (blurRadius > px.y * 0.5) {
		float2 r = blurRadius * float2(px.x / px.y, 1.0);
		float3 ring = 0.0;
		float turn = frac(52.9829189 * frac(dot(uv / px, float2(0.06711056, 0.00583715)))) * 1.570796;
		[unroll] for (int k = 0; k < 4; k++) {
			float a = k * 1.570796 + turn;
			ring += Read(at + r * float2(cos(a), sin(a)) * 0.5);
			ring += Read(at + r * float2(cos(a + 0.785398), sin(a + 0.785398)));
		}
		colour = colour * 0.2 + ring * 0.1;
	}

	float2 step = px * 1.25;
	float3 around = (Read(at + float2(step.x, 0.0)) + Read(at - float2(step.x, 0.0)) + Read(at + float2(0.0, step.y)) + Read(at - float2(0.0, step.y))) * 0.25;
	colour += (colour - around) * TESR_BodycamImage.x;

	float3 glow = 0.0;
	[unroll] for (int i = 0; i < 8; i++) {
		float a = i * 0.785398;
		float2 way = float2(cos(a), sin(a)) * float2(px.x / px.y, 1.0);
		float2 way2 = float2(cos(a + 0.3927), sin(a + 0.3927)) * float2(px.x / px.y, 1.0);
		glow += max(Read(at + way * 0.012) - 0.65, 0.0) * 0.65;
		glow += max(Read(at + way2 * 0.035) - 0.65, 0.0) * 0.35;
	}
	colour += glow * 0.125 * TESR_BodycamImage.w * 2.0;

	float3 block = Read(Bend(blockUV, 1.0) + shutter);
	float flat = 1.0 - saturate(length(colour - around) * 8.0);
	float dark = 1.0 - saturate(Luma(block) * 2.5);
	colour = lerp(colour, block, TESR_BodycamNoise.y * flat * dark);
	[branch] if (TESR_BodycamVideo.x > 0.01) {
		float breakup = TESR_BodycamVideo.x;
		float2 bigUV = (floor(uv / (px * 16.0)) + 0.5) * px * 16.0;
		float3 big = Read(Bend(bigUV, 1.0) + shutter);
		float2 hugeUV = (floor(uv / (px * 32.0)) + 0.5) * px * 32.0;
		float3 huge = Read(Bend(hugeUV, 1.0) + shutter);
		colour = lerp(colour, block, saturate(breakup * 0.35 * (0.4 + 0.6 * flat)));
		colour = lerp(colour, big, saturate((breakup - 0.3) * 0.45 * (0.3 + 0.7 * flat)));
		colour = lerp(colour, huge, saturate((breakup - 1.2) * 0.4 * (0.2 + 0.8 * flat)));
		float levels = max(48.0 - breakup * 12.0, 8.0);
		colour = lerp(colour, floor(colour * levels + 0.5) / levels, saturate(breakup * 0.3));
	}

	float dirt = LensDirt(uv);
	float bright = Luma(glow) * 0.125 + 0.2 * Luma(colour) * Luma(colour);
	colour += dirt * bright * TESR_BodycamFrame.w;

	float gain = 1.0;
	[branch] if (TESR_BodycamAdapt.x > 0.0 && TESR_BodycamAdapt.w > 0.5) {
		float2 measured = tex2Dlod(TESR_BodycamExposureBuffer, float4(0.5, 0.5, 0.0, 0.0)).rg;
		gain = clamp(pow(max(measured.x, 1e-4) / max(measured.y, 1e-4), TESR_BodycamAdapt.x / 2.2), 0.2, 5.0);
	}
	colour *= TESR_BodycamFrame.z * gain;
	colour = pow(ClipToWhite(colour), 1.0 / (1.0 + TESR_BodycamMotion.w));
	colour = ClipToWhite((colour - 0.5) * TESR_BodycamImage.y + 0.5);
	colour *= lerp(float3(1.0, 1.0, 1.0), float3(0.93, 0.98, 1.12) / 0.9795, TESR_BodycamGrade.x);
	float luma = Luma(colour);
	colour = lerp(luma.xxx, colour, TESR_BodycamImage.z);
	colour *= lerp(float3(1.0, 1.0, 1.0), float3(0.94, 1.0, 0.95), TESR_BodycamNoise.w);
	colour = ClipToWhite(colour);
	colour = (1.0 - exp(-max(colour, 0.0) * 1.15)) / (1.0 - exp(-1.15));
	colour = saturate(colour);

	float frame = TESR_BodycamNoise.z;
	float2 cell = floor(uv / px);
	float n = Hash(cell + frame * 17.31) - 0.5;
	float3 chroma = float3(Hash(cell * 1.3 + frame * 3.7), Hash(cell * 0.7 + frame * 5.1), Hash(cell * 1.9 + frame * 7.3)) - 0.5;
	float amount = TESR_BodycamNoise.x * 0.06 * lerp(1.0, 0.35, saturate(Luma(colour) * 1.5));
	colour += (n + chroma * 0.4) * amount;

	colour *= 1.0 - TESR_BodycamLens.w * smoothstep(0.6, 1.9, edge);
	float oval = length(uv * 2.0 - 1.0);
	float ovalStart = lerp(1.6, 0.75, TESR_BodycamFrame.x);
	colour *= 1.0 - smoothstep(ovalStart, ovalStart + 0.35, oval);
	colour *= all(at >= 0.0) && all(at <= 1.0) ? 1.0 : 0.0;

	float lit = 0.0, shadow = 0.0;
	float texel = max(floor(TESR_BodycamText.y * scale1080 + 0.5), 1.0);
	float2 corner = float2(1.0 / px.x - TESR_BodycamText.z * texel - 40.0 * scale1080, 34.0 * scale1080);
	float2 pixel = uv / px;
	float2 t = (pixel - corner) / texel;
	[branch] if (TESR_BodycamText.x > 0.0 && all(t > -1.0) && all(t < TESR_BodycamText.zw + 1.0)) {
		lit = tex2Dlod(TESR_BodycamTextBuffer, float4((floor(t) + 0.5) / TextTextureSize, 0.0, 0.0)).a;
		float2 back = (pixel - corner - max(floor(texel * 0.35), 1.0)) / texel;
		shadow = tex2Dlod(TESR_BodycamTextBuffer, float4((floor(back) + 0.5) / TextTextureSize, 0.0, 0.0)).a;
	}
	colour = lerp(colour, colour * 0.25, shadow * TESR_BodycamText.x);
	colour = lerp(colour, float3(0.93, 0.93, 0.9), lit * TESR_BodycamText.x);
	return float4(saturate(colour), 1.0);
}

technique {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 BodycamPS();
	}
}

technique Measure {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 MeasurePS();
	}
}
