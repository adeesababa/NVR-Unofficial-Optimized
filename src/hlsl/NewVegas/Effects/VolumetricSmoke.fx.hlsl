// GunFX volumetric smoke (prototype).
// GunFX.dll keeps its smoke (heat smoke, after-fire trail, muzzle puffs, ejection smoke) as chains of points in world
// space and hands over one record per tube segment between neighbouring points (or per lone point, a soft sphere).
// Every segment is a Gaussian tube (density falls off with distance from its axis, radius and density blended along
// it), integrated along the view ray in closed form at the ray's closest approach, so the smoke is one continuous
// volume however far apart the points are.
// Two steps: VolumetricSmokeSegments draws each segment as a screen quad over the area it can reach and adds into
// TESR_SmokeBuffer its optical depth (r), the share of it the light reaches (g), the muzzle flash's light on it (b)
// and its age (a),
// so the cost follows the smoke's size on screen, not the number of segments; VolumetricSmoke then lights the total and
// lays it over the image. Look: noise attached to the smoke makes its outline lumpy and eats into its edges (more as
// it ages); light passing through the smoke toward the sun (indoors: from above) leaves the far side darker, and the
// smoke glows when you look toward the sun; each shot's muzzle flash lights the smoke around it; fresh smoke is whiter
// and greys with age, thin smoke has a cool tint and light through it warms slightly; it is shaded as if round and
// lumpy (lit side and top brighter, lumps catching light on one side), for a sense of depth; it takes its ambient light from
// the view, is darker deep inside, fades behind scene surfaces and close to the camera, and your own first-person gun
// stays in front of it.

float4 TESR_ReciprocalResolution;
float4 TESR_SunDirection;
float4 TESR_SunColor;
float4 TESR_SunAmbient;

float4 SmokeInfo;        // y: debug (solid red, seen through walls), z: 1 outdoors (sun light), 0 indoors, w: 1 = gun in front
float4 SmokeParams;      // x: edge detail, y: brightness, z: self-shadow, w: clock (seconds)
float4 SmokeFlash;       // xyz: the muzzle flash (NVR's camera-centred space), w: its light now (0 = none)
float4 SmokeFlashColor;  // rgb: its colour, w: its reach (game units)
float4 SmokeLook;        // x: colour by thickness and age (0 = plain grey, 1 = normal), y: depth shading (0 = flat, 1 = normal)

sampler2D TESR_RenderedBuffer : register(s0) = sampler_state {
	ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE;
};
sampler2D TESR_DepthBuffer : register(s1) = sampler_state {
	ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE;
};
sampler2D TESR_SmokeBuffer : register(s2) = sampler_state {
	ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE;
};
// Depth of the first-person model only (NVR clears it in third person); above 0 where the gun is drawn.
sampler2D TESR_DepthBufferViewModel : register(s3) = sampler_state {
	ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE;
};

#include "Includes/Depth.hlsl"

float hash3(float3 p) {
	p = frac(p * 0.3183099 + 0.1);
	p *= 17.0;
	return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}
float noise3(float3 x) {
	float3 i = floor(x);
	float3 f = frac(x);
	f = f * f * (3.0 - 2.0 * f);
	return lerp(lerp(lerp(hash3(i), hash3(i + float3(1, 0, 0)), f.x), lerp(hash3(i + float3(0, 1, 0)), hash3(i + float3(1, 1, 0)), f.x), f.y),
	            lerp(lerp(hash3(i + float3(0, 0, 1)), hash3(i + float3(1, 0, 1)), f.x), lerp(hash3(i + float3(0, 1, 1)), hash3(i + float3(1, 1, 1)), f.x), f.y), f.z);
}
// erfc (Abramowitz & Stegun 7.1.27, error below 0.0005).
float erfcApprox(float x) {
	float z = abs(x);
	float t = 1.0 + z * (0.278393 + z * (0.230389 + z * (0.000972 + z * 0.078108)));
	float e = 1.0 / (t * t * t * t);
	return x >= 0.0 ? e : 2.0 - e;
}
// The light the smoke is lit by: the sun outdoors, from above indoors.
float3 LightDirection() { return SmokeInfo.z > 0.5 ? normalize(TESR_SunDirection.xyz) : float3(0.0, 0.0, 1.0); }

// ---- Step 1: one quad per segment, adding its optical depth ------------------------------------------------------
// Each vertex of a segment's quad carries the whole record (the same for all four corners):
//   a: end A x, y, z (NVR's camera-centred world space), radius
//   b: end B x, y, z (the same), radius (same as A for a lone sphere)
//   c: density at A, density at B, age 0..1, end weights (A 0..15 + 16 * B 0..15, + 256 * seed)
//   d: the smoke's own noise coordinate at A and at B (it travels with the smoke), age 0..1 at A and at B
struct SEGMENT { float4 position : POSITION0; float4 a : TEXCOORD0; float4 b : TEXCOORD1; float4 c : TEXCOORD2; float4 d : TEXCOORD3; };
SEGMENT SegmentVS(SEGMENT input) { return input; }

float4 SegmentPS(SEGMENT input, float2 vpos : VPOS) : COLOR0 {
	float2 uv = (vpos + 0.5) * TESR_ReciprocalResolution.xy;
	// NVR's world space is centred on the camera (objects are drawn at their position minus TESR_CameraPosition): the
	// reconstructed position and the eye (the inverse view's origin) are in that space, and NVR hands the smoke over in it.
	float viewDepth;
	float3 world = reconstructWorldPosition(uv, viewDepth).xyz;
	float3 cam = mul(float4(0.0, 0.0, 0.0, 1.0), TESR_InvViewTransform).xyz;
	float3 toPixel = world - cam;
	float sceneDist = length(toPixel);
	float3 dir = toPixel / max(sceneDist, 0.0001);
	float debug = SmokeInfo.y;

	float4 a = input.a;
	float4 b = input.b;
	float4 c = input.c;
	float4 d = input.d;
	float3 A = a.xyz - cam;
	float3 AB = b.xyz - a.xyz;
	// Closest approach between the ray (t * dir, 0 <= t <= sceneDist) and the segment A + s * AB.
	float len2 = dot(AB, AB);
	float sRaw = 0.0;
	if (len2 > 0.0001) {
		float bb = dot(dir, AB), cc = dot(dir, A), ee = dot(AB, A);
		float denom = len2 - bb * bb;
		sRaw = denom > 0.00001 ? (bb * cc - ee) / denom : 0.0;
	}
	// Joints: an end shared with the neighbouring segment does not stop at a flat cut there (at a bend the two cuts
	// leave a thin crack between them). Instead the segment runs on one radius past the joint, fading out, while the
	// neighbour fades in: the two always add up to the whole, whatever the bend. A strand's free ends are rounded.
	// The flags (whole numbers; rounded so the interpolated value stays exact) give each end's weight: 1 for a free end,
	// less at a joint (how much of the wedge outside a bend to fill).
	float flags = floor(c.w + 0.5);
	float fillA = fmod(flags, 16.0) / 15.0;
	float fillB = fmod(floor(flags / 16.0 + 0.001), 16.0) / 15.0;
	float segLen = sqrt(len2);
	float eA = fillA < 0.999 ? min(a.w / max(segLen, 0.01), 0.5) : 0.0;   // how far it runs on past a joint (segment lengths)
	float eB = fillB < 0.999 ? min(b.w / max(segLen, 0.01), 0.5) : 0.0;
	float s = clamp(sRaw, -eA, 1.0 + eB);
	float sEnd = saturate(s);                                  // radius, density and age stop at the ends
	float along = 1.0;
	if (eA > 0.0) along *= lerp(fillA, 1.0, smoothstep(-eA, eA, s));
	if (eB > 0.0) along *= lerp(fillB, 1.0, 1.0 - smoothstep(1.0 - eB, 1.0 + eB, s));
	float3 P = A + s * AB;
	float r = max(lerp(a.w, b.w, sEnd), 0.05);
	float tc = dot(P, dir);
	float t = clamp(tc, 0.0, sceneDist + debug * 100000.0);   // the debug view sees through walls
	float3 off = P - t * dir;                                  // from the ray to the axis
	float rho2 = dot(off, off);
	if (rho2 > 9.0 * r * r) return 0.0;                        // nowhere near the ray
	float3 axis = segLen > 0.01 ? AB / segLen : float3(0.0, 0.0, 1.0);
	float age = lerp(d.z, d.w, sEnd);

	// Edge detail: noise in the smoke's own frame (across the tube in radii, along it by the smoke's own coordinate, so
	// it travels and widens with the smoke and drifts slowly). A larger noise makes the outline lumpy; a finer one
	// eats into the edges, more as the smoke ages; the thick core stays whole.
	float3 q = (-off / r) * 1.2 + axis * (lerp(d.x, d.y, s) + SmokeParams.w * 0.1);
	float lumps = noise3(q);
	float fine = noise3(q * 2.7 + 13.1);
	float re = r * (0.85 + 0.35 * lumps);
	if (rho2 > 6.25 * re * re) return 0.0;                     // 2.5 radii
	float g = exp(-rho2 / (re * re));
	float erode = SmokeParams.x * (0.25 + 0.75 * age) * (1.0 - fine);
	g = saturate((g - erode) / max(1.0 - erode, 0.05));
	if (g <= 0.0) return 0.0;

	// Length of the ray inside the tube: crossing it ~ sqrt(pi) r / sin(angle), capped by the segment length.
	float sinA = segLen > 0.01 ? length(cross(dir, AB)) / segLen : 1.0;
	float path = 1.7725 * re / max(sinA, 1.7725 * re / (segLen + 1.7725 * re));
	// Behind the scene surface: fade out over the radius (at least 2 units; not in the debug view). Close to the
	// camera: fade out too, so walking into your own smoke never fills the screen.
	float occ = max(saturate((sceneDist - tc) / max(r, 2.0) + 0.5), debug);
	float nearFade = saturate((tc - 6.0) / 18.0);
	float density = lerp(c.x, c.y, sEnd);
	float dens = density * g * path * along * occ * nearFade;

	// Light reaching this point through the tube, from the light's side: the tube's density integrated from here
	// toward the light (closed form for a Gaussian tube, capped by the segment's length when the light runs along it).
	float3 L = LightDirection();
	float3 o = -off;                                           // this point, from the axis
	float3 Lp = L - axis * dot(L, axis);                       // the light's direction across the tube
	float lp = length(Lp);
	float x = dot(o, Lp) / max(lp, 0.0001);                    // how far toward the light this point already is
	float side2 = max(dot(o, o) - x * x, 0.0);
	float lightPath = 0.8862 * re / max(lp, 0.15) * exp(-side2 / (re * re)) * erfcApprox(x / re);
	lightPath = min(lightPath, segLen + 1.7725 * re);
	float lit = exp(-density * lightPath * 3.0 * SmokeParams.z);
	// Depth: shade the tube as if it had a soft, round surface (its side and top toward the light brighter, the far side
	// darker), even where it is too thin to shadow itself. Its normal: across the tube where the ray passes, turned
	// toward you the nearer the ray passes to the axis. Measured against the middle (the part facing you), so the smoke
	// gains shape without getting darker or brighter overall.
	float3 across = o / (1.5 * re);
	float3 normal = normalize(across - dir * sqrt(saturate(1.0 - dot(across, across))) + 0.0001);
	float shape = clamp(1.0 + 0.4 * (dot(normal, L) - dot(-dir, L)), 0.7, 1.35);
	lit *= lerp(1.0, shape, SmokeLook.y);

	// Muzzle flash: it lights the smoke within about its reach.
	float3 toFlash = SmokeFlash.xyz - (cam + t * dir);
	float flash = SmokeFlash.w * exp(-dot(toFlash, toFlash) / max(SmokeFlashColor.w * SmokeFlashColor.w, 1.0));
	return float4(dens, dens * lit, dens * flash, dens * age);
}

// ---- Step 2: light the total and lay it over the image -----------------------------------------------------------
struct VSIN { float4 position : POSITION0; float2 uv : TEXCOORD0; };
struct VSOUT { float4 position : POSITION0; float2 uv : TEXCOORD0; };
VSOUT FrameVS(VSIN input) { VSOUT o; o.position = input.position; o.uv = input.uv; return o; }

float4 CompositePS(VSOUT input) : COLOR0 {
	float4 color = tex2D(TESR_RenderedBuffer, input.uv);
	float4 smokeBuffer = tex2D(TESR_SmokeBuffer, input.uv);
	float tau = smokeBuffer.r;
	if (tau < 0.0005) return color;
	// Your own gun (the first-person model) is always in front of the smoke, so smoke never shows inside it. (It is
	// drawn with its own field of view, so comparing its depth with the smoke's is never exact near the gun.) Not in
	// the debug view, which shows all the smoke.
	if (SmokeInfo.w > 0.5 && SmokeInfo.y < 0.5 && tex2D(TESR_DepthBufferViewModel, input.uv).x > 0.0) return color;
	float transmittance = exp(-tau);
	if (SmokeInfo.y > 0.5) {
		color.rgb = lerp(color.rgb, float3(1.0, 0.05, 0.05), saturate(1.0 - exp(-4.0 * tau)));
		return color;
	}
	float lit = saturate(smokeBuffer.g / tau);     // share of the smoke the light reaches
	float flash = smokeBuffer.b / tau;             // muzzle flash light on it
	float age = saturate(smokeBuffer.a / tau);     // how old the smoke here is (0..1 of its life)
	// Ambient light: the smoke scatters the light of its surroundings, so it looks lighter than dark things behind it.
	// It is taken from the whole view (a 4 x 4 grid of samples, the same for every pixel), so it fits interiors, whose
	// light the sun values do not describe, and draws no edges of nearby objects (the gun) into the smoke.
	float3 env = 0.0;
	[unroll] for (int gy = 0; gy < 4; gy++)
		[unroll] for (int gx = 0; gx < 4; gx++)
			env += tex2Dlod(TESR_RenderedBuffer, float4(0.125 + 0.25 * gx, 0.125 + 0.25 * gy, 0, 0)).rgb;
	env = 1.8 * env / 16.0 + 0.05;
	env = lerp(dot(env, float3(0.299, 0.587, 0.114)).xxx, env, 0.5);   // smoke is grey: half the surroundings' tint
	float viewDepth;
	float3 world = reconstructWorldPosition(input.uv, viewDepth).xyz;
	float3 dir = normalize(world - mul(float4(0.0, 0.0, 0.0, 1.0), TESR_InvViewTransform).xyz);
	// Direct light, scattered mostly forward (Henyey-Greenstein, g 0.6, 1 straight toward the light): the smoke glows
	// when you look toward the sun, and its far side (less light reaches it) is darker.
	float cosView = dot(dir, LightDirection());
	float forward = 0.064 / pow(1.36 - 1.2 * cosView, 1.5);
	float outdoors = SmokeInfo.z;
	// Outdoors the sun is the main light (the sky's ambient a smaller part), so the lit and shaded sides differ clearly.
	float3 ambient = lerp(env, 0.72 * TESR_SunAmbient.rgb, 0.5 * outdoors) * lerp(0.75, 0.8, outdoors);
	float3 direct = outdoors > 0.5 ? 0.5 * TESR_SunColor.rgb * (0.4 + 1.1 * forward) : 0.45 * env;   // indoors: soft, from above
	float deep = 1.0 - 0.5 * SmokeParams.z * (1.0 - exp(-tau));                                        // darker deep inside
	float3 smoke = (ambient * deep + direct * lit + SmokeFlashColor.rgb * flash) * SmokeParams.y;
	// Relief: the smoke's thickness read like a height map a couple of pixels to each side, lit from the light's
	// direction on screen, so lumps and overlaps get a lit and a shaded edge (a flat area stays as it is).
	if (SmokeLook.y > 0.001) {
		float2 px = TESR_ReciprocalResolution.xy * 2.0;
		float hL = 1.0 - exp(-tex2D(TESR_SmokeBuffer, input.uv - float2(px.x, 0.0)).r);
		float hR = 1.0 - exp(-tex2D(TESR_SmokeBuffer, input.uv + float2(px.x, 0.0)).r);
		float hU = 1.0 - exp(-tex2D(TESR_SmokeBuffer, input.uv - float2(0.0, px.y)).r);
		float hD = 1.0 - exp(-tex2D(TESR_SmokeBuffer, input.uv + float2(0.0, px.y)).r);
		float3 bump = normalize(float3(hL - hR, hD - hU, 0.3));                   // screen: x right, y up, z toward you
		float3 lightView = mul((float3x3)TESR_InvViewTransform, LightDirection());  // x right, y up, z forward
		float3 lightScreen = normalize(float3(lightView.x, lightView.y, 0.3 - lightView.z));
		float relief = (dot(bump, lightScreen) * 0.5 + 0.5) / max(lightScreen.z * 0.5 + 0.5, 0.2);
		smoke *= lerp(1.0, clamp(relief, 0.8, 1.25), SmokeLook.y);
	}
	// Colour: fresh smoke is whiter and a little brighter, greying as it ages; thin smoke scatters blue a little more
	// (a cool, bluish grey), thick smoke is neutral; light coming through the smoke loses a little blue (the scene
	// behind it warms slightly).
	float tint = SmokeLook.x;
	float thin = exp(-1.5 * tau);
	smoke *= lerp(1.0.xxx, float3(0.86, 0.93, 1.06), thin * tint) * lerp(1.0, 1.12 - 0.24 * age, tint);
	float3 through = exp(-tau * lerp(1.0.xxx, float3(0.93, 1.0, 1.08), tint));
	color.rgb = color.rgb * through + smoke * (1.0 - through);
	return color;
}

// First technique: the one NVR's effect chain draws (step 2). VolumetricSmoke.h runs step 1 itself just before.
technique VolumetricSmoke {
	pass P0 {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 CompositePS();
		ZEnable = FALSE; ZWriteEnable = FALSE; AlphaBlendEnable = FALSE; AlphaTestEnable = FALSE;
		CullMode = NONE; ColorWriteEnable = 15;
	}
}

technique VolumetricSmokeSegments {
	pass P0 {
		VertexShader = compile vs_3_0 SegmentVS();
		PixelShader = compile ps_3_0 SegmentPS();
		ZEnable = FALSE; ZWriteEnable = FALSE; AlphaTestEnable = FALSE; StencilEnable = FALSE; CullMode = NONE;
		AlphaBlendEnable = TRUE; SrcBlend = ONE; DestBlend = ONE; BlendOp = ADD; ColorWriteEnable = 15;
	}
}
