# AMD FSR 3.1 upscaler next to FSR 2.2.1 (2026-10-28, branch claude/fsr31-upscaler-0ab1f2)

Credit and licence: FidelityFX Super Resolution 3.1 is AMD's (FSR 3.1.x upscaler from the AMD FidelityFX SDK 1.1.4, GPUOpen,
https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK), MIT licence. The licence text ships next to the shaders:
`src/hlsl/NewVegas/Compute/FSR31/LICENSE-FidelityFX-SDK.txt`. AMD's GPU headers in that folder are unchanged; only
`fsr3upscaler/ffx_fsr3upscaler_callbacks_nvr.h` (made from AMD's `ffx_fsr3upscaler_callbacks_glsl.h`, kept beside it; the changes
are listed at its top), the small pass wrappers `FSR31*.comp` and `core/UpscalerFsr31.cpp` are NVR's. **Upscaling only: no frame
generation** (not built, not prototyped).

## The setting
`[Shaders.TAA.FSR] Version` (menu: TAA / FSR / DLSS, a combo): **0 = FSR 2.2.1 (default, exactly as before)**, 1 = FSR 3.1.
Everything else is shared: `Enabled`, `RenderScale` (the quality-mode combo), `LowResEffects`, `Sharpness`, `WeaponReactive`,
`DebugView`. DLSS keeps its own tab and wins over both, as before. Live (no restart).

Why not the key `Upscaler`: until 2026-10-27 `[Shaders.TAA.FSR] Upscaler = 1` meant DLSS; old settings files still carry it (the
user's has `Upscaler = 0` and the old `DlssHelperTest`). Reusing it would have switched such a file to FSR 3.1 by surprise.

## Which way: 32-bit SPIR-V in the fork, no helper (the 32-bit question)
FSR 3.1 is open source and its GPU half is plain GLSL (AMD ships a GLSL Vulkan backend: `sdk/src/backends/vk/shaders/fsr3upscaler`).
Only AMD's prebuilt runtime DLLs are 64-bit, and NVR does not need them: exactly as for FSR 2, AMD's passes are compiled to SPIR-V
(glslang 15, Vulkan SDK 1.3.296.0) and recorded by NVR's 32-bit code into DXVK's command stream through the fork's compute
extension; `core/UpscalerFsr31.cpp` does on the CPU what AMD's `ffx_fsr3upscaler.cpp` does (constants, ping-pong resources, clears,
dispatch order). So no 64-bit helper, no shared images, no semaphores, no new DXVK patch: it runs with the d3d9.dll the user already
has (extension 1.3 or newer for the mip sampler). The DLSS-style helper would only be needed for closed 64-bit code.

What AMD's code needed for D3D9 resources (all in the callbacks header): no r32ui render target in D3D9, so the reconstructed
previous depth and SPD's atomic counter are storage buffers (atomicMin / atomicAdd), and the 1x1 frame info is a buffer (reset with
UpdateBuffer); r8 / rgba8 resources became r16f / rgba16f; samplers come from DXVK's sampler heap (plus a mipmapped linear one for
the shading-change pyramid: NVR's plain linear sampler is clamped to mip 0); exposure 1; the reactive mask is the green channel of
NVR's depth input (weapon); no transparency-and-composition mask (0, AMD's default); SPD runs its LDS path (no subgroup quad ops).
Bindings were checked against the compiled SPIR-V with spirv-dis.

Inputs are FSR 2's, unchanged: the same depth, camera-only motion vectors, reactive mask for the weapon, RenderScale v2
(LowResEffects) and its HDR input, native AA after tonemapping. Jitter: FSR 3.1's own sequence is the same function as FSR 2's
(Halton 2/3, `8 * (display / render)^2` phases: `ffxFsr3UpscalerGetJitterOffset` / `GetJitterPhaseCount` are identical), so NVR's
jitter is used as is; the phase count steps by one per frame after a change, as AMD does. Sharpening: 3.1's RCAS (same as FSR 2's).
Failure handling: a missing `FSR31*.spv` or a failed pass is logged once and FSR 2 takes over the same frame and the rest of the
session (it does NOT turn RenderScale off). Switching between FSR 2 and 3.1 restarts the history and frees the other's textures.

Per frame, in AMD's order: prepare inputs, luma pyramid (SPD), shading-change pyramid (SPD), shading change, prepare reactivity,
luma instability, accumulate (or accumulate + RCAS when Sharpness > 0). F10 timers `  FSR3.1 ...` inside "FSR upscale (RenderScale)"
or "TAA" (native AA).

## Results (2026-10-28, slot C 1920x1080, GTX 1070, installed zresolve fork d3d9, copy of the user's toml)
Runs: `20261028-145940-slotC` (lists `fsr31-still` at og-desert + og-goodsprings, one launch) and `20261028-150446-slotC`
(`fsr31-pan`, og-goodsprings, camera turning 20 deg/s). Pictures `H:\_NVR-autotest-shots\<run>\`, crops + scripts
`bench\deep-roadmap\evidence\fsr31\`. Jitter forced on in every shot (see "Note" below).

**It works**: all 11 pipelines made, `FSR 3.1 resources made (... about 146 MB)` at 1080p, every 3.1 shot ran 3.1 (no
`[ERROR]`, no fallback, RenderScale stayed on), at native AA, Quality (0.667, HDR input), Performance (0.5), with RCAS.
Pictures whole, same colours (3.1 is 0.3/255 darker on average; within FSR-vs-FSR noise, not visible).

| GPU ms (F10 medians) | FSR 2.2.1 | FSR 3.1 | difference |
|---|---|---|---|
| desert, Quality 0.667 (whole "FSR upscale (RenderScale v2)") | 2.19 | 2.62 | +0.43 |
| Goodsprings, Quality | 2.20 | 2.66 | +0.46 |
| desert / Goodsprings, Performance 0.5 | 1.95 / 1.92 | 2.20 / 2.21 | +0.25 / +0.28 |
| desert / Goodsprings, native AA ("TAA") | 2.77 / 2.62 | 4.11 / 3.34 | +1.3 / +0.7 |
| Quality + Sharpness 0.5 (RCAS 0.17-0.19 in both) | 2.83 / 2.50 | 3.28 / 2.83 | +0.45 / +0.33 |
| Goodsprings pan, Quality / native | 2.39 / 2.86 | 2.76 / 3.51 | +0.37 / +0.65 |
| game frame total, Quality (desert / Goodsprings) | 6.67 / 10.51 | 7.20 / 11.31 | +0.5 / +0.8 |

FSR 3.1 passes at Quality (1280x720 -> 1920x1080): prepare inputs 0.18, luma pyramid 0.05, shading-change pyramid 0.27,
shading change 0.01, prepare reactivity 0.21, luma instability 0.15, accumulate 1.08 (FSR 2's accumulate is the same 1.04; the extra
cost is the new reactivity / instability / pyramid passes). Both share the RenderScale copy, prep and composite.

Picture:
- **Still camera, frozen world (shimmer)**: pixels that change between two repeats of the same setting (the jitter moving):
  FSR 2 9.1-10.9 %, **FSR 3.1 7.5-7.9 %** (desert and Goodsprings alike): about a quarter less flicker on a still picture.
- Still detail (pylon lattice, fences, grass, `desert-pylon.png`, `desert-fence.png`): the same sharpness; 3.1 a touch smoother
  on the pylon lattice. Nothing lost.
- **Motion (turning)**: no trails or ghosting with either on poles, fences, roofs, the truck. Thin hanging wires: FSR 2 breaks them
  into dashes while turning (`pan-2-pole.png`), FSR 3.1 keeps them as continuous lines (`pan-31-wires.png`), the main visible gain.
- Not covered: NPCs and the first-person weapon were not in frame in these spots (no NPC walked past, the save has no weapon
  out), so NPC/weapon ghosting is unproven; both upscalers get the same camera-only motion and the same weapon reactive mask.

Verdict: FSR 3.1 is a little steadier and holds thin wires in motion, for about +0.4 ms at Quality on the GTX 1070 (+0.7 to +1.3 ms
as native AA). Worth offering as a choice; FSR 2 stays the default.

Note (the user's own settings): the user's toml has `[Shaders.TAA.Main] Jitter = false` with FSR on. Without jitter neither FSR
gets sub-pixel samples (it then only smooths in motion). The `userjitter` shots (jitter off, as the user plays) are in the run for
a look; they were not measured separately.

## Files to copy (install)
Built on the installed build (claude/p84v3-tessring 5c547109); nothing else in the DLL changes. Game folder = `<FNV>`:

| from the repo / build | to |
|---|---|
| `NewVegasReloaded\Release\NewVegasReloaded.dll` (+ `.pdb`) | `<FNV>\Data\NVSE\Plugins\` |
| `resource\NewVegasReloaded.dll.defaults.toml` (adds `[Shaders.TAA.FSR] Version`, so the menu shows it) | `<FNV>\Data\NVSE\Plugins\` |
| `src\hlsl\NewVegas\Compute\FSR31*.spv` (11 files: PrepareInputs, LumaPyramid, ShadingChangePyramid, ShadingChange, PrepareReactivity, LumaInstability, RCAS, Accumulate, AccumulateSharpen, AccumulateHdr, AccumulateSharpenHdr) | `<FNV>\Data\Shaders\NewVegasReloaded\Compute\` |
| `src\hlsl\NewVegas\Compute\FSR31\LICENSE-FidelityFX-SDK.txt` (AMD's MIT notice, must ship with it) | `<FNV>\Data\Shaders\NewVegasReloaded\Compute\FSR31\` |

No d3d9.dll / dxvk.conf change (any NVR DXVK fork with extension 1.3+; the installed zresolve fork is fine). No .hlsl changes, so
the shader Cache does not need clearing. Then in the menu: TAA / FSR / DLSS -> FSR -> Version -> "1 - FSR 3.1" (live). Without
the .spv files the log says "FSR 3.1: Compute\FSR31*.spv is missing" once and FSR 2 runs.
