#pragma once


#include "NvrDxvkExt.h"

struct NvrRtShared;

class RtCore {
public:
	struct User {
		const char*	Name;
		bool		(*Wants)();
		void		(*Setup)();
		void		(*Record)(IDirect3DDevice9* Device);
		UINT		Point = 0;
	};

	static void			AddUser(const User& user);
	static void			SetMaskHook(UInt32 (*Hook)(NiGeometry* Geometry, bool Land));

	static int			AddImage(const char* Name, UINT Width, UINT Height, NVR_EXT_SHARED_FORMAT Format);
	static int			AddShader(const char* Name);

	static bool			CopyIn(int Image, IUnknown* Source, const RECT* Rect = nullptr);
	static bool			AddPass(int Shader, const int* Images, UINT ImageCount, UINT WriteMask, const void* Constants, UINT ConstantsSize,
							UINT GroupsX, UINT GroupsY);
	static bool			CopyOut(int Image, IUnknown* Target);

	static void			Frame(IDirect3DDevice9* Device);
	static bool			Exchange(IDirect3DDevice9* Device, const User& user);
#define NVR_RTCORE_EXCHANGE 1
	static D3DXVECTOR3	Eye();
	static bool			Running();
	static const char*	StatusText();
	static const NvrRtShared* Helper();
	static UInt32		Instances();
	static UInt32		Geometries();
	static UInt32		Waiting();
	static UInt32		Released();
};
