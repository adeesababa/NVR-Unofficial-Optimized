// UNOFFICIAL 2026-10-16 cloth wind ([Shaders.Cloth.Main], core/ClothWind.h): capes, dusters, flags, banners, curtains move in the
// wind by maths in the vertex shader alone -- no physics, no CPU work per vertex. Which objects: textures whose path holds one of
// the Keywords (per draw, by NVR). Vertex shaders only (ObjectTemplate, Shadows\ShadowMap.vso).
//
// Per draw NVR sets (vertex shader constants; strength 0 = this draw is not cloth and nothing below runs):
//   c248 ClothA: x strength 0-1, y mask (0 vertex colour RED: 1 = loose, 0 = pinned; 1 = whole mesh; 2 = by height: pinned at the
//                top, loosest at the bottom -- for meshes without an authored red mask; 3 = the same by the vertex's BIND-POSE
//                height (the mesh's own position before skinning: worn clothing; independent of the pose, the actor's place and
//                the space the bones put it in); the fraction above 3 is the height (/1000) below which legs and boots stay still), z ripple 0-1, w flutter 0-1
//   c249 ClothB: xyz wind direction in the vertex shader's space (unit), w cycles per unit along the wind (1 / wavelength)
//   c250 ClothC: x main wave phase, y gust phase, z/w flutter phases -- in cycles, wrapped on the CPU (double precision), so the
//                motion never jitters after long play
//   c251 ClothD: x intensity (units), y ripple cycles per unit, z top height (mask 2), w 1 / (top - bottom) (mask 2)
//   c252 ClothE: (2026-10-19) worn clothing follows its wearer: xyz the offset (units: trailing behind the movement + the stride
//                swish), w the lag behind a turn (radians, around the upright axis) -- 0 for everything but worn clothing
//   c253 ClothF: xy the wearer's upright axis in the vertex shader's space (the turn lag swings points around it)
// The same function runs in every pass of the object (the extra lamp passes test depth EQUAL, so they must land on the same spot)
// and in the shadow maps.
//
// Wave shape: Crytek's smooth triangle wave (Sousa, "Vegetation Procedural Animation and Shading in Crysis", GPU Gems 3 ch. 16,
// 2007) -- cheaper than sin() and smooth. Normals: rebuilt from two moved neighbours (finite differences along the tangent and
// the binormal), so lighting and shadows follow the folds.
float4 ClothA : register(c248);
float4 ClothB : register(c249);
float4 ClothC : register(c250);
float4 ClothD : register(c251);
float4 ClothE : register(c252);
float4 ClothF : register(c253);

float ClothTri(float c) {	// 0..1, period 1, smooth at the turns
    const float t = abs(frac(c + 0.5f) * 2.0f - 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// The displacement at point p (the shader's space) with surface normal n; n = 0 gives the push along the wind only.
float3 ClothDisplacement(float3 p, float3 n) {
    const float along = dot(p, ClothB.xyz) * ClothB.w;
    const float wave = ClothTri(along - ClothC.x) * 2.0f - 1.0f;                                      // billow, travels with the wind
    const float gust = 0.7f + 0.3f * ClothTri(ClothC.y + dot(p, float3(0.0021f, 0.0027f, 0.0013f)));   // slow, varies over the cloth
    const float r = ClothD.y;
    const float ripple = 0.6f * (ClothTri(dot(p, float3(0.71f, 0.53f, 0.37f)) * r + ClothC.z) * 2.0f - 1.0f)
                       + 0.4f * (ClothTri(dot(p, float3(-0.43f, 0.81f, 0.59f)) * r * 1.7f - ClothC.w) * 2.0f - 1.0f);
    return ClothD.x * gust * (ClothB.xyz * (0.6f + 0.4f * wave) + n * (ClothA.z * wave + ClothA.w * ripple));
}

// The wearer's movement at point p (worn clothing; core/ClothWind.h MotionOf): the same offset everywhere, plus a small turn of the
// point around the wearer's upright axis. Under the same mask as the wind (pinned shoulders, still boots).
float3 ClothMotion(float3 p) {
    const float2 r = p.xy - ClothF.xy;
    return ClothE.xyz + ClothE.w * float3(-r.y, r.x, 0.0f);
}

// bindP: the vertex before skinning (mode 3 only; for unskinned meshes it is p itself).
float ClothMask(float4 vertexColor, float3 p, float3 bindP) {
    float m;
    [flatten] if (ClothA.y > 2.5f) {	// worn clothing: smooth from pinned (top part) to loose (hem)
        m = saturate((ClothD.z - bindP.z) * ClothD.w);
        m = m * m * (3.0f - 2.0f * m);
        const float cut = (ClothA.y - 3.0f) * 1000.0f;	// legs and boots (meshes with the feet at z 0): fades to 0 below this height, full 10 units above
        [flatten] if (cut > 0.5f) m *= saturate((bindP.z - cut) * 0.1f);
    }
    else m = ClothA.y > 1.5f ? saturate((ClothD.z - p.z) * ClothD.w) : (ClothA.y > 0.5f ? 1.0f : saturate(vertexColor.r));
    return ClothA.x * m;
}

// Moves the vertex and rebuilds tangent, binormal and normal. Every pass of an object must call it with the same inputs.
void ClothWind(inout float3 p, inout float3 T, inout float3 B, inout float3 N, float4 vertexColor, float3 bindP) {
    [branch] if (ClothA.x > 0.0f) {
        const float mask = ClothMask(vertexColor, p, bindP);
        const float e = 2.0f;
        const float3 n = normalize(N);
        const float3 d0 = mask * (ClothDisplacement(p, n) + ClothMotion(p));
        const float3 dt = mask * (ClothDisplacement(p + T * e, n) + ClothMotion(p + T * e));
        const float3 db = mask * (ClothDisplacement(p + B * e, n) + ClothMotion(p + B * e));
        const float3 t2 = T * e + (dt - d0);
        const float3 b2 = B * e + (db - d0);
        // The turn of the frame: the moved frame's normal minus the unmoved one's, added to the mesh's own normal -- so with no
        // displacement every vector comes back exactly as it was (the game's tangent frames are not always square).
        float3 n0 = normalize(cross(T, B));
        float3 n2 = normalize(cross(t2, b2));
        n0 = dot(n0, N) < 0.0f ? -n0 : n0;
        n2 = dot(n2, N) < 0.0f ? -n2 : n2;
        p += d0;
        T = normalize(t2) * length(T);
        B = normalize(b2) * length(B);
        N = normalize(n + (n2 - n0)) * length(N);
    }
}

// Shadow maps: position only, but the same displacement as the lit passes (2026-10-17: it used n = 0 and so left out the part along
// the normal -- up to ~0.85 of the intensity -- and the shadow caster sat off the cloth, which then shadowed itself in patches).
// The normal is in the same space as p and need not be unit (a missing one reads as zero: the push along the wind only).
float3 ClothWindPosition(float3 p, float3 n, float4 vertexColor, float3 bindP) {
    [branch] if (ClothA.x > 0.0f) {
        const float l = length(n);
        p += ClothMask(vertexColor, p, bindP) * (ClothDisplacement(p, l > 1e-4f ? n / l : 0.0f) + ClothMotion(p));
    }
    return p;
}

// ObjectTemplate: the tangent frame is a float3x3 (rows tangent, binormal, normal).
void ClothWindTBN(inout float3 p, inout float3x3 tbn, float4 vertexColor, float3 bindP) {
    float3 T = tbn[0], B = tbn[1], N = tbn[2];
    ClothWind(p, T, B, N, vertexColor, bindP);
    tbn = float3x3(T, B, N);
}
