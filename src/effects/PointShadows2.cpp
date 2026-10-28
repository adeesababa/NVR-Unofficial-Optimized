#include <string>
#include "PointShadows2.h"
#include "../core/DxvkCompute.h"

struct PointShadows2Uniforms {
	D3DXVECTOR4	cameraPosition;
	D3DXVECTOR4	cameraData;
	D3DXMATRIX	viewTransform;
	D3DXMATRIX	projectionTransform;
	D3DXVECTOR4	shadowFade;
	D3DXVECTOR4	shadowLightPosition[ShadowCubeMapsScreen];
	D3DXVECTOR4	lightColor[TrackedLightsMax + ShadowCubeMapsScreen];
};
static_assert(sizeof(PointShadows2Uniforms) == 752, "PointShadows2Uniforms must match the std140 block");

struct PointShadows2Push { UInt32 samplerIndex; UInt32 pointSamplerIndex; };

namespace {
	const D3DXVECTOR4* Registered(const char* name) {
		auto found = TheShaderManager->ConstantsTable.find(name);
		return found == TheShaderManager->ConstantsTable.end() ? nullptr : found->second;
	}

	IDirect3DBaseTexture9* EngineTexture(const std::string& name) {
		std::string n = name;
		return TheTextureManager->GetTextureByName(n);
	}

	const NVR_EXT_BINDING_DESC PointShadows2Bindings[] = {
		{ 1, 0, NVR_EXT_BIND_SAMPLED_IMAGE, 0, NVR_EXT_VIEW_2D,   FALSE, 0 },
		{ 1, 1, NVR_EXT_BIND_SAMPLED_IMAGE, 1, NVR_EXT_VIEW_2D,   FALSE, 0 },
		{ 1, 2, NVR_EXT_BIND_SAMPLED_IMAGE, 2, NVR_EXT_VIEW_2D,   FALSE, 0 },
		{ 1, 3, NVR_EXT_BIND_SAMPLED_IMAGE, 3, NVR_EXT_VIEW_CUBE, FALSE, 0 },
		{ 1, 4, NVR_EXT_BIND_SAMPLED_IMAGE, 4, NVR_EXT_VIEW_CUBE, FALSE, 0 },
		{ 1, 5, NVR_EXT_BIND_SAMPLED_IMAGE, 5, NVR_EXT_VIEW_CUBE, FALSE, 0 },
		{ 1, 6, NVR_EXT_BIND_SAMPLED_IMAGE, 6, NVR_EXT_VIEW_CUBE, FALSE, 0 },
		{ 1, 7, NVR_EXT_BIND_SAMPLED_IMAGE, 7, NVR_EXT_VIEW_CUBE, FALSE, 0 },
		{ 2, 0, NVR_EXT_BIND_UNIFORM_BUFFER, 0, NVR_EXT_VIEW_2D,  FALSE, 0 },
		{ 0, 0, NVR_EXT_BIND_SAMPLER,        0, NVR_EXT_VIEW_2D,  FALSE, 0 },
		{ 0, 0, NVR_EXT_BIND_SAMPLER,        1, NVR_EXT_VIEW_2D,  FALSE, 4 },
	};

	bool EnsureCopy(IDirect3DDevice9* Device, IDirect3DSurface9* Target, IDirect3DTexture9*& Texture, IDirect3DSurface9*& Surface) {
		D3DSURFACE_DESC desc = {};
		if (FAILED(Target->GetDesc(&desc))) return false;
		if (Texture && Surface) {
			D3DSURFACE_DESC copy = {};
			if (SUCCEEDED(Surface->GetDesc(&copy)) && copy.Width == desc.Width && copy.Height == desc.Height && copy.Format == desc.Format) return true;
		}
		if (Surface) { Surface->Release(); Surface = nullptr; }
		if (Texture) { Texture->Release(); Texture = nullptr; }
		if (FAILED(Device->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, desc.Format, D3DPOOL_DEFAULT, &Texture, NULL))) return false;
		return SUCCEEDED(Texture->GetSurfaceLevel(0, &Surface));
	}
}

bool PointShadows2Effect::ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	ShadowsExteriorEffect* shadows = TheShaderManager->Effects.ShadowsExteriors;
	if (techniqueIndex != 0 || ClearRenderTarget || !shadows || RenderTarget != shadows->Textures.ShadowPassSurface) return false;

	IDirect3DBaseTexture9* depth = EngineTexture("TESR_DepthBuffer");
	IDirect3DBaseTexture9* normals = EngineTexture("TESR_NormalsBuffer");
	IDirect3DBaseTexture9* cube6 = EngineTexture("TESR_ShadowCubeMapBuffer6");
	IDirect3DBaseTexture9* cube7 = EngineTexture("TESR_ShadowCubeMapBuffer7");
	IDirect3DBaseTexture9* cube8 = EngineTexture("TESR_ShadowCubeMapBuffer8");
	IDirect3DBaseTexture9* cube9 = EngineTexture("TESR_ShadowCubeMapBuffer9");
	IDirect3DBaseTexture9* cube10 = EngineTexture("TESR_ShadowCubeMapBuffer10");
	if (!depth || !normals || !cube6 || !cube7 || !cube8 || !cube9 || !cube10) return false;

	const D3DXVECTOR4* cameraPosition = Registered("TESR_CameraPosition");
	const D3DXVECTOR4* cameraData = Registered("TESR_CameraData");
	const D3DXVECTOR4* shadowFade = Registered("TESR_ShadowFade");
	const D3DXVECTOR4* shadowLightPosition = Registered("TESR_ShadowLightPosition");
	const D3DXVECTOR4* lightColor = Registered("TESR_LightColor");
	const D3DXVECTOR4* viewTransform = Registered("TESR_ViewTransform");
	const D3DXVECTOR4* projectionTransform = Registered("TESR_ProjectionTransform");
	if (!cameraPosition || !cameraData || !shadowFade || !shadowLightPosition || !lightColor || !viewTransform || !projectionTransform) return false;

	PointShadows2Uniforms uniforms = {};
	uniforms.cameraPosition = *cameraPosition;
	uniforms.cameraData = *cameraData;
	memcpy(&uniforms.viewTransform, viewTransform, sizeof(D3DXMATRIX));
	memcpy(&uniforms.projectionTransform, projectionTransform, sizeof(D3DXMATRIX));
	uniforms.shadowFade = *shadowFade;
	memcpy(uniforms.shadowLightPosition, shadowLightPosition, sizeof(uniforms.shadowLightPosition));
	memcpy(uniforms.lightColor, lightColor, sizeof(uniforms.lightColor));

	if (!extPass.Tried) {
		extPass.Tried = true;
		extPass.Pipeline = DxvkCompute::CreateGraphicsPipeline("PointShadows2", PointShadows2Bindings, _countof(PointShadows2Bindings), sizeof(PointShadows2Push), true);
	}
	ID3D9NvrExtSampler* linear = DxvkCompute::LinearClamp();
	ID3D9NvrExtSampler* point = DxvkCompute::PointClamp();
	if (!extPass.Pipeline || !linear || !point) return false;
	ID3D9NvrExtBuffer* constants = ExtUniforms(extUniforms, &uniforms, sizeof(uniforms));
	if (!constants) return false;

	if (!EnsureCopy(Device, RenderTarget, copyTexture, copySurface)) return false;
	if (FAILED(Device->StretchRect(RenderTarget, NULL, copySurface, NULL, D3DTEXF_NONE))) return false;

	IDirect3DBaseTexture9* textures[8] = { depth, normals, copyTexture, cube6, cube7, cube8, cube9, cube10 };
	NVR_EXT_RESOURCE_BINDING resources[_countof(PointShadows2Bindings)] = {};
	for (UINT i = 0; i < 8; i++) {
		resources[i].Slot = i; resources[i].Type = NVR_EXT_BIND_SAMPLED_IMAGE; resources[i].pResource = textures[i];
	}
	resources[8].Slot = 0; resources[8].Type = NVR_EXT_BIND_UNIFORM_BUFFER; resources[8].pResource = constants;
	resources[9].Slot = 0; resources[9].Type = NVR_EXT_BIND_SAMPLER; resources[9].pResource = linear;
	resources[10].Slot = 1; resources[10].Type = NVR_EXT_BIND_SAMPLER; resources[10].pResource = point;
	PointShadows2Push push = {};
	return ExtFullScreen(Device, RenderTarget, RenderedSurface, SourceBuffer, extPass.Pipeline, resources, _countof(resources), &push, sizeof(push), true);
}
