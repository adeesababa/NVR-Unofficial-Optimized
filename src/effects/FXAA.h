#pragma once

class FXAAEffect : public EffectRecord
{
public:
	FXAAEffect() : EffectRecord("FXAA") {};

	bool	ShouldRender();
};
