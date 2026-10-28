#pragma once

class GpuParticles {
public:
	static bool		Wanted();
	static void		Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget);

private:
	static bool		Ready();
};
