// UNOFFICIAL 2026-10-29: compile one game-shader template variant offline with the game's D3DX (D3DXCompileShaderFromFile, ps/vs_3_0),
// print errors and the instruction slot count. Usage: template_compile.exe <template.hlsl> <profile> NAME[=VALUE] ...
#include <windows.h>
#include <d3dx9.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
int main(int argc, char** argv) {
	if (argc < 3) { printf("usage: template_compile <file> <profile> NAME[=VALUE]...\n"); return 2; }
	std::vector<std::string> names, values;
	for (int i = 3; i < argc; i++) { std::string a = argv[i]; size_t e = a.find('='); names.push_back(a.substr(0, e)); values.push_back(e == std::string::npos ? "" : a.substr(e + 1)); }
	std::vector<D3DXMACRO> macros;
	for (size_t i = 0; i < names.size(); i++) macros.push_back({ names[i].c_str(), values[i].c_str() });
	macros.push_back({ NULL, NULL });
	ID3DXBuffer* code = nullptr; ID3DXBuffer* errors = nullptr;
	const DWORD t0 = GetTickCount();
	HRESULT hr = D3DXCompileShaderFromFileA(argv[1], macros.data(), NULL, "main", argv[2], 0, &code, &errors, NULL);
	const DWORD ms = GetTickCount() - t0;
	if (errors) printf("%s\n", (const char*)errors->GetBufferPointer());
	if (FAILED(hr) || !code) { printf("FAILED (%lu ms)\n", ms); return 1; }
	ID3DXBuffer* dis = nullptr; int slots = -1;
	if (SUCCEEDED(D3DXDisassembleShader((const DWORD*)code->GetBufferPointer(), FALSE, NULL, &dis)) && dis) {
		const char* a = strstr((const char*)dis->GetBufferPointer(), "approximately ");
		if (a) slots = atoi(a + 14);
	}
	printf("OK %d instruction slots, %lu ms\n", slots, ms);
	return 0;
}
