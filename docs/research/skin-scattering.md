# Skin subsurface scattering: research for NVR

Date 2026-10-11. Read-only: no source edits, no builds, no game-folder changes, no git writes.
Our tree: `work\NVR-pbr-effects`. Reference fork: `work\PBR-pr` (commit c12d09e, read only).
All costs are estimates from bandwidth and tap counts. Nothing was measured or compiled.

## In short

- Our default skin has no real subsurface scattering. The "wrap/rim" function and its five settings are dead code. What runs is plain Lambert plus a hand-written terminator band.
- The VPS path has a real pre-integrated LUT, but only when VanillaPlusSkin.dll is loaded.
- The fork's convincing look comes from a screen-space blur of skin light. A mask is drawn with each skin draw. Cost is roughly 0.3-2 ms at 2560x1440, depending on how much skin is on screen.
- The blur has a GTX 1070 risk we have already hit once in our own code (a bound depth-stencil dropped an MRT draw).
- Recommended order: a better per-pixel model first (about 0.1 ms, no MRT), then the blur behind a GPU probe. Everything defaults off.

## 1. What our skin lighting does now

Default path (no VPS). Hand-written SKIN2000-2011 shaders in `src\hlsl\NewVegas\Shaders\`:
- Sun: `GetLighting` = N.L plus a back-light fresnel (`Includes\Skin.hlsl:51-59`), called at `SKIN2000.pso.hlsl:66`. No wrap, so the sun terminator is a hard edge.
- Point lights: `getPointLight` (`Includes\Skin.hlsl:98-115`) adds a smoothstep band around the terminator, tinted by GlowMap. `SKIN2002.pso.hlsl:78`. SKIN2004-2011 all carry the same band, written inline (e.g. `SKIN2004.pso.hlsl:88-92`).
- Specular: `GetSpecular` is scaled by `SKIN_SPECULAR_STRENGTH`, defined as 0 (`Includes\Skin.hlsl:6-8`). Our build does not mute the engine's own highlight passes on skin (no matching code in `src\NewVegas\Hooks\Shaders.cpp`).
- Dead code: `Skin()` (`Includes\Skin.hlsl:26-48`) with `HalfLambert`, `BlinnPhongSpecular` and `GetSSS` (lines 10-24, 65-67). Nothing calls them. This corrects the brief: the wrap/rim approximation is not running.
- Dead settings: `src\effects\Skin.cpp:11-18` copies Attenuation, SpecularPower, MaterialThickness, RimScalar and CoeffRed/Green/Blue into constants. Only `Skin()` reads them, and SpecularPower only feeds the zeroed GetSpecular. So the five keys in `[_Shaders.Skin.Main]` (`resource\NewVegasReloaded.dll.defaults.toml:990-997`) change nothing. Their menu sliders mislead. The enable comment at line 1000 also claims an SSS approximation that is not there.

VPS path. `src\core\Hooks\GameCommon.cpp:16-17` sets `bVPSLoaded` from a DLL check, not from textures (the brief assumed textures). `src\effects\Skin.h:24-51` routes every SKIN name to `SkinVPSTemplate.hlsl`.
- `Includes\SkinVPS.hlsl:40-58`: three LUT reads, one per colour channel, each with a more softened normal, indexed by N.L and 1-curvature. This is real pre-integrated SSS.
- The LUT is the GlowMap texture (`SkinVPSTemplate.hlsl:237`, calls at 353-495). So it depends on VPS's own texture set. This is from the code; VPS's files were not checked.
- `build\shader-test\old` holds an older copy of the skin shaders. Ignore it.

What our current model lacks: light does not spread across the face, so cheeks and nose get hard shading. There is no thin-part glow (ears, nostrils) and no curvature response. Red bleed exists only inside the wrap band.

## 2. How the fork's screen-space SSS works

Paths in this section are in the fork (`work\PBR-pr\src`) unless marked as ours.

Mask. The engine's SkinShader virtuals bracket every skin draw: slot 27 (PrepareGeometryForRendering) and slot 35 (PostGeometry). The fork detours both (`src\NewVegas\Hooks\Hooks.cpp:41-42`; `Shaders.cpp:1216-1223, 1262-1322`).
- `BindForSkinDraw` (`src\effects\SkinScattering.cpp:251-308`) binds two extra fp16 targets as MRT 1 and 2. They are created in `EnsureTarget` (165-216).
- Gates: NVR's own SKIN pixel shader only (`Shaders.cpp:1285-1287`), world scene only (`SkinScattering.cpp:257`), opaque or additive ONE/ONE draws only (266-278).
- The skin pixel shader writes `PS_OUTPUT` (`src\hlsl\NewVegas\Shaders\SkinTemplate.hlsl:366-370, 511-521`). COLOR1 = highlight share, with distance in alpha. COLOR2 = albedo, with the luma of the colour in alpha.
- Both targets are cleared on the first skin draw of each frame (`SkinScattering.cpp:220-242, 289-292`). `Unbind` runs at PostGeometry and at scene end (312-323).

Blur. `src\hlsl\NewVegas\Effects\SkinScattering.fx.hlsl`, two full-screen passes (techniques at 191-200).
- Skin test: depth matches the stored distance, and the colour still matches what skin drew (fx:61-81). Anything drawn over skin is left alone.
- Light is divided by albedo^detail before the blur and multiplied back after (fx:93-97, 162-169). Dark texels are excluded (fx:87-90).
- 17 taps with a kernel from d'Eon's profile (`SkinScattering.cpp:82-147`). Taps run only on skin pixels.
- It runs first in the chain, right after the scene copy (`fork src\core\ShaderManager.cpp:878-883`), so the colour test sees the scene as drawn.

Per-pixel side (`src\hlsl\NewVegas\Shaders\Includes\SkinLighting.hlsl`): spherical-Gaussian diffusion with curvature from screen-space derivatives (106-111, 119-147); per-channel softened normals; penumbra tint (150-153); thin-part transmission (199-203); dual-lobe GGX (156-171). Per-pixel diffusion switches itself off when the blur is bound (line 111), to avoid doubling (comment at 39-41).

Their pitfalls, from their own comments:
- Clear hits every bound target, so the skin target is cleared alone (`SkinScattering.cpp:218-219`).
- MRT targets must match the scene's MSAA (200-211). Our chain already refuses MSAA targets (`src\core\ShaderManager.cpp:1625`).
- Engine highlight passes doubled the highlights, so they are muted while skin draws (`Shaders.cpp:1225-1232, 1272-1276, 1318-1321`).
- FaceGen light-only passes bound the bare texture, which tinted lips and brows (`Shaders.cpp:1233-1260`).
- Interior faces take different pass lists (`Shaders.cpp:1324-1345`).
- Not blurred: first-person arms, reflections, fading or additive-only actors (`SkinScattering.h:21-27`).

Their costs: the fork's docs have no timings. The drivers are the full-screen passes and the extra full copies (`fork src\core\EffectRecord.cpp:366, 381-388`), plus the MRT clears.

## 3. Options for our build

### (a) Better per-pixel model in our SKIN shaders

Files:
- New include `src\hlsl\NewVegas\Shaders\Includes\SkinScatter.hlsl`: SG diffusion, transmission, penumbra tint.
- `Includes\Skin.hlsl`: replace the dead `Skin()` code. Only SKIN2000/2001/2002/2003/2006 .pso include it, so object shaders are untouched.
- Do NOT touch `Includes\SkinHelpers.hlsl`: object templates share it (`ObjectTemplate.hlsl:223`, `EnvTemplate.hlsl:51`, `ObjectRefractionTemplate.hlsl:35`).
- Vertex shaders to change (one interpolant each, for the vertex normal). The pairings are SKIN2000.vso for SKIN2000 and SKIN2001.vso for SKIN2001 (by name); SKIN2005.vso for SKIN2002 (`SKIN2002.pso.hlsl:48`); SKIN2013.vso for SKIN2006 (`SKIN2006.pso.hlsl:40`). SKIN2003's pairing was not checked. SKIN2000.vso's VS_OUTPUT (lines 34-40) has no normal output, and curvature needs one.
- `src\effects\Skin.cpp/.h`: new constants through RegisterConstant by name, with no fixed register numbers.

Cost: roughly 100-200 ALU and one extra normal fetch per skin pixel (sun plus one lamp). Estimate at 2560x1440 with skin on screen: about 0.1 ms.
Risks: low. No MRT, no depth-stencil games. The vertex change must be checked on every actor type. Keep the existing c100-c107 and c240-c247 pins.
VPS: unchanged. VPS routes every SKIN name to SkinVPSTemplate, so this code is never reached. Keep that gate.
Look: softer terminator, red-tinted shadow edge, a glow on ears. It does not spread light across the screen, so on its own it is not convincing SSS.
Gamma: the fork's model assumes linear lighting. Our build has no linear path (encodeColor, decodeColor and linearLighting are absent). If we want the fork's maths, decode inside the skin shaders only (x^2 in, sqrt out).

### (b) Port the separable blur

New files: `src\effects\SkinScattering.cpp/.h` and `src\hlsl\NewVegas\Effects\SkinScattering.fx.hlsl`. Keep the fork's GPL attribution (`SkinScattering.h:3-5`, fx:2-5).
Edits:
- `src\effects\Effects.h`, `src\core\ShaderManager.h/.cpp`: register near ShaderManager.cpp:49-51. Call right after `Chain.Begin` (`ShaderManager.cpp:1087`), before ShadowsExteriors (1114) and AO (1131).
- `src\NewVegas\Hooks\Hooks.cpp` (slots 27 and 35), `Shaders.cpp/.h` (PrepareGeometry and PostGeometry, muting, FaceGen fix), `Render.cpp` (world-scene flag; RenderWorldSceneGraphHook at 1547).
- SKIN2000-2011 .pso that draw skin, and `SkinVPSTemplate.hlsl`: write COLOR1 and COLOR2.
- `resource\NewVegasReloaded.dll.defaults.toml`: a new `[_Shaders.Skin.Scattering]` section.

Savings from our chain: `src\core\EffectRecord.cpp:436-462` renders each pass into the chain's spare texture, so the fork's per-pass copies go away. Set `writesWholeTarget` (`EffectRecord.h:37`) so no prefill copy happens (`EffectRecord.cpp:451`).
Simplification: our non-VPS SKIN shaders have no highlights (SKIN_SPECULAR_STRENGTH = 0), so COLOR1.rgb can be 0 and only distance is needed. That allows R32F for distance and A8R8G8B8 for albedo and luma. It needs independent MRT bit depths, which CombineDepth already checks (`CombineDepth.cpp:116-119`).

Cost at 2560x1440 (estimate). One fp16 target is 29.5 MB. The source copy, the MRT clear and the two passes move about 410 MB per frame when skin is on screen. At about 200 GB/s that is roughly 2 ms. With R32F and A8 targets, roughly 1.6 ms. Scissored to the skin bounds plus about 32 px of padding, roughly 0.3-0.5 ms if skin covers about 15% of the screen. Taps: 17 per pass, 4-5 fetches each, on skin pixels only.

Risks:
1. GTX 1070 depth-stencil drop. CombineDepth says a two-target draw was silently dropped on a GTX 1070 while the game's depth-stencil was bound (`CombineDepth.cpp:26-28, 134-138`). Our fog pass unbinds it because it is full-screen (`VolumetricFog.cpp:307`). A skin draw cannot unbind it, because it needs the depth test. If the draw is dropped, the skin disappears, not just the blur. Add a marker probe like `CombineDepth.cpp:124-186` before enabling. The repo CLAUDE.md says FNV often runs under DXVK, where this may differ. Test on the runtime you actually use.
2. Multi-pass skin. The light-only, additive and FaceGen passes need the fork's muting and albedo handling (section 2).
3. Gamma convention. The fork decodes with x^2 (fx:84-85). Our effects use exact sRGB (`src\hlsl\NewVegas\Effects\Includes\Helpers.hlsl:29-45`). Pick one. x^2 is cheaper and matches the fork.
4. Registers. Do not copy the fork's explicit registers (`SkinLighting.hlsl:34-46`, MergedLights c154-c155). In our build c146-c166 hold the old-camera shadow matrices (`Shadow.hlsl:79-87`), c137 starts sky (`SkyAmbient.hlsl:43`), and c134-c135 are PBR (`PBRScale.hlsl:19, 33`). Use RegisterConstant by name, as `Skin.cpp:4-5` does.
5. Target lifetime. Release targets and call ClearSampler on resolution change (fork `SkinScattering.cpp:151-162, 165-216`).

VPS: the LUT already scatters, so a blur on top doubles it. Keep the blur off when VanillaPlusSkin.dll is loaded.

## 4. Recommended plan

Each step is sized for one sitting. Every new setting defaults off.

1. Per-pixel model (option a). Delete the dead `Skin()` code and its five keys. Add `Includes\SkinScatter.hlsl`, the vertex-normal interpolant and the constants. Setting `[_Shaders.Skin.Main] PerPixelDiffusion = false`, with `Translucency = 0.5` used only when it is on. With it off, output must match the current build pixel for pixel (the dead code never ran). Derivatives stay at top level (see the comment at `SKIN2000.pso.hlsl:74-75`).
2. GPU probe, no visuals. At startup, draw a tiny MRT pass with the depth-stencil bound and read back a marker, as CombineDepth does. Log the result. Setting `[_Shaders.Skin.Scattering] Probe = false` (a diagnostic). The result decides whether step 3 may run.
3. Blur, minimal (option b). Port the effect with `ScreenSpace = false`. Write COLOR1 and COLOR2 in the SKIN pixel shaders and in `SkinVPSTemplate.hlsl`, but keep the blur off for VPS. Hook PrepareGeometry and PostGeometry, and mute the engine's highlight passes. Keys, using the fork's defaults: `ScreenSpace = false`, `Width = 8.0`, `DepthFollow = 1.0`, `AlbedoDetail = 1.0`, `StrengthRed = 0.48`, `StrengthGreen = 0.41`, `StrengthBlue = 0.28`, `FalloffRed = 1.0`, `FalloffGreen = 0.37`, `FalloffBlue = 0.3`.
4. Measure. Use the existing per-effect timer (`EffectRecord` renderTime). Record ms in a town (outdoors) and an interior, with skin on screen.
5. Optimise only if step 4 says so: R32F and A8 targets, a skin-bounds scissor, and skipping skin draws far from the camera.
6. Later: light-only SKIN2004-2007 albedo, interior variants, SKIN2008-2011.

Setting names:
- `[_Shaders.Skin.Main]`: `PerPixelDiffusion` (false), `Translucency` (0.5, used only when on).
- `[_Shaders.Skin.Scattering]`: `ScreenSpace` (false), `Probe` (false), `Width`, `DepthFollow`, `AlbedoDetail`, `StrengthRed/Green/Blue`, `FalloffRed/Green/Blue`.
- Remove as dead: `[_Shaders.Skin.Main]` `Attenuation`, `CoeffRed/Green/Blue`, `MaterialThickness`, `RimScalar`, `SpecularPower`.

## Not verified

- No compiles, no GPU timings, no in-game run. All costs are estimates.
- The VPS LUT-in-GlowMap reading comes from the code, not from VPS's texture files.
- Whether GTX 1070 drops MRT draws under DXVK. Our note covers native D3D9 only.
- Whether the engine's highlight passes draw on skin in our build. The code shows no muting for them; I did not confirm this with a frame capture.

## Summary

1. Our default skin has no real SSS: the wrap/rim code and its five settings are dead, and only the VPS path (when VanillaPlusSkin.dll loads) has a real LUT.
2. Cheapest first: a per-pixel model in the SKIN shaders (about 0.1 ms, no MRT), default off, with the dead code removed.
3. The convincing part is the fork's screen-space blur (about 0.3-2 ms depending on skin coverage and target size), worth porting next, default off.
4. Gate the blur behind a GPU probe, because a skin draw with the depth-stencil bound may be dropped on a GTX 1070.
5. Keep VPS users on the LUT path, and never stack the blur on top of the LUT.
