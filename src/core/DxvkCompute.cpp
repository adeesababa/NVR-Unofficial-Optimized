#include "DxvkCompute.h"

ID3D9NvrExt*	DxvkCompute::Ext = nullptr;
NVR_EXT_INFO	DxvkCompute::ExtInfo = {};
bool			DxvkCompute::Failed = false;
char			DxvkCompute::StatusText[160] = "not checked yet";
ID3D9NvrExtSampler* DxvkCompute::SamplerLinear = nullptr;
ID3D9NvrExtSampler* DxvkCompute::SamplerPoint = nullptr;
ID3D9NvrExtSampler* DxvkCompute::SamplerWrap = nullptr;
ID3D9NvrExtSampler* DxvkCompute::SamplerPointWrap = nullptr;

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

bool DxvkCompute::HasSpirv(const char* Name) {
	char path[MAX_PATH];
	_snprintf_s(path, sizeof(path), _TRUNCATE, "Data\\Shaders\\NewVegasReloaded\\Compute\\%s.spv", Name);
	return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

bool DxvkCompute::VariantsOn() {
	return TheSettingManager->SettingsMain.Main.NvrDxvkVariants;
}

bool DxvkCompute::PassesOn() {
	return TheSettingManager->SettingsMain.Main.NvrDxvkPasses && Available();
}

bool DxvkCompute::ReplacementsOn() {
	return TheSettingManager->SettingsMain.Main.NvrDxvkReplacements && Available();
}

// "textures=0,1,2" -> bit mask; "floats=0-3,8-15" -> 224-bit register mask. Unknown lines are ignored.
static bool ParseReplacementManifest(const char* Path, UINT& TextureMask, UINT* FloatMask, bool& FloatsGiven) {
	std::ifstream file(Path);
	if (!file) return false;
	std::string line;
	while (std::getline(file, line)) {
		const char* s = line.c_str();
		while (*s == ' ' || *s == '\t') s++;
		bool textures = !_strnicmp(s, "textures=", 9), floats = !_strnicmp(s, "floats=", 7);
		if (!textures && !floats) continue;
		s += textures ? 9 : 7;
		if (floats) FloatsGiven = true;
		while (*s) {
			while (*s == ' ' || *s == ',') s++;
			if (!*s) break;
			char* end = nullptr;
			long first = strtol(s, &end, 10), last = first;
			if (end == s) break;
			s = end;
			if (*s == '-') { last = strtol(s + 1, &end, 10); s = end; }
			for (long r = first; r <= last; r++) {
				if (textures && r >= 0 && r < 16) TextureMask |= 1u << r;
				if (floats && r >= 0 && r < 224) FloatMask[r / 32] |= 1u << (r % 32);
			}
		}
	}
	return true;
}

bool DxvkCompute::RegisterReplacement(const char* Name, const void* Bytecode, UINT Size) {
	if (!Name || !Bytecode || !Size || !ReplacementsOn()) return false;

	// 2026-10-25: the interior and exterior records of a shader share its name but not its bytecode, so a replacement is
	// looked up as <Name>@<hash>.spv first (FNV-1a of the bytecode, 8 hex digits), then <Name>.spv. With a Replace\dump
	// folder present, every pixel shader is written there as <Name>@<hash>.pso (the exact bytes a replacement is made from).
	UINT hash = 2166136261u;
	for (UINT i = 0; i < Size; i++) hash = (hash ^ ((const unsigned char*)Bytecode)[i]) * 16777619u;
	char dump[MAX_PATH];
	_snprintf_s(dump, sizeof(dump), _TRUNCATE, "Data\\Shaders\\NewVegasReloaded\\Compute\\Replace\\dump\\%s@%08X.pso", Name, hash);
	if (GetFileAttributesA("Data\\Shaders\\NewVegasReloaded\\Compute\\Replace\\dump") != INVALID_FILE_ATTRIBUTES &&
		GetFileAttributesA(dump) == INVALID_FILE_ATTRIBUTES) {
		std::ofstream out(dump, std::ios::binary);
		out.write((const char*)Bytecode, Size);
	}

	char path[MAX_PATH], key[MAX_PATH];
	_snprintf_s(key, sizeof(key), _TRUNCATE, "%s@%08X", Name, hash);
	_snprintf_s(path, sizeof(path), _TRUNCATE, "Data\\Shaders\\NewVegasReloaded\\Compute\\Replace\\%s.spv", key);
	if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
		_snprintf_s(key, sizeof(key), _TRUNCATE, "%s", Name);
		_snprintf_s(path, sizeof(path), _TRUNCATE, "Data\\Shaders\\NewVegasReloaded\\Compute\\Replace\\%s.spv", key);
		if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) return false;	// the normal case: no replacement for this shader
	}

	if (ExtInfo.ExtVersionMinor < 2) {
		static bool said = false;
		if (!said) Logger::Log("UNOFFICIAL NVR DXVK: shader replacements need extension 1.2 (this d3d9.dll has %u.%u): %s and the others stay as they are", ExtInfo.ExtVersionMajor, ExtInfo.ExtVersionMinor, Name);
		said = true;
		return false;
	}

	std::vector<char> words;
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	std::streamsize size = file ? (std::streamsize)file.tellg() : 0;
	if (size < 20 || (size % 4) != 0) {
		Logger::Log("UNOFFICIAL NVR DXVK: %s is not SPIR-V (size %d): %s stays as it is", path, (int)size, Name);
		return false;
	}
	file.seekg(0, std::ios::beg);
	words.resize((size_t)size);
	file.read(words.data(), size);
	if (!file) { Logger::Log("UNOFFICIAL NVR DXVK: could not read %s", path); return false; }

	char manifest[MAX_PATH];
	_snprintf_s(manifest, sizeof(manifest), _TRUNCATE, "Data\\Shaders\\NewVegasReloaded\\Compute\\Replace\\%s.txt", key);
	NVR_EXT_SHADER_REPLACEMENT_DESC desc = {};
	bool floatsGiven = false;
	if (!ParseReplacementManifest(manifest, desc.TextureMask, desc.FloatRegisterMask, floatsGiven)) {
		Logger::Log("UNOFFICIAL NVR DXVK: %s has no %s (textures= / floats= lines): %s stays as it is", path, manifest, Name);
		return false;
	}

	desc.pBytecode = Bytecode;
	desc.BytecodeSize = Size;
	desc.pSpirv = words.data();
	desc.SpirvSize = (UINT)words.size();
	desc.pDebugName = Name;
	HRESULT result = Ext->RegisterShaderReplacement(&desc);
	if (FAILED(result)) {
		Logger::Log("UNOFFICIAL NVR DXVK: RegisterShaderReplacement(%s) refused (%08lX; DXVK's log says why, usually textures= naming a sampler the original does not use): it stays as it is", Name, result);
		return false;
	}
	Logger::Log("UNOFFICIAL NVR DXVK: %s will run as %s (textures %04X, %s float layout)", Name, path, desc.TextureMask, floatsGiven ? "listed" : "original compacted");
	return true;
}

ID3D9NvrExtSampler* DxvkCompute::LinearWrap() {
	if (!SamplerWrap) SamplerWrap = CreateSampler(NVR_EXT_FILTER_LINEAR, NVR_EXT_ADDRESS_WRAP);
	return SamplerWrap;
}

// UNOFFICIAL 2026-10-25 (extension 1.3): see DxvkCompute.h. One sampler per kind, made on first use.
ID3D9NvrExtSampler* DxvkCompute::MipSampler(bool Anisotropic, NVR_EXT_ADDRESS Address) {
	static ID3D9NvrExtSampler* samplers[2][4] = {};
	if (!Available() || ExtInfo.ExtVersionMinor < 3 || (UINT)Address > 3) return nullptr;
	ID3D9NvrExtSampler*& s = samplers[Anisotropic ? 1 : 0][Address];
	if (!s) s = CreateSampler(Anisotropic ? NVR_EXT_FILTER_ANISOTROPIC : NVR_EXT_FILTER_LINEAR_MIPMAPS, Address);
	return s;
}

// UNOFFICIAL 2026-10-25 (Cinema): NEAREST min and mag, WRAP (mips are not filtered either way).
ID3D9NvrExtSampler* DxvkCompute::PointWrap() {
	if (!SamplerPointWrap) SamplerPointWrap = CreateSampler(NVR_EXT_FILTER_NEAREST, NVR_EXT_ADDRESS_WRAP);
	return SamplerPointWrap;
}

