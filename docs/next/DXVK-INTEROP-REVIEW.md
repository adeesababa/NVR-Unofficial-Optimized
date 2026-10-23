# DXVK Vulkan-compute spike: DeepSeek-V4-Pro's code, reviewed (2026-10-23)

Brief: deepseek-prompt.txt (NVR AvgLuma pass + DXVK v3.0.1 d3d9_interfaces.h). Answer: deepseek-answer.md (cut off at 64k tokens)
+ deepseek-answer-2.md (continuation). Tokens: ~32k in / ~85k out. Checked against DXVK v3.0.1 source (d3d9_interop.cpp,
d3d9_device.cpp, dxvk_queue.cpp, dxvk_context.cpp from github.com/doitsujin/dxvk). Nothing built, nothing installed.

## Verdict
Not usable as written. It would not compile, and the core synchronisation is wrong. The review also turned up a structural
problem with the route itself: **DXVK's public D3D9 interop has no GPU-side way to order our work after DXVK's.**

## The structural finding (confirmed in DXVK's source)
- `FlushRenderingCommands()` = `Flush()` + `SynchronizeCsThread(All)`. `Flush` only appends the command list to
  `DxvkSubmissionQueue::m_submitQueue`; a separate "dxvk-submit" thread calls vkQueueSubmit later (dxvk_queue.cpp
  `submitCmdLists`).
- `LockSubmissionQueue()` = `lockDeviceQueue()` = takes `m_mutexQueue` only. It does NOT wait for queued entries.
  So after flush + lock, DXVK's scene rendering (and DeepSeek's layout transitions, also recorded via
  `TransitionTextureLayout` -> EmitCs) may still be waiting in DXVK's queue. Our vkQueueSubmit can reach the GPU
  BEFORE them: the compute pass reads a half-drawn scene in the wrong layout. That is a race, i.e. intermittent garbage.
- DXVK orders its own submissions with timeline semaphores it does not expose. The interop has no semaphore export.
- The only correct primitive in the interop is `WaitForResource(tex, D3DLOCK_READONLY)`. It waits until the GPU has
  finished writing the texture (`m_dxvkDevice->waitForResource`), i.e. the CPU waits for the GPU every frame.
  That removes CPU/GPU overlap. In a ~16 ms frame that is likely several ms lost, far more than any compute pass saves.
- Ways around it all leave the public interop: a patched/forked DXVK that exposes its timeline semaphore (what RTX
  Remix does, as a full fork), or hooking DXVK internals (breaks across versions). Either ties NVR to one DXVK build.

## Code problems (would show up before the race)
1. Won't compile/link: the header declares `VkComputeSpike::Initialize(IDirect3DDevice9*)`, `Run`, `Shutdown`,
   `Enabled`, but the .cpp defines them inside an anonymous namespace (Initialize with no parameter). Wrong includes
   ("SettingsManager.h" is SettingManager.h; "../d3d9/d3d9_interfaces.h" is not vendored); invented diff anchors and paths
   (src/render/, a RenderManager shutdown block that does not exist, a settings.toml path).
2. No barriers at the start of its own command buffer: nothing makes DXVK's writes visible to the compute reads, and with
   4 submissions in flight sharing one partial-sums buffer, frame N+1 can overwrite sums frame N is still reading.
3. One cached source image + view: NVR's FrameChain swaps TESR_RenderedBuffer between textures every pass, and DXVK 2.4+
   can relocate images. The code disables itself the first time the VkImage changes, i.e. within a frame or two.
4. The go/no-go log line never prints: `ReadTextureLuma` calls LockRect on D3DPOOL_DEFAULT render-target textures,
   which D3D9 (and DXVK) refuse. Needs GetRenderTargetData into a SYSTEMMEM surface.
5. "cpu" time is never accumulated (totalCpuSeconds stays 0).
6. Initialize from RenderManager init creates a TextureManager texture, probably before TextureManager exists.
7. Shutdown calls vkDeviceWaitIdle on DXVK's device without the submission lock (Vulkan requires all queues externally
   synchronised for that call). Device reset is not handled. Minor: HalfToFloat NaN mantissa shifted by 12, not 13.

## What is good
Dynamic Vulkan loading with VK_NO_PROTOTYPES, 64-bit handle types on x86, the format-feature check, descriptor/pipeline
setup, fail-once-and-disable, the non-blocking fence ring idea, and the GLSL two-phase reduction. Its linearize/luma
match NVR's Helpers.hlsl exactly.

## Recommendation
Do not build this. The real question for the tester (Ovaltine) who suggested the route: which method does he mean, and
how does it order NVR's Vulkan work after DXVK's without a CPU wait or a DXVK fork? If he has an answer (e.g. a known
DXVK patch or hook), a fixed spike is ~3-5 session-hours. Without one, per-frame compute through the public interop costs
a CPU-GPU sync every frame, so it is a NO-GO for performance work. It could still suit rare, non-per-frame jobs (e.g. a
one-off bake on load), where a wait does not matter.
