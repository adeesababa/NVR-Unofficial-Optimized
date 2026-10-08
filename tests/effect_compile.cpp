#include <windows.h>
#include <d3dx9.h>
#include <cstdio>

static bool CompileOne(const char* path, const char* forwardShadows) {
	D3DXMACRO macros[] = { { "FORWARD_SHADOWS", forwardShadows }, { NULL, NULL } };
	ID3DXBuffer *source = nullptr, *errors = nullptr, *effect = nullptr;
	ID3DXEffectCompiler* compiler = nullptr;
	HRESULT hr = D3DXPreprocessShaderFromFileA(path, macros, NULL, &source, &errors);
	bool ok = SUCCEEDED(hr);
	if (ok) {
		if (errors) { errors->Release(); errors = nullptr; }
		hr = D3DXCreateEffectCompiler((const char*)source->GetBufferPointer(), source->GetBufferSize(), NULL, NULL, 0, &compiler, &errors);
		ok = SUCCEEDED(hr);
		if (ok) {
			if (errors) { errors->Release(); errors = nullptr; }
			hr = compiler->CompileEffect(0, &effect, &errors);
			ok = SUCCEEDED(hr);
		}
	}
	std::printf("%s %s (FORWARD_SHADOWS %s)%s\n", ok ? "PASS:" : "FAIL:", path, forwardShadows, ok && effect ? "" : "");
	if (!ok && errors) std::printf("%s\n", (const char*)errors->GetBufferPointer());
	if (effect) effect->Release();
	if (compiler) compiler->Release();
	if (errors) errors->Release();
	if (source) source->Release();
	return ok;
}

int main(int argc, char** argv) {
	if (argc < 2) { std::puts("usage: effect_compile <effect .fx.hlsl file> [more files...]"); return 2; }
	int failures = 0;
	for (int i = 1; i < argc; i++) {
		if (!CompileOne(argv[i], "0")) failures++;
		if (!CompileOne(argv[i], "1")) failures++;
	}
	std::printf(failures ? "%d effect compile(s) FAILED\n" : "All effects compiled\n", failures);
	return failures ? 1 : 0;
}
