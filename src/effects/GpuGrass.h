#pragma once

namespace GpuGrass {
	void	ReadSettings();
	bool	BeginEarly(IDirect3DDevice9* Device, bool GrassEnabled);
	void	Arm(void* Pass, int Part, float Nearest);
	void	Disarm();
	void	EndEarly(IDirect3DDevice9* Device);
}
