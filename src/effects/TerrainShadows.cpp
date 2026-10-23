#include "TerrainShadows.h"
#include "LandHeights.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

static const char* TerrainShadowsSection = "Shaders.ShadowsExteriors.TerrainShadows";

void TerrainShadowMap::UpdateSettings() {
	const UINT OldResolution = Settings.MaxResolution;
	const float OldBias = Settings.Bias, OldSoftness = Settings.Softness;
	Settings.Enabled = TheSettingManager->GetSettingI(TerrainShadowsSection, "Enabled") != 0;
	Settings.MaxResolution = 1024u << std::clamp(TheSettingManager->GetSettingI(TerrainShadowsSection, "MaxResolution"), 0, 2);
	Settings.Bias = std::clamp(TheSettingManager->GetSettingF(TerrainShadowsSection, "Bias"), 0.0f, 4096.0f);
	Settings.Softness = std::clamp(TheSettingManager->GetSettingF(TerrainShadowsSection, "Softness"), 0.1f, 10.0f);
	Settings.Strength = std::clamp(TheSettingManager->GetSettingF(TerrainShadowsSection, "Strength"), 0.0f, 1.0f);
	Settings.Darkness = std::clamp(TheSettingManager->GetSettingF(TerrainShadowsSection, "Darkness"), 0.0f, 0.95f);
	Settings.Contrast = std::clamp(TheSettingManager->GetSettingF(TerrainShadowsSection, "Contrast"), 0.5f, 8.0f);
	Settings.DebugView = std::clamp(TheSettingManager->GetSettingI(TerrainShadowsSection, "DebugView"), 0, 2);
	Settings.UseHeightMapFile = TheSettingManager->GetSettingI(TerrainShadowsSection, "UseHeightMapFile") != 0;
	if (Settings.MaxResolution != OldResolution || Settings.Bias != OldBias || Settings.Softness != OldSoftness)
		SettingsChangedAt = GetTickCount64();
}

void TerrainShadowMap::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_TerrainShadowData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_TerrainShadowRange", &Constants.Range);
	TheShaderManager->RegisterConstant("TESR_TerrainShadowParams", &Constants.Params);
}

void TerrainShadowMap::RegisterTextures() {
	TheTextureManager->RegisterTexture("TESR_TerrainShadowBuffer", (IDirect3DBaseTexture9**)&Texture);
	TheTextureManager->RegisterTexture("TESR_TerrainShadowGround", (IDirect3DBaseTexture9**)&GroundTexture);
}

void TerrainShadowMap::ReleaseTexture() {
	if (Texture) {
		TheShaderManager->ClearShaderSamplers("TESR_TerrainShadowBuffer", sizeof("TESR_TerrainShadowBuffer"));
		Texture->Release();
		Texture = nullptr;
	}
	if (GroundTexture) {
		TheShaderManager->ClearShaderSamplers("TESR_TerrainShadowGround", sizeof("TESR_TerrainShadowGround"));
		GroundTexture->Release();
		GroundTexture = nullptr;
	}
	TextureWidth = TextureHeight = 0;
	Uploaded = false;
}

static bool SameText(const std::string& a, const char* b) {
	size_t n = strlen(b);
	if (a.size() != n) return false;
	for (size_t i = 0; i < n; i++)
		if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
	return true;
}

bool TerrainShadowMap::LoadHeightMapFile(const LoadRequest& Request, HeightMap& Out) {
	namespace fs = std::filesystem;
	const std::string& Name = Request.Name;
	const UINT MaxResolution = Request.MaxResolution;
	if (Name.empty()) { Out.Message = "no editor id to find an xLODGen height map by"; return false; }
	const fs::path Folder = fs::path("Data\\Textures\\Terrain") / Name;
	std::error_code Error;
	fs::path File;
	int Fields[6] = {};
	bool CSNaming = false;
	fs::file_time_type Newest;
	for (const fs::directory_entry& Entry : fs::directory_iterator(Folder, Error)) {
		if (!Entry.is_regular_file(Error) || !SameText(Entry.path().extension().string(), ".dds")) continue;
		std::vector<std::string> Parts;
		std::string Stem = Entry.path().stem().string();
		for (size_t Start = 0;;) {
			size_t Dot = Stem.find('.', Start);
			Parts.push_back(Stem.substr(Start, Dot == std::string::npos ? std::string::npos : Dot - Start));
			if (Dot == std::string::npos) break;
			Start = Dot + 1;
		}
		size_t k = 1;
		while (k < Parts.size() && !SameText(Parts[k], "HeightMap")) k++;
		if (k >= Parts.size() || Parts.size() != k + 7 || !SameText(Parts[0], Name.c_str())) continue;
		int Values[6] = {};
		bool Parsed = true;
		for (int i = 0; i < 6 && Parsed; i++) {
			char* End = nullptr;
			Values[i] = (int)strtol(Parts[k + 1 + i].c_str(), &End, 10);
			Parsed = End && *End == '\0' && End != Parts[k + 1 + i].c_str();
		}
		if (!Parsed) continue;
		const fs::file_time_type Written = Entry.last_write_time(Error);
		if (!File.empty() && Written <= Newest) continue;
		Newest = Written;
		memcpy(Fields, Values, sizeof(Fields));
		File = Entry.path();
		CSNaming = k == 2 && SameText(Parts[1], "Terrain");
	}
	if (File.empty()) {
		Out.Message = "no xLODGen height map in " + Folder.string();
		return false;
	}

	std::ifstream Stream(File, std::ios::binary);
	unsigned char Header[148] = {};
	Stream.read((char*)Header, 128);
	auto ReadU32 = [&](size_t Offset) { UINT32 v; memcpy(&v, &Header[Offset], 4); return v; };
	if (!Stream || memcmp(Header, "DDS ", 4)) {
		Out.Message = File.string() + " is not a DDS file";
		return false;
	}
	const UINT Height = ReadU32(12), Width = ReadU32(16);
	const UINT32 PixelFlags = ReadU32(80), FourCC = ReadU32(84), Bits = ReadU32(88), RedMask = ReadU32(92);
	bool Is16 = false;
	if (FourCC == MAKEFOURCC('D', 'X', '1', '0')) {
		Stream.read((char*)Header + 128, 20);
		const UINT32 Format = ReadU32(128);
		Is16 = Stream && (Format == 56 || Format == 57);
	}
	else if (FourCC == 81) Is16 = true;
	else if (!(PixelFlags & 0x4) && Bits == 16 && RedMask == 0xFFFF) Is16 = true;
	if (!Is16 || !Width || !Height || Width > 16384 || Height > 16384) {
		Out.Message = File.string() + " is not a 16-bit single-channel DDS";
		return false;
	}

	auto Fits = [&](int West, int South, int East, int North) {
		return East >= West && North >= South && (UINT)(East - West + 1) * 32 == Width && (UINT)(North - South + 1) * 32 == Height;
	};
	int West, South, East, North;
	const int* F = Fields;
	if (CSNaming && Fits(F[0], F[1], F[2], F[3])) { West = F[0]; South = F[1]; East = F[2]; North = F[3]; }
	else if (Fits(F[1], F[0], F[3], F[2])) { West = F[1]; South = F[0]; East = F[3]; North = F[2]; }
	else if (Fits(F[0], F[1], F[2], F[3])) { West = F[0]; South = F[1]; East = F[2]; North = F[3]; }
	else {
		Out.Message = "the cells in " + File.filename().string() + " do not match its " + std::to_string(Width) + "x" + std::to_string(Height) + " size (32 texels a cell)";
		return false;
	}

	const UINT Longer = Width > Height ? Width : Height;
	const UINT Factor = Longer > MaxResolution ? (Longer + MaxResolution - 1) / MaxResolution : 1;
	Out.Width = (Width + Factor - 1) / Factor;
	Out.Height = (Height + Factor - 1) / Factor;
	const float NoLand = -1.0e30f;
	Out.Heights.assign((size_t)Out.Width * Out.Height, NoLand);
	Out.Peaks.assign((size_t)Out.Width * Out.Height, NoLand);
	std::vector<UINT16> Row(Width);
	std::vector<float> Sum(Out.Width), Peak(Out.Width);
	std::vector<UINT> Count(Out.Width);
	float Low = FLT_MAX, High = -FLT_MAX;
	for (UINT r = 0; r < Out.Height; r++) {
		std::fill(Sum.begin(), Sum.end(), 0.0f);
		std::fill(Peak.begin(), Peak.end(), NoLand);
		std::fill(Count.begin(), Count.end(), 0u);
		const UINT EndY = (r + 1) * Factor < Height ? (r + 1) * Factor : Height;
		for (UINT y = r * Factor; y < EndY; y++) {
			if (!Stream.read((char*)Row.data(), (std::streamsize)Width * 2)) {
				Out.Heights.clear();
				Out.Peaks.clear();
				Out.Width = Out.Height = 0;
				Out.Message = File.string() + " is shorter than its header says";
				return false;
			}
			for (UINT x = 0; x < Width; x++) {
				if (!Row[x]) continue;
				const float h = ((float)Row[x] - 32767.0f) * 8.0f;
				Sum[x / Factor] += h;
				Peak[x / Factor] = (std::max)(Peak[x / Factor], h);
				Count[x / Factor]++;
			}
		}
		for (UINT c = 0; c < Out.Width; c++) {
			if (!Count[c]) continue;
			const float h = Sum[c] / (float)Count[c];
			Out.Heights[(size_t)r * Out.Width + c] = h;
			Out.Peaks[(size_t)r * Out.Width + c] = Peak[c];
			Low = (std::min)(Low, h);
			High = (std::max)(High, h);
		}
	}
	if (Low > High) {
		Out.Heights.clear();
		Out.Peaks.clear();
		Out.Width = Out.Height = 0;
		Out.Message = File.string() + " holds no land";
		return false;
	}
	for (float& h : Out.Heights)
		if (h == NoLand) h = Low;
	for (float& h : Out.Peaks)
		if (h == NoLand) h = Low;

	Out.Texel = 128.0f * Factor;
	Out.West = West * 4096.0f + 0.5f * (Factor - 1) * 128.0f;
	Out.North = (North + 1) * 4096.0f - 128.0f - 0.5f * (Factor - 1) * 128.0f;

	char Text[600];
	sprintf_s(Text, "UNOFFICIAL terrain shadows: xLODGen height map %s, %ux%u (cells %d,%d to %d,%d), land heights %.0f to %.0f, used at %ux%u (%.0f units a texel)",
		File.string().c_str(), Width, Height, West, South, East, North, Low, High, Out.Width, Out.Height, Out.Texel);
	Out.Message = Text;
	return true;
}

TerrainShadowMap::HeightMap TerrainShadowMap::Load(LoadRequest Request) {
	HeightMap Out;
	Out.WorldSpace = Request.WorldSpace;
	try {
		std::string FileMessage;
		if (Request.UseHeightMapFile) {
			if (LoadHeightMapFile(Request, Out)) return Out;
			FileMessage = Out.Message;
			Out = HeightMap();
			Out.WorldSpace = Request.WorldSpace;
		}
		std::vector<LandHeights::Plugin> Plugins;
		Plugins.reserve(Request.Plugins.size());
		for (const std::string& Name : Request.Plugins) Plugins.push_back({ Name });
		LandHeights::Result Land;
		const auto Start = std::chrono::steady_clock::now();
		const bool Read = LandHeights::Read(Plugins, Request.DefiningFile, Request.ObjectID, Request.MaxResolution, Land);
		const double Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count();
		char Text[200];
		sprintf_s(Text, "UNOFFICIAL terrain shadows: %s (%s %06X), the land read from the plugins in %.1f s: ",
			Request.Name.empty() ? "worldspace" : Request.Name.c_str(), Request.DefiningFile.c_str(), Request.ObjectID, Seconds);
		Out.Message = Text + Land.Message + (FileMessage.empty() ? "" : " (" + FileMessage + ")");
		if (!Read) return Out;
		Out.Heights.swap(Land.Heights);
		Out.Peaks.swap(Land.Peaks);
		Out.Width = Land.Width;
		Out.Height = Land.Height;
		Out.West = Land.West;
		Out.North = Land.North;
		Out.Texel = Land.Texel;
	}
	catch (const std::exception& e) {
		Out.Heights.clear();
		Out.Heights.shrink_to_fit();
		Out.Peaks.clear();
		Out.Peaks.shrink_to_fit();
		Out.Width = Out.Height = 0;
		Out.Message = std::string("[ERROR] Terrain shadows: loading the land heights for ") + Request.Name + " failed (" + e.what() + ")";
	}
	return Out;
}

TerrainShadowMap::SweepResult TerrainShadowMap::Sweep(std::shared_ptr<const HeightMap> Map, std::vector<UINT32>* Packed, D3DXVECTOR3 SunDir, float Softness, float Bias, UINT Generation) {
	SweepResult Out;
	Out.Generation = Generation;
	Out.SunDir = SunDir;
	try {
		const std::vector<float>& H = Map->Heights;
		const std::vector<float>& P = Map->Peaks.size() == H.size() ? Map->Peaks : Map->Heights;
		const int W = (int)Map->Width, R = (int)Map->Height;
		float Low = FLT_MAX, High = -FLT_MAX;
		for (float h : H) Low = (std::min)(Low, h);
		for (float h : P) High = (std::max)(High, h);

		const float Flat = sqrtf(SunDir.x * SunDir.x + SunDir.y * SunDir.y);
		const bool Overhead = Flat <= 1e-4f;
		const float Columns = Overhead ? 1.0f : -SunDir.x / Flat, Rows = Overhead ? 0.0f : SunDir.y / Flat;
		const bool ColumnMajor = fabsf(Columns) >= fabsf(Rows);
		const float Major = ColumnMajor ? Columns : Rows, Minor = ColumnMajor ? Rows : Columns;
		const int Step = Major > 0.0f ? 1 : -1;
		const float Slope = Minor / fabsf(Major);
		const float StepLength = Map->Texel * sqrtf(1.0f + Slope * Slope);
		const int AlongCount = ColumnMajor ? W : R, AcrossCount = ColumnMajor ? R : W;

		const float Elevation = Overhead ? 1.5f : atan2f(SunDir.z, Flat);
		const float Soft = Softness * 3.14159265f / 180.0f, MaxAngle = 3.14159265f * 0.5f - 0.01f;
		const float DropUpper = StepLength * tanf(std::clamp(Elevation - Soft, 0.0f, MaxAngle));
		const float DropLower = StepLength * tanf(std::clamp(Elevation + Soft, 0.0f, MaxAngle));
		const float Blur = Overhead ? 0.0f : (std::min)(0.5f * (DropUpper + DropLower), Bias);

		Out.Low = Low - 2.0f * Bias - 16.0f;
		Out.Range = (High + 16.0f) - Out.Low;
		Out.Blur = Blur;
		const float Scale = 65535.0f / Out.Range;
		auto Encode = [&](float h) { return (UINT32)std::clamp((h - Out.Low) * Scale + 0.5f, 0.0f, 65535.0f); };

		Packed->resize(H.size());
		std::vector<float> PreviousUpper(AcrossCount), PreviousLower(AcrossCount), CurrentUpper(AcrossCount), CurrentLower(AcrossCount);
		size_t ShadowedCount = 0;
		const int First = Step > 0 ? 0 : AlongCount - 1;
		for (int n = 0; n < AlongCount; n++) {
			const int i = First + n * Step;
			for (int j = 0; j < AcrossCount; j++) {
				const size_t k = ColumnMajor ? (size_t)j * W + i : (size_t)i * W + j;
				float CastUpper = -FLT_MAX, CastLower = -FLT_MAX;
				const float From = j - Slope;
				if (n > 0 && !Overhead && From >= 0.0f && From <= (float)(AcrossCount - 1)) {
					const int j0 = (int)From;
					const int j1 = (std::min)(j0 + 1, AcrossCount - 1);
					const float t = From - j0;
					CastUpper = PreviousUpper[j0] + (PreviousUpper[j1] - PreviousUpper[j0]) * t - DropUpper;
					CastLower = PreviousLower[j0] + (PreviousLower[j1] - PreviousLower[j0]) * t - DropLower;
				}
				const float Upper = (std::max)(H[k], CastUpper), Lower = (std::max)(H[k], CastLower);
				if (H[k] < Upper - Bias - Blur) ShadowedCount++;
				CurrentUpper[j] = (std::max)(P[k], CastUpper);
				CurrentLower[j] = (std::max)(P[k], CastLower);
				(*Packed)[k] = Encode(Upper) | (Encode(Lower) << 16);
			}
			PreviousUpper.swap(CurrentUpper);
			PreviousLower.swap(CurrentLower);
		}
		Out.Shadowed = H.empty() ? 0.0f : (float)ShadowedCount / (float)H.size();
		Out.Count = H.size();
	}
	catch (const std::exception& e) {
		Out.Count = 0;
		Out.Error = std::string("[ERROR] Terrain shadows: the shadow sweep failed (") + e.what() + ")";
	}
	return Out;
}

void TerrainShadowMap::Update(IDirect3DDevice9* Device, const D3DXVECTOR3& SunDir) {
	TESWorldSpace* World = Player ? Player->GetWorldSpace() : nullptr;
	if (!Settings.Enabled || !World) {
		Constants.Data.w = 0.0f;
		if (!Settings.Enabled && !Loading && !Sweeping && (Map || Texture)) {
			Map.reset();
			ReleaseTexture();
			PendingUpload = SweepResult();
			Uploading = false;
			std::vector<UINT32>().swap(SweepBuffer);
			RequestedWorldSpace = nullptr;
			UploadedGeneration = ~0u;
		}
		return;
	}
	while (World->parent && (World->parentFlags & 1)) World = World->parent;

	const bool Settled = GetTickCount64() - SettingsChangedAt > 750;

	if (Loading && LoadJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
		HeightMap Loaded = LoadJob.get();
		Loading = false;
		Logger::Log("%s", Loaded.Message.c_str());
		Map = std::make_shared<const HeightMap>(std::move(Loaded));
		Generation++;
	}

	if (!Loading && (RequestedWorldSpace != World || ((RequestedResolution != Settings.MaxResolution || RequestedFile != Settings.UseHeightMapFile) && Settled))) {
		RequestedWorldSpace = World;
		RequestedResolution = Settings.MaxResolution;
		RequestedFile = Settings.UseHeightMapFile;
		LoadRequest Request;
		Request.WorldSpace = World;
		const char* Name = World->GetEditorName();
		Request.Name = Name ? Name : "";
		Request.ObjectID = World->refID & 0xFFFFFF;
		Request.MaxResolution = Settings.MaxResolution;
		Request.UseHeightMapFile = Settings.UseHeightMapFile;
		if (DataHandler) {
			const ModList& Mods = DataHandler->modList;
			const UInt32 Count = (std::min)(Mods.loadedModCount, (UInt32)0xFF);
			for (UInt32 i = 0; i < Count; i++)
				if (Mods.loadedMods[i]) Request.Plugins.emplace_back(Mods.loadedMods[i]->name);
			const UInt32 Index = World->refID >> 24;
			if (Index < Count && Mods.loadedMods[Index]) Request.DefiningFile = Mods.loadedMods[Index]->name;
		}
		if (Map && Map->WorldSpace != World) Map.reset();
		try {
			LoadJob = std::async(std::launch::async, &TerrainShadowMap::Load, std::move(Request));
			Loading = true;
		}
		catch (const std::exception& e) {
			Logger::Log("[ERROR] Terrain shadows: could not start loading the heights (%s)", e.what());
		}
	}

	if ((Sweeping || Uploading) && !StallLogged && GetTickCount64() - BusySince > 15000) {
		StallLogged = true;
		Logger::Log("[WARNING] Terrain shadows: the %s has run for %.0f s; the shadows will not follow the sun until it ends",
			Sweeping ? "shadow sweep" : "texture upload", (GetTickCount64() - BusySince) / 1000.0);
	}

	if (Sweeping && SweepJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
		SweepResult Result = SweepJob.get();
		Sweeping = false;
		if (!Result.Error.empty()) Logger::Log("%s", Result.Error.c_str());
		static ULONGLONG LastLog = 0;
		if (TheSettingManager->SettingsMain.Develop.DebugMode && Result.Error.empty() && GetTickCount64() - LastLog > 5000) {
			LastLog = GetTickCount64();
			const D3DXVECTOR3& L = Result.SunDir;
			const float Azimuth = atan2f(L.x, L.y) * 57.2958f, Elevation = asinf(std::clamp(L.z, -1.0f, 1.0f)) * 57.2958f;
			Logger::Log("[TerrainShadows] sweep: light from azimuth %.0f (0 north, 90 east), elevation %.1f, %s; fade %.2f, strength %.2f; land in shadow %.1f%%, blur %.0f, bias %.0f",
				Azimuth < 0.0f ? Azimuth + 360.0f : Azimuth, Elevation, TheShaderManager->GameState.dayLight > 0.5f ? "the sun" : "the moon",
				LastFade, Settings.Strength, Result.Shadowed * 100.0f, Result.Blur, SweptBias);
		}
		const bool Valid = Map && Result.Generation == Generation && Result.Count == (size_t)Map->Width * Map->Height && SweepBuffer.size() == Result.Count;
		const bool SameEncoding = Valid && Uploaded && Texture && TextureWidth == Map->Width && TextureHeight == Map->Height &&
			UploadedGeneration == Result.Generation && Constants.Range.x == Result.Low && Constants.Range.y == Result.Range;
		if (Valid && SameEncoding) {
			PendingUpload = std::move(Result);
			PendingRow = 0;
			Uploading = true;
			BusySince = GetTickCount64();
			StallLogged = false;
		}
		else if (Valid) {
			if (!Texture || TextureWidth != Map->Width || TextureHeight != Map->Height) {
				ReleaseTexture();
				GroundTwoChannels = FAILED(Device->CreateTexture(Map->Width, Map->Height, 1, 0, D3DFMT_L16, D3DPOOL_MANAGED, &GroundTexture, NULL));
				if (GroundTwoChannels && FAILED(Device->CreateTexture(Map->Width, Map->Height, 1, 0, D3DFMT_G16R16, D3DPOOL_MANAGED, &GroundTexture, NULL)))
					GroundTexture = nullptr;
				if (!GroundTexture || FAILED(Device->CreateTexture(Map->Width, Map->Height, 1, 0, D3DFMT_G16R16, D3DPOOL_MANAGED, &Texture, NULL))) {
					Logger::Log("[ERROR] Terrain shadows: could not create the %ux%u shadow height textures; turned off until the settings change", Map->Width, Map->Height);
					Texture = nullptr;
					ReleaseTexture();
					Settings.Enabled = false;
					Constants.Data.w = 0.0f;
					return;
				}
				TextureWidth = Map->Width;
				TextureHeight = Map->Height;
			}
			bool GroundDone = false;
			D3DLOCKED_RECT GroundRect;
			if (GroundTexture && SUCCEEDED(GroundTexture->LockRect(0, &GroundRect, NULL, 0))) {
				const float Scale = 65535.0f / Result.Range;
				for (UINT r = 0; r < Map->Height; r++) {
					BYTE* Row = (BYTE*)GroundRect.pBits + (size_t)r * GroundRect.Pitch;
					for (UINT c = 0; c < Map->Width; c++) {
						const UINT16 v = (UINT16)std::clamp((Map->Heights[(size_t)r * Map->Width + c] - Result.Low) * Scale + 0.5f, 0.0f, 65535.0f);
						if (GroundTwoChannels) ((UINT32*)Row)[c] = v; else ((UINT16*)Row)[c] = v;
					}
				}
				GroundTexture->UnlockRect(0);
				GroundDone = true;
			}
			else LockFailures++;
			D3DLOCKED_RECT Rect;
			if (GroundDone && SUCCEEDED(Texture->LockRect(0, &Rect, NULL, 0))) {
				for (UINT r = 0; r < Map->Height; r++)
					memcpy((BYTE*)Rect.pBits + (size_t)r * Rect.Pitch, &SweepBuffer[(size_t)r * Map->Width], (size_t)Map->Width * sizeof(UINT32));
				Texture->UnlockRect(0);
				Constants.Data = D3DXVECTOR4(Map->West - 0.5f * Map->Texel, Map->North + 0.5f * Map->Texel, 1.0f / (Map->Width * Map->Texel), 0.0f);
				Constants.Params.x = 1.0f / (Map->Height * Map->Texel);
				Constants.Range = D3DXVECTOR4(Result.Low, Result.Range, SweptBias, Result.Blur);
				Uploaded = true;
				UploadedWorldSpace = Map->WorldSpace;
				UploadedGeneration = Result.Generation;
				SweepsUploaded++;
			}
			else LockFailures++;
			Uploading = false;
		}
	}

	if (Uploading) {
		if (!Map || !Texture || PendingUpload.Generation != UploadedGeneration || SweepBuffer.size() != (size_t)TextureWidth * TextureHeight) {
			Uploading = false;
			PendingUpload = SweepResult();
		}
		else {
			const UINT Rows = (std::min)(128u, TextureHeight - PendingRow);
			RECT Strip = { 0, (LONG)PendingRow, (LONG)TextureWidth, (LONG)(PendingRow + Rows) };
			D3DLOCKED_RECT Rect;
			if (SUCCEEDED(Texture->LockRect(0, &Rect, &Strip, 0))) {
				for (UINT r = 0; r < Rows; r++)
					memcpy((BYTE*)Rect.pBits + (size_t)r * Rect.Pitch, &SweepBuffer[(size_t)(PendingRow + r) * TextureWidth], (size_t)TextureWidth * sizeof(UINT32));
				Texture->UnlockRect(0);
			}
			else LockFailures++;
			PendingRow += Rows;
			if (PendingRow >= TextureHeight) {
				Constants.Range.w = PendingUpload.Blur;
				Uploading = false;
				SweepsUploaded++;
				PendingUpload = SweepResult();
			}
		}
	}

	D3DXVECTOR3 Sun = SunDir;
	D3DXVec3Normalize(&Sun, &Sun);
	const bool NewSettings = Settled && (Settings.Bias != SweptBias || Settings.Softness != SweptSoftness);
	if (Map && !Map->Heights.empty() && !Sweeping && !Uploading && Sun.z > 0.0f &&
		(SweptGeneration != Generation || NewSettings || D3DXVec3Dot(&Sun, &SweptSunDir) < 0.99999048f)) {
		if (SweepBuffer.size() != Map->Heights.size()) std::vector<UINT32>().swap(SweepBuffer);
		try {
			SweepJob = std::async(std::launch::async, &TerrainShadowMap::Sweep, Map, &SweepBuffer, Sun, Settings.Softness, Settings.Bias, Generation);
			Sweeping = true;
			SweepsStarted++;
			BusySince = GetTickCount64();
			StallLogged = false;
		}
		catch (const std::exception& e) {
			Logger::Log("[ERROR] Terrain shadows: could not start the shadow sweep (%s)", e.what());
		}
		SweptSunDir = Sun;
		SweptGeneration = Generation;
		SweptBias = Settings.Bias;
		SweptSoftness = Settings.Softness;
	}

	Constants.Params.y = (float)Settings.DebugView;
	Constants.Params.z = Settings.Darkness;
	Constants.Params.w = Settings.Contrast;
	float Fade = std::clamp(Sun.z / 0.1f, 0.0f, 1.0f);
	Fade = Fade * Fade * (3.0f - 2.0f * Fade);
	if (TheShaderManager->GameState.dayLight <= 0.5f && TheShaderManager->Effects.ShadowsExteriors)
		Fade *= 1.0f - std::clamp(TheShaderManager->Effects.ShadowsExteriors->Constants.ShadowFade.x, 0.0f, 1.0f);
	LastFade = Fade;
	Constants.Data.w = (Uploaded && UploadedWorldSpace == World && Sun.z > 0.0f) ? Settings.Strength * Fade : 0.0f;

	static ULONGLONG LastStatus = 0;
	if (TheSettingManager->SettingsMain.Develop.DebugMode && GetTickCount64() - LastStatus > 10000) {
		LastStatus = GetTickCount64();
		auto Angles = [](const D3DXVECTOR3& L, float& Az, float& El) {
			Az = atan2f(L.x, L.y) * 57.2958f; if (Az < 0.0f) Az += 360.0f;
			El = asinf(std::clamp(L.z, -1.0f, 1.0f)) * 57.2958f;
		};
		float Az, El, SweptAz, SweptEl;
		Angles(Sun, Az, El);
		Angles(SweptSunDir, SweptAz, SweptEl);
		Logger::Log("[TerrainShadows] status: hour %.2f, dayLight %.2f; light az %.1f el %.1f, last swept az %.1f el %.1f; %s%s%s; sweeps started %u, uploaded %u, lock failures %u; on screen %.2f (fade %.2f)%s",
			TheShaderManager->ShaderConst.GameTime.y, TheShaderManager->GameState.dayLight, Az, El, SweptAz, SweptEl,
			Sweeping ? "sweeping " : "", Uploading ? "uploading " : "", (!Sweeping && !Uploading) ? "idle" : "",
			SweepsStarted, SweepsUploaded, LockFailures, Constants.Data.w, Fade,
			!Uploaded ? ", nothing uploaded" : UploadedWorldSpace != World ? ", texture is another worldspace's" : "");
	}
}
