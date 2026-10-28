#pragma once

// UNOFFICIAL 2026-10-28 (NVR DXVK): the ray tracing core shared by NVR's RT effects (docs\next\FORK-RT-CORE.md).
//
// Windows' 32-bit Vulkan drivers offer no ray tracing, so the scene lives in a 64-bit helper, NvrRtHelper.exe (tools\nvr-rt-helper),
// on the same GPU. Every frame, at one point of the frame (RtCore::Frame, after the depth and normals exist, before the sun shadows),
// the core:
//   1. gathers the scene: the opaque static geometry of the loaded cells within [Main.Main.RayTracing] SceneRadius of the camera
//      (land and objects; not actors, alpha-tested or blended geometry, SpeedTree leaves, decals): each new geometry is converted
//      by the fork (ID3D9NvrExtRt::StageGeometry) into the shared arena image and built ONCE in the helper; each frame only the
//      instance list (camera-relative transforms) goes over;
//   2. asks every RT effect (a User) that wants to trace this frame to record: CopyIn its inputs into shared images, AddPass its
//      trace passes (compute shaders run by the helper), CopyOut its results into NVR textures;
//   3. records, in DXVK's command stream: the arena copy, the inputs, "inputs ready" = N, a GPU wait for "output ready" = N, the
//      outputs. Nothing waits on the CPU.
// Off (no user wants RT), native D3D9, stock DXVK, a fork without patch 0028, a GPU without ray tracing for 64-bit programs, or no
// helper: nothing runs and the users keep their old paths (the reason is logged once). A helper that dies or stops answering is
// ended by a watchdog thread, which releases the GPU wait; RT is then off for the session.
//
// Coordinates: scene space = world space minus the camera position (TheRenderManager->CameraPosition), Gamebryo units, z up;
// RenderManager::CreateD3DMatrix's convention. A trace pass gets what it needs to reconstruct positions in its own constants.

#include "NvrDxvkExt.h"

struct NvrRtShared;

class RtCore {
public:
	struct User {
		const char*	Name;
		bool		(*Wants)();							// it traces this frame
		void		(*Setup)();							// registers its images (AddImage) before each helper start
		void		(*Record)(IDirect3DDevice9* Device);	// this frame's CopyIn / AddPass / CopyOut
	};

	static void			AddUser(const User& user);
	// Optional: the cull mask of each instance (default 0x01 land, 0x02 objects); one hook, the last set wins. 0 = never hit.
	static void			SetMaskHook(UInt32 (*Hook)(NiGeometry* Geometry, bool Land));

	// Setup only: a shared image (a pass's input or output); returns its index (or -1). Index 0 is the core's arena. The image is made
	// with both sides rounded up to 256 texels (the game's and the helper's drivers agree on the layout only then): the content is the
	// top-left Width x Height; give the logical size to the pass in its constants. CopyOut copies only that rectangle.
	static int			AddImage(const char* Name, UINT Width, UINT Height, NVR_EXT_SHARED_FORMAT Format);
	// A trace shader, Data\Shaders\NewVegasReloaded\Compute\Rt\<Name>.spv (the helper compiles it once); any time; -1 if full.
	static int			AddShader(const char* Name);

	// Record only:
	static bool			CopyIn(int Image, IUnknown* Source, const RECT* Rect = nullptr);
	static bool			AddPass(int Shader, const int* Images, UINT ImageCount, UINT WriteMask, const void* Constants, UINT ConstantsSize,
							UINT GroupsX, UINT GroupsY);
	static bool			CopyOut(int Image, IUnknown* Target);

	static void			Frame(IDirect3DDevice9* Device);	// the RT point of the frame
	static bool			Running();							// the helper is ready (users trace from the frame after it got ready)
	static const char*	StatusText();
	static const NvrRtShared* Helper();						// the helper's counters (null when not running)
	static UInt32		Instances();						// this frame's
	static UInt32		Geometries();						// known (built or waiting for arena space)
	static UInt32		Waiting();							// geometries not yet sent (the arena was full this frame)
	static UInt32		Released();							// geometries released since start
};
