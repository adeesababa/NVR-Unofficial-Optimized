#pragma once

// UNOFFICIAL 2026-10-14: the physical sky's lookup table (Effects\SkyView.fx.hlsl). Filled once a frame while
// [Shaders.Sky.Physical] is on and outdoors (ShaderManager::RenderEffectsPreTonemapping); the sky shader (SKY.pso) reads it
// the next frame. Its settings and constants (TESR_PhysSky, TESR_PhysSkySun) belong to the sky shaders (Sky.cpp).
class SkyViewEffect : public EffectRecord
{
public:
	SkyViewEffect() : EffectRecord("SkyView") {};

	struct TexturesStruct {
		IDirect3DTexture9* TableTexture = nullptr;
		IDirect3DSurface9* TableSurface = nullptr;
	};
	TexturesStruct	Textures;

	void	UpdateConstants() {};
	void	RegisterTextures();
	bool	ShouldRender();
	void	RenderTable(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget);
};
