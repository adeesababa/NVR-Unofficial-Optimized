# NVR DXVK fork: plan (item #1 for the next build session; user decision 2026-10-23)

Goal (the user): leave D3D9's shader limits behind for NVR's own effects, by shipping an NVR-owned DXVK build (as RTX
Remix forks DXVK) with a small "NVR extension" through which NVR records Vulkan work into DXVK's OWN command stream.

Background: evidence\dxvkcompute\REVIEW.md. DXVK's public interop cannot order an outside vkQueueSubmit after DXVK's own
work without a CPU wait (Flush is async, LockSubmissionQueue does not wait, no semaphore export). Inside a fork that
problem disappears: our commands run on DXVK's CS thread, in its context, with its resource tracking and barriers.
Licence: DXVK is zlib (fork and ship OK; credit it). NVR stays GPL.

## Where it stands (2026-10-25)
Branch `nvr-dxvk-compute` (NVR-Unofficial-Optimized, `mine/`); the fork itself is the patch series in `tools/nvr-dxvk/`
(DXVK v3.0.1 + NVR extension 1.2), built by the "NVR DXVK d3d9.dll" workflow. Details: NVR-DXVK-EXTENSION.md.
- Done: steps 2-4 (stock build, extension, AvgLuma compute) and the "later" full-screen passes (1.1: eleven effects
  ported line for line), phase 2's mechanism (1.2: pixel shader replacement). All verified on software Vulkan only.
- CI fixed 2026-10-25: "Build New Vegas Reloaded" moved from the retired `windows-2019` runner to `windows-2022` (v143
  toolset in CI only; the owner's VS2019 projects are unchanged). First green run 2026-10-25 (run 40190791536, 3 min, no errors, no warnings in the new files): the
  NVR-side C++ (DxvkCompute, AvgLuma compute, the eleven ported effects, replacements) compiles under MSVC for the first
  time. Packaging fix: build.bat and check-package.ps1 now ship `Shaders\NewVegasReloaded\Compute\` (the .spv files;
  before, a built package had none, so every fork path would have failed to load its shader and fallen back to D3D9).
- Not done: nothing has run in the game yet (Wine could not run DXVK 3.0). That in-game smoke test is the gate for
  everything else.
- What the fork buys and does not (plain answers): NVR-DXVK-WHAT-IT-BUYS.md.

## Next steps, in order (session-hours)
1. In-game smoke test in a test slot copy, never the user's game (1-2 h): the slot gets the fork's d3d9.dll + dxvk.conf
   for the run only and loses them on reset (the restore point must never hold a d3d9.dll; make_slot.py refuses one).
   Checks 1-3 and 6 of NVR-DXVK-EXTENSION.md "what the owner must test": log lines, stock-like behaviour with the options
   off, AvgLumaCompute and NvrDxvkPasses toggled live with A/B shots (same picture). Tools built 2026-10-25: run_slot.sh
   `FORK_D3D9=<folder>` (copies d3d9.dll [+ dxvk.conf] into the slot after 'native'; the reset moves it out again),
   game_swap.py `dev` now also copies Compute, shots list bench\foundations\lists\dxvkfork-1-lucky22.txt, tree
   work\NVR-dxvkfork-ci (holds the CI-built DLL). Run: `LOCKTAG=<own> FORK_D3D9=<folder> sh run_slot_when_free.sh lucky22
   dxvkfork-1-lucky22 dxvkfork1 NVR-dxvkfork-ci`.
   FIRST RESULT 2026-10-25 (slot A, CI DLL b85cd6e7, fork d3d9.dll 76d69219): the fork loads in the real game and NVR finds
   it: `UNOFFICIAL NVR DXVK: NVR DXVK extension 1.2 on DXVK v3.0.1-3-gb794414d (push 240 bytes, subgroup 32): compute
   paths available`; DXVK's log is clean (no `NVR ext:` lines). But the game never reached its main loop (presents ~20 fps
   for 600 s, "game loops 0", twice), so no pictures. CONTROL: stock DXVK 3.0.1 (the user's d3d9.dll.dxvk301) in the same
   slot hangs the same way, while stock DXVK passed in the real game folder on 2026-10-21. So this is DXVK in a slim slot
   copy (or with this CI-built v143 NVR DLL), NOT the fork.
   CAUSE FOUND (hangstack on the stuck slot game, same with the combo-1023 DLL): the main thread sat in a MessageBox from
   the user's xNVSE plugin DepthResolve.dll: "Incompatible DXVK version" (its log: DXVK status 1, RESZ status 0). It
   pops on every DXVK 3.0.1 launch, stock or fork (the user saw it). NVR does not use DepthResolve (NVR resolves depth
   itself); it feeds DoF-Fix.dll. run_slot.sh now renames the slot's DepthResolve.dll aside for FORK_D3D9 runs only
   and back on reset (the hard link is kept, so make_slot verify passes).
   PASS 2026-10-25 14:53 (run 20261025-145232-slotA, shots H:\_NVR-autotest-shots\20261025-145232-slotA, diffs
   G:\_NVR-build-scratch\dxvkfork2-diffs): the game runs on the fork, loads the save, all 8 shots, no `failed` lines.
   - `AvgLuma: compute path running`; `Exposure / FXAA / Sharpening runs as a Vulkan pass` (ImageAdjust, LowHF and
     SkyDither printed no line: probably inactive in that shot; recheck).
   - Vulkan passes vs their D3D9 techniques: 10.4 % of pixels > 2/255, mean 1.2/255, signed +0.02 = the same as two
     identical shots (11.7 %, 1.36, +0.02): same picture within scene noise (a few brighter edge pixels around one neon
     sign to recheck at full resolution).
   - AvgLumaCompute on: picture 0.7/255 darker on average. AvgLuma check: exact average luma 0.0230 vs the D3D9 pass's
     112 taps 0.0163 (+0.0068, the old pass reads about 30 % low in this casino scene), so exposure adapts a bit darker.
     A real (and expected) difference, not a bug; whether Exposure's defaults should be retuned for it is the owner's call.
   TIMING 2026-10-25 16:44 (run 20261025-164255-slotC, list dxvkfork-2-timing-lucky22: slot C 1920x1080 alone, Lucky 38,
   D3D9 / Vulkan alternated 3 times in one launch, 600 frames per shot, F10 GPU timers; a slim slot, so ms are relative,
   not the user's 1440p numbers; no full slot exists and rebuilding one needs the real install at a restore point):
   | timer (ms, mean of 3) | D3D9 | fork | diff |
   |---|---|---|---|
   | Exposure | 0.1045 | 0.1049 | +0.0005 |
   | FXAA | 0.0834 | 0.0814 | -0.0020 |
   | Sharpening | 0.1213 | 0.1188 | -0.0026 |
   | Average luma (112 taps vs exact compute) | 0.0440 | 0.1473 | +0.1033 |
   | Game frame total | 7.17 | 7.07 | -0.10 (scene noise, +-0.5 between rounds) |
   So the route itself costs nothing: a pass through the extension costs what its D3D9 technique costs. The exact
   AvgLuma is 0.1 ms dearer (it reads all 2.07 M pixels; the old pass reads 112): a correctness gain, not a speed gain;
   a two-level reduction or reading a mip would cut it if it ships. ImageAdjust, LowHF and SkyDither had no timer line in
   either mode (they did not run in this scene: idle at their defaults), so they are still unmeasured in game.
   PHASE 2 STEP 1 ANSWERED 2026-10-25 17:21 (slot A, Lucky 38, defaults + HandBlood on, list dxvkfork-0-lucky22, runs
   20261025-172054-slotA native / 20261025-172251-slotA fork): native D3D9 refused Interiors\SLS2035.pso (hr 0x8876086C,
   3727 instruction slots) and NVR stepped it down to its reduced-quality Fit version; on DXVK the same shader loaded at full
   quality, 0 refusals. So the ~3,600-instruction wall is the native driver's and is already gone under DXVK (stock or fork).
   What DXVK does NOT lift: fxc's ps_3_0 limits at compile time (32 temps, 224 constants, 16 samplers): those need phase 2's
   SPIR-V replacements.
   Not automatable here (the test plugin blocks all input): alt-tab, fullscreen toggle (device Reset; covered only by the
   harness test), ReShade chained, loading-screen transitions. Owner's 5-minute check when they first try the fork.
   PHASE 2 STEP 3 PASSES 2026-10-25 18:30: the game's own 6-lamp shiny-object shader runs as our SPIR-V, same picture.
   - Tool: tools/nvr-dxvk/replace/psasm2glsl.py translates a ps_3_0 shader (fxc /dumpbin disassembly) line for line into
     GLSL under the fork's contract (D3D9 textures + heap samplers, listed float registers, DXVK's alpha test, null-sampler
     rule, bool constants from spec data, DXVK's float clamps); compile.sh = glslangValidator + spirv-val (WSL; glslang and
     spirv-tools unpacked from Ubuntu's packages into ~/vktools, no root).
   - NVR (c1ae0245): a replacement is found as `Compute\Replace\<Name>@<FNV-1a of the bytecode>.spv` first (the interior and
     exterior records share a name, not bytecode); a `Replace\dump` folder collects every pixel shader's exact bytecode.
   - Found by colour canaries (138 SLS shaders as solid colours, then halving): the Lucky 38 carpet, walls and ceiling are
     SLS2034/SLS2036 @8FF9A125 (same bytecode), 3,162 instruction slots, 8 samplers (3 2D + 5 lamp-shadow cubes), 55 float
     registers. Translated: 2,778 GLSL lines, 532 KB of SPIR-V, validated.
   - Run 20261025-182602 (replacement) vs 20261025-182939 (none), same slot/settings: 8.5 % of pixels > 2/255, mean
     1.02/255, signed +0.02, i.e. below two identical shots of one launch (11.7 %, 1.36); diff image = flicker only (neon,
     securitron), nothing on the replaced surfaces. DXVK log: registered + replaced, no errors.
   - Gotcha found on the way (crash in dxvk-shader thread): DXVK rewrites the spec-data block (set 3) into spec constants
     member by member; it must be a struct of plain uint members, never an array. psasm2glsl declares it that way.
   - The replacement is tied to the bytecode, i.e. to the settings that compiled it (defaults here); a shipping version needs
     one per variant the settings produce, or (better) hand-written GLSL keyed by shader name that reads its options from
     constants. That is step 4: the first fork-only gain (all lamps in one pass, or the character shadow map) in this shader.
   STEP 4 (2026-10-25 19:05): "all lamps in one pass" made cheaper; lossless, but still not a win. Details:
   - Why one pass was slow (LampsInOnePass, +1.08 ms Ultra-Luxe on 10-18): the 6-lamp shaders (SLS2034/2035/2036) computed
     all 6 lamp slots and their 6 shadow-cube lookups on every pixel, unused slots multiplied by 0; LampReachSkip's branches
     were left out of exactly these shaders because the native driver refuses them past ~3,600 slots.
   - Built (089ff9d9): [Shaders.PBR.Main] DxvkLampBranches (default on, restart; only when the runtime is DXVK, stock or
     fork; NO_SLOT_LIMIT define, ObjectTemplate.hlsl LAMP_IFK + ManyLightsDirect.hlsl): per pixel, a lamp slot the draw does
     not use (k >= lightsUsed) or a lamp out of reach is skipped, its shadow lookup too. fxc: compiles in all four checked
     variants (OPT 3,581 -> 3,821 slots, +HandBlood 4,238; no 32-temp failure). Native D3D9: unchanged (define 0).
   - Same picture: branches off vs on 8.4 % / 1.11 / -0.006 (one pass) and 8.2 % / 1.11 / -0.001 (multi-pass), below
     same-launch noise (11.5 % / 1.37).
   - Timing (slot C 1080p, list dxvkfork-3-onepass, LampsInOnePass off/on x3 per launch, world scene GPU ms):
     | | branches off: multi / one | branches on: multi / one |
     |---|---|---|
     | Ultra-Luxe | 4.44 / 5.80 (+1.36) | 4.28 / 5.45 (+1.16) |
     | Lucky 38 | 4.08 / 4.06 (-0.02) | 4.10 / 4.00 (-0.10) |
     Split (dxvkfork-4, Ultra-Luxe, branches on): one pass +0.49 with lamp shadows, +0.55 with them off (Strength 0): the
     remaining cost is NOT the shadow lookups. Most likely the multi-pass path is already cheap per pixel (LampScissor draws
     each extra lamp pass only inside that lamp's screen rectangle, TrimLampPasses drops out-of-reach pixels), while one pass
     shades the whole object with every lamp's branch. Run-to-run spread +-0.4 ms per round: the gain from the branches is
     about 0.1-0.35 ms on the one-pass path, not more.
   - Verdict: keep DxvkLampBranches on (free, lossless, DXVK only); LampsInOnePass stays off. The fork's lamp gain is not
     "one pass" but would need the one pass to do what LampScissor does (light lists per screen tile from an NVR buffer in
     set 4, a clustered/tiled light loop): a real SPIR-V-only design, est. 8-12 session-hours, with no guarantee it beats
     the scissored passes at 6 lamps. Better next fork targets: compute versions of the full-screen effects that cost the
     most (AmbientLight 0.4 ms, point shadow cube maps 0.5 ms, volumetric fog 0.26 ms at 1080p), where compute's shared
     memory and atomics do help.
   PARALLEL PORT ROUND 2026-10-25 20:15 (15 agents at the user's request: 3 Opus medium, 12 Haiku high; each in its own
   worktree, compile-checked locally, merged here; outcomes G:\_NVR-build-scratch\port-results.txt):
   - Compute (Opus): AmbientLightBlurCompute and AmbientLightSmoothCompute built, CPU models exact, IN GAME same picture
     (12.6 % / 1.45 and 10.2 % / 1.21 vs noise 11.9 % / 1.40) but SLOWER on the GTX 1070 at 1080p (half-res buffers):
     blur 0.20 ms vs D3D9 X+Y 0.09; prepare+smooth 0.32 vs 0.11 (smooth writes a buffer and copies it back). Both off,
     marked in the defaults. Bloom: judged not worth porting (chain already ~82 % of memory bandwidth; exact compute
     saves ~0.02 ms). Lesson: at these sizes the D3D9 passes are already bandwidth-cheap; compute only pays where it
     removes whole passes or full-resolution traffic, not by tiling small separable filters.
   - Vulkan passes (Haiku), all behind NvrDxvkPasses: IN GAME same picture (Lucky 38 pairs at noise level): Coloring
     (10.7 % / 1.27), DitherBuster (12.6 % / 1.47), Cinema (12.9 % / 1.49), BloomLegacy (11.2 % / 1.32). Merged, not yet
     run in game: BarrelHaze (needs GunFX firing), DepthOfField distant blur, SkinScattering (needs NewSkin at startup;
     first uniform-buffer port), Snow, Precipitations (need snow/rain). Not ported: Underwater (constants > 240 push bytes,
     caustics need mipmapped/anisotropic samplers the extension does not offer).
   - CRASH (fork bug, open): the Bodycam and HeatHaze ports crashed DXVK's CS thread in D3D9NvrExt::Draw's command
     (dereference of a pointer valued 1 after the draw, fork d3d9.dll +0x6FCA5, in the state put-back / unbind code). Both
     reverted (3a9830dd, the HeatHaze revert). Untested ports that build their own bindings (BarrelHaze, DepthOfField,
     Snow, Precipitations, SkinScattering) may hit it too: test each before trusting it. Next: a symbol build of the fork
     (keep DWARF, addr2line) and a harness repro of the two passes' exact bindings.
   - CRASH FIXED 2026-10-25 20:55 (fork patch 0004, d447c933): symbolised with the new symbols build (workflow artifact
     nvr-dxvk-x32-symbols; NVR_DXVK_DEBUG=1 in build.sh): nvrSetState -> DxvkContextState move -> DxvkXfbState ->
     Rc<DxvkBuffer>::decRef on garbage. Cause: DXVK 3.0 gives each graphics stage a fixed push-data slot in its
     "independent" (graphics pipeline library, fast-linked) layout, the fragment stage 48 bytes at offset 192; the
     extension's passes declare fragment push blocks of up to 240 bytes, so in a fast-linked pipeline their constants and
     sampler heap indices (offsets 96, 100, 192, 208...) were written past DxvkPushDataState into DxvkXfbState. Every
     pass with more than 48 push bytes did this while its full pipeline was still compiling (Cinema, Coloring, Snow,
     DepthOfField, Precipitations too; HeatHaze and Bodycam happened to hit live pointers). Fix: such a shader never gets
     a pipeline library (DxvkShaderPipelineLibrary::canCreatePipelineLibraryForShader); D3D9's own shaders fit their
     slots and are unaffected. HeatHaze and Bodycam ports restored (28a8e638, 58e59f2b). In game (fork 0004, slot C):
     no crash; HeatHaze, Snow, DepthOfField distant blur (desert view defaults) and Bodycam ran as Vulkan passes;
     Bodycam = D3D9 within its own film-grain noise (72.9 % / 8.95 both); HeatHaze and Snow at noise but the effects
     hardly change that scene even in D3D9, so their "same picture" still needs a scene where they show.
   - Fixed on the way: MotionBlur's Vulkan port fed its second pass a stale surface (fell off the frame chain) and its
     first pass could write straight into the game target (aa5c8398; found by the SkinScattering agent).
   - Extension gaps found: samplers without mips/anisotropy; no uniform-buffer path in ExtTexturePass (SkinScattering does
     its own); passes over 240 push bytes.
   Note: the version string is `git describe` (v3.0.1-3-g...), not `v3.0.1-nvr1` as the
   extension doc says; and the fork shares stock DXVK's state cache (%LOCALAPPDATA%\dxvk) unless dxvk.conf moves it.
2. Phase 2 step 1 (1 h, stock DXVK, slot): does the ~3,600-instruction refusal (HandBlood indoors, 6 lamps) go away?
3. Phase 2 first real replacement (4-8 h): one SLS20xx variant ported line for line, same picture in A/B shots.
4. Only then: the first fork-only gain (all lamps in one pass, or the character shadow map lookup) and a timing A/B in
   a full slot (slim slots are for pictures, never timing).

## Scope
- In: NVR's post effects and new GPU work (compute + modern full-screen passes): no 16-sampler, 224-constant, 16-bool,
  32-temp or instruction-slot limits; shared memory, structured buffers, atomics.
- Out (for now): the game's object/terrain shaders (SLS/PAR/terrain). They stay D3D9 SM3 through DXVK's normal path.
- Native D3D9 keeps working: every fork-only feature is optional, off without the fork, with the current D3D9 path kept.

## Steps
1. Speed baseline (optional, 1-2 h): native vs DXVK 3.0.1, two runs each, the user's settings, a heavy scene (Strip).
   Runners need d3d9.dll renamed to d3d9.dll.dxvk first (run_dxvk_once.sh copies it in).
2. Build stock DXVK 3.0.1 from source, 32-bit, and check it matches the release in game (2-4 h).
   Downloads to OK with the user first: DXVK source (git, tag v3.0.1 + submodules), Meson + Ninja (pip), glslang
   (shader compiler DXVK's build uses), and a toolchain: MSVC works with Meson for DXVK, or MinGW-w64 (the official
   builds use gcc). Disk: C: is nearly full, so build on G:.
3. The NVR extension (6-10 h): a new COM interface on the D3D9 device (e.g. ID3D9VkNvrExt, own GUID), exposing:
   - create a compute pipeline from SPIR-V; bind D3D9 textures/surfaces as sampled or storage images and our own
     buffers; push constants; Dispatch. Implemented with EmitCs + DxvkContext (DXVK's D3D11 side already does compute:
     reuse that path), so DXVK does layouts and barriers.
   - later: full-screen graphics passes with our own SPIR-V (frees post effects from SM3 entirely).
   Keep the patch small and in its own files, so rebasing onto new DXVK releases stays cheap.
4. First use + measurement (3-5 h): the screen-brightness average (AvgLuma) as an exact compute reduction; compare with
   the D3D9 pass and time it (F10). Reuse the good parts of DeepSeek's draft (GLSL reduction, dynamic loading, fail-once).
5. Go/no-go, then candidates: AmbientLight blurs/accumulate, light grid, froxel fog, cloud march, BloodFlow fluid (the
   0.2-0.6 ms prototype in evidence\fluidsim), reflections.

## Phase 2 (after the go/no-go; user 2026-10-23: "the next step forward"): the game's lit-object shaders
Why: the hard walls left in D3D9 are in the game's own draw shaders, not in post effects: the 6-lamp object shaders sit at
ps_3_0's 32 temps (SLS2035 with _rmaos + hand blood + 6 lamps) and past the native driver's ~3,600-instruction limit
(HandBlood, P82); a character shadow map (face shadows, evidence\faceshadow) needs a sampler + constants in the same shaders.
1. Quick check first (1 h, stock DXVK): does the ~3,600-instruction refusal disappear under DXVK (HandBlood on, an indoor
   6-lamp scene)? If yes, stock DXVK users already escape that one wall. The 32-temp limit stays (fxc enforces it).
2. Shader replacement in the fork: the NVR extension registers a SPIR-V replacement for a given D3D9 shader (by its
   bytecode hash or NVR's shader name); DXVK binds ours wherever the game binds that shader. Constants and textures the
   game sets keep flowing; ours can read extra NVR buffers/textures (no 16-sampler / 224-constant / 32-temp limits).
3. First targets: the lit-object family (SLS20xx: any number of lamps in one pass, no 6-lamp cap, no extra lamp passes),
   then skin + hair with a character shadow map lookup.
4. Fallback: the current D3D9 shaders stay for native D3D9 (the user plays native) and stock DXVK; same settings, same look
   where both can do it. Fork-only gains (more lamps per pass, character shadow map) are options marked "needs NVR DXVK".
Risk: matching the game's constant layout per shader variant (many SLS variants); start with the few the 6-lamp path uses.
Estimate: 10-20 session-hours after phase 1, mostly in step 2.

## Open questions
- Would the tester Ovaltine (who proposed it) help write or maintain the fork?
- Release packaging: ship our d3d9.dll + dxvk.conf with NVR as an optional download; ReShade chaining; testers on other
  DXVK builds (gplasync etc.).
- The unexplained DXVK-only cloud flicker (evidence\cloudflicker) is still open: keep FlickerWatch in the fork builds.

Estimate: ~12-20 session-hours to a measured proof. Main risk: DXVK internals (step 3) and keeping the patch small.

## Compatibility rules (the user asked 2026-10-23: presets, NVR's menu etc. must keep working)
- The fork replaces only d3d9.dll. NVR's DLL, ImGui menu, settings toml and presets stay as they are.
- An effect that gains a compute version keeps its existing setting names and meaning; NVR picks the compute path
  automatically when the NVR extension is present. Same preset = same look on native, stock DXVK and our DXVK.
- New fork-only options get defaults (old presets load unchanged); the menu marks them "needs NVR DXVK" and they do
  nothing without it. The existing .hlsl effects stay for the native path; compute shaders are added beside them.
