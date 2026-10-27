// UNOFFICIAL 2026-10-27 Persistent wetness and snow ([Shaders.WetWorld.Main] / [Shaders.SnowAccumulation.Main] Persistent, CoverMap.h):
// reading the cover map, a top-down map round the camera that keeps wetness, puddle water and snow per spot. The effect declares
// sampler2D TESR_CoverMapBuffer (WRAP addressing) before including this. The map wraps round (a spot's texel is its world position
// modulo the map's size), so it never needs moving; only its window (the area it holds now) follows the camera.

float4 TESR_CoverMapData;	// x: 1 / the map's size (game units), yz: the camera's place in the map (texture coordinates), w: the map's size
float4 TESR_CoverMapWindow;	// xy: the window's corner relative to the camera (units), z: the window's size, w: the width of its faded border (units)
float4 TESR_CoverMapGlobal;	// xyz: wetness, puddle water and snow of open ground (used outside the window), w: 1 when the map holds data

// rel: a world position relative to the camera (only xy is used). Returns wetness, puddle water and snow there (xyz) and how much of
// that came from the map (w: 1 inside the window, fading to 0 at its border, where the open-ground values take over).
float4 CoverMapRead(float2 rel)
{
	float4 stored = tex2Dlod(TESR_CoverMapBuffer, float4(rel * TESR_CoverMapData.x + TESR_CoverMapData.yz, 0.0f, 0.0f));
	float2 fromCorner = rel - TESR_CoverMapWindow.xy;
	float2 edge = min(fromCorner, TESR_CoverMapWindow.z - fromCorner);
	float inside = saturate(min(edge.x, edge.y) / max(TESR_CoverMapWindow.w, 1.0f)) * TESR_CoverMapGlobal.w;
	return float4(lerp(TESR_CoverMapGlobal.xyz, stored.rgb, inside), inside);
}
