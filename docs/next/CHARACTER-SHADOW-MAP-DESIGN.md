# Dedicated character shadow map: design and prototype (2026-10-23)

Brief: CHARACTER-SHADOW-MAP.md (the problem, the measurements, the mitigations already on combo-1023, the hard limits).
Prototype: branch `character-shadow-map` off combo-1023, off by default
(`[Shaders.ShadowsExteriors.ShadowMaps] CharacterShadowMap = false`). With it off nothing is compiled into the shaders
and the cascade path is byte for byte what it was. Status, unverified points and the in-game test are at the end.

## 1. The idea in one paragraph
The sun cascades cannot give a face 1-2 cm shadows past about 2 m: the cascade that holds the head has 1 cm texels
(Middle at the default 1536 atlas) and a 3 x 3 filter, and the cascades move with the camera's zoom (the FOV scales
their radii). So the DLL draws one more small sun shadow map, of the player alone, fitted round the head and upper body,
from the same quantised sun direction, every frame the cascades are drawn. It is independent of the camera (no zoom
scaling), its texels are 1-3 mm, and the skin and object shaders (and the deferred sun pass) read it wherever a pixel is
inside its box: the shadow there is the darker of the cascades' value and the map's, fading back to the cascades alone
over the box's outer band. The cascades still draw the player, so the player's shadow on the ground and the world's
shadows on the player are exactly as before; the map only adds back the fine self-shadow the cascades' texels lose.

## 2. Map fit (ShadowsExteriorEffect::GetCharacterViewProj, ShadowsExterior.cpp)
- A sphere round the player becomes a sun-aligned ortho box 2R across and 2R deep: eye R toward the sun from the
  centre, `D3DXMatrixLookAtRH` with world +Z up (+Y when the sun is within 2.6 degrees of the zenith, as the cascades'
  OverheadSunFix), `D3DXMatrixOrthoOffCenterRH(-R, R, -R, R, 0, 2R)`. The same construction as GetCascadeViewProj and
  AmbientLight::GridSunViewProj, in camera-relative world space (the space of the casters' world matrices and of the
  shaders' GetShadowWorldPos).
- Fit 0 (default): the segment from the Spine2 bone to the head bone (`Player->ActorSkinInfo->Spine2Node / HeadNode`),
  centre at its middle, R = half its length + Margin (24 units: hat brims, hair, shoulders, and the sun comes in at an
  angle so every side needs room). Typical R ~ 48 units: a 96-unit box = 1.37 m, at 1024 texels 0.094 units = 1.3 mm
  per texel (Near is ~4 mm, Middle ~10 mm at CascadeResolution 1 = 1536).
- Fit 1: the actor node's world bound + Margin (the whole body, weapon included): R ~ 100-130 units, ~3 mm texels.
- Depth: the box is 2R deep, which is enough. A receiver beyond the far plane compares at depth 1 and is shadowed by
  any caster in the map in front of it (the shadow on the ground, when the ground is inside the box's footprint); a
  receiver nearer the sun than the near plane compares below 0 and is lit (nothing is drawn in front of it; the caster
  shader pancakes to 0). So the map never has to reach the ground.
- Stability (no shimmer): R is rounded UP to 8 world units, so a breathing bound does not change the texel size every
  frame (the texel size changes only at an 8-unit step, a visible resample once in a while, logged with
  CharacterShadowMapLog). The projection is then shifted so the world origin lands on a texel corner (the cascades'
  rounding), so a step of the character moves the picture by whole texels and the shadows do not swim. The sun is the
  cascades' quantised / glided direction, so the map turns only when they do. The camera's zoom plays no part: the same
  texels at 1 m and at 5 m.
- Resolution: CharacterShadowMapResolution, 1024 by default (4 MB R32F + 4 MB D24S8), 256..4096. 2048 halves the texel
  (0.7 mm) for four times the fill.

## 3. What is drawn into it (ShadowManager::RenderCharacterShadowMap, ShadowManager.cpp)
- Drawn right after the sun cascades (inside the same "cascades drawn this frame" block, so the cascade passes' shaders,
  billboard vectors and render states are already set), into its own R32F target with `TESR_ShadowData.z = 1`, which
  makes ShadowMap.pso write plain depth whatever the cascades' Mode (the ortho map's rule). So the map works with VSM,
  EVSM and PCF cascades alike, and needs no prefilter and no MSAA.
- Its own walk, not the loaded cells: `AccumChildren(Player->GetNode(), ...)` against the box's cull planes. The body,
  hair, headgear, clothing and the held weapon all hang under that node, skinned geometry and rigid alike, so the
  existing passes sort them (skinnedGeoPass, alphaPass for alpha-tested hair cards and hat meshes, geometryPass for
  rigid parts). Forms: Actors only, AlphaEnabled on, MinRadius 0 (eyelashes and strands count), no LOD, no land.
- CharacterShadowMapOthers (off): the other actors of the loaded cells whose node bound touches the box are walked in
  too (a CollectMovers-style scan of the cells' references: NPC, Creature, LeveledCreature), so a companion's arm over
  your shoulder casts as sharp a shadow as your own. Nearby static geometry (walls, trees) is NOT drawn: its shadow on
  the player comes from the cascades, through the min() in the lookup, at cascade sharpness. That is deliberate: it
  keeps the draw tiny and avoids a second full cell walk per frame (the cascades' one walk is 0.04-0.08 ms of CPU).
- Only in third person with the sun up and the shadows not faded (first person: nothing drawn,
  `TESR_CharShadowData.w = 0`, every shader skips the lookup).
- BeginShadowMap / DrawShadowMap are the cascades' own (clear to depth 1, TwoSidedCasters honoured, land drop off).

## 4. Sampling (Includes/Shadow.hlsl CharacterMapShadow; SunShadows.fx.hlsl GetLightAmount)
- Forward: GetSunShadow computes the cascades' shadow as before; then, `#if SHADOW_CHARACTER_MAP` and
  `TESR_CharShadowData.w > 0`, `shadow = CharacterMapShadow(worldPos, worldNormal, NdotL, shadow)`, before the horizon
  fade. The function transforms the point (plus a normal offset of 1.5 map texels at a grazing sun) by the map's
  matrix; `edge = max(|x|, |y|)` in clip space; `weight = 1 - smoothstep(1 - band, 1, edge)` (band =
  CharacterShadowMapBlend / R); outside the box weight is 0 and the cascades' value returns untouched; inside, a 3 x 3
  PCF weighed with the quadratic B-spline the cascades use (plain path, no SmoothFilter branch), with the receiver-plane
  slope slack (tan of the sun's angle, capped at 5, per texel of depth) and a constant bias (CharacterShadowMapBias,
  world units, converted to the map's depth); result `lerp(cascade, min(cascade, lit), weight)`.
- Why min(): a depth map cannot say which caster made a texel, so the player's sharp self-shadow and the cascades'
  blurry copy of it cannot be separated per pixel. min() keeps the sharp dark core from the map and, where the cascade
  has a wider soft halo round a thin shadow, that halo stays (a faint gray rim, 1-2 texels of the cascade). The clean
  alternative, leaving the player out of the cascade walks when the map is on, needs every receiver family that can
  show the player's shadow (terrain, grass, parallax, trees) to read the map too, which is out of budget (section 5);
  noted as phase 2 under section 7.
- Deferred (ForwardShadows off, SunShadows.fx `Shadow()`): GetLightAmount is now a wrapper round the old cascade lookup
  (renamed GetLightAmountCascades) that applies the same rule with the deferred path's 2 x 2 PCF. The branch condition
  includes the per-pixel weight, so the effect compiler keeps it a dynamic branch and takes none of ps_3_0's 16
  constant-branch registers (this file is at that cap).
- Blending to the cascades: the band (12 world units by default) inside the box's edge; the cascades alone outside. The
  point looked up is the same for both (the map adds nothing to the cascade's own normal offset).
- Identifying actor draws: not needed for the player-only version. The test is spatial (is this pixel inside the box),
  so the wall behind the player's head inside the box also reads the map, which is right: the player's shadow falls on
  it, sharper. The per-draw flag precedent (CharacterObjectReach, c204) becomes necessary only for the N-actor atlas
  (which map to read: section 7) or to exclude draws.
- Cascade texel parity: with the map on, CharacterShadowReach / PlayerInNear / HandoverBlend keep working as before on
  the cascades' part of the result; PlayerInNear becomes largely unnecessary.

## 5. Budget per shader family (ps_3_0: 16 samplers, 224 float constants, 16 bool constants, 32 temps, ~3,600 slots)
Compiled in only where three things hold: CHARACTER_SHADOW_MAP (the setting at startup, ShaderRecord::BuildDefines,
defined last and only when on, so the define list is unchanged when off; the list's cap is raised 42 -> 46 anyway,
Defines[48]), the includer's opt-in `SHADOW_CHARACTER_MAP_IN 1`, FORWARD_SHADOWS, and not INTERIOR_SHADOWS (the
Interior records compile the sun's code out, and their s11 is a lamp's cube map).

| Family | Gets it | Samplers | Float constants | Bool | Temps (inside the branch) | Slots (estimate) |
|---|---|---|---|---|---|---|
| Skin, new (SkinTemplate SKIN2000-2011) | yes | +1 (s11) | +6 (c206-c209, c213-c214) | 0 | ~8 | ~+100 |
| Skin, VPS (SkinVPSTemplate SKIN2004-2011 with NewSkin off) | yes | +1 | +6 | 0 | ~8 | ~+100 |
| Skin, hand-written (SKIN2000/2001/2002/2006.pso) | yes | +1 | +6 | 0 | ~8 | ~+100 |
| Object (ObjectTemplate PS), LIGHTS <= 3, not NVR_SLOT_TIGHT | yes | +1 | +6 | 0 | ~8 | ~+100 |
| Object, 4-6 lamps (SLS2033-2036 and the OPT specular 4+) | no | 0 | 0 | 0 | 0 | 0 |
| Terrain, terrain LOD, terrain fade, parallax, grass, leaves, particles, SM300x | no | 0 | 0 | 0 | 0 | 0 |
| SunShadows.fx (deferred) | yes | +1 (s5) | +6 | 0 (dynamic branch) | ~6 | ~+60 |

- Registers were chosen from the full map of what every pixel shader pins: c204 is CharacterObjectReach, c205
  LampFalloff, c210-c212 ExactPassSums / SpecularFade / MaterialAware, c218-c223 HandBlood / Rmaos / HudRain / PBRMath;
  c206-c209 and c213-c217 are free in every opted-in family (ParticleLight's c203-c214 and ClothWind's c200-c226 are
  particle / vertex shaders). s11 is free in the Default (outdoor) records (s0-s7 the game's, s9 the atlas, s10 its
  cross-fade copy or the live cube, s8 and s11-s15 the interior cube maps only).
- The 6-lamp shaders are left out on purpose (the Interior one is at the slot limit; the four-lamp ones were at the
  32-temp limit with two cascade lookups in 2026-10-14). Outdoors by day a hat lit by four lamps is rare.
- Measuring the real slot growth (the owner, Windows): build tests\game_shaders.vcxproj (tools\test-game-shaders.ps1
  builds it) and run, for each family, `game_shaders.exe --asm-template <template> ps_3_0 out.txt <defines>` once
  without and once with `CHARACTER_SHADOW_MAP=1` (plus the template's own defines, e.g. `PS LIGHTS=3 SPECULAR`,
  `PS POINTS=1`); the disassembly's last line is the slot count. The game log also reports a shader the driver refuses.

## 6. Cost and how to measure it
- Draw: one 1024^2 R32F target cleared and 10-40 small meshes (the player's parts, hair cards, headgear, weapon) drawn
  with the cascade shaders; no walk of the loaded cells. Estimate 0.05-0.15 ms GPU, under 0.05 ms CPU. 2048^2: about
  twice the GPU fill, the same draws.
- Sample: ~100 slots per opted-in pixel, but only the pixels inside the box pay the 9 taps (the `[branch]` on weight);
  the rest pay the matrix transform and the edge test (~15 slots). The box covers a few percent of the screen at 2 m,
  more when zoomed in. Estimate under 0.1 ms at 1440p. Deferred path: the same, 4 taps.
- Measure: F10 (GpuTimer) adds the rows "Character shadow map" (GPU) and "Character shadow map (CPU)" next to
  "Sun cascade geometry"; the lit-pixel cost shows in the world scene's own row with the setting on vs off.
  CharacterShadowMapLog prints the fit (R, texel size in cm) and the caster count once a second.

## 7. Extension: the N nearest NPCs (note only; the player first)
- Same draw, several boxes: one small map per actor in an atlas (e.g. 4 x 512^2 in one 1024^2 texture, each tile its own
  matrix and centre), fitted and drawn by the same code with a per-tile walk of that actor's node. CPU: N node walks.
- The lookup must know which tile: either (a) a per-pixel search over N centres (N transforms, N edge tests: slots and
  temps grow with N, 4 is the practical cap in the skin family, 2 in objects), or (b) the per-draw flag precedent
  (CharacterObjectReach.h): the RenderPass hooks tell the shader which actor a draw belongs to (c204.y = tile index +
  1), one transform per pixel. (b) is cheap in the shader and matches the existing design; it needs the draw's actor
  (walk up from the geometry to the actor's node, cache per geometry as CharacterObjectReach does). Receivers that are
  not actors (the ground under an NPC) keep the cascades.
- Choosing N: the actors nearest the camera within the Near cascade's radius, re-ranked every frame with hysteresis so
  tiles do not swap every other frame.
- CharacterShadowMapOthers (shipped, off) already gives the first half of the benefit at no shader cost: another actor
  inside the player's box is a caster in the player's map.

## 8. Status (prototype, branch character-shadow-map)
Done:
- Settings `[Shaders.ShadowsExteriors.ShadowMaps] CharacterShadowMap / Resolution / Fit / Margin / Blend / Bias / Others / Log`
  (defaults.toml, hidden section as the rest; read in ShadowsExteriorEffect::UpdateSettings, live except the first).
- ShadowsExterior.h/.cpp: settings and constants (`TESR_CharShadowCameraToLight`, `TESR_CharShadowCenter`,
  `TESR_CharShadowData`, registered by name), the texture `TESR_CharShadowMap` (made at startup when on, remade on a size
  change with the shaders' cached pointers cleared), the fit (GetCharacterViewProj).
- ShadowManager.h/.cpp: RenderCharacterShadowMap (draw, optional other actors, F10 timers, log), called after the
  cascades; CharacterData.w zeroed per frame.
- ShaderRecord::BuildDefines: CHARACTER_SHADOW_MAP (when on, last); Defines[48], cap 46. EffectRecord: the same macro
  for the effects (EffectMacros[8]).
- Shadow.hlsl: declarations, CharacterMapShadow, the call in GetSunShadow. Opt-ins in SkinTemplate, SkinVPSTemplate,
  SKIN2000/2001/2002/2006.pso, ObjectTemplate (PS, LIGHTS <= 3, not slot-tight). SunShadows.fx.hlsl: the deferred twin.
- Native D3D9 only (no DXVK dependence); no MRT; all texture binds go through the render-state cache (ShaderRecord::SetCT
  and EffectRecord::SetCT bind by name; the draw's render target is set as the ortho map's is).

Unverified (nothing here was compiled or run: no Windows, no game):
- C++ compiles (VS2019 Win32). The new code uses only calls already used in the same files (Player->ActorSkinInfo bones,
  GetWorldBound, WithinFrustum, AccumChildren, Begin/DrawShadowMap, SetFrustumPlanes, InitTexture, ClearShaderSamplers).
- HLSL compiles in every opted-in shader under ps_3_0: slot growth (estimate ~100) and temporaries (the branch adds ~8 at
  a point where the lighting state is live). If the log shows a skin or object shader failing to compile or refused by
  the driver with the setting on, narrow the ObjectTemplate opt-in further (`LIGHTS <= 2`) or drop the hand-written skin
  shaders; the function itself can also lose the slope term.
- The register choice (c206-c209, c213-c214, s11) against shaders not grepped here (none found, but the game's own
  constant writes above c200 are not visible in this tree).
- Fit 0 relies on `ActorSkinInfo->Spine2Node` being valid in third person (HeadNode already is, PlayerInNear uses it);
  Fit 1 falls back to the node bound and is used automatically when the bones are missing.
- Bias: 0.15 units constant + 1.5 texels of slope slack is a guess for 1-3 mm texels; acne or detached shadows adjust
  CharacterShadowMapBias (0.05-0.5).
- The gray halo from min() with the cascades' blurred self-shadow (section 4): expected, size unknown until seen.
- Deferred path: only reasoned about; most users run ForwardShadows on.

In-game test (the owner):
1. Set `CharacterShadowMap = true` in the user toml (restart: it compiles into the shaders; clear
   Shaders\NewVegasReloaded\Shaders\Cache if a shader seems unchanged). Copy NewVegasReloaded.dll to nvse\Plugins\ and
   the changed .hlsl files (Includes\Shadow.hlsl, SkinTemplate.hlsl, SkinVPSTemplate.hlsl, SKIN2000/2001/2002/2006.pso.hlsl,
   ObjectTemplate.hlsl under Shaders\NewVegasReloaded\Shaders\; Effects\SunShadows.fx.hlsl under ...\Effects\).
2. Check the log for "UNOFFICIAL CharacterShadowMap: compiled ... map made" and no shader compile errors / driver
   refusals; with `CharacterShadowMapLog = true`, the fit line once a second (expect R ~ 48, texel ~0.13 cm).
3. Third person, 13 h, PlayerInNear 0, the test face with a hat or bandana. Zoom from 1 m to 5 m. Before (setting
   off): hair, nose and brim shadows fade past ~1.5-2 m. After: they stay sharp at every distance, and do not shimmer
   as you walk or turn (the sun's quantised steps aside). Toggle the setting live (no restart needed after the first)
   for A/B at the same spot.
4. Look for: acne / fine stripes on the skin (raise Bias), shadows detached from hair (lower Bias or Margin), a visible
   line where the map ends (raise Blend), a gray halo round thin shadows (section 4's min(); report its width), the
   first-person view unchanged, interiors unchanged.
5. F10: the "Character shadow map" rows; compare the frame with the setting on and off.

## 9. First in-game result (2026-10-24) and the diagnostics added for it
The owner's screenshot (third person, afternoon sun): everything on the character from the head down to the knees is
in shadow, flat, with a hard horizontal line at the knees; below the knees the suit is sunlit. Reading:
- The line IS the map's box: a 96-unit box round the chest (Fit 0, R 48), seen from a sun about 45 degrees up, reaches
  about 68 units above and below the chest on the body, which is the knees. So the fit, the matrix and the live flag all
  reach the shaders (the edge is in the right place; the hand-over band is 12 units, which reads as a hard line next to
  a fully dark interior). Fit 1 would move the line to the feet and the ground, not remove it.
- The fault is the inside: every pixel inside the box reads "shadowed", including sunlit skin and cloth. That means the
  map's compare fails everywhere, which happens if the sampler reads black (the texture not bound on s11, or the map never
  cleared / drawn) or if the depth the map holds is systematically nearer the sun than the point looked up.
- One real bug found and fixed: a point past the box's far plane (depth above 1) compared against a texel cleared to 1
  and was always shadowed; it now compares just under 1 (an empty texel leaves it lit, any caster shadows it). That only
  covers a sliver at the knees, not the whole upper body.
- Added for the next run: `CharacterShadowMapDebug` (live): 1 = inside the box the sun shadow is the map's own verdict
  alone (white lit / black shadowed), 2 = the depth the map holds at the point (black = empty or unbound texture, white =
  nothing drawn there, gray = a caster), 3 = the point's own depth in the box. With `[Main.Develop] DebugMode = true`
  the test-image key (the one that writes Test\shadowmapatlas.jpg) also writes Test\charshadowmap.jpg: the map itself.
  The log already says, once per shader, "Texture TESR_CharShadowMap Successfully bound" or "was not bound".
- What each answer means: debug 2 black over the body and debug 3 gray = the texture is not reaching the shaders (bind
  path); debug 2 white everywhere = nothing is drawn into the map (walk or draw); debug 2 gray but debug 1 black = the
  compare / bias / transform disagree between the draw and the lookup.
