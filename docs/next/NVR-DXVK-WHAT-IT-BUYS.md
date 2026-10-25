# NVR DXVK fork: what it really buys (plain answers, 2026-10-25)

Short version: the fork is mainly about **removing shader limits**, not about raw speed. It does nothing for things popping
in at a distance caused by LOD/fade distances. It only helps NVR when the game runs through DXVK, and today you play native
D3D9.

## 1. Performance or fewer limits?
**Mainly fewer limits.** Under D3D9 every shader (the game's and NVR's) is Shader Model 3.0: 224 float constants, 16
textures, 32 temporary registers, and on your native driver about 3,600 instructions per shader. No compute shaders, no
shared memory, no buffers. The fork lets NVR run its own Vulkan shaders inside DXVK (compute and full-screen passes, and
since extension 1.2, replacements for the game's own pixel shaders) where none of those caps exist.

Speed: the fork does not make the game itself faster. DXVK translates D3D9 to Vulkan, and on your PC that costs a little
(your 2026-10-23 measurement: native 16.0 ms vs DXVK 3.0.1 17.1 ms per frame). The fork starts from that same number.
Speed can come later, per effect, where the old limits forced wasteful work: several passes that could be one, many-tap
blurs that compute does better, extra lamp passes for objects with more than 6 lamps (phase 2: all lamps in one pass).
Those are "maybe, measure each one" gains, not a promise. The effects ported so far are line-for-line copies and should
cost about the same as before.

## 2. Can it stop shaders popping in at a distance?
Two different problems:
- **Compile stalls** (a hitch the first time a new shader is needed, e.g. crossing into a new area; ~62 % of arrival
  hitches were this, ArrivalSampler 2026-10-20): stock DXVK already helps a bit (it compiles on worker threads and keeps a
  cache on disk). The fork adds nothing extra here yet. NVR's own Vulkan passes are each made once, the first time the effect runs, so they cannot cause hitches
  later while you travel. The real fix for game-shader hitches would be building them ahead of time (the shader
  preloader idea, deep/warmup2-cs); the fork could make that easier but does not do it today.
- **Things appearing/fading in at a distance** (objects, grass, shadows, effects that switch on at a range): that is the
  game's LOD and fade distances and NVR's own effect ranges. The fork **does not change these**. They are settings and
  game data, not shader limits.

## 3. Can shaders run better in LOD cells?
Partly, later. Far-away LOD land and objects use the game's simple LOD shaders. With phase 2 (shader replacement) NVR
could swap those for richer ones (e.g. better lighting or shadows on distant land) without the SM3 caps. But what limits
LOD most is the content: low-poly meshes and low-resolution textures, and the game not sending lamps/shadows for them. The
fork cannot fix the content, and LOD shaders are already cheap, so there is little speed to win there.

## Which ones the fork helps with
| | fork helps? |
|---|---|
| Shader limits (constants, textures, temps, instruction cap, compute) | **Yes**, the main point |
| Raw frame rate | Not by itself; possible per-effect savings later, each measured |
| Compile hitches | No more than stock DXVK already does |
| Pop-in from LOD / fade distances | **No** |
| Nicer shading on LOD | A little, via phase 2, looks only |
| Native D3D9 players (you, today) | **No**: everything fork-only is optional and does nothing without the fork's d3d9.dll |
