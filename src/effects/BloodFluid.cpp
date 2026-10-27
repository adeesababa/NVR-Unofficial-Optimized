// UNOFFICIAL 2026-10-26 BloodFluid (BloodFlow v2): see BloodFluid.h, Compute/BloodFluidSim.comp and Compute/BloodFluidDraw.frag.

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

#include "BloodFlow.h"
#include "BloodFluid.h"
#include "../core/DxvkCompute.h"
#include "../core/FrameClock.h"

namespace {
	// The blocks as the GLSL declares them (BloodFluid.glsl Frame, std140: vec4s only; BloodFluidSim.comp Push and Emitter).
	struct FrameBlock {
		float	eyeRel[4], right[4], up[4], fwd[4], screen[4], sim[4], fluid[4], wall[4], settle[4], gridf[4];
		UInt32	counts[4], counts2[4];
		float	look[4], look2[4], viewRot0[4], viewRot1[4], viewRot2[4], fresh[4], dryColor[4], match[4], depthConsts[4];
		float	sunDir[4], sunColor[4], sunAmbient[4], horizon[4];
		float	lightPos[12][4], shadowLightPos[12][4], lightColor[24][4];
	};
	struct SimPush { UInt32 mode, total, pad0, pad1; float shift[4]; };

	const UInt32 ParticleBytes = 64, DrawBytes = 48;
	const UInt32 GridCells = 1u << 17;		// hashed grid nodes (16 bytes each: 2 MB)
	const UInt32 TileCap = 128;				// particles a 16 x 16 tile lists
	const float Step = 1.0f / 120.0f;		// fixed simulation step (the spike's)
	const int MaxSteps = 4;
	// The spike's numbers (proto/fluid_preview.cpp), game units: particle radius 1.2, grid cell 3, rest density = particles per
	// cell at a spacing of twice the radius, pressure 600, cohesion 120, cling range 1.5, collide window 25, drag 0.5, splash 1.
	const float Radius = 1.2f, Cell = 3.0f, Gravity = 687.0f;	// (9.81 m/s^2 at 1.4287 cm a unit, as BloodFlow)

	enum { ModeEmit = 0, ModeSplat = 1, ModeIntegrate = 2, ModeTiles = 3, ModeRebase = 4 };

	std::unordered_map<UInt64, double> SplatsDone;	// splats already thrown (key -> when)
	std::unordered_map<UInt64, double> PoolsTaken;	// pools the fluid draws (key -> until when BloodFlow leaves them out)

	const D3DXVECTOR4* Constant(const char* name) {
		auto found = TheShaderManager->ConstantsTable.find(name);
		return found == TheShaderManager->ConstantsTable.end() ? nullptr : found->second;
	}
	void Copy4(float* dst, const D3DXVECTOR4* src) { if (src) memcpy(dst, src, 16); else memset(dst, 0, 16); }
	IDirect3DBaseTexture9* Texture(const char* name) { std::string n = name; return TheTextureManager->GetTextureByName(n); }

	UInt32 Hash(UInt64 x) {
		x ^= x >> 33; x *= 0xff51afd7ed558ccdull; x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ull; x ^= x >> 33;
		return (UInt32)x;
	}

	ID3D9NvrExtBuffer* MakeBuffer(UInt32 size, UInt32 flags, const char* name) {
		NVR_EXT_BUFFER_DESC desc = { size, flags, name };
		ID3D9NvrExtBuffer* buffer = nullptr;
		if (FAILED(DxvkCompute::Extension()->CreateBuffer(&desc, &buffer))) buffer = nullptr;
		if (!buffer) Logger::Log("UNOFFICIAL BloodFluid: could not make buffer %s (%u bytes)", name, size);
		return buffer;
	}

	UInt32 DeadBits() { const float dead = -1e30f; UInt32 bits; memcpy(&bits, &dead, 4); return bits; }
}

BloodFluid& TheBloodFluid() {
	static BloodFluid fluid;
	return fluid;
}

void BloodFluid::ReadSettings() {
	const char* s = "Shaders.BloodFlow.Fluid";
	Settings.Enabled = TheSettingManager->GetSettingI(s, "Enabled") != 0;
	Settings.Splats = TheSettingManager->GetSettingI(s, "Splats") != 0;
	Settings.Pools = TheSettingManager->GetSettingI(s, "Pools") != 0;
	Settings.ReplaceStreams = TheSettingManager->GetSettingI(s, "ReplaceStreams") != 0;
	Settings.Amount = std::clamp(TheSettingManager->GetSettingF(s, "Amount"), 0.1f, 4.0f);
	Settings.MaxParticles = std::clamp(TheSettingManager->GetSettingI(s, "MaxParticles"), 2048, 65536);
	Settings.SettleTime = std::clamp(TheSettingManager->GetSettingF(s, "SettleTime"), 2.0f, 120.0f);
	Settings.BleedTime = std::clamp(TheSettingManager->GetSettingF(s, "BleedTime"), 0.5f, 60.0f);
	Settings.Viscosity = std::clamp(TheSettingManager->GetSettingF(s, "Viscosity"), 0.0f, 60.0f);
	Settings.Cling = std::clamp(TheSettingManager->GetSettingF(s, "Cling"), 0.0f, 2000.0f);
	Settings.Friction = std::clamp(TheSettingManager->GetSettingF(s, "Friction"), 0.0f, 400.0f);
	Settings.Lifetime = std::clamp(TheSettingManager->GetSettingF(s, "Lifetime"), 20.0f, 7200.0f);
	Settings.StainSize = std::clamp(TheSettingManager->GetSettingF(s, "StainSize"), 1.0f, 8.0f);
	Settings.Threshold = std::clamp(TheSettingManager->GetSettingF(s, "Threshold"), 0.05f, 3.0f);
	Settings.Bump = std::clamp(TheSettingManager->GetSettingF(s, "Bump"), 0.0f, 4.0f);
	Settings.DebugView = std::clamp(TheSettingManager->GetSettingI(s, "DebugView"), 0, 3);
	Settings.TestThrow = std::clamp(TheSettingManager->GetSettingF(s, "TestThrow"), 0.0f, 60.0f);
	const bool pour = TheSettingManager->GetSettingI(s, "TestPour") != 0;
	if (!pour) PourDone = false;	// (on again: a new pour)
	Settings.TestPour = pour;
	Settings.StatsEvery = std::clamp(TheSettingManager->GetSettingI(s, "StatsEvery"), 0, 100000);
	static int logged = -1;
	if (logged != (Settings.Enabled ? 1 : 0)) {
		logged = Settings.Enabled ? 1 : 0;
		Logger::Log("UNOFFICIAL BloodFluid: %s", Settings.Enabled ? "on (runs when NVR DXVK's extension is there)" : "off");
	}
}

bool BloodFluid::On() const {
	return Settings.Enabled && !Failed && DxvkCompute::Available() && DxvkCompute::Info().ExtVersionMinor >= 1;
}

bool BloodFluid::Active() const {
	return On() && (PendingCount > 0 || (Used > 0 && TheFrameRateManager->Time < AliveUntil) || Settings.TestThrow > 0.0f || (Settings.TestPour && !PourDone));
}

void BloodFluid::Queue(const D3DXVECTOR3& Origin, float GroundZ, const D3DXVECTOR3& Dir, float Speed, float Spread, float Cone, float BackOff,
	float Window, int Count, UInt32 Seed, UInt32 Colour, UInt32 Kind) {
	if (PendingCount >= EmittersMax || Count <= 0) return;
	const UInt32 capacity = (UInt32)Settings.MaxParticles;
	if (AllocCapacity != capacity) {
		if (Used > 0 || PendingCount > 0) Clear("MaxParticles changed");
		AllocCapacity = capacity;
	}
	Count = (std::min<int>)(Count, (int)(std::min<UInt32>)(capacity / 4, 8192u - PendingParticles));
	if (Count <= 0) return;
	const D3DXVECTOR4& eye = TheRenderManager->CameraPosition;
	if (!HaveAnchor || Used == 0) {
		// (Rounded, so the anchor itself does not carry odd fractions.)
		Anchor[0] = floor(eye.x / 1024.0 + 0.5) * 1024.0; Anchor[1] = floor(eye.y / 1024.0 + 0.5) * 1024.0; Anchor[2] = floor(eye.z / 1024.0 + 0.5) * 1024.0;
		HaveAnchor = true;
	}
	Emitter& e = Pending[PendingCount++];
	e.Origin[0] = (float)(Origin.x - Anchor[0]); e.Origin[1] = (float)(Origin.y - Anchor[1]); e.Origin[2] = (float)(Origin.z - Anchor[2]);
	e.Origin[3] = (float)(GroundZ - Anchor[2]);
	e.Dir[0] = Dir.x; e.Dir[1] = Dir.y; e.Dir[2] = Dir.z; e.Dir[3] = Speed;
	e.Shape[0] = Spread; e.Shape[1] = Cone; e.Shape[2] = BackOff; e.Shape[3] = Window;
	e.Data[0] = Head; e.Data[1] = (UInt32)Count; e.Data[2] = Seed; e.Data[3] = (Colour & 0xFFFFFFu) | (Kind << 24);
	Head = (Head + (UInt32)Count) % capacity;
	Used = (std::min)(capacity, Used + (UInt32)Count);
	PendingParticles += (UInt32)Count;
	Emitted += (UInt64)Count;
	AliveUntil = (std::max)(AliveUntil, TheFrameRateManager->Time + Window + Settings.Lifetime + 1.0);
	if (LoggedEmits < 24) {
		LoggedEmits++;
		Logger::Log("UNOFFICIAL BloodFluid: %s, %d particles at (%.1f %.1f %.1f), thrown (%.2f %.2f %.2f) at %.0f, ground %.1f",
			Kind == 1 ? "a body bleeds" : Kind == 2 ? "test throw" : "a splat throws", Count, Origin.x, Origin.y, Origin.z, Dir.x, Dir.y, Dir.z, Speed, GroundZ);
	}
}

bool BloodFluid::Offer(UInt64 Key, const BloodFlowBoxes::Item& Item) {
	if (!On()) return false;
	const double now = TheFrameRateManager->Time;
	if (Item.Pool) {
		auto taken = PoolsTaken.find(Key);
		if (taken != PoolsTaken.end()) {
			if (now < taken->second) return true;
			PoolsTaken.erase(taken);
			return false;
		}
		// Fresh (BloodFlow ages a pool from when the body came to rest, on its clock: twice real time).
		if (!Settings.Pools || !(Item.Age < 2.0f) || !(Item.Fade > 0.0f)) return false;
		const float R = std::clamp(Item.Radius, 4.0f, 60.0f);
		const int count = (int)std::clamp(Settings.Amount * 700.0f * (R / 25.0f) * (R / 25.0f), 60.0f, 2400.0f);
		const D3DXVECTOR3 at(Item.Center.x, Item.Center.y, Item.Center.z + Radius + 0.5f);
		Queue(at, Item.Center.z, D3DXVECTOR3(0.0f, 0.0f, 1.0f), 18.0f, 0.22f * R, 0.0f, 0.0f, Settings.BleedTime, count, Hash(Key), (UInt32)Item.Tint, 1);
		PoolsTaken[Key] = now + Settings.BleedTime + Settings.Lifetime;
		return true;
	}
	if (!Settings.Splats || !(Item.Age < 3.0f) || !Item.Visible || !(Item.Fade > 0.0f)) return false;
	if (SplatsDone.count(Key)) return false;
	if (SplatsDone.size() > 4096) {
		for (auto it = SplatsDone.begin(); it != SplatsDone.end();) { if (now - it->second > 60.0) it = SplatsDone.erase(it); else ++it; }
	}
	SplatsDone[Key] = now;
	const float R = std::clamp(Item.Radius, 4.0f, 30.0f);
	D3DXVECTOR3 dir = -Item.Normal;	// (BloodFlow's normal is against the way the game threw it)
	const float l = BloodFlowBoxes::Length(dir);
	dir = l > 1e-3f ? dir / l : D3DXVECTOR3(0.0f, 0.0f, -1.0f);
	// Where blood that falls out of sight lands: the floor the player stands on, unless the splat is lower.
	const float ground = Player ? (std::min)((float)Player->pos.z, Item.Center.z) - 1.0f : Item.Center.z - 200.0f;
	const int count = (int)std::clamp(Settings.Amount * 140.0f * (R / 15.0f), 24.0f, 480.0f);
	Queue(Item.Center, ground, dir, 420.0f, 0.35f * R, 0.35f, 4.0f + 0.15f * R, 0.08f, count, Hash(Key), (UInt32)Item.Tint, 0);
	return false;
}

void BloodFluid::Release() {
	DxvkCompute::Drop(Particles);
	DxvkCompute::Drop(Grid);
	DxvkCompute::Drop(Emitters);
	DxvkCompute::Drop(Draws);
	DxvkCompute::Drop(TileCounts);
	DxvkCompute::Drop(TileLists);
	DxvkCompute::Drop(FrameBuffer);
	Capacity = 0; Width = Height = 0;
}

void BloodFluid::Clear(const char* Why) {
	if (Used > 0 || PendingCount > 0) Logger::Log("UNOFFICIAL BloodFluid: blood cleared (%s)", Why);
	Head = 0; Used = 0; PendingCount = 0; PendingParticles = 0;
	HaveAnchor = false; ShiftPending = false;
	AliveUntil = -1.0;
	StepDebt = 0.0;
	PoolsTaken.clear();
	SplatsDone.clear();
	if (Particles && DxvkCompute::Available()) DxvkCompute::Extension()->ClearBuffer(Particles, 0, 0, DeadBits());
}

bool BloodFluid::Ready(IDirect3DDevice9* Device) {
	if (Failed || !DxvkCompute::Available()) return false;
	ID3D9NvrExt* ext = DxvkCompute::Extension();
	if (!PipelinesTried) {
		PipelinesTried = true;
		std::vector<char> sim, draw;
		if (!DxvkCompute::LoadSpirv("BloodFluidSim", sim) || !DxvkCompute::LoadSpirv("BloodFluidDraw", draw)) {
			Logger::Log("UNOFFICIAL BloodFluid: shader files missing (Shaders\\NewVegasReloaded\\Compute\\BloodFluidSim.spv / BloodFluidDraw.spv): the fluid stays off");
			Failed = true;
			return false;
		}
		const NVR_EXT_BINDING_DESC simBindings[] = {
			{ 1, 0, NVR_EXT_BIND_STORAGE_BUFFER, 0, NVR_EXT_VIEW_2D, TRUE,  0 },	// Particles
			{ 1, 1, NVR_EXT_BIND_STORAGE_BUFFER, 1, NVR_EXT_VIEW_2D, TRUE,  0 },	// Grid
			{ 1, 2, NVR_EXT_BIND_STORAGE_BUFFER, 2, NVR_EXT_VIEW_2D, FALSE, 0 },	// Emitters
			{ 1, 3, NVR_EXT_BIND_STORAGE_BUFFER, 3, NVR_EXT_VIEW_2D, TRUE,  0 },	// Draws
			{ 1, 4, NVR_EXT_BIND_STORAGE_BUFFER, 4, NVR_EXT_VIEW_2D, TRUE,  0 },	// TileCounts
			{ 1, 5, NVR_EXT_BIND_STORAGE_BUFFER, 5, NVR_EXT_VIEW_2D, TRUE,  0 },	// TileLists
			{ 1, 6, NVR_EXT_BIND_SAMPLED_IMAGE,  0, NVR_EXT_VIEW_2D, FALSE, 0 },	// depth
			{ 1, 7, NVR_EXT_BIND_SAMPLED_IMAGE,  1, NVR_EXT_VIEW_2D, FALSE, 0 },	// view-model depth
			{ 2, 0, NVR_EXT_BIND_UNIFORM_BUFFER, 6, NVR_EXT_VIEW_2D, FALSE, 0 },	// Frame
			{ 0, 0, NVR_EXT_BIND_SAMPLER,        0, NVR_EXT_VIEW_2D, FALSE, 8 },	// POINT / CLAMP, its heap index at push offset 8
		};
		NVR_EXT_COMPUTE_PIPELINE_DESC c = {};
		c.pSpirv = sim.data(); c.SpirvSize = (UINT)sim.size();
		c.BindingCount = _countof(simBindings); c.pBindings = simBindings;
		c.PushConstantSize = sizeof(SimPush);
		c.UsesSamplerHeap = TRUE;
		c.pDebugName = "BloodFluidSim";
		if (FAILED(ext->CreateComputePipeline(&c, &SimPipeline))) SimPipeline = nullptr;
		const NVR_EXT_BINDING_DESC drawBindings[] = {
			{ 1, 0, NVR_EXT_BIND_SAMPLED_IMAGE,  0, NVR_EXT_VIEW_2D, FALSE, 0 },	// depth
			{ 1, 1, NVR_EXT_BIND_SAMPLED_IMAGE,  1, NVR_EXT_VIEW_2D, FALSE, 0 },	// normals
			{ 1, 2, NVR_EXT_BIND_SAMPLED_IMAGE,  2, NVR_EXT_VIEW_2D, FALSE, 0 },	// the image before (TESR_SourceBuffer)
			{ 1, 3, NVR_EXT_BIND_SAMPLED_IMAGE,  3, NVR_EXT_VIEW_2D, FALSE, 0 },	// point shadow buffer
			{ 1, 4, NVR_EXT_BIND_SAMPLED_IMAGE,  4, NVR_EXT_VIEW_2D, FALSE, 0 },	// view-model depth
			{ 1, 5, NVR_EXT_BIND_STORAGE_BUFFER, 0, NVR_EXT_VIEW_2D, FALSE, 0 },	// Draws
			{ 1, 6, NVR_EXT_BIND_STORAGE_BUFFER, 1, NVR_EXT_VIEW_2D, FALSE, 0 },	// TileCounts
			{ 1, 7, NVR_EXT_BIND_STORAGE_BUFFER, 2, NVR_EXT_VIEW_2D, FALSE, 0 },	// TileLists
			{ 2, 0, NVR_EXT_BIND_UNIFORM_BUFFER, 3, NVR_EXT_VIEW_2D, FALSE, 0 },	// Frame
			{ 0, 0, NVR_EXT_BIND_SAMPLER,        0, NVR_EXT_VIEW_2D, FALSE, 0 },	// POINT / CLAMP, its heap index at push offset 0
		};
		NVR_EXT_GRAPHICS_PIPELINE_DESC g = {};
		g.pFragmentSpirv = draw.data(); g.FragmentSpirvSize = (UINT)draw.size();
		g.BindingCount = _countof(drawBindings); g.pBindings = drawBindings;
		g.PushConstantSize = 16;
		g.UsesSamplerHeap = TRUE;
		g.ColorTargetCount = 1;
		g.Blend = NVR_EXT_BLEND_ALPHA;	// as BloodFlow's paint: SrcAlpha / InvSrcAlpha
		g.ColorWriteMask = 7;			// ... and ColorWriteEnable 7 (the image's alpha untouched)
		g.pDebugName = "BloodFluidDraw";
		if (FAILED(ext->CreateGraphicsPipeline(&g, &DrawPipeline))) DrawPipeline = nullptr;
		if (!SimPipeline || !DrawPipeline) {
			Logger::Log("UNOFFICIAL BloodFluid: a pipeline was refused (DXVK's log says why): the fluid stays off");
			DxvkCompute::Drop(SimPipeline); DxvkCompute::Drop(DrawPipeline);
			Failed = true;
			return false;
		}
		Logger::Log("UNOFFICIAL BloodFluid: pipelines ready");
		// 2026-10-26: NVR's world depth under DXVK comes through RESZ, which DXVK 3.0.1 offers only on AMD. Without it the depth
		// buffer stays empty and the blood finds no surfaces (it falls to its fallback ground and is never painted).
		IDirect3D9* d3d = nullptr;
		D3DDISPLAYMODE mode = {};
		if (SUCCEEDED(Device->GetDirect3D(&d3d)) && d3d) {
			if (SUCCEEDED(d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &mode)) && FAILED(d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, mode.Format,
				D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE, (D3DFORMAT)MAKEFOURCC('R', 'E', 'S', 'Z'))))
				Logger::Log("UNOFFICIAL BloodFluid: WARNING: this DXVK offers no RESZ on this GPU, so NVR gets no world depth and the blood will not find walls or floors. "
					"Fix: the NVR DXVK build with patch 0007, or 'd3d9.customVendorId = 1002' in dxvk.conf.");
			d3d->Release();
		}
	}

	const UInt32 capacity = (UInt32)Settings.MaxParticles;
	if (Capacity != capacity) {
		DxvkCompute::Drop(Particles); DxvkCompute::Drop(Draws);
		Particles = MakeBuffer(capacity * ParticleBytes, NVR_EXT_BUFFER_STORAGE | NVR_EXT_BUFFER_TRANSFER_DST | NVR_EXT_BUFFER_TRANSFER_SRC, "BloodFluid particles");
		Draws = MakeBuffer(capacity * DrawBytes, NVR_EXT_BUFFER_STORAGE, "BloodFluid draws");
		if (!Particles || !Draws) { Failed = true; Release(); return false; }
		ext->ClearBuffer(Particles, 0, 0, DeadBits());
		Capacity = capacity;
		Logger::Log("UNOFFICIAL BloodFluid: room for %u particles (%u KB)", Capacity, Capacity * (ParticleBytes + DrawBytes) / 1024);
	}
	if (!Grid) Grid = MakeBuffer(GridCells * 16, NVR_EXT_BUFFER_STORAGE | NVR_EXT_BUFFER_TRANSFER_DST, "BloodFluid grid");
	if (!Emitters) Emitters = MakeBuffer(EmittersMax * sizeof(Emitter), NVR_EXT_BUFFER_STORAGE | NVR_EXT_BUFFER_TRANSFER_DST, "BloodFluid emitters");
	if (!FrameBuffer) FrameBuffer = MakeBuffer((sizeof(FrameBlock) + 15) & ~15u, NVR_EXT_BUFFER_UNIFORM | NVR_EXT_BUFFER_TRANSFER_DST, "BloodFluid frame");
	const UInt32 w = TheRenderManager->width, h = TheRenderManager->height;
	if (w != Width || h != Height || !TileCounts || !TileLists) {
		DxvkCompute::Drop(TileCounts); DxvkCompute::Drop(TileLists);
		TilesX = (w + 15) / 16; TilesY = (h + 15) / 16;
		TileCounts = MakeBuffer(TilesX * TilesY * 4, NVR_EXT_BUFFER_STORAGE | NVR_EXT_BUFFER_TRANSFER_DST, "BloodFluid tile counts");
		TileLists = MakeBuffer(TilesX * TilesY * TileCap * 4, NVR_EXT_BUFFER_STORAGE, "BloodFluid tile lists");
		Width = w; Height = h;
	}
	if (!Grid || !Emitters || !FrameBuffer || !TileCounts || !TileLists) { Failed = true; Release(); return false; }
	return true;
}

bool BloodFluid::Render(IDirect3DDevice9* Device, IDirect3DSurface9* Image, IDirect3DSurface9* Source, BloodFlowEffect* Owner) {
	if (!On() || !Image || !Owner) return false;
	const double now = TheFrameRateManager->Time;
	double elapsed = LastTime < 0.0 ? 0.0 : now - LastTime;
	LastTime = now;
	if (elapsed < 0.0 || elapsed > 0.25) elapsed = Step;	// back from a pause or a load: one step
	const D3DXVECTOR4& camera = TheRenderManager->CameraPosition;
	const D3DXVECTOR3 eye(camera.x, camera.y, camera.z);
	// Through a door, or a jump (fast travel, a load): the blood stays behind. (Walking from one exterior cell into the next keeps it.)
	const bool exterior = TheShaderManager->GameState.isExterior;
	const bool door = TheShaderManager->GameState.isCellChanged && !(exterior && LastExterior);
	if ((Used > 0 || PendingCount > 0) && (door || BloodFlowBoxes::Length(eye - LastEye) > 3000.0f)) Clear(door ? "through a door" : "the camera jumped");
	LastEye = eye;
	LastExterior = exterior;
	if (AllocCapacity != (UInt32)Settings.MaxParticles) {
		if (Used > 0 || PendingCount > 0) Clear("MaxParticles changed");
		AllocCapacity = (UInt32)Settings.MaxParticles;
	}

	// TEST emitters: a throw from the camera at the view centre, a pool bleeding on the ground ahead.
	const D3DXVECTOR4* viewConstant = Constant("TESR_ViewTransform");
	const D3DXVECTOR4* projConstant = Constant("TESR_ProjectionTransform");
	if (!viewConstant || !projConstant) return false;
	const D3DXMATRIX& view = *(const D3DXMATRIX*)viewConstant;
	const D3DXVECTOR3 forward(view._13, view._23, view._33), right(view._11, view._21, view._31), upv(view._12, view._22, view._32);
	static UInt32 ThrowCount = 0;
	if (Settings.TestThrow > 0.0f && now - LastThrow >= Settings.TestThrow) {
		LastThrow = now;
		const float ground = Player ? (float)Player->pos.z - 1.0f : eye.z - 120.0f;
		Queue(eye + forward * 70.0f, ground, forward, 520.0f, 2.0f, 0.08f, 30.0f, 0.06f, (int)(360.0f * Settings.Amount), Hash(0x7E570000ull + (UInt64)(++ThrowCount)), 0, 2);	// (seeded by count: the same throws every run)
	}
	if (Settings.TestPour && !PourDone && Player) {
		PourDone = true;
		D3DXVECTOR3 flat(forward.x, forward.y, 0.0f);
		const float l = BloodFlowBoxes::Length(flat);
		flat = l > 1e-3f ? flat / l : D3DXVECTOR3(0.0f, 1.0f, 0.0f);
		const float z = (float)Player->pos.z;
		const D3DXVECTOR3 at = D3DXVECTOR3((float)Player->pos.x, (float)Player->pos.y, z) + flat * 110.0f;
		Queue(D3DXVECTOR3(at.x, at.y, z + Radius + 0.5f), z, D3DXVECTOR3(0.0f, 0.0f, 1.0f), 18.0f, 5.5f, 0.0f, 0.0f, Settings.BleedTime,
			(int)(700.0f * Settings.Amount), 0x9E3779B9u, 0, 1);
	}

	if (PendingCount == 0 && !(Used > 0 && now < AliveUntil)) return false;
	if (!Ready(Device)) return false;
	ID3D9NvrExt* ext = DxvkCompute::Extension();

	// The anchor follows the camera in big steps; the particles move with it.
	if (HaveAnchor) {
		const double dx = eye.x - Anchor[0], dy = eye.y - Anchor[1], dz = eye.z - Anchor[2];
		if (fabs(dx) > 4096.0 || fabs(dy) > 4096.0 || fabs(dz) > 4096.0) {
			double next[3] = { floor(eye.x / 1024.0 + 0.5) * 1024.0, floor(eye.y / 1024.0 + 0.5) * 1024.0, floor(eye.z / 1024.0 + 0.5) * 1024.0 };
			for (int k = 0; k < 3; k++) { PendingShift[k] = (float)(Anchor[k] - next[k]); Anchor[k] = next[k]; }
			for (int e = 0; e < PendingCount; e++) {
				for (int k = 0; k < 3; k++) Pending[e].Origin[k] += PendingShift[k];
				Pending[e].Origin[3] += PendingShift[2];
			}
			ShiftPending = Used > 0;
		}
	}

	// This frame's fixed steps.
	StepDebt += elapsed;
	int steps = (int)floor(StepDebt / Step);
	steps = std::clamp(steps, 0, MaxSteps);
	StepDebt -= steps * Step;
	if (StepDebt > Step * MaxSteps) StepDebt = 0.0;	// (slower than 30 fps: the blood runs slower, it does not jump)

	// The frame's constants.
	FrameBlock f = {};
	const D3DXMATRIX& proj = *(const D3DXMATRIX*)projConstant;
	const D3DXVECTOR4* cameraData = Constant("TESR_CameraData");
	f.eyeRel[0] = (float)(eye.x - Anchor[0]); f.eyeRel[1] = (float)(eye.y - Anchor[1]); f.eyeRel[2] = (float)(eye.z - Anchor[2]);
	f.eyeRel[3] = cameraData ? cameraData->y : 100000.0f;
	LastFarZ = f.eyeRel[3];
	f.right[0] = right.x; f.right[1] = right.y; f.right[2] = right.z; f.right[3] = proj._11;
	f.up[0] = upv.x; f.up[1] = upv.y; f.up[2] = upv.z; f.up[3] = proj._22;
	f.fwd[0] = forward.x; f.fwd[1] = forward.y; f.fwd[2] = forward.z; f.fwd[3] = cameraData ? cameraData->x : 5.0f;
	f.screen[0] = (float)Width; f.screen[1] = (float)Height; f.screen[2] = 1.0f / Width; f.screen[3] = 1.0f / Height;
	f.sim[0] = Step; f.sim[1] = Gravity; f.sim[2] = Radius; f.sim[3] = (float)steps;
	f.fluid[0] = 600.0f; f.fluid[1] = 120.0f; f.fluid[2] = Settings.Viscosity; f.fluid[3] = powf(Cell / (2.0f * Radius), 3.0f);
	f.wall[0] = Settings.Friction; f.wall[1] = Settings.Cling; f.wall[2] = 1.5f; f.wall[3] = 25.0f;
	f.settle[0] = 0.4f * Settings.SettleTime; f.settle[1] = 0.6f * Settings.SettleTime; f.settle[2] = 0.5f; f.settle[3] = 0.4f;	// (splash: the spike's 1.0 flung a throw over a whole table top in game, 2026-10-26)
	f.gridf[0] = Cell; f.gridf[1] = 1.0f / Cell; f.gridf[2] = 1500.0f; f.gridf[3] = Settings.Lifetime;
	f.counts[0] = Capacity; f.counts[1] = Used; f.counts[2] = TilesX; f.counts[3] = TilesY;
	f.counts2[0] = TileCap; f.counts2[1] = GridCells - 1; f.counts2[2] = (UInt32)Settings.DebugView; f.counts2[3] = (UInt32)PendingCount;
	f.look[0] = Settings.StainSize; f.look[1] = Radius * 1.3f; f.look[2] = Settings.Threshold; f.look[3] = Settings.Bump;
	// BloodFlow's look: its colours and times (its clock runs at TunedClockRate x real time).
	const BloodFlowEffect::BloodFlowStruct& c = Owner->Constants;
	f.look2[0] = (float)TunedClockRate; f.look2[1] = c.Dry.w; f.look2[2] = c.Info.z; f.look2[3] = c.Fresh.w;
	f.viewRot0[0] = view._11; f.viewRot0[1] = view._12; f.viewRot0[2] = view._13;
	f.viewRot1[0] = view._21; f.viewRot1[1] = view._22; f.viewRot1[2] = view._23;
	f.viewRot2[0] = view._31; f.viewRot2[1] = view._32; f.viewRot2[2] = view._33;
	f.fresh[0] = c.Fresh.x; f.fresh[1] = c.Fresh.y; f.fresh[2] = c.Fresh.z;
	f.dryColor[0] = c.Dry.x; f.dryColor[1] = c.Dry.y; f.dryColor[2] = c.Dry.z;
	f.match[0] = c.Match.x; f.match[1] = c.Match.y; f.match[2] = c.Match.w; f.match[3] = c.Info.x;
	Copy4(f.depthConsts, Constant("TESR_DepthConstants"));
	Copy4(f.sunDir, Constant("TESR_SunDirection"));
	Copy4(f.sunColor, Constant("TESR_SunColor"));
	Copy4(f.sunAmbient, Constant("TESR_SunAmbient"));
	Copy4(f.horizon, Constant("TESR_HorizonColor"));
	const D3DXVECTOR4* lamps = Constant("TESR_LightPosition");
	const D3DXVECTOR4* shadowLamps = Constant("TESR_ShadowLightPosition");
	const D3DXVECTOR4* lampColours = Constant("TESR_LightColor");
	for (int i = 0; i < 12; i++) {
		Copy4(f.lightPos[i], lamps ? lamps + i : nullptr);
		Copy4(f.shadowLightPos[i], shadowLamps ? shadowLamps + i : nullptr);
		for (int k = 0; k < 3; k++) { f.lightPos[i][k] -= ((const float*)&eye)[k]; f.shadowLightPos[i][k] -= ((const float*)&eye)[k]; }
	}
	for (int i = 0; i < 24; i++) Copy4(f.lightColor[i], lampColours ? lampColours + i : nullptr);

	IDirect3DBaseTexture9* depth = Texture("TESR_DepthBuffer");
	IDirect3DBaseTexture9* normals = Texture("TESR_NormalsBuffer");
	IDirect3DBaseTexture9* pointShadow = Texture("TESR_PointShadowBuffer");
	IDirect3DBaseTexture9* viewModel = TheTextureManager->DepthTextureViewModel;
	IDirect3DBaseTexture9* scene = TheTextureManager->SourceTexture;
	if (!depth || !normals || !viewModel || !scene || !Source || Source != TheTextureManager->SourceSurface) return false;
	if (!pointShadow) { pointShadow = depth; f.match[3] = 0.0f; }
	ID3D9NvrExtSampler* pointSampler = DxvkCompute::PointClamp();
	if (!pointSampler) return false;

	HRESULT hr = ext->UpdateBuffer(FrameBuffer, 0, sizeof(FrameBlock), &f);
	const NVR_EXT_RESOURCE_BINDING simResources[] = {
		{ 0, NVR_EXT_BIND_STORAGE_BUFFER, Particles,   0, 0, 0, FALSE },
		{ 1, NVR_EXT_BIND_STORAGE_BUFFER, Grid,        0, 0, 0, FALSE },
		{ 2, NVR_EXT_BIND_STORAGE_BUFFER, Emitters,    0, 0, 0, FALSE },
		{ 3, NVR_EXT_BIND_STORAGE_BUFFER, Draws,       0, 0, 0, FALSE },
		{ 4, NVR_EXT_BIND_STORAGE_BUFFER, TileCounts,  0, 0, 0, FALSE },
		{ 5, NVR_EXT_BIND_STORAGE_BUFFER, TileLists,   0, 0, 0, FALSE },
		{ 0, NVR_EXT_BIND_SAMPLED_IMAGE,  depth,       0, 0, 0, FALSE },
		{ 1, NVR_EXT_BIND_SAMPLED_IMAGE,  viewModel,   0, 0, 0, FALSE },
		{ 6, NVR_EXT_BIND_UNIFORM_BUFFER, FrameBuffer, 0, 0, 0, FALSE },
		{ 0, NVR_EXT_BIND_SAMPLER,        pointSampler, 0, 0, 0, FALSE },
	};
	SimPush push = {};
	NVR_EXT_DISPATCH_DESC d = {};
	d.pPipeline = SimPipeline;
	d.BindingCount = _countof(simResources);
	d.pBindings = simResources;
	d.pPushData = &push;
	d.PushDataSize = sizeof(push);
	d.GroupsY = 1; d.GroupsZ = 1;
	DxvkCompute::PreLoad(simResources, _countof(simResources));	// UNOFFICIAL 2026-10-26 (alflicker): managed textures on the GPU (DxvkCompute.h)
	auto dispatch = [&](UInt32 mode, UInt32 threads) {
		if (FAILED(hr) || threads == 0) return;
		push.mode = mode;
		d.GroupsX = (threads + 63) / 64;
		hr = ext->Dispatch(&d);
	};

	static GpuTimer simTimer("  Blood fluid sim"), drawTimer("  Blood fluid draw");
	{
		GpuProfileScope gpu(simTimer, Device);
		if (ShiftPending) {
			memcpy(push.shift, PendingShift, 12);
			dispatch(ModeRebase, Used);
			memset(push.shift, 0, 16);
			ShiftPending = false;
		}
		if (PendingCount > 0) {
			if (SUCCEEDED(hr)) hr = ext->UpdateBuffer(Emitters, 0, PendingCount * sizeof(Emitter), Pending);
			push.total = PendingParticles;
			dispatch(ModeEmit, PendingParticles);
			push.total = 0;
			PendingCount = 0; PendingParticles = 0;
		}
		for (int s = 0; s < steps && SUCCEEDED(hr); s++) {
			hr = ext->ClearBuffer(Grid, 0, 0, 0);
			dispatch(ModeSplat, Used);
			dispatch(ModeIntegrate, Used);
		}
	}
	bool drew = false;
	{
		GpuProfileScope gpu(drawTimer, Device);
		if (SUCCEEDED(hr)) hr = ext->ClearBuffer(TileCounts, 0, 0, 0);
		dispatch(ModeTiles, Used);
		if (SUCCEEDED(hr)) {
			// The draw reads the image as it was from TESR_SourceBuffer (BloodFlow's paint included) and blends over it.
			Device->StretchRect(Image, NULL, Source, NULL, D3DTEXF_NONE);
			const NVR_EXT_RESOURCE_BINDING drawResources[] = {
				{ 0, NVR_EXT_BIND_SAMPLED_IMAGE,  depth,       0, 0, 0, FALSE },
				{ 1, NVR_EXT_BIND_SAMPLED_IMAGE,  normals,     0, 0, 0, FALSE },
				{ 2, NVR_EXT_BIND_SAMPLED_IMAGE,  scene,       0, 0, 0, FALSE },
				{ 3, NVR_EXT_BIND_SAMPLED_IMAGE,  pointShadow, 0, 0, 0, FALSE },
				{ 4, NVR_EXT_BIND_SAMPLED_IMAGE,  viewModel,   0, 0, 0, FALSE },
				{ 0, NVR_EXT_BIND_STORAGE_BUFFER, Draws,       0, 0, 0, FALSE },
				{ 1, NVR_EXT_BIND_STORAGE_BUFFER, TileCounts,  0, 0, 0, FALSE },
				{ 2, NVR_EXT_BIND_STORAGE_BUFFER, TileLists,   0, 0, 0, FALSE },
				{ 3, NVR_EXT_BIND_UNIFORM_BUFFER, FrameBuffer, 0, 0, 0, FALSE },
				{ 0, NVR_EXT_BIND_SAMPLER,        pointSampler, 0, 0, 0, FALSE },
			};
			const UInt32 drawPush[4] = {};	// (the sampler's heap index is filled in by the extension)
			NVR_EXT_DRAW_DESC draw = {};
			draw.pPipeline = DrawPipeline;
			draw.TargetCount = 1;
			draw.Targets[0].pResource = Image;
			draw.BindingCount = _countof(drawResources);
			draw.pBindings = drawResources;
			draw.pPushData = drawPush;
			draw.PushDataSize = sizeof(drawPush);
			DxvkCompute::PreLoad(drawResources, _countof(drawResources)); // UNOFFICIAL 2026-10-26 (alflicker): managed textures on the GPU (DxvkCompute.h)
			hr = ext->Draw(&draw);
			drew = SUCCEEDED(hr);
		}
	}
	if (FAILED(hr)) {
		Logger::Log("UNOFFICIAL BloodFluid: a dispatch or the draw failed (%08lX; DXVK's log says why): the fluid is off for this session", hr);
		Failed = true;
		Release();
		return false;
	}
	if (++Frames == 1) Logger::Log("UNOFFICIAL BloodFluid: running (%u particles max, %ux%u tiles, %d steps this frame)", Capacity, TilesX, TilesY, steps);
	if (Settings.StatsEvery > 0 && Frames % (UInt32)Settings.StatsEvery == 0) Stats(Frames);
	return drew;
}

// TEST ONLY (StatsEvery): what the particles are doing, from a read-back of the buffer (waits for the GPU).
void BloodFluid::Stats(UInt32 Frame) {
	if (!Particles || Used == 0) return;
	std::vector<float> data((size_t)Used * 16);
	if (FAILED(DxvkCompute::Extension()->ReadBuffer(Particles, 0, Used * ParticleBytes, data.data()))) { Logger::Log("UNOFFICIAL BloodFluid stats: ReadBuffer failed"); return; }
	int dead = 0, unborn = 0, air = 0, contact = 0, ground = 0, frozen = 0, settled = 0, moving = 0;
	double speed = 0.0, ageSum = 0.0;
	float zMin = 1e30f, zMax = -1e30f;
	for (UInt32 i = 0; i < Used; i++) {
		const float* q = &data[(size_t)i * 16];
		if (q[3] <= -1e29f) { dead++; continue; }
		if (q[3] < 0.0f) { unborn++; continue; }
		const UInt32 flags = (UInt32)(q[7] + 0.5f);
		const float v = sqrtf(q[4] * q[4] + q[5] * q[5] + q[6] * q[6]);
		ageSum += q[3];
		zMin = (std::min)(zMin, q[2]); zMax = (std::max)(zMax, q[2]);
		if (flags & 16) { settled++; continue; }
		if (flags & 8) frozen++;
		else if (flags & 4) ground++;
		else if (flags & 1) contact++;
		else air++;
		if (v > 0.5f) { moving++; speed += v; }
	}
	if (Settings.DebugView == 3 && Grid) {	// what the first particles see (BloodFluidSim.comp tiles mode)
		float probe[8][4] = {};
		if (SUCCEEDED(DxvkCompute::Extension()->ReadBuffer(Grid, (GridCells - 8) * 16, sizeof(probe), probe))) {
			for (int k = 7; k >= 0; k--) Logger::Log("UNOFFICIAL BloodFluid probe %d: view depth %.2f, scene depth there %.2f, view model %.4f, raw depth %.6f (farZ %.1f)",
				7 - k, probe[k][0], probe[k][1], probe[k][2], probe[k][3], LastFarZ);
			// (Raw depth 1.0 everywhere = NVR has no world depth: under DXVK on NVIDIA without RESZ, see DXVK-FORK-PLAN.md.)
		}
	}
	const int alive = (int)Used - dead - unborn;
	Logger::Log("UNOFFICIAL BloodFluid stats (frame %u): %d alive (%d not born, %d empty): %d in the air, %d on surfaces, %d on the fallback ground, %d held (hidden), %d settled; %d moving at %.1f u/s on average; mean age %.1f s; z %.1f..%.1f (anchor space)",
		Frame, alive, unborn, dead, air, contact, ground, frozen, settled, moving, moving ? speed / moving : 0.0, alive > 0 ? ageSum / alive : 0.0, zMin, zMax);
}
