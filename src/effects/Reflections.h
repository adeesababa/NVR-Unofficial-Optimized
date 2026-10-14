#pragma once

class ReflectionsEffect : public EffectRecord
{
public:
	ReflectionsEffect() : EffectRecord("Reflections") {};

	struct SettingsStruct {
		float	GroundSheen;
		float	RainWetness;
		float	InteriorFloors;
		float	Roughness;
		float	WetRoughness;
		float	MaxDistance;
		int		Steps;
		float	Thickness;
		float	Temporal;
		float	BlurRadius;
		float	DrawDistance;
		bool	SkyFallback;
		bool	CubeMap;
		float	CubeCapture;
		float	CubeFade;
		int		DebugView;
	};
	SettingsStruct	Settings;

	struct ConstantsStruct {
		D3DXVECTOR4	Data;
		D3DXVECTOR4	Data2;
		D3DXVECTOR4	Data3;
		D3DXVECTOR4	Data4;
		D3DXVECTOR4	Layout;
		D3DXMATRIX	Reproject;
		D3DXVECTOR4	Cube;
		D3DXVECTOR4	Cube2;
	};
	ConstantsStruct	Constants;

	static const UINT CubeSize = 64;
	static const UINT CubeLevels = 7;
	struct CubesStruct {
		IDirect3DCubeTexture9*	Capture[2] = { nullptr, nullptr };
		IDirect3DSurface9*		CaptureFace[2][6] = {};
		IDirect3DCubeTexture9*	Env = nullptr;
		IDirect3DSurface9*		EnvFace[CubeLevels][6] = {};
		IDirect3DBaseTexture9*	CaptureLastSlot = nullptr;
		IDirect3DBaseTexture9*	CaptureNewSlot = nullptr;
		IDirect3DBaseTexture9*	EnvSlot = nullptr;
	};
	CubesStruct	Cubes;

	struct TexturesStruct {
		IDirect3DTexture9* TraceTexture = nullptr;
		IDirect3DSurface9* TraceSurface = nullptr;
		IDirect3DTexture9* BufferTexture = nullptr;
		IDirect3DSurface9* BufferSurface = nullptr;
		IDirect3DTexture9* TempTexture = nullptr;
		IDirect3DSurface9* TempSurface = nullptr;
		IDirect3DTexture9* HistoryTexture = nullptr;
		IDirect3DSurface9* HistorySurface = nullptr;
	};
	TexturesStruct	Textures;

	void	UpdateConstants();
	void	RegisterConstants();
	void	RegisterTextures();
	void	UpdateSettings();
	bool	ShouldRender();
	void	RenderBuffers(IDirect3DDevice9* Device);
	bool	CubeWanted();
	void	RenderCubeOnly(IDirect3DDevice9* Device);
	bool	CubeReady() const;

private:
	D3DXMATRIX	lastView, lastInvView, lastProj;
	UINT		frame = 0;
	UINT		lastRenderedFrame = 0;
	bool		haveHistory = false;
	int			captureIndex = 0;
	bool		cubeValid = false;
	void*		cubeCell = nullptr;
	UINT		cubeFrame = 0;
	D3DXMATRIX	cubeInvView;
	bool		cubeOnly = false;
	void	CubeStep(IDirect3DDevice9* Device);
	void	RenderCube(IDirect3DDevice9* Device, bool reset);
};
