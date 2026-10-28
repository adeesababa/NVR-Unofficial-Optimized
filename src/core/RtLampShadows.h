#pragma once

// UNOFFICIAL 2026-10-28 RT lamp shadows ([Shaders.ShadowsInteriors.RT], docs/next/FORK-RT-LAMPS.md). Indoors, each lamp's forward
// shadow (PointShadowForward.hlsl) is read from a traced result instead of its cube map. The tracing runs in the RT core's 64-bit
// helper (32-bit Windows Vulkan drivers have no ray tracing, so the game cannot trace): every frame NVR sends it the frame's depth
// and normals and this table of up to 18 shadowed lamps, the helper runs Compute/Rt/RtLampTrace.comp against its scene, and the
// result (4-bit visibility per lamp per pixel + the view depth) comes back as a D3D9 texture, a frame or more later. The object
// shaders (POINT_SHADOW_RT) reproject into the frame that result was traced for and use it where that frame saw the same surface;
// elsewhere the cube map, as before. The lamps are still picked, ranked and given cube slots as before; cube slots 0-17 are the
// traced slots.
//
// POINT_SHADOW_RT is defined at startup only with the setting on and the RT helper available (RtCoreStandIn.h until the RT core
// lands). Without it (the GTX 1070 has no RT anywhere; native D3D9; stock DXVK) every shader is exactly what it was.
//
// Per draw (PointShadowForward::SetForPass): c206-c209 the reprojection, c215 mode + depth tolerance, c216-c217 the draw's lights'
// traced slots, the result on s15 (the sixth lamp's cube stage: see the shader).

#include "LampFixtures.h"
#include "RtCoreStandIn.h"	// the RT core's helper (session rt-sun-core); a stand-in until it lands

namespace RtLampShadows {

	inline constexpr int TracedSlots = 18;		// RtLampTrace.comp: 6 lamps x 4 bits in each of r, g, b (exact floats), depth in a

	inline bool CompiledIn = false;			// POINT_SHADOW_RT in the object shaders (fixed at startup)
	inline char Status[200] = "not checked yet";

	// The trace pass's constants: RtLampTrace.comp's Pass block (std140: matrices 64 bytes, every vec4 16)
	struct TracePass {
		D3DXMATRIX	view, projection;			// TESR_ViewTransform, TESR_ProjectionTransform as their SetMatrix uploads them
		float		cameraData[4];				// TESR_CameraData (y far)
		float		size[4];					// the output's size, 1 / size
		float		params[4];					// rays, StartOffset, LampStop, noise offset
		float		params2[4];					// LampModelDistance, far mask, near mask, lamp count
		float		lamps[TracedSlots][4];		// camera-relative position, reach (0 = none)
		float		lampSize[TracedSlots][4];	// x: radius the rays aim over
	};
	static_assert(sizeof(TracePass) == 64 * 2 + 16 * 4 + 16 * TracedSlots * 2, "std140 layout of RtLampTrace.comp's Pass");

	// The camera each submitted frame was traced with, by the helper's frame number (the result names its frame)
	struct TracedCamera { UInt64 frame; D3DXMATRIX view; D3DXVECTOR4 position; float p11, p22; };
	inline TracedCamera Cameras[8] = {};

	// This frame's per-draw values
	inline float Mode = 0.0f;				// c215.x: 0 cube maps, 1 traced, 2 compare view
	inline int MaxLamps = 8;				// cube slots below min(this, 18) use the traced result
	inline float Reproject[4][4] = {};		// c206-c209
	inline float DepthTolerance = 0.02f;	// c215.y
	inline IDirect3DTexture9* Result = nullptr;
	inline float Uploaded[7][4] = {};
	inline bool UploadedValid = false;

	inline const D3DXVECTOR4* Registered(const char* name) {
		auto found = TheShaderManager->ConstantsTable.find(name);
		return found == TheShaderManager->ConstantsTable.end() ? nullptr : found->second;
	}

	// RenderManager, right after DxvkCompute::Initialize and before any shader is loaded: decides POINT_SHADOW_RT for the session.
	inline void Initialize() {
		CompiledIn = false;
		if (!TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.RT", "Enabled")) {
			strcpy_s(Status, "off ([Shaders.ShadowsInteriors.RT] Enabled false at startup)");
			return;
		}
		if (!TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Forward", "Enabled"))
			strcpy_s(Status, "off: needs the per-object lamp shadows ([Shaders.ShadowsInteriors.Forward] Enabled)");
		else if (!RtCoreStandIn::Available())
			_snprintf_s(Status, sizeof(Status), _TRUNCATE, "off: no ray tracing (%s)", RtCoreStandIn::Status());
		else {
			CompiledIn = true;
			strcpy_s(Status, "on: lamp shaders compiled to read the traced lamp shadows");
		}
		Logger::Log("UNOFFICIAL RT lamp shadows: %s", Status);
	}

	// The game-to-scene instance mask the RT core gives each piece of the scene (the trace pass's cull masks below): lamp glass and glow
	// meshes (LampFixtures::LampPart), the rest of a lamp model listed in the cell (fixtures, shades, chandeliers, fans), actors,
	// everything else. A lamp part inside a lamp model counts as glass.
	inline UINT InstanceMask(NiGeometry* geometry, bool actor) {
		if (actor) return 0x08;
		if (!geometry) return 0x01;
		if (LampFixtures::LampPart(geometry)) return 0x02;
		if (LampFixtures::ModelIndexOf(geometry) >= 0) return 0x04;
		return 0x01;
	}

	// PointShadowForward::Publish, once a frame indoors after the slots are known: the trace request for this frame's depth, and the
	// reprojection into the newest result. slotLights: the published lamps' cube slot, position (world), reach and fade.
	struct SlotLamp { int slot; D3DXVECTOR3 position; float reach; float fade; };
	inline void Submit(const SlotLamp* lamps, int count) {
		Mode = 0.0f;
		Result = nullptr;
		if (!CompiledIn) return;
		const char* section = "Shaders.ShadowsInteriors.RT";
		MaxLamps = (std::min)((std::max)(TheSettingManager->GetSettingI(section, "MaxLamps"), 0), TracedSlots);
		DepthTolerance = (std::max)(TheSettingManager->GetSettingF(section, "DepthTolerance"), 0.0f);
		const bool live = TheSettingManager->GetSettingI(section, "Live") != 0;
		const int debugView = TheSettingManager->GetSettingI(section, "DebugView");

		// This frame's request
		static UInt64 frame = 0;
		frame++;
		const D3DXVECTOR4* view = Registered("TESR_ViewTransform");
		const D3DXVECTOR4* projection = Registered("TESR_ProjectionTransform");
		const D3DXVECTOR4* cameraData = Registered("TESR_CameraData");
		if (!view || !projection || !cameraData || !MaxLamps) return;
		TracePass pass = {};
		memcpy(&pass.view, view, sizeof(D3DXMATRIX));
		memcpy(&pass.projection, projection, sizeof(D3DXMATRIX));
		memcpy(pass.cameraData, cameraData, sizeof(pass.cameraData));
		const float width = (float)TheRenderManager->width, height = (float)TheRenderManager->height;
		pass.size[0] = width; pass.size[1] = height; pass.size[2] = 1.0f / width; pass.size[3] = 1.0f / height;
		pass.params[0] = (float)(std::min)((std::max)(TheSettingManager->GetSettingI(section, "Rays"), 1), 8);
		pass.params[1] = (std::max)(TheSettingManager->GetSettingF(section, "StartOffset"), 0.0f);
		pass.params[2] = (std::max)(TheSettingManager->GetSettingF(section, "LampStop"), 0.0f);
		pass.params[3] = TheSettingManager->GetSettingI(section, "Jitter") ? fmodf((float)frame * 0.61803399f, 1.0f) : 0.0f;
		// Lamp models skip their own light over the last LampModelDistance of each ray, as LampModelShadows false does for the cube maps
		const bool skipModels = TheSettingManager->GetSettingI(section, "SkipLampModels") != 0;
		pass.params2[0] = skipModels ? (std::max)(TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Forward", "LampModelDistance"), 0.0f) : 0.0f;
		int farMask = 0x01 | 0x04;
		if (!TheSettingManager->GetSettingI(section, "SkipLampGlass")) farMask |= 0x02;
		if (TheSettingManager->GetSettingI(section, "Actors")) farMask |= 0x08;
		pass.params2[1] = (float)farMask;
		pass.params2[2] = (float)(skipModels ? (farMask & ~0x04) : farMask);
		pass.params2[3] = (float)MaxLamps;
		const float radius = (std::max)(TheSettingManager->GetSettingF(section, "LampRadius"), 0.0f);
		const float invisible = TheSettingManager->GetSettingF(section, "InvisibleLightRadius");
		const D3DXVECTOR4& camera = TheRenderManager->CameraPosition;
		for (int i = 0; i < count; i++) {
			const int s = lamps[i].slot;
			if (s < 0 || s >= MaxLamps) continue;
			pass.lamps[s][0] = lamps[i].position.x - camera.x;
			pass.lamps[s][1] = lamps[i].position.y - camera.y;
			pass.lamps[s][2] = lamps[i].position.z - camera.z;
			pass.lamps[s][3] = lamps[i].reach;
			pass.lampSize[s][0] = lamps[i].fade < 0.0f && invisible >= 0.0f ? invisible : radius;	// negative fade: an invisible light
		}
		TracedCamera& mine = Cameras[frame & 7];
		mine = { frame, TheRenderManager->viewMatrix, camera, TheRenderManager->projMatrix._11, TheRenderManager->projMatrix._22 };
		RtCoreStandIn::SubmitLampTrace(frame, &pass, sizeof(pass));

		// The newest result and the camera it was traced with
		UInt64 resultFrame = 0;
		IDirect3DTexture9* texture = RtCoreStandIn::LampTraceResult(&resultFrame);
		const TracedCamera* traced = nullptr;
		for (const TracedCamera& c : Cameras) if (c.frame == resultFrame && resultFrame) traced = &c;
		if (!texture || !traced || !live) return;
		const D3DXMATRIX& v = traced->view;
		const D3DXVECTOR3 right(v._11, v._21, v._31), up(v._12, v._22, v._32), forward(v._13, v._23, v._33);
		const D3DXVECTOR3 d(camera.x - traced->position.x, camera.y - traced->position.y, camera.z - traced->position.z);
		const float rows[3][3] = { { right.x, up.x, forward.x }, { right.y, up.y, forward.y }, { right.z, up.z, forward.z } };
		for (int i = 0; i < 3; i++) {
			Reproject[i][0] = rows[i][0] * traced->p11;
			Reproject[i][1] = rows[i][1] * traced->p22;
			Reproject[i][2] = 0.0f;
			Reproject[i][3] = rows[i][2];
		}
		Reproject[3][0] = D3DXVec3Dot(&d, &right) * traced->p11;
		Reproject[3][1] = D3DXVec3Dot(&d, &up) * traced->p22;
		Reproject[3][2] = 0.0f;
		Reproject[3][3] = D3DXVec3Dot(&d, &forward);
		Result = texture;
		Mode = debugView == 1 ? 2.0f : 1.0f;
	}

	// The slot a draw's light reads in the traced result, -1 for its cube map.
	inline float TracedSlot(int cubeSlot) { return Mode > 0.0f && cubeSlot >= 0 && cubeSlot < MaxLamps ? (float)cubeSlot : -1.0f; }

	// PointShadowForward::SetForPass: slots = the draw's lights' TracedSlot (-1 none). Binds the result on s15 (or the neutral 2D
	// texture: s15 is a 2D sampler in these shaders).
	inline void SetForPass(const float slots[6], bool force) {
		if (!CompiledIn) return;
		float c[7][4] = {};
		memcpy(c, Reproject, sizeof(Reproject));
		c[4][0] = Mode; c[4][1] = DepthTolerance;
		memcpy(c[5], slots, 4 * sizeof(float));
		c[6][0] = slots[4]; c[6][1] = slots[5]; c[6][2] = c[6][3] = -1.0f;
		if (force || !UploadedValid || memcmp(c, Uploaded, sizeof(c))) {
			TheRenderManager->device->SetPixelShaderConstantF(206, &c[0][0], 4);
			TheRenderManager->device->SetPixelShaderConstantF(215, &c[4][0], 3);
			memcpy(Uploaded, c, sizeof(c));
			UploadedValid = true;
		}
	}

	inline IDirect3DBaseTexture9* Stage15Texture() { return Result ? Result : RtCoreStandIn::Neutral2D(); }

}
