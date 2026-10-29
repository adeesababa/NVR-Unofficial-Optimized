#pragma once


namespace RtSunShadows {
	void	Register();
	void	Apply(IDirect3DDevice9* Device);
	bool	Replacing();
	float	ReplaceRange();
	D3DXVECTOR4* ReplaceConstant();
	void	RenderDebug(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget);
}
