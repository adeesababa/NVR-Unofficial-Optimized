# Persistent wetness and snow cover (fork round 2, 2026-10-27)

Branch `deep/fork2-wetsnow` (from `mine/nvr-dxvk-compute` 34a8df4d). No DXVK fork patch: plain D3D9, so it works on native
D3D9 (how the user plays) and through DXVK alike.
Build (round 2): `G:\_NVR-build-scratch\fork2-wetsnow-out\NewVegasReloaded.dll`, sha256 8077F89EAEA07FF1..., VS2019 Release
Win32, no warnings in the new or changed files. Effects checked with the game's D3DX compiler (all FORWARD_SHADOWS /
REVERSED_DEPTH combinations compile); the old WetWorld / SnowAccumulation techniques still compile byte for byte identical to
before the feature. The wrap-round bookkeeping (which texel holds which spot after a move, the readers' addressing) was checked
against brute force offline (4000 random moves, 0 errors).
To install by hand: the DLL into `nvse\Plugins\`, and `Effects\CoverMap.fx.hlsl`, `Effects\Includes\CoverMap.hlsl`,
`Effects\WetWorld.fx.hlsl`, `Effects\SnowAccumulation.fx.hlsl` into `Shaders\NewVegasReloaded\Effects\` (+ the defaults toml).

## What it does (plain English)
Today rain wetness, puddles and snow cover are ONE amount for the whole world that rises and falls over game time. With the new
settings on, NVR keeps a map round the player (about 117 m across) that remembers, spot by spot, how wet the ground is, how full
its puddles are, how deep the snow lies and how hard it has been trodden:
- rain and snow build up only where they can reach (the effects' own top-surface tests, from the top-down "ortho" map, decide
  which pixels are the top surface; ground under a roof stays dry, as before);
- after the rain each spot dries on its own: sunlit ground first, shaded ground (behind walls, buildings, trees, found from the
  heights in the map, towards the sun) and everything at night about three times slower; puddles take longer than wet ground;
- snow melts the same way and gathers more in hollows and against walls (drifts); the player's feet tread a trail of compacted
  snow (greyer, flatter, the ground showing only in its middle; fresh snow fills it again);
- it stays put: look away and back, and the same spots are wet or snowy. The map belongs to one exterior worldspace; another
  worldspace, a loaded save or a jump of more than 72 game hours starts it again;
- outside the map, and on spots whose height it never saw, the open-ground amounts apply (the old single amount, following the
  same rules for an open, sunlit spot), so there is no edge.

With both settings off (the default) nothing of this runs, nothing is allocated, and WetWorld / SnowAccumulation draw with their
old techniques (byte for byte the old programs).

## Settings (all off / unchanged by default)
`[Shaders.WetWorld.Main]`
- `Persistent = false` — the feature for rain.
- `PersistentWetTime = 0.25` — game hours of rain for open ground to get fully wet.
- `PersistentDryTime = 1.5` — game hours for wet ground in full sun to dry.
- `PersistentPuddleFill = 0.5`, `PersistentPuddleDry = 4.0` — the same for puddles.
- `PersistentShade = 0.35` — shade (and night) dries at this share of the sunlit speed.
- `PersistentMapSize = 8192` (units across), `PersistentMapResolution = 1024` (texels; 8 units a texel, 32 MB) — shared with snow.
- `PersistentDebugView = 0` — 1: the map top-down in the top-left corner; 2: its values painted over the world (test only).

`[Shaders.SnowAccumulation.Main]`
- `Persistent = false` — the feature for snow; the existing `Increase` / `Decrease` set its speed (open sunlit ground).
- `PersistentShade = 0.35`, `PersistentDrifts = 0.5`, `PersistentFootprints = 0.7` (how hard steps tread),
  `PersistentFootprintSize = 10` (half the trail's width, units).

## How it works
- `src/effects/CoverMap.h/.cpp`, `Effects/CoverMap.fx.hlsl`, `Effects/Includes/CoverMap.hlsl`. An effect of its own ("CoverMap",
  registered like the others but with no settings section, so always on when its file loads; reached through `TheCoverMap`, not
  added to Effects.h, so no class layout any other file sees changed).
- Two pairs of A16B16G16R16 maps (A16B16G16R16F if refused), ping-pong, both written in one pass (two render targets):
  main (the readers'): r wetness, g puddle water, b snow, a trodden snow; aux: r the top surface's height (1-unit steps round a
  reference height; 0 = never seen), g sun/shade at the last update, b where the height came from (ortho map / camera).
- The map is world-fixed and wraps round (a spot's texel = its world texel index modulo the size), so it is never copied when the
  player moves: each update works out per texel whether it still holds the same spot as last time (window origin then and now);
  texels whose spot left the window start from the open-ground values.
- Heights. (1) The ortho map, where it holds something on the spot (two rounds through its inverse matrix: it looks down at a
  slight slant). It holds NO LAND at all: evidence\orthomap-LOG.md (2026-10-17) found every land quad rejected by its multibound
  frustum test, so it holds only statics and trees; and it is a square turned 45 degrees (its up axis comes from the world's up
  and its slanted direction (0.05, 0.05, 1)) fitted round the view 10-3000 units ahead, so the map's corners and the ground behind
  the camera lie outside it. Round 1's debug view showed exactly that: heights only on the plateau's statics and the plants. (2)
  Since round 2 also the camera's depth: the spot at a guessed height is put on the screen, the depth there gives the surface on
  that line of sight, three rounds; accepted only when that surface point lies on the spot (within 1.5 texels: not an occluder),
  faces up (not a wall or an underside), is not the sky and not the weapon. A height from the ortho map is not replaced by the
  camera's (a roof seen from above beats the ground under it seen from the side). Spots never seen keep no height: they count as
  sunlit, no drift, so they follow the open-ground amounts exactly.
- The update pass (one full-screen draw into the other pair, VPOS addressing; 646 instruction slots) runs every 4th frame while
  the ortho map is drawn, plus whenever game time has gathered enough change, a footstep happened or the map has to start. Per
  texel: heights, sun or shade (a 6-tap march towards the sun over the map's own heights, 12 to 384 units), drifts (4 neighbours'
  heights), the rates, the footprint capsule (the player's path since the last update, only where the feet were on that spot's
  top surface): trodden = max(trodden, press), snow x (1 - 0.15 press); fresh snow fills the trail (trodden - 3 x snow gained),
  no snow means no trail.
- The CPU keeps the open-ground model (one open spot, sunlit by day), upper bounds over all spots (so WetWorld / SnowAccumulation
  keep drawing until the last wet / snowy spot is gone, then stop as before), and gathers game time until each change is at least
  24 steps of the 16-bit map (slow melting in the shade would otherwise round away).
- Readers: WetWorld techniques 5 and 6 (FastPaths, with / without the fixed reflection trace) take the flat ground's wetness and
  the puddle fill from the map instead of the rain fade and `TESR_WetWorldData.z`; ripples, splashes and wall streaks still follow
  the rain falling now (1666 -> 1683 instruction slots; fixed trace 1914 -> 1930). SnowAccumulation technique 1 takes the snow
  amount per spot (none where it is 0) and draws trodden snow compacted: normal-map bumps x (1 - 0.75 t), colour towards a grey
  of its own brightness (x0.56-0.62, 80 % at t = 1), no sparkle, rim light x (1 - 0.6 t), coverage cut only where
  t x (0.7..1.3 noise) passes 0.5..0.9 (the ground in the middle of a hard-trodden trail). Snow pass 597 -> 736 slots.
- Neither persistent technique is a Vulkan pass (their ExtRender only takes the old techniques), so with NvrDxvkPasses on they
  run as D3D9 passes through DXVK; the old techniques keep their Vulkan ports.
- Unchanged on purpose: `TESR_WetWorldData` itself (the PBR, terrain and skin shaders read it for their own wet look): they
  still dry when the rain stops. Also unchanged: the old SnowAccumulation roof test, which leaves plain land bare (the ortho map
  holds no land, so on land the test 'is this pixel the top surface' fails); persistent snow shows the same bare land.

## Round 1 bulk test (2026-10-26 23:39, og-desert, native D3D9, DLL 7CF6CA24) and round 2 changes
- Worked: map 1024x1024 made, first update with the ortho map in; F10 'Cover map update' 0.03-0.05 ms average (p90 0.13);
  ground dries over time (dry-new-40min vs dry-old differs on 69 % of pixels, 80min 26 %); snow builds; the trail shows.
- Fix 1, the trail was a near-black bare strip: footprints removed snow (S x 0.4), and the snow's coverage pattern turns thin
  snow into bare ground; next to bright snow the exposure makes bare desert ground near-black. Now trodden snow is its own
  channel and stays snow (see Readers); trail 20 units wide (was 28) with softer edges.
- Fix 2, large hatched (was red) areas in the debug map: explained above (no land in the ortho map; its 45-degree square fitted
  ahead of the view). Now heights also come from the camera's depth, and spots without a height follow the open-ground amounts
  (said in the legend: hatching, not red).
- Check 3, dry-new-40min brighter than dry-old (+1.7): the light, not the wet look. At timescale 600 each 300-frame shot is ~50
  game minutes of a falling afternoon sun: the ground drops ~3/255 per shot whatever the wetness (72.5, 69.5, 66.3, 64.1, 61.9,
  61.0 for shots 01-06; the mountains drop with it), and old vs new in the same rain (02 vs 03) shows the same step. WetWorld's
  default Amount = 0 ('darkness of puddles') does not darken wet ground; its wet look is sheen and sky reflection. The list now
  takes the old-behaviour picture right after dry-new-80min at the same game time (timescale back to 30 for it).

## Expected cost
- Off: zero (no allocation, no pass, the old techniques).
- On: measured 0.03-0.05 ms a frame on average (p90 0.13 ms) in round 1; round 2's camera heights add a few taps in a branch
  (estimate +20-30 %). 32 MB of video memory (two pairs of maps). The WetWorld pass keeps running after the rain until the last
  spot dried (up to PersistentPuddleDry / PersistentShade = ~11 game hours with the defaults, ~23 real minutes at timescale 30),
  at its usual cost (~1.7-2.7 ms in rain at 1440p; less when dry, FastPaths exits early on dry pixels).

## Testing
Bulk list `work\bench\foundations\lists\fork2-wetsnow-bulk.txt`, scene og-desert (ForceRain / ForceSnow, timescale 600 so the
changes show in seconds). See its header for what each shot should show. The one debug shot (PersistentDebugView 1 after the
footprint step): snow white, the trail a short brown line at the player, heights from the statics and from what the camera saw.

## Missing / next steps (session-hours)
1. Land in the ortho map (fix or bypass the multibound test for the ortho map, behind a switch): snow on plain land for
   SnowAccumulation old and persistent, puddles in land hollows for WetWorld, exact heights everywhere: 2-3 h, needs game checks
   (it changes the rain / snow effects' roof tests on land).
2. In-game check of round 2, then tune the defaults (dry times, shade factor, drift strength, trail look) by eye: 1-2 h.
3. Shade from things the map has no height for (actors, far hills): using the sun cascades / terrain shadows instead: 2-3 h.
4. Feed the open-ground (or per-spot) wetness to the PBR / terrain / skin wet looks so objects dry with the ground: 1-2 h.
5. Footprints are a smooth trail, not single prints; prints from the feet bones and other actors' steps: 2-4 h.
6. Melting snow wets the ground: 1 h.
