#pragma once

namespace HiddenObjectCulling {

	enum { Slots = 8 };
	struct State {
		IDirect3DQuery9* query[Slots]; UInt32 issuedFrame[Slots]; bool writesDepth[Slots];
		UInt32 frame, lastSeen;
		UInt16 passes, passesLast;
		bool hidden;
		const void* parent; float pos[3], scale;
	};

	inline bool Enabled = false;
	inline bool LampPass = false;
	inline bool Active = false;
	inline bool WorldSeen = false;
	inline int MaxAge = 4;
	inline UInt32 FrameNo = 0;
	inline DWORD Thread = 0;
	inline IDirect3DDevice9* Device = nullptr;
	inline std::unordered_map<const void*, State> States;
	inline State* Pending = nullptr;
	inline int PendingSlot = 0;

	inline const double kMaxDegreesPerFrame = 2.5, kMaxUnitsPerFrame = 32.0;
	inline float CamRot[Slots][9] = {}, CamPos[Slots][3] = {};
	inline bool CamHave[Slots] = {};
	inline bool Stale[Slots] = {};

	inline unsigned StatFrames = 0;
	inline double StatObjects = 0, StatMulti = 0, StatHidden = 0, StatSkipped = 0, StatDrawnLater = 0, StatNotReady = 0,
		StatStaleDropped = 0, StatQueryFails = 0, StatQueries = 0, StatStaleFrames = 0, StatKept = 0, StatReused = 0;
	inline double StatAge[Slots] = {};

	inline void OnBind(const NiD3DPixelShaderEx* shader) {
		LampPass = false;
		if (!Active || !shader || !shader->Name) return;
		const char* name = shader->Name;
		if (strncmp(name, "SLS20", 5) || strlen(name) != 11 || strcmp(name + 7, ".pso")) return;
		const int number = (name[5] - '0') * 10 + (name[6] - '0');
		LampPass = number == 45 || number == 46 || (number >= 51 && number <= 56);
	}

	inline void ReleaseAll() {
		for (auto& entry : States) for (IDirect3DQuery9* q : entry.second.query) if (q) q->Release();
		States.clear();
		Pending = nullptr;
	}

	inline bool ReadCamera(float* rot, float* pos) {
		__try {
			typedef UInt8* (__cdecl* GetCameraFn)();
			UInt8* camera = ((GetCameraFn)0x00524C90)();
			if (!camera) return false;
			memcpy(rot, camera + 0x68, sizeof(float) * 9);
			memcpy(pos, camera + 0x8C, sizeof(float) * 3);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	inline void UpdateCameraGuard() {
		const int cur = FrameNo % Slots;
		CamHave[cur] = ReadCamera(CamRot[cur], CamPos[cur]);
		Stale[0] = false;
		for (UInt32 age = 1; age < Slots; age++) {
			const int old = (FrameNo - age) % Slots;
			bool stale = !CamHave[cur] || !CamHave[old];
			if (!stale) {
				double dot = 0, dist = 0;
				for (int i = 0; i < 9; i++) dot += (double)CamRot[cur][i] * CamRot[old][i];
				for (int i = 0; i < 3; i++) { const double d = CamPos[cur][i] - CamPos[old][i]; dist += d * d; }
				const double cosAngle = (std::max)(-1.0, (std::min)(1.0, (dot - 1.0) / 2.0));
				const double degrees = acos(cosAngle) * 57.29577951308232;
				stale = degrees > kMaxDegreesPerFrame * age || sqrt(dist) > kMaxUnitsPerFrame * age;
			}
			Stale[age] = stale;
		}
		if (Stale[1] && Stale[2]) StatStaleFrames += 1;
	}

	inline void BeginFrame(IDirect3DDevice9* device) {
		Thread = GetCurrentThreadId();
		Device = device;
		WorldSeen = false;
		Active = false;
		LampPass = false;
		Pending = nullptr;
		const bool enabled = TheSettingManager->SettingsMain.Main.HiddenObjectCulling;
		MaxAge = TheSettingManager->SettingsMain.Main.HiddenObjectCullingMaxAge;
		if (!enabled && Enabled) ReleaseAll();
		if (enabled && !Enabled) Logger::Log("UNOFFICIAL HiddenObjectCulling on: extra passes of objects hidden a few frames ago are skipped.");
		Enabled = enabled;
		FrameNo++;
		if (FrameNo == 0) FrameNo = 1;
		if (Enabled) {
			UpdateCameraGuard();
			if ((FrameNo & 1023) == 0) {
				for (auto it = States.begin(); it != States.end();) {
					if (FrameNo - it->second.lastSeen > 1200) {
						for (IDirect3DQuery9* q : it->second.query) if (q) q->Release();
						it = States.erase(it);
					}
					else ++it;
				}
			}
		}
		if (GpuTimer::Enabled && Enabled && ++StatFrames >= 240) {
			const double f = StatFrames;
			char ages[160] = {};
			size_t used = 0;
			for (int age = 1; age < Slots; age++) {
				const int w = _snprintf_s(ages + used, sizeof(ages) - used, _TRUNCATE, "%s%d: %.0f", age > 1 ? ", " : "", age, StatAge[age] / f);
				if (w < 0) break;
				used += w;
			}
			Logger::Log("HIDDEN OBJECT CULLING: objects %.0f/frame, with later passes (queried) %.0f, hidden %.0f (%.0f%% of queried), lamp passes skipped %.0f/frame, later passes drawn %.0f/frame (of hidden objects, not lamp passes: %.0f) | answers used by age in frames (max %d) %s | none in yet %.0f, dropped (camera moved) %.0f /frame | frames with camera moving fast %.0f%% | new object at a reused address %.0f /frame | objects tracked %u%s",
				StatObjects / f, StatMulti / f, StatHidden / f, StatMulti ? 100.0 * StatHidden / StatMulti : 0.0, StatSkipped / f, StatDrawnLater / f, StatKept / f,
				MaxAge, ages, StatNotReady / f, StatStaleDropped / f, 100.0 * StatStaleFrames / f, StatReused / f, (unsigned)States.size(),
				StatQueryFails ? " | query creation failed" : "");
			StatFrames = 0;
			StatObjects = StatMulti = StatHidden = StatSkipped = StatDrawnLater = StatNotReady = StatStaleDropped = StatQueryFails = StatQueries = StatStaleFrames = StatKept = StatReused = 0;
			for (double& a : StatAge) a = 0;
		}
	}

	inline void BeginWorld() {
		Active = Enabled && !WorldSeen && Device && GetCurrentThreadId() == Thread;
		WorldSeen = true;
	}
	inline void EndWorld() { Active = false; LampPass = false; Pending = nullptr; }

	inline bool Before(const void* pass, bool world) {
		Pending = nullptr;
		const bool lampPass = LampPass;
		LampPass = false;
		if (!Active || !world || !pass) return false;
		const UInt8* geometry = *(const UInt8* const*)pass;
		if (!geometry) return false;
		State& s = States[geometry];
		s.lastSeen = FrameNo;
		if (s.frame == FrameNo) {
			if (s.passes < 0xFFFF) s.passes++;
			if (s.hidden && lampPass) { if (GpuTimer::Enabled) StatSkipped += 1; return true; }
			if (GpuTimer::Enabled) { StatDrawnLater += 1; if (s.hidden) StatKept += 1; }
			return false;
		}
		const void* parent = *(const void* const*)(geometry + 0x18);
		const float* pos = (const float*)(geometry + 0x8C);
		const float scale = *(const float*)(geometry + 0x98);
		bool firstTime = s.frame == 0;
		if (!firstTime && (s.parent != parent || s.pos[0] != pos[0] || s.pos[1] != pos[1] || s.pos[2] != pos[2] || s.scale != scale)) {
			for (UInt32& issued : s.issuedFrame) issued = 0;
			firstTime = true;
			if (GpuTimer::Enabled) StatReused += 1;
		}
		s.parent = parent;
		s.pos[0] = pos[0]; s.pos[1] = pos[1]; s.pos[2] = pos[2];
		s.scale = scale;
		if (!firstTime) s.passesLast = s.passes;
		s.frame = FrameNo;
		s.passes = 1;
		s.hidden = false;
		if (GpuTimer::Enabled) StatObjects += 1;
		if (!firstTime && s.passesLast <= 1) return false;
		if (GpuTimer::Enabled) StatMulti += 1;
		const int cur = FrameNo % Slots;
		bool answered = false;
		for (UInt32 age = 1; age <= (UInt32)MaxAge && age < Slots && !answered; age++) {
			const int slot = (FrameNo - age) % Slots;
			if (!s.query[slot] || s.issuedFrame[slot] != FrameNo - age) continue;
			if (Stale[age]) { if (GpuTimer::Enabled) StatStaleDropped += 1; break; }
			DWORD pixels = 1;
			if (s.query[slot]->GetData(&pixels, sizeof(pixels), 0) != S_OK) continue;
			answered = true;
			s.hidden = s.writesDepth[slot] && pixels == 0;
			if (GpuTimer::Enabled) StatAge[age] += 1;
		}
		if (GpuTimer::Enabled) {
			if (!answered) StatNotReady += 1;
			if (s.hidden) StatHidden += 1;
		}
		if (!s.query[cur] && FAILED(Device->CreateQuery(D3DQUERYTYPE_OCCLUSION, &s.query[cur]))) { s.query[cur] = nullptr; StatQueryFails += 1; }
		if (!s.query[cur]) return false;
		s.query[cur]->Issue(D3DISSUE_BEGIN);
		Pending = &s;
		PendingSlot = cur;
		return false;
	}

	inline void After() {
		State* s = Pending;
		if (!s) return;
		Pending = nullptr;
		s->query[PendingSlot]->Issue(D3DISSUE_END);
		s->issuedFrame[PendingSlot] = FrameNo;
		if (GpuTimer::Enabled) StatQueries += 1;
		DWORD zwrite = 0;
		Device->GetRenderState(D3DRS_ZWRITEENABLE, &zwrite);
		s->writesDepth[PendingSlot] = zwrite != 0;
	}

	inline void Abandon() {
		State* s = Pending;
		if (!s) return;
		Pending = nullptr;
		s->query[PendingSlot]->Issue(D3DISSUE_END);
		s->issuedFrame[PendingSlot] = 0;
	}
}
