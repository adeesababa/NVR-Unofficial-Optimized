#pragma once

// UNOFFICIAL 2026-10-28 RT lamp shadows ([Shaders.ShadowsInteriors.RT], docs/next/FORK-RT-LAMPS.md). Indoors, each lamp's forward
// shadow (PointShadowForward.hlsl) is read from a traced result instead of its cube map. The tracing is an RT core user (RtCore.h,
// FORK-RT-CORE.md): at the core's point of the frame (after the depth and normals) the helper runs Compute/Rt/RtLampTrace.spv for the
// up to 9 shadowed lamps of this frame (cube slots 0-8) and the result lands in TESR_RtLampShadow (G32R32F). The object shaders draw
// earlier in the frame, so they use the PREVIOUS frame's result: they reproject into the frame it was traced for and use it where that
// frame saw the same surface; elsewhere the cube map, as before. The lamps are still picked, ranked and given cube slots as before.
//
// POINT_SHADOW_RT is defined at startup only with the setting and the per-object lamp shadows on and a d3d9.dll with the RT core's
// interface (NVR DXVK patch 0028). Without it every shader is exactly what it was. With it, but no ray tracing for the helper (the
// GTX 1070), the core stays off, Mode stays 0 and every lamp keeps its cube map (same picture, the RT shaders' extra branch only).
//
// Per draw (PointShadowForward::SetForPass): c206-c209 the reprojection, c215 mode / tolerance / depth unit / people-from-cube, c216-c217
// the draw's lights' traced slots, the result on s15 (the sixth lamp's cube stage: see the shader).

#include <algorithm>
#include "PointShadowSchedule.h"	// PointShadowSlotState (KeepCube)
#include "LampFixtures.h"
#include "DxvkCompute.h"
#include "RtCore.h"

namespace RtLampShadows {

	inline constexpr int TracedSlots = 9;		// RtLampTrace.comp: 6 lamps in r, 3 + the depth code in g (exact floats)
	inline constexpr const char* kSection = "Shaders.ShadowsInteriors.RT";

	// Instance mask bits the RT core gives the scene (FORK-RT-CORE.md: 0x01 land, 0x02 objects); ours for the lamps' own parts,
	// through the core's mask hook (InstanceMask below): 0x04 lamp glass and glow meshes, 0x08 the rest of a lamp model. The core puts
	// alpha-tested casters under 0x10, non-opaque (agreed scheme 2026-10-29); the lamp rays do not test alpha yet, so they leave 0x10
	// out and those casters let lamp light through (the cube maps still draw them alpha-tested: a known difference).
	inline constexpr UINT MaskGlass = 0x04, MaskLampModel = 0x08, MaskAlphaTested = 0x10;

	inline bool CompiledIn = false;			// POINT_SHADOW_RT in the object shaders (fixed at startup)
	inline char Status[200] = "not checked yet";

	// The trace pass's uniform block (RtLampTrace.comp, std140, 304 bytes; the core allows 512)
	struct PassConstants {
		float size[4], forward[4], right[4], up[4], origin[4], params[4], params2[4];
		float lamps[TracedSlots][4];
		float radius[3][4];
		float stops[3][4];						// per lamp: where its rays stop short of it, at least LampStop (its own globe / bulb / glass)
		float flags[4];							// x: OneSided (diagnostic: count only casters facing the lamp, as the cube maps do)
	};
	static_assert(sizeof(PassConstants) <= 512, "RtCore: 512 bytes of constants a pass");

	// This frame's lamps (Submit, from PointShadowForward::Publish) for Record
	struct SlotLamp { int slot; D3DXVECTOR3 position; float reach; float fade; float stop; };	// stop: past the lamp's own glowing part (0 none)
	inline SlotLamp Lamps[TracedSlots] = {};
	inline int LampCount = 0;
	inline bool LampsThisFrame = false;

	// Which lamps get traced (2026-10-29): the ones the screen uses most. Vault 21's lobby (447 lights, 64 cube slots): the lamps that
	// light the view held slots above 8, so tracing cube slots 0-8 traced lamps nothing on screen used (coverage 0 %). Now every draw
	// counts its lamps (NoteUse, by cube slot) and the next frame traces the MaxLamps most used; the result carries its own slot ->
	// traced-index map (TracedMap), so a frame's draws decode with the map of the frame they read.
	inline constexpr int MapSlots = 64;
	inline UInt32 DrawUse[MapSlots] = {};
	inline int PendingMap[MapSlots];		// this frame's trace: cube slot -> traced index (-1 none)
	inline int TracedMap[MapSlots];		// the newest result's
	inline void NoteUse(int cubeSlot) { if (cubeSlot >= 0 && cubeSlot < MapSlots) DrawUse[cubeSlot]++; }

	// The camera of the frame the newest result was traced for (Record) and whether it exists
	struct TracedCamera { D3DXMATRIX view; D3DXVECTOR3 location;	// location: the traced frame's eye in world space
		 float p11, p22, farZ; bool valid; };
	inline TracedCamera Traced = {};
	inline TracedCamera Pending = {};

	// Per draw
	inline float Mode = 0.0f;				// c215.x: 0 cube maps, 1 traced, 2 compare view, 3 coverage view, 4 one lamp (DebugView 3)
	inline float DebugLamp = 0.0f;			// c217.z: DebugView 3's traced lamp index
	inline int MaxLamps = 8;				// cube slots below min(this, 9) use the traced result
	inline float Reproject[4][4] = {};		// c206-c209
	inline float RtConstant[4] = {};		// c215
	inline float Uploaded[7][4] = {};
	inline bool UploadedValid = false;

	inline int imageDepth = -1, imageNormals = -1, imageResult = -1, shader = -1;
	inline IDirect3DTexture9* ResultTexture = nullptr;
	inline IDirect3DSurface9* ResultSurface = nullptr;
	inline UINT TextureWidth = 0, TextureHeight = 0;
	inline UInt32 FramesTraced = 0;

	// Our part of the instance mask (MaskHook below): lamp glass and glow meshes (LampFixtures::LampPart), the rest of a
	// lamp model listed in the cell (fixtures, shades, chandeliers, fans); 0 = the core's own (land / object).
	inline UInt32 InstanceMask(NiGeometry* geometry, bool actor) {
		if (actor || !geometry || TheShaderManager->GameState.isExterior) return 0;
		if (LampFixtures::LampPart(geometry)) return MaskGlass;
		if (LampFixtures::ModelIndexOf(geometry) >= 0) return MaskLampModel;
		return 0;
	}

	// RenderManager, right after DxvkCompute::Initialize and before any shader is loaded: decides POINT_SHADOW_RT for the session.
	inline void Initialize() {
		CompiledIn = false;
		for (int i = 0; i < MapSlots; i++) PendingMap[i] = TracedMap[i] = -1;
		ID3D9NvrExtRt* rt = nullptr;
		ID3D9NvrExt* ext = DxvkCompute::Extension();
		if (!TheSettingManager->GetSettingI(kSection, "Enabled"))
			strcpy_s(Status, "off ([Shaders.ShadowsInteriors.RT] Enabled false at startup)");
		else if (!TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Forward", "Enabled"))
			strcpy_s(Status, "off: needs the per-object lamp shadows ([Shaders.ShadowsInteriors.Forward] Enabled)");
		else if (!ext || FAILED(ext->QueryInterface(__uuidof(ID3D9NvrExtRt), (void**)&rt)) || !rt)
			strcpy_s(Status, "off: needs NVR DXVK with the RT core (patch 0028)");
		else {
			rt->Release();
			CompiledIn = true;
			strcpy_s(Status, "lamp shaders compiled to read traced lamp shadows (used once the RT helper runs)");
		}
		if (TheSettingManager->GetSettingI(kSection, "Enabled")) Logger::Log("UNOFFICIAL RT lamp shadows: %s", Status);
	}

	inline const D3DXVECTOR4* Constant(const char* name) {
		auto found = TheShaderManager->ConstantsTable.find(name);
		return found == TheShaderManager->ConstantsTable.end() ? nullptr : found->second;
	}
	inline IDirect3DBaseTexture9* Texture(const char* name) {
		auto found = TheTextureManager->TextureNames.find(name);
		return found == TheTextureManager->TextureNames.end() || !found->second ? nullptr : *found->second;
	}

	// PointShadowForward::Publish, once a frame indoors after the slots are known (before the world draws): this frame's lamps for
	// the trace, and the reprojection into the newest result for this frame's draws.
	inline void Submit(const SlotLamp* lamps, int count) {
		Mode = 0.0f;
		LampCount = 0;
		LampsThisFrame = false;
		if (!CompiledIn) return;
		MaxLamps = (std::min)((std::max)(TheSettingManager->GetSettingI(kSection, "MaxLamps"), 0), TracedSlots);
		// The MaxLamps lamps the last frame's draws used most (ties: the lower cube slot, i.e. NVR's own order); unused lamps never
		for (int i = 0; i < MapSlots; i++) PendingMap[i] = -1;
		int order[MapSlots]; int candidates = 0;
		for (int i = 0; i < count && candidates < MapSlots; i++)
			if (lamps[i].slot >= 0 && lamps[i].slot < MapSlots && DrawUse[lamps[i].slot] > 0) order[candidates++] = i;
		std::sort(order, order + candidates, [&](int a, int b) {
			const UInt32 ua = DrawUse[lamps[a].slot], ub = DrawUse[lamps[b].slot];
			return ua != ub ? ua > ub : lamps[a].slot < lamps[b].slot;
		});
		for (int k = 0; k < candidates && LampCount < MaxLamps; k++) {
			PendingMap[lamps[order[k]].slot] = LampCount;
			Lamps[LampCount++] = lamps[order[k]];
		}
		if (count) memset(DrawUse, 0, sizeof(DrawUse));
		LampsThisFrame = TheSettingManager->GetSettingI(kSection, "Live") != 0;

		// The newest result: traced last frame (Record moves Pending to Traced once its copy-out is recorded)
		if (!Traced.valid || !ResultTexture || !LampsThisFrame || !RtCore::Running()) return;
		const D3DXMATRIX& v = Traced.view;
		const D3DXVECTOR3 right(v._11, v._21, v._31), up(v._12, v._22, v._32), forward(v._13, v._23, v._33);
		const D3DXVECTOR4& camera = TheRenderManager->CameraPosition;
		// A camera-relative point of this frame, seen from the traced frame's eye (that frame's CameraPosition: the eye is the scene's
		// origin, RtCore::Eye; 2026-10-29: Pointers::Generic::CameraLocation reads ~0 in game, which put the eye at the world origin)
		const D3DXVECTOR3 d(camera.x - Traced.location.x, camera.y - Traced.location.y, camera.z - Traced.location.z);
		const float rows[3][3] = { { right.x, up.x, forward.x }, { right.y, up.y, forward.y }, { right.z, up.z, forward.z } };
		for (int i = 0; i < 3; i++) {
			Reproject[i][0] = rows[i][0] * Traced.p11;
			Reproject[i][1] = rows[i][1] * Traced.p22;
			Reproject[i][2] = 0.0f;
			Reproject[i][3] = rows[i][2];
		}
		Reproject[3][0] = D3DXVec3Dot(&d, &right) * Traced.p11;
		Reproject[3][1] = D3DXVec3Dot(&d, &up) * Traced.p22;
		Reproject[3][2] = 0.0f;
		Reproject[3][3] = D3DXVec3Dot(&d, &forward);
		RtConstant[1] = (std::max)(TheSettingManager->GetSettingF(kSection, "DepthTolerance"), 0.004f);	// at least the depth code's step
		RtConstant[2] = log2f((std::max)(Traced.farZ, 2.0f)) / 4095.0f;
		RtConstant[3] = TheSettingManager->GetSettingI(kSection, "ActorsFromCubeMaps") ? 1.0f : 0.0f;
		const int debugView = TheSettingManager->GetSettingI(kSection, "DebugView");
		Mode = debugView == 1 ? 2.0f : debugView == 2 ? 3.0f : debugView == 3 ? 4.0f : 1.0f;
		DebugLamp = (float)(std::min)((std::max)(TheSettingManager->GetSettingI(kSection, "DebugLamp"), 0), TracedSlots - 1);
		RtConstant[0] = Mode;
	}

	// The slot a draw's light reads in the traced result, -1 for its cube map.
	inline float TracedSlot(int cubeSlot) {
		return Mode > 0.0f && cubeSlot >= 0 && cubeSlot < MapSlots && TracedMap[cubeSlot] >= 0 ? (float)TracedMap[cubeSlot] : -1.0f;
	}

	// UNOFFICIAL 2026-10-29 SkipTracedCubes ([Shaders.ShadowsInteriors.RT], on): the cost win. A lamp the trace covered last frame
	// keeps the cube map it has (the fallback where the traced frame did not see a point) instead of redrawing it: no redraw for the
	// schedule, moved casters or a moved light. Drawn as before when it has no cube map of its own yet (new lamp, new cell, another
	// light in the slot, another radius, a partial one), and, with ActorsFromCubeMaps on, while people move near it (their shadows
	// come from the cube map). ShadowManager's cube loop asks before drawing slot i.
	inline UInt32 SkippedCubes = 0, SkippedCubesFrames = 0;
	inline bool KeepCube(int slot, const PointShadowSlotState& last, const PointShadowSlotState& now) {
		if (!CompiledIn || Mode <= 0.0f || slot < 0 || slot >= MapSlots || TracedMap[slot] < 0) return false;
		if (!TheSettingManager->GetSettingI(kSection, "SkipTracedCubes")) return false;
		if (!last.valid || last.partial || last.light != now.light || last.cell != now.cell || last.texture != now.texture ||
			last.radius != now.radius) return false;
		if (!now.staticCasters && TheSettingManager->GetSettingI(kSection, "ActorsFromCubeMaps")) return false;
		SkippedCubes++;
		return true;
	}

	// PointShadowForward::SetForPass: slots = the draw's lights' TracedSlot (-1 none).
	inline void SetForPass(const float slots[6], bool force) {
		if (!CompiledIn) return;
		float c[7][4] = {};
		memcpy(c, Reproject, sizeof(Reproject));
		memcpy(c[4], RtConstant, sizeof(RtConstant));
		c[4][0] = Mode;
		memcpy(c[5], slots, 4 * sizeof(float));
		c[6][0] = slots[4]; c[6][1] = slots[5]; c[6][2] = DebugLamp; c[6][3] = -1.0f;	// z: DebugView 3's lamp
		if (force || !UploadedValid || memcmp(c, Uploaded, sizeof(c))) {
			TheRenderManager->device->SetPixelShaderConstantF(206, &c[0][0], 4);
			TheRenderManager->device->SetPixelShaderConstantF(215, &c[4][0], 3);
			memcpy(Uploaded, c, sizeof(c));
			UploadedValid = true;
		}
	}

	// s15 in POINT_SHADOW_RT shaders: the result, or a 1x1 "nothing traced" (depth code 0 never matches) before there is one
	inline IDirect3DBaseTexture9* Stage15Texture() {
		if (Mode > 0.0f && ResultTexture) return ResultTexture;
		static IDirect3DTexture9* neutral = nullptr;
		if (!neutral && SUCCEEDED(TheRenderManager->device->CreateTexture(1, 1, 1, 0, D3DFMT_G32R32F, D3DPOOL_MANAGED, &neutral, NULL))) {
			D3DLOCKED_RECT lock;
			if (SUCCEEDED(neutral->LockRect(0, &lock, NULL, 0))) {
				float* texel = (float*)lock.pBits;
				texel[0] = 16777215.0f; texel[1] = 4095.0f;
				neutral->UnlockRect(0);
			}
		}
		return neutral;
	}

	// --- The RT core user -----------------------------------------------------------------------------------------------------------
	inline bool Wants() {
		return CompiledIn && LampsThisFrame && LampCount > 0 && !TheShaderManager->GameState.isExterior;
	}

	inline void Setup() {
		const UINT w = TheRenderManager->width, h = TheRenderManager->height;
		imageDepth = RtCore::AddImage("NVR RT lamps depth", w, h, NVR_EXT_SHARED_R32F);
		imageNormals = RtCore::AddImage("NVR RT lamps normals", w, h, NVR_EXT_SHARED_RGBA16F);
		imageResult = RtCore::AddImage("NVR RT lamps", w, h, NVR_EXT_SHARED_RG32F);
	}

	inline void Record(IDirect3DDevice9* Device) {
		if (imageDepth < 0 || imageNormals < 0 || imageResult < 0 || shader < 0) return;
		const UINT w = TheRenderManager->width, h = TheRenderManager->height;
		if (!ResultTexture || TextureWidth != w || TextureHeight != h) {
			if (ResultSurface) { ResultSurface->Release(); ResultSurface = nullptr; }
			if (ResultTexture) { ResultTexture->Release(); ResultTexture = nullptr; }
			Traced.valid = false;
			for (int i = 0; i < MapSlots; i++) TracedMap[i] = -1;
			TheTextureManager->InitTexture("TESR_RtLampShadow", &ResultTexture, &ResultSurface, w, h, D3DFMT_G32R32F);
			TextureWidth = w;
			TextureHeight = h;
			if (!ResultTexture) return;
		}
		IDirect3DBaseTexture9* depth = Texture("TESR_DepthBuffer");
		IDirect3DBaseTexture9* normals = Texture("TESR_NormalsBuffer");
		const D3DXVECTOR4* cameraData = Constant("TESR_CameraData");
		if (!depth || !normals || !cameraData) return;

		const D3DXMATRIX& view = TheRenderManager->viewMatrix;
		const D3DXMATRIX& proj = TheRenderManager->projMatrix;
		PassConstants c = {};
		c.size[0] = (float)w; c.size[1] = (float)h; c.size[2] = 1.0f / w; c.size[3] = 1.0f / h;
		c.forward[0] = view._13; c.forward[1] = view._23; c.forward[2] = view._33; c.forward[3] = cameraData->y;
		c.right[0] = view._11 / proj._11; c.right[1] = view._21 / proj._11; c.right[2] = view._31 / proj._11;
		c.up[0] = view._12 / proj._22; c.up[1] = view._22 / proj._22; c.up[2] = view._32 / proj._22;
		const D3DXVECTOR3 eye = RtCore::Eye();	// the eye in scene space (0, 0, 0: the scene is camera-relative)
		const D3DXVECTOR4& camera = TheRenderManager->CameraPosition;
		c.origin[0] = eye.x; c.origin[1] = eye.y; c.origin[2] = eye.z;
		c.origin[3] = 4095.0f / log2f((std::max)(cameraData->y, 2.0f));
		static UInt32 frame = 0;
		frame++;
		c.params[0] = (float)(std::min)((std::max)(TheSettingManager->GetSettingI(kSection, "Rays"), 1), 8);
		c.params[1] = (std::max)(TheSettingManager->GetSettingF(kSection, "StartOffset"), 0.0f);
		c.params[2] = (std::max)(TheSettingManager->GetSettingF(kSection, "LampStop"), 0.0f);
		c.params[3] = TheSettingManager->GetSettingI(kSection, "Jitter") ? fmodf((float)frame * 0.61803399f, 1.0f) : 0.0f;
		// Lamp models skip their own light over the last LampModelDistance of each ray, as LampModelShadows false does for the cube maps
		const bool skipModels = TheSettingManager->GetSettingI(kSection, "SkipLampModels") != 0;
		c.params2[0] = skipModels ? (std::max)(TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Forward", "LampModelDistance"), 0.0f) : 0.0f;
		UINT farMask = 0xFF & ~MaskAlphaTested;	// 0xEF: non-opaque alpha-tested casters are not tested here, skipped
		if (TheSettingManager->GetSettingI(kSection, "SkipLampGlass")) farMask &= ~MaskGlass;
		c.params2[1] = (float)farMask;
		c.params2[2] = (float)(skipModels ? (farMask & ~MaskLampModel) : farMask);
		c.params2[3] = (float)TracedSlots;
		c.flags[0] = TheSettingManager->GetSettingI(kSection, "OneSided") ? 1.0f : 0.0f;
		const float radius = (std::max)(TheSettingManager->GetSettingF(kSection, "LampRadius"), 0.0f);
		const float invisible = TheSettingManager->GetSettingF(kSection, "InvisibleLightRadius");
		for (int i = 0; i < LampCount; i++) {	// traced index i (PendingMap)
			const SlotLamp& l = Lamps[i];
			float* lamp = c.lamps[i];
			lamp[0] = l.position.x - camera.x; lamp[1] = l.position.y - camera.y; lamp[2] = l.position.z - camera.z;
			lamp[3] = l.reach;
			c.radius[i / 4][i % 4] = l.fade < 0.0f && invisible >= 0.0f ? invisible : radius;	// negative fade: an invisible light
			c.stops[i / 4][i % 4] = l.stop;
		}

		if (!RtCore::CopyIn(imageDepth, depth) || !RtCore::CopyIn(imageNormals, normals)) return;
		const int images[3] = { imageDepth, imageNormals, imageResult };
		if (!RtCore::AddPass(shader, images, 3, 0x4, &c, sizeof(c), (w + 7) / 8, (h + 7) / 8)) return;
		if (!RtCore::CopyOut(imageResult, ResultSurface)) return;
		// The next frame's draws reproject into this frame
		Traced = { view, D3DXVECTOR3(camera.x + eye.x, camera.y + eye.y, camera.z + eye.z), proj._11, proj._22, cameraData->y, true };
		memcpy(TracedMap, PendingMap, sizeof(TracedMap));
		// Which lamps were traced (index -> cube slot -> where): matches the LAMPSLOT lines of LogLamps; now and then
		if ((FramesTraced % 600) == 1 && FramesTraced <= 6001) {
			char text[900]; int used = 0;
			for (int i = 0; i < LampCount && used < 800; i++)
				used += sprintf_s(text + used, sizeof(text) - used, " | %d: slot %d at %.0f %.0f %.0f reach %.0f stop %.0f", i, Lamps[i].slot,
					Lamps[i].position.x, Lamps[i].position.y, Lamps[i].position.z, Lamps[i].reach, Lamps[i].stop);
			Logger::Log("UNOFFICIAL RT lamp shadows: trace %u, %d lamps, cube redraws skipped %u since the last line (SkipTracedCubes)%s",
				FramesTraced, LampCount, SkippedCubes, LampCount ? text : "");
			SkippedCubes = 0;
		}
		if (FramesTraced++ == 0)
			Logger::Log("UNOFFICIAL NVR DXVK: RT lamp shadows traced (%ux%u, %d lamps, %d rays).", w, h, LampCount, (int)c.params[0]);
	}

	// RtCore's mask hook (the FULL mask, replacing the core's land 0x01 / objects 0x02): a lamp's glass or model gets ONLY its own bit,
	// so the lamp rays' cull masks (no 0x04, near the lamp no 0x08) skip it while every other ray (the sun's 0xFF) still hits it.
	inline UInt32 MaskCounts[4] = {};		// land, objects, glass, lamp models (logged now and then: is the hook used, does it find lamps?)
	inline UInt32 MaskHook(NiGeometry* geometry, bool land) {
		static UInt32 calls = 0;
		if ((++calls % 20000) == 0 && calls <= 200000)
			Logger::Log("UNOFFICIAL RT lamp shadows: mask hook %u calls: land %u, objects %u, lamp glass %u, lamp models %u (cell glow list %u)",
				calls, MaskCounts[0], MaskCounts[1], MaskCounts[2], MaskCounts[3], (UInt32)LampFixtures::Glows.size());
		if (land) { MaskCounts[0]++; return 0x01; }
		const UInt32 own = InstanceMask(geometry, false);
		MaskCounts[own == MaskGlass ? 2 : own == MaskLampModel ? 3 : 1]++;
		return own ? own : 0x02;
	}

	// ShaderManager, once before the first RtCore::Frame (next to RtSunShadows::Register)
	inline void Register() {
		if (!CompiledIn) return;
		RtCore::SetMaskHook(MaskHook);
		shader = RtCore::AddShader("RtLampTrace");
		RtCore::AddUser({ "RT lamp shadows", Wants, Setup, Record });
	}

}
