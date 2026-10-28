#pragma once

#include "../core/NvrDxvkExt.h"

class AvgLumaEffect : public EffectRecord
{
public:
	AvgLumaEffect() : EffectRecord("AvgLuma") {};

	struct AvgLumaStruct {
	};
	AvgLumaStruct	Constants;

	struct AvgLumaTextures {
		IDirect3DTexture9* AvgLumaTexture;
		IDirect3DSurface9* AvgLumaSurface;
	};
	AvgLumaTextures	Textures;

	void	UpdateConstants() {};
	void	RegisterTextures();

	bool	RenderCompute(IDirect3DDevice9* Device);
	void	CheckCompute(IDirect3DDevice9* Device, bool computeRan);

private:
	struct ComputePush {
		UINT32		width, height, groupsX, groupsY, phase, samplerIndex, pad0, pad1;
		D3DXVECTOR4	motionBlurData, depthOfFieldData, gameTime, exposureData, cameraData;
		D3DXVECTOR4	histogram;
	};
	bool	ComputeReady();
	void	ComputeRelease();

	ID3D9NvrExtPipeline*	histogramPipeline = nullptr;
	ID3D9NvrExtBuffer*		histogramBins = nullptr;
	UINT					histogramFrames = 0;

	ID3D9NvrExtPipeline*	computePipeline = nullptr;
	ID3D9NvrExtBuffer*		computePartials = nullptr;
	ID3D9NvrExtBuffer*		computeState[2] = {};
	ID3D9NvrExtSampler*		computeSampler = nullptr;
	UINT					computeWidth = 0, computeHeight = 0, computeGroupsX = 0, computeGroupsY = 0;
	int						computeCurrent = 0;
	bool					computeTried = false;
	bool					computeOk = false;
	UINT					computeFrames = 0;
	UINT					checkFrames = 0;
};
