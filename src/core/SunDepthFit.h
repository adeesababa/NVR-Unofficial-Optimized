#pragma once

#include <cmath>
#include <algorithm>
#ifndef SUNDEPTHFIT_MATH_ONLY
#include "GpuProfiler.h"
#include "RenderRect.h"
#endif

namespace SunDepthFit {
	inline bool Enabled = false;
	inline int  DebugView = 0;
	inline bool Log = false;

	constexpr int BlockSize = 8;
	constexpr int FarLevels = 13;
	constexpr int NearLevels = 7;
	constexpr int Queries = 1 + FarLevels + NearLevels;
	constexpr int RingSize = 4;
	constexpr float FloorDepth = 250.0f;
	constexpr float MaxCoarser = 1.05f;
	constexpr float MinLocalGain = 1.25f;
	constexpr float MinGain = 1.03f;
	constexpr int ShrinkFrames = 8;
	constexpr int MaxAge = 30;

	inline float SliceRadius(float z1, float z2, float k2) {
		const float half = 0.5f * (z2 - z1);
		return sqrtf(z2 * z2 * k2 + half * half);
	}
	struct Layout { float Start = 0.0f; float S[4] = {}; };
	inline float RadiusAt(float z, const Layout& layout, float k2) {
		float from = layout.Start;
		for (int i = 0; i < 4; i++) {
			if (z < layout.S[i] || i == 3) return SliceRadius(from, layout.S[i], k2);
			from = layout.S[i];
		}
		return 1.0e30f;
	}
	inline void PracticalSplits(float lo, float hi, int count, float lambda, float* out) {
		const float ratio = hi / lo, range = hi - lo;
		for (int i = 0; i < count; i++) {
			const float p = (i + 1) / (float)count;
			const float lg = lo * powf(ratio, p), un = lo + range * p;
			out[i] = lambda * (lg - un) + un;
		}
		out[count - 1] = hi;
	}
	inline void Compare(const Layout& old, const Layout& fit, float lo, float hi, float k2, float& worst, float& gain, float& best) {
		float points[12];
		int count = 0;
		points[count++] = lo;
		points[count++] = hi;
		for (int i = 0; i < 4; i++) {
			if (old.S[i] > lo && old.S[i] < hi) points[count++] = old.S[i];
			if (fit.S[i] > lo && fit.S[i] < hi) points[count++] = fit.S[i];
		}
		std::sort(points, points + count);
		double sum = 0.0, total = 0.0;
		worst = 0.0f;
		best = 0.0f;
		for (int p = 0; p + 1 < count; p++) {
			const float a = points[p], b = points[p + 1];
			if (b <= a * 1.0001f) continue;
			const float z = sqrtf(a * b);
			const float before = RadiusAt(z, old, k2), after = RadiusAt(z, fit, k2);
			worst = (std::max)(worst, after / before);
			best = (std::max)(best, before / after);
			const double weight = log((double)b / a);
			sum += log((double)before / after) * weight;
			total += weight;
		}
		gain = total > 0.0 ? (float)exp(sum / total) : 1.0f;
	}

	struct Choice { bool Fitted = false; Layout Chosen; float Gain = 1.0f, Worst = 1.0f, Lambda = 0.0f; int Keep = -1; };
	inline Choice Search(float minZ, float maxZ, float lambda, const float d[4], float reach, float lo, bool keepAir, bool airReaders, float k2) {
		Choice choice;
		choice.Lambda = lambda;
		Layout normal;
		normal.Start = minZ;
		for (int i = 0; i < 4; i++) normal.S[i] = d[i];
		const int last = keepAir ? 2 : 3;
		const float lambdas[4] = { lambda, 0.5f, 0.75f, 1.0f };
		for (int j = 0; j <= last; j++) {
			if (j > 0 && d[j - 1] >= reach) break;
			const float from = j == 0 ? (airReaders ? minZ : (std::max)(minZ, lo)) : d[j - 1];
			if (reach <= from * 1.05f) continue;
			if (keepAir && reach >= maxZ * 0.95f) continue;
			for (int l = 0; l < 4; l++) {
				if (l > 0 && fabsf(lambdas[l] - lambda) < 0.01f) continue;
				Layout candidate;
				candidate.Start = j == 0 ? from : minZ;
				for (int i = 0; i < j; i++) candidate.S[i] = d[i];
				PracticalSplits(from, reach, last - j + 1, lambdas[l], &candidate.S[j]);
				if (keepAir) candidate.S[3] = maxZ;
				bool increasing = candidate.S[0] > candidate.Start * 1.01f;
				for (int i = 1; i < 4; i++) increasing = increasing && candidate.S[i] > candidate.S[i - 1] * 1.001f;
				if (!increasing) continue;
				float worst, gain, best;
				Compare(normal, candidate, lo, reach, k2, worst, gain, best);
				if (worst <= MaxCoarser && best >= MinLocalGain && gain >= MinGain && gain > choice.Gain) {
					choice.Fitted = true; choice.Chosen = candidate; choice.Gain = gain; choice.Worst = worst; choice.Keep = j; choice.Lambda = lambdas[l];
				}
			}
		}
		return choice;
	}

#ifndef SUNDEPTHFIT_MATH_ONLY

	inline D3DXVECTOR4 Data(0.0f, 0.0f, 0.0f, 0.0f);
	inline D3DXVECTOR4 Info(0.0f, 0.0f, 0.0f, 0.0f);
	inline D3DXVECTOR4 Splits(0.0f, 0.0f, 0.0f, 0.0f);
	inline D3DXVECTOR4 Original(0.0f, 0.0f, 0.0f, 0.0f);

	inline float UnfittedRadiusW = 0.0f;
	inline float CurrentMinZ = 0.0f, CurrentMaxZ = 0.0f;

	struct Pose { D3DXVECTOR3 Position = D3DXVECTOR3(0, 0, 0), Forward = D3DXVECTOR3(1, 0, 0); float Fov = 0.0f; };
	inline Pose CameraPose() {
		Pose pose;
		NiCamera* camera = WorldSceneGraph ? WorldSceneGraph->camera : nullptr;
		if (!camera) return pose;
		const NiMatrix33& r = camera->m_worldTransform.rot;
		pose.Position = D3DXVECTOR3(camera->m_worldTransform.pos.x, camera->m_worldTransform.pos.y, camera->m_worldTransform.pos.z);
		pose.Forward = D3DXVECTOR3(r.data[0][0], r.data[1][0], r.data[2][0]);
		D3DXVec3Normalize(&pose.Forward, &pose.Forward);
		pose.Fov = WorldSceneGraph->cameraFOV;
		return pose;
	}

	class FitEffect : public EffectRecord {
	public:
		FitEffect() : EffectRecord("SunDepthFit") {}
	};
	inline FitEffect* FitRecord = nullptr;
	inline bool EffectTried = false;

	struct QuerySet {
		IDirect3DQuery9* Query[Queries] = {};
		float Level[Queries] = {};
		int FarCount = 0, NearCount = 0;
		bool Pending = false;
		unsigned Frame = 0, Order = 0;
		Pose Camera;
		float MinZ = 0.0f, MaxZ = 0.0f;
		UINT Blocks = 0;
	};
	inline QuerySet Sets[RingSize];
	inline unsigned IssueOrder = 0;

	struct Result {
		bool Valid = false;
		int FarLevel = 0;
		int NearLevel = 0;
		bool Sky = true;
		unsigned Frame = 0;
		Pose Camera;
		float MinZ = 0.0f, MaxZ = 0.0f;
		DWORD Counts[Queries] = {};
		UINT Blocks = 0;
	};
	inline Result Latest;

	inline IDirect3DTexture9* BlockTexture = nullptr;
	inline IDirect3DSurface9* BlockSurface = nullptr;
	inline IDirect3DTexture9* ScratchTexture = nullptr;
	inline IDirect3DSurface9* ScratchSurface = nullptr;
	inline UINT GridW = 0, GridH = 0;
	inline bool ResourcesFailed = false;
	inline bool BlocksValid = false;

	inline unsigned Frame = 0;
	inline int AppliedFar = 0, AppliedNear = 0;
	inline bool AppliedAir = true;
	inline int HoldFrames = 0, HoldFar = 0, HoldNear = 0;
	inline bool HoldAir = true;
	inline float LastSplits[4] = {}, LastStart = 0.0f;
	inline bool LastFitted = false;

	struct Stats { unsigned Frames = 0, Fitted = 0, NoGain = 0, Moving = 0, NoMeasure = 0; double FarSum = 0.0, GainSum = 0.0; };
	inline Stats Window;

	inline float FarLevelDepth(float maxZ, int k) { return maxZ * exp2f(-k / 3.0f); }
	inline float NearLevelDepth(float minZ, int n) { return minZ * exp2f((float)n); }

	inline void ReleaseResources() {
		for (QuerySet& set : Sets) {
			for (IDirect3DQuery9*& query : set.Query) if (query) { query->Release(); query = nullptr; }
			set.Pending = false;
		}
		if (BlockSurface) { BlockSurface->Release(); BlockSurface = nullptr; }
		if (BlockTexture) { BlockTexture->Release(); BlockTexture = nullptr; }
		if (ScratchSurface) { ScratchSurface->Release(); ScratchSurface = nullptr; }
		if (ScratchTexture) { ScratchTexture->Release(); ScratchTexture = nullptr; }
		BlocksValid = false;
		Latest.Valid = false;
	}

	inline bool EnsureEffect() {
		if (FitRecord && FitRecord->Effect) { FitRecord->Enabled = true; return true; }
		if (EffectTried) return false;
		EffectTried = true;
		FitRecord = new FitEffect();
		FitRecord->LoadEffect();
		ID3DXEffect* fx = FitRecord->Effect;
		if (!fx || fx->GetTechniqueByName("Reduce") != fx->GetTechnique(0) || fx->GetTechniqueByName("Count") != fx->GetTechnique(1) ||
			fx->GetTechniqueByName("CascadeView") != fx->GetTechnique(2) || !fx->GetTechnique(2)) {
			Logger::Log("UNOFFICIAL FitToDepth: Effects\\SunDepthFit.fx.hlsl %s: the sun cascades keep their normal splits.",
				fx ? "does not have the Reduce, Count and CascadeView techniques" : "could not be loaded (missing, or see the compile error above)");
			if (fx) FitRecord->DisposeEffect();
			return false;
		}
		FitRecord->Enabled = true;
		return true;
	}

	inline bool EnsureResources(IDirect3DDevice9* device) {
		const UINT w = (TheRenderManager->width + BlockSize - 1) / BlockSize, h = (TheRenderManager->height + BlockSize - 1) / BlockSize;
		if (w == GridW && h == GridH && (BlockTexture || ResourcesFailed)) return BlockTexture != nullptr;
		ReleaseResources();
		GridW = w; GridH = h;
		ResourcesFailed = true;
		HRESULT hr = device->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &BlockTexture, NULL);
		if (SUCCEEDED(hr)) hr = BlockTexture->GetSurfaceLevel(0, &BlockSurface);
		if (SUCCEEDED(hr)) hr = device->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &ScratchTexture, NULL);
		if (SUCCEEDED(hr)) hr = ScratchTexture->GetSurfaceLevel(0, &ScratchSurface);
		for (QuerySet& set : Sets)
			for (IDirect3DQuery9*& query : set.Query)
				if (SUCCEEDED(hr)) hr = device->CreateQuery(D3DQUERYTYPE_OCCLUSION, &query);
		if (FAILED(hr)) {
			Logger::Log("UNOFFICIAL FitToDepth: the block grid (%ux%u) or its occlusion queries could not be made (%08lX): the sun cascades keep their normal splits.", w, h, hr);
			ReleaseResources();
			return false;
		}
		ResourcesFailed = false;
		Logger::Log("UNOFFICIAL FitToDepth: measuring the visible depth on a %ux%u grid of %dx%d pixel blocks, %d occlusion queries a frame, read back without waiting.",
			w, h, BlockSize, BlockSize, Queries);
		return true;
	}

	inline void Collect() {
		for (;;) {
			QuerySet* oldest = nullptr;
			for (QuerySet& set : Sets) if (set.Pending && (!oldest || set.Order < oldest->Order)) oldest = &set;
			if (!oldest) return;
			Result r;
			const int used = 1 + oldest->FarCount + oldest->NearCount;
			bool failed = false;
			for (int q = 0; q < used; q++) {
				const HRESULT hr = oldest->Query[q]->GetData(&r.Counts[q], sizeof(DWORD), 0);
				if (hr == S_FALSE) return;
				if (FAILED(hr)) { failed = true; break; }
			}
			oldest->Pending = false;
			if (failed) continue;
			const DWORD allowed = oldest->Blocks / 8192;
			r.Sky = r.Counts[0] > allowed;
			for (int k = 1; k <= oldest->FarCount && r.Counts[k] <= allowed; k++) r.FarLevel = k;
			for (int n = 1; n <= oldest->NearCount && r.Counts[oldest->FarCount + n] <= allowed; n++) r.NearLevel = n;
			r.Valid = true;
			r.Frame = oldest->Frame;
			r.Camera = oldest->Camera;
			r.MinZ = oldest->MinZ; r.MaxZ = oldest->MaxZ;
			r.Blocks = oldest->Blocks;
			Latest = r;
		}
	}

	inline bool Wanted() { return (Enabled || DebugView == 2) && TheShaderManager->GameState.isExterior; }

	inline void Measure(IDirect3DDevice9* device) {
		if (!Wanted() || CurrentMaxZ <= 0.0f) return;
		ShadowsExteriorEffect* shadows = TheShaderManager->Effects.ShadowsExteriors;
		if (!shadows || !shadows->Enabled || !shadows->Settings.Exteriors.Enabled) return;
		if (!EnsureEffect() || !EnsureResources(device)) return;
		Collect();
		QuerySet* set = nullptr;
		for (QuerySet& s : Sets) if (!s.Pending) { set = &s; break; }
		if (!set) return;

		set->MinZ = CurrentMinZ; set->MaxZ = CurrentMaxZ;
		set->FarCount = 0; set->NearCount = 0;
		set->Level[0] = 0.0f;
		for (int k = 1; k <= FarLevels; k++) {
			const float level = FarLevelDepth(set->MaxZ, k);
			if (level < (std::max)(FloorDepth, 8.0f * set->MinZ)) break;
			set->Level[++set->FarCount] = level;
		}
		for (int n = 1; n <= NearLevels; n++) {
			const float level = NearLevelDepth(set->MinZ, n);
			if (level >= set->MaxZ * 0.5f) break;
			set->NearCount++;
			set->Level[set->FarCount + set->NearCount] = level;
		}
		set->Blocks = RenderRect::Width(GridW) * RenderRect::Height(GridH);

		ID3DXEffect* fx = FitRecord->Effect;
		auto* renderState = TheRenderManager->renderState;
		IDirect3DSurface9* targetWas = nullptr;
		IDirect3DSurface9* depthWas = nullptr;
		device->GetRenderTarget(0, &targetWas);
		device->GetDepthStencilSurface(&depthWas);
		device->SetDepthStencilSurface(nullptr);

		bool ok = false;
		UINT passes = 0;
		device->SetRenderTarget(0, BlockSurface);
		fx->SetTechnique(fx->GetTechnique(0));
		FitRecord->SetCT();
		if (SUCCEEDED(fx->Begin(&passes, 0))) {
			if (passes > 0 && SUCCEEDED(fx->BeginPass(0))) {
				RenderRect::ApplyViewport(device);
				ok = SUCCEEDED(device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2));
				fx->EndPass();
			}
			fx->End();
		}
		if (ok) {
			ok = false;
			device->SetRenderTarget(0, ScratchSurface);
			fx->SetTechnique(fx->GetTechnique(1));
			Data = D3DXVECTOR4(0.0f, 1.0f, 1.0f / GridW, 1.0f / GridH);
			FitRecord->SetCT();
			D3DXHANDLE data = fx->GetParameterByName(NULL, "TESR_SunDepthFitData");
			if (data && SUCCEEDED(fx->Begin(&passes, 0))) {
				if (passes > 0 && SUCCEEDED(fx->BeginPass(0))) {
					device->SetTexture(2, BlockTexture);
					renderState->NoteDeviceTexture(2, BlockTexture);
					ok = true;
					RenderRect::ApplyViewport(device);
					const int used = 1 + set->FarCount + set->NearCount;
					for (int q = 0; q < used && ok; q++) {
						const float mode = q == 0 ? 1.0f : q <= set->FarCount ? 0.0f : 2.0f;
						Data = D3DXVECTOR4(set->Level[q], mode, 1.0f / GridW, 1.0f / GridH);
						fx->SetVector(data, &Data);
						fx->CommitChanges();
						ok = SUCCEEDED(set->Query[q]->Issue(D3DISSUE_BEGIN)) && SUCCEEDED(device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2)) &&
							SUCCEEDED(set->Query[q]->Issue(D3DISSUE_END));
					}
					fx->EndPass();
					device->SetTexture(2, nullptr);
					renderState->NoteDeviceTexture(2, nullptr);
				}
				fx->End();
			}
		}
		device->SetRenderTarget(0, targetWas);
		device->SetDepthStencilSurface(depthWas);
		RenderRect::ApplyViewport(device);
		if (targetWas) targetWas->Release();
		if (depthWas) depthWas->Release();
		if (!ok) {
			static bool said = false;
			if (!said) Logger::Log("UNOFFICIAL FitToDepth: a measuring pass failed; that frame is not measured.");
			said = true;
			return;
		}
		BlocksValid = true;
		set->Pending = true;
		set->Frame = Frame;
		set->Order = ++IssueOrder;
		set->Camera = CameraPose();
	}

	inline void Reset() {
		Latest.Valid = false;
		for (QuerySet& set : Sets) set.Pending = false;
		AppliedFar = 0; AppliedNear = 0; AppliedAir = true; HoldFrames = 0;
	}

	inline bool Choose(float minZ, float maxZ, float lambda, const float d[4], float fit[4], float& start, bool airReaders) {
		Frame++;
		CurrentMinZ = minZ; CurrentMaxZ = maxZ;
		for (int i = 0; i < 4; i++) fit[i] = d[i];
		start = minZ;
		Original = D3DXVECTOR4(d[0], d[1], d[2], d[3]);
		Splits = Original;
		Info = D3DXVECTOR4((float)DebugView, d[3], d[3], minZ);

		const bool exterior = TheShaderManager->GameState.isExterior;
		if (!Enabled || !exterior || TheShaderManager->GameState.isCameraCut) Reset();
		else if (BlockTexture) Collect();
		bool fitted = false;
		float chosenGain = 1.0f, chosenWorst = 1.0f, chosenLambda = lambda;
		int chosenKeep = -1;
		const char* why = "";
		if (Enabled && exterior) {
			int wantFar = 0, wantNear = 0;
			bool sky = true;
			why = "no measurement yet";
			if (Latest.Valid) {
				const Pose now = CameraPose();
				const D3DXVECTOR3 moved = now.Position - Latest.Camera.Position;
				const float reach = Latest.FarLevel ? FarLevelDepth(maxZ, Latest.FarLevel) : maxZ;
				const float turned = D3DXVec3Dot(&now.Forward, &Latest.Camera.Forward);
				if (Latest.MaxZ != maxZ || Latest.MinZ != minZ) why = "the shadow distance or the near plane changed";
				else if (Frame - Latest.Frame > (unsigned)MaxAge) why = "no recent measurement";
				else if (turned < cosf(3.0f * 3.14159265f / 180.0f)) why = "camera turning";
				else if (D3DXVec3Length(&moved) > (std::max)(100.0f, 0.05f * reach)) why = "camera moving";
				else if (fabsf(now.Fov - Latest.Camera.Fov) > 1.0f) why = "zoom";
				else { wantFar = Latest.FarLevel; wantNear = Latest.NearLevel; sky = Latest.Sky; why = ""; }
			}
			const bool wantAir = airReaders && sky;
			if (wantFar < AppliedFar || wantNear < AppliedNear || (wantAir && !AppliedAir)) {
				AppliedFar = (std::min)(AppliedFar, wantFar);
				AppliedNear = (std::min)(AppliedNear, wantNear);
				AppliedAir = AppliedAir || wantAir;
				HoldFrames = 0;
			}
			if (wantFar != AppliedFar || wantNear != AppliedNear || wantAir != AppliedAir) {
				if (HoldFrames == 0) { HoldFar = wantFar; HoldNear = wantNear; HoldAir = wantAir; }
				HoldFar = (std::min)(HoldFar, wantFar); HoldNear = (std::min)(HoldNear, wantNear); HoldAir = HoldAir || wantAir;
				if (++HoldFrames >= ShrinkFrames) {
					AppliedFar = HoldFar; AppliedNear = HoldNear; AppliedAir = HoldAir;
					HoldFrames = 0;
				}
			}
			else HoldFrames = 0;

			const float reach = AppliedFar ? FarLevelDepth(maxZ, AppliedFar) : maxZ;
			const float lo = AppliedNear ? (std::min)(NearLevelDepth(minZ, AppliedNear), reach * 0.5f) : minZ;
			Info.y = reach;
			Info.w = lo;
			if ((AppliedFar || AppliedNear) && reach > lo * 1.05f) {
				const D3DXMATRIX& proj = TheRenderManager->projMatrix;
				const float tx = proj._11 != 0.0f ? 1.0f / proj._11 : 1.0f, ty = proj._22 != 0.0f ? 1.0f / proj._22 : 0.5625f;
				const Choice choice = Search(minZ, maxZ, lambda, d, reach, lo, AppliedAir, airReaders, tx * tx + ty * ty);
				if (choice.Fitted) {
					fitted = true;
					for (int i = 0; i < 4; i++) fit[i] = choice.Chosen.S[i];
					start = choice.Chosen.Start;
					why = "";
					chosenGain = choice.Gain; chosenWorst = choice.Worst; chosenKeep = choice.Keep; chosenLambda = choice.Lambda;
				}
				else if (!*why) why = "no layout finer without a coarser spot";
			}
			else if (!*why) why = "the whole range is in view";
			Splits = D3DXVECTOR4(fit[0], fit[1], fit[2], fit[3]);
			Info.y = fitted ? (AppliedAir ? fit[2] : fit[3]) : d[3];

			if (GpuTimer::Enabled) {
				Window.Frames++;
				if (fitted) { Window.Fitted++; Window.FarSum += reach; Window.GainSum += chosenGain; }
				else if (!strcmp(why, "camera turning") || !strcmp(why, "camera moving") || !strcmp(why, "zoom")) Window.Moving++;
				else if (!strcmp(why, "no layout finer without a coarser spot") || !strcmp(why, "the whole range is in view")) Window.NoGain++;
				else Window.NoMeasure++;
				if (Window.Frames >= 240) {
					Logger::Log("SUN DEPTH FIT STATS: %u frames, fitted in %u (mean far reach %.0f, %.2fx finer on average), unfitted: %u camera moving, %u nothing to gain, %u no measurement",
						Window.Frames, Window.Fitted, Window.Fitted ? Window.FarSum / Window.Fitted : 0.0, Window.Fitted ? Window.GainSum / Window.Fitted : 1.0,
						Window.Moving, Window.NoGain, Window.NoMeasure);
					Window = Stats();
				}
			}
		}

		bool changed = fitted != LastFitted || start != LastStart;
		for (int i = 0; i < 4; i++) changed = changed || fit[i] != LastSplits[i];
		if (changed && (fitted || LastFitted)) {
			TheShadowManager->ForceAllCascades = true;
			if (Log)
				Logger::Log("SUN DEPTH FIT: %s | splits %.0f %.0f %.0f %.0f from %.0f (normal %.0f %.0f %.0f %.0f from %.0f) | visible %.0f..%.0f%s%s | kept %d, lambda %.2f, %.2fx finer, worst %.3f%s%s",
					fitted ? "FITTED" : "normal", fit[0], fit[1], fit[2], fit[3], start, d[0], d[1], d[2], d[3], minZ, Info.w, Info.y,
					Latest.Sky ? ", sky" : "", AppliedAir ? ", last cascade kept for the air" : "", chosenKeep, chosenLambda, chosenGain, chosenWorst,
					*why ? " | " : "", why);
		}
		LastFitted = fitted;
		LastStart = start;
		for (int i = 0; i < 4; i++) LastSplits[i] = fit[i];
		return fitted;
	}

	inline void RenderView(IDirect3DDevice9* device, IDirect3DSurface9* renderTarget) {
		if (DebugView <= 0 || !TheShaderManager->GameState.isExterior) return;
		if (!EnsureEffect()) return;
		Info.x = (DebugView == 2 && BlockTexture && BlocksValid) ? 2.0f : 1.0f;
		if (Info.x > 1.5f) FitRecord->SetPassTexture(2, BlockTexture);
		FitRecord->writesWholeTarget = true;
		FitRecord->Render(device, renderTarget, TheTextureManager->RenderedSurface, 2, false, nullptr);
		FitRecord->writesWholeTarget = false;
		FitRecord->ClearPassTextures();
	}
#endif
}
