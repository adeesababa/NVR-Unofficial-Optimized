#pragma once

#include "PointShadowSchedule.h"

namespace PointShadowForward {

	struct RenderPassView {
		NiGeometry*			geometry;
		UInt16				passEnum;
		UInt8				unk06;
		UInt8				unk07;
		UInt8				unk08;
		UInt8				numLights;
		UInt8				maxLights;
		UInt8				unk0B;
		ShadowSceneLight**	lights;
	};
	static_assert(offsetof(RenderPassView, numLights) == 0x09 && offsetof(RenderPassView, lights) == 0x0C, "RenderPass layout");

	struct Slot { const ShadowSceneLight* light; D3DXVECTOR3 position; float radius; IDirect3DCubeTexture9* cube; int slot; float fade; };

	inline bool CompiledIn = false;
	inline int CompiledCount = 0;
	inline bool Active = false;
	inline bool FirstPerson = true;
	inline Slot Slots[ShadowCubeMapsMax] = {};
	inline int SlotCount = 0;
	inline float Params[4] = {};
	inline float DebugView = 0.0f;
	inline IDirect3DCubeTexture9* Neutral = nullptr;
	inline const DWORD Stage[6] = { 8, 11, 12, 13, 14, 15 };
	inline unsigned StatFrames = 0, StatDraws = 0, StatMatchedDraws = 0, StatMatchedLights = 0;

	inline bool ScopeFix = true;
	inline bool ScopeInView = false, ScopeWasInView = false;
	inline unsigned OtherCameraLogs = 0, ScopeLogs = 0;

	inline bool IsLens(const RenderPassView* pass) {
		if (!pass || !pass->geometry) return false;
		const NiAVObject* node = pass->geometry;
		for (int depth = 0; node && depth < 8; depth++, node = node->m_parent)
			if (node->m_pcName && !strncmp(node->m_pcName, "B42Lens", 7)) return true;
		return false;
	}

	inline bool OtherCamera(const RenderPassView* pass, bool firstPerson) {
		if (!ScopeFix) return false;
		const D3DVIEWPORT9& port = TheRenderManager->m_kD3DPort;
		if (port.Width != TheRenderManager->width || port.Height != TheRenderManager->height) {
			if (OtherCameraLogs < 3) {
				OtherCameraLogs++;
				Logger::Log("UNOFFICIAL forward shadows: a %ux%u render from another camera (a B42 Optics scope picture?) gets no lamp shadows.",
					(unsigned)port.Width, (unsigned)port.Height);
			}
			return true;
		}
		if (firstPerson && pass && pass->geometry) {
			const NiAVObject* node = pass->geometry;
			for (int depth = 0; node && depth < 8; depth++, node = node->m_parent)
				if (node->m_pcName && !strncmp(node->m_pcName, "B42Lens", 7)) { ScopeInView = true; return true; }
		}
		return false;
	}

	inline bool Eligible(const char* name, const ShaderTemplate& t) {
		if (!name || !strstr(name, ".pso")) return false;
		if (t.Name) return !strcmp(t.Name, "ObjectTemplate") || !strcmp(t.Name, "SkinVPSTemplate");
		return !strcmp(name, "SKIN2002.pso") || !strcmp(name, "SKIN2006.pso") || !strcmp(name, "SM3002.pso") ||
			!strcmp(name, "SM3003.pso") || !strcmp(name, "SM3005.pso") || !strcmp(name, "SM3007.pso");
	}

	inline bool BoundThisFrame = false;
	inline unsigned Frame = 0;
	inline void Begin() {
		Frame++;
		Active = false; SlotCount = 0; BoundThisFrame = false;
		ScopeFix = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Forward", "ScopeFix") != 0;
	}

	inline void Publish(const PointShadowSlotState* slots, ShadowSceneLight* const* lights, int count, IDirect3DCubeTexture9* const* cubes) {
		static const ShadowSceneLight* lastLights[ShadowCubeMapsMax] = {};
		static double lastStart[ShadowCubeMapsMax] = {};
		static int lastCount = 0;
		Active = false;
		SlotCount = 0;
		if (!CompiledIn) { lastCount = 0; return; }
		const float strength = TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Forward", "Strength");
		if (!(strength > 0.0f)) { lastCount = 0; return; }
		LARGE_INTEGER frequency, counter;
		QueryPerformanceFrequency(&frequency);
		QueryPerformanceCounter(&counter);
		const double now = (double)counter.QuadPart / (double)frequency.QuadPart;
		const float fadeIn = TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Forward", "FadeIn");
		double start[ShadowCubeMapsMax] = {};
		const float face = (float)(std::max)(TheShaderManager->Effects.ShadowsExteriors->Settings.Interiors.ShadowCubeMapSize, 1);
		Params[0] = (std::min)(strength, 1.0f);
		Params[1] = TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Forward", "Bias");
		Params[2] = TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Forward", "NormalOffset") * 2.0f / face;
		Params[3] = TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Forward", "Softness") * 2.0f / face;
		DebugView = (float)TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Forward", "DebugView");
		ScopeFix = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Forward", "ScopeFix") != 0;
		if (ScopeInView != ScopeWasInView && ScopeLogs < 20) {
			ScopeLogs++;
			Logger::Log("UNOFFICIAL forward shadows: B42 Optics lens %s (no lamp shadows on it).", ScopeInView ? "in view" : "gone");
		}
		ScopeWasInView = ScopeInView;
		ScopeInView = false;
		FirstPerson = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Forward", "FirstPerson") != 0;
		static unsigned rejectedLast = 0, rejectLogs = 0;
		unsigned rejected = 0;
		for (int i = 0; i < count && i < ShadowCubeMapsMax; i++) {
			if (!lights[i] || !cubes[i] || !slots[i].valid || slots[i].light != lights[i] || slots[i].texture != cubes[i] ||
				!(slots[i].radius > 0.0f)) {
				if (lights[i]) {
					rejected |= 1u << i;
					if (!(rejectedLast & (1u << i)) && rejectLogs < 200 &&
						TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Forward", "LogLamps")) {
						rejectLogs++;
						Logger::Log("LAMP %p in slot %d gets no shadow: its cube map is not drawn for it (drawn %d, same lamp %d, same texture %d, radius %.0f)",
							lights[i], i, (int)slots[i].valid, (int)(slots[i].light == lights[i]), (int)(slots[i].texture == cubes[i]), slots[i].radius);
					}
				}
				continue;
			}
			double since = now;
			for (int j = 0; j < lastCount; j++) if (lastLights[j] == lights[i]) { since = lastStart[j]; break; }
			const float fade = fadeIn > 0.0f ? (float)(std::min)(1.0, (now - since) / fadeIn) : 1.0f;
			start[SlotCount] = since;
			Slots[SlotCount++] = { lights[i], D3DXVECTOR3(slots[i].x, slots[i].y, slots[i].z), slots[i].radius, cubes[i], i, fade };
		}
		rejectedLast = rejected;
		for (int s = 0; s < SlotCount; s++) { lastLights[s] = Slots[s].light; lastStart[s] = start[s]; }
		lastCount = SlotCount;
		Active = SlotCount > 0;
	}

	inline IDirect3DCubeTexture9* NeutralCube() {
		if (!Neutral && SUCCEEDED(TheRenderManager->device->CreateCubeTexture(1, 1, 0, D3DFMT_R32F, D3DPOOL_MANAGED, &Neutral, NULL))) {
			for (int face = 0; face < 6; face++) {
				D3DLOCKED_RECT lock;
				if (SUCCEEDED(Neutral->LockRect((D3DCUBEMAP_FACES)face, 0, &lock, NULL, 0))) {
					*(float*)lock.pBits = 1.0f;
					Neutral->UnlockRect((D3DCUBEMAP_FACES)face, 0);
				}
			}
		}
		return Neutral;
	}

	inline void Bind(DWORD stage, IDirect3DBaseTexture9* texture) {
		TheRenderManager->renderState->SetTexture(stage, texture);
		static const DWORD states[][2] = { { D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP }, { D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP },
			{ D3DSAMP_ADDRESSW, D3DTADDRESS_CLAMP }, { D3DSAMP_MAGFILTER, D3DTEXF_LINEAR }, { D3DSAMP_MINFILTER, D3DTEXF_LINEAR },
			{ D3DSAMP_MIPFILTER, D3DTEXF_NONE }, { D3DSAMP_SRGBTEXTURE, FALSE } };
		for (const auto& s : states)
			if (TheRenderManager->renderState->GetSamplerState(stage, (D3DSAMPLERSTATETYPE)s[0]) != s[1])
				TheRenderManager->SetSamplerState(stage, (D3DSAMPLERSTATETYPE)s[0], s[1]);
	}

	inline bool Bound = false;
	inline float Uploaded[14][4] = {};

	inline void SetForPass(const RenderPassView* pass, bool world, bool firstPerson, bool force) {
		float constants[14][4] = {};
		for (int k = 0; k < 6; k++) constants[8 + k][0] = constants[8 + k][1] = constants[8 + k][2] = 1.0f;
		IDirect3DBaseTexture9* bound[6];
		IDirect3DCubeTexture9* neutral = NeutralCube();
		for (int k = 0; k < 6; k++) bound[k] = neutral;
		int matched = 0;
		if (Active && (world || (firstPerson && FirstPerson)) && pass && pass->lights && !OtherCamera(pass, firstPerson)) {
			const D3DXVECTOR4& camera = TheRenderManager->CameraPosition;
			const int count = (std::min)((int)pass->numLights, 6);
			for (int k = 0; k < count; k++) {
				const ShadowSceneLight* light = pass->lights[k];
				if (!light || !light->bPointLight) continue;
				for (int s = 0; s < SlotCount; s++) {
					if (Slots[s].light != light) continue;
					constants[k][0] = Slots[s].position.x - camera.x;
					constants[k][1] = Slots[s].position.y - camera.y;
					constants[k][2] = Slots[s].position.z - camera.z;
					constants[k][3] = 1.0f / Slots[s].radius;
					bound[k] = Slots[s].cube;
					static const float palette[11][3] = { { 1, 0.15f, 0.1f }, { 0.1f, 1, 0.15f }, { 0.15f, 0.35f, 1 },
						{ 1, 0.9f, 0.1f }, { 0.1f, 1, 1 }, { 1, 0.2f, 1 }, { 1, 0.55f, 0.1f }, { 0.55f, 0.25f, 1 },
						{ 0.6f, 1, 0.3f }, { 1, 0.45f, 0.6f }, { 0.4f, 0.8f, 1 } };
					memcpy(constants[8 + k], palette[Slots[s].slot % 11], 3 * sizeof(float));
					constants[8 + k][3] = Slots[s].fade;
					matched++;
					break;
				}
			}
		}
		if (matched) memcpy(constants[6], Params, sizeof(Params));
		constants[7][0] = matched ? DebugView : 0.0f;
		for (int k = 0; k < 6; k++)
			if (force || TheRenderManager->renderState->GetTexture(Stage[k]) != bound[k]) Bind(Stage[k], bound[k]);
		if (force || memcmp(constants, Uploaded, sizeof(constants))) {
			TheRenderManager->device->SetPixelShaderConstantF(174, &constants[0][0], 14);
			memcpy(Uploaded, constants, sizeof(constants));
		}
		if (GpuTimer::Enabled && !force) { StatDraws++; if (matched) { StatMatchedDraws++; StatMatchedLights += matched; } }
	}

	inline void SetForDraw(NiD3DPixelShaderEx* pixelShader, bool world, bool firstPerson) {
		Bound = false;
		if (!CompiledIn || !pixelShader) return;
		ShaderRecordPixel* interiorRecord = pixelShader->GetShaderRecord(ShaderRecordType::Interior);
		if (!interiorRecord || !interiorRecord->PointShadowForward || pixelShader->ShaderHandle != interiorRecord->ShaderHandle)
			return;
		Bound = true;
		BoundThisFrame = true;
		SetForPass(*(RenderPassView**)0x011F91E0, world, firstPerson, true);
	}

	inline void OnRenderPass(const RenderPassView* pass, bool world, bool firstPerson) {
		if (Bound) SetForPass(pass, world, firstPerson, false);
	}

	inline void FrameStats() {
		if (!GpuTimer::Enabled || !CompiledIn) return;
		if (++StatFrames >= 240) {
			const float perFrame = 1.0f / StatFrames;
			Logger::Log("POINT SHADOWS FORWARD %d shaders compiled in: %.1f cube maps in use, %.0f objects, %.0f with a shadowed lamp, %.1f lamps matched per frame",
				CompiledCount, (float)SlotCount, StatDraws * perFrame, StatMatchedDraws * perFrame, StatMatchedLights * perFrame);
			StatFrames = StatDraws = StatMatchedDraws = StatMatchedLights = 0;
		}
	}
}
