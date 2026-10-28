# Fork RT: interior lamp shadows traced (2026-10-28)

Item 3 of NVR's hybrid ray-tracing plan (with the RT core + sun shadows, RT AO/sky and RT reflections). Branch
`claude/rt-lamp-shadows-d97675` (off `claude/p84v3-tessring` 03b62a5a). Fork patch `tools/nvr-dxvk/patches/0029-*` (two commits,
built on the fork series through 0027; worktree `G:\_NVR-build-scratch\dxvk-rtlamp`, branch `nvr-rtlamp`). Nothing pushed, nothing
installed.

## Status
- **Written and checked offline; not run.** The RT core (session rt-sun-core: ID3D9NvrExtRt "1.8-rt", fork patch 0028,
  `docs/next/FORK-RT-CORE.md`) has not landed, so there is no scene to trace yet: `src/core/RtScene.h` is a stand-in that answers
  "no scene", and RT lamp shadows stay off with the log line `UNOFFICIAL RT lamp shadows: off: no ray tracing scene (...)`.
- Next: rebase onto the core's branch, replace `RtScene.h` with its scene, build the fork (0028 + 0029), run the bulk lists below on
  the iGPU, fill in "Results".

## What it does (plain)
Indoors, each lamp's shadow is traced against the room's real geometry instead of looked up in the lamp's shadow cube map. Same
lamps, same per-object lamp shadows (a lamp's shadow takes away only that lamp's light), but none of the cube map's limits: no
texel stair-steps, no seams on floor pieces, no blur from the map's resolution, no bias speckles or light leaks at contact,
shadows sharp where things touch and softer with distance (LampRadius). The cube maps stay as the fallback and the live A/B.

## Why this shape (decided, with reasons)
- **In the object shaders, not a post pass.** NVR's interior lamp shadows are forward (PointShadowForward.hlsl inside the lit
  object shaders, per lamp, per draw). A screen-space RT mask after the scene could only darken the finished picture (the old
  look, with its known faults), or would come a frame late for the forward shaders. Tracing in the shader keeps the forward
  model exactly and needs no history.
- **A marker the fork rewrites, not a new compiler.** D3D9 shaders (ps_3_0) cannot express ray queries, and DXVK 3.0 converts
  them through dxbc-spirv's IR, which has no ray-query ops. So NVR writes each ray as `texCUBEgrad(cube, origin, ray, (tMin,
  tMax, mask))` (texldd on a cube sampler: nothing in the game or NVR samples a cube with gradients), and the fork rewrites the
  lowered SPIR-V of those samples into `rayQueryInitialize / Proceed / GetIntersectionType` (`src/dxvk/dxvk_nvr_rtpatch.cpp`,
  ~300 lines, a plain word-level pass, no DXVK dependencies). The scene's address is baked in as a constant
  (`OpConvertUToAccelerationStructureKHR` on a uvec2): no new descriptor, no pipeline-layout change, no per-draw binding.
- **One D3D9 shader, both paths.** The cube lookups stay in the RT shaders behind a branch on c215.x: `Live` false gives the cube
  maps in the same launch (bench A/B without a restart), and the compare view runs both.
- **Off is byte-identical.** `POINT_SHADOW_RT` is defined only when the setting is on at startup AND the fork has the hook AND the
  RT core has a scene. The ObjectTemplate pixel shaders without it compile to exactly HEAD's assembly (checked: 4 variants x
  5 option sets: plain, MAG+EVEN+SMOOTH, TEXEL, FIT 1, FIT 3 -- all identical). On the GTX 1070 (no ray queries) the core reports
  no RT, the hook is never enabled, nothing changes.

## Settings `[Shaders.ShadowsInteriors.RT]` (defaults toml; off by default)
| key | default | meaning |
|---|---|---|
| Enabled | false | restart; needs `[Shaders.ShadowsInteriors.Forward] Enabled`, NVR DXVK with 0028 + 0029, a GPU with ray queries |
| Live | true | live A/B: false = the cube maps again (same shaders) |
| MaxLamps | 8 | how many shadowed lamps (by cube slot, the best-ranked first) are traced; the rest keep their cube map |
| Rays | 1 | rays per lamp per pixel (1-8) |
| LampRadius | 4 | world units: the disc the rays aim over (soft shadows); 0 = hard, no noise |
| InvisibleLightRadius | 24 | the same for the game's invisible lights (-1 = LampRadius) |
| Jitter | true | rotate the ray pattern each frame (for TAA) |
| StartOffset | 0.5 | world units off the surface (along the camera-facing normal) and the ray's tMin: no self-shadow |
| LampStop | 12 | a ray stops this short of its lamp: the bulb and glass round the light point never block it |
| SkipLampModels | true | lamp models (LampFixtures' list) don't block the last LampModelDistance of their own light's rays |
| SkipLampGlass | true | lamp glass / bulbs / glow meshes never block |
| Actors | true | people and creatures block (if the core's scene holds them) |
| DebugView | 0 | 1 = compare: white both lit, black both blocked, RED only the rays blocked, GREEN only the cube map blocked |

## How (files)
- `src/hlsl/NewVegas/Shaders/Includes/PointShadowForward.hlsl`: `POINT_SHADOW_RT`; `PointShadowRay` (per lamp: N rays over a
  disc of LampRadius facing the point, golden-angle spiral turned by a position hash + the frame; each ray in two segments: the
  whole ray with every instance but glass (mask 0x01|0x04|0x08), the last LampModelDistance without lamp models (so a chandelier
  shadows the room's other lamps but not its own light)); `PointShadowDone` / `PointShadowFactor` for the compare view; the per
  lamp choice (c215.z bit k = shader light k). Registers c215-c217 (free in every pixel shader that includes it; ps_3_0 ends at c223).
- `src/core/RtLampShadows.h`: startup decision (`Initialize`, from RenderManager right after DxvkCompute::Initialize), per-frame
  constants (`Update`, from PointShadowForward::Publish), per-draw upload (`SetForPass`), the instance mask classifier for the core
  (`InstanceMask`: LampFixtures::LampPart = glass 0x02, a listed lamp model = 0x04, actors 0x08, else 0x01).
- `src/core/RtScene.h`: STAND-IN for the core's scene (Address / BuiltThisFrame / Status).
- `src/core/NvrDxvkExt.h`: `ID3D9NvrExtRtLamp` (GUID ...d340): `GetRayQueryInfo`, `EnableCubeGradRayQueries(scene address)`,
  `GetRayQueryStats`.
- Fork (0029): `src/dxvk/dxvk_nvr_rtpatch.{h,cpp}` (the SPIR-V pass + the hook's global state), one call in
  `DxvkIrShader::getCode` (fragment shaders only), `src/d3d9/d3d9_nvr_rtlamp.{h,cpp}` (the interface; refuses without the
  device's rayQuery + accelerationStructure features), device member + QueryInterface, meson.

## What the core must provide (asked of rt-sun-core)
1. One scene for the session, created at startup (address valid before NVR loads its shaders, never moved), built every frame
   before the world's draws, interiors included (statics + actors), camera-relative space.
2. Each instance's Mask from `RtLampShadows::InstanceMask` (without it every instance has mask 0xFF: lamp glass and models then
   do block; LampStop alone keeps the bulb itself out).
3. BuildScene's barriers must cover FRAGMENT-shader reads by later D3D9 draws, and the next rebuild must wait for them: DXVK does
   not know the converted lamp shaders read the scene.

## Verified (offline)
- The SPIR-V pass: a glslang fragment shader shaped like DXVK's output (separate images, sampler heap, two marked cube-grad
  samples, one ordinary cube textureLod, one inside a branch) -> 2 samples replaced, the ordinary one untouched, `spirv-val
  --target-env vulkan1.2` valid (`tools/nvr-dxvk/test/rtlamp/`: rtpatch_test.cpp + marker.frag; MSVC build line in the file's
  header). Not yet run on real DXVK-converted SPIR-V: check with `DXVK_SHADER_DUMP_PATH` on the first run.
- How dxbc-spirv (6da3ed38, the fork's) lowers texldd, read from its source (`sm3/sm3_resources.cpp` emitSample /
  emitSampleImageFunction): every (sampler, kind) pair gets its own function, so the markers live in `sampleTexture_<s>_..._grad`
  and never share code with the cube path's texldl (`..._explicit`); inside it the Grad sample follows a null-texture check
  (unbound stage: returns (0,0,0,1) and the patched ray never runs = "blocked") and a depth-compare branch (Dref ops, not matched).
  NVR binds a cube (the lamp's or the neutral one) on every lamp stage of the Interior records each draw, so the check passes.
  The coordinate/derivatives arrive as vec4 params and are cut to the cube's vec3 before the sample; the pass shuffles to vec3
  either way.
- HLSL: every ObjectTemplate variant tried compiles with RT (1318-2387 slots; the 6-lamp shaders grow ~400-600 slots, fine on
  DXVK which has no driver slot limit: RT shaders are only ever made on the fork); SM3002/3003/3005/3007 compile with RT (8-9 texldd
  each, the cube ones 2 per lamp). RT off: identical to HEAD (above).
- NVR DLL: VS2019 Release Win32 builds clean (`H:\_NVR-rtlamp\nvr-out`, SHA256 7AF031C9..., with the stand-in scene).

## Bench (not run yet)
Lists `work\bench\foundations\lists\rtlamp-1-{lucky22,saloon16,save26,save27}.txt` (2026-10-03 work root; save26/save27 = the GS bar
lantern / Vault 21 saves, whichever is Vault 21): 24 shots each, `frames 300` + `profile`: cube vs rays pairs, compare views hard
and soft, soft 4 rays / 1 ray, the self-shadow check (SkipLampGlass/SkipLampModels off), actors off, DebugView 2 tints with
LogLamps, and the cost sweep MaxLamps 0/1/2/4/8/16/64 (1 ray, hard) and Rays 2/4/8 (radius 4). Run (one launch per scene):
`LOCKTAG=rtlamp SNAPSHOT=snap-dxvkfork-merge-20261026 FORK_D3D9=<fork 0028+0029 x32> RUN_TOML=<copy of run-toml-flashlight-20261028
with [Shaders.ShadowsInteriors.RT] Enabled = true, FSR off, 1280x720> SLOT_D3DDEVICE="AMD Radeon(TM) Graphics"
SLOT_DXVK_DEVICE="AMD Radeon(TM) Graphics" sh bench/deep-roadmap/multirun/run_slot_when_free.sh <scene> rtlamp-1-<scene> rtlamp-1 <tree>`.
Pass criteria: cmp-hard mostly white/black with red/green only along edges (cube-map softness) and no green areas (= rays let light
through where the cube map blocks: a leak or missing geometry); noskip vs rt1 shows the self-shadowing that the skips remove (no
dark cones above sconces, no fan-blade ovals in rt1); no ceiling garbage round ceiling lamps; cost from the GPU PROFILE lines per
MaxLamps step (relative only: the iGPU is ~1/10 of a 1070).

## Results
(none yet)

## Known limits / next
- Soft shadows are per-pixel noise at Rays 1-2; with TAA it averages, without TAA it shows. No dedicated denoiser yet (a forward
  shader has no neighbours or history to denoise with): if the noise matters, the next step is a per-lamp screen-space
  visibility buffer traced after the depth prepass the fork does not have -- or simply Rays 4 with TAA. Hard shadows (LampRadius
  0) are noise-free.
- Alpha-tested casters (lamp shades with cut-outs, chain-link, foliage) are opaque in the core's v1: they shadow fully.
- The cube maps are still drawn while RT is on (fallback, A/B). Skipping the cube redraw for traced lamps is the real CPU/GPU
  saving and is the next change once the pictures pass.
- The compare view needs both paths; it is a test view only.
