# Sun cascades fitted to the visible depth (SDSM), first step (2026-10-27, branch deep/fork2-sdsm)

Fork round 2 feature agent. Native D3D9 feature (works the same through DXVK and the NVR fork); no fork patch. Not run in game
by the agent (the coordinator's bulk test does that). Checked off-game:
- the DLL builds clean (no warnings in the new code); `SunDepthFit.fx.hlsl` compiles with D3DX9 (d3dx9_43, the game's compiler):
  3 techniques, ReducePS 127 slots, CountPS 16, CascadeViewPS 178;
- a stand-alone D3D9 test (GTX 1070, a synthetic 1283x721 depth buffer: sky, a far strip, ground, a near corner) ran the Reduce
  pass and the 21 counted draws exactly as SunDepthFit.h does: the block grid and **all 21 occlusion-query counts equal a CPU
  count**, on native D3D9 and through the fork's DXVK d3d9.dll (v3.0.1-11; the first query there takes ~17 ms, the pipeline
  compile, which the game simply waits out a frame or two);
- `SunDepthFit::Search` (the layout choice, built alone with SUNDEPTHFIT_MATH_ONLY) gives the same layouts as an independent
  Python model on the scenarios below.
The test sources (fitsmoke.cpp, searchcheck.cpp, fxcheck.cpp, sim_choose.py) are in `G:\_NVR-build-scratch\fork2-sdsm-tests`
(build with the NuGet D3DX package's include / lib x86; put d3dx9_43.dll, and for the DXVK run the fork's d3d9.dll, next to the exe).

## Settings ([Shaders.ShadowsExteriors.Main], all live, every Quality)
| key | default | what |
|---|---|---|
| FitToDepth | false | the fit. false = exactly as before: nothing is created, measured or changed |
| FitToDepthDebug | 0 | 1 = each sun cascade coloured as the lit shaders pick it + a bar of the layout (in use / normal); 2 = the measured block grid |
| FitToDepthLog | false | a log line whenever the cascade layout changes (splits, what was measured, why) |

## What it does
The four sun cascades normally cover fixed slices of the view: [near + 10, near + Distance] split by the CascadeLambda formula
(211 / 571 / 1659 / 6005 units at the defaults), each map fitted round the bounding sphere of its whole slice of the view frustum.
When what is on screen ends sooner (looking down, a street of buildings, a canopy, a canyon) the far cascades hold ground nobody
sees. FitToDepth measures how far the visible surfaces reach and gives those cascades to the depths that are visible.

1. **Measuring** (`SunDepthFit::Measure`, pre-tonemap chain right after CombineDepth): `Effects\SunDepthFit.fx` technique Reduce
   turns the linear depth buffer into a grid of 8 x 8 pixel blocks (farthest surface, nearest surface, sky flag; 320 x 180 at
   1440p). Technique Count is then drawn over the grid 21 times, each draw inside a D3D9 **occlusion query**: blocks past each of
   13 far levels (2^(-1/3) apart below the shadow distance, not under 250 units), blocks nearer than each of 7 near levels
   (doubling from near + 10), blocks with sky. The counts come back frames later through `GetData` without the flush flag: no
   `GetRenderTargetData`, no stall, the same on native D3D9 and DXVK. A ring of 4 query sets; if the GPU is further behind, that
   frame is not measured. (Chosen over the ring of SYSTEMMEM surfaces the brief suggested: on native D3D9 `GetRenderTargetData`
   can serialize the CPU and GPU, while queries are asynchronous by design, and the per-level counts give the quantized levels and
   an outlier allowance of one block in 8192 for free.)
2. **Choosing** (`SunDepthFit::Choose`, from `ShadowsExteriorEffect::GetCascadeDepths`): far reach F = the nearest far level at
   most one block in 8192 goes past; near bound = the farthest near level no more than that start before. Candidate layouts keep
   the first j cascades exactly as they are and re-split the rest over [start of cascade j, F] with the CascadeLambda formula and
   lambdas 0.5 / 0.75 / 1.0 (j = 0 may start at the near bound). Each candidate is compared with the normal layout over the
   visible depths using the sphere radius GetCascadeViewProj builds (sqrt(z2^2 (tan^2 x + tan^2 y) + ((z2 - z1)/2)^2)): it is
   taken only if no visible depth gets more than 5 % coarser, some get at least 25 % finer and the view 3 % finer on average
   (log-depth weighted). The best wins; otherwise the normal splits.
   *Why not the textbook SDSM (re-split everything over [visible min, visible max])*: worked through for this cascade layout it
   makes parts of the view coarser, e.g. looking down it sharpened 0-1.6 m and made 1.6-3 m 19 % coarser; with the third-person
   player as the nearest thing on screen it made the player's own cascade 2.3x coarser. The rule above cannot do that.
3. **Air**: the volumetric light (and the fog volume on its grid) and the sandstorm read the cascades along each view ray from the
   eye. While one of them draws, the first cascade keeps starting at the eye; with sky on screen as well, the last cascade keeps
   reaching the shadow distance and only the first three are fitted.
4. **Latency / stability**: the measurement is 2-4 frames old, so it is used only while the camera has turned less than 3 degrees,
   moved less than max(100 units, 5 % of F) and kept its FOV since; otherwise, after a cell change or when nothing is newer than 30
   frames, the normal splits come back that same frame. More coverage is taken at once, less only after 8 frames in a row asking
   for it. Levels are quantized, so sphere sizes (and the texel snapping) hold still between changes; every change sets
   `ShadowManager::ForceAllCascades`, so LimitFrequency / staggered updates never mix cascades of two layouts.
5. **Casters** between the sun and what is seen are kept as before: the cascade cull volumes have no near plane
   (SetActivePlaneState(62)) and ShadowMap.vso pancakes casters to depth 0.
6. Unchanged around it: HandoverBlend bands (they follow the radii), PlayerInNear (applied after), CharacterShadowMap, SkipWhenFaded,
   SunCascadeSkipFlat / OverheadSunFix, the sun cross-fade. The outdoor lamp shadows' reach (TESR_ShadowFade.w) keeps the unfitted
   value.

## Expected results (offline, defaults: Distance 6000, lambda 0.9, 90 degree FOV; radius = map texel size x 768 at 1536)
| view (visible depths) | normal radii | fitted radii | where it is sharper |
|---|---|---|---|
| looking down 35 deg, 1st person (120..596) | 262 / 679 / 1979 / 7223 | 262 / 346 / 488 / 689 | 3-4 m 2x, 4-6 m 1.4x, 6-8.5 m same |
| looking down, 3rd person (120..946) | same | 262 / 406 / 668 / 1101 | 3-5 m 1.7x, 5-8 m same, 8-13 m 1.8x |
| wall 10 m away, sky, volumetric light on (15..751) | same | 262 / 679 / 866 / 7373 | 8-10.5 m 2.3x |
| wall 21 m, no air readers (15..1501) | same | 262 / 679 / 1077 / 1746 | 8-13 m 1.8x, 13-21 m 1.1x |
| canyon, no sky (15..2383) | same | 262 / 679 / 1979 / 2758 | 24-34 m 2.6x |
| open view to the horizon, any person | same | no fit | - |
The nearest cascade (0-3 m) never changes in these views; the gain is in the middle distance.

## Cost
GPU: Reduce reads every depth pixel once (64 point taps per block) + 21 tiny draws over the grid: expected about 0.05 ms at 1440p
(F10 "Sun depth fit"). CPU: two effect passes and 21 draws with one constant each, well under 0.1 ms. While a fit is in use the far
cascades are smaller, so fewer objects are drawn into them ("Sun cascades (CPU)" should go down); each layout change redraws all
four cascades once.

## Files
- `src/core/SunDepthFit.h` (new, header-only; included only by ShadowsExterior.cpp and ShaderManager.cpp, so nothing else rebuilds)
- `src/hlsl/NewVegas/Effects/SunDepthFit.fx.hlsl` (new runtime file; loaded by SunDepthFit.h itself, not a menu effect)
- `src/effects/ShadowsExterior.cpp` (settings, constants, GetCascadeDepths hook, lamp-shadow reach), `src/core/ShaderManager.cpp`
  (measuring passes after CombineDepth, debug view before the Debug effect), `resource/NewVegasReloaded.dll.defaults.toml`
- Test list: `work\bench\foundations\lists\fork2-sdsm-bulk.txt` (og-desert, og-goodsprings)

Built DLL: `G:\_NVR-build-scratch\fork2-sdsm-out\NewVegasReloaded.dll` (sha256 9D9591F94FD1F83E...), on mine/nvr-dxvk-compute 34a8df4d.
Install for a test: the DLL -> `nvse\Plugins`, the defaults toml -> `nvse\Plugins`, `Effects\SunDepthFit.fx.hlsl` ->
`Shaders\NewVegasReloaded\Effects\` (clear `Effects\Cache` if it seems not to take).

## Not done / next
- In-game check (the bulk list). Watch for: pumping (sharpness changing when the camera stops), a missing distant shadow for a
  frame or two after turning quickly toward far ground (the 3-degree guard should prevent it), the cost lines.
- Tight per-cascade bounds (the sphere round only the visible samples of each slice) were looked at: about 10 % in level views
  (the frustum's full width dominates every sphere at 90 degrees FOV), more only when looking down, and they would need a separate
  texel size per cascade in Shadow.hlsl / SunShadows.fx / SunCascades.hlsl (all derive it from the radius today). Not worth it yet.
- If the in-game test shows the gain is real: the outlier allowance and the 5 % / 25 % rules are constants in SunDepthFit.h,
  easy to tune (1 session-hour with one bulk run). Fork-side idea for later: do the reduction as one compute dispatch (fewer
  draws), only worth it if the F10 line shows the 21 draws matter.
