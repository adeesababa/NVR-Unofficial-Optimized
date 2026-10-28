# Linear scene: draw the world in linear light, encode once (plan, 2026-10-28)

User: "seems like this bug keeps popping up over and over how can we solve this once and for all across all areas" and then
"Yes: plan and build the linear-scene fix (world drawn in linear light into the fp16 target, encoded once), off by default with
the old path kept for A/B."

## The one cause behind every "lighter patch" report

| date | report | where | what the earlier fix covered |
|---|---|---|---|
| 10-21 | some Lucky 38 floor tiles shinier | objects, 3+ lamps | FoldHighlights (folds highlight passes into the light pass) |
| 10-21 | lamp-rich walls brighter | objects, extra lamp passes | LampSums (per-pass increments) |
| 10-25 | Novac rubble / road pieces lighter | parallax objects | HighlightSums + parallax LampSums |
| 10-28 | Freeside road piece lighter at night (POM) | parallax, night lamps | run cap 2 -> 8 (claude/lampruns 51003e6a, unconfirmed) |
| 10-28 | road surface goes lighter in one frame while turning, by day | Freeside, no lamps; ~40 highlight passes a frame | none yet |

The game draws some surfaces in several passes and blends them in the frame: a light pass, extra-lamp passes ADDED (ONE+ONE), a
texture pass that MULTIPLIES, highlight passes ADDED, fog. NVR's linear lighting decodes colours, lights in linear light and
**encodes each pass's result on its own** (`LinEncode`, `includes/LinearLight.hlsl`) before the game's blend adds it. The blend
then sums encoded values: enc(a) + enc(b) instead of enc(a + b) — a small light comes out several times too strong (1 % light ->
12 % stored at gamma 2.2). Whether a surface takes extra passes changes with lamps, distance, the camera and the game's sort, so
the error appears and vanishes per piece. Each fix so far patched one route (one pass kind, one shader family, one run count);
the next report takes a route that is not patched.

## The fix: keep the world in linear light until the world is done

The world is already drawn into a 16-bit float target (A16B16G16R16F, `bench\foundations\REPORT.md` line 54), so it can hold
linear light with no precision loss. If every NVR world shader wrote **linear** light (no per-pass `LinEncode`), the game's own
ADD and MULTIPLY blends would be exact, for every pass kind, every shader family, any number of runs. One full-screen step encodes
the result once, where the stored curve is expected again.

`[Shaders.PBR.Linear] LinearScene` (planned): false by default, needs a restart (shaders are compiled with it). Off = exactly the
current shaders and hooks.

## What has to change (and what the census decides)

1. **The linear span.** First choice: the main opaque list, `RenderBatches(1, 0x25B)` at 0x00B65BED (GrassFirst::MainListHook
   already wraps it). Everything drawn after it (decals in groups 2/3, alpha, water, particles, first person) stays in the stored
   curve as today, so their look does not change. Open: whether sky and water fall inside 1..0x25B (the exe's name list is not a
   reliable map past ~0x170) — **the census answers it**.
2. **NVR's lit shaders** (ObjectTemplate, ParallaxTemplate, SkinTemplate, Terrain*, Leaf, grass, SM300x): with `LINEAR_SCENE_ON`
   and the live flag (`TESR_LinearLighting.w`), `LinEncode` returns the linear value inside the span; **fog** lerps to the
   decoded fog colour (`LinColor(fogColor)`) so fog keeps its colour (fog's blend amount now applies in linear light: a slight
   look change, to be measured).
3. **The game's own shaders in the span.** The texture pass (BSSM_TEXTURE*, 0x14A-0x153) multiplies by the texture colour and is
   the game's shader (not in `PBR.h`'s template list): in linear it must multiply by the DECODED colour, so NVR needs its own
   texture-pass shaders (simple: texture x vertex colour, decoded). Any other game pixel shader drawing opaque into the span
   (EnvTemplate SLS2057/58, LOD, distant trees, ...) writes stored values and would be encoded twice (too dark): each gets an NVR
   twin that outputs linear, or its pixels are marked (stencil bit) and skipped by the encode step. **The census lists them.**
4. **The encode step** at the end of the span (MainListHook, right after the call): one full-screen pass
   `scene = pow(scene, 1/gamma)` over the pixels the span wrote (sky pixels skipped by depth or by the mask), with the game's MSAA
   on the multisampled target per sample (same approach as PassSums' fold-in).
5. **Retire the per-route patches under the switch:** LampSums, HighlightSums, ExactPassSums and the run cap do nothing with
   LinearScene on (the blend is already exact). FoldHighlights stays (it also saves ~0.8 ms), but its divide-by-texture-colour
   maths must use the decoded colour there.
6. **NVR's own readers inside the span** (terrain blending's depth copy, OnePassDepth, PassSums' frame copies, GrassFirst): any
   that reads the scene COLOUR mid-span gets linear values; list them and decode where they read.

## Steps

| step | what | done when | session-hours |
|---|---|---|---|
| 1 | `[Main.Main.Misc] LinearSceneCensus` (dev builds, live): every draw into world / first person by pass, pixel shader (NVR / game), blend, main list or not; log every 600 frames | built (claude/linear-scene); a census log from Freeside day, an interior (Lucky 38) and an exterior with water | 1 (done) |
| 2 | shader switch (2 above) + NVR texture-pass shaders (3) | builds; off = byte-identical shaders | 6-10 |
| 3 | encode step + sky/game-shader mask (3, 4) | builds; on/off pictures match where nothing is multi-pass | 4-6 |
| 4 | retire patches under the switch (5, 6) | builds | 2 |
| 5 | ONE bulk A/B list: Lucky 38 tiles, Novac rubble, Freeside road day + night, Ultra-Luxe, noon/dusk exterior, water, sky, fog, glass, decals; F10 cost | patch scenes: piece / neighbour brightness ratio within 2 % on; nothing else moves more than the fog change; cost <= +0.3 ms | 4-6 |

Total ~17-25 session-hours. Real risk: the number of the game's own shaders inside the span (step 3) — each needs a twin or a
mask. The census measures that before step 2 starts.

## A/B

Off (default): every shader compiled exactly as today, every hook as today. On: one restart. Same launch A/B is impossible for
the compile switch, so the bulk list runs twice (off, on) with the same shots; the per-route patches' own A/B lists stay valid
for the off side.
