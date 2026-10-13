# Freeside white overlay after Wait: bug hunt

Read-only investigation, 2026-10-11. No source, build, git, or game-folder changes were made.

Sources: NVR-pbr-effects working tree (branch `experiment/pbr-effects`, uncommitted changes present), upstream NVR-Lunacy `master` for comparison, session log `NewVegasReloaded_2026-10-11_18-19-31.log` (18:19 to 18:34), and the installed DLL `Data\NVSE\Plugins\NewVegasReloaded.dll` (timestamp 18:53, after the session ended).

## Answer in brief

1. **A game image-space modifier stuck active (fade, night eye, or cinematic).** An entry left in `TES::activeIMODs` or `Sky::currFadeInIMOD` keeps the game's c22 (Fade colour and alpha) and c19/c20 set. The final HDR shader applies c22 last, so a stuck entry gives a flat white screen. NVR writes none of these in that pass, so turning NVR effects off cannot clear it. This fits "flat, after Wait, survives NVR toggles".
2. **One NaN in the HDR scene, kept by the game's eye adaptation.** An NVR pre-tonemap effect writes NaN once at the time change. If the adapted-light value blends with its previous value, NaN stays NaN and survives after the effect is off. The strongest unguarded writer is the sun-direction normalize (`ShaderManager.cpp:371-373`), which feeds GodRays sky glare (`GodRays.fx.hlsl:71`). The trigger is not confirmed.
3. **NVR's sky shader writes the wrong bytes into c20 (Tint).** `Shaders.cpp:12` passes `&skyObjectID` with a count of one float4, so c20 becomes (skyObjectID, sunDisk.r, sunDisk.g, sunDisk.b). It is not gated by effects and changes with time of day. The game very likely sets c20 itself, in which case it is harmless. Ranked last.

**Single best test:** with the 18:53 DLL, Wait in Freeside until the overlay appears, press F10 once (it also starts GPU timing; press it again to stop), then read the newest `IMAGESPACE` lines in the new log. An active fade, night eye, or cinematic entry means cause 1. `IMAGESPACE 0 active` with the white still on screen points to cause 2 or 3.

## 0. Facts that limit the causes

- Tonemapping Enabled=false means NVR's ISHDR is never bound. `NiD3DPixelShaderEx::SetupShader` binds the vanilla handle when `Enabled` is false (`ShaderRecord.cpp:629-630`). `ShaderCollection::SwitchShader` sets that flag (`ShaderCollection.cpp:6-8`). The NVR records are still compiled and loaded (log lines 782-788), but they are not bound.
- The game's luminance pass `HDRDOWN9` (map 0x78FA30) and the adaptation code are the game's own. `src\hlsl\NewVegas\Shaders` has no replacement for them, so the luminance measure always reads the HDR scene that NVR writes into.
- The user TOML (saved 18:26) still has these Enabled=true: Specular, VolumetricLight, BounceLight, HeatHaze (all default off), GodRays, VolumetricFog, Sky, Underwater, WetWorld, Flashlight, Bloom, DepthOfField, AO, and others. Only Tonemapping, Exposure, and BloomLegacy are off. Check the in-menu state before trusting "every effect off".
- The session log has no pixel data and no IMOD dump. The sun-step log stops after 20 lines (`ShadowsExterior.cpp:791`), so later waits are not in it.

## 1. Which final shader runs with Tonemapping off, and what c1, c19, c20, c22 do

- The bound final shader is the game's own `ISHDRBLENDINSHADERCIN`. `GetShaderCollection` sends every `ISHDR*` and `HDR*` name to the Tonemapping collection (`ShaderManager.cpp:520`), and the vanilla handle is bound while that collection is off (section 0). The vanilla math is not in the repo, so the constant meanings below come from NVR's copy of the shader (`ISHDRBLENDINSHADERCIN.pso.hlsl`).
- c1 HDRParam.x: bright-pass cutoff (lines 121-122, NVR's copy of the vanilla bloom combine).
- c19 Cinematic: x saturation (line 100), y average-luma pivot for contrast (line 146), z contrast gain (line 146), w brightness multiplier (line 128).
- c20 Tint: rgb colour, a strength (line 101).
- c22 Fade: rgb colour, a alpha. Applied last: `final = lerp(final, Fade.rgb, Fade.a)` (line 147). The header calls it "Night eye color" (line 10).
- NVR writes none of c19 or c22 in the image-space pass. The only other pixel-constant writes are c1 in a separate NVR draw (`MaterialPass.cpp:897`) and c20 (`Shaders.cpp:12`, see cause 3).
- How a stuck fade makes white: an entry left in `TES::activeIMODs` (`Game.h:4395`, +0x6C) or in `Sky::currFadeInIMOD` (`Game.h:4176`, +0x11C) keeps Fade.a near 1 with a white colour. `ShaderManager.cpp:1288-1290` names "the wait-menu fade" as one of these entries. Expiry uses a tick-count helper (`ImageSpaceModifierInstanceForm::IsExpired` at 0x1287B0, tick helper at 0x128990), so a stuck entry would mean its stop never ran. Not verified.

## 2. Eye adaptation, NaN, and which NVR writers reach the HDR scene

- The game's adaptation is temporal. Engine symbols: `ImageSpaceEffectHDR::GetEyeAdaptSpeed` (0x1C8C50), `GetTargetLUM` (0x1C8C90), `ClearAdaptedLight` (0x7AC4A0), and the console command `Cmd_ClearAdaptedLight` (0x1D37C0).
- If adapted = lerp(adapted, measured, speed), then one NaN in the adapted value stays NaN for every later frame (`lerp(NaN, x, s)` is NaN for any s). Turning effects off stops new NaN but not the stored one. Whether a NaN in the scene reaches the adapted value depends on the game's luminance reduction, which is not in the repo.
- A zero measure makes exposure huge, which gives white. A NaN exposure gives NaN output, which the GPU may show as black or white. Not known for this card.
- Clamps differ in how they treat NaN. `max(x, 0)` and `shade()` (`Effects\Includes\Helpers.hlsl:3`, `max(dot,0)`) probably return the non-NaN operand, so they hide NaN on common GPUs. `pows()` (`Shaders\Includes\Helpers.hlsl:19`, `pow(abs(a),b)*sign(a)`) does not hide NaN.
- NVR writers into the HDR scene (pre-tonemap chain, `ShaderManager.cpp:975-1281`):

  | Writer | On in user TOML | Time-of-day input | NaN route | Risk |
  |---|---|---|---|---|
  | GodRays sky mask | yes | sun direction from `directionalLight`; daytime exteriors override it (377-378), night and dusk do not | `pows(dot(ViewSpaceLightDir, ...), 180)` at `GodRays.fx.hlsl:71`, no clamp; ViewSpaceLightDir comes from the unguarded normalize at line 373 | high if the light direction can be zero (not proven) |
  | VolumetricLight (new, uncommitted) | yes | `NightStrength` lerp (`VolumetricLight.cpp:33`) | cos with SmoothedSunDir (`VolumetricLight.fx.hlsl:160`); if max() squashes a NaN cos to 1e-4, phase hits its cap of 8 (bright, not NaN) | medium (brightness) |
  | Specular | yes | sun vector (`Specular.fx.hlsl:134`) | NaN L is probably squashed by saturate in NdotL and NdotH | low |
  | VolumetricFog | yes | SmoothedSunDir (`VolumetricFog.fx.hlsl:368`) | not fully reviewed (933 lines, 295 divisions) | medium |
  | BounceLight | yes (default off) | none | tangent is guarded (`BounceLight.fx.hlsl:160-161`); `sqrt(light.rg)` at 147 | low |
  | Sky (SKY, SKYTEX) | yes | sun and sky colours | `normalize(eyeDir.xy)` at `SKYTEX.pso.hlsl:83` is zero only at the exact zenith or nadir; SkyAmbient is guarded (86-90) | low |
  | Sandstorm (new) | no | storm only | none found; 51 divisions not reviewed | low |
  | Exposure | no | luminance | `log()` in `Exposure.fx.hlsl`; log(0) is -inf | off here |
  | Bloom | yes | none | writes only `TESR_BloomBuffer` (`Bloom.cpp:100-142`), read only by the NVR ISHDR, which is off here | none for the scene |
  | Water | yes | yes | not reviewed in this pass | unknown |

- Post-tonemap effects (HeatHaze, DOF, Cinema, ImageAdjust, WaterLens, DitherBuster) write LDR after the game's HDR pass, so they cannot reach the adaptation. They would only show while on.
- Time of day: `SunPosition` is guarded (`ShaderManager.cpp:365-368`, a fix for a cold interior load). `SunDir` has the same normalize with no guard (line 373). Nothing in the code proves that `directionalLight->direction` can be zero in an exterior after a wait.

## 3. Our build against upstream master

- Bloom path: `Bloom.cpp` and `Bloom.fx.hlsl` are unchanged against master. Bloom writes only `TESR_BloomBuffer`, and with Tonemapping off nothing in the vanilla path reads it, so it cannot leave a bright texture on screen here.
- HDR targets: `TextureManager.cpp:17-18` uses A16B16G16R16F for SourceBuffer and RenderedBuffer, the same as master (lines 16-17). `CreateHDRRenderTarget` (`NewVegas/Base.h:131-135`) returns NULL. There is no `FrameChain*` file in `src\core`. The frame-chain code is in `ShaderManager.cpp` around 1627-1730, not re-read in this pass.
- New in our build and relevant: the world-scene guard (`Render.cpp:1715-1722`; 4 hits here, 0 in master). After 10 frames with no world render, which is what the Wait menu does, the hook skips the pre-tonemap effects, the chain, and `UpdateConstants` (lines 1729-1731). The game's image space then runs on whatever SourceTarget holds: the last world frame or the menu background. The log shows one guard event (18:22:04, line 967, StripGomorrah). The Freeside waits at 18:25 show no guard event.
- Sky projection cache (`Sky.cpp`, new): returns early when its inputs match, so it can keep stale sky irradiance. That affects PBR lighting, not the HDR overlay.
- HueSafeClip (ISHDR lines 130-140): active only with Tonemapping on, so not on this path.
- Not new, so not a regression: the unguarded SunDir normalize (master `ShaderManager.cpp:356-358`), the GodRays sky mask (master `GodRays.fx.hlsl:71`), and the c20 write (master `Shaders.cpp:12`).

## 4. Cheap, safe guards (not applied)

1. C++: guard SunDir the way SunPosition is guarded (`ShaderManager.cpp:371-374`). If the squared length is tiny, fall back to SunPosition or (0,0,1). About 4 lines, no shader change. This is the only NaN source found that depends on time of day.
2. Shader: use `shade(...)` in `GodRays.fx.hlsl:71` and `:258` (max(dot,0)) in place of raw `pows(dot(...))`. In `Specular.fx.hlsl:134`, fall back to a fixed sun direction when `TESR_SunDirection` is near zero.
3. c20: pass a real float4, `D3DXVECTOR4 tint(ShaderConst.skyObjectID, 0, 0, 0)`, in `Shaders.cpp:12`. This changes the look only if the game does not set c20 itself.
4. Adaptation: call `ImageSpaceEffectHDR::ClearAdaptedLight` (0x7AC4A0) once where the sun-jump detector already logs "waiting, sleeping or loading" (`ShadowsExterior.cpp:770-773`). That re-seeds adaptation after a time skip. Needs one test that the call is safe.
5. A full-screen NaN scrub pass each frame would work, but it costs a pass per frame. Keep it as the last option.

## 5. The one in-game test

- The installed DLL contains the IMOD dump strings. The session log has no `IMAGESPACE` line at all, and the DLL was replaced at 18:53, after the session ended at 18:34. The session therefore ran an older build, so the dump never ran. The function also returns early if `Tes` is null (`ShaderManager.cpp:1305`).
- Steps: (a) check the in-menu effect states; (b) go to Freeside and Wait until the overlay appears; (c) press F10 once; (d) read the newest `IMAGESPACE active N` lines in the new log.
- Reading it: an active fade, night eye, cinematic, or tint entry means cause 1. `IMAGESPACE 0 active` with white still on screen means cause 2 or 3. To separate those two, run `ClearAdaptedLight` in the console (not tested here; check that the console accepts it). If the white clears, it is cause 2.

## 6. Not verified

- Whether `directionalLight->direction` is ever (0,0,0) in an exterior after a wait.
- Whether the game sets c20 before the vanilla HDR pass. If it does, cause 3 goes away.
- Whether a save load resets the adaptation (not checked in code).
- The exact vanilla HDR math (not in the repo).
- Whether the VolumetricFog and Water shaders produce NaN (not reviewed line by line).
- When the white appeared (no pixel data in the log).
