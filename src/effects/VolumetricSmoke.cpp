#include "VolumetricSmoke.h"

void VolumetricSmokeEffect::RegisterTextures() {
	using namespace VolumetricSmoke;
	FullState.Forget();
	HalfState.Forget();
	LowState.Forget();
	HazeState.Forget();
	TheTextureManager->InitTexture("TESR_SmokeBuffer", &Texture, &Surface, TheRenderManager->width, TheRenderManager->height, D3DFMT_A16B16G16R16F);
	FullTexture = Texture;
	FullSurface = Surface;
	TheTextureManager->InitTexture("TESR_SmokeBufferLow", &LowTexture, &LowSurface, (TheRenderManager->width + 7) / 8,
		(TheRenderManager->height + 7) / 8, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("TESR_SmokeHaze", &HazeTexture, &HazeSurface, (TheRenderManager->width + 3) / 4,
		(TheRenderManager->height + 3) / 4, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("TESR_SmokeCut", &CutTexture, &CutSurface, (TheRenderManager->width + 3) / 4,
		(TheRenderManager->height + 3) / 4, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("TESR_SmokeEnv", &EnvTexture, &EnvSurface, 1, 1, D3DFMT_A16B16G16R16F);
	const UINT w = (TheRenderManager->width + 1) / 2, h = (TheRenderManager->height + 1) / 2;
	if (SUCCEEDED(TheRenderManager->device->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &HalfTexture, NULL)))
		HalfTexture->GetSurfaceLevel(0, &HalfSurface);
	else {
		HalfTexture = nullptr;
		Logger::Log("VolumetricSmoke: no half-resolution buffer (%ux%u); full resolution only", w, h);
	}
}

namespace {
	const D3DXVECTOR4* ExtRegistered(const char* name) {
		auto found = TheShaderManager->ConstantsTable.find(name);
		return found == TheShaderManager->ConstantsTable.end() ? nullptr : found->second;
	}
	struct ExtPush { UInt32 pointIndex, linearIndex, wrapIndex, pad; };

	const NVR_EXT_BINDING_DESC HazeBindings[] = {
		{ 1, 0, NVR_EXT_BIND_SAMPLED_IMAGE, 0, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 1, 1, NVR_EXT_BIND_SAMPLED_IMAGE, 1, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 1, 2, NVR_EXT_BIND_SAMPLED_IMAGE, 2, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 1, 3, NVR_EXT_BIND_SAMPLED_IMAGE, 3, NVR_EXT_VIEW_3D, FALSE, 0 },
		{ 2, 0, NVR_EXT_BIND_UNIFORM_BUFFER, 0, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 0, 0, NVR_EXT_BIND_SAMPLER, 0, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 0, 0, NVR_EXT_BIND_SAMPLER, 1, NVR_EXT_VIEW_2D, FALSE, 4 },
		{ 0, 0, NVR_EXT_BIND_SAMPLER, 2, NVR_EXT_VIEW_2D, FALSE, 8 },
	};
	const NVR_EXT_BINDING_DESC EnvBindings[] = {
		{ 1, 0, NVR_EXT_BIND_SAMPLED_IMAGE, 0, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 2, 0, NVR_EXT_BIND_UNIFORM_BUFFER, 0, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 0, 0, NVR_EXT_BIND_SAMPLER, 0, NVR_EXT_VIEW_2D, FALSE, 0 },
	};
	const NVR_EXT_BINDING_DESC CompositeBindings[] = {
		{ 1, 0, NVR_EXT_BIND_SAMPLED_IMAGE, 0, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 1, 1, NVR_EXT_BIND_SAMPLED_IMAGE, 1, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 1, 2, NVR_EXT_BIND_SAMPLED_IMAGE, 2, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 1, 3, NVR_EXT_BIND_SAMPLED_IMAGE, 3, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 1, 4, NVR_EXT_BIND_SAMPLED_IMAGE, 4, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 1, 5, NVR_EXT_BIND_SAMPLED_IMAGE, 5, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 1, 6, NVR_EXT_BIND_SAMPLED_IMAGE, 6, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 1, 7, NVR_EXT_BIND_SAMPLED_IMAGE, 7, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 2, 0, NVR_EXT_BIND_UNIFORM_BUFFER, 0, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 0, 0, NVR_EXT_BIND_SAMPLER, 0, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 0, 0, NVR_EXT_BIND_SAMPLER, 1, NVR_EXT_VIEW_2D, FALSE, 4 },
	};

	NVR_EXT_RESOURCE_BINDING Bound(NVR_EXT_BIND_TYPE type, UINT slot, IUnknown* resource) {
		NVR_EXT_RESOURCE_BINDING binding = {};
		binding.Type = type;
		binding.Slot = slot;
		binding.pResource = resource;
		return binding;
	}
	IDirect3DBaseTexture9* DepthTextureOf() {
		return TheShaderManager->Effects.CombineDepth ? TheShaderManager->Effects.CombineDepth->Textures.CombinedDepthTexture : nullptr;
	}
}

bool VolumetricSmokeEffect::FillExtConstants(const D3DXVECTOR4& info, const D3DXVECTOR4& flash) {
	using namespace VolumetricSmoke;
	const D3DXVECTOR4* invView = ExtRegistered("TESR_InvViewTransform");
	const D3DXVECTOR4* invProjection = ExtRegistered("TESR_InvProjectionTransform");
	const D3DXVECTOR4* projection = ExtRegistered("TESR_ProjectionTransform");
	const D3DXVECTOR4* reciprocal = ExtRegistered("TESR_ReciprocalResolution");
	const D3DXVECTOR4* sunDirection = ExtRegistered("TESR_SunDirection");
	const D3DXVECTOR4* sunColor = ExtRegistered("TESR_SunColor");
	const D3DXVECTOR4* sunAmbient = ExtRegistered("TESR_SunAmbient");
	const D3DXVECTOR4* cameraData = ExtRegistered("TESR_CameraData");
	const D3DXVECTOR4* cameraPosition = ExtRegistered("TESR_CameraPosition");
	if (!invView || !invProjection || !projection || !reciprocal || !sunDirection || !sunColor || !sunAmbient || !cameraData || !cameraPosition)
		return false;
	ExtConstantsStruct& c = extConstants;
	memcpy(&c.InvView, invView, sizeof(D3DXMATRIX));
	memcpy(&c.InvProjection, invProjection, sizeof(D3DXMATRIX));
	memcpy(&c.Projection, projection, sizeof(D3DXMATRIX));
	c.ReciprocalResolution = *reciprocal;
	c.SunDirection = *sunDirection;
	c.SunColor = *sunColor;
	c.SunAmbient = *sunAmbient;
	c.CameraData = *cameraData;
	c.CameraPosition = *cameraPosition;
	c.Info = info;
	c.Params = Params;
	c.Flash = flash;
	c.FlashColor = FlashColor;
	c.Look = Look;
	c.HazeInfo = D3DXVECTOR4((float)HazeCount, (float)BulletCount, BulletLook.x, BulletLook.y);
	c.BulletMove = D3DXVECTOR4(BulletMoving, 0.0f, 0.0f, 0.0f);
	memcpy(c.HazeShape, HazeShape, sizeof(c.HazeShape));
	memcpy(c.HazeLook, HazeLook, sizeof(c.HazeLook));
	memcpy(c.BulletFrom, BulletFrom, sizeof(c.BulletFrom));
	memcpy(c.BulletWay, BulletWay, sizeof(c.BulletWay));
	return true;
}

bool VolumetricSmokeEffect::ExtHaze(const D3DXVECTOR4& look) {
	using namespace VolumetricSmoke;
	if (!extHazePass.Tried) {
		extHazePass.Tried = true;
		extHazePass.Pipeline = DxvkCompute::CreateGraphicsPipeline("VolumetricSmokeHaze", HazeBindings, _countof(HazeBindings), sizeof(ExtPush), true, 2, NVR_EXT_BLEND_NONE);
	}
	IDirect3DBaseTexture9* depth = DepthTextureOf();
	IDirect3DBaseTexture9* noise = ExtFileTexture("TESR_NoiseVolume");
	ID3D9NvrExtSampler* point = DxvkCompute::PointClamp();
	ID3D9NvrExtSampler* linear = DxvkCompute::LinearClamp();
	ID3D9NvrExtSampler* wrap = DxvkCompute::MipSampler(false, NVR_EXT_ADDRESS_WRAP);
	if (!extHazePass.Pipeline || !point || !linear || !wrap || !depth || !noise || !Texture || !LowTexture || !HazeSurface || !CutSurface)
		return false;
	extConstants.Look = look;
	ID3D9NvrExtBuffer* uniforms = ExtUniforms(extUniforms, &extConstants, sizeof(extConstants));
	if (!uniforms) return false;
	NVR_EXT_RESOURCE_BINDING resources[] = {
		Bound(NVR_EXT_BIND_SAMPLED_IMAGE, 0, depth), Bound(NVR_EXT_BIND_SAMPLED_IMAGE, 1, Texture),
		Bound(NVR_EXT_BIND_SAMPLED_IMAGE, 2, LowTexture), Bound(NVR_EXT_BIND_SAMPLED_IMAGE, 3, noise),
		Bound(NVR_EXT_BIND_UNIFORM_BUFFER, 0, uniforms),
		Bound(NVR_EXT_BIND_SAMPLER, 0, point), Bound(NVR_EXT_BIND_SAMPLER, 1, linear), Bound(NVR_EXT_BIND_SAMPLER, 2, wrap) };
	ExtPush push = {};
	IDirect3DSurface9* targets[2] = { HazeSurface, CutSurface };
	return ExtDrawTo(extHazePass.Pipeline, targets, 2, resources, _countof(resources), &push, sizeof(push));
}

bool VolumetricSmokeEffect::ExtEnv(const D3DXVECTOR4& envInfo) {
	using namespace VolumetricSmoke;
	if (!extEnvPass.Tried) {
		extEnvPass.Tried = true;
		extEnvPass.Pipeline = DxvkCompute::CreateGraphicsPipeline("VolumetricSmokeEnv", EnvBindings, _countof(EnvBindings), sizeof(ExtPush), true, 1, NVR_EXT_BLEND_ALPHA);
	}
	ID3D9NvrExtSampler* point = DxvkCompute::PointClamp();
	if (!extEnvPass.Pipeline || !point || !EnvSurface || !TheTextureManager->RenderedTexture) return false;
	extConstants.EnvInfo = envInfo;
	ID3D9NvrExtBuffer* uniforms = ExtUniforms(extUniforms, &extConstants, sizeof(extConstants));
	if (!uniforms) return false;
	NVR_EXT_RESOURCE_BINDING resources[] = {
		Bound(NVR_EXT_BIND_SAMPLED_IMAGE, 0, TheTextureManager->RenderedTexture),
		Bound(NVR_EXT_BIND_UNIFORM_BUFFER, 0, uniforms), Bound(NVR_EXT_BIND_SAMPLER, 0, point) };
	ExtPush push = {};
	IDirect3DSurface9* target = EnvSurface;
	return ExtDrawTo(extEnvPass.Pipeline, &target, 1, resources, _countof(resources), &push, sizeof(push));
}

bool VolumetricSmokeEffect::ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	using namespace VolumetricSmoke;
	if (techniqueIndex != 0 || ClearRenderTarget || !extReady) return false;
	if (!extCompositePass.Tried) {
		extCompositePass.Tried = true;
		extCompositePass.Pipeline = DxvkCompute::CreateGraphicsPipeline("VolumetricSmokeComposite", CompositeBindings, _countof(CompositeBindings), sizeof(ExtPush), true);
	}
	IDirect3DBaseTexture9* textures[8] = { TheTextureManager->RenderedTexture, DepthTextureOf(), Texture, TheTextureManager->DepthTextureViewModel,
		LowTexture, HazeTexture, EnvTexture, CutTexture };
	for (IDirect3DBaseTexture9* texture : textures) if (!texture) return false;
	ID3D9NvrExtSampler* point = DxvkCompute::PointClamp();
	ID3D9NvrExtSampler* linear = DxvkCompute::LinearClamp();
	if (!extCompositePass.Pipeline || !point || !linear) return false;
	extConstants.Look = Look;
	ID3D9NvrExtBuffer* uniforms = ExtUniforms(extUniforms, &extConstants, sizeof(extConstants));
	if (!uniforms) return false;
	NVR_EXT_RESOURCE_BINDING resources[] = {
		Bound(NVR_EXT_BIND_SAMPLED_IMAGE, 0, textures[0]), Bound(NVR_EXT_BIND_SAMPLED_IMAGE, 1, textures[1]),
		Bound(NVR_EXT_BIND_SAMPLED_IMAGE, 2, textures[2]), Bound(NVR_EXT_BIND_SAMPLED_IMAGE, 3, textures[3]),
		Bound(NVR_EXT_BIND_SAMPLED_IMAGE, 4, textures[4]), Bound(NVR_EXT_BIND_SAMPLED_IMAGE, 5, textures[5]),
		Bound(NVR_EXT_BIND_SAMPLED_IMAGE, 6, textures[6]), Bound(NVR_EXT_BIND_SAMPLED_IMAGE, 7, textures[7]),
		Bound(NVR_EXT_BIND_UNIFORM_BUFFER, 0, uniforms),
		Bound(NVR_EXT_BIND_SAMPLER, 0, point), Bound(NVR_EXT_BIND_SAMPLER, 1, linear) };
	ExtPush push = {};
	if (writesWholeTarget)
		return ExtFullScreen(Device, RenderTarget, RenderedSurface, SourceBuffer, extCompositePass.Pipeline, resources, _countof(resources), &push, sizeof(push), true);

	FrameChain& chain = TheShaderManager->Chain;
	if (!chain.Owns(RenderTarget, RenderedSurface) || Scissor.right <= Scissor.left || Scissor.bottom <= Scissor.top) return false;
	if (SourceBuffer && usesSourceBuffer && SourceBuffer != RenderTarget)
		Device->StretchRect(TheTextureManager->RenderedSurface, NULL, SourceBuffer, NULL, D3DTEXF_LINEAR);
	IDirect3DSurface9* output = chain.Output();
	Device->StretchRect(TheTextureManager->RenderedSurface, NULL, output, NULL, D3DTEXF_NONE);
	RECT area = Scissor;
	if (!ExtDrawTo(extCompositePass.Pipeline, &output, 1, resources, _countof(resources), &push, sizeof(push), &area)) return false;
	chain.Commit();
	if (++ExtFrames == 1) Logger::Log("UNOFFICIAL NVR DXVK: %s runs as a Vulkan pass.", Name);
	return true;
}
