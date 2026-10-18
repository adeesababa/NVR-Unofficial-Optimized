#pragma once

struct ShaderTemplate {
	const char* Name;
	D3DXMACRO Defines[36];	// UNOFFICIAL 2026-10-17: 36 (was 30): BuildDefines appends up to 20+ and the interior-override record of a 6-define template was at 27 of the old cap 28
};
