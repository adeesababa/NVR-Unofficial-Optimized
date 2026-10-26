# Debugging a broken frame on the NVR DXVK fork (2026-10-26)

Branch `deep/fork-debug-capture` (on `deep/fork-combo-1026`). Everything here is OFF by default and changes nothing in normal
play. It only helps when a picture is wrong and someone needs to see exactly which step drew what.

Three tools:

1. **Named steps in a capture** (`GpuDebugLabels`): in a RenderDoc capture every NVR step shows up as a named folder:
   each effect ("Bloom", "AmbientOcclusion", ...), each shadow map and sun cascade ("Sun cascade 0 (near)"), the game's own
   parts ("World scene (game)", "First person (game)", "Game image space"), and the fork's own Vulkan passes (inside the
   folder of the NVR step that ran them, e.g. "FSR reconstruct", "DoF bokeh scatter (compute)", "Bloom (DXVK extension)").
2. **A capture button** (`RenderDocCaptureKey`, and the shot-list command `capture`): asks RenderDoc to grab the next frame.
3. **Vulkan validation** (a `dxvk.conf` template): the Khronos validation layer, including synchronization validation,
   checks every Vulkan call the fork makes and writes complaints into the DXVK log.

## What is on this PC (checked 2026-10-26) and what you would need

| Thing | Installed? | Needed for |
|---|---|---|
| Vulkan runtime (vulkan-1.dll 1.4.309, 32- and 64-bit) | yes (comes with the NVIDIA driver) | everything (already fine) |
| `vulkaninfo.exe` | yes (C:\Windows\System32) | checking layers |
| RenderDoc | **no** (no folder, no Vulkan layer registered) | tools 1 and 2 |
| Vulkan SDK (Khronos validation layer) | **no** (no C:\VulkanSDK, no `VK_LAYER_KHRONOS_validation` in `vulkaninfo`) | tool 3 |

Vulkan layers that ARE installed and hook every Vulkan game: RivaTuner (RTSS), Overwolf (two), OBS, Steam overlay and
Steam's shader cache, plus NVIDIA's own. The overlays sometimes break captures; the templates show how to switch them off
for one run.

Nothing was downloaded or installed. To use the tools, install:

- **RenderDoc** (free, MIT licence): https://renderdoc.org/builds -> "Windows 64-bit" installer (.msi), roughly 30 MB
  download, about 100 MB installed. The 64-bit RenderDoc also captures 32-bit games such as Fallout: New Vegas. On its first
  start it may say Vulkan capture is not set up and offer to fix it: say yes (it registers its Vulkan layer; needs admin).
- **Vulkan SDK** (free, only for validation): https://vulkan.lunarg.com/sdk/home -> Windows -> the latest SDK installer,
  roughly 200-300 MB download, 1-2 GB installed. In the installer, TICK the optional **32-bit components** ("SDK 32-bit Core
  Components" or similar): the game is 32-bit and can only use the 32-bit layer (folder `C:\VulkanSDK\<version>\Bin32`).
  The exact sizes are on the download pages.

## Tool 1 + 2: capture a frame with RenderDoc (step by step)

This only works on the **fork's d3d9.dll** (DXVK turns the game into Vulkan, and RenderDoc can capture Vulkan). It cannot
capture the normal native D3D9 game. Use a copy of the game or a test slot, never your real install.

1. Put the fork's `d3d9.dll` next to `FalloutNV.exe` in the copy you test with.
2. Next to it, put `tools/nvr-dxvk/debug/dxvk.conf.markers`, renamed to **`dxvk.conf`**. (This needs a fork d3d9.dll built
   with patch 0014. With an older fork, skip this file and add `DXVK_DEBUG` = `markers` in RenderDoc's "Environment
   Variables" box in step 5 instead.)
3. In NVR's settings file (or the menu with "Developer options" ticked, section Main > Develop): set
   `GpuDebugLabels = true`. Optional: `RenderDocCaptureKey = 87` (F11) for NVR's own capture key.
4. Start RenderDoc. File -> Launch Application.
5. "Executable Path": the program you normally start the game with (for example `nvse_loader.exe` in the game folder).
   Tick **"Capture Child Processes"** (the loader starts FalloutNV.exe, which is the one to capture).
   "Working Directory": the game folder. Click **Launch**.
6. In game, go to the broken spot. Press **F12** (RenderDoc's own key) or your `RenderDocCaptureKey`. A small RenderDoc text
   in the corner says a capture was taken. NVR's log also says `RENDERDOC: capture written (...): <file>`.
7. Back in RenderDoc, double-click the capture. In the "Event Browser" on the left, open the folders: each NVR step is one
   folder with its name. Click a draw inside it; the "Texture Viewer" shows what it drew, "Pipeline State" what it read.
8. File -> Save Capture As... to keep it (an .rdc file, often 100-500 MB) and send it to whoever is looking at the bug.

If no folders appear: check the NVR log for `GPU DEBUG LABELS: on ... (D3DPERF_GetStatus = 1)`. If it says `= 0`, DXVK did not
get `DXVK_DEBUG=markers` (the dxvk.conf is missing, misnamed, or the d3d9.dll lacks patch 0014). DXVK's own log
(`FalloutNV_d3d9.log` next to the game) should contain `NVR debugEnv: DXVK_DEBUG=markers` and `Debug Utils are enabled`.

If the capture fails or the game crashes under RenderDoc: close RivaTuner, Overwolf and OBS, or use the commented "overlays
off" line in `dxvk.conf.markers`.

### Capturing a shot from the auto-test

Shot lists accept `capture [frames]` inside a shot: it asks RenderDoc for a capture of that shot's picture frame (2 frames by
default, because DXVK shows a frame slightly late, so the picture frame is surely inside). It only does something when the
game was started from RenderDoc; otherwise the log says once that renderdoc.dll is not loaded. Example:

    frames 300
    shot broken-water
    set Main.Develop.Main GpuDebugLabels true
    capture 2

## Tool 3: Vulkan validation (+ synchronization validation)

Validation checks the fork's Vulkan calls for mistakes (wrong image layouts, missing barriers between a write and a read,
bad descriptors). It makes the game much slower (2-10x): use it on one broken spot, never for timing.

1. Install the Vulkan SDK with the 32-bit components (see above).
2. Copy `tools/nvr-dxvk/debug/dxvk.conf.validation` next to the fork's `d3d9.dll` as **`dxvk.conf`**. Edit the
   `VK_ADD_LAYER_PATH=C:/VulkanSDK/1.4.309.0/Bin32` part to the version you installed (the folder must contain
   `VkLayer_khronos_validation.json`).
3. Start the game as usual (no RenderDoc needed). Go to the broken spot, wait a few seconds, quit.
4. Open `FalloutNV_d3d9.log` next to the game. Near the top: `NVR debugEnv: ...` lines and `Enabled instance layers:
   VK_LAYER_KHRONOS_validation`. Below: one `err:` / `warn:` line per problem. Synchronization problems mention
   "SYNC-HAZARD-..." (for example `SYNC-HAZARD-READ-AFTER-WRITE`); those are the ones that cause flicker and random garbage.
5. If every single draw reports "set not bound" or descriptor errors (DXVK's own draws too), the layer is too old for
   DXVK 3.0's descriptor path: uncomment the two `dxvk.enableDescriptor...` lines in the template and run again.

If the log says `Validation layers not found, set VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`, the path in step 2 is
wrong or the 32-bit components were not installed.

### In a test slot (run_slot / bench queue)

`run_slot.sh` copies `d3d9.dll` and `dxvk.conf` from the `FORK_D3D9` folder into the slot. So a folder with the fork's
d3d9.dll (patch 0014 or later) + one of the templates renamed to `dxvk.conf` is all a slot needs; no environment variables
have to reach the game. Make one folder per purpose, e.g. `G:\_NVR-build-scratch\nvr-dxvk-debug-markers\` and
`...\nvr-dxvk-debug-validation\`, and pass it as `FORK_D3D9=`. A validation run is slow: give it a short list and do not
read its F10 timings.

## The settings ([Main.Develop.Main], all off by default)

| Setting | Default | What it does |
|---|---|---|
| `GpuDebugLabels` | false | Each NVR step opens a named group (D3DPERF_BeginEvent/EndEvent). Native D3D9: only a D3D9 tool (PIX) sees them, near-zero cost. DXVK: nothing unless DXVK_DEBUG=markers, then they become Vulkan debug labels. |
| `RenderDocCaptureKey` | 0 | DirectInput key code that asks RenderDoc for a capture (87 = F11, 88 = F12 is RenderDoc's own). 0 = no key. Only acts when renderdoc.dll is already in the game. |
| `RenderDocCaptureFrames` | 1 | Frames per key press (1-10). |

## How it works (for programmers)

- `src/core/GpuDebug.h`: `GpuDebug::LabelScope` (D3DPERF_* looked up in the loaded d3d9.dll with GetModuleHandle, never
  loaded by NVR); `RequestCapture` / `Tick` use RenderDoc's in-app API (`RENDERDOC_GetAPI`, 1.1.2 table, declared locally
  so no RenderDoc header is needed) only if `GetModuleHandle("renderdoc.dll")` finds it. NVR never loads or ships RenderDoc.
- Labels come from `GpuProfileScope` (every F10 timer scope, timer on or off), `EffectRecord::Render` (each effect, plus an
  inner "(DXVK extension)" group around `ExtRender`), the pre-scene span in `Render.cpp`, and each sun cascade in
  `ShadowManager.cpp`. No DXVK patch is needed for the labels: DXVK 3.0.1's `D3D9UserDefinedAnnotation` records them into the
  same CS stream as the extension's work (`EmitCs`), so the extension's dispatches land inside NVR's groups.
- DXVK 3.0.1 facts (from its source): `D3DPERF_BeginEvent` does nothing unless `DXVK_DEBUG=markers` (the annotation object
  is created only with `DxvkDebugFlag::Markers`); `dxvk.enableDebugUtils = True` or RenderDoc's
  `ENABLE_VULKAN_RENDERDOC_CAPTURE=1` give only DXVK's internal labels, not the app's; `DXVK_DEBUG=validation` enables
  `VK_LAYER_KHRONOS_validation` if the loader lists it and routes its messages into the DXVK log. None of these is a
  dxvk.conf option in stock DXVK, hence patch 0014.
- Patch 0014 (`tools/nvr-dxvk/patches/0014-...`, extension "1.5-debug"): `nvr.debugEnv = "NAME=value|NAME=value"` in
  dxvk.conf sets those variables (SetEnvironmentVariableA) in `DxvkInstance`'s constructor, after reading the config and
  before DXVK reads DXVK_DEBUG and before the Vulkan loader is used. Only `src/dxvk/dxvk_instance.cpp` changes.
- Not done: D3D9 resource names (DXVK 3.0.1's D3D9 side ignores `WKPDID_D3DDebugObjectName`; textures show as numbers in a
  capture). A later patch could name NVR's render targets.

## Untested

Code-only (standing rule: no in-game runs by agents). Builds clean (NVR with MSBuild v142; the fork through the GitHub
workflow). Not yet seen in game: the labels in a real RenderDoc capture, the capture key, the `capture` shot command with
RenderDoc loaded, the validation template on Windows. The bulk list `bench\foundations\lists\fork1026-debugcap-bulk.txt`
checks that the switches change no picture, cost nothing visible, and that `capture` without RenderDoc only logs.
