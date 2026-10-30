#pragma once
#include "LinearPreDecode.h"
#include "GpuProfiler.h"
#include "PointShadowForward.h"

// Ported 2026-10-28 from Conor (mrfury97)'s fork onto P84 v3 (github.com/mrfury97/NVR-Unofficial-Optimized pull request 1, branch
// manylamps-one-pass, commits 82ef371b 7127ac3d 3e73bf06, GPL-3.0-or-later; user: "seems like a good fit"); notes docs/next/MANYLAMPS-PORT.md.
//
// UNOFFICIAL 2026-10-27 ManyLampsOnePass ([Main.Main.Misc] ManyLampsOnePass, DXVK only; with MoreLampsPerObject; it switches LampsInOnePass's
// one-pass limit on by itself since the port):
// shiny objects lit by 6 to 13 lamps are drawn in ONE pass with all their lamps, instead of the game's 5-7 passes (light-only pass,
// extra lamp passes, texture pass, highlight passes, fog pass). It needs a restart to come on (its shaders); after that its lamp limit
// is live: off goes back to LampsInOnePass's 5 + f with the same shaders, for an F10 comparison at one spot.
// Measured 2026-10-27, Vault 21 lobby, same spot (one launch, 12 -> 5 -> 12): world draws 6,015 vs 8,677 a frame, frame time 20.3-21.0
// vs 22.9-23.1 ms, GPU world scene 12.2-12.4 vs 14.5-14.6 ms.
//
// Why the 6-lamp limit was there (read in the decompile):
//  - BSShaderPPLightingProperty::GetRenderPasses_2x (0x00BDF790) sends a shiny object to the one many-lamp pass (AddPass_Opt
//    0x00BDE9B0) only while its lamps are at most 2 + f (LampsInOnePass: 5 + f; ADD EAX,imm8 at 0x00BE00E0). Past that: several passes.
//  - The game's own light arrays hold 10 lights: ShadowLightShader::kLightData (0x011FD9A8, 10 x float4, followed by spPixelShaders) and
//    the pixel shader's PSLightPosition (c19-c26, then Toggles c27). AddPass_Opt caps the pass at 6 + f entries (MoreLampsPerObject).
//  - The 6-lamp shaders carry each lamp's direction in its own interpolator: at 6 every one of ps_3_0's 10 inputs is taken.
// So past 6 lamps nothing of the game's can be stretched. Instead:
//  - the compare is raised to 12 + f (LampsInOnePass::BeginFrame writes CompareImm), the game's cap stays 6 + f (its arrays stay safe);
//  - before every draw (Render.cpp's RenderPassStandard/SkinnedHook, and at a shader bind) NVR writes the object's lamps itself into
//    pixel registers c37-c63 (free in every ObjectTemplate pixel shader): c37.x the count, then per lamp its position and reach in the
//    object's own space, exactly as the game makes them for its shaders (c38 + 2k; ShadowLightShader::SetupGeometryConstants_Lights
//    0x00B78A90: the lamp's world position plus ShadowSceneNode::kLightingOffset through BSUtilities::GetInverseWorldMatrix 0x00C4C2D0,
//    the reach divided by the object's scale unless skinned) and its colour as the game computes it (c39 + 2k;
//    SetupGeometryOpt_LightsSpecular 0x00B7CB00: diffuse x dimmer x the light's LOD dimmer), decoded to linear on the CPU when linear
//    lighting is compiled in. The lamps are the pass's own (light 1 on; light 0 is the cell's directional light) and, for a many-lamp
//    pass the game capped, the object's other active lamps (BSShaderLightingProperty light list +0x60, read as
//    GetNumberOfActiveNonShadowLights 0x00B707D0 does), brightest first as the game sorts them;
//  - the game's own constant upload for these passes (NiD3DShaderConstantMap::SetShaderConstants 0x00E826D0, the call at 0x00E8283B)
//    writes zeros into c39-c86 while it sets up each draw, after NVR's upload: ConstantFilter's pixel hook calls AfterPixelWrite, which
//    puts the block back at once (the game's own constants for these shaders are all below c32);
//  - the 4-lamp-and-up shiny shaders (ObjectTemplate.hlsl MANY_LAMPS: SLS2025-2027.vso, SLS2034-2036.pso) carry the surface's tangent
//    frame (in the vertex's own space, as the game's lamp directions are made) instead of six lamp directions, and light every lamp per
//    pixel from these registers.
// The game's kLightingOffset is 0 in the world pass and set only while the first person is drawn (TESMain::DrawWorld_MTRender1stPerson ->
// ShadowSceneNode::SetLightingOffset 0x008750E0); for first-person draws it is learned from the game's own value for lamp 1 after each
// such draw (AfterDraw) and used for the next.
// Lamps 7-24 get no forward cube-map shadow (only six cube slots); the screen-space lamp shadows cover them as before.
// 2026-10-27 (PassFamilyView, Vault 21: the floor and wall pieces lit by 13+ lamps, 15-16 there, were still drawn in several passes
// and looked different from their one-pass neighbours): 24 lamps, the block in three ranges of pixel registers no NVR pixel shader
// uses: c37-c63 (the count and lamps 1-13), c76-c87 (lamps 14-19, between LinearPreDecode's c64-c75 and terrain's c88-c97) and
// c98-c107 (lamps 20-24, below the sun shadows' c108). The game's zeros (c39-c86) reach the second range too: AfterPixelWrite
// restores any range a write overlaps.
namespace ManyLamps {

	enum { MaxLamps = 24, FirstRegister = 37, Registers = 1 + 2 * MaxLamps };	// rows of the block (see Ranges)
	enum : UInt8 { CompareImm = MaxLamps - 1 };		// lamps <= 23 + f in one pass (f = 1 when the directional light is dark: 24)
	struct Range { UINT reg, row, rows; };
	constexpr Range Ranges[3] = { { 37, 0, 27 }, { 76, 27, 12 }, { 98, 39, 10 } };	// ObjectTemplate.hlsl NVR_ManyLamps / B / C
	static_assert(27 + 12 + 10 == Registers, "ManyLamps ranges");

	inline int Wanted = -1;			// the setting at startup (-1 not read yet)
	inline int State = -1;			// -1 not decided yet, 0 off, 1 on
	inline bool LinearCompiled = false;	// [Shaders.PBR.Linear] Enabled at startup: the shaders decode PSLightColor (LINEAR_LIGHTING_ON)
	inline float Block[Registers][4] = {};
	inline UINT BlockRegisters = 0;		// rows of the block on the device now (1 + 2 x count)
	inline const void* LastPass = nullptr;	// the pass whose lamps are in the block now
	inline UInt32 LastFrame = 0;
	inline UInt32 FrameId = 1;
	inline bool Bound = false;			// one of our shaders is bound (OnBind)
	inline bool OwnUpload = false;		// NVR's own upload is under way (AfterPixelWrite ignores it)
	inline bool FirstPerson = false;	// the pass being set up is drawn in the first-person pass
	inline D3DXVECTOR3 FirstPersonOffset(0.0f, 0.0f, 0.0f);	// the game's kLightingOffset for first-person draws, as learned
	inline bool HookWarned = false;

	// F10 statistics (one line per window)
	inline unsigned StatFrames = 0;
	inline unsigned long long StatDraws = 0, StatExtraDraws = 0, StatExtras = 0, StatOver = 0, StatByCount[MaxLamps + 1] = {};

	// The setting as the shaders were compiled with it (ShaderRecord::BuildDefines reads this too): startup value, DXVK and
	// MoreLampsPerObject (the shaders' SPECULAR_LAMPS6) required.
	inline bool WantedAtStartup() {
		if (Wanted < 0) Wanted = TheSettingManager->GetSettingI("Main.Main.Misc", "ManyLampsOnePass") != 0 &&
			TheSettingManager->GetSettingI("Main.Main.Misc", "MoreLampsPerObject") != 0;
		return Wanted != 0;
	}
	inline bool ShadersWant() {
		return WantedAtStartup() && TheRenderManager && !strncmp(TheRenderManager->D3D9RuntimeDescription(), "DXVK", 4);
	}

	// UNOFFICIAL 2026-10-29 LightCommonLamps ([Main.Main.Misc], restart; docs/next/ROOT-ASSUMPTIONS-PLAN.md C, Includes/LightCommon.hlsl): the
	// same lamps, but from ONE lamp table a frame (a float texture on s3) instead of each lamp's position and colour in the draw's constants:
	// a draw gets its list of table indices and its own world -> object rows (c37-c44), and the shader lights every lamp past the sixth in a
	// real loop (constant arrays can only be unrolled in ps_3_0). MANY_LAMPS_ON is then 2 (ShaderRecord::BuildDefines). Off: as before.
	inline int TableWanted = -1;
	inline bool TableAtStartup() {
		if (TableWanted < 0) TableWanted = TheSettingManager->GetSettingI("Main.Main.Misc", "LightCommonLamps") != 0;
		return TableWanted != 0;
	}
	// UNOFFICIAL 2026-10-30 ClusteredLamps ([Main.Main.Misc], restart to compile, then live; needs LightCommonLamps; Conor: "some form of
	// deferred rendering"): lighting decoupled from the game's per-object lamp lists. Once a frame the table's lamps are binned on the CPU into
	// screen tiles (64 px) x 8 depth slices (froxels; slice 0 nearer than 64 units, slice k covers 2^(k+5)..2^(k+6), slice 7 the rest), up to
	// 31 lamps a froxel, most important first; the lists go into the lamp table's rows 1+. The many-lamp shader keeps the draw's first six
	// lamps (their cube-map shadow slots) and then lights, per pixel, every other lamp of ITS froxel instead of the draw's list (no 24-lamp
	// cap; only the lamps that reach that part of the screen and depth). 0 live = the LightCommonLamps loop over the draw's list, as before.
	inline int ClusterWanted = -1;
	inline bool ClusterAtStartup() {
		if (ClusterWanted < 0) ClusterWanted = TableAtStartup() && TheSettingManager->GetSettingI("Main.Main.Misc", "ClusteredLamps") != 0;
		return ClusterWanted != 0;
	}
	inline const char* ShaderDefine() { return ShadersWant() ? (TableAtStartup() ? (ClusterAtStartup() ? "3" : "2") : "1") : "0"; }

	inline bool Active() {
		if (State < 0 && TheRenderManager && TheRenderManager->device) {
			State = ShadersWant() ? 1 : 0;
			LinearCompiled = TheSettingManager->GetSettingI("Shaders.PBR.Linear", "Enabled") != 0;
			if (WantedAtStartup())
				Logger::Log(State ? "UNOFFICIAL ManyLampsOnePass: on (shiny objects with up to %d lamps in one pass; lamps in c37-c63, c76-c87, c98-c107)"
					: "UNOFFICIAL ManyLampsOnePass: off (needs DXVK; the shaders keep 6 lamp slots)", (int)MaxLamps);
		}
		return State == 1;
	}

	inline bool IsOptPass(UInt16 e) { return e >= 0xC3 && e <= 0xD6; }

	// The game's world -> object-space matrix for its lamp constants (identity rotation minus NiRenderer::kPosAdjust for skinned objects).
	typedef void(__cdecl* InverseWorldFn)(const NiTransform*, const NiSkinInstance*, D3DXMATRIX*);
	inline const InverseWorldFn GetInverseWorldMatrix = (InverseWorldFn)0x00C4C2D0;
	inline D3DXMATRIX ToObject;		// this pass's
	inline float ReachScale = 1.0f;	// 1 / the object's scale (1 when skinned), as SetupGeometryConstants_Lights
	inline D3DXVECTOR3 LightingOffset(0.0f, 0.0f, 0.0f);	// this pass's kLightingOffset as NVR applies it

	// One lamp into the block; false when it is not a usable point light.
	inline bool Put(int k, const ShadowSceneLight* light, bool linear, float gamma) {
		const NiPointLight* source = light->sourceLight;
		if (!source || !light->bPointLight) return false;
		const D3DXVECTOR3 world(source->m_worldTransform.pos.x + LightingOffset.x, source->m_worldTransform.pos.y + LightingOffset.y,
			source->m_worldTransform.pos.z + LightingOffset.z);
		D3DXVECTOR3 local;
		D3DXVec3TransformCoord(&local, &world, &ToObject);
		float* p = Block[1 + 2 * k];
		p[0] = local.x;
		p[1] = local.y;
		p[2] = local.z;
		p[3] = source->Spec.r * ReachScale;
		float dimmer = source->Dimmer;
		if (!*(const bool*)0x011F941E && dimmer > 1.0f) dimmer = 1.0f;	// BSShaderManager::bHDR
		const float lod = light->fLODDimmer;
		const float raw[4] = { source->Diff.r * dimmer * lod, source->Diff.g * dimmer * lod, source->Diff.b * dimmer * lod, 0.0f };
		float* c = Block[2 + 2 * k];
		if (linear) LinearPreDecode::DecodeFast(c, raw, gamma);
		else memcpy(c, raw, sizeof(raw));
		return true;
	}

	// --- LightCommonLamps: the frame's lamp table ---------------------------------------------------------------------------------
	enum { TableLamps = 1024, TableTexels = 2 * TableLamps, NoLamp = 4095 };	// LightCommon.hlsl LIGHT_COMMON_TEXELS / LIGHT_COMMON_NONE
	// UNOFFICIAL 2026-10-30 ClusteredLamps: the froxel grid (LightCommon.hlsl LIGHT_COMMON_ROWS, ManyLightsDirect.hlsl MANY_LAMPS_ON 3)
	enum { TilePx = 64, MaxTilesX = 40, MaxTilesY = 24, Slices = 8, ListTexels = 8, ListLamps = 31,
		MaxFroxels = MaxTilesX * MaxTilesY * Slices, TableRows = 1 + (MaxFroxels * ListTexels + TableTexels - 1) / TableTexels };
	static_assert(TableRows == 31, "LIGHT_COMMON_ROWS 31.0f in ObjectTemplate.hlsl");
	inline int TilesX = 0, TilesY = 0;
	inline bool ClusterLive = false;	// this frame: compiled in, the setting on, the lists made
	inline bool WorldDraw = false;		// the pass being set up is drawn in the world scene (the only camera / viewport the lists are made for)
	inline UInt32 ListsFrame = 0;		// FrameId the froxel lists were made for (at the frame's first world draw)
	inline double StatClusterMs = 0;
	inline unsigned long long StatClusterLamps = 0, StatClusterEntries = 0, StatClusterOver = 0, StatClusterFrames = 0;
	inline IDirect3DTexture9* Table = nullptr;
	inline bool TableFailed = false;
	inline UInt32 TableFrame = 0;		// FrameId the table was filled for
	inline std::unordered_map<const ShadowSceneLight*, int> TableIndex;
	inline unsigned StatTableMissing = 0, StatTableLamps = 0;

	inline bool TableMode() { return Active() && TableAtStartup() && !TableFailed; }
	inline void Upload();	// (below)

	// Every lamp of the scene (ShadowSceneNode's list) into the table, once a frame, at the first draw that wants it (so the lamps' LOD
	// dimmers are this frame's: the scene walk sets them). Position in the world (the draw adds its lighting offset), reach, colour as Put.
	// UNOFFICIAL 2026-10-30 ClusteredLamps: the active lamps into the froxel lists (rows 1+ of the locked table; pitch in floats).
	inline void BinFroxels(float* t, int pitch, const std::vector<const ShadowSceneLight*>& active) {
		if (TheSettingManager->GetSettingI("Main.Main.Misc", "ClusteredLamps") == 0) return;
		LARGE_INTEGER freq, c0, c1; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&c0);
		D3DVIEWPORT9 vp = {};
		TheRenderManager->device->GetViewport(&vp);
		TilesX = (std::min)((int)MaxTilesX, (int)(vp.Width + TilePx - 1) / TilePx);
		TilesY = (std::min)((int)MaxTilesY, (int)(vp.Height + TilePx - 1) / TilePx);
		if (TilesX <= 0 || TilesY <= 0) return;
		const D3DXMATRIX& inv = TheRenderManager->InvViewMatrix;
		const D3DXVECTOR3 right(inv._11, inv._12, inv._13), up(inv._21, inv._22, inv._23), fwd(inv._31, inv._32, inv._33);
		const float tanX = fabsf(TheRenderManager->InvProjMatrix._11), tanY = fabsf(TheRenderManager->InvProjMatrix._22);
		const D3DXVECTOR4& cam = TheRenderManager->CameraPosition;
		auto sliceOf = [](float z) { return z < 64.0f ? 0 : (std::min)((int)Slices - 1, (int)floorf(log2f(z)) - 5); };
		struct L { int index; float key; float x, y, z, r; };
		std::vector<L> lamps; lamps.reserve(active.size());
		for (const ShadowSceneLight* light : active) {
			const NiPointLight* s = light->sourceLight;
			const D3DXVECTOR3 v(s->m_worldTransform.pos.x - cam.x, s->m_worldTransform.pos.y - cam.y, s->m_worldTransform.pos.z - cam.z);
			const float x = D3DXVec3Dot(&v, &right), y = D3DXVec3Dot(&v, &up), z = D3DXVec3Dot(&v, &fwd), r = s->Spec.r;
			if (r <= 0.0f || z + r <= 1.0f) continue;	// wholly behind the camera
			const float dist = (std::max)(sqrtf(x * x + y * y + z * z), 0.25f * r + 1.0f);
			const float brightness = (s->Diff.r + s->Diff.g + s->Diff.b) * s->Dimmer * light->fLODDimmer;
			lamps.push_back({ TableIndex[light], brightness * r / dist, x, y, z, r });
		}
		std::sort(lamps.begin(), lamps.end(), [](const L& a, const L& b) { return a.key > b.key; });	// a full list drops the least important
		static std::vector<UInt8> counts;
		counts.assign((size_t)TilesX * TilesY * Slices, 0);
		float* lists = t + pitch;	// row 1
		auto put = [&](int froxel, int k, float value) {	// entry k of the froxel's list (-1 = its count)
			const int texel = froxel * ListTexels + (k + 1) / 4, comp = (k + 1) % 4;
			lists[(texel / TableTexels) * pitch + (texel % TableTexels) * 4 + comp] = value;
		};
		for (const L& l : lamps) {
			const float zmin = l.z - l.r, zmax = l.z + l.r;
			int x0 = 0, x1 = TilesX - 1, y0 = 0, y1 = TilesY - 1;
			if (zmin > 8.0f) {	// a conservative screen rect of the sphere (each side taken at the depth that makes it widest)
				const float lx = l.x - l.r, hx = l.x + l.r, ly = l.y - l.r, hy = l.y + l.r;
				const float nx0 = lx / ((lx < 0.0f ? zmin : zmax) * tanX), nx1 = hx / ((hx > 0.0f ? zmin : zmax) * tanX);
				const float ny0 = ly / ((ly < 0.0f ? zmin : zmax) * tanY), ny1 = hy / ((hy > 0.0f ? zmin : zmax) * tanY);
				if (nx0 > 1.0f || nx1 < -1.0f || ny0 > 1.0f || ny1 < -1.0f) continue;	// off screen
				x0 = (std::max)(0, (int)floorf((nx0 * 0.5f + 0.5f) * vp.Width / TilePx));
				x1 = (std::min)(TilesX - 1, (int)floorf((nx1 * 0.5f + 0.5f) * vp.Width / TilePx));
				y0 = (std::max)(0, (int)floorf((0.5f - ny1 * 0.5f) * vp.Height / TilePx));
				y1 = (std::min)(TilesY - 1, (int)floorf((0.5f - ny0 * 0.5f) * vp.Height / TilePx));
			}
			const int s0 = sliceOf((std::max)(zmin, 0.0f)), s1 = sliceOf(zmax);
			StatClusterLamps++;
			for (int sl = s0; sl <= s1; sl++)
				for (int ty = y0; ty <= y1; ty++)
					for (int tx = x0; tx <= x1; tx++) {
						const int froxel = (sl * TilesY + ty) * TilesX + tx;
						UInt8& c = counts[froxel];
						if (c >= ListLamps) { StatClusterOver++; continue; }
						put(froxel, c, (float)l.index);
						c++;
						StatClusterEntries++;
					}
		}
		for (size_t f = 0; f < counts.size(); f++) put((int)f, -1, (float)counts[f]);
		ClusterLive = true;
		QueryPerformanceCounter(&c1);
		StatClusterMs += 1000.0 * (c1.QuadPart - c0.QuadPart) / freq.QuadPart;
		StatClusterFrames++;
	}

	inline void BuildTable() {
		TableFrame = FrameId;
		TableIndex.clear();
		IDirect3DDevice9* d = TheRenderManager->device;
		const UINT rows = ClusterAtStartup() ? TableRows : 1;	// UNOFFICIAL 2026-10-30 ClusteredLamps: the froxel lists below the lamps
		if (!Table && FAILED(d->CreateTexture(TableTexels, rows, 1, D3DUSAGE_DYNAMIC, D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &Table, NULL))) {
			Logger::Log("[ERROR] UNOFFICIAL LightCommonLamps: the lamp table could not be made: the lamps of many-lamp objects stay dark (set it off)");
			TableFailed = true;
			return;
		}
		D3DLOCKED_RECT r = {};
		if (FAILED(Table->LockRect(0, &r, NULL, D3DLOCK_DISCARD))) return;
		float* t = (float*)r.pBits;
		memset(t, 0, TableTexels * 4 * sizeof(float));
		std::vector<const ShadowSceneLight*> active;	// UNOFFICIAL 2026-10-30 ClusteredLamps: the lamps the game has on
		const bool linear = LinearCompiled && LinearPreDecode::Gamma > 1.0f;
		const float gamma = LinearPreDecode::Gamma;
		int n = 0;
		for (NiTList<ShadowSceneLight>::Entry* e = SceneNode ? SceneNode->lights.start : nullptr; e && n < TableLamps; e = e->next) {
			const ShadowSceneLight* light = e->data;
			const NiPointLight* source = light ? light->sourceLight : nullptr;
			if (!source || !light->bPointLight || TableIndex.count(light)) continue;
			float* p = t + n * 8;
			p[0] = source->m_worldTransform.pos.x; p[1] = source->m_worldTransform.pos.y; p[2] = source->m_worldTransform.pos.z;
			p[3] = source->Spec.r;
			float dimmer = source->Dimmer;
			if (!*(const bool*)0x011F941E && dimmer > 1.0f) dimmer = 1.0f;	// BSShaderManager::bHDR (as Put)
			const float lod = light->fLODDimmer;
			const float raw[4] = { source->Diff.r * dimmer * lod, source->Diff.g * dimmer * lod, source->Diff.b * dimmer * lod, 0.0f };
			if (linear) LinearPreDecode::DecodeFast(p + 4, raw, gamma);
			else memcpy(p + 4, raw, sizeof(raw));
			TableIndex[light] = n++;
			// as GetNumberOfActiveNonShadowLights: not switched off for the view (+0x110), not culled, not flagged (+0xEC)
			const UInt8* raw8 = (const UInt8*)light;
			if (*(const UInt16*)(raw8 + 0x110) != 0xFF && !(((const UInt8*)source)[0x30] & 1) && !raw8[0xEC]) active.push_back(light);
		}
		ClusterLive = false;
		// (the lists only at a world-scene draw: an earlier draw of the frame may be a reflection or a cube map, with its own camera and size)
		if (ClusterAtStartup() && WorldDraw) { ListsFrame = FrameId; BinFroxels(t, r.Pitch / (int)sizeof(float), active); }
		Table->UnlockRect(0);
		StatTableLamps = n;
	}

	inline int TableSlot(const ShadowSceneLight* light) {
		if (!light) return NoLamp;
		auto found = TableIndex.find(light);
		if (found != TableIndex.end()) return found->second;
		if (GpuTimer::Enabled) StatTableMissing++;
		return NoLamp;	// a lamp the scene's list did not hold at the frame's first draw: left out this frame
	}

	// The object's lamps for this pass as table indices (the same choice and order as SetForPass below).
	inline void SetForPassTable(const PointShadowForward::RenderPassView* pass) {
		// UNOFFICIAL 2026-10-30 ClusteredLamps: made again at the frame's first world draw (a discarding lock: earlier draws keep theirs)
		if (TableFrame != FrameId || (ClusterAtStartup() && WorldDraw && ListsFrame != FrameId)) BuildTable();
		if (TableFailed) return;
		int idx[MaxLamps];
		int count = 0;
		if (pass && pass->lights && pass->geometry) {
			NiGeometry* geometry = pass->geometry;
			GetInverseWorldMatrix(&geometry->m_worldTransform, geometry->skinInstance, &ToObject);
			ReachScale = !geometry->skinInstance && geometry->m_worldTransform.scale > 0.0f ? 1.0f / geometry->m_worldTransform.scale : 1.0f;
			LightingOffset = FirstPerson ? FirstPersonOffset : D3DXVECTOR3(0.0f, 0.0f, 0.0f);
			for (int i = 1; i < pass->numLights && count < MaxLamps; i++) {
				const ShadowSceneLight* light = pass->lights[i];	// (lamp k stays light k + 1 of the pass: its shadow slot)
				idx[count++] = light && light->sourceLight && light->bPointLight ? TableSlot(light) : NoLamp;
			}
			if (pass->maxLights > pass->numLights && IsOptPass(pass->passEnum)) {	// as SetForPass: a capped many-lamp pass's other lamps
				const UInt8* property = (const UInt8*)geometry->propertyState.m_spShadeProperty;
				int extras = 0;
				for (const UInt32* node = property ? *(const UInt32* const*)(property + 0x60) : nullptr; node; node = (const UInt32*)node[0]) {
					const UInt8* light = (const UInt8*)node[2];
					if (!light || *(const UInt16*)(light + 0x110) == 0xFF) continue;
					const UInt8* niLight = *(const UInt8* const*)(light + 0xF8);
					if (!niLight || (niLight[0x30] & 1) || light[0xEC]) continue;
					bool known = false;
					for (int i = 1; i < pass->numLights && !known; i++) known = (const UInt8*)pass->lights[i] == light;
					if (known) continue;
					if (count == MaxLamps) { StatOver++; continue; }
					const ShadowSceneLight* l = (const ShadowSceneLight*)light;
					if (!l->sourceLight || !l->bPointLight) continue;
					const int slot = TableSlot(l);
					if (slot == NoLamp) continue;
					idx[count++] = slot; extras++;
				}
				if (GpuTimer::Enabled && extras) { StatExtraDraws++; StatExtras += extras; }
			}
		}
		for (int k = count; k < MaxLamps; k++) idx[k] = NoLamp;
		Block[0][0] = (float)count; Block[0][1] = ReachScale; Block[0][2] = Block[0][3] = 0.0f;
		Block[1][0] = LightingOffset.x; Block[1][1] = LightingOffset.y; Block[1][2] = LightingOffset.z; Block[1][3] = 0.0f;
		const D3DXMATRIX& m = ToObject;	// row vectors: local = (world, 1) x m, so row r of the shader's transform is m's column r
		const float rows[3][4] = { { m._11, m._21, m._31, m._41 }, { m._12, m._22, m._32, m._42 }, { m._13, m._23, m._33, m._43 } };
		memcpy(Block[2], rows, sizeof(rows));
		for (int f = 0; f < 12; f++)	// two 12-bit indices a float (exact below 2^24), eight a float4 (LightCommonIndex)
			Block[5 + f / 4][f % 4] = (float)(idx[f * 2] + 4096 * idx[f * 2 + 1]);
		BlockRegisters = 8;
		if (ClusterAtStartup()) {	// UNOFFICIAL 2026-10-30 ClusteredLamps: c45 view depth from the object's space, c46-c47 the froxel grid
			D3DXMATRIX toWorld;
			const bool on = ClusterLive && ListsFrame == FrameId && WorldDraw && !FirstPerson && D3DXMatrixInverse(&toWorld, nullptr, &ToObject);
			if (on) {	// view depth = (object point, 1) x toWorld - camera, along the camera's forward axis
				const D3DXMATRIX& iv = TheRenderManager->InvViewMatrix;
				const float f[3] = { iv._31, iv._32, iv._33 };
				const D3DXVECTOR4& cam = TheRenderManager->CameraPosition;
				for (int j = 0; j < 3; j++) Block[8][j] = toWorld.m[j][0] * f[0] + toWorld.m[j][1] * f[1] + toWorld.m[j][2] * f[2];
				Block[8][3] = (toWorld.m[3][0] - cam.x) * f[0] + (toWorld.m[3][1] - cam.y) * f[1] + (toWorld.m[3][2] - cam.z) * f[2];
			}
			else memset(Block[8], 0, sizeof(Block[8]));
			Block[9][0] = 1.0f / TilePx; Block[9][1] = (float)TilesX; Block[9][2] = (float)(TilesX * TilesY); Block[9][3] = on ? 1.0f : 0.0f;
			Block[10][0] = (float)Slices - 1.0f; Block[10][1] = (float)TableRows; Block[10][2] = Block[10][3] = 0.0f;
			BlockRegisters = 11;
		}
		Upload();
		TheRenderManager->renderState->SetTexture(3, Table);	// through the game's state cache, so its own binds there still happen
		for (DWORD s : { D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER }) TheRenderManager->renderState->SetSamplerState(3, (D3DSAMPLERSTATETYPE)s, D3DTEXF_POINT, false);
		TheRenderManager->renderState->SetSamplerState(3, D3DSAMP_MIPFILTER, D3DTEXF_NONE, false);
		TheRenderManager->renderState->SetSamplerState(3, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP, false);
		TheRenderManager->renderState->SetSamplerState(3, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP, false);
		LastPass = pass;
		LastFrame = FrameId;
		if (GpuTimer::Enabled) { StatDraws++; StatByCount[count]++; }
	}

	// The block's rows in use onto the device (NVR's own upload: AfterPixelWrite ignores it).
	inline void Upload() {
		OwnUpload = true;
		for (const Range& r : Ranges) {
			if (BlockRegisters <= r.row) break;
			TheRenderManager->device->SetPixelShaderConstantF(r.reg, &Block[r.row][0], min(r.rows, BlockRegisters - r.row));
		}
		OwnUpload = false;
	}

	// The object's lamps for this pass into the block.
	inline void SetForPass(const PointShadowForward::RenderPassView* pass) {
		if (TableMode()) { SetForPassTable(pass); return; }	// UNOFFICIAL 2026-10-29 LightCommonLamps
		int count = 0;
		const bool linear = LinearCompiled && LinearPreDecode::Gamma > 1.0f;	// as LinColor: decoded only above gamma 1
		const float gamma = LinearPreDecode::Gamma;
		if (pass && pass->lights && pass->geometry) {
			NiGeometry* geometry = pass->geometry;
			GetInverseWorldMatrix(&geometry->m_worldTransform, geometry->skinInstance, &ToObject);
			ReachScale = !geometry->skinInstance && geometry->m_worldTransform.scale > 0.0f ? 1.0f / geometry->m_worldTransform.scale : 1.0f;
			LightingOffset = FirstPerson ? FirstPersonOffset : D3DXVECTOR3(0.0f, 0.0f, 0.0f);
			for (int i = 1; i < pass->numLights && count < MaxLamps; i++) {
				const ShadowSceneLight* light = pass->lights[i];
				// Lamp k must stay light k + 1 of the pass (the shader gives it that light's cube-map shadow slot): one that is not a usable
				// point light keeps its place with reach 0, which the shader skips.
				if (!light || !Put(count, light, linear, gamma)) memset(Block[1 + 2 * count], 0, 2 * sizeof(Block[0]));
				count++;
			}
			// A many-lamp pass the game capped (AddPass_Opt keeps 6 + f entries of maxLights): its other lamps from the object's list.
			if (pass->maxLights > pass->numLights && IsOptPass(pass->passEnum)) {
				const UInt8* property = (const UInt8*)geometry->propertyState.m_spShadeProperty;
				int extras = 0;
				for (const UInt32* node = property ? *(const UInt32* const*)(property + 0x60) : nullptr; node; node = (const UInt32*)node[0]) {
					const UInt8* light = (const UInt8*)node[2];
					if (!light || *(const UInt16*)(light + 0x110) == 0xFF) continue;	// as GetNumberOfActiveNonShadowLights
					const UInt8* niLight = *(const UInt8* const*)(light + 0xF8);
					if (!niLight || (niLight[0x30] & 1) || light[0xEC]) continue;
					bool known = false;
					for (int i = 1; i < pass->numLights && !known; i++) known = (const UInt8*)pass->lights[i] == light;
					if (known) continue;
					if (count == MaxLamps) { StatOver++; continue; }
					if (Put(count, (const ShadowSceneLight*)light, linear, gamma)) { count++; extras++; }
				}
				if (GpuTimer::Enabled && extras) { StatExtraDraws++; StatExtras += extras; }
			}
		}
		Block[0][0] = (float)count;
		Block[0][1] = Block[0][2] = Block[0][3] = 0.0f;
		BlockRegisters = 1 + 2 * count;
		Upload();
		LastPass = pass;
		LastFrame = FrameId;
		if (GpuTimer::Enabled) { StatDraws++; StatByCount[count]++; }
	}

	// Only while one of NVR's many-lamp shiny shaders is bound (SLS2034-2036.pso, NVR's record in place): other pixel shaders, the
	// game's or NVR's, may hold their own values in c37-c63 for a whole run of draws (set once at their bind), and a write per object
	// would overwrite them.
	inline bool IsOurs(const char* name) {
		return name && !strncmp(name, "SLS203", 6) && name[6] >= '4' && name[6] <= '6' && !strcmp(name + 7, ".pso");
	}

	// Before every object's draw (Render.cpp), and when the game binds a pass's shaders (SetShadersHook, BSBatchRenderer::BeginPass: the run's
	// first object, from BSShaderManager::pCurrentRenderPass).
	inline void ForPass(const PointShadowForward::RenderPassView* pass, bool firstPerson, bool world = false) {
		if (!Bound || !Active()) return;
		FirstPerson = firstPerson;
		WorldDraw = world;
		SetForPass(pass);
	}
	inline void OnBind(const char* pixelShaderName, bool nvrRecord, bool firstPerson, bool world = false) {
		Bound = nvrRecord && IsOurs(pixelShaderName) && Active();
		if (!Bound) return;
		FirstPerson = firstPerson;
		WorldDraw = world;
		const PointShadowForward::RenderPassView* pass = *(const PointShadowForward::RenderPassView* const*)0x011F91E0;
		if (pass && (pass != LastPass || LastFrame != FrameId)) SetForPass(pass);
	}

	// ConstantFilter's pixel hook, after every upload: one that covered part of the block while one of our shaders is bound (the game's
	// zeros in c39-c86, see above) gets the block put back at once.
	inline void AfterPixelWrite(UINT start, UINT count) {
		if (OwnUpload || !Bound || !BlockRegisters || LastFrame != FrameId) return;
		bool overlaps = false;
		for (const Range& r : Ranges) {
			if (BlockRegisters <= r.row) break;
			const UINT used = min(r.rows, BlockRegisters - r.row);
			if (start < r.reg + used && start + count > r.reg) { overlaps = true; break; }
		}
		if (overlaps) Upload();
	}

	// After a draw with one of our shaders (Render.cpp): for first-person draws, the game's own lamp 1 (c19, object space with its
	// kLightingOffset) back to world space, minus the lamp's world position, is the offset for the next first-person draw.
	inline void AfterDraw(const PointShadowForward::RenderPassView* pass) {
		if (!FirstPerson || !Bound || pass != LastPass || !Active()) return;
		if (pass->numLights < 2 || !pass->lights || !pass->lights[1] || !pass->lights[1]->sourceLight) return;
		float game[4];
		D3DXMATRIX toWorld;
		if (FAILED(TheRenderManager->device->GetPixelShaderConstantF(19, game, 1)) || !D3DXMatrixInverse(&toWorld, nullptr, &ToObject)) return;
		D3DXVECTOR3 world;
		const D3DXVECTOR3 local(game[0], game[1], game[2]);
		D3DXVec3TransformCoord(&world, &local, &toWorld);
		const NiPoint3& lamp = pass->lights[1]->sourceLight->m_worldTransform.pos;
		const D3DXVECTOR3 offset(world.x - lamp.x, world.y - lamp.y, world.z - lamp.z);
		if (_finite(offset.x) && _finite(offset.y) && _finite(offset.z)) FirstPersonOffset = offset;
	}

	// Once a frame (Render.cpp). restoreHooked: ConstantFilter's pixel hook is in (without it the game's zeros stay and the lamps are lost).
	inline void BeginFrame(bool restoreHooked) {
		FrameId++;
		if (!Active()) return;
		if (!restoreHooked && !HookWarned) {
			HookWarned = true;
			Logger::Log("[ERROR] UNOFFICIAL ManyLampsOnePass: the shader-constant hooks are not in (see SkipRedundantConstants above); objects lit by many lamps will be too dark. Set [Main.Main.Misc] ManyLampsOnePass = false.");
		}
		if (!GpuTimer::Enabled) { StatFrames = 0; return; }
		if (++StatFrames < 240) return;
		const double f = StatFrames;
		char line[1024]; int n = 0;
		n += sprintf_s(line + n, sizeof(line) - n, "MANY LAMPS %u frames, per frame: draws %.0f, lamps per draw", StatFrames, StatDraws / f);
		for (int k = 0; k <= MaxLamps; k++) if (StatByCount[k]) n += sprintf_s(line + n, sizeof(line) - n, " %d:%.1f", k, StatByCount[k] / f);
		n += sprintf_s(line + n, sizeof(line) - n, " | capped many-lamp passes given their other lamps %.1f (%.1f lamps), lamps past %d dropped %.2f",
			StatExtraDraws / f, StatExtras / f, (int)MaxLamps, StatOver / f);
		if (TableMode()) n += sprintf_s(line + n, sizeof(line) - n, " | lamp table %u lamps, lamps not in it %.2f", StatTableLamps, StatTableMissing / f);
		if (StatClusterFrames) {	// UNOFFICIAL 2026-10-30 ClusteredLamps
			const double cf = (double)StatClusterFrames;
			n += sprintf_s(line + n, sizeof(line) - n, " | CLUSTERED %dx%dx%d froxels: binning %.3f ms CPU, %.1f lamps binned, %.0f list entries (%.2f a froxel), over 31 dropped %.0f",
				TilesX, TilesY, (int)Slices, StatClusterMs / cf, StatClusterLamps / cf, StatClusterEntries / cf, StatClusterEntries / cf / (std::max)(1, TilesX * TilesY * (int)Slices), StatClusterOver / cf);
			StatClusterMs = 0; StatClusterLamps = StatClusterEntries = StatClusterOver = StatClusterFrames = 0;
		}
		Logger::Log("%s", line);
		StatFrames = 0; StatDraws = StatExtraDraws = StatExtras = StatOver = 0; StatTableMissing = 0;
		memset(StatByCount, 0, sizeof(StatByCount));
	}
}
