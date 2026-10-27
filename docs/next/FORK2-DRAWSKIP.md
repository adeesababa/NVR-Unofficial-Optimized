# NVR DXVK fork: GPU-side draw skipping (2026-10-27, branch deep/fork2-drawskip)

Wave-2 feature agent (brief: bench\deep-roadmap\FORK2-1027-AGENTS.md, fork patch number 0025). Base: deep/fork-combo-1026.

## Short version
- Asked: skip the GPU work of game draws whose object is hidden behind what is already on screen, decided on the GPU (Vulkan
  conditional rendering), off by default. First measure whether it can pay.
- Built: STEP 0, the measurement. `[Main.Main.Performance] DrawSkipCensus` (test aid, 0 = off) counts, in the real game, how
  much of the world scene goes to draws whose object ends up hidden, and its skip modes measure on the F10 timer how much GPU
  time each kind of skip could save. It is plain D3D9 (occlusion queries read back without waiting): it runs on native D3D9,
  stock DXVK and NVR DXVK alike. No fork patch (0025 unused) and no `NvrDxvkDrawSkip` setting yet: building the GPU skip before
  the census has run would be building blind (the brief's "first measure").
- Expected answer (reasoning + earlier measurements below): small. Hidden pixels are already thrown away by the depth test
  before they are shaded, so a GPU skip can only save (a) the vertex work of draws that end up drawing nothing and (b) the pixel
  work of objects drawn BEFORE whatever later covers them. (b) is the only big one, and it needs last frame's depth (pops when
  things move) or a depth pre-pass, which NVR already has for the costliest passes (LampsDepthFirst, DXVK).
- Next: the coordinator's bulk run of `fork2-drawskip-bulk.txt` (lucky22 + og-desert) gives the numbers. Decision rule in
  "What the bulk run decides".

## Why a GPU skip can only save little here (plain)
The game draws its opaque world by pass ID: every object's first (depth-writing) pass first, then the added lamp passes with
z EQUAL (ENGINE-ANSWERS.md D1). The graphics card tests depth before it runs the pixel shader. So:
1. An object whose pixels are all behind what is already drawn costs only its vertex work and the depth test; its pixel shader
   never runs. Its later lamp passes too. Skipping such draws saves the vertex work only, and FNV's meshes are light.
2. An object drawn before what later covers it is shaded fully, and then painted over. That is real waste, but to skip it the
   decision has to know the future: last frame's depth (wrong for a frame whenever the camera or an NPC moves: a hole or a pop)
   or a depth pre-pass (exact; NVR's LampsDepthFirst already does it for the many-lamp passes on DXVK and measured -0.1..-0.5 ms
   in Lucky 38 / the casino). Earlier counts: the many-lamp passes shade 1.50 samples per screen pixel, 1.21 with depth first,
   so only 19 % of their pixel work is covered later (LampsDepthFirstCount, Ultra-Luxe), and fully hidden objects are only a part
   of that 19 %.
3. Conditional rendering skips only the draw itself: DXVK still binds the pipeline, descriptors and constants for it, and the
   game still spends its CPU on it. On this PC the game is GPU-bound (DXVK PROFILE: GPU never idle), so only GPU time counts.

A captured Lucky 38 frame (RenderDoc, lucky22-d3d9_frame1850.rdc, replay timings, the world scene = render passes 12-16 on the
main colour + depth targets): 3,026 draws, 9.6 ms in replay (replay times each draw on its own, so small draws are inflated);
305 draws of 5 us or more make 79 % of that time, while 2,574 draws (85 % of the count) take 0-1 us each (16 %). Hidden draws are
mostly in the cheap group, so even skipping ALL of them could not save more than a small share. (Per-draw samples-passed counts
of that capture: see "RenderDoc census" below if it ran.)

## The census (built): settings, what it logs, how to read it
`[Main.Main.Performance]` (both live, off by default; DrawSkipCensus is in the developer-only list, the DebugView key is hidden by
its name):
- `DrawSkipCensus = 0`: 0 off; 1 count; 2 / 3 / 4 count and leave out one kind of hidden draw (below).
- `DrawSkipCensusDebugView = false`: red rectangles over objects hidden in the finished depth, yellow over objects hidden when
  drawn that the bound test could not prove.

Counting (`src/core/DrawSkipCensus.h`, hooks in `src/NewVegas/Hooks/Render.cpp`), in one frame of four (so F10 p50 stays clean):
- every pass of every object in the frame's first world scene (RenderPassStandard / RenderPassSkinned) is drawn inside its own
  D3D9 occlusion query: the samples it drew; the triangles come from the world scene's DrawIndexedPrimitive calls (device vtable,
  chained like NewGrass / SandstormGlass);
- after the world scene each object's world bound sphere is tested against the finished depth: the screen rectangle round the
  sphere (the 8 corners of the view-aligned cube round it, projected) drawn at the sphere's nearest depth with depth test only
  and no writes, inside a query; 0 samples = no part of the sphere can be seen. Spheres that reach the near plane and skinned
  objects (their bound does not follow the bones) are "not testable" (counted as seen). Outside the "World scene (game)" timer.
- answers are read 2-4 frames later without waiting (a ring of 6 counting frames; one not in after 20 frames is dropped,
  counted as "dropped late").

Log line, every 240 frames: `DRAW SKIP CENSUS (mode m, N counting frames, ...)`: world draw calls and triangles (and the share
that are object passes) | objects, passes, triangles, samples (per screen pixel) | passes that drew nothing | HIDDEN WHEN DRAWN
(the object's first, depth-writing pass drew nothing): objects, their passes and triangles, of them later passes | HIDDEN IN THE
FINISHED DEPTH (bound test): objects, passes, triangles, the samples they shaded anyway (% of all samples = pixel work covered
later) and how many of them were seen when drawn | hidden when drawn but not proven by the bound test. Modes 2-4 add a
`DRAW SKIP CENSUS skips` line (camera-still share, passes and triangles left out per frame).

Skip modes (test only; only while the camera stands still 8 frames, answers <= 48 frames old, never skinned objects, not while
HiddenObjectCulling or LampsDepthFirst is on; in mode 4 every object is drawn in full in one counting frame of eight to
refresh its answers, so mode 4's saving reads a few percent low):
- 2: later passes of objects hidden when drawn are left out = what an EXACT same-frame GPU skip could save (they would draw
  nothing). Picture cannot change.
- 3: every pass of objects hidden in the finished depth (one frame earlier) is left out = what a GPU test against last frame's
  depth could save. On a still scene the picture cannot change (the finished depth never needs a hidden object).
- 4: every pass that drew nothing the last time is left out = upper bound of any per-draw skip.
Read F10 GPU `World scene (game)` **p50** of each shot against mode 1 (same counting cost).

Cost: off = one boolean test per pass. On: CPU (thousands of queries and a map lookup per object) mainly in the counting frames,
and the bound-test rectangles (depth test only) after the world scene in counting frames.

## What the bulk run decides (`bench\foundations\lists\fork2-drawskip-bulk.txt`, lucky22 + og-desert)
Shots: settle, off-a, c1-a, c3-a, c1-b, c3-b, c2, c4, off-b, view (DebugView). Saving = c1 p50 - cN p50 of "World scene (game)".
- c3 saves >= 10 % of the world scene in lucky22: build step 1 = "skip objects hidden in last frame's depth" on NVR DXVK (below),
  8-12 session-hours.
- only c2 saves >= 10 %: build the exact same-frame skip (below), 5-8 session-hours.
- neither (expected): STOP. Write the numbers here; GPU-side draw skipping is not worth it for FNV on this GPU. The cheaper,
  exact answers to "hidden work" stay LampsDepthFirst (pixel work, DXVK) and HiddenObjectCulling (CPU, any runtime).
Proof that the census works: the view shot shows red rectangles in Lucky 38 (behind the bar, walls, pillars); c2/c3/c4 log
"passes left out" > 0 with "camera still" ~100 %; pictures of c2/c3/c4 equal c1 within the c1-a/c1-b noise.

## Design for step 1 on the fork (not built; patch 0025, extension "1.7-drawskip")
DXVK side (all additive; a new interface `ID3D9NvrExtPredicate` queried from `ID3D9NvrExt`, E_NOINTERFACE on older builds):
- enable `VK_EXT_conditional_rendering` (`conditionalRendering`) when the device has it (the GTX 1070 does): dxvk_device_info.h/.cpp
  (one extension entry + one ENABLE_EXT_FEATURE line), and two `DxvkCommandList` wrappers (vkCmdBegin/EndConditionalRenderingEXT).
- `SetDrawPredicate(buffer, offset)` / `ClearDrawPredicate()`: recorded into the CS stream; `DxvkContext` keeps the slice, and
  `drawGeneric` wraps the draw in Begin/End conditional rendering (inside the render pass; the buffer is tracked for lifetime).
  A CS command between draws already ends DXVK 3.0's draw batching (D3D9DeviceEx::DrawIndexedPrimitive merges only back-to-back
  draws), so a predicated draw is never merged with an unpredicated one. Only draws whose depth test is LESS / LESS_EQUAL /
  EQUAL may be predicated (the context knows the pipeline state).
- barriers DXVK does not know about: after NVR's predicate dispatch, an explicit memory barrier compute write ->
  CONDITIONAL_RENDERING read; before the next write, the reverse (WAR).
- proof counter: the predicate buffer copied through the 1.5-debug readback ring (never waits) and the zeros counted =
  "skipped draws" in the log, as the brief asks.
NVR side, "last frame's depth" variant (step 1 if c3 pays): a slot per object (the census's object table), its world bound
uploaded once a frame (UpdateBuffer, 16 bytes per object); before the world scene one compute pass reprojects last frame's
depth into this frame's view (forward splat, nearest wins, holes = far = "seen"), one builds a max-depth pyramid of it, one tests
each sphere's rectangle and nearest depth against the pyramid and writes the predicate (1 = draw). The pass hooks set the
object's slot before each of its passes (`SetDrawPredicate`) and clear it after. New objects (no slot yet), skinned objects,
objects whose sphere reaches the near plane and alpha-blended passes are never predicated. Known failure: an NPC or door that
moved away uncovers an object one frame late (the next frame's test sees it); a camera cut (loading, teleport) resets all slots
to "draw". Setting `[Main.Main.Performance] NvrDxvkDrawSkip = false`.
Exact same-frame variant (step 1 if only c2 pays): wrap each object's first pass in an extension-owned occlusion query (own
VkQueryPool, begun/ended by the context right around the draw, inside the render pass), end the render pass ONCE at the first
added-lamp pass ID (0x12C), `vkCmdCopyQueryPoolResults` into the predicate buffer, barrier, and predicate the added passes on
it. Exact by construction (a z EQUAL pass of an object whose depth-writing first pass drew nothing draws nothing), no pops, one
extra render-pass break per frame.

## Files
- `src/core/DrawSkipCensus.h` (new), `src/NewVegas/Hooks/Render.cpp` (hooks: BeginFrame, BeginWorld/EndWorld, per pass
  Before / BeginDraw / EndDraw), `resource/NewVegasReloaded.dll.defaults.toml` (two keys + DevOnly list).
- Bulk list: `C:\Users\foot\Documents\Codex\2026-10-03\this\work\bench\foundations\lists\fork2-drawskip-bulk.txt`.
- Install (test slot only): `NewVegasReloaded.dll` -> `<game>\Data\nvse\Plugins\`. No shader files, no fork d3d9.dll needed
  (the census runs on any runtime; the coordinator's fork setup is fine).
