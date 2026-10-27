#include "ShaderRecord.h"
#include "DxvkCompute.h"	// UNOFFICIAL 2026-10-24 (NVR DXVK phase 2)
#include "ShaderWarmUp.h"
#include "PointShadowForward.h"
#include "ShaderPrecompile.h"
#include <unordered_set>
#include "GpuProfiler.h"
#include "LinearPreDecode.h"	// UNOFFICIAL 2026-10-17
#include "ReflMaterial.h"	// UNOFFICIAL 2026-10-20 MaterialAware reflections
#include "LandLight.h"	// UNOFFICIAL 2026-10-25 (SunFar)
ShaderProgram::ShaderProgram() {

	FloatShaderValues = NULL;
	TextureShaderValues = NULL;
	FloatShaderValuesCount = 0;
	TextureShaderValuesCount = 0;

}

ShaderProgram::~ShaderProgram() {

	delete[] FloatShaderValues;
	delete[] TextureShaderValues;
}


ShaderRecord::ShaderRecord() {

	HasRenderedBuffer = false;
	HasDepthBuffer = false;
	ClearSamplers = true;

}
ShaderRecord::~ShaderRecord() {}

/*
 * Unbinds a named sampler's cached texture so it is re-resolved on the next SetCT.
 *
 * TextureRecord caches a raw IDirect3DTexture9* and SetCT only calls BindTexture when that
 * pointer is null, so a texture that gets Release()d and recreated leaves every consumer
 * holding a dangling pointer. Each ShaderTextureValue owns its own TextureRecord (see
 * ShaderTextureValue::GetTextureRecord), so there is no shared record to invalidate -- every
 * consumer has to be cleared individually.
 */
void ShaderProgram::ClearSampler(const char* TextureName, size_t Length) {
	ShaderTextureValue* Sampler;
	for (UInt32 c = 0; c < TextureShaderValuesCount; c++) {
		Sampler = &TextureShaderValues[c];
		// GetTextureRecord bails without assigning Texture when the sampler type is
		// unrecognised, so this can legitimately be null.
		if (!Sampler->Texture) continue;
		if (!memcmp(Sampler->Name, TextureName, Length) && Sampler->Texture->Texture) {
			Sampler->Texture->Texture = nullptr;
		}
	}
}

void ShaderProgram::ReportError(HRESULT result) {
	if (result == E_ABORT) Logger::Log("Operation aborted");
	if (result == E_ACCESSDENIED) Logger::Log("Access Denied");
	if (result == E_FAIL) Logger::Log("Operation Failed");
	if (result == E_HANDLE) Logger::Log("Handle invalid");
	if (result == E_INVALIDARG) Logger::Log("Argument invalid");
	if (result == E_NOINTERFACE) Logger::Log("Interface invalid");
	if (result == E_NOTIMPL) Logger::Log("Not implemented");
	if (result == E_OUTOFMEMORY) Logger::Log("Out of memory");
	if (result == E_POINTER) Logger::Log("Invalid pointer");
	if (result == E_UNEXPECTED) Logger::Log("Unexpected fail");
}

/*
* Check for file existence.
*/
bool ShaderProgram::FileExists(const char* path) {
	WIN32_FILE_ATTRIBUTE_DATA attributes = { 0 };
	return GetFileAttributesExA(path, GetFileExInfoStandard, &attributes);
}

/*
* Check if cached preprocessed source exists and matches the current result.
*/
bool ShaderProgram::CheckPreprocessResult(const char* CachedPreprocessPath, ID3DXBuffer* ShaderSource) {
	std::ifstream File(CachedPreprocessPath, std::ios::in | std::ios::binary);
	if (!File.is_open()) return false;
	const std::string Cached((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
	return !Cached.empty() && Cached == PortableSource(ShaderSource);
}

// UNOFFICIAL: the preprocessed source as it is cached and compared. D3DX writes the full path of the top file into its
// #line directives, so a cache made in one game folder never matched another and everything compiled again; only the
// part of those paths from "\Data\" on is kept. That lets a release ship a ready cache (the compiled shaders do not depend
// on the path; the compiler itself still gets the full source, for its error messages).
std::string ShaderProgram::PortableSource(ID3DXBuffer* Source) {
	std::string Text((const char*)Source->GetBufferPointer(), Source->GetBufferSize());
	size_t At = 0;
	while ((At = Text.find("#line ", At)) != std::string::npos) {
		const size_t LineEnd = Text.find('\n', At);
		const size_t Open = Text.find('"', At);
		const size_t Close = Open == std::string::npos ? std::string::npos : Text.find('"', Open + 1);
		if (Open != std::string::npos && Close != std::string::npos && (LineEnd == std::string::npos || Close < LineEnd)) {
			std::string Path = Text.substr(Open + 1, Close - Open - 1);
			for (char& c : Path) c = (char)tolower((unsigned char)c);
			const size_t Data = Path.rfind("\\\\data\\\\");	// the path is C-escaped: two backslashes a separator
			if (Data != std::string::npos) Text.erase(Open + 1, Data + 2);
		}
		At += 6;
	}
	return Text;
}

// UNOFFICIAL 2026-10-14: the defines every NVR game shader is compiled with (the template's, the overrides first, then the
// settings'), in its own function so ShaderPrecompile works them out the same way.
void ShaderRecord::BuildDefines(ShaderTemplate& Template, const D3DXMACRO* Overrides) {
	// Append a define to the template, keeping the array null-terminated.
	auto AppendDefine = [&Template](const char* Name, const char* Definition) {
		int i = 0;
		while (i < 46 && Template.Defines[i].Name != NULL) {
			if (!strcmp(Template.Defines[i].Name, Name)) return;	// an override already set it
			i++;
		}
		if (i >= 46) {  // out of room (Defines[48]: 46 entries and the terminator); skip rather than overrun
			static bool warned = false;	// fixes-a #12: say so once (a dropped define = the shader compiled without that option)
			if (!warned) { warned = true; Logger::Log("[ERROR] ShaderRecord: more than 46 shader defines, '%s' and any after it are dropped", Name); }
			return;
		}
		Template.Defines[i] = { Name, Definition };
		Template.Defines[i + 1] = { NULL, NULL };
	};

	// UNOFFICIAL: overrides first, so the global defines below with the same name are skipped.
	if (Overrides)
		for (const D3DXMACRO* o = Overrides; o->Name; o++) AppendDefine(o->Name, o->Definition);

	if (TheRenderManager->IsReversedDepth())
		AppendDefine("REVERSED_DEPTH", "");

	// Forward sun shadows. Read straight from the setting manager rather than from the
	// ShadowsExteriors effect, because shaders can be loaded before that effect is built.
	//
	// This governs whether the forward path is compiled in at all, so that builds which do not
	// want it pay nothing for it. Whether it RUNS is a separate, runtime decision made by
	// TESR_ShadowForwardData -- see ShadowsExteriorEffect::UpdateSettings. Changing the setting
	// alters the preprocessed source, so CheckPreprocessResult recompiles on next load.
	AppendDefine("FORWARD_SHADOWS",
		TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Main", "ForwardShadows") ? "1" : "0");

	// UNOFFICIAL sun shadow cross-fade (SunSmoothing CrossFade): compiled in only when on, so that otherwise the
	// shaders are exactly what they were. Read here for the same reason as FORWARD_SHADOWS; takes effect on restart.
	// UNOFFICIAL optional contact-hardening sun shadows: compiled in only when on at startup, like SUN_CROSSFADE.
	AppendDefine("CONTACT_HARDENING", TheSettingManager->GetSettingI("Shaders.ContactHardening.Status", "Enabled") ? "1" : "0");
	// UNOFFICIAL 2026-10-22 CasterReachEverywhere, a TEST switch: CharacterShadowReach's slack cap (Includes/Shadow.hlsl ShadowSlackCap)
	// compiled into every forward shader instead of the skin shaders only. Off (the default): the other shaders are exactly what they were.
	AppendDefine("SHADOW_SLACK_CAP_ALL", TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Main", "CasterReachEverywhere") ? "1" : "0");
	// UNOFFICIAL 2026-10-15: interior forward lamp shadows with smooth edges (Includes/PointShadowForward.hlsl)
	AppendDefine("POINT_SHADOW_SMOOTH", TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Forward", "SmoothEdges") ? "1" : "0");
	// UNOFFICIAL 2026-10-21 (deep/lampedge) EvenTaps: High/Ultra's extra taps spread evenly; MagnifiedSoftness above 0: lamp shadow edges
	// at least that soft where the shadow map is magnified on screen. Each defined only when on, so that off the define list and the
	// shaders are exactly what they were (the list is near its 38-entry cap: 29 + 6 template defines + these two).
	if (TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Forward", "EvenTaps"))
		AppendDefine("POINT_SHADOW_EVEN", "1");
	// TexelFilter: the lamp shadows filtered as the sun's PCF (texel-aligned quadratic B-spline); MagnifiedSoftness is not needed with it
	// (and not compiled in: the six-lamp shaders have no room for both).
	const bool texelFilter = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Forward", "TexelFilter") != 0;
	if (texelFilter)
		AppendDefine("POINT_SHADOW_TEXEL", "1");
	else if (TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Forward", "MagnifiedSoftness") > 0.0f)
		AppendDefine("POINT_SHADOW_MAG", "1");

	// UNOFFICIAL CubeReflections (Includes/LiveCube.hlsl): metal and the env-map pass read the live surroundings cube only when
	// compiled in, which is when either setting is above 0 at startup, so that otherwise those shaders are exactly what they were.
	AppendDefine("LIVE_CUBE", TheSettingManager->GetSettingF("Shaders.PBR.Metal", "CubeReflections") > 0.0f ||
		TheSettingManager->GetSettingF("Shaders.PBR.EnvMaps", "CubeReflections") > 0.0f ? "1" : "0");

	// UNOFFICIAL material rules (Includes/Object.hlsl, ObjectTemplate.hlsl): compiled in only when on at startup, so that
	// otherwise the object shaders are exactly what they were (the rules' branches cost 1-5% there even unused).
	AppendDefine("MATERIAL_RULES_ON", TheSettingManager->GetSettingI("Shaders.PBR.MaterialRules", "Enabled") ? "1" : "0");

	// UNOFFICIAL _rmaos maps (Includes/Object.hlsl, ObjectTemplate.hlsl, Render.cpp RmaosDraw): compiled in only when on at
	// startup, so that otherwise the object shaders are exactly what they were.
	AppendDefine("RMAOS_ON", TheSettingManager->GetSettingI("Shaders.PBR.Rmaos", "Enabled") ? "1" : "0");
	// UNOFFICIAL 2026-10-20 MaterialAware reflections (Includes/Object.hlsl ReflMaterialAlpha, effects/Reflections.cpp): the objects' first
	// passes leave their _rmaos material in the scene's alpha. Defined only when on at startup (with the _rmaos maps): off, the define
	// list and the shaders are exactly what they were (the list is near its 34-entry cap).
	if (ReflMaterial::CompiledIn())
		AppendDefine("REFL_MATERIAL_ON", "1");

	// UNOFFICIAL rain on the first-person weapon (Includes/HudRain.hlsl, ObjectTemplate.hlsl, Render.cpp HudRain): compiled in only
	// when Wet World's HudDrops is above 0 at startup.
	AppendDefine("HUD_RAIN_ON", TheSettingManager->GetSettingF("Shaders.WetWorld.Main", "HudDrops") > 0.0f ? "1" : "0");

	// UNOFFICIAL linear lighting (Includes/LinearLight.hlsl: ObjectTemplate, TerrainTemplate): compiled in only when on at
	// startup, so that otherwise those shaders are exactly what they were. Gamma stays live (PBRShaders::UpdateSettings).
	AppendDefine("LINEAR_LIGHTING_ON", TheSettingManager->GetSettingI("Shaders.PBR.Linear", "Enabled") ? "1" : "0");

	// UNOFFICIAL physical sky (Includes/Atmosphere.hlsl, SKY.pso): compiled in only when on at startup, so that otherwise the
	// sky shader is exactly what it was. Its other settings are live (SkyShaders::UpdateSettings).
	AppendDefine("PHYSICAL_SKY_ON", TheSettingManager->GetSettingI("Shaders.Sky.Physical", "Enabled") ? "1" : "0");

	// UNOFFICIAL MoreLampsPerObject (Hooks.cpp patches the game's cap to match): the specular many-lamp object shaders take
	// 6 lamps instead of 3. Read at startup, like the engine patch.
	AppendDefine("SPECULAR_LAMPS6", TheSettingManager->GetSettingI("Main.Main.Misc", "MoreLampsPerObject") ? "1" : "0");
	// UNOFFICIAL 2026-10-17 LampReachSkip (ObjectTemplate.hlsl LAMP_IF, ManyLightsDirect.hlsl): each lamp's light term in a branch on
	// its attenuation, skipped where the lamp cannot reach (it adds exactly 0 there). Compile time (a live switch cost 0.6 ms indoors).
	AppendDefine("LAMP_REACH_SKIP", TheSettingManager->GetSettingI("Shaders.PBR.Main", "LampReachSkip") ? "1" : "0");
	// UNOFFICIAL 2026-10-25 DxvkLampBranches (ObjectTemplate.hlsl NO_SLOT_LIMIT): on DXVK, which has no ~3,600-slot limit, the 6-lamp
	// object shaders skip unused lamp slots, out-of-reach lamps and their shadow lookups. Native D3D9 would refuse those shaders: 0 there.
	AppendDefine("NO_SLOT_LIMIT", (TheSettingManager->GetSettingI("Shaders.PBR.Main", "DxvkLampBranches") &&
		TheRenderManager && !strncmp(TheRenderManager->D3D9RuntimeDescription(), "DXVK", 4)) ? "1" : "0");
	// UNOFFICIAL 2026-10-17 UnlitShadowSkip (ObjectTemplate.hlsl SunShadowWanted): no forward sun-shadow lookups on object pixels whose
	// bumped normal faces away from the sun (the sun adds exactly nothing there). Compile time, like LAMP_REACH_SKIP.
	AppendDefine("UNLIT_SHADOW_SKIP", TheSettingManager->GetSettingI("Shaders.PBR.Main", "UnlitShadowSkip") ? "1" : "0");
	// UNOFFICIAL 2026-10-18 SmithCorrelated (Includes/PBR.hlsl BRDF + SmithCorrelatedKeep; idea from Physically-Based-Reloaded (Balebandro),
	// GPL-3.0): the object and parallax pixel shaders take the exact height-correlated Smith masking term (Heitz 2014) and the coupled
	// diffuse it needs. A look change, compiled in only when on at startup, so that off the shaders are exactly what they were.
	// fixes-a #11: only 1 is on; any other value is off. The old test mode 2 (slow, and refused by the GPU in one indoor many-lamp
	// shader) is no longer reachable from the menu or the config.
	{
		const int smith = TheSettingManager->GetSettingI("Shaders.PBR.Main", "SmithCorrelated");
		AppendDefine("SMITH_CORRELATED_ON", smith == 1 ? "1" : "0");
	}
	// UNOFFICIAL 2026-10-18 SpecularFade (Includes/PBR.hlsl PBR_SPEC_FADE, core/PbrPop.h): the object shaders' highlights fade out with the
	// game's specular distance fade instead of vanishing at once where the game swaps in the shaders without one. Compiled in only when on
	// at startup (the per-draw value then switches it live: 1 = the old result exactly).
	AppendDefine("SPEC_FADE_ON", TheSettingManager->GetSettingI("Shaders.PBR.Main", "SpecularFade") ? "1" : "0");
	// UNOFFICIAL 2026-10-21 FoldHighlights (ObjectTemplate.hlsl NVR_FOLD, core/FoldHighlights.h): the light-only and extra-lamp passes of shiny
	// objects with 3+ lamps can draw their lamps' highlights (and metal) themselves, as the one-pass shaders do. Compiled in only when on at
	// startup (the per-draw switch then turns it on live: 0 = the old result exactly).
	AppendDefine("FOLD_HIGHLIGHTS_ON", TheSettingManager->GetSettingI("Shaders.PBR.Main", "FoldHighlights") ? "1" : "0");
	// UNOFFICIAL 2026-10-22 LampDistanceFix (ObjectTemplate.hlsl LAMP_ATT_SQ): the 2- and 3-lamp object shaders take each lamp's falloff from the
	// lamp vector squared per pixel (as the game's shaders do) instead of a squared distance blended from the vertices, which left big flat
	// surfaces near a lamp (rugs, floors, posters, walls) almost unlit. Compiled in only when on at startup; off = the shaders as before.
	AppendDefine("LAMP_VECTOR_ATT_ON", TheSettingManager->GetSettingI("Shaders.PBR.Main", "LampDistanceFix") ? "1" : "0");
	// UNOFFICIAL 2026-10-21 LampSums (ObjectTemplate.hlsl NVR_LAMP_SUMS, core/PassSums.h): the extra lamp passes add their light to the light-only
	// pass's in linear light (exact where one such pass covers a pixel). Compiled in only when on at startup (with linear lighting); the
	// per-run switch then turns it on live: 0 = the old result exactly.
	AppendDefine("LAMP_SUMS_ON", TheSettingManager->GetSettingI("Shaders.PBR.Main", "LampSums") ? "1" : "0");
	// UNOFFICIAL 2026-10-25 HighlightSums (ObjectTemplate.hlsl / ParallaxTemplate.hlsl NVR_LAMP_SUMS in the ONLY_SPECULAR variants, core/PassSums.h):
	// the highlight passes FoldHighlights leaves to the game (parallax objects above all) add their light in linear light the same way.
	// Compiled in only when on at startup (with linear lighting); the per-run switch then turns it on live: 0 = the old result exactly.
	AppendDefine("HIGHLIGHT_SUMS_ON", TheSettingManager->GetSettingI("Shaders.PBR.Main", "HighlightSums") ? "1" : "0");
	// UNOFFICIAL 2026-10-17 LinearPreDecode (core/LinearPreDecode.h, ObjectTemplate.hlsl LIN_PREDECODED): with linear lighting on, the lit
	// objects' colour constants are decoded once on the CPU instead of per pixel. Read at startup, like LINEAR_LIGHTING_ON.
	AppendDefine("LIN_PREDECODE_ON", TheSettingManager->GetSettingI("Shaders.PBR.Linear", "PreDecode") ? "1" : "0");
	// UNOFFICIAL 2026-10-21 LampFalloff (Includes/LampFalloff.hlsl, effects/PBR.cpp): the lamps' physically shaped falloff and MatchEverywhere,
	// compiled in only when one of the switches is on at startup (then live), and defined only then: off, the define list and every
	// shader are exactly what they were.
	// 1: the constant and MatchEverywhere only; 2: the shape too (Exterior or Interior). The interior lamp-shadow records override it
	// without Interior (ShaderManager::LoadShader, PBRShaders::LampFalloffInteriorDefine).
	if (PBRShaders::LampFalloffCompiledIn())
		AppendDefine("LAMP_FALLOFF_ON", PBRShaders::LampFalloffShapeIn() ? "2" : "1");

	// UNOFFICIAL terrain blending (TerrainTemplate.hlsl, from Physically-Based-Reloaded): compiled in only when on at startup,
	// like LIVE_CUBE, so that otherwise the land shaders are exactly what they were. TextureManager makes its depth copy
	// under the same condition.
	AppendDefine("TERRAIN_BLEND", TheSettingManager->GetSettingI("Shaders.Terrain.Blending", "Enabled") ? "1" : "0");
	// UNOFFICIAL 2026-10-19 Terrain Variation (Includes/TerrainVariation.hlsl): compiled in only when on at startup, and only for the
	// first Layers blend layers (after Conor's port, 66653eea + d07ff4b9: it changes the shape of the land's sampling, not a value).
	{
		AppendDefine("TERRAIN_VARIATION", TheSettingManager->GetSettingI("Shaders.Terrain.Variation", "Enabled") ? "1" : "0");
		static char layerDefine[2] = { '3', 0 };
		layerDefine[0] = (char)('0' + std::clamp(TheSettingManager->GetSettingI("Shaders.Terrain.Variation", "Layers"), 1, 7));
		AppendDefine("TERRAIN_VARIATION_LAYERS", layerDefine);
	}

	// UNOFFICIAL 2026-10-13 blood on the first-person weapon and gloves (Includes/HandBlood.hlsl, HandBlood.cpp): compiled in only
	// when on at startup, like TERRAIN_BLEND (the per-draw switch alone cost every lit object 2-5 %).
	AppendDefine("HAND_BLOOD", TheSettingManager->GetSettingI("Shaders.HandBlood.Status", "Enabled") ? "1" : "0");
	// UNOFFICIAL 2026-10-16 cloth wind (Includes/ClothWind.hlsl, core/ClothWind.h): the vertex shaders get it only when on at startup.
	AppendDefine("CLOTH_WIND_ON", TheSettingManager->GetSettingI("Shaders.Cloth.Main", "Enabled") ? "1" : "0");
	// UNOFFICIAL 2026-10-20 plant wind (Includes/PlantWind.hlsl, core/PlantWind.h): the static plants' part, likewise.
	AppendDefine("PLANT_WIND_ON", TheSettingManager->GetSettingI("Shaders.Plants.Wind", "Enabled") ? "1" : "0");

	AppendDefine("SUN_CROSSFADE", TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.SunSmoothing", "CrossFade") &&
		TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Main", "ForwardShadows") ? "1" : "0");

	// Which skylighting model is compiled in. 0 = spherical harmonic irradiance, 1 = the older
	// single directional sample. Compile time rather than a runtime branch: ps_3_0 flattens
	// branches like this, so a runtime switch would make every lit pixel pay for BOTH paths.
	// Changing the setting alters the preprocessed source, so the cache recompiles on next load.
	AppendDefine("SKYLIGHTING_MODE",
		TheSettingManager->GetSettingI("Shaders.PBR.Main", "SkylightingMode") ? "1" : "0");

	// Shadow atlas encoding: 0 = VSM, 1 = EVSM2, 2 = EVSM4. Compile time for the same reason as
	// above -- Shadow.hlsl's GetSunShadow and ShadowMap.pso both branch on it with #if, not a
	// runtime read, since ps_3_0 would otherwise flatten all three variants into every shadowed
	// pixel. Read straight from the setting manager rather than from the ShadowsExteriors
	// effect, same reasoning as FORWARD_SHADOWS above.
	//
	// Must mirror ShadowsExteriorEffect::UpdateSettingsFromQuality's Quality -> Mode mapping:
	// Quality 0/1 -> VSM, 2 -> EVSM2, 3 -> EVSM4. Quality 4 (Custom) reads Mode directly, same
	// as UpdateSettingsFromQuality does for that case. Previously this was never defined, so it
	// silently fell back to Shadow.hlsl's own "#ifndef SHADOW_FIXED_MODE -> 2" default: every
	// shader was compiled assuming EVSM4 regardless of Quality, so GetSunShadow's
	// "TESR_ShadowFormatData.x != SHADOW_FIXED_MODE" check only ever matched at Quality 3/Full,
	// and forward shadows silently no-op'd (return unshadowed) at every other quality level.
	{
		int quality = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Main", "Quality");
		int shadowMode;
		switch (quality) {
		case 0:
		case 1:
			shadowMode = 0; // VSM
			break;
		case 2:
			shadowMode = 1; // EVSM2
			break;
		case 3:
			shadowMode = 2; // EVSM4
			break;
		default: // 4 (Custom), or an out-of-range value -- fall back to the raw setting.
			shadowMode = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.ShadowMaps", "Mode");
			if (shadowMode < 0) shadowMode = 0;
			if (shadowMode > 3) shadowMode = 3;	// UNOFFICIAL: 3 = PCF
			break;
		}
		AppendDefine("SHADOW_FIXED_MODE", shadowMode == 0 ? "0" : shadowMode == 1 ? "1" : shadowMode == 2 ? "2" : "3");
	}
	// UNOFFICIAL 2026-10-23 CharacterObjectReach: CharacterShadowReach's slack cap compiled into the object pixel shaders for the draws
	// flagged as an actor's head, hair, face mask or headgear (ObjectTemplate.hlsl, Includes/Shadow.hlsl SHADOW_SLACK_CAP_ACTOR). Defined
	// only when on, and last, so that off the list and the shaders are exactly what they were and it never pushes another define out.
	if (TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Main", "CharacterObjectReach"))
		AppendDefine("SHADOW_CHARACTER_OBJECTS", "1");
	// UNOFFICIAL 2026-10-23 CharacterShadowMap ([Shaders.ShadowsExteriors.ShadowMaps]; Includes/Shadow.hlsl CharacterMapShadow): the
	// player's own sun shadow map, read by the skin shaders and the object pixel shaders. Defined only when on, and last, as above.
	// (ShadowsExteriorEffect::RegisterTextures reads the same setting for CharacterMapCompiled.)
	if (TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.ShadowMaps", "CharacterShadowMap"))
		AppendDefine("CHARACTER_SHADOW_MAP", "1");
	// UNOFFICIAL 2026-10-24 SmoothObjectNormals ([Shaders.PBR.Main], restart): the object and skin shaders' shadow lookups and sky ambient take
	// the vertex normal instead of the per-triangle one (Includes/Shadow.hlsl ShadowFacingNormal). 0: the shaders exactly as before.
	AppendDefine("OBJECT_SMOOTH_NORMALS", TheSettingManager->GetSettingI("Shaders.PBR.Main", "SmoothObjectNormals") ? "1" : "0");

}

/*
Loads the shader by name from a given subfolder (optionally). Shader will be compiled if needed.
@returns the ShaderRecord for this shader.
*/
ShaderRecord* ShaderRecord::LoadShader(const char* Name, const char* SubPath, ShaderTemplate Template, const char* CacheSubPath,
	const D3DXMACRO* Overrides) {
	auto timer = TimeLogger();
	ShaderPrecompile::StartOnce();	// UNOFFICIAL: the first shader starts the parallel compile (once)

	ShaderRecord* ShaderProg = NULL;
	ID3DXBuffer* ShaderSource = NULL;
	ID3DXBuffer* Shader = NULL;
	ID3DXBuffer* Errors = NULL;
	ID3DXConstantTable* ConstantTable = NULL;
	void* Function = NULL;
	char ShaderProfile[7];
	
	char BaseDirectory[MAX_PATH];
	char CacheDirectory[MAX_PATH];
	char ShaderSourcePath[MAX_PATH];
	char ShaderPreprocessedPath[MAX_PATH];
	char ShaderCompiledPath[MAX_PATH];
	
	// Build filenames.
	strcpy(BaseDirectory, ShadersPath);
	strcpy(CacheDirectory, BaseDirectory);
	strcat(CacheDirectory, "Cache\\");
	if (SubPath) strcat(BaseDirectory, SubPath);
	if (CacheSubPath) {
		strcat(CacheDirectory, CacheSubPath);
		CreateDirectoryA(CacheDirectory, NULL);	// nothing else writes there
	}
	else if (SubPath) strcat(CacheDirectory, SubPath);

	strcpy(ShaderSourcePath, BaseDirectory);
	if (Template.Name != NULL) {
		strcat(ShaderSourcePath, Template.Name);
	}
	else {
		strcat(ShaderSourcePath, Name);
	}
	strcat(ShaderSourcePath, ".hlsl");

	if (!FileExists(ShaderSourcePath)) {
		return ShaderProg;
	}

	// UNOFFICIAL 2026-10-27 [Shaders.Grass.Main] PointLights (off by default): NVR's 1POINT grass shaders (GRASS23x004-007.vso,
	// GRASS23x001TMS/003TMS.pso: grass by a lamp or fire) only when on; off, the game's own as before (startup setting).
	// (2026-10-27: also their twins without transparency multisampling, GRASS23x001/003.pso, used with the game's MSAA off.)
	if (!memcmp(Name, "GRASS23x00", 10) && (!strcmp(Name + 10, "4.vso") || !strcmp(Name + 10, "5.vso") || !strcmp(Name + 10, "6.vso") ||
		!strcmp(Name + 10, "7.vso") || !strcmp(Name + 10, "1TMS.pso") || !strcmp(Name + 10, "3TMS.pso") ||
		!strcmp(Name + 10, "1.pso") || !strcmp(Name + 10, "3.pso")) &&
		!TheSettingManager->GetSettingI("Shaders.Grass.Main", "PointLights"))
		return ShaderProg;

	strcpy(ShaderPreprocessedPath, CacheDirectory);
	strcat(ShaderPreprocessedPath, Name);
	strcat(ShaderPreprocessedPath, ".hlsl");

	strcpy(ShaderCompiledPath, CacheDirectory);
	strcat(ShaderCompiledPath, Name);

	D3DXMACRO* Macros = &(Template.Defines[0]);

	BuildDefines(Template, Overrides);	// UNOFFICIAL 2026-10-14: in its own function, shared with ShaderPrecompile

	// UNOFFICIAL 2026-10-14 ParallelShaderCompile (ShaderPrecompile.h): the result a worker thread already made for this shader.
	const bool precompiled = ShaderPrecompile::Take(ShaderCompiledPath, &ShaderSource, &Shader);
	HRESULT prepass = precompiled ? D3D_OK : D3DXPreprocessShaderFromFileA(ShaderSourcePath, Macros, NULL, &ShaderSource, &Errors);
	if (prepass == D3D_OK) {
		bool Compile = precompiled ? false : !CheckPreprocessResult(ShaderPreprocessedPath, ShaderSource);

		if (strstr(Name, ".vso"))
			strcpy(ShaderProfile, "vs_3_0");
		else if (strstr(Name, ".pso"))
			strcpy(ShaderProfile, "ps_3_0");

		if (precompiled) Function = Shader ? Shader->GetBufferPointer() : NULL;
		else if (!Compile){
			std::ifstream FileBinary(ShaderCompiledPath, std::ios::in | std::ios::binary | std::ios::ate);
			if (FileBinary.is_open()) {
				std::streamoff Size = FileBinary.tellg();
				if (Size > 0 && SUCCEEDED(D3DXCreateBuffer(Size, &Shader))) {
					FileBinary.seekg(0, std::ios::beg);
					Function = Shader->GetBufferPointer();
					FileBinary.read((char*)Function, Size);
					FileBinary.close();
				}
				else {
					FileBinary.close();
					Logger::Log("ERROR: Failed to create buffer for shader binary %s", ShaderCompiledPath);
				}
			}
			else {
				Logger::Log("ERROR: Shader binary %s not found.", ShaderCompiledPath);
			}
		}

		if (!precompiled && (Compile || !Function)) {
			// compile if option was enabled or compiled version not found
			D3DXCompileShader(
				(const char*)ShaderSource->GetBufferPointer(),
				ShaderSource->GetBufferSize(),
				NULL,
				NULL,
				"main",
				ShaderProfile,
				NULL,
				&Shader,
				&Errors,
				&ConstantTable
			);
			if (Errors) Logger::Log((char*)Errors->GetBufferPointer());
			if (Shader) {
				Function = Shader->GetBufferPointer();
				std::ofstream FileBinary(ShaderCompiledPath, std::ios::out | std::ios::binary);
				FileBinary.write((const char*)Function, Shader->GetBufferSize());
				FileBinary.flush();
				FileBinary.close();

				std::ofstream FilePreprocess(ShaderPreprocessedPath, std::ios::out | std::ios::binary);
				const std::string Portable = PortableSource(ShaderSource);	// UNOFFICIAL: see PortableSource
				FilePreprocess.write(Portable.data(), Portable.size());
				FilePreprocess.flush();
				FilePreprocess.close();

				if (Template.Name == NULL)
					Logger::Log("Shader compiled: %s", ShaderCompiledPath);
				else
					Logger::Log("Shader compiled: %s using template: %s", ShaderCompiledPath, ShaderSourcePath);
			}
		}

		HRESULT get = D3DXGetShaderConstantTableEx((const DWORD*)Function, D3DXCONSTTABLE_LARGEADDRESSAWARE, &ConstantTable);

		if (FAILED(get)) {
			Logger::Log("Encountered an issue getting constant table for %s", Name);
			ReportError(get);
		}
		else {
			D3DXCONSTANTTABLE_DESC ConstantTableDesc;
			get = ConstantTable->GetDesc(&ConstantTableDesc);
			ReportError(get);

			if (FAILED(get)) {
				Logger::Log("Issues getting constants descriptions for %s", Name);
			}
			else if (Shader) {
				HRESULT createResult = E_FAIL;
				if (ShaderProfile[0] == 'v') {
					ShaderProg = new ShaderRecordVertex(Name);
					// UNOFFICIAL 2026-10-27 (NVR DXVK 1.7-displace, core\DxvkCompute.h): with NvrDxvkDisplacement on at load, the land's
					// vertex shader is registered with its tessellation control shader. Off, or no NVR DXVK: nothing happens.
					DxvkCompute::RegisterDisplace(Name, Template.Name, Template.Defines, true, Function, D3DXGetShaderSize((const DWORD*)Function));
					createResult = TheRenderManager->device->CreateVertexShader((const DWORD*)Function, &((ShaderRecordVertex*)ShaderProg)->ShaderHandle);
				}
				else {
					ShaderProg = new ShaderRecordPixel(Name);
					// UNOFFICIAL 2026-10-24 (NVR DXVK phase 2, core\DxvkCompute.h): a Compute\Replace\<Name>.spv is registered before the
					// device sees the bytecode, so DXVK binds it in this shader's place. No file, no extension: nothing happens.
					DxvkCompute::RegisterReplacement(Name, Function, D3DXGetShaderSize((const DWORD*)Function), Template.Name ? Template.Name : Name, Macros);
					// UNOFFICIAL 2026-10-27 (NVR DXVK 1.7-displace): the land's pixel shaders with their tessellation evaluation shaders.
					DxvkCompute::RegisterDisplace(Name, Template.Name, Template.Defines, false, Function, D3DXGetShaderSize((const DWORD*)Function));
					createResult = TheRenderManager->device->CreatePixelShader((const DWORD*)Function, &((ShaderRecordPixel*)ShaderProg)->ShaderHandle);
				}
				
				if (SUCCEEDED(createResult)) {
					ShaderProg->CreateCT(ShaderSource, ConstantTable);
					// UNOFFICIAL 2026-10-17 LinearPreDecode (core/LinearPreDecode.h): ObjectTemplate pixel shaders compiled with linear lighting
					// and LIN_PREDECODE_ON read their colour constants already decoded; the constant hook decodes them while one is bound.
					if (ShaderProfile[0] == 'p' && Template.Name && !strcmp(Template.Name, "ObjectTemplate")) {
						bool linear = false, predecode = false;
						for (int d = 0; d < 44 && Template.Defines[d].Name; d++) {
							if (!strcmp(Template.Defines[d].Name, "LINEAR_LIGHTING_ON")) linear = Template.Defines[d].Definition && !strcmp(Template.Defines[d].Definition, "1");
							if (!strcmp(Template.Defines[d].Name, "LIN_PREDECODE_ON")) predecode = Template.Defines[d].Definition && !strcmp(Template.Defines[d].Definition, "1");
						}
						if (linear && predecode) LinearPreDecode::Register(((ShaderRecordPixel*)ShaderProg)->ShaderHandle);
					}
					Logger::Log("Shader loaded: %s", ShaderCompiledPath);
				}
				else {
					// UNOFFICIAL 2026-10-20 (P82 tester): which file, the device's answer and the compiler's slot count, so a report says why.
					// A shader longer than the driver takes is refused (the GTX 1070 took 3,596 slots and refused 3,820; a P82 tester's
					// RTX 4070 Ti SUPER refused the same shaders).
					unsigned slots = 0;
					ID3DXBuffer* listing = NULL;
					if (SUCCEEDED(D3DXDisassembleShader((const DWORD*)Function, FALSE, NULL, &listing)) && listing) {
						const char* at = strstr((const char*)listing->GetBufferPointer(), "approximately ");
						if (at) sscanf_s(at, "approximately %u", &slots);
						listing->Release();
					}
					Logger::Log("ERROR: Failed to create DirectX shader for %s (%s): the device refused it (hr 0x%08X), %u instruction slots",
						Name, ShaderCompiledPath, (unsigned)createResult, slots);
					ReportError(createResult);
					delete ShaderProg;
					ShaderProg = nullptr;
				}
			}
		}
	}
	else {
		if (Errors) Logger::Log((char*)Errors->GetBufferPointer());
	}

	timer.LogTime("ShaderRecord::LoadShader");

	if (ShaderSource) ShaderSource->Release();
	if (Shader) Shader->Release();
	if (Errors) Errors->Release();
	return ShaderProg;
}

/**
* Creates the constants table for the Shader.
* @param ShaderSource
* @param ConstantTable
*/
void ShaderRecord::CreateCT(ID3DXBuffer* ShaderSource, ID3DXConstantTable* ConstantTable) {

	D3DXCONSTANTTABLE_DESC ConstantTableDesc;
	D3DXCONSTANT_DESC ConstantDesc;
	D3DXHANDLE Handle;
	UINT ConstantCount = 1;
	UInt32 FloatIndex = 0;
	UInt32 TextureIndex = 0;

	HRESULT get = ConstantTable->GetDesc(&ConstantTableDesc);
	if (FAILED(get)) Logger::Log("CreateCT : failed to get constants desc");
	ReportError(get);

	//Logger::Log("CreateCT: Shader %s has %i constants", Name, ConstantTableDesc.Constants);
	//Logger::Log("%s", (const char*)ShaderSource->GetBufferPointer());

	bool sunDir18 = false, terrainSky = false;	// UNOFFICIAL 2026-10-25 (LandLight.h): the game's SunDir in c18 of a land pixel shader
	for (UINT c = 0; c < ConstantTableDesc.Constants; c++) {
		Handle = ConstantTable->GetConstant(NULL, c);
		ConstantTable->GetConstantDesc(Handle, &ConstantDesc, &ConstantCount);
		if (!strcmp(ConstantDesc.Name, "SunDir") && ConstantDesc.RegisterSet == D3DXRS_FLOAT4 && ConstantDesc.RegisterIndex == 18) sunDir18 = true;
		if (!strcmp(ConstantDesc.Name, "TESR_TerrainSkyData")) terrainSky = true;
		if (memcmp(ConstantDesc.Name, "TESR_", 5)) continue; // Only treat constants named with TESR prefix
		if (ConstantDesc.RegisterSet == D3DXRS_FLOAT4) FloatShaderValuesCount += 1;
		if (ConstantDesc.RegisterSet == D3DXRS_SAMPLER) TextureShaderValuesCount += 1;
	}

	IsLandLight = sunDir18 && terrainSky && Name && strstr(Name, ".pso");

	auto timer = TimeLogger();
	if (FloatShaderValuesCount) FloatShaderValues = new ShaderFloatValue[FloatShaderValuesCount];
	if (TextureShaderValuesCount) TextureShaderValues = new ShaderTextureValue[TextureShaderValuesCount];

	// Should be better but still crashes with NVHR -- TODO: Check how to fix it
	//if (FloatShaderValuesCount) FloatShaderValues = (ShaderValue*)Pointers::Functions::FormMemoryAlloc(FloatShaderValuesCount * sizeof(ShaderValue));
	//if (TextureShaderValuesCount) TextureShaderValues = (ShaderValue*)Pointers::Functions::FormMemoryAlloc(TextureShaderValuesCount * sizeof(ShaderValue));

	timer.LogTime("ShaderRecord::createCT Malloc");

	for (UINT c = 0; c < ConstantTableDesc.Constants; c++) {
		Handle = ConstantTable->GetConstant(NULL, c);
		ConstantTable->GetConstantDesc(Handle, &ConstantDesc, &ConstantCount);
		if (memcmp(ConstantDesc.Name, "TESR_", 5)) continue; // Only treat constants named with TESR prefix

		switch (ConstantDesc.RegisterSet) {
		case D3DXRS_FLOAT4:
			FloatShaderValues[FloatIndex].Name = ConstantDesc.Name;
			FloatShaderValues[FloatIndex].Type = ConstantDesc.Type;
			FloatShaderValues[FloatIndex].RegisterIndex = ConstantDesc.RegisterIndex;
			FloatShaderValues[FloatIndex].RegisterCount = ConstantDesc.RegisterCount;
			FloatIndex++;
			break;
		case D3DXRS_SAMPLER:
			TextureShaderValues[TextureIndex].Name = ConstantDesc.Name;
			TextureShaderValues[TextureIndex].Type = TextureRecord::GetTextureType(ConstantDesc.Type);
			TextureShaderValues[TextureIndex].RegisterIndex = ConstantDesc.RegisterIndex;
			TextureShaderValues[TextureIndex].RegisterCount = 1;
			TextureShaderValues[TextureIndex].GetSamplerStateString(ShaderSource, ConstantDesc.RegisterIndex);
			TextureShaderValues[TextureIndex].GetTextureRecord();

			// mark this shader as needing to render depth/a buffer of the scene before the object can be rendered
			if (!memcmp(ConstantDesc.Name, "TESR_DepthBuffer", 17)) HasDepthBuffer = true;
			if (!memcmp(ConstantDesc.Name, "TESR_RenderedBuffer", 20)) HasRenderedBuffer = true;
			if (!strcmp(ConstantDesc.Name, "TESR_TerrainBlendDepth")) HasTerrainBlendDepth = true;	// UNOFFICIAL terrain blending
	
			TextureIndex++;
			break;
		default:
			Logger::Log("Found unsupported constant type in shader constant table: %s : %s", ConstantDesc.Name, ConstantDesc.RegisterSet);
			break;
		}
	}

	timer.LogTime("ShaderRecord::createCT Done");
}


// extract info about texture from the shader source code
void ShaderTextureValue::GetSamplerStateString(ID3DXBuffer* ShaderSource, UINT32 Index) {
	std::string Source = std::string((const char*)ShaderSource->GetBufferPointer());

	size_t SamplerPos = Source.find(("register ( s" + std::to_string(Index) + " )"));
	if (SamplerPos == std::string::npos) {
		Logger::Log("[ERROR] %s  cannot be binded: sampler index not found", Name);
		return;
	}

	size_t SamplerEnd = Source.find("\n", SamplerPos + 1);
	std::string SamplerLine = Source.substr(SamplerPos, SamplerEnd - SamplerPos);

	//Only these samplers are bindable to an arbitrary texture
	size_t StartTexture = SamplerLine.find("<");
	size_t EndTexture = SamplerLine.rfind(">");
	if (StartTexture != std::string::npos && EndTexture != std::string::npos) {
		std::string TextureString = SamplerLine.substr(StartTexture + 1, EndTexture - StartTexture - 1);

		// find texture path
		if (TextureString.find("ResourceName") != std::string::npos) {
			size_t StartPath = TextureString.find("\"");
			size_t EndPath = TextureString.rfind("\"");
			std::string PathS;
			PathS = TextureString.substr(StartPath + 1, EndPath - 1 - StartPath);
			PathS.insert(0, "Data\\Textures\\");
			TexturePath = PathS;
		}
	}

	// UNOFFICIAL 2026-10-19: the state block may run over several lines ('= sampler_state {' on the declaration's line, the states
	// and '};' below, as GunFX's VolumetricSmoke and BarrelHaze write them). Only the declaration's own line was searched, so those
	// samplers logged '[ERROR] ... Samplerstate description not found' at every start and silently ran with the default states
	// (WRAP, POINT) instead of their own (CLAMP, LINEAR for the half and quarter resolution smoke buffers).
	size_t StartStatePos = SamplerLine.find("{");
	size_t EndStatePos = SamplerLine.rfind("}");
	if (StartStatePos != std::string::npos && EndStatePos == std::string::npos) {
		const size_t blockStart = SamplerPos + StartStatePos;
		const size_t blockEnd = Source.find("}", blockStart + 1);
		const size_t nextDeclaration = Source.find("register", blockStart + 1);
		if (blockEnd != std::string::npos && (nextDeclaration == std::string::npos || blockEnd < nextDeclaration)) {
			std::string block = Source.substr(blockStart + 1, blockEnd - blockStart - 1);
			for (char& c : block) if (c == '\r' || c == '\n' || c == '\t') c = ' ';
			SamplerString = block;
			return;
		}
	}
	if (EndStatePos == std::string::npos || StartStatePos == std::string::npos) {
		Logger::Log("[ERROR] during binding of %s : Samplerstate description not found", Name);
		return;
	}
	SamplerString = SamplerLine.substr(StartStatePos + 1, EndStatePos - StartStatePos - 1);
}


/*
* Gets a texture record with information from the Sampler and source
*/
void ShaderTextureValue::GetTextureRecord() {
	auto timer = TimeLogger();

	//Logger::Log("Loading texture %s (type:%i) (path: %s)", Name, Type, TexturePath);

	if (!Type) {
		Logger::Log("[ERROR] Sampler %s doesn't have a valid type", Name);
		return;
	}

	Texture = new TextureRecord();

	// preload file textures, game textures will get bind during constant table setting
	if (TexturePath != "") Texture->Texture = TheTextureManager->GetFileTexture(TexturePath, Type);

	// override default sampler states if the sampler string was found
	if (!SamplerString.empty()) Texture->GetSamplerStates(trim(SamplerString));

	timer.LogTime("ShaderTextureValue::GetTextureRecord");
}


/*
* Associates a found shader constant name to a D3DXVECTOR4 pointer from the ConstantsTable.
*/
void ShaderFloatValue::GetValueFromConstantTable() {
	std::string constantName = Name;
	std::map<std::string, D3DXVECTOR4*>::iterator iter = TheShaderManager->ConstantsTable.find(constantName);

	if (iter != TheShaderManager->ConstantsTable.end()) {
		Value = TheShaderManager->ConstantsTable.at(Name);
	}
	else {
		Logger::Log("Custom constant found: %s", Name);
		D3DXVECTOR4 v; v.x = v.y = v.z = v.w = 0.0f;
		TheShaderManager->CustomConst[Name] = v;
		Value = &TheShaderManager->CustomConst[Name];
	}
}


/*
* Sets the Constant Table for the shader
*/
void ShaderRecord::SetCT() {

	// UNOFFICIAL: the first use of a game shader this session goes on the warm-up list (ShaderWarmUp.h)
	if (!UsedOnce) { UsedOnce = true; ShaderWarmUp::NoteFirstUse(Name); }

	if (HasRenderedBuffer) TheRenderManager->device->StretchRect(TheRenderManager->currentRTGroup->RenderTargets[0]->data->Surface, NULL, TheTextureManager->RenderedSurface, NULL, D3DTEXF_NONE);
	if (HasDepthBuffer) {
		//Logger::Log("Resolving depth buffer for shader %s", Name);
		TheRenderManager->ResolveDepthBuffer(TheTextureManager->DepthTexture);
	}
	// UNOFFICIAL terrain blending: before the constants below are uploaded (it sets whether this draw blends), and the
	// first land bind of the world scene copies the depth drawn so far.
	if (HasTerrainBlendDepth && TheShaderManager->Shaders.Terrain) TheShaderManager->Shaders.Terrain->PrepareBlending();
	// UNOFFICIAL 2026-10-25 (LandLight.h): the light vector the last land draw lit with, then this bind's own turn.
	if (LandLight::Pending) LandLight::Read();
	if (IsLandLight) LandLight::Pending = true;

	// reset samplers
	if (ClearSamplers) {
		for (int i = 0; i < 16; i++)
			TheRenderManager->renderState->SetTexture(i, nullptr);
	}
	
	// binds textures
	ShaderTextureValue* Sampler;
	for (UInt32 c = 0; c < TextureShaderValuesCount; c++) {
		Sampler = &TextureShaderValues[c];
		if (Sampler->Texture->Texture == nullptr) {
			// UNOFFICIAL 2026-10-17: a texture that is registered but empty for now (TESR_ReflectionsEnv while the Reflections
			// cube is off -- the shader never reads it then) is re-read from its slot, not looked up by name on every draw.
			if (Sampler->Texture->TextureRef) Sampler->Texture->Texture = *Sampler->Texture->TextureRef;
			else Sampler->Texture->BindTexture(Sampler->Name);

			if (Sampler->Texture->Texture) Logger::Log("%s : Texture %s Successfully bound", Name, Sampler->Name);
			//else Logger::Log("[ERROR] : Could not bind texture %s for shader %s", Sampler->Name, Name);
		}

		if (Sampler->Texture->Texture != nullptr) {
			TheRenderManager->renderState->SetTexture(Sampler->RegisterIndex, Sampler->Texture->Texture);

			for (int i = 1; i < SamplerStatesMax; i++) {
				if (TheRenderManager->renderState->GetSamplerState(Sampler->RegisterIndex, (D3DSAMPLERSTATETYPE)i) != Sampler->Texture->SamplerStates[i])
					TheRenderManager->SetSamplerState(Sampler->RegisterIndex, (D3DSAMPLERSTATETYPE)i, Sampler->Texture->SamplerStates[i]);
			}
		}
		else {
			// UNOFFICIAL 2026-10-17: once per shader and texture, not on every draw (each line is flushed to disk: a player's
			// log reached 450 MB, 1.5 million lines of TESR_ReflectionsEnv in 47 minutes; 95% of our own test logs).
			static std::unordered_set<const void*> warned;
			if (warned.insert(Sampler).second)
				Logger::Log("%s : Texture %s was not bound (said once; it binds by itself if it appears later)", Name, Sampler->Name);
		}
	}

	// update constants
	ShaderFloatValue* Constant;
	for (UInt32 c = 0; c < FloatShaderValuesCount; c++) {
		Constant = &FloatShaderValues[c];

		if (Constant->Value == nullptr) Constant->GetValueFromConstantTable();
		if (Constant->Value == nullptr) {
			Logger::Log("[Error] %s : Couldn't get value for Constant %s", Name, Constant->Name);
			continue;
		}

		SetShaderConstantF(Constant->RegisterIndex, Constant->Value, Constant->RegisterCount); // TODO: for matrices, rows and columns are inverted because of the way this works.
	}
}


/*
* Hook - Function to replace shader handles during pass initialisation. 
*/
void NiD3DVertexShaderEx::SetupShader(IDirect3DVertexShader9* CurrentVertexHandle) {

	if ((!Enabled || !TheSettingManager->SettingsMain.Main.RenderEffects) && ShaderHandleBackup) {
		ShaderHandle = (IDirect3DVertexShader9*)ShaderHandleBackup;
		return;
	}

	// UNOFFICIAL 2026-10-22 MarkShaders, vanilla mode (test only): the vertex shaders SLS<VsFrom>..SLS<VsTo> as the game's own.
	if ((MarkShadersVanilla == 1 || MarkShadersVanilla == 3) && MarkShadersVsTo > 0 && ShaderHandleBackup && Name && !memcmp(Name, "SLS", 3)) {
		const int number = atoi(Name + 3);
		if (number >= MarkShadersVsFrom && number <= MarkShadersVsTo) {
			ShaderHandle = (IDirect3DVertexShader9*)ShaderHandleBackup;
			return;
		}
	}

	if (GetShaderRecord(ShaderRecordType::Exterior) && Player->GetWorldSpace()) {
		ShaderHandle = GetShaderRecord(ShaderRecordType::Exterior)->ShaderHandle;
		if (CurrentVertexHandle != ShaderHandle) GetShaderRecord(ShaderRecordType::Exterior)->SetCT();
	}
	else if (GetShaderRecord(ShaderRecordType::Interior) && !Player->GetWorldSpace()) {
		ShaderHandle = GetShaderRecord(ShaderRecordType::Interior)->ShaderHandle;
		if (CurrentVertexHandle != ShaderHandle) GetShaderRecord(ShaderRecordType::Interior)->SetCT();
	}
	else if (GetShaderRecord(ShaderRecordType::Default)) {
		ShaderHandle = GetShaderRecord(ShaderRecordType::Default)->ShaderHandle;
		if (CurrentVertexHandle != ShaderHandle) GetShaderRecord(ShaderRecordType::Default)->SetCT();
	}
	else if (ShaderHandleBackup) {
		ShaderHandle = (IDirect3DVertexShader9*)ShaderHandleBackup;
	}

}

void NiD3DVertexShaderEx::DisposeShader() {
	for (UInt32 i = 0; i < 3; i++) {
		if (ShaderProg[i]) {
			delete ShaderProg[i];
			ShaderProg[i] = NULL;
		}
	}
}

ShaderRecordVertex* NiD3DVertexShaderEx::GetShaderRecord(ShaderRecordType Type) {
	return (ShaderRecordVertex*)NiD3DShaderProgram::GetShaderRecord(Type);
}

void __fastcall NiD3DVertexShaderEx::Free(NiD3DVertexShaderEx* shader) {
	shader->DisposeShader();
	ShaderCollection* Collection = TheShaderManager->GetShaderCollection(shader->Name);
	if (Collection) {
		std::vector<NiD3DVertexShaderEx*>* pList = &Collection->VertexShaderList;
		pList->erase(std::remove(pList->begin(), pList->end(), shader), pList->end());
	}
	ThisCall(0xE89B70, shader);
}


/*
* Hook - Function to replace shader handles during pass initialisation.
*/
void NiD3DPixelShaderEx::SetupShader(IDirect3DPixelShader9* CurrentPixelHandle) {

	// UNOFFICIAL 2026-10-17 grass study, diagnostic (only while the F10 GPU profile is on, a few lines a run): the device's
	// states when a grass pixel shader is set up, to know how the game draws grass (alpha test, blend, depth, culling, coverage)
	// before its shader is changed. Read only.
	if (GpuTimer::Enabled && Name && !memcmp(Name, "GRASS", 5)) {
		static unsigned binds = 0;
		if (binds < 8 || (binds % 5000 == 0 && binds <= 50000)) {
			IDirect3DDevice9* d = TheRenderManager->device;
			DWORD s[13] = {};
			const D3DRENDERSTATETYPE k[13] = { D3DRS_ALPHATESTENABLE, D3DRS_ALPHAREF, D3DRS_ALPHAFUNC, D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND,
				D3DRS_DESTBLEND, D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ZFUNC, D3DRS_CULLMODE, D3DRS_COLORWRITEENABLE, D3DRS_ADAPTIVETESS_Y,
				D3DRS_MULTISAMPLEANTIALIAS };
			for (int i = 0; i < 13; i++) d->GetRenderState(k[i], &s[i]);
			Logger::Log("GRASS PROBE bind %u %s: alphatest %u ref %u func %u | blend %u src %u dst %u | z %u zwrite %u zfunc %u | cull %u colorwrite 0x%X | atoc 0x%08X msaa %u",
				binds, Name, s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7], s[8], s[9], s[10], s[11], s[12]);
		}
		binds++;
	}

	if (!Enabled || !TheSettingManager->SettingsMain.Main.RenderEffects) {
		ShaderHandle = (IDirect3DPixelShader9*)ShaderHandleBackup;
		return;
	}

	// UNOFFICIAL 2026-10-22 MarkShaders (test only, [Shaders.PBR.Main] MarkShadersFrom/To, read each frame in UpdateConstants): the
	// pixel shaders SLS<from>..SLS<to> draw flat magenta, to see which shader draws a surface. ps_2_0, so any vertex shader fits.
	// Both 0 = off (the default): nothing below changes.
	if (MarkShadersTo > 0) {
		static int logged = 0;
		if (logged < 40) { logged++; Logger::Log("MarkShaders: pixel shader '%s' (range %d-%d)", Name ? Name : "(null)", MarkShadersFrom, MarkShadersTo); }
	}
	// MarkShadersFamily: 0 the SLS shaders, 1 the parallax (PAR) ones, 2 every other pixel shader (the range is ignored).
	const bool isSLS = Name && !memcmp(Name, "SLS", 3), isPAR = Name && !memcmp(Name, "PAR", 3);
	if (MarkShadersTo > 0 && Name && (MarkShadersFamily == 2 ? !isSLS && !isPAR : MarkShadersFamily == 1 ? isPAR : isSLS)) {
		const int number = atoi(Name + 3);
		if (MarkShadersFamily == 2 || (number >= MarkShadersFrom && number <= MarkShadersTo)) {
			static IDirect3DPixelShader9* magenta = nullptr;
			static bool tried = false;
			if (!tried) {
				tried = true;
				static const char src[] = "float4 main() : COLOR0 { return float4(1, 0, 1, 1); }";
				ID3DXBuffer* code = nullptr;
				if (SUCCEEDED(D3DXCompileShader(src, sizeof(src) - 1, NULL, NULL, "main", "ps_2_0", 0, &code, NULL, NULL)) && code) {
					TheRenderManager->device->CreatePixelShader((const DWORD*)code->GetBufferPointer(), &magenta);
					code->Release();
				}
				Logger::Log("MarkShaders: magenta shader %s", magenta ? "ready" : "FAILED");
			}
			if (MarkShadersVanilla == 1) {	// the game's own shader instead (with MarkShadersVsFrom/To for the vertex shaders that feed it)
				ShaderHandle = (IDirect3DPixelShader9*)ShaderHandleBackup;
				return;
			}
			if (magenta && MarkShadersVanilla != 3) {	// 3: the vertex shaders only (the pixel shaders stay NVR's)
				ShaderHandle = magenta;
				return;
			}
		}
	}

	if (GetShaderRecord(ShaderRecordType::Exterior) && Player->GetWorldSpace()) {
		ShaderHandle = GetShaderRecord(ShaderRecordType::Exterior)->ShaderHandle;
		if (CurrentPixelHandle != ShaderHandle) GetShaderRecord(ShaderRecordType::Exterior)->SetCT();
	}
	else if (GetShaderRecord(ShaderRecordType::Interior) && !Player->GetWorldSpace()) {
		ShaderHandle = GetShaderRecord(ShaderRecordType::Interior)->ShaderHandle;
		if (CurrentPixelHandle != ShaderHandle) GetShaderRecord(ShaderRecordType::Interior)->SetCT();
	}
	else if (GetShaderRecord(ShaderRecordType::Default)) {
		ShaderHandle = GetShaderRecord(ShaderRecordType::Default)->ShaderHandle;
		if (CurrentPixelHandle != ShaderHandle) GetShaderRecord(ShaderRecordType::Default)->SetCT();
	}
	else {
		ShaderHandle = (IDirect3DPixelShader9*)ShaderHandleBackup;
	}
}


void NiD3DPixelShaderEx::DisposeShader() {
	for (UInt32 i = 0; i < 3; i++) {
		if (ShaderProg[i]) {
			delete ShaderProg[i];
			ShaderProg[i] = NULL;
		}
	}
}

ShaderRecordPixel* NiD3DPixelShaderEx::GetShaderRecord(ShaderRecordType Type) {
	return (ShaderRecordPixel*)NiD3DShaderProgram::GetShaderRecord(Type);
}

void __fastcall NiD3DPixelShaderEx::Free(NiD3DPixelShaderEx* shader) {
	shader->DisposeShader();
	ShaderCollection* Collection = TheShaderManager->GetShaderCollection(shader->Name);
	if (Collection) {
		std::vector<NiD3DPixelShaderEx*>* pList = &Collection->PixelShaderList;
		pList->erase(std::remove(pList->begin(), pList->end(), shader), pList->end());
	}
	ThisCall(0xE89970, shader);
}

ShaderRecordVertex::ShaderRecordVertex(const char* shaderName) {

	Name = shaderName;
	ShaderHandle = NULL;
	ClearSamplers = false;

}

ShaderRecordVertex::~ShaderRecordVertex() {

	// UNOFFICIAL 2026-10-13: none when the driver refused the shader (LoadShader deletes the record then): releasing
	// nothing crashed the game at startup instead of falling back to the game's own shader.
	if (ShaderHandle) ShaderHandle->Release();

}

ShaderRecordPixel::ShaderRecordPixel(const char* shaderName) {
	Name = shaderName;
	ShaderHandle = NULL;
	ClearSamplers = true;

}

ShaderRecordPixel::~ShaderRecordPixel() {

	// UNOFFICIAL 2026-10-13: none when the driver refused the shader (LoadShader deletes the record then): releasing
	// nothing crashed the game at startup instead of falling back to the game's own shader.
	LinearPreDecode::Unregister(ShaderHandle);	// UNOFFICIAL 2026-10-17 LinearPreDecode
	if (ShaderHandle) ShaderHandle->Release();

}


void ShaderRecordVertex::SetShaderConstantF(UInt32 RegisterIndex, D3DXVECTOR4* Value, UInt32 RegisterCount) {

	TheRenderManager->device->SetVertexShaderConstantF(RegisterIndex, (const float*)Value, RegisterCount);

}

void ShaderRecordPixel::SetShaderConstantF(UInt32 RegisterIndex, D3DXVECTOR4* Value, UInt32 RegisterCount) {

	TheRenderManager->device->SetPixelShaderConstantF(RegisterIndex, (const float*)Value, RegisterCount);

}
