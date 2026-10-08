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

	inline int FirstShaderLight(const RenderPassView* pass) { return pass->passEnum == 0xCA || pass->passEnum == 0xD1 ? 1 : 0; }

	struct Slot { const ShadowSceneLight* light; D3DXVECTOR3 position; float radius; IDirect3DCubeTexture9* cube; int slot; float fade; };

	inline bool CompiledIn = false;
	inline int CompiledCount = 0;
	inline bool Active = false;
	inline bool FirstPerson = true;
	inline Slot Slots[ShadowCubeMapsMax] = {};
	inline int SlotCount = 0;
	inline int Focus = -1;
	inline unsigned TraceDraws = 0, TraceDrawsWithLamp = 0;
	inline IDirect3DSurface9* TraceReadback = nullptr;
	inline UINT TraceReadbackSize = 0;

	inline void TraceCube(const char* what, IDirect3DCubeTexture9* cube) {
		IDirect3DDevice9* device = TheRenderManager->device;
		D3DSURFACE_DESC desc;
		if (!cube || FAILED(cube->GetLevelDesc(0, &desc))) { Logger::Log("TRACE   %s: no cube map", what); return; }
		if (desc.Format != D3DFMT_R32F) { Logger::Log("TRACE   %s: format %d, not read", what, (int)desc.Format); return; }
		if (!TraceReadback || TraceReadbackSize != desc.Width) {
			if (TraceReadback) TraceReadback->Release();
			TraceReadback = nullptr;
			if (FAILED(device->CreateOffscreenPlainSurface(desc.Width, desc.Height, D3DFMT_R32F, D3DPOOL_SYSTEMMEM, &TraceReadback, NULL))) {
				TraceReadback = nullptr;
				Logger::Log("TRACE   %s: no readback surface", what);
				return;
			}
			TraceReadbackSize = desc.Width;
		}
		static const char* faceName[6] = { "+X", "-X", "+Y", "-Y", "down", "up" };
		char text[900] = "";
		size_t used = 0;
		for (int face = 0; face < 6; face++) {
			IDirect3DSurface9* surface = nullptr;
			D3DLOCKED_RECT lock;
			if (FAILED(cube->GetCubeMapSurface((D3DCUBEMAP_FACES)face, 0, &surface)) || !surface) continue;
			const HRESULT copied = device->GetRenderTargetData(surface, TraceReadback);
			surface->Release();
			if (FAILED(copied) || FAILED(TraceReadback->LockRect(&lock, NULL, D3DLOCK_READONLY))) {
				used += sprintf_s(text + used, sizeof(text) - used, " | %s: not read", faceName[face]);
				continue;
			}
			unsigned empty = 0, zero = 0, drawn = 0;
			double sum = 0.0;
			float nearest = 1.0f;
			for (UINT y = 0; y < desc.Height; y++) {
				const float* row = (const float*)((const char*)lock.pBits + y * lock.Pitch);
				for (UINT x = 0; x < desc.Width; x++) {
					const float v = row[x];
					if (!(v < 0.999f)) empty++;
					else if (!(v > 0.0f)) zero++;
					else { drawn++; sum += v; if (v < nearest) nearest = v; }
				}
			}
			const float centre = ((const float*)((const char*)lock.pBits + (desc.Height / 2) * lock.Pitch))[desc.Width / 2];
			TraceReadback->UnlockRect();
			const float all = (float)(desc.Width * desc.Height) * 0.01f;
			used += sprintf_s(text + used, sizeof(text) - used, " | %s: %.0f%% empty, %.0f%% zero, nearest %.3f, mean %.3f, centre %.3f",
				faceName[face], empty / all, zero / all, nearest, drawn ? sum / drawn : 0.0, centre);
		}
		Logger::Log("TRACE   %s %p (%ux%u)%s", what, (const void*)cube, desc.Width, desc.Height, text);
	}
	inline float Params[4] = {};
	inline float DebugView = 0.0f;
	inline float NearFade = 0.0f;
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

	inline bool UsesAllSlots(bool exterior) {
		return CompiledIn && !exterior && TheShaderManager->Effects.ShadowsExteriors->Settings.Interiors.Forward.Enabled;
	}
	inline int ShadowedSlots(bool exterior) {
		const int points = TheShaderManager->Effects.ShadowsExteriors->Settings.Interiors.LightPoints;
		return UsesAllSlots(exterior) ? (std::min)(points, (int)ShadowCubeMapsMax) : (std::min)(points, (int)ShadowCubeMapsSampled);
	}
	inline unsigned Frame = 0;

	struct ObjectLampState {
		UInt32 frame = 0, lastFrame = 0;
		UInt8 count = 0, lastCount = 0;
		const ShadowSceneLight* lamps[12] = {}; UInt8 how[12] = {}; const char* shader[12] = {};
		const ShadowSceneLight* last[12] = {}; UInt8 lastHow[12] = {}; const char* lastShader[12] = {};
	};
	inline std::unordered_map<const void*, ObjectLampState> ObjectLamps;
	inline bool LogObjectLamps = false;
	inline const char* LastPixelShaderName = nullptr;
	inline bool LastPixelShaderShadowed = false;
	inline const char* LastVertexShaderName = nullptr;
	inline bool LastVertexShaderNvr = false, LastPixelShaderNvr = false;

	struct AimPass { const char* vs; const char* ps; bool vsNvr, psNvr, shadowed; UInt16 passEnum; UInt8 numLights; bool strength;
		UInt8 count; UInt8 index[6]; bool point[6]; bool uploaded[6]; const ShadowSceneLight* lamps[6]; };
	struct AimHit { const void* geometry; float along, off, radius; UInt8 passes; AimPass pass[4]; };
	inline AimHit AimHits[10];
	inline int AimHitCount = 0;
	inline UInt32 AimFrame = 0, AimLastLogFrame = 0;
	inline const void* AimLogged[10] = {};
	inline int AimLoggedCount = 0;
	inline unsigned AimLines = 0;
	struct CameraState { UInt32 frame; D3DXVECTOR3 forward, position, player; };
	inline CameraState Cameras[8] = {};
	inline unsigned ObjectLampLines = 0;
	inline unsigned StatLampAppeared = 0, StatLampGone = 0, StatLampLostShadow = 0, StatLampGotShadow = 0, StatLampStill = 0;

	inline bool KeepLampsWanted = false;
	inline bool KeepLampsNow = false;
	inline std::unordered_map<const ShadowSceneLight*, UInt16> GameCullMask;
	inline unsigned StatCullCalls = 0, StatCullOut = 0, StatCullKept = 0;
	inline const UInt32 kTestFrustumCallSite = 0x00B5E915, kTestFrustumCull = 0x00B9E970;
	typedef void(__thiscall* TestFrustumCullFn)(ShadowSceneLight* light, void* cullingProcess);

	inline bool LampReachesView(const ShadowSceneLight* light) {
		const NiPointLight* source = light ? light->sourceLight : nullptr;
		if (!source) return false;
		const D3DXMATRIX& camera = TheRenderManager->InvViewMatrix;
		const D3DXVECTOR3 right(camera._11, camera._12, camera._13), up(camera._21, camera._22, camera._23), forward(camera._31, camera._32, camera._33);
		const float tanX = fabsf(TheRenderManager->InvProjMatrix._11), tanY = fabsf(TheRenderManager->InvProjMatrix._22);
		const float normX = 1.0f / sqrtf(1.0f + tanX * tanX), normY = 1.0f / sqrtf(1.0f + tanY * tanY);
		const D3DXVECTOR3 v(source->m_worldTransform.pos.x - TheRenderManager->CameraPosition.x, source->m_worldTransform.pos.y - TheRenderManager->CameraPosition.y,
			source->m_worldTransform.pos.z - TheRenderManager->CameraPosition.z);
		const float reach = source->Spec.r * 1.05f + 32.0f + 0.1f * D3DXVec3Length(&v);
		const float x = D3DXVec3Dot(&v, &right), y = D3DXVec3Dot(&v, &up), z = D3DXVec3Dot(&v, &forward);
		if (z < -reach) return false;
		if ((tanX * z - x) * normX < -reach || (tanX * z + x) * normX < -reach) return false;
		if ((tanY * z - y) * normY < -reach || (tanY * z + y) * normY < -reach) return false;
		return true;
	}

	inline void __fastcall TestFrustumCullHook(ShadowSceneLight* light, void*, void* cullingProcess) {
		auto previous = GameCullMask.find(light);
		if (previous != GameCullMask.end()) light->bIsEnabled = previous->second;
		((TestFrustumCullFn)kTestFrustumCull)(light, cullingProcess);
		GameCullMask[light] = light->bIsEnabled;
		StatCullCalls++;
		if (light->bIsEnabled != 0xFF) return;
		StatCullOut++;
		if (KeepLampsNow && light->bPointLight && LampReachesView(light)) {
			light->bIsEnabled = 0;
			StatCullKept++;
		}
	}

	inline void Begin() {
		Frame++;
		Cameras[Frame & 7] = { Frame, D3DXVECTOR3(TheRenderManager->CameraForward.x, TheRenderManager->CameraForward.y, TheRenderManager->CameraForward.z),
			D3DXVECTOR3(TheRenderManager->CameraPosition.x, TheRenderManager->CameraPosition.y, TheRenderManager->CameraPosition.z),
			Player ? D3DXVECTOR3(Player->pos.x, Player->pos.y, Player->pos.z) : D3DXVECTOR3(0.0f, 0.0f, 0.0f) };
		if ((Frame & 1023) == 0)
			for (auto it = ObjectLamps.begin(); it != ObjectLamps.end();) it = Frame - it->second.frame > 1024 ? ObjectLamps.erase(it) : std::next(it);
		if ((Frame & 4095) == 0) GameCullMask.clear();
		KeepLampsNow = KeepLampsWanted && !TheShaderManager->GameState.isExterior;
		Active = false; SlotCount = 0; BoundThisFrame = false;
		ScopeFix = TheShaderManager->Effects.ShadowsExteriors->Settings.Interiors.Forward.ScopeFix;
	}

	inline void Publish(const PointShadowSlotState* slots, ShadowSceneLight* const* lights, int count, IDirect3DCubeTexture9* const* cubes) {
		static const ShadowSceneLight* lastLights[ShadowCubeMapsMax] = {};
		static double lastStart[ShadowCubeMapsMax] = {};
		static int lastCount = 0;
		Active = false;
		SlotCount = 0;
		Focus = -1;
		if (!CompiledIn) { lastCount = 0; return; }
		const ShadowsExteriorEffect::InteriorsStruct::ForwardStruct& forward = TheShaderManager->Effects.ShadowsExteriors->Settings.Interiors.Forward;
		LogObjectLamps = forward.LogLamps;
		KeepLampsWanted = forward.Enabled && forward.KeepLampsInView;
		const float strength = forward.Strength;
		if (!forward.Enabled || !(strength > 0.0f)) { lastCount = 0; return; }
		LARGE_INTEGER frequency, counter;
		QueryPerformanceFrequency(&frequency);
		QueryPerformanceCounter(&counter);
		const double now = (double)counter.QuadPart / (double)frequency.QuadPart;
		const float fadeIn = forward.FadeIn;
		double start[ShadowCubeMapsMax] = {};
		const float face = (float)(std::max)(TheShaderManager->Effects.ShadowsExteriors->Settings.Interiors.ShadowCubeMapSize, 1);
		Params[0] = (std::min)(strength, 1.0f);
		Params[1] = forward.Bias;
		Params[2] = forward.NormalOffset * 2.0f / face;
		Params[3] = forward.Softness * 2.0f / face;
		DebugView = (float)forward.DebugView;
		NearFade = TheShaderManager->Effects.ShadowsExteriors->Constants.PointShadowNear.x;
		ScopeFix = TheShaderManager->Effects.ShadowsExteriors->Settings.Interiors.Forward.ScopeFix;
		if (ScopeInView != ScopeWasInView && ScopeLogs < 20) {
			ScopeLogs++;
			Logger::Log("UNOFFICIAL forward shadows: B42 Optics lens %s (no lamp shadows on it).", ScopeInView ? "in view" : "gone");
		}
		ScopeWasInView = ScopeInView;
		ScopeInView = false;
		FirstPerson = forward.FirstPerson;
		static UInt64 rejectedLast = 0; static unsigned rejectLogs = 0;
		UInt64 rejected = 0;
		for (int i = 0; i < count && i < ShadowCubeMapsMax; i++) {
			if (!lights[i] || !cubes[i] || !slots[i].valid || slots[i].light != lights[i] || slots[i].texture != cubes[i] ||
				!(slots[i].radius > 0.0f)) {
				if (lights[i]) {
					rejected |= 1ull << i;
					if (!(rejectedLast & (1ull << i)) && rejectLogs < 200 && forward.LogLamps) {
						rejectLogs++;
						Logger::Log("LAMP %p in slot %d gets no shadow: its cube map is not drawn for it (drawn %d, same lamp %d, same texture %d, radius %.0f)",
							lights[i], i, (int)slots[i].valid, (int)(slots[i].light == lights[i]), (int)(slots[i].texture == cubes[i]), slots[i].radius);
					}
				}
				continue;
			}
			double since = now;
			for (int j = 0; j < lastCount; j++) if (lastLights[j] == lights[i]) { since = lastStart[j]; break; }
			float fade = fadeIn > 0.0f ? (float)(std::min)(1.0, (now - since) / fadeIn) : 1.0f;
			const NiPointLight* source = lights[i]->sourceLight;
			if (forward.FillLightRadius > 0.0f && source && source->Spec.r > forward.FillLightRadius) fade *= forward.FillLightShadowStrength;
			start[SlotCount] = since;
			Slots[SlotCount++] = { lights[i], D3DXVECTOR3(slots[i].x, slots[i].y, slots[i].z), slots[i].radius, cubes[i], i, fade };
		}
		rejectedLast = rejected;
		for (int s = 0; s < SlotCount; s++) { lastLights[s] = Slots[s].light; lastStart[s] = start[s]; }
		lastCount = SlotCount;
		Active = SlotCount > 0;
		for (int s = 0; s < SlotCount; s++) if (Slots[s].slot == 0) { Focus = s; break; }
		static double lastTrace = 0.0;
		if (forward.DebugView >= 3 && now - lastTrace >= 2.0) {
			lastTrace = now;
			if (Focus < 0) Logger::Log("TRACE no lamp in cube slot 0 this frame (%d lamps with shadows)", SlotCount);
			else {
				const Slot& f = Slots[Focus];
				const NiPointLight* source = f.light->sourceLight;
				Logger::Log("TRACE focus lamp %p (cube slot 0) at %.0f %.0f %.0f, cube radius %.0f, reach %.0f, fade %.2f, camera %.0f %.0f %.0f; last period %u draws got it, %u of them lit by it",
					(const void*)f.light, f.position.x, f.position.y, f.position.z, f.radius, source ? source->Spec.r : -1.0f, f.fade,
					TheRenderManager->CameraPosition.x, TheRenderManager->CameraPosition.y, TheRenderManager->CameraPosition.z,
					TraceDraws, TraceDrawsWithLamp);
				TraceCube("live cube", f.cube);
				TraceCube("static cube", TheShaderManager->Effects.ShadowsExteriors->Textures.ShadowCubeMapStaticTexture[f.slot]);
			}
			TraceDraws = TraceDrawsWithLamp = 0;
		}
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

	inline void Bind(DWORD stage, IDirect3DBaseTexture9* texture, bool device = false) {
		TheRenderManager->renderState->SetTexture(stage, texture);
		if (device) TheRenderManager->device->SetTexture(stage, texture);
		static const DWORD states[][2] = { { D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP }, { D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP },
			{ D3DSAMP_ADDRESSW, D3DTADDRESS_CLAMP }, { D3DSAMP_MAGFILTER, D3DTEXF_LINEAR }, { D3DSAMP_MINFILTER, D3DTEXF_LINEAR },
			{ D3DSAMP_MIPFILTER, D3DTEXF_NONE }, { D3DSAMP_SRGBTEXTURE, FALSE } };
		for (const auto& s : states)
			if (TheRenderManager->renderState->GetSamplerState(stage, (D3DSAMPLERSTATETYPE)s[0]) != s[1])
				TheRenderManager->SetSamplerState(stage, (D3DSAMPLERSTATETYPE)s[0], s[1]);
	}

	inline bool Bound = false;
	inline float Uploaded[15][4] = {};

	inline void SetForPass(const RenderPassView* pass, bool world, bool firstPerson, bool force) {
		float constants[15][4] = {};
		for (int k = 0; k < 6; k++) constants[8 + k][0] = constants[8 + k][1] = constants[8 + k][2] = 1.0f;
		IDirect3DBaseTexture9* bound[6];
		IDirect3DCubeTexture9* neutral = NeutralCube();
		for (int k = 0; k < 6; k++) bound[k] = neutral;
		int matched = 0;
		const bool trace = DebugView > 2.5f;
		int focusIndex = -1;
		bool wanted = false;
		if (Active && (world || (firstPerson && FirstPerson)) && pass && pass->lights && !OtherCamera(pass, firstPerson)) {
			wanted = true;
			const D3DXVECTOR4& camera = TheRenderManager->CameraPosition;
			const int first = FirstShaderLight(pass);
			const int count = (std::min)((int)pass->numLights - first, 6);
			for (int k = 0; k < count; k++) {
				const ShadowSceneLight* light = pass->lights[first + k];
				if (!light || !light->bPointLight) continue;
				if (trace && Focus >= 0 && Slots[Focus].light == light) focusIndex = k;
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
					memcpy(constants[8 + k], palette[Slots[s].slot == 0 ? 0 : 1 + (Slots[s].slot - 1) % 10], 3 * sizeof(float));
					constants[8 + k][3] = Slots[s].fade;
					matched++;
					break;
				}
			}
		}
		if (matched) memcpy(constants[6], Params, sizeof(Params));
		constants[7][0] = matched ? DebugView : 0.0f;
		constants[7][1] = matched ? NearFade : 0.0f;
		if (trace && wanted) {
			memcpy(constants[6], Params, sizeof(Params));
			constants[7][0] = DebugView;
			constants[7][2] = (float)focusIndex;
			if (Focus >= 0) {
				const D3DXVECTOR4& camera = TheRenderManager->CameraPosition;
				constants[14][0] = Slots[Focus].position.x - camera.x;
				constants[14][1] = Slots[Focus].position.y - camera.y;
				constants[14][2] = Slots[Focus].position.z - camera.z;
				constants[14][3] = 1.0f / Slots[Focus].radius;
				bound[5] = Slots[Focus].cube;
				if (!force) { TraceDraws++; if (focusIndex >= 0) TraceDrawsWithLamp++; }
			}
		}
		for (int k = 0; k < 6; k++)
			if (force || TheRenderManager->renderState->GetTexture(Stage[k]) != bound[k]) Bind(Stage[k], bound[k], force);
		if (force || memcmp(constants, Uploaded, sizeof(constants))) {
			TheRenderManager->device->SetPixelShaderConstantF(174, &constants[0][0], 15);
			memcpy(Uploaded, constants, sizeof(constants));
		}
		if (GpuTimer::Enabled && !force) { StatDraws++; if (matched) { StatMatchedDraws++; StatMatchedLights += matched; } }
	}

	inline void SetForDraw(NiD3DPixelShaderEx* pixelShader, bool world, bool firstPerson) {
		Bound = false;
		LastPixelShaderName = pixelShader ? pixelShader->Name : nullptr;
		LastPixelShaderShadowed = false;
		if (!CompiledIn || !pixelShader) return;
		ShaderRecordPixel* interiorRecord = pixelShader->GetShaderRecord(ShaderRecordType::Interior);
		if (!interiorRecord || !interiorRecord->PointShadowForward || pixelShader->ShaderHandle != interiorRecord->ShaderHandle)
			return;
		Bound = true;
		LastPixelShaderShadowed = Active;
		BoundThisFrame = true;
		SetForPass(*(RenderPassView**)0x011F91E0, world, firstPerson, true);
	}

	inline void OnRenderPass(const RenderPassView* pass, bool world, bool firstPerson) {
		if (Bound) SetForPass(pass, world, firstPerson, false);
	}

	inline unsigned ObjectLampFrameLines = 0, ObjectLampLinesFrame = 0;
	inline void CompareObjectLamps(const void* geometry, const ObjectLampState& o) {
		const CameraState& a = Cameras[o.lastFrame & 7];
		const CameraState& b = Cameras[o.frame & 7];
		const bool known = a.frame == o.lastFrame && b.frame == o.frame;
		const float turn = known ? D3DXToDegree(acosf((std::max)(-1.0f, (std::min)(1.0f, D3DXVec3Dot(&a.forward, &b.forward))))) : -1.0f;
		const D3DXVECTOR3 camMove = b.position - a.position, playerMove = b.player - a.player;
		const float moved = known ? D3DXVec3Length(&camMove) : -1.0f, walked = known ? D3DXVec3Length(&playerMove) : -1.0f;
		auto find = [](const ShadowSceneLight* const* lamps, int count, const ShadowSceneLight* light) { for (int i = 0; i < count; i++) if (lamps[i] == light) return i; return -1; };
		auto slotOf = [](const ShadowSceneLight* light) { for (int s = 0; s < SlotCount; s++) if (Slots[s].light == light) return Slots[s].slot; return -1; };
		const NiAVObject* object = (const NiAVObject*)geometry;
		char text[1400] = "";
		size_t used = 0;
		int changes = 0;
		for (int round = 0; round < 2; round++) {
			const int count = round ? o.lastCount : o.count;
			for (int i = 0; i < count; i++) {
				const ShadowSceneLight* light = round ? o.last[i] : o.lamps[i];
				if (round && find(o.lamps, o.count, light) >= 0) continue;
				const int now = find(o.lamps, o.count, light), before = find(o.last, o.lastCount, light);
				const int howNow = now >= 0 ? o.how[now] : 0, howBefore = before >= 0 ? o.lastHow[before] : 0;
				if (howNow == howBefore) continue;
				changes++;
				if (!howBefore) StatLampAppeared++;
				else if (!howNow) StatLampGone++;
				else if ((howBefore & 1) && !(howNow & 1)) StatLampLostShadow++;
				else if (!(howBefore & 1) && (howNow & 1)) StatLampGotShadow++;
				if (known && walked < 0.5f) StatLampStill++;
				if (used + 200 > sizeof(text)) continue;
				const char sign = !howBefore ? '+' : !howNow ? '-' : '~';
				const int slot = slotOf(light);
				if (slot >= 0 && light->sourceLight && object->m_kWorldBound) {
					const NiPoint3& lamp = light->sourceLight->m_worldTransform.pos;
					const NiBound& bound = *object->m_kWorldBound;
					const float dx = lamp.x - bound.Center.x, dy = lamp.y - bound.Center.y, dz = lamp.z - bound.Center.z;
					const float reach = light->sourceLight->Spec.r;
					const float gap = sqrtf(dx * dx + dy * dy + dz * dz) - bound.Radius - reach;
					const float cx = lamp.x - b.position.x, cy = lamp.y - b.position.y, cz = lamp.z - b.position.z;
					used += sprintf_s(text + used, sizeof(text) - used, " %c%p[slot %d, gap %.0f, reach %.0f, LOD %.2f, fade %.2f, faces out of view %02X (game %02X), %.0f from camera, NVR view test %s]",
						sign, (const void*)light, slot, gap, reach, light->fLODDimmer, light->fFade, (unsigned)light->bIsEnabled,
						GameCullMask.count(light) ? (unsigned)GameCullMask[light] : 0xFFFFu, sqrtf(cx * cx + cy * cy + cz * cz), LampReachesView(light) ? "in" : "out");
				}
				else used += sprintf_s(text + used, sizeof(text) - used, " %c%p[no cube map]", sign, (const void*)light);
			}
		}
		if (!changes || !known || !(walked < 0.5f) || !(turn > 0.01f || moved > 0.5f)) return;
		if (ObjectLampLinesFrame != Frame) { ObjectLampLinesFrame = Frame; ObjectLampFrameLines = 0; }
		if (ObjectLampLines >= 600 || ObjectLampFrameLines >= 2) return;
		ObjectLampFrameLines++;
		if (++ObjectLampLines == 600) Logger::Log("OBJECT lamps log: 600 lines, stopping until restart.");
		Logger::Log("OBJECT %s %p (bound radius %.0f): lamps %d -> %d,%s | camera turned %.2f deg, moved %.1f",
			object->m_pcName ? object->m_pcName : "(unnamed)", geometry, object->m_kWorldBound ? object->m_kWorldBound->Radius : -1.0f,
			(int)o.lastCount, (int)o.count, text, turn, moved);
	}

	inline void AimFlush() {
		if (!AimHitCount || AimLines >= 500 || Frame - AimLastLogFrame < 30) return;
		bool same = AimHitCount == AimLoggedCount;
		for (int i = 0; same && i < AimHitCount; i++) {
			bool found = false;
			for (int j = 0; j < AimLoggedCount; j++) if (AimLogged[j] == AimHits[i].geometry) found = true;
			same = found;
		}
		if (same) return;
		AimLastLogFrame = Frame;
		AimLoggedCount = AimHitCount;
		for (int i = 0; i < AimHitCount; i++) AimLogged[i] = AimHits[i].geometry;
		for (int i = 1; i < AimHitCount; i++)
			for (int j = i; j > 0 && AimHits[j].off < AimHits[j - 1].off; j--) { AimHit t = AimHits[j]; AimHits[j] = AimHits[j - 1]; AimHits[j - 1] = t; }
		AimLines++;
		Logger::Log("AIM %d objects on the line of sight (camera %.0f %.0f %.0f)", AimHitCount,
			TheRenderManager->CameraPosition.x, TheRenderManager->CameraPosition.y, TheRenderManager->CameraPosition.z);
		auto slotOf = [](const ShadowSceneLight* light) { for (int s = 0; s < SlotCount; s++) if (Slots[s].light == light) return Slots[s].slot; return -1; };
		for (int i = 0; i < AimHitCount && AimLines < 500; i++) {
			const AimHit& h = AimHits[i];
			const NiAVObject* object = (const NiAVObject*)h.geometry;
			char text[1200] = "";
			size_t used = 0;
			for (int p = 0; p < h.passes && used + 160 < sizeof(text); p++) {
				const AimPass& a = h.pass[p];
				used += sprintf_s(text + used, sizeof(text) - used, " | %s (%s) + %s (%s) %s, pass %04X, %d lights%s:", a.vs ? a.vs : "?",
					a.vsNvr ? "NVR" : "game", a.ps ? a.ps : "?", a.psNvr ? "NVR" : "game", a.shadowed ? "forward shadows" : "NO forward shadows",
					(unsigned)a.passEnum, (int)a.numLights, a.shadowed && !a.strength ? " (strength 0: lookups skipped)" : "");
				for (int k = 0; k < a.count && used + 40 < sizeof(text); k++) {
					if (!a.point[k]) { used += sprintf_s(text + used, sizeof(text) - used, " %d:dir", (int)a.index[k]); continue; }
					const int slot = slotOf(a.lamps[k]);
					used += sprintf_s(text + used, sizeof(text) - used, " %d:%p/s%d%s", (int)a.index[k], (const void*)a.lamps[k], slot,
						!a.shadowed ? "" : a.uploaded[k] ? "" : slot >= 0 ? "(NOT UPLOADED)" : "");
				}
			}
			AimLines++;
			Logger::Log("AIM   %s %p: bound radius %.0f, centre %.0f along the line, %.0f off it%s", object->m_pcName ? object->m_pcName : "(unnamed)",
				h.geometry, h.radius, h.along, h.off, text);
		}
	}

	inline void NoteAim(const RenderPassView* pass) {
		if (AimFrame != Frame) { AimFlush(); AimHitCount = 0; AimFrame = Frame; }
		const NiAVObject* object = pass->geometry;
		if (!object->m_kWorldBound) return;
		const NiBound& bound = *object->m_kWorldBound;
		const D3DXMATRIX& camera = TheRenderManager->InvViewMatrix;
		D3DXVECTOR3 forward(camera._31, camera._32, camera._33);
		D3DXVec3Normalize(&forward, &forward);
		const D3DXVECTOR3 v(bound.Center.x - TheRenderManager->CameraPosition.x, bound.Center.y - TheRenderManager->CameraPosition.y,
			bound.Center.z - TheRenderManager->CameraPosition.z);
		const float along = D3DXVec3Dot(&v, &forward);
		const float off2 = D3DXVec3LengthSq(&v) - along * along;
		if (along < -bound.Radius || off2 > bound.Radius * bound.Radius) return;
		int i = 0;
		while (i < AimHitCount && AimHits[i].geometry != object) i++;
		if (i == AimHitCount) {
			if (AimHitCount == 10) return;
			AimHits[i] = {};
			AimHits[i].geometry = object; AimHits[i].along = along; AimHits[i].off = sqrtf((std::max)(off2, 0.0f)); AimHits[i].radius = bound.Radius;
			AimHitCount++;
		}
		AimHit& h = AimHits[i];
		if (h.passes == 4) return;
		AimPass& a = h.pass[h.passes++];
		a = {};
		a.vs = LastVertexShaderName; a.ps = LastPixelShaderName; a.vsNvr = LastVertexShaderNvr; a.psNvr = LastPixelShaderNvr;
		a.shadowed = LastPixelShaderShadowed && Bound;
		a.passEnum = pass->passEnum; a.numLights = pass->numLights; a.strength = Uploaded[6][0] > 0.0f;
		const int lights = pass->lights ? (std::min)((int)pass->numLights, 6) : 0;
		for (int k = 0; k < lights; k++) {
			if (!pass->lights[k]) continue;
			a.index[a.count] = (UInt8)k; a.point[a.count] = pass->lights[k]->bPointLight != 0;
			const int j = k - FirstShaderLight(pass);
			a.uploaded[a.count] = j >= 0 && j < 6 && Uploaded[j][3] > 0.0f;
			a.lamps[a.count++] = pass->lights[k];
		}
	}

	inline void NoteDraw(const RenderPassView* pass, bool world) {
		if (!LogObjectLamps || !world || !pass || !pass->geometry) return;
		NoteAim(pass);
		if (!pass->lights) return;
		ObjectLampState& o = ObjectLamps[pass->geometry];
		if (o.frame != Frame) {
			if (o.frame && o.lastFrame && o.frame == o.lastFrame + 1) CompareObjectLamps(pass->geometry, o);
			memcpy(o.last, o.lamps, sizeof(o.lamps)); memcpy(o.lastHow, o.how, sizeof(o.how)); memcpy(o.lastShader, o.shader, sizeof(o.shader));
			o.lastCount = o.count;
			o.lastFrame = o.frame;
			o.count = 0;
			o.frame = Frame;
		}
		const UInt8 how = LastPixelShaderShadowed ? 1 : 2;
		const int lights = (std::min)((int)pass->numLights, 12);
		for (int k = 0; k < lights; k++) {
			const ShadowSceneLight* light = pass->lights[k];
			if (!light || !light->bPointLight) continue;
			int i = 0;
			while (i < o.count && o.lamps[i] != light) i++;
			if (i == o.count) {
				if (o.count == 12) continue;
				o.lamps[i] = light; o.how[i] = 0; o.shader[i] = nullptr; o.count++;
			}
			o.how[i] |= how;
			if (!o.shader[i] || how == 2) o.shader[i] = LastPixelShaderName;
		}
	}

	inline void FrameStats() {
		if (!GpuTimer::Enabled || !CompiledIn) return;
		if (++StatFrames >= 240) {
			const float perFrame = 1.0f / StatFrames;
			Logger::Log("POINT SHADOWS FORWARD %d shaders compiled in: %.1f cube maps in use, %.0f objects, %.0f with a shadowed lamp, %.1f lamps matched per frame",
				CompiledCount, (float)SlotCount, StatDraws * perFrame, StatMatchedDraws * perFrame, StatMatchedLights * perFrame);
			Logger::Log("POINT SHADOWS KeepLampsInView %s: per frame %.1f lamp view tests by the game, %.1f lamps it called wholly out of view, %.1f of those kept active (their light reaches the view)",
				KeepLampsNow ? "on" : "off", StatCullCalls * perFrame, StatCullOut * perFrame, StatCullKept * perFrame);
			StatCullCalls = StatCullOut = StatCullKept = 0;
			if (LogObjectLamps)
				Logger::Log("POINT SHADOWS OBJECT LAMPS (LogLamps): per frame %.2f lamps came onto objects, %.2f left them, %.2f lost their shadow (now in a pass without), %.2f got it; %.2f of these with the player standing still",
					StatLampAppeared * perFrame, StatLampGone * perFrame, StatLampLostShadow * perFrame, StatLampGotShadow * perFrame, StatLampStill * perFrame);
			StatLampAppeared = StatLampGone = StatLampLostShadow = StatLampGotShadow = StatLampStill = 0;
			StatFrames = StatDraws = StatMatchedDraws = StatMatchedLights = 0;
		}
	}
}
