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
> - An F10 GPU/CPU profiler that writes per-effect timings to `NewVegasReloaded.log`.
>
> Only tested on one Windows PC (GTX 1070). Keep a backup of your previous NVR files.

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