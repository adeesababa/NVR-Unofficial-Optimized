#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace LandHeights {

	struct Plugin {
		std::string	Name;
	};

	struct Result {
		std::vector<float>	Heights;
		std::vector<float>	Peaks;
		uint32_t	Width = 0, Height = 0;
		float		West = 0.0f, North = 0.0f;
		float		Texel = 128.0f;
		float		Low = 0.0f, High = 0.0f;
		int			CellWest = 0, CellSouth = 0, CellEast = 0, CellNorth = 0;
		uint32_t	Cells = 0;
		std::string	Message;
	};

	bool	Read(const std::vector<Plugin>& Plugins, const std::string& WorldFile, uint32_t WorldObjectID, uint32_t MaxResolution, Result& Out);

	bool	Inflate(const uint8_t* In, size_t InSize, std::vector<uint8_t>& Out, size_t Limit);
}
