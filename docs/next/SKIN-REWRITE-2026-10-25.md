# The skin shader, rewritten on the build's own stack (2026-10-25)

Branch `deep/skin-rewrite` (worktree `NVR-Lunacy\.claude\worktrees\skin-rewrite`, junction `work\NVR-skin-rewrite`), on
`deep/combo-1025b` 7207c9d1 (the installed line). Request: "rewrite our skin shader with similar or better features visually and
have it be built around our stack of shaders instead of being a frankenstein port of two different skin shaders".

## What was there

Three skin paths, chosen at startup:

| path | files | what it was |
|---|---|---|
| default (`[Shaders.Skin.NewSkin] Enabled = false`) | `SKIN2000-2011.pso.hlsl`, `SKIN2000/2001/2005/2013.vso.hlsl`, `Includes/Skin.hlsl` | the vanilla skin shaders translated instruction by instruction, with NVR's sun shadow, the sky ambient, linear lighting and a per-pixel wrap (`PerPixelDiffusion`) grafted on; five dead settings (`Attenuation`, `Coeff*`, `RimScalar`, `SpecularPower`); no highlights of its own (the game's extra highlight passes did that) |
| NewSkin (off, WIP) | `SkinTemplate.hlsl`, `Includes/SkinLighting.hlsl` | Conor's (mrfury97) skin shader, ported 2026-10-15, with its own copies of the BRDF helpers, vanilla's wrap + rim light added back on top (`FillLight`), four Community Shaders options grafted on 2026-10-19, and a vanilla-matched highlight boost |
| Vanilla Plus Skin (that mod installed) | `SkinVPSTemplate.hlsl`, `Includes/SkinVPS.hlsl` | VPS's own LUT lighting plus NVR's sun shadow (kept) |

Plus Conor's screen-space blur (`Effects/SkinScattering.fx.hlsl`, `SkinScattering.*`: Jimenez & Gutierrez's separable SSS with
Community Shaders' kernel), which only runs with the game's MSAA off (D3D9 draws nothing with extra multisampled targets bound).

## What there is now

One skin shader, `SkinTemplate.hlsl` + `Includes/Skin.hlsl`, built from the same includes as the object shaders:

| part | comes from |
|---|---|
| highlights: two GGX lobes at skin's F0 0.028 (Unreal's skin model: 0.75 x and 1.3 x the roughness, 0.85 / 0.15), specular anti-aliasing, (1 - F) on the diffuse, the Smith-correlated masking option | `Includes/PBR.hlsl` (`BRDF`, `Fresnel`, `SpecularAA`, `EnvBRDFApprox`, moved here from Object.hlsl) |
| light and ambient strengths, the sky light (spherical harmonics), linear lighting | `Includes/PBRScale.hlsl`, `SkyAmbient.hlsl`, `LinearLight.hlsl` |
| the lamps' falloff (the game's, the objects' decode, or the physical shape) | `Includes/LampFalloff.hlsl` (`LampAttOther`) |
| the sun's shadows: cascades, PCF / contact hardening, the character shadow map, the smooth vertex normal, the skin's edge softness and reach | `Includes/Shadow.hlsl` (`GetSunShadow`, `ShadowLookupNormal`, `SHADOW_SKIN_SOFTNESS`; new helper `SunShadowCoarse`) |
| the lamps' shadows indoors | `Includes/PointShadowForward.hlsl` (the Interior records) |
| rain on the first-person arms | `Includes/HudRain.hlsl` |
| the screen-space blur's inputs (optional, MSAA off) | unchanged interface: COLOR1 the highlights' share + distance, COLOR2 albedo + luma |

What skin adds, in `Includes/Skin.hlsl`, each from the published model it names:
- **Diffusion under the skin**: Penner's pre-integrated skin (GPU Pro 2) over d'Eon's measured profile (GPU Gems 3), evaluated
  as the cosine irradiance of a spherical Gaussian (Hill's closed form, MJP's SG series) whose sharpness follows the curvature
  measured from screen-space derivatives. The sharpness constants were fitted for this build (`scratch fit_skin.py`: the
  pre-integrated table over radii 1-50 mm, 1D-projected profile as Penner's code; largest error 0.07 red, 0.016 green, 0.008
  blue; the constants the port carried fit the same table to within noise, which validated the table). Fine relief is
  softened per colour with a blurred normal (Penner's second technique). `Scattering` scales the curvature (1 = twice the
  measured width: the true width barely shows on a head at game distances).
- **Transmission** through thin parts: Barre-Brisebois & Bouchard (GDC 2011), coloured by Jimenez's transmittance of d'Eon's
  profile at a thickness read off the curvature: white at the thinnest rims, orange, deep red. `Translucency`.
- **Shadow edges**: the penumbra widened into the shadow per colour, red furthest (`ShadowScatter`), and the skin's own far
  side lets the scattered light through the shadow map's self-shadow when one coarse tap further toward the sun is lit
  (`SunShadowCoarse`; without it every terminator inside a cast shadow kept a red band).
- **Sheen**: a fuzz lobe (Estevez & Kulla's Charlie distribution, Neubelt's visibility) at the rims. `Sheen`.
- **Sky reflection**: both lobes' split-sum response times the sky along the reflection (sharp sky for the tight lobe,
  convolved for the broad one), horizon-faded. `SkyReflection` (outdoors).
- **Wet skin** in the rain: smoother, shinier (`RainWetness`).

Gone: the vanilla wrap + rim fill light, the vanilla-matched highlight boost, the four Community Shaders grafts
(`Fuzz` became `Sheen`, built in; `CreaseDamping` is what the stack's specular anti-aliasing does; `EnergyBalance`'s (1 - F)
is built in; `TransmitProfile` is the transmission's colour now), the dead vanilla settings, the hand-translated SKIN20xx
files and `SkinLighting.hlsl`. The game's own face highlight passes are muted by default (`MuteGameHighlights`): the skin
draws its own highlights.

Settings: `[Shaders.Skin.Main]` (outdoors) and `[Shaders.Skin.Interiors]` (indoors) hold SpecularStrength, Roughness,
Scattering, Translucency, ShadowScatter, Sheen; Main also SkyReflection, RainWetness, MuteGameHighlights,
OverrideVanillaPlusSkin. `[Shaders.Skin.Scattering]` is the screen-space blur (outdoors), its indoor keys stay in
Interiors. `[Shaders.Skin.Status] Enabled` off = the game's own skin shaders. `[Shaders.Skin.NewSkin]` is gone; old keys in a
user's file are ignored.

Code: `src/effects/Skin.h/.cpp` (constants c192-c195: TESR_SkinMaterial, TESR_SkinLook, NVR_SkinDraw per draw, TESR_SkinDebug;
the template map; VPS routing decided when the SKIN names load), `Hooks/Shaders.cpp` (the FaceGen maps bound to the light-only
face passes, the highlight-pass mute, the blur's targets, the interior-faces patch; every hook idle while VPS's shaders are in
use), `SkinScattering.*` (the blur, unchanged but for the gate).

## Limits
- The sun cross-fade (SunSmoothing CrossFade) is compiled out of skin: with it the skin shaders reach 4,900-5,500 slots under
  PCF + contact hardening, past what drivers create (~3,600). A sun step moves a skin shadow in one go.
- Lamp-only added passes (3+ lamps on a face indoors) have no texture to divide their highlights by, so those highlights come
  out tinted by the face texture (as before).
- The screen-space blur still needs the game's MSAA off (as before).
- Per-pixel scattering is a curvature estimate: it softens small features and wraps the light; the broad softness of a whole
  face at close range is the blur's job (or `Scattering` above 1).

## Offline checks
- `scratch check_skin.py`: all 26 vertex + 13 pixel variants compile with the game's compiler (d3dx9_43) under the user's
  settings (PCF + contact hardening, character map, linear lighting, HudDrops, smooth normals): pixel shaders 741-2836 slots
  outdoors, 1296-2663 in the interior lamp-shadow records; EVSM4: up to 3586; everything off: 448-1055. Highest constant c222
  (pixel), c247 (vertex). 0 errors, 0 warnings.
- DLL: VS2019 Release Win32, 0 errors (`build_tree.ps1`, 21:28-21:29).

## In-game results
(see below)
