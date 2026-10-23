# NVR DXVK fork: plan (item #1 for the next build session; user decision 2026-10-23)

Goal (the user): leave D3D9's shader limits behind for NVR's own effects, by shipping an NVR-owned DXVK build (as RTX
Remix forks DXVK) with a small "NVR extension" through which NVR records Vulkan work into DXVK's OWN command stream.

Background: evidence\dxvkcompute\REVIEW.md. DXVK's public interop cannot order an outside vkQueueSubmit after DXVK's own
work without a CPU wait (Flush is async, LockSubmissionQueue does not wait, no semaphore export). Inside a fork that
problem disappears: our commands run on DXVK's CS thread, in its context, with its resource tracking and barriers.
Licence: DXVK is zlib (fork and ship OK; credit it). NVR stays GPL.

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
