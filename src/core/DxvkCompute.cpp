#include "DxvkCompute.h"

ID3D9NvrExt*	DxvkCompute::Ext = nullptr;
NVR_EXT_INFO	DxvkCompute::ExtInfo = {};
bool			DxvkCompute::Failed = false;
char			DxvkCompute::StatusText[160] = "not checked yet";
ID3D9NvrExtSampler* DxvkCompute::SamplerLinear = nullptr;
ID3D9NvrExtSampler* DxvkCompute::SamplerPoint = nullptr;

void DxvkCompute::Initialize(IDirect3DDevice9* device) {
	Ext = nullptr;
	Failed = false;
	if (!device) return;

	ID3D9NvrExt* ext = nullptr;
	if (FAILED(device->QueryInterface(__uuidof(ID3D9NvrExt), (void**)&ext)) || !ext) {
		_snprintf_s(StatusText, sizeof(StatusText), _TRUNCATE, "NVR DXVK extension not present (native D3D9 or stock DXVK): compute paths off, D3D9 passes used");
		Logger::Log("UNOFFICIAL NVR DXVK: %s", StatusText);
		return;
	}

	ext->GetInfo(&ExtInfo);
	if (ExtInfo.ExtVersionMajor != NVR_EXT_VERSION_MAJOR) {
		_snprintf_s(StatusText, sizeof(StatusText), _TRUNCATE, "NVR DXVK extension %u.%u found but this NVR needs %u.x: compute paths off",
			ExtInfo.ExtVersionMajor, ExtInfo.ExtVersionMinor, (unsigned)NVR_EXT_VERSION_MAJOR);
		Logger::Log("UNOFFICIAL NVR DXVK: %s", StatusText);
		ext->Release();
		return;
	}

	Ext = ext;	// keeps a device reference for the session (never disposed, like the managers)
	_snprintf_s(StatusText, sizeof(StatusText), _TRUNCATE, "NVR DXVK extension %u.%u on DXVK %s (push %u bytes, subgroup %u): compute paths available",
		ExtInfo.ExtVersionMajor, ExtInfo.ExtVersionMinor, ExtInfo.DxvkVersion, ExtInfo.MaxPushBytes, ExtInfo.SubgroupSize);
	Logger::Log("UNOFFICIAL NVR DXVK: %s", StatusText);
}

const char* DxvkCompute::Status() {
	return StatusText;
}

void DxvkCompute::Fail(const char* Reason) {
	if (Failed) return;
	Failed = true;
	_snprintf_s(StatusText, sizeof(StatusText), _TRUNCATE, "compute paths OFF for this session: %s", Reason ? Reason : "unknown");
	Logger::Log("UNOFFICIAL NVR DXVK: %s", StatusText);
}

bool DxvkCompute::LoadSpirv(const char* Name, std::vector<char>& Words) {
	Words.clear();
	char path[MAX_PATH];
	_snprintf_s(path, sizeof(path), _TRUNCATE, "Data\\Shaders\\NewVegasReloaded\\Compute\\%s.spv", Name);

	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file) {
		Logger::Log("UNOFFICIAL NVR DXVK: cannot open %s", path);
		return false;
	}
	std::streamsize size = file.tellg();
	file.seekg(0, std::ios::beg);
	if (size < 20 || (size % 4) != 0) {
		Logger::Log("UNOFFICIAL NVR DXVK: %s is not SPIR-V (size %d)", path, (int)size);
		return false;
	}
	Words.resize((size_t)size);
	file.read(Words.data(), size);
	if (!file) {
		Words.clear();
		Logger::Log("UNOFFICIAL NVR DXVK: could not read %s", path);
		return false;
	}
	return true;
}

ID3D9NvrExtPipeline* DxvkCompute::CreatePipeline(const char* Name, const NVR_EXT_BINDING_DESC* Bindings, UINT Count, UINT PushSize, bool UsesSamplers) {
	if (!Available()) return nullptr;

	std::vector<char> words;
	if (!LoadSpirv(Name, words)) {
		Fail("a compute shader file is missing (see the line above)");
		return nullptr;
	}

	NVR_EXT_COMPUTE_PIPELINE_DESC desc = {};
	desc.pSpirv = words.data();
	desc.SpirvSize = (UINT)words.size();
	desc.BindingCount = Count;
	desc.pBindings = Bindings;
	desc.PushConstantSize = PushSize;
	desc.UsesSamplerHeap = UsesSamplers ? TRUE : FALSE;
	desc.SamplerHeapSet = 0;
	desc.SamplerHeapBinding = 0;
	desc.pDebugName = Name;

	ID3D9NvrExtPipeline* pipeline = nullptr;
	HRESULT result = Ext->CreateComputePipeline(&desc, &pipeline);
	if (FAILED(result) || !pipeline) {
		Logger::Log("UNOFFICIAL NVR DXVK: CreateComputePipeline(%s) failed (%08lX); DXVK's log says why", Name, result);
		Fail("a compute pipeline was refused");
		return nullptr;
	}
	Logger::Log("UNOFFICIAL NVR DXVK: compute pipeline %s ready (%u bindings, %u push bytes)", Name, Count, PushSize);
	return pipeline;
}

ID3D9NvrExtBuffer* DxvkCompute::CreateBuffer(UINT Size, UINT Flags, const char* Name) {
	if (!Available()) return nullptr;
	NVR_EXT_BUFFER_DESC desc = { Size, Flags, Name };
	ID3D9NvrExtBuffer* buffer = nullptr;
	HRESULT result = Ext->CreateBuffer(&desc, &buffer);
	if (FAILED(result) || !buffer) {
		Logger::Log("UNOFFICIAL NVR DXVK: CreateBuffer(%s, %u bytes) failed (%08lX)", Name ? Name : "", Size, result);
		Fail("a buffer could not be created");
		return nullptr;
	}
	return buffer;
}

ID3D9NvrExtSampler* DxvkCompute::CreateSampler(NVR_EXT_FILTER Filter, NVR_EXT_ADDRESS Address) {
	if (!Available()) return nullptr;
	NVR_EXT_SAMPLER_DESC desc = { Filter, Address };
	ID3D9NvrExtSampler* sampler = nullptr;
	HRESULT result = Ext->CreateSampler(&desc, &sampler);
	if (FAILED(result) || !sampler) {
		Logger::Log("UNOFFICIAL NVR DXVK: CreateSampler failed (%08lX)", result);
		Fail("a sampler could not be created");
		return nullptr;
	}
	return sampler;
}

ID3D9NvrExtPipeline* DxvkCompute::CreateGraphicsPipeline(const char* Name, const NVR_EXT_BINDING_DESC* Bindings, UINT Count, UINT PushSize, bool UsesSamplers, UINT TargetCount, NVR_EXT_BLEND Blend) {
	if (!Available()) return nullptr;
	if (ExtInfo.ExtVersionMinor < 1) {
		Logger::Log("UNOFFICIAL NVR DXVK: full-screen passes need extension 1.1 (this d3d9.dll has %u.%u): %s stays a D3D9 pass", ExtInfo.ExtVersionMajor, ExtInfo.ExtVersionMinor, Name);
		return nullptr;
	}

	std::vector<char> words;
	if (!LoadSpirv(Name, words)) {
		Fail("a pass shader file is missing (see the line above)");
		return nullptr;
	}

	NVR_EXT_GRAPHICS_PIPELINE_DESC desc = {};
	desc.pFragmentSpirv = words.data();
	desc.FragmentSpirvSize = (UINT)words.size();
	desc.BindingCount = Count;
	desc.pBindings = Bindings;
	desc.PushConstantSize = PushSize;
	desc.UsesSamplerHeap = UsesSamplers ? TRUE : FALSE;
	desc.SamplerHeapSet = 0;
	desc.SamplerHeapBinding = 0;
	desc.ColorTargetCount = TargetCount;
	desc.Blend = Blend;
	desc.ColorWriteMask = 0;
	desc.pDebugName = Name;

	ID3D9NvrExtPipeline* pipeline = nullptr;
	HRESULT result = Ext->CreateGraphicsPipeline(&desc, &pipeline);
	if (FAILED(result) || !pipeline) {
		Logger::Log("UNOFFICIAL NVR DXVK: CreateGraphicsPipeline(%s) failed (%08lX); DXVK's log says why", Name, result);
		Fail("a pass pipeline was refused");
		return nullptr;
	}
	Logger::Log("UNOFFICIAL NVR DXVK: pass %s ready (%u bindings, %u push bytes, %u target%s)", Name, Count, PushSize, TargetCount, TargetCount == 1 ? "" : "s");
	return pipeline;
}

ID3D9NvrExtSampler* DxvkCompute::LinearClamp() {
	if (!SamplerLinear) SamplerLinear = CreateSampler(NVR_EXT_FILTER_LINEAR, NVR_EXT_ADDRESS_CLAMP);
	return SamplerLinear;
}

ID3D9NvrExtSampler* DxvkCompute::PointClamp() {
	if (!SamplerPoint) SamplerPoint = CreateSampler(NVR_EXT_FILTER_NEAREST, NVR_EXT_ADDRESS_CLAMP);
	return SamplerPoint;
}

bool DxvkCompute::PassesOn() {
	return TheSettingManager->SettingsMain.Main.NvrDxvkPasses && Available();
}

