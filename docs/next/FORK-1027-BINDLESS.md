# Fork idea 2026-10-27: bindless textures + persistent mapped constants

Idea as asked: "Bindless + persistent mapped constants: no per-draw binds/constant churn, makes merging thousands of draws general."

**Verdict: NO-GO. Nothing built.** Half of the idea is already in stock DXVK 3.0.1; the other half (bindless) removes work
that runs on DXVK's own thread, which is not on the critical path; and it does not touch the cost that killed draw merging
before (the game engine's ~1 us per-object setup). The earlier NO-GOs already answer question 3.

## 1. Where per-draw CPU time goes today (measured, not guessed)

Source: fork patch 0008 profiling (`DXVK PROFILE` lines, list dxvkfork-17-profile, runs 20261026-162634 / -162839, slot C 1080p,
GTX 1070), written up in docs/next/DXVK-FORK-PLAN.md on mine/nvr-dxvk-compute, section "Where DXVK's time goes". ms per frame:

| scene | draws | frame | game outside D3D9 | DXVK on game thread | Present wait | DXVK CS thread busy | GPU |
|---|---|---|---|---|---|---|---|
| og-desert | 905 | 6.3-6.5 | 2.1-2.3 | 0.26-0.34 | 3.8-4.1 | 0.86 | busy the whole frame |
| Lucky 38 (lucky22) | 3331 | 8.6-9.1 | 6.4-6.8 | 0.86-1.03 | 1.0-1.5 | 2.6-3.1 | busy the whole frame |

Waits on the CS thread and GPU syncs: 0.00 everywhere.

- Per draw under DXVK: ~0.28-0.37 us on the game thread (D3D9 state tracking + copying the used constants into the ring +
  queueing CS commands), ~0.8-0.95 us on DXVK's CS thread (descriptor sets, barriers, Vulkan calls), in parallel.
- The game's own work dominates: 6.4-6.8 ms "outside D3D9" in Lucky 38. Native measurements agree (ENGINE-ANSWERS D6/D8,
  CPU-LIMITS.md): ~0.7-1 us of engine + NVR work per draw entry vs 0.28 us for the DrawIndexedPrimitive itself and 0.02-0.05 us
  per state call. Game's own world draws (native, NVR off): Strip ~1.0 ms, Freeside ~2.0 ms, Ultra-Luxe ~5 ms.
- CPU-bound or GPU-bound? On this PC: **GPU-bound** in every measured scene (slot desert + Lucky 38 under the fork: GPU never
  idle, 1-4 ms Present wait; native Ultra-Luxe/hangar: FNVProbe + F10, 5-7 ms Present wait; Goodsprings world GPU 9 ms).
  Confirmed, not refuted. No fork profile exists for Freeside/Strip/Goodsprings, but native numbers there show the same
  picture (main-thread world draws 1-2 ms, Present waits). Caveat (memory feedback-gpu-bound-verdicts): CPU-bound players
  exist; for them only the **game-thread** share matters, and DXVK's game-thread share is 0.3-1.0 ms for the whole frame.

## 2. What DXVK v3.0.1 already does (checked in the patch base source, G:\_NVR-build-scratch\dxvk-nvr11 = v3.0.1 + 0001-0011)

- **Persistent mapped constants: already there.** `D3D9ConstantBuffer` (src/d3d9/d3d9_constant_buffer.*) is a 1 MB per-stage
  ring in mapped host memory. `UpdateShaderConstants` (d3d9_device.cpp ~5990) runs only when the constants are dirty, copies
  only the registers the bound shader uses (per-shader constant layout), and binds the new range with
  `bindUniformBufferRange` (offset change only). When the ring fills it swaps in a fresh slice (`invalidateBuffer`), no wait.
  Small state (alpha ref, fog, point size, float count) already goes through **push data** (`m_pushData`, 256 B budget).
- **Bindless samplers: already there.** Samplers live in one global sampler heap/set (`getSamplerDescriptorSet`), bound once.
- **Bindless resources: there, but not on this GPU.** DXVK 3.0 has three binding models: `VK_EXT_descriptor_heap`,
  `VK_EXT_descriptor_buffer`, and legacy descriptor sets. The GTX 1070 driver (582.66) reports `descriptorBuffer : 0` and no
  descriptor heap (DXVK log of every slot run), so DXVK uses the legacy path: on each draw whose bindings changed it allocates
  a descriptor set from a pool and writes it with an update template (`updateDescriptorSetsBindings`, dxvk_context.cpp ~6963).
  A constant-offset change also dirties the buffer set (no dynamic offsets), so most draws get a fresh set. **All of that runs
  on the CS thread**, which is idle most of the frame (2.6-3.1 of 8.6 ms in the busiest scene).

What "bindless textures + persistent constants" would add beyond that: only replacing the legacy per-draw descriptor-set
writes with one big UPDATE_AFTER_BIND image array indexed through push data (the 1070 supports descriptor indexing:
`runtimeDescriptorArray 1`, `descriptorBindingSampledImageUpdateAfterBind 1`). That needs DXSO (DXVK's D3D9 shader compiler)
to emit indexed image access for every game and NVR shader, new pipeline layouts, and residency tracking. Saving: part of the
CS thread's ~0.8 us/draw. Game thread: ~0 (the constant copy and state tracking stay). Frame time on a GPU-bound PC: 0.
On a CPU-bound PC: 0 too, unless the CS thread becomes the slower thread, which needs the game thread under ~3 ms in a
3000-draw scene; it is 7.5 ms.

## 3. Does it make draw merging general enough to beat the instancing result?

No, and the earlier work already says why (ENGINE-ANSWERS D7/D8, memory nvr-instancing / nvr-batchrender / nvr-merged-lamp-passes):
- Instancing prototype (Freeside pass 0xFC): 551 fewer draws saved only ~0.15 ms (~0.27 us per draw); hooking every device
  call cost ~0.24-0.4 ms; net +0.09 ms. Sort-only -0.15 ms was the one cheap win.
- The cost that stays is the engine's ~1 us per-entry setup (BSBatchRenderer computes matrices, lamps, material constants per
  object). Bindless lets copies with *different textures* join one draw, which raises the census ceiling (more merges), but
  every merged copy still runs that setup unless NVR replaces it with its own constant math - the same 20-40 h, lossy-risk job
  the instancing note named. Bindless is not the missing piece; skipping the engine's setup is.
- Merged lamp passes were exact but slower on the GPU (merged loops vs scissored passes); bindless does not change that.
- Census ceilings for reference (all merges, native, main thread): Ultra-Luxe ~1.1 ms, Freeside ~1.1-1.2 ms, Strip ~0.4 ms.
  Even a perfect general merge lands under those, and only on CPU-bound PCs.

## 4. Estimate and risk

- Bindless binding model in the fork (DXSO indexed images, layouts, push-data indices, residency): **30-50 session-hours**.
  Real risk: it changes how *every* shader DXVK compiles binds textures; any slip is a crash or a wrong texture on some
  object, and it diverges from upstream DXVK (which already ships the same thing as descriptor heap for GPUs that support it).
  Pascal not supporting descriptor buffer/heap is exactly why we would have to write our own.
- On top, merging that pays (engine per-entry setup skipped, own constant math, proof that lamps/material match): **20-40 h**,
  high risk, picture risk.
- Expected gain: 0 fps on this PC (GPU-bound); for CPU-bound players at most the instancing ceilings above (~0.4-1.2 ms),
  and that part comes from the engine-side work, not from bindless.

## What would be worth doing instead (not started)

- If CPU-bound players are the target: the already-measured cheap items (pass 0xFC sort -0.15 ms, PreDecodeCache /
  FastCasterHash -1.5..-2.0 ms Ultra-Luxe, FoldHighlights -1.7 ms) are worth far more per hour.
- To check the one open assumption (CS thread never becomes the bottleneck on weak CPUs): run the existing profiling list
  (dxvkfork-17-profile, needs fork patch 0008, which is on mine/nvr-dxvk-compute but **not** in the combo fork) with the
  `affinity` shot command at 2 cores. If "DXVK CS thread busy" then approaches the frame time, revisit; no code needed.

No prototype, no fork patch, no setting, no bulk list (nothing to toggle).
