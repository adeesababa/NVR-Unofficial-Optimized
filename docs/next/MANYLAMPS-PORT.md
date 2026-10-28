# ManyLampsOnePass / ManyLampsGlow / FoldFog port (2026-10-28)

Source: Conor (mrfury97)'s pull request on his fork,
https://github.com/mrfury97/NVR-Unofficial-Optimized/pull/1 (branch `manylamps-one-pass`, base `deep/fork-combo-1026`,
commits 82ef371b, 7127ac3d, 3e73bf06). He measured about 2-3 ms faster frames in the Vault 21 lobby on a 4070 Ti SUPER
(world draws 8,677 -> 6,015).

Branch: `claude/manylamps-port` (worktree `H:\_NVR-manylamps`, junction `work\NVR-manylamps`), on the installed
`claude/p84v3-tessring` 5c547109. Nothing pushed, nothing installed, no comment on the PR.

## What the switches do (Conor's design, unchanged)

| switch | where | default | what |
|---|---|---|---|
| ManyLampsOnePass | `[Main.Main.Misc]` | false | restart to come on (its shaders), then live. Shiny objects lit by up to 24 lamps drawn in ONE pass. NVR uploads the object's lamps into pixel constants c37-c63, c76-c87 and c98-c107 (`core/ManyLamps.h`); ConstantFilter's pixel hook puts the block back after the game zeroes c39-c86 for each draw. Shaders: ObjectTemplate `MANY_LAMPS` (SLS2025-2027.vso, SLS2034-2036.pso) + `Includes/ManyLightsDirect.hlsl`. DXVK only. |
| ManyLampsGlow | `[Main.Main.Misc]` | true | live, only while ManyLampsOnePass is on. NOPs the `bGlowMap` test at 0x00BE0134, patches the emittance range test at 0x00B7BAFA (BT table), binds the glow map on s4 before each glow one-pass draw. |
| FoldFog | `[Main.Main.Misc]` | true | live. An opaque object whose passes are <lit pass> <env pass> <fog pass> gets `bNoFog` on the lit pass; the fog pass is not drawn. Works on native D3D9 too. |

## What changed in the port

Conor's three commits cherry-picked; conflicts resolved by keeping both sides (ConstantFilter's DeferredResync from 10-27
next to his `PixelWriteAfter`; Render.cpp's `LinearScene::BeginFrame` next to `FoldFog::BeginFrame`, CRLF kept). Then one
port commit:

1. **ManyLampsOnePass switches the one-pass limit on by itself.** In his PR it needed `LampsInOnePass = true` as well. In this
   build LampsInOnePass is developer-only and off (LinearScene sums the game's passes exactly, so its picture reason is gone), so
   a player turning on ManyLampsOnePass in the menu would have got nothing. Now: ManyLampsOnePass on = one pass up to 23 + f
   lamps; off (live) = LampsInOnePass's own setting (off: the game's 2 + f). That also makes the live A/B "game's passes vs
   one pass".
2. **ShaderRecord:** LinearPreDecode's define scan read only the first 44 defines; MANY_LAMPS_ON moves LIN_PREDECODE_ON one
   later, so it now reads the whole list (63), like the LinearScene scan.
3. **Credit** to Conor (mrfury97)'s fork in ManyLamps.h, FoldFog.h, LampsInOnePass.h (glow) and ManyLightsDirect.hlsl, and in
   the toml text.
4. **ObjectTemplate.hlsl:** Conor's glow term (GlowMap on s4 + emittance, SI LIGHTS >= 4 shaders) is compiled only with
   MANY_LAMPS_ON. Before, it changed SLS2030 / SLS2033 / SLS2036 even with ManyLampsOnePass off. Only the glow one-pass passes
   use those, and they never run without ManyLampsGlow, but now the default shaders are byte-identical to 5c547109.
5. **toml text** for ManyLampsOnePass: no longer "with LampsInOnePass on", 3 to 24 lamps, lamps 7-24 without a cube shadow,
   "same picture with LinearScene".

## Checked against the current build

- **LinearScene:** the MANY_LAMPS shaders are ObjectTemplate variants, so they get `LINEAR_SCENE_ON` / `LinearSceneOut` like the
  other templates and ShaderRecord notes them as capable (checked in the compiled constant table: `TESR_LinearLighting` c191 is
  there). The lamp colours are decoded on the CPU with `LinearPreDecode::Gamma`, the gamma the shaders use (PBR.cpp sets it each
  frame, PreDecode on or off). LinearScene's own writes (c0 in its convert pass, c191 per draw) do not touch the lamp block, and
  its convert pass restores the bound shader.
- **FoldFog vs LinearScene:** the object shader fogs in the stored curve (`lerp(finalColor, fogColor, fog)` after LinEncode),
  then LinearSceneOut decodes the whole colour, so the folded fog is the same lerp the fog pass did. The only difference is the
  one Conor describes: the env pass's reflection is fogged once instead of twice. As a bonus, the fog pass is a non-capable
  draw inside the main list, so skipping it can only save LinearScene conversions.
- **LampsInOnePass / LampSums / HighlightSums / FoldHighlights:** LampSums, HighlightSums and ExactPassSums are off under
  LinearScene (PassSums returns 0) and work on lamp/highlight runs of multi-pass objects; FoldHighlights folds highlights of
  multi-pass objects. A one-pass object has none of those passes, so ManyLampsOnePass only removes work from them.
- **GameEyes per-draw fix:** stands aside under LinearScene; with LinearScene off it swaps only eye draws to the game's shader.
  ManyLamps decides "our shader" at the bind (record name SLS2034-2036 + NVR record), and an upload during an eye draw is ignored
  by the game's shader: no interaction.
- **Defines cap:** 64 (62 entries). Worst case counted: template defines (<= 6) + interior overrides + 43 appended = under 62.
- **Registers:** constant tables of SLS2034/2035/2036 (exterior and interior variants, compiled offline the way the game does):
  nothing else in c37-c63, c76-c87 or c98-c107. Shadow.hlsl's TESR_InvProjectionTransform / TESR_InvViewTransform (c100-c107)
  are not referenced by these pixel shaders (not in their tables). LinearPreDecode copies c64-c75, LinearScene c191: clear.
- **ConstantFilter / SkipRedundantConstants:** the detours are installed whatever the setting (`Hooked` set there); the block
  restore goes through the same filter, so its cache stays right.
- **Native D3D9:** `ManyLamps::ShadersWant()` needs the DXVK runtime string: MANY_LAMPS_ON 0, `Active()` false, the compare
  limit and the glow patches never applied, `PixelWriteAfter` stays null. FoldFog is a pass-list change and runs there.
- **Shaders with ManyLampsOnePass off** (the default): see the offline check below.

## Offline shader check

All 222 template shaders (ObjectTemplate, ParallaxTemplate, SkinTemplate, TerrainTemplate; exterior and interior records) were
compiled the way the game does (d3dx9_43, the defaults): **ManyLampsOnePass off = byte-identical to 5c547109** (after item 4).
With it on, SLS2034/2035/2036 grow to 3,420-4,753 slots and 25-27 temps (DXVK only, as designed) and all compile. The on-state
hashes are the same before and after item 4, so the game run below tested exactly the shaders in this commit.

## In-game A/B (slot A, NVR DXVK fork = the installed zresolve d3d9, user's toml + ManyLampsOnePass true)

Run `bench\autotest\20261028-155410-slotA` (pictures `H:\_NVR-autotest-shots\20261028-155410-slotA`), list
`bench\foundations\lists\manylamps-1-dxvk.txt`, toml `bench\deep-roadmap\tools\run-toml-manylamps-20261028.toml`. One launch,
every switch toggled live. **CPU-bound:** slim slot 960x540, and the user's toml has FSR at RenderScale 0.67, so the world is
drawn at about 640x360. The game thread (outside D3D9) takes most of the frame and GPU busy is lower. Hidden Valley sat at the
200 fps cap (5.00 ms) in every state.

Per shot: DXVK PROFILE windows (the window that straddles a switch is left out) and census world draws (NVR's count, taken
before FoldFog).

| spot | state | frame ms | game thread ms | GPU busy ms | GPU world ms | Vulkan draws | census world draws |
|---|---|---|---|---|---|---|---|
| Lucky 38 floor | ManyLampsOnePass on (x3) | 5.22-5.30 | 4.47-4.53 | 4.45 | 3.0-3.5 | 2,000-2,050 | 1,774-1,788 |
| | off (x2) | 6.72-6.88 | 5.72-5.82 | 4.80 | 4.16 | 3,472 | 4,030-4,067 |
| | on, ManyLampsGlow off | 5.59 | 4.75 | 4.68 | 3.73 | 2,332 | 2,015-2,033 |
| | on, FoldFog off | 5.40 | 4.60 | 4.54 | 3.14 | 2,136 | (same) |
| | all off | 6.89 | 5.86 | 4.83 | 4.07 | 3,556 | 4,030-4,067 |
| Vault 21 lobby | on (x3) | 6.67-7.14 | 5.77-6.16 | 5.28 | 3.7-5.3 | 3,414 | 1,911 |
| | off (x2) | 13.00-13.06 | 11.10-11.14 | 6.21 | 8.0-8.1 | 8,440-8,452 | 9,548-9,634 |
| | on, glow off | 9.05 | 7.81 | 5.81 | 6.86 | 5,381 | 3,845-3,880 |
| | on, FoldFog off | 6.87 | 5.90 | 5.29 | 4.39 | 3,502 | (same) |
| | all off | 13.03 | 11.17 | 6.23 | 8.75 | 8,473 | 9,548-9,634 |
| Hidden Valley bunker | on (x3) | 5.00 (cap) | 3.08-3.17 | 3.53 | 1.21-1.26 | 736-750 | 389-392 |
| | off (x2) | 5.00 (cap) | 3.92-4.00 | 3.71 | 1.36 | 1,312 | 1,366-1,378 |
| | all off | 5.00 (cap) | 3.87 | 3.66 | 1.32 | 1,316 | |

- **Vault 21: frame 13.0 -> 6.9 ms (-47 %), census draws 9,600 -> 1,900, Vulkan draws 8,450 -> 3,410.** ManyLampsGlow alone is
  about 2 ms of that (9.05 ms with it off). Same direction as Conor's 4070 Ti SUPER numbers, and larger because this slot is more
  CPU-bound. Lucky 38: 6.8 -> 5.3 ms (-23 %). Hidden Valley: game thread 3.9 -> 3.1 ms (frame capped).
- **GPU time went down too** on this GTX 1070 at low resolution (GPU world: Vault 21 8.0 -> 3.7-5.3 ms, Lucky 38 4.1 -> 3.0-3.5).
  On 10-18, LampsInOnePass measured +1 ms GPU in the Ultra-Luxe at full resolution; that was not re-measured GPU-bound here.
- FoldFog: 105-127 fog passes a frame not drawn in Lucky 38, 60-90 in Vault 21, 10-16 in Hidden Valley; about -0.1 to -0.2 ms here.
- ManyLamps statistics: up to 23 lamps per draw in Vault 21, "lamps past 24 dropped 0.00" everywhere. Glow: s4 "not the glow map
  after the draw 0.00" (no lost binds).
- LinearScene: 2-3.6 conversions a frame, as before.

**Pictures** (PSNR / pixels more than 8/255 apart; noise between two identical states is about 41-45 dB / 0.2-0.7 %):

| pair | Lucky 38 | Vault 21 | Hidden Valley |
|---|---|---|---|
| on vs on, off vs off (noise) | 45.2-45.6 dB / 0.19-0.23 % | 43.0-44.0 dB / 0.27-0.37 % | 41.1-41.7 dB / 0.5-0.7 % |
| ManyLampsOnePass on vs off | 39.2-39.4 dB / 2.3-2.5 % | 40.2-40.5 dB / 1.8-1.9 % | 40.3-42.6 dB / 0.3-0.9 % |
| FoldFog off vs on | 44.2 dB / 0.39 % | 43.9 dB / 0.35 % | 43.8 dB / 0.13 % |
| ManyLampsGlow off vs on | 44.3 dB / 0.39 % | 43.9 dB / 0.42 % | 40.3 dB / 0.85 % |

- **FoldFog and ManyLampsGlow: within noise.**
- **ManyLampsOnePass: not identical.** The difference is small and local: mean luma -0.9 % (Lucky 38) and -0.6 % (Vault 21). The
  diff maps (`bench\deep-roadmap\evidence\manylamps\l38-onoff.png`, `v21-onoff.png`: top off / on, bottom red = on darker, green = on brighter) put it only on shiny trims: stair rails, window and
  door frames, a chandelier rim, one ledge. Most of these come out darker with it on, a few brighter; no whole wall or floor piece
  changes. Cause: with it off, those objects are multi-pass and FoldHighlights folds their highlight passes into the lamp passes
  (census: all ~909 highlight passes a frame in Lucky 38 were skipped by FoldHighlights). With it on they get the one-pass shader,
  which lights every lamp's diffuse and specular per pixel, the way 1-2-lamp objects always were. So "on" is the exact lighting
  and matches the neighbours that were already one-pass. This is not a LinearScene problem: LinearScene makes the multi-pass sum
  exact, but the folded highlight is an approximation of its own.

## Native D3D9 (run `bench\autotest\20261028-155804-slotA`, list `manylamps-2-native`, same toml, no fork)

Log: "ManyLampsOnePass: off (needs DXVK; the shaders keep 6 lamp slots)". No LampsInOnePass or ManyLampsGlow patch was applied,
and no crash. The on and off shots are the same (43.5 dB / 0.40 %, noise). FoldFog runs there too: 84 fog passes a frame not
drawn, off vs on 43.1 dB / 0.43 % (noise).

## Defaults

As in Conor's PR: ManyLampsOnePass **false**, ManyLampsGlow true (it only acts while ManyLampsOnePass is on), FoldFog **true**.
Nothing in the tests argues against them. Turning ManyLampsOnePass on by default is the user's call: the gain is large when the
CPU limits the frame rate, the picture changes slightly on shiny trims (exact instead of folded), and the GPU cost on a GPU-bound
rig at full resolution has not been measured on this build.

## Install (not done)

From `H:\_NVR-manylamps` (= `work\NVR-manylamps`), DLL sha256 F6B76462..., game folder `<FNV>`:

| from | to |
|---|---|
| `build\optimized\NewVegasReloaded.dll` | `<FNV>\Data\NVSE\Plugins\NewVegasReloaded.dll` |
| `build\optimized\NewVegasReloaded.pdb` | `<FNV>\Data\NVSE\Plugins\NewVegasReloaded.pdb` |
| `resource\NewVegasReloaded.dll.defaults.toml` | `<FNV>\Data\NVSE\Plugins\NewVegasReloaded.dll.defaults.toml` |
| `src\hlsl\NewVegas\Shaders\ObjectTemplate.hlsl` | `<FNV>\Data\Shaders\NewVegasReloaded\Shaders\ObjectTemplate.hlsl` |
| `src\hlsl\NewVegas\Shaders\Includes\ManyLightsDirect.hlsl` | `<FNV>\Data\Shaders\NewVegasReloaded\Shaders\Includes\ManyLightsDirect.hlsl` |

To try it, set `[Main.Main.Misc] ManyLampsOnePass = true` in the user's toml and restart: the new shaders compile once, and
after that the switch is live (menu or F10). If a shader change doesn't seem to take effect, clear
`<FNV>\Data\Shaders\NewVegasReloaded\Shaders\Cache\`. It needs DXVK; the user plays on the NVR DXVK fork, so that is covered.

## Left open

- GPU cost when GPU-bound (2560x1440, no RenderScale) is not measured on this build. Watch F10's "GPU world" line in Vault 21
  and the Ultra-Luxe.
- The trim difference is explained (exact vs folded highlights) but not proven pixel by pixel. A pair with FoldHighlights off
  would show whether "off" then matches "on".
- Lamps 7-24 of an object have no forward cube-map shadow (Conor's limit; the screen-space lamp shadows still cover them).
- Commit c2d69631 (the cherry-picked FoldFog) does not compile on its own (a leftover conflict marker, removed in cad05175). The
  branch tip builds.
