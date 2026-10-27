# Persistent wetness and snow cover (fork round 2, 2026-10-27)

Branch `deep/fork2-wetsnow` (from `mine/nvr-dxvk-compute` 34a8df4d). No DXVK fork patch: plain D3D9, so it works on native
D3D9 (how the user plays) and through DXVK alike. Not tested in the game yet (the coordinator's bulk test does that).

## What it does (plain English)
Today rain wetness, puddles and snow cover are ONE amount for the whole world that rises and falls over game time. With the new
settings on, NVR keeps a map round the player (about 117 m across) that remembers, spot by spot, how wet the ground is, how full
its puddles are and how deep the snow lies:
- rain and snow build up only where they can reach (the top-down "ortho" map the rain and snow effects already use decides what is
  the top surface; ground under a roof stays dry, as before);
- after the rain each spot dries on its own: sunlit ground first, shaded ground (behind walls, under trees, found from the heights
  in the map, towards the sun) and everything at night about three times slower; puddles take longer than wet ground;
- snow melts the same way, gathers more in hollows and against walls (drifts), and the player's feet press a trail into it;
- it stays put: look away and back, and the same spots are wet or snowy. The map belongs to one exterior worldspace; another
  worldspace, a loaded save or a jump of more than 72 game hours starts it again;
- outside the map the old single amount is used (it follows the same rules for an open, sunlit spot, so there is no edge).

With both settings off (the default) nothing of this runs, nothing is allocated, and WetWorld / SnowAccumulation draw with their
old techniques: the compiled old techniques were checked byte for byte against the previous effect files (D3DX 9.43, both
FORWARD_SHADOWS values): identical.

## Settings (all off / unchanged by default)
`[Shaders.WetWorld.Main]`
- `Persistent = false` — the feature for rain.
- `PersistentWetTime = 0.25` — game hours of rain for open ground to get fully wet.
- `PersistentDryTime = 1.5` — game hours for wet ground in full sun to dry.
- `PersistentPuddleFill = 0.5`, `PersistentPuddleDry = 4.0` — the same for puddles.
- `PersistentShade = 0.35` — shade (and night) dries at this share of the sunlit speed.
- `PersistentMapSize = 8192` (units across), `PersistentMapResolution = 1024` (texels; 8 units a texel, 16 MB) — shared with snow.
- `PersistentDebugView = 0` — 1: the map top-down in the top-left corner; 2: its values painted over the world (test only).

`[Shaders.SnowAccumulation.Main]`
- `Persistent = false` — the feature for snow; the existing `Increase` / `Decrease` set its speed (open sunlit ground).
- `PersistentShade = 0.35`, `PersistentDrifts = 0.5`, `PersistentFootprints = 0.6`, `PersistentFootprintSize = 14`.

## How it works
- `src/effects/CoverMap.h/.cpp`, `Effects/CoverMap.fx.hlsl`, `Effects/Includes/CoverMap.hlsl`. An effect of its own ("CoverMap",
  registered like the others but with no settings section, so always on when its file loads; reached through `TheCoverMap`, not
  added to Effects.h, so no class layout any other file sees changed).
- Two A16B16G16R16 render targets (A16B16G16R16F if the first is refused), ping-pong. r = wetness, g = puddle water, b = snow,
  a = the top surface's height as last seen in the ortho map (1-unit steps round a reference height; 0 = never seen).
- The map is world-fixed and wraps round (a spot's texel = its world texel index modulo the size), so it is never copied when the
  player moves: each update works out per texel whether it still holds the same spot as last time (window origin then and now);
  texels whose spot left the window start from the open-ground values.
- The update pass (one full-screen draw into the other map, VPOS addressing) runs every 4th frame while the ortho map is drawn,
  plus whenever game time has gathered enough change, a footstep happened or the map has to start. Per texel: the height from
  the ortho map (two rounds through its inverse matrix, because the ortho map looks down at a slight slant), sun or shade (a
  6-tap march towards the sun over the map's own heights, 12 to 384 units), drifts (4 neighbours' heights), the rates, the
  footprint capsule (the player's path since the last update, only where the feet were on that spot's top surface).
- The CPU keeps the open-ground model (one open spot, sunlit by day), upper bounds over all spots (so WetWorld / SnowAccumulation
  keep drawing until the last wet / snowy spot is gone, then stop as before), and gathers game time until each change is at least
  24 steps of the 16-bit map (slow melting in the shade would otherwise round away).
- Readers: WetWorld techniques 5 and 6 (FastPaths, with / without the fixed reflection trace) take the flat ground's wetness and
  the puddle fill from the map instead of the rain fade and `TESR_WetWorldData.z`; ripples, splashes and wall streaks still follow
  the rain falling now. SnowAccumulation technique 1 takes the snow amount per spot (and none where it is 0). Each adds one
  bilinear tap: WetWorld 1666 -> 1683 instruction slots (fixed trace 1914 -> 1930), SnowAccumulation's Snow pass 597 -> 611.
- Neither persistent technique is a Vulkan pass (their ExtRender only takes the old techniques), so with NvrDxvkPasses on they
  run as D3D9 passes through DXVK; the old techniques keep their Vulkan ports.
- Unchanged on purpose: `TESR_WetWorldData` itself (the PBR, terrain and skin shaders read it for their own wet look): they
  still dry when the rain stops. A next step could feed them the open-ground value.

## Expected cost
- Off: zero (no allocation, no pass, the old techniques).
- On: about 0.1-0.2 ms per map update at 1024x1024 on a GTX 1070 (estimate, ~13 point taps and ~100 ALU per texel), every 4th
  frame or so: ~0.05 ms a frame on average, 16 MB of video memory. The WetWorld pass keeps running after the rain until the last
  spot dried (up to PersistentPuddleDry / PersistentShade = ~11 game hours with the defaults, ~23 real minutes at timescale 30),
  at its usual cost (~1.7-2.7 ms in rain at 1440p; less when dry, FastPaths exits early on dry pixels).

## Testing
Bulk list `work\bench\foundations\lists\fork2-wetsnow-bulk.txt`, scene og-desert (ForceRain / ForceSnow, timescale 600 so the
changes show in seconds). See its header for what each shot should show. The one debug shot (PersistentDebugView 1 after the
footprint step) proves the map holds data: snow white, the trail a dark line, ground heights as grey stripes.

## Missing / next steps (session-hours)
1. In-game check of the bulk list, then tune the defaults (dry times, shade factor, drift strength) by eye: 1-2 h.
2. The sun/shade test uses only the map's own heights: shade from things the ortho map does not draw (actors, small clutter) or
   from far hills is not seen. Using the sun cascades / terrain shadows instead: 2-3 h.
3. Feed the open-ground (or per-spot) wetness to the PBR / terrain / skin wet looks so objects dry with the ground: 1-2 h.
4. Footprints are a smooth trail, not single prints; prints from the feet bones and other actors' steps: 2-4 h.
5. A first-snow-on-wet / melt-water link (melting snow wets the ground): 1 h.
6. The map is kept across a visit to an interior (same worldspace) and the time spent inside dries it, but rain that the game
   reports only outdoors is not seen while inside: a check of what the weather reports in interiors: 0.5 h.
