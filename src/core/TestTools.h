#pragma once

#if __has_include("ReleaseBuild.h")
	#define NVR_TEST_TOOLS 0
#else
	#define NVR_TEST_TOOLS 1
#endif
