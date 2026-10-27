#version 450
#extension GL_EXT_nonuniform_qualifier : enable   // a runtime-sized sampler heap (the index is uniform for a draw)
// UNOFFICIAL 2026-10-27 NVR DXVK tessellation displacement (fork patch 0021, extension 1.7-displace; docs/next/FORK2-DISPLACE.md).
// [Main.Main.Performance] NvrDxvkDisplacement, [Shaders.Terrain.Displacement].
//
// Tessellation evaluation shader of the land: every generated point gets the vertex shader's outputs interpolated from its
// triangle's corners, and is moved along the land's normal by the land textures' height (their diffuse alpha, the height map the
// land's parallax reads), weighted by the layer blend as the pixel shader blends the colours. The land's pixel shader (SLS2092-2146)
// then runs unchanged on the displaced surface: real bumps with their own silhouette and depth (so fog, AO, SSR and the deferred
// shadows see them). Compiled once per layer count (TEX_COUNT 1-7: one texture per layer, exactly the ones that pixel shader reads)
// and per winding (cw keeps the game's triangle winding with Vulkan's upper-left tessellation domain; WINDING_CCW the test variant).
//
// Strength 0 and FixedFactor 1 give the game's own triangles exactly: the clip position is the vertex shader's, interpolated, plus
// the displacement through ModelViewProj.
#ifndef TEX_COUNT
  #define TEX_COUNT 1
#endif

#ifdef WINDING_CCW
layout(triangles, fractional_odd_spacing, ccw) in;
#else
layout(triangles, fractional_odd_spacing, cw) in;
#endif

#define PASS(n) layout(location = n) in vec4 i_v##n[]; layout(location = n) out vec4 o_v##n;
PASS(1) PASS(2) PASS(3) PASS(4) PASS(5) PASS(6) PASS(7) PASS(8) PASS(9) PASS(10)
#undef PASS

layout(location = 30) in vec2 i_nvr[];   // TerrainDisplace.tesc: x displacement weight, y log2 uv spacing

out gl_PerVertex {
  vec4 gl_Position;
  float gl_ClipDistance[6];
};

layout(set = 0, binding = 0) uniform sampler s_samplers[];   // DXVK's sampler heap

// The land pixel shader's BaseMap[0..TEX_COUNT-1] (s0-s6): diffuse, height in alpha
layout(set = 1, binding = 0) uniform texture2D t_base0;
#if TEX_COUNT > 1
layout(set = 1, binding = 1) uniform texture2D t_base1;
#endif
#if TEX_COUNT > 2
layout(set = 1, binding = 2) uniform texture2D t_base2;
#endif
#if TEX_COUNT > 3
layout(set = 1, binding = 3) uniform texture2D t_base3;
#endif
#if TEX_COUNT > 4
layout(set = 1, binding = 4) uniform texture2D t_base4;
#endif
#if TEX_COUNT > 5
layout(set = 1, binding = 5) uniform texture2D t_base5;
#endif
#if TEX_COUNT > 6
layout(set = 1, binding = 6) uniform texture2D t_base6;
#endif

layout(set = 2, binding = 0, std140) uniform VsConsts { vec4 c[256]; } vsc;          // the vertex shader's float constants (compacted)
layout(set = 2, binding = 1, std140) uniform ClipPlanes { vec4 plane[6]; } clipPlanes;  // D3D9 user clip planes, clip space (0: off)

// The block starts at offset 32: 0-31 are D3D9's shared push data (fog, alpha ref) that every stage sees (bulk round 2: from offset
// 0 the sampler indices were D3D9's fog data, so the heights came from random samplers).
layout(push_constant) uniform Push {
  layout(offset = 32)
  uint  samplerIndex[4];  // heap index of the pixel shader's sampler i: bits 16 * (i & 1) of [i / 2]
  uint  vsIndex;          // byte k: where ModelViewProj row k (c0-c3) sits in VsConsts (0xFF: missing)
  uint  psBits;           // bit i: layer i has a height map (the land pixel shader's LandHeight c34 / c35 > 0, read per draw)
  float strength;         // Strength: game units between the lowest and the highest point of a height map
  float center;           // Center: the height (0-1) that stays where the flat land is
  float debugView;        // DebugView: 2 the height as grey, 3 the split count (blue 1 -> red 16) and the weight (green),
                          // 4 the uv (TEXCOORD0) as red / green, 5 the layer weights (COLOR0 xyz), 6 the normal (TEXCOORD5)
  float lodBias;          // LodBias: added to the height lookup's detail level
} pc;

#define SMP(i) s_samplers[(pc.samplerIndex[(i) >> 1] >> (16u * uint((i) & 1))) & 0xFFFFu]

float g_hsum = 0.0;
float g_wsum = 0.0;

// One layer's height, blended as the pixel shader blends the colours (getTerrainHeight in Parallax.hlsl: 0.5 where a layer has no
// height map). The detail level follows the spacing of the generated points, so the bumps are the height map's broad shapes
// rather than texel noise.
#define LAYER(i, T, w) \
  if ((w) > 0.0005) { \
    float h = 0.5; \
    if ((pc.psBits & (1u << (i))) != 0u) { \
      float lod = lodUv + log2(float(textureSize(sampler2D(T, SMP(i)), 0).x)) + pc.lodBias; \
      h = textureLod(sampler2D(T, SMP(i)), uv, max(lod, 0.0)).a; \
    } \
    g_hsum += (w) * h; \
    g_wsum += (w); \
  }

float landHeight(vec2 uv, vec4 b0, vec4 b1, float lodUv) {
  LAYER(0, t_base0, b0.x)
#if TEX_COUNT > 1
  LAYER(1, t_base1, b0.y)
#endif
#if TEX_COUNT > 2
  LAYER(2, t_base2, b0.z)
#endif
#if TEX_COUNT > 3
  LAYER(3, t_base3, b0.w)
#endif
#if TEX_COUNT > 4
  LAYER(4, t_base4, b1.x)
#endif
#if TEX_COUNT > 5
  LAYER(5, t_base5, b1.y)
#endif
#if TEX_COUNT > 6
  LAYER(6, t_base6, b1.z)
#endif
  return g_wsum > 0.0 ? g_hsum / g_wsum : 0.5;
}

void main() {
  vec3 b = gl_TessCoord;

#define INTERP(n) vec4 v##n = b.x * i_v##n[0] + b.y * i_v##n[1] + b.z * i_v##n[2];
  INTERP(1) INTERP(2) INTERP(3) INTERP(4) INTERP(5) INTERP(6) INTERP(7) INTERP(8) INTERP(9) INTERP(10)
#undef INTERP
  vec2 nvr = b.x * i_nvr[0] + b.y * i_nvr[1] + b.z * i_nvr[2];

  float weight = nvr.x;
  float nl = length(v6.xyz);
  vec3 n = nl > 1e-6 ? v6.xyz / nl : vec3(0.0, 0.0, 1.0);

  bool haveMvp = (pc.vsIndex & 0xFFu) != 0xFFu && ((pc.vsIndex >> 8u) & 0xFFu) != 0xFFu
              && ((pc.vsIndex >> 16u) & 0xFFu) != 0xFFu && (pc.vsIndex >> 24u) != 0xFFu;

  float h = 0.5;
  float disp = 0.0;
  if (haveMvp && weight > 0.0 && pc.strength != 0.0) {
    h = landHeight(v1.xy, v9, v10, nvr.y);
    disp = weight * pc.strength * (h - pc.center);
  }

  // The vertex shader's clip position (interpolated), moved by the displacement through ModelViewProj (row k = c[k] . (v, 1))
  vec4 clipN = vec4(0.0);
  if (haveMvp) {
    clipN.x = dot(vsc.c[pc.vsIndex & 0xFFu].xyz, n);
    clipN.y = dot(vsc.c[(pc.vsIndex >> 8u) & 0xFFu].xyz, n);
    clipN.z = dot(vsc.c[(pc.vsIndex >> 16u) & 0xFFu].xyz, n);
    clipN.w = dot(vsc.c[pc.vsIndex >> 24u].xyz, n);
  }
  precise vec4 clip = v7 + disp * clipN;

  gl_Position = clip;
  for (int k = 0; k < 6; k++)
    gl_ClipDistance[k] = dot(clip, clipPlanes.plane[k]);

  o_v1 = v1;
  o_v2 = v2;
  o_v3 = vec4(v3.xyz + n * disp, clip.w);   // the object-space position and the view depth, as the vertex shader writes them
  o_v4 = v4;
  o_v5 = v5;
  o_v6 = v6;
  // projectionPosition stays the flat land's: the pixel shader's forward sun shadows and fog read it, and the sun's shadow maps
  // hold the flat land (not displaced yet), so a displaced point there would shadow itself (bulk round 2: land sunk below the
  // flat land went dark). The rasterized position, the depth and the object position (lighting, parallax) follow the bumps.
  o_v7 = v7;
  o_v8 = v8;
  o_v9 = v9;
  o_v10 = v10;

  // Debug views (they replace the vertex colour, which the land multiplies into its colour). 4-6 prove that the pixel shader's
  // inputs arrive through the tessellation, interpolated: smooth gradients that follow the land, no black, no blocks.
  if (pc.debugView > 1.5 && pc.debugView < 2.5) {
    o_v2 = vec4(vec3(h), 1.0);
  } else if (pc.debugView > 2.5 && pc.debugView < 3.5) {
    float t = clamp((gl_TessLevelInner[0] - 1.0) / 15.0, 0.0, 1.0);
    o_v2 = vec4(t, weight, 1.0 - t, 1.0);
  } else if (pc.debugView > 3.5 && pc.debugView < 4.5) {
    o_v2 = vec4(fract(v1.xy), 0.0, 1.0);
  } else if (pc.debugView > 4.5 && pc.debugView < 5.5) {
    o_v2 = vec4(clamp(v9.xyz, 0.0, 1.0), 1.0);
  } else if (pc.debugView > 5.5) {
    o_v2 = vec4(n * 0.5 + 0.5, 1.0);
  }
}