#pragma once
namespace LandLight {
	inline bool		Pending = false;
	inline float	Dir[3] = { 0.0f, 0.0f, 0.0f };
	inline UInt32	Frame = 0;
	inline UInt32	Now = 0;

	inline void Read() {
		Pending = false;
		float v[4] = {};
		if (FAILED(TheRenderManager->device->GetPixelShaderConstantF(18, v, 1))) return;
		const float length = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
		if (length < 0.98f || length > 1.02f || v[2] <= 0.0f) return;
		const D3DXVECTOR4& sun = TheShaderManager->ShaderConst.SunDir;
		const float a = sqrtf(v[0] * v[0] + v[1] * v[1]), b = sqrtf(sun.x * sun.x + sun.y * sun.y);
		if (a > 0.05f && b > 0.05f && (v[0] * sun.x + v[1] * sun.y) / (a * b) < 0.99f) return;
		Dir[0] = v[0] / length; Dir[1] = v[1] / length; Dir[2] = v[2] / length;
		Frame = Now;
	}
}
