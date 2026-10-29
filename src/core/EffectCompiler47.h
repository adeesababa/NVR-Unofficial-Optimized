#pragma once

#include <windows.h>
#include <d3dx9.h>
#include <fstream>
#include <iterator>
#include <string>

namespace EffectCompiler47 {

	typedef HRESULT(WINAPI* D3DCompileFn)(LPCVOID, SIZE_T, LPCSTR, const void*, void*, LPCSTR, LPCSTR, UINT, UINT, ID3DXBuffer**, ID3DXBuffer**);

	inline D3DCompileFn Function() {
		static const D3DCompileFn fn = []() -> D3DCompileFn {
			HMODULE module = LoadLibraryA("d3dcompiler_47.dll");
			return module ? (D3DCompileFn)GetProcAddress(module, "D3DCompile") : nullptr;
		}();
		return fn;
	}

	inline bool Use = false;
	inline void ReadSetting() {
		SettingManager::Configuration::ConfigNode node;
		const bool wanted = TheSettingManager->Config.FillNode(&node, "Main.Main.Performance", "NewEffectCompiler") ? node.BoolValue : true;
		Use = wanted && Function();
	}

	inline std::string CacheText(ID3DXBuffer* source) {
		std::string text = ShaderProgram::PortableSource(source);
		if (Use) text += "\n// NVR: effect compiled with d3dcompiler_47 ([Main.Main.Performance] NewEffectCompiler)\n";
		return text;
	}
	inline bool CacheFresh(const char* path, ID3DXBuffer* source) {
		std::ifstream file(path, std::ios::in | std::ios::binary);
		if (!file) return false;
		const std::string cached((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		return !cached.empty() && cached == CacheText(source);
	}

	inline bool Compile(ID3DXBuffer* source, const char* name, ID3DXBuffer** binary, ID3DXBuffer** errors) {
		*binary = nullptr;
		D3DCompileFn fn = Use ? Function() : nullptr;
		if (!fn || !source) return false;
		if (SUCCEEDED(fn(source->GetBufferPointer(), source->GetBufferSize(), name, nullptr, nullptr, nullptr, "fx_2_0", 0, 0, binary, errors)) && *binary)
			return true;
		if (*binary) { (*binary)->Release(); *binary = nullptr; }
		return false;
	}
}
