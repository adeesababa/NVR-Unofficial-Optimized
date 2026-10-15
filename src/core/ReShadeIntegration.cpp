#ifdef assert
#undef assert
#endif
#include <cassert>
#ifdef IMGUI_VERSION_NUM
#undef IMGUI_VERSION_NUM
#endif
#include <reshade.hpp>
#include "TestTools.h"
#if NVR_TEST_TOOLS
#include "SettingShots.h"
#endif

using namespace reshade::api;

namespace {

	HMODULE NvrModule = nullptr;
	HMODULE ReShadeModule = nullptr;
	bool Registered = false;
	UInt32 RegisterTries = 0;
	UInt32 FramesSeen = 0;

	effect_runtime* Runtime = nullptr;
	bool ReportedVulkan = false;
	UInt32 PresentCount = 0;
	UInt32 DepthCopiedAt = ~0u;
	bool DepthBound = false;

	IDirect3DTexture9* DepthTexture = nullptr;
	IDirect3DSurface9* DepthSurface = nullptr;
	ID3DXEffect* DepthEffect = nullptr;
	D3DXHANDLE ParamsHandle = NULL;
	D3DXHANDLE SourceHandle = NULL;
	bool DepthFailed = false;
	IDirect3DStateBlock9* SavedState = nullptr;

	const char DepthCopyFX[] = R"(
float4 Params;	// x near, y far, z 1 = the game's depth is reversed, w 1 = single-channel (linear) source
texture CombinedDepth;
sampler2D Source = sampler_state { Texture = <CombinedDepth>; ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };

struct VSOUT { float4 pos : POSITION; float2 uv : TEXCOORD0; };

VSOUT VS(float4 pos : POSITION0, float2 uv : TEXCOORD0) {
	VSOUT OUT;
	OUT.pos = pos;
	OUT.uv = uv;
	return OUT;
}

float4 PS(float2 uv : TEXCOORD0) : COLOR0 {
	float4 texel = tex2D(Source, uv);
	float reversed = texel.y;
	if (Params.w > 0.5) {
		float viewZ = max(texel.x * Params.y, 1.0e-6);
		reversed = Params.x * (Params.y - viewZ) / (viewZ * (Params.y - Params.x));
	}
	float depth = Params.z > 0.5 ? reversed : 1.0 - reversed;
	return float4(depth, depth, depth, 1.0);
}

technique Copy {
	pass P0 {
		ZEnable = false; ZWriteEnable = false; StencilEnable = false; AlphaBlendEnable = false; AlphaTestEnable = false;
		ColorWriteEnable = 15; CullMode = None; ScissorTestEnable = false; SRGBWriteEnable = false; FogEnable = false;
		VertexShader = compile vs_3_0 VS();
		PixelShader = compile ps_3_0 PS();
	}
}
)";

	HMODULE FindReShade() {
		HMODULE modules[1024];
		DWORD bytes = 0;
		if (!K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &bytes)) return nullptr;
		const DWORD count = min(bytes, (DWORD)sizeof(modules)) / sizeof(HMODULE);
		for (DWORD i = 0; i < count; ++i)
			if (GetProcAddress(modules[i], "ReShadeRegisterAddon") && GetProcAddress(modules[i], "ReShadeUnregisterAddon"))
				return modules[i];
		return nullptr;
	}

	bool IsD3D9(effect_runtime* runtime) {
		return runtime && runtime->get_device()->get_api() == device_api::d3d9;
	}

	void TakeRuntime(effect_runtime* runtime) {
		if (Runtime || !runtime) return;
		if (!IsD3D9(runtime)) {
			if (!ReportedVulkan) {
				Logger::Log("UNOFFICIAL ReShade: its runtime is not D3D9 (ReShade as a Vulkan layer under DXVK?): NVR depth and drawing before the HUD are off, only ReShadeDepthCompat applies.");
				ReportedVulkan = true;
			}
			return;
		}
		Runtime = runtime;
		Logger::Log("UNOFFICIAL ReShade: D3D9 runtime found.");
	}

	void OnInitRuntime(effect_runtime* runtime) { TakeRuntime(runtime); }

	void OnDestroyRuntime(effect_runtime* runtime) {
		if (runtime == Runtime) Runtime = nullptr;
	}

	void OnPresent(effect_runtime* runtime) {
		TakeRuntime(runtime);
		if (runtime != Runtime) return;
		PresentCount++;
#if NVR_TEST_TOOLS
		SettingShots::CaptureFinal();
#endif
	}

	void OnBeginEffects(effect_runtime* runtime, command_list*, resource_view, resource_view) {
		if (runtime != Runtime || !DepthTexture || DepthCopiedAt != PresentCount) return;
		if (!TheSettingManager->SettingsMain.Main.ReShadeNVRDepth) return;
		const resource_view depth = { reinterpret_cast<uintptr_t>(DepthTexture) };
		runtime->update_texture_bindings("DEPTH", depth, depth);
		DepthBound = true;
	}

	void OnFinishEffects(effect_runtime* runtime, command_list*, resource_view, resource_view) {
		if (runtime != Runtime || !DepthBound) return;
		runtime->update_texture_bindings("DEPTH", resource_view{ 0 }, resource_view{ 0 });
		DepthBound = false;
	}

	bool TryRegister() {
		RegisterTries++;
		const auto registerAddon = reinterpret_cast<bool(*)(void*, uint32_t)>(GetProcAddress(ReShadeModule, "ReShadeRegisterAddon"));
		if (!registerAddon || !NvrModule) return false;
		uint32_t version = 0;
		for (uint32_t v = RESHADE_API_VERSION; v >= 1 && !version; --v)
			if (registerAddon(NvrModule, v)) version = v;
		if (!version) {
			Logger::Log("UNOFFICIAL ReShade: ReShade refused NVR as an add-on (API versions %u..1, try %u); see ReShade.log.", RESHADE_API_VERSION, RegisterTries);
			return false;
		}
		reshade::register_event<reshade::addon_event::init_effect_runtime>(OnInitRuntime);
		reshade::register_event<reshade::addon_event::destroy_effect_runtime>(OnDestroyRuntime);
		reshade::register_event<reshade::addon_event::reshade_present>(OnPresent);
		reshade::register_event<reshade::addon_event::reshade_begin_effects>(OnBeginEffects);
		reshade::register_event<reshade::addon_event::reshade_finish_effects>(OnFinishEffects);
		Registered = true;
		Logger::Log("UNOFFICIAL ReShade: NVR registered as a ReShade add-on (API version %u, headers %u).", version, RESHADE_API_VERSION);
		return true;
	}

	bool CreateDepthCopy(IDirect3DDevice9* Device) {
		ID3DXBuffer* errors = nullptr;
		HRESULT result = D3DXCreateEffect(Device, DepthCopyFX, sizeof(DepthCopyFX) - 1, NULL, NULL, D3DXFX_LARGEADDRESSAWARE, NULL, &DepthEffect, &errors);
		if (FAILED(result)) {
			Logger::Log("UNOFFICIAL ReShade: depth copy shader failed (%08lx): %s", result, errors ? (const char*)errors->GetBufferPointer() : "");
			if (errors) errors->Release();
			return false;
		}
		if (errors) errors->Release();
		ParamsHandle = DepthEffect->GetParameterByName(NULL, "Params");
		SourceHandle = DepthEffect->GetParameterByName(NULL, "CombinedDepth");
		if (!ParamsHandle || !SourceHandle) {
			Logger::Log("UNOFFICIAL ReShade: depth copy shader parameters missing.");
			return false;
		}
		result = Device->CreateTexture(TheRenderManager->width, TheRenderManager->height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT, &DepthTexture, NULL);
		if (FAILED(result) || FAILED(DepthTexture->GetSurfaceLevel(0, &DepthSurface))) {
			Logger::Log("UNOFFICIAL ReShade: depth texture (R32F %ux%u) failed (%08lx).", TheRenderManager->width, TheRenderManager->height, result);
			return false;
		}
		return true;
	}

	void CopyDepth(IDirect3DDevice9* Device) {
		IDirect3DTexture9* source = TheShaderManager->Effects.CombineDepth ? TheShaderManager->Effects.CombineDepth->Textures.CombinedDepthTexture : nullptr;
		if (!source || DepthFailed) return;
		if (!DepthEffect && !CreateDepthCopy(Device)) {
			DepthFailed = true;
			return;
		}
		D3DSURFACE_DESC desc = {};
		source->GetLevelDesc(0, &desc);
		const D3DXVECTOR4& camera = TheRenderManager->CameraData;
		const D3DXVECTOR4 params(camera.x, camera.y, TheRenderManager->IsReversedDepth() ? 1.0f : 0.0f, desc.Format == D3DFMT_R32F ? 1.0f : 0.0f);
		DepthEffect->SetVector(ParamsHandle, &params);
		DepthEffect->SetTexture(SourceHandle, source);
		Device->SetRenderTarget(0, DepthSurface);
		for (DWORD i = 1; i < 4; ++i) Device->SetRenderTarget(i, nullptr);
		Device->SetStreamSource(0, TheShaderManager->FrameVertex, 0, sizeof(FrameVS));
		Device->SetFVF(FrameFVF);
		UINT passes = 0;
		if (SUCCEEDED(DepthEffect->Begin(&passes, 0))) {
			if (SUCCEEDED(DepthEffect->BeginPass(0))) {
				Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
				DepthEffect->EndPass();
			}
			DepthEffect->End();
		}
		DepthEffect->SetTexture(SourceHandle, nullptr);
		DepthCopiedAt = PresentCount;
		static bool reported = false;
		if (!reported) {
			Logger::Log("UNOFFICIAL ReShade: NVR's combined depth handed to ReShade as DEPTH (%s source, %s depth).",
				desc.Format == D3DFMT_R32F ? "R32F" : "G32R32F", TheRenderManager->IsReversedDepth() ? "reversed" : "standard");
			reported = true;
		}
	}

	bool CanDrawInto(IDirect3DDevice9* Device, IDirect3DSurface9* target, bool& isBackBuffer) {
		isBackBuffer = false;
		D3DSURFACE_DESC desc = {}, backDesc = {};
		if (!target || FAILED(target->GetDesc(&desc)) || desc.MultiSampleType != D3DMULTISAMPLE_NONE) return false;
		IDirect3DSurface9* backBuffer = nullptr;
		if (FAILED(Device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer)) || !backBuffer) return false;
		isBackBuffer = backBuffer == target;
		const bool fits = isBackBuffer || (SUCCEEDED(backBuffer->GetDesc(&backDesc)) &&
			backDesc.Width == desc.Width && backDesc.Height == desc.Height && backDesc.Format == desc.Format);
		backBuffer->Release();
		return fits;
	}

	void DrawEffects(IDirect3DDevice9* Device, IDirect3DSurface9* OutputSurface) {
		bool isBackBuffer = false;
		static bool reportedDrawn = false, reportedSkipped = false;
		const bool fits = CanDrawInto(Device, OutputSurface, isBackBuffer);
		if (fits && !reportedDrawn) {
			Logger::Log("UNOFFICIAL ReShade: effects drawn before the HUD (into %s).", isBackBuffer ? "the back buffer" : "NVR's output, back buffer size and format");
			reportedDrawn = true;
		}
		if (!fits && !reportedSkipped) {
			Logger::Log("UNOFFICIAL ReShade: NVR's output does not match the back buffer (size, format or multisampling); ReShade draws those frames at present, over the HUD.");
			reportedSkipped = true;
		}
		if (!fits) return;
		Device->SetRenderTarget(0, OutputSurface);
		for (DWORD i = 1; i < 4; ++i) Device->SetRenderTarget(i, nullptr);
		const uintptr_t handle = reinterpret_cast<uintptr_t>(OutputSurface);
		Runtime->render_effects(Runtime->get_command_queue()->get_immediate_command_list(), resource_view{ handle }, resource_view{ handle | 1 });
	}

}

void ReShadeIntegration::Initialize() {
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(&ReShadeIntegration::Initialize), &NvrModule);
	ReShadeModule = FindReShade();
	Loaded = ReShadeModule != nullptr;
	if (!Loaded) return;
	char path[MAX_PATH] = {};
	GetModuleFileNameA(ReShadeModule, path, MAX_PATH);
	Logger::Log("UNOFFICIAL ReShade: found %s; ReShadeDepthCompat on while it is loaded.", path);
	TryRegister();
}

void ReShadeIntegration::RenderEffects(IDirect3DSurface9* OutputSurface) {
	if (!Loaded) return;
	FramesSeen++;
	if (!Registered) {
		if (RegisterTries < 5 && FramesSeen % 120 == 0) TryRegister();
		return;
	}
	if (!Runtime) return;
	const auto& Main = TheSettingManager->SettingsMain.Main;
	if (!Main.ReShadeNVRDepth && !Main.ReShadeBeforeHUD) return;

	IDirect3DDevice9* Device = TheRenderManager->device;
	if (!SavedState && FAILED(Device->CreateStateBlock(D3DSBT_ALL, &SavedState))) return;
	SavedState->Capture();
	IDirect3DSurface9* targets[4] = {};
	IDirect3DSurface9* depthStencil = nullptr;
	for (DWORD i = 0; i < 4; ++i)
		if (FAILED(Device->GetRenderTarget(i, &targets[i]))) targets[i] = nullptr;
	Device->GetDepthStencilSurface(&depthStencil);

	if (Main.ReShadeNVRDepth) CopyDepth(Device);
	if (Main.ReShadeBeforeHUD) DrawEffects(Device, OutputSurface);

	for (DWORD i = 0; i < 4; ++i) {
		Device->SetRenderTarget(i, targets[i]);
		if (targets[i]) targets[i]->Release();
	}
	Device->SetDepthStencilSurface(depthStencil);
	if (depthStencil) depthStencil->Release();
	SavedState->Apply();
}
