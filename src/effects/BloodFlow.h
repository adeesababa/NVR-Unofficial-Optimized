#pragma once

#include "BloodFlowBoxes.h"

class BloodFlowEffect : public EffectRecord
{
public:
	BloodFlowEffect() : EffectRecord("BloodFlow") {};

	struct BloodFlowSettingsStruct {
		bool	Splats;
		bool	Pools;
		float	DripLength;
		float	FlowSpeed;
		float	Viscosity;
		float	BloodAmount;
		float	DryTime;
		float	PoolSize;
		float	PoolSpreadTime;
		float	PoolDryTime;
		float	Gloss;
		float	FreshColor[3];
		float	DryColor[3];
		float	MaxFall;
		float	MaxDistance;
		int		MaxSplats;
		int		MaxPools;
		int		DebugView;
		bool	SplatterAtlas;
		bool	MatchGameColor;
		float	MatchShade;
		float	MatchBrightness;
		float	StreamWidth;
		float	Beads;
		float	BeadSize;
		float	Splat;
		float	SplatReach;
		float	SplatSpray;
		float	SplatDrips;
		float	Merge;
		float	GibPools;
		int		MergeCount;
	};
	BloodFlowSettingsStruct	Settings;

	struct BloodFlowStruct {
		D3DXVECTOR4	Fresh;
		D3DXVECTOR4	Dry;
		D3DXVECTOR4	Info;
		D3DXVECTOR4	Pool;
		D3DXVECTOR4	Probe;
		D3DXVECTOR4	Flow;
		D3DXVECTOR4	Match;
		D3DXVECTOR4	DryRatio;
		D3DXVECTOR4	Beads;
		D3DXVECTOR4	Splat;
	};
	BloodFlowStruct	Constants;

	void	UpdateConstants();
	void	RegisterConstants();
	void	RegisterTextures();
	void	UpdateSettings();
	bool	ShouldRender();
	void	Render(IDirect3DDevice9* device, IDirect3DSurface9* target, IDirect3DSurface9* rendered,
		UINT technique, bool clear, IDirect3DSurface9* source) override;

	typedef BloodFlowBoxes::Item Item;

	static const int	ItemMax = 96;
	static const int	PathRows = ItemMax * BloodFlowBoxes::Streams;
	Item				Items[ItemMax];
	int					Count = 0;
	VolumetricSmokeQuads::Vertex Vertices[ItemMax * 36];
	VolumetricSmokeQuads::Vertex ProbeVertices[ItemMax * 6];
	VolumetricSmokeQuads::Vertex BoundsVertices[ItemMax * 6];
	IDirect3DVertexDeclaration9* Declaration = nullptr;
	double				Clock = 0.0;

	IDirect3DTexture9*	PathTexture[2] = {};
	IDirect3DSurface9*	PathSurface[2] = {};
	IDirect3DTexture9*	PathRead = nullptr;
	int					PathIndex = 0;
	IDirect3DTexture9*	BoundsTexture = nullptr;
	IDirect3DSurface9*	BoundsSurface = nullptr;

	struct SlotState { UInt64 Key = 0; bool Used = false; UInt32 SeenFrame = 0; int WrittenPass = -10; };
	SlotState			Slots[ItemMax];
	UInt32				Frame = 0;
	int					ProbePasses = 0;

private:
	void	GatherSplats();
	void	GatherPools();
	int		SlotFor(UInt64 key);
};
