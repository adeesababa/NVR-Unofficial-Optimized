#pragma once


#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <cmath>
#pragma comment(lib, "ole32.lib")

namespace BodycamAudio {
	struct Settings {
		float Mix = 0.0f;
		float Bits = 0.0f;
		float SampleRate = 0.0f;
		float Compression = 0.0f;
		float GainDb = 0.0f;
		float Tone = 0.0f;
		float Hiss = 0.0f;
	};
	static Settings Wanted;

	struct Format { int Channels = 0; int Rate = 0; int Bits = 0; bool Float = false; };

	struct Biquad {
		float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
		void Set(bool high, float cutoff, float rate) {
			const float w = 6.2831853f * (std::min)(cutoff, rate * 0.45f) / rate, c = cosf(w), s = sinf(w), alpha = s / (2.0f * 0.7071f);
			const float a0 = 1.0f + alpha;
			if (high) { b0 = (1.0f + c) * 0.5f / a0; b1 = -(1.0f + c) / a0; b2 = b0; }
			else { b0 = (1.0f - c) * 0.5f / a0; b1 = (1.0f - c) / a0; b2 = b0; }
			a1 = -2.0f * c / a0; a2 = (1.0f - alpha) / a0;
		}
		float Run(float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
	};

	struct Stream {
		Format Fmt;
		BYTE* Data = nullptr;
		UINT32 Frames = 0;
		Biquad High[2], Low[2];
		float ToneSet = -1.0f;
		float Envelope = 0.0f, Gain = 1.0f, Mix = 0.0f, Held = 0.0f, HoldPhase = 1.0f;
		UINT32 Noise = 22222;
	};

	static std::mutex Lock;
	static std::unordered_map<void*, Format> Clients;
	static std::unordered_map<void*, Stream> Streams;
	static Format MixFormat;
	static bool Installed = false, Tried = false;

	typedef HRESULT(STDMETHODCALLTYPE* InitializeFn)(IAudioClient*, AUDCLNT_SHAREMODE, DWORD, REFERENCE_TIME, REFERENCE_TIME, const WAVEFORMATEX*, LPCGUID);
	typedef HRESULT(STDMETHODCALLTYPE* GetServiceFn)(IAudioClient*, REFIID, void**);
	typedef HRESULT(STDMETHODCALLTYPE* GetBufferFn)(IAudioRenderClient*, UINT32, BYTE**);
	typedef HRESULT(STDMETHODCALLTYPE* ReleaseBufferFn)(IAudioRenderClient*, UINT32, DWORD);
	static InitializeFn RealInitialize = nullptr;
	static GetServiceFn RealGetService = nullptr;
	static GetBufferFn RealGetBuffer = nullptr;
	static ReleaseBufferFn RealReleaseBuffer = nullptr;

	static Format Read(const WAVEFORMATEX* w) {
		Format f;
		if (!w) return f;
		f.Channels = w->nChannels; f.Rate = (int)w->nSamplesPerSec; f.Bits = w->wBitsPerSample;
		f.Float = w->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
		if (w->wFormatTag == WAVE_FORMAT_EXTENSIBLE && w->cbSize >= 22) {
			const WAVEFORMATEXTENSIBLE* x = (const WAVEFORMATEXTENSIBLE*)w;
			f.Float = IsEqualGUID(x->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != 0;
		}
		return f;
	}

	static HRESULT STDMETHODCALLTYPE InitializeHook(IAudioClient* client, AUDCLNT_SHAREMODE mode, DWORD flags, REFERENCE_TIME duration,
		REFERENCE_TIME period, const WAVEFORMATEX* format, LPCGUID session) {
		const HRESULT hr = RealInitialize(client, mode, flags, duration, period, format, session);
		if (SUCCEEDED(hr)) { std::lock_guard<std::mutex> guard(Lock); Clients[client] = Read(format); }
		return hr;
	}

	static HRESULT STDMETHODCALLTYPE GetServiceHook(IAudioClient* client, REFIID riid, void** out) {
		const HRESULT hr = RealGetService(client, riid, out);
		if (SUCCEEDED(hr) && out && *out && IsEqualIID(riid, __uuidof(IAudioRenderClient))) {
			std::lock_guard<std::mutex> guard(Lock);
			auto known = Clients.find(client);
			Streams[*out].Fmt = known != Clients.end() ? known->second : MixFormat;
		}
		return hr;
	}

	static HRESULT STDMETHODCALLTYPE GetBufferHook(IAudioRenderClient* render, UINT32 frames, BYTE** data) {
		const HRESULT hr = RealGetBuffer(render, frames, data);
		if (SUCCEEDED(hr) && data) {
			std::lock_guard<std::mutex> guard(Lock);
			auto it = Streams.find(render);
			if (it == Streams.end()) it = Streams.emplace(render, Stream()).first, it->second.Fmt = MixFormat;
			it->second.Data = *data;
			it->second.Frames = frames;
		}
		return hr;
	}

	static inline float Sample(const BYTE* p, const Format& f) { return f.Float ? *(const float*)p : *(const SHORT*)p / 32768.0f; }
	static inline void Store(BYTE* p, const Format& f, float v) {
		if (f.Float) *(float*)p = v;
		else *(SHORT*)p = (SHORT)(std::max)(-32768.0f, (std::min)(32767.0f, v * 32768.0f));
	}

	static void Process(Stream& s, UINT32 frames) {
		const Format& f = s.Fmt;
		if (!s.Data || f.Channels <= 0 || f.Rate <= 0 || !((f.Float && f.Bits == 32) || (!f.Float && f.Bits == 16))) return;
		const Settings w = Wanted;
		const float target = (std::max)(0.0f, (std::min)(1.0f, w.Mix));
		if (target <= 0.0f && s.Mix <= 0.0f) return;
		const float rate = (float)f.Rate;
		if (fabsf(s.ToneSet - w.Tone) > 1e-3f) {
			const float t = (std::max)(0.0f, (std::min)(1.0f, w.Tone));
			const float lowCut = 60.0f * powf(450.0f / 60.0f, t), highCut = 16000.0f * powf(2800.0f / 16000.0f, t);
			for (int k = 0; k < 2; k++) { s.High[k].Set(true, lowCut, rate); s.Low[k].Set(false, highCut, rate); }
			s.ToneSet = w.Tone;
		}
		const int stride = f.Channels * f.Bits / 8, bytes = f.Bits / 8;
		const float gain = powf(10.0f, w.GainDb / 20.0f);
		const float levels = w.Bits >= 1.0f ? powf(2.0f, (std::max)(2.0f, (std::min)(16.0f, w.Bits)) - 1.0f) : 0.0f;
		const float hold = w.SampleRate >= 500.0f ? (std::min)(1.0f, w.SampleRate / rate) : 1.0f;
		const float attack = 1.0f - expf(-1.0f / (0.003f * rate)), release = 1.0f - expf(-1.0f / (0.25f * rate));
		const float ramp = 1.0f / (0.05f * rate);
		const float compression = (std::max)(0.0f, (std::min)(1.0f, w.Compression));
		for (UINT32 i = 0; i < frames; i++) {
			BYTE* frame = s.Data + (size_t)i * stride;
			float mono = 0.0f;
			for (int c = 0; c < f.Channels; c++) mono += Sample(frame + c * bytes, f);
			mono /= (float)f.Channels;
			float y = mono;
			for (int k = 0; k < 2; k++) y = s.Low[k].Run(s.High[k].Run(y));
			y *= gain;
			const float level = fabsf(y);
			s.Envelope += (level - s.Envelope) * (level > s.Envelope ? attack : release);
			const float wantGain = (std::min)(2.5f, 0.12f / (std::max)(s.Envelope, 0.01f));
			s.Gain += (wantGain - s.Gain) * 0.002f;
			y *= 1.0f + (s.Gain - 1.0f) * compression;
			y = tanhf(y * 1.5f) * 0.8f;
			s.HoldPhase += hold;
			if (s.HoldPhase >= 1.0f) { s.HoldPhase -= 1.0f; s.Held = y; }
			y = s.Held;
			if (levels > 0.0f) y = floorf(y * levels + 0.5f) / levels;
			s.Noise = s.Noise * 1664525u + 1013904223u;
			y += ((s.Noise >> 9) / 8388608.0f - 1.0f) * 0.02f * w.Hiss;
			s.Mix += target > s.Mix ? (std::min)(ramp, target - s.Mix) : (std::max)(-ramp, target - s.Mix);
			for (int c = 0; c < f.Channels; c++) {
				BYTE* p = frame + c * bytes;
				const float dry = Sample(p, f);
				Store(p, f, dry + (y - dry) * s.Mix);
			}
		}
	}

	static HRESULT STDMETHODCALLTYPE ReleaseBufferHook(IAudioRenderClient* render, UINT32 written, DWORD flags) {
		if (written && !(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
			std::lock_guard<std::mutex> guard(Lock);
			auto it = Streams.find(render);
			if (it != Streams.end() && it->second.Data) Process(it->second, (std::min)(written, it->second.Frames));
		}
		return RealReleaseBuffer(render, written, flags);
	}

	static bool Patch(void** table, int index, void* hook, void** real) {
		DWORD old = 0;
		if (!VirtualProtect(&table[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return false;
		if (table[index] != hook) { *real = table[index]; table[index] = hook; }
		VirtualProtect(&table[index], sizeof(void*), old, &old);
		return true;
	}

	static void Install() {
		if (Tried) return;
		Tried = true;
		std::thread worker([] {
			if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return;
			IMMDeviceEnumerator* enumerator = nullptr;
			IMMDevice* device = nullptr;
			IAudioClient* client = nullptr;
			IAudioRenderClient* render = nullptr;
			WAVEFORMATEX* mix = nullptr;
			if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&enumerator)) &&
				SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)) &&
				SUCCEEDED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&client)) &&
				SUCCEEDED(client->GetMixFormat(&mix)) &&
				SUCCEEDED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 10000000, 0, mix, nullptr)) &&
				SUCCEEDED(client->GetService(__uuidof(IAudioRenderClient), (void**)&render))) {
				MixFormat = Read(mix);
				void** clientTable = *(void***)client;
				void** renderTable = *(void***)render;
				Installed = Patch(clientTable, 3, (void*)&InitializeHook, (void**)&RealInitialize) &&
					Patch(clientTable, 14, (void*)&GetServiceHook, (void**)&RealGetService) &&
					Patch(renderTable, 3, (void*)&GetBufferHook, (void**)&RealGetBuffer) &&
					Patch(renderTable, 4, (void*)&ReleaseBufferHook, (void**)&RealReleaseBuffer);
			}
			if (mix) CoTaskMemFree(mix);
			if (render) render->Release();
			if (client) client->Release();
			if (device) device->Release();
			if (enumerator) enumerator->Release();
			CoUninitialize();
		});
		worker.join();
		Logger::Log("UNOFFICIAL Bodycam mic: %s (device mix format %d ch, %d Hz, %d bit%s)", Installed ? "audio filter installed" : "could not hook the audio output",
			MixFormat.Channels, MixFormat.Rate, MixFormat.Bits, MixFormat.Float ? " float" : "");
	}
}
