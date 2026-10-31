# RT sun: physically sized soft shadows + BLAS/TLAS update cost (rt-sun, 2026-10-31)

Branch `claude/rt-sun-soft`, on `claude/rt-test-merge` 01a9a623 (round 13b). Everything below is off by default and RT-only
(no change without NVR DXVK + the RT helper; the 1070 / native D3D9 path is untouched).

## Where we stand (read from the code and the last iGPU run, rtsun-32 20261030-150024-slotC)

- Soft sun already exists: `SunAngle` (degrees, default 0 = one hard ray) jitters one ray a pixel over the sun's disc
  (`RtSunShadow.comp`), then the shared denoiser (`RtDenoise`: temporal + 3 a-trous passes, steps 1/2/4) smooths it.
  The gap: the a-trous chain blurs every penumbra by the same ~15 px. With the real sun (0.53 deg) a penumbra is
  `blocker distance x 0.0046`: a few pixels for a pole a few metres away, zero where a foot meets the ground. The fixed
  blur makes contact shadows as soft as long ones: no contact hardening (what modern RT games get from a penumbra-sized
  filter: NVIDIA SIGMA, AMD FidelityFX Shadow Denoiser).
- BLAS: static meshes build once (PREFER_FAST_TRACE), optional compaction (`Compact`, off). Skinned actors build with
  ALLOW_UPDATE and are **refit in place** every pose (helper, since 2026-10-29); `ActorUpdateInterval` (2) poses each actor
  every 2nd frame. TLAS rebuilt every frame (PREFER_FAST_BUILD).
- Cost on the iGPU (rtsun-32, steady): builds 0.9-1.0 ms a frame (the arena copy + actor refits), TLAS 0.33 ms, passes 5.6+ ms.
- Found while reading: actors are posed **camera-relative** (the palette subtracts the camera), but the frames
  `ActorUpdateInterval` skips keep an identity instance transform, so between poses an actor moves with the camera
  (by one frame's camera move: ~5 units walking). Shadows of people lag/slide when the camera moves.

## Part A: physically sized soft sun with contact hardening

Switch: `[Shaders.ShadowsExteriors.RT] Penumbra = false` (live).

1. With Penumbra on and `SunAngle` 0, the sun's real angular diameter is used: 0.53 deg (larger SunAngle values are kept).
2. The trace also writes each pixel's **blocker distance** (the hit's ray t; 0 = free) into a new shared image
   (`NVR RT sun hit distance`, R16F, registered at every helper start: binding 7 of `RtSunShadow.comp`).
3. The shared denoiser runs **temporal only** (Iterations 0): the jittered cone samples converge over the history
   (16 frames) without spatial blur.
4. New pass `Compute\RtSunPenumbra.frag` (a fork pass in the game, traced size):
   - blocker search: the average blocker distance of the blocked samples in a disk of `PenumbraMaxRadius` px
     (16 taps, rotated per pixel);
   - penumbra radius in pixels = `blocker distance x tan(half angle) / (world size of a pixel at this depth)`;
     0 at contact, wide for far blockers (PCSS-style estimate, but from the real traced distances);
   - a 16-tap rotated Poisson filter of the accumulated visibility at that radius, weighted by depth (relative) and
     view normal so it does not bleed across edges.
5. `PenumbraMaxRadius` (px, 32) caps the filter (the cost is fixed: 32 taps). DebugView 11: the penumbra radius
   (grey = radius / max).

Cost expected: 1 extra R16F copy-out + one 32-tap pass, instead of 3 a-trous passes (25 taps each): about even.

## Part B: BLAS/TLAS update cost

1. **Actors follow the camera between poses (fix, always on with Actors):** each posed partition remembers the
   camera it was posed against; its instance translation is `pose camera - camera`. Exact for an actor that did not
   move; one pose late for one that did (as before, minus the camera slide).
2. **Still actors are not re-posed:** `[Main.Main.RayTracing] ActorStillSkip = false` (live). Each partition keeps its
   world-space bone palette; when no bone moved more than `ActorStillThreshold` (units, 0.05) and no rotation entry
   more than 1e-4 since its last pose, there is no skinning, no arena upload and no refit: the BLAS stays, the instance
   follows the camera (1). NPCs standing, sitting, sleeping, dead bodies: most of a town.
3. **Refit drift:** refitted BLAS get slower to trace as limbs move away from the pose they were built in.
   `[Main.Main.RayTracing] ActorRebuildInterval = 0` (0 = never, as before): after this many poses a partition is
   built afresh (op flag `NVR_RT_OP_FLAG_REBUILD` 0x10; a helper without it refits as before: protocol stays 4).
4. **Compaction** exists (`Compact`, off): measured in the same run (helper log: compacted MB before -> after).
5. Not built, and why:
   - TLAS refit: the instance list changes most frames (reach culling, grass, actors entering), and the build is
     0.33 ms on the iGPU; a refit would save part of that only on frames with an identical list.
   - Swaying geometry: trees and grass sway in their vertex shaders; the CPU has no swayed positions to refit with.
     rt-ao-sky's PlantSway already leans whole plants through the TLAS transform (no rebuild).

## Bench (one bulk run, iGPU, Goodsprings, Actors on)

List `rtsun-33`: soft A/B (SunAngle 0.53 + Denoise vs + Penumbra), DebugView 4 of each, DebugView 11, a downward
view at the player's feet (contact); then ActorStillSkip on/off and ActorRebuildInterval 60 with the RT CORE log lines
(posed / still-skipped partitions, refits / full builds) and the F10 "Ray tracing" timer; Compact on for the whole run.

## Results (2026-10-31, iGPU Radeon 780M-class, 1280x720, Goodsprings 10:00 clear; helper fb92e174 = bench\deep-roadmap\rtsun\extra-p4f, fork-0033c)

Runs: rtsun-33 20261031-024258-slotC (full size, 5x5: hit the self-test limit at 6 fps), 20261031-030308-slotC (half size,
limit again), **20261031-032300-slotC PASS** (actors, compaction), **rtsun-33b 20261031-035434-slotC PASS** (Penumbra).

- **Penumbra works.** Same frame, DebugView 4: the old soft path (a-trous) blurs every edge alike; with Penumbra the shadows are
  crisp where the caster meets the ground (windmill legs, car tyres, posts) and soften toward the far end of long shadows. The pass
  draws every frame (S_OK, 640x360 at HalfResolution, tan 0.00463 = 0.53 deg, max radius 16 px traced). Lone noise specks stay in
  lit areas far from any blocker (the filter skips pixels with no blocker near; the temporal history alone is left there).
  DebugView 11 / 12 were not checked: the shot tool resets every setting to the run toml before each shot, so those shots ran with
  Penumbra off (the lists are fixed: each shot names all its settings).
- **Cost.** The denoiser for RT sun: 3.85 ms (temporal + 3 a-trous, 3x3, half size) -> 1.07 ms temporal only with Penumbra; the
  penumbra pass itself was not separately timed (its timer needs 120 frames on; it was on ~100). Full size with the 5x5 kernel the
  old soft path costs **25 ms** of denoising on this iGPU: SunAngle > 0 at full size is not usable there today.
- **Actors.** Goodsprings, ~14 posed partitions a frame (ActorUpdateInterval 2: half the frames between poses). ActorStillSkip 0.05:
  1.9 skipped (builds 0.90 -> 0.77 ms); 0.5: 2.0 skipped (0.89 -> 0.79 ms). Idle animation (breathing) keeps most bodies "moving";
  the skip helps with the dead, the seated and the player standing. ActorRebuildInterval 30: full builds 30 -> 114 over the window,
  builds back to 0.88-0.94 ms (the rebuild costs about what the skip saves). Keep both off by default; they are there for crowded
  towns (to measure there).
- **Compaction** (Compact, existing switch): 473 structures 227 MB -> 46 MB (-80 % of the BLAS memory), no frame cost seen.
