#pragma once

// UNOFFICIAL 2026-10-28 RT lamp shadows ([Shaders.ShadowsInteriors.RT], docs/next/FORK-RT-LAMPS.md). Indoors, each lamp's
// forward shadow (PointShadowForward.hlsl) is traced against the scene instead of read from its cube map: the object shaders
// are compiled with POINT_SHADOW_RT, whose rays are texCUBEgrad samples that NVR DXVK turns into ray queries against its TLAS
// (fork patch nvr-rtlamp, ID3D9NvrExtRtLamp). Everything else stays: the lamps are still picked, ranked and given cube slots
// as before (the cube maps are the fallback and the A/B), and the shader takes the rays or the cube map per lamp per draw.
//
// Needs, at startup: the setting on, NVR DXVK with the RtLamp interface, and a GPU with ray queries (the fork says so). Without
// any of them (the GTX 1070, native D3D9, stock DXVK, an older fork) POINT_SHADOW_RT is never defined and every shader is
// exactly what it was; the menu shows why.
//
// Per draw (PointShadowForward::SetForPass) c215-c217, see the shader for the layout.

#include "DxvkCompute.h"
#include "LampFixtures.h"
#include "RtScene.h"	// the RT core's scene (rt-sun-core); a stand-in until it lands

namespace RtLampShadows {

	inline bool CompiledIn = false;			// POINT_SHADOW_RT in the object shaders (fixed at startup)
	inline char Status[200] = "not checked yet";

	// This frame's values (Update), and the draw's last upload
	inline float Mode = 0.0f;				// c215.x: 0 cube maps, 1 rays, 2 compare view
	inline float Rays = 1.0f;				// c215.y
	inline float Seed = 0.0f;				// c215.w
	inline int MaxLamps = 8;				// lamps by cube slot below this get rays, the others keep their cube map
	inline float Space[4] = {};				// c216: camera-relative -> TLAS space; w the stop before the lamp
	inline float Lamp[4] = {};				// c217
	inline float Uploaded[3][4] = {};
	inline bool UploadedValid = false;

	inline ID3D9NvrExtRtLamp* Interface() {
		static bool queried = false;
		static ID3D9NvrExtRtLamp* rt = nullptr;
		if (!queried) {
			queried = true;
			ID3D9NvrExt* ext = DxvkCompute::Extension();
			if (!ext || FAILED(ext->QueryInterface(__uuidof(ID3D9NvrExtRtLamp), (void**)&rt))) rt = nullptr;
		}
		return rt;
	}

	// RenderManager, right after DxvkCompute::Initialize and before any shader is loaded: decides POINT_SHADOW_RT for the session.
	// The scene is the RT core's (RtScene.h): its address is fixed for the session and baked into the lamp shaders.
	inline void Initialize() {
		CompiledIn = false;
		if (!TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.RT", "Enabled")) {
			strcpy_s(Status, "off ([Shaders.ShadowsInteriors.RT] Enabled false at startup)");
			return;
		}
		if (!TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Forward", "Enabled")) {
			strcpy_s(Status, "off: needs the per-object lamp shadows ([Shaders.ShadowsInteriors.Forward] Enabled)");
			Logger::Log("UNOFFICIAL RT lamp shadows: %s", Status);
			return;
		}
		ID3D9NvrExtRtLamp* rt = Interface();
		const UINT64 scene = rt ? RtScene::Address() : 0;
		if (!rt) strcpy_s(Status, "off: this d3d9.dll has no RT lamp interface (needs NVR DXVK with patch nvr-rtlamp)");
		else if (!scene) _snprintf_s(Status, sizeof(Status), _TRUNCATE, "off: no ray tracing scene (%s)", RtScene::Status());
		else if (FAILED(rt->EnableCubeGradRayQueries(scene)))
			strcpy_s(Status, "off: the fork refused the ray query hook (see the DXVK log)");
		else {
			CompiledIn = true;
			strcpy_s(Status, "on: lamp shaders compiled with rays");
		}
		Logger::Log("UNOFFICIAL RT lamp shadows: %s", Status);
	}

	// PointShadowForward::Publish, once a frame indoors.
	inline void Update() {
		if (!CompiledIn) return;
		const char* section = "Shaders.ShadowsInteriors.RT";
		const int debugView = TheSettingManager->GetSettingI(section, "DebugView");
		// Rays only once the RT core built the scene this frame (an unbuilt scene must never be traced)
		Mode = !TheSettingManager->GetSettingI(section, "Live") || !RtScene::BuiltThisFrame() ? 0.0f : debugView == 1 ? 2.0f : 1.0f;
		Rays = (float)(std::min)((std::max)(TheSettingManager->GetSettingI(section, "Rays"), 1), 8);
		MaxLamps = (std::max)(TheSettingManager->GetSettingI(section, "MaxLamps"), 0);
		static unsigned frame = 0;
		frame++;
		Seed = TheSettingManager->GetSettingI(section, "Jitter") ? fmodf((float)frame * 0.61803399f, 1.0f) : 0.0f;
		// The scene is in camera-relative world space, as the shader's positions: no offset (kept for a scene in another space)
		Space[0] = Space[1] = Space[2] = 0.0f;
		Space[3] = (std::max)(TheSettingManager->GetSettingF(section, "LampStop"), 0.0f);
		const float radius = (std::max)(TheSettingManager->GetSettingF(section, "LampRadius"), 0.0f);
		const float invisible = TheSettingManager->GetSettingF(section, "InvisibleLightRadius");
		Lamp[0] = radius;
		Lamp[1] = invisible < 0.0f ? radius : invisible;
		// Lamp models skip their own light over the last LampModelDistance of each ray, as LampModelShadows false does for the cube maps
		const bool skipModels = TheSettingManager->GetSettingI(section, "SkipLampModels") != 0;
		Lamp[2] = skipModels ? (std::max)(TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Forward", "LampModelDistance"), 0.0f) : 0.0f;
		// Cull masks (the fork's instance masks): 0x01 everything else, 0x02 lamp glass and glow meshes, 0x04 lamp models, 0x08 actors
		int farMask = 0x01 | 0x04;
		if (!TheSettingManager->GetSettingI(section, "SkipLampGlass")) farMask |= 0x02;
		if (TheSettingManager->GetSettingI(section, "Actors")) farMask |= 0x08;
		const int nearMask = skipModels ? (farMask & ~0x04) : farMask;
		const float offset = (std::min)((std::max)(TheSettingManager->GetSettingF(section, "StartOffset"), 0.0f), 31.0f);
		Lamp[3] = (float)farMask + 256.0f * (float)nearMask + 65536.0f * floorf(offset * 8.0f + 0.5f);
	}

	// The RT core's instance mask for a piece of the scene (the rays' cull masks in Update): lamp glass and glow meshes (LampFixtures::
	// LampPart), the rest of a lamp model listed in the cell (LampFixtures' models: fixtures, shades, chandeliers, fans), actors,
	// everything else. A lamp part inside a lamp model counts as glass.
	inline UINT InstanceMask(NiGeometry* geometry, bool actor) {
		if (actor) return 0x08;
		if (!geometry) return 0x01;
		if (LampFixtures::LampPart(geometry)) return 0x02;
		if (LampFixtures::ModelIndexOf(geometry) >= 0) return 0x04;
		return 0x01;
	}

	// PointShadowForward::SetForPass: rayBits = the draw's lights (bit k = shader light k) that take rays this frame.
	inline void SetForPass(unsigned rayBits, bool force) {
		if (!CompiledIn) return;
		float c[3][4] = {};
		if (rayBits && Mode > 0.0f) {
			c[0][0] = Mode; c[0][1] = Rays; c[0][2] = (float)(rayBits & 63u); c[0][3] = Seed;
			memcpy(c[1], Space, sizeof(Space));
			memcpy(c[2], Lamp, sizeof(Lamp));
		}
		if (force || !UploadedValid || memcmp(c, Uploaded, sizeof(c))) {
			TheRenderManager->device->SetPixelShaderConstantF(215, &c[0][0], 3);
			memcpy(Uploaded, c, sizeof(c));
			UploadedValid = true;
		}
	}

}
