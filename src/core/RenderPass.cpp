#include "RenderPass.h"
#include "ClothWind.h"
#include "ShadowBoneUpload.h"
#include "OrthoProbe.h"	// UNOFFICIAL 2026-10-17 test tools: ortho map caster dump


void RenderPass::BindDiffuseOf(NiGeometry* Geo) {
	ShadowsExteriorEffect::ShadowStruct* ShadowConstants = &TheShaderManager->Effects.ShadowsExteriors->Constants;
	ShadowConstants->Data.w = 0.0f;
	if (!BindDiffuse) return;
	BSShaderProperty* ShaderProperty = (BSShaderProperty*)Geo->GetProperty(NiProperty::PropertyType::kType_Shade);
	if (!ShaderProperty || !ShaderProperty->IsLightingProperty()) return;
	NiSourceTexture** textures = ((BSShaderPPLightingProperty*)ShaderProperty)->ppTextures[0];
	NiTexture* Texture = textures ? *textures : nullptr;
	if (!Texture || !Texture->rendererData || !Texture->rendererData->dTexture) return;
	NiDX9RenderState* RenderState = TheRenderManager->renderState;
	RenderState->SetTexture(0, Texture->rendererData->dTexture);
	RenderState->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP, false);
	RenderState->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP, false);
	RenderState->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR, false);
	RenderState->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR, false);
	RenderState->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR, false);
	ShadowConstants->Data.w = 1.0f;
}


// The caster's world matrix for the sun map (camera relative, row vectors: _43 is the height). Land is lowered by LandDrop.
void RenderPass::UpdateShadowWorld(NiGeometry* Geo) {
	TheRenderManager->CreateD3DMatrix(&TheShaderManager->ShaderConst.ShadowWorld, &Geo->m_worldTransform);
	if (LandDrop > 0.0f && LandSet.count(Geo)) TheShaderManager->ShaderConst.ShadowWorld._43 -= LandDrop;
}


void RenderPass::RenderAccum() {
	if (GeometryList.empty()) return;

	// Could add setup of device/renderstate/current shaders here
	NiDX9RenderState* RenderState = TheRenderManager->renderState;
	RenderState->SetPixelShader(PixelShader->ShaderHandle, false);
	RenderState->SetVertexShader(VertexShader->ShaderHandle, false);

	// Render normal geometry
	while (!GeometryList.empty()) {
		// UNOFFICIAL (Fable): pop before the null check. The old top()/continue order left a null entry on the list
		// and never ended the loop.
		NiGeometry* Geo = GeometryList.top();
		GeometryList.pop();
		if (!Geo) continue;

		UpdateConstants(Geo);
		PixelShader->SetCT();
		VertexShader->SetCT();
		ClothWind::ForGeometry(TheRenderManager->device, Geo, true);	// UNOFFICIAL cloth wind: the cloth's shadow moves with it
#if NVR_TEST_TOOLS
		if (OrthoProbe::Recording) OrthoProbe::OnDraw(this, Geo);	// UNOFFICIAL 2026-10-17: only while an ortho dump is recorded
#endif

		RenderGeometry(Geo);
		ClothWind::AfterDraw(TheRenderManager->device);	// UNOFFICIAL 2026-10-19: no cloth values left behind for other draws
	}
}


void RenderPass::RenderGeometry(NiGeometry* Geo) {
	NiGeometryData* ModelData = Geo->geomData;
	NiGeometryBufferData* GeoData = ModelData->m_pkBuffData;
	NiD3DShaderDeclaration* ShaderDeclaration = Geo->shader->ShaderDeclaration;

	// Set proper cull based on stencil property.
	NiProperty* pProp = Geo->GetProperty(NiProperty::PropertyType::kType_Stencil);

	NiDX9RenderState* RenderState = TheRenderManager->renderState;
	if (RenderPass::TwoSided) {
		RenderState->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE, RenderStateArgs);	// UNOFFICIAL TwoSidedCasters
	}
	else if (pProp) {
		NiStencilProperty* pStencilProp = static_cast<NiStencilProperty*>(pProp);
		RenderState->SetCullMode(pStencilProp->GetDrawMode());
	}
	else {
		RenderState->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW, RenderStateArgs);
	}

	TheRenderManager->PackGeometryBuffer(GeoData, ModelData, NULL, ShaderDeclaration);
	if (GeoData && GeoData->VertCount) DrawGeometryBuffer(Geo, GeoData);
}


// draws the geo data from the Geometry buffer
void RenderPass::DrawGeometryBuffer(NiGeometry* Geo, NiGeometryBufferData* GeoData) {
	int StartIndex = 0;
	int PrimitiveCount = 0;

	for (UInt32 i = 0; i < GeoData->StreamCount; i++) {
		TheRenderManager->device->SetStreamSource(i, GeoData->VBChip[i]->VB, 0, GeoData->VertexStride[i]);
	}

	if (GeoData->IB)
		TheRenderManager->device->SetIndices(GeoData->IB);
	if (GeoData->FVF)
		TheRenderManager->renderState->SetFVF(GeoData->FVF, false);
	else
		TheRenderManager->renderState->SetVertexDeclaration(GeoData->VertexDeclaration, false);

	uint16_t dirtyFlags = Geo->geomData->m_usDirtyFlags;
	NiTriStrips* pStrips = Geo->IsTriStrips();
	if (pStrips) {
		ThisStdCall(0xE74840, NiDX9Renderer::GetSingleton(), Geo);  // RenderTriStripsAlt
	}
	else {
		ThisStdCall(0xE745A0, NiDX9Renderer::GetSingleton(), Geo);  // RenderTriShapeAlt
	}
	// Add back the dirty flag that are reset with vanilla render functions.
	Geo->geomData->m_usDirtyFlags = dirtyFlags;
}

void RenderPass::DrawSkinnedGeometryBuffer(NiGeometry* Geo, NiGeometryBufferData* GeoData, NiSkinPartition::Partition* Partition) {
	int StartIndex = 0;
	int PrimitiveCount = 0;

	for (UInt32 i = 0; i < GeoData->StreamCount; i++) {
		TheRenderManager->device->SetStreamSource(i, GeoData->VBChip[i]->VB, 0, GeoData->VertexStride[i]);
	}

	if (GeoData->IB)
		TheRenderManager->device->SetIndices(GeoData->IB);

	if (GeoData->FVF)
		TheRenderManager->renderState->SetFVF(GeoData->FVF, false);
	else
		TheRenderManager->renderState->SetVertexDeclaration(GeoData->VertexDeclaration, false);

	ThisStdCall(0xE6D310, NiDX9Renderer::GetSingleton(), GeoData, Partition, nullptr);  // DrawSkinnedGeometry
}


ShadowRenderPass::ShadowRenderPass() {
	PixelShader = TheShadowManager->ShadowMapPixel;
	VertexShader = TheShadowManager->ShadowMapVertex;
	RegisterConstants();
}


int ShadowRenderPass::TestObject(NiGeometry* Geo) {
	if (!Geo->geomData || !Geo->geomData->m_pkBuffData) return 0; // discard objects without buffer data

	BSShaderProperty* ShaderProperty = (BSShaderProperty*)Geo->GetProperty(NiProperty::PropertyType::kType_Shade);
	if (!ShaderProperty || !ShaderProperty->IsLightingProperty()) return 0;
	return 1;
}

bool ShadowRenderPass::AccumObject(NiGeometry* Geo) {
	const int test = TestObject(Geo);
	if (test == 1) GeometryList.push(Geo);
	return test != 0;
}


void ShadowRenderPass::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_ShadowWorldTransform", (D3DXVECTOR4*)&TheShaderManager->ShaderConst.ShadowWorld);
	//TheTextureManager->RegisterTexture("TESR_DiffuseMap", &Constants.DiffuseMap);
}


void ShadowRenderPass::UpdateConstants(NiGeometry* Geo) {
	ShadowsExteriorEffect::ShadowStruct* ShadowConstants = &TheShaderManager->Effects.ShadowsExteriors->Constants;
	ShadowConstants->Data.x = 0.0f; // Type of geo (0 normal, 1 actors (skinned), 2 speedtree leaves)
	ShadowConstants->Data.y = 0.0f; // Alpha Control
	UpdateShadowWorld(Geo);
	BindDiffuseOf(Geo);	// UNOFFICIAL light grid colour (nothing unless RenderPass::BindDiffuse)
}



AlphaShadowRenderPass::AlphaShadowRenderPass() {
	PixelShader = TheShadowManager->ShadowMapPixel;
	VertexShader = TheShadowManager->ShadowMapVertex;
	RegisterConstants();
}


int AlphaShadowRenderPass::TestObject(NiGeometry* Geo) {
	if (!Geo->geomData || !Geo->geomData->m_pkBuffData) return 0; // discard objects without buffer data

	BSShaderProperty* ShaderProperty = (BSShaderProperty*)Geo->GetProperty(NiProperty::PropertyType::kType_Shade);
	NiAlphaProperty* AProp = (NiAlphaProperty*)Geo->GetProperty(NiProperty::PropertyType::kType_Alpha);

	if (!ShaderProperty || !ShaderProperty->IsLightingProperty()) return 0;
	if (!AProp) return 0;
	if (!(AProp->flags & NiAlphaProperty::AlphaFlags::ALPHA_BLEND_MASK) && !(AProp->flags & NiAlphaProperty::AlphaFlags::TEST_ENABLE_MASK)) return 0;
	return 1;
}

bool AlphaShadowRenderPass::AccumObject(NiGeometry* Geo) {
	const int test = TestObject(Geo);
	if (test == 1) GeometryList.push(Geo);
	return test != 0;
}


void AlphaShadowRenderPass::RegisterConstants() {
}


void AlphaShadowRenderPass::UpdateConstants(NiGeometry* Geo) {
	ShadowsExteriorEffect::ShadowStruct* ShadowConstants = &TheShaderManager->Effects.ShadowsExteriors->Constants;
	ShadowConstants->Data.x = 0.0f; // Type of geo (0 normal, 1 actors (skinned), 2 speedtree leaves)
	ShadowConstants->Data.y = 0.0f; // Alpha Control
	UpdateShadowWorld(Geo);

	BSShaderProperty* ShaderProperty = (BSShaderProperty*)Geo->GetProperty(NiProperty::PropertyType::kType_Shade);
	NiTexture* Texture = *((BSShaderPPLightingProperty*)ShaderProperty)->ppTextures[0];

	// UNOFFICIAL (Fable): rendererData can be null for a texture the renderer has not created yet.
	if (Texture && Texture->rendererData && Texture->rendererData->dTexture) {

		ShadowConstants->Data.y = 1.0f; // Alpha Control
//			Constants.DiffuseMap = Texture->rendererData->dTexture;

		//// Set diffuse texture at register 0
		NiDX9RenderState* RenderState = TheRenderManager->renderState;
		RenderState->SetTexture(0, Texture->rendererData->dTexture);
		RenderState->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP, false);
		RenderState->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP, false);
		RenderState->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT, false);
		RenderState->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT, false);
		RenderState->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_POINT, false);
	}
}


SkinnedGeoShadowRenderPass::SkinnedGeoShadowRenderPass() {
	PixelShader = TheShadowManager->ShadowMapPixel;
	VertexShader = TheShadowManager->ShadowMapVertex;
	RegisterConstants();
}


int SkinnedGeoShadowRenderPass::TestObject(NiGeometry* Geo) {
	// check data for rigged geometry
	if (!Geo->skinInstance)
		return 0;

	NiSkinInstance* skinInstance = Geo->skinInstance;
	if (skinInstance->IsKindOf<BSDismemberSkinInstance>() && !((BSDismemberSkinInstance*)skinInstance)->IsRenderable)
		return 2; // claimed as skinned geometry, nothing to draw

	// only accum if valid data present; claimed in any case because we still found skinned geo either way
	if (Geo->skinInstance->SkinPartition && Geo->skinInstance->SkinPartition->Partitions)
		return 1;

	return 0;
}

bool SkinnedGeoShadowRenderPass::AccumObject(NiGeometry* Geo) {
	const int test = TestObject(Geo);
	if (test == 1) GeometryList.push(Geo);
	return test != 0;
}


void SkinnedGeoShadowRenderPass::RegisterConstants() {
	//TheShaderManager->RegisterConstant("TESR_Bones", NULL);
}


void SkinnedGeoShadowRenderPass::UpdateConstants(NiGeometry* Geo) {
	// Render skinned geometry
	ShadowsExteriorEffect::ShadowStruct* Constants = &TheShaderManager->Effects.ShadowsExteriors->Constants;
	Constants->Data.x = 1.0f; // Type of geo (0 normal, 1 actors (skinned), 2 speedtree leaves)
	Constants->Data.y = 0.0f; // Alpha control
	TheRenderManager->CreateD3DMatrix(&TheShaderManager->ShaderConst.ShadowWorld, &Geo->m_worldTransform);
	BindDiffuseOf(Geo);	// UNOFFICIAL light grid colour
}


void SkinnedGeoShadowRenderPass::RenderGeometry(NiGeometry* Geo) {
	NiGeometryData* ModelData = Geo->geomData;
	NiGeometryBufferData* GeoData = ModelData->m_pkBuffData;
	NiSkinInstance* SkinInstance = Geo->skinInstance;
	NiD3DShaderDeclaration* ShaderDeclaration = Geo->shader->ShaderDeclaration;

	// Set proper cull based on stencil property.
	NiProperty* pProp = Geo->GetProperty(NiProperty::PropertyType::kType_Stencil);

	NiDX9RenderState* RenderState = TheRenderManager->renderState;
	if (RenderPass::TwoSided) {
		RenderState->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE, RenderStateArgs);	// UNOFFICIAL TwoSidedCasters
	}
	else if (pProp) {
		NiStencilProperty* pStencilProp = static_cast<NiStencilProperty*>(pProp);
		RenderState->SetCullMode(pStencilProp->GetDrawMode());
	}
	else {
		RenderState->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW, RenderStateArgs);
	}

	NiSkinPartition* SkinPartition = SkinInstance->SkinPartition;
	TheRenderManager->CalculateBoneMatrixes(SkinInstance, &Geo->m_worldTransform);
	
	if (SkinInstance->SkinToWorldWorldToSkin) 
		memcpy(&TheShaderManager->Effects.ShadowsExteriors->Constants.ShadowWorld, SkinInstance->SkinToWorldWorldToSkin, 0x40);

	DismemberPartition* pDismemberPartition = NULL;
	if (SkinInstance->IsKindOf<BSDismemberSkinInstance>())
		pDismemberPartition = ((BSDismemberSkinInstance*)SkinInstance)->partitions;

	for (UInt32 p = 0; p < SkinPartition->PartitionsCount; p++) {
		if (pDismemberPartition && !pDismemberPartition[p].Enabled)
			continue;

		NiSkinPartition::Partition* Partition = &SkinPartition->Partitions[p];
		if (!Partition)
			continue;

		GeoData = Partition->BuffData;
		if (GeoData) {
			UploadShadowBones(TheRenderManager->device, (const float*)SkinInstance->BoneMatrixes,
			                  Partition->pBones, Partition->Bones);

			//TheRenderManager->PackSkinnedGeometryBuffer(GeoData, ModelData, SkinInstance, Partition, ShaderDeclaration);
			DrawSkinnedGeometryBuffer(Geo, GeoData, Partition);
		}

		if (!pDismemberPartition)
			continue;

		for (UInt32 d = p + 1; d < SkinPartition->PartitionsCount; d++) {
			if (pDismemberPartition[p + 1].StartCap)
				break;

			++p;
			++Partition;
			if (pDismemberPartition[p].Enabled) {
				GeoData = Partition->BuffData;
				if (GeoData) {
					UploadShadowBones(TheRenderManager->device, (const float*)SkinInstance->BoneMatrixes,
					                  Partition->pBones, Partition->Bones);


					DrawSkinnedGeometryBuffer(Geo, GeoData, Partition);
				}
			}

		}
	}
}


SpeedTreeShadowRenderPass::SpeedTreeShadowRenderPass() {
	PixelShader = TheShadowManager->ShadowMapPixel;
	VertexShader = TheShadowManager->ShadowMapVertex;
	RegisterConstants();
}


void SpeedTreeShadowRenderPass::RegisterConstants() {
	//TheShaderManager->RegisterConstant("TESR_BillboardRight", Constants.BillboardRight);
	//TheShaderManager->RegisterConstant("TESR_BillboardUp", Constants.BillboardUp);
	//TheShaderManager->RegisterConstant("TESR_RockParams", (D3DXVECTOR4*)Pointers::ShaderParams::RockParams);
	//TheShaderManager->RegisterConstant("TESR_RustleParams", (D3DXVECTOR4*)Pointers::ShaderParams::RustleParams);
	//TheShaderManager->RegisterConstant("TESR_WindMatrices", (D3DXVECTOR4*)Pointers::ShaderParams::WindMatrixes);
	//TheShaderManager->RegisterConstant("TESR_LeafBase", Constants.LeafBase);
	//TheTextureManager->RegisterTexture("TESR_LeafDiffuseMap", &Constants.DiffuseMap);
}


int SpeedTreeShadowRenderPass::TestObject(NiGeometry* Geo) {
	NiShadeProperty* shaderProp = static_cast<NiShadeProperty*>(Geo->GetProperty(NiProperty::kType_Shade));
	if (!shaderProp || shaderProp->m_eShaderType != NiShadeProperty::kProp_SpeedTreeLeaf) return 0;
	return 1;
}

bool SpeedTreeShadowRenderPass::AccumObject(NiGeometry* Geo) {
	const int test = TestObject(Geo);
	if (test == 1) GeometryList.push(Geo);
	return test != 0;
}


void SpeedTreeShadowRenderPass::UpdateConstants(NiGeometry* Geo) {

	ShadowsExteriorEffect::ShadowStruct* ShadowConstants = &TheShaderManager->Effects.ShadowsExteriors->Constants;
	IDirect3DDevice9* Device = TheRenderManager->device;
	NiDX9RenderState* RenderState = TheRenderManager->renderState;

	ShadowConstants->Data.x = 2.0f; // Type of geo (0 normal, 1 actors (skinned), 2 speedtree leaves)
	ShadowConstants->Data.y = 0.0f; // Alpha control
	TheRenderManager->CreateD3DMatrix(&TheShaderManager->ShaderConst.ShadowWorld, &Geo->m_worldTransform);

	// Bind constant values for leaf transformation
	Device->SetVertexShaderConstantF(63, (float*)&TheShadowManager->BillboardRight, 1);
	Device->SetVertexShaderConstantF(64, (float*)&TheShadowManager->BillboardUp, 1);
	Device->SetVertexShaderConstantF(65, Pointers::ShaderParams::RockParams, 1);
	Device->SetVertexShaderConstantF(66, Pointers::ShaderParams::RustleParams, 1);
	Device->SetVertexShaderConstantF(67, Pointers::ShaderParams::WindMatrixes, 16);

	SpeedTreeLeafShaderProperty* STProp = (SpeedTreeLeafShaderProperty*)Geo->GetProperty(NiProperty::PropertyType::kType_Shade);
	BSTreeNode* Node = (BSTreeNode*)Geo->m_parent->m_parent;
	NiDX9SourceTextureData* Texture = (NiDX9SourceTextureData*)Node->TreeModel->LeavesTexture->rendererData;

	if (Texture) ShadowConstants->Data.y = 1.0f;

	// Bind constant values for leaf transformation
	Device->SetVertexShaderConstantF(83, STProp->leafData->leafBase, 48);
	
	// Set diffuse texture at register 0
	// UNOFFICIAL (Fable): the leaves texture can have no renderer data yet; bind nothing then instead of crashing.
	RenderState->SetTexture(0, Texture ? Texture->dTexture : nullptr);
	RenderState->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP, false);
	RenderState->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP, false);
	RenderState->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT, false);
	RenderState->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT, false);
	RenderState->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_POINT, false);
}


TerrainLODPass::TerrainLODPass() {
	PixelShader = TheShadowManager->ShadowMapPixel;
	VertexShader = TheShadowManager->ShadowMapVertex;
	RegisterConstants();
}


int TerrainLODPass::TestObject(NiGeometry* Geo) {
	if (!Geo->geomData || !Geo->geomData->m_pkBuffData) return 0; // discard objects without buffer data

	BSShaderProperty* ShaderProperty = (BSShaderProperty*)Geo->GetProperty(NiProperty::PropertyType::kType_Shade);
	if (!ShaderProperty || !ShaderProperty->IsLightingProperty()) return 0;
	return 1;
}

bool TerrainLODPass::AccumObject(NiGeometry* Geo) {
	const int test = TestObject(Geo);
	if (test == 1) GeometryList.push(Geo);
	return test != 0;
}


void TerrainLODPass::UpdateConstants(NiGeometry* Geo) {
	ShadowsExteriorEffect::ShadowStruct* ShadowConstants = &TheShaderManager->Effects.ShadowsExteriors->Constants;
	ShadowConstants->Data.x = 3.0f; // Type of geo (0 normal, 1 actors (skinned), 2 speedtree leaves, 3 terrain LOD)
	ShadowConstants->Data.y = 0.0f; // Alpha Control
	TheRenderManager->CreateD3DMatrix(&TheShaderManager->ShaderConst.ShadowWorld, &Geo->m_worldTransform);
	BindDiffuseOf(Geo);	// UNOFFICIAL light grid colour
	D3DXMatrixTranspose(&Constants.WorldTranspose, &TheShaderManager->ShaderConst.ShadowWorld);

	BSShaderPPLightingProperty* prop = (BSShaderPPLightingProperty*)Geo->GetProperty(NiProperty::PropertyType::kType_Shade);

	Constants.LODLandParams.x = prop->fMorphDistance;
	Constants.LODLandParams.y = *BSShaderManager::fLODLandDrop;

	Constants.HighDetailRange.x = BSShaderManager::kLoadedRange->x - BSShaderManager::kCameraPos->x;
	Constants.HighDetailRange.y = BSShaderManager::kLoadedRange->y - BSShaderManager::kCameraPos->y;
	Constants.HighDetailRange.z = BSShaderManager::kLoadedRange->z - 15;
	Constants.HighDetailRange.w = BSShaderManager::kLoadedRange->w - 15;

	IDirect3DDevice9* Device = TheRenderManager->device;
	Device->SetVertexShaderConstantF(140, (float*)&Constants.WorldTranspose, 4);
	Device->SetVertexShaderConstantF(144, (float*)&Constants.HighDetailRange, 1);
	Device->SetVertexShaderConstantF(145, (float*)&Constants.LODLandParams, 1);
}
