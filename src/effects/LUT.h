#pragma once

class LUTEffect : public EffectRecord
{
public:
	LUTEffect() : EffectRecord("LUT") {};

	struct LUTStruct {
		D3DXVECTOR4 Data;
		D3DXVECTOR4 Blend;  // x=dayNightLerp (0=night, 1=day), y=isInterior (0 or 1)
	};

	struct LUTSettingsStruct {
		float Strength;
		bool  PreTonemapping;
		bool  HDRCompat;
	};

	LUTStruct          Constants;
	LUTSettingsStruct  Settings;

	IDirect3DTexture9* DayTexture      = nullptr;
	IDirect3DTexture9* NightTexture    = nullptr;
	IDirect3DTexture9* InteriorTexture = nullptr;

	std::vector<std::string> LUTFiles;
	int DayIdx      = 0;
	int NightIdx    = 0;
	int InteriorIdx = 0;

	void RegisterConstants();
	void RegisterTextures();
	void UpdateSettings();
	void UpdateConstants();
	bool ShouldRender() override;

	void ScanLUTFolder();

	// Assigns an already-loaded texture to a slot (member pointer, sampler binding,
	// loading is TextureManager::GetFileTexture()'s job, persistence is SaveLUTSetting()'s.
	void AssignLUTSlot(int slot, IDirect3DBaseTexture9* texture, const char* filename); // slot: 0=day, 1=night, 2=interior

	// Persists the chosen filename for a slot back to settings.
	void SaveLUTSetting(int slot, const char* filename); // slot: 0=day, 1=night, 2=interior

	// Convenience orchestrator for a user-driven LUT pick: loads the file via
	// TheTextureManager->GetFileTexture(), assigns it to the slot, and saves the choice.
	void LoadLUT(int slot, const char* filename); // slot: 0=day, 1=night, 2=interior

	static const char* LUTFolder;

private:
	float CellCount[3] = {};

	bool DayNeutral      = true;
	bool NightNeutral    = true;
	bool InteriorNeutral = true;
};
