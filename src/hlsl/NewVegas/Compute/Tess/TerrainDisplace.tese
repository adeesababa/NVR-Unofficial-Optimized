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
//
// 2026-10-27 (user: black outlines where the bumpy land meets rocks, terrain blending on or off alike):
//  - The cause: the pixel shader's forward sun shadows look up projectionPosition (TEXCOORD6), which stayed the FLAT land's. Where
//    the land rises against a rock whose side slopes outward towards its foot (most rocks and mounds), the flat point under the
//    raised land lies inside the rock, so the rock shadowed it: a dark band round every rock, as wide as the rise over the rock's
//    slope (wider with more Strength). projectionPosition now rises with the land (never below the flat land: the sun's shadow
//    maps still hold the flat land, and a point under it would shadow itself, bulk round 2). Fog reads it too (a few units nearer).
//  - ObjectBlend (OBJECT_DEPTH variants, TEX_COUNT 1-6): the displacement fades to ObjectBlendKeep where the land meets anything
//    drawn before it (rocks, buildings, characters' feet, the near grass), so they meet the ground as on flat land instead of
//    sinking into it. The scene's depth as the land starts (terrain blending's copy, Terrain.cpp) is read at the pixel shader's s6,
//    BaseMap[6]'s slot, which is free below 7 layers (the TES can only read textures that pixel shader uses, s0-s7: NVR DXVK
//    1.7-displace); 7-layer land has no free slot and keeps its bumps (the plain variant).
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

layout(location = 30) in vec4 i_nvr[];   // TerrainDisplace.tesc: x relief weight, y log2 uv spacing, z distance fade, w object blend

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
#if defined(OBJECT_DEPTH) && TEX_COUNT < 7
// ObjectBlend: the pixel shader's TESR_DisplaceDepth (s6), the scene's depth as the land starts (INTZ: the depth itself, 0-1)
layout(set = 1, binding = 6) uniform texture2D t_depth;
#define HAVE_OBJECT_DEPTH 1
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
  float viewParams;       // DebugView x 16 + (LodBias + 4). DebugView: 2 the height as grey, 3 the split count (blue 1 -> red 16)
                          // and the weight (green), 4 the uv (TEXCOORD0) as red / green, 5 the layer weights (COLOR0 xyz), 6 the
                          // normal (TEXCOORD5), 7 the object blend (white: all the bumps, black: none). LodBias: added to the height
                          // lookup's detail level.
  float sceneScale;       // ObjectBlend: the draw's viewport as a share of the depth copy (RenderScale draws the world into its
                          // top-left part): round(x * 2048) * 4096 + round(y * 2048)
} pc;

#define SMP(i) s_samplers[(pc.samplerIndex[(i) >> 1] >> (16u * uint((i) & 1))) & 0xFFFFu]

float debugView() {
  return floor(pc.viewParams / 16.0);
}

float lodBias() {
  return pc.viewParams - debugView() * 16.0 - 4.0;
}

float g_hsum = 0.0;
float g_wsum = 0.0;

// One layer's height, blended as the pixel shader blends the colours (getTerrainHeight in Parallax.hlsl: 0.5 where a layer has no
// height map). The detail level follows the spacing of the generated points, so the bumps are the height map's broad shapes
// rather than texel noise.
#define LAYER(i, T, w) \
  if ((w) > 0.0005) { \
    float h = 0.5; \
    if ((pc.psBits & (1u << (i))) != 0u) { \
      float lod = lodUv + log2(float(textureSize(sampler2D(T, SMP(i)), 0).x)) + lodBias(); \
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

#ifdef HAVE_OBJECT_DEPTH
// ObjectBlend: the share of the displacement kept at the flat land point p (object space; n its normal, t a tangent): 1 far from
// anything drawn before the land, Keep where something touches it, a smooth fade over Width game units between. Nine points of the
// flat land round p (p itself, four at Width / 2, four at Width) are projected with ModelViewProj; at each, the scene's depth gives
// how far in front of (or behind) that land point the first surface along its view ray is, and the nearest of
// sqrt(offset^2 + gap^2) is the distance to it. Linear filtering and the clamp to the screen keep the result continuous in p, so the
// two patches that share an edge agree on it (no cracks).
const vec2 kBlendTaps[9] = vec2[9](vec2(0.0), vec2(0.5, 0.0), vec2(-0.5, 0.0), vec2(0.0, 0.5), vec2(0.0, -0.5),
  vec2(0.70711, 0.70711), vec2(-0.70711, 0.70711), vec2(0.70711, -0.70711), vec2(-0.70711, -0.70711));

float objectBlend(vec3 p, vec3 n, vec3 tangent, float width, float keep) {
  vec4 r0 = vsc.c[pc.vsIndex & 0xFFu], r1 = vsc.c[(pc.vsIndex >> 8u) & 0xFFu];
  vec4 r2 = vsc.c[(pc.vsIndex >> 16u) & 0xFFu], r3 = vsc.c[pc.vsIndex >> 24u];
  // Depth = A + B / view depth: for any perspective camera ModelViewProj's row 2 is A x row 3 + (0, 0, 0, B), whatever the object's
  // place (row 3 gives the view depth, clip w). Holds for reversed depth too.
  float A = dot(r2.xyz, r3.xyz) / max(dot(r3.xyz, r3.xyz), 1e-20);
  float B = r2.w - A * r3.w;
  float qx = floor(pc.sceneScale / 4096.0);
  vec2 scale = vec2(qx, pc.sceneScale - qx * 4096.0) / 2048.0;

  vec3 t = tangent - n * dot(tangent, n);
  t = dot(t, t) > 1e-12 ? normalize(t) : vec3(1.0, 0.0, 0.0);
  vec3 bt = cross(n, t);

  float nearest = 1e9;
  for (int k = 0; k < 9; k++) {
    vec2 o = kBlendTaps[k] * width;
    vec3 x = p + t * o.x + bt * o.y;
    vec4 c = vec4(dot(r0.xyz, x) + r0.w, dot(r1.xyz, x) + r1.w, dot(r2.xyz, x) + r2.w, dot(r3.xyz, x) + r3.w);
    if (c.w < 1.0)
      continue;   // at or behind the camera
    vec2 uv = clamp(c.xy / c.w * vec2(0.5, -0.5) + 0.5, 0.0, 1.0) * scale;
    float den = textureLod(sampler2D(t_depth, SMP(6)), uv, 0.0).r - A;
    if (abs(den) < 1e-12)
      continue;   // an infinitely far plane: nothing there
    float sceneW = B / den;
    if (!(sceneW > 0.0))
      continue;
    float gap = c.w - sceneW;   // > 0: the surface is in front of this land point, < 0: behind it (a rock's buried foot)
    nearest = min(nearest, sqrt(dot(o, o) + gap * gap));
  }
  return mix(keep, 1.0, smoothstep(0.0, 1.0, nearest / max(width, 1.0)));
}
#endif

void main() {
  vec3 b = gl_TessCoord;

#define INTERP(n) vec4 v##n = b.x * i_v##n[0] + b.y * i_v##n[1] + b.z * i_v##n[2];
  INTERP(1) INTERP(2) INTERP(3) INTERP(4) INTERP(5) INTERP(6) INTERP(7) INTERP(8) INTERP(9) INTERP(10)
#undef INTERP
  vec4 nvr = b.x * i_nvr[0] + b.y * i_nvr[1] + b.z * i_nvr[2];

  float weight = nvr.x;
  float nl = length(v6.xyz);
  vec3 n = nl > 1e-6 ? v6.xyz / nl : vec3(0.0, 0.0, 1.0);

  bool haveMvp = (pc.vsIndex & 0xFFu) != 0xFFu && ((pc.vsIndex >> 8u) & 0xFFu) != 0xFFu
              && ((pc.vsIndex >> 16u) & 0xFFu) != 0xFFu && (pc.vsIndex >> 24u) != 0xFFu;

  // ObjectBlend: the share of the displacement kept here (1: all of it). i_nvr[0].w is the same for every point of the draw.
  float keepShare = 1.0;
#ifdef HAVE_OBJECT_DEPTH
  float blend = i_nvr[0].w;
  if (haveMvp && blend >= 1.0 && nvr.z > 0.0 && pc.strength != 0.0)
    keepShare = objectBlend(v3.xyz, n, v4.xyz, floor(blend), clamp(fract(blend) / 0.99, 0.0, 1.0));
#endif

  // Displacement = Strength x (relief + offset): the relief (height - 0.5) fades with distance and to 0 on quadrant borders (each
  // quadrant is its own draw with its own textures); the offset (0.5 - Center, how far the average height rises) only fades with
  // distance, so a border keeps the same average level as the land on both sides of it (no groove along it). Both fade near objects
  // (ObjectBlend), so there the land is the flat land again.
  float h = 0.5;
  if (haveMvp && weight > 0.0 && pc.strength != 0.0 && keepShare > 0.0)
    h = landHeight(v1.xy, v9, v10, nvr.y);
  float disp = haveMvp ? pc.strength * keepShare * (weight * (h - 0.5) + nvr.z * (0.5 - pc.center)) : 0.0;

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
  // projectionPosition (the pixel shader's forward sun shadows and fog) rises with the land, but never sinks below the flat land:
  // the sun's shadow maps hold the flat land (not displaced yet), so a point under it would shadow itself (bulk round 2: land sunk
  // below the flat land went dark). Until 2026-10-27 it stayed the flat land's everywhere, and the flat point under land raised
  // against a rock lay inside the rock: the black outlines round rocks (see the top). The rasterized position, the depth and the
  // object position (lighting, parallax) follow the bumps.
  o_v7 = v7 + max(disp, 0.0) * clipN;
  o_v8 = v8;
  o_v9 = v9;
  o_v10 = v10;

  // Debug views (they replace the vertex colour, which the land multiplies into its colour). 4-6 prove that the pixel shader's
  // inputs arrive through the tessellation, interpolated: smooth gradients that follow the land, no black, no blocks.
  float dv = debugView();
  if (dv > 1.5 && dv < 2.5) {
    o_v2 = vec4(vec3(h), 1.0);
  } else if (dv > 2.5 && dv < 3.5) {
    float t = clamp((gl_TessLevelInner[0] - 1.0) / 15.0, 0.0, 1.0);
    o_v2 = vec4(t, weight, 1.0 - t, 1.0);
  } else if (dv > 3.5 && dv < 4.5) {
    o_v2 = vec4(fract(v1.xy), 0.0, 1.0);
  } else if (dv > 4.5 && dv < 5.5) {
    o_v2 = vec4(clamp(v9.xyz, 0.0, 1.0), 1.0);
  } else if (dv > 5.5 && dv < 6.5) {
    o_v2 = vec4(n * 0.5 + 0.5, 1.0);
  } else if (dv > 6.5) {
    o_v2 = vec4(vec3(keepShare), 1.0);   // white: all the bumps; black: none (ObjectBlend); 7-layer land always white
  }
}