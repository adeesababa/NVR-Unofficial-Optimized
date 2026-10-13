#pragma once

#include "BloodFlowBoxes.h"

// UNOFFICIAL, optional (off by default; Shaders.BloodFlow): faked liquid blood.
//  - Splats: the game's own blood splats (its simple decals, read from its decal lists) grow streams that run down the
//    surface the way gravity pulls along it (straight down a wall, slantwise across a slope), slowing as they go. A
//    stream that reaches something flat (a ledge, the floor) gathers in a small puddle there; one that reaches an edge
//    with nothing under it hangs in a swelling drop that lets go, falls and leaves a stain where it lands. Fresh blood is
//    wet and shines a little; thin trails dry first, darker and browner.
//  - Pools: a pool of blood spreads out under a body after death; on a slope it slides downhill and trickles run off
//    its low side, gathering where the ground levels out. Glossy at first, drying to a dark stain with a ring.
// Nothing new is placed in the game: every splat is the game's and goes when the game removes it; pools follow the
// bodies the game keeps loaded. Where each stream goes is worked out on the GPU from the depth buffer (BloodFlow.fx.hlsl,
// probe pass, into a small path texture that keeps each path once it has been seen whole); each item is then drawn as a
// box around it, the pixel shader finding the surface in it from the depth buffer and painting the blood onto it,
// blended straight into the current image. The cost follows how much blood is near; none at all when there is none.
class BloodFlowEffect : public EffectRecord
{
public:
	BloodFlowEffect() : EffectRecord("BloodFlow") {};

	struct BloodFlowSettingsStruct {
		bool	Splats;			// drips and drying on the game's wall splats
		bool	Pools;			// pools under bodies
		float	DripLength;		// longest drip, in game units
		float	DripSpeed;		// how fast drips run (1 = normal)
		float	DryTime;		// seconds for a splat or drip to dry
		float	PoolSize;		// pool size, as a share of the body's size
		float	PoolSpreadTime;	// seconds a pool takes to spread (most of the way)
		float	PoolDryTime;	// seconds for a pool to dry
		float	Gloss;			// how glossy fresh blood is
		float	FreshColor[3];	// sRGB
		float	DryColor[3];	// sRGB
		float	MaxFall;		// how far drops falling off an edge are followed, in game units
		float	MaxDistance;	// game units
		int		MaxSplats;
		int		MaxPools;
		int		DebugView;		// 1: boxes in green, blood in red
	};
	BloodFlowSettingsStruct	Settings;

	struct BloodFlowStruct {
		D3DXVECTOR4	Fresh;		// rgb: fresh blood colour (linear); w: gloss
		D3DXVECTOR4	Dry;		// rgb: dried blood colour (linear); w: splat dry time (s)
		D3DXVECTOR4	Info;		// x: outdoors, y: debug view, z: pool dry time (s), w: drip speed
		D3DXVECTOR4	Pool;		// x: pool spread time (s); y: longest drip (game units); z: longest fall; w: gravity (units/s^2)
		D3DXVECTOR4	Probe;		// x: 1 / path texture rows; y: finest detail a path is kept at (units per pixel)
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
	double				Clock = 0.0;	// seconds the effect has run (pauses with the effect)

	// The path texture (BloodFlowBoxes.h), twice: the probe pass reads last frame's and writes the other.
	IDirect3DTexture9*	PathTexture[2] = {};
	IDirect3DSurface9*	PathSurface[2] = {};
	IDirect3DTexture9*	PathRead = nullptr;	// what TESR_BloodFlowPaths binds
	int					PathIndex = 0;		// the one holding the latest paths
	// The box round each item's real paths (2 x ItemMax: lowest and highest corner), from the latest paths.
	IDirect3DTexture9*	BoundsTexture = nullptr;
	IDirect3DSurface9*	BoundsSurface = nullptr;

	// Each item keeps its slot (rows in the path texture) while it is gathered every frame.
	struct SlotState { UInt64 Key = 0; bool Used = false; UInt32 SeenFrame = 0; int WrittenPass = -10; };
	SlotState			Slots[ItemMax];
	UInt32				Frame = 0;			// UpdateConstants calls
	int					ProbePasses = 0;	// probe passes drawn

private:
	void	GatherSplats();
	void	GatherPools();
	int		SlotFor(UInt64 key);
};
