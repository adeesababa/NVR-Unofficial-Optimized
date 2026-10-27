#pragma once
#include <vector>
#include <unordered_map>
#include <cmath>
#include <algorithm>

// UNOFFICIAL 2026-10-27 DrawSkipCensus (test aid, [Main.Main.Performance] DrawSkipCensus, 0 = off, live; docs/next/FORK2-DRAWSKIP.md).
// Question it answers before any GPU-side draw skipping is built on NVR DXVK: how much of the world scene's GPU time goes to game
// draws whose object ends up hidden? Plain D3D9 occlusion queries, read a few frames later without waiting, so it runs the same on
// native D3D9, stock DXVK and NVR DXVK.
//
// Counting (every mode, in one frame of four: kEvery): in the frame's first world scene each pass of each object (the
// RenderPassStandard / Skinned hooks) is drawn inside its own occlusion query (the samples it drew); at the world scene's end each
// object's world bound sphere is tested against the finished depth: a screen rectangle round the sphere at the sphere's nearest
// depth, depth test only, no writes, inside a query (0 samples = no part of the sphere can be seen). The 'DRAW SKIP CENSUS' log line
// (every 240 frames) gives per counting frame: objects, passes, triangles, samples; passes that drew nothing; objects hidden when
// drawn (their first, depth-writing pass drew nothing: H1) and their later passes; objects hidden in the finished depth (H2, the bound
// test), their passes, triangles and the samples they shaded anyway (pixel work covered later); the world scene's draw calls.
//
// Skip modes measure the GPU time each kind of skip could save (F10 'World scene (game)', p50, against mode 1: same counting cost):
//   1 = count only.
//   2 = later passes (2nd, 3rd...) of H1 objects are not drawn: what an EXACT same-frame GPU skip could save (those passes would
//       draw nothing: z EQUAL / LESSEQUAL against a depth that already hides the object). The picture cannot change.
//   3 = every pass of H2 objects is not drawn: what a GPU test against LAST frame's depth could save (the pixel work of objects drawn
//       before what covers them). On a still scene the picture cannot change (the finished depth does not need them).
//   4 = every pass that drew nothing the last time it was drawn is not drawn: the upper bound of any per-draw skip.
// Skips are made only while the camera stands still (8 frames, < 1 unit, < 0.2 degrees), from answers at most 48 frames old, never
// for skinned objects (their world bound does not follow the bones) or objects with a pass drawn without the usual depth test (it
// could draw over what hides them), and not while HiddenObjectCulling or LampsDepthFirst is on (they
// change the same draws). In mode 4 every object is drawn in full in one counting frame of eight, so its answers stay fresh (that
// costs a few percent of the measured saving). Test aid only: a moving NPC can uncover a skipped object for a few frames.
// [Main.Main.Performance] DrawSkipCensusDebugView = true: red rectangles over objects hidden in the finished depth (H2), yellow over
// objects hidden when drawn but not proven by the bound test, drawn over the world (no depth test) at the world scene's end, and a
// status square top left (green: the bound test runs and passed its self-check; magenta: it failed it; grey: nothing tested).
namespace DrawSkipCensus {

	enum : UInt32 {
		kRing = 6,			// counting frames whose answers can be in flight
		kEvery = 4,			// one counting frame in four (the others only make the skips): the counting cost stays out of the p50
		kMaxAge = 48,		// oldest answer (frames) a skip may use (above the refresh period + the answers' delay)
		kRefreshCycles = 8,	// mode 4: each object is drawn in full in one counting frame of eight
		kStillFrames = 8,	// frames the camera must have stood still before any skip
		kLogFrames = 240,
		kPassSlots = 16,	// per-pass triangle counts kept per object (for the skipped-triangles count)
	};
	inline const float kStillUnits = 1.0f;
	inline const float kStillCos = 0.999994f;	// cos(0.2 degrees)

	struct Object {
		const void* parent = nullptr; float pos[3] = {}; float scale = 0.0f;
		UInt32 epoch = 0, hash = 0, seenFrame = 0;
		UInt16 passes = 0;			// passes met this frame
		int record = -1;			// its entry in this counting frame's object list
		bool skinned = false;
		bool noSkip = false;		// a pass of it was drawn without a normal depth test (z off, ALWAYS, ...): never left out
		UInt32 h1Frame = 0, h2Frame = 0, zeroFrame = 0;	// frame each answer describes (0 = none)
		bool h1 = false, h2 = false;
		UInt32 zeroMask = 0;		// pass ordinals (< 32) that drew nothing the last time they were drawn
		UInt32 prims[kPassSlots] = {};
	};
	struct PassRecord { int object; UInt16 ordinal; UInt8 zwrite; int query; UInt32 prims; };
	struct ObjectRecord { const void* key; UInt32 epoch; int query; UInt8 kind; };	// kind 0 tested (query), 1 off screen (hidden), 2 not testable
	struct Frame {
		UInt32 frame = 0; bool pending = false;
		std::vector<PassRecord> passes;
		std::vector<ObjectRecord> objects;
		std::vector<IDirect3DQuery9*> pool;
		UInt32 used = 0;
	};
	struct Window {
		double frames, objects, passes, prims, samples, zeroPasses, zeroPrims, h1Objects, h1Passes, h1Prims, h1Later, h1LaterPrims,
			tested, offscreen, untestable, h2Objects, h2Passes, h2Prims, h2Samples, h2Seen, h2SeenSamples, h1NotH2,
			worldFrames, worldDips, worldPrims, objectDips, objectPrims, skipFrames, skippedPasses, skippedPrims, skippedObjects,
			late, stillFrames, queryFails, unsafePasses, behind, laterGuard, mismatches;
	};

	inline int Mode = 0;
	inline bool DebugView = false;
	inline bool Active = false;			// inside the frame's first world scene with the census on
	inline bool Counting = false;		// this frame counts (queries + bound tests)
	inline bool SkipAllowed = false;	// a skip mode, camera still, nothing conflicting
	inline bool WorldSeen = false;
	inline bool LoggedConflict = false;
	inline bool BoundBroken = false;	// the bound test failed its self-check: its answers are not used for skips
	inline bool LoggedView = false;		// the camera line was written since the census was switched on
	inline UInt32 LastTested = 0;		// objects the last counting frame tested with a rectangle (debug view status square)
	inline UInt32 FrameNo = 0;
	inline DWORD Thread = 0;
	inline IDirect3DDevice9* Device = nullptr;
	inline IDirect3DSurface9* WorldDepth = nullptr;	// not held (compared only)
	inline std::unordered_map<const void*, Object> Objects;
	inline std::vector<const void*> FrameObjects;	// objects met in this world scene (every frame: the debug view)
	inline Frame Ring[kRing];
	inline Frame* Cur = nullptr;
	inline Window W = {};
	inline UInt32 WindowStart = 0;
	inline IDirect3DStateBlock9* State = nullptr;
	inline bool StateFailed = false;

	// the pass between Before and EndDraw
	inline Object* Pending = nullptr;
	inline int PendingObject = -1;
	inline UInt16 PendingOrdinal = 0;
	inline bool OpenPass = false;
	inline int OpenQuery = -1;
	inline UInt32 PassPrims = 0;

	// camera stillness
	inline float AnchorRot[9] = {}, AnchorPos[3] = {};
	inline UInt32 StillSince = 0;
	inline bool HaveAnchor = false;

	// ---- the world scene's DrawIndexedPrimitive (device vtable slot 82, chained like NewGrass / SandstormGlass): triangle counts ----
	typedef HRESULT(STDMETHODCALLTYPE* DrawIndexedPrimitiveFn)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
	inline DrawIndexedPrimitiveFn NextDip = nullptr;
	inline bool DipHooked = false;
	inline bool InWorld = false;
	inline UInt32 FrameDips = 0, FramePrims = 0, FrameObjectDips = 0, FrameObjectPrims = 0;

	inline HRESULT STDMETHODCALLTYPE DipHook(IDirect3DDevice9* device, D3DPRIMITIVETYPE type, INT base, UINT minIndex, UINT vertices, UINT start, UINT count) {
		if (InWorld && device == Device) {
			FrameDips++; FramePrims += count;
			if (OpenPass) { PassPrims += count; FrameObjectDips++; FrameObjectPrims += count; }
		}
		return NextDip(device, type, base, minIndex, vertices, start, count);
	}

	inline void HookDip(IDirect3DDevice9* device) {
		if (DipHooked || !device) return;
		void** table = *(void***)device;
		if (table[82] == (void*)&DipHook) { DipHooked = true; return; }
		DWORD old = 0;
		if (!VirtualProtect(&table[82], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return;
		NextDip = (DrawIndexedPrimitiveFn)table[82];
		table[82] = (void*)&DipHook;
		VirtualProtect(&table[82], sizeof(void*), old, &old);
		DipHooked = true;	// left in for the session (a pass-through when the census is off); NewGrass / SandstormGlass chain on top
	}

	inline void ReleaseAll() {
		for (Frame& f : Ring) {
			for (IDirect3DQuery9* q : f.pool) if (q) q->Release();
			f.pool.clear(); f.passes.clear(); f.objects.clear(); f.used = 0; f.pending = false; f.frame = 0;
		}
		Objects.clear();
		FrameObjects.clear();
		Cur = nullptr;
		Pending = nullptr; OpenPass = false;
		if (State) { State->Release(); State = nullptr; }
		StateFailed = false;
	}

	inline IDirect3DQuery9* NewQuery(int& index) {
		index = -1;
		if (!Cur || !Device) return nullptr;
		if (Cur->used >= Cur->pool.size()) {
			IDirect3DQuery9* q = nullptr;
			if (FAILED(Device->CreateQuery(D3DQUERYTYPE_OCCLUSION, &q)) || !q) { W.queryFails += 1; return nullptr; }
			Cur->pool.push_back(q);
		}
		index = (int)Cur->used++;
		return Cur->pool[index];
	}

	inline bool Fresh(UInt32 frame) { return frame && FrameNo - frame <= kMaxAge; }

	// ---- the bound test's geometry: the screen rectangle round a world bound sphere, at the sphere's nearest depth ----
	// The camera is read from the world scene graph's NiCamera itself (eye, forward / up / right = rotation columns 0 / 1 / 2, the
	// frustum at distance 1), exactly as LampScissor's exact rectangles do it during the same world scene. 2026-10-27 fix: the
	// first version took RenderManager's ViewMatrix / InvProjMatrix, which at the world scene's end did not describe the world
	// camera (bulk run lucky22: 0 objects tested, 477 'off screen', 945 'not testable'), so visible objects were called hidden.
	struct View {
		float eye[3], forward[3], up[3], right[3];
		float left, rightF, top, bottom, nearZ, farZ;
		D3DVIEWPORT9 vp; bool reversed; bool ok;
	};
	inline View Cam = {};

	inline void SetupView() {
		Cam.ok = false;
		if (!TheRenderManager || !Device || FAILED(Device->GetViewport(&Cam.vp)) || !Cam.vp.Width || !Cam.vp.Height) return;
		NiCamera* camera = WorldSceneGraph ? WorldSceneGraph->camera : nullptr;
		if (!camera || camera->Frustum.Ortho) return;
		const NiMatrix33& rot = camera->m_worldTransform.rot;
		const NiPoint3& eye = camera->m_worldTransform.pos;
		const NiFrustum& f = camera->Frustum;
		for (int i = 0; i < 3; i++) { Cam.forward[i] = rot.data[i][0]; Cam.up[i] = rot.data[i][1]; Cam.right[i] = rot.data[i][2]; }
		Cam.eye[0] = eye.x; Cam.eye[1] = eye.y; Cam.eye[2] = eye.z;
		Cam.left = f.Left; Cam.rightF = f.Right; Cam.top = f.Top; Cam.bottom = f.Bottom; Cam.nearZ = f.Near; Cam.farZ = f.Far;
		Cam.reversed = TheRenderManager->IsReversedDepth();
		Cam.ok = Cam.rightF - Cam.left > 0.0f && Cam.top - Cam.bottom > 0.0f && Cam.nearZ > 0.0f && Cam.farZ > Cam.nearZ &&
			std::isfinite(Cam.eye[0]) && std::isfinite(Cam.eye[1]) && std::isfinite(Cam.eye[2]);
	}

	// Kinds: 0 a rectangle to test (rect + depth set); 1 off screen (cannot be seen); 2 not testable (reaches the near plane, bad
	// bound); 3 wholly behind the camera (cannot be seen). The sphere is taken a little larger (5 % + 2 units: wind, cloth and
	// skin move vertices a bit past the bound the game keeps).
	inline int BoundRect(const void* key, float rect[4], float& depth, float* viewOut = nullptr) {
		if (!Cam.ok) return 2;
		const NiAVObject* object = (const NiAVObject*)key;
		const NiBound* bound = object->m_kWorldBound;
		if (!bound) return 2;
		const float r0 = bound->Radius, cx = bound->Center.x, cy = bound->Center.y, cz = bound->Center.z;
		if (!(r0 > 0.0f) || !std::isfinite(r0) || !std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(cz)) return 2;
		const float r = r0 * 1.05f + 2.0f;
		const float p[3] = { cx - Cam.eye[0], cy - Cam.eye[1], cz - Cam.eye[2] };
		const float vz = p[0] * Cam.forward[0] + p[1] * Cam.forward[1] + p[2] * Cam.forward[2];
		const float vx = p[0] * Cam.right[0] + p[1] * Cam.right[1] + p[2] * Cam.right[2];
		const float vy = p[0] * Cam.up[0] + p[1] * Cam.up[1] + p[2] * Cam.up[2];
		if (viewOut) { viewOut[0] = vx; viewOut[1] = vy; viewOut[2] = vz; viewOut[3] = r; }
		if (vz + r < Cam.nearZ) return 3;
		const float zNear = vz - r;
		if (!(zNear > Cam.nearZ * 1.01f)) return 2;	// the sphere reaches the near plane: no rectangle (counted as seen)
		// The sphere lies in the view-aligned cube round it; for points in front of the eye the projection keeps that, so the
		// rectangle of the cube's 8 projected corners holds the sphere's whole picture (x/z and y/z peak at the corners).
		const float fw = Cam.rightF - Cam.left, fh = Cam.top - Cam.bottom;
		float nx0 = 1e30f, nx1 = -1e30f, ny0 = 1e30f, ny1 = -1e30f;
		for (int iz = 0; iz < 2; iz++) {
			const float z = iz ? vz + r : zNear;
			for (int i = 0; i < 2; i++) {
				const float x = i ? vx + r : vx - r, y = i ? vy + r : vy - r;
				const float nx = (2.0f * x / z - (Cam.rightF + Cam.left)) / fw, ny = (2.0f * y / z - (Cam.top + Cam.bottom)) / fh;
				nx0 = (std::min)(nx0, nx); nx1 = (std::max)(nx1, nx);
				ny0 = (std::min)(ny0, ny); ny1 = (std::max)(ny1, ny);
			}
		}
		const float W = (float)Cam.vp.Width, H = (float)Cam.vp.Height, X = (float)Cam.vp.X, Y = (float)Cam.vp.Y;
		float x0 = floorf(X + (nx0 + 1.0f) * 0.5f * W) - 2.0f, x1 = ceilf(X + (nx1 + 1.0f) * 0.5f * W) + 2.0f;
		float y0 = floorf(Y + (1.0f - ny1) * 0.5f * H) - 2.0f, y1 = ceilf(Y + (1.0f - ny0) * 0.5f * H) + 2.0f;
		x0 = (std::max)(x0, X); y0 = (std::max)(y0, Y); x1 = (std::min)(x1, X + W); y1 = (std::min)(y1, Y + H);
		if (!(x1 > x0) || !(y1 > y0)) return 1;
		// The sphere's nearest depth as the depth buffer stores it (D3D projection from the frustum's near / far; reversed when
		// the game clears depth to 0).
		const float n = Cam.nearZ, fz = Cam.farZ;
		float d = Cam.reversed ? n * (fz - zNear) / ((fz - n) * zNear) : fz * (zNear - n) / ((fz - n) * zNear);
		d = Cam.vp.MinZ + d * (Cam.vp.MaxZ - Cam.vp.MinZ);
		d += Cam.reversed ? 2e-5f : -2e-5f;	// a hair nearer: equal counts as seen
		depth = (std::max)(0.0f, (std::min)(1.0f, d));
		rect[0] = x0; rect[1] = y0; rect[2] = x1; rect[3] = y1;
		return 0;
	}

	struct QuadVertex { float x, y, z, rhw; DWORD color; };
	inline void DrawRect(const float rect[4], float depth, DWORD color) {
		const QuadVertex v[4] = {
			{ rect[0] - 0.5f, rect[1] - 0.5f, depth, 1.0f, color }, { rect[2] - 0.5f, rect[1] - 0.5f, depth, 1.0f, color },
			{ rect[0] - 0.5f, rect[3] - 0.5f, depth, 1.0f, color }, { rect[2] - 0.5f, rect[3] - 0.5f, depth, 1.0f, color },
		};
		Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(QuadVertex));
	}

	inline void SetQuadState(bool debug) {
		IDirect3DDevice9* d = Device;
		d->SetVertexShader(nullptr);
		d->SetPixelShader(nullptr);
		d->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
		d->SetTexture(0, nullptr);
		d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		d->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
		d->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
		d->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
		d->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
		d->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		d->SetRenderState(D3DRS_ZENABLE, debug ? D3DZB_FALSE : D3DZB_TRUE);
		d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
		d->SetRenderState(D3DRS_ZFUNC, Cam.reversed ? D3DCMP_GREATEREQUAL : D3DCMP_LESSEQUAL);
		d->SetRenderState(D3DRS_COLORWRITEENABLE, debug ? 0xF : 0);
		d->SetRenderState(D3DRS_COLORWRITEENABLE1, 0);
		d->SetRenderState(D3DRS_COLORWRITEENABLE2, 0);
		d->SetRenderState(D3DRS_COLORWRITEENABLE3, 0);
		d->SetRenderState(D3DRS_ALPHABLENDENABLE, debug ? TRUE : FALSE);
		d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		d->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
		d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
		d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
		d->SetRenderState(D3DRS_STENCILENABLE, FALSE);
		d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
		d->SetRenderState(D3DRS_FOGENABLE, FALSE);
		d->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
		d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
		d->SetRenderState(D3DRS_LIGHTING, FALSE);
		d->SetRenderState(D3DRS_DEPTHBIAS, 0);
		d->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, 0);
		d->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
	}

	// ---- reading a counting frame's answers (oldest first, never waiting) ----
	inline bool Gather(Frame& f, bool force) {
		if (!f.pending) return true;
		if (f.used) {
			DWORD last = 0;
			const HRESULT hr = f.pool[f.used - 1]->GetData(&last, sizeof(last), 0);
			if (hr == S_FALSE) {
				if (!force) return false;
				W.late += 1;
				f.pending = false;
				return true;
			}
		}
		std::vector<DWORD> samples(f.used, 0);
		for (UInt32 i = 0; i < f.used; i++) {
			DWORD s = 0;
			if (f.pool[i]->GetData(&s, sizeof(s), 0) != S_OK) {	// one not in (out of order): try again later, or drop it
				if (!force) return false;
				W.late += 1;
				f.pending = false;
				return true;
			}
			samples[i] = s;
		}
		f.pending = false;

		const size_t n = f.objects.size();
		struct Sum { double passes = 0, prims = 0, samples = 0, later = 0, laterPrims = 0, laterSamples = 0; int first = -1; };
		std::vector<Sum> sums(n);
		W.frames += 1;
		W.objects += (double)n;
		for (size_t i = 0; i < f.passes.size(); i++) {
			const PassRecord& p = f.passes[i];
			if (p.query < 0 || p.object < 0 || (size_t)p.object >= n) continue;
			const DWORD s = samples[p.query];
			W.passes += 1; W.prims += p.prims; W.samples += s;
			if (!s) { W.zeroPasses += 1; W.zeroPrims += p.prims; }
			Sum& o = sums[p.object];
			o.passes += 1; o.prims += p.prims; o.samples += s;
			if (p.ordinal > 0) { o.later += 1; o.laterPrims += p.prims; o.laterSamples += s; }
			else o.first = (int)i;
		}
		double seenObjects = 0, mismatches = 0;
		for (size_t k = 0; k < n; k++) {
			const ObjectRecord& r = f.objects[k];
			const Sum& o = sums[k];
			const PassRecord* first = o.first >= 0 ? &f.passes[o.first] : nullptr;
			const bool h1Known = first && first->zwrite;
			const bool h1 = h1Known && samples[first->query] == 0;
			if (o.samples > 0) seenObjects += 1;
			bool h2Known = false, h2 = false;
			if (r.kind == 0 && r.query >= 0) {
				h2Known = true;
				h2 = samples[r.query] == 0;
				W.tested += 1;
				// Guard: a later pass (z EQUAL, drawn after every object's base pass) that still drew pixels means the object is
				// on top somewhere; then it is not hidden, whatever the rectangle said.
				if (h2 && o.laterSamples > 0) { h2 = false; W.laterGuard += 1; }
			}
			else if (r.kind == 1 || r.kind == 3) {
				// Off screen / behind the camera: hidden, unless one of its passes drew pixels this frame: then the bound test is
				// wrong for it (counted; many of these switch the hidden-in-depth skips off: BoundBroken).
				h2Known = true;
				h2 = o.samples == 0;
				if (!h2) { mismatches += 1; W.mismatches += 1; }
				(r.kind == 1 ? W.offscreen : W.behind) += 1;
			}
			else W.untestable += 1;
			if (h1) { W.h1Objects += 1; W.h1Passes += o.passes; W.h1Prims += o.prims; W.h1Later += o.later; W.h1LaterPrims += o.laterPrims; }
			if (h2) {
				W.h2Objects += 1; W.h2Passes += o.passes; W.h2Prims += o.prims; W.h2Samples += o.samples;
				if (o.samples > 0) { W.h2Seen += 1; W.h2SeenSamples += o.samples; }
			}
			if (h1 && r.kind == 0 && !h2) W.h1NotH2 += 1;
			auto it = Objects.find(r.key);
			if (it == Objects.end() || it->second.epoch != r.epoch) continue;
			Object& obj = it->second;
			if (h1Known && f.frame >= obj.h1Frame) { obj.h1 = h1; obj.h1Frame = f.frame; }
			if (h2Known && f.frame >= obj.h2Frame) { obj.h2 = h2; obj.h2Frame = f.frame; }
		}
		// Self-check of the bound test: objects that drew pixels but were placed off screen or behind the camera. More than a few
		// = the rectangles do not match the picture (wrong camera): the hidden-in-depth answers are not used any more (logged once).
		if (!BoundBroken && mismatches > (std::max)(2.0, 0.02 * seenObjects)) {
			BoundBroken = true;
			Logger::Log("UNOFFICIAL DrawSkipCensus: the bound test disagrees with the queries (%.0f of %.0f objects that drew pixels were placed off screen or behind the camera, frame %u): 'hidden in the finished depth' is not used for skips until the census is switched off and on.",
				mismatches, seenObjects, f.frame);
		}
		// per-pass "drew nothing" bits (only for passes drawn in that frame; skipped ones keep their bit)
		for (const PassRecord& p : f.passes) {
			if (p.query < 0 || p.object < 0 || (size_t)p.object >= n || p.ordinal >= 32) continue;
			auto it = Objects.find(f.objects[p.object].key);
			if (it == Objects.end() || it->second.epoch != f.objects[p.object].epoch || f.frame < it->second.zeroFrame) continue;
			Object& obj = it->second;
			const UInt32 bit = 1u << p.ordinal;
			if (samples[p.query] == 0) obj.zeroMask |= bit; else obj.zeroMask &= ~bit;
			obj.zeroFrame = f.frame;
		}
		return true;
	}

	inline void Report() {
		const double f = W.frames, wf = W.worldFrames, sf = W.skipFrames;
		if (f > 0) {
			const double px = Cam.vp.Width && Cam.vp.Height ? (double)Cam.vp.Width * Cam.vp.Height : 1.0;
			Logger::Log("DRAW SKIP CENSUS (mode %d, %.0f counting frames, %.0f dropped late, screen %ux%u): world draw calls %.0f (triangles %.0fk), %.0f%% of them object passes | objects %.0f, passes %.0f (triangles %.0fk), samples %.0fk (%.2f per screen pixel) | passes that drew nothing %.0f (triangles %.0fk) | HIDDEN WHEN DRAWN (first pass drew nothing): objects %.0f, their passes %.0f (triangles %.0fk), of them later passes %.0f (triangles %.0fk) | HIDDEN IN THE FINISHED DEPTH (bound test; %.0f tested, %.0f off screen, %.0f behind the camera, %.0f not testable; kept visible: %.0f by a later pass that drew, %.0f that drew pixels but were placed off screen%s): objects %.0f, passes %.0f (triangles %.0fk), samples shaded anyway %.0fk (%.1f%% of all samples; %.0f objects were seen when drawn) | hidden when drawn but not proven by the bound test %.0f",
				Mode, f, W.late, (unsigned)Cam.vp.Width, (unsigned)Cam.vp.Height,
				wf ? W.worldDips / wf : 0.0, wf ? W.worldPrims / wf / 1000.0 : 0.0, W.worldDips ? 100.0 * W.objectDips / W.worldDips : 0.0,
				W.objects / f, W.passes / f, W.prims / f / 1000.0, W.samples / f / 1000.0, W.samples / f / px,
				W.zeroPasses / f, W.zeroPrims / f / 1000.0,
				W.h1Objects / f, W.h1Passes / f, W.h1Prims / f / 1000.0, W.h1Later / f, W.h1LaterPrims / f / 1000.0,
				W.tested / f, W.offscreen / f, W.behind / f, W.untestable / f, W.laterGuard / f, W.mismatches / f,
				BoundBroken ? ", BOUND TEST FAILED ITS SELF-CHECK: not used for skips" : "", W.h2Objects / f, W.h2Passes / f, W.h2Prims / f / 1000.0, W.h2Samples / f / 1000.0,
				W.samples ? 100.0 * W.h2Samples / W.samples : 0.0, W.h2Seen / f, W.h1NotH2 / f);
		}
		if (Mode >= 2) {
			Logger::Log("DRAW SKIP CENSUS skips (mode %d: %s): %.0f%% of %u frames with the camera still; per skipping frame %.0f passes left out (triangles %.0fk; %.0f objects) | passes without a normal depth test %.0f per counting frame (their objects are never left out)%s",
				Mode, Mode == 2 ? "later passes of objects hidden when drawn" : Mode == 3 ? "objects hidden in the finished depth" : "passes that drew nothing last time",
				100.0 * W.stillFrames / (std::max)(1.0, wf), (unsigned)wf, sf ? W.skippedPasses / sf : 0.0, sf ? W.skippedPrims / sf / 1000.0 : 0.0,
				sf ? W.skippedObjects / sf : 0.0, f > 0 ? W.unsafePasses / f : 0.0, W.queryFails ? " | query creation failed" : "");
		}
		W = {};
	}

	// RenderHook, every frame before the game renders: setting, camera stillness, answers that came in, the log line.
	inline void BeginFrame(IDirect3DDevice9* device) {
		Thread = GetCurrentThreadId();
		Device = device;
		WorldSeen = false;
		Active = false;
		Pending = nullptr; OpenPass = false;
		const int mode = TheSettingManager ? TheSettingManager->GetSettingI("Main.Main.Performance", "DrawSkipCensus") : 0;
		const int clamped = mode < 0 ? 0 : mode > 4 ? 4 : mode;
		if (clamped != Mode) {
			if (Mode && W.frames) Report();
			if (!clamped) ReleaseAll();
			else Logger::Log("UNOFFICIAL DrawSkipCensus: mode %d (%s). Test aid: occlusion queries round every world pass in one frame of four.", clamped,
				clamped == 1 ? "count" : clamped == 2 ? "count + skip later passes of objects hidden when drawn" : clamped == 3 ? "count + skip objects hidden in the finished depth" : "count + skip passes that drew nothing last time");
			Mode = clamped;
			W = {};
			WindowStart = FrameNo;
			BoundBroken = false;
			LoggedView = false;
		}
		DebugView = Mode && TheSettingManager->GetSettingI("Main.Main.Performance", "DrawSkipCensusDebugView") != 0;
		FrameNo++;
		if (FrameNo == 0) FrameNo = 1;
		if (!Mode) return;

		// Camera stillness: compared with an anchor taken when it last moved.
		float rot[9], pos[3];
		if (HiddenObjectCulling::ReadCamera(rot, pos)) {
			double dot = 0, dist = 0;
			for (int i = 0; i < 9; i++) dot += (double)rot[i] * AnchorRot[i];
			for (int i = 0; i < 3; i++) { const double d = pos[i] - AnchorPos[i]; dist += d * d; }
			const double cosAngle = (dot - 1.0) / 2.0;
			if (!HaveAnchor || cosAngle < kStillCos || sqrt(dist) > kStillUnits) {
				memcpy(AnchorRot, rot, sizeof(rot)); memcpy(AnchorPos, pos, sizeof(pos));
				HaveAnchor = true;
				StillSince = FrameNo;
			}
		}
		else { HaveAnchor = false; StillSince = FrameNo; }

		// Answers: oldest counting frame first; a frame whose slot is about to be reused is dropped if still not in.
		for (UInt32 age = kRing * kEvery; age >= 1; age--) {
			const UInt32 frame = FrameNo - age;
			if (frame % kEvery) continue;
			Frame& f = Ring[(frame / kEvery) % kRing];
			if (!f.pending || f.frame != frame) continue;
			if (!Gather(f, age >= (kRing - 1) * kEvery)) break;
		}

		if ((FrameNo & 1023) == 0) {	// forget objects not met for 1,200 frames
			for (auto it = Objects.begin(); it != Objects.end();) {
				if (FrameNo - it->second.seenFrame > 1200) it = Objects.erase(it);
				else ++it;
			}
		}
		if (FrameNo - WindowStart >= kLogFrames) { Report(); WindowStart = FrameNo; }
	}

	// RenderWorldSceneGraphHook: the frame's first world scene starts.
	inline void BeginWorld(IDirect3DDevice9* device) {
		Active = Mode && !WorldSeen && Device && device == Device && GetCurrentThreadId() == Thread;
		WorldSeen = true;
		Cur = nullptr;
		Pending = nullptr; OpenPass = false;
		if (!Active) return;
		HookDip(Device);
		const bool conflict = TheSettingManager->SettingsMain.Main.HiddenObjectCulling || OnePassDepth::Mode;
		if (conflict && Mode >= 2 && !LoggedConflict) {
			LoggedConflict = true;
			Logger::Log("UNOFFICIAL DrawSkipCensus: HiddenObjectCulling or LampsDepthFirst is on: the census counts but makes no skips (they change the same draws).");
		}
		const bool still = HaveAnchor && FrameNo - StillSince >= kStillFrames;
		SkipAllowed = Mode >= 2 && still && !conflict;
		W.worldFrames += 1;
		if (still) W.stillFrames += 1;
		if (SkipAllowed) W.skipFrames += 1;
		Counting = (FrameNo % kEvery) == 0;
		if (Counting) {
			Frame& f = Ring[(FrameNo / kEvery) % kRing];
			if (f.pending) { W.late += 1; f.pending = false; }	// never came in: its queries are reused
			f.frame = FrameNo;
			f.passes.clear(); f.objects.clear(); f.used = 0;
			Cur = &f;
		}
		IDirect3DSurface9* depth = nullptr;
		Device->GetDepthStencilSurface(&depth);
		WorldDepth = depth;
		if (depth) depth->Release();
		FrameObjects.clear();
		FrameDips = FramePrims = FrameObjectDips = FrameObjectPrims = 0;
		InWorld = true;
	}

	// The pass hooks, before an object's pass in the world scene (after HiddenObjectCulling). True: do not draw this pass.
	inline bool Before(const void* pass, bool world, bool skinned) {
		Pending = nullptr;
		if (!Active || !world || !pass) return false;
		const void* key = *(const void* const*)pass;	// the pass's NiGeometry
		if (!key) return false;
		Object& o = Objects[key];
		if (o.seenFrame != FrameNo) {
			// First pass of this object in this world scene. A different parent, position or scale than last time = another object
			// at a reused address (or one that moved): its answers are forgotten.
			const UInt8* g = (const UInt8*)key;
			const void* parent = *(const void* const*)(g + 0x18);
			const float* pos = (const float*)(g + 0x8C);
			const float scale = *(const float*)(g + 0x98);
			if (o.seenFrame == 0) o.hash = (UInt32)(((uintptr_t)key >> 4) * 2654435761u) >> 8;
			if (o.parent != parent || o.pos[0] != pos[0] || o.pos[1] != pos[1] || o.pos[2] != pos[2] || o.scale != scale) {
				o.epoch++;
				o.h1Frame = o.h2Frame = o.zeroFrame = 0;
				o.h1 = o.h2 = false;
				o.zeroMask = 0;
				o.noSkip = false;
			}
			o.parent = parent; o.pos[0] = pos[0]; o.pos[1] = pos[1]; o.pos[2] = pos[2]; o.scale = scale;
			o.seenFrame = FrameNo;
			o.passes = 0;
			o.skinned = skinned;
			o.record = -1;
			FrameObjects.push_back(key);
			if (Cur) {
				o.record = (int)Cur->objects.size();
				Cur->objects.push_back({ key, o.epoch, -1, 2 });
			}
		}
		if (skinned) o.skinned = true;
		const UInt16 ordinal = o.passes < 0xFFFF ? o.passes++ : o.passes;
		bool skip = false;
		if (SkipAllowed && !o.skinned && !o.noSkip) {
			// Mode 4 only: a pass left out gets no new answer, so each object is drawn in full in one counting frame of eight. Modes 2
			// and 3 need no refresh (first passes are always drawn; the bound test runs whether the object was drawn or not).
			const bool refresh = Mode == 4 && Counting && ((FrameNo / kEvery + o.hash) % kRefreshCycles) == 0;
			if (!refresh) {
				if (Mode == 2) skip = ordinal > 0 && o.h1 && Fresh(o.h1Frame);
				else if (Mode == 3) skip = !BoundBroken && o.h2 && Fresh(o.h2Frame);
				else if (Mode == 4) skip = ordinal < 32 && ((o.zeroMask >> ordinal) & 1) && Fresh(o.zeroFrame);
			}
		}
		if (skip) {
			W.skippedPasses += 1;
			W.skippedPrims += ordinal < kPassSlots ? o.prims[ordinal] : 0;
			if (ordinal == 0 || (Mode == 2 && ordinal == 1)) W.skippedObjects += 1;
			return true;
		}
		Pending = &o;
		PendingObject = o.record;
		PendingOrdinal = ordinal;
		return false;
	}

	// Right round the game's draw of the pass (after LampScissor, which may still drop it).
	inline void BeginDraw() {
		OpenPass = false;
		if (!Pending) return;
		OpenPass = true;
		PassPrims = 0;
		OpenQuery = -1;
		if (Cur && PendingObject >= 0)
			if (IDirect3DQuery9* q = NewQuery(OpenQuery)) q->Issue(D3DISSUE_BEGIN);
	}
	inline void EndDraw() {
		if (!OpenPass) { Pending = nullptr; return; }
		OpenPass = false;
		Object* o = Pending;
		Pending = nullptr;
		if (PendingOrdinal < kPassSlots) o->prims[PendingOrdinal] = PassPrims;
		if (OpenQuery < 0 || !Cur) return;
		Cur->pool[OpenQuery]->Issue(D3DISSUE_END);
		DWORD zwrite = 0, zenable = 0, zfunc = 0;
		Device->GetRenderState(D3DRS_ZWRITEENABLE, &zwrite);	// the pass's own state is still on the device
		Device->GetRenderState(D3DRS_ZENABLE, &zenable);
		Device->GetRenderState(D3DRS_ZFUNC, &zfunc);
		// A pass that does not test depth the usual way can draw over what hides its object: that object is never left out.
		const bool reversed = TheRenderManager && TheRenderManager->IsReversedDepth();
		const bool normalTest = zenable && (zfunc == D3DCMP_EQUAL || (reversed ? (zfunc == D3DCMP_GREATER || zfunc == D3DCMP_GREATEREQUAL)
			: (zfunc == D3DCMP_LESS || zfunc == D3DCMP_LESSEQUAL)));
		if (!normalTest) { o->noSkip = true; W.unsafePasses += 1; }
		Cur->passes.push_back({ PendingObject, PendingOrdinal, (UInt8)(zwrite ? 1 : 0), OpenQuery, PassPrims });
		OpenQuery = -1;
	}

	// RenderWorldSceneGraphHook, the world scene is finished: the bound tests against its depth, the debug view.
	inline void EndWorld() {
		if (!Active) return;
		Active = false;
		InWorld = false;
		Pending = nullptr; OpenPass = false;
		W.worldDips += FrameDips; W.worldPrims += FramePrims; W.objectDips += FrameObjectDips; W.objectPrims += FrameObjectPrims;
		if (!Cur && !DebugView) return;
		IDirect3DSurface9* depth = nullptr;
		Device->GetDepthStencilSurface(&depth);
		const bool sameDepth = depth && depth == WorldDepth;
		if (depth) depth->Release();
		SetupView();
		if (!sameDepth || !Cam.ok) {
			if (Cur) Cur->pending = true;	// every object stays 'not testable'
			Cur = nullptr;
			return;
		}
		if (!State && !StateFailed && FAILED(Device->CreateStateBlock(D3DSBT_ALL, &State))) { State = nullptr; StateFailed = true; }
		if (!State) { if (Cur) Cur->pending = true; Cur = nullptr; return; }
		State->Capture();
		if (Cur) {
			SetQuadState(false);
			UInt32 kinds[4] = {};
			int sample = -1;	// one tested object for the camera line
			float sampleView[4] = {}, sampleRect[4] = {}, sampleDepth = 0.0f;
			for (ObjectRecord& r : Cur->objects) {
				float rect[4], d = 0.0f, view[4] = {};
				const auto it = Objects.find(r.key);
				if (it != Objects.end() && it->second.skinned) { r.kind = 2; kinds[2]++; continue; }
				const int kind = BoundRect(r.key, rect, d, view);
				r.kind = (UInt8)kind;
				kinds[kind & 3]++;
				if (kind != 0) continue;
				if (sample < 0) { sample = (int)(&r - Cur->objects.data()); memcpy(sampleView, view, sizeof(view)); memcpy(sampleRect, rect, sizeof(rect)); sampleDepth = d; }
				int index = -1;
				IDirect3DQuery9* q = NewQuery(index);
				if (!q) { r.kind = 2; continue; }
				q->Issue(D3DISSUE_BEGIN);
				DrawRect(rect, d, 0);
				q->Issue(D3DISSUE_END);
				r.query = index;
			}
			LastTested = kinds[0];
			if (!LoggedView) {
				LoggedView = true;
				Logger::Log("DRAW SKIP CENSUS camera (frame %u): eye (%.1f %.1f %.1f) forward (%.3f %.3f %.3f) up (%.3f %.3f %.3f) frustum L %.4f R %.4f T %.4f B %.4f near %.2f far %.0f, viewport %ux%u at %u,%u depth %.2f-%.2f, %s depth | this frame's %u objects: %u tested, %u off screen, %u not testable, %u behind | first tested: view (%.1f %.1f %.1f) radius %.1f -> rectangle %.0f,%.0f-%.0f,%.0f depth %.6f",
					FrameNo, Cam.eye[0], Cam.eye[1], Cam.eye[2], Cam.forward[0], Cam.forward[1], Cam.forward[2], Cam.up[0], Cam.up[1], Cam.up[2],
					Cam.left, Cam.rightF, Cam.top, Cam.bottom, Cam.nearZ, Cam.farZ, (unsigned)Cam.vp.Width, (unsigned)Cam.vp.Height,
					(unsigned)Cam.vp.X, (unsigned)Cam.vp.Y, Cam.vp.MinZ, Cam.vp.MaxZ, Cam.reversed ? "reversed" : "normal",
					(unsigned)Cur->objects.size(), kinds[0], kinds[1], kinds[2], kinds[3],
					sampleView[0], sampleView[1], sampleView[2], sampleView[3], sampleRect[0], sampleRect[1], sampleRect[2], sampleRect[3], sampleDepth);
			}
			Cur->pending = true;
			Cur = nullptr;
		}
		if (DebugView) {
			SetQuadState(true);
			for (const void* key : FrameObjects) {
				const auto it = Objects.find(key);
				if (it == Objects.end() || it->second.skinned) continue;
				const Object& o = it->second;
				const bool h2 = o.h2 && Fresh(o.h2Frame), h1 = o.h1 && Fresh(o.h1Frame);
				if (!h2 && !h1) continue;
				float rect[4], d = 0.0f;
				if (BoundRect(key, rect, d) != 0) continue;
				DrawRect(rect, d, h2 ? D3DCOLOR_ARGB(70, 255, 0, 0) : D3DCOLOR_ARGB(90, 255, 220, 0));
			}
			// Status square, top left: green = the bound test runs and passed its self-check, magenta = it failed it (not used for
			// skips), grey = nothing could be tested.
			const float sq[4] = { (float)Cam.vp.X + 8.0f, (float)Cam.vp.Y + 8.0f, (float)Cam.vp.X + 40.0f, (float)Cam.vp.Y + 40.0f };
			DrawRect(sq, 0.0f, BoundBroken ? D3DCOLOR_ARGB(255, 255, 0, 255) : LastTested ? D3DCOLOR_ARGB(255, 0, 255, 0) : D3DCOLOR_ARGB(255, 128, 128, 128));
		}
		State->Apply();
	}
}
