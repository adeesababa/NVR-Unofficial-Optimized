# Dedicated character shadow map: brief (2026-10-23)

## The problem
P82 testers: small sun shadows on faces (hair strands on the brow, nose shadow) fade in only when the camera gets within
~1.5-2 m, and faces look blocky/speckled further out. Cause (checked in code): sun cascade texel size = 4 x radius / atlas
(Includes/Shadow.hlsl ~754-756). Default CascadeResolution 1 = 1536: Near ~0.4 cm texels, Middle ~1 cm, Far ~3 cm. A 1-2 cm
strand shadow survives only on Near; the 3x3 PCF blurs it away on Middle. The third-person zoom changes the FOV and NVR scales
the cascade radii with the FOV (src/effects/ShadowsExterior.cpp ~1247-1251), so the cascades move as you zoom.

## What exists (branch combo-1023, all off by default)
- HandoverBlend (on, 70): a blended band between cascades (SunShadows.fx.hlsl / Shadow.hlsl), hides the switch only.
- CharacterShadowReach 0.5 (on): caps the forward PCF slack on skin (Shadow.hlsl ShadowSlackCap ~252-270).
- PlayerInNear (off; 420 recommended): grows the Near cascade to hold the player's head in third person
  (ShadowsExterior.cpp GetCascadeViewProj). Fixes the tester's case at CascadeLambda 0.997; at the shipped 0.9 the head is
  already in Near. Measured: 2.5 m face shadow 15.6 -> 27.1 (0-255), +0.03 ms.
- CharacterObjectReach (off): the skin slack cap on head/face/headgear object draws via a per-draw flag in c204
  (src/core/CharacterObjectReach.h, set in both RenderPass hooks; head detection = skinned to head/neck bones only or under
  "Bip01 Head"/"HeadAnims"). No measurable change on the test bandana.
These are mitigations. The correct fix is a dedicated character shadow map.

## The idea (a)
A small extra sun shadow map (~1024^2, maybe 2048^2) fitted tightly round the player's head/upper body (later: the nearest
N actors), drawn after the sun cascades from the same sun direction, containing only actors (skinned geometry) and nearby
casters that can shade them. Skin and actor object shaders (forward) and the deferred sun-shadow pass sample it when the
pixel is inside its bounds, blending to the cascades at its edge. Estimated < 0.5 ms.

## Where things are
- Sun cascades are drawn in ShadowManager::RenderShadowMaps (src/core/ShadowManager.cpp, cascade loop ~2054, RenderShadowMap(...)
  with per-cascade view-proj). Cascade fit: ShadowsExteriorEffect::GetCascadeViewProj (src/effects/ShadowsExterior.cpp).
- Forward sampling: src/hlsl/NewVegas/Shaders/Includes/Shadow.hlsl (used by SkinTemplate.hlsl / SKIN2004-2011, ObjectTemplate.hlsl,
  TerrainTemplate.hlsl). Deferred: src/hlsl/NewVegas/Effects/SunShadows.fx.hlsl (GetLightAmount, cascade selection ~165+).
- Per-draw actor detection precedent: src/core/CharacterObjectReach.h.

## Hard limits (they have bitten before)
- ps_3_0: 16 samplers, 224 float constants (NVR writes c171-c189, c194, c197-c199, c204 itself per draw), 16 bool registers
  (often all used), 32 temp registers, instruction slots. Several 6-lamp object shaders already sit near the driver's ~3,600-slot
  limit (adding code to them made the driver refuse them): any shader change must report its slot growth per family.
- No MRT with MSAA. The game's own render-state cache must be kept truthful (NoteDeviceTexture for direct SetTexture calls).
- Native D3D9 must keep working (the planned NVR DXVK fork may relax limits later, but this feature must not depend on it).
- Off by default, with a setting; the existing cascade path unchanged when off.

Design and prototype (2026-10-23): CHARACTER-SHADOW-MAP-DESIGN.md, branch character-shadow-map.
