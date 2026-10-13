#pragma once

// UNOFFICIAL LampScissor ([Main.Main.Performance] LampScissor, 2026-10-11).
//
// Indoors the game adds the light of extra lamps to an object in passes of their own (ObjectTemplate DIFFUSE, PS
// SLS2045/2046, and POINT specular, SLS2051-2056): the whole object is drawn again, shaded and blended for every pixel,
// although a lamp only reaches pixels within its radius (vanillaAttSq is exactly 0 beyond it). In the Rivet City hangar
// these passes cost ~2.5-3 ms of the 7 ms world scene (P47), about half of their draws passing 0 pixels.
// Here such a draw is limited to the screen rectangle that can hold its lamps' spheres (scissor test), so pixels no lamp
// can reach are not shaded at all. Exact: the rectangle is the projection of each sphere's bounding box (so it contains
// the sphere), widened by a margin; a lamp whose box reaches behind the near plane, or anything unusual (not a point
// light, no radius, another camera's viewport, an orthographic camera) leaves the draw unscissored. A draw none of whose
// lamps is on screen at all is skipped (it adds nothing visible).
// Reach: ShadowLightShader::SetupGeometryConstants_Lights 0x00B78A90 puts the lamp into the object's model space and
// divides the radius by the object's scale (or scales the position instead for passes 0x231-0x233), so in world space
// a lamp reaches exactly NiPointLight::Spec.r (+0xE0, BSShaderLightingProperty::SetLight1x2x 0x00B70820) from its world
// position (+0x8C). ShadowSceneLight +0xF4 = point light, +0xF8 = the NiPointLight.
// Only the world scene, only the Standard (not skinned) draw, only NVR's own versions of those pixel shaders.
namespace LampScissor {

	inline bool Enabled = false;		// the setting, read each frame
	inline bool LampOnlyPass = false;	// the bound pixel shader is one of NVR's lamp-only passes (SetShadersHook)
	inline bool Scissored = false;		// Before() turned the scissor test on; After() restores
	inline DWORD SavedEnable = FALSE;
	inline RECT SavedRect = {};
	inline unsigned StatFrames = 0;
	inline double StatDraws = 0, StatScissored = 0, StatSkipped = 0, StatFull = 0, StatAreaShare = 0;

	// SetShadersHook, after the game bound the pass's shaders.
	inline void OnBind(const NiD3DPixelShaderEx* shader, bool world) {
		LampOnlyPass = false;
		if (!Enabled || !world || !shader || !shader->Name || !shader->ShaderHandleBackup ||
			shader->ShaderHandle == (IDirect3DPixelShader9*)shader->ShaderHandleBackup) return;
		const char* name = shader->Name;
		if (strncmp(name, "SLS20", 5) || strlen(name) != 11 || strcmp(name + 7, ".pso")) return;
		const int number = (name[5] - '0') * 10 + (name[6] - '0');
		LampOnlyPass = number == 45 || number == 46 || (number >= 51 && number <= 56);
	}

	inline void BeginFrame() {
		Enabled = TheSettingManager->SettingsMain.Main.LampScissor;
		LampOnlyPass = false;
		Scissored = false;
		if (GpuTimer::Enabled && ++StatFrames >= 240) {
			const double f = StatFrames;
			if (StatDraws > 0)
				Logger::Log("LAMP SCISSOR %s: lamp-only draws %.0f/frame, scissored %.0f (average %.0f%% of the screen), skipped (no lamp on screen) %.0f, full screen (lamp behind the camera or unusual) %.0f",
					Enabled ? "on" : "off", StatDraws / f, StatScissored / f, StatScissored > 0 ? 100.0 * StatAreaShare / StatScissored : 0.0,
					StatSkipped / f, StatFull / f);
			StatFrames = 0;
			StatDraws = StatScissored = StatSkipped = StatFull = StatAreaShare = 0;
		}
	}

	enum Result { Draw, Skip };

	// Before an object's draw (RenderPassStandardHook). Skip: nothing of it can be lit; otherwise drawn, maybe scissored.
	inline Result Before(const PointShadowForward::RenderPassView* pass, bool world) {
		Scissored = false;
		if (!Enabled || !LampOnlyPass || !world || !pass || !pass->numLights || !pass->lights) return Draw;
		if (GpuTimer::Enabled) StatDraws += 1;
		IDirect3DDevice9* device = TheRenderManager->device;
		D3DVIEWPORT9 port;
		NiCamera* camera = WorldSceneGraph ? WorldSceneGraph->camera : nullptr;
		if (!camera || camera->Frustum.Ortho || FAILED(device->GetViewport(&port)) || port.X || port.Y ||
			port.Width != TheRenderManager->width || port.Height != TheRenderManager->height) {
			if (GpuTimer::Enabled) StatFull += 1;
			return Draw;
		}
		const NiMatrix33& rot = camera->m_worldTransform.rot;
		const NiPoint3& eye = camera->m_worldTransform.pos;
		const NiFrustum& frustum = camera->Frustum;
		const float frustumWidth = frustum.Right - frustum.Left, frustumHeight = frustum.Top - frustum.Bottom;
		if (frustumWidth <= 0.0f || frustumHeight <= 0.0f || frustum.Near <= 0.0f) { if (GpuTimer::Enabled) StatFull += 1; return Draw; }
		// View basis as RenderManager::UpdateSceneCameraData builds it: forward, up, right = columns 0, 1, 2.
		const float forward[3] = { rot.data[0][0], rot.data[1][0], rot.data[2][0] };
		const float up[3] = { rot.data[0][1], rot.data[1][1], rot.data[2][1] };
		const float right[3] = { rot.data[0][2], rot.data[1][2], rot.data[2][2] };
		float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
		bool anyOnScreen = false;
		for (UInt8 k = 0; k < pass->numLights; k++) {
			const UInt8* light = (const UInt8*)pass->lights[k];
			if (!light || !*(const bool*)(light + 0xF4)) { if (GpuTimer::Enabled) StatFull += 1; return Draw; }
			const NiPointLight* source = *(NiPointLight* const*)(light + 0xF8);
			if (!source) { if (GpuTimer::Enabled) StatFull += 1; return Draw; }
			const float radius = source->Spec.r;
			if (!(radius > 0.0f) || radius > 1e6f) { if (GpuTimer::Enabled) StatFull += 1; return Draw; }
			const float reach = radius * 1.02f + 2.0f;
			const NiPoint3& centre = source->m_worldTransform.pos;
			float lampMinX = 1e30f, lampMinY = 1e30f, lampMaxX = -1e30f, lampMaxY = -1e30f;
			for (int corner = 0; corner < 8; corner++) {
				const float p[3] = { centre.x + ((corner & 1) ? reach : -reach) - eye.x, centre.y + ((corner & 2) ? reach : -reach) - eye.y,
					centre.z + ((corner & 4) ? reach : -reach) - eye.z };
				const float z = p[0] * forward[0] + p[1] * forward[1] + p[2] * forward[2];
				if (z < frustum.Near * 0.5f) { if (GpuTimer::Enabled) StatFull += 1; return Draw; }	// box reaches behind the camera
				const float x = p[0] * right[0] + p[1] * right[1] + p[2] * right[2];
				const float y = p[0] * up[0] + p[1] * up[1] + p[2] * up[2];
				const float ndcX = (2.0f * x / z - (frustum.Right + frustum.Left)) / frustumWidth;
				const float ndcY = (2.0f * y / z - (frustum.Top + frustum.Bottom)) / frustumHeight;
				const float sx = (ndcX + 1.0f) * 0.5f * port.Width, sy = (1.0f - ndcY) * 0.5f * port.Height;
				lampMinX = (std::min)(lampMinX, sx); lampMaxX = (std::max)(lampMaxX, sx);
				lampMinY = (std::min)(lampMinY, sy); lampMaxY = (std::max)(lampMaxY, sy);
			}
			if (lampMaxX < 0.0f || lampMaxY < 0.0f || lampMinX > (float)port.Width || lampMinY > (float)port.Height) continue;	// off screen
			anyOnScreen = true;
			minX = (std::min)(minX, lampMinX); maxX = (std::max)(maxX, lampMaxX);
			minY = (std::min)(minY, lampMinY); maxY = (std::max)(maxY, lampMaxY);
		}
		if (!anyOnScreen) { if (GpuTimer::Enabled) StatSkipped += 1; return Skip; }
		RECT rect;
		rect.left = (LONG)(std::max)(0.0f, floorf(minX) - 1.0f);
		rect.top = (LONG)(std::max)(0.0f, floorf(minY) - 1.0f);
		rect.right = (LONG)(std::min)((float)port.Width, ceilf(maxX) + 1.0f);
		rect.bottom = (LONG)(std::min)((float)port.Height, ceilf(maxY) + 1.0f);
		if (rect.right <= rect.left || rect.bottom <= rect.top) { if (GpuTimer::Enabled) StatSkipped += 1; return Skip; }
		if (rect.left == 0 && rect.top == 0 && rect.right == (LONG)port.Width && rect.bottom == (LONG)port.Height) return Draw;	// whole screen anyway
		if (FAILED(device->GetRenderState(D3DRS_SCISSORTESTENABLE, &SavedEnable)) || FAILED(device->GetScissorRect(&SavedRect))) return Draw;
		device->SetScissorRect(&rect);
		device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
		Scissored = true;
		if (GpuTimer::Enabled) {
			StatScissored += 1;
			StatAreaShare += (double)(rect.right - rect.left) * (rect.bottom - rect.top) / ((double)port.Width * port.Height);
		}
		return Draw;
	}

	// After the draw: the scissor state as it was (the game's render-state cache never saw the change).
	inline void After() {
		if (!Scissored) return;
		Scissored = false;
		IDirect3DDevice9* device = TheRenderManager->device;
		device->SetScissorRect(&SavedRect);
		device->SetRenderState(D3DRS_SCISSORTESTENABLE, SavedEnable);
	}
}
