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
- `[Wisp] fScale`, `fMaxRate`, `fOpacity`: thickness, density and visibility of the heat smoke. `fStopDelay` is the
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
  4 haze, 5 blast, 6 energy weapons too, 7 after-fire trail. Without calls, everything is on and only `GunFX.ini` decides.

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
