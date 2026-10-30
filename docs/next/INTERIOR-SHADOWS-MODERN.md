# Interior shadows: a modern design (2026-10-29, plan only, nothing built)

## The problem, said at the right level
What we see (Conor's photo, the intshadow bench runs): rings under cups, characters shaded from the inside, holes, Vault 21's
447 lights against 64 cube slots, and the lamps without a slot washing out the shadows of the lamps with one.

All of these come from **one assumption**: a shadow is a cube map that has to sit in a per-frame slot, bound to a draw
(6 samplers, s8 + s11-s15) and redrawn when something moves. So:
- slots are scarce, and the unshadowed rest fills in the shadows (holes);
- actors live inside the cube maps, so every lamp that reaches a person is redrawn and has a coarse self-shadow (shading from the inside);
- a cup is 2-3 texels in a 1 m cube face, so its shadow is bias and ring artefacts (rings).

Clustering, TwoSidedCasters and InvisibleOnly all work inside that assumption, and they measured as zero or negative. The fix
is to drop the assumption.

## What changed in modern games
Modern games (UE5 VSM, Last of Us II, Doom Eternal, RE Engine, Cyberpunk RT, Indiana Jones) handle many interior lights by
**splitting visibility by how often it changes**, not by light:

| layer | changes | modern technique | cost when nothing moves |
|---|---|---|---|
| static: walls, furniture x static lights | never (per cell) | cached shadow atlas / virtual shadow-map pages | 0 |
| moving: people, ragdolls, doors | every frame, small | capsule shadows (TLOU, UE), or a small dynamic overlay | small, scales with people, not lights |
| contact: cups, fingers, rims, creases | sub-texel | screen-space contact shadows per light | one short screen ray |
| optional: everything exact | n/a | ray-traced lamp shadows (our `rt-lamp-shadows`) | RT GPUs only |

FNV interiors suit this very well: the lights are static, the geometry is static, and the only things that move are people
and a few clutter objects. So **visibility in an interior is almost a constant**. Compute it once per cell and read it for free.

## The design: "static atlas + moving delta + contact"

### 1. Static shadow atlas: every light shadowed, none needs a slot
- Replace the 64 per-slot cube textures with **one big depth atlas** (D16 / R16F render target, 8192^2 = 128 MB, or 4096^2 = 32 MB) of
  **octahedral** maps, one rectangle per light. One octahedral square = a whole cube, one sampler, and seam filtering works.
- **Resolution follows how sharp the shadow must be**, not one size: a real lamp near the camera gets 512^2, a far one 256^2, and an
  invisible fill light (InvisibleLightRadius, a broad soft disc) gets 128^2, because its shadow is soft anyway. Vault 21: ~225 real at
  256-512 + 222 fills at 128 fits in 8192^2 with room to spare.
- **Drawn once per cell**, amortised: the existing StaticOnly cube-layer code renders N lights a frame (16 a frame = all 447 in ~30 frames,
  half a second after load, nearest first). It never redraws, because actors are no longer in it (section 2). The current static cube
  cache with its serial/hash becomes the atlas page table.
- Forward shaders: the 6 `samplerCUBE` become **1 atlas sampler + a per-light rect in constants**, so a draw can test every light the
  game gave it (ManyLamps lists included) and the 6-cube binding per draw goes away. That fixes the holes **by construction**: no lit
  light is unshadowed any more.
- Filtering: PCSS with the **physical lamp size** from LampFixtures (glowing part radius) for real lamps, wide for fills; normal offset
  scaled by the **world size of one atlas texel** at that distance (that is what kills the cup/ring acne at any resolution).

### 2. People leave the shadow maps: capsule shadows
- Every actor near a lamp becomes ~14-20 capsules from its bone matrices (head, torso, upper/lower arms and legs, hands, feet). BloodFlow
  and the skin code already read bones.
- Per pixel, per light: an analytic cone-vs-capsule occlusion (soft, correct penumbra for the lamp size, no maps, no draws, any number of
  lights). This is how Last of Us and UE do characters under many indirect/fill lights, and it is exactly the look you want from fill lights.
- This removes ActorsFromCubeMaps / RedrawActorsOnly: no lamp redraw when someone walks by (CPU win), and no coarse self-shadowing
  inside the character (shading from the inside fixed). Optional for the 1-2 lamps nearest the player: keep a small real dynamic map of the
  actor only (for exact silhouettes on walls), min'd with the static atlas.

### 3. Contact shadows per light
- A short screen-space ray (8-16 steps, ~30 cm, depth buffer) toward each of the top few lights a pixel uses. This catches the cups, plates,
  fingers, rims and creases the atlas can't resolve, and the cup rings disappear even where TwoSidedCasters doesn't help.

### 4. One visibility interface (where RT plugs in)
Layers 2-3 do not belong in the forward shaders: the shiny-object Interior records are already at the ~3,600 instruction limit
(POINT_SHADOW_FIT). They run as **one screen-space pass** that writes per-pixel, per-lamp visibility for the lamps the screen uses most.
That is the **format and the selection rt-lamp-shadows already built** (RG32F, 4 bits a lamp, NoteUse ranking, TracedMap slot->index,
reprojected next frame). Forward shader: `vis = atlas(light) * screenVis(light)`.
- No RT GPU (GTX 1070, iGPU): screenVis = capsules x contact.
- RT GPU: screenVis = traced rays (already working, 9 lamps); the atlas covers the rest. Both paths read the same way, so one shader works for both.

## Why this is "on par" and not just another tweak
- Every light the game uses is shadowed (447/447), with no slot ranking and no flicker from lamps swapping slots.
- Soft penumbrae sized from the real lamps, contact-hardening close up, soft occlusion from fill lights: the same three cues current games use.
- Cost goes **down** where it hurts: the static layer is 0 after the first half-second, people cost capsules instead of cube redraws, and
  draws bind 1 sampler instead of 6.

## Plan (session-hours; the real risk named)
1. **Octahedral atlas + amortised static fill**, interiors, behind `[Shaders.ShadowsInteriors.Atlas] Enabled` (off = byte-identical
   shaders, cube path kept as the A/B and the fallback). ~5-7 h. Risk: atlas memory in a 32-bit process on native D3D9 (fine under DXVK:
   default-pool RT, no sysmem copy); the 64-entry constant table needs a per-draw light->rect mapping inside the c174-c187 budget.
2. **Capsule shadows** from bones, with actors dropped from the atlas when it's on. ~4 h. Risk: creatures with odd skeletons (fallback: their
   bounding capsule), and holding objects (weapon = one extra capsule).
3. **Contact shadows** into the shared visibility buffer. ~3 h. Risk: screen-edge / thin-object halos (fade them out by depth thickness).
4. RT path reads through the same buffer (mostly merge work on the rt-test-merge branch). ~2 h.

Measure in one bulk batch after 1+2+3 (no per-step runs): intshadow_lookback.py shadow % / holes % at L38, V21, saloon, and Conor's cup
table (save needed); CPU ms in V21 vs combo-1028; atlas fill time after cell load.

## Rejected on the way
- More cube slots: memory and sampler count scale per light, and actors still force redraws.
- Clustering / borrowing a group's map: measured worse (clus3); a light's shadow from the wrong position is a wrong shadow.
- Baking fill lights into an irradiance volume: correct in principle, but it changes the lighting (the game's light lists), not
  only the shadows. A big look change; keep it for later. The 128^2 soft atlas entries give most of it for free.
- RT only: no RT on 32-bit drivers in-process, the helper needs an RT GPU, and it's 9 lamps today. It's the top tier, not the base.
