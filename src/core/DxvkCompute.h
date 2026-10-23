#pragma once

// UNOFFICIAL 2026-10-23 (NVR DXVK): the NVR extension on an NVR-built DXVK d3d9.dll (tools\nvr-dxvk,
// docs\next\NVR-DXVK-EXTENSION.md). Through it NVR records Vulkan compute work into DXVK's own command
// stream, in order with the D3D9 work around it: no 16-sampler, 224-constant, 16-bool or instruction
// limits for the passes that get a compute version. Absent (native D3D9, stock DXVK): Available() is
// false and every effect keeps its D3D9 pass. Nothing here changes a setting's name or meaning.
//
// Shader files: Data\Shaders\NewVegasReloaded\Compute\<Name>.spv (built from the .comp next to them
// in src\hlsl\NewVegas\Compute with glslangValidator -V --target-env vulkan1.1).
//
// Fail once: the first failure of any step (no extension, a file missing, a refused pipeline) is
// logged once and the compute paths stay off for the rest of the session.

#include "NvrDxvkExt.h"

class DxvkCompute {
public:
	static void					Initialize(IDirect3DDevice9* device);	// RenderManager::Initialize; logs the outcome
	static bool					Available() { return Ext != nullptr && !Failed; }
	static ID3D9NvrExt*			Extension() { return Ext; }
	static const NVR_EXT_INFO&	Info() { return ExtInfo; }
	static const char*			Status();	// one line for the log and the menu

	// Reads Data\Shaders\NewVegasReloaded\Compute\<Name>.spv; empty on failure (logged once per name).
	static bool					LoadSpirv(const char* Name, std::vector<char>& Words);

	// Helpers; null on failure (logged). The sampler heap is set 0, binding 0 in every NVR .comp.
	static ID3D9NvrExtPipeline*	CreatePipeline(const char* Name, const NVR_EXT_BINDING_DESC* Bindings, UINT Count, UINT PushSize, bool UsesSamplers);
	static ID3D9NvrExtBuffer*	CreateBuffer(UINT Size, UINT Flags, const char* Name);
	static ID3D9NvrExtSampler*	CreateSampler(NVR_EXT_FILTER Filter, NVR_EXT_ADDRESS Address);

	// Marks every compute path off for the session (logged once with the reason).
	static void					Fail(const char* Reason);

	template<class T> static void Drop(T*& Object) { if (Object) { Object->Release(); Object = nullptr; } }

private:
	static ID3D9NvrExt*			Ext;
	static NVR_EXT_INFO			ExtInfo;
	static bool					Failed;
	static char					StatusText[160];
};
