
#include <algorithm>
#include <cmath>
#include <string>

#include "Upscaler.h"
#include "DxvkCompute.h"
#include "GpuProfiler.h"
#include "../../tools/nvr-dlss-helper/NvrDlssProtocol.h"

namespace {
	const UINT kNvidiaVendor = 0x10DE;
	const UINT64 kReleaseMargin = 1ull << 20;

	bool FileExists(const std::wstring& path) {
		const DWORD attributes = GetFileAttributesW(path.c_str());
		return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
	}

	std::string HrText(HRESULT hr) {
		char text[96];
		if (HRESULT_FACILITY(hr) == NVR_EXT_FACILITY_VK) sprintf_s(text, "VkResult %d", -(int)(hr & 0xFFFF));
		else if (HRESULT_FACILITY(hr) == FACILITY_WIN32) sprintf_s(text, "Win32 error %lu", (unsigned long)HRESULT_CODE(hr));
		else if (hr == D3DERR_NOTAVAILABLE) sprintf_s(text, "D3DERR_NOTAVAILABLE: not shared / not supported");
		else if (SUCCEEDED(hr)) sprintf_s(text, "it returned no handle");
		else sprintf_s(text, "HRESULT 0x%08lX", (unsigned long)hr);
		return text;
	}

	std::wstring GameDir() {
		wchar_t path[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, path, MAX_PATH);
		std::wstring dir(path);
		const size_t slash = dir.find_last_of(L"\\/");
		return slash == std::wstring::npos ? L"." : dir.substr(0, slash);
	}

	class DlssUpscaler : public Upscaler::IUpscaler {
	public:
		~DlssUpscaler() override {}

		const char* Name() const override {
			if (state != Running) return "FSR 2.2.1 (DLSS not running)";
			return passThrough ? "DLSS helper pass-through test (no DLSS)" : "NVIDIA DLSS (64-bit helper)";
		}
		IDirect3DTexture9*	Output() const override { return usedSelf ? output.Texture : Upscaler::Fsr2()->Output(); }
		bool				Failed() const override { return state == Running ? false : Upscaler::Fsr2()->Failed(); }
		bool				Evaluate(IDirect3DDevice9* Device, const Upscaler::Inputs& In) override;
		void				ReleaseResources() override { Stop("its resources were released"); state = Idle; }

		bool	IsOff() const { return state == Off; }
		void	MarkUnused() { dlssNeedsReset = true; }
		void	SetOptions(bool test, int jitterFlip) {
			if (test != passThrough && state != Idle && state != Off) {
				Stop(test ? "HelperTest turned on" : "HelperTest turned off");
				state = Idle;
			}
			passThrough = test;
			flipX = (jitterFlip & 1) != 0;
			flipY = (jitterFlip & 2) != 0;
		}

	private:
		enum State { Idle, Starting, Running, Off };
		struct Tex { IDirect3DTexture9* Texture = nullptr; IDirect3DSurface9* Surface = nullptr; };

		State		state = Idle;
		bool		passThrough = false;
		bool		flipX = false, flipY = false;
		bool		usedSelf = false;
		bool		fsrNeedsReset = false;
		bool		dlssNeedsReset = true;
		ID3D9NvrExtShare*		share = nullptr;
		ID3D9NvrExtSharedImage*	images[NVR_DLSS_IMAGE_COUNT] = {};
		ID3D9NvrExtSharedFence*	inputsReady = nullptr;
		ID3D9NvrExtSharedFence*	outputReady = nullptr;
		Tex			output;
		UINT		inputWidth = 0, inputHeight = 0, displayWidth = 0, displayHeight = 0;
		HANDLE		mapping = nullptr, frameEvent = nullptr, statusEvent = nullptr, process = nullptr, watchdog = nullptr;
		NvrDlssShared*	shared = nullptr;
		ULONGLONG	startTick = 0;
		UInt64		frame = 0;
		volatile LONG64	lastRecorded = 0;
		volatile LONG	watchStop = 0;
		volatile LONG	tripped = 0;
		char		tripReason[160] = {};
		UInt32		reported = 0;
		UInt32		lastSerial = 0;
		double		recordUs = 0.0;
		UInt32		recordCount = 0;
		std::wstring helperDir;

		bool	TurnOff(const char* why);
		bool	Start(const Upscaler::Inputs& In);
		void	Stop(const char* why);
		void	Poll();
		bool	Record(IDirect3DDevice9* Device, const Upscaler::Inputs& In);
		bool	FallBack(IDirect3DDevice9* Device, const Upscaler::Inputs& In);
		void	LogHelperMessage();
		static DWORD WINAPI WatchdogMain(void* self);
	};

	DlssUpscaler TheDlss;

	bool DlssUpscaler::TurnOff(const char* why) {
		Logger::Log("UNOFFICIAL NVR DXVK: [Shaders.TAA.DLSS] Enabled (%s): %s; FSR 2 is used instead for this session.",
			passThrough ? "DLSS helper pass-through test" : "DLSS", why);
		Stop(nullptr);
		state = Off;
		return false;
	}

	void DlssUpscaler::LogHelperMessage() {
		if (!shared || shared->messageSerial == lastSerial) return;
		lastSerial = shared->messageSerial;
		char message[sizeof(shared->message)];
		memcpy(message, (const void*)shared->message, sizeof(message));
		message[sizeof(message) - 1] = 0;
		Logger::Log("UNOFFICIAL NVR DXVK: DLSS helper: %s", message);
	}

	DWORD WINAPI DlssUpscaler::WatchdogMain(void* self) {
		DlssUpscaler* d = (DlssUpscaler*)self;
		UINT64 lastValue = 0;
		ULONGLONG lastProgress = GetTickCount64();
		const char* why = nullptr;
		while (!InterlockedCompareExchange(&d->watchStop, 0, 0)) {
			if (WaitForSingleObject(d->process, 100) == WAIT_OBJECT_0) { why = "the helper process exited (NvrDlssHelper.log says why)"; break; }
			UINT64 done = 0;
			if (FAILED(d->outputReady->GetCompletedValue(&done))) continue;
			const LONG64 recorded = InterlockedCompareExchange64(&d->lastRecorded, 0, 0);
			const ULONGLONG now = GetTickCount64();
			if (done != lastValue || (LONG64)done >= recorded) { lastValue = done; lastProgress = now; }
			else if (now - lastProgress > 3000) { why = "no frame came back from the helper for 3 seconds"; break; }
		}
		if (!why) return 0;
		strncpy_s(d->tripReason, why, _TRUNCATE);
		TerminateProcess(d->process, 1);
		WaitForSingleObject(d->process, 2000);
		UINT64 done = 0;
		d->outputReady->GetCompletedValue(&done);
		const UINT64 recorded = (UINT64)InterlockedCompareExchange64(&d->lastRecorded, 0, 0);
		d->outputReady->SignalFromHost((std::max)(done, recorded) + kReleaseMargin);
		InterlockedExchange(&d->tripped, 1);
		return 0;
	}

	bool DlssUpscaler::Start(const Upscaler::Inputs& In) {
		ID3D9NvrExt* ext = DxvkCompute::Extension();
		if (!ext || !DxvkCompute::Available()) return TurnOff("no NVR DXVK");
		if (FAILED(ext->QueryInterface(__uuidof(ID3D9NvrExtShare), (void**)&share)) || !share) {
			share = nullptr;
			return TurnOff("this NVR DXVK d3d9.dll cannot share images with another process (fork patch 1.7-dlss or newer needed)");
		}
		NVR_EXT_SHARE_INFO info = {};
		share->GetShareInfo(&info);
		if (!info.Supported) return TurnOff("the Vulkan driver cannot share images and semaphores between processes");
		if (info.VendorId != kNvidiaVendor) return TurnOff("not an NVIDIA GPU (DLSS needs an NVIDIA RTX GPU)");

		const std::wstring game = GameDir();
		const std::wstring candidates[] = { game, game + L"\\Data\\NVSE\\Plugins\\NvrDlss" };
		std::wstring exe;
		for (const std::wstring& dir : candidates) {
			if (FileExists(dir + L"\\NvrDlssHelper.exe")) { helperDir = dir; exe = dir + L"\\NvrDlssHelper.exe"; break; }
		}
		if (exe.empty()) return TurnOff("NvrDlssHelper.exe was not found (the game folder, or Data\\NVSE\\Plugins\\NvrDlss)");
		if (!passThrough && !FileExists(helperDir + L"\\nvngx_dlss.dll"))
			return TurnOff("nvngx_dlss.dll (NVIDIA's DLSS runtime) is not next to NvrDlssHelper.exe");

		const NVR_EXT_SHARED_IMAGE_DESC descs[NVR_DLSS_IMAGE_COUNT] = {
			{ In.InputWidth, In.InputHeight, NVR_EXT_SHARED_RGBA16F, "NVR DLSS colour" },
			{ In.InputWidth, In.InputHeight, NVR_EXT_SHARED_R32F, "NVR DLSS depth" },
			{ In.InputWidth, In.InputHeight, NVR_EXT_SHARED_RG16F, "NVR DLSS motion" },
			{ In.DisplayWidth, In.DisplayHeight, NVR_EXT_SHARED_RGBA16F, "NVR DLSS output" },
		};
		char why[320];
		for (UInt32 i = 0; i < NVR_DLSS_IMAGE_COUNT; i++) {
			const HRESULT hr = share->CreateSharedImage(&descs[i], &images[i]);
			if (FAILED(hr)) {
				sprintf_s(why, "the shared image '%s' could not be made (fork CreateSharedImage: %s; the DXVK log says more)", descs[i].pDebugName, HrText(hr).c_str());
				return TurnOff(why);
			}
		}
		ID3D9NvrExtSharedFence** fences[2] = { &inputsReady, &outputReady };
		for (UInt32 i = 0; i < 2; i++) {
			const HRESULT hr = share->CreateSharedFence(0, fences[i]);
			if (FAILED(hr)) {
				sprintf_s(why, "the shared semaphore '%s' could not be made (fork CreateSharedFence: %s; the DXVK log says more)", i ? "output ready" : "inputs ready", HrText(hr).c_str());
				return TurnOff(why);
			}
		}
		TheTextureManager->InitTexture("TESR_DlssOutput", &output.Texture, &output.Surface, In.DisplayWidth, In.DisplayHeight, D3DFMT_A16B16G16R16F);
		if (!output.Surface) return TurnOff("its output texture could not be made");
		inputWidth = In.InputWidth; inputHeight = In.InputHeight; displayWidth = In.DisplayWidth; displayHeight = In.DisplayHeight;

		wchar_t base[96];
		swprintf_s(base, L"Local\\NvrDlss-%lu-%llu", GetCurrentProcessId(), (unsigned long long)GetTickCount64());
		mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(NvrDlssShared), base);
		shared = mapping ? (NvrDlssShared*)MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(NvrDlssShared)) : nullptr;
		frameEvent = CreateEventW(nullptr, FALSE, FALSE, (std::wstring(base) + NVR_DLSS_EVENT_FRAME_SUFFIX).c_str());
		statusEvent = CreateEventW(nullptr, FALSE, FALSE, (std::wstring(base) + NVR_DLSS_EVENT_STATUS_SUFFIX).c_str());
		if (!shared || !frameEvent || !statusEvent) return TurnOff("its shared-memory block could not be made");
		memset(shared, 0, sizeof(NvrDlssShared));
		shared->magic = NVR_DLSS_MAGIC;
		shared->version = NVR_DLSS_PROTOCOL_VERSION;
		shared->size = sizeof(NvrDlssShared);
		shared->mode = passThrough ? NVR_DLSS_MODE_PASSTHROUGH : NVR_DLSS_MODE_DLSS;
		shared->parentPid = GetCurrentProcessId();
		shared->vendorId = info.VendorId;
		memcpy(shared->deviceLuid, info.DeviceLUID, 8);
		shared->deviceLuidValid = info.DeviceLUIDValid ? 1u : 0u;
		memcpy(shared->deviceUuid, info.DeviceUUID, 16);
		memcpy(shared->driverUuid, info.DriverUUID, 16);
		shared->displayWidth = displayWidth;
		shared->displayHeight = displayHeight;
		shared->inputWidth = inputWidth;
		shared->inputHeight = inputHeight;

		std::wstring cmdLine = L"\"" + exe + L"\" --shm " + base;
		STARTUPINFOW si = { sizeof(si) };
		PROCESS_INFORMATION pi = {};
		if (!CreateProcessW(exe.c_str(), &cmdLine[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW | ABOVE_NORMAL_PRIORITY_CLASS, nullptr, helperDir.c_str(), &si, &pi)) {
			sprintf_s(why, "NvrDlssHelper.exe could not be started (CreateProcess: Win32 error %lu)", GetLastError());
			return TurnOff(why);
		}
		CloseHandle(pi.hThread);
		process = pi.hProcess;

		auto give = [&](const char* what, HRESULT created, HANDLE local, uint64_t& remote) {
			if (FAILED(created) || !local) {
				sprintf_s(why, "the %s's handle could not be made (fork CreateHandle: %s; the DXVK log says more)", what, HrText(created).c_str());
				return false;
			}
			DWORD exitCode = 0;
			if (GetExitCodeProcess(process, &exitCode) && exitCode != STILL_ACTIVE) {
				CloseHandle(local);
				sprintf_s(why, "NvrDlssHelper.exe exited right after it started (exit code %lu; NvrDlssHelper.log next to it says why)", exitCode);
				return false;
			}
			HANDLE target = nullptr;
			if (!DuplicateHandle(GetCurrentProcess(), local, process, &target, 0, FALSE, DUPLICATE_SAME_ACCESS | DUPLICATE_CLOSE_SOURCE)) {
				sprintf_s(why, "the %s's handle could not be given to the helper (DuplicateHandle into the helper process: Win32 error %lu)", what, GetLastError());
				return false;
			}
			remote = (uint64_t)(uintptr_t)target;
			return true;
		};
		for (UInt32 i = 0; i < NVR_DLSS_IMAGE_COUNT; i++) {
			NVR_EXT_SHARED_IMAGE_INFO image = {};
			images[i]->GetInfo(&image);
			NvrDlssImageDesc& d = shared->images[i];
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
			char what[64];
			sprintf_s(what, "shared image '%s'", descs[i].pDebugName);
			if (!give(what, hr, handle, d.handle)) return TurnOff(why);
		}
		HANDLE handle = nullptr;
		HRESULT hr = inputsReady->CreateHandle(&handle);
		if (!give("'inputs ready' semaphore", hr, handle, shared->semInputsReady)) return TurnOff(why);
		handle = nullptr;
		hr = outputReady->CreateHandle(&handle);
		if (!give("'output ready' semaphore", hr, handle, shared->semOutputReady)) return TurnOff(why);
		Logger::Log("UNOFFICIAL NVR DXVK: DLSS helper: 4 images and 2 semaphores given to the helper (process %lu).", GetProcessId(process));
		MemoryBarrier();
		shared->setupDone = 1;
		SetEvent(frameEvent);

		frame = 0;
		InterlockedExchange64(&lastRecorded, 0);
		InterlockedExchange(&watchStop, 0);
		InterlockedExchange(&tripped, 0);
		watchdog = CreateThread(nullptr, 64 * 1024, WatchdogMain, this, 0, nullptr);
		if (!watchdog) return TurnOff("its watchdog thread could not be started");
		startTick = GetTickCount64();
		state = Starting;
		lastSerial = 0;
		Logger::Log("UNOFFICIAL NVR DXVK: DLSS helper started (%s, %ls): inputs %ux%u, display %ux%u, GPU %s; FSR 2 runs until it is ready.",
			passThrough ? "pass-through test" : "DLSS", exe.c_str(), inputWidth, inputHeight, displayWidth, displayHeight, info.DeviceName);
		return true;
	}

	void DlssUpscaler::Stop(const char* why) {
		if (why && state != Idle && state != Off) Logger::Log("UNOFFICIAL NVR DXVK: DLSS helper stopped: %s.", why);
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
		DxvkCompute::Drop(inputsReady);
		DxvkCompute::Drop(outputReady);
		DxvkCompute::Drop(share);
		if (output.Surface) { output.Surface->Release(); output.Surface = nullptr; }
		if (output.Texture) { output.Texture->Release(); output.Texture = nullptr; }
		if (shared) { UnmapViewOfFile(shared); shared = nullptr; }
		HANDLE* handles[] = { &mapping, &frameEvent, &statusEvent };
		for (HANDLE* h : handles) if (*h) { CloseHandle(*h); *h = nullptr; }
		inputWidth = inputHeight = displayWidth = displayHeight = 0;
		usedSelf = false;
		fsrNeedsReset = true;
		dlssNeedsReset = true;
	}

	void DlssUpscaler::Poll() {
		LogHelperMessage();
		if (state == Starting) {
			const UInt32 helperState = shared->state;
			if (helperState == NVR_DLSS_STATE_READY) {
				state = Running;
				dlssNeedsReset = true;
				reported = 0;
				Logger::Log("UNOFFICIAL NVR DXVK: DLSS helper ready after %llu ms (%s); it upscales from the next frame on.",
					(unsigned long long)(GetTickCount64() - startTick), passThrough ? "pass-through test: the picture is only stretched, with a marker" : "NVIDIA DLSS");
			}
			else if (helperState == NVR_DLSS_STATE_FAILED || helperState == NVR_DLSS_STATE_EXITED) TurnOff("the helper gave up (its message is above)");
			else if (InterlockedCompareExchange(&tripped, 0, 0)) TurnOff(tripReason);
			else if (GetTickCount64() - startTick > 15000) TurnOff("the helper did not get ready within 15 seconds");
			return;
		}
		if (state == Running) {
			if (InterlockedCompareExchange(&tripped, 0, 0)) TurnOff(tripReason);
			else if (shared->state == NVR_DLSS_STATE_FAILED) TurnOff("the helper stopped (its message is above)");
		}
	}

	bool DlssUpscaler::FallBack(IDirect3DDevice9* Device, const Upscaler::Inputs& In) {
		usedSelf = false;
		Upscaler::Inputs in = In;
		if (fsrNeedsReset) in.Reset = true;
		fsrNeedsReset = false;
		return Upscaler::Fsr2()->Evaluate(Device, in);
	}

	bool DlssUpscaler::Record(IDirect3DDevice9* Device, const Upscaler::Inputs& In) {
		LARGE_INTEGER t0, t1, frequency;
		QueryPerformanceCounter(&t0);
		const RECT render = { 0, 0, (LONG)In.RenderWidth, (LONG)In.RenderHeight };
		if (FAILED(share->CopyToShared(images[NVR_DLSS_IMAGE_COLOR], 0, 0, In.Color, &render)) ||
			FAILED(share->CopyToShared(images[NVR_DLSS_IMAGE_DEPTH], 0, 0, In.Depth, &render)) ||
			FAILED(share->CopyToShared(images[NVR_DLSS_IMAGE_MOTION], 0, 0, In.Motion, &render))) {
			TurnOff("an input could not be copied to the helper (formats or sizes; the DXVK log says more)");
			return false;
		}
		const UInt64 n = ++frame;
		NvrDlssFrame& f = shared->frames[n % NVR_DLSS_FRAME_RING];
		f.frame = 0;
		MemoryBarrier();
		f.renderWidth = In.RenderWidth;
		f.renderHeight = In.RenderHeight;
		f.reset = (In.Reset || dlssNeedsReset) ? 1u : 0u;
		f.hdr = In.Hdr ? 1u : 0u;
		f.jitterX = flipX ? -In.JitterX : In.JitterX;
		f.jitterY = flipY ? -In.JitterY : In.JitterY;
		f.mvScaleX = (float)In.RenderWidth;
		f.mvScaleY = (float)In.RenderHeight;
		f.preExposure = 1.0f;
		f.frameTimeMs = In.DeltaSeconds * 1000.0f;
		f.quality = 0;
		f.flags = 0;
		LARGE_INTEGER written;
		QueryPerformanceCounter(&written);
		f.writtenQpc = written.QuadPart;
		MemoryBarrier();
		f.frame = n;
		MemoryBarrier();
		InterlockedExchange64((volatile LONG64*)&shared->latestFrame, (LONG64)n);
		SetEvent(frameEvent);
		InterlockedExchange64(&lastRecorded, (LONG64)n);
		share->SignalShared(inputsReady, n);
		share->WaitShared(outputReady, n);
		share->CopyFromShared(output.Texture, 0, 0, images[NVR_DLSS_IMAGE_OUTPUT], nullptr);
		dlssNeedsReset = false;
		fsrNeedsReset = true;
		usedSelf = true;
		QueryPerformanceCounter(&t1);
		QueryPerformanceFrequency(&frequency);
		recordUs += (double)(t1.QuadPart - t0.QuadPart) * 1e6 / (double)frequency.QuadPart;
		recordCount++;

		if ((n % 300) == 0 && (reported < 3 || GpuTimer::Enabled || (n % 3600) == 0)) {
			reported++;
			UINT64 done = 0;
			outputReady->GetCompletedValue(&done);
			Logger::Log("DLSS HELPER (%s): %llu frames exchanged, last recorded %llu, back from the helper %llu; helper GPU %.3f ms avg (last %.3f), "
				"helper response %.3f ms avg (max %.3f), %u overwritten, %u DLSS evaluations; NVR records %.3f ms CPU avg; %ux%u -> %ux%u %s",
				passThrough ? "pass-through test" : "DLSS", (unsigned long long)shared->framesSubmitted, (unsigned long long)n, (unsigned long long)done,
				shared->gpuTimeUsAvg / 1000.0, shared->gpuTimeUsLast / 1000.0, shared->responseUsAvg / 1000.0, shared->responseUsMax / 1000.0,
				shared->paramMismatches, shared->dlssEvaluated, recordCount ? recordUs / recordCount / 1000.0 : 0.0,
				In.RenderWidth, In.RenderHeight, In.DisplayWidth, In.DisplayHeight, In.Hdr ? "HDR" : "LDR");
			shared->responseUsMax = 0;
			recordUs = 0.0;
			recordCount = 0;
		}
		return true;
	}

	bool DlssUpscaler::Evaluate(IDirect3DDevice9* Device, const Upscaler::Inputs& In) {
		if (state == Off) return FallBack(Device, In);
		if (!In.Color || !In.Depth || !In.Motion || !In.RenderWidth || !In.RenderHeight || !In.DisplayWidth || !In.DisplayHeight ||
			In.RenderWidth > In.InputWidth || In.RenderHeight > In.InputHeight) return FallBack(Device, In);
		if (state == Idle) {
			Start(In);
			return FallBack(Device, In);
		}
		Poll();
		if (state != Running) return FallBack(Device, In);
		if (In.InputWidth != inputWidth || In.InputHeight != inputHeight || In.DisplayWidth != displayWidth || In.DisplayHeight != displayHeight) {
			Stop("the input or output size changed");
			state = Idle;
			return FallBack(Device, In);
		}
		static GpuTimer timer("  DLSS helper (copies + helper's work + waits)");
		GpuProfileScope gpu(timer, Device);
		if (!Record(Device, In)) return FallBack(Device, In);
		return true;
	}
}

namespace Upscaler {
	IUpscaler* Dlss() { return &TheDlss; }

	void DlssOptions(bool PassThroughTest, int JitterFlip) { TheDlss.SetOptions(PassThroughTest, JitterFlip); }

	IUpscaler* Choose(int setting) {
		static IUpscaler* last = nullptr;
		IUpscaler* chosen;
		if (setting == 1 && !TheDlss.IsOff()) chosen = &TheDlss;
		else {
			TheDlss.MarkUnused();
			chosen = (setting == 2 && !Fsr31()->Failed()) ? Fsr31() : Fsr2();
		}
		if (last && chosen != last && (chosen == Fsr31() || last == Fsr31())) {
			chosen->RequestReset();
			if (last == Fsr2() || last == Fsr31()) last->ReleaseResources();
		}
		last = chosen;
		return chosen;
	}
}
