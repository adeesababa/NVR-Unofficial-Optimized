#include <algorithm>
#include <vector>

#include "SkinScattering.h"
#include "../core/PassSums.h"

static const float SkinScatterMMPerUnit = 14.2857f;

void SkinScatteringEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_SkinScatterData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_SkinScatterKernel", &Constants.Kernel[0]);
	TheShaderManager->RegisterConstant("TESR_SkinScatterDebug", &Constants.Debug);
}

void SkinScatteringEffect::RegisterTextures() {
	TheTextureManager->RegisterTexture("TESR_SkinScatterBuffer", (IDirect3DBaseTexture9**)&Texture);
	TheTextureManager->RegisterTexture("TESR_SkinAlbedoBuffer", (IDirect3DBaseTexture9**)&AlbedoTexture);

	D3DCAPS9 Caps;
	TheRenderManager->device->GetDeviceCaps(&Caps);
	Supported = Caps.NumSimultaneousRTs >= 3
		&& (Caps.PrimitiveMiscCaps & D3DPMISCCAPS_INDEPENDENTWRITEMASKS)
		&& (Caps.PrimitiveMiscCaps & D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS);
	if (!Supported) Logger::Log("[WARNING] SkinScattering: the device cannot bind three render targets of their own formats; skin scattering is off");
}

static void ReadProfile(SkinScatteringEffect::ProfileStruct* Profile, const char* Section) {
	Profile->ScreenSpace = TheSettingManager->GetSettingI(Section, "ScreenSpace") != 0;
	Profile->Width = max(0.0f, TheSettingManager->GetSettingF(Section, "Width"));
	Profile->DepthFollow = max(0.0f, TheSettingManager->GetSettingF(Section, "DepthFollow"));
	Profile->AlbedoDetail = std::clamp(TheSettingManager->GetSettingF(Section, "AlbedoDetail"), 0.0f, 1.0f);
	Profile->Strength = D3DXVECTOR3(
		TheSettingManager->GetSettingF(Section, "StrengthRed"),
		TheSettingManager->GetSettingF(Section, "StrengthGreen"),
		TheSettingManager->GetSettingF(Section, "StrengthBlue"));
	Profile->Falloff = D3DXVECTOR3(
		std::clamp(TheSettingManager->GetSettingF(Section, "FalloffRed"), 0.0f, 1.0f),
		std::clamp(TheSettingManager->GetSettingF(Section, "FalloffGreen"), 0.0f, 1.0f),
		std::clamp(TheSettingManager->GetSettingF(Section, "FalloffBlue"), 0.0f, 1.0f));
}

void SkinScatteringEffect::UpdateSettings() {
	ReadProfile(&ExteriorProfile, "Shaders.Skin.Scattering");
	ReadProfile(&InteriorProfile, "Shaders.Skin.Interiors");
	const int DebugView = std::clamp(TheSettingManager->GetSettingI("Shaders.Skin.Debug", "DebugView"), 0, SkinShaders::DebugViewCount);
	Constants.Debug.x = DebugView >= SkinShaders::FirstScatteringDebugView ? (float)(DebugView - SkinShaders::FirstScatteringDebugView + 1) : 0.0f;
	SkinShaderDebug = DebugView > 0 && DebugView < SkinShaders::FirstScatteringDebugView;
	KernelDirty = true;
}

void SkinScatteringEffect::UpdateConstants() {
	if (!SkinShaders::NewSkinEnabled()) return;
	const bool isInterior = !TheShaderManager->GameState.isExterior;
	if (isInterior != ProfileIsInterior) {
		ProfileIsInterior = isInterior;
		KernelDirty = true;
	}
	Profile = isInterior ? InteriorProfile : ExteriorProfile;
	ScreenSpace = Profile.ScreenSpace;

	if (KernelDirty) {
		CalculateKernel();
		KernelDirty = false;
	}
	Constants.Data.x = Profile.Width / SkinScatterMMPerUnit;
	Constants.Data.y = Profile.DepthFollow;
	Constants.Data.z = 0.002f;
	Constants.Data.w = Profile.AlbedoDetail;
	TAAEffect* TAA = TheShaderManager->Effects.TAA;
	Constants.Debug.y = TAA && TAA->Enabled && TAA->Effect ? 1.0f : 0.0f;
	Constants.Debug.z = (float)(TheFrameRateManager->FrameNumber % 64);
}

static D3DXVECTOR3 SkinScatterGaussian(const D3DXVECTOR3& Falloff, float Variance, float R) {
	float g[3];
	const float falloff[3] = { Falloff.x, Falloff.y, Falloff.z };
	for (int i = 0; i < 3; i++) {
		float rr = R / (0.001f + falloff[i]);
		g[i] = exp(-(rr * rr) / (2.0f * Variance)) / (2.0f * 3.14f * Variance);
	}
	return D3DXVECTOR3(g[0], g[1], g[2]);
}

static D3DXVECTOR3 SkinScatterProfile(const D3DXVECTOR3& Falloff, float R) {
	return 0.100f * SkinScatterGaussian(Falloff, 0.0484f, R)
		+ 0.118f * SkinScatterGaussian(Falloff, 0.187f, R)
		+ 0.113f * SkinScatterGaussian(Falloff, 0.567f, R)
		+ 0.358f * SkinScatterGaussian(Falloff, 1.99f, R)
		+ 0.078f * SkinScatterGaussian(Falloff, 7.41f, R);
}

void SkinScatteringEffect::CalculateKernel() {
	const int N = KernelSamples;
	const float Range = N > 20 ? 3.0f : 2.0f;
	const float Exponent = 2.0f;
	D3DXVECTOR4* K = Constants.Kernel;

	float step = 2.0f * Range / (N - 1);
	for (int i = 0; i < N; i++) {
		float o = -Range + float(i) * step;
		float sign = o < 0.0f ? -1.0f : 1.0f;
		K[i].w = Range * sign * fabs(pow(o, Exponent)) / pow(Range, Exponent);
	}

	for (int i = 0; i < N; i++) {
		float w0 = i > 0 ? fabs(K[i].w - K[i - 1].w) : 0.0f;
		float w1 = i < N - 1 ? fabs(K[i].w - K[i + 1].w) : 0.0f;
		float area = (w0 + w1) / 2.0f;
		D3DXVECTOR3 t = area * SkinScatterProfile(Profile.Falloff, K[i].w);
		K[i].x = t.x;
		K[i].y = t.y;
		K[i].z = t.z;
	}

	D3DXVECTOR4 centre = K[N / 2];
	for (int i = N / 2; i > 0; i--) K[i] = K[i - 1];
	K[0] = centre;

	D3DXVECTOR3 sum(0.0f, 0.0f, 0.0f);
	for (int i = 0; i < N; i++) sum += D3DXVECTOR3(K[i].x, K[i].y, K[i].z);
	for (int i = 0; i < N; i++) {
		K[i].x /= max(sum.x, 1e-6f);
		K[i].y /= max(sum.y, 1e-6f);
		K[i].z /= max(sum.z, 1e-6f);
	}

	const D3DXVECTOR3& s = Profile.Strength;
	K[0].x = (1.0f - s.x) + s.x * K[0].x;
	K[0].y = (1.0f - s.y) + s.y * K[0].y;
	K[0].z = (1.0f - s.z) + s.z * K[0].z;
	for (int i = 1; i < N; i++) {
		K[i].x *= s.x;
		K[i].y *= s.y;
		K[i].z *= s.z;
	}
}

void SkinScatteringEffect::ReleaseTarget() {
	if (MSAASurface) { MSAASurface->Release(); MSAASurface = nullptr; }
	if (TextureSurface) { TextureSurface->Release(); TextureSurface = nullptr; }
	if (Texture) { Texture->Release(); Texture = nullptr; }
	if (AlbedoMSAASurface) { AlbedoMSAASurface->Release(); AlbedoMSAASurface = nullptr; }
	if (AlbedoTextureSurface) { AlbedoTextureSurface->Release(); AlbedoTextureSurface = nullptr; }
	if (AlbedoTexture) { AlbedoTexture->Release(); AlbedoTexture = nullptr; }
	TargetDesc = {};
	ClearSampler("TESR_SkinScatterBuffer", strlen("TESR_SkinScatterBuffer"));
	ClearSampler("TESR_SkinAlbedoBuffer", strlen("TESR_SkinAlbedoBuffer"));
}

bool SkinScatteringEffect::EnsureTarget(const D3DSURFACE_DESC& SceneDesc) {
	if (SceneDesc.Width != TheRenderManager->width || SceneDesc.Height != TheRenderManager->height) return false;

	if (Texture && TargetDesc.Width == SceneDesc.Width && TargetDesc.Height == SceneDesc.Height &&
		TargetDesc.MultiSampleType == SceneDesc.MultiSampleType && TargetDesc.MultiSampleQuality == SceneDesc.MultiSampleQuality)
		return true;

	if (Texture && ++Recreations > 2) {
		Logger::Log("[WARNING] SkinScattering: skin is drawn into more than one full-screen target; skin scattering is off");
		ReleaseTarget();
		Supported = false;
		return false;
	}

	ReleaseTarget();
	IDirect3DDevice9* Device = TheRenderManager->device;
	if (FAILED(Device->CreateTexture(SceneDesc.Width, SceneDesc.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &Texture, NULL))) {
		Logger::Log("[ERROR] SkinScattering: could not create the skin target; skin scattering is off");
		Texture = nullptr;
		Supported = false;
		return false;
	}
	Texture->GetSurfaceLevel(0, &TextureSurface);
	if (FAILED(Device->CreateTexture(SceneDesc.Width, SceneDesc.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &AlbedoTexture, NULL))) {
		Logger::Log("[ERROR] SkinScattering: could not create the skin albedo target; skin scattering is off");
		AlbedoTexture = nullptr;
		ReleaseTarget();
		Supported = false;
		return false;
	}
	AlbedoTexture->GetSurfaceLevel(0, &AlbedoTextureSurface);

	if (SceneDesc.MultiSampleType != D3DMULTISAMPLE_NONE) {
		if (FAILED(Device->CreateRenderTarget(SceneDesc.Width, SceneDesc.Height, D3DFMT_A16B16G16R16F,
			SceneDesc.MultiSampleType, SceneDesc.MultiSampleQuality, FALSE, &MSAASurface, NULL)) ||
			FAILED(Device->CreateRenderTarget(SceneDesc.Width, SceneDesc.Height, D3DFMT_A16B16G16R16F,
			SceneDesc.MultiSampleType, SceneDesc.MultiSampleQuality, FALSE, &AlbedoMSAASurface, NULL))) {
			Logger::Log("[ERROR] SkinScattering: could not create the multisampled skin targets; skin scattering is off");
			ReleaseTarget();
			Supported = false;
			return false;
		}
	}

	TargetDesc = SceneDesc;
	Logger::Log("SkinScattering: skin target %ux%u, %u samples", SceneDesc.Width, SceneDesc.Height, (UInt32)SceneDesc.MultiSampleType);
	return true;
}

void SkinScatteringEffect::ClearTarget(IDirect3DSurface9* SceneTarget) {
	IDirect3DDevice9* Device = TheRenderManager->device;
	D3DVIEWPORT9 Viewport;
	RECT ScissorRect;
	DWORD Scissor, WriteMask;
	Device->GetViewport(&Viewport);
	Device->GetScissorRect(&ScissorRect);
	Device->GetRenderState(D3DRS_SCISSORTESTENABLE, &Scissor);
	Device->GetRenderState(D3DRS_COLORWRITEENABLE, &WriteMask);
	Device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
	Device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);

	Device->SetRenderTarget(0, MSAASurface ? MSAASurface : TextureSurface);
	Device->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);
	Device->SetRenderTarget(0, AlbedoMSAASurface ? AlbedoMSAASurface : AlbedoTextureSurface);
	Device->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);
	Device->SetRenderTarget(0, SceneTarget);

	Device->SetRenderState(D3DRS_COLORWRITEENABLE, WriteMask);
	Device->SetRenderState(D3DRS_SCISSORTESTENABLE, Scissor);
	Device->SetScissorRect(&ScissorRect);
	Device->SetViewport(&Viewport);
}

void SkinScatteringEffect::BeginFrame() {
	Unbind();
	ClearedThisFrame = false;
	SkinDrawn = false;
}

void SkinScatteringEffect::BindForSkinDraw(NiD3DPixelShaderEx* PixelShader) {
	if (Bound) return;
	if (!Enabled || !ScreenSpace || !Supported || Effect == nullptr) return;

	if (!InWorldScene) return;
	if (!TheSettingManager->SettingsMain.Main.RenderEffects || !TheShaderManager->Shaders.Skin->Enabled) return;

	if (!PixelShader || PixelShader->ShaderHandle == PixelShader->ShaderHandleBackup) return;
	if (TheRenderManager->renderState->GetPixelShader() != PixelShader->ShaderHandle) return;

	IDirect3DDevice9* Device = TheRenderManager->device;

	DWORD Blend = FALSE;
	Device->GetRenderState(D3DRS_ALPHABLENDENABLE, &Blend);
	bool Additive = false;
	if (Blend) {
		DWORD Src, Dst;
		Device->GetRenderState(D3DRS_SRCBLEND, &Src);
		Device->GetRenderState(D3DRS_DESTBLEND, &Dst);
		if (Src != D3DBLEND_ONE || Dst != D3DBLEND_ONE) return;
		Additive = true;
	}

	IDirect3DSurface9* Scene = nullptr;
	if (FAILED(Device->GetRenderTarget(0, &Scene)) || !Scene) return;
	D3DSURFACE_DESC SceneDesc;
	Scene->GetDesc(&SceneDesc);
	if (SceneDesc.MultiSampleType != D3DMULTISAMPLE_NONE) {
		Scene->Release();
		Logger::Log("[WARNING] SkinScattering: the scene is multisampled (the game's MSAA); drawing with extra multisampled targets bound draws nothing on D3D9 drivers such as NVIDIA's, so screen-space skin scattering is off (per-pixel diffusion instead). Switch MSAA off to use it.");
		Supported = false;
		return;
	}
	if (!EnsureTarget(SceneDesc)) {
		Scene->Release();
		return;
	}

	if (!ClearedThisFrame) {
		ClearTarget(Scene);
		ClearedThisFrame = true;
	}
	Scene->Release();

	D3DVIEWPORT9 Viewport;
	Device->GetViewport(&Viewport);
	Device->GetRenderState(D3DRS_COLORWRITEENABLE1, &SavedWriteMask1);
	Device->GetRenderState(D3DRS_COLORWRITEENABLE2, &SavedWriteMask2);
	Device->SetRenderState(D3DRS_COLORWRITEENABLE1, Additive ? (D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE) : 0xF);
	Device->SetRenderState(D3DRS_COLORWRITEENABLE2, Additive ? 0 : 0xF);
	Device->SetRenderTarget(1, MSAASurface ? MSAASurface : TextureSurface);
	Device->SetRenderTarget(2, AlbedoMSAASurface ? AlbedoMSAASurface : AlbedoTextureSurface);
	Device->SetViewport(&Viewport);

	Bound = true;
	SkinDrawn = true;
}

static bool PixelShaderWritesColor1(const std::vector<DWORD>& Code) {
	const size_t Count = Code.size();
	if (Count < 2 || (Code[0] & 0xFFFF0000) != 0xFFFF0000 || ((Code[0] >> 8) & 0xFF) < 2) return false;
	for (size_t i = 1; i < Count; ) {
		const DWORD Token = Code[i];
		const DWORD Opcode = Token & 0xFFFF;
		if (Opcode == 0xFFFF) break;
		if (Opcode == 0xFFFE) { i += 1 + ((Token >> 16) & 0x7FFF); continue; }
		const DWORD Length = (Token >> 24) & 0x0F;
		const bool Declaration = Opcode == 0x1F || Opcode == 0x51 || Opcode == 0x30 || Opcode == 0x2F;
		if (!Declaration && Length >= 1 && i + 1 < Count) {
			const DWORD Dest = Code[i + 1];
			const DWORD Type = ((Dest >> 28) & 0x7) | ((Dest >> 8) & 0x18);
			if (Type == D3DSPR_COLOROUT && (Dest & D3DSP_REGNUM_MASK) == 1) return true;
		}
		i += 1 + Length;
	}
	return false;
}

bool SkinScatteringEffect::BindForGameHighlights(NiD3DPixelShaderEx* PixelShader) {
	if (HighlightsBound) return true;
	if (Bound) return false;
	if (!Enabled || !ScreenSpace || !Supported || Effect == nullptr) return true;
	if (!InWorldScene) return true;
	if (PassSums::Active != PassSums::None) return false;

	IDirect3DDevice9* Device = TheRenderManager->device;
	IDirect3DSurface9* Scene = nullptr;
	if (FAILED(Device->GetRenderTarget(0, &Scene)) || !Scene) return false;
	D3DSURFACE_DESC SceneDesc;
	Scene->GetDesc(&SceneDesc);
	if (SceneDesc.Width != TheRenderManager->width || SceneDesc.Height != TheRenderManager->height || SceneDesc.MultiSampleType != D3DMULTISAMPLE_NONE) {
		Scene->Release();
		return true;
	}

	DWORD Blend = FALSE, Op = 0, Src = 0, Dst = 0;
	Device->GetRenderState(D3DRS_ALPHABLENDENABLE, &Blend);
	Device->GetRenderState(D3DRS_BLENDOP, &Op);
	Device->GetRenderState(D3DRS_SRCBLEND, &Src);
	Device->GetRenderState(D3DRS_DESTBLEND, &Dst);
	bool Copy = Blend && Op == D3DBLENDOP_ADD && Dst == D3DBLEND_ONE && (Src == D3DBLEND_ONE || Src == D3DBLEND_SRCALPHA);

	IDirect3DPixelShader9* Shader = PixelShader ? PixelShader->ShaderHandle : nullptr;
	if (Copy) Copy = Shader && TheRenderManager->renderState->GetPixelShader() == Shader;
	if (Copy) {
		UINT Size = 0;
		Copy = SUCCEEDED(Shader->GetFunction(NULL, &Size)) && Size >= 2 * sizeof(DWORD);
		if (Copy) {
			auto Known = WritesColor1.find(Shader);
			if (Known == WritesColor1.end() || Known->second.first != Size) {
				std::vector<DWORD> Code(Size / sizeof(DWORD));
				const bool Writes = SUCCEEDED(Shader->GetFunction(Code.data(), &Size)) && PixelShaderWritesColor1(Code);
				Known = WritesColor1.insert_or_assign(Shader, std::make_pair(Size, Writes)).first;
				if (!Writes) Logger::Log("[WARNING] SkinScattering: the game's highlight pass on skin uses pixel shader %s, which does not write the skin target (COLOR1; ObjectTemplate.hlsl from this build?): those highlights are muted while the skin blur runs",
					PixelShader->Name ? PixelShader->Name : "?");
			}
			Copy = Known->second.second;
		}
	}
	if (!Copy || !EnsureTarget(SceneDesc)) {
		Scene->Release();
		return false;
	}
	if (!ClearedThisFrame) {
		ClearTarget(Scene);
		ClearedThisFrame = true;
	}
	Scene->Release();

	D3DVIEWPORT9 Viewport;
	Device->GetViewport(&Viewport);
	Device->GetRenderState(D3DRS_COLORWRITEENABLE1, &SavedWriteMask1);
	Device->GetRenderState(D3DRS_COLORWRITEENABLE2, &SavedWriteMask2);
	Device->SetRenderState(D3DRS_COLORWRITEENABLE1, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
	Device->SetRenderTarget(1, MSAASurface ? MSAASurface : TextureSurface);
	Device->SetViewport(&Viewport);
	HighlightsBound = true;
	return true;
}

void SkinScatteringEffect::Unbind() {
	if (!Bound && !HighlightsBound) return;
	IDirect3DDevice9* Device = TheRenderManager->device;
	D3DVIEWPORT9 Viewport;
	Device->GetViewport(&Viewport);
	Device->SetRenderTarget(1, NULL);
	Device->SetRenderTarget(2, NULL);
	Device->SetViewport(&Viewport);
	Device->SetRenderState(D3DRS_COLORWRITEENABLE1, SavedWriteMask1);
	Device->SetRenderState(D3DRS_COLORWRITEENABLE2, SavedWriteMask2);
	Bound = false;
	HighlightsBound = false;
}

bool SkinScatteringEffect::ShouldRender() {
	if (!SkinShaders::NewSkinEnabled()) return false;
	if (!ScreenSpace || !SkinDrawn || !Texture || !TextureSurface || !AlbedoTextureSurface) return false;
	if (SkinShaderDebug) return false;
	if (MSAASurface) TheRenderManager->device->StretchRect(MSAASurface, NULL, TextureSurface, NULL, D3DTEXF_NONE);
	if (AlbedoMSAASurface) TheRenderManager->device->StretchRect(AlbedoMSAASurface, NULL, AlbedoTextureSurface, NULL, D3DTEXF_NONE);
	return true;
}
