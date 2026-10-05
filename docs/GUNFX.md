# GunFX (experimental, since P67)

GunFX adds modern-shooter gun effects to Fallout: New Vegas. It is a small xNVSE plugin (`GunFX.dll`) that
works together with this build of New Vegas Reloaded.

| Effect | What you see | Menu switch |
|---|---|---|
| Muzzle puff | A small puff of smoke at the muzzle on every shot. | `Puff` |
| Heat smoke | A thin strand of smoke rising from the barrel during sustained fire. It stops when you stop firing, and the after-fire trail takes over. | `HeatSmoke` |
| After-fire trail | A thin, wispy trail rising from the barrel only once you stop firing, like a cigarette. It fades as the barrel cools and stops when you fire again. | `AfterFireTrail` |
| Ejection smoke | A burst of smoke from the ejection port with every casing, bigger when the barrel is hot. Bolt-action, lever and pump guns puff when the casing comes out. | `EjectionSmoke` |
| Barrel glow | The barrel glows after sustained fire and cools down slowly. | `BarrelGlow` |
| Heat haze | Heat shimmer around a hot barrel. It grows stronger and larger as the barrel heats up. | `HeatHaze` |
| Muzzle blast | A quick, wide ripple of heat shimmer at the muzzle on every shot, whatever the barrel heat. | `MuzzleBlast` |
| Volumetric smoke (prototype, P70) | Draws the smoke (heat smoke, after-fire trail, muzzle puff, ejection smoke) as continuous, soft volumes instead of particle sprites, so it never breaks into blobs while you move. | `VolumetricSmoke` |

Each gun builds its own heat. Switching guns does not carry heat over, and putting a gun away stops its smoke.
Everything works in first and third person, for your own gun. Other characters can get the muzzle puff
(`bPlayerOnly=0` in `[Puff]`); the rest is player-only for now.

> **Experimental.** GunFX has been tested on one PC only. Every effect is **off by default**, so installing P67
> changes nothing until you switch effects on.

## Install

GunFX is included in the P67 zip. It needs xNVSE and this build's `NewVegasReloaded.dll` (P67 or newer), which
draws the glow, haze and muzzle blast. The zip contains:

- `Data\NVSE\Plugins\GunFX.dll`: the plugin.
- `Data\NVSE\Plugins\GunFX.ini.defaults`: default settings. `GunFX.ini` is created from it at the first start and
  later only receives new settings, so your changes are never overwritten.
- `Data\Meshes\GunFX\`: the smoke effects.
- `Data\Shaders\NewVegasReloaded\Effects\BarrelHaze.fx.hlsl`: the haze and blast screen effect.
- `Data\Shaders\NewVegasReloaded\Effects\VolumetricSmoke.fx.hlsl`: the volumetric smoke (P70 or newer).
- `Shaders\ObjectTemplate.hlsl`: includes the barrel glow.

If you used the earlier test plugin **BarrelSmoke**, remove `BarrelSmoke.dll` from `Data\NVSE\Plugins`. Your
`BarrelSmoke.ini` and `Data\Meshes\BarrelSmoke` can go too, or move them to a backup folder. While both DLLs are
installed, GunFX stays off and says so in its log. GunFX takes over the per-gun files in
`Data\NVSE\Plugins\BarrelSmoke\Weapons\` by itself.

## Turning effects on and off

Open the NVR menu in game with the **O** key, then go to **Main > GunFX**. There is one switch per effect. They
apply immediately, so you can compare while firing.

One more switch, **EnergyWeapons**, decides whether energy weapons get GunFX at all. It is off by default, because
smoke and casing puffs can look out of place on lasers and plasma guns. GunFX recognises an energy weapon from the
weapon's own data: it uses the Energy Weapons skill, or it is an energy pistol or rifle. Modded weapons are
covered too.

## Fine-tuning: `Data\NVSE\Plugins\GunFX.ini`

Open it with Notepad. Every setting has a comment above it. Save the file while the game is running and the
change applies within a second. A few you are most likely to want:

- `[Wisp] fHeatPerShot`, `fCoolPerSecond`, `fHeatStart`, `fHeatFull`: how quickly heat smoke appears and how long
  it lasts. More heat per shot or a lower `fHeatStart` means smoke sooner. Faster cooling means it stops sooner.
- `[Wisp] fScale`, `fMaxRate`, `fOpacity`: thickness, density and visibility of the heat smoke. `iEmitters` (in `[Wisp]` and `[Trail]`) spreads 1-4 emitters along the barrel's path between frames, so the smoke stays one
  continuous plume while you move instead of breaking into separate blobs. `fStopDelay` is the
  seconds after your last shot that still count as firing (0 = the heat smoke keeps going until the barrel cools).
- `[Trail] fStartDelay`, `fMinHeat`, `fFullHeat`, `fMaxRate`: when the after-fire trail starts after your last shot,
  how warm the barrel must be, and how dense it gets. It uses the same barrel heat as the heat smoke.
- `[Puff] fScale`, `fBurstSeconds`: size of the muzzle puff.
- `[Ejection] fScale`, `fHotScale`, `fBurstSeconds`: size of the port burst, cold and hot.
- `[Glow] fStartHeat`, `fHeatSpan`, `fRadius`, `fLength`: when the glow starts, how many shots until it is full,
  and how much of the barrel glows.
- `[Haze] fStrengthPixels`, `fHeightPixels`, `fWidthPixels`: strength and size of the shimmer at full heat. Both
  grow with the barrel heat; `fMinSize` sets how small it starts (0.35 = a third of full size).
- `[Blast] fStrengthPixels`, `fRadiusPixels`, `fSeconds`: strength, size and length of the muzzle blast.

The look of each kind of smoke (`[Puff]`, `[Wisp]`, `[Trail]` and `[Ejection]`) is under "Look of the smoke" in its section:

- `fSize`, `fSizeVariation`: how big each bit of smoke grows.
- `fSmokeLife`, `fSmokeLifeVariation`: how long it lives.
- `fGrowSeconds`, `fShrinkSeconds`: how long it swells, and whether it shrinks at the end.
- `fRise`: how fast it rises.
- `fCurl`, `fCurlScale`: how much it swirls.
- `fOpacity`: how thick it is.
- `fFadeIn`, `fFadeStart`, `fFadeEnd`: when it appears and fades, as shares of its life.
- `fStartSize`, `fSpeed`, `fSpeedVariation`, `fSpread`: how each bit starts. Its starting size as a share of `fSize`,
  how fast it leaves the gun and how unevenly, and the angle it leaves at. A small start size and spread with many
  puffs a second give a narrow, connected strand at the barrel. Growth over the whole life with an early, gradual fade
  makes it widen and soften as it rises.

The game can only use a look that is stored in an effect file. So GunFX writes a copy of the effect file with your
values into `Data\Meshes\GunFX\Generated\` and uses that copy. Saved changes apply to the next smoke. Every
combination of values gets its own small file. You can delete that folder at any time; GunFX recreates what it
needs.

To move the smoke at the barrel tip, use `fOffsetX`, `fOffsetY` and `fOffsetZ` in `[Muzzle]`. The muzzle puff, the
heat smoke and the after-fire trail all use this one offset, so they always line up. The values are game units along
the gun's own axes, so they turn with the gun: Y is forward along the barrel, X is sideways, Z is up.

Other placement settings:

- `[Ejection]` has its own offsets for the port burst.
- `[Glow]`'s offsets move the glow, the haze and the muzzle blast.
- `sNode` in `[Puff]` or `[Wisp]` can attach that smoke to another node of the gun's model, for example
  `ShellCasingNode`. Blank means the barrel tip (`ProjectileNode`).

`GunFX.log` lists each gun's node names the first time you fire it. A gun that needs a different spot can get its
own `[Muzzle]` values in its per-gun file.

Testing aids: `[Glow] bPreview=1` keeps the full glow on after one shot, and `[Haze] bPreview=1` does the same for
the haze. `[Haze] bShowMask=1` paints the haze area red and the muzzle blast blue. Set them back to 0 afterwards.

## Volumetric smoke (prototype)

The game's particle smoke is made of flat sprites. Each puff is a separate picture, so when the gun moves the
smoke can break into blobs. With **Main > GunFX > VolumetricSmoke** on, GunFX keeps its smoke as chains of points
instead: the heat smoke and the after-fire trail as strands from the barrel, the muzzle puff and the ejection smoke
as short bursts (the puff blows forward out of the barrel, slows down and rises; during automatic fire the bursts
join into one stream). Each point rises, curls, slows down, grows (fast at first) and fades. NVR draws the chains as
soft volumes of smoke: with a lumpy outline and wispy edges that travel with the smoke, lit by the sun (the side away
from it darker; glowing when you look toward the sun) and the light around them, lit orange for a moment by each
muzzle flash, whiter when fresh and cool grey when thin, drifting with the wind outdoors, parting around you as you
walk through it, hidden behind walls, faded out close to the camera, and always behind your own first-person gun. The sprite version of a smoke is not
spawned while its volumetric version is on. Each smoke's own switch still applies (`HeatSmoke`, `AfterFireTrail`,
`Puff`, `EjectionSmoke`). Your own gun only; other characters' puffs stay sprites.

Each smoke's look still comes from its own section (`[Wisp]` for the heat smoke, `[Trail]`, `[Puff]`,
`[Ejection]`): size, start size, growth, life, speed, spread, rise, curl, opacity and fades. `[Volume]` scales
them for the volumetric version:

- `bHeatSmoke`, `bTrail`, `bPuff`, `bEjection`: which smoke turns volumetric (0 keeps that one as sprites).
- `bGunInFront`: your own first-person gun is always drawn in front of the smoke, so smoke never shows inside it
  (1 by default; 0 sorts smoke and gun by distance).
- `fExpand`, `fThinning`: how big the smoke ends up (it grows fast at first, then slower) and how much it thins
  out as it widens (0 keeps its opacity like the sprites).
- `bGunInFront`: your own first-person gun is always drawn in front of the smoke, so smoke never shows inside it
  (1 by default; 0 sorts smoke and gun by distance).
- `fSizeScale`: width of the heat smoke and trail compared with the sprite size (`fSize` times `fScale`; 0.25 by
  default, since a sprite's soft edges are mostly transparent). `fBurstSizeScale` does the same for the puff and
  the ejection smoke (1.0 by default).
- `fDensityScale`: opacity. `fOpacity` x `fDensityScale` is how thick the strand looks straight across where it
  starts (0.2 hides about a fifth of what is behind it, 0.8 about half, 2 nearly all). It thins out as it widens.
- `fRiseScale`, `fCurlScale`, `fDrag`: how it rises, bends sideways and slows down. `fSmooth` evens out the sharp
  kinks a quick or jittery gun movement leaves in a strand.
- `fDriftRight`, `fDriftAway`: the heat smoke and trail lean toward the right of where you aim and away from you,
  so they stay out of the middle of your view.
- `bWrapGun`, `fGunLength`, `fGunRadius`, `fGunSwirl`: the gun as a solid shape: smoke never stays inside it, the
  gun splits the trail as you walk or swing into it, smoke slides round the barrel, and swinging it through smoke
  leaves little whirlpools behind it (`fBodySwirl`, `fSwirlLife`: walking through smoke does the same behind you;
  the whirlpools carry each other, so a wake rolls up into eddies). `fBarrelSpread`: the
  heat smoke also rises off the hot barrel, not only the muzzle. The heat smoke and trail start a little way inside the
  barrel and flow out of its end (with the gun's real shape, GunFX finds where the barrel ends). `bGunShape`: the smoke flows round the gun's real shape (its sights,
  magazine, scope and bipod), read from its model the first time you hold it and turned into a distance map in the
  background (a fraction of a second; the tube stands in meanwhile, and the log says when the shape is ready); 0 = the
  plain tube. The barrel's direction comes from the muzzle node's
  own axis (the log says so once per gun). While the gun is nearly still (firing in place), the barrel eases fresh
  smoke out gently instead of flicking it, so its kicks don't throw the smoke around; moving it makes it firm again
  (walking forward it still eases the smoke out over a few frames; the puff leaves with the gun's own movement, so you
  don't run straight into it). Smoke rising off the barrel starts on its top, and smoke born inside your body's reach
  (the ejection port) drifts clear over half a second. Your body and the gun move the air around them as they move: smoke ahead of
  you parts before you reach it and slides past your sides (no last-moment shove).
- `fTendrils`, `fTendrilAngle`, `fTendrilSize`, `fTendrilSpeed`: thin wisps that peel off where a strand bends
  sharply while you move the gun, and fray from a strand your body splits (chance, how sharp a bend, thickness, speed). `fBurstDrag` is the slowing
  down of the puff and ejection bursts, `fPuffForward` how fast the puff blows forward out of the barrel,
  `iBurstPoints` how many points each burst starts with (more = a fuller cloud).
- `fNoise`: edge detail, the lumpy outline and wispy, eaten-away edges (more as the smoke ages).
- `fBrightness`, `fSelfShadow`: how bright it is, and how much darker its side away from the sun (indoors: its
  underside) and its thick inside are.
- `fFlash`, `fFlashRadius`, `fFlashSeconds`: the muzzle flash's orange light on the smoke (strength, reach, fade).
- `fTint`: colour by thickness and age (fresh smoke whiter, thin smoke cool grey; 0 = plain grey).
- `fDepth`: shading for a sense of depth, as if the smoke were round and lumpy (0 = flat).
- `fWind`, `fWindPickup`, `fWindDirectionOffset`: drift with the weather's wind outdoors (speed at full wind, how
  quickly smoke takes it on, a turn in degrees if it drifts the wrong way). `GunFX.log` lists the wind it reads.
- `bPlayerPush`, `fPlayerRadius`, `fPlayerHeight`, `fPlayerPush`, `fStir`: your body keeps the smoke out: no smoke
  stays inside it or runs through it (smoke ahead of you slides aside, a strand you walk through is split), some
  follows in your wake, and stirred smoke thins out.
- `fDiffuse`: stirred smoke spreads out like real smoke mixing into the air: smoke your body or the gun disturbs (or a
  whirlpool catches) billows wider, thins into a ragged veil, its points wander apart and strongly stirred spots peel
  off wisps (0 = off, 1 = normal, higher = more).
- `bUnzip`, `fFanSpeed`: walking into the heat smoke or trail splits it around you into two arms that fan out, like a bow
  wave (it splits where the air ahead of you starts to part; `fFanSpeed`: how fast the arms spread). `bUnzip` is on (1)
  or off (0).
- `bEnabled=0` in a gun's own file keeps that gun on the sprite smoke.
- `bDebug=1` draws it solid red, even through walls, to check where it is. Both `GunFX.log` and NVR's log also
  record where the smoke is for the first 20 seconds or so it is shown.

It costs a little GPU time only while smoke is on screen, and only for the part of the screen it covers.

## Per-gun settings: `Data\NVSE\Plugins\GunFX\Weapons\`

The first time you equip a gun, GunFX creates a file for it there, named after the gun (for example
`Assault Carbine.ini`). It lists every setting, each line starting with `;`. A `;` means "use the value from
`GunFX.ini`", so a new gun follows your main settings, and later changes to `GunFX.ini` still apply to it.

- **To give one gun its own value,** delete the `;` at the start of that line and change the value, for example
  `fScale=1.6` under `[Ejection]` for a bigger port burst on just that gun.
- **To go back to the main setting,** put the `;` back.
- **To start a gun over,** delete its file. It is recreated, all `;`, the next time you equip the gun.

Settings switch the moment you change guns, and saving a gun's file while it is equipped applies immediately.
Guns with the same name share a file. The menu switches always apply to every gun.

## Troubleshooting

- **Nothing appears.** Check that the switches under Main > GunFX are on, then look at `GunFX.log` in the game
  folder. Its first lines say whether GunFX loaded and hooked the game, and what it is doing.
- **No ejection smoke on one gun.** The gun's model may have no `ShellCasingNode`; `GunFX.log` says so.
- **Glow, haze or blast missing.** These need P67's `NewVegasReloaded.dll` and shaders. Delete the `Cache`
  folders inside `Data\Shaders\NewVegasReloaded\` if a shader seems stale.
- **To remove GunFX,** delete `GunFX.dll`, `GunFX.ini`, `GunFX.ini.defaults`, the `GunFX` folder next to them and
  `Data\Meshes\GunFX`.

## For developers of other NVR builds

GunFX is self-contained in the `GunFX/` folder: plugin source, a Visual Studio 2019 (v142) project, default
settings and the baked effect meshes. It talks to NVR through a few exported C functions:

- `GunFX_GetHeatV3(float out[10], const void* weaponNode)`: muzzle position (render-time space), heat, rearward
  axis and glow length/radius/forward reach, for the barrel glow in `ObjectTemplate.hlsl`.
- `GunFX_GetHazeV3(float out[16], const void* weaponNode)`: haze strength and size, the muzzle and axis in world
  space from the game loop (out[8..13]; the first-person model is in another space during NVR's render pass), and
  the muzzle blast (out[14..15]).
- `GunFX_SetSwitches(UInt32 bits)`: the menu switches. Bits: 0 puff, 1 heat smoke, 2 ejection smoke, 3 glow,
  4 haze, 5 blast, 6 energy weapons too, 7 after-fire trail, 8 volumetric smoke. Without calls, everything is on and
  only `GunFX.ini` decides.
- `GunFX_GetVolumeSmoke(float* out, int maxRecords, float params[8])`: the volumetric smoke, one record of 12
  floats per tube segment (absolute world space: both ends' position and radius, both ends' density, age, end
  weights and seed). Returns the record count (at most 384). NVR's world space is centred on the camera, so the
  positions are converted before use. `GunFX_GetVolumeSmoke2` gives 16 floats per record (adding the smoke's own
  noise coordinate and the age at both ends); `GunFX_GetSmokeFlash(float out[8])` the muzzle flash's position, light,
  colour and reach. `src/effects/VolumetricSmoke.h` (+ `.cpp`, `VolumetricSmokeQuads.h`) and
  `src/hlsl/NewVegas/Effects/VolumetricSmoke.fx.hlsl` draw it in two steps: one quad per segment adds its optical
  depth, the share the light reaches and the flash's light into `TESR_SmokeBuffer`, then one pass lights the total
  and lays it over the image.

Smoke looks: `BakeLook` in `GunFX.cpp` writes a copy of the effect NIF with the INI's emitter, grow/fade, gravity
and colour values into `Meshes\GunFX\Generated\` and spawns that copy. Writing live particle modifiers crashed the
game.

On the NVR side, the parts to copy are in the P67 and P68 commits:

- `src/NewVegas/Hooks/Render.cpp`: the `GunFXSwitches` and `BarrelHeat` namespaces, plus the short calls into
  them added to `SetShadersHook`, `RenderWorldSceneGraphHook` and `RenderFirstPersonHook`.
- `src/effects/BarrelHaze.h` and `src/hlsl/NewVegas/Effects/BarrelHaze.fx.hlsl`: the screen effect, registered in
  `ShaderManager.cpp`/`.h` and `Effects.h`.
- The heat block in `src/hlsl/NewVegas/Shaders/ObjectTemplate.hlsl` (constants c171-c173).
- The `[_Main.GunFX.Main]` section in `resource/NewVegasReloaded.dll.defaults.toml`.

`BarrelHeat::SetForDraw` uses this build's `ShaderSplit` context (world or first person) to tell the two passes
apart. A build without it needs its own flag, set around `RenderFirstPerson`.

Game addresses target FalloutNV.exe 1.4.0.525 and are listed at the top of `GunFX/src/GunFX.cpp`. The
weapon-fire and casing-ejection hooks chain whatever the call site called before, so other plugins hooking the
same calls keep working.
