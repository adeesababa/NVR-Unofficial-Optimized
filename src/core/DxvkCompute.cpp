#include "DxvkCompute.h"
#include "RenderRect.h"
#include "CapturedNormals.h"

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

	Ext = ext;
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

ID3D9NvrExtBuffer* DxvkCompute::CreateBufferNoFail(UINT Size, UINT Flags, const char* Name) {
	if (!Available()) return nullptr;
	NVR_EXT_BUFFER_DESC desc = { Size, Flags, Name };
	ID3D9NvrExtBuffer* buffer = nullptr;
	HRESULT result = Ext->CreateBuffer(&desc, &buffer);
	if (FAILED(result) || !buffer) {
		Logger::Log("UNOFFICIAL NVR DXVK: CreateBuffer(%s, %u bytes) failed (%08lX)", Name ? Name : "", Size, result);
		return nullptr;
	}
	return buffer;
}

void DxvkCompute::PreLoad(const NVR_EXT_RESOURCE_BINDING* Bindings, UINT Count) {
	for (UINT i = 0; Bindings && i < Count; i++)
		if (Bindings[i].Type == NVR_EXT_BIND_SAMPLED_IMAGE && Bindings[i].pResource)
			static_cast<IDirect3DResource9*>(static_cast<void*>(Bindings[i].pResource))->PreLoad();
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

ID3D9NvrExtPipeline* DxvkCompute::CreateGraphicsPipeline(const char* Name, const NVR_EXT_BINDING_DESC* Bindings, UINT Count, UINT PushSize, bool UsesSamplers, UINT TargetCount, NVR_EXT_BLEND Blend, bool FailAll) {
	if (!Available()) return nullptr;
	if (ExtInfo.ExtVersionMinor < 1) {
		Logger::Log("UNOFFICIAL NVR DXVK: full-screen passes need extension 1.1 (this d3d9.dll has %u.%u): %s stays a D3D9 pass", ExtInfo.ExtVersionMajor, ExtInfo.ExtVersionMinor, Name);
		return nullptr;
	}

	std::vector<char> words;
	if (!LoadSpirv(Name, words)) {
		if (FailAll) Fail("a pass shader file is missing (see the line above)");
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
		if (FailAll) Fail("a pass pipeline was refused");
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

bool DxvkCompute::StencilOn() {
	return TheSettingManager->SettingsMain.Main.NvrDxvkStencil && Available() && ExtInfo.ExtVersionMinor >= 6;
}

bool DxvkCompute::KeptOnD3D9(const char* Name) {
	const char* list = TheSettingManager->SettingsMain.Main.NvrDxvkKeepD3D9;
	if (!list[0] || !Name || !Name[0]) return false;
	const size_t length = strlen(Name);
	for (const char* p = list; (p = strstr(p, Name)) != nullptr; p += length) {
		const bool startOk = p == list || p[-1] == ',' || p[-1] == ' ';
		const bool endOk = p[length] == '\0' || p[length] == ',' || p[length] == ' ';
		if (startOk && endOk) return true;
	}
	return false;
}

bool DxvkCompute::PassesOn() {
	return TheSettingManager->SettingsMain.Main.NvrDxvkPasses && !RenderRect::Active() && Available();
}

void DxvkCompute::ProfileTick(bool On) {
	static bool profiling = false;
	static unsigned frames = 0;
	static NVR_EXT_PROFILE last = {};
	if (!Ext || ExtInfo.ExtVersionMinor < 5) return;
	if (On != profiling) {
		profiling = On;
		Ext->SetProfiling(On ? TRUE : FALSE);
		if (On) { Ext->GetProfile(&last); frames = 0; }
		Logger::Log("DXVK PROFILE %s (every 8th frame's D3D9 calls timed; a line every 120 frames)", On ? "on" : "off");
		return;
	}
	if (!On || ++frames < 120) return;
	frames = 0;

	NVR_EXT_PROFILE now = {};
	if (FAILED(Ext->GetProfile(&now))) return;
	const double f = (double)(now.Frames - last.Frames), timed = (double)(now.TimedFrames - last.TimedFrames);
	const double us = (double)(now.TimeUs - last.TimeUs);
	if (f < 1.0 || us <= 0.0) { last = now; return; }
	const double ticksPerMs = (double)(now.Tsc - last.Tsc) / us * 1000.0;
	const double calls = (double)(now.ApiCalls - last.ApiCalls);
	const double frameMs = us / 1000.0 / f;
	const double apiMs = timed > 0.0 ? ((double)(now.ApiTicks - last.ApiTicks) - calls * (double)now.OverheadTicks100 / 100.0) / ticksPerMs / timed : 0.0;
	const double presentWaitMs = timed > 0.0 ? (double)(now.TimedWaitTicks - last.TimedWaitTicks) / ticksPerMs / timed : 0.0;
	const double csSyncMs = (double)(now.CsSyncUs - last.CsSyncUs) / 1000.0 / f;
	const double gpuSyncMs = (double)(now.GpuSyncUs - last.GpuSyncUs) / 1000.0 / f;
	const double dxvkWorkMs = apiMs - presentWaitMs - csSyncMs - gpuSyncMs;
	const double csBusyMs = frameMs - (double)(now.CsIdleUs - last.CsIdleUs) / 1000.0 / f;
	const double gpuBusyMs = frameMs - (double)(now.GpuIdleUs - last.GpuIdleUs) / 1000.0 / f;
	Logger::Log("DXVK PROFILE frame %.2f ms (%.0f frames, passes %s) | game thread: outside D3D9 %.2f, inside D3D9 %.2f = DXVK work %.2f + "
		"present wait %.2f + wait for DXVK thread %.2f + wait for GPU %.2f (%.0f calls) | DXVK thread busy %.2f | GPU busy %.2f | per frame: "
		"draws %.0f, render passes %.0f, barriers %.0f, submits %.1f, chunks %.0f | new pipelines %llu",
		frameMs, f, PassesOn() ? "Vulkan" : "D3D9", frameMs - apiMs, apiMs, dxvkWorkMs, presentWaitMs, csSyncMs, gpuSyncMs, timed > 0.0 ? calls / timed : 0.0,
		csBusyMs, gpuBusyMs, (double)(now.Draws - last.Draws) / f, (double)(now.RenderPasses - last.RenderPasses) / f,
		(double)(now.Barriers - last.Barriers) / f, (double)(now.Submits - last.Submits) / f, (double)(now.CsChunks - last.CsChunks) / f,
		(unsigned long long)(now.GraphicsPipelines - last.GraphicsPipelines));
	last = now;
}

bool DxvkCompute::ReplacementsOn() {
	return TheSettingManager->SettingsMain.Main.NvrDxvkReplacements && Available();
}

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

bool DxvkCompute::RegisterReplacement(const char* Name, const void* Bytecode, UINT Size, const char* Template, const D3DXMACRO* Defines) {
	if (!Name || !Bytecode || !Size || !ReplacementsOn()) return false;

	UINT hash = 2166136261u;
	for (UINT i = 0; i < Size; i++) hash = (hash ^ ((const unsigned char*)Bytecode)[i]) * 16777619u;
	char dump[MAX_PATH];
	_snprintf_s(dump, sizeof(dump), _TRUNCATE, "Data\\Shaders\\NewVegasReloaded\\Compute\\Replace\\dump\\%s@%08X.pso", Name, hash);
	if (GetFileAttributesA("Data\\Shaders\\NewVegasReloaded\\Compute\\Replace\\dump") != INVALID_FILE_ATTRIBUTES &&
		GetFileAttributesA(dump) == INVALID_FILE_ATTRIBUTES) {
		std::ofstream out(dump, std::ios::binary);
		out.write((const char*)Bytecode, Size);
		if (Template) {
			char definesPath[MAX_PATH];
			_snprintf_s(definesPath, sizeof(definesPath), _TRUNCATE, "Data\\Shaders\\NewVegasReloaded\\Compute\\Replace\\dump\\%s@%08X.defines", Name, hash);
			std::ofstream defs(definesPath, std::ios::binary);
			defs << "template=" << Template << "\n";
			for (int d = 0; Defines && d < 64 && Defines[d].Name; d++)
				defs << Defines[d].Name << "=" << (Defines[d].Definition ? Defines[d].Definition : "") << "\n";
		}
	}

	char path[MAX_PATH], key[MAX_PATH];
	_snprintf_s(key, sizeof(key), _TRUNCATE, "%s@%08X", Name, hash);
	_snprintf_s(path, sizeof(path), _TRUNCATE, "Data\\Shaders\\NewVegasReloaded\\Compute\\Replace\\%s.spv", key);
	if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
		_snprintf_s(key, sizeof(key), _TRUNCATE, "%s", Name);
		_snprintf_s(path, sizeof(path), _TRUNCATE, "Data\\Shaders\\NewVegasReloaded\\Compute\\Replace\\%s.spv", key);
		if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) return false;
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

ID3D9NvrExtSampler* DxvkCompute::MipSampler(bool Anisotropic, NVR_EXT_ADDRESS Address) {
	static ID3D9NvrExtSampler* samplers[2][4] = {};
	if (!Available() || ExtInfo.ExtVersionMinor < 3 || (UINT)Address > 3) return nullptr;
	ID3D9NvrExtSampler*& s = samplers[Anisotropic ? 1 : 0][Address];
	if (!s) s = CreateSampler(Anisotropic ? NVR_EXT_FILTER_ANISOTROPIC : NVR_EXT_FILTER_LINEAR_MIPMAPS, Address);
	return s;
}

ID3D9NvrExtSampler* DxvkCompute::PointWrap() {
	if (!SamplerPointWrap) SamplerPointWrap = CreateSampler(NVR_EXT_FILTER_NEAREST, NVR_EXT_ADDRESS_WRAP);
	return SamplerPointWrap;
}


static ID3D9NvrExtTess*	DisplaceInterface = nullptr;
static bool				DisplaceQueried = false;
static UINT				DisplaceRegistered[2] = {};
static UINT				DisplaceDepthShaders = 0;
static UINT				DisplaceShadowVs = 0;
static unsigned			DisplaceShadowPs = 0;
static bool				DisplaceCastShadows = false;
static unsigned			DisplaceShadowAge = 1000;
static bool				DisplaceContactOutside = true;
static bool				DisplaceIgnoreMipBias = true;
static bool				DisplaceBlendShadows = false;
static std::vector<std::pair<IDirect3DPixelShader9*, int>> LandPixelShaders;

static NVR_EXT_TESS_STATE	DisplaceBase = {};
static NVR_EXT_TESS_STATE	DisplaceSent = {};
static bool					DisplaceSentOnce = false;
static float				DisplaceObjectBlend = 0.0f;

static NVR_EXT_TESS_STATE	DisplaceObj = {};
static UINT					DisplaceObjRegistered[2] = {};
static UINT					DisplaceObjSkipped = 0;
static unsigned				DisplaceSentFamily = DxvkCompute::kDisplaceLand;
static unsigned				DisplaceObjBinds = 0;

static bool					DisplaceFar = false;
static NVR_EXT_TESS_STATE	DisplaceWanted = {};
static bool					DisplaceFarOn = true;
static unsigned				DisplaceLandPasses[3] = {}, DisplaceLandStrips[3] = {}, DisplaceFarPasses = 0, DisplaceFarStrips = 0, DisplaceFarSwitches = 0;
static const float			kDisplaceFarMargin = 256.0f;

static void DisplaceSend(const NVR_EXT_TESS_STATE& State, unsigned Family = DxvkCompute::kDisplaceLand) {
	static const NVR_EXT_TESS_STATE off = {};
	const NVR_EXT_TESS_STATE* send = &State;
	if (Family == DxvkCompute::kDisplaceLand) {
		if (&State != &DisplaceWanted) DisplaceWanted = State;
		if (DisplaceFar) send = &off;
	}
	DisplaceSentFamily = Family;
	if (DisplaceSentOnce && !memcmp(send, &DisplaceSent, sizeof(*send))) return;
	DisplaceSent = *send;
	DisplaceSentOnce = true;
	DisplaceInterface->SetTessState(send);
}

static float DisplacePackScale(float X, float Y) {
	const float qx = floorf(std::clamp(X, 0.0f, 1.0f) * 2048.0f + 0.5f), qy = floorf(std::clamp(Y, 0.0f, 1.0f) * 2048.0f + 0.5f);
	return qx * 4096.0f + qy;
}

static bool DisplaceSwitch() {
	return TheSettingManager->GetSettingI("Main.Main.Performance", "NvrDxvkDisplacement") != 0;
}

static bool DisplaceSettingOr(const char* Key, bool Missing, const char* Section = "Shaders.Terrain.Displacement") {
	SettingManager::Configuration::ConfigNode node;
	return TheSettingManager->Config.FillNode(&node, Section, Key) ? node.IntValue != 0 : Missing;
}

static float DisplaceFloatOr(const char* Key, float Missing, const char* Section = "Shaders.Terrain.Displacement") {
	SettingManager::Configuration::ConfigNode node;
	return TheSettingManager->Config.FillNode(&node, Section, Key) ? node.FloatValue : Missing;
}

static ID3D9NvrExtTess* DisplaceTess() {
	if (!DisplaceQueried) {
		DisplaceQueried = true;
		ID3D9NvrExt* ext = DxvkCompute::Extension();
		if (ext && SUCCEEDED(ext->QueryInterface(__uuidof(ID3D9NvrExtTess), (void**)&DisplaceInterface)) && DisplaceInterface)
			Logger::Log("UNOFFICIAL NVR DXVK: NvrDxvkDisplacement: this d3d9.dll has the tessellation interface (1.7-displace); the land's shaders are registered as they load");
		else {
			DisplaceInterface = nullptr;
			Logger::Log("UNOFFICIAL NVR DXVK: NvrDxvkDisplacement is on but this d3d9.dll has no tessellation interface (needs NVR DXVK 1.7-displace): the land stays as it is");
		}
	}
	return DisplaceInterface;
}

bool DxvkCompute::DisplaceOn() {
	return DisplaceSwitch() && Ext != nullptr && DisplaceInterface != nullptr
		&& DisplaceRegistered[0] && DisplaceRegistered[1];
}

static const char* DisplaceDefine(const D3DXMACRO* Defines, const char* Name) {
	for (const D3DXMACRO* m = Defines; m && m->Name; m++)
		if (!strcmp(m->Name, Name)) return m->Definition ? m->Definition : "";
	return nullptr;
}

bool DxvkCompute::DisplaceObjectsLayout() {
	if (CapturedNormals::CompiledIn() && !TheSettingManager->GetSettingI("Shaders.POM.Displacement", "Enabled")) return false;
	return DisplaceSwitch() && Ext != nullptr;
}

static unsigned RegisterDisplaceObject(ID3D9NvrExtTess* Tess, const char* Name, const D3DXMACRO* Defines, bool Vertex, const void* Bytecode, UINT Size) {
	const char* layout = DisplaceDefine(Defines, "NVR_DISPLACE_OBJECTS");
	if (!layout || strcmp(layout, "1")) return 0;
	const bool diffuse = DisplaceDefine(Defines, "DIFFUSE") != nullptr;

	if (Vertex) {
		std::vector<char> tcs;
		if (!DxvkCompute::LoadSpirv(diffuse ? "Tess\\ObjectDisplace-c.tcs" : "Tess\\ObjectDisplace.tcs", tcs)) return 0;
		NVR_EXT_TESS_VS_DESC desc = {};
		desc.pBytecode = Bytecode;
		desc.BytecodeSize = Size;
		desc.Family = DxvkCompute::kDisplaceObjects;
		desc.pControlSpirv = tcs.data();
		desc.ControlSpirvSize = (UINT)tcs.size();
		for (UINT k = 0; k < 4; k++) desc.VsFloatRegisters[k] = k;
		desc.pDebugName = Name;
		const HRESULT result = Tess->RegisterTessVertexShader(&desc);
		if (SUCCEEDED(result)) DisplaceObjRegistered[0]++;
		else Logger::Log("UNOFFICIAL NVR DXVK: displacement: object vertex shader %s refused (%08lX; DXVK's log says why): its objects stay flat", Name, result);
		return SUCCEEDED(result) ? DxvkCompute::kDisplaceObjects : 0;
	}

	int slot = -1;
	{
		ID3DXConstantTable* table = nullptr;
		if (SUCCEEDED(D3DXGetShaderConstantTableEx((const DWORD*)Bytecode, D3DXCONSTTABLE_LARGEADDRESSAWARE, &table)) && table) {
			D3DXHANDLE handle = table->GetConstantByName(NULL, "HeightMap");
			if (handle) slot = (int)table->GetSamplerIndex(handle);
			table->Release();
		}
	}
	char cw[64], ccw[64];
	_snprintf_s(cw, sizeof(cw), _TRUNCATE, "Tess\\ObjectDisplace-h%d%s.tes", slot, diffuse ? "-c" : "");
	_snprintf_s(ccw, sizeof(ccw), _TRUNCATE, "Tess\\ObjectDisplace-h%d%s-ccw.tes", slot, diffuse ? "-c" : "");
	std::vector<char> tes, tesCcw;
	if (slot < 2 || slot > 3 || !DxvkCompute::LoadSpirv(cw, tes)) {
		DisplaceObjSkipped++;
		Logger::Log("UNOFFICIAL NVR DXVK: displacement: object pixel shader %s: height map at s%d, no %s.spv: drawn flat (its object's other passes may not be)", Name, slot, cw);
		return 0;
	}
	const bool haveCcw = DxvkCompute::LoadSpirv(ccw, tesCcw);

	NVR_EXT_TESS_PS_DESC desc = {};
	desc.pBytecode = Bytecode;
	desc.BytecodeSize = Size;
	desc.Family = DxvkCompute::kDisplaceObjects;
	desc.pEvalSpirv[0] = tes.data();
	desc.EvalSpirvSize[0] = (UINT)tes.size();
	desc.pEvalSpirv[1] = haveCcw ? tesCcw.data() : nullptr;
	desc.EvalSpirvSize[1] = haveCcw ? (UINT)tesCcw.size() : 0;
	desc.TextureMask = 1u << slot;
	desc.PsFloatRegisters[0] = 0xFFFFFFFF;
	desc.PsFloatRegisters[1] = 0xFFFFFFFF;
	desc.pDebugName = Name;
	const HRESULT result = Tess->RegisterTessPixelShader(&desc);
	if (SUCCEEDED(result)) DisplaceObjRegistered[1]++;
	else {
		DisplaceObjSkipped++;
		Logger::Log("UNOFFICIAL NVR DXVK: displacement: object pixel shader %s refused (%08lX; DXVK's log says why): drawn flat", Name, result);
	}
	return SUCCEEDED(result) ? DxvkCompute::kDisplaceObjects : 0;
}

unsigned DxvkCompute::RegisterDisplace(const char* Name, const char* TemplateName, const D3DXMACRO* Defines, bool Vertex, const void* Bytecode, UINT Size) {
	if (!Name || !Bytecode || !Size) return 0;
	const bool object = TemplateName && !strcmp(TemplateName, "ParallaxTemplate");
	const bool shadow = Vertex ? !strcmp(Name, "ShadowMapLand.vso") : TemplateName && !strcmp(TemplateName, "ShadowMapLand.pso");
	if (!shadow && !object && (!TemplateName || strcmp(TemplateName, "TerrainTemplate"))) return 0;
	if (!DisplaceSwitch() || !Ext) return 0;
	ID3D9NvrExtTess* tess = DisplaceTess();
	if (!tess) return 0;
	if (object) return RegisterDisplaceObject(tess, Name, Defines, Vertex, Bytecode, Size);

	const UINT family = shadow ? kDisplaceLandShadow : kDisplaceLand;
	if (Vertex) {
		std::vector<char> tcs;
		if (!LoadSpirv("Tess\\TerrainDisplace.tcs", tcs)) return 0;
		NVR_EXT_TESS_VS_DESC desc = {};
		desc.pBytecode = Bytecode;
		desc.BytecodeSize = Size;
		desc.Family = family;
		desc.pControlSpirv = tcs.data();
		desc.ControlSpirvSize = (UINT)tcs.size();
		for (UINT k = 0; k < 4; k++) desc.VsFloatRegisters[k] = k;
		desc.pDebugName = Name;
		const HRESULT result = tess->RegisterTessVertexShader(&desc);
		if (SUCCEEDED(result)) { if (shadow) DisplaceShadowVs++; else DisplaceRegistered[0]++; }
		Logger::Log("UNOFFICIAL NVR DXVK: displacement: land %svertex shader %s %s (%08lX)", shadow ? "shadow caster " : "", Name,
			SUCCEEDED(result) ? "registered" : "refused (DXVK's log says why)", result);
		return SUCCEEDED(result) ? family : 0;
	}

	int layers = 0;
	for (const D3DXMACRO* m = Defines; m && m->Name; m++)
		if (!strcmp(m->Name, "TEX_COUNT") && m->Definition) layers = atoi(m->Definition);
	if (layers < 1 || layers > 7) return 0;

	bool depthAtS6 = false;
	{
		ID3DXConstantTable* table = nullptr;
		if (SUCCEEDED(D3DXGetShaderConstantTableEx((const DWORD*)Bytecode, D3DXCONSTTABLE_LARGEADDRESSAWARE, &table)) && table) {
			D3DXHANDLE handle = table->GetConstantByName(NULL, shadow ? "DisplaceDepth" : "TESR_DisplaceDepth");
			depthAtS6 = handle && table->GetSamplerIndex(handle) == 6;
			table->Release();
		}
	}

	char cw[64], ccw[64];
	std::vector<char> tes, tesCcw;
	bool haveCcw = false;
	const char* kind = shadow ? "s" : "";
	if (depthAtS6) {
		_snprintf_s(cw, sizeof(cw), _TRUNCATE, "Tess\\TerrainDisplace-t%d-%sd.tes", layers, kind);
		_snprintf_s(ccw, sizeof(ccw), _TRUNCATE, "Tess\\TerrainDisplace-t%d-%sd-ccw.tes", layers, kind);
		if (LoadSpirv(cw, tes)) haveCcw = LoadSpirv(ccw, tesCcw);
		else {
			depthAtS6 = false;
			Logger::Log("UNOFFICIAL NVR DXVK: displacement: %s: no %s.spv (copy the new Compute\\Tess shaders): ObjectBlend off for this land", Name, cw);
		}
	}
	if (!depthAtS6) {
		if (shadow) {
			_snprintf_s(cw, sizeof(cw), _TRUNCATE, "Tess\\TerrainDisplace-t%d-s.tes", layers);
			_snprintf_s(ccw, sizeof(ccw), _TRUNCATE, "Tess\\TerrainDisplace-t%d-s-ccw.tes", layers);
		}
		else {
			_snprintf_s(cw, sizeof(cw), _TRUNCATE, "Tess\\TerrainDisplace-t%d.tes", layers);
			_snprintf_s(ccw, sizeof(ccw), _TRUNCATE, "Tess\\TerrainDisplace-t%d-ccw.tes", layers);
		}
		if (!LoadSpirv(cw, tes)) {
			if (shadow) Logger::Log("UNOFFICIAL NVR DXVK: displacement: %s: no %s.spv (copy the new Compute\\Tess shaders): %d-layer land casts flat shadows", Name, cw, layers);
			return 0;
		}
		haveCcw = LoadSpirv(ccw, tesCcw);
	}

	NVR_EXT_TESS_PS_DESC desc = {};
	desc.pBytecode = Bytecode;
	desc.BytecodeSize = Size;
	desc.Family = family;
	desc.pEvalSpirv[0] = tes.data();
	desc.EvalSpirvSize[0] = (UINT)tes.size();
	desc.pEvalSpirv[1] = haveCcw ? tesCcw.data() : nullptr;
	desc.EvalSpirvSize[1] = haveCcw ? (UINT)tesCcw.size() : 0;
	desc.TextureMask = (1u << layers) - 1u;
	if (depthAtS6) desc.TextureMask |= 1u << 6;
	desc.PsFloatRegisters[0] = 34;
	desc.PsFloatRegisters[1] = 35;
	desc.pDebugName = Name;
	const HRESULT result = tess->RegisterTessPixelShader(&desc);
	if (SUCCEEDED(result)) {
		if (shadow) DisplaceShadowPs |= 1u << (layers - 1);
		else {
			DisplaceRegistered[1]++;
			if (depthAtS6) DisplaceDepthShaders++;
		}
	}
	else Logger::Log("UNOFFICIAL NVR DXVK: displacement: land %spixel shader %s refused (%08lX; DXVK's log says why): its land stays flat%s", shadow ? "shadow caster " : "",
		Name, result, shadow ? " in the sun's shadow maps" : "");
	return SUCCEEDED(result) ? family : 0;
}

void DxvkCompute::NoteLandPixelShader(IDirect3DPixelShader9* Shader, const D3DXMACRO* Defines) {
	int layers = 0;
	for (const D3DXMACRO* m = Defines; m && m->Name; m++)
		if (!strcmp(m->Name, "TEX_COUNT") && m->Definition) layers = atoi(m->Definition);
	if (Shader && layers >= 1 && layers <= 7) LandPixelShaders.emplace_back(Shader, layers);
}

int DxvkCompute::LandLayersOf(IDirect3DPixelShader9* Shader) {
	static IDirect3DPixelShader9* last = nullptr;
	static int lastLayers = 0;
	if (Shader == last) return lastLayers;
	last = Shader;
	lastLayers = 0;
	for (const auto& entry : LandPixelShaders) if (entry.first == Shader) { lastLayers = entry.second; break; }
	return lastLayers;
}

bool DxvkCompute::DisplaceShadowsWanted() {
	return DisplaceCastShadows && DisplaceOn() && DisplaceBase.Enable;
}

unsigned DxvkCompute::DisplaceShadowLayers() {
	return DisplaceShadowVs ? DisplaceShadowPs : 0;
}

bool DxvkCompute::DisplaceBlendInShadows() {
	return DisplaceBlendShadows;
}

float DxvkCompute::DisplaceReach() {
	return DisplaceBase.ControlParams[4];
}

void DxvkCompute::DisplaceShadowBegin(bool ObjectBlend, float ScaleX, float ScaleY) {
	if (!DisplaceInterface) return;
	DisplaceFar = false;
	NVR_EXT_TESS_STATE state = DisplaceBase;
	state.Wireframe = 0;
	state.ControlParams[6] = ObjectBlend ? DisplaceObjectBlend : 0.0f;
	state.EvalParams[3] = ObjectBlend ? DisplacePackScale(ScaleX, ScaleY) : DisplacePackScale(1.0f, 1.0f);
	DisplaceSend(state);
}

void DxvkCompute::DisplaceShadowEnd() {
	if (DisplaceInterface) DisplaceSend(DisplaceBase);
}

void DxvkCompute::DisplaceShadowsDrawn(unsigned Pieces) {
	if (Pieces) DisplaceShadowAge = 0;
}

bool DxvkCompute::DisplaceContactFade(float& Start, float& End) {
	if (!DisplaceContactOutside || !DisplaceInterface || !DisplaceRegistered[0] || !DisplaceRegistered[1] || !DisplaceBase.Enable
		|| !(DisplaceBase.EvalParams[0] > 0.0f)) return false;
	const float fadeStart = DisplaceBase.ControlParams[3], fadeEnd = DisplaceBase.ControlParams[4];
	const float quarter = 0.25f * (std::max)(fadeEnd - fadeStart, 1.0f);
	Start = (std::max)(fadeEnd - quarter, 0.0f);
	End = fadeEnd + quarter;
	return true;
}

static float DisplaceMipCompensation() {
	if (!DisplaceIgnoreMipBias) return 0.0f;
	TAAEffect* taa = TheShaderManager ? TheShaderManager->Effects.TAA : nullptr;
	return taa ? std::clamp(-taa->WorldMipBias(), 0.0f, 3.0f) : 0.0f;
}

bool DxvkCompute::DisplaceDepthWanted() {
	return DisplaceInterface && DisplaceRegistered[0] && DisplaceDepthShaders && DisplaceBase.Enable && DisplaceObjectBlend > 0.0f;
}

void DxvkCompute::DisplaceSceneDepth(bool Valid, float ScaleX, float ScaleY) {
	if (!DisplaceInterface || !DisplaceRegistered[0] || !DisplaceRegistered[1]) return;
	NVR_EXT_TESS_STATE state = DisplaceBase;
	if (Valid && DisplaceObjectBlend > 0.0f) {
		state.ControlParams[6] = DisplaceObjectBlend;
		state.EvalParams[3] = DisplacePackScale(ScaleX, ScaleY);
	}
	DisplaceSend(state);
}

void DxvkCompute::DisplaceBind(unsigned Family) {
	if (!DisplaceInterface) return;
	if (Family == kDisplaceObjects) {
		DisplaceObjBinds++;
		DisplaceSend(DisplaceObj, kDisplaceObjects);
	}
	else if (Family == kDisplaceLand && DisplaceSentFamily == kDisplaceObjects) DisplaceSend(DisplaceBase);
}

void DxvkCompute::DisplaceLandPass(NiGeometry* Geometry, UInt16 PassEnum, int Context) {
	bool pieceFar = false;
	if (DisplaceBase.Enable && Geometry && PassEnum >= 0x1EF && PassEnum <= 0x230) {
		const int context = std::clamp(Context, 0, 2);
		const NiGeometryBufferData* buffers = Geometry->geomData ? Geometry->geomData->m_pkBuffData : nullptr;
		const unsigned strips = buffers ? (buffers->IB ? buffers->NumArrays : 1u) : 0u;
		DisplaceLandPasses[context]++;
		DisplaceLandStrips[context] += strips;
		const NiBound* bound = Geometry->m_kWorldBound;
		if (DisplaceFarOn && context != kDisplaceOther && bound) {
			const D3DXVECTOR4& cam = TheRenderManager->CameraPosition;
			const float dx = bound->Center.x - cam.x, dy = bound->Center.y - cam.y;
			const float reach = DisplaceBase.ControlParams[4] + kDisplaceFarMargin + bound->Radius;
			pieceFar = dx * dx + dy * dy > reach * reach;
		}
		if (pieceFar) {
			DisplaceFarPasses++;
			DisplaceFarStrips += strips;
		}
	}
	if (pieceFar == DisplaceFar) return;
	DisplaceFar = pieceFar;
	DisplaceFarSwitches++;
	if (DisplaceInterface && DisplaceSentOnce && DisplaceSentFamily == kDisplaceLand) DisplaceSend(DisplaceWanted);
}

static void DisplaceObjectSettings() {
	const char* s = "Shaders.POM.Displacement";
	NVR_EXT_TESS_STATE& state = DisplaceObj;
	state.Enable = DisplaceSwitch() && DisplaceSettingOr("Enabled", false, s) ? TRUE : FALSE;
	const int debugView = std::clamp((int)DisplaceFloatOr("DebugView", 0.0f, s), 0, 1);
	state.Winding = (int)DisplaceFloatOr("Winding", 0.0f, s) == 1 ? 1 : 0;
	state.Wireframe = debugView == 1 ? 1 : 0;
	const float strength = std::clamp(DisplaceFloatOr("Strength", 8.0f, s), 0.0f, 64.0f);
	const float fadeStart = (std::max)(DisplaceFloatOr("FadeStart", 1024.0f, s), 0.0f);
	state.ControlParams[0] = (float)std::clamp((int)DisplaceFloatOr("MaxFactor", 16.0f, s), 1, 64)
		+ 128.0f * (float)std::clamp((int)DisplaceFloatOr("FixedFactor", 0.0f, s), 0, 64);
	state.ControlParams[1] = std::clamp(DisplaceFloatOr("EdgePixels", 12.0f, s), 2.0f, 64.0f);
	state.ControlParams[2] = 0.5f * (float)TheRenderManager->width;
	state.ControlParams[3] = fadeStart;
	state.ControlParams[4] = (std::max)(DisplaceFloatOr("FadeEnd", 2048.0f, s), fadeStart + 1.0f);
	state.ControlParams[5] = 0.5f * (float)TheRenderManager->height;
	state.ControlParams[6] = 16.0f * strength + 1.0f;
	state.ControlParams[7] = 0.0f;
	state.EvalParams[0] = strength;
	state.EvalParams[1] = floorf(std::clamp(DisplaceFloatOr("Contrast", 2.0f, s), 0.0f, 32.0f) * 16.0f + 0.5f)
		+ 0.99f * std::clamp(DisplaceFloatOr("Center", 0.5f, s), 0.0f, 1.0f);
	state.EvalParams[2] = std::clamp(DisplaceFloatOr("LodBias", 0.0f, s), -2.0f, 4.0f) + 4.0f + DisplaceMipCompensation();
	state.EvalParams[3] = std::clamp(DisplaceFloatOr("EdgeFade", 1.5f, s), 0.0f, 8.0f);
}

void DxvkCompute::DisplaceTick() {
	if (!DisplaceInterface) return;
	DisplaceFar = false;

	const bool objects = DisplaceObjRegistered[0] && DisplaceObjRegistered[1];
	static unsigned objFrame = 0;
	if (objects) {
		if ((objFrame % 8) == 0) DisplaceObjectSettings();
		if (DisplaceSentFamily == kDisplaceObjects) DisplaceSend(DisplaceObj, kDisplaceObjects);
		if ((objFrame % 600) == 599)
			Logger::Log("NVR TESS objects %s: %u vertex, %u pixel shaders registered (%u left flat), %.1f object shader binds a frame",
				DisplaceObj.Enable ? "on" : "off", DisplaceObjRegistered[0], DisplaceObjRegistered[1], DisplaceObjSkipped, (double)DisplaceObjBinds / 600.0);
		if ((objFrame % 600) == 599) DisplaceObjBinds = 0;
		objFrame++;
	}

	if (!DisplaceRegistered[0] || !DisplaceRegistered[1]) return;

	NVR_EXT_TESS_STATE& state = DisplaceBase;
	static unsigned frame = 0;
	static NVR_EXT_TESS_STATS last = {};
	static float viewParams = 4.0f;
	if ((frame++ % 8) == 0) {
		const char* s = "Shaders.Terrain.Displacement";
		state.Enable = DisplaceSwitch() && DisplaceSettingOr("Enabled", false) ? TRUE : FALSE;
		const int debugView = std::clamp(TheSettingManager->GetSettingI(s, "DebugView"), 0, 7);
		state.Winding = TheSettingManager->GetSettingI(s, "Winding") == 1 ? 1 : 0;
		state.Wireframe = debugView == 1 ? 1 : 0;
		const float fadeStart = (std::max)(TheSettingManager->GetSettingF(s, "FadeStart"), 0.0f);
		state.ControlParams[0] = (float)std::clamp(TheSettingManager->GetSettingI(s, "MaxFactor"), 1, 64)
			+ 128.0f * (float)std::clamp(TheSettingManager->GetSettingI(s, "FixedFactor"), 0, 64);
		state.ControlParams[1] = std::clamp(TheSettingManager->GetSettingF(s, "EdgeLength"), 2.0f, 64.0f);
		state.ControlParams[2] = std::clamp(TheSettingManager->GetSettingF(s, "EdgeGrowth"), 0.0f, 0.1f);
		state.ControlParams[3] = fadeStart;
		state.ControlParams[4] = (std::max)(TheSettingManager->GetSettingF(s, "FadeEnd"), fadeStart + 1.0f);
		state.ControlParams[5] = (std::max)(TheSettingManager->GetSettingF(s, "BorderGrid"), 0.0f);
		state.ControlParams[6] = 0.0f;
		state.ControlParams[7] = 0.0f;
		state.EvalParams[0] = std::clamp(TheSettingManager->GetSettingF(s, "Strength"), 0.0f, 64.0f);
		state.EvalParams[1] = floorf(std::clamp(DisplaceFloatOr("Contrast", 4.0f), 0.0f, 32.0f) * 16.0f + 0.5f)
			+ 0.99f * std::clamp(DisplaceFloatOr("Center", 0.5f), 0.0f, 1.0f);
		viewParams = 16.0f * (float)debugView + std::clamp(TheSettingManager->GetSettingF(s, "LodBias"), -2.0f, 4.0f) + 4.0f;
		viewParams += 512.0f * floorf(std::clamp(DisplaceFloatOr("BumpSize", 48.0f), 0.0f, 255.0f) + 0.5f);
		if (!DisplaceSettingOr("BendNormals", true)) viewParams += 128.0f;
		state.EvalParams[3] = DisplacePackScale(1.0f, 1.0f);
		const bool blend = DisplaceSettingOr("ObjectBlend", false);
		const float width = floorf(std::clamp(TheSettingManager->GetSettingF(s, "ObjectBlendWidth"), 4.0f, 512.0f) + 0.5f);
		const float keep = std::clamp(TheSettingManager->GetSettingF(s, "ObjectBlendKeep"), 0.0f, 1.0f);
		DisplaceObjectBlend = blend && keep < 1.0f ? width + keep * 0.99f : 0.0f;
		DisplaceCastShadows = DisplaceSettingOr("CastShadows", true);
		DisplaceFarOn = DisplaceSettingOr("FarUntessellated", true);
		DisplaceContactOutside = DisplaceSettingOr("ContactShadowsOutside", true);
		DisplaceIgnoreMipBias = DisplaceSettingOr("IgnoreMipBias", true);
		DisplaceBlendShadows = DisplaceSettingOr("ObjectBlendShadows", false);
		if (DisplaceObjectBlend > 0.0f && !DisplaceBlendShadows) viewParams += 131072.0f;
	}
	if (DisplaceShadowAge < 1000) DisplaceShadowAge++;
	const bool shadowsHold = DisplaceCastShadows && DisplaceShadowVs && DisplaceShadowAge <= 16;
	state.EvalParams[2] = viewParams + DisplaceMipCompensation() + (shadowsHold ? 256.0f : 0.0f);
	if (DisplaceSentFamily != kDisplaceObjects) DisplaceSend(state);

	if ((frame % 600) == 1) {
		static UINT lastFrameNumber = 0;
		const UINT frames = TheFrameRateManager->FrameNumber - lastFrameNumber;
		lastFrameNumber = TheFrameRateManager->FrameNumber;
		NVR_EXT_TESS_STATS now = {};
		if (frames && SUCCEEDED(DisplaceInterface->GetTessStats(&now))) {
			const double f = (double)frames;
			Logger::Log("NVR TESS %s: per frame %.1f land draws tessellated (lists and strips), %.1f left as they are (fans, UP draws: the DXVK log names the type), %.1f rebinds | land passes (strip draws): world %.1f (%.1f), water reflection %.1f (%.1f), other %.1f (%.1f) | far land %s: %.1f passes (%.1f strip draws) untessellated (wholly past FadeEnd + %.0f), %.1f near/far switches | shaders %u VS, %u PS, %u refused by the interface check | ObjectBlend %s (%u of the %u land pixel shaders can: below 7 layers)",
				state.Enable ? "on" : "off", (double)(now.TessellatedDraws - last.TessellatedDraws) / f, (double)(now.OtherTopologyDraws - last.OtherTopologyDraws) / f,
				(double)(now.Bindings - last.Bindings) / f,
				DisplaceLandPasses[kDisplaceWorld] / f, DisplaceLandStrips[kDisplaceWorld] / f, DisplaceLandPasses[kDisplaceReflection] / f,
				DisplaceLandStrips[kDisplaceReflection] / f, DisplaceLandPasses[kDisplaceOther] / f, DisplaceLandStrips[kDisplaceOther] / f,
				DisplaceFarOn ? "on" : "off", DisplaceFarPasses / f, DisplaceFarStrips / f, kDisplaceFarMargin, DisplaceFarSwitches / f,
				now.VertexShaders, now.PixelShaders, now.RefusedPairs, DisplaceObjectBlend > 0.0f ? "on" : "off", DisplaceDepthShaders, DisplaceRegistered[1]);
			last = now;
		}
		for (int c = 0; c < 3; c++) DisplaceLandPasses[c] = DisplaceLandStrips[c] = 0;
		DisplaceFarPasses = DisplaceFarStrips = DisplaceFarSwitches = 0;
	}
}

