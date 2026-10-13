# Foliage translucency (grass, SpeedTree leaves, NIF plants): research, 2026-10-11

Read-only research by a Haiku agent; saved here by the orchestrator. Nothing built or measured.

## Plain English
- Grass and SpeedTree leaves can glow when backlit from a vertex-shader-only change. Their pixel shaders already apply the
  forward sun shadow to the sun term (grass multiplies, leaves subtract the shadowed share), so glow added to the sun term is
  shadowed automatically.
- Vanilla sun uses sat(N.L). Add the far-side term sat(-N.L) times "eye looks into the sun" pow(sat(V.-L), P). The two never
  overlap (sat(x) + sat(-x) = |x|), so nothing is brightened twice.
- Cost: about 15-18 vertex instructions per vertex, zero per pixel (estimate).
- NIF plants (object shaders, possibly Joshua trees/bushes): nothing tells the shader "this is a plant" -> needs a C++ per-draw
  signal first; defer.

## Light data per shader (src\hlsl\NewVegas\Shaders)
- Grass VS GRASS23x000.vso: L = DiffuseDir c0 (line 11, toward the sun), DiffuseColor c1, AmbientColor c6, AddlParams.x c7.
  Normal is fake: orient = (frac(inst).xyz-0.5)*2 (74); NdotL line 75; sun line 78. Camera-relative world position from
  GetShadowWorldPos (86); camera is the origin, so V = normalize(-p). 001 uses the vertex normal (74, 77); 002 orient on a
  billboard basis (83, 96); 003 vertex normal in the billboard basis (79-80, 91).
- Grass PS GRASS23x000TMS.pso: sun = IN.sun.xyz (27), *= GetSunShadow (32-37), PBRLight + PBRAmbient + SkyAmbient (40).
- Leaf VS STLEAF001.vso (003 = same + point light): model space; N = normalize(leafDir*NormalAdjust.y + IN.normal) (96-97);
  NdotL 99; sun 102-103 -> TEXCOORD3; combined 104 -> TEXCOORD1; shadowWorldPos 116. World sun dir: TESR_SmoothedSunDir c131.
- Leaf PS Leaf.hlsl:21-32: lighting - sun*(1-s).
- Object PS ObjectTemplate.hlsl: tangent-space lightDir/viewDir (345-349), shadowMultiplier (711-738), getSunLighting (791+).
- Precedent: Includes\Skin.hlsl:51-60 already has a "light behind" term.

## Formula
    back  = sat(-dot(N, L))            // far side lit
    align = pow(sat(dot(V, -L)), P)    // eye looks into the sun through the card
    gate  = sat(L.z * G)               // off with the sun below the horizon (GetSunShadow returns 1 at night)
    T     = k * back * align * gate    // k = Translucency, 0 = off
- Grass (all four VS): NdotL += T on lines 75 (000), 74 (001), 83 (002), 80 (003). PS unchanged.
- Leaves (001, 003): trans = T * lightScale * DiffuseColor.rgb * SunScale.x; add to OUT.sun (103) and OUT.lighting (104).
  Interiors: also gate on TESR_ShadowFormatData.z (c129) in the leaf VS only (c100-c133 overlap grass InstanceData).
- Object path (phase 3, after line 738): T_obj * shadowMultiplier * PSLightColor[0].rgb * TESR_PBRData.z * albedo.
- Defaults: ship Translucency 0.0, test 0.5, Power 4, SunGate 4.

## Registers (verify against vanilla tables)
- Grass VS: c18 (NVR map: MVP c9-c12, InstanceData c20-c247, matrices c248-c255). Never reference c100-c133 from grass VS.
- Leaf VS: c82 (engine uploads stop at c81 per STLEAF001.vso.hlsl:4).
- Leaves are replaced only while the PBR collection is enabled (ShaderManager.cpp:568, 576).

## Plan
1. [_Shaders.Foliage.Main] Translucency 0.0 / Power 4.0 / SunGate 4.0 in the defaults toml (menu auto-generated).
2. PBR.cpp/.h: RegisterConstant("TESR_Translucency"), copied in UpdateConstants like TrimLampPasses (PBR.cpp:93-94).
3. Grass VS 000-003 (c18), leaf VS 001/003 (c82). Guard NaN (P >= 0.5, normalize with max(len, 1e-6)).
4. Tests: k=0 identical screenshot; glow only in sun; none at night/indoors; glow at sunrise; still dims with ForwardShadows off.
5. Phase 3 (NIF plants): needs a plant signal (hook writing a per-draw constant, c168 free in the object chain; or a checked
   BSShaderFlags bit). Joshua trees: check the CASTERS log shader type (SpeedTreeLeaf 0x6 vs PPLighting 0x8).

Not verified: vanilla constant tables, engine SunDir space for leaves, DiffuseColor at night, instruction cost.
