> [!IMPORTANT]
> ## UNOFFICIAL build: "New Vegas Reloaded UNOFFICIAL Optimized"
>
> This branch is an **unofficial, modified version** of New Vegas Reloaded. It is **not** made,
> endorsed or supported by the New Vegas Reloaded / TESReloaded authors or maintainers.
>
> **Do not use the official New Vegas Reloaded channels (Discord, Nexus, wiki, or the upstream
> GitHub issue trackers) for help with this version.** Report problems with it here, or not at all.
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
>
> ### What this version changes
> Performance work on the post-processing and shadow pipelines, plus fixes, on top of the original:
> - Copy-free effect chain: effects alternate between two buffers instead of copying the whole
>   frame after every effect (kill switch: `DisableFrameChain = true` under `[Main.Main.Misc]`).
> - Dedicated half-resolution targets for ambient occlusion, volumetric fog and god rays, with fewer
>   full-resolution copies; effects that never read `TESR_SourceBuffer` no longer refresh it.
> - Shadow cascades: middle cascade updated every 4 frames and far/LOD every 8, only freshly updated
>   cascades resolved/filtered, cheaper face culling and batched bone uploads.
> - Cheaper SMAA edge detection, single-pass dither buster, merged point-shadow passes, and other
>   shader-level savings.
> - Fixes: horizontal black lines on sun-shadowed surfaces (contact shadows), a read/write feedback
>   loop in the contact-shadow passes, volumetric fog reconstructing the whole scene from half
>   resolution, explicit SMAA stencil state, and a few D3D reference leaks.
> - Overall it should generally run better without compromising visual quality too much.
>   
> - An F10 GPU/CPU profiler that writes per-effect timings to `NewVegasReloaded.log`.
>
> **Experimental.** So far this has only been tested on one Windows PC (GTX 1070), and not in every
> configuration (interiors, underwater, point-light shadows, AMD cards, native D3D9 without DXVK).
> It needs more testing and feedback. If you try it, reports of what works and what breaks,
> ideally with your `NewVegasReloaded.log` and an F10 profile, are welcome in this repository's
> issues. Keep a backup of your previous NVR files.
>
> ### How to install
> This build replaces files of an existing New Vegas Reloaded install; it is not a standalone mod.
> 1. Install New Vegas Reloaded normally first (xNVSE and the rest of its requirements included)
>    and check that it runs.
> 2. Back up your `Data\NVSE\Plugins\NewVegasReloaded.dll` and `Data\Shaders\NewVegasReloaded\`
>    folder.
> 3. With the game closed, extract the release zip into your `Data` folder (or your mod manager's
>    NVR mod folder) and overwrite when asked. It contains `NVSE\Plugins\NewVegasReloaded.dll` and
>    the changed shaders under `Shaders\NewVegasReloaded\Effects\`.
> 4. Start the game. `NewVegasReloaded.log` (in the game folder) should contain lines starting
>    with `UNOFFICIAL`, such as `UNOFFICIAL frame chain`.
>
> If a shader change does not seem to apply, delete the `Cache` folders under
> `Data\Shaders\NewVegasReloaded\`. To uninstall, restore your backup.
>
> ### How to use
> - Settings work exactly as in normal NVR: press <kbd>O</kbd> in game, or edit
>   `Data\NVSE\Plugins\NewVegasReloaded.dll.toml`.
> - **F10** starts/stops the built-in profiler. While it runs, average per-effect GPU and CPU times
>   are written to `NewVegasReloaded.log` every 120 frames (`GPU PROFILE ...`, `CPU PROFILE ...`).
>   `Frame interval (CPU)` is your real frame time.
> - If you see image problems, add `DisableFrameChain = true` under `[_Main.Main.Misc]` in the
>   `.toml` to turn off the copy-free effect chain without reinstalling.
> - Optional speed-for-quality trade: lower `MaxSearchSteps` under `[_Shaders.SMAA.Main]` (e.g. 16).

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
