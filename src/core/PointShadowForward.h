#pragma once

#include "PointShadowSchedule.h"

// UNOFFICIAL interior forward point-light shadows (NVR-Lunacy\docs\interior-forward-shadows.md).
//
// Indoors the screen-space pass (Effects\PointShadows.fx + ShadowsInteriors.fx) could only darken the finished image,
// one mask for every light, lamp glow and ambient included. With [Shaders.ShadowsInteriors.Forward] Enabled, the lit
// pixel shaders (ObjectTemplate, SKIN2002/2006, SkinVPSTemplate, SM3002/3003/3005/3007) get an Interior record compiled
// with INTERIOR_SHADOWS 1 (Shaders\Includes\PointShadowForward.hlsl), and each point light's own direct light is
// multiplied by its own shadow cube map inside the object shader:
//  - ShadowManager::RenderShadowMaps: Begin() before anything, Publish() after the cube-map loop: the lights whose cube
//    map really is theirs this frame (drawn or kept), with the position and radius it was drawn for.
//  - SetShadersHook (Render.cpp): SetForDraw() matches the game's light list of the draw (BSShaderManager::
//    pCurrentRenderPass 0x011F91E0: +0x09 count, +0x0C ShadowSceneLight*, light k = PSLightColor[k]) to those lights by
//    pointer, writes c174-c187 and binds the cube maps to s8, s11-s15.
//  - BSBatchRenderer::RenderPassImmediately_Standard/_Skinned (Render.cpp): OnRenderPass() does the same for every
//    object. The game binds shaders only when the pass type or shader changes and then draws a run of objects with
//    them, each with its own lamps; with SetForDraw alone the rest of a run kept the first object's (flicker).
//  - ShaderManager::RenderEffectsPreTonemapping: while this runs, the old screen-space shadows are not applied again;
//    KeepDarkening keeps their darkening of what no tracked lamp reaches (the mask without its cube-map lookups, one
//    apply pass, no blur unless DarkeningBlur), otherwise neither the mask nor the apply runs.
// Exteriors, and every shader while Enabled is off, are exactly as before (no Interior records are made).
namespace PointShadowForward {

	// BSShaderProperty::RenderPass, from the game's code: constructor 0x00BA8EC0, SetLights 0x00BA8C50,
	// ShadowLightShader::SetupGeometryConstants_Lights 0x00B78A90 (reads +0x09 and +0x0C),
	// BSShaderLightingProperty::SetLight1x2x 0x00B70820 (light k -> kShaderLightConstants[k + 1] = PSLightColor[k]).
	struct RenderPassView {
		NiGeometry*			geometry;	// 00
		UInt16				passEnum;	// 04
		UInt8				unk06;		// 06
		UInt8				unk07;		// 07
		UInt8				unk08;		// 08
		UInt8				numLights;	// 09
		UInt8				maxLights;	// 0A
		UInt8				unk0B;		// 0B
		ShadowSceneLight**	lights;		// 0C  light k = PSLightColor[k]
	};
	static_assert(offsetof(RenderPassView, numLights) == 0x09 && offsetof(RenderPassView, lights) == 0x0C, "RenderPass layout");

	struct Slot { const ShadowSceneLight* light; D3DXVECTOR3 position; float radius; IDirect3DCubeTexture9* cube; int slot; float fade; };

	inline bool CompiledIn = false;			// an Interior record was compiled with INTERIOR_SHADOWS
	inline int CompiledCount = 0;
	inline bool Active = false;				// this frame: indoors, Strength > 0, at least one cube map to use
	inline bool FirstPerson = true;			// [Shaders.ShadowsInteriors.Forward] FirstPerson, read per frame
	inline Slot Slots[ShadowCubeMapsMax] = {};
	inline int SlotCount = 0;
	inline float Params[4] = {};				// c180: strength, bias, normal offset, PCF spread
	inline float DebugView = 0.0f;			// c181.x
	inline IDirect3DCubeTexture9* Neutral = nullptr;	// 1x1 cube holding 1 ("nothing drawn") for unmatched stages
	inline const DWORD Stage[6] = { 8, 11, 12, 13, 14, 15 };	// NVR_PointShadowCube0-5
	inline unsigned StatFrames = 0, StatDraws = 0, StatMatchedDraws = 0, StatMatchedLights = 0;

	// B42 Optics (PIP scopes, through JIP's ProjectExtraCamera): the scope's picture is the world drawn from another
	// camera into a small square texture (Data\config\B42Optics\Settings.ini RESOLUTION, 512), shown on lens shapes
	// (PRJ, SHD, VGN, ...) under the "B42LensRoot"/"B42Lens*" nodes of its lens meshes. The shaders rebuild positions
	// with the main camera's matrices (GetShadowWorldPos), so lamp shadows land wrong in both: with ScopeFix neither
	// gets them. The log says when either case was seen.
	inline bool ScopeFix = true;			// [Shaders.ShadowsInteriors.Forward] ScopeFix, read per frame (Publish)
	inline bool ScopeInView = false, ScopeWasInView = false;	// a B42 lens shape was drawn this frame / last frame
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

	// The pixel shaders that read c174-c187 when compiled with INTERIOR_SHADOWS (the files PointShadowForward.hlsl is
	// included by).
	inline bool Eligible(const char* name, const ShaderTemplate& t) {
		if (!name || !strstr(name, ".pso")) return false;
		if (t.Name) return !strcmp(t.Name, "ObjectTemplate") || !strcmp(t.Name, "SkinVPSTemplate");
		return !strcmp(name, "SKIN2002.pso") || !strcmp(name, "SKIN2006.pso") || !strcmp(name, "SM3002.pso") ||
			!strcmp(name, "SM3003.pso") || !strcmp(name, "SM3005.pso") || !strcmp(name, "SM3007.pso");
	}

	inline bool BoundThisFrame = false;		// a forward-shadow shader was bound this frame (the PBR shaders are drawing)
	inline unsigned Frame = 0;				// counts frames (Begin), for ScopeFix outdoors (Render.cpp)
	inline void Begin() {
		Frame++;
		Active = false; SlotCount = 0; BoundThisFrame = false;
		ScopeFix = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Forward", "ScopeFix") != 0;
	}

	// After the cube-map loop, indoors. A slot counts only if its cube map is that light's: slots[i] holds what the cube
	// was last drawn for (PointShadowSchedule.h redraws at once on a new light, position, radius or cell).
	inline void Publish(const PointShadowSlotState* slots, ShadowSceneLight* const* lights, int count, IDirect3DCubeTexture9* const* cubes) {
		// FadeIn: when each published lamp got its shadow (kept while it stays published frame after frame).
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
				// LogLamps: a lamp with a slot whose cube map is not its own this frame gets no shadow.
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
			// The radius the cube map stores distances over (256 for carried lights), as RenderShadowCubeMap drew it.
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

	// Linear like the screen-space pass (PointShadows.fx). Through the game's render state cache, as ShaderRecord::SetCT binds
	// textures: the game then knows the stage changed
	// and rebinds its own texture there when a later shader needs one.
	inline void Bind(DWORD stage, IDirect3DBaseTexture9* texture) {
		TheRenderManager->renderState->SetTexture(stage, texture);
		static const DWORD states[][2] = { { D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP }, { D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP },
			{ D3DSAMP_ADDRESSW, D3DTADDRESS_CLAMP }, { D3DSAMP_MAGFILTER, D3DTEXF_LINEAR }, { D3DSAMP_MINFILTER, D3DTEXF_LINEAR },
			{ D3DSAMP_MIPFILTER, D3DTEXF_NONE }, { D3DSAMP_SRGBTEXTURE, FALSE } };
		for (const auto& s : states)
			if (TheRenderManager->renderState->GetSamplerState(stage, (D3DSAMPLERSTATETYPE)s[0]) != s[1])
				TheRenderManager->SetSamplerState(stage, (D3DSAMPLERSTATETYPE)s[0], s[1]);
	}

	inline bool Bound = false;				// the pixel shader the game bound last is one of the records that read c174-c187
	inline float Uploaded[14][4] = {};		// c174-c187 as last written

	// One object's render pass: its lamps to c174-c187 and the cube stages. wanted: the world pass, or the first-person
	// pass with FirstPerson on (never the water reflection). force: just after a shader bind, write everything.
	inline void SetForPass(const RenderPassView* pass, bool world, bool firstPerson, bool force) {
		float constants[14][4] = {};// c174-c179 per light of the draw; c180 params (x 0: the shader skips it all);
										// c181 debug view; c182-c187 per light of the draw: rgb debug colour (white = no shadow), w how far its shadow has faded in (FadeIn)
		for (int k = 0; k < 6; k++) constants[8 + k][0] = constants[8 + k][1] = constants[8 + k][2] = 1.0f;
		IDirect3DBaseTexture9* bound[6];
		IDirect3DCubeTexture9* neutral = NeutralCube();
		for (int k = 0; k < 6; k++) bound[k] = neutral;
		int matched = 0;
		if (Active && (world || (firstPerson && FirstPerson)) && pass && pass->lights && !OtherCamera(pass, firstPerson)) {
			const D3DXVECTOR4& camera = TheRenderManager->CameraPosition;	// GetShadowWorldPos's origin
			const int count = (std::min)((int)pass->numLights, 6);
			for (int k = 0; k < count; k++) {
				const ShadowSceneLight* light = pass->lights[k];
				if (!light || !light->bPointLight) continue;	// light 0 of most passes is the cell's directional light
				for (int s = 0; s < SlotCount; s++) {
					if (Slots[s].light != light) continue;
					constants[k][0] = Slots[s].position.x - camera.x;
					constants[k][1] = Slots[s].position.y - camera.y;
					constants[k][2] = Slots[s].position.z - camera.z;
					constants[k][3] = 1.0f / Slots[s].radius;
					bound[k] = Slots[s].cube;
					static const float palette[11][3] = { { 1, 0.15f, 0.1f }, { 0.1f, 1, 0.15f }, { 0.15f, 0.35f, 1 },
						{ 1, 0.9f, 0.1f }, { 0.1f, 1, 1 }, { 1, 0.2f, 1 }, { 1, 0.55f, 0.1f }, { 0.55f, 0.25f, 1 },
						{ 0.6f, 1, 0.3f }, { 1, 0.45f, 0.6f }, { 0.4f, 0.8f, 1 } };	// by cube slot
					memcpy(constants[8 + k], palette[Slots[s].slot % 11], 3 * sizeof(float));
					constants[8 + k][3] = Slots[s].fade;	// FadeIn
					matched++;
					break;
				}
			}
		}
		if (matched) memcpy(constants[6], Params, sizeof(Params));
		constants[7][0] = matched ? DebugView : 0.0f;
		// Every declared cube stage gets a cube texture, matched or not: a 2D texture left there by another shader would be
		// the wrong type for the sampler. Between objects only what changed (the game's texture cache tells).
		for (int k = 0; k < 6; k++)
			if (force || TheRenderManager->renderState->GetTexture(Stage[k]) != bound[k]) Bind(Stage[k], bound[k]);
		if (force || memcmp(constants, Uploaded, sizeof(constants))) {
			TheRenderManager->device->SetPixelShaderConstantF(174, &constants[0][0], 14);
			memcpy(Uploaded, constants, sizeof(constants));
		}
		if (GpuTimer::Enabled && !force) { StatDraws++; if (matched) { StatMatchedDraws++; StatMatchedLights += matched; } }
	}

	// SetShadersHook, after the game bound a pass's shaders and set its constants.
	inline void SetForDraw(NiD3DPixelShaderEx* pixelShader, bool world, bool firstPerson) {
		Bound = false;
		if (!CompiledIn || !pixelShader) return;
		ShaderRecordPixel* interiorRecord = pixelShader->GetShaderRecord(ShaderRecordType::Interior);
		if (!interiorRecord || !interiorRecord->PointShadowForward || pixelShader->ShaderHandle != interiorRecord->ShaderHandle)
			return;	// only those records read c174-c187
		Bound = true;
		BoundThisFrame = true;
		SetForPass(*(RenderPassView**)0x011F91E0, world, firstPerson, true);
	}

	// BSBatchRenderer::RenderPassImmediately_Standard/_Skinned: once per object, after the bind (if any) for its run.
	inline void OnRenderPass(const RenderPassView* pass, bool world, bool firstPerson) {
		if (Bound) SetForPass(pass, world, firstPerson, false);
	}

	// Once a frame (RenderShadowMaps), with the F10 profile: how much work it did.
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
