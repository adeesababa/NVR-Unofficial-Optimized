# New Vegas Reloaded - UNOFFICIAL Optimized

**A faster version of New Vegas Reloaded (NVR) that keeps the same look.**

New Vegas Reloaded makes Fallout: New Vegas look much better - shadows, fog, light rays, ambient
occlusion and more - but those effects cost a lot of frame rate. This build reworks how the effects
are drawn so they do the same job with much less work, and fixes a few visual bugs along the way.

### What you get
- **More FPS with NVR's effects on.** On the test PC (GTX 1070 at 2560x1440) the heavy effects
  all got cheaper - for example ambient occlusion went from about 2.3 ms to 0.5 ms per frame, god
  rays from about 1.6 ms to 0.5 ms, and SMAA and contact shadows by roughly 40-50%.
- **The same image.** Almost all of the speed-ups produce the same picture as before.
- **Bug fixes:** no more horizontal black lines on sun-lit surfaces, and no more flickering dashes
  on tree branches against the sky.
- **Optional extra speed** for weaker PCs (see below), each one a simple on/off switch.
- **Optional visual extras** (since P64): lit particles and blood, bounce light and contact-hardening
  shadows, all off by default (see below).
- **Experimental Gun FX** (since P67): muzzle puffs, heat smoke from a hot barrel, ejection-port smoke,
  barrel glow, heat haze and a muzzle-blast shimmer, each with its own switch under **Main > GunFX**,
  all off by default. Guide: [docs/GUNFX.md](docs/GUNFX.md).
- **New in P75, off by default:** lamp-by-lamp shadows indoors, a metal look for guns, lit "chrome"
  reflections, and a better bounce light (see below). **Fixed in P75:** the flashlight's bright rim
  around grass, bright colours changing hue, and shadows over B42 Optics scopes.
- **A built-in performance meter** (F10) that shows what each effect costs on your PC. While it is
  running, a small dim red dot and "PROF" show in the top-right corner of the screen. The log also gets
  frame-time percentiles and "1% low" figures, which are the best way to compare two setups.

> [!NOTE]
> **Experimental.** So far this has been tested on only one PC (NVIDIA GTX 1070, Windows 10).
> AMD/Intel cards, DXVK and many mod setups have not been tried; interiors have only had a short test. Keep a backup of your
> NVR files. Reports of what works and what breaks are welcome in this repository's issues.

### How to install
Since P45 the zip is a complete New Vegas Reloaded install: the plugin DLL, its settings file, all
shaders and all of NVR's textures. You need [xNVSE](https://github.com/xNVSE/NVSE); you do not need
to install New Vegas Reloaded separately first. (The textures are NVR's own, redistributed under the
same license; they will be removed from the zip if the original authors object.)
1. If you already have New Vegas Reloaded, back up `Data\NVSE\Plugins\NewVegasReloaded.dll`,
   `NewVegasReloaded.dll.defaults.toml`, your `NewVegasReloaded.dll.toml` (your own settings) and
   the `Data\Shaders\NewVegasReloaded\` folder. (The textures in the zip are the same as NVR's own;
   your custom LUTs in `Data\Textures\NewVegasReloaded\LUTs` are not touched.)
2. With the game closed, download the latest zip from **Releases** and extract it into your `Data`
   folder (or install it as a mod with your mod manager), overwriting when asked.
3. Start the game. Your own NVR settings are kept; without them, they are created from the defaults.
   Since the P75 update the zip also holds a ready-made shader cache, so the first start no longer
   spends minutes compiling shaders (settings that change the shaders rebuild only those, by themselves).

To uninstall, put your backed-up files back. If an effect looks wrong after updating, delete the
`Cache` folders inside `Data\Shaders\NewVegasReloaded\`, and look in the newest log (see **Logs**
below) for lines containing `error X`: they name a shader file that is missing or from another
version. Extracting a zip cannot remove files, so a shader left over from an older NVR that this
build no longer has (for example `Shaders\SKIN2012.pso.hlsl`) stays; it is harmless, or delete it.
Up to P39 the zips held only the changed shader files and missed `Effects\Includes\Normals.hlsl`:
on installs without our version, sun shadows and volumetric fog failed to load and the shadow
Darkness slider darkened the whole screen. P40 fixes both.

Custom colour-grading LUTs (Shaders > LUT, files in `Data\Textures\NewVegasReloaded\LUTs`) can be
horizontal strip images N*N wide and N high (256x16, 1024x32, 1089x33, 4096x64, ...) or, since P45,
`.cube` files as exported by grading tools. Since P45 images are loaded at their exact size (before,
a strip whose sides were not powers of two, such as 1089x33, was stretched and came out as
scrambled, speckled colours), the day, night and interior LUTs may have different sizes, an image of
any other shape is ignored with an `UNOFFICIAL LUT ... is ignored` line in the log, and a missing LUT
file no longer turns the picture black.

### Settings
Press <kbd>O</kbd> in game to open the NVR menu, as usual. Two new panels:
- **Main > Main > Performance** - the speed-ups that don't change the image. They are on by
  default; you only need them to switch one off if you suspect it causes a problem.
  Leave `MergedDepthNormals` off (it doesn't work on some graphics drivers; since P40 it checks
  itself and switches off, with a line in the log, if your driver skips it).
  `WorldSceneGuard` is a safety net: if the game stops drawing the 3D world for a moment (seen once
  after loading a save straight into an interior, which left the image almost black), NVR's effects
  are skipped until it starts again.
- **Main > Main > ReducedQuality** - extra speed in exchange for a small loss of quality. All
  off by default (since P49), and they take effect immediately, so you can compare while playing:
  - `FXAA` - a much cheaper edge smoothing than SMAA (about 0.15 ms instead of about 1 ms).
    Turn SMAA off (Shaders > SMAA) when you use it. Slightly softer image.
  - `GodRaysLowRes` - lower-resolution light rays. Softer rays.
  - `AOLowRes` - lower-resolution ambient occlusion. Softer corner shading.
  - `PointShadowInterval` - in interiors, redraw the shadows of lamps and other lights every 2nd,
    3rd or 4th frame instead of every frame (1 = every frame, the original behaviour and the default
    since P49; earlier builds used 2). Static rooms look the same; shadows of people and doors moving
    through the light lag a frame or more, which can look like stuttering or flickering shadows on
    people. Lights that only reach still objects are never redrawn needlessly, whatever the setting
    (since P44). If you pressed Save in an older build, your settings file holds the old default 2:
    since P49 that is reset once to 1 (the log says so); set it again if you really want it.
  - `StaggeredSunShadows` (since P49; before P49 always on) - redraw the sun shadows of things
    more than about 3 m away every 4th frame, and beyond about 9 m every 8th, instead of every frame.
    Still shadows look the same, but people and creatures walking at that distance get shadows that
    stutter, and their bodies can flicker. Saves roughly 0.5 ms per frame outdoors on a GTX 1070.
  - `CachedDistantShadows` (since P59) - the same slower schedule as `StaggeredSunShadows`, but
    whenever a person or creature casts a shadow at that distance, those shadows are redrawn every
    frame, so people don't get the stutter or flicker. Other things that move far away (a door, a
    knocked-over can) can lag a few frames. Saves the most in quiet places (up to about 1 ms on a
    GTX 1070); on busy streets like the Strip someone is nearly always in range, so it saves about
    0.1 ms there. Does nothing while `StaggeredSunShadows` is on.
  - `CheapReflections` - draw the reflection in water without sun shadows and without the bumpy
    terrain detail, like the original game's reflections. Only matters where water is in view.
  - `CheapUnderwaterTerrain` (since P48) - ground under water (lake and river beds, and everything
    when you swim underwater) is drawn without the terrain's fake 3D bumps (parallax), which you see
    through moving, murky water anyway. Dry ground keeps them. This removes most of the frame-rate
    drop when wading or swimming.
  - `ParallaxLite` (since P61; P60 had it as `TerrainParallaxLite` for the ground only, and a saved
    value carries over) - a cheaper version of the fake 3D bumps (parallax) on the ground and on
    objects that have them (many rocks, cliffs and walls): fewer search steps plus one quick
    correction, and on the ground fewer samples for their small shadows. The bumps can sit very
    slightly differently, mostly when looking along the ground. On a GTX 1070 at 1440p: open desert
    terrain 3.7 -> 2.6 ms (about 81 -> 89 fps); rocky desert with many blended ground textures
    6.2 -> 4.1 ms (about 65 -> 73.5 fps). It also stops the ground's bumps sooner, at about 15 m
    instead of 29 m (scaled to your screen: 1024 units at 1440p, 768 at 1080p, 1536 at 4K), where
    they are only a few pixels tall; in a rocky desert view that saved another 0.8 ms (about 4 fps)
    with no visible difference.
  - `NearCascadeInterval` - set to 2 to redraw the nearest sun shadows (up to about 3 m) every 2nd
    frame instead of every frame (1 = every frame, the original). Still shadows stay put; shadows of moving people
    and creatures lag one frame, and right after a very fast turn the edge of the view may show
    softer shadows for one frame.

- **Shaders > Exposure** (since P50) - the automatic brightness adjustment now adapts over a second or
  two (`DarkAdaptSpeed` and `LightAdaptSpeed` 0.2 instead of 50). At 50 it followed every blinking sign
  within a few frames, so the whole screen flickered on the Strip. If your settings file still holds
  the old 50 (from pressing Save in an older build), P50 moves it to 0.2 once and says so in the log.
- **Shaders > ShadowsExteriors > SunSmoothing** (since P64) - the sun moves in steps of about one in-game
  hour (to keep shadow edges from crawling), and in the original every step made all sun shadows jump at
  once. Now each step slides over `GlideSeconds` (3, 0 = the original jump); waiting, sleeping and loading
  still jump. `CrossFade` (off, needs a restart, uses about 72 MB more video memory) fades from the old
  shadows to the new ones instead of sliding them, for anyone who sees shadow edges crawl while they slide.
- **Shaders > PBR > SkylightingSaturation** (since P64) - colour strength of the sky light (1 = as before,
  0 = grey, above 1 = more colourful). Costs nothing.
- **Shaders > Sharpening > Interiors** and **Shaders > Flashlight > Interiors** (since P64) - separate
  settings indoors (Main is now outdoors). Your saved values are copied to the new indoor section once.
- **Water puddles** (fixed in P64) - puddles in the rain never showed the sun or lamps reflected in them
  (their shine was left on a developer test value). If you have rain weathers, set Shaders > WetWorld >
  Main `Amount` 1.0, `Increase` 0.3 and `Decrease` 1.2 to get the darker puddle patches back too (the
  upstream defaults switch them off).
- **Characters' sun shadows** (fixed in P66) - with `ForwardShadows` on, people and creatures turned
  brighter or darker all at once whenever *you* stepped into or out of a shadow, because their shadow was
  checked in the wrong place (the game's skeleton data overwrote the camera values their shaders use).
  They now get their own, correct shadows.
- **Flashlight around grass** (fixed in P75, `Shaders > Flashlight > Main > EdgeFix`, on) - with MSAA the
  flashlight lit the edges of grass, leaves and characters as if they were the background behind them: a
  bright rim and jagged edges. Edge pixels are now lit in two parts. About 0.1 ms with grass filling the beam.
- **Bright colours keep their hue** (fixed in P75, `Shaders > Tonemapping > Main > HueSafeClip`, on) - a
  colour too bright for the screen had its brightest channel cut, so lit orange turned yellow (very visible
  under the flashlight). It now loses a little saturation instead. Colours that fit on screen are unchanged.
  `Shaders > Debug > Main > ClipView` (off) marks the pixels that still clip.
- **B42 Optics scopes** (fixed in P75, `Shaders > ShadowsInteriors > Forward > ScopeFix`, on) - the scope's
  lens and picture-in-picture view got the main view's shadows laid over them (they are drawn from another
  camera). Only active while a B42 Optics lens is on screen; without that mod nothing changes.
- **ParallaxLite edge** (fixed in P75) - with `ParallaxLite` on, the ground's bumps stopped along a line that
  slid along as you walked, and their small shadows had two rings. They now fade out smoothly. Same cost.
- **Fewer driver calls** (P75, `Main > Main > Performance > SkipRedundantConstants`, on) - shader-constant
  uploads that set exactly what the graphics card already holds are skipped (about 9 in 10 indoors, where the
  game's own draw calls limit the frame rate). Same picture.

**Experimental Gun FX** (since P67). The zip includes the `GunFX.dll` plugin. In the NVR menu, **Main > GunFX**
has one switch per effect (muzzle puff, heat smoke, after-fire trail (since P69), ejection smoke, barrel glow, heat
haze, muzzle blast), all off
by default, plus `EnergyWeapons` (since P68; off = no GunFX on lasers, plasma and other energy weapons, modded ones
included). Since P68 the smoke's look (size, opacity, rise, swirl, fading) is set in `GunFX.ini` too. Since P75
the volumetric smoke fills a room with a lingering gun-smoke haze and bullets cut tunnels through it; these two
are the most expensive parts, with their own switches `SmokeHaze` and `BulletTunnels`. Fine-tuning is in `Data\NVSE\Plugins\GunFX.ini`, and every gun also gets its own settings file the
first time you equip it. See [docs/GUNFX.md](docs/GUNFX.md) for how to configure it.

**Optional visual extras** (since P64). These change the look, so they are **off by default** and cost
nothing while off. Turn them on in the menu to try them:
- **Shaders > Particles** - blood, smoke, dust and debris, and the blood sprays and blood on characters,
  are lit by the scene (ambient light, the sun through the shadows, nearby lamps) instead of keeping full
  brightness in the shade, at night and indoors. Muzzle flashes, fire, sparks and glows are unchanged.
  `Strength` 0..1 blends from the game's look; `Brightness` if they look too dark or bright. About 0.3 ms
  during a firefight on a GTX 1070.
- **Shaders > BounceLight** - light bouncing off lit surfaces onto nearby ones (colour bleeding), from
  what is on screen. Since P75 it also guesses light from just off screen (`OffScreenLight`), bounces more
  than once (`MultiBounce`) and reuses the last frame (`Temporal`: less grain; a moving character's glow on
  the floor trails by about 0.15 s, set it off if that bothers you). About 0.5 ms at 1440p on a GTX 1070.
- **Shaders > ShadowsInteriors > Forward** (since P75, needs a restart) - indoor lamp shadows drawn on each
  object, lamp by lamp, instead of one dark mask over the finished picture: a lamp's shadow takes away only
  that lamp's light, so lamp glow and the room's ambient light stay bright and two lamps each cast their own
  shadow. Needs `Shaders > PBR` on. `DebugView` 1 shows each lamp's shadows in its own colour.
- **Shaders > PBR > Metal > MetalOn** (since P75) - guns read as metal instead of plastic: reflections of the
  surroundings, colour on coloured metals, polished edges (your own weapon only by default).
- **Shaders > PBR > EnvMaps > Lighting** (since P75) - the game's "chrome" reflections on guns, glass and
  metal follow the light around them instead of gleaming the same in a dark cave as at noon (0 = the game's).
- **Shaders > ContactHardening** - sun shadows sharp where an object meets the ground and softer the
  further they fall from it, as under the real sun. Needs a restart the first time it is turned on; after
  that the switch works live. `SunSize` 1 and `MaxSoftness` 4 are the tested defaults (higher values can
  show aliasing).

To keep a change for next time, press **Save** at the top of the menu.

Tip: the game's own anti-aliasing (MSAA) is a separate setting. To try without it, set
`iMultiSample=0` in `Documents\My Games\FalloutNV\FalloutPrefs.ini`, or choose Antialiasing: Off in
the game's launcher.

**Performance meter:** press **F10** to start or stop it. While it runs, the time each effect takes
is written to the log every couple of seconds.

**Logs:** since P46 every game start writes a new log to the `NVR-Unofficial-Optimized-Logs` folder
in the game folder (next to `FalloutNV.exe`), named after the date and time it started, for example
`NewVegasReloaded_2026-09-29_14-05-33.log`. Every line starts with the time of day. The folder keeps
the 25 newest logs; at each start the oldest ones are deleted. When you report a problem, attach the
log of that session. (Before P46 there was a single `NewVegasReloaded.log` in the game folder,
overwritten at every start; P46 moves it into the new folder the first time it runs.)

Technical details of every change: [docs/UNOFFICIAL-TECHNICAL.md](docs/UNOFFICIAL-TECHNICAL.md).

> [!IMPORTANT]
> **Unofficial build.** This is a modified version of New Vegas Reloaded. It is **not** made,
> endorsed or supported by the New Vegas Reloaded / TESReloaded authors or maintainers.
> **Please don't ask for help with it in the official NVR channels** (Discord, Nexus, wiki or the
> upstream GitHub issue trackers) - use this repository's issues instead.
>
> It is provided **as is, without support or warranty of any kind**, under the same license as the
> original project (GPL-3.0 with the additional terms in [License.md](License.md)). You may use,
> modify and redistribute it under those terms. Binaries built from it must keep the word
> "UNOFFICIAL" (or another custom name) in their product name or description and may not be sold.
>
> **Credit:** New Vegas Reloaded / TESReloaded by Alessio Tamburini (Alenet), maintained by Llde,
> with the New Vegas Reloaded team and all contributors listed in [License.md](License.md) and below.
> Built on the fork by [macrimmon12-tech](https://github.com/macrimmon12-tech/TESReloaded10)
> (volumetric fog v2, skin and lighting fixes, ImGui menu and more), whose history is preserved in
> this repository. All original copyright notices are unchanged.
>
> This is shared in good faith. If any of the original authors or maintainers feel this release
> steps on their toes, please open an issue here and it will be taken down.

---

*Below is the original New Vegas Reloaded readme.*

<div align="center">
    <a href="https://dlpnd.github.io/nvr-wiki/"><img src="https://i.imgur.com/SUr8ORH.png" width="1024" alt="NVR" /></a>
</div>

<div align="center">

## What is New Vegas Reloaded?

New Vegas Reloaded is a custom graphical extender for Obsidian's "Fallout: New Vegas". It overrides the rendering pipeline to inject various effects that can be completely configured.

Started originally as part of the [TESReloaded](https://github.com/llde/TESReloaded10) project, it has now branched off into it's own project.
</div>

## Features
![](https://i.imgur.com/SLXwTZO.jpeg)
For a full list of features, installation guides, screenshots and more please consult the [NVR Wiki](https://dlpnd.github.io/nvr-wiki/)

## Contributions
This project wouldn't be possible without all the amazing contributions from the Fallout New Vegas and Bethesda modding community. If you want to contribute, please reach out on the [Discord](https://discord.com/invite/QgN6mR6eTK)!  Thank you to all of our contributors so far! 
<div align=center>
  <a href="https://github.com/pr0bability/TESReloaded10/graphs/contributors">
    <img src="https://contrib.rocks/image?repo=pr0bability/TESReloaded10" />
  </a>
  </p>
</div>

----
## Installation

[Check the Wiki page for the most up-do-date instructions on how to install](https://dlpnd.github.io/nvr-wiki/docs/Installation)

----
## Configuration

To activate/Deactivate each effect, press <kbd>O</kbd> in game to bring up the menu. Navigate the menu with the arrow keys, and activate/deactivate effects and increase/decrease effect values with the numpad <kbd>+</kbd>/<kbd>-</kbd> keys.

The configuration is stored in `NewVegasReloaded/nvse/Plugins/NewVegasReloaded.dll.toml` and can be edited directly with a text editor.

----
## Building from source
![GitHub Actions Workflow Status](https://img.shields.io/github/actions/workflow/status/pr0bability/TESReloaded10/build.yml?style=for-the-badge&color=a87300)

**Requirements**
* [Microsoft Visual Studio 2019](https://community.chocolatey.org/packages/visualstudio2019community)
* [DirectX SDK 2009](https://community.chocolatey.org/packages/directx-sdk)

**Building**

Run the `build.bat` file in command line, with the following command:

```shell
build.bat "NewVegasReloaded" "C:/DeployFolder/"
```

 * The second argument is optional, and can be used to copy the built library to the game folder or (preferably) to a mod folder managed by [Mod Organizer 2](https://vivanewvegas.github.io/mo2.html). Otherwise, the built files can be found in the `/build/` folder at the root, to be copied manually.

----
## License
Check [License.md](License.md) for the licensing terms.
