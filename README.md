# New Vegas Reloaded 4.4.1 - UNOFFICIAL Boutique (side build)

**The official New Vegas Reloaded 4.4.1, made faster, with the same picture.**

This branch is the official NVR 4.4.1 source (pr0bability/TESReloaded10 at `86ce118a`, the 4.4.1 release) with the
performance work of [NVR UNOFFICIAL Optimized](https://github.com/adeesababa/NVR-Unofficial-Optimized) ported onto it.
It is for people who want to stay on 4.4.1 - its effects, its fog, its menu, its settings - and only want more FPS.

- **Same picture as 4.4.1**, at full resolution, with no new shader features and no frame-skipping. Every change was
  checked in game by switching it off and on inside one session: nothing visible changes.
- **Faster:** on a GTX 1070 at 2560x1440 with 4.4.1's default settings, a lamp-lit casino interior went from about
  40 to 63 FPS and the desert at noon from about 36 to 51 FPS.
- **4.4.1's own menu**, unchanged. The new options are under **Main > Main > Performance**, all on by default; each can
  be switched off live (SlimDepthBuffer needs a restart) to get the original 4.4.1 behaviour back.

What changed, in short (details: [docs/UNOFFICIAL-4.4.1-BOUTIQUE.md](docs/UNOFFICIAL-4.4.1-BOUTIQUE.md)):
effects pass the image to each other without copying the whole frame after every pass; ambient occlusion keeps its
term in small buffers and does its two sampling passes in one; when nothing renders in between, the sun-shadow blend
and the AO blend are done inside the fog pass (the 4.4.1 fog itself is unchanged); god rays and the interior shadow
blur work in their own small buffers at the resolution they always used; a lighter depth buffer; the twelfth lamp's
shadow map, which nothing reads, is no longer drawn; lamp shadow maps skip objects that cannot reach them; and a few
skipped no-op passes.

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
> with the New Vegas Reloaded team and all contributors listed in [License.md](License.md);
> the 4.4.1 release by pr0bability. All original copyright notices are unchanged. Thanks to
> [WallSoGB](https://github.com/WallSoGB) for the Fallout: New Vegas IDA Pro database our optimization project uses.
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