#pragma once


#include <algorithm>
#include "LampFixtures.h"
#include "DxvkCompute.h"
#include "RtCore.h"

namespace RtLampShadows {

	inline constexpr int TracedSlots = 9;
	inline constexpr const char* kSection = "Shaders.ShadowsInteriors.RT";

	inline constexpr UINT MaskGlass = 0x04, MaskLampModel = 0x08;

	inline bool CompiledIn = false;
	inline char Status[200] = "not checked yet";

	struct PassConstants {
		float size[4], forward[4], right[4], up[4], origin[4], params[4], params2[4];
		float lamps[TracedSlots][4];
		float radius[3][4];
		float stops[3][4];
	};
	static_assert(sizeof(PassConstants) <= 512, "RtCore: 512 bytes of constants a pass");

	struct SlotLamp { int slot; D3DXVECTOR3 position; float reach; float fade; float stop; };
	inline SlotLamp Lamps[TracedSlots] = {};
	inline int LampCount = 0;
	inline bool LampsThisFrame = false;

	inline constexpr int MapSlots = 64;
	inline UInt32 DrawUse[MapSlots] = {};
	inline int PendingMap[MapSlots];
	inline int TracedMap[MapSlots];
	inline void NoteUse(int cubeSlot) { if (cubeSlot >= 0 && cubeSlot < MapSlots) DrawUse[cubeSlot]++; }

	struct TracedCamera { D3DXMATRIX view; D3DXVECTOR3 location;
		 float p11, p22, farZ; bool valid; };
	inline TracedCamera Traced = {};
	inline TracedCamera Pending = {};

	inline float Mode = 0.0f;
	inline int MaxLamps = 8;
	inline float Reproject[4][4] = {};
	inline float RtConstant[4] = {};
	inline float Uploaded[7][4] = {};
	inline bool UploadedValid = false;

	inline int imageDepth = -1, imageNormals = -1, imageResult = -1, shader = -1;
	inline IDirect3DTexture9* ResultTexture = nullptr;
	inline IDirect3DSurface9* ResultSurface = nullptr;
	inline UINT TextureWidth = 0, TextureHeight = 0;
	inline UInt32 FramesTraced = 0;

	inline UInt32 InstanceMask(NiGeometry* geometry, bool actor) {
		if (actor || !geometry || TheShaderManager->GameState.isExterior) return 0;
		if (LampFixtures::LampPart(geometry)) return MaskGlass;
		if (LampFixtures::ModelIndexOf(geometry) >= 0) return MaskLampModel;
		return 0;
	}

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

	inline void Submit(const SlotLamp* lamps, int count) {
		Mode = 0.0f;
		LampCount = 0;
		LampsThisFrame = false;
		if (!CompiledIn) return;
		MaxLamps = (std::min)((std::max)(TheSettingManager->GetSettingI(kSection, "MaxLamps"), 0), TracedSlots);
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

		if (!Traced.valid || !ResultTexture || !LampsThisFrame || !RtCore::Running()) return;
		const D3DXMATRIX& v = Traced.view;
		const D3DXVECTOR3 right(v._11, v._21, v._31), up(v._12, v._22, v._32), forward(v._13, v._23, v._33);
		const D3DXVECTOR4& camera = TheRenderManager->CameraPosition;
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
		RtConstant[1] = (std::max)(TheSettingManager->GetSettingF(kSection, "DepthTolerance"), 0.004f);
		RtConstant[2] = log2f((std::max)(Traced.farZ, 2.0f)) / 4095.0f;
		RtConstant[3] = TheSettingManager->GetSettingI(kSection, "ActorsFromCubeMaps") ? 1.0f : 0.0f;
		const int debugView = TheSettingManager->GetSettingI(kSection, "DebugView");
		Mode = debugView == 1 ? 2.0f : debugView == 2 ? 3.0f : 1.0f;
		RtConstant[0] = Mode;
	}

	inline float TracedSlot(int cubeSlot) {
		return Mode > 0.0f && cubeSlot >= 0 && cubeSlot < MapSlots && TracedMap[cubeSlot] >= 0 ? (float)TracedMap[cubeSlot] : -1.0f;
	}

	inline void SetForPass(const float slots[6], bool force) {
		if (!CompiledIn) return;
		float c[7][4] = {};
		memcpy(c, Reproject, sizeof(Reproject));
		memcpy(c[4], RtConstant, sizeof(RtConstant));
		c[4][0] = Mode;
		memcpy(c[5], slots, 4 * sizeof(float));
		c[6][0] = slots[4]; c[6][1] = slots[5]; c[6][2] = c[6][3] = -1.0f;
		if (force || !UploadedValid || memcmp(c, Uploaded, sizeof(c))) {
			TheRenderManager->device->SetPixelShaderConstantF(206, &c[0][0], 4);
			TheRenderManager->device->SetPixelShaderConstantF(215, &c[4][0], 3);
			memcpy(Uploaded, c, sizeof(c));
			UploadedValid = true;
		}
	}

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
		const D3DXVECTOR3 eye = RtCore::Eye();
		const D3DXVECTOR4& camera = TheRenderManager->CameraPosition;
		c.origin[0] = eye.x; c.origin[1] = eye.y; c.origin[2] = eye.z;
		c.origin[3] = 4095.0f / log2f((std::max)(cameraData->y, 2.0f));
		static UInt32 frame = 0;
		frame++;
		c.params[0] = (float)(std::min)((std::max)(TheSettingManager->GetSettingI(kSection, "Rays"), 1), 8);
		c.params[1] = (std::max)(TheSettingManager->GetSettingF(kSection, "StartOffset"), 0.0f);
		c.params[2] = (std::max)(TheSettingManager->GetSettingF(kSection, "LampStop"), 0.0f);
		c.params[3] = TheSettingManager->GetSettingI(kSection, "Jitter") ? fmodf((float)frame * 0.61803399f, 1.0f) : 0.0f;
		const bool skipModels = TheSettingManager->GetSettingI(kSection, "SkipLampModels") != 0;
		c.params2[0] = skipModels ? (std::max)(TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Forward", "LampModelDistance"), 0.0f) : 0.0f;
		UINT farMask = 0xFF;
		if (TheSettingManager->GetSettingI(kSection, "SkipLampGlass")) farMask &= ~MaskGlass;
		c.params2[1] = (float)farMask;
		c.params2[2] = (float)(skipModels ? (farMask & ~MaskLampModel) : farMask);
		c.params2[3] = (float)TracedSlots;
		const float radius = (std::max)(TheSettingManager->GetSettingF(kSection, "LampRadius"), 0.0f);
		const float invisible = TheSettingManager->GetSettingF(kSection, "InvisibleLightRadius");
		for (int i = 0; i < LampCount; i++) {
			const SlotLamp& l = Lamps[i];
			float* lamp = c.lamps[i];
			lamp[0] = l.position.x - camera.x; lamp[1] = l.position.y - camera.y; lamp[2] = l.position.z - camera.z;
			lamp[3] = l.reach;
			c.radius[i / 4][i % 4] = l.fade < 0.0f && invisible >= 0.0f ? invisible : radius;
			c.stops[i / 4][i % 4] = l.stop;
		}

		if (!RtCore::CopyIn(imageDepth, depth) || !RtCore::CopyIn(imageNormals, normals)) return;
		const int images[3] = { imageDepth, imageNormals, imageResult };
		if (!RtCore::AddPass(shader, images, 3, 0x4, &c, sizeof(c), (w + 7) / 8, (h + 7) / 8)) return;
		if (!RtCore::CopyOut(imageResult, ResultSurface)) return;
		Traced = { view, D3DXVECTOR3(camera.x + eye.x, camera.y + eye.y, camera.z + eye.z), proj._11, proj._22, cameraData->y, true };
		memcpy(TracedMap, PendingMap, sizeof(TracedMap));
		if (FramesTraced++ == 0)
			Logger::Log("UNOFFICIAL NVR DXVK: RT lamp shadows traced (%ux%u, %d lamps, %d rays).", w, h, LampCount, (int)c.params[0]);
	}

	inline UInt32 MaskCounts[4] = {};
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

	inline void Register() {
		if (!CompiledIn) return;
		RtCore::SetMaskHook(MaskHook);
		shader = RtCore::AddShader("RtLampTrace");
		RtCore::AddUser({ "RT lamp shadows", Wants, Setup, Record });
	}

}
