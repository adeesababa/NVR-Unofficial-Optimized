#pragma once

class GrassShaders : public ShaderCollection
{
public:
	GrassShaders() : ShaderCollection("Grass") {};

	struct GrassStruct {
		D3DXVECTOR4		Scale;
	};
	GrassStruct	Constants;
	bool		GameShaderFix = true;
	bool		DrawFirst = false;
	int			DrawFirstRestore = 5;
	bool		DrawFirstCull = true;
	static inline int	DrawPart = 0;
	static inline float	LastPairing[4] = {};
	static inline bool	LastBindNVR = false;
	static inline unsigned	BindSerial = 0;
	static inline const char*	LastBindName = nullptr;

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
	void	SetForDraw(NiD3DVertexShaderEx* VertexShader, NiD3DPixelShaderEx* PixelShader);
};