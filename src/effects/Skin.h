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
		D3DXVECTOR4		SkinDiffusion;	// UNOFFICIAL: x PerPixelDiffusion, y Translucency (TESR_SkinDiffusion)
	};
	SkinStruct Constants;

	// UNOFFICIAL 2026-10-15: Conor's skin shader ([Shaders.Skin.NewSkin] Enabled, read once at startup: it picks which shader
	// files the SKIN names load, and whether the skin hooks are installed). CREDIT: Conor (mrfury97), from his NVR testbed
	// (github.com/mrfury97/Conor-sClaudeHell-NVR-SAL-Fork, branch testbed). Every SKIN20xx shader comes from SkinTemplate.hlsl,
	// lit by Includes/SkinLighting.hlsl (spherical-Gaussian subsurface scattering, transmission, dual-lobe highlights, sky light),
	// and the SkinScattering effect blurs the skin's light on screen. Off (the default): this build's SKIN20xx files as before.
	static bool NewSkinEnabled() {
		static int Setting = -1;
		if (Setting < 0) Setting = TheSettingManager->GetSettingI("Shaders.Skin.NewSkin", "Enabled") ? 1 : 0;
		return Setting != 0;
	}

	struct NewSkinStruct {
		D3DXVECTOR4		LightData;    // TESR_SkinLightData (c192): x SpecularStrength, y Roughness, z PerPixelWidth, w Translucency
		D3DXVECTOR4		LightExtra;   // TESR_SkinLightExtra (c193): x SkyReflectionScale (0 indoors), y wetness (rain x RainWetness), z ShadowScatter, w the vanilla-matched highlight factor (1-12)
		D3DXVECTOR4		DebugData;    // TESR_SkinDebugData (c195): x [Shaders.Skin.Debug] DebugView (1-7), y 1 while the Interiors settings are in use, z FillLight
		// 2026-10-19, Community Shaders' Advanced Skin details ([Shaders.Skin.NewSkin], all off by default; SkinLighting.hlsl):
		D3DXVECTOR4		LightCS;      // TESR_SkinLightCS (c196): x Fuzz strength, y EnergyBalance, z CreaseDamping, w TransmitProfile (1 on)
	};
	NewSkinStruct NewConstants = {};

	// The skin material, outdoors ([Shaders.Skin.NewSkin] + [Shaders.Skin.Scattering]) and indoors ([Shaders.Skin.Interiors]);
	// UpdateConstants picks one each frame.
	struct MaterialStruct {
		float SpecularStrength;
		float Roughness;
		float VanillaMatchedHighlights;
		float ShadowScatter;
		float Translucency;
		float PerPixelWidth;
		float FillLight;			// port: vanilla's wrap + rim light on top (SkinLighting.hlsl SkinVanillaFill), 0 = Conor's model alone
	};
	MaterialStruct Exterior = {};
	MaterialStruct Interior = {};
	float SkyReflectionScale = 1.0f;	// outdoors only: forced off indoors
	float RainWetness = 0.5f;			// outdoors only
	D3DXVECTOR4 CSDetails = {};			// 2026-10-19: [Shaders.Skin.NewSkin] Fuzz, EnergyBalance, CreaseDamping, TransmitProfile (indoors and out)

	// [Shaders.Skin.Debug] DebugView. 1-7 are drawn by the skin shaders (SkinTemplate.hlsl); 8-10 by the skin scattering
	// effect, which steps aside for 1-7 so it does not blur them.
	int DebugView = 0;
	bool MuteGameHighlights = true;	// port: [Shaders.Skin.NewSkin] MuteGameHighlights (Hooks/Shaders.cpp): the game's own face highlight passes off
	bool BindFaceGenMaps = true;	// port, diagnostic: [Shaders.Skin.Debug] BindFaceGenMaps (Hooks/Shaders.cpp BindFaceGenMaps)
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

	// Conor's map: vanilla's variants, from the vanilla shaders' own constants and inputs (shaderpackage010.sdp); see
	// SkinTemplate.hlsl. Vertex shaders come in pairs: static, then bone-skinned.
	std::map<std::string_view, ShaderTemplate> NewSkinTemplates() {
		return std::map<std::string_view, ShaderTemplate>{
			// ADTS: sun (+ projected shadow), fog and vertex colour.
			{ "SKIN2000.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}} } },
			{ "SKIN2001.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"SKIN", ""}} } },
			{ "SKIN2002.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"PROJ_SHADOW", ""}} } },
			{ "SKIN2003.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"PROJ_SHADOW", ""}, {"SKIN", ""}} } },
			{ "SKIN2004.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"POINTS", "1"}} } },
			{ "SKIN2005.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"POINTS", "1"}, {"SKIN", ""}} } },
			{ "SKIN2006.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"POINTS", "1"}, {"PROJ_SHADOW", ""}} } },
			{ "SKIN2007.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"POINTS", "1"}, {"PROJ_SHADOW", ""}, {"SKIN", ""}} } },
			// AD: light only, the engine applies the texture afterwards.
			{ "SKIN2008.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"ONLY_LIGHT", ""}, {"POINTS", "1"}} } },
			{ "SKIN2009.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"ONLY_LIGHT", ""}, {"POINTS", "1"}, {"SKIN", ""}} } },
			{ "SKIN2010.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"ONLY_LIGHT", ""}, {"POINTS", "1"}, {"PROJ_SHADOW", ""}} } },
			{ "SKIN2011.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"ONLY_LIGHT", ""}, {"POINTS", "1"}, {"PROJ_SHADOW", ""}, {"SKIN", ""}} } },
			{ "SKIN2012.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"ONLY_LIGHT", ""}, {"POINTS", "2"}} } },
			{ "SKIN2013.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"ONLY_LIGHT", ""}, {"POINTS", "2"}, {"SKIN", ""}} } },
			{ "SKIN2014.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"ONLY_LIGHT", ""}, {"POINTS", "2"}, {"PROJ_SHADOW", ""}} } },
			{ "SKIN2015.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"ONLY_LIGHT", ""}, {"POINTS", "2"}, {"PROJ_SHADOW", ""}, {"SKIN", ""}} } },
			// DiffusePt: point lights only.
			{ "SKIN2016.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"DIFFUSE", ""}, {"POINTS", "2"}} } },
			{ "SKIN2017.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"DIFFUSE", ""}, {"POINTS", "2"}, {"SKIN", ""}} } },
			{ "SKIN2018.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"DIFFUSE", ""}, {"POINTS", "3"}} } },
			{ "SKIN2019.vso", ShaderTemplate{ "SkinTemplate", {{"VS", ""}, {"DIFFUSE", ""}, {"POINTS", "3"}, {"SKIN", ""}} } },
			// ADTS10: sun + up to 4 point lights, live count in EmittanceColor.a.
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