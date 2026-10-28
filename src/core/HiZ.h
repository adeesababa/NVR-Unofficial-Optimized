#pragma once


#include "DxvkCompute.h"

class HiZ {
public:
	static const UINT Levels = 7;
	static void					Build(IDirect3DDevice9* Device, bool Wanted);
	static ID3D9NvrExtBuffer*	Current();
	static bool					Failed() { return failed; }
	static int					CheckEvery;

private:
	static void					Check(IDirect3DTexture9* depth, float invertedDepth);
	static bool					Ensure();
	static void					Release();
	static UINT					LevelOffset(UINT k);

	static ID3D9NvrExtBuffer*	buffer;
	static ID3D9NvrExtPipeline*	pipeline;
	static UINT					width, height;
	static UINT					groupsX, groupsY;
	static UINT					builtFrame;
	static bool					built;
	static bool					failed;
	static UINT					frames;
};
