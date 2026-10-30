// UNOFFICIAL 2026-10-28 (NVR DXVK): the ray tracing core (RtCore.h; docs\next\FORK-RT-CORE.md). The helper's side is
// tools\nvr-rt-helper (MIT, built on its own, 64-bit); the protocol is tools\nvr-rt-helper\NvrRtProtocol.h. The launch, the shared
// block, the watchdog and the stop follow UpscalerDlss.cpp (the DLSS helper), which proved them.

#include <algorithm>
#include <cmath>
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
	// Shared images are made in sizes rounded up to this (the content in the top-left corner). Measured 2026-10-28 on AMD 25.10: the
	// game's 32-bit driver and the helper's 64-bit driver lay out an optimal-tiling image the same way only when both sides are
	// multiples of 256 texels (1280x720 R32F: 3686400 bytes in the game, 3957104 in the helper; 1280x768 and 1920x1080 -> 2048x1280
	// etc. agree, texel for texel: tools\nvr-rt-helper\test\rt_helper_test.cpp RT_TEST_SCREEN / RT_TEST_GAME).
	const UINT kImageAlign = 256;
	UINT AlignImage(UINT v) { return (v + kImageAlign - 1) / kImageAlign * kImageAlign; }

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
		bool	textured = false;	// skinned: its last build carried texcoords (hit textures)
		bool	skinned = false;	// an actor's partition (dropped at once when the player changes cell)
	};

	// A diffuse texture's average colour (linear rgb), from its smallest mip, once per texture (protocol v2's instance records).
	// Managed textures only (they keep a system-memory copy: LockRect is a plain read); DXT1/3/5 take their first block's two end
	// colours, 32-bit formats their first texel. false: unknown (a neutral grey goes to the helper).
	struct Albedo { float rgb[3]; bool known; };
	std::unordered_map<const void*, Albedo> albedoCache;

	float SrgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f); }

	Albedo ReadAlbedo(IDirect3DBaseTexture9* base) {
		Albedo a = { { 0.5f, 0.5f, 0.5f }, false };
		IDirect3DTexture9* texture = nullptr;
		if (!base || base->GetType() != D3DRTYPE_TEXTURE || FAILED(base->QueryInterface(__uuidof(IDirect3DTexture9), (void**)&texture)) || !texture) return a;
		const DWORD level = texture->GetLevelCount() ? texture->GetLevelCount() - 1 : 0;
		D3DSURFACE_DESC desc = {};
		D3DLOCKED_RECT locked = {};
		if (SUCCEEDED(texture->GetLevelDesc(level, &desc)) && desc.Pool == D3DPOOL_MANAGED &&
			SUCCEEDED(texture->LockRect(level, &locked, nullptr, D3DLOCK_READONLY | D3DLOCK_NO_DIRTY_UPDATE))) {
			const unsigned char* b = (const unsigned char*)locked.pBits;
			auto from565 = [](unsigned short c, float* out) { out[0] = ((c >> 11) & 31) / 31.0f; out[1] = ((c >> 5) & 63) / 63.0f; out[2] = (c & 31) / 31.0f; };
			float c0[3], c1[3];
			switch (desc.Format) {
			case D3DFMT_DXT1: case D3DFMT_DXT2: case D3DFMT_DXT3: case D3DFMT_DXT4: case D3DFMT_DXT5: {
				const unsigned char* block = b + (desc.Format == D3DFMT_DXT1 ? 0 : 8);
				from565(*(const unsigned short*)block, c0);
				from565(*(const unsigned short*)(block + 2), c1);
				for (int i = 0; i < 3; i++) a.rgb[i] = SrgbToLinear(0.5f * (c0[i] + c1[i]));
				a.known = true;
				break;
			}
			case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8:
				a.rgb[0] = SrgbToLinear(b[2] / 255.0f); a.rgb[1] = SrgbToLinear(b[1] / 255.0f); a.rgb[2] = SrgbToLinear(b[0] / 255.0f);
				a.known = true;
				break;
			default: break;
			}
			texture->UnlockRect(level);
		}
		texture->Release();
		return a;
	}

	const Albedo& AlbedoOf(NiGeometry* geo, BSShaderProperty* shaderProperty) {
		static const Albedo unknown = { { 0.5f, 0.5f, 0.5f }, false };
		if (!shaderProperty->IsLightingProperty()) return unknown;
		NiSourceTexture** textures = ((BSShaderPPLightingProperty*)shaderProperty)->ppTextures[0];
		NiTexture* texture = textures ? *textures : nullptr;
		if (!texture || !texture->rendererData || !texture->rendererData->dTexture) return unknown;
		IDirect3DBaseTexture9* d3d = texture->rendererData->dTexture;
		auto found = albedoCache.find(d3d);
		if (found == albedoCache.end()) {
			if (albedoCache.size() > 65536) albedoCache.clear();	// textures come and go with the cells: keep it bounded
			found = albedoCache.emplace(d3d, ReadAlbedo(d3d)).first;
		}
		return found->second;
	}

	// UNOFFICIAL 2026-10-30 the glow / emissive flag (NVR_RT_RECORD_GLOW; rt-reflections' GlowBrightness reads it): the draw's shader has
	// a glow map (BSShaderPPLightingProperty texture slot 2: neon, lit signs and windows, lamp globes).
	bool HasGlowMap(BSShaderProperty* shaderProperty) {
		const UInt32 type = static_cast<NiShadeProperty*>((NiProperty*)shaderProperty)->m_eShaderType;
		if (!shaderProperty->IsLightingProperty() && type != NiShadeProperty::kProp_PPLighting && type != NiShadeProperty::kProp_Lighting30) return false;
		NiSourceTexture** glow = ((BSShaderPPLightingProperty*)shaderProperty)->ppTextures[2];
		return glow && *glow && (*glow)->rendererData && (*glow)->rendererData->dTexture;
	}
	IDirect3DBaseTexture9* GlowTextureOf(BSShaderProperty* shaderProperty) {
		return HasGlowMap(shaderProperty) ? (*((BSShaderPPLightingProperty*)shaderProperty)->ppTextures[2])->rendererData->dTexture : nullptr;
	}

	// Where a vertex declaration keeps the position (cached per declaration).
	struct PositionElement {
		int stream = -1; UInt32 offset = 0; NVR_EXT_RT_POSITION_FORMAT format = NVR_EXT_RT_POSITION_FLOAT3;
		int uvStream = -1; UInt32 uvOffset = 0; bool uvHalf = false;	// TEXCOORD0 (FLOAT2 / FLOAT16_2): alpha-tested casters
		// skinned actors: BLENDWEIGHT / BLENDINDICES (stream, offset, NVR_EXT_RT_SKIN_* format, weights stored)
		int weightStream = -1; UInt32 weightOffset = 0, weightFormat = 0, weightCount = 0;
		int indexStream = -1; UInt32 indexOffset = 0, indexFormat = 0;
	};

	struct PendingCopy { int image; IUnknown* target; UINT width, height; };	// 0, 0: the image's registered size

	struct Core {
		enum State { Idle, Starting, Running, Off };
		State		state = Idle;
		std::vector<RtCore::User> users;
		ID3D9NvrExtShare*		share = nullptr;
		ID3D9NvrExtRt*			rt = nullptr;
		ID3D9NvrExtRtSkin*		rtSkin = nullptr;	// fork patch 0033 (skinned actors); null without it
		ID3D9NvrExtRtSeq*		rtSeq = nullptr;	// fork patch 0034 (the arena's sequence texel, 2026-10-30); null without it
		bool		actorsOn = false;					// [Main.Main.RayTracing] Actors (read at each helper start)
		float		actorRadius = 3000.0f;				// [Main.Main.RayTracing] ActorRadius (live)
		UInt32		actorInterval = 2;					// [Main.Main.RayTracing] ActorUpdateInterval (live)
		UInt32		actorPartitions = 0;				// skinned partitions staged (exchanges 1-60)
		UInt32		partitionFate[5] = {};
		UInt32		skinTex[6] = {};	// hit textures on actors: not PPLighting, no diffuse, texcoords elsewhere, no slot, staged without texcoords, textured				// skipped: no buffer data, too many bones, not described, streams / half positions, palette
		std::vector<ID3D9NvrExtSharedImage*> images;	// [0] = the arena
		std::vector<std::string> imageNames;
		std::vector<NVR_EXT_SHARED_IMAGE_DESC> imageDescs;	// the sizes made (rounded up: kImageAlign)
		std::vector<std::pair<UINT, UINT>> imageSizes;			// the sizes the users asked for (what CopyOut copies)
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
		NvrRtFrame*	record = nullptr;			// the ring slot being written (null outside an exchange)
		bool		recording = false;
		std::vector<PendingCopy> copiesOut;
		// UNOFFICIAL 2026-10-28 (RT reflections) the second exchange (RtCore::Exchange): the scene is gathered once per game frame; a
		// later exchange of the same frame sends this frame's instance list again (no geometry ops: those went with the first record).
		UInt32		gatheredFrame = 0;			// TheFrameRateManager->FrameNumber of the last Gather (+1, 0 = never)
		std::vector<NvrRtInstance> frameInstances;
		std::vector<NvrRtInstanceRecord> frameRecords;	// (protocol v2) the instances' records, resent with them

		// the scene
		std::unordered_map<GeometryKey, GeometryEntry, GeometryKeyHash> geometries;
		std::unordered_map<const void*, PositionElement> declarations;
		UInt32		nextId = 1;
		UInt32		sceneFrame = 0;
		UInt32		arenaUsed = 0, arenaCapacity = 0;
		// Why geometry was left out ([0] objects, [1] land), counted from exchange 1 to 60 and logged with the scene at 60
		enum Skip { kSeen, kNoShader, kSkinned, kNotLighting, kFlags, kLeaf, kAlpha, kDescribe, kMeasure, kTaken, kSkipCount };
		UInt32		skips[3][kSkipCount] = {};	// [0] objects, [1] land, [2] LOD land
		UInt32		landCells[3] = {};		// cells walked, cells without a land node, land nodes culled (APP_CULLED)
		UInt32		landWalk[5] = {};
		// Alpha-tested casters ([Main.Main.RayTracing] AlphaTested, read at each helper start): the alpha atlas (a shared R16F image,
		// 64 x 64 a texture) and the textures already in it
		bool		alphaOn = false;
		// alpha-tested meshes by fate (exchanges 1-60): [0] blended only, [1] feature off, [2] not described, [3] texcoords not in the
		// position's stream, [4] no diffuse texture, [5] alpha unreadable (format / pool) or atlas full, [6] waited (upload budget), [7] taken
		UInt32		alphaFate[8] = {};
		int			atlasImage = -1;
		UInt32		atlasUsed = 0, atlasThisFrame = 0, glowSlots = 0;	// glowSlots: slots with a glow map (GlowTextures)
		std::unordered_map<UInt64, int> alphaSlots;			// diffuse texture | glow texture << 32 -> slot (-1: unreadable)
		IDirect3DTexture9* alphaUpload = nullptr;			// 64 x 64 R16F, dynamic: one slot's alpha on its way to the atlas
		IDirect3DTexture9* albedoUpload = nullptr;			// 64 x 64 A8R8G8B8: one slot's colour on its way to the albedo atlas
		bool		hitTextures = false;				// [Main.Main.RayTracing] HitTextures (read at each helper start)
		int			albedoAtlasImage = -1;
		// UNOFFICIAL 2026-10-30 glow textures ([Main.Main.RayTracing] GlowTextures, with HitTextures; rt-ao-sky GlowGI / rt-reflections
		// GlowBrightness): a slot is a (diffuse, glow map) pair; the glow map goes into the glow atlas (RGBA8) at the same slot and texcoords
		IDirect3DTexture9* glowUpload = nullptr;
		int			glowAtlasImage = -1;
		PositionElement lastElement;						// Describe's: where the texcoords are
		int			AlphaSlotOf(IDirect3DBaseTexture9* texture, IDirect3DBaseTexture9* glow = nullptr);		// land subtree: objects popped, APP_CULLED, nodes, faded fade nodes (alpha <= 0, walked anyway), switch nodes
		UInt32		instances = 0, waiting = 0, released = 0;
		float		radius = 8192.0f;
		// UNOFFICIAL 2026-10-30 scene cost (the main session's soak list, item 2), all [Main.Main.RayTracing], live:
		//   Compact: static geometry is compacted by the helper after its build (NVR_RT_OP_FLAG_COMPACT; about half the memory).
		//   BuildTrianglesPerFrame: new geometry farther than BuildNearRadius waits a frame once this many triangles were staged this
		//     frame (0: no limit, as before): a cell change builds its near geometry at once and the rest over the next frames.
		//   CullSmallAngle: objects (not land, not actors) whose bound radius / distance is below this (radians) stay out of the scene.
		bool		compactOn = false;
		UInt32		buildBudget = 0, trianglesThisFrame = 0, budgetWaits = 0, culledSmall = 0;
		float		buildNear = 2048.0f, cullAngle = 0.0f;
		std::vector<UInt32> pendingReleases;	// release ops that did not fit this frame's op list (sent first next frame)
		void		Release(UInt32 id) { pendingReleases.push_back(id); released++; }
		std::vector<NiAVObject*> stack;

		void		TurnOff(const char* why);
		bool		Start();
		void		Stop(const char* why);
		void		Poll();
		void		Gather();
		void		WalkCell(TESObjectCELL* cell);
		void		Walk(NiAVObject* root, bool land, bool lod = false);
		void		AddGeometry(NiGeometry* geo, bool land, bool lod = false);
		void		AddSkinned(NiGeometry* geo);
		bool		DescribeData(NiGeometryBufferData* data, const char* name, NVR_EXT_RT_GEOMETRY_DESC& desc, std::vector<NVR_EXT_RT_RANGE>& ranges, GeometryKey& key);
		bool		Describe(NiGeometry* geo, NVR_EXT_RT_GEOMETRY_DESC& desc, std::vector<NVR_EXT_RT_RANGE>& ranges, GeometryKey& key);
		void		LogHelperMessage();
		bool		Ready();					// Frame's checks before any exchange (start, poll, screen size): true = may exchange now
		bool		Exchange(IDirect3DDevice9* Device, const RtCore::User* only);	// one frame record: Point-0 users (only = null) or one user
		static DWORD WINAPI WatchdogMain(void* self);
	};

	Core TheCore;

	// A texture's slot in the alpha atlas: >= 0, -1 not possible (never again), -2 not this frame (8 a frame at most). On the GPU: the
	// fork pass RtAlphaToAtlas draws the texture's alpha into a 64 x 64 R16F target (the texture's mips downsample; any pool and format),
	// then that goes into the atlas slot. Both are recorded before this exchange's signal, so the helper sees them this frame.
	int Core::AlphaSlotOf(IDirect3DBaseTexture9* texture, IDirect3DBaseTexture9* glow) {
		if (glowAtlasImage < 0) glow = nullptr;
		const UInt64 key = (UInt64)(uintptr_t)texture | ((UInt64)(uintptr_t)glow << 32);	// a 32-bit process
		auto found = alphaSlots.find(key);
		if (found != alphaSlots.end()) return found->second;
		const UInt32 capacity = NVR_RT_ALPHA_ATLAS_SLOTS * NVR_RT_ALPHA_ATLAS_SLOTS;
		if (atlasImage < 0 || atlasUsed >= capacity) return -1;
		if (atlasThisFrame >= 8) return -2;
		atlasThisFrame++;
		const UInt32 S = NVR_RT_ALPHA_SLOT_SIZE;
		static ID3D9NvrExtPipeline* pipelines[2] = {};	// [0] alpha only (RtAlphaToAtlas), [1] alpha + colour (RtTextureToAtlas)
		static bool triedPipelines[2] = {};
		const int which = albedoAtlasImage >= 0 ? 1 : 0;
		ID3D9NvrExtPipeline*& pipeline = pipelines[which];
		if (!triedPipelines[which]) {
			triedPipelines[which] = true;
			std::vector<char> spirv;
			ID3D9NvrExt* ext = DxvkCompute::Extension();
			if (ext && DxvkCompute::LoadSpirv(which ? "RtTextureToAtlas" : "RtAlphaToAtlas", spirv)) {
				NVR_EXT_BINDING_DESC bindings[2] = {
					{ 1, 0, NVR_EXT_BIND_SAMPLED_IMAGE, 0, NVR_EXT_VIEW_2D, FALSE, 0 },
					{ 0, 0, NVR_EXT_BIND_SAMPLER, 0, NVR_EXT_VIEW_2D, FALSE, 4 },
				};
				NVR_EXT_GRAPHICS_PIPELINE_DESC desc = {};
				desc.pFragmentSpirv = spirv.data();
				desc.FragmentSpirvSize = (UINT)spirv.size();
				desc.BindingCount = 2;
				desc.pBindings = bindings;
				desc.PushConstantSize = 16;
				desc.UsesSamplerHeap = TRUE;
				desc.SamplerHeapSet = 0;
				desc.SamplerHeapBinding = 0;
				desc.ColorTargetCount = which ? 2 : 1;
				desc.Blend = NVR_EXT_BLEND_NONE;
				desc.ColorWriteMask = 0xFu;
				desc.pDebugName = which ? "RtTextureToAtlas" : "RtAlphaToAtlas";
				if (FAILED(ext->CreateGraphicsPipeline(&desc, &pipeline))) pipeline = nullptr;
			}
			if (!pipeline) Logger::Log("UNOFFICIAL NVR DXVK: RT alpha-tested casters: the alpha pass could not be made (Compute\\RtAlphaToAtlas.spv; the DXVK log says why)");
		}
		int slot = -1;
		if (!alphaUpload) TheRenderManager->device->CreateTexture(S, S, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R16F, D3DPOOL_DEFAULT, &alphaUpload, nullptr);
		if (which && !albedoUpload) TheRenderManager->device->CreateTexture(S, S, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &albedoUpload, nullptr);
		if (glow && !glowUpload) TheRenderManager->device->CreateTexture(S, S, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &glowUpload, nullptr);
		ID3D9NvrExtSampler* sampler = DxvkCompute::MipSampler(false, NVR_EXT_ADDRESS_WRAP);
		IDirect3DSurface9* target = nullptr;
		IDirect3DSurface9* target2 = nullptr;
		if (which && (!albedoUpload || FAILED(albedoUpload->GetSurfaceLevel(0, &target2)))) target2 = nullptr;
		if (pipeline && sampler && alphaUpload && (!which || target2) && SUCCEEDED(alphaUpload->GetSurfaceLevel(0, &target))) {
			NVR_EXT_RESOURCE_BINDING bindings[2] = {};
			bindings[0].Slot = 0; bindings[0].Type = NVR_EXT_BIND_SAMPLED_IMAGE; bindings[0].pResource = texture;
			bindings[1].Slot = 0; bindings[1].Type = NVR_EXT_BIND_SAMPLER; bindings[1].pResource = sampler;
			const UInt32 push[4] = { 0, 0, 0, 0 };
			NVR_EXT_DRAW_DESC draw = {};
			draw.pPipeline = pipeline;
			draw.TargetCount = which ? 2 : 1;
			draw.Targets[0].pResource = target;
			draw.Targets[1].pResource = target2;
			draw.BindingCount = 2;
			draw.pBindings = bindings;
			draw.pPushData = push;
			draw.PushDataSize = sizeof(push);
			const RECT all = { 0, 0, (LONG)S, (LONG)S };
			const UInt32 at = atlasUsed;
			const UInt32 x = (at % NVR_RT_ALPHA_ATLAS_SLOTS) * S, y = (at / NVR_RT_ALPHA_ATLAS_SLOTS) * S;
			if (SUCCEEDED(DxvkCompute::Extension()->Draw(&draw)) && SUCCEEDED(share->CopyToShared(images[atlasImage], x, y, alphaUpload, &all)) &&
				(!which || SUCCEEDED(share->CopyToShared(images[albedoAtlasImage], x, y, albedoUpload, &all)))) {
				slot = (int)at;
				if (glow) {	// the same pass on the glow map: its alpha into the alpha upload (already copied, in stream order: a scratch now)
					IDirect3DSurface9* glowTarget = nullptr;
					bindings[0].pResource = glow;
					if (!glowUpload || FAILED(glowUpload->GetSurfaceLevel(0, &glowTarget))) slot = -1;
					else {
						draw.Targets[1].pResource = glowTarget;
						if (FAILED(DxvkCompute::Extension()->Draw(&draw)) || FAILED(share->CopyToShared(images[glowAtlasImage], x, y, glowUpload, &all))) slot = -1;
						glowTarget->Release();
					}
				}
				if (slot >= 0) { atlasUsed++; if (glow) glowSlots++; }
			}
			target->Release();
		}
		if (target2) target2->Release();
		alphaSlots.emplace(key, slot);
		return slot;
	}
	UInt32 (*MaskHook)(NiGeometry*, bool) = nullptr;

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
			else if (now - lastProgress > 10000) { why = "no frame came back from the helper for 10 seconds"; break; }
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
		if (FAILED(ext->QueryInterface(__uuidof(ID3D9NvrExtRtSkin), (void**)&rtSkin))) rtSkin = nullptr;
		if (FAILED(ext->QueryInterface(__uuidof(ID3D9NvrExtRtSeq), (void**)&rtSeq))) rtSeq = nullptr;
		if (!rtSeq) Logger::Log("UNOFFICIAL NVR DXVK: RT: this d3d9.dll has no arena sequence check (fork patch 0034, 1.8-rt-seq); on some GPUs its "
			"geometry copy can run before the geometry is converted (RTX GPU fault, 2026-10-30): update the NVR DXVK fork.");
		actorsOn = TheSettingManager->GetSettingI("Main.Main.RayTracing", "Actors") != 0;
		if (actorsOn && !rtSkin) Logger::Log("UNOFFICIAL NVR DXVK: RT actors off: this d3d9.dll cannot pose skinned geometry (fork patch 0033, 1.8-rt-skin).");
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
		imageSizes.clear();
		const UINT arenaMB = (std::max)(4, (std::min)(256, TheSettingManager->GetSettingI("Main.Main.RayTracing", "ArenaMB")));
		imageNames.push_back("NVR RT arena");
		imageDescs.push_back({ kArenaWidth, AlignImage(arenaMB * 1024u * 1024u / 4u / kArenaWidth), NVR_EXT_SHARED_R32F, nullptr });
		imageSizes.push_back({ imageDescs.back().Width, imageDescs.back().Height });
		alphaOn = TheSettingManager->GetSettingI("Main.Main.RayTracing", "AlphaTested") != 0;
		hitTextures = TheSettingManager->GetSettingI("Main.Main.RayTracing", "HitTextures") != 0;
		atlasImage = -1;
		albedoAtlasImage = -1;
		glowAtlasImage = -1;
		atlasUsed = 0;
		glowSlots = 0;
		alphaSlots.clear();
		if (alphaOn || hitTextures) {	// the slots are shared: hit textures keep the alpha atlas too
			atlasImage = (int)imageDescs.size();
			imageNames.push_back("NVR RT alpha atlas");
			const UINT side = NVR_RT_ALPHA_SLOT_SIZE * NVR_RT_ALPHA_ATLAS_SLOTS;
			imageDescs.push_back({ side, side, NVR_EXT_SHARED_R16F, nullptr });
			imageSizes.push_back({ side, side });
		}
		if (hitTextures) {
			albedoAtlasImage = (int)imageDescs.size();
			imageNames.push_back("NVR RT albedo atlas");
			const UINT side = NVR_RT_ALPHA_SLOT_SIZE * NVR_RT_ALPHA_ATLAS_SLOTS;
			imageDescs.push_back({ side, side, NVR_EXT_SHARED_RGBA8, nullptr });
			imageSizes.push_back({ side, side });
			if (TheSettingManager->GetSettingI("Main.Main.RayTracing", "GlowTextures") != 0) {
				glowAtlasImage = (int)imageDescs.size();
				imageNames.push_back("NVR RT glow atlas");
				imageDescs.push_back({ side, side, NVR_EXT_SHARED_RGBA8, nullptr });
				imageSizes.push_back({ side, side });
			}
		}
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
		pendingReleases.clear();
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
		DxvkCompute::Drop(rtSkin);
		DxvkCompute::Drop(rtSeq);
		DxvkCompute::Drop(share);
		if (shared) { UnmapViewOfFile(shared); shared = nullptr; }
		HANDLE* handles[] = { &mapping, &frameEvent, &statusEvent };
		for (HANDLE* h : handles) if (*h) { CloseHandle(*h); *h = nullptr; }
		geometries.clear();
		pendingReleases.clear();
		alphaSlots.clear();
		if (alphaUpload) { alphaUpload->Release(); alphaUpload = nullptr; }
		if (albedoUpload) { albedoUpload->Release(); albedoUpload = nullptr; }
		if (glowUpload) { glowUpload->Release(); glowUpload = nullptr; }
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
			if (e.Usage == D3DDECLUSAGE_TEXCOORD && e.UsageIndex == 0 && (e.Type == D3DDECLTYPE_FLOAT2 || e.Type == D3DDECLTYPE_FLOAT16_2)) {
				out.uvStream = e.Stream; out.uvOffset = e.Offset; out.uvHalf = e.Type == D3DDECLTYPE_FLOAT16_2;
			}
			if (e.Usage == D3DDECLUSAGE_BLENDWEIGHT && e.UsageIndex == 0) {
				UInt32 format = 99, count = 0;
				switch (e.Type) {
				case D3DDECLTYPE_FLOAT1: format = NVR_EXT_RT_SKIN_WEIGHTS_FLOAT; count = 1; break;
				case D3DDECLTYPE_FLOAT2: format = NVR_EXT_RT_SKIN_WEIGHTS_FLOAT; count = 2; break;
				case D3DDECLTYPE_FLOAT3: format = NVR_EXT_RT_SKIN_WEIGHTS_FLOAT; count = 3; break;
				case D3DDECLTYPE_FLOAT4: format = NVR_EXT_RT_SKIN_WEIGHTS_FLOAT; count = 4; break;
				case D3DDECLTYPE_FLOAT16_2: format = NVR_EXT_RT_SKIN_WEIGHTS_HALF; count = 2; break;
				case D3DDECLTYPE_FLOAT16_4: format = NVR_EXT_RT_SKIN_WEIGHTS_HALF; count = 4; break;
				case D3DDECLTYPE_UBYTE4N: format = NVR_EXT_RT_SKIN_WEIGHTS_UBYTE4N; count = 4; break;
				default: break;
				}
				if (format != 99) { out.weightStream = e.Stream; out.weightOffset = e.Offset; out.weightFormat = format; out.weightCount = count; }
			}
			if (e.Usage == D3DDECLUSAGE_BLENDINDICES && e.UsageIndex == 0 && (e.Type == D3DDECLTYPE_UBYTE4 || e.Type == D3DDECLTYPE_D3DCOLOR)) {
				out.indexStream = e.Stream; out.indexOffset = e.Offset;
				out.indexFormat = e.Type == D3DDECLTYPE_D3DCOLOR ? NVR_EXT_RT_SKIN_INDICES_D3DCOLOR : NVR_EXT_RT_SKIN_INDICES_UBYTE4;
			}
			if (e.Usage != D3DDECLUSAGE_POSITION || e.UsageIndex != 0 || out.stream >= 0) continue;
			if (e.Type == D3DDECLTYPE_FLOAT3) out.format = NVR_EXT_RT_POSITION_FLOAT3;
			else if (e.Type == D3DDECLTYPE_FLOAT4) out.format = NVR_EXT_RT_POSITION_FLOAT4;
			else if (e.Type == D3DDECLTYPE_FLOAT16_4) out.format = NVR_EXT_RT_POSITION_HALF4;
			else return false;
			out.stream = e.Stream;
			out.offset = e.Offset;
		}
		return out.stream >= 0;
	}

	bool Core::Describe(NiGeometry* geo, NVR_EXT_RT_GEOMETRY_DESC& desc, std::vector<NVR_EXT_RT_RANGE>& ranges, GeometryKey& key) {
		return DescribeData(geo->geomData->m_pkBuffData, geo->m_pcName, desc, ranges, key);
	}

	bool Core::DescribeData(NiGeometryBufferData* data, const char* name, NVR_EXT_RT_GEOMETRY_DESC& desc, std::vector<NVR_EXT_RT_RANGE>& ranges, GeometryKey& key) {
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

		lastElement = position;
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
		desc.pDebugName = name;
		return true;
	}

	// UNOFFICIAL 2026-10-29 skinned actors ([Main.Main.RayTracing] Actors; fork patch 0033 ID3D9NvrExtRtSkin): every partition of a
	// skinned geometry, posed every frame by the fork (palette = bone world x SkinToBone, Gamebryo's skinning, scene-relative), built
	// fast in the helper, placed with the identity. Mask NVR_RT_MASK_ACTOR.
	void Core::AddSkinned(NiGeometry* geo) {
		UInt32* skip = skips[0];
		NiSkinInstance* skin = geo->skinInstance;
		NiSkinPartition* parts = skin ? skin->SkinPartition : nullptr;
		if (!rtSkin || !parts || !parts->Partitions || !skin->SkinData || !skin->SkinData->BoneData || !skin->BoneObjects) { skip[kSkinned]++; return; }
		// Any shader: the bodies' skin shader is not a lighting property (rtsun-16: only the hidden dismemberment caps were in)
		BSShaderProperty* shaderProperty = (BSShaderProperty*)geo->GetProperty(NiProperty::PropertyType::kType_Shade);
		if (!shaderProperty) { skip[kNotLighting]++; return; }
		// Dismemberment: a skin that does not render (the gore caps until a limb comes off) or a disabled partition stays out
		BSDismemberSkinInstance* dismember = *(void**)skin == Pointers::VirtualTables::BSDismemberSkinInstance ? (BSDismemberSkinInstance*)skin : nullptr;
		if (dismember && !dismember->IsRenderable) { skip[kSkinned]++; return; }
		NiAlphaProperty* alpha = (NiAlphaProperty*)geo->GetProperty(NiProperty::PropertyType::kType_Alpha);
		// Bodies are alpha-tested (the hair and fringe edges): they go in opaque (rtsun-13: with them left out, only heads and hands were
		// in the scene). Blended-only (glass, fx) stays out.
		if (alpha && (alpha->flags & NiAlphaProperty::AlphaFlags::ALPHA_BLEND_MASK) && !(alpha->flags & NiAlphaProperty::AlphaFlags::TEST_ENABLE_MASK)) { skip[kAlpha]++; return; }
		// The skin shader is not a "lighting property" by its shader index, but its class is BSShaderPPLightingProperty (the textures)
		const UInt32 shaderType = static_cast<NiShadeProperty*>((NiProperty*)shaderProperty)->m_eShaderType;
		const bool ppLighting = shaderProperty->IsLightingProperty() || shaderType == NiShadeProperty::kProp_PPLighting || shaderType == NiShadeProperty::kProp_Lighting30;
		static const Albedo grey = { { 0.5f, 0.5f, 0.5f }, false };
		const Albedo& albedo = ppLighting ? AlbedoOf(geo, shaderProperty) : grey;
		IDirect3DBaseTexture9* diffuse = nullptr;	// hit textures: the body's diffuse texture
		if (hitTextures && !ppLighting) skinTex[0]++;
		if (hitTextures && ppLighting) {
			NiSourceTexture** textures = ((BSShaderPPLightingProperty*)shaderProperty)->ppTextures[0];
			NiTexture* texture = textures ? *textures : nullptr;
			if (texture && texture->rendererData) diffuse = texture->rendererData->dTexture;
			if (!diffuse) skinTex[1]++;
		}
		const float cx = TheRenderManager->CameraPosition.x, cy = TheRenderManager->CameraPosition.y, cz = TheRenderManager->CameraPosition.z;
		static std::vector<NVR_EXT_RT_RANGE> ranges;
		static std::vector<float> palette;
		static int told = 0;
		if (told < 16) {	// the first skinned geometries, once: what their partitions look like
			told++;
			char line[512];
			int n = sprintf_s(line, "RT CORE: skinned '%s': %u partitions, own buffer data %p:", geo->m_pcName ? geo->m_pcName : "", parts->PartitionsCount,
				geo->geomData ? geo->geomData->m_pkBuffData : nullptr);
			for (UInt32 p = 0; p < parts->PartitionsCount && p < 8 && n > 0 && n < 440; p++) {
				const NiSkinPartition::Partition& part = parts->Partitions[p];
				n += sprintf_s(line + n, sizeof(line) - n, " [%u: buf %p, %u bones, %u a vertex, %u verts, %u tris]", p, part.BuffData, part.Bones,
					part.BonesPerVertex, part.Vertices, part.Triangles);
			}
			Logger::Log("%s", line);
		}
		for (UInt32 p = 0; p < parts->PartitionsCount; p++) {
			const NiSkinPartition::Partition& part = parts->Partitions[p];
			if (!part.BuffData) { partitionFate[0]++; continue; }
			if (dismember && dismember->partitions && p < dismember->partitionNumber && !dismember->partitions[p].Enabled) { partitionFate[1]++; continue; }
			if (!part.pBones || !part.Bones || part.Bones > NVR_EXT_RT_SKIN_MAX_BONES || !part.BonesPerVertex) { partitionFate[1]++; continue; }
			NVR_EXT_RT_GEOMETRY_DESC desc;
			GeometryKey key;
			if (!DescribeData(part.BuffData, geo->m_pcName, desc, ranges, key)) { skip[kDescribe]++; partitionFate[2]++; continue; }
			const PositionElement& e = lastElement;
			if (e.weightStream != e.stream || e.indexStream != e.stream || e.format == NVR_EXT_RT_POSITION_HALF4) { skip[kDescribe]++; partitionFate[3]++; continue; }
			// the palette: partition bone k = the skin's bone part.pBones[k]
			palette.resize(part.Bones * 12u);
			bool ok = true;
			for (UInt32 k = 0; k < part.Bones && ok; k++) {
				const UInt32 g = part.pBones[k];
				NiAVObject* bone = g < skin->SkinData->Bones ? skin->BoneObjects[g] : nullptr;
				if (!bone) { ok = false; break; }
				const NiTransform& a = bone->m_worldTransform;
				const NiTransform& b = skin->SkinData->BoneData[g].SkinToBone;
				float* m = &palette[k * 12u];
				const float scale = a.scale * b.scale;
				for (int r = 0; r < 3; r++) {
					for (int c = 0; c < 3; c++)
						m[r * 4 + c] = scale * (a.rot.data[r][0] * b.rot.data[0][c] + a.rot.data[r][1] * b.rot.data[1][c] + a.rot.data[r][2] * b.rot.data[2][c]);
					const float t = a.rot.data[r][0] * b.pos.x + a.rot.data[r][1] * b.pos.y + a.rot.data[r][2] * b.pos.z;
					m[r * 4 + 3] = (r == 0 ? a.pos.x : r == 1 ? a.pos.y : a.pos.z) + a.scale * t - (r == 0 ? cx : r == 1 ? cy : cz);
				}
			}
			if (!ok) { skip[kSkinned]++; partitionFate[4]++; continue; }
			NVR_EXT_RT_SKIN_DESC s = {};
			s.WeightsDelta = (INT)e.weightOffset - (INT)e.offset;
			s.WeightsFormat = (NVR_EXT_RT_SKIN_WEIGHTS)e.weightFormat;
			s.WeightCount = (std::min)(e.weightCount, (UInt32)part.BonesPerVertex);
			s.BonesPerVertex = (std::min)((UInt32)part.BonesPerVertex, (UInt32)4);
			s.IndicesDelta = (INT)e.indexOffset - (INT)e.offset;
			// The game's skin shaders read the indices as blendIndices.zyxw * 765.01 (EnvTemplate.hlsl): a D3DCOLOR's swizzle undone, so
			// bone k is byte k in memory whatever the declared type (rtsun-14: un-swizzling it again posed the multi-bone torso wrong)
			s.IndicesFormat = NVR_EXT_RT_SKIN_INDICES_UBYTE4;
			s.PaletteCount = part.Bones;
			s.pPalette = palette.data();
			// Hit textures: the texcoords go with the posed positions (fork 0033 stages them for skinned geometry too), the colour in the atlas
			int slot = -1;
			if (diffuse && e.uvStream != e.stream) skinTex[2]++;
			if (diffuse && e.uvStream == e.stream) {
				slot = AlphaSlotOf(diffuse);
				if (slot >= 0) desc.Flags = NVR_EXT_RT_TEXCOORD_FLAGS((int)e.uvOffset - (int)e.offset, e.uvHalf);
				else skinTex[3]++;
			}

			GeometryEntry& entry = geometries[key];
			if (entry.refused) continue;
			entry.lastSeen = sceneFrame;
			entry.skinned = true;
			if (!entry.id) entry.id = nextId++ & 0xFFFFFF;
			// ActorUpdateInterval: a posed partition is re-posed only every Nth frame (staggered by id), the frames between keep the
			// last pose (a frame or two late: unseen in shadows; rt-lamp: actors were most of the RT cost indoors, ~60 partitions a frame)
			const bool pose = !entry.sent || actorInterval <= 1 || ((sceneFrame + entry.id) % actorInterval) == 0;
			NVR_EXT_RT_GEOMETRY_INFO info = {};
			if (pose && FAILED(rt->MeasureGeometry(&desc, &info))) { entry.refused = true; skip[kMeasure]++; continue; }
			if (record->instanceCount >= NVR_RT_MAX_INSTANCES) { waiting++; continue; }
			bool hasUv = entry.textured;	// what the helper has: decided by what the fork staged (an older 0033 drops skinned texcoords)
			if (pose) {
				if (record->opCount >= NVR_RT_MAX_OPS || arenaUsed + info.Texels + 1 > arenaCapacity) { waiting++; if (!entry.sent) continue; }
				else if (FAILED(rtSkin->StageSkinnedGeometry(&desc, &s, images[0], arenaUsed, &info))) { entry.refused = true; skip[kMeasure]++; continue; }
				else {
					NvrRtGeometryOp& op = record->ops[record->opCount++];
					op = NvrRtGeometryOp();
					op.op = NVR_RT_OP_BUILD;
					op.id = entry.id;
					op.vertexCount = info.VertexCount;
					op.triangleCount = info.TriangleCount;
					op.arenaTexel = arenaUsed;
					hasUv = slot >= 0 && info.Texels == 3u * (info.VertexCount + info.TriangleCount) + 2u * info.VertexCount;
					entry.textured = hasUv;
					if (slot >= 0) skinTex[hasUv ? 5 : 4]++;
					op.flags = NVR_RT_OP_FLAG_DYNAMIC | (hasUv ? NVR_RT_OP_FLAG_TEXCOORDS : 0u);
					arenaUsed += info.Texels;
					entry.sent = true;
					actorPartitions++;
				}
			}

			NvrRtInstanceRecord& extra = record->records[record->instanceCount];
			extra.albedo[0] = albedo.rgb[0]; extra.albedo[1] = albedo.rgb[1]; extra.albedo[2] = albedo.rgb[2]; extra.albedo[3] = 1.0f;
			extra.flags = (albedo.known ? NVR_RT_RECORD_ALBEDO_KNOWN : 0u) | (hasUv && slot >= 0 ? NVR_RT_RECORD_TEXTURED | ((UInt32)slot << 16) : 0u);
			extra.geometryId = entry.id;
			extra.vertexOffset = extra.indexOffset = 0;
			NvrRtInstance& instance = record->instances[record->instanceCount++];
			memset(instance.transform, 0, sizeof(instance.transform));
			instance.transform[0][0] = instance.transform[1][1] = instance.transform[2][2] = 1.0f;	// posed in scene space already
			instance.geometryId = entry.id;
			instance.customIndex = 0;
			instance.mask = NVR_RT_MASK_ACTOR;
			instance.flags = 0x1u;
			skip[kTaken]++;
		}
	}

	void Core::AddGeometry(NiGeometry* geo, bool land, bool lod) {
		UInt32* skip = skips[lod ? 2 : land ? 1 : 0];
		skip[kSeen]++;
		float distance = 0.0f;	// the bound's centre from the camera (objects only: land's bounds are not usable)
		if (!land && geo->m_kWorldBound) {
			const NiBound* b = geo->m_kWorldBound;
			const float dx = b->Center.x - TheRenderManager->CameraPosition.x, dy = b->Center.y - TheRenderManager->CameraPosition.y,
				dz = b->Center.z - TheRenderManager->CameraPosition.z;
			distance = sqrtf(dx * dx + dy * dy + dz * dz);
			if (cullAngle > 0.0f && distance > b->Radius && b->Radius < cullAngle * distance) { culledSmall++; return; }
		}
		if (!geo->shader || !geo->geomData || !geo->geomData->m_pkBuffData) { skip[kNoShader]++; return; }
		if (geo->skinInstance) { skip[kSkinned]++; return; }
		BSShaderProperty* shaderProperty = (BSShaderProperty*)geo->GetProperty(NiProperty::PropertyType::kType_Shade);
		if (!shaderProperty || !shaderProperty->IsLightingProperty()) { skip[kNotLighting]++; return; }
		if (shaderProperty->GetFlag(BSSP_REFRACTION) || shaderProperty->GetFlag(BSSP_FIRE_REFRACTION) || shaderProperty->GetFlag(BSSP_DECAL) ||
			shaderProperty->GetFlag(BSSP_DYNAMIC_DECAL)) { skip[kFlags]++; return; }
		if (static_cast<NiShadeProperty*>((NiProperty*)shaderProperty)->m_eShaderType == NiShadeProperty::kProp_SpeedTreeLeaf) { skip[kLeaf]++; return; }
		// Alpha-tested and blended geometry (foliage cards, fences, glass) is not traced yet: the cascades keep it (FORK-RT-SUN.md).
		NiAlphaProperty* alpha = (NiAlphaProperty*)geo->GetProperty(NiProperty::PropertyType::kType_Alpha);
		// Alpha-tested casters (AlphaTested on): traced with their diffuse alpha (RtAlpha.glsl); blended-only (glass, fx) stays out
		bool alphaTested = false;
		if (alpha && (alpha->flags & (NiAlphaProperty::AlphaFlags::ALPHA_BLEND_MASK | NiAlphaProperty::AlphaFlags::TEST_ENABLE_MASK))) {
			if (!(alpha->flags & NiAlphaProperty::AlphaFlags::TEST_ENABLE_MASK)) { skip[kAlpha]++; alphaFate[0]++; return; }
			if (!alphaOn) { skip[kAlpha]++; alphaFate[1]++; return; }
			alphaTested = true;
		}

		NVR_EXT_RT_GEOMETRY_DESC desc;
		static std::vector<NVR_EXT_RT_RANGE> ranges;
		GeometryKey key;
		if (!Describe(geo, desc, ranges, key)) {
			if (alphaTested) { skip[kAlpha]++; alphaFate[2]++; return; }
			skip[kDescribe]++;
			static int told = 0;
			NiGeometryBufferData* bd = geo->geomData->m_pkBuffData;
			if (land && told < 2) {
				told++;
				Logger::Log("RT CORE: land geometry '%s' not described: VertCount %u, IB %p, streams %u, FVF %X, declaration %p, primitive %d, arrays %u, tris %u",
					geo->m_pcName ? geo->m_pcName : "", bd->VertCount, bd->IB, bd->StreamCount, bd->FVF, bd->VertexDeclaration, (int)bd->PrimitiveType, bd->NumArrays, bd->TriCount);
			}
			return;
		}

		int alphaSlot = -1;
		if (alphaTested) {
			// the texcoords must sit in the position's stream; the diffuse alpha must be readable
			if (lastElement.uvStream != lastElement.stream || !shaderProperty->IsLightingProperty()) { skip[kAlpha]++; alphaFate[3]++; return; }
			NiSourceTexture** textures = ((BSShaderPPLightingProperty*)shaderProperty)->ppTextures[0];
			NiTexture* texture = textures ? *textures : nullptr;
			if (!texture || !texture->rendererData || !texture->rendererData->dTexture) { skip[kAlpha]++; alphaFate[4]++; return; }
			alphaSlot = AlphaSlotOf(texture->rendererData->dTexture, GlowTextureOf(shaderProperty));
			if (alphaSlot == -2) { waiting++; alphaFate[6]++; return; }	// this frame's uploads are used up: next frame
			if (alphaSlot < 0) { skip[kAlpha]++; alphaFate[5]++; return; }
			desc.Flags = NVR_EXT_RT_TEXCOORD_FLAGS((int)lastElement.uvOffset - (int)lastElement.offset, lastElement.uvHalf);
		}
		// Hit textures ([Main.Main.RayTracing] HitTextures): opaque geometry with a diffuse texture keeps its texcoords and gets an atlas
		// slot, so a ray's hit can read the texture's colour (RtHit.glsl). Without a slot this frame (upload budget) it waits a frame.
		bool textured = false;
		if (!alphaTested && hitTextures && lastElement.uvStream == lastElement.stream && shaderProperty->IsLightingProperty()) {
			NiSourceTexture** textures = ((BSShaderPPLightingProperty*)shaderProperty)->ppTextures[0];
			NiTexture* texture = textures ? *textures : nullptr;
			if (texture && texture->rendererData && texture->rendererData->dTexture) {
				alphaSlot = AlphaSlotOf(texture->rendererData->dTexture, GlowTextureOf(shaderProperty));
				if (alphaSlot == -2) { waiting++; return; }
				if (alphaSlot >= 0) {
					textured = true;
					desc.Flags = NVR_EXT_RT_TEXCOORD_FLAGS((int)lastElement.uvOffset - (int)lastElement.offset, lastElement.uvHalf);
				}
			}
		}
		GeometryEntry& entry = geometries[key];
		if (entry.refused) return;
		entry.lastSeen = sceneFrame;
		if (!entry.sent) {
			if (!entry.id) entry.id = nextId++ & 0xFFFFFF;
			NVR_EXT_RT_GEOMETRY_INFO info = {};
			if (FAILED(rt->MeasureGeometry(&desc, &info))) { entry.refused = true; skip[kMeasure]++; return; }
			if ((alphaTested || textured) && info.Texels != 3u * (info.VertexCount + info.TriangleCount) + 2u * info.VertexCount) {
				hitTextures = false;
				alphaOn = false;	// a fork without 1.8-rt-uv (patch 0029): no texcoords staged, so no alpha-tested casters this session
				Logger::Log("UNOFFICIAL NVR DXVK: RT alpha-tested casters off: this d3d9.dll does not stage texcoords (fork patch 0029, 1.8-rt-uv).");
				return;
			}
			if (record->opCount >= NVR_RT_MAX_OPS || arenaUsed + info.Texels + 1 > arenaCapacity) { waiting++; return; }	// next frame (+1: the sequence texel)
			if (buildBudget && !land && distance > buildNear && trianglesThisFrame && trianglesThisFrame + info.TriangleCount > buildBudget) {
				waiting++; budgetWaits++; return;	// far: a later frame (BuildTrianglesPerFrame)
			}
			if (FAILED(rt->StageGeometry(&desc, images[0], arenaUsed, &info))) { entry.refused = true; return; }
			trianglesThisFrame += info.TriangleCount;
			NvrRtGeometryOp& op = record->ops[record->opCount++];
			op = NvrRtGeometryOp();
			op.op = NVR_RT_OP_BUILD;
			op.id = entry.id;
			op.vertexCount = info.VertexCount;
			op.triangleCount = info.TriangleCount;
			op.arenaTexel = arenaUsed;
			op.flags = alphaTested ? (NVR_RT_OP_FLAG_TEXCOORDS | NVR_RT_OP_FLAG_ALPHA) : textured ? NVR_RT_OP_FLAG_TEXCOORDS : 0u;
			if (compactOn) op.flags |= NVR_RT_OP_FLAG_COMPACT;
			arenaUsed += info.Texels;
			entry.sent = true;
			static int probes = 0;
			if (probes < 3) {	// compared with the helper's readback of the same ids (NvrRtHelper.log "probe")
				probes++;
				NiGeometryData* gd = geo->geomData;
				NiGeometryBufferData* bd = gd->m_pkBuffData;
				const NiVBChip* chip = bd->VBChip[0];
				const NiPoint3* v = gd->m_pkVertex;
				D3DMATRIX pm;
				TheRenderManager->CreateD3DMatrix(&pm, &geo->m_worldTransform);
				Logger::Log("RT CORE probe: geometry %u '%s' land %d: %u vertices (VertCount %u, BaseVertexIndex %u, chip offset %u, stride %u, "
					"position offset %u, format %d, FVF %X), %u triangles (%s, %u ranges), model bound %.1f %.1f %.1f r %.1f, CPU vertex 0 %s%.1f %.1f %.1f, "
					"translation (camera relative) %.1f %.1f %.1f, arena texel %u", entry.id, geo->m_pcName ? geo->m_pcName : "", land ? 1 : 0, info.VertexCount,
					bd->VertCount, bd->BaseVertexIndex, chip ? chip->Offset : 0u, desc.VertexStride, desc.PositionOffset, (int)desc.PositionFormat, bd->FVF,
					info.TriangleCount, bd->PrimitiveType == D3DPT_TRIANGLESTRIP ? "strips" : "list", desc.RangeCount, gd->m_kBound.Center.x,
					gd->m_kBound.Center.y, gd->m_kBound.Center.z, gd->m_kBound.Radius, v ? "" : "(none) ", v ? v[0].x : 0.0f, v ? v[0].y : 0.0f,
					v ? v[0].z : 0.0f, pm.m[3][0], pm.m[3][1], pm.m[3][2], arenaUsed - info.Texels);
			}
		}
		if (record->instanceCount >= NVR_RT_MAX_INSTANCES) return;
		skip[kTaken]++;
		if (alphaTested) alphaFate[7]++;
		NvrRtInstanceRecord& extra = record->records[record->instanceCount];
		const Albedo& albedo = AlbedoOf(geo, shaderProperty);
		extra.albedo[0] = albedo.rgb[0]; extra.albedo[1] = albedo.rgb[1]; extra.albedo[2] = albedo.rgb[2]; extra.albedo[3] = 1.0f;
		extra.flags = (land ? NVR_RT_RECORD_LAND : 0u) | (albedo.known ? NVR_RT_RECORD_ALBEDO_KNOWN : 0u) | (HasGlowMap(shaderProperty) ? NVR_RT_RECORD_GLOW : 0u);
		if (alphaTested) extra.flags |= NVR_RT_RECORD_ALPHA_TESTED | ((UInt32)alpha->alphaTestRef << 8) | ((UInt32)alphaSlot << 16);
		else if (textured) extra.flags |= NVR_RT_RECORD_TEXTURED | ((UInt32)alphaSlot << 16);
		extra.geometryId = entry.id;
		extra.vertexOffset = extra.indexOffset = 0;	// the helper's
		NvrRtInstance& instance = record->instances[record->instanceCount++];
		D3DMATRIX m;
		TheRenderManager->CreateD3DMatrix(&m, &geo->m_worldTransform);	// camera relative, row vectors (p' = p M)
		for (int r = 0; r < 3; r++) {
			for (int c = 0; c < 3; c++) instance.transform[r][c] = m.m[c][r];
			instance.transform[r][3] = m.m[3][r];
		}
		instance.geometryId = entry.id;
		instance.customIndex = land ? 1u : 0u;
		instance.mask = MaskHook ? (MaskHook(geo, land) & 0xFFu) : (land ? 0x01u : 0x02u);	// default: bit 0 land, bit 1 objects
		if (alphaTested) instance.mask = NVR_RT_MASK_ALPHA;	// only rays that test alpha (RtAlpha.glsl) see it
		instance.flags = 0x1u;	// both faces (the game's two-sided and one-sided meshes alike: shadows need the back faces too)
	}

	void Core::Walk(NiAVObject* root, bool land, bool lod) {
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
			// Land: no reach test (its nodes' world bounds are not usable: rtsun-6 met no land at all with it; the cascades' walk does not
			// test land either). The loaded grid limits it (a few quads a cell).
			if (land && !lod && object) { landWalk[0]++; if (object->m_flags & NiAVObject::NiFlags::APP_CULLED) landWalk[1]++; }
			// LOD land keeps the reach test (big quads with real bounds); its quads where the full land is loaded are APP_CULLED
			if (!object || (object->m_flags & NiAVObject::NiFlags::APP_CULLED) || ((!land || lod) && !inReach(object))) continue;
			if (object->IsGeometry()) {
				NiGeometry* g = static_cast<NiGeometry*>(object);
				if (g->skinInstance && actorsOn && rtSkin) AddSkinned(g);
				else AddGeometry(g, land, lod);
				continue;
			}
			NiNode* node = object->IsNiNode();
			if (!node) continue;
			if (land && !lod) landWalk[2]++;
			// Faded out: not drawn. Land ignores it, as the cascades' walk does (rt-reflections' og-lake run met no land with it).
			if (node->IsFadeNode() && static_cast<BSFadeNode*>(node)->FadeAlpha <= 0.0f) { if (land) { if (!lod) landWalk[3]++; } else continue; }
			if (node->IsKindOf<NiSwitchNode>()) {
				if (land && !lod) landWalk[4]++;
				NiSwitchNode* sw = static_cast<NiSwitchNode*>(node);
				if (sw->m_iIndex >= 0 && sw->m_iIndex < node->m_children.end) stack.push_back(node->m_children.data[sw->m_iIndex]);
				continue;
			}
			for (int i = 0; i < node->m_children.end; i++) if (node->m_children.data[i]) stack.push_back(node->m_children.data[i]);
		}
	}

	void Core::WalkCell(TESObjectCELL* cell) {
		if (!cell) return;
		NiNode* landNode = cell->GetChildNode(TESObjectCELL::kCellNode_Land);
		landCells[0]++;
		if (!landNode) landCells[1]++;
		else if (landNode->m_flags & NiAVObject::NiFlags::APP_CULLED) landCells[2]++;
		Walk(landNode, true);
		for (TList<TESObjectREFR>::Entry* entry = &cell->objectList.First; entry; entry = entry->next) {
			TESObjectREFR* ref = entry->item;
			if (!ref || !ref->baseForm || (ref->flags & TESForm::FormFlags::kFormFlags_NotCastShadows)) continue;
			const UInt8 type = ref->baseForm->formType;
			if (type == TESForm::FormType::kFormType_Land) continue;
			const bool actor = type == TESForm::FormType::kFormType_NPC || type == TESForm::FormType::kFormType_Creature ||
				type == TESForm::FormType::kFormType_LeveledCreature;
			if (actor && !(actorsOn && rtSkin)) continue;	// actors: [Main.Main.RayTracing] Actors + fork patch 0033
			if (actor) {	// actors only near the camera ([Main.Main.RayTracing] ActorRadius): each is rebuilt every frame
				const float dx = ref->pos.x - TheRenderManager->CameraPosition.x, dy = ref->pos.y - TheRenderManager->CameraPosition.y;
				if (dx * dx + dy * dy > actorRadius * actorRadius) continue;
			}
			if ((TESObjectREFR*)Player == ref) continue;	// walked once, by Gather
			Walk(ref->GetNode(), false);
		}
	}

	void Core::Gather() {
		sceneFrame++;
		// A cell change (a door, a jump, a load): every actor partition goes at once (its buffers and bones may be unloaded and the
		// addresses reused by new ones); the actors still there are posed again from scratch this frame
		static const void* lastCell = nullptr;
		const void* cell = Player ? (const void*)Player->parentCell : nullptr;
		if (cell != lastCell) {
			for (auto it = geometries.begin(); it != geometries.end(); ) {
				if (!it->second.skinned) { ++it; continue; }
				if (it->second.sent) Release(it->second.id);	// (2026-10-30: queued; a full op list used to drop it and the helper kept it)
				it = geometries.erase(it);
			}
			lastCell = cell;
		}
		// the releases that did not fit an earlier frame's op list go first
		size_t sent = 0;
		while (sent < pendingReleases.size() && record->opCount < NVR_RT_MAX_OPS) {
			NvrRtGeometryOp& op = record->ops[record->opCount++];
			op = NvrRtGeometryOp();
			op.op = NVR_RT_OP_RELEASE;
			op.id = pendingReleases[sent++];
		}
		pendingReleases.erase(pendingReleases.begin(), pendingReleases.begin() + sent);
		compactOn = TheSettingManager->GetSettingI("Main.Main.RayTracing", "Compact") != 0;
		buildBudget = (UInt32)(std::max)(0, TheSettingManager->GetSettingI("Main.Main.RayTracing", "BuildTrianglesPerFrame"));
		buildNear = (std::max)(0.0f, TheSettingManager->GetSettingF("Main.Main.RayTracing", "BuildNearRadius"));
		cullAngle = (std::max)(0.0f, TheSettingManager->GetSettingF("Main.Main.RayTracing", "CullSmallAngle"));
		trianglesThisFrame = 0;
		radius = (std::max)(512.0f, TheSettingManager->GetSettingF("Main.Main.RayTracing", "SceneRadius"));
		actorRadius = (std::max)(0.0f, TheSettingManager->GetSettingF("Main.Main.RayTracing", "ActorRadius"));
		actorInterval = (UInt32)(std::max)(1, (std::min)(8, TheSettingManager->GetSettingI("Main.Main.RayTracing", "ActorUpdateInterval")));
		arenaUsed = 0;
		waiting = 0;
		atlasThisFrame = 0;
		if (Player && Player->GetWorldSpace() && Tes && Tes->gridCellArray) {
			GridCellArray* cells = Tes->gridCellArray;
			const UInt32 count = cells->size * cells->size;
			const UInt32 landBefore = skips[1][kTaken];
			for (UInt32 i = 0; i < count; i++) WalkCell(cells->GetCell(i));
			if (actorsOn && rtSkin && Player) Walk(Player->GetNode(), false);	// the player's third-person body (not in the cells' lists)
			// LOD land only when the loaded cells gave no land geometry this frame (rtsun-8 at og-lake: every land node empty, the ground on
			// screen is LOD terrain). With the full land there, LOD land stays out: its coarse quads stand above the real ground and cast
			// false shadows on it (rtsun-9 at og-goodsprings: black slabs on the road and the house front).
			if (skips[1][kTaken] == landBefore) Walk(BGSTerrainManager::GetRootLandLODNode(), true, true);
		}
		else if (Player) WalkCell(Player->parentCell);
		instances = record->instanceCount;

		// Geometries not met for a while (their cell unloaded, the object gone): released in the helper
		if ((sceneFrame % 30) == 0) {
			for (auto it = geometries.begin(); it != geometries.end(); ) {
				if (sceneFrame - it->second.lastSeen > kReleaseAfterFrames) {
					if (it->second.sent) Release(it->second.id);
					it = geometries.erase(it);
				}
				else ++it;
			}
		}
		record->arenaTexels = arenaUsed;
	}
}

void RtCore::AddUser(const User& user) { TheCore.users.push_back(user); }
D3DXVECTOR3 RtCore::Eye() { return D3DXVECTOR3(0.0f, 0.0f, 0.0f); }

void RtCore::SetMaskHook(UInt32 (*Hook)(NiGeometry*, bool)) { MaskHook = Hook; }

int RtCore::AddImage(const char* Name, UINT Width, UINT Height, NVR_EXT_SHARED_FORMAT Format) {
	if (!TheCore.inSetup || !Width || !Height) return -1;
	TheCore.imageNames.push_back(Name ? Name : "NVR RT image");
	TheCore.imageDescs.push_back({ AlignImage(Width), AlignImage(Height), Format, nullptr });
	TheCore.imageSizes.push_back({ Width, Height });
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
	if (!TheCore.recording || !f || f->passCount >= NVR_RT_MAX_PASSES || Shader < 0 || ImageCount > NVR_RT_PASS_IMAGES_MAX || ConstantsSize > NVR_RT_PASS_CONSTANTS)
		return false;
	NvrRtPass& p = f->passes[f->passCount];
	memset(&p, 0, offsetof(NvrRtPass, constants));
	p.shader = (uint32_t)Shader;
	p.groupsX = GroupsX;
	p.groupsY = GroupsY;
	p.groupsZ = 1;
	for (UINT i = 0; i < ImageCount; i++) {
		if (Images[i] <= 0 || Images[i] >= (int)TheCore.images.size()) return false;
		if (i < NVR_RT_PASS_IMAGES) p.images[i] = (uint32_t)Images[i];
		else p.extraImages |= (uint32_t)Images[i] << (8u * (i - NVR_RT_PASS_IMAGES));	// images 8..11: bindings 12..15 (2026-10-30)
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
	TheCore.copiesOut.push_back({ Image, Target, 0, 0 });
	return true;
}

bool RtCore::CopyOut(int Image, IUnknown* Target, UINT Width, UINT Height) {
	if (!TheCore.recording || Image <= 0 || Image >= (int)TheCore.images.size() || !Target || !Width || !Height) return false;
	TheCore.copiesOut.push_back({ Image, Target, (std::min)(Width, TheCore.imageSizes[Image].first), (std::min)(Height, TheCore.imageSizes[Image].second) });
	return true;
}

namespace {
	bool Core::Ready() {
		if (state == Core::Off) return false;
		bool wanted = false;
		for (const RtCore::User& user : users) wanted = wanted || (user.Wants && user.Wants());
		if (!wanted) return false;	// a running helper stays up (idle); it exits with the game
		if (state == Core::Idle) { Start(); return false; }
		Poll();
		if (state != Core::Running) return false;
		if (TheRenderManager->width != width || TheRenderManager->height != height) {
			Stop("the screen size changed (the shared images are made again)");
			state = Core::Idle;
			return false;
		}
		return true;
	}

	// One frame record = one exchange with the helper. The record counter (frame) counts exchanges, so each exchange has its own
	// inputsReady / outputReady values. The geometry staging of an exchange is recorded after the previous exchange's WaitShared
	// (exchanges are recorded one after the other on the render thread), so the arena rule holds with two exchanges a frame.
	bool Core::Exchange(IDirect3DDevice9* Device, const RtCore::User* only) {
		Core& c = *this;
		// The ring slot: the helper must have copied out the record that used it last (normally long ago; a few ms at worst)
		const UInt64 n = c.frame + 1;
		for (int i = 0; i < 100 && c.shared->consumedFrame + (int64_t)NVR_RT_FRAME_RING < (int64_t)n; i++) Sleep(1);
		if (c.shared->consumedFrame + (int64_t)NVR_RT_FRAME_RING < (int64_t)n) {
			static UInt32 skipped = 0;
			if ((skipped++ % 300) == 0) Logger::Log("UNOFFICIAL NVR DXVK: RT helper behind (frame %llu, read up to %lld): this frame not traced (%u so far).",
				(unsigned long long)n, (long long)c.shared->consumedFrame, skipped);
			return false;
		}
		c.frame = n;
		NvrRtFrame& f = c.shared->frames[n % NVR_RT_FRAME_RING];
		f.frame = 0;
		MemoryBarrier();
		f.opCount = f.instanceCount = f.passCount = f.arenaTexels = 0;
		c.record = &f;
		c.recording = true;
		c.copiesOut.clear();

		const UInt32 now = TheFrameRateManager->FrameNumber + 1;
		if (c.gatheredFrame != now) {
			c.Gather();
			// 2026-10-30: with fork 0034 the texel after this frame's geometry gets the frame's sequence number, written after the
			// conversions; the helper checks it before building (a reordered copy = an earlier flush's data: built empty, logged once)
			if (c.rtSeq && f.arenaTexels && f.arenaTexels < c.arenaCapacity &&
				SUCCEEDED(c.rtSeq->FlushStagedGeometrySeq(c.images[0], NvrRtSequenceOf(n), f.arenaTexels)))
				f.arenaTexels |= NVR_RT_ARENA_SEQUENCE;
			else c.rt->FlushStagedGeometry(c.images[0]);
			c.frameInstances.assign(f.instances, f.instances + f.instanceCount);
			c.frameRecords.assign(f.records, f.records + f.instanceCount);
			c.gatheredFrame = now;
		}
		else {
			// The frame's second exchange: the same scene, no geometry ops (they went with the first record), no arena rows.
			memcpy(f.instances, c.frameInstances.data(), c.frameInstances.size() * sizeof(NvrRtInstance));
			memcpy(f.records, c.frameRecords.data(), c.frameRecords.size() * sizeof(NvrRtInstanceRecord));
			f.instanceCount = (uint32_t)c.frameInstances.size();
		}
		if (only) { if (only->Record) only->Record(Device); }
		else for (const RtCore::User& user : c.users) if (user.Point == 0 && user.Wants && user.Wants() && user.Record) user.Record(Device);
		const bool recorded = f.passCount > 0;

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
		for (const PendingCopy& copy : c.copiesOut) {
			const RECT used = { 0, 0, (LONG)(copy.width ? copy.width : c.imageSizes[copy.image].first),
				(LONG)(copy.height ? copy.height : c.imageSizes[copy.image].second) };
			c.share->CopyFromShared(copy.target, 0, 0, c.images[copy.image], &used);
		}
		c.copiesOut.clear();
		c.recording = false;
		c.record = nullptr;

		if (n == 60 || (n % 600) == 0) {
			const NvrRtShared* s = c.shared;
			Logger::Log("RT CORE: frame %llu, %u instances, %u geometries known (%u waiting for arena space, %u released); helper: %u geometries, %llu triangles, "
				"%llu MB; GPU %.3f ms avg (builds %.3f, scene %.3f, passes %.3f), response %.3f ms avg (max %.3f)",
				(unsigned long long)n, c.instances, (unsigned)c.geometries.size(), c.waiting, c.released, s->geometries, (unsigned long long)s->triangles,
				(unsigned long long)(s->geometryBytes >> 20), s->gpuTimeUsAvg / 1000.0, s->buildUsAvg / 1000.0, s->sceneUsAvg / 1000.0, s->traceUsAvg / 1000.0,
				s->responseUsAvg / 1000.0, s->responseUsMax / 1000.0);
			c.shared->responseUsMax = 0;
			if (c.buildBudget || c.cullAngle > 0.0f || c.compactOn || !c.pendingReleases.empty())
				Logger::Log("RT CORE: scene cost: compact %s, %u far builds deferred by BuildTrianglesPerFrame %u, %u small objects culled (CullSmallAngle %.4f), "
					"%u releases queued", c.compactOn ? "on" : "off", c.budgetWaits, c.buildBudget, c.culledSmall, c.cullAngle, (unsigned)c.pendingReleases.size());
			c.budgetWaits = c.culledSmall = 0;
			if (n == 60 && c.actorsOn)
				Logger::Log("RT CORE: actors: %u skinned partitions posed in exchanges 1-60 (%s); skipped: no buffer data %u, bones %u, not described %u, "
					"streams / half positions %u, palette %u; hit textures: not PPLighting %u, no diffuse %u, texcoords in another stream %u, "
					"no slot %u, staged without texcoords %u, textured %u", c.actorPartitions, c.rtSkin ? "fork 1.8-rt-skin" : "this d3d9.dll has no skinning: off",
					c.partitionFate[0], c.partitionFate[1], c.partitionFate[2], c.partitionFate[3], c.partitionFate[4],
					c.skinTex[0], c.skinTex[1], c.skinTex[2], c.skinTex[3], c.skinTex[4], c.skinTex[5]);
			if (n == 60) {
				const UInt32* a = c.alphaFate;
				Logger::Log("RT CORE: alpha-tested meshes met (exchanges 1-60): taken %u, atlas %u slots; left out: blended only %u, AlphaTested off %u, "
					"not described %u, texcoords not in the position's stream %u, no diffuse texture %u, alpha unreadable or atlas full %u; "
					"waited for an upload %u; glow maps in the atlas %u (%s)", a[7], c.atlasUsed, a[0], a[1], a[2], a[3], a[4], a[5], a[6], c.glowSlots, c.glowAtlasImage >= 0 ? "GlowTextures on" : "GlowTextures off");
			}
			if (n == 60)
				for (int k = 0; k < 3; k++) {
					const UInt32* m = c.skips[k];
					char cells[256] = "";
					if (k == 1) sprintf_s(cells, "; cells walked %u, without a land node %u, land node culled %u; land subtree: objects %u, culled %u, nodes %u, "
						"faded %u, switch %u", c.landCells[0], c.landCells[1], c.landCells[2], c.landWalk[0], c.landWalk[1], c.landWalk[2], c.landWalk[3], c.landWalk[4]);
					Logger::Log("RT CORE: %s geometry met (exchanges 1-60): %u, taken %u; left out: no shader/data %u, skinned %u, not a lighting shader %u, "
						"decal/refraction %u, tree leaves %u, alpha %u, not described %u, refused by the fork %u%s", k == 2 ? "LOD land" : k ? "land" : "object", m[kSeen], m[kTaken],
						m[kNoShader], m[kSkinned], m[kNotLighting], m[kFlags], m[kLeaf], m[kAlpha], m[kDescribe], m[kMeasure],
						cells);
				}
		}
		return recorded;
	}
}

void RtCore::Frame(IDirect3DDevice9* Device) {
	Core& c = TheCore;
	if (!c.Ready()) return;
	// UNOFFICIAL 2026-10-28: only the Point-0 users exchange here; with none on, a Point-1 user's Exchange gathers the scene itself.
	bool pointZero = false;
	for (const User& user : c.users) pointZero = pointZero || (user.Point == 0 && user.Wants && user.Wants());
	if (!pointZero) return;
	static GpuTimer timer("Ray tracing (copies + helper + waits)");
	GpuProfileScope gpu(timer, Device);
	c.Exchange(Device, nullptr);
}

// UNOFFICIAL 2026-10-28 (RT reflections): RtCore.h. Frame (earlier in the frame) has already started the helper and checked the screen.
bool RtCore::Exchange(IDirect3DDevice9* Device, const User& user) {
	Core& c = TheCore;
	if (c.state != Core::Running || !c.shared || TheRenderManager->width != c.width || TheRenderManager->height != c.height) return false;
	if (!user.Wants || !user.Wants()) return false;
	return c.Exchange(Device, &user);
}

bool RtCore::Running() { return TheCore.state == Core::Running; }
const char* RtCore::StatusText() { return TheCore.status; }
const NvrRtShared* RtCore::Helper() { return TheCore.state == Core::Running ? TheCore.shared : nullptr; }
UInt32 RtCore::Instances() { return TheCore.instances; }
UInt32 RtCore::Geometries() { return (UInt32)TheCore.geometries.size(); }
UInt32 RtCore::Waiting() { return TheCore.waiting; }
UInt32 RtCore::Released() { return TheCore.released; }
bool RtCore::AlphaTested() { return TheCore.state == Core::Running && TheCore.alphaOn && TheCore.atlasImage >= 0; }
int RtCore::AlphaAtlasImage() { return TheCore.alphaOn || TheCore.hitTextures ? TheCore.atlasImage : -1; }	// shared slots
int RtCore::AlbedoAtlasImage() { return TheCore.state == Core::Running && TheCore.hitTextures ? TheCore.albedoAtlasImage : -1; }
int RtCore::GlowAtlasImage() { return TheCore.state == Core::Running && TheCore.hitTextures ? TheCore.glowAtlasImage : -1; }
bool RtCore::Actors() { return TheCore.state == Core::Running && TheCore.actorsOn && TheCore.rtSkin; }
