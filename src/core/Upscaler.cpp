
#include <algorithm>
#include <cmath>

#include "Upscaler.h"
#include "DxvkCompute.h"
#include "GpuProfiler.h"

namespace {
	struct FSRConstants {
		INT32	renderSize[2];
		INT32	maxRenderSize[2];
		INT32	displaySize[2];
		INT32	inputColorResourceDimensions[2];
		INT32	lumaMipDimensions[2];
		INT32	lumaMipLevelToUse;
		INT32	frameIndex;
		float	deviceToViewDepth[4];
		float	jitterOffset[2];
		float	motionVectorScale[2];
		float	downscaleFactor[2];
		float	motionVectorJitterCancellation[2];
		float	preExposure;
		float	previousFramePreExposure;
		float	tanHalfFOV;
		float	jitterPhaseCount;
		float	deltaTime;
		float	dynamicResChangeFactor;
		float	viewSpaceToMetersFactor;
		float	pad;
	};
	static_assert(sizeof(FSRConstants) == 128, "cbFSR2 layout");

	struct FSRPush { UInt32 pointIndex, linearIndex, pad0, pad1; UInt32 rcas[4]; };

#define IMG(b)      { 1, b, NVR_EXT_BIND_SAMPLED_IMAGE, b, NVR_EXT_VIEW_2D, FALSE, 0 }
#define UAV(b)      { 1, b, NVR_EXT_BIND_STORAGE_IMAGE, b, NVR_EXT_VIEW_2D, TRUE, 0 }
#define CB(s, b)    { s, b, NVR_EXT_BIND_UNIFORM_BUFFER, 0, NVR_EXT_VIEW_2D, FALSE, 0 }
#define DEPTHBUF(b, w) { 1, b, NVR_EXT_BIND_STORAGE_BUFFER, 1, NVR_EXT_VIEW_2D, w, 0 }
#define POINTS      { 0, 0, NVR_EXT_BIND_SAMPLER, 0, NVR_EXT_VIEW_2D, FALSE, 0 }
#define LINEARS     { 0, 0, NVR_EXT_BIND_SAMPLER, 1, NVR_EXT_VIEW_2D, FALSE, 4 }
	const NVR_EXT_BINDING_DESC LumaBindings[] = { IMG(0), UAV(1), POINTS };
	const NVR_EXT_BINDING_DESC ReconstructBindings[] = { IMG(0), IMG(1), IMG(2), DEPTHBUF(5, TRUE), UAV(6), UAV(7), UAV(11), CB(1, 12) };
	const NVR_EXT_BINDING_DESC DepthClipBindings[] = { DEPTHBUF(0, FALSE), IMG(1), IMG(2), IMG(6), IMG(7), IMG(8), IMG(9), UAV(12), UAV(13), CB(1, 14), POINTS, LINEARS };
	const NVR_EXT_BINDING_DESC LockBindings[] = { IMG(0), UAV(1), DEPTHBUF(2, TRUE), CB(1, 3) };
	const NVR_EXT_BINDING_DESC AccumulateBindings[] = { IMG(1), IMG(2), IMG(3), IMG(4), IMG(6), IMG(10), IMG(12), UAV(13), UAV(14), UAV(15), UAV(16), UAV(17), CB(1, 18), POINTS, LINEARS };
	const NVR_EXT_BINDING_DESC AccumulateSharpenBindings[] = { IMG(1), IMG(2), IMG(3), IMG(4), IMG(6), IMG(10), IMG(12), UAV(13), UAV(14), UAV(16), UAV(17), CB(1, 18), POINTS, LINEARS };
	const NVR_EXT_BINDING_DESC RCASBindings[] = { IMG(1), UAV(2), CB(1, 3) };
#undef IMG
#undef UAV
#undef CB
#undef DEPTHBUF
#undef POINTS
#undef LINEARS

	enum { LumaMip, Reconstruct, DepthClip, Lock, Accumulate, AccumulateSharpen, RCAS, PipelineCount };
	struct PipelineInfo { const char* name; const char* hdrName; const NVR_EXT_BINDING_DESC* bindings; UINT count; UINT push; bool samplers; };
	const PipelineInfo Pipelines[PipelineCount] = {
		{ "FSRLumaMip",            nullptr,                      LumaBindings,              _countof(LumaBindings),              16, true  },
		{ "FSR2Reconstruct",       "FSR2ReconstructHdr",         ReconstructBindings,       _countof(ReconstructBindings),       0,  false },
		{ "FSR2DepthClip",         nullptr,                      DepthClipBindings,         _countof(DepthClipBindings),         32, true  },
		{ "FSR2Lock",              nullptr,                      LockBindings,              _countof(LockBindings),              0,  false },
		{ "FSR2Accumulate",        "FSR2AccumulateHdr",          AccumulateBindings,        _countof(AccumulateBindings),        32, true  },
		{ "FSR2AccumulateSharpen", "FSR2AccumulateSharpenHdr",   AccumulateSharpenBindings, _countof(AccumulateSharpenBindings), 32, true  },
		{ "FSR2RCAS",              nullptr,                      RCASBindings,              _countof(RCASBindings),              32, false },
	};

	NVR_EXT_RESOURCE_BINDING Image(UINT slot, IUnknown* resource, bool storage = false) {
		NVR_EXT_RESOURCE_BINDING b = {};
		b.Slot = slot; b.Type = storage ? NVR_EXT_BIND_STORAGE_IMAGE : NVR_EXT_BIND_SAMPLED_IMAGE; b.pResource = resource;
		return b;
	}
	NVR_EXT_RESOURCE_BINDING Buffer(UINT slot, NVR_EXT_BIND_TYPE type, ID3D9NvrExtBuffer* buffer) {
		NVR_EXT_RESOURCE_BINDING b = {};
		b.Slot = slot; b.Type = type; b.pResource = buffer;
		return b;
	}
	NVR_EXT_RESOURCE_BINDING Sampler(UINT slot, ID3D9NvrExtSampler* sampler) {
		NVR_EXT_RESOURCE_BINDING b = {};
		b.Slot = slot; b.Type = NVR_EXT_BIND_SAMPLER; b.pResource = sampler;
		return b;
	}

	UInt32 HalfBits(float value) {
		UInt32 f = *(UInt32*)&value;
		UInt32 sign = (f >> 16) & 0x8000u;
		int exponent = (int)((f >> 23) & 0xFF) - 127 + 15;
		UInt32 mantissa = f & 0x7FFFFFu;
		if (exponent <= 0) return sign;
		if (exponent >= 31) return sign | 0x7C00u;
		return sign | ((UInt32)exponent << 10) | (mantissa >> 13);
	}

	class Fsr2Upscaler : public Upscaler::IUpscaler {
	public:
		const char*			Name() const override { return "FSR 2.2.1"; }
		IDirect3DTexture9*	Output() const override { return output.Texture; }
		bool				Failed() const override { return failed; }
		bool				Evaluate(IDirect3DDevice9* Device, const Upscaler::Inputs& In) override;
		void				ReleaseResources() override;

	private:
		struct Tex { IDirect3DTexture9* Texture = nullptr; IDirect3DSurface9* Surface = nullptr; };
		ID3D9NvrExtPipeline*	pipelines[PipelineCount] = {};
		ID3D9NvrExtPipeline*	hdrPipelines[PipelineCount] = {};
		bool		tried = false;
		bool		hdrTried = false;
		bool		failed = false;
		ID3D9NvrExtBuffer*	constants = nullptr;
		bool		constantsTried = false;
		ID3D9NvrExtBuffer*	reconstructedDepth = nullptr;
		Tex			lumaMip, dilatedMotion[2], dilatedDepth, lockLuma, newLocks, prepared, lumaHistory[2], reactiveMasks, upscaled[2],
					lockStatus[2], output;
		UINT		inputWidth = 0, inputHeight = 0, displayWidth = 0, displayHeight = 0;
		int			parity = 0;
		int			frameIndex = 0;

		bool	Fail(const char* why) {
			Logger::Log("[ERROR] UNOFFICIAL NVR DXVK: FSR 2: %s; FSR is off for this session (TAA's own resolve / native resolution runs)", why);
			failed = true;
			return false;
		}
		bool	MakePipelines(bool hdr);
		bool	EnsureResources(UINT inW, UINT inH, UINT outW, UINT outH);
		bool	Dispatch(ID3D9NvrExtPipeline* pipeline, int index, const NVR_EXT_RESOURCE_BINDING* bindings, UINT count, const void* push, UINT pushSize, UINT groupsX, UINT groupsY);
	};

	void Fsr2Upscaler::ReleaseResources() {
		Tex* textures[] = { &lumaMip, &dilatedMotion[0], &dilatedMotion[1], &dilatedDepth, &lockLuma, &newLocks, &prepared, &lumaHistory[0],
			&lumaHistory[1], &reactiveMasks, &upscaled[0], &upscaled[1], &lockStatus[0], &lockStatus[1], &output };
		for (Tex* t : textures) {
			if (t->Surface) { t->Surface->Release(); t->Surface = nullptr; }
			if (t->Texture) { t->Texture->Release(); t->Texture = nullptr; }
		}
		DxvkCompute::Drop(reconstructedDepth);
		inputWidth = inputHeight = displayWidth = displayHeight = 0;
	}

	bool Fsr2Upscaler::MakePipelines(bool hdr) {
		if (!hdr) {
			if (tried) return pipelines[RCAS] != nullptr;
			tried = true;
			for (int i = 0; i < PipelineCount; i++) {
				const PipelineInfo& p = Pipelines[i];
				pipelines[i] = DxvkCompute::CreatePipeline(p.name, p.bindings, p.count, p.push, p.samplers);
				if (!pipelines[i]) return Fail("a compute pipeline could not be made (Compute\\FSR*.spv missing or refused)");
			}
			return true;
		}
		if (hdrTried) return hdrPipelines[Accumulate] != nullptr;
		hdrTried = true;
		for (int i = 0; i < PipelineCount; i++) {
			const PipelineInfo& p = Pipelines[i];
			if (!p.hdrName) continue;
			if (!DxvkCompute::HasSpirv(p.hdrName)) {
				Logger::Log("UNOFFICIAL NVR DXVK: FSR 2: Compute\\%s.spv is missing; its LDR build runs on the HDR picture instead (bright edges alias more).", p.hdrName);
				hdrPipelines[i] = pipelines[i];
				continue;
			}
			hdrPipelines[i] = DxvkCompute::CreatePipeline(p.hdrName, p.bindings, p.count, p.push, p.samplers);
			if (!hdrPipelines[i]) return Fail("an HDR compute pipeline could not be made (Compute\\FSR2*Hdr.spv refused)");
		}
		return true;
	}

	bool Fsr2Upscaler::EnsureResources(UINT inW, UINT inH, UINT outW, UINT outH) {
		if (inputWidth == inW && inputHeight == inH && displayWidth == outW && displayHeight == outH && output.Surface) return true;
		ReleaseResources();

		struct Make { Tex* texture; const char* name; UINT w, h; D3DFORMAT format; };
		const UINT lumaW = (std::max)(1u, inW / 32), lumaH = (std::max)(1u, inH / 32);
		const Make list[] = {
			{ &lumaMip,          "TESR_FSRLumaMip",         lumaW, lumaH, D3DFMT_R16F },
			{ &dilatedMotion[0], "TESR_FSRDilatedMotion0",  inW,  inH,  D3DFMT_G16R16F },
			{ &dilatedMotion[1], "TESR_FSRDilatedMotion1",  inW,  inH,  D3DFMT_G16R16F },
			{ &dilatedDepth,     "TESR_FSRDilatedDepth",    inW,  inH,  D3DFMT_R32F },
			{ &lockLuma,         "TESR_FSRLockLuma",        inW,  inH,  D3DFMT_R16F },
			{ &newLocks,         "TESR_FSRNewLocks",        outW, outH, D3DFMT_R16F },
			{ &prepared,         "TESR_FSRPrepared",        inW,  inH,  D3DFMT_A16B16G16R16F },
			{ &lumaHistory[0],   "TESR_FSRLumaHistory0",    outW, outH, D3DFMT_A16B16G16R16F },
			{ &lumaHistory[1],   "TESR_FSRLumaHistory1",    outW, outH, D3DFMT_A16B16G16R16F },
			{ &reactiveMasks,    "TESR_FSRReactiveMasks",   inW,  inH,  D3DFMT_G16R16F },
			{ &upscaled[0],      "TESR_FSRUpscaled0",       outW, outH, D3DFMT_A16B16G16R16F },
			{ &upscaled[1],      "TESR_FSRUpscaled1",       outW, outH, D3DFMT_A16B16G16R16F },
			{ &lockStatus[0],    "TESR_FSRLockStatus0",     outW, outH, D3DFMT_G16R16F },
			{ &lockStatus[1],    "TESR_FSRLockStatus1",     outW, outH, D3DFMT_G16R16F },
			{ &output,           "TESR_FSROutput",          outW, outH, D3DFMT_A16B16G16R16F },
		};
		for (const Make& m : list) {
			TheTextureManager->InitTexture(m.name, &m.texture->Texture, &m.texture->Surface, m.w, m.h, m.format);
			if (!m.texture->Surface) {
				Logger::Log("[ERROR] FSR 2: could not create %s (%ux%u, format %u)", m.name, m.w, m.h, (UInt32)m.format);
				ReleaseResources();
				return false;
			}
		}
		reconstructedDepth = DxvkCompute::CreateBuffer(inW * inH * 4, NVR_EXT_BUFFER_STORAGE | NVR_EXT_BUFFER_TRANSFER_DST, "FSR reconstructed depth");
		if (!reconstructedDepth) { ReleaseResources(); return false; }

		IDirect3DDevice9* device = TheRenderManager->device;
		Tex* cleared[] = { &lockStatus[0], &lockStatus[1], &prepared, &newLocks, &upscaled[0], &upscaled[1],
			&lumaHistory[0], &lumaHistory[1], &dilatedMotion[0], &dilatedMotion[1] };
		for (Tex* t : cleared) device->ColorFill(t->Surface, nullptr, D3DCOLOR_ARGB(0, 0, 0, 0));
		DxvkCompute::Extension()->ClearBuffer(reconstructedDepth, 0, 0, 0x3f800000u);

		inputWidth = inW; inputHeight = inH; displayWidth = outW; displayHeight = outH;
		parity = 0;
		frameIndex = 0;
		const UInt64 renderBytes = (UInt64)inW * inH * (4 + 4 + 4 + 2 + 8 + 4 + 4), displayBytes = (UInt64)outW * outH * (2 + 8 * 2 + 8 * 2 + 4 * 2 + 8);
		Logger::Log("UNOFFICIAL NVR DXVK: FSR 2 resources made (render up to %ux%u, display %ux%u, about %u MB)", inW, inH, outW, outH,
			(UInt32)((renderBytes + displayBytes) / (1024 * 1024)));
		return true;
	}

	bool Fsr2Upscaler::Dispatch(ID3D9NvrExtPipeline* pipeline, int index, const NVR_EXT_RESOURCE_BINDING* bindings, UINT count, const void* push, UINT pushSize, UINT groupsX, UINT groupsY) {
		NVR_EXT_DISPATCH_DESC dispatch = {};
		dispatch.pPipeline = pipeline;
		dispatch.BindingCount = count;
		dispatch.pBindings = bindings;
		dispatch.pPushData = push;
		dispatch.PushDataSize = pushSize;
		dispatch.GroupsX = groupsX;
		dispatch.GroupsY = groupsY;
		dispatch.GroupsZ = 1;
		static GpuTimer timers[PipelineCount] = { GpuTimer("  FSR luma mip (NVR)"), GpuTimer("  FSR reconstruct"), GpuTimer("  FSR depth clip"),
			GpuTimer("  FSR lock"), GpuTimer("  FSR accumulate"), GpuTimer("  FSR accumulate (sharpen)"), GpuTimer("  FSR RCAS") };
		GpuProfileScope gpu(timers[index], TheRenderManager->device);
		DxvkCompute::PreLoad(bindings, count);
		if (FAILED(DxvkCompute::Extension()->Dispatch(&dispatch))) {
			Logger::Log("[ERROR] UNOFFICIAL NVR DXVK: FSR 2 pass %s failed to dispatch; FSR is off for this session", Pipelines[index].name);
			failed = true;
			return false;
		}
		return true;
	}

	bool Fsr2Upscaler::Evaluate(IDirect3DDevice9* Device, const Upscaler::Inputs& In) {
		if (failed) return false;
		if (!DxvkCompute::Available()) return false;
		if (!In.Color || !In.Depth || !In.Motion || !In.RenderWidth || !In.RenderHeight || In.RenderWidth > In.InputWidth ||
			In.RenderHeight > In.InputHeight || !In.DisplayWidth || !In.DisplayHeight) return Fail("an input is missing");
		if (!MakePipelines(false)) return false;
		if (In.Hdr && !MakePipelines(true)) return false;
		if (!EnsureResources(In.InputWidth, In.InputHeight, In.DisplayWidth, In.DisplayHeight)) return Fail("its textures could not be made");

		ID3D9NvrExtSampler* point = DxvkCompute::PointClamp();
		ID3D9NvrExtSampler* linear = DxvkCompute::LinearClamp();
		if (!point || !linear) return Fail("its samplers could not be made");

		if (In.Reset) {
			Device->ColorFill(lockStatus[parity].Surface, nullptr, D3DCOLOR_ARGB(0, 0, 0, 0));
			Device->ColorFill(upscaled[parity].Surface, nullptr, D3DCOLOR_ARGB(0, 0, 0, 0));
			DxvkCompute::Extension()->ClearBuffer(reconstructedDepth, 0, 0, 0x3f800000u);
			frameIndex = 0;
		}
		else frameIndex++;
		const int srv = parity, uav = parity ^ 1;
		const INT32 renderW = (INT32)In.RenderWidth, renderH = (INT32)In.RenderHeight;
		const INT32 displayW = (INT32)In.DisplayWidth, displayH = (INT32)In.DisplayHeight;

		FSRConstants c = {};
		c.renderSize[0] = renderW;
		c.renderSize[1] = renderH;
		c.maxRenderSize[0] = c.inputColorResourceDimensions[0] = (INT32)In.InputWidth;
		c.maxRenderSize[1] = c.inputColorResourceDimensions[1] = (INT32)In.InputHeight;
		c.displaySize[0] = displayW;
		c.displaySize[1] = displayH;
		c.lumaMipLevelToUse = 4;
		c.lumaMipDimensions[0] = (std::max)(1, (INT32)In.InputWidth / 32);
		c.lumaMipDimensions[1] = (std::max)(1, (INT32)In.InputHeight / 32);
		c.frameIndex = frameIndex;
		const float nearZ = In.NearZ, farZ = In.FarZ;
		const float q = farZ / (nearZ - farZ);
		c.deviceToViewDepth[0] = -q;
		c.deviceToViewDepth[1] = q * nearZ;
		c.deviceToViewDepth[2] = In.TanHalfFovX;
		c.deviceToViewDepth[3] = In.TanHalfFovY;
		c.jitterOffset[0] = In.JitterX;
		c.jitterOffset[1] = In.JitterY;
		c.motionVectorScale[0] = c.motionVectorScale[1] = 1.0f;
		c.downscaleFactor[0] = (float)renderW / (float)displayW;
		c.downscaleFactor[1] = (float)renderH / (float)displayH;
		c.preExposure = c.previousFramePreExposure = 1.0f;
		c.tanHalfFOV = In.TanHalfFovX;
		c.jitterPhaseCount = In.JitterPhases;
		c.deltaTime = In.DeltaSeconds < 0.0f ? 0.0f : (In.DeltaSeconds > 1.0f ? 1.0f : In.DeltaSeconds);
		c.dynamicResChangeFactor = 0.0f;
		c.viewSpaceToMetersFactor = 1.0f / In.UnitsPerMetre;
		if (!constants && !constantsTried) {
			constantsTried = true;
			constants = DxvkCompute::CreateBuffer((sizeof(c) + 15) & ~15u, NVR_EXT_BUFFER_UNIFORM | NVR_EXT_BUFFER_TRANSFER_DST, "FSR 2 constants");
		}
		if (!constants || FAILED(DxvkCompute::Extension()->UpdateBuffer(constants, 0, sizeof(c), &c))) return Fail("its constant buffer could not be written");

		FSRPush push = {};
		const float sharpness = In.Sharpness;
		const bool sharpen = sharpness > 0.0f;
		if (sharpen) {
			float stops = -2.0f * sharpness + 2.0f;
			float s = exp2f(-stops);
			push.rcas[0] = *(UInt32*)&s;
			push.rcas[1] = HalfBits(s) | (HalfBits(s) << 16);
		}
		ID3D9NvrExtPipeline* const* set = In.Hdr ? hdrPipelines : pipelines;
		auto pipe = [&](int i) { return (Pipelines[i].hdrName && In.Hdr) ? set[i] : pipelines[i]; };

		const UINT rx8 = (renderW + 7) / 8, ry8 = (renderH + 7) / 8;
		const UINT dx8 = (displayW + 7) / 8, dy8 = (displayH + 7) / 8;
		ID3D9NvrExtBuffer* depthBuffer = reconstructedDepth;
		IDirect3DBaseTexture9* source = In.Color;

		{
			NVR_EXT_RESOURCE_BINDING b[] = { Image(0, source), Image(1, lumaMip.Texture, true), Sampler(0, point) };
			struct { UInt32 pointIndex, pad; INT32 w, h; } lumaPush = { 0, 0, renderW, renderH };
			if (!Dispatch(pipe(LumaMip), LumaMip, b, _countof(b), &lumaPush, sizeof(lumaPush), (std::max)(1, renderW / 32), (std::max)(1, renderH / 32))) return false;
		}
		{
			NVR_EXT_RESOURCE_BINDING b[] = { Image(0, In.Motion), Image(1, In.Depth), Image(2, source),
				Buffer(1, NVR_EXT_BIND_STORAGE_BUFFER, depthBuffer), Image(6, dilatedMotion[uav].Texture, true), Image(7, dilatedDepth.Texture, true),
				Image(11, lockLuma.Texture, true), Buffer(0, NVR_EXT_BIND_UNIFORM_BUFFER, constants) };
			if (!Dispatch(pipe(Reconstruct), Reconstruct, b, _countof(b), nullptr, 0, rx8, ry8)) return false;
		}
		{
			NVR_EXT_RESOURCE_BINDING b[] = { Buffer(1, NVR_EXT_BIND_STORAGE_BUFFER, depthBuffer), Image(1, dilatedMotion[uav].Texture),
				Image(2, dilatedDepth.Texture), Image(6, dilatedMotion[srv].Texture), Image(7, In.Motion), Image(8, source),
				Image(9, In.Depth), Image(12, reactiveMasks.Texture, true), Image(13, prepared.Texture, true),
				Buffer(0, NVR_EXT_BIND_UNIFORM_BUFFER, constants), Sampler(0, point), Sampler(1, linear) };
			if (!Dispatch(pipe(DepthClip), DepthClip, b, _countof(b), &push, sizeof(push), rx8, ry8)) return false;
		}
		{
			NVR_EXT_RESOURCE_BINDING b[] = { Image(0, lockLuma.Texture), Image(1, newLocks.Texture, true),
				Buffer(1, NVR_EXT_BIND_STORAGE_BUFFER, depthBuffer), Buffer(0, NVR_EXT_BIND_UNIFORM_BUFFER, constants) };
			if (!Dispatch(pipe(Lock), Lock, b, _countof(b), nullptr, 0, rx8, ry8)) return false;
		}
		{
			NVR_EXT_RESOURCE_BINDING b[] = { Image(1, reactiveMasks.Texture), Image(2, dilatedMotion[uav].Texture), Image(3, upscaled[srv].Texture),
				Image(4, lockStatus[srv].Texture), Image(6, prepared.Texture), Image(10, lumaMip.Texture), Image(12, lumaHistory[srv].Texture),
				Image(13, upscaled[uav].Texture, true), Image(14, lockStatus[uav].Texture, true), Image(15, output.Texture, true),
				Image(16, newLocks.Texture, true), Image(17, lumaHistory[uav].Texture, true),
				Buffer(0, NVR_EXT_BIND_UNIFORM_BUFFER, constants), Sampler(0, point), Sampler(1, linear) };
			if (sharpen) {
				NVR_EXT_RESOURCE_BINDING s[_countof(b) - 1];
				UINT n = 0;
				for (const NVR_EXT_RESOURCE_BINDING& r : b) if (!(r.Type == NVR_EXT_BIND_STORAGE_IMAGE && r.Slot == 15)) s[n++] = r;
				if (!Dispatch(pipe(AccumulateSharpen), AccumulateSharpen, s, n, &push, sizeof(push), dx8, dy8)) return false;
			}
			else if (!Dispatch(pipe(Accumulate), Accumulate, b, _countof(b), &push, sizeof(push), dx8, dy8)) return false;
		}
		if (sharpen) {
			NVR_EXT_RESOURCE_BINDING b[] = { Image(1, upscaled[uav].Texture), Image(2, output.Texture, true), Buffer(0, NVR_EXT_BIND_UNIFORM_BUFFER, constants) };
			if (!Dispatch(pipe(RCAS), RCAS, b, _countof(b), &push, sizeof(push), (displayW + 15) / 16, (displayH + 15) / 16)) return false;
		}
		parity ^= 1;
		return true;
	}

	Fsr2Upscaler TheFsr2;
}

namespace Upscaler {
	IUpscaler* Fsr2() { return &TheFsr2; }

}
