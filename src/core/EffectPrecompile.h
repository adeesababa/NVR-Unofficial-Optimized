#pragma once

#include <algorithm>
#include <atomic>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include "EffectCompiler47.h"

namespace EffectPrecompile {

	struct Job {
		std::string Source, Preprocessed, Compiled;
		D3DXMACRO Macros[8] = {};
		unsigned long long Size = 0;
		std::atomic<bool> Done{ false };
		bool Waited = false;
	};

	inline std::vector<Job*> Jobs;
	inline std::unordered_map<std::string, Job*> ByCompiledPath;
	inline std::atomic<size_t> NextJob{ 0 };
	inline std::atomic<unsigned> CompiledCount{ 0 };
	inline bool Started = false;
	inline unsigned WaitedCount = 0;
	inline double StartTime = 0.0;

	inline bool WriteFile(const std::string& path, const void* data, size_t size) {
		FILE* f = nullptr;
		if (fopen_s(&f, path.c_str(), "wb") || !f) return false;
		fwrite(data, 1, size, f);
		fclose(f);
		return true;
	}

	inline void Run(Job* job) {
		ID3DXBuffer* source = nullptr;
		ID3DXBuffer* errors = nullptr;
		if (FAILED(D3DXPreprocessShaderFromFileA(job->Source.c_str(), job->Macros, NULL, &source, &errors)) || !source) {
			if (errors) errors->Release();
			if (source) source->Release();
			return;
		}
		if (errors) { errors->Release(); errors = nullptr; }
		if (!EffectCompiler47::CacheFresh(job->Preprocessed.c_str(), source) || !ShaderProgram::FileExists(job->Compiled.c_str())) {
			ID3DXEffectCompiler* compiler = nullptr;
			ID3DXBuffer* binary = nullptr;
			ID3DXBuffer* newErrors = nullptr;
			if (EffectCompiler47::Compile(source, job->Source.c_str(), &binary, &newErrors)) {
				const std::string portable = EffectCompiler47::CacheText(source);
				if (WriteFile(job->Compiled, binary->GetBufferPointer(), binary->GetBufferSize()))
					WriteFile(job->Preprocessed, portable.data(), portable.size());
				CompiledCount++;
				binary->Release();
				binary = nullptr;
			}
			else if (SUCCEEDED(D3DXCreateEffectCompiler((const char*)source->GetBufferPointer(), source->GetBufferSize(), NULL, NULL, NULL, &compiler, &errors)) && compiler) {
				if (errors) { errors->Release(); errors = nullptr; }
				if (SUCCEEDED(compiler->CompileEffect(NULL, &binary, &errors)) && binary) {
					const std::string portable = EffectCompiler47::CacheText(source);
					if (WriteFile(job->Compiled, binary->GetBufferPointer(), binary->GetBufferSize()))
						WriteFile(job->Preprocessed, portable.data(), portable.size());
					CompiledCount++;
				}
				compiler->Release();
			}
			if (binary) binary->Release();
			if (errors) errors->Release();
			if (newErrors) newErrors->Release();
		}
		source->Release();
	}

	inline void Worker() {
		for (;;) {
			const size_t i = NextJob.fetch_add(1);
			if (i >= Jobs.size()) return;
			Run(Jobs[i]);
			Jobs[i]->Done = true;
		}
	}

	inline void Start() {
		if (Started) return;
		Started = true;
		if (!TheSettingManager->GetSettingI("Main.Main.Performance", "ParallelShaderCompile")) return;
		StartTime = (double)GetTickCount64();
		EffectCompiler47::ReadSetting();
		D3DXMACRO macros[8] = {};
		EffectRecord::BuildMacros(macros);
		CreateDirectoryA(EffectsPath "Cache\\", NULL);
		WIN32_FIND_DATAA find;
		HANDLE h = FindFirstFileA(EffectsPath "*.fx.hlsl", &find);
		if (h == INVALID_HANDLE_VALUE) return;
		do {
			std::string file(find.cFileName);
			if (file.size() <= 8) continue;
			const std::string name = file.substr(0, file.size() - 8);
			Job* job = new Job();
			job->Source = std::string(EffectsPath) + file;
			job->Preprocessed = std::string(EffectsPath) + "Cache\\" + name + ".fx.hlsl";
			job->Compiled = std::string(EffectsPath) + "Cache\\" + name + ".fx";
			job->Size = ((unsigned long long)find.nFileSizeHigh << 32) | find.nFileSizeLow;
			std::copy(std::begin(macros), std::end(macros), job->Macros);
			Jobs.push_back(job);
			ByCompiledPath[job->Compiled] = job;
		} while (FindNextFileA(h, &find));
		FindClose(h);
		std::stable_sort(Jobs.begin(), Jobs.end(), [](const Job* a, const Job* b) { return a->Size > b->Size; });
		const unsigned cores = std::thread::hardware_concurrency();
		const unsigned threads = (std::max)(1u, (std::min)(cores > 1 ? cores - 1 : 1u, 12u));
		for (unsigned i = 0; i < threads; i++) std::thread(Worker).detach();
		Logger::Log("UNOFFICIAL ParallelShaderCompile: %u effects on %u threads (biggest first).", (unsigned)Jobs.size(), threads);
	}

	inline void Wait(const char* CompiledPath) {
		if (Jobs.empty()) return;
		auto it = ByCompiledPath.find(CompiledPath);
		if (it == ByCompiledPath.end() || it->second->Waited) return;
		Job* job = it->second;
		while (!job->Done) Sleep(1);
		job->Waited = true;
		if (++WaitedCount == 1 || WaitedCount == Jobs.size())
			Logger::Log("UNOFFICIAL ParallelShaderCompile: %u effect(s) ready, %u compiled so far, %.1f s after the start.", WaitedCount,
				CompiledCount.load(), ((double)GetTickCount64() - StartTime) / 1000.0);
	}
}
