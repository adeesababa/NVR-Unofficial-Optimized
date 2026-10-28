#pragma once


#include <map>
#include <string>
#include <vector>
#include "NvrDxvkExt.h"
#include "EffectRecord.h"

class Sm6Pass {
public:
	static bool On();

	bool Prepare(EffectRecord* Effect, const char* Technique, UINT Pass);
	void SetTexture(const char* Name, IDirect3DBaseTexture9* Texture) { TextureOverrides[Name] = Texture; }
	void SetVector(const char* Name, const D3DXVECTOR4& Value) { VectorOverrides[Name] = Value; }
	bool Draw(IDirect3DDevice9* Device, IDirect3DSurface9* const* Targets, UINT Count, EffectRecord::ExtUv Uv, const RECT* Viewport = nullptr);
	bool Ready() const { return Pipeline != nullptr && !Failed; }
	UINT Targets() const { return TargetCount; }
	bool Plain() const { return VertexShader == "FrameVS" && StateCount == 0; }
	static bool Scissored(IDirect3DDevice9* Device) { DWORD on = FALSE; return Device && SUCCEEDED(Device->GetRenderState(D3DRS_SCISSORTESTENABLE, &on)) && on; }
	UInt32 Frames = 0;

private:
	struct Constant {
		std::string Name;
		UINT Offset = 0, Size = 0;
		std::string Type;
		std::vector<float> Default;
		D3DXHANDLE Handle = NULL;
		D3DXPARAMETER_DESC Desc = {};
	};
	struct Texture {
		std::string Name;
		UINT Binding = 0, Slot = 0;
		NVR_EXT_VIEW_TYPE View = NVR_EXT_VIEW_2D;
		int Stage = -1;
	};
	bool Fail(const char* Why);
	bool LoadLayout(const char* Path);
	void FillConstants();
	IDirect3DBaseTexture9* TextureFor(IDirect3DDevice9* Device, const Texture& T, IDirect3DBaseTexture9*& Held);

	EffectRecord* Owner = nullptr;
	unsigned Generation = 0;
	bool Tried = false, Failed = false;
	std::string Key;
	std::vector<Constant> Constants;
	std::vector<Texture> Textures;
	std::vector<std::pair<UINT, std::string>> SamplerStates;
	std::vector<ID3D9NvrExtSampler*> Samplers;
	std::vector<unsigned char> UboData;
	UINT UboSize = 0, PushSize = 0, TargetCount = 1;
	std::string VertexShader;
	int StateCount = 0;
	ID3D9NvrExtPipeline* Pipeline = nullptr;
	EffectRecord::ExtUniformBuffer Ubo;
	std::map<std::string, IDirect3DBaseTexture9*> TextureOverrides;
	std::map<std::string, D3DXVECTOR4> VectorOverrides;
	std::vector<std::string> Missing;
};

struct Sm6PassSet { std::map<std::string, Sm6Pass> Passes; };
