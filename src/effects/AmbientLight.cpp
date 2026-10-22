#include "AmbientLight.h"
#include "BounceLightMath.h"

void AmbientLightEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_AmbientLightData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_AmbientLightData2", &Constants.Data2);
}

void AmbientLightEffect::UpdateSettings() {
	Constants.Data.x = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientLight.Main", "Radius"), 10.0f, 1000.0f);
	Constants.Data.y = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientLight.Main", "OcclusionStrength"), 0.0f, 4.0f);
	Constants.Data.z = 0.0f;	// UNOFFICIAL 2026-10-14: the screen bounce removed (BounceStrength); GridBounce is the bounce
	Constants.Data.w = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientLight.Main", "DrawDistance"), 500.0f, 50000.0f);
	Constants.Data2.x = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientLight.Main", "DarkestOcclusion"), 0.0f, 1.0f);
	Constants.Data2.y = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientLight.Main", "BrightnessFade"), 0.0f, 4.0f);
	Constants.Data2.z = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientLight.Main", "BlurRadius"), 0.5f, 4.0f);
	Constants.Data2.w = (float)std::clamp(TheSettingManager->GetSettingI("Shaders.AmbientLight.Main", "DebugView"), 0, 10);
	temporal = TheSettingManager->GetSettingI("Shaders.AmbientLight.Main", "Temporal") != 0;
	gridBounce = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientLight.Main", "GridBounce"), 0.0f, 10.0f);
	gridCell = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientLight.Main", "GridCellSize"), 50.0f, 800.0f);
	gridSteps = std::clamp(TheSettingManager->GetSettingI("Shaders.AmbientLight.Main", "GridSteps"), 1, 16);
	gridSpread = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientLight.Main", "GridSpread"), 0.0f, 0.6f);
	gridBlocking = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientLight.Main", "GridBlocking"), 0.0f, 4.0f);
	gridScreenStrength = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientLight.Main", "GridScreenStrength"), 0.0f, 40.0f);
	gridColour = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientLight.Main", "GridColour"), 0.0f, 1.0f);
	interiorBalance = TheSettingManager->GetSettingI("Shaders.AmbientLight.Main", "InteriorBalance") != 0;
	interiorBounceShare = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientLight.Main", "InteriorBounceShare"), 0.0f, 4.0f);
	normalSmoothing = std::clamp(TheSettingManager->GetSettingI("Shaders.AmbientLight.Main", "NormalSmoothing"), 0, 2);	// UNOFFICIAL 2026-10-21 (alrocks)
	horizonBias = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientLight.Main", "HorizonBias"), 0.0f, 45.0f);
}

// Half resolution, made the first time it runs (about 29 MB at 1440p: one A32B32G32R32F, four A16B16G16R16F).
// UNOFFICIAL 2026-10-14: the two history buffers too (Temporal; without them it runs as before, history share 0).
void AmbientLightEffect::EnsureBuffers() {
	if (buffersTried) return;
	buffersTried = true;
	const int width = MaxQuality::Reduced(TheRenderManager->width, 2);
	const int height = MaxQuality::Reduced(TheRenderManager->height, 2);
	TheTextureManager->InitTexture("NVR_AmbientLightPrep", &prepTexture, &prepSurface, width, height, D3DFMT_A32B32G32R32F);
	TheTextureManager->InitTexture("NVR_AmbientLight0", &bufferTexture[0], &bufferSurface[0], width, height, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("NVR_AmbientLight1", &bufferTexture[1], &bufferSurface[1], width, height, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("NVR_AmbientLightHistory0", &historyTexture[0], &historySurface[0], width, height, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("NVR_AmbientLightHistory1", &historyTexture[1], &historySurface[1], width, height, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("NVR_AmbientLightOcclusion0", &occlusionHistory[0], &occlusionHistorySurface[0], width, height, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("NVR_AmbientLightOcclusion1", &occlusionHistory[1], &occlusionHistorySurface[1], width, height, D3DFMT_A16B16G16R16F);
	// UNOFFICIAL 2026-10-18 (ambfix2) indoor balance: 32 x 18 samples (16 x 9 texels) of the picture's light, the occlusion's darkening and the bounce,
	// and their average with the bounce's scale (1 x 1, one read while the other is written).
	TheTextureManager->InitTexture("NVR_AmbientLightStats", &statsTexture, &statsSurface, StatsWidth, StatsHeight, D3DFMT_A32B32G32R32F);
	TheTextureManager->InitTexture("NVR_AmbientLightBalance0", &balanceTexture[0], &balanceSurface[0], 1, 1, D3DFMT_A32B32G32R32F);
	TheTextureManager->InitTexture("NVR_AmbientLightBalance1", &balanceTexture[1], &balanceSurface[1], 1, 1, D3DFMT_A32B32G32R32F);
}

// UNOFFICIAL 2026-10-14 light grid: the sun map (256^2 R32F + depth) and the atlas pairs (128^2 A16B16G16R16F), about 0.9 MB.
void AmbientLightEffect::EnsureGridBuffers() {
	if (gridBuffersTried) return;
	gridBuffersTried = true;
	TheTextureManager->InitTexture("NVR_GridSunDepth", &gridSunTexture, &gridSunSurface, GridSunMapSize, GridSunMapSize, D3DFMT_R32F);
	TheRenderManager->device->CreateDepthStencilSurface(GridSunMapSize, GridSunMapSize, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, true, &gridSunDepth, NULL);
	TheTextureManager->InitTexture("NVR_GridWave0", &gridWave[0], &gridWaveSurface[0], GridAtlasSize, GridAtlasSize, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("NVR_GridWave1", &gridWave[1], &gridWaveSurface[1], GridAtlasSize, GridAtlasSize, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("NVR_GridAccum0", &gridAccum[0], &gridAccumSurface[0], GridAtlasSize, GridAtlasSize, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("NVR_GridAccum1", &gridAccum[1], &gridAccumSurface[1], GridAtlasSize, GridAtlasSize, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("NVR_GridScreen0", &gridScreen[0], &gridScreenSurface[0], GridAtlasSize, GridAtlasSize, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("NVR_GridScreen1", &gridScreen[1], &gridScreenSurface[1], GridAtlasSize, GridAtlasSize, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("NVR_GridScreenColor0", &gridScreenColor[0], &gridScreenColorSurface[0], GridAtlasSize, GridAtlasSize, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("NVR_GridScreenColor1", &gridScreenColor[1], &gridScreenColorSurface[1], GridAtlasSize, GridAtlasSize, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("NVR_GridGeometry", &gridGeometry, &gridGeometrySurface, GridAtlasSize, GridAtlasSize, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("NVR_GridSunColor", &gridSunColorTexture, &gridSunColorSurface, GridSunMapSize, GridSunMapSize, D3DFMT_A8R8G8B8);
	TheTextureManager->InitTexture("NVR_GridWaveColor0", &gridWaveColor[0], &gridWaveColorSurface[0], GridAtlasSize, GridAtlasSize, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("NVR_GridWaveColor1", &gridWaveColor[1], &gridWaveColorSurface[1], GridAtlasSize, GridAtlasSize, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("NVR_GridAccumColor0", &gridAccumColor[0], &gridAccumColorSurface[0], GridAtlasSize, GridAtlasSize, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("NVR_GridAccumColor1", &gridAccumColor[1], &gridAccumColorSurface[1], GridAtlasSize, GridAtlasSize, D3DFMT_A16B16G16R16F);
	if (!gridWaveColorSurface[0] || !gridWaveColorSurface[1] || !gridAccumColorSurface[0] || !gridAccumColorSurface[1]) {
		gridSunColorSurface = nullptr;	// no colour then: the sun map is drawn without it and the bounce takes one ground colour
		Logger::Log("UNOFFICIAL AmbientLight: light grid colour buffers could not be made; the bounce takes one ground colour.");
	}
	if (!gridSunSurface || !gridSunDepth || !gridWaveSurface[0] || !gridWaveSurface[1] || !gridAccumSurface[0] || !gridAccumSurface[1] || !gridGeometrySurface)
		Logger::Log("UNOFFICIAL AmbientLight: light grid buffers could not be made; GridBounce stays off.");
}

// Called by ShadowManager with the sun's direction while it draws the sun's shadow maps (outdoors): draw the grid's sun map this
// frame? Every 4th frame, at once when the grid has to move (the player walked into another cell), never with the sun down.
bool AmbientLightEffect::WantsGridSunMap(const D3DXVECTOR3& sunDir) {
	gridSunFresh = false;
	gridSunActive = false;
	if (!Enabled || !Effect || gridBounce <= 0.0f || sunDir.z < 0.03f || TheShaderManager->GameState.isUnderwater) return false;
	EnsureGridBuffers();
	if (!gridSunSurface || !gridSunDepth || !gridWaveSurface[0] || !gridWaveSurface[1] || !gridAccumSurface[0] || !gridAccumSurface[1] || !gridGeometrySurface) return false;
	gridSunActive = true;
	return GridMoveOrDue();
}

// The grid's corner for the camera now (whole cells), and whether to update it: every 4th frame, at once when it has to move.
bool AmbientLightEffect::GridMoveOrDue() {
	const NiPoint3 camera = WorldSceneGraph->camera->m_worldTransform.pos;
	const double cell = gridCell;
	gridPendingOrigin[0] = floor(camera.x / cell) * cell - 16.0 * cell;
	gridPendingOrigin[1] = floor(camera.y / cell) * cell - 16.0 * cell;
	gridPendingOrigin[2] = floor(camera.z / cell) * cell - 8.0 * cell;
	const bool moved = !gridValid || gridPendingOrigin[0] != gridOrigin[0] || gridPendingOrigin[1] != gridOrigin[1] || gridPendingOrigin[2] != gridOrigin[2];
	return moved || (gridFrame++ % 4) == 0;
}

// As ShadowsExteriorEffect::GetCascadeViewProj, for a fixed box: the grid (32 x 32 x 16 cells), seen from the sun, in
// camera-relative space; rounded to whole map texels so it holds still while the grid does.
D3DXMATRIX AmbientLightEffect::GridSunViewProj(const D3DXVECTOR3& sunDir) {
	const NiPoint3 camera = WorldSceneGraph->camera->m_worldTransform.pos;
	const float cell = gridCell;
	const float radius = 24.0f * cell;	// half the box's diagonal: sqrt(16^2 + 16^2 + 8^2) cells
	D3DXVECTOR3 centre((float)(gridPendingOrigin[0] + 16.0 * cell - camera.x), (float)(gridPendingOrigin[1] + 16.0 * cell - camera.y),
		(float)(gridPendingOrigin[2] + 8.0 * cell - camera.z));
	D3DXVECTOR3 eye = centre + sunDir * radius;
	D3DXVECTOR3 up = fabsf(sunDir.z) > 0.999f ? D3DXVECTOR3(0.0f, 1.0f, 0.0f) : D3DXVECTOR3(0.0f, 0.0f, 1.0f);
	D3DXMATRIX view, proj, viewProj;
	D3DXMatrixLookAtRH(&view, &eye, &centre, &up);
	D3DXMatrixOrthoOffCenterRH(&proj, -radius, radius, -radius, radius, 0.0f, 2.0f * radius);
	viewProj = view * proj;
	const float size = (float)GridSunMapSize;
	D3DXVECTOR4 origin(-camera.x, -camera.y, -camera.z, 1.0f);
	D3DXVec4Transform(&origin, &origin, &viewProj);
	D3DXVec4Scale(&origin, &origin, size / 2.0f);
	D3DXVECTOR4 rounded(roundf(origin.x), roundf(origin.y), origin.z, origin.w);
	proj._41 += (rounded.x - origin.x) * 2.0f / size;
	proj._42 += (rounded.y - origin.y) * 2.0f / size;
	viewProj = view * proj;

	ShadowsExteriorEffect* shadows = TheShaderManager->Effects.ShadowsExteriors;
	gridSunMap.Forms = shadows->ShadowMaps[ShadowManager::MapMiddle].Forms;
	gridSunMap.Forms.Lod = false;
	gridSunMap.Forms.MinRadius = gridSunMap.Forms.OrigMinRadius * radius / size;
	gridSunMap.ShadowMapResolution = size;
	gridSunMap.ShadowMapInverseResolution = 1.0f / size;
	gridSunMap.ShadowMapRadius = radius;
	gridSunMap.ShadowMapNear = 0.0f;
	gridSunMap.CustomClearRequired = false;
	gridSunMap.ClearColor = D3DXVECTOR4(1.0f, 0.0f, 0.0f, 1.0f);
	gridSunMap.ShadowMapViewPort = { 0, 0, (DWORD)GridSunMapSize, (DWORD)GridSunMapSize, 0.0f, 1.0f };
	gridSunMap.CameraTranslation = D3DXVECTOR3(camera.x, camera.y, camera.z);
	NiFrustum frustum(-radius, radius, radius, -radius, 0.0f, 2.0f * radius, true);
	TheCameraManager->SetFrustumPlanes(&gridSunMap.ShadowMapFrustumPlanes, &viewProj, eye, frustum);
	gridSunMap.ShadowMapFrustumPlanes.SetActivePlaneState(62);

	// The map's clip space <-> the grid's own space (relative to its corner), through camera-relative space now.
	D3DXMATRIX inverse, toGrid, fromGrid;
	D3DXMatrixInverse(&inverse, NULL, &viewProj);
	D3DXMatrixTranslation(&toGrid, (float)(camera.x - gridPendingOrigin[0]), (float)(camera.y - gridPendingOrigin[1]), (float)(camera.z - gridPendingOrigin[2]));
	D3DXMatrixTranslation(&fromGrid, (float)(gridPendingOrigin[0] - camera.x), (float)(gridPendingOrigin[1] - camera.y), (float)(gridPendingOrigin[2] - camera.z));
	gridSunToGrid = inverse * toGrid;
	gridToSun = fromGrid * viewProj;
	gridSunTexel = 2.0f * radius / size;
	gridSunFresh = true;
	return viewProj;
}

// The grid, every few frames: the screen memory first (indoors and at night: no sun map), then inject (the sun map's surfaces
// and/or the screen memory), then spread gridSteps times (each pass writes this step and the running total).
void AmbientLightEffect::RenderGrid(IDirect3DDevice9* Device, bool sun, bool screen) {
	D3DXHANDLE technique = Effect->GetTechniqueByName("AmbientLightGrid");
	D3DXHANDLE originHandle = ParameterHandle(paramGridOrigin, "NVR_GridOrigin");
	D3DXHANDLE dataHandle = ParameterHandle(paramGridData, "NVR_GridData");
	D3DXHANDLE lightHandle = ParameterHandle(paramGridLight, "NVR_GridLight");
	D3DXHANDLE sunToGridHandle = ParameterHandle(paramGridSunToGrid, "NVR_GridSunToGrid");
	D3DXHANDLE toSunHandle = ParameterHandle(paramGridToSun, "NVR_GridToSun");
	D3DXHANDLE stepHandle = ParameterHandle(paramGridStep, "NVR_GridStep");
	D3DXHANDLE shiftHandle = ParameterHandle(paramGridShift, "NVR_GridShift");
	D3DXTECHNIQUE_DESC description = {};
	if (!technique || !originHandle || !dataHandle || !lightHandle || !sunToGridHandle || !toSunHandle || !stepHandle || !shiftHandle ||
		FAILED(Effect->GetTechniqueDesc(technique, &description)) || description.Passes != 3) {
		static bool reported = false;
		if (!reported) Logger::Log("UNOFFICIAL AmbientLight: the light grid needs the AmbientLight.fx.hlsl that came with this DLL; GridBounce off.");
		reported = true;
		gridValid = false;
		return;
	}
	screen = screen && gridScreenSurface[0] && gridScreenSurface[1] && gridScreenColorSurface[0] && gridScreenColorSurface[1];
	static GpuTimer gridTimer("  Ambient light grid (inject + spread)");
	GpuProfileScope gpu(gridTimer, Device);
	const NiPoint3 camera = WorldSceneGraph->camera->m_worldTransform.pos;
	D3DXVECTOR4 origin((float)(gridPendingOrigin[0] - camera.x), (float)(gridPendingOrigin[1] - camera.y), (float)(gridPendingOrigin[2] - camera.z), gridCell);
	const bool colour = gridWaveColorSurface[0] && gridAccumColorSurface[0];
	D3DXVECTOR4 data(gridBounce, gridSpread, gridSunTexel, colour && gridSunHasColor ? 1.0f : 0.0f);
	const D3DXVECTOR4& sunColor = TheShaderManager->ShaderConst.sunColor;
	D3DXVECTOR4 light(powf((std::max)(sunColor.x, 0.0f), 2.2f), powf((std::max)(sunColor.y, 0.0f), 2.2f), powf((std::max)(sunColor.z, 0.0f), 2.2f), 1.0f);
	// The screen memory moves with the grid: a cell now at c was at c + shift.
	const bool keep = gridScreenValid && !TheShaderManager->GameState.isCellChanged;
	D3DXVECTOR4 shift(keep ? (float)((gridPendingOrigin[0] - gridScreenOrigin[0]) / gridCell) : 0.0f,
		keep ? (float)((gridPendingOrigin[1] - gridScreenOrigin[1]) / gridCell) : 0.0f,
		keep ? (float)((gridPendingOrigin[2] - gridScreenOrigin[2]) / gridCell) : 0.0f, keep ? 1.0f : 0.0f);
	D3DVIEWPORT9 original = {}, atlas = { 0, 0, (DWORD)GridAtlasSize, (DWORD)GridAtlasSize, 0.0f, 1.0f };
	Device->GetViewport(&original);
	Effect->SetTechnique(technique);
	SetCT();	// the TESR_ textures (depth, normals) for the screen memory
	Effect->SetVector(originHandle, &origin);
	Effect->SetVector(dataHandle, &data);
	Effect->SetVector(lightHandle, &light);
	Effect->SetVector(shiftHandle, &shift);
	if (D3DXHANDLE h = ParameterHandle(paramGridScreen, "NVR_GridScreen")) {
		D3DXVECTOR4 screenData(gridScreenStrength, 0.0f, 0.0f, 0.0f);
		Effect->SetVector(h, &screenData);
	}
	if (sun) {
		Effect->SetMatrix(sunToGridHandle, &gridSunToGrid);
		Effect->SetMatrix(toSunHandle, &gridToSun);
	}
	auto clearStages = [&]() {
		for (int s = 5; s <= 14; s++) if (s != 11) Device->SetTexture(s, nullptr);
	};
	// UNOFFICIAL 2026-10-18 (ambfix): targets 1-3 have their own colour write masks, and the game leaves them without alpha (0x7, as
	// BounceLight found for target 1): every alpha the grid stores in them was lost - the colour's light (so the bounce came out grey,
	// whatever lit the room), the screen memory's colour weight and the cells' cover (GridBlocking did nothing). All four channels here.
	DWORD writeMask[3] = { 0xF, 0xF, 0xF };
	for (int i = 0; i < 3; i++) {
		Device->GetRenderState((D3DRENDERSTATETYPE)(D3DRS_COLORWRITEENABLE1 + i), &writeMask[i]);
		Device->SetRenderState((D3DRENDERSTATETYPE)(D3DRS_COLORWRITEENABLE1 + i), 0xF);
	}
	static bool maskLogged = false;
	if (!maskLogged) {
		maskLogged = true;
		Logger::Log("UNOFFICIAL AmbientLight: light grid: write masks of targets 1-3 were 0x%X 0x%X 0x%X (all four channels while the grid is made).",
			(unsigned)writeMask[0], (unsigned)writeMask[1], (unsigned)writeMask[2]);
	}
	UINT passes = 0;
	HRESULT result = Effect->Begin(&passes, 0);
	int wave = 0, accum = 0;
	if (SUCCEEDED(result)) {
		// The screen memory.
		if (screen) {
			const int read = gridScreenIndex, write = gridScreenIndex ^ 1;
			clearStages();
			result = Device->SetRenderTarget(0, gridScreenSurface[write]);
			if (SUCCEEDED(result)) result = Device->SetRenderTarget(1, gridScreenColorSurface[write]);
			if (SUCCEEDED(result)) result = Device->SetViewport(&atlas);
			if (SUCCEEDED(result)) result = Effect->BeginPass(0);
			if (SUCCEEDED(result)) {
				Device->SetTexture(1, TheTextureManager->RenderedTexture);	// TESR_SourceBuffer: the image now
				Device->SetTexture(6, gridScreen[read]);					// NVR_GridScreenOld
				Device->SetTexture(7, gridScreenColor[read]);				// NVR_GridScreenColorOld
				result = Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
				HRESULT endResult = Effect->EndPass();
				if (SUCCEEDED(result)) result = endResult;
			}
			if (SUCCEEDED(result)) {
				gridScreenIndex = write;
				gridScreenValid = true;
				for (int i = 0; i < 3; i++) gridScreenOrigin[i] = gridPendingOrigin[i];
			}
			Device->SetRenderTarget(1, nullptr);
		}
		for (int step = 0; step <= gridSteps && SUCCEEDED(result); step++) {
			const int writeWave = step == 0 ? 0 : wave ^ 1, writeAccum = step == 0 ? 0 : accum ^ 1;
			clearStages();
			result = Device->SetRenderTarget(0, gridWaveSurface[writeWave]);
			// The second target: on injection the cells' surfaces (blocking), then the running total.
			if (SUCCEEDED(result)) result = Device->SetRenderTarget(1, step == 0 ? gridGeometrySurface : gridAccumSurface[writeAccum]);
			D3DXVECTOR4 stepData(step == 1 ? 1.0f : 0.0f, gridBlocking, sun ? 1.0f : 0.0f, screen && gridScreenValid ? 1.0f : 0.0f);
			Effect->SetVector(stepHandle, &stepData);
			// The colour, carried along with the light: targets 2 and 3.
			if (SUCCEEDED(result)) result = Device->SetRenderTarget(2, colour ? gridWaveColorSurface[writeWave] : nullptr);
			if (SUCCEEDED(result)) result = Device->SetRenderTarget(3, colour ? gridAccumColorSurface[writeAccum] : nullptr);
			if (SUCCEEDED(result)) result = Device->SetViewport(&atlas);
			if (SUCCEEDED(result)) result = Effect->BeginPass(step == 0 ? 1 : 2);
			if (FAILED(result)) break;
			if (step == 0) {
				if (sun) { Device->SetTexture(10, gridSunTexture); if (gridSunHasColor) Device->SetTexture(12, gridSunColorTexture); }
				if (screen && gridScreenValid) { Device->SetTexture(6, gridScreen[gridScreenIndex]); Device->SetTexture(7, gridScreenColor[gridScreenIndex]); }
			}
			else {
				Device->SetTexture(8, gridWave[wave]); Device->SetTexture(9, gridAccum[accum]);
				Device->SetTexture(5, gridGeometry);	// NVR_GridGeometry (NVR_ALPrep's stage)
				if (colour) { Device->SetTexture(13, gridWaveColor[wave]); Device->SetTexture(14, gridAccumColor[accum]); }
			}
			result = Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
			HRESULT endResult = Effect->EndPass();
			if (SUCCEEDED(result)) result = endResult;
			wave = writeWave;
			accum = writeAccum;
		}
		Effect->End();
	}
	Device->SetRenderTarget(1, nullptr);
	Device->SetRenderTarget(2, nullptr);
	Device->SetRenderTarget(3, nullptr);
	for (int i = 0; i < 3; i++) Device->SetRenderState((D3DRENDERSTATETYPE)(D3DRS_COLORWRITEENABLE1 + i), writeMask[i]);
	clearStages();
	Device->SetTexture(1, TheTextureManager->SourceTexture);
	Device->SetViewport(&original);
	if (SUCCEEDED(result)) {
		gridAccumIndex = accum;
		gridColorValid = colour;
		for (int i = 0; i < 3; i++) gridOrigin[i] = gridPendingOrigin[i];
		gridValid = true;
		gridRenderedFrame = TheFrameRateManager->FrameNumber;
		static bool logged = false;
		if (!logged) Logger::Log("UNOFFICIAL AmbientLight: light grid running (cell %.0f units, %d steps, sun map %s, screen memory %s).",
			gridCell, gridSteps, sun ? "yes" : "no", screen ? "yes" : "no");
		logged = true;
	}
	else {
		gridValid = false;
		static bool reported = false;
		if (!reported) Logger::Log("UNOFFICIAL AmbientLight: light grid render failed (%08lx).", result);
		reported = true;
	}
}

bool AmbientLightEffect::ShouldRender() {
	return Enabled && Effect && !TheShaderManager->GameState.isUnderwater;
}

void AmbientLightEffect::Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget,
	IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget,
	IDirect3DSurface9* SourceBuffer) {
	if (!ShouldRender()) { renderTime = 0; historyValid = false; return; }
	EnsureBuffers();
	// UNOFFICIAL 2026-10-18 (ambfix): the scene's own light (the cell's ambient and directional colours indoors, the sky's ambient and
	// the sun's colour outdoors), logged when it changes: the apply's tint is judged against it.
	{
		D3DXVECTOR4 ambient = TheShaderManager->ShaderConst.sunAmbient, direct = TheShaderManager->ShaderConst.sunColor;
		const bool interior = !TheShaderManager->GameState.isExterior && Player && Player->parentCell && Player->parentCell->IsInterior() && Player->parentCell->lighting;
		UInt32 inherit = 0;
		TESObjectCELL::LightingData* fromTemplate = nullptr;
		if (interior) {
			TESObjectCELL* cell = Player->parentCell;
			ambient = cell->lighting->ambient.toD3DXVECTOR4(); direct = cell->lighting->directional.toD3DXVECTOR4();
			// UNOFFICIAL 2026-10-18 (ambfix2): a cell with a lighting template takes the fields its inherit flags name (LNAM: 1 ambient,
			// 2 directional) from the template (BGSLightingTemplate: TESForm, then the same data as XCLL). The cell's own values are
			// then placeholders: Rivet City's hangar (RCHangar, TTW) has ambient 0 0 0 and inherits RCUpperDecksTemplate's 44 48 62
			// (P81 read the placeholders: a 4 x too dark, yellow scene light, so every surface counted as lamp-lit and the bounce came
			// out strong and blue). The flags follow the template pointer (TESObjectCELL 0xDC; the Xbox 360 debug build's type
			// records name them pLightingTemplate / iLightingTemplateInheritanceFlags, 184 / 188 there). Part of InteriorBalance.
			inherit = cell->unk0DC;
			if (cell->lightingTemplate && interiorBalance) fromTemplate = (TESObjectCELL::LightingData*)((char*)cell->lightingTemplate + sizeof(TESForm));
			if (fromTemplate && (inherit & 1)) ambient = fromTemplate->ambient.toD3DXVECTOR4();
			if (fromTemplate && (inherit & 2)) direct = fromTemplate->directional.toD3DXVECTOR4();
		}
		// 2026-10-19 (a P81 tester's log: ~12000 of these lines in 30 minutes): outdoors the sky's colours drift a little every
		// frame, so 'when it changes' was every frame. Now: on an indoor/outdoor switch, or a change of more than 0.02 in a channel,
		// at most 200 lines a session.
		static D3DXVECTOR4 loggedAmbient(-1.0f, -1.0f, -1.0f, -1.0f), loggedDirect(-1.0f, -1.0f, -1.0f, -1.0f);
		static bool loggedInterior = false;
		static UInt32 loggedLines = 0;
		auto moved = [](const D3DXVECTOR4& a, const D3DXVECTOR4& b) {
			return fabsf(a.x - b.x) > 0.02f || fabsf(a.y - b.y) > 0.02f || fabsf(a.z - b.z) > 0.02f;
		};
		if (loggedLines < 200 && (interior != loggedInterior || moved(ambient, loggedAmbient) || moved(direct, loggedDirect))) {
			loggedAmbient = ambient; loggedDirect = direct; loggedInterior = interior;
			if (++loggedLines == 200) Logger::Log("UNOFFICIAL AmbientLight: scene light lines stop here (200 this session)");
			Logger::Log("UNOFFICIAL AmbientLight: scene light (%s): ambient %.4f %.4f %.4f, %s %.4f %.4f %.4f", interior ? "cell" : "sky",
				ambient.x, ambient.y, ambient.z, interior ? "directional" : "sun", direct.x, direct.y, direct.z);
			if (interior) {
				TESObjectCELL::LightingData* own = Player->parentCell->lighting;
				Logger::Log("UNOFFICIAL AmbientLight: cell lighting: own ambient %u %u %u directional %u %u %u fog near %.1f, inherit flags 0x%X, template %s%s",
					own->ambient.r, own->ambient.g, own->ambient.b, own->directional.r, own->directional.g, own->directional.b, own->fogNear, inherit,
					Player->parentCell->lightingTemplate ? "yes" : "none", Player->parentCell->lightingTemplate && !fromTemplate ? " (not used: InteriorBalance off)" : "");
				if (fromTemplate) Logger::Log("UNOFFICIAL AmbientLight: template lighting: ambient %u %u %u directional %u %u %u fog near %.1f",
					fromTemplate->ambient.r, fromTemplate->ambient.g, fromTemplate->ambient.b, fromTemplate->directional.r, fromTemplate->directional.g,
					fromTemplate->directional.b, fromTemplate->fogNear);
			}
		}
		// The light every surface gets (linear, on a white surface): the ambient, indoors plus half the cell's directional light.
		auto linear = [](float c) { c = (std::max)(c, 0.0f); return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f); };
		const float half = interior ? 0.5f : 0.0f;
		sceneLight = D3DXVECTOR4(linear(ambient.x + half * direct.x), linear(ambient.y + half * direct.y), linear(ambient.z + half * direct.z), 1.0f);
	}
	D3DXHANDLE layoutHandle = ParameterHandle(paramLayout, "NVR_ALLayout");
	IDirect3DTexture9* scene = TheTextureManager->RenderedTexture;
	D3DXTECHNIQUE_DESC description = {};
	D3DVIEWPORT9 original = {};
	D3DSURFACE_DESC half = {};
	D3DXHANDLE technique = Effect->GetTechnique(0);
	if (!layoutHandle || !scene || !prepTexture || !prepSurface || !bufferTexture[0] || !bufferTexture[1] || !bufferSurface[0] ||
		!bufferSurface[1] || !historyTexture[0] || !historyTexture[1] || !historySurface[0] || !historySurface[1] ||
		!occlusionHistory[0] || !occlusionHistory[1] || !occlusionHistorySurface[0] || !occlusionHistorySurface[1] || !technique ||
		FAILED(Effect->GetTechniqueDesc(technique, &description)) || description.Passes != 8 ||
		FAILED(Device->GetViewport(&original)) || FAILED(bufferSurface[0]->GetDesc(&half))) {
		static bool reported = false;
		if (!reported) Logger::Log("UNOFFICIAL AmbientLight: buffers missing or AmbientLight.fx.hlsl is not the matching version (8 passes), effect skipped.");
		reported = true;
		renderTime = 0;
		return;
	}
	// UNOFFICIAL 2026-10-20 (skyamb) Skylighting (AmbientOcclusion.h): outdoors with it on, the same passes with the sky test's factor in
	// Apply (technique AmbientLightSky, stage 10), from the AO's sky target at this effect's half resolution: the AO's sky pass wrote it
	// just before (ShaderManager).
	IDirect3DTexture9* skyTexture = nullptr;
	if (AmbientOcclusionEffect* ao = TheShaderManager->Effects.AmbientOcclusion; ao && ao->Effect && AmbientOcclusionEffect::SkyV2Active()) {
		D3DXHANDLE skyTechnique = TechniqueHandle(techSky, "AmbientLightSky");
		IDirect3DTexture9* written = ao->SkyTexture();
		D3DSURFACE_DESC skyDesc = {};
		D3DXTECHNIQUE_DESC skyTechniqueDesc = {};
		if (skyTechnique && written && SUCCEEDED(written->GetLevelDesc(0, &skyDesc)) && skyDesc.Width == half.Width && skyDesc.Height == half.Height &&
			SUCCEEDED(Effect->GetTechniqueDesc(skyTechnique, &skyTechniqueDesc)) && skyTechniqueDesc.Passes == 8) {
			technique = skyTechnique;
			skyTexture = written;
		}
		static int reported = -1;
		if ((skyTexture ? 1 : 0) != reported) {
			reported = skyTexture ? 1 : 0;
			Logger::Log("UNOFFICIAL AmbientLight: Skylighting %s.", skyTexture ? "applied (the AO's sky pass writes the sky target)" :
				"NOT applied (no sky target this frame, or no AmbientLightSky technique: AmbientLight.fx.hlsl must be this DLL's)");
		}
	}
	auto timer = TimeLogger();
	FrameChain& chain = TheShaderManager->Chain;
	const bool chained = chain.Owns(RenderTarget, RenderedSurface);
	IDirect3DSurface9* finalTarget = chained ? chain.Output() : RenderTarget;
	D3DVIEWPORT9 reduced = original;
	reduced.Width = half.Width;
	reduced.Height = half.Height;
	D3DXVECTOR4 layout((float)half.Width / original.Width, (float)half.Height / original.Height, 1.0f / half.Width, 1.0f / half.Height);
	IDirect3DSurface9* depthSurface = nullptr;
	Device->GetDepthStencilSurface(&depthSurface);
	if (!ReShadeIntegration::KeepDepthBound()) Device->SetDepthStencilSurface(nullptr);

	// UNOFFICIAL 2026-10-14 light grid: inject and spread when the sun map was drawn this frame; the apply reads the finished
	// grid (stage 9) with its corner relative to the camera now.
	// UNOFFICIAL 2026-10-14: outdoors with the sun up the sun map feeds it (updated when ShadowManager drew the map); indoors and
	// at night the screen memory (updated on the same cadence, decided here).
	const bool gridOn = gridBounce > 0.0f;
	if (gridOn) {
		EnsureGridBuffers();
		const bool buffers = gridWaveSurface[0] && gridWaveSurface[1] && gridAccumSurface[0] && gridAccumSurface[1] && gridGeometrySurface;
		if (buffers) {
			if (gridSunActive) { if (gridSunFresh) RenderGrid(Device, true, false); }
			else if (GridMoveOrDue()) RenderGrid(Device, false, true);
		}
		gridSunFresh = false;
		gridSunActive = false;	// WantsGridSunMap sets it again next frame while the sun map is in use
	}
	Effect->SetTechnique(technique);
	SetCT();
	Effect->SetVector(layoutHandle, &layout);
	if (D3DXHANDLE dataHandle = ParameterHandle(paramGridData, "NVR_GridData")) {
		const bool use = gridOn && gridValid;
		D3DXVECTOR4 data(use ? gridBounce : 0.0f, gridSpread, gridSunTexel, use && gridColorValid ? 1.0f : 0.0f);
		Effect->SetVector(dataHandle, &data);
		if (use) {
			const NiPoint3 camera = WorldSceneGraph->camera->m_worldTransform.pos;
			D3DXVECTOR4 origin((float)(gridOrigin[0] - camera.x), (float)(gridOrigin[1] - camera.y), (float)(gridOrigin[2] - camera.z), gridCell);
			if (D3DXHANDLE h = ParameterHandle(paramGridOrigin, "NVR_GridOrigin")) Effect->SetVector(h, &origin);
		}
	}
	if (D3DXHANDLE h = ParameterHandle(paramScene, "NVR_ALScene")) {
		D3DXVECTOR4 scene(sceneLight.x, sceneLight.y, sceneLight.z, gridColour);
		Effect->SetVector(h, &scene);
	}
	// UNOFFICIAL 2026-10-18 (ambfix2, user: "indoors it's too powerful ... diverges from the original mood of each vanilla room"):
	// indoors (InteriorBalance) the occlusion darkens only the ambient light, not the lamps', and the bounce gives back at most
	// InteriorBounceShare x the light the occlusion took, so the room keeps its own brightness; worked out from 32 x 18 samples
	// (passes 5-6) and eased over about 10 frames. Outdoors nothing of this runs (the picture exactly as before).
	const bool interiorMode = interiorBalance && !TheShaderManager->GameState.isExterior && statsSurface && balanceSurface[0] && balanceSurface[1];
	const bool balanceRuns = interiorMode && gridOn && gridValid;
	if (!balanceRuns || TheShaderManager->GameState.isCellChanged) balanceValid = false;
	const int balanceRead = balanceIndex, balanceWrite = balanceIndex ^ 1;
	if (D3DXHANDLE h = ParameterHandle(paramInterior, "NVR_ALInterior")) {
		D3DXVECTOR4 data(interiorMode ? 1.0f : 0.0f, balanceValid ? 1.0f : 0.0f, interiorBounceShare, 0.9f);
		Effect->SetVector(h, &data);
	}
	// UNOFFICIAL 2026-10-21 (alrocks): the normal smoothing's reach and the horizon bias (radians) for Prepare and Estimate.
	if (D3DXHANDLE h = ParameterHandle(paramEdges, "NVR_ALEdges")) {
		D3DXVECTOR4 edges((float)normalSmoothing, horizonBias * (3.14159265f / 180.0f), 0.0f, 0.0f);
		Effect->SetVector(h, &edges);
	}
	D3DXHANDLE flagsHandle = ParameterHandle(paramFlags, "NVR_ALFlags");
	const bool shadow = foldShadow && flagsHandle;
	if (flagsHandle) {
		D3DXVECTOR4 flags(shadow ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
		Effect->SetVector(flagsHandle, &flags);
	}
	// UNOFFICIAL 2026-10-19 SunFar: the land's and the clouds' shadow factor on stage 13 for the folded sun-shadow apply (Apply pass).
	IDirect3DTexture9* sunFar = shadow && TheShaderManager->Effects.SunFar ? TheShaderManager->Effects.SunFar->FactorTexture() : nullptr;
	if (D3DXHANDLE h = ParameterHandle(paramSunFarFlags, "NVR_SunFarFlags")) {
		D3DXVECTOR4 sunFarFlags(sunFar ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
		Effect->SetVector(h, &sunFarFlags);
	}
	else sunFar = nullptr;
	// UNOFFICIAL Temporal: last frame's bounce is usable if it was made a moment ago in the same cell (a load or a door means
	// another place); the depth test in the shader catches the rest.
	const double now = TheFrameRateManager->Time;
	const bool continuous = historyValid && now - lastTime >= 0.0 && now - lastTime < 0.5 && !TheShaderManager->GameState.isCellChanged;
	D3DXHANDLE temporalHandle = ParameterHandle(paramTemporal, "NVR_ALTemporal");
	D3DXHANDLE reprojectHandle = ParameterHandle(paramReproject, "NVR_ALReproject");
	const int readIndex = historyIndex, writeIndex = historyIndex ^ 1;
	if (temporalHandle && reprojectHandle) {
		static const float HistoryShare = 0.9f;		// last frame's share for a spot that has not moved
		static const float MotionPixels = 4.0f;		// half as much for one that moved this many half-resolution pixels
		D3DXVECTOR4 temporalData(temporal && continuous ? HistoryShare : 0.0f, MotionPixels, (float)(frameNumber % 1024), 0.0f);
		D3DXMATRIX reproject;
		D3DXMatrixIdentity(&reproject);
		if (continuous) BounceLightReprojection(TheRenderManager->viewMatrix, TheRenderManager->InvViewMatrix, lastView, lastInvView, lastProj, &reproject);
		Effect->SetVector(temporalHandle, &temporalData);
		Effect->SetMatrix(reprojectHandle, &reproject);
	}
	static GpuTimer passTimers[8] = { GpuTimer("  Ambient light prepare (half)"), GpuTimer("  Ambient light estimate (half)"),
		GpuTimer("  Ambient light blur X (half)"), GpuTimer("  Ambient light blur Y (half)"), GpuTimer("  Ambient light accumulate (half)"),
		GpuTimer("  Ambient light balance samples (indoors)"), GpuTimer("  Ambient light balance (indoors)"), GpuTimer("  Ambient light apply (full)") };
	D3DVIEWPORT9 statsViewport = { 0, 0, (DWORD)StatsWidth, (DWORD)StatsHeight, 0.0f, 1.0f }, balanceViewport = { 0, 0, 1, 1, 0.0f, 1.0f };
	DWORD writeMask1 = 0xF;	// UNOFFICIAL 2026-10-18 (ambfix): Accumulate's second target, all four channels (see RenderGrid)
	Device->GetRenderState(D3DRS_COLORWRITEENABLE1, &writeMask1);
	Device->SetRenderState(D3DRS_COLORWRITEENABLE1, 0xF);
	UINT passes = 0;
	HRESULT result = Effect->Begin(&passes, 0);
	if (SUCCEEDED(result)) {
		for (UINT p = 0; p < 8 && SUCCEEDED(result); ++p) {
			// Prepare -> prep; Estimate (prep) -> 0; blur X (0) -> 1; blur Y (1) -> 0; Accumulate (0, last history) -> this
			// frame's history; (indoors) balance samples -> stats, balance (stats, last balance) -> this frame's balance;
			// Apply (0 for the occlusion, this frame's history for the bounce) -> the frame.
			if ((p == 5 || p == 6) && !balanceRuns) continue;
			const bool apply = p == 7;
			IDirect3DSurface9* destination = apply ? finalTarget : p == 0 ? prepSurface : p == 4 ? historySurface[writeIndex] :
				p == 5 ? statsSurface : p == 6 ? balanceSurface[balanceWrite] : bufferSurface[p == 2 ? 1 : 0];
			Device->SetTexture(5, nullptr);
			Device->SetTexture(6, nullptr);
			Device->SetTexture(7, nullptr);
			result = Device->SetRenderTarget(0, destination);
			// UNOFFICIAL: the occlusion's history is Accumulate's second target.
			if (SUCCEEDED(result)) result = Device->SetRenderTarget(1, p == 4 ? occlusionHistorySurface[writeIndex] : nullptr);
			if (SUCCEEDED(result)) result = Device->SetViewport(apply ? &original : p == 5 ? &statsViewport : p == 6 ? &balanceViewport : &reduced);
			if (SUCCEEDED(result)) result = Effect->BeginPass(p);
			if (FAILED(result)) break;
			if (p > 0) Device->SetTexture(5, prepTexture);
			Device->SetTexture(15, TheTextureManager->DepthTextureViewModel);	// NVR_ALViewModel (Prepare, Apply)
			const bool applyLike = p == 5 || p == 7;	// the balance samples read what Apply reads
			if (p == 2 || p == 4 || p == 7) Device->SetTexture(6, bufferTexture[0]);
			else if (p == 3) Device->SetTexture(6, bufferTexture[1]);
			else if (p == 6) Device->SetTexture(6, statsTexture);								// NVR_ALStats
			if (p == 4) Device->SetTexture(7, historyTexture[readIndex]);
			else if (p == 6) Device->SetTexture(7, balanceTexture[balanceRead]);				// NVR_ALBalance: last frames'
			else if (p == 7 && interiorMode) Device->SetTexture(7, balanceTexture[balanceRuns ? balanceWrite : balanceRead]);
			else if (p == 7) Device->SetTexture(7, historyTexture[writeIndex]);
			if (applyLike && gridValid) Device->SetTexture(9, gridAccum[gridAccumIndex]);
			if (applyLike && gridValid && gridColorValid) Device->SetTexture(14, gridAccumColor[gridAccumIndex]);
			if (p == 7 && gridScreenValid) Device->SetTexture(8, gridScreen[gridScreenIndex]);	// DebugView 8
			if (p == 7 && sunFar) {	// UNOFFICIAL 2026-10-19 SunFar (NVR_SunFarAL) on the Apply pass (pass 7 since InteriorBalance), point sampled
				Device->SetTexture(13, sunFar);
				const DWORD states[][2] = { {D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP}, {D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP},
					{D3DSAMP_MAGFILTER, D3DTEXF_POINT}, {D3DSAMP_MINFILTER, D3DTEXF_POINT}, {D3DSAMP_MIPFILTER, D3DTEXF_NONE} };
				for (const auto& s : states) TheRenderManager->SetSamplerState(13, (D3DSAMPLERSTATETYPE)s[0], s[1]);
			}
			if (p == 4) Device->SetTexture(11, occlusionHistory[readIndex]);
			else if (applyLike) Device->SetTexture(11, occlusionHistory[writeIndex]);
			if (p == 7 && skyTexture) {	// UNOFFICIAL 2026-10-20 (skyamb): the sky target for Apply (NVR_ALSkyVis)
				Device->SetTexture(10, skyTexture);
				TheRenderManager->renderState->NoteDeviceTexture(10, skyTexture);
			}
			Device->SetTexture(1, scene);	// TESR_SourceBuffer slot: the current image
			{
				GpuProfileScope gpu(passTimers[p], Device);
				result = Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
			}
			HRESULT endResult = Effect->EndPass();
			if (SUCCEEDED(result)) result = endResult;
		}
		Effect->End();
	}
	Device->SetTexture(5, nullptr);
	Device->SetTexture(6, nullptr);
	Device->SetTexture(7, nullptr);
	Device->SetTexture(9, nullptr);
	Device->SetTexture(11, nullptr);
	Device->SetTexture(14, nullptr);
	Device->SetTexture(15, nullptr);
	Device->SetTexture(8, nullptr);
	if (sunFar) Device->SetTexture(13, nullptr);
	if (skyTexture) {	// UNOFFICIAL 2026-10-20 (skyamb)
		Device->SetTexture(10, nullptr);
		TheRenderManager->renderState->NoteDeviceTexture(10, nullptr);
	}
	Device->SetRenderTarget(1, nullptr);
	Device->SetRenderState(D3DRS_COLORWRITEENABLE1, writeMask1);
	Device->SetTexture(1, TheTextureManager->SourceTexture);
	// UNOFFICIAL Temporal: this frame's history is next frame's "last frame".
	if (SUCCEEDED(result) && balanceRuns) {
		balanceIndex = balanceWrite;	// this frame's balance is next frame's "last"
		balanceValid = true;
	}
	if (SUCCEEDED(result)) {
		historyIndex = writeIndex;
		historyValid = true;
		lastView = TheRenderManager->viewMatrix;
		lastInvView = TheRenderManager->InvViewMatrix;
		lastProj = TheRenderManager->projMatrix;
		lastTime = now;
		frameNumber++;
	}
	else historyValid = false;
	Device->SetRenderTarget(0, RenderTarget);
	Device->SetViewport(&original);
	Device->SetDepthStencilSurface(depthSurface);
	if (depthSurface) depthSurface->Release();
	if (SUCCEEDED(result)) {
		if (chained) chain.Commit();
		else Device->StretchRect(RenderTarget, NULL, RenderedSurface, NULL, D3DTEXF_NONE);
		shadowApplied = shadow;
	}
	else {
		static bool reported = false;
		if (!reported) Logger::Log("UNOFFICIAL AmbientLight: render failed (%08lx).", result);
		reported = true;
	}
	renderTime = timer.LogTime("AmbientLight::Render");
}
