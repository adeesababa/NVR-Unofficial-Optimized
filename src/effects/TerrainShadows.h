#pragma once

#include <future>
#include <memory>
#include <string>
#include <vector>

class TerrainShadowMap {
public:
	struct SettingsStruct {
		bool	Enabled;
		UINT	MaxResolution;
		float	Bias;
		float	Softness;
		float	Strength;
		float	Darkness;
		float	Contrast;
		int		DebugView;
		bool	UseHeightMapFile;
	};
	SettingsStruct	Settings = {};

	struct ConstantsStruct {
		D3DXVECTOR4	Data;
		D3DXVECTOR4	Range;
		D3DXVECTOR4	Params;
	};
	ConstantsStruct	Constants = {};

	IDirect3DTexture9*	Texture = nullptr;
	IDirect3DTexture9*	GroundTexture = nullptr;

	void	UpdateSettings();
	void	RegisterConstants();
	void	RegisterTextures();
	void	Update(IDirect3DDevice9* Device, const D3DXVECTOR3& SunDir);
	bool	Active() const { return Constants.Data.w > 0.0f; }

private:
	struct HeightMap {
		std::vector<float>	Heights;
		std::vector<float>	Peaks;
		UINT	Width = 0, Height = 0;
		float	West = 0.0f, North = 0.0f;
		float	Texel = 128.0f;
		void*	WorldSpace = nullptr;
		std::string	Message;
	};
	struct SweepResult {
		size_t	Count = 0;
		float	Low = 0.0f, Range = 1.0f, Blur = 0.0f;
		UINT	Generation = 0;
		std::string	Error;
		float	Shadowed = 0.0f;
		D3DXVECTOR3	SunDir = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
	};
	struct LoadRequest {
		void*		WorldSpace = nullptr;
		std::string	Name;
		std::string	DefiningFile;
		UInt32		ObjectID = 0;
		std::vector<std::string>	Plugins;
		UINT		MaxResolution = 2048;
		bool		UseHeightMapFile = true;
	};

	static HeightMap	Load(LoadRequest Request);
	static bool			LoadHeightMapFile(const LoadRequest& Request, HeightMap& Out);
	static SweepResult	Sweep(std::shared_ptr<const HeightMap> Map, std::vector<UINT32>* Packed, D3DXVECTOR3 SunDir, float Softness, float Bias, UINT Generation);
	void	ReleaseTexture();

	void*				RequestedWorldSpace = nullptr;
	UINT				RequestedResolution = 0;
	bool				RequestedFile = true;
	std::future<HeightMap>	LoadJob;
	bool				Loading = false;
	std::shared_ptr<const HeightMap>	Map;
	UINT				Generation = 0;

	std::future<SweepResult>	SweepJob;
	bool				Sweeping = false;
	std::vector<UINT32>	SweepBuffer;
	ULONGLONG			BusySince = 0;
	bool				StallLogged = false;
	UINT				LockFailures = 0;
	UINT				SweepsStarted = 0, SweepsUploaded = 0;
	D3DXVECTOR3			SweptSunDir = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
	UINT				SweptGeneration = ~0u;
	float				SweptBias = -1.0f, SweptSoftness = -1.0f;
	ULONGLONG			SettingsChangedAt = 0;
	bool				Uploaded = false;
	UINT				UploadedGeneration = ~0u;
	SweepResult			PendingUpload;
	UINT				PendingRow = 0;
	bool				Uploading = false;
	void*				UploadedWorldSpace = nullptr;
	UINT				TextureWidth = 0, TextureHeight = 0;
	bool				GroundTwoChannels = false;
	float				LastFade = 1.0f;
};
