#pragma once
#include "RtCore.h"

namespace RtCoreClient {
	inline bool Ready() { return RtCore::Running(); }
	inline bool Exchange(IDirect3DDevice9* Device, const RtCore::User& user) {
#ifdef NVR_RTCORE_EXCHANGE
		return RtCore::Exchange(Device, user);
#else
		(void)Device; (void)user; return false;
#endif
	}
}
