#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ShaderPrecompile {

	struct Job {
		std::string Name, Source, Preprocessed, Compiled;
		ShaderTemplate Template = {};
		std::string Log;
		ID3DXBuffer* SourceBuffer = nullptr;
		ID3DXBuffer* ShaderBuffer = nullptr;
		bool Preprocessed_ok = false;
		std::atomic<bool> Done{ false };
		bool Taken = false;
	};

	inline std::vector<Job*> Jobs;
	inline std::unordered_map<std::string, Job*> ByCompiledPath;
	inline std::atomic<size_t> NextJob{ 0 };
	inline bool Started = false;
	inline double StartTime = 0.0;

	inline void Paths(const char* Name, const char* SubPath, const char* CacheSubPath, const char* TemplateName, Job* job) {
		std::string base = ShadersPath, cache = std::string(ShadersPath) + "Cache\\";
		if (SubPath) base += SubPath;
		if (CacheSubPath) { cache += CacheSubPath; CreateDirectoryA(cache.c_str(), NULL); }
		else if (SubPath) cache += SubPath;
		job->Source = base + (TemplateName ? TemplateName : Name) + ".hlsl";
		job->Preprocessed = cache + Name + ".hlsl";
		job->Compiled = cache + Name;
	}

	inline void Run(Job* job) {
		ID3DXBuffer* errors = nullptr;
		HRESULT prepass = D3DXPreprocessShaderFromFileA(job->Source.c_str(), &job->Template.Defines[0], NULL, &job->SourceBuffer, &errors);
		if (prepass != D3D_OK) {
			if (errors) { job->Log += (char*)errors->GetBufferPointer(); errors->Release(); }
			return;
		}
		if (errors) { errors->Release(); errors = nullptr; }
		job->Preprocessed_ok = true;
		const bool fresh = ShaderProgram::CheckPreprocessResult(job->Preprocessed.c_str(), job->SourceBuffer);
		if (fresh) {
			FILE* f = nullptr;
			if (!fopen_s(&f, job->Compiled.c_str(), "rb") && f) {
				fseek(f, 0, SEEK_END);
				long size = ftell(f);
				fseek(f, 0, SEEK_SET);
				if (size > 0 && SUCCEEDED(D3DXCreateBuffer(size, &job->ShaderBuffer)))
					if (fread(job->ShaderBuffer->GetBufferPointer(), 1, size, f) != (size_t)size) { job->ShaderBuffer->Release(); job->ShaderBuffer = nullptr; }
				fclose(f);
			}
			if (job->ShaderBuffer) return;
		}
		const char* profile = strstr(job->Name.c_str(), ".vso") ? "vs_3_0" : "ps_3_0";
		ID3DXConstantTable* table = nullptr;
		D3DXCompileShader((const char*)job->SourceBuffer->GetBufferPointer(), job->SourceBuffer->GetBufferSize(), NULL, NULL, "main",
			profile, NULL, &job->ShaderBuffer, &errors, &table);
		if (table) table->Release();
		if (errors) { job->Log += (char*)errors->GetBufferPointer(); errors->Release(); }
		if (job->ShaderBuffer) {
			FILE* f = nullptr;
			if (!fopen_s(&f, job->Compiled.c_str(), "wb") && f) {
				fwrite(job->ShaderBuffer->GetBufferPointer(), 1, job->ShaderBuffer->GetBufferSize(), f);
				fclose(f);
			}
			const std::string portable = ShaderProgram::PortableSource(job->SourceBuffer);
			if (!fopen_s(&f, job->Preprocessed.c_str(), "wb") && f) {
				fwrite(portable.data(), 1, portable.size(), f);
				fclose(f);
			}
			job->Log += "Shader compiled (parallel): " + job->Compiled;
		}
	}

	inline void Worker() {
		for (;;) {
			const size_t i = NextJob.fetch_add(1);
			if (i >= Jobs.size()) return;
			Run(Jobs[i]);
			Jobs[i]->Done = true;
		}
	}

	inline void Add(const char* Name, const char* SubPath, const char* CacheSubPath, ShaderTemplate Template, const D3DXMACRO* Overrides) {
		Job* job = new Job();
		job->Name = Name;
		Paths(Name, SubPath, CacheSubPath, Template.Name, job);
		if (!ShaderProgram::FileExists(job->Source.c_str()) || ByCompiledPath.count(job->Compiled)) { delete job; return; }
		job->Template = Template;
		ShaderRecord::BuildDefines(job->Template, Overrides);
		Jobs.push_back(job);
		ByCompiledPath[job->Compiled] = job;
	}

	inline void StartOnce() {
		if (Started) return;
		if (!TheShaderManager || TheShaderManager->ShaderNames.empty()) return;
		Started = true;
		if (!TheSettingManager->GetSettingI("Main.Main.Performance", "ParallelShaderCompile")) return;
		StartTime = (double)GetTickCount64();
		static D3DXMACRO interior[] = { { "INTERIOR_SHADOWS", "1" }, { "FORWARD_SHADOWS", "0" }, { "SUN_CROSSFADE", "0" },
			{ "CONTACT_HARDENING", "0" }, { NULL, NULL }, { NULL, NULL } };
		if (const char* lampFalloff = PBRShaders::LampFalloffInteriorDefine()) interior[4] = { "LAMP_FALLOFF_ON", lampFalloff };
		const bool interiorShadows = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Forward", "Enabled") != 0;
		std::unordered_set<std::string> templated;
		for (const auto& [CollectionName, Pointer] : TheShaderManager->ShaderNames) {
			ShaderCollection* Collection = Pointer ? *Pointer : nullptr;
			if (!Collection) continue;
			for (const auto& [Name, Template] : Collection->Templates()) {
				const std::string name(Name);
				templated.insert(name);
				Add(name.c_str(), NULL, NULL, Template, NULL);
				if (interiorShadows && name.size() > 4 && name.compare(name.size() - 4, 4, ".pso") == 0 && PointShadowForward::Eligible(name.c_str(), Template))
					Add(name.c_str(), NULL, "Interiors\\", Template, interior);
			}
		}
		WIN32_FIND_DATAA find;
		HANDLE h = FindFirstFileA(ShadersPath "*.?so.hlsl", &find);
		if (h != INVALID_HANDLE_VALUE) {
			do {
				std::string name(find.cFileName);
				name.resize(name.size() - 5);
				if (templated.count(name) || !TheShaderManager->GetShaderCollection(name.c_str())) continue;
				Add(name.c_str(), NULL, NULL, ShaderTemplate{}, NULL);
				if (interiorShadows && name.compare(name.size() - 4, 4, ".pso") == 0 && PointShadowForward::Eligible(name.c_str(), ShaderTemplate{}))
					Add(name.c_str(), NULL, "Interiors\\", ShaderTemplate{}, interior);
			} while (FindNextFileA(h, &find));
			FindClose(h);
		}
		h = FindFirstFileA(ShadersPath "Shadows\\*.?so.hlsl", &find);
		if (h != INVALID_HANDLE_VALUE) {
			do {
				std::string name(find.cFileName);
				name.resize(name.size() - 5);
				Add(name.c_str(), "Shadows\\", NULL, ShaderTemplate{}, NULL);
			} while (FindNextFileA(h, &find));
			FindClose(h);
		}
		const unsigned cores = std::thread::hardware_concurrency();
		const unsigned threads = (std::max)(1u, (std::min)(cores > 1 ? cores - 1 : 1u, 12u));
		for (unsigned i = 0; i < threads; i++) std::thread(Worker).detach();
		Logger::Log("UNOFFICIAL ParallelShaderCompile: %u shaders on %u threads.", (unsigned)Jobs.size(), threads);
	}

	inline bool Take(const char* CompiledPath, ID3DXBuffer** Source, ID3DXBuffer** Shader) {
		if (!Started || Jobs.empty()) return false;
		auto it = ByCompiledPath.find(CompiledPath);
		if (it == ByCompiledPath.end() || it->second->Taken) return false;
		Job* job = it->second;
		while (!job->Done) Sleep(1);
		job->Taken = true;
		if (!job->Log.empty()) Logger::Log("%s", job->Log.c_str());
		if (!job->Preprocessed_ok) { if (job->SourceBuffer) job->SourceBuffer->Release(); return false; }
		*Source = job->SourceBuffer;
		*Shader = job->ShaderBuffer;
		job->SourceBuffer = job->ShaderBuffer = nullptr;
		static size_t taken = 0;
		if (++taken == Jobs.size()) {
			Logger::Log("UNOFFICIAL ParallelShaderCompile: all %u taken, %.1f s after the start.", (unsigned)Jobs.size(), ((double)GetTickCount64() - StartTime) / 1000.0);
		}
		return true;
	}
}
