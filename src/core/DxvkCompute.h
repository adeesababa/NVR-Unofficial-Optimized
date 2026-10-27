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
	// UNOFFICIAL 2026-10-26: whether Compute\<Name>.spv exists (a variant an older package may lack: no log, no failure).
	static bool					HasSpirv(const char* Name);

	// Helpers; null on failure (logged). The sampler heap is set 0, binding 0 in every NVR .comp and .frag.
	static ID3D9NvrExtPipeline*	CreatePipeline(const char* Name, const NVR_EXT_BINDING_DESC* Bindings, UINT Count, UINT PushSize, bool UsesSamplers);
	// A full-screen pass: <Name>.spv is the fragment shader (built-in vertex shader, uv at location 0).
	// UNOFFICIAL 2026-10-26 FailAll false (debugging tools): a missing file or refused pipeline is only logged; the other paths stay on.
	static ID3D9NvrExtPipeline*	CreateGraphicsPipeline(const char* Name, const NVR_EXT_BINDING_DESC* Bindings, UINT Count, UINT PushSize, bool UsesSamplers, UINT TargetCount = 1, NVR_EXT_BLEND Blend = NVR_EXT_BLEND_NONE, bool FailAll = true);
	// The samplers every pass uses (made once): the effects' LINEAR/CLAMP and POINT/CLAMP states.
	static ID3D9NvrExtSampler*	LinearClamp();
	static ID3D9NvrExtSampler*	PointClamp();
	static ID3D9NvrExtSampler*	LinearWrap();	// the file textures' (dirt, blood, water noise) WRAP state
	static ID3D9NvrExtSampler*	PointWrap();	// UNOFFICIAL 2026-10-25 (Cinema): POINT/WRAP, the blue noise file texture (MIN/MAG NONE)
	// UNOFFICIAL 2026-10-25 (extension 1.3): trilinear over all mips, or the same with 16x anisotropy (D3D9 ANISOTROPIC), for
	// file textures sampled with mips (Underwater's caustics, ...). Null on an older NVR DXVK: keep the D3D9 pass then.
	static ID3D9NvrExtSampler*	MipSampler(bool Anisotropic, NVR_EXT_ADDRESS Address);
	// [Main.Main.Performance] NvrDxvkPasses: the ported full-screen effects run as Vulkan passes (false, or no extension: their D3D9 passes).
	static bool					PassesOn();
	// [Main.Main.Performance] NvrDxvkVariants (UNOFFICIAL 2026-10-26): Vulkan passes use their compile-time variants
	// (Compute\variants.txt, the D3D9 techniques' fixed switches) where the .spv exists; off: the run-time switched shaders (A/B).
	static bool					VariantsOn();
	// [Main.Main.Performance] NvrDxvkStencil (UNOFFICIAL 2026-10-26): Vulkan passes test a stencil where their D3D9 technique does
	// (extension 1.6 DrawDepthStencil; SMAA's weight pass only on the edge pixels); off, or an older NVR DXVK: without (A/B).
	static bool					StencilOn();
	// [Main.Main.Performance] NvrDxvkKeepD3D9 (UNOFFICIAL 2026-10-26, test tool): whether this effect (its EffectRecord name, e.g.
	// PointShadows; names separated by commas or spaces) keeps its D3D9 passes while the others run as Vulkan passes, so one pass's
	// real cost shows in the frame total. EffectRecord::Render, ExtFullScreen and ExtDrawTo check it (so effects with their own
	// Render, AmbientLight, SMAA, AO, ..., fall back to D3D9 for the frame too).
	static bool					KeptOnD3D9(const char* Name);
	static ID3D9NvrExtBuffer*	CreateBuffer(UINT Size, UINT Flags, const char* Name);
	// UNOFFICIAL 2026-10-26 (debugging tools): as CreateBuffer, but a failure is only logged (the other compute paths stay on).
	static ID3D9NvrExtBuffer*	CreateBufferNoFail(UINT Size, UINT Flags, const char* Name);
	// UNOFFICIAL 2026-10-26 (alflicker): DXVK copies a D3DPOOL_MANAGED texture (the file textures: blue noise, SMAA's tables, noise
	// volumes, ...) to the GPU only when a D3D9 draw binds it, and the extension reads the GPU image as it is: a file texture only
	// Vulkan passes read stayed empty (AmbientLight's blue noise read 0 everywhere, so its rotating pattern moved the whole screen at
	// once: the every-4th-frame flicker; a few D3D9 frames cured it for the session). PreLoad (IDirect3DResource9) uploads what is
	// pending; nothing for the render targets. Every Draw/Dispatch call passes its bindings through this first.
	static void					PreLoad(const NVR_EXT_RESOURCE_BINDING* Bindings, UINT Count);

	// UNOFFICIAL 2026-10-26 (extension 1.5, diagnostic): while On (F10 profiling), a "DXVK PROFILE" line every 120 frames: where a frame
	// goes between the game thread (the game and NVR outside D3D9 calls; inside them DXVK's own work and its waits), DXVK's command
	// thread and the GPU. RenderHook calls it once a frame; an older NVR DXVK, native D3D9 or stock DXVK: nothing.
	static void					ProfileTick(bool On);

	// Phase 2 (extension 1.2): [Main.Main.Performance] NvrDxvkReplacements. Called by ShaderRecord::LoadShader before the device
	// sees a pixel shader's bytecode: if Data\Shaders\NewVegasReloaded\Compute\Replace\<Name>@<hash>.spv (FNV-1a of the bytecode,
	// 8 hex digits; a Replace\dump folder collects every shader as <Name>@<hash>.pso) or <Name>.spv exists, DXVK binds that SPIR-V
	// fragment shader wherever the game binds this shader. The .txt next to it says what the replacement reads from the game:
	//   textures=0,1,2        the D3D9 samplers it declares (set 1, binding i); must be ones the original uses
	//   floats=0-3,8-15       the float registers it wants in its constant block (set 2, binding 6), packed in that order;
	//                         absent: the original's own compacted register list (DXVK's layout, harder to write against)
	// True when registered. A problem leaves the shader as it is (logged); it does not turn the other compute paths off.
	static bool					ReplacementsOn();
	// UNOFFICIAL 2026-10-26 (FORK-1026-LIMITS.md): with the Replace\dump folder, the shader's source file (template) and its defines
	// are written next to the .pso as <Name>@<hash>.defines, so tools\nvr-dxvk\replace\nolimits.py can build the no-limits version
	// (NVR_NO_SM3_LIMITS) of exactly that variant. Template/Defines may be null (nothing extra written).
	static bool					RegisterReplacement(const char* Name, const void* Bytecode, UINT Size, const char* Template = nullptr, const D3DXMACRO* Defines = nullptr);
	static ID3D9NvrExtSampler*	CreateSampler(NVR_EXT_FILTER Filter, NVR_EXT_ADDRESS Address);

	// Marks every compute path off for the session (logged once with the reason).
	static void					Fail(const char* Reason);

	template<class T> static void Drop(T*& Object) { if (Object) { Object->Release(); Object = nullptr; } }

private:
	static ID3D9NvrExt*			Ext;
	static NVR_EXT_INFO			ExtInfo;
	static bool					Failed;
	static char					StatusText[160];
	static ID3D9NvrExtSampler*	SamplerLinear;
	static ID3D9NvrExtSampler*	SamplerPoint;
	static ID3D9NvrExtSampler*	SamplerWrap;
	static ID3D9NvrExtSampler*	SamplerPointWrap;
};
