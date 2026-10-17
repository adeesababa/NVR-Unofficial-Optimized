#pragma once

class GrassShaders : public ShaderCollection
{
public:
	GrassShaders() : ShaderCollection("Grass") {};

	struct GrassStruct {
		D3DXVECTOR4		Scale;
	};
	GrassStruct	Constants;
	bool		GameShaderFix = true;	// UNOFFICIAL 2026-10-17 [Shaders.Grass.Main] GameShaderFix (see SetForDraw)

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
	void	SetForDraw(NiD3DVertexShaderEx* VertexShader, NiD3DPixelShaderEx* PixelShader);
};