#pragma once

#include <cstdlib>

inline bool IsIdentityLUT(IDirect3DTexture9* texture)
{
	D3DSURFACE_DESC desc;
	if (!texture || FAILED(texture->GetLevelDesc(0, &desc))) return false;
	if (desc.Format != D3DFMT_A8R8G8B8 && desc.Format != D3DFMT_X8R8G8B8) return false;

	const UINT n = desc.Height;
	if (n < 2 || desc.Width != n * n) return false;

	D3DLOCKED_RECT rect;
	if (FAILED(texture->LockRect(0, &rect, NULL, D3DLOCK_READONLY))) return false;

	bool identity = true;
	for (UINT g = 0; g < n && identity; g++) {
		const BYTE* row = (const BYTE*)rect.pBits + (size_t)g * rect.Pitch;
		const int expectedG = (int)(g * 255.0f / (n - 1) + 0.5f);
		for (UINT b = 0; b < n && identity; b++) {
			const int expectedB = (int)(b * 255.0f / (n - 1) + 0.5f);
			for (UINT r = 0; r < n; r++) {
				const BYTE* px = row + (size_t)(b * n + r) * 4;
				const int expectedR = (int)(r * 255.0f / (n - 1) + 0.5f);
				if (abs((int)px[2] - expectedR) > 1 || abs((int)px[1] - expectedG) > 1 || abs((int)px[0] - expectedB) > 1) {
					identity = false;
					break;
				}
			}
		}
	}
	texture->UnlockRect(0);
	return identity;
}
