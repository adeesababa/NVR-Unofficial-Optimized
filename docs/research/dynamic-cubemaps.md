# Dynamic cubemaps for NVR: research

Date 2026-10-11. Read-only: no source edits, no builds, no game-folder changes, no git writes.
Reference: `work\PBR-pr` (commit c12d09e). Ours: `work\NVR-pbr-effects` (HEAD d52d93ab).
Short refs: fx = PBR-pr `DynamicCubemaps.fx.hlsl`, cpp = PBR-pr `DynamicCubemaps.cpp`, h = PBR-pr `DynamicCubemaps.h`. Other paths are in our tree unless marked PBR-pr.
All costs are estimates from texel and tap counts. Nothing was compiled, run or timed.

## In short

- The reference builds a 128 px cube from the screen every frame. Only its "authored" PBR materials read it (materials with an `_rmaos` map beside the diffuse texture). We have no such materials, so a port would feed our metal and our env-map pass.
- Our biggest gap: metal ignores which side of the sky the sun is on. Sharper use of the sky harmonics we already compute should close most of it, with no new passes (about 0.01 ms).
- For real surroundings (a red room, the actual sky), a 64 px capture is the lean version (about 0.03-0.1 ms, default off).
- The full port is too big for one sitting. Its cons (ghosting, pops, flicker, seams) come with any cube size.

## Visual pros and cons (plain English)

**What would look better**
- Guns and metal: a gun held toward a sunset should get warm light on the side facing the sun. Today the metal only asks whether a reflection points up or down, so both sides look alike.
- Rooms: a gun in a room lit by a red lamp should take red on the side facing the lamp and the walls. Today indoor metal takes only the room's overall tint.
- Glass and chrome props: once the cube is wired in, they would reflect the real surroundings. Today the game's fixed cube is used unless [Shaders.PBR.EnvMaps] Lighting is above 0 (default 0).
- Wet ground: puddles could mirror the sky and nearby buildings. Today they show only sun and lamp glints.

**What could look wrong**
- Ghosting: a spot seen earlier keeps its old look for several seconds after you turn away. An NPC you passed can still show in a gun's shine.
- Filling in: directions never on screen show blurred nearby colour, or the sky model. The shine pops when that area first comes into view.
- Lag: one frame behind, and about five frames to settle after a fast turn.
- Soft or blocky mirrors: a 128 px cube has about 0.7 degrees per texel, 64 px about 1.4, 32 px about 2.8. At 1440p (about 20 pixels per degree) that is about 14, 28 or 56 screen pixels. Polished parts look blocky. Satin metal (our default Roughness is 0.5) hides it.
- Wrong near reflections: anything closer than about 24 units is left out (fx:85). A gun pressed against a wall shows far-away fill, and reflections have no depth, so they never line up with real objects.
- Self-reflection: the first-person gun and arms are left out of the capture (fx:96-97), so the gun never mirrors itself. But the spot it sits in shows what is behind it, which can look wrong.
- Seams: detail stops at the screen edge and fill starts just outside it. Visible when turning.
- Doors and loads: a reset on an interior/exterior change, or a jump over 1500 units (cpp:7, 24), then a few frames of fill.
- Indoor shimmer: indoor fill is the room's average, so a flickering lamp makes the shine flicker.

| Effect | Full port | 64 px capture | No cube, sharp sky |
|---|---|---|---|
| Sunset side on metal | yes | yes | yes, smooth |
| Red-lit room, walls, lamps | yes | rough metal only | no |
| Real sky (clouds, sun disc) | yes | rough metal only | no |
| Wet ground | yes, needs new code | later | no |
| Ghosting, lag, pops, seams | yes | yes | no |
| Blocky polished mirrors | little | yes | no |
| Indoor shimmer | yes | yes | no |
| Cost (estimate) | 0.1-0.3 ms | 0.03-0.1 ms | about 0.01 ms |

## 1. How the reference cubemap works (PBR-pr)

Once per frame, on the scene as drawn (ShaderManager.cpp:887, PBR-pr; cpp:120-201):
1. Capture: all six faces, 128x128 texels each (fx:87-106). A texel whose direction is on screen, more than 24 units away and not the first-person model takes the scene colour (linear, clamped to 64) and blends 50/50 with last frame (fx:85-101). Other texels keep their value and decay 0.5% a frame (fx:105, cpp:44).
2. Downsample: 7 copies per face into the mips (cpp:112-116).
3. Infer: empty texels step to coarser mips (fx:124-129). Still empty: the sky's radiance outdoors (fx:112-120, 135). Indoors: the average of the room's six faces, or the ambient colour before anything was seen (fx:138-144).
4. Prefilter: GGX importance sampling, 16 samples per texel, one roughness per mip, roughness = mip / 7 (fx:150-194).

- Size and format: 128 px, FP16 (A16B16G16R16F, cpp:64), 8 mips, four cubes (two capture, one inferred, one env), about 4 MB of video memory (h:20-21).
- Faces: all six update every frame. Only about a tenth of the sphere is on screen at a typical view (estimate), so most of each cube is filled, not captured.
- Draws: 54 per frame (6 capture, 6 infer, 42 prefilter), 90 StretchRect copies, one SetRenderTarget per face draw (cpp:100-108, 161-179). One target per pass, so no MRT. The depth-stencil is unbound for these passes (cpp:136-138), the same precaution as CombineDepth.cpp:134-135, after the GTX 1070 dropped an MRT draw (CombineDepth.cpp:26-31).
- Texture taps per frame: capture about 0.13M, infer about 0.15M outdoors, prefilter about 0.52M (16 per texel over 32.8k texels). About 0.8M outdoors and 1.4M indoors, because the room average reads six extra taps per texel (fx:138-143) that could be computed once.
- Cost at 2560x1440 on a GTX 1070: about 0.1-0.3 ms, GPU and CPU together (estimate). The cube size is fixed, so resolution barely matters; only the capture reads screen pixels. The reference claims "well under a millisecond" (PBR-pr defaults toml:640). Our Specular effect costs about 0.44 ms (project notes).

## 2. Which surfaces read it, and how

Reference (PBR-pr), authored `_rmaos` materials only. The hook binds the cube to s11 per draw (Hooks/Shaders.cpp:219-229). The game's own env passes stay on top by default (VanillaEnvMapOnPBR = true, PBR-pr toml:607; Shaders.cpp:201-210).
- Object reflection and indoor ambient: getObjectSkyReflection, Includes/Object.hlsl:306-342 (cube read at 335); called from ObjectTemplate.hlsl:832 and 1112.
- Wet film on objects: ObjectTemplate.hlsl:837-853 and 1116-1134 (cube at 845 and 1126).
- Parallax wet film: ParallaxTemplate.hlsl:692-701.
- Not read: water, terrain, skin (no special case), glass (the game's own env pass still draws).

Ours, for comparison:
- First-person metal: a sky model by world-up only, plus the room's ambient colour (Includes/Object.hlsl:203-218; ObjectTemplate.hlsl:797-802). Metal is off in the defaults (MetalOn = false, toml:679). Project notes suggest the user runs it on.
- Glass and chrome: the game's static cube, scaled by the light only when Lighting > 0 (EnvTemplate.hlsl:139-140; PBR.cpp:41; default 0, toml:692).
- Specular post effect: outdoors only, sky by world-up (Specular.cpp:42-44; Specular.fx.hlsl:158-166).
- Wet ground: WetWorld post effect, sun and lamp glints only. Its sky Fresnel term is computed but commented out (WetWorld.fx.hlsl:256, 289-290).
- No `_rmaos` path and no per-object wet layer in our build.

The difference in one line: the reference puts the captured frame into the reflection; ours uses a sky model.

## 3. Options for our build

### (a) Full port
- Files: new src/effects/DynamicCubemaps.h/.cpp and src/hlsl/NewVegas/Effects/DynamicCubemaps.fx.hlsl; ShaderManager.h/.cpp (register; capture right after the scene copy at 1088, before the shadow and AO passes; debug view near 1522); Effects.h; a per-draw bind like PointShadowForward.h:410-411; Object.hlsl, ObjectTemplate.hlsl, EnvTemplate.hlsl; vcxproj; defaults toml.
- Samplers and constants: the reference's s11 is taken in our build. Interior object shaders compiled with INTERIOR_SHADOWS (ShaderManager.cpp:612-617; on by default, toml:884) declare five point-shadow cubes at s11-s15 (PointShadowForward.hlsl:49-54). Use s10 instead: free unless SUN_CROSSFADE is on (Shadow.hlsl:74-92; CrossFade = false, toml:741). Use c190, which no register() in src\hlsl\NewVegas uses. The cube's own settings are effect parameters, so they have no fixed registers.
- Cost: about 0.1-0.3 ms; about 4 MB.
- Risks:
  - Sampler order, the main trap: the reference declares TESR_NormalsBuffer after its cube samplers (fx:31-40). Our binder assigns stages by declaration order (EffectRecord.cpp:317, 351), so normals would land on stage 7 while the shader reads s3. Only the mirror debug view reads normals, so it would look wrong there. Declare in register order (the rule at Specular.fx.hlsl:25).
  - s10 is also used by the terrain's normal-map array (Shadow.hlsl:59-60 says it spans s7-s13). Restore it after each object draw, as the reference does for s10 and s11 (Hooks/Shaders.cpp, PBR-pr, 76-79 and 236-249).
  - Colour: the reference decodes the frame as gamma 2 (fx:99). Ours is sRGB, decoded by linearize() (Specular.fx.hlsl:211). Our object lighting is not linear, so read the cube sqrt-encoded (the reference does this at its Object.hlsl:339).
  - MRT: none, as long as each pass keeps one target. Keep it that way (CombineDepth.cpp:26-31).
  - DXVK: cube-face copies are untested. Check the "D3D9 runtime" line and compare DXVK with native.

### (b) 64 px capture (recommended second step)
- Same code at 64 px, with 4 prefilter levels instead of 7 (about 30 draws instead of 54). 32 px saves less than its texel count suggests, because call overhead stays.
- Feeds two things only: the metal's env term (Includes/Object.hlsl:215-218, sampler s10) and EnvTemplate's surroundings (EnvTemplate.hlsl:139, sampler s2, which is free there; its samplers are s0, s1, s3).
- Its sky part can come from option (c); its room part from the room average.
- Cost: about 0.03-0.1 ms (about a quarter of the pixel work of the full port).
- Extra risk: blocky polished mirrors. If they look bad, use the cube only for the rough lobe and keep the sharp sky for polished metal.

### (c) No cube: sharp sky harmonics, plus notes on screen-space options
- The sky harmonics we already compute can be read sharp. Sky.cpp:143-154 divides out the cosine weights (A0/pi = 1, A1/pi = 2/3, A2/pi = 1/4). Multiplying the same nine values by 1, 1.5 and 4 gives the sky's radiance along any direction. The reference does exactly this in its SkyRadiance (fx:112-120). The result is smooth: no sun disc, clouds or room colour.
- Files: SkyAmbient.hlsl (add the function for both SKYLIGHTING_MODE branches; mode 1 already has GetSkyRadiance, lines 67-80); Includes/Object.hlsl getMetalAmbient (215-218); EnvTemplate.hlsl:139.
- Bearing for the metal: the metal path packs only world-up into spare channels (ObjectTemplate.hlsl:395-411). Take the bearing from the geometric normal, shadowGeometricNormal (ObjectTemplate.hlsl:730, already used at 795), and keep upReflect for height so normal-map bumps still show.
- Samplers, constants, MRT: none new. Cost about 0.01 ms.
- Risks:
  - Below the horizon the harmonics are zero (Sky.cpp:69-70, 103-104), so ground reflections go black unless you add the reference's small ground term (fx:118-119).
  - It needs Shaders.Sky on: the harmonics refresh only then (VolumetricFog.cpp:27).
  - Sharp weights give more contrast than the irradiance, so check for dark bands.
  - Object shaders are large, and ps_3_0 allows 512 static instructions. Compile and check the log.
  - Indoors it changes nothing, because SkylightingScale is 0 indoors (toml:672).
- Screen-space reflections on our depth and normal buffers: exact for what is on screen, with no ghosting, but about 0.3-1 ms (estimate). They would hit every surface, since the post pass has no per-pixel metal mask, and the first-person weapon has its own depth, so it can smear into its own reflection. Not recommended now.
- Untested idea for indoor metal: tint it by the colour of the nearest lamps (PSLightColor is already in the shader). Approximate, with no room colour.

## 4. Recommended plan (each step fits one sitting; everything default off)

Step 1: sharp sky for metal and the env pass.
- Setting: [Shaders.PBR.Metal] SharpSky = 0. Use a compile-time define through ShaderRecord::AppendDefine (the ShaderRecord.cpp:184 pattern), as the sky mode does (SkyAmbient.hlsl:4-7). Changing it recompiles shaders.
- Files: SkyAmbient.hlsl, Includes/Object.hlsl, ObjectTemplate.hlsl (bearing), EnvTemplate.hlsl, ShaderRecord.cpp, defaults toml.
- Test: first-person gun at sunset, facing east and then west; an indoor room (must not change); ground reflections (must not go black).

Step 2: 64 px capture for metal and the env surroundings.
- Settings: [Shaders.DynamicCubemaps.Status] Enabled = 0; [Shaders.DynamicCubemaps.Main] Size = 64, DebugView = 0 (0-3), DebugRoughness = 0.0; [Shaders.PBR.Metal] CubeReflections = 0.
- Build and deploy: add the new .cpp/.h to NewVegasReloaded.vcxproj. Copy the .fx.hlsl to the same relative path under the deployed Shaders folder, and the DLL to nvse\Plugins (CLAUDE.md). Compare the whole Shaders folder before installing (install-consistency note). Clear Shaders\...\Cache if a change does not show.
- Verify: "Succesfully bound" log lines for the cube and normals (EffectRecord.cpp:346); DebugView 3 (coverage) and 2 (mirror); a GpuTimer around the pass (as ShaderManager.cpp:987-996); gun screenshots with the cube on and off.

Step 3 (later, optional): puddles read the same cube in WetWorld.fx.hlsl (a post effect, so no object-shader change). Setting: [Shaders.WetWorld.Main] CubeReflection = 0.

Not recommended now: the full port (a).

## Not checked
- No timings. Every ms figure is an estimate from counts.
- The reference's mirror view was not run. The normals-stage problem is inferred from the binder code (EffectRecord.cpp:270 in PBR-pr, 317 in ours), not observed.
- Nothing tested on DXVK or on the GTX 1070.
