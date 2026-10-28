#pragma once


#include "BloodFlowBoxes.h"
#include "../core/NvrDxvkExt.h"

class BloodFlowEffect;

class BloodFluid {
public:
	struct SettingsStruct {
		bool	Enabled = false;
		bool	Splats = true;
		bool	Pools = true;
		bool	ReplaceStreams = true;
		float	Amount = 1.0f;
		int		MaxParticles = 16384;
		float	SettleTime = 12.0f;
		float	BleedTime = 8.0f;
		float	Viscosity = 8.0f;
		float	Cling = 300.0f;
		float	Friction = 40.0f;
		float	Lifetime = 900.0f;
		float	StainSize = 3.2f;
		float	Threshold = 0.3f;
		float	Bump = 0.6f;
		float	Drops = 0.3f;
		float	DropSize = 0.55f;
		bool	OldThrow = false;
		bool	OldColour = false;
		float	TextureRelief = 1.0f;
		int		DebugView = 0;
		float	TestThrow = 0.0f;
		bool	TestPour = false;
		int		StatsEvery = 0;
	};
	SettingsStruct Settings;

	void	ReadSettings();
	bool	On() const;
	bool	Active() const;
	bool	ReplacesStreams() const { return On() && Settings.ReplaceStreams; }
	bool	Offer(UInt64 Key, const BloodFlowBoxes::Item& Item);
	bool	Render(IDirect3DDevice9* Device, IDirect3DSurface9* Image, IDirect3DSurface9* Source, BloodFlowEffect* Owner);

private:
	struct Emitter {
		float	Origin[4];
		float	Dir[4];
		float	Shape[4];
		UInt32	Data[4];
	};
	static const int		EmittersMax = 64;
	Emitter					Pending[EmittersMax];
	int						PendingCount = 0;
	UInt32					PendingParticles = 0;

	bool	Ready(IDirect3DDevice9* Device);
	void	Release();
	void	Clear(const char* Why);
	void	Queue(const D3DXVECTOR3& Origin, float GroundZ, const D3DXVECTOR3& Dir, float Speed, float Spread, float Cone, float BackOff,
				float Window, int Count, UInt32 Seed, UInt32 Colour, UInt32 Kind);
	void	Stats(UInt32 Frame);

	ID3D9NvrExtPipeline*	SimPipeline = nullptr;
	ID3D9NvrExtPipeline*	DrawPipeline = nullptr;
	ID3D9NvrExtBuffer*		Particles = nullptr;
	ID3D9NvrExtBuffer*		Grid = nullptr;
	ID3D9NvrExtBuffer*		Emitters = nullptr;
	ID3D9NvrExtBuffer*		Draws = nullptr;
	ID3D9NvrExtBuffer*		TileCounts = nullptr;
	ID3D9NvrExtBuffer*		TileLists = nullptr;
	ID3D9NvrExtBuffer*		FrameBuffer = nullptr;
	bool					Failed = false;
	bool					PipelinesTried = false;
	UInt32					Capacity = 0;
	UInt32					AllocCapacity = 0;
	bool					LastExterior = true;
	UInt32					Width = 0, Height = 0, TilesX = 0, TilesY = 0;

	UInt32					Head = 0;
	UInt32					Used = 0;
	bool					HaveAnchor = false;
	double					Anchor[3] = {};
	float					PendingShift[3] = {};
	bool					ShiftPending = false;
	double					LastTime = -1.0;
	double					StepDebt = 0.0;
	double					AliveUntil = -1.0;
	double					LastThrow = -1e9;
	bool					PourDone = false;
	D3DXVECTOR3				LastEye = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
	UInt32					Frames = 0;
	float					LastFarZ = 0.0f;
	int						LoggedEmits = 0;
	UInt64					Emitted = 0;
};

BloodFluid& TheBloodFluid();
