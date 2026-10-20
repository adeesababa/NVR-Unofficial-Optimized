float4 TESR_ReciprocalResolution;
float4 TESR_SkyColor;
float4 TESR_HorizonColor;
float4 TESR_WaterSettings;
float4 TESR_ReflectionsData;
float4 TESR_ReflectionsData2;
float4 TESR_ReflectionsData3;
float4 TESR_ReflectionsData4;
float4x4 TESR_ShadowCameraToLightTransformOrtho;
float4 TESR_ReflectionsLayout;
float4x4 TESR_ReflectionsReproject;
float4 TESR_ReflectionsCube;
float4 TESR_ReflectionsCube2;
float4 TESR_ReflectionsGrid;
float4 TESR_ReflectionsGrid2;
float4 TESR_ReflectionsGrid3;

sampler2D TESR_RenderedBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_DepthBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_NormalsBuffer : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_DepthBufferViewModel : register(s3) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_ReflectionsTrace : register(s4) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_ReflectionsBuffer : register(s5) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_ReflectionsTemp : register(s6) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_ReflectionsHistory : register(s7) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
samplerCUBE TESR_ReflectionsCapture : register(s8) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; ADDRESSW = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
samplerCUBE TESR_ReflectionsCaptureNew : register(s9) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; ADDRESSW = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
samplerCUBE TESR_ReflectionsEnv : register(s10) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; ADDRESSW = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_OrthoMapBuffer : register(s11) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_ReflectionsGridLight : register(s12) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_ReflectionsGridColor : register(s13) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
float4 NVR_ReflectionsSky;
sampler2D NVR_ReflectionsSkyVis : register(s14) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
texture NoTexture;

static const float DryReflect = TESR_ReflectionsData.x;
static const float Wetness = TESR_ReflectionsData.y;
static const float DryRoughness = TESR_ReflectionsData.z;
static const float WetRoughness = TESR_ReflectionsData.w;
static const float RayLength = TESR_ReflectionsData2.x;
static const float Steps = TESR_ReflectionsData2.y;
static const float ThicknessScale = TESR_ReflectionsData2.z;
static const float NoiseFrame = TESR_ReflectionsData2.w;
static const float HistoryWeight = TESR_ReflectionsData3.x;
static const float BlurRadius = TESR_ReflectionsData3.y;
static const float DebugView = TESR_ReflectionsData3.z;
static const float SkyFallback = TESR_ReflectionsData3.w;
static const float DrawDistance = TESR_ReflectionsData4.x;
static const float RoofMaskOn = TESR_ReflectionsData4.y;
static const float2 LowTexel = TESR_ReflectionsLayout.xy;
static const float CubeFace = TESR_ReflectionsCube.x;
static const float CubeSize = TESR_ReflectionsCube.y;
static const float CubeMip = TESR_ReflectionsCube.z;
static const float CubeReset = TESR_ReflectionsCube.w;
static const float CaptureBlend = TESR_ReflectionsCube2.x;
static const float CubeFade = TESR_ReflectionsCube2.y;
static const float CubeLastMip = TESR_ReflectionsCube2.z;
static const float CubeOn = TESR_ReflectionsCube2.w;
static const float GridOn = TESR_ReflectionsGrid2.x;
static const int RefineSteps = 5;
static const float BrightestReflection = 16.0;

struct VSOUT { float4 vertPos : POSITION; float2 UVCoord : TEXCOORD0; };
struct VSIN { float4 vertPos : POSITION0; float2 UVCoord : TEXCOORD0; };
VSOUT FrameVS(VSIN IN) { VSOUT OUT = (VSOUT)0.0f; OUT.vertPos = IN.vertPos; OUT.UVCoord = IN.UVCoord; return OUT; }

#include "Includes/Helpers.hlsl"
#include "Includes/Depth.hlsl"

float GradientNoise(float2 pixel) {
	pixel += NoiseFrame * 5.588238;
	return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

float3 ViewPosition(float2 uv, float depth) {
	return float3((uv.x * 2.0 - 1.0) / TESR_ProjectionTransform[0][0], (1.0 - uv.y * 2.0) / TESR_ProjectionTransform[1][1], 1.0) * depth;
}

float2 ToScreen(float3 view) {
	return float2(view.x * TESR_ProjectionTransform[0][0] / view.z * 0.5 + 0.5, 0.5 - view.y * TESR_ProjectionTransform[1][1] / view.z * 0.5);
}

bool IsWeapon(float2 uv) {
	float viewmodelDepth = tex2Dlod(TESR_DepthBufferViewModel, float4(uv, 0, 0)).x;
	return TESR_DepthConstants.z == 0.0 ? viewmodelDepth < 0.9 : viewmodelDepth > 0.01;
}

float3 ViewNormal(float2 uv) { return normalize(tex2Dlod(TESR_NormalsBuffer, float4(uv, 0, 0)).xyz * 2.0 - 1.0); }

float3 ViewToWorldDir(float3 v) { return mul((float3x3)TESR_ViewTransform, v); }

static const float RoofMargin = 40.0;
float OpenAbove(float2 uv, float depth) {
	float4 p = mul(float4(toWorld(uv) * depth, 1.0), TESR_ShadowCameraToLightTransformOrtho);
	p.xyz /= p.w;
	[branch] if (abs(p.x) > 1.0 || abs(p.y) > 1.0 || p.z < 0.0 || p.z > 1.0) return 1.0;
	float top = tex2Dlod(TESR_OrthoMapBuffer, float4(p.x * 0.5 + 0.5, p.y * -0.5 + 0.5, 0.0, 0.0)).r;
	float3 depthAxis = float3(TESR_ShadowCameraToLightTransformOrtho[0][2], TESR_ShadowCameraToLightTransformOrtho[1][2], TESR_ShadowCameraToLightTransformOrtho[2][2]);
	float perUnit = length(depthAxis) / max(abs(TESR_ShadowCameraToLightTransformOrtho[3][3]), 1.0e-6);
	return top > p.z - max(RoofMargin * perUnit, 0.0001) ? 1.0 : 0.0;
}

float Reflectivity(float2 uv, float depth, float3 viewNormal, out float roughness) {
	roughness = DryRoughness;
	[branch] if (depth >= DrawDistance || depth >= farZ * 0.98 || IsWeapon(uv)) return 0.0;
	float3 worldNormal = ViewToWorldDir(viewNormal);
	float floorShare = saturate((worldNormal.z - 0.75) / 0.2);
	[branch] if (floorShare <= 0.0) return 0.0;
	float worldZ = TESR_CameraPosition.z + toWorld(uv).z * depth;
	[branch] if (abs(worldZ - TESR_WaterSettings.x) < max(2.0, depth * 0.002)) return 0.0;
	float wetness = Wetness;
	[branch] if (wetness > 0.0 && RoofMaskOn > 0.5) wetness *= OpenAbove(uv, depth);
	float amount = DryReflect + wetness;
	roughness = lerp(DryRoughness, WetRoughness, wetness / max(amount, 1.0e-4));
	float fade = 1.0 - smoothstep(0.7 * DrawDistance, DrawDistance, depth);
	return min(amount, 2.0) * floorShare * fade;
}

float EnvironmentFresnel(float roughness, float NdotV) {
	const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
	const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
	float4 r = roughness * c0 + c1;
	float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
	float2 ab = float2(-1.04, 1.04) * a004 + r.zw;
	return 0.04 * ab.x + ab.y;
}

float3 SkyAlong(float3 worldDir) {
	float up = saturate(worldDir.z);
	return lerp(linearize(TESR_HorizonColor.rgb), linearize(TESR_SkyColor.rgb), sqrt(up)) * smoothstep(0.0, 0.15, worldDir.z);
}

float3 Tame(float3 c) { return c / (1.0 + dot(c, float3(0.2126, 0.7152, 0.0722))); }
float3 Untame(float3 t) { return t / max(1.0 - dot(t, float3(0.2126, 0.7152, 0.0722)), 1.0e-3); }

float3 FaceDirection(float face, float2 st) {
	if (face < 0.5) return float3(1.0, -st.y, -st.x);
	if (face < 1.5) return float3(-1.0, -st.y, st.x);
	if (face < 2.5) return float3(st.x, 1.0, st.y);
	if (face < 3.5) return float3(st.x, -1.0, -st.y);
	if (face < 4.5) return float3(st.x, -st.y, 1.0);
	return float3(-st.x, -st.y, -1.0);
}

float3 WorldToViewDir(float3 w) { return mul(float4(w, 0.0), TESR_ViewTransform).xyz; }

float4 EnvAlong(float3 worldDir, float roughness) {
	float lod = roughness * CubeLastMip;
	float4 sharp = texCUBElod(TESR_ReflectionsEnv, float4(worldDir, lod));
	float4 coarse = texCUBElod(TESR_ReflectionsEnv, float4(worldDir, min(lod + 2.0, CubeLastMip)));
	float4 m = sharp + coarse * (1.0 - saturate(sharp.a));
	return float4(m.rgb / max(m.a, 1.0e-4), saturate(m.a));
}

static const float GridX = 32.0, GridY = 32.0, GridZ = 16.0;
static const float GridAlbedo = 0.4;
static const float GridIrradianceScale = 1.5;
float4 GridShBasis(float3 d) { return float4(0.282095, 0.488603 * d); }
float2 GridCellUV(float3 c) { return (float2(fmod(c.z, 4.0), floor(c.z / 4.0)) * 32.0 + c.xy + 0.5) / 128.0; }

float CoveredAt(float3 p) {
	float4 m = mul(float4(p, 1.0), TESR_ShadowCameraToLightTransformOrtho);
	m.xyz /= m.w;
	[branch] if (abs(m.x) > 1.0 || abs(m.y) > 1.0 || m.z < 0.0 || m.z > 1.0) return 0.0;
	float top = tex2Dlod(TESR_OrthoMapBuffer, float4(m.x * 0.5 + 0.5, m.y * -0.5 + 0.5, 0.0, 0.0)).r;
	float3 depthAxis = float3(TESR_ShadowCameraToLightTransformOrtho[0][2], TESR_ShadowCameraToLightTransformOrtho[1][2], TESR_ShadowCameraToLightTransformOrtho[2][2]);
	float perUnit = length(depthAxis) / max(abs(TESR_ShadowCameraToLightTransformOrtho[3][3]), 1.0e-6);
	return top < m.z - max(RoofMargin * perUnit, 0.0001) ? 1.0 : 0.0;
}

float4 GridAlong(float3 start, float3 worldR) {
	float size = TESR_ReflectionsGrid.w;
	float3 dims = float3(GridX, GridY, GridZ);
	float4 toward = GridShBasis(-worldR);
	float light = 0.0, weight = 0.0, inside = 0.0, covered = 0.0;
	float4 colour = 0.0;
	bool open = true;
	[loop] for (float k = 0.0; k < 3.5; k += 1.0) {
		float t = size * (k < 0.5 ? 0.75 : exp2(k));
		float3 p = start + worldR * t;
		float3 g = (p - TESR_ReflectionsGrid.xyz) / size - 0.5;
		float3 edge = min(g + 0.5, dims - 0.5 - g);
		float fade = saturate(min(min(edge.x, edge.y), edge.z) / 2.0);
		[branch] if (open && fade > 0.0) {
			g = clamp(g, 0.0, dims - 1.0);
			float z0 = floor(g.z), z1 = min(z0 + 1.0, GridZ - 1.0);
			float4 sh = lerp(tex2Dlod(TESR_ReflectionsGridLight, float4(GridCellUV(float3(g.xy, z0)), 0.0, 0.0)),
				tex2Dlod(TESR_ReflectionsGridLight, float4(GridCellUV(float3(g.xy, z1)), 0.0, 0.0)), g.z - z0);
			light += max(dot(sh, toward), 0.0) * fade;
			[branch] if (TESR_ReflectionsGrid2.z > 0.5) colour += tex2Dlod(TESR_ReflectionsGridColor, float4(GridCellUV(float3(g.xy, round(g.z))), 0.0, 0.0)) * fade;
			weight += fade;
			inside = max(inside, fade);
			[branch] if (RoofMaskOn > 0.5) {
				float c = CoveredAt(p);
				covered = max(covered, c * fade);
				open = c < 0.5;
			}
		}
	}
	[branch] if (weight <= 0.0) return 0.0;
	float3 hue = 1.0;
	[branch] if (colour.a > 1.0e-5) {
		hue = max(colour.rgb / colour.a, 0.0);
		hue = min(hue / max(luma(hue), 1.0e-4), 3.0);
	}
	float3 radiance = (light / weight * GridIrradianceScale + TESR_ReflectionsGrid3.rgb * GridAlbedo) * hue * TESR_ReflectionsGrid2.y;
	float share = inside;
	[branch] if (SkyFallback > 0.0) share *= max(covered, 1.0 - smoothstep(0.0, 0.15, worldR.z));
	return float4(radiance, share);
}

float4 CapturePS(VSOUT IN, float2 vpos : VPOS) : COLOR0 {
	float2 st = (vpos + 0.5) / CubeSize * 2.0 - 1.0;
	float3 dir = FaceDirection(CubeFace, st);
	float4 result = CubeReset > 0.0 ? 0.0 : texCUBElod(TESR_ReflectionsCapture, float4(dir, 0.0)) * (1.0 - CubeFade);

	float3 view = WorldToViewDir(dir);
	[branch] if (view.z > 0.05) {
		float2 uv = ToScreen(view);
		[branch] if (all(uv > 0.005) && all(uv < 0.995)) {
			float2 du = ToScreen(WorldToViewDir(FaceDirection(CubeFace, st + float2(2.0 / CubeSize, 0.0)))) - uv;
			float2 dv = ToScreen(WorldToViewDir(FaceDirection(CubeFace, st + float2(0.0, 2.0 / CubeSize)))) - uv;
			float3 sum = 0.0;
			float found = 0.0;
			[unroll] for (int y = 0; y < 4; y++) {
				[unroll] for (int x = 0; x < 4; x++) {
					float2 at = uv + du * ((x - 1.5) / 4.0) + dv * ((y - 1.5) / 4.0);
					bool usable = all(at > 0.0) && all(at < 1.0) && !IsWeapon(at) && readDepthLod(at) > 40.0;
					float3 c = linearize(tex2Dlod(TESR_RenderedBuffer, float4(at, 0, 0)).rgb);
					float brightest = max(c.r, max(c.g, c.b));
					sum += usable ? c * min(1.0, 32.0 / max(brightest, 1.0e-4)) : 0.0;
					found += usable ? 1.0 : 0.0;
				}
			}
			[branch] if (found > 0.0) {
				float2 edge = min(uv, 1.0 - uv);
				float blend = CaptureBlend * saturate(min(edge.x, edge.y) * 20.0) * found / 16.0;
				result = lerp(result, float4(sum / found, 1.0), blend);
			}
		}
	}
	return result;
}

float4 FilterPS(VSOUT IN, float2 vpos : VPOS) : COLOR0 {
	float2 st = (vpos + 0.5) / CubeSize * 2.0 - 1.0;
	float3 dir = FaceDirection(CubeFace, st);
	[branch] if (CubeMip < 0.5) return texCUBElod(TESR_ReflectionsCaptureNew, float4(dir, 0.0));
	float2 texel = 2.0 / CubeSize;
	float4 sum = 0.0;
	[unroll] for (int y = 0; y < 4; y++) {
		[unroll] for (int x = 0; x < 4; x++) {
			float2 offset = (float2(x, y) - 1.5) / 4.0 * 1.5 * texel;
			sum += texCUBElod(TESR_ReflectionsCaptureNew, float4(FaceDirection(CubeFace, st + offset), 0.0));
		}
	}
	return sum / 16.0;
}

float4 TracePS(VSOUT IN, float2 vpos : VPOS) : COLOR0 {
	float2 uv = (vpos + 0.5) * LowTexel;
	float2 pixelUV = uv - 0.5 * (LowTexel - TESR_ReciprocalResolution.xy);
	float depth = readDepthLod(pixelUV);
	float3 N = ViewNormal(pixelUV);
	float roughness;
	float amount = Reflectivity(pixelUV, depth, N, roughness);
	[branch] if (amount <= 0.0) return 0.0;

	float3 position = ViewPosition(pixelUV, depth);
	float3 V = normalize(position);

	float a = roughness * roughness;
	float u1 = GradientNoise(vpos), u2 = GradientNoise(vpos + float2(37.0, 17.0));
	float cosTheta = sqrt((1.0 - u1) / (1.0 + (a * a - 1.0) * u1));
	float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
	float phi = 6.2831853 * u2;
	float3 tangent = normalize(abs(N.z) < 0.999 ? cross(N, float3(0, 0, 1)) : cross(N, float3(1, 0, 0)));
	float3 bitangent = cross(N, tangent);
	float3 H = normalize(tangent * (sinTheta * cos(phi)) + bitangent * (sinTheta * sin(phi)) + N * cosTheta);
	float3 R = reflect(V, H);
	[branch] if (dot(R, N) <= 0.0) R = reflect(V, N);

	float jitter = GradientNoise(vpos + float2(11.0, 71.0));
	float lastT = 0.0;
	float lastZ = position.z;
	float hitT = -1.0;
	bool searching = true;
	[loop] for (float i = 1.0; i <= Steps && searching; i += 1.0) {
		float s = (i - 1.0 + jitter) / Steps;
		float t = RayLength * s * s + 2.0;
		float3 p = position + R * t;
		float2 at = ToScreen(p);
		[branch] if (p.z <= nearZ || any(at <= 0.0) || any(at >= 1.0)) searching = false;
		else {
			float sceneZ = readDepthLod(at);
			float behind = p.z - sceneZ;
			float thickness = (max(abs(p.z - lastZ), 20.0) * 1.5 + t * 0.03) * ThicknessScale;
			[branch] if (behind > 0.0 && behind < thickness && sceneZ < farZ * 0.98) { hitT = t; searching = false; }
			else { lastT = t; lastZ = p.z; }
		}
	}

	float3 colour = 0.0;
	float confidence = 0.0;
	[branch] if (hitT > 0.0) {
		float lo = lastT, hi = hitT;
		[unroll] for (int r = 0; r < RefineSteps; r++) {
			float m = 0.5 * (lo + hi);
			float3 q = position + R * m;
			if (q.z > readDepthLod(ToScreen(q))) hi = m; else lo = m;
		}
		float2 hit = ToScreen(position + R * hi);
		float3 hitNormal = ViewNormal(hit);
		[branch] if (!IsWeapon(hit) && dot(hitNormal, R) < 0.2) {
			colour = linearize(tex2Dlod(TESR_RenderedBuffer, float4(hit, 0, 0)).rgb);
			float2 edge = min(hit, 1.0 - hit);
			confidence = saturate(min(edge.x, edge.y) * 12.0) * (1.0 - smoothstep(0.75, 1.0, hi / RayLength));
		}
	}
	float3 sum = colour * confidence;
	float3 worldR = ViewToWorldDir(R);
	float screenShare = confidence, cubeShare = 0.0, gridShare = 0.0, skyShare = 0.0;
	float4 grid = 0.0, env = 0.0;
	[branch] if (CubeOn > 0.0 && confidence < 1.0) env = EnvAlong(worldR, roughness);
	float gridFirst = smoothstep(0.15, 0.35, roughness);
	bool gridUseful = (gridFirst > 0.0 || env.a < 1.0) && !(SkyFallback > 0.0 && RoofMaskOn < 0.5 && worldR.z >= 0.15);
	[branch] if (GridOn > 0.0 && ((confidence < 1.0 && gridUseful) || abs(DebugView - 5.0) < 0.5)) grid = GridAlong(ViewToWorldDir(position), worldR);
	[branch] if (confidence < 1.0 && (CubeOn > 0.0 || GridOn > 0.0)) {
		float missing = 1.0 - confidence;
		float cubeA = missing * env.a, gridA = (missing - cubeA) * grid.a;
		float gridB = missing * grid.a, cubeB = (missing - gridB) * env.a;
		cubeShare = lerp(cubeA, cubeB, gridFirst);
		gridShare = lerp(gridA, gridB, gridFirst);
		sum += env.rgb * cubeShare + grid.rgb * gridShare;
		confidence += cubeShare + gridShare;
	}
	[branch] if (confidence < 1.0 && SkyFallback > 0.0) {
		skyShare = (1.0 - confidence) * smoothstep(0.0, 0.15, worldR.z);
		skyShare *= lerp(1.0, lerp(NVR_ReflectionsSky.y, 1.0, tex2Dlod(NVR_ReflectionsSkyVis, float4(uv, 0, 0)).b), NVR_ReflectionsSky.x);
		sum += SkyAlong(worldR) * skyShare;
		confidence += skyShare;
	}
	colour = sum / max(confidence, 1.0e-4);
	[branch] if (DebugView > 4.5) {
		[branch] if (DebugView < 5.5) colour = grid.rgb;
		else colour = float3(gridShare, screenShare, cubeShare) + 0.5 * skyShare;
		return float4(Tame(colour), 1.0);
	}
	float brightest = max(colour.r, max(colour.g, colour.b));
	colour *= min(1.0, lerp(BrightestReflection, 3.0, saturate(roughness * 2.0)) / max(brightest, 1.0e-4));
	return float4(Tame(colour) * confidence, confidence);
}

float4 TemporalPS(VSOUT IN, float2 vpos : VPOS) : COLOR0 {
	float2 uv = (vpos + 0.5) * LowTexel;
	float4 current = tex2Dlod(TESR_ReflectionsTrace, float4(uv, 0, 0));
	float depth = readDepthLod(uv - 0.5 * (LowTexel - TESR_ReciprocalResolution.xy));

	float4 low = current, high = current;
	[unroll] for (int y = -1; y <= 1; y++) {
		[unroll] for (int x = -1; x <= 1; x++) {
			if (x == 0 && y == 0) continue;
			float4 tap = tex2Dlod(TESR_ReflectionsTrace, float4(uv + float2(x, y) * LowTexel, 0, 0));
			low = min(low, tap); high = max(high, tap);
		}
	}

	float3 world = toWorld(uv) * depth;
	float4 clip = mul(float4(world, 1.0), TESR_ReflectionsReproject);
	float w = max(clip.w, 0.001);
	float2 last = float2(clip.x / w * 0.5 + 0.5, 0.5 - clip.y / w * 0.5);
	float inside = all(last > 0.0) && all(last < 1.0) && current.a + high.a > 0.0;
	float4 history = tex2Dlod(TESR_ReflectionsHistory, float4(last, 0, 0));
	history = clamp(history, low, high);
	return lerp(current, history, HistoryWeight * inside);
}

float4 BlurAxis(sampler2D source, float2 vpos, float2 axis) {
	float2 uv = (vpos + 0.5) * LowTexel;
	float4 centre = tex2Dlod(source, float4(uv, 0, 0));
	float2 pixelUV = uv - 0.5 * (LowTexel - TESR_ReciprocalResolution.xy);
	float depth = readDepthLod(pixelUV);
	float roughness;
	float amount = Reflectivity(pixelUV, depth, ViewNormal(pixelUV), roughness);
	[branch] if (amount <= 0.0) return centre;

	float radius = BlurRadius * saturate(0.5 + roughness * 2.0);
	float sigma = max(radius * 0.5, 0.5);
	float falloff = -0.5 / (sigma * sigma);
	float tolerance = depth * 0.03 + 4.0;
	float4 sum = centre;
	float weights = 1.0;
	[unroll] for (int i = 1; i <= 8; i++) {
		float gauss = i <= radius ? exp(falloff * i * i) : 0.0;
		float2 ua = uv + axis * i * LowTexel, ub = uv - axis * i * LowTexel;
		float da = readDepthLod(ua - 0.5 * (LowTexel - TESR_ReciprocalResolution.xy)), db = readDepthLod(ub - 0.5 * (LowTexel - TESR_ReciprocalResolution.xy));
		float wa = gauss / (1.0 + abs(da - depth) / tolerance);
		float wb = gauss / (1.0 + abs(db - depth) / tolerance);
		sum += tex2Dlod(source, float4(ua, 0, 0)) * wa + tex2Dlod(source, float4(ub, 0, 0)) * wb;
		weights += wa + wb;
	}
	return sum / weights;
}

float4 BlurXPS(VSOUT IN, float2 vpos : VPOS) : COLOR0 { return BlurAxis(TESR_ReflectionsTemp, vpos, float2(1.0, 0.0)); }
float4 BlurYPS(VSOUT IN, float2 vpos : VPOS) : COLOR0 { return BlurAxis(TESR_ReflectionsTrace, vpos, float2(0.0, 1.0)); }

float4 CompositePS(VSOUT IN) : COLOR0 {
	float2 uv = IN.UVCoord;
	float4 color = tex2Dlod(TESR_RenderedBuffer, float4(uv, 0, 0));
	float depth = readDepthLod(uv);
	[branch] if (abs(DebugView - 4.0) < 0.5) {
		bool isFloor = ViewToWorldDir(ViewNormal(uv)).z > 0.75 && depth < DrawDistance && depth < farZ * 0.98;
		[branch] if (!isFloor) return float4(color.rgb * 0.15, color.a);
		float open = RoofMaskOn > 0.5 ? OpenAbove(uv, depth) : 1.0;
		return open > 0.5 ? float4((0.4 + 0.6 * saturate(Wetness)).xxx, 1.0) : float4(0.5, 0.05, 0.05, 1.0);
	}
	[branch] if (abs(DebugView - 3.0) < 0.5) {
		float4 env = EnvAlong(normalize(toWorld(uv)), 0.0);
		return float4(delinearize(env.rgb * env.a), 1.0);
	}
	float3 N = ViewNormal(uv);
	float roughness;
	float amount = Reflectivity(uv, depth, N, roughness);
	[branch] if (amount <= 0.0) return DebugView > 0.0 ? float4(color.rgb * 0.15, color.a) : color;

	float2 position = uv / LowTexel - 0.5;
	float2 base = floor(position);
	float2 fraction = position - base;
	float tolerance = depth * 0.03 + 4.0;
	float4 reflection = 0.0;
	float weights = 1.0e-5;
	[unroll] for (int y = 0; y < 2; y++) {
		[unroll] for (int x = 0; x < 2; x++) {
			float2 at = (base + float2(x, y) + 0.5) * LowTexel;
			float tapDepth = readDepthLod(at - 0.5 * (LowTexel - TESR_ReciprocalResolution.xy));
			float weight = (x ? fraction.x : 1 - fraction.x) * (y ? fraction.y : 1 - fraction.y);
			weight /= 1.0 + abs(tapDepth - depth) / tolerance;
			reflection += tex2Dlod(TESR_ReflectionsBuffer, float4(at, 0, 0)) * weight;
			weights += weight;
		}
	}
	reflection /= weights;

	float3 V = normalize(ViewPosition(uv, depth));
	float share = saturate(amount * EnvironmentFresnel(roughness, saturate(dot(-V, N)))) * saturate(reflection.a);
	float3 reflected = Untame(reflection.rgb / max(reflection.a, 1.0e-4));

	[branch] if (DebugView > 4.5) return float4(delinearize(reflected), 1.0);
	[branch] if (DebugView > 1.5) return float4(1.0 - reflection.a, reflection.a, amount * 0.5, 1.0);
	[branch] if (DebugView > 0.5) return float4(delinearize(reflected * share), color.a);

	float3 lin = linearize(color.rgb);
	lin = lerp(lin, reflected, share);
	return float4(delinearize(lin), color.a);
}

technique Composite {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 CompositePS();
	}
}

technique Trace {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 TracePS();
		Texture[4] = <NoTexture>;
		Texture[6] = <NoTexture>;
	}
}

technique BlurX {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 BlurXPS();
		Texture[4] = <NoTexture>;
	}
}

technique Temporal {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 TemporalPS();
		Texture[6] = <NoTexture>;
	}
}

technique BlurY {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 BlurYPS();
		Texture[5] = <NoTexture>;
	}
}

technique Capture {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 CapturePS();
		Texture[9] = <NoTexture>;
		Texture[10] = <NoTexture>;
	}
}

technique Filter {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 FilterPS();
		Texture[10] = <NoTexture>;
		Texture[8] = <NoTexture>;
	}
}
