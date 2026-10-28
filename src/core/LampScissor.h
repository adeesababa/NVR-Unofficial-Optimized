#pragma once

namespace LampScissor {

	inline bool Enabled = false;
	inline bool Exact = true;
	inline bool LampOnlyPass = false;
	inline bool Scissored = false;
	inline DWORD SavedEnable = FALSE;
	inline RECT SavedRect = {};
	inline unsigned StatFrames = 0;
	inline int StatMode = -1;
	inline double StatDraws = 0, StatScissored = 0, StatSkipped = 0, StatAreaShare = 0, StatShaded = 0;
	inline double StatBehind = 0, StatUnusual = 0, StatInside = 0, StatCovers = 0;
	enum Unusual { OwnColour, CameraOff, NoLight, NotPoint, NoSource, BadRadius, StateFailed, UnusualKinds };
	inline double StatWhy[UnusualKinds] = {};
	inline double StatEmpty = 0;
	inline const char* BoundName = "";
	inline unsigned UnusualLogged = 0;

	inline void OnBind(const NiD3DPixelShaderEx* shader, bool world) {
		LampOnlyPass = false;
		if (!Enabled || !world || !shader || !shader->Name || !shader->ShaderHandleBackup ||
			shader->ShaderHandle == (IDirect3DPixelShader9*)shader->ShaderHandleBackup) return;
		const char* name = shader->Name;
		if (strncmp(name, "SLS20", 5) || strlen(name) != 11 || strcmp(name + 7, ".pso")) return;
		const int number = (name[5] - '0') * 10 + (name[6] - '0');
		LampOnlyPass = number == 45 || number == 46 || (number >= 51 && number <= 56);
		if (LampOnlyPass) BoundName = name;
	}

	inline void LogUnusual(Unusual why, const PointShadowForward::RenderPassView* pass, int k) {
		static const char* names[UnusualKinds] = { "own colour (barrel glow)", "camera or viewport", "no light", "not a point light", "no source light", "radius", "scissor state" };
		if (UnusualLogged >= 40) return;
		static std::vector<unsigned> done;
		const unsigned key = ((unsigned)why << 16) | pass->passEnum | ((k > 0 ? 1u : 0u) << 24);
		for (unsigned d : done) if (d == key) return;
		done.push_back(key);
		UnusualLogged++;
		const UInt8* light = k >= 0 ? (const UInt8*)pass->lights[k] : nullptr;
		const NiPointLight* source = light ? *(NiPointLight* const*)(light + 0xF8) : nullptr;
		const NiGeometry* geometry = pass->geometry;
		Logger::Log("LAMP SCISSOR unusual: %s, pass 0x%03X (%s, %u lamps of max %u), lamp %d: point %d ambient %d dynamic %d, source %p radius %.2f dimmer %.2f at (%.0f %.0f %.0f), object '%s'",
			names[why], pass->passEnum, BoundName, pass->numLights, pass->maxLights, k, light ? *(const bool*)(light + 0xF4) : -1, light ? *(const bool*)(light + 0xF5) : -1,
			light ? *(const bool*)(light + 0xFC) : -1, (const void*)source, source ? source->Spec.r : 0.0f, source ? source->Dimmer : 0.0f,
			source ? source->m_worldTransform.pos.x : 0.0f, source ? source->m_worldTransform.pos.y : 0.0f, source ? source->m_worldTransform.pos.z : 0.0f,
			geometry && geometry->m_pcName ? geometry->m_pcName : "(unnamed)");
	}

	inline void BeginFrame() {
		Enabled = TheSettingManager->SettingsMain.Main.LampScissor;
		Exact = TheSettingManager->SettingsMain.Main.LampScissorExact;
		LampOnlyPass = false;
		Scissored = false;
		if (!GpuTimer::Enabled) return;
		const int mode = Enabled ? (Exact ? 2 : 1) : 0;
		if (mode != StatMode || StatFrames >= 240) {
			const double f = StatFrames;
			if (f >= 30 && StatDraws > 0) {
				const double full = StatBehind + StatUnusual + StatInside + StatCovers;
				Logger::Log("LAMP SCISSOR %s: lamp-only draws %.0f/frame, scissored %.0f (average %.0f%% of the screen), skipped (no lamp on screen) %.0f, full screen %.0f (camera inside a lamp's reach %.0f, rectangle covers the screen %.0f, lamp behind the camera %.0f, unusual %.0f); screen open to these draws %.1f%% (full screen = 100) (%.0f frames)",
					StatMode == 2 ? "on (exact)" : "on (box)", StatDraws / f, StatScissored / f, StatScissored > 0 ? 100.0 * StatAreaShare / StatScissored : 0.0,
					StatSkipped / f, full / f, StatInside / f, StatCovers / f, StatBehind / f, StatUnusual / f, 100.0 * StatShaded / StatDraws, f);
				if (StatEmpty > 0) Logger::Log("LAMP SCISSOR empty lamp slots (ignored, black): in %.0f draws a frame", StatEmpty / f);
				if (StatUnusual > 0)
					Logger::Log("LAMP SCISSOR unusual by kind: own colour %.0f, camera or viewport %.0f, no light %.0f, not a point light %.0f, no source light %.0f, radius %.0f, scissor state %.0f",
						StatWhy[OwnColour] / f, StatWhy[CameraOff] / f, StatWhy[NoLight] / f, StatWhy[NotPoint] / f, StatWhy[NoSource] / f, StatWhy[BadRadius] / f, StatWhy[StateFailed] / f);
			}
			for (double& w : StatWhy) w = 0;
			StatEmpty = 0;
			StatFrames = 0;
			StatDraws = StatScissored = StatSkipped = StatAreaShare = StatShaded = 0;
			StatBehind = StatUnusual = StatInside = StatCovers = 0;
			StatMode = mode;
		}
		StatFrames++;
	}

	inline bool AxisRange(float a, float z, float r, float n, float& lo, float& hi) {
		if (z + r < n) return false;
		lo = 1e30f; hi = -1e30f;
		const bool cut = z - r < n;
		const float d2 = a * a + z * z, r2 = r * r;
		if (d2 > r2) {
			const float l2 = d2 - r2, l = sqrtf(l2);
			for (float side = -1.0f; side <= 1.0f; side += 2.0f) {
				const float ta = (l2 * a + side * r * l * z) / d2, tz = (l2 * z - side * r * l * a) / d2;
				if (tz > 0.0f && (!cut || tz >= n)) { lo = (std::min)(lo, ta / tz); hi = (std::max)(hi, ta / tz); }
			}
		}
		if (cut) {
			const float h = n - z, w = sqrtf((std::max)(0.0f, r2 - h * h));
			lo = (std::min)(lo, (a - w) / n); hi = (std::max)(hi, (a + w) / n);
		}
		return lo <= hi;
	}

	enum Result { Draw, Skip };

	inline Result Before(const PointShadowForward::RenderPassView* pass, bool world, bool ownColour) {
		Scissored = false;
		if (!Enabled || !LampOnlyPass || !world || !pass || !pass->numLights || !pass->lights) return Draw;
		const bool stats = GpuTimer::Enabled;
		if (stats) StatDraws += 1;
		auto unusual = [stats, pass](Unusual why, int k) { if (stats) { StatUnusual += 1; StatShaded += 1; StatWhy[why] += 1; LogUnusual(why, pass, k); } return Draw; };
		if (ownColour) return unusual(OwnColour, -1);
		IDirect3DDevice9* device = TheRenderManager->device;
		D3DVIEWPORT9 port;
		NiCamera* camera = WorldSceneGraph ? WorldSceneGraph->camera : nullptr;
		if (!camera || camera->Frustum.Ortho || FAILED(device->GetViewport(&port)) || port.X || port.Y ||
			!TAAEffect::IsWorldViewport(port)) return unusual(CameraOff, -1);
		const NiMatrix33& rot = camera->m_worldTransform.rot;
		const NiPoint3& eye = camera->m_worldTransform.pos;
		const NiFrustum& frustum = camera->Frustum;
		const float frustumWidth = frustum.Right - frustum.Left, frustumHeight = frustum.Top - frustum.Bottom;
		if (frustumWidth <= 0.0f || frustumHeight <= 0.0f || frustum.Near <= 0.0f) return unusual(CameraOff, -1);
		const float forward[3] = { rot.data[0][0], rot.data[1][0], rot.data[2][0] };
		const float up[3] = { rot.data[0][1], rot.data[1][1], rot.data[2][1] };
		const float right[3] = { rot.data[0][2], rot.data[1][2], rot.data[2][2] };
		auto screenX = [&](float t) { return ((2.0f * t - (frustum.Right + frustum.Left)) / frustumWidth + 1.0f) * 0.5f * port.Width; };
		auto screenY = [&](float t) { return (1.0f - (2.0f * t - (frustum.Top + frustum.Bottom)) / frustumHeight) * 0.5f * port.Height; };
		const float nearClip = frustum.Near * 0.5f;
		float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
		bool anyOnScreen = false, inside = false, emptySlots = false;
		for (UInt8 k = 0; k < pass->numLights; k++) {
			const UInt8* light = (const UInt8*)pass->lights[k];
			if (!light) {
				if (Exact) { emptySlots = true; continue; }
				return unusual(NoLight, k);
			}
			if (!*(const bool*)(light + 0xF4)) return unusual(NotPoint, k);
			const NiPointLight* source = *(NiPointLight* const*)(light + 0xF8);
			if (!source) return unusual(NoSource, k);
			const float radius = source->Spec.r;
			if (!(radius > 0.0f) || radius > 1e6f) return unusual(BadRadius, k);
			const float reach = radius * 1.02f + 2.0f;
			const NiPoint3& centre = source->m_worldTransform.pos;
			float lampMinX = 1e30f, lampMinY = 1e30f, lampMaxX = -1e30f, lampMaxY = -1e30f;
			if (Exact) {
				const float p[3] = { centre.x - eye.x, centre.y - eye.y, centre.z - eye.z };
				const float z = p[0] * forward[0] + p[1] * forward[1] + p[2] * forward[2];
				const float x = p[0] * right[0] + p[1] * right[1] + p[2] * right[2];
				const float y = p[0] * up[0] + p[1] * up[1] + p[2] * up[2];
				float xLo, xHi, yLo, yHi;
				if (!AxisRange(x, z, reach, nearClip, xLo, xHi) || !AxisRange(y, z, reach, nearClip, yLo, yHi)) continue;
				lampMinX = screenX(xLo); lampMaxX = screenX(xHi);
				lampMinY = screenY(yHi); lampMaxY = screenY(yLo);
				if (x * x + y * y + z * z <= reach * reach) inside = true;
			}
			else {
				for (int corner = 0; corner < 8; corner++) {
					const float p[3] = { centre.x + ((corner & 1) ? reach : -reach) - eye.x, centre.y + ((corner & 2) ? reach : -reach) - eye.y,
						centre.z + ((corner & 4) ? reach : -reach) - eye.z };
					const float z = p[0] * forward[0] + p[1] * forward[1] + p[2] * forward[2];
					if (z < frustum.Near * 0.5f) { if (stats) { StatBehind += 1; StatShaded += 1; } return Draw; }
					const float x = p[0] * right[0] + p[1] * right[1] + p[2] * right[2];
					const float y = p[0] * up[0] + p[1] * up[1] + p[2] * up[2];
					const float ndcX = (2.0f * x / z - (frustum.Right + frustum.Left)) / frustumWidth;
					const float ndcY = (2.0f * y / z - (frustum.Top + frustum.Bottom)) / frustumHeight;
					const float sx = (ndcX + 1.0f) * 0.5f * port.Width, sy = (1.0f - ndcY) * 0.5f * port.Height;
					lampMinX = (std::min)(lampMinX, sx); lampMaxX = (std::max)(lampMaxX, sx);
					lampMinY = (std::min)(lampMinY, sy); lampMaxY = (std::max)(lampMaxY, sy);
				}
			}
			if (lampMaxX < 0.0f || lampMaxY < 0.0f || lampMinX > (float)port.Width || lampMinY > (float)port.Height) continue;
			anyOnScreen = true;
			minX = (std::min)(minX, lampMinX); maxX = (std::max)(maxX, lampMaxX);
			minY = (std::min)(minY, lampMinY); maxY = (std::max)(maxY, lampMaxY);
		}
		if (stats && emptySlots) StatEmpty += 1;
		if (!anyOnScreen) { if (stats) StatSkipped += 1; return Skip; }
		RECT rect;
		rect.left = (LONG)(std::max)(0.0f, floorf(minX) - 1.0f);
		rect.top = (LONG)(std::max)(0.0f, floorf(minY) - 1.0f);
		rect.right = (LONG)(std::min)((float)port.Width, ceilf(maxX) + 1.0f);
		rect.bottom = (LONG)(std::min)((float)port.Height, ceilf(maxY) + 1.0f);
		if (rect.right <= rect.left || rect.bottom <= rect.top) { if (stats) StatSkipped += 1; return Skip; }
		if (rect.left == 0 && rect.top == 0 && rect.right == (LONG)port.Width && rect.bottom == (LONG)port.Height) {
			if (stats) { (inside ? StatInside : StatCovers) += 1; StatShaded += 1; }
			return Draw;
		}
		if (FAILED(device->GetRenderState(D3DRS_SCISSORTESTENABLE, &SavedEnable)) || FAILED(device->GetScissorRect(&SavedRect))) return unusual(StateFailed, -1);
		device->SetScissorRect(&rect);
		device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
		Scissored = true;
		if (stats) {
			const double share = (double)(rect.right - rect.left) * (rect.bottom - rect.top) / ((double)port.Width * port.Height);
			StatScissored += 1;
			StatAreaShare += share;
			StatShaded += share;
		}
		return Draw;
	}

	inline void After() {
		if (!Scissored) return;
		Scissored = false;
		IDirect3DDevice9* device = TheRenderManager->device;
		device->SetScissorRect(&SavedRect);
		device->SetRenderState(D3DRS_SCISSORTESTENABLE, SavedEnable);
	}
}
