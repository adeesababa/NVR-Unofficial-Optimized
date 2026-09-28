#pragma once

// Lean FXAA pass (Effects\FXAA.fx.hlsl), driven by [Main.Main.ReducedQuality] FXAAInsteadOfMSAA
// rather than a Shaders.FXAA section. The same switch turns the game's MSAA off at startup
// (NewVegas\Hooks\Settings.cpp).
class FXAAEffect : public EffectRecord
{
public:
	FXAAEffect() : EffectRecord("FXAA") {};

	bool	ShouldRender();
};
