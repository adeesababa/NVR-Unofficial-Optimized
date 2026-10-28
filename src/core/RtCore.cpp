// UNOFFICIAL 2026-10-28 (NVR DXVK): the ray tracing core (RtCore.h; docs\next\FORK-RT-CORE.md). The helper's side is
// tools\nvr-rt-helper (MIT, built on its own, 64-bit); the protocol is tools\nvr-rt-helper\NvrRtProtocol.h. The launch, the shared
// block, the watchdog and the stop follow UpscalerDlss.cpp (the DLSS helper), which proved them.

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#include "RtCore.h"
#include "DxvkCompute.h"
#include "GpuProfiler.h"
#include "../../tools/nvr-rt-helper/NvrRtProtocol.h"

namespace {
	const UINT64 kReleaseMargin = 1ull << 20;	// the watchdog / Stop release every wait that could still be recorded
	const UINT kArenaWidth = 4096;
	const UINT kReleaseAfterFrames = 120;		// a geometry not met for this many frames is released in the helper

	bool FileExists(const std::wstring& path) {
		const DWORD attributes = GetFileAttributesW(path.c_str());
		return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
	}

	std::wstring GameDir() {
		wchar_t path[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, path, MAX_PATH);
		std::wstring dir(path);
		const size_t slash = dir.find_last_of(L"\\/");
		return slash == std::wstring::npos ? L"." : dir.substr(0, slash);
	}

	std::string HrText(HRESULT hr) {
		char text[96];
		if (HRESULT_FACILITY(hr) == NVR_EXT_FACILITY_VK) sprintf_s(text, "VkResult %d", -(int)(hr & 0xFFFF));
		else if (HRESULT_FACILITY(hr) == FACILITY_WIN32) sprintf_s(text, "Win32 error %lu", (unsigned long)HRESULT_CODE(hr));
		else if (hr == D3DERR_NOTAVAILABLE) sprintf_s(text, "D3DERR_NOTAVAILABLE");
		else sprintf_s(text, "HRESULT 0x%08lX", (unsigned long)hr);
		return text;
	}

	// A geometry the game draws: its D3D9 buffers and draw ranges. Two NiGeometry objects that share the buffer data (the same
	// model placed twice) are one geometry with two instances.
	struct GeometryKey {
		const void*	buffData;
		const void*	vb;
		const void*	ib;
		UInt32		vertCount, triCount, baseVertex;
		bool operator==(const GeometryKey& o) const {
			return buffData == o.buffData && vb == o.vb && ib == o.ib && vertCount == o.vertCount && triCount == o.triCount && baseVertex == o.baseVertex;
		}
	};
	struct GeometryKeyHash {
		size_t operator()(const GeometryKey& k) const {
			size_t h = (size_t)k.buffData * 2654435761u;
			h ^= (size_t)k.vb + 0x9e3779b9 + (h << 6) + (h >> 2);
			h ^= (size_t)k.ib + 0x9e3779b9 + (h << 6) + (h >> 2);
			h ^= (size_t)(k.vertCount * 31u + k.triCount * 17u + k.baseVertex) + 0x9e3779b9 + (h << 6) + (h >> 2);
			return h;
		}
	};
	struct GeometryEntry {
		UInt32	id = 0;
		UInt32	lastSeen = 0;
		bool	sent = false;		// a BUILD op went to the helper
		bool	refused = false;	// the fork refused it (logged once): never again
	};

	// Where a vertex declaration keeps the position (cached per declaration).
	struct PositionElement { int stream = -1; UInt32 offset = 0; NVR_EXT_RT_POSITION_FORMAT format = NVR_EXT_RT_POSITION_FLOAT3; };

	struct PendingCopy { int image; IUnknown* target; };

	struct Core {
		enum State { Idle, Starting, Running, Off };
		State		state = Idle;
		std::vector<RtCore::User> users;
		ID3D9NvrExtShare*		share = nullptr;
		ID3D9NvrExtRt*			rt = nullptr;
		std::vector<ID3D9NvrExtSharedImage*> images;	// [0] = the arena
		std::vector<std::string> imageNames;
		std::vector<NVR_EXT_SHARED_IMAGE_DESC> imageDescs;
		bool		inSetup = false;
		ID3D9NvrExtSharedFence*	inputsReady = nullptr;
		ID3D9NvrExtSharedFence*	outputReady = nullptr;
		HANDLE		mapping = nullptr, frameEvent = nullptr, statusEvent = nullptr, process = nullptr, watchdog = nullptr;
		NvrRtShared* shared = nullptr;
		ULONGLONG	startTick = 0;
		UInt64		frame = 0;
		volatile LONG64	lastRecorded = 0;
		volatile LONG	watchStop = 0;
		volatile LONG	tripped = 0;
		char		tripReason[160] = {};
		UInt32		lastSerial = 0;
		UINT		width = 0, height = 0;		// the screen size the users' images were made for
		std::vector<std::string> shaders;		// names, in registration order (the helper's shader indices)
		char		status[200] = "not started";
		bool		loggedOff = false;

		// this frame
		NvrRtFrame*	record = nullptr;			// the ring slot being written (null outside Frame)
		bool		recording = false;
		std::vector<PendingCopy> copiesOut;

		// the scene
		std::unordered_map<GeometryKey, GeometryEntry, GeometryKeyHash> geometries;
		std::unordered_map<const void*, PositionElement> declarations;
		UInt32		nextId = 1;
		UInt32		sceneFrame = 0;
		UInt32		arenaUsed = 0, arenaCapacity = 0;
		UInt32		instances = 0, waiting = 0, released = 0;
		float		radius = 8192.0f;
		std::vector<NiAVObject*> stack;

		void		TurnOff(const char* why);
		bool		Start();
		void		Stop(const char* why);
		void		Poll();
		void		Gather();
		void		WalkCell(TESObjectCELL* cell);
		void		Walk(NiAVObject* root, bool land);
		void		AddGeometry(NiGeometry* geo, bool land);
		bool		Describe(NiGeometry* geo, NVR_EXT_RT_GEOMETRY_DESC& desc, std::vector<NVR_EXT_RT_RANGE>& ranges, GeometryKey& key);
		void		LogHelperMessage();
		static DWORD WINAPI WatchdogMain(void* self);
	};

	Core TheCore;

	void Core::TurnOff(const char* why) {
		Logger::Log("UNOFFICIAL NVR DXVK: ray tracing core off for this session: %s. The RT effects keep their usual paths.", why);
		_snprintf_s(status, sizeof(status), _TRUNCATE, "off: %s", why);
		Stop(nullptr);
		state = Off;
	}

	void Core::LogHelperMessage() {
		if (!shared || shared->messageSerial == lastSerial) return;
		lastSerial = shared->messageSerial;
		char message[sizeof(shared->message)];
		memcpy(message, (const void*)shared->message, sizeof(message));
		message[sizeof(message) - 1] = 0;
		Logger::Log("UNOFFICIAL NVR DXVK: RT helper: %s", message);
	}

	DWORD WINAPI Core::WatchdogMain(void* self) {
		Core* c = (Core*)self;
		UINT64 lastValue = 0;
		ULONGLONG lastProgress = GetTickCount64();
		const char* why = nullptr;
		while (!InterlockedCompareExchange(&c->watchStop, 0, 0)) {
			if (WaitForSingleObject(c->process, 100) == WAIT_OBJECT_0) { why = "the helper process exited (NvrRtHelper.log says why)"; break; }
			UINT64 done = 0;
			if (FAILED(c->outputReady->GetCompletedValue(&done))) continue;
			const LONG64 recorded = InterlockedCompareExchange64(&c->lastRecorded, 0, 0);
			const ULONGLONG now = GetTickCount64();
			if (done != lastValue || (LONG64)done >= recorded) { lastValue = done; lastProgress = now; }
			else if (now - lastProgress > 3000) { why = "no frame came back from the helper for 3 seconds"; break; }
		}
		if (!why) return 0;
		strncpy_s(c->tripReason, why, _TRUNCATE);
		TerminateProcess(c->process, 1);
		WaitForSingleObject(c->process, 2000);
		UINT64 done = 0;
		c->outputReady->GetCompletedValue(&done);
		const UINT64 recorded = (UINT64)InterlockedCompareExchange64(&c->lastRecorded, 0, 0);
		c->outputReady->SignalFromHost((std::max)(done, recorded) + kReleaseMargin);
		InterlockedExchange(&c->tripped, 1);
		return 0;
	}

	bool Core::Start() {
		ID3D9NvrExt* ext = DxvkCompute::Extension();
		if (!ext || !DxvkCompute::Available()) { TurnOff("no NVR DXVK (native D3D9 or stock DXVK)"); return false; }
		if (FAILED(ext->QueryInterface(__uuidof(ID3D9NvrExtShare), (void**)&share)) || !share) {
			share = nullptr;
			TurnOff("this NVR DXVK d3d9.dll cannot share images with another process (fork patch 1.7-dlss / 0026 needed)");
			return false;
		}
		if (FAILED(ext->QueryInterface(__uuidof(ID3D9NvrExtRt), (void**)&rt)) || !rt) {
			rt = nullptr;
			TurnOff("this NVR DXVK d3d9.dll has no ray tracing core (fork patch 1.8-rt / 0028 needed)");
			return false;
		}
		NVR_EXT_SHARE_INFO info = {};
		share->GetShareInfo(&info);
		if (!info.Supported) { TurnOff("the Vulkan driver cannot share images and semaphores between processes"); return false; }

		const std::wstring game = GameDir();
		const std::wstring candidates[] = { game, game + L"\\Data\\NVSE\\Plugins\\NvrRt" };
		std::wstring exe, helperDir;
		for (const std::wstring& dir : candidates)
			if (FileExists(dir + L"\\NvrRtHelper.exe")) { helperDir = dir; exe = dir + L"\\NvrRtHelper.exe"; break; }
		if (exe.empty()) { TurnOff("NvrRtHelper.exe was not found (the game folder, or Data\\NVSE\\Plugins\\NvrRt)"); return false; }

		// The images: the arena first, then every user's (registered by their Setup now, at this screen size).
		width = TheRenderManager->width;
		height = TheRenderManager->height;
		imageNames.clear();
		imageDescs.clear();
		const UINT arenaMB = (std::max)(4, (std::min)(256, TheSettingManager->GetSettingI("Main.Main.RayTracing", "ArenaMB")));
		imageNames.push_back("NVR RT arena");
		imageDescs.push_back({ kArenaWidth, arenaMB * 1024u * 1024u / 4u / kArenaWidth, NVR_EXT_SHARED_R32F, nullptr });
		inSetup = true;
		for (const RtCore::User& user : users) if (user.Setup) user.Setup();
		inSetup = false;
		if (imageDescs.size() > NVR_RT_MAX_IMAGES) { TurnOff("the RT effects asked for more shared images than the protocol holds"); return false; }
		char why[320];
		images.assign(imageDescs.size(), nullptr);
		for (size_t i = 0; i < imageDescs.size(); i++) {
			imageDescs[i].pDebugName = imageNames[i].c_str();
			const HRESULT hr = share->CreateSharedImage(&imageDescs[i], &images[i]);
			if (FAILED(hr)) {
				sprintf_s(why, "the shared image '%s' could not be made (fork CreateSharedImage: %s)", imageNames[i].c_str(), HrText(hr).c_str());
				TurnOff(why);
				return false;
			}
		}
		arenaCapacity = imageDescs[0].Width * imageDescs[0].Height;
		ID3D9NvrExtSharedFence** fences[2] = { &inputsReady, &outputReady };
		for (UInt32 i = 0; i < 2; i++) {
			const HRESULT hr = share->CreateSharedFence(0, fences[i]);
			if (FAILED(hr)) { sprintf_s(why, "a shared semaphore could not be made (fork CreateSharedFence: %s)", HrText(hr).c_str()); TurnOff(why); return false; }
		}

		wchar_t base[96];
		swprintf_s(base, L"Local\\NvrRt-%lu-%llu", GetCurrentProcessId(), (unsigned long long)GetTickCount64());
		mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(NvrRtShared), base);
		shared = mapping ? (NvrRtShared*)MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(NvrRtShared)) : nullptr;
		frameEvent = CreateEventW(nullptr, FALSE, FALSE, (std::wstring(base) + NVR_RT_EVENT_FRAME_SUFFIX).c_str());
		statusEvent = CreateEventW(nullptr, FALSE, FALSE, (std::wstring(base) + NVR_RT_EVENT_STATUS_SUFFIX).c_str());
		if (!shared || !frameEvent || !statusEvent) { TurnOff("its shared-memory block could not be made"); return false; }
		memset(shared, 0, sizeof(NvrRtShared));
		shared->magic = NVR_RT_MAGIC;
		shared->version = NVR_RT_PROTOCOL_VERSION;
		shared->size = sizeof(NvrRtShared);
		shared->parentPid = GetCurrentProcessId();
		shared->vendorId = info.VendorId;
		memcpy(shared->deviceLuid, info.DeviceLUID, 8);
		shared->deviceLuidValid = info.DeviceLUIDValid ? 1u : 0u;
		memcpy(shared->deviceUuid, info.DeviceUUID, 16);
		memcpy(shared->driverUuid, info.DriverUUID, 16);
		// shaders registered so far (the helper compiles them before it says ready)
		const std::string shaderDir = std::string("Data\\Shaders\\NewVegasReloaded\\Compute\\Rt\\");
		char cwd[MAX_PATH] = {};
		GetCurrentDirectoryA(MAX_PATH, cwd);
		for (size_t i = 0; i < shaders.size() && i < NVR_RT_MAX_SHADERS; i++)
			_snprintf_s(shared->shaderPaths[i], sizeof(shared->shaderPaths[i]), _TRUNCATE, "%s\\%s%s.spv", cwd, shaderDir.c_str(), shaders[i].c_str());
		shared->shaderCount = (uint32_t)(std::min<size_t>)(shaders.size(), NVR_RT_MAX_SHADERS);

		std::wstring cmdLine = L"\"" + exe + L"\" --shm " + base;
		STARTUPINFOW si = { sizeof(si) };
		PROCESS_INFORMATION pi = {};
		if (!CreateProcessW(exe.c_str(), &cmdLine[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW | ABOVE_NORMAL_PRIORITY_CLASS, nullptr, helperDir.c_str(), &si, &pi)) {
			sprintf_s(why, "NvrRtHelper.exe could not be started (CreateProcess: Win32 error %lu)", GetLastError());
			TurnOff(why);
			return false;
		}
		CloseHandle(pi.hThread);
		process = pi.hProcess;

		auto give = [&](const char* what, HRESULT created, HANDLE local, uint64_t& remote) {
			if (FAILED(created) || !local) { sprintf_s(why, "the %s's handle could not be made (fork CreateHandle: %s)", what, HrText(created).c_str()); return false; }
			HANDLE target = nullptr;
			if (!DuplicateHandle(GetCurrentProcess(), local, process, &target, 0, FALSE, DUPLICATE_SAME_ACCESS | DUPLICATE_CLOSE_SOURCE)) {
				sprintf_s(why, "the %s's handle could not be given to the helper (Win32 error %lu)", what, GetLastError());
				return false;
			}
			remote = (uint64_t)(uintptr_t)target;
			return true;
		};
		shared->imageCount = (uint32_t)images.size();
		for (size_t i = 0; i < images.size(); i++) {
			NVR_EXT_SHARED_IMAGE_INFO image = {};
			images[i]->GetInfo(&image);
			NvrRtImageDesc& d = shared->images[i];
			d.allocationSize = image.AllocationSize;
			d.memoryTypeIndex = image.MemoryTypeIndex;
			d.vkFormat = image.VkFormat;
			d.width = image.Width;
			d.height = image.Height;
			d.usage = image.Usage;
			d.flags = image.Flags;
			d.tiling = image.Tiling;
			d.layout = image.Layout;
			d.handleType = image.HandleType;
			d.dedicated = image.Dedicated ? 1u : 0u;
			HANDLE handle = nullptr;
			const HRESULT hr = images[i]->CreateHandle(&handle);
			if (!give(imageNames[i].c_str(), hr, handle, d.handle)) { TurnOff(why); return false; }
		}
		HANDLE handle = nullptr;
		HRESULT hr = inputsReady->CreateHandle(&handle);
		if (!give("'inputs ready' semaphore", hr, handle, shared->semInputsReady)) { TurnOff(why); return false; }
		handle = nullptr;
		hr = outputReady->CreateHandle(&handle);
		if (!give("'output ready' semaphore", hr, handle, shared->semOutputReady)) { TurnOff(why); return false; }
		MemoryBarrier();
		shared->setupDone = 1;
		SetEvent(frameEvent);

		frame = 0;
		InterlockedExchange64(&lastRecorded, 0);
		InterlockedExchange(&watchStop, 0);
		InterlockedExchange(&tripped, 0);
		watchdog = CreateThread(nullptr, 64 * 1024, WatchdogMain, this, 0, nullptr);
		if (!watchdog) { TurnOff("its watchdog thread could not be started"); return false; }
		startTick = GetTickCount64();
		state = Starting;
		lastSerial = 0;
		geometries.clear();
		nextId = 1;
		_snprintf_s(status, sizeof(status), _TRUNCATE, "starting on %s", info.DeviceName);
		Logger::Log("UNOFFICIAL NVR DXVK: RT helper started (%ls): %u shared images (arena %ux%u), %u shaders, GPU %s.", exe.c_str(),
			(unsigned)images.size(), imageDescs[0].Width, imageDescs[0].Height, (unsigned)shaders.size(), info.DeviceName);
		return true;
	}

	void Core::Stop(const char* why) {
		if (why && state != Idle && state != Off) Logger::Log("UNOFFICIAL NVR DXVK: RT helper stopped: %s.", why);
		if (watchdog) {
			InterlockedExchange(&watchStop, 1);
			WaitForSingleObject(watchdog, 3000);
			CloseHandle(watchdog);
			watchdog = nullptr;
		}
		const UInt64 recorded = (UInt64)InterlockedCompareExchange64(&lastRecorded, 0, 0);
		if (process) {
			if (shared && WaitForSingleObject(process, 0) != WAIT_OBJECT_0) {
				for (int i = 0; i < 200 && (UInt64)shared->submittedFrame < recorded; i++) Sleep(5);
				shared->quit = 1;
				SetEvent(frameEvent);
				if (WaitForSingleObject(process, 3000) != WAIT_OBJECT_0) TerminateProcess(process, 1);
			}
			CloseHandle(process);
			process = nullptr;
		}
		if (outputReady) {
			UINT64 done = 0;
			outputReady->GetCompletedValue(&done);
			if (done < recorded) outputReady->SignalFromHost(recorded + kReleaseMargin);
		}
		for (ID3D9NvrExtSharedImage*& image : images) DxvkCompute::Drop(image);
		images.clear();
		DxvkCompute::Drop(inputsReady);
		DxvkCompute::Drop(outputReady);
		DxvkCompute::Drop(rt);
		DxvkCompute::Drop(share);
		if (shared) { UnmapViewOfFile(shared); shared = nullptr; }
		HANDLE* handles[] = { &mapping, &frameEvent, &statusEvent };
		for (HANDLE* h : handles) if (*h) { CloseHandle(*h); *h = nullptr; }
		geometries.clear();
	}

	void Core::Poll() {
		LogHelperMessage();
		if (state == Starting) {
			const UInt32 helperState = shared->state;
			if (helperState == NVR_RT_STATE_READY) {
				state = Running;
				_snprintf_s(status, sizeof(status), _TRUNCATE, "running");
				Logger::Log("UNOFFICIAL NVR DXVK: RT helper ready after %llu ms; ray tracing from the next frame on.", (unsigned long long)(GetTickCount64() - startTick));
			}
			else if (helperState == NVR_RT_STATE_FAILED || helperState == NVR_RT_STATE_EXITED) TurnOff("the helper gave up (its message is above)");
			else if (InterlockedCompareExchange(&tripped, 0, 0)) TurnOff(tripReason);
			else if (GetTickCount64() - startTick > 15000) TurnOff("the helper did not get ready within 15 seconds");
			return;
		}
		if (state == Running) {
			if (InterlockedCompareExchange(&tripped, 0, 0)) TurnOff(tripReason);
			else if (shared->state == NVR_RT_STATE_FAILED) TurnOff("the helper stopped (its message is above)");
		}
	}

	// --- The scene ----------------------------------------------------------------------------------------------------------

	bool PositionOf(IDirect3DVertexDeclaration9* declaration, PositionElement& out) {
		D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH + 1];
		UINT count = 0;
		if (!declaration || FAILED(declaration->GetDeclaration(nullptr, &count)) || count > MAXD3DDECLLENGTH + 1) return false;
		if (FAILED(declaration->GetDeclaration(elements, &count))) return false;
		for (UINT i = 0; i < count && elements[i].Stream != 0xFF; i++) {
			const D3DVERTEXELEMENT9& e = elements[i];
			if (e.Usage != D3DDECLUSAGE_POSITION || e.UsageIndex != 0) continue;
			if (e.Type == D3DDECLTYPE_FLOAT3) out.format = NVR_EXT_RT_POSITION_FLOAT3;
			else if (e.Type == D3DDECLTYPE_FLOAT4) out.format = NVR_EXT_RT_POSITION_FLOAT4;
			else if (e.Type == D3DDECLTYPE_FLOAT16_4) out.format = NVR_EXT_RT_POSITION_HALF4;
			else return false;
			out.stream = e.Stream;
			out.offset = e.Offset;
			return true;
		}
		return false;
	}

	bool Core::Describe(NiGeometry* geo, NVR_EXT_RT_GEOMETRY_DESC& desc, std::vector<NVR_EXT_RT_RANGE>& ranges, GeometryKey& key) {
		NiGeometryBufferData* data = geo->geomData->m_pkBuffData;
		if (!data || !data->VertCount || !data->IB || !data->VBChip || !data->VertexStride || !data->StreamCount) return false;
		PositionElement position;
		if (data->FVF) {
			if ((data->FVF & D3DFVF_POSITION_MASK) != D3DFVF_XYZ) return false;
			position.stream = 0;
			position.offset = 0;
		}
		else {
			auto found = declarations.find(data->VertexDeclaration);
			if (found == declarations.end()) {
				PositionElement e;
				if (!PositionOf(data->VertexDeclaration, e)) e.stream = -1;
				found = declarations.emplace(data->VertexDeclaration, e).first;
			}
			position = found->second;
		}
		if (position.stream < 0 || (UInt32)position.stream >= data->StreamCount || !data->VBChip[position.stream] || !data->VBChip[position.stream]->VB) return false;

		const UInt32 stride = data->VertexStride[position.stream];
		key = { data, data->VBChip[position.stream]->VB, data->IB, data->VertCount, data->TriCount, data->BaseVertexIndex };

		// The draws RenderPass::DrawGeometryBuffer makes (the game's RenderTriStripsAlt / RenderTriShapeAlt): one per strip, or the list.
		ranges.clear();
		if (data->PrimitiveType == D3DPT_TRIANGLESTRIP) {
			UInt32 start = 0;
			for (UInt32 i = 0; i < data->NumArrays; i++) {
				const UInt32 length = data->ArrayLengths ? data->ArrayLengths[i] : data->TriCount + 2;
				if (length >= 3) ranges.push_back({ NVR_EXT_RT_TRIANGLE_STRIP, start, length - 2, 0 });
				start += length;
			}
		}
		else if (data->PrimitiveType == D3DPT_TRIANGLELIST) ranges.push_back({ NVR_EXT_RT_TRIANGLE_LIST, 0, data->TriCount, 0 });
		if (ranges.empty() || ranges.size() > NVR_EXT_RT_MAX_RANGES) return false;

		desc = {};
		desc.pVertexBuffer = data->VBChip[position.stream]->VB;
		desc.PositionOffset = position.offset + data->BaseVertexIndex * stride;	// indices count from this geometry's first vertex
		desc.VertexStride = stride;
		desc.VertexCount = data->VertCount;
		desc.PositionFormat = position.format;
		desc.pIndexBuffer = data->IB;
		desc.RangeCount = (UINT)ranges.size();
		desc.pRanges = ranges.data();
		desc.pDebugName = geo->m_pcName;
		return true;
	}

	void Core::AddGeometry(NiGeometry* geo, bool land) {
		if (!geo->shader || !geo->geomData || !geo->geomData->m_pkBuffData || geo->skinInstance) return;
		BSShaderProperty* shaderProperty = (BSShaderProperty*)geo->GetProperty(NiProperty::PropertyType::kType_Shade);
		if (!shaderProperty || !shaderProperty->IsLightingProperty()) return;
		if (shaderProperty->GetFlag(BSSP_REFRACTION) || shaderProperty->GetFlag(BSSP_FIRE_REFRACTION) || shaderProperty->GetFlag(BSSP_DECAL) ||
			shaderProperty->GetFlag(BSSP_DYNAMIC_DECAL)) return;
		if (static_cast<NiShadeProperty*>((NiProperty*)shaderProperty)->m_eShaderType == NiShadeProperty::kProp_SpeedTreeLeaf) return;
		// Alpha-tested and blended geometry (foliage cards, fences, glass) is not traced yet: the cascades keep it (FORK-RT-SUN.md).
		NiAlphaProperty* alpha = (NiAlphaProperty*)geo->GetProperty(NiProperty::PropertyType::kType_Alpha);
		if (alpha && (alpha->flags & (NiAlphaProperty::AlphaFlags::ALPHA_BLEND_MASK | NiAlphaProperty::AlphaFlags::TEST_ENABLE_MASK))) return;

		NVR_EXT_RT_GEOMETRY_DESC desc;
		static std::vector<NVR_EXT_RT_RANGE> ranges;
		GeometryKey key;
		if (!Describe(geo, desc, ranges, key)) return;

		GeometryEntry& entry = geometries[key];
		if (entry.refused) return;
		entry.lastSeen = sceneFrame;
		if (!entry.sent) {
			if (!entry.id) entry.id = nextId++ & 0xFFFFFF;
			NVR_EXT_RT_GEOMETRY_INFO info = {};
			if (FAILED(rt->MeasureGeometry(&desc, &info))) { entry.refused = true; return; }
			if (record->opCount >= NVR_RT_MAX_OPS || arenaUsed + info.Texels > arenaCapacity) { waiting++; return; }	// next frame
			if (FAILED(rt->StageGeometry(&desc, images[0], arenaUsed, &info))) { entry.refused = true; return; }
			NvrRtGeometryOp& op = record->ops[record->opCount++];
			op = NvrRtGeometryOp();
			op.op = NVR_RT_OP_BUILD;
			op.id = entry.id;
			op.vertexCount = info.VertexCount;
			op.triangleCount = info.TriangleCount;
			op.arenaTexel = arenaUsed;
			arenaUsed += info.Texels;
			entry.sent = true;
		}
		if (record->instanceCount >= NVR_RT_MAX_INSTANCES) return;
		NvrRtInstance& instance = record->instances[record->instanceCount++];
		D3DMATRIX m;
		TheRenderManager->CreateD3DMatrix(&m, &geo->m_worldTransform);	// camera relative, row vectors (p' = p M)
		for (int r = 0; r < 3; r++) {
			for (int c = 0; c < 3; c++) instance.transform[r][c] = m.m[c][r];
			instance.transform[r][3] = m.m[3][r];
		}
		instance.geometryId = entry.id;
		instance.customIndex = land ? 1u : 0u;
		instance.mask = land ? 0x01u : 0x02u;	// cull mask bit 0 land, bit 1 objects
		instance.flags = 0x1u;	// both faces (the game's two-sided and one-sided meshes alike: shadows need the back faces too)
	}

	void Core::Walk(NiAVObject* root, bool land) {
		if (!root) return;
		const NiPoint3 camera = { TheRenderManager->CameraPosition.x, TheRenderManager->CameraPosition.y, TheRenderManager->CameraPosition.z };
		auto inReach = [&](NiAVObject* o) {
			const NiBound* b = o->m_kWorldBound;
			if (!b) return true;
			const float dx = b->Center.x - camera.x, dy = b->Center.y - camera.y, dz = b->Center.z - camera.z;
			const float reach = radius + b->Radius;
			return dx * dx + dy * dy + dz * dz <= reach * reach;
		};
		stack.clear();
		stack.push_back(root);
		while (!stack.empty()) {
			NiAVObject* object = stack.back();
			stack.pop_back();
			if (!object || (object->m_flags & NiAVObject::NiFlags::APP_CULLED) || !inReach(object)) continue;
			if (object->IsGeometry()) { AddGeometry(static_cast<NiGeometry*>(object), land); continue; }
			NiNode* node = object->IsNiNode();
			if (!node) continue;
			if (node->IsFadeNode() && static_cast<BSFadeNode*>(node)->FadeAlpha <= 0.0f) continue;	// faded out: not drawn
			if (node->IsKindOf<NiSwitchNode>()) {
				NiSwitchNode* sw = static_cast<NiSwitchNode*>(node);
				if (sw->m_iIndex >= 0 && sw->m_iIndex < node->m_children.end) stack.push_back(node->m_children.data[sw->m_iIndex]);
				continue;
			}
			for (int i = 0; i < node->m_children.end; i++) if (node->m_children.data[i]) stack.push_back(node->m_children.data[i]);
		}
	}

	void Core::WalkCell(TESObjectCELL* cell) {
		if (!cell) return;
		Walk(cell->GetChildNode(TESObjectCELL::kCellNode_Land), true);
		for (TList<TESObjectREFR>::Entry* entry = &cell->objectList.First; entry; entry = entry->next) {
			TESObjectREFR* ref = entry->item;
			if (!ref || !ref->baseForm || (ref->flags & TESForm::FormFlags::kFormFlags_NotCastShadows)) continue;
			const UInt8 type = ref->baseForm->formType;
			if (type == TESForm::FormType::kFormType_Land || type == TESForm::FormType::kFormType_NPC || type == TESForm::FormType::kFormType_Creature ||
				type == TESForm::FormType::kFormType_LeveledCreature) continue;	// actors are skinned: not yet (FORK-RT-SUN.md)
			Walk(ref->GetNode(), false);
		}
	}

	void Core::Gather() {
		sceneFrame++;
		radius = (std::max)(512.0f, TheSettingManager->GetSettingF("Main.Main.RayTracing", "SceneRadius"));
		arenaUsed = 0;
		waiting = 0;
		if (Player && Player->GetWorldSpace() && Tes && Tes->gridCellArray) {
			GridCellArray* cells = Tes->gridCellArray;
			const UInt32 count = cells->size * cells->size;
			for (UInt32 i = 0; i < count; i++) WalkCell(cells->GetCell(i));
		}
		else if (Player) WalkCell(Player->parentCell);
		instances = record->instanceCount;

		// Geometries not met for a while (their cell unloaded, the object gone): released in the helper
		if ((sceneFrame % 30) == 0) {
			for (auto it = geometries.begin(); it != geometries.end(); ) {
				if (sceneFrame - it->second.lastSeen > kReleaseAfterFrames && record->opCount < NVR_RT_MAX_OPS) {
					if (it->second.sent) {
						NvrRtGeometryOp& op = record->ops[record->opCount++];
						op = NvrRtGeometryOp();
						op.op = NVR_RT_OP_RELEASE;
						op.id = it->second.id;
						released++;
					}
					it = geometries.erase(it);
				}
				else ++it;
			}
		}
		record->arenaTexels = arenaUsed;
	}
}

void RtCore::AddUser(const User& user) { TheCore.users.push_back(user); }

int RtCore::AddImage(const char* Name, UINT Width, UINT Height, NVR_EXT_SHARED_FORMAT Format) {
	if (!TheCore.inSetup || !Width || !Height) return -1;
	TheCore.imageNames.push_back(Name ? Name : "NVR RT image");
	TheCore.imageDescs.push_back({ Width, Height, Format, nullptr });
	return (int)TheCore.imageDescs.size() - 1;
}

int RtCore::AddShader(const char* Name) {
	for (size_t i = 0; i < TheCore.shaders.size(); i++) if (TheCore.shaders[i] == Name) return (int)i;
	if (TheCore.shaders.size() >= NVR_RT_MAX_SHADERS) return -1;
	TheCore.shaders.push_back(Name);
	const int index = (int)TheCore.shaders.size() - 1;
	if (TheCore.shared) {	// the helper is up: tell it (it compiles the shader while idle)
		char cwd[MAX_PATH] = {};
		GetCurrentDirectoryA(MAX_PATH, cwd);
		_snprintf_s(TheCore.shared->shaderPaths[index], sizeof(TheCore.shared->shaderPaths[index]), _TRUNCATE,
			"%s\\Data\\Shaders\\NewVegasReloaded\\Compute\\Rt\\%s.spv", cwd, Name);
		MemoryBarrier();
		TheCore.shared->shaderCount = (uint32_t)TheCore.shaders.size();
		SetEvent(TheCore.frameEvent);
	}
	return index;
}

bool RtCore::CopyIn(int Image, IUnknown* Source, const RECT* Rect) {
	if (!TheCore.recording || Image <= 0 || Image >= (int)TheCore.images.size() || !Source) return false;
	return SUCCEEDED(TheCore.share->CopyToShared(TheCore.images[Image], 0, 0, Source, Rect));
}

bool RtCore::AddPass(int Shader, const int* Images, UINT ImageCount, UINT WriteMask, const void* Constants, UINT ConstantsSize, UINT GroupsX, UINT GroupsY) {
	NvrRtFrame* f = TheCore.record;
	if (!TheCore.recording || !f || f->passCount >= NVR_RT_MAX_PASSES || Shader < 0 || ImageCount > NVR_RT_PASS_IMAGES || ConstantsSize > NVR_RT_PASS_CONSTANTS)
		return false;
	NvrRtPass& p = f->passes[f->passCount];
	memset(&p, 0, offsetof(NvrRtPass, constants));
	p.shader = (uint32_t)Shader;
	p.groupsX = GroupsX;
	p.groupsY = GroupsY;
	p.groupsZ = 1;
	for (UINT i = 0; i < ImageCount; i++) {
		if (Images[i] <= 0 || Images[i] >= (int)TheCore.images.size()) return false;
		p.images[i] = (uint32_t)Images[i];
	}
	p.imageCount = ImageCount;
	p.writeMask = WriteMask;
	p.constantsSize = ConstantsSize;
	if (ConstantsSize) memcpy(p.constants, Constants, ConstantsSize);
	f->passCount++;
	return true;
}

bool RtCore::CopyOut(int Image, IUnknown* Target) {
	if (!TheCore.recording || Image <= 0 || Image >= (int)TheCore.images.size() || !Target) return false;
	TheCore.copiesOut.push_back({ Image, Target });
	return true;
}

void RtCore::Frame(IDirect3DDevice9* Device) {
	Core& c = TheCore;
	if (c.state == Core::Off) return;
	bool wanted = false;
	for (const User& user : c.users) wanted = wanted || (user.Wants && user.Wants());
	if (!wanted) return;	// a running helper stays up (idle); it exits with the game
	if (c.state == Core::Idle) { c.Start(); return; }
	c.Poll();
	if (c.state != Core::Running) return;
	if (TheRenderManager->width != c.width || TheRenderManager->height != c.height) {
		c.Stop("the screen size changed (the shared images are made again)");
		c.state = Core::Idle;
		return;
	}

	// The ring slot: the helper must have copied out the record that used it last (normally long ago; a few ms at worst)
	const UInt64 n = c.frame + 1;
	for (int i = 0; i < 100 && c.shared->consumedFrame + (int64_t)NVR_RT_FRAME_RING < (int64_t)n; i++) Sleep(1);
	if (c.shared->consumedFrame + (int64_t)NVR_RT_FRAME_RING < (int64_t)n) {
		static UInt32 skipped = 0;
		if ((skipped++ % 300) == 0) Logger::Log("UNOFFICIAL NVR DXVK: RT helper behind (frame %llu, read up to %lld): this frame not traced (%u so far).",
			(unsigned long long)n, (long long)c.shared->consumedFrame, skipped);
		return;
	}
	static GpuTimer timer("Ray tracing (copies + helper + waits)");
	GpuProfileScope gpu(timer, Device);
	c.frame = n;
	NvrRtFrame& f = c.shared->frames[n % NVR_RT_FRAME_RING];
	f.frame = 0;
	MemoryBarrier();
	f.opCount = f.instanceCount = f.passCount = f.arenaTexels = 0;
	c.record = &f;
	c.recording = true;
	c.copiesOut.clear();

	c.Gather();
	c.rt->FlushStagedGeometry(c.images[0]);
	for (const User& user : c.users) if (user.Wants && user.Wants() && user.Record) user.Record(Device);

	LARGE_INTEGER written;
	QueryPerformanceCounter(&written);
	f.writtenQpc = written.QuadPart;
	MemoryBarrier();
	f.frame = n;
	MemoryBarrier();
	InterlockedExchange64((volatile LONG64*)&c.shared->latestFrame, (LONG64)n);
	SetEvent(c.frameEvent);
	InterlockedExchange64(&c.lastRecorded, (LONG64)n);
	c.share->SignalShared(c.inputsReady, n);
	c.share->WaitShared(c.outputReady, n);
	for (const PendingCopy& copy : c.copiesOut) c.share->CopyFromShared(copy.target, 0, 0, c.images[copy.image], nullptr);
	c.copiesOut.clear();
	c.recording = false;
	c.record = nullptr;

	if ((n % 600) == 0) {
		const NvrRtShared* s = c.shared;
		Logger::Log("RT CORE: frame %llu, %u instances, %u geometries known (%u waiting for arena space, %u released); helper: %u geometries, %llu triangles, "
			"%llu MB; GPU %.3f ms avg (builds %.3f, scene %.3f, passes %.3f), response %.3f ms avg (max %.3f)",
			(unsigned long long)n, c.instances, (unsigned)c.geometries.size(), c.waiting, c.released, s->geometries, (unsigned long long)s->triangles,
			(unsigned long long)(s->geometryBytes >> 20), s->gpuTimeUsAvg / 1000.0, s->buildUsAvg / 1000.0, s->sceneUsAvg / 1000.0, s->traceUsAvg / 1000.0,
			s->responseUsAvg / 1000.0, s->responseUsMax / 1000.0);
		c.shared->responseUsMax = 0;
	}
}

bool RtCore::Running() { return TheCore.state == Core::Running; }
const char* RtCore::StatusText() { return TheCore.status; }
const NvrRtShared* RtCore::Helper() { return TheCore.state == Core::Running ? TheCore.shared : nullptr; }
UInt32 RtCore::Instances() { return TheCore.instances; }
UInt32 RtCore::Geometries() { return (UInt32)TheCore.geometries.size(); }
UInt32 RtCore::Waiting() { return TheCore.waiting; }
UInt32 RtCore::Released() { return TheCore.released; }
