#pragma once

// UNOFFICIAL 2026-10-26 (NVR DXVK extension 1.5-hdr, fork patch 0007): true HDR output on an HDR display.
//
// [Shaders.HDR.Main] Enabled (default off; a settings-only menu entry like Cloth / CloudShadows). Needs the NVR DXVK d3d9.dll with ID3D9NvrExtHdr; on native D3D9, stock DXVK or an
// older fork nothing here runs and the picture is exactly the old one.
//
// How: the game's D3D9 back buffer stays 8-bit SDR (the HUD, menus and everything after NVR draw into it as always). The fork
// creates the Vulkan swap chain as HDR10 (PQ, 10-bit; Mode 1) or scRGB (FP16 linear; Mode 2) and converts at present:
// the SDR picture is decoded to linear light and put at PaperWhiteNits; where it still equals NVR's finished picture (marked
// here after NVR's last effect, i.e. no HUD/menu drew over it) each pixel is multiplied by a highlight gain (HdrGain.frag,
// computed here from the scene before tonemapping and the tonemapper's SDR result): 1 for shadows and mid-tones (the SDR look,
// unchanged), rising towards PeakNits / PaperWhiteNits for what the SDR tonemapper squeezed into white (sun, sky near the sun,
// lamps, fire, specular glints). HUD and menus therefore stay at paper white, never blinding.
//
// Display check: the output must be in HDR mode in Windows (DXGI colour space ST.2084), else the output stays SDR automatically
// (logged); IgnoreDisplayCheck forces it (tests on an SDR desktop: Windows then maps the HDR signal down itself).
//
// Test tool: CaptureFrame (a counter: each new non-zero value) writes, 60 frames later, a frame's HDR output, as the fork sends it (scRGB,
// 1.0 = 80 nits), to NVR-Unofficial-Optimized-Logs\NeonSelfTest_HdrCapture_<n>.pfm.world (a PFM: RGB float, bottom row first;
// the name lets the slot test runner collect it with the pictures) with a summary line in the log.

#include <dxgi1_6.h>
#include <vector>
#include <cstdio>
#include "DxvkCompute.h"
#include "../effects/Tonemapping.h"

namespace HdrOutput {

	struct State {
		ID3D9NvrExtHdr*			Hdr = nullptr;		// queried once
		bool					Queried = false;
		bool					Active = false;		// a HDR mode is set in the fork
		NVR_EXT_HDR_OUTPUT		Applied = {};
		bool					Refused = false;	// the fork refused the mode (logged once until the settings change)
		int						DisplayHdr = -1;	// -1 unknown, 0 SDR desktop, 1 Windows HDR on (ST.2084)
		float					DisplayMaxNits = 0.0f;
		unsigned				Frame = 0;
		char					LastReason[160] = "";
		ID3D9NvrExtPipeline*	GainPipeline = nullptr;
		bool					GainTried = false;
		IDirect3DTexture9*		Gain = nullptr;
		IDirect3DSurface9*		GainSurface = nullptr;
		UINT					GainWidth = 0, GainHeight = 0;
		bool					GainReady = false;	// this frame's gain was drawn
		IDirect3DBaseTexture9*	Scene = nullptr;	// this frame's HDR scene before tonemapping (not owned)
		int						CaptureSetting = 0;
		bool					CapturePending = false;
		unsigned				CaptureCount = 0;
		unsigned				CaptureAt = 0;		// frame at which the capture is asked for (60 frames after the setting changed)
	};
	inline State S;

	inline void Reason(const char* text) {
		if (strcmp(S.LastReason, text)) { strncpy_s(S.LastReason, text, _TRUNCATE); Logger::Log("UNOFFICIAL HDR output: %s", text); }
	}

	// Windows' HDR switch for the monitor showing the game window (IDXGIOutput6::GetDesc1). dxgi.dll is loaded on demand.
	inline void CheckDisplay(IDirect3DDevice9* Device) {
		D3DDEVICE_CREATION_PARAMETERS cp = {};
		HMONITOR monitor = nullptr;
		if (SUCCEEDED(Device->GetCreationParameters(&cp)) && cp.hFocusWindow) monitor = MonitorFromWindow(cp.hFocusWindow, MONITOR_DEFAULTTONEAREST);
		typedef HRESULT(WINAPI* CreateFactory)(REFIID, void**);
		static CreateFactory create = (CreateFactory)GetProcAddress(LoadLibraryA("dxgi.dll"), "CreateDXGIFactory1");
		int hdr = 0; float maxNits = 0.0f;
		IDXGIFactory1* factory = nullptr;
		if (create && SUCCEEDED(create(__uuidof(IDXGIFactory1), (void**)&factory))) {
			IDXGIAdapter1* adapter = nullptr;
			for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; a++) {
				IDXGIOutput* output = nullptr;
				for (UINT o = 0; adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; o++) {
					IDXGIOutput6* output6 = nullptr;
					if (SUCCEEDED(output->QueryInterface(__uuidof(IDXGIOutput6), (void**)&output6))) {
						DXGI_OUTPUT_DESC1 desc = {};
						if (SUCCEEDED(output6->GetDesc1(&desc)) && (!monitor || desc.Monitor == monitor)) {
							if (desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) hdr = 1;
							if (desc.MaxLuminance > maxNits) maxNits = desc.MaxLuminance;
						}
						output6->Release();
					}
					output->Release();
				}
				adapter->Release();
			}
			factory->Release();
		}
		if (hdr != S.DisplayHdr) Logger::Log("UNOFFICIAL HDR output: display %s (Windows HDR %s), display peak %.0f nits", hdr ? "in HDR mode" : "in SDR mode", hdr ? "on" : "off", maxNits);
		S.DisplayHdr = hdr;
		S.DisplayMaxNits = maxNits;
	}

	inline bool Setting(const char* key) { return TheSettingManager->GetSettingI("Shaders.HDR.Main", key) != 0; }
	inline float SettingF(const char* key) { return TheSettingManager->GetSettingF("Shaders.HDR.Main", key); }

	inline void SetOff() {
		if (S.Active && S.Hdr) {
			NVR_EXT_HDR_OUTPUT off = {};
			S.Hdr->SetHdrOutput(&off);
		}
		S.Active = false;
		S.Applied = {};
	}

	// Every rendered frame, before NVR's post chain (Render.cpp): reads the settings, keeps the fork's mode in step.
	inline void BeginFrame(IDirect3DDevice9* Device, IDirect3DBaseTexture9* Scene) {
		S.Scene = Scene;
		S.GainReady = false;
		S.Frame++;
		const bool enabled = Setting("Enabled");
		if (!enabled) { if (S.Active) { SetOff(); Reason("off"); } return; }
		if (!S.Queried) {
			S.Queried = true;
			if (DxvkCompute::Extension())
				DxvkCompute::Extension()->QueryInterface(__uuidof(ID3D9NvrExtHdr), (void**)&S.Hdr);
		}
		if (!S.Hdr) { Reason("Enabled, but this d3d9.dll has no HDR output (needs NVR DXVK with extension 1.5-hdr): SDR as before"); return; }
		if (S.DisplayHdr < 0 || S.Frame % 600 == 0) CheckDisplay(Device);
		if (!S.DisplayHdr && !Setting("IgnoreDisplayCheck")) {
			if (S.Active) SetOff();
			Reason("Enabled, but Windows HDR is off for this display: SDR as before (turn HDR on in Windows' display settings)");
			return;
		}

		// The fork's view (EDID; DXVK parses it on every call, so once per session)
		static NVR_EXT_HDR_INFO info = {};
		static bool infoRead = false;
		if (!infoRead) {
			infoRead = true;
			S.Hdr->GetHdrInfo(&info);
			Logger::Log("UNOFFICIAL HDR output: the fork's surface offers HDR10 %s, scRGB %s; EDID peak %.0f nits", info.SupportsHdr10 ? "yes" : "no", info.SupportsScRgb ? "yes" : "no", info.DisplayMaxNits);
		}
		NVR_EXT_HDR_OUTPUT out = {};
		out.Mode = TheSettingManager->GetSettingI("Shaders.HDR.Main", "Mode") == 2 ? NVR_EXT_HDR_SCRGB : NVR_EXT_HDR_HDR10;
		out.PaperWhiteNits = std::clamp(SettingF("PaperWhiteNits"), 80.0f, 1000.0f);
		float peak = SettingF("PeakNits");
		if (peak <= 0.0f) peak = S.DisplayMaxNits > 0.0f ? S.DisplayMaxNits : (info.DisplayMaxNits > 0.0f ? info.DisplayMaxNits : 1000.0f);
		out.PeakNits = std::clamp(peak, out.PaperWhiteNits, 10000.0f);
		out.MaxFrameAverageNits = out.PaperWhiteNits;
		out.HudThreshold = std::clamp(SettingF("HudThreshold"), 0.0f, 255.0f) / 255.0f;
		out.Flags = (Setting("ShowHudMask") ? NVR_EXT_HDR_FLAG_SHOW_HUD_MASK : 0) | (Setting("Gamma22") ? NVR_EXT_HDR_FLAG_GAMMA22 : 0);

		if (!S.Active || memcmp(&out, &S.Applied, sizeof(out))) {
			if (S.Refused && !memcmp(&out, &S.Applied, sizeof(out))) return;
			S.Applied = out;
			const HRESULT hr = S.Hdr->SetHdrOutput(&out);
			S.Refused = FAILED(hr);
			S.Active = SUCCEEDED(hr);
			char text[160];
			if (S.Active) sprintf_s(text, "on: %s, paper white %.0f nits, peak %.0f nits%s", out.Mode == NVR_EXT_HDR_SCRGB ? "scRGB" : "HDR10", out.PaperWhiteNits, out.PeakNits, S.DisplayHdr ? "" : " (display check ignored: Windows HDR is off)");
			else sprintf_s(text, "the fork refused %s (%08lX): SDR as before", out.Mode == NVR_EXT_HDR_SCRGB ? "scRGB" : "HDR10", hr);
			Reason(text);
		}
	}

	// The gain pass (HdrGain.frag) into an R16F texture of the SDR picture's size. Sdr: the tonemapper's output (the game target
	// at the start of NVR's post chain).
	inline void RenderGain(IDirect3DDevice9* Device, IDirect3DSurface9* Sdr) {
		if (!S.Active || !S.Scene || !Sdr || !DxvkCompute::Available()) return;
		D3DSURFACE_DESC desc = {};
		if (FAILED(Sdr->GetDesc(&desc))) return;
		if (!S.Gain || S.GainWidth != desc.Width || S.GainHeight != desc.Height) {
			DxvkCompute::Drop(S.GainSurface); DxvkCompute::Drop(S.Gain);
			if (FAILED(Device->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R16F, D3DPOOL_DEFAULT, &S.Gain, nullptr)) || !S.Gain) { Reason("could not create the gain texture: SDR picture only"); return; }
			S.Gain->GetSurfaceLevel(0, &S.GainSurface);
			S.GainWidth = desc.Width; S.GainHeight = desc.Height;
		}
		if (!S.GainTried) {
			S.GainTried = true;
			if (!DxvkCompute::HasSpirv("HdrGain")) { Reason("Compute\\HdrGain.spv is missing: the HDR output shows the SDR picture at paper white"); return; }
			NVR_EXT_BINDING_DESC bindings[3] = {};
			bindings[0].Set = 1; bindings[0].Binding = 0; bindings[0].Type = NVR_EXT_BIND_SAMPLED_IMAGE; bindings[0].Slot = 0;
			bindings[1].Set = 1; bindings[1].Binding = 1; bindings[1].Type = NVR_EXT_BIND_SAMPLED_IMAGE; bindings[1].Slot = 1;
			bindings[2].Type = NVR_EXT_BIND_SAMPLER; bindings[2].Slot = 0; bindings[2].PushOffset = 32;
			S.GainPipeline = DxvkCompute::CreateGraphicsPipeline("HdrGain", bindings, 3, 36, true);
		}
		ID3D9NvrExtSampler* sampler = DxvkCompute::PointClamp();
		if (!S.GainPipeline || !sampler) return;

		struct { float peak, knee, strength, sceneGamma, exposure, outGamma, pad0, pad1; UINT sampler; } push = {};
		push.peak = (std::max)(1.0f, S.Applied.PeakNits / S.Applied.PaperWhiteNits);
		push.knee = std::clamp(SettingF("HighlightKnee"), 0.0f, 0.99f);
		push.strength = std::clamp(SettingF("HighlightStrength"), 0.0f, 1.0f);
		const D3DXVECTOR4 toneMapping = TheShaderManager->Shaders.Tonemapping ? TheShaderManager->Shaders.Tonemapping->Constants.ToneMapping : D3DXVECTOR4(0, 0, 0, 2.2f);
		const D3DXVECTOR4 hdrData = TheShaderManager->Shaders.Tonemapping ? TheShaderManager->Shaders.Tonemapping->Constants.HDRData : D3DXVECTOR4(0, 1, 1, 2.2f);
		const bool gammaSpace = toneMapping.w < 0.0f || hdrData.w < 0.0f;	// the tonemapper's gamma-space path: sRGB curves
		push.sceneGamma = gammaSpace ? 0.0f : toneMapping.w;
		push.exposure = hdrData.y;
		push.outGamma = gammaSpace ? 0.0f : hdrData.w;

		NVR_EXT_RESOURCE_BINDING resources[3] = {};
		resources[0].Slot = 0; resources[0].Type = NVR_EXT_BIND_SAMPLED_IMAGE; resources[0].pResource = S.Scene;
		resources[1].Slot = 1; resources[1].Type = NVR_EXT_BIND_SAMPLED_IMAGE; resources[1].pResource = Sdr;
		resources[2].Slot = 0; resources[2].Type = NVR_EXT_BIND_SAMPLER; resources[2].pResource = sampler;
		NVR_EXT_DRAW_DESC draw = {};
		draw.pPipeline = S.GainPipeline;
		draw.TargetCount = 1;
		draw.Targets[0].pResource = S.GainSurface;
		draw.BindingCount = 3;
		draw.pBindings = resources;
		draw.pPushData = &push;
		draw.PushDataSize = sizeof(push);
		DxvkCompute::PreLoad(resources, 3);	// UNOFFICIAL 2026-10-27 (merge with the alflicker fix): managed textures on the GPU (DxvkCompute.h)
		const HRESULT hr = DxvkCompute::Extension()->Draw(&draw);
		if (FAILED(hr)) { Reason("the gain pass failed (DXVK's log says why): SDR picture at paper white"); DxvkCompute::Drop(S.GainPipeline); return; }
		S.GainReady = true;
	}

	// After NVR's last effect (and ReShade's, which draws before the HUD), before the HUD: the fork copies the picture and the gain.
	inline void MarkScene(IDirect3DSurface9* Finished) {
		if (!S.Active || !S.GainReady || !Finished) return;
		if (FAILED(S.Hdr->MarkHdrScene(Finished, S.GainSurface))) Reason("MarkHdrScene failed (DXVK's log says why): HUD-free pixels shown without highlight gain");
	}

	// Test tool, once a frame (Render.cpp, after MarkScene): CaptureFrame changed -> ask the fork; the frame after -> read and save.
	inline void Capture() {
		if (!S.Hdr) return;
		if (S.CapturePending) {
			S.CapturePending = false;
			UINT w = 0, h = 0;
			if (S.Hdr->ReadHdrCapture(&w, &h, nullptr, 0) != S_OK || !w || !h) { Logger::Log("UNOFFICIAL HDR output: capture: nothing captured"); return; }
			std::vector<unsigned short> half((size_t)w * h * 4);
			if (FAILED(S.Hdr->ReadHdrCapture(&w, &h, half.data(), (UINT)(half.size() * 2)))) { Logger::Log("UNOFFICIAL HDR output: capture: read failed"); return; }
			auto toFloat = [](unsigned short v) {
				const unsigned sign = (v >> 15) & 1, exp = (v >> 10) & 31, man = v & 1023;
				float f = exp == 0 ? ldexpf((float)man, -24) : exp == 31 ? 65504.0f : ldexpf((float)(man | 1024), (int)exp - 25);
				return sign ? -f : f;
			};
			char path[MAX_PATH];
			S.CaptureCount++;
			// NeonSelfTest_*.world: the name the slot test runner moves to the run's picture folder (multirun run_slot.sh move_world)
			sprintf_s(path, ".\\NVR-Unofficial-Optimized-Logs\\NeonSelfTest_HdrCapture_%u.pfm.world", (unsigned)S.CaptureSetting);
			FILE* file = nullptr;
			double sum = 0.0; float peak = 0.0f; size_t above = 0, total = (size_t)w * h;
			std::vector<float> row((size_t)w * 3);
			if (!fopen_s(&file, path, "wb") && file) {
				fprintf(file, "PF\n%u %u\n-1.0\n", w, h);
				for (int y = (int)h - 1; y >= 0; y--) {
					for (UINT x = 0; x < w; x++) for (int c = 0; c < 3; c++) row[x * 3 + c] = toFloat(half[((size_t)y * w + x) * 4 + c]) * 80.0f;
					fwrite(row.data(), sizeof(float), row.size(), file);
				}
				fclose(file);
			}
			for (size_t i = 0; i < total; i++) {
				const float y = (0.2126f * toFloat(half[i * 4]) + 0.7152f * toFloat(half[i * 4 + 1]) + 0.0722f * toFloat(half[i * 4 + 2])) * 80.0f;
				sum += y; peak = (std::max)(peak, y);
				if (y > S.Applied.PaperWhiteNits * 1.02f) above++;
			}
			Logger::Log("UNOFFICIAL HDR output: capture %u (%ux%u, nits, linear): mean %.1f, max %.1f, %.3f %% of pixels above paper white %.0f -> %s",
				(unsigned)S.CaptureSetting, w, h, sum / total, peak, 100.0 * above / total, S.Applied.PaperWhiteNits, path);
			return;
		}
		const int setting = TheSettingManager->GetSettingI("Shaders.HDR.Main", "CaptureFrame");
		if (setting != 0 && setting != S.CaptureSetting) { S.CaptureSetting = setting; S.CaptureAt = S.Frame + 60; }
		else if (setting == 0) { S.CaptureSetting = 0; S.CaptureAt = 0; }
		if (S.CaptureAt && S.Frame >= S.CaptureAt) {
			S.CaptureAt = 0;
			if (SUCCEEDED(S.Hdr->RequestHdrCapture())) S.CapturePending = true;
		}
	}
}
