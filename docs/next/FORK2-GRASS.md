# NVR DXVK fork, round 2: GPU grass (2026-10-27, branch deep/fork2-grass, fork patch 0024 "1.7-grass")

Grass strands that the graphics card places, culls and counts itself, then draws with one indirect draw per level of detail,
into the game's own scene with its depth buffer, instead of the game's alpha-tested grass cards. FIRST STEP: built, off by
default, not run in game yet (the coordinator's bulk test: `bench\foundations\lists\fork2-grass-bulk.txt`).
Base: `mine/deep/fork-combo-1026` fast-forwarded to `mine/deep/fork-combo-1027` (b814b9ae, a descendant of 1026): the depth
attachment this needs builds on 1027's DrawDepthStencil (patch 0010), so patch 0024 sits on the merged series 0001-0016 and the
merge into 1027 is a plain merge.

## In plain words
- What it does: where the game puts a grass clump (its own tufts: place, slope, size, brightness, the grass types of each land
  texture), it draws a bush of thin strands in that card's own colours and outline (read from the card texture), lit the way the
  game lights its grass, moving in NVR's grass wind (or the game's sway). The cards themselves are skipped. Density can add more
  clumps of the same plant beside each of the game's, for fuller fields. The far fading grass stays the game's cards.
- Why: the game's grass cost is the alpha test on layers of cards (the 10-17 study: 2.3-2.6 ms at density 30, and no lossless way
  under ~1 ms). Opaque strands need no alpha test, so the depth test throws away hidden pixels before they are shaded; the GPU
  does the placement work in one compute pass, so dense grass costs triangles, not overdraw.
- Off (the default), native D3D9, stock DXVK or a fork without patch 0024: nothing changes (no hook, no device call).
- Expected cost (to be measured by the bulk test): the cull pass ~0.02-0.05 ms for ~10k tufts; the strands at the INI density
  (iMinGrassSize 80, ~40 early grass draws, ~9k tufts; with the defaults about 16k near strands x 5 triangles + 90k far strands x 1
  = ~170k triangles) ~0.1-0.3 ms against the cards' 0.32-0.41 ms GRASS time; at Density 3 (about the cards' density 30)
  ~0.4-0.8 ms against the cards' 2.3-2.6 ms. TARGETS: Density 1 at or below the cards' GRASS time
  (on vs off), Density 3 under 1 ms.

## Where the placement comes from (and what was not done)
The brief asked for the game's placement: landscape texture -> grass types, or the placement the game computes. The second is
reached cheaply and exactly: during GrassFirst's early grass call (Render.cpp, the near grass drawn before the land) each card
draw is caught at its DrawIndexedPrimitive (as NewGrass does): the TallGrassShader's instance table for that draw (up to 228
tufts of one grass type on one piece of land; xyz world position, frac(xyz) the packed slope normal, w the size step + light
scale), the card model (height, width, sway weight per height: NewGrass::GetModel) and the card's colour and silhouette per height
band (NewGrass::GetRamp, read once per model from its texture). The game itself already did the land-texture -> grass-type
lookup, the density, the slope and water tests (TESObjectLAND::CalculateGrassParams, TESTerrainLODManager::CreateGrass; research
`wave-1018b\f05-grass.md`). Reproducing that placement on the GPU from the land data (per-quadrant LTEX layers, TESGrassAreaParam
grids, the land heights) would let the GPU place grass the game never placed (denser than iMinGrassSize, beyond the game's grass
radius); that is step 3 (estimate below), not needed for dense grass now: Density spreads extra clumps on each tuft's slope.

## What was built
Fork (`tools/nvr-dxvk/patches/0024-NVR-extension-1.7-grass-...patch`, on the 1027 series 0001-0016; the three d3d9_nvr_ext* files):
- New interface `ID3D9NvrExtGeometryScene` (GUID ...d324, queried from ID3D9NvrExt; additive, nothing else changes; the version
  number stays 1.6, "1.7-grass" in comments), one method `DrawGeometryScene`: a geometry pipeline's draw (ID3D9NvrExtGeometry,
  patch 0013) with
  - a depth-stencil attachment with REAL depth (the vertex shader's z, viewport range 0-1; the tests of 1.6's NVR_EXT_DEPTH_STENCIL),
  - multisampled targets at their own sample count (the game's MSAA scene), and attachment-only targets (CreateRenderTarget surfaces),
  - face culling, zero depth bias and depth bounds 0-1 for the pass,
  - indirect counts: a VkDrawIndirectCommand read from a buffer a compute shader wrote (new buffer flag `NVR_EXT_BUFFER_INDIRECT`;
    DXVK orders the compute writes before the indirect read).
  The shared DrawImpl gets one more optional parameter (the scene description); Draw / DrawShifted / DrawDepthStencil / DrawGeometry
  behave exactly as before. CI: run 40443132806 built the first version (green); the final patch (plus attachment-only targets and
  a depth-stencil surface larger than the target, both allowed by D3D9): run 40445689167, green (artifact nvr-dxvk-x32, not
  downloaded here).
  The series 0001-0016 + 0024 re-applied in order onto v3.0.1 gives the same d3d9 tree as the development worktree (checked).
NVR (`src/effects/GpuGrass.{h,cpp}`, shaders `src/hlsl/NewVegas/Compute/GpuGrass.glsl`, `GpuGrassCull.comp`, `GpuGrass.vert`
(-> GpuGrassNearVS.spv / GpuGrassFarVS.spv with -DGG_FAR=0/1), `GpuGrass.frag` (-> GpuGrass.spv)):
- Catch (CPU, per early card draw): the instance table, the draw's grass constants (sun direction and colour, ambient, dimmer, wind,
  fade, ModelViewProj, fog), one 128-byte draw record (plant height and width, sway line, ScaleMask, the card's bright colour and
  alpha edge per height band, its opaque area). The card draw is swallowed (KeepCards false) or kept (true). Left to the game:
  draws farther than BladeDistance, models that are not cut-out cards (rocks, debris), the 001/003 variants (lit by real normals),
  geometry not at the world origin.
- Cull pass (compute, one thread per clump, 64 per group): the game's own part-1 rule (only tufts before the fade start; the fading
  ones stay cards), extra clumps (Density, on the tuft's slope plane), frustum test, strands by distance (Blades near -> BladesFar
  at LodFar; the last strand of a fractional count shrinks, no popping), ONE atomic per clump reserves its records in the near list
  (5 triangles a strand, nearer than DetailDistance) or the far list (1 triangle). Each strand: a quadratic Bezier (NewGrass round 6's shape: roots near the
  centre, tips inside the card's alpha outline at their height, LengthMin..1 of the plant, Bend, Droop), the wind at the tip (the
  game's sway with the card's weight at that height, or GrassWindMove when [Shaders.Grass.Wind] is on, plus Flutter), the width
  from Coverage (the clump's strands together cover Coverage x the card's opaque area: fewer far strands are wider, so the field
  keeps its density) with a floor of MinPixelWidth pixels.
- Draws (two indirect geometry draws into the device's render target and depth buffer, right after the early grass call, before the
  land): the game's depth compare (GREATEREQUAL, reversed depth) with depth writes, MSAA as the scene, RGB writes only (as the game's
  grass). Vertex shader: Bezier + taper + kinks; colour = the card's ramp at the strand's height on the plant x (ambient x light scale
  + sun x NdotL(packed slope normal) x light scale x dimmer) = exactly the game's grass pixel shader's lighting (gamma space), per-strand
  Variation, TipLift, Saturation, then the game's grass fog. Fragment shader: writes the colour (opaque, no alpha test).
- F10: "GPU grass (cull + draw)". Log: "ready", per-frame catch counts (first 3 frames, then every 3600), the GPU's own counts
  through the non-waiting readback (clumps drawn / culled, strands near / far, overflow).

## Settings: [Shaders.Grass.GPUGrass] (all live)
| key | default | meaning |
|---|---|---|
| Enabled | false | the feature (needs the 0024 fork d3d9.dll, NVR's grass shaders, Grass DrawFirst on, Terrain Blending off: GrassFirst stands aside with it) |
| KeepCards | false | true = the cards stay, strands added round them (hybrid) |
| BladeDistance | 7000 | pieces whose nearest tuft is farther stay cards |
| Density | 1.0 | clumps per game tuft (1-6) |
| ExtraRadius | 60 | how far an extra clump stands from its tuft |
| Blades / BladesFar | 64 / 8 | strands per clump near / from LodFar (thin twigs near: ~1-2 units with Coverage 1) |
| LodNear / LodFar | 600 / 2500 | full count up to / far count from (smooth in between) |
| DetailDistance | 1200 | strands nearer are 5-triangle curves (near list), farther 1 triangle (far list) |
| Coverage | 1.0 | the clump's strands cover this x one card's opaque area (sets the width) |
| MinPixelWidth | 0.75 | narrowest strand (pixels) |
| HeightScale, LengthMin, TipReach, Spread, Bend, Kink, Droop, Flutter | 0.9, 0.4, 1.0, 0.15, 0.5, 0.5, 0.5, 0.15 | strand shape (NewGrass's meanings) |
| Variation, TipLift, Saturation | 0.2, 0.3, 1.0 | colour |
| MaxBlades | 524288 | strands per list a frame can hold (48 bytes each: 2 x 25 MB of video memory) |
| DebugView | 0 | 1 near red / far blue, 2 colour per game draw |
| Log | 0 | 1 = GPU counts every 300 frames |

## Test (bulk list fork2-grass-bulk.txt, og-goodsprings + og-desert)
off/on ABAB with F10 timing, dense3, hybrid, dbg-lod (the proof shot: every GPU strand red or blue), the camera pitched down 35
degrees off/on/debug (close-up). What "works" looks like is written at the top of the list. Things I could not check here (no game
run): the look against the cards (the risk the brief names: NewGrass's round 4 showed blades alone read as a different plant until
they were thin, many and in the card's outline; the defaults follow round 6's strand shape with 32 strands near and a coverage-matched
width far), and whether the game's scene target at the early grass call is the one the extension can write (multisampled or not:
both handled by the patch).

## Bulk run 1 (2026-10-27, og-goodsprings, 52ce2b6b / DLL 9a8ddb34 / fork CI 40445689167): FAILED, cause found and fixed
- Seen: "ready", "18 early grass draws caught (4104 tufts), their cards skipped; left to the game: 0" (the catching works), the draws
  were accepted (no extension error: the scene target and depth buffer pass the patch's checks), but the GPU counts every time "0 clumps
  drawn, 0 outside the view, 1 tufts past the fade start; strands near 0, far 0": nothing drawn, the cards skipped = bare ground.
- Cause: a BINDING CLASH. The extension binds a uniform buffer and a read-only storage buffer of the same slot number to the same
  DXVK resource (NvrExtBufferSlotBase + slot, d3d9_nvr_ext.cpp NvrBindResources: both go through bindUniformBuffer). The cull pass
  had its frame block on uniform slot 0 and the tufts on storage slot 0, the tufts bound last: the shader read the tuft buffer as its
  frame block. Its tuft count (frame byte 64) became the 5th tuft's w (size step ~1) and its density the 8th tuft's height (thousands),
  so every thread worked on tuft 0 as one of thousands of "extra clumps"; tuft 0 sat in a mixed draw past the (garbage) fade start, so
  all of them stopped there and only the c = 0 one is counted: exactly "0 / 0 / 1". (Hi-Z met the same clash on 10-26.)
- Proof without the game: `tests/gpugrass_cull_check.cpp` runs the real GpuGrassCull.spv on the GTX 1070 with plain Vulkan, fed as
  GpuGrass.cpp feeds it (game-style reversed-depth camera, 18 draws x 228 tufts packed as the game packs them). With the clash
  reproduced (`clash`): "0 clumps drawn, 0 outside the view, 1 past the fade start" = the bulk log. With separate bindings: every tuft
  accounted for (4104 of 4104: 1348 clumps drawn, 2519 outside the view, 237 past the fade start = the CPU's own count 237), 4729 near
  + 13303 far strands, near widths 0.55-3.75 units, heights 1-53 units, no NaN; Density 3: 4049 clumps, 14026 + 40055 strands.
- Fix (GpuGrass.cpp): one slot number per buffer (frame 0, tufts 1, tuft draws 2, draws 3, strands 4, counts 5), in the cull pass and
  the strand draws. Safety net added: the counts are read back every 60 frames (never waits); every tuft's own clump must end as drawn,
  outside the view or past the fade start, so if those add up to fewer than the tufts uploaded twice in a row, GPU grass turns itself
  off and the cards come back (logged "THE CULL PASS DID NOT SEE EVERY TUFT"). The fork is unchanged (same CI 40445689167 d3d9.dll).
  Suggestion for the fork (not done: it would touch every pass): BuildPipelineBindings could refuse a uniform buffer and a read-only
  storage buffer on the same slot.

## Bulk run 2 (2026-10-27, og-goodsprings, 3740f1de / DLL c9922cc8): counts right, NOTHING VISIBLE; cause found offline and fixed
- Seen: the GPU counts were right ("3648 tufts uploaded, 853 clumps drawn, 1054 outside the view, 1741 past the fade start; strands near
  616, far 7560"), "GPU grass (cull + draw)" 0.07-0.11 ms, but no strand on screen (cards skipped = bare ground); the DebugView 1 shot
  differed from the plain one in 19 pixels (2 red, 0 blue).
- Offline: `tests/gpugrass_draw_check.cpp` runs the whole GPU path (the cull pass, then both indirect strand draws with the real
  vertex and fragment shaders) on the GTX 1070 with plain Vulkan into a 1920x1080 target with a reversed-depth buffer cleared to 0:
  with the game's GREATER_OR_EQUAL the strands cover 226,922 pixels (169,729 near / red, 57,193 far / blue; Density 3: 456,555), with
  LESS_OR_EQUAL 0 pixels. So the shaders, the counts and the indirect draws are right; the depth compare decides everything.
- Cause (the one integration value that was not read from what the GPU uses): the compare came from the game's own render-state cache
  (NiDX9RenderState::GetRenderState(D3DRS_ZFUNC)). The reversed depth buffer is a renderer setting (NiDX9Renderer::m_fZClear = 0, NVR's
  RenderManager::IsReversedDepth) whose GREATEREQUAL reaches the device past that cache (NVR's own probes always read the device and
  see 7), so the cache's LESSEQUAL rejected every strand against the cleared depth; the few that passed were drawn over nearer things.
- Fix: the compare is the DEVICE's D3DRS_ZFUNC, checked against the projection the strands are drawn with (z/w falling with distance
  = reversed = GREATER(EQUAL), else LESS(EQUAL)); if the two disagree the projection's is used. The first three frames log the target
  (size, format, samples), the depth buffer, the viewport, the device's and the cache's ZFUNC, the projection's direction and the
  compare chosen, so a next failure shows its cause in the log.

## Missing / next (session-hours)
1. In-game look + cost tuning from the bulk run (Coverage, Blades, colour pick; maybe the card's top-decile colour as round 6
   suggested): 1-2 h.
2. Strands take the card texture's own colour along their line (bind the grass atlas in the vertex/fragment stage; DxvkCompute::
   PreLoad first), grass collision (the push texture in the cull pass), the sandstorm's grass gusts (c19.y/z of the card shaders),
   NVR's sun shadows on the strands (the cascade atlas in the fragment shader; the game's cards get none): 2-4 h.
3. Placement on the GPU from the land itself (LTEX layers + TESGrassAreaParam grids + land heights uploaded per loaded cell): grass
   denser than the game places it and past its grass radius, independent of iMinGrassSize's CPU cost: 8-12 h.
4. Occlusion culling of clumps against last frame's depth (Hi-Z): only if the cost needs it (the depth test already skips hidden
   pixels; vertex work is small): 2-3 h.
5. A native D3D9 version is NOT possible in this form (no compute, no indirect draws); NewGrass (core/NewGrass.h) is the D3D9 route.
