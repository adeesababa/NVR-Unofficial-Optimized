// UNOFFICIAL 2026-10-18 NewGrass ([Shaders.Grass.NewGrass], OFF by default; core/NewGrass.h; bench\deep-roadmap\evidence\newgrass-DESIGN.md).
// Real blade geometry in place of the game's alpha-tested grass cards, built here from the game's OWN grass instance table: the
// TallGrassShader uploads c20..c247 (up to 228 tufts: xyz position, frac = packed slope normal, w = size step + brightness) for the
// card draw this one replaces, so positions, density, grass types per region and the per-tuft size and brightness all stay the
// game's. Each vertex is 4 bytes (UBYTE4): x tuft index, y blade index, z level along the blade, w side (0 left, 1 right).
// Ideas: Ghost of Tsushima's procedural grass (Eric Wohllaib, GDC 2021: Bezier blades, hashed per-blade variation, side-tilted
// normals, blades per tuft falling with distance); Jahrmann & Wimmer 2017 'Responsive Real-Time Grass Rendering' (width grows with
// distance so fewer blades keep the coverage). D3D9 SM3: no compute or stream-out, so the blade is pure vertex-shader arithmetic.
//
// REGISTER MAP (hand-pinned): the game's own grass constants are read where they are (c0 DiffuseDir, c1 DiffuseColor, c2 ScaleMask,
// c4 WindData, c5 AlphaParam, c6 Ambient, c7 AddlParams, c9-c12 ModelViewProj, c14 FogColor, c15 FogParam, c20-c247 InstanceData);
// ours sit at c248-c251 (NewGrass.h writes them per draw). Literals (def) go to the free registers below c20 (c3, c8, c13, c16-c19).
// No TESR_ constant here on purpose: Shadow.hlsl's pins (c100-c133) would land inside the instance table. The pixel shader
// rebuilds the camera-relative world position from the clip position it gets (GetShadowWorldPos).

float3 DiffuseDir   : register(c0);
float3 DiffuseColor : register(c1);
float3 ScaleMask    : register(c2);
float4 WindData     : register(c4);   // xy direction, z magnitude, w phase (the game's)
float4 AlphaParam   : register(c5);   // z fade start, w fade range
float4 AmbientColor : register(c6);
float4 AddlParams   : register(c7);   // x grass dimmer
row_major float4x4 ModelViewProj : register(c9);
float4 FogColor     : register(c14);
float4 FogParam     : register(c15);

#ifndef GRASS_INSTANCE_COUNT
    #define GRASS_INSTANCE_COUNT 228
#endif
float4 InstanceData[GRASS_INSTANCE_COUNT] : register(c20);

float4 NG_Lod   : register(c248); // x LodNear (units), y 1 / (LodFar - LodNear), z 1 / WidenDistance, w far end of our blades (0 = none)
float4 NG_Shape : register(c249); // x model height (units, before the tuft's size step), y tuft spread radius, z width / height, w bend
float4 NG_Misc  : register(c250); // x 1 = blades grow along the slope normal (LFS variant), y blades per tuft, z height scale, w tufts in this draw
float4 NG_Var   : register(c251); // x 1 / (levels - 1), y 1 = only the tufts before the fade start (GrassFirst part 1 of a mixed draw), z flutter

struct VS_INPUT {
    float4 packed : TEXCOORD0;   // UBYTE4: x tuft, y blade, z level, w side
};

struct VS_OUTPUT {
    float4 position : POSITION;
    float4 clip     : TEXCOORD0;   // the clip position again (the PS rebuilds the camera-relative world position from it)
    float4 params   : TEXCOORD1;   // x height along the blade 0..1, y side 0..1, z tint seed 0..1, w the tuft's light scale
    float4 normal   : TEXCOORD2;   // xyz world normal of the blade (side-tilted), w grass dimmer
    float4 sunDir   : TEXCOORD3;   // xyz the sun direction the game lights the grass with
    float4 ambient  : TEXCOORD4;   // the game's grass ambient colour
    float4 sun      : TEXCOORD5;   // the game's grass sun colour
    float4 fog      : COLOR0;      // rgb fog colour, w fog amount (as the game's grass VS)
};

// A stable number 0..1 from a position and a salt (the tuft position, so blades keep their place when the engine rebuilds a group).
float Hash(float2 p) {
    return frac(sin(dot(p, float2(12.9898f, 78.233f))) * 43758.5453f);
}

VS_OUTPUT main(VS_INPUT IN) {
    VS_OUTPUT OUT;

    int idx = (int)(IN.packed.x + 0.5f);
    float4 inst = InstanceData[idx];
    float blade = IN.packed.y;
    float t = IN.packed.z * NG_Var.x;                 // 0 root .. 1 tip
    float side = IN.packed.w * 2.0f - 1.0f;           // -1 / +1

    float4 rootClip = mul(ModelViewProj, float4(inst.xyz, 1.0f));
    float depth = rootClip.w;
    // The game's fade metric (REVERSED_DEPTH: length(x, y, w)), the same test GrassFirst's parts use.
    float instDist = length(float3(rootClip.xy, rootClip.w));

    // Blades per tuft fall with distance (LodNear -> LodFar: all -> one); the blade past the budget shrinks away over one blade of
    // budget (no popping as the camera moves), then is folded to a point outside the view (no raster work).
    float budget = lerp(NG_Misc.y, 1.0f, saturate((depth - NG_Lod.x) * NG_Lod.y));
    float keep = saturate(budget - blade);
    if (NG_Lod.w > 0.0f) keep *= saturate((NG_Lod.w - depth) / (0.15f * NG_Lod.w));
    bool gone = IN.packed.x >= NG_Misc.w || keep <= 0.0f || (NG_Var.y > 0.5f && instDist > AlphaParam.z);
    [branch] if (gone) {
        VS_OUTPUT none = (VS_OUTPUT)0;
        none.position = float4(2.0f, 2.0f, 0.5f, 1.0f);
        return none;
    }

    // Packed per tuft: orientation (the ground normal) and the light scale, as the game's VS reads them.
    float4 f = frac(inst);
    float lightScale = f.w * 0.75f + 0.25f;
    float3 orient = normalize((f.xyz - 0.5f) * 2.0f);
    float3 up = NG_Misc.x > 0.5f ? normalize(float3(0.0f, 0.0f, 1.0f) + orient) : float3(0.0f, 0.0f, 1.0f);

    // Per-blade variation from the tuft's place and the blade index.
    float2 key = inst.xy * 0.03125f + blade * float2(7.13f, 3.71f);
    float h1 = Hash(key);
    float h2 = Hash(key + 17.17f);
    float h3 = Hash(key + 41.3f);
    float h4 = Hash(key + 97.9f);

    // The tuft's size step, on the axes the grass scales (uniform or height only), as the game.
    float3 scale = (0.01f * inst.w) * ScaleMask.xyz + 1.0f;
    float height = NG_Shape.x * scale.z * NG_Misc.z * (0.75f + 0.5f * h1) * keep;
    float spread = NG_Shape.y * scale.x;

    // Where the blade stands in the tuft (even over the disk) and which way it faces.
    float ang = h2 * 6.2831853f;
    float3 base = inst.xyz + float3(cos(ang), sin(ang), 0.0f) * (spread * sqrt(h3));
    float fa = h4 * 6.2831853f;
    float3 facing = float3(cos(fa), sin(fa), 0.0f);
    float3 dir = normalize(facing - up * dot(facing, up));
    float3 sideVec = cross(up, dir);

    // Wind: the game's per-tuft phase and magnitude (the far cards sway with it), a little jitter per blade, a flutter across.
    float phase = (inst.x + inst.y) * 0.0078125f + WindData.w;
    phase = frac(phase * 0.159154937f + 0.5f) * 6.28318548f - 3.14159274f;
    float swing = sin(phase + h1 * 1.5f);
    float3 wind = float3(WindData.xy * (swing * WindData.z), 0.0f)
                + sideVec * (sin(phase * 2.3f + h2 * 6.2831853f) * WindData.z * NG_Var.z);

    // The blade: a quadratic Bezier from the root, leaning its facing way by the bend, the wind pulling the top.
    float bend = NG_Shape.w * (0.6f + 0.8f * h3);
    float3 p0 = base;
    float3 p1 = base + up * (height * 0.55f) + dir * (height * bend * 0.25f) + wind * 0.3f;
    float3 p2 = base + up * (height * (1.0f - 0.3f * bend)) + dir * (height * bend) + wind;
    float u = 1.0f - t;
    float3 pos = (u * u) * p0 + (2.0f * u * t) * p1 + (t * t) * p2;
    float3 tangent = normalize(2.0f * u * (p1 - p0) + 2.0f * t * (p2 - p1));

    // Width tapers to the tip and grows with distance (fewer, wider blades keep the far coverage).
    float width = height * NG_Shape.z * (1.0f + depth * NG_Lod.z) * (1.0f - t * t);
    pos += sideVec * (side * 0.5f * width);

    float3 normal = normalize(cross(sideVec, tangent));
    normal = normalize(normal + sideVec * (side * 0.35f));   // a rounded blade: the normal tilts toward each side

    OUT.position = mul(ModelViewProj, float4(pos, 1.0f));
    OUT.clip = OUT.position;
    OUT.params = float4(t, IN.packed.w, h4, lightScale);
    OUT.normal = float4(normal, AddlParams.x);
    OUT.sunDir = float4(DiffuseDir, 0.0f);
    OUT.ambient = float4(AmbientColor.rgb, 1.0f);
    OUT.sun = float4(DiffuseColor, 1.0f);

    float fogDist = length(float3(OUT.position.xy, OUT.position.w - OUT.position.z));
    float fogStrength = 1.0f - saturate((FogParam.x - fogDist) / FogParam.y);
    OUT.fog.rgb = FogColor.rgb;
    OUT.fog.w = pow(fogStrength, FogParam.z);

    return OUT;
};
