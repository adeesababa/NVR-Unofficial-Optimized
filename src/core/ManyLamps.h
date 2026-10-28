#pragma once
#include "LinearPreDecode.h"
#include "GpuProfiler.h"
#include "PointShadowForward.h"

namespace ManyLamps {

	enum { MaxLamps = 24, FirstRegister = 37, Registers = 1 + 2 * MaxLamps };
	enum : UInt8 { CompareImm = MaxLamps - 1 };
	struct Range { UINT reg, row, rows; };
	constexpr Range Ranges[3] = { { 37, 0, 27 }, { 76, 27, 12 }, { 98, 39, 10 } };
	static_assert(27 + 12 + 10 == Registers, "ManyLamps ranges");

	inline int Wanted = -1;
	inline int State = -1;
	inline bool LinearCompiled = false;
	inline float Block[Registers][4] = {};
	inline UINT BlockRegisters = 0;
	inline const void* LastPass = nullptr;
	inline UInt32 LastFrame = 0;
	inline UInt32 FrameId = 1;
	inline bool Bound = false;
	inline bool OwnUpload = false;
	inline bool FirstPerson = false;
	inline D3DXVECTOR3 FirstPersonOffset(0.0f, 0.0f, 0.0f);
	inline bool HookWarned = false;

	inline unsigned StatFrames = 0;
	inline unsigned long long StatDraws = 0, StatExtraDraws = 0, StatExtras = 0, StatOver = 0, StatByCount[MaxLamps + 1] = {};

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

	typedef void(__cdecl* InverseWorldFn)(const NiTransform*, const NiSkinInstance*, D3DXMATRIX*);
	inline const InverseWorldFn GetInverseWorldMatrix = (InverseWorldFn)0x00C4C2D0;
	inline D3DXMATRIX ToObject;
	inline float ReachScale = 1.0f;
	inline D3DXVECTOR3 LightingOffset(0.0f, 0.0f, 0.0f);

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
		if (!*(const bool*)0x011F941E && dimmer > 1.0f) dimmer = 1.0f;
		const float lod = light->fLODDimmer;
		const float raw[4] = { source->Diff.r * dimmer * lod, source->Diff.g * dimmer * lod, source->Diff.b * dimmer * lod, 0.0f };
		float* c = Block[2 + 2 * k];
		if (linear) LinearPreDecode::DecodeFast(c, raw, gamma);
		else memcpy(c, raw, sizeof(raw));
		return true;
	}

	inline void Upload() {
		OwnUpload = true;
		for (const Range& r : Ranges) {
			if (BlockRegisters <= r.row) break;
			TheRenderManager->device->SetPixelShaderConstantF(r.reg, &Block[r.row][0], min(r.rows, BlockRegisters - r.row));
		}
		OwnUpload = false;
	}

	inline void SetForPass(const PointShadowForward::RenderPassView* pass) {
		int count = 0;
		const bool linear = LinearCompiled && LinearPreDecode::Gamma > 1.0f;
		const float gamma = LinearPreDecode::Gamma;
		if (pass && pass->lights && pass->geometry) {
			NiGeometry* geometry = pass->geometry;
			GetInverseWorldMatrix(&geometry->m_worldTransform, geometry->skinInstance, &ToObject);
			ReachScale = !geometry->skinInstance && geometry->m_worldTransform.scale > 0.0f ? 1.0f / geometry->m_worldTransform.scale : 1.0f;
			LightingOffset = FirstPerson ? FirstPersonOffset : D3DXVECTOR3(0.0f, 0.0f, 0.0f);
			for (int i = 1; i < pass->numLights && count < MaxLamps; i++) {
				const ShadowSceneLight* light = pass->lights[i];
				if (!light || !Put(count, light, linear, gamma)) memset(Block[1 + 2 * count], 0, 2 * sizeof(Block[0]));
				count++;
			}
			if (pass->maxLights > pass->numLights && IsOptPass(pass->passEnum)) {
				const UInt8* property = (const UInt8*)geometry->propertyState.m_spShadeProperty;
				int extras = 0;
				for (const UInt32* node = property ? *(const UInt32* const*)(property + 0x60) : nullptr; node; node = (const UInt32*)node[0]) {
					const UInt8* light = (const UInt8*)node[2];
					if (!light || *(const UInt16*)(light + 0x110) == 0xFF) continue;
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

	inline bool IsOurs(const char* name) {
		return name && !strncmp(name, "SLS203", 6) && name[6] >= '4' && name[6] <= '6' && !strcmp(name + 7, ".pso");
	}

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
