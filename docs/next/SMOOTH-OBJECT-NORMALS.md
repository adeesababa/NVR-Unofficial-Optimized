# SmoothObjectNormals (2026-10-24): no more per-triangle facets on characters

Setting: `[Shaders.PBR.Main] SmoothObjectNormals = true` (default on; needs a restart, it recompiles the shaders).
Branch character-shadow-map, with the character shadow map prototype.

## The problem (owner's screenshot, a lamp-lit interior)
Every triangle of the character's clothes showed as a flat facet with a hard edge. The object shaders (clothes, armour,
hair, weapons) get their normal in tangent space and had no world normal in the pixel shader, so NVR built one from
screen derivatives (Shadow.hlsl GetShadowGeometricNormal): one normal per triangle. Three things used it and so stepped at
every triangle edge: the sun shadow lookup's normal offset and slope slack, the lamp shadow lookups' normal offset
(PointShadowForward, NormalOffset), and the sky-colour ambient term (Object.hlsl getAmbientLighting, SkylightingScale).
Indoors the sky term and the lamp lookups are what light a character, so there the facets were plain. Skin had been given
the vertex normal already (SmoothCharacterShadows, sun and lamps), but not the clothes, nor the older skin shader SKIN2001.

## The fix
- The vertex shaders send the vertex normal in camera-relative world space (GetShadowSmoothNormal, the skin's way):
  ObjectTemplate with up to 3 lamps in its own interpolator (TEXCOORD8; PROJ_SHADOW variants have none free and keep the
  flat normal), ObjectTemplate with 4-6 lamps and SkinVPSTemplate octahedrally encoded in uv.zw (no interpolator spare).
- The pixel shaders take it for all three uses (ShadowFacingNormal: unit length, turned to face the camera as the old
  normal was, so two-sided cloth and hair cards still push their lookups out on the visible side) and no longer compute
  the derivative normal, so the slot count should stay about the same. SKIN2001.pso reads the normal its vertex shader
  already sent and ShadowLookupNormal takes the smooth normal whenever the switch is on.
- Every reader of the normal is gated by the vertex shader's sentinel already (a vanilla vertex shader: no sun lookup, no
  lamp lookup, no sky term), so an unwritten normal is never used.

## Unverified (not compiled here)
- ps_3_0 input count in the 3-lamp object shaders is now exactly 10 in the fullest variant (vertex colour, fog, 7 texture
  coordinates, TEXCOORD8); vs_3_0 outputs 11 of 12. If a variant fails to compile with "too many inputs", drop
  OBJ_SMOOTH_NORMAL_INTERP for it in ObjectTemplate.hlsl (it then keeps the flat normal).
- Slot count of the six-lamp specular shader (NVR_SLOT_TIGHT): the octahedral decode (~10 slots) replaces the derivative
  normal (~10 slots). Check the log for a refused shader with the setting on.
- The ternaries in ShadowEncodeNormal / ShadowDecodeNormal compile under D3DX 9.43 (plain scalar selects).

## Test
Same interior, same spot, `SmoothObjectNormals` false vs true (restart each time; clear Shaders\NewVegasReloaded\Shaders\Cache
if a shader seems unchanged). Copy: NewVegasReloaded.dll; Shaders\Includes\Shadow.hlsl, ObjectTemplate.hlsl,
SkinVPSTemplate.hlsl, SKIN2001.pso.hlsl under Shaders\NewVegasReloaded\Shaders\.
