#pragma once

class SharpeningEffect : public EffectRecord
{
public:
	SharpeningEffect() : EffectRecord("Sharpening") {};

	struct SharpeningStruct {
		D3DXVECTOR4		Data;
	};
	SharpeningStruct	Constants;
	D3DXVECTOR4			Exterior = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);
	D3DXVECTOR4			Interior = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();

};