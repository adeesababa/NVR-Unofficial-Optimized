#pragma once


#include "NvrDxvkExt.h"

namespace Upscaler {

	struct Inputs {
		IDirect3DBaseTexture9*	Color = nullptr;
		IDirect3DBaseTexture9*	Depth = nullptr;
		IDirect3DBaseTexture9*	Motion = nullptr;
		UINT	RenderWidth = 0, RenderHeight = 0;
		UINT	InputWidth = 0, InputHeight = 0;
		UINT	DisplayWidth = 0, DisplayHeight = 0;
		float	JitterX = 0.0f, JitterY = 0.0f;
		float	JitterPhases = 8.0f;
		float	NearZ = 1.0f, FarZ = 1.0f;
		float	TanHalfFovX = 1.0f, TanHalfFovY = 1.0f;
		float	DeltaSeconds = 0.0f;
		float	Sharpness = 0.0f;
		float	UnitsPerMetre = 70.0f;
		bool	Hdr = false;
		bool	Reset = false;
	};

	class IUpscaler {
	public:
		virtual ~IUpscaler() {}
		virtual const char*			Name() const = 0;
		virtual bool				Evaluate(IDirect3DDevice9* Device, const Inputs& In) = 0;
		virtual IDirect3DTexture9*	Output() const = 0;
		virtual bool				Failed() const = 0;
		virtual void				ReleaseResources() = 0;
		virtual void				RequestReset() {}
	};

	IUpscaler*	Fsr2();

	IUpscaler*	Fsr31();

	IUpscaler*	Dlss();

	void		DlssOptions(bool PassThroughTest, int JitterFlip);

	IUpscaler*	Choose(int setting);
}
