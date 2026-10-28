# Fork RT: interior lamp shadows traced (2026-10-28)

Item 3 of NVR's hybrid ray-tracing plan (with the RT core + sun shadows, RT AO/sky and RT reflections). Branch
`claude/rt-lamp-shadows-d97675` (off `claude/p84v3-tessring` 03b62a5a). Nothing pushed, nothing installed.

## Status
- **Written and checked offline; not run.** The tracing needs the RT core's 64-bit helper (session rt-sun-core,
  `docs/next/FORK-RT-CORE.md`, not landed yet). `src/core/RtCoreStandIn.h` stands in for it and answers "no ray tracing", so RT lamp
  shadows stay off (log: `UNOFFICIAL RT lamp shadows: off: no ray tracing (the RT core is not in this build)`).
- Next: rebase onto the core's branch, map RtCoreStandIn's four calls onto its API (submit a pass by .spv name with depth +
  normals in and an RGBA32F image out, get the newest result back), build, run the bulk lists below on the iGPU, fill in "Results".

## Design change, 2026-10-28 afternoon (why the first version was parked)
The first version traced inside the object shaders: a fork patch rewrote marked `texCUBEgrad` samples of the converted lamp shaders
into ray queries against a TLAS whose address it baked in (offline-tested: valid SPIR-V; dxbc-spirv's lowering checked). rt-sun-core
then measured that **AMD's 32-bit Windows Vulkan driver exposes no ray tracing at all** (the 64-bit one does), and FNV + the fork's
d3d9.dll are 32-bit: no ray query can run inside the game on Windows. The RT core became a 64-bit helper process sharing images with
the game. The in-shader version only works where a 32-bit driver has RT (Linux RADV under Proton): parked, not deleted, in
`tools/nvr-dxvk/parked/` (`rtlamp-in-shader-ray-queries.patch` = the fork side, `rtlamp-test/` = its SPIR-V test; the NVR side is in
commit 85b45a1a).

## What it does (plain)
Indoors, each lamp's shadow comes from rays traced against the room's real geometry instead of the lamp's shadow cube map. Same
lamps, same per-object lamp shadows (a lamp's shadow takes away only that lamp's light), without the cube map's limits: no texel
stair-steps, no seams on floor pieces, no bias speckles or leaks at contact, soft with distance (LampRadius). The tracing runs in the
helper beside the game and arrives a frame or so late; where it does not match what is on screen the cube map is used.

## How
1. **Trace (in the helper):** `src/hlsl/NewVegas/Compute/Rt/RtLampTrace.comp` (GLSL, GL_EXT_ray_query; compiles and passes
   spirv-val for Vulkan 1.2). Per pixel of a frame's depth (TESR_DepthBuffer, normals TESR_NormalsBuffer), for up to 18 shadowed
   lamps (cube slots 0-17): Rays rays over a disc of LampRadius facing the point, each in two segments (the whole ray without lamp
   glass; the last LampModelDistance without lamp models either, so a lamp never shadows its own light while a chandelier still
   shadows the room's other lamps), stopping LampStop short of the lamp, starting StartOffset off the surface. Result RGBA32F:
   4-bit visibility, 6 lamps per float channel (exact integers below 2^24), view depth in alpha.
2. **Request (NVR, once a frame indoors):** `RtLampShadows::Submit` (from PointShadowForward::Publish once the lamp slots are known)
   fills the pass constants (std140, `TracePass`: view/projection as TESR_, camera data, size, ray settings, masks, the 18 lamps
   camera-relative with reach and radius), remembers the frame's camera, and hands both to the core.
3. **Use (object shaders, `POINT_SHADOW_RT`):** the newest result is bound on s15, point sampled. Each lamp of a draw that has a
   traced slot reprojects its point into the traced frame (`NVR_RtLampReproject`, c206-c209: camera-relative now -> that frame's
   clip, TAA's own reprojection as a matrix), and takes the traced value if the stored view depth matches within DepthTolerance;
   otherwise (hidden there, off screen, slot past MaxLamps/18) its cube map as before.
4. **Registers / samplers:** c206-c209 (the character shadow map's, outdoors only), c215-c217; s15 = the sixth lamp's cube stage,
   because every other sampler is taken in some Interior variant (s9 the sun atlas, s10 the live cube). So in RT builds the sixth
   light of a draw (the rare six-lamp OPT passes) has no cube fallback (traced or lit) and DebugView 3 (cube trace) shows grey.

Checked offline:
- ObjectTemplate pixel shaders as the game compiles Interior records (FORWARD_SHADOWS 0): 4 variants x 5 option sets (plain;
  MAG+EVEN+SMOOTH; TEXEL; FIT 3; LIVE_CUBE): **RT off = byte-identical to HEAD**; RT on compiles everywhere, 1146-2178 slots
  (plain ps_3_0: under the native driver's ~3600 limit too). SM3002/3003/3005/3007 compile with RT.
- The packing (pack as the .comp, unpack as the HLSL, float32, 2000 random sets) and the reprojection matrix (against TAA's formula,
  200 random cameras/moves) agree exactly (numpy).
- NVR DLL: VS2019 Release Win32 builds clean (`H:\_NVR-rtlamp\nvr-out`).

## Settings `[Shaders.ShadowsInteriors.RT]` (defaults toml; off by default)
| key | default | meaning |
|---|---|---|
| Enabled | false | restart; needs `[Shaders.ShadowsInteriors.Forward] Enabled`, the RT helper, a GPU with ray tracing |
| Live | true | live A/B: false = the cube maps again (same shaders) |
| MaxLamps | 8 | 0-18 shadowed lamps traced (by cube slot, best-ranked first); the rest keep their cube map |
| Rays | 1 | rays per lamp per pixel (1-8) |
| LampRadius | 4 | world units: the disc the rays aim over; 0 = hard, no noise |
| InvisibleLightRadius | 24 | the same for the game's invisible lights (-1 = LampRadius) |
| Jitter | true | rotate the ray pattern each frame (for TAA) |
| StartOffset | 0.5 | world units off the surface: no self-shadow |
| LampStop | 12 | a ray stops this short of its lamp (bulb, glass) |
| SkipLampModels | true | lamp models don't block the last LampModelDistance of their own light's rays |
| SkipLampGlass | true | lamp glass / bulbs / glow meshes never block |
| Actors | true | people and creatures block (if the scene holds them) |
| DepthTolerance | 0.02 | share of the distance within which the traced frame's surface counts as this point's |
| DebugView | 0 | 1 = compare: white both lit, black both blocked, RED only traced blocked, GREEN only cube blocked |

## What the RT core must provide (told to rt-sun-core)
1. Available() before NVR loads its shaders; a pass run by .spv name with TESR_DepthBuffer + TESR_NormalsBuffer shared in, an RGBA32F
   output shared back as a D3D9 texture, the 1.3 KB constants block, and the frame number the result belongs to.
2. The scene for that frame's camera (camera-relative), interiors with statics + actors, each instance's Mask from
   `RtLampShadows::InstanceMask` (0x01 rest, 0x02 lamp glass, 0x04 lamp models, 0x08 actors). Without masks (0xFF) lamp glass and
   models block; LampStop alone keeps the bulb itself out.

## Bench (not run yet)
Lists `work\bench\foundations\lists\rtlamp-1-{lucky22,saloon16,save26,save27}.txt` (2026-10-03 work root; save26/save27 = the GS bar
lantern / Vault 21 saves, whichever is Vault 21): 24 shots each, `frames 300` + `profile`: cube vs traced pairs, compare views hard and
soft, soft 4 rays / 1 ray, the self-shadow check (SkipLampGlass/SkipLampModels off), actors off, DebugView 2 tints with LogLamps, and
the cost sweep MaxLamps 0/1/2/4/8/16/64 (64 = 18) and Rays 2/4/8. Run, one launch per scene: `LOCKTAG=rtlamp BENCHQ_SOLO=1
SNAPSHOT=snap-dxvkfork-merge-20261026 FORK_D3D9=<the core's fork x32> RUN_TOML=<copy of run-toml-flashlight-20261028 with
[Shaders.ShadowsInteriors.RT] Enabled = true, FSR off> SLOT_RES=1280x720 SLOT_D3DDEVICE="AMD Radeon(TM) Graphics"
SLOT_DXVK_DEVICE="AMD Radeon(TM) Graphics" sh bench/deep-roadmap/multirun/run_slot_when_free.sh <scene> rtlamp-1-<scene> rtlamp-1 <tree>`.
Pass criteria: cmp-hard mostly white/black with red/green only along soft edges, no green areas (light the traced result lets
through where the cube map blocks: a leak or missing geometry), no dark cones above sconces / fan-blade ovals in rt1 (they appear
in noskip), no ceiling garbage; cost = the helper's trace time per MaxLamps step (relative only: the iGPU is ~1/10 of a 1070).

## Results
(none yet)

## Known limits / next
- A frame or more of lag: a moving lamp or person casts its traced shadow a frame late; a surface just uncovered uses its cube map
  for that frame. Fast camera turns: the newly visible screen edge uses cube maps until the next result.
- Soft shadows are per-pixel noise at Rays 1-2: TAA averages it (Jitter); without TAA use Rays 4+, or LampRadius 0 (noise-free).
  A spatial denoise in the helper (depth/normal-aware blur per lamp before packing) is the next step if it shows.
- Alpha-tested casters are opaque in the core's v1.
- The cube maps are still drawn while RT is on (fallback, A/B); skipping redraws for lamps whose pixels are all traced is the saving
  to take once the pictures pass.
