#pragma once
#include "LinearPreDecode.h"
#include "GpuProfiler.h"
#include "PointShadowForward.h"

// Ported 2026-10-28 from Conor (mrfury97)'s fork onto P84 v3 (github.com/mrfury97/NVR-Unofficial-Optimized pull request 1, branch
// manylamps-one-pass, commits 82ef371b 7127ac3d 3e73bf06, GPL-3.0-or-later; user: "seems like a good fit"); notes docs/next/MANYLAMPS-PORT.md.
//
// UNOFFICIAL 2026-10-27 ManyLampsOnePass ([Main.Main.Misc] ManyLampsOnePass, DXVK only; with MoreLampsPerObject; it switches LampsInOnePass's
// one-pass limit on by itself since the port):
// shiny objects lit by 6 to 13 lamps are drawn in ONE pass with all their lamps, instead of the game's 5-7 passes (light-only pass,
// extra lamp passes, texture pass, highlight passes, fog pass). It needs a restart to come on (its shaders); after that its lamp limit
// is live: off goes back to LampsInOnePass's 5 + f with the same shaders, for an F10 comparison at one spot.
// Measured 2026-10-27, Vault 21 lobby, same spot (one launch, 12 -> 5 -> 12): world draws 6,015 vs 8,677 a frame, frame time 20.3-21.0
// vs 22.9-23.1 ms, GPU world scene 12.2-12.4 vs 14.5-14.6 ms.
//
// Why the 6-lamp limit was there (read in the decompile):
//  - BSShaderPPLightingProperty::GetRenderPasses_2x (0x00BDF790) sends a shiny object to the one many-lamp pass (AddPass_Opt
//    0x00BDE9B0) only while its lamps are at most 2 + f (LampsInOnePass: 5 + f; ADD EAX,imm8 at 0x00BE00E0). Past that: several passes.
//  - The game's own light arrays hold 10 lights: ShadowLightShader::kLightData (0x011FD9A8, 10 x float4, followed by spPixelShaders) and
//    the pixel shader's PSLightPosition (c19-c26, then Toggles c27). AddPass_Opt caps the pass at 6 + f entries (MoreLampsPerObject).
//  - The 6-lamp shaders carry each lamp's direction in its own interpolator: at 6 every one of ps_3_0's 10 inputs is taken.
// So past 6 lamps nothing of the game's can be stretched. Instead:
//  - the compare is raised to 12 + f (LampsInOnePass::BeginFrame writes CompareImm), the game's cap stays 6 + f (its arrays stay safe);
//  - before every draw (Render.cpp's RenderPassStandard/SkinnedHook, and at a shader bind) NVR writes the object's lamps itself into
//    pixel registers c37-c63 (free in every ObjectTemplate pixel shader): c37.x the count, then per lamp its position and reach in the
//    object's own space, exactly as the game makes them for its shaders (c38 + 2k; ShadowLightShader::SetupGeometryConstants_Lights
//    0x00B78A90: the lamp's world position plus ShadowSceneNode::kLightingOffset through BSUtilities::GetInverseWorldMatrix 0x00C4C2D0,
//    the reach divided by the object's scale unless skinned) and its colour as the game computes it (c39 + 2k;
//    SetupGeometryOpt_LightsSpecular 0x00B7CB00: diffuse x dimmer x the light's LOD dimmer), decoded to linear on the CPU when linear
//    lighting is compiled in. The lamps are the pass's own (light 1 on; light 0 is the cell's directional light) and, for a many-lamp
//    pass the game capped, the object's other active lamps (BSShaderLightingProperty light list +0x60, read as
//    GetNumberOfActiveNonShadowLights 0x00B707D0 does), brightest first as the game sorts them;
//  - the game's own constant upload for these passes (NiD3DShaderConstantMap::SetShaderConstants 0x00E826D0, the call at 0x00E8283B)
//    writes zeros into c39-c86 while it sets up each draw, after NVR's upload: ConstantFilter's pixel hook calls AfterPixelWrite, which
//    puts the block back at once (the game's own constants for these shaders are all below c32);
//  - the 4-lamp-and-up shiny shaders (ObjectTemplate.hlsl MANY_LAMPS: SLS2025-2027.vso, SLS2034-2036.pso) carry the surface's tangent
//    frame (in the vertex's own space, as the game's lamp directions are made) instead of six lamp directions, and light every lamp per
//    pixel from these registers.
// The game's kLightingOffset is 0 in the world pass and set only while the first person is drawn (TESMain::DrawWorld_MTRender1stPerson ->
// ShadowSceneNode::SetLightingOffset 0x008750E0); for first-person draws it is learned from the game's own value for lamp 1 after each
// such draw (AfterDraw) and used for the next.
// Lamps 7-24 get no forward cube-map shadow (only six cube slots); the screen-space lamp shadows cover them as before.
// 2026-10-27 (PassFamilyView, Vault 21: the floor and wall pieces lit by 13+ lamps, 15-16 there, were still drawn in several passes
// and looked different from their one-pass neighbours): 24 lamps, the block in three ranges of pixel registers no NVR pixel shader
// uses: c37-c63 (the count and lamps 1-13), c76-c87 (lamps 14-19, between LinearPreDecode's c64-c75 and terrain's c88-c97) and
// c98-c107 (lamps 20-24, below the sun shadows' c108). The game's zeros (c39-c86) reach the second range too: AfterPixelWrite
// restores any range a write overlaps.
namespace ManyLamps {

	enum { MaxLamps = 24, FirstRegister = 37, Registers = 1 + 2 * MaxLamps };	// rows of the block (see Ranges)
	enum : UInt8 { CompareImm = MaxLamps - 1 };		// lamps <= 23 + f in one pass (f = 1 when the directional light is dark: 24)
	struct Range { UINT reg, row, rows; };
	constexpr Range Ranges[3] = { { 37, 0, 27 }, { 76, 27, 12 }, { 98, 39, 10 } };	// ObjectTemplate.hlsl NVR_ManyLamps / B / C
	static_assert(27 + 12 + 10 == Registers, "ManyLamps ranges");

	inline int Wanted = -1;			// the setting at startup (-1 not read yet)
	inline int State = -1;			// -1 not decided yet, 0 off, 1 on
	inline bool LinearCompiled = false;	// [Shaders.PBR.Linear] Enabled at startup: the shaders decode PSLightColor (LINEAR_LIGHTING_ON)
	inline float Block[Registers][4] = {};
	inline UINT BlockRegisters = 0;		// rows of the block on the device now (1 + 2 x count)
	inline const void* LastPass = nullptr;	// the pass whose lamps are in the block now
	inline UInt32 LastFrame = 0;
	inline UInt32 FrameId = 1;
	inline bool Bound = false;			// one of our shaders is bound (OnBind)
	inline bool OwnUpload = false;		// NVR's own upload is under way (AfterPixelWrite ignores it)
	inline bool FirstPerson = false;	// the pass being set up is drawn in the first-person pass
	inline D3DXVECTOR3 FirstPersonOffset(0.0f, 0.0f, 0.0f);	// the game's kLightingOffset for first-person draws, as learned
	inline bool HookWarned = false;

	// F10 statistics (one line per window)
	inline unsigned StatFrames = 0;
	inline unsigned long long StatDraws = 0, StatExtraDraws = 0, StatExtras = 0, StatOver = 0, StatByCount[MaxLamps + 1] = {};

	// The setting as the shaders were compiled with it (ShaderRecord::BuildDefines reads this too): startup value, DXVK and
	// MoreLampsPerObject (the shaders' SPECULAR_LAMPS6) required.
	inline bool WantedAtStartup() {
		if (Wanted < 0) Wanted = TheSettingManager->GetSettingI("Main.Main.Misc", "ManyLampsOnePass") != 0 &&
			TheSettingManager->GetSettingI("Main.Main.Misc", "MoreLampsPerObject") != 0;
		return Wanted != 0;
	}
	inline bool ShadersWant() {
		return WantedAtStartup() && TheRenderManager && !strncmp(TheRenderManager->D3D9RuntimeDescription(), "DXVK", 4);
	}

	inline bool Active() {
		if (State < 0 && TheRenderManager && TheRenderManager->device) {
			State = ShadersWant() ? 1 : 0;
			LinearCompiled = TheSettingManager->GetSettingI("Shaders.PBR.Linear", "Enabled") != 0;
			if (WantedAtStartup())
				Logger::Log(State ? "UNOFFICIAL ManyLampsOnePass: on (shiny objects with up to %d lamps in one pass; lamps in c37-c63, c76-c87, c98-c107)"
					: "UNOFFICIAL ManyLampsOnePass: off (needs DXVK; the shaders keep 6 lamp slots)", (int)MaxLamps);
		}
		return State == 1;
	}

	inline bool IsOptPass(UInt16 e) { return e >= 0xC3 && e <= 0xD6; }

	// The game's world -> object-space matrix for its lamp constants (identity rotation minus NiRenderer::kPosAdjust for skinned objects).
	typedef void(__cdecl* InverseWorldFn)(const NiTransform*, const NiSkinInstance*, D3DXMATRIX*);
	inline const InverseWorldFn GetInverseWorldMatrix = (InverseWorldFn)0x00C4C2D0;
	inline D3DXMATRIX ToObject;		// this pass's
	inline float ReachScale = 1.0f;	// 1 / the object's scale (1 when skinned), as SetupGeometryConstants_Lights
	inline D3DXVECTOR3 LightingOffset(0.0f, 0.0f, 0.0f);	// this pass's kLightingOffset as NVR applies it

	// One lamp into the block; false when it is not a usable point light.
	inline bool Put(int k, const ShadowSceneLight* light, bool linear, float gamma) {
		const NiPointLight* source = light->sourceLight;
		if (!source || !light->bPointLight) return false;
		const D3DXVECTOR3 world(source->m_worldTransform.pos.x + LightingOffset.x, source->m_worldTransform.pos.y + LightingOffset.y,
			source->m_worldTransform.pos.z + LightingOffset.z);
		D3DXVECTOR3 local;
		D3DXVec3TransformCoord(&local, &world, &ToObject);
		float* p = Block[1 + 2 * k];
		p[0] = local.x;
		p[1] = local.y;
		p[2] = local.z;
		p[3] = source->Spec.r * ReachScale;
		float dimmer = source->Dimmer;
		if (!*(const bool*)0x011F941E && dimmer > 1.0f) dimmer = 1.0f;	// BSShaderManager::bHDR
		const float lod = light->fLODDimmer;
		const float raw[4] = { source->Diff.r * dimmer * lod, source->Diff.g * dimmer * lod, source->Diff.b * dimmer * lod, 0.0f };
		float* c = Block[2 + 2 * k];
		if (linear) LinearPreDecode::DecodeFast(c, raw, gamma);
		else memcpy(c, raw, sizeof(raw));
		return true;
	}

	// The block's rows in use onto the device (NVR's own upload: AfterPixelWrite ignores it).
	inline void Upload() {
		OwnUpload = true;
		for (const Range& r : Ranges) {
			if (BlockRegisters <= r.row) break;
			TheRenderManager->device->SetPixelShaderConstantF(r.reg, &Block[r.row][0], min(r.rows, BlockRegisters - r.row));
		}
		OwnUpload = false;
	}

	// The object's lamps for this pass into the block.
	inline void SetForPass(const PointShadowForward::RenderPassView* pass) {
		int count = 0;
		const bool linear = LinearCompiled && LinearPreDecode::Gamma > 1.0f;	// as LinColor: decoded only above gamma 1
		const float gamma = LinearPreDecode::Gamma;
		if (pass && pass->lights && pass->geometry) {
			NiGeometry* geometry = pass->geometry;
			GetInverseWorldMatrix(&geometry->m_worldTransform, geometry->skinInstance, &ToObject);
			ReachScale = !geometry->skinInstance && geometry->m_worldTransform.scale > 0.0f ? 1.0f / geometry->m_worldTransform.scale : 1.0f;
			LightingOffset = FirstPerson ? FirstPersonOffset : D3DXVECTOR3(0.0f, 0.0f, 0.0f);
			for (int i = 1; i < pass->numLights && count < MaxLamps; i++) {
				const ShadowSceneLight* light = pass->lights[i];
				// Lamp k must stay light k + 1 of the pass (the shader gives it that light's cube-map shadow slot): one that is not a usable
				// point light keeps its place with reach 0, which the shader skips.
				if (!light || !Put(count, light, linear, gamma)) memset(Block[1 + 2 * count], 0, 2 * sizeof(Block[0]));
				count++;
			}
			// A many-lamp pass the game capped (AddPass_Opt keeps 6 + f entries of maxLights): its other lamps from the object's list.
			if (pass->maxLights > pass->numLights && IsOptPass(pass->passEnum)) {
				const UInt8* property = (const UInt8*)geometry->propertyState.m_spShadeProperty;
				int extras = 0;
				for (const UInt32* node = property ? *(const UInt32* const*)(property + 0x60) : nullptr; node; node = (const UInt32*)node[0]) {
					const UInt8* light = (const UInt8*)node[2];
					if (!light || *(const UInt16*)(light + 0x110) == 0xFF) continue;	// as GetNumberOfActiveNonShadowLights
					const UInt8* niLight = *(const UInt8* const*)(light + 0xF8);
					if (!niLight || (niLight[0x30] & 1) || light[0xEC]) continue;
					bool known = false;
					for (int i = 1; i < pass->numLights && !known; i++) known = (const UInt8*)pass->lights[i] == light;
					if (known) continue;
					if (count == MaxLamps) { StatOver++; continue; }
					if (Put(count, (const ShadowSceneLight*)light, linear, gamma)) { count++; extras++; }
				}
				if (GpuTimer::Enabled && extras) { StatExtraDraws++; StatExtras += extras; }
			}
		}
		Block[0][0] = (float)count;
		Block[0][1] = Block[0][2] = Block[0][3] = 0.0f;
		BlockRegisters = 1 + 2 * count;
		Upload();
		LastPass = pass;
		LastFrame = FrameId;
		if (GpuTimer::Enabled) { StatDraws++; StatByCount[count]++; }
	}

	// Only while one of NVR's many-lamp shiny shaders is bound (SLS2034-2036.pso, NVR's record in place): other pixel shaders, the
	// game's or NVR's, may hold their own values in c37-c63 for a whole run of draws (set once at their bind), and a write per object
	// would overwrite them.
	inline bool IsOurs(const char* name) {
		return name && !strncmp(name, "SLS203", 6) && name[6] >= '4' && name[6] <= '6' && !strcmp(name + 7, ".pso");
	}

	// Before every object's draw (Render.cpp), and when the game binds a pass's shaders (SetShadersHook, BSBatchRenderer::BeginPass: the run's
	// first object, from BSShaderManager::pCurrentRenderPass).
	inline void ForPass(const PointShadowForward::RenderPassView* pass, bool firstPerson) {
		if (!Bound || !Active()) return;
		FirstPerson = firstPerson;
		SetForPass(pass);
	}
	inline void OnBind(const char* pixelShaderName, bool nvrRecord, bool firstPerson) {
		Bound = nvrRecord && IsOurs(pixelShaderName) && Active();
		if (!Bound) return;
		FirstPerson = firstPerson;
		const PointShadowForward::RenderPassView* pass = *(const PointShadowForward::RenderPassView* const*)0x011F91E0;
		if (pass && (pass != LastPass || LastFrame != FrameId)) SetForPass(pass);
	}

	// ConstantFilter's pixel hook, after every upload: one that covered part of the block while one of our shaders is bound (the game's
	// zeros in c39-c86, see above) gets the block put back at once.
	inline void AfterPixelWrite(UINT start, UINT count) {
		if (OwnUpload || !Bound || !BlockRegisters || LastFrame != FrameId) return;
		bool overlaps = false;
		for (const Range& r : Ranges) {
			if (BlockRegisters <= r.row) break;
			const UINT used = min(r.rows, BlockRegisters - r.row);
			if (start < r.reg + used && start + count > r.reg) { overlaps = true; break; }
		}
		if (overlaps) Upload();
	}

	// After a draw with one of our shaders (Render.cpp): for first-person draws, the game's own lamp 1 (c19, object space with its
	// kLightingOffset) back to world space, minus the lamp's world position, is the offset for the next first-person draw.
	inline void AfterDraw(const PointShadowForward::RenderPassView* pass) {
		if (!FirstPerson || !Bound || pass != LastPass || !Active()) return;
		if (pass->numLights < 2 || !pass->lights || !pass->lights[1] || !pass->lights[1]->sourceLight) return;
		float game[4];
		D3DXMATRIX toWorld;
		if (FAILED(TheRenderManager->device->GetPixelShaderConstantF(19, game, 1)) || !D3DXMatrixInverse(&toWorld, nullptr, &ToObject)) return;
		D3DXVECTOR3 world;
		const D3DXVECTOR3 local(game[0], game[1], game[2]);
		D3DXVec3TransformCoord(&world, &local, &toWorld);
		const NiPoint3& lamp = pass->lights[1]->sourceLight->m_worldTransform.pos;
		const D3DXVECTOR3 offset(world.x - lamp.x, world.y - lamp.y, world.z - lamp.z);
		if (_finite(offset.x) && _finite(offset.y) && _finite(offset.z)) FirstPersonOffset = offset;
	}

	// Once a frame (Render.cpp). restoreHooked: ConstantFilter's pixel hook is in (without it the game's zeros stay and the lamps are lost).
	inline void BeginFrame(bool restoreHooked) {
		FrameId++;
		if (!Active()) return;
		if (!restoreHooked && !HookWarned) {
			HookWarned = true;
			Logger::Log("[ERROR] UNOFFICIAL ManyLampsOnePass: the shader-constant hooks are not in (see SkipRedundantConstants above); objects lit by many lamps will be too dark. Set [Main.Main.Misc] ManyLampsOnePass = false.");
		}
		if (!GpuTimer::Enabled) { StatFrames = 0; return; }
		if (++StatFrames < 240) return;
		const double f = StatFrames;
		char line[512]; int n = 0;
		n += sprintf_s(line + n, sizeof(line) - n, "MANY LAMPS %u frames, per frame: draws %.0f, lamps per draw", StatFrames, StatDraws / f);
		for (int k = 0; k <= MaxLamps; k++) if (StatByCount[k]) n += sprintf_s(line + n, sizeof(line) - n, " %d:%.1f", k, StatByCount[k] / f);
		n += sprintf_s(line + n, sizeof(line) - n, " | capped many-lamp passes given their other lamps %.1f (%.1f lamps), lamps past %d dropped %.2f",
			StatExtraDraws / f, StatExtras / f, (int)MaxLamps, StatOver / f);
		Logger::Log("%s", line);
		StatFrames = 0; StatDraws = StatExtraDraws = StatExtras = StatOver = 0;
		memset(StatByCount, 0, sizeof(StatByCount));
	}
}
