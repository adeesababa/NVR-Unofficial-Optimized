#pragma once


#include <vector>
#include <string>
#include <initializer_list>
#include "DxvkCompute.h"

namespace DebugReadback {
	inline ID3D9NvrExtReadback* Ext = nullptr;
	inline bool Tried = false;

	inline ID3D9NvrExtReadback* Interface() {
		if (Tried) return Ext;
		Tried = true;
		ID3D9NvrExt* ext = DxvkCompute::Extension();
		if (!ext || !DxvkCompute::Available()) {
			Logger::Log("UNOFFICIAL debugging tools: need NVR DXVK (the NVR extension is not present: native D3D9 or stock DXVK); nothing is done.");
			return nullptr;
		}
		if (FAILED(ext->QueryInterface(__uuidof(ID3D9NvrExtReadback), (void**)&Ext)) || !Ext) {
			Ext = nullptr;
			Logger::Log("UNOFFICIAL debugging tools: need NVR DXVK 1.5-debug (fork patch 0016, non-waiting readback); this d3d9.dll has extension %u.%u without it: nothing is done.",
				DxvkCompute::Info().ExtVersionMajor, DxvkCompute::Info().ExtVersionMinor);
			return nullptr;
		}
		Logger::Log("UNOFFICIAL debugging tools: NVR DXVK 1.5-debug readback available (never waits).");
		return Ext;
	}
}

class DebugReadbackRing {
public:
	static const int Slots = 4;

	bool Ready() const { return Size != 0; }

	bool Init(UINT size, const char* name) {
		if (Size == size) return true;
		Release();
		ID3D9NvrExtReadback* rb = DebugReadback::Interface();
		if (!rb) return false;
		for (int i = 0; i < Slots; i++) {
			if (FAILED(rb->CreateReadbackBuffer(size, name, &Buffers[i])) || !Buffers[i]) {
				Logger::Log("UNOFFICIAL debugging tools: CreateReadbackBuffer(%s, %u bytes) failed", name ? name : "", size);
				Release();
				return false;
			}
		}
		Size = size;
		return true;
	}

	int Submit(ID3D9NvrExtBuffer* Source, UINT Bytes) {
		ID3D9NvrExtReadback* rb = DebugReadback::Interface();
		if (!rb || !Size || !Source || Bytes > Size) return -1;
		if (Count >= Slots) { Skipped++; return -1; }
		const int slot = (Oldest + Count) % Slots;
		if (FAILED(rb->CopyToReadback(Buffers[slot], 0, Source, 0, Bytes))) return -1;
		Count++;
		return slot;
	}

	bool Poll(void* Out, UINT Bytes, int& slot) {
		ID3D9NvrExtReadback* rb = DebugReadback::Interface();
		if (!rb || !Count || Bytes > Size) return false;
		const HRESULT result = rb->TryReadback(Buffers[Oldest], 0, Bytes, Out);
		if (result == S_FALSE) return false;
		slot = Oldest;
		Oldest = (Oldest + 1) % Slots;
		Count--;
		return result == S_OK;
	}

	void Release() {
		for (int i = 0; i < Slots; i++) DxvkCompute::Drop(Buffers[i]);
		Size = 0; Oldest = 0; Count = 0;
	}

	UINT Skipped = 0;

private:
	ID3D9NvrExtBuffer*	Buffers[Slots] = {};
	UINT				Size = 0;
	int					Oldest = 0, Count = 0;
};

class ShaderCounters {
public:
	static const UINT MaxCounters = 32, MaxProbes = 8;
	struct Block { UINT header[4]; UINT count[MaxCounters]; float probe[MaxProbes][4]; };
	static_assert(sizeof(Block) == 272, "Counters.glsl layout");

	ShaderCounters(const char* Name, std::initializer_list<const char*> Counters, std::initializer_list<const char*> Probes)
		: name(Name), counterNames(Counters), probeNames(Probes) { All().push_back(this); }

	static NVR_EXT_BINDING_DESC Binding(UINT Slot, UINT Set = 3, UINT BindingIndex = 0) {
		NVR_EXT_BINDING_DESC b = { Set, BindingIndex, NVR_EXT_BIND_STORAGE_BUFFER, Slot, NVR_EXT_VIEW_2D, TRUE, 0 };
		return b;
	}
	static NVR_EXT_RESOURCE_BINDING Bind(UINT Slot, ID3D9NvrExtBuffer* Buffer) {
		NVR_EXT_RESOURCE_BINDING r = {};
		r.Slot = Slot; r.Type = NVR_EXT_BIND_STORAGE_BUFFER; r.pResource = Buffer;
		return r;
	}

	ID3D9NvrExtBuffer* Begin(int ProbeX, int ProbeY) {
		if (failed) return nullptr;
		if (!buffer) {
			if (!DebugReadback::Interface()) { failed = true; return nullptr; }
			buffer = DxvkCompute::CreateBufferNoFail(sizeof(Block), NVR_EXT_BUFFER_STORAGE | NVR_EXT_BUFFER_TRANSFER_SRC | NVR_EXT_BUFFER_TRANSFER_DST, name);
			if (!buffer || !ring.Init(sizeof(Block), name)) {
				Logger::Log("UNOFFICIAL SHADER COUNTERS %s: could not be set up; off for the session.", name);
				failed = true;
				DxvkCompute::Drop(buffer);
				return nullptr;
			}
			Logger::Log("UNOFFICIAL SHADER COUNTERS %s: on (read back without waiting, logged once a second).", name);
		}
		Collect();
		Block reset = {};
		reset.header[0] = ProbeX >= 0 ? (UINT)ProbeX : 0xFFFFFFFFu;
		reset.header[1] = ProbeY >= 0 ? (UINT)ProbeY : 0xFFFFFFFFu;
		reset.header[2] = ++frame;
		probeX = ProbeX; probeY = ProbeY;
		if (FAILED(DxvkCompute::Extension()->UpdateBuffer(buffer, 0, sizeof(Block), &reset))) return nullptr;
		begun = true;
		return buffer;
	}

	void End() {
		if (!begun) return;
		begun = false;
		ring.Submit(buffer, sizeof(Block));
	}

	const std::string& Summary() const { return summary; }
	const char* Name() const { return name; }
	static std::vector<ShaderCounters*>& All() { static std::vector<ShaderCounters*> all; return all; }

private:
	void Collect() {
		Block block;
		int slot = 0;
		while (ring.Poll(&block, sizeof(Block), slot)) {
			for (UINT i = 0; i < MaxCounters; i++) sums[i] += block.count[i];
			memcpy(lastProbe, block.probe, sizeof(lastProbe));
			lastProbeX = (int)block.header[0]; lastProbeY = (int)block.header[1];
			framesRead++;
		}
		const ULONGLONG now = GetTickCount64();
		if (!windowStart) windowStart = now;
		if (now - windowStart < 1000 || !framesRead) return;
		char line[2048];
		int n = _snprintf_s(line, sizeof(line), _TRUNCATE, "%u frames read (%u skipped, readback ring full)", framesRead, ring.Skipped);
		for (size_t i = 0; i < counterNames.size() && i < MaxCounters && n > 0 && n < (int)sizeof(line); i++)
			n += _snprintf_s(line + n, sizeof(line) - n, _TRUNCATE, " | %s %.1f", counterNames[i], (double)sums[i] / framesRead);
		if (lastProbeX != -1 && n > 0 && n < (int)sizeof(line)) {
			n += _snprintf_s(line + n, sizeof(line) - n, _TRUNCATE, " || probe pixel (%d, %d):", lastProbeX, lastProbeY);
			for (size_t j = 0; j < probeNames.size() && j < MaxProbes && n > 0 && n < (int)sizeof(line); j++)
				n += _snprintf_s(line + n, sizeof(line) - n, _TRUNCATE, " %s (%.4g %.4g %.4g %.4g)", probeNames[j], lastProbe[j][0], lastProbe[j][1], lastProbe[j][2], lastProbe[j][3]);
		}
		summary = line;
		Logger::Log("SHADER COUNTERS %s (per frame): %s", name, line);
		memset(sums, 0, sizeof(sums));
		framesRead = 0;
		ring.Skipped = 0;
		windowStart = now;
	}

	const char*					name;
	std::vector<const char*>	counterNames, probeNames;
	ID3D9NvrExtBuffer*			buffer = nullptr;
	DebugReadbackRing			ring;
	bool						failed = false, begun = false;
	UINT						frame = 0, framesRead = 0;
	unsigned long long			sums[MaxCounters] = {};
	float						lastProbe[MaxProbes][4] = {};
	int							probeX = -1, probeY = -1, lastProbeX = -1, lastProbeY = -1;
	ULONGLONG					windowStart = 0;
	std::string					summary;
};
