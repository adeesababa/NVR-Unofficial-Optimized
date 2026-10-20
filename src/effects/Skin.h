#pragma once

class SkinShaders : public ShaderCollection
{
public:
	SkinShaders() : ShaderCollection("Skin") {};

	// Set from NewVegasReloaded/Main.cpp when VanillaPlusSkin.dll is detected, same soft-
	// detection pattern as AmbientOcclusion's bNVAOLoaded. VPS ships its own skin lighting
	// model (a subsurface-scattering LUT) through its own NVSE plugin, using the same vanilla
	// shader names ("SKIN2000.vso" etc.) that this collection's own hand-authored/decompiled
	// replacements are keyed on. Since the two can't blend at the file level, Templates()
	// below routes every SKIN name to SkinVPSTemplate.hlsl instead when this is set, which
	// reproduces VPS's own lighting model and adds NVR's forward sun-shadow atlas -- the one
	// thing VPS's own shader has no access to.
	bool bVPSLoaded = false;

	struct SkinStruct {
		D3DXVECTOR4		SkinData;
		D3DXVECTOR4		SkinColor;
		D3DXVECTOR4		SkinDiffusion;
	};
	SkinStruct Constants;

	static bool NewSkinEnabled() {
		static int Setting = -1;
		if (Setting < 0) Setting = TheSettingManager->GetSettingI("Shaders.Skin.NewSkin", "Enabled") ? 1 : 0;
		return Setting != 0;
	}

	struct NewSkinStruct {
		D3DXVECTOR4		LightData;
		D3DXVECTOR4		LightExtra;
		D3DXVECTOR4		DebugData;
		D3DXVECTOR4		LightCS;
	};
	NewSkinStruct NewConstants = {};

	struct MaterialStruct {
		float SpecularStrength;
		float Roughness;
		float VanillaMatchedHighlights;
		float ShadowScatter;
		float Translucency;
		float PerPixelWidth;
		float FillLight;
	};
	MaterialStruct Exterior = {};
	MaterialStruct Interior = {};
	float SkyReflectionScale = 1.0f;
	float RainWetness = 0.5f;
	D3DXVECTOR4 CSDetails = {};

	int DebugView = 0;
	bool MuteGameHighlights = true;
	bool BindFaceGenMaps = true;
	static const int DebugViewCount = 10;
	static const int FirstScatteringDebugView = 8;

	std::map<std::string_view, ShaderTemplate> Templates() {
		if (NewSkinEnabled()) return NewSkinTemplates();
		if (!bVPSLoaded) return std::map<std::string_view, ShaderTemplate>();

		return std::map<std::string_view, ShaderTemplate>{
			// Vertex shaders: ADTS Base/SKIN family, plus AD's LIGHTS=3/SKIN (ONLY_LIGHT).
			{ "SKIN2000.vso", ShaderTemplate{ "SkinVPSTemplate", {{"VS", ""}} } },
			{ "SKIN2001.vso", ShaderTemplate{ "SkinVPSTemplate", {{"VS", ""}, {"SKIN", ""}} } },
			{ "SKIN2005.vso", ShaderTemplate{ "SkinVPSTemplate", {{"VS", ""}, {"LIGHTS", "2"}, {"SKIN", ""}} } },
			{ "SKIN2013.vso", ShaderTemplate{ "SkinVPSTemplate", {{"VS", ""}, {"ONLY_LIGHT", ""}, {"LIGHTS", "3"}, {"SKIN", ""}} } },

			// Pixel shaders: ADTS Base/PROJ_SHADOW/LIGHTS=2 family.
			{ "SKIN2000.pso", ShaderTemplate{ "SkinVPSTemplate", {{"PS", ""}} } },
			{ "SKIN2001.pso", ShaderTemplate{ "SkinVPSTemplate", {{"PS", ""}, {"PROJ_SHADOW", ""}} } },
			{ "SKIN2002.pso", ShaderTemplate{ "SkinVPSTemplate", {{"PS", ""}, {"LIGHTS", "2"}} } },
			{ "SKIN2003.pso", ShaderTemplate{ "SkinVPSTemplate", {{"PS", ""}, {"LIGHTS", "2"}, {"PROJ_SHADOW", ""}} } },
			// Pixel shaders: AD's ONLY_LIGHT LIGHTS=2/3 family.
			{ "SKIN2004.pso", ShaderTemplate{ "SkinVPSTemplate", {{"PS", ""}, {"ONLY_LIGHT", ""}, {"LIGHTS", "2"}} } },
			{ "SKIN2005.pso", ShaderTemplate{ "SkinVPSTemplate", {{"PS", ""}, {"ONLY_LIGHT", ""}, {"LIGHTS", "2"}, {"PROJ_SHADOW", ""}} } },
			{ "SKIN2006.pso", ShaderTemplate{ "SkinVPSTemplate", {{"PS", ""}, {"ONLY_LIGHT", ""}, {"LIGHTS", "3"}} } },
			{ "SKIN2007.pso", ShaderTemplate{ "SkinVPSTemplate", {{"PS", ""}, {"ONLY_LIGHT", ""}, {"LIGHTS", "3"}, {"PROJ_SHADOW", ""}} } },
			// Pixel shaders: DiffusePt point-light-only family.
			{ "SKIN2008.pso", ShaderTemplate{ "SkinVPSTemplate", {{"PS", ""}, {"DIFFUSE", ""}, {"LIGHTS", "2"}} } },
			{ "SKIN2009.pso", ShaderTemplate{ "SkinVPSTemplate", {{"PS", ""}, {"DIFFUSE", ""}, {"LIGHTS", "3"}} } },
			// Pixel shaders: ADTS10 many-lights family.
			{ "SKIN2010.pso", ShaderTemplate{ "SkinVPSTemplate", {{"PS", ""}, {"LIGHTS", "9"}} } },
			{ "SKIN2011.pso", ShaderTemplate{ "SkinVPSTemplate", {{"PS", ""}, {"LIGHTS", "4"}} } },
		};
	};

	std::map<std::string_view, ShaderTemplate> NewSkinTemplates() {
		return std::map<std::string_view, ShaderTemplate>{
			{ "SKIN2000.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}} } },
			{ "SKIN2001.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"SKIN", ""}} } },
			{ "SKIN2002.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"PROJ_SHADOW", ""}} } },
			{ "SKIN2003.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"PROJ_SHADOW", ""}, {"SKIN", ""}} } },
			{ "SKIN2004.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"POINTS", "1"}} } },
			{ "SKIN2005.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"POINTS", "1"}, {"SKIN", ""}} } },
			{ "SKIN2006.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"POINTS", "1"}, {"PROJ_SHADOW", ""}} } },
			{ "SKIN2007.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"POINTS", "1"}, {"PROJ_SHADOW", ""}, {"SKIN", ""}} } },
			{ "SKIN2008.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"ONLY_LIGHT", ""}, {"POINTS", "1"}} } },
			{ "SKIN2009.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"ONLY_LIGHT", ""}, {"POINTS", "1"}, {"SKIN", ""}} } },
			{ "SKIN2010.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"ONLY_LIGHT", ""}, {"POINTS", "1"}, {"PROJ_SHADOW", ""}} } },
			{ "SKIN2011.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"ONLY_LIGHT", ""}, {"POINTS", "1"}, {"PROJ_SHADOW", ""}, {"SKIN", ""}} } },
			{ "SKIN2012.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"ONLY_LIGHT", ""}, {"POINTS", "2"}} } },
			{ "SKIN2013.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"ONLY_LIGHT", ""}, {"POINTS", "2"}, {"SKIN", ""}} } },
			{ "SKIN2014.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"ONLY_LIGHT", ""}, {"POINTS", "2"}, {"PROJ_SHADOW", ""}} } },
			{ "SKIN2015.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"ONLY_LIGHT", ""}, {"POINTS", "2"}, {"PROJ_SHADOW", ""}, {"SKIN", ""}} } },
			{ "SKIN2016.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"DIFFUSE", ""}, {"POINTS", "2"}} } },
			{ "SKIN2017.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"DIFFUSE", ""}, {"POINTS", "2"}, {"SKIN", ""}} } },
			{ "SKIN2018.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"DIFFUSE", ""}, {"POINTS", "3"}} } },
			{ "SKIN2019.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"DIFFUSE", ""}, {"POINTS", "3"}, {"SKIN", ""}} } },
			{ "SKIN2020.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"MANY", ""}, {"POINTS", "4"}} } },
			{ "SKIN2021.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"MANY", ""}, {"POINTS", "4"}, {"SKIN", ""}} } },
			{ "SKIN2022.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"MANY", ""}, {"POINTS", "3"}} } },
			{ "SKIN2023.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"MANY", ""}, {"POINTS", "3"}, {"SKIN", ""}} } },
			{ "SKIN2024.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"MANY", ""}, {"POINTS", "1"}} } },
			{ "SKIN2025.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"MANY", ""}, {"POINTS", "1"}, {"SKIN", ""}} } },

			{ "SKIN2000.pso", ShaderTemplate{ "SkinTemplate", {{"PS", ""}} } },
			{ "SKIN2001.pso", ShaderTemplate{ "SkinTemplate", {{"PS", ""}, {"PROJ_SHADOW", ""}} } },
			{ "SKIN2002.pso", ShaderTemplate{ "SkinTemplate", {{"PS", ""}, {"POINTS", "1"}} } },
			{ "SKIN2003.pso", ShaderTemplate{ "SkinTemplate", {{"PS", ""}, {"POINTS", "1"}, {"PROJ_SHADOW", ""}} } },
			{ "SKIN2004.pso", ShaderTemplate{ "SkinTemplate", {{"PS", ""}, {"ONLY_LIGHT", ""}, {"POINTS", "1"}} } },
			{ "SKIN2005.pso", ShaderTemplate{ "SkinTemplate", {{"PS", ""}, {"ONLY_LIGHT", ""}, {"POINTS", "1"}, {"PROJ_SHADOW", ""}} } },
			{ "SKIN2006.pso", ShaderTemplate{ "SkinTemplate", {{"PS", ""}, {"ONLY_LIGHT", ""}, {"POINTS", "2"}} } },
			{ "SKIN2007.pso", ShaderTemplate{ "SkinTemplate", {{"PS", ""}, {"ONLY_LIGHT", ""}, {"POINTS", "2"}, {"PROJ_SHADOW", ""}} } },
			{ "SKIN2008.pso", ShaderTemplate{ "SkinTemplate", {{"PS", ""}, {"DIFFUSE", ""}, {"POINTS", "2"}} } },
			{ "SKIN2009.pso", ShaderTemplate{ "SkinTemplate", {{"PS", ""}, {"DIFFUSE", ""}, {"POINTS", "3"}} } },
			{ "SKIN2010.pso", ShaderTemplate{ "SkinTemplate", {{"PS", ""}, {"MANY", ""}, {"POINTS", "4"}} } },
			{ "SKIN2011.pso", ShaderTemplate{ "SkinTemplate", {{"PS", ""}, {"MANY", ""}, {"POINTS", "3"}} } },
			{ "SKIN2012.pso", ShaderTemplate{ "SkinTemplate", {{"PS", ""}, {"MANY", ""}, {"POINTS", "1"}} } },
		};
	};

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
};