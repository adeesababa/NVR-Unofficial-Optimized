// NvrDlssHelper.exe -- the 64-bit helper behind New Vegas Reloaded's DLSS upscaler ([Shaders.TAA.FSR] Upscaler = 1).
// MIT licence (LICENSE in this folder). NVIDIA's DLSS SDK parts it links (nvsdk_ngx_s.lib) and loads (nvngx_dlss.dll) are
// NVIDIA's, under NVIDIA's RTX SDK licence (NVIDIA-SDK-NOTICE.txt). Built without the SDK (NVR_DLSS_NO_SDK) it can only
// do the pass-through test.
//
// Why a separate process: Fallout New Vegas, NVR and the NVR DXVK d3d9.dll are 32-bit; NVIDIA ships DLSS (NGX) for 64-bit
// Windows only. The helper makes its own Vulkan device on the same GPU, imports four images and two timeline semaphores the
// fork exports (VK_KHR_external_memory_win32 / VK_KHR_external_semaphore_win32), and per frame records DLSS (or, in the
// pass-through test, a stretch copy plus a marker) between a GPU wait for "inputs ready" = N and a signal of "output
// ready" = N. The protocol is NvrDlssProtocol.h; docs/next/FORK2-DLSS.md in the NVR repository has the whole design.
//
// UNOFFICIAL 2026-10-30 protocol 2 (DLSS Ray Reconstruction, docs/next/FORK2-DLSS-RR.md): with NVR's RR switch on there are
// eight images (four RR guides: diffuse albedo, specular albedo, normals, roughness), and a frame flagged NVR_DLSS_FRAME_RR is
// evaluated by DLSS-D (NGX's RayReconstruction feature, nvngx_dlssd.dll next to the helper) instead of DLSS SR. RR has its
// own NGX feature handle and parameter map; RR not available or failing only reports rrAvailable = 0, DLSS SR goes on. DLSS
// SR also takes NVR's model preset per frame now (a hint per quality mode, set before the feature is made).
//
// Usage:
//   NvrDlssHelper.exe --shm <name>   started by NVR (never by hand)
//   NvrDlssHelper.exe --probe        prints the GPU, the Vulkan interop support and whether DLSS (and Ray Reconstruction) is
//                                    available, then exits
//   NvrDlssHelper.exe --selftest     (or --selftest dlss: the child in DLSS mode; without DLSS it must give up cleanly) the whole cross-process exchange without the game: this process plays NVR's part
//                                    (exports images and semaphores from a Vulkan device of its own), starts a second copy
//                                    of itself in pass-through mode and checks the pixels and the order of 300 frames
//   NvrDlssHelper.exe --selftest rr  the same with the RR exchange: eight images, every frame an RR frame, the four guides
//                                    drawn as thumbnails by the pass-through child (--selftest dlss rr: the child in DLSS mode)
// Only runs on NVIDIA GPUs (vendor 0x10DE): NVIDIA's licence for the DLSS SDK covers systems with NVIDIA GPUs only.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#ifndef NVR_DLSS_NO_SDK
#include <nvsdk_ngx_vk.h>
#include <nvsdk_ngx_helpers_vk.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "NvrDlssProtocol.h"

namespace {

	// ---------------------------------------------------------------------------------------------------------------------
	// Log: NvrDlssHelper.log next to the exe (or NvrDlssHelper-selftest.log), plus the last line into the shared block.

	CRITICAL_SECTION	g_logLock;
	FILE*				g_log = nullptr;
	NvrDlssShared*		g_shared = nullptr;
	HANDLE				g_statusEvent = nullptr;
	bool				g_console = false;
	std::wstring		g_exeDir;

	void Log(const char* format, ...) {
		char line[1024];
		va_list args;
		va_start(args, format);
		vsnprintf(line, sizeof(line), format, args);
		va_end(args);
		SYSTEMTIME t;
		GetLocalTime(&t);
		EnterCriticalSection(&g_logLock);
		if (g_log) {
			fprintf(g_log, "%02u:%02u:%02u.%03u %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, line);
			fflush(g_log);
		}
		if (g_console) printf("%s\n", line);
		LeaveCriticalSection(&g_logLock);
	}

	// A status line NVR should see (also logged).
	void Status(const char* format, ...) {
		char line[512];
		va_list args;
		va_start(args, format);
		vsnprintf(line, sizeof(line), format, args);
		va_end(args);
		Log("%s", line);
		if (g_shared) {
			strncpy_s(g_shared->message, sizeof(g_shared->message), line, _TRUNCATE);
			MemoryBarrier();
			InterlockedIncrement((volatile LONG*)&g_shared->messageSerial);
			if (g_statusEvent) SetEvent(g_statusEvent);
		}
	}

	void SetState(NvrDlssState state) {
		if (!g_shared) return;
		MemoryBarrier();
		g_shared->state = state;
		if (g_statusEvent) SetEvent(g_statusEvent);
	}

	std::wstring ExeDir() {
		wchar_t path[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, path, MAX_PATH);
		std::wstring dir(path);
		size_t slash = dir.find_last_of(L"\\/");
		return slash == std::wstring::npos ? L"." : dir.substr(0, slash);
	}

	void OpenLog(const wchar_t* name) {
		std::wstring path = g_exeDir + L"\\" + name;
		if (_wfopen_s(&g_log, path.c_str(), L"w") != 0) {
			// The game folder may be read-only: the temp folder then.
			wchar_t temp[MAX_PATH] = {};
			GetTempPathW(MAX_PATH, temp);
			path = std::wstring(temp) + name;
			if (_wfopen_s(&g_log, path.c_str(), L"w") != 0) g_log = nullptr;
		}
	}

	const char* VkResultName(VkResult r) {
		switch (r) {
			case VK_SUCCESS: return "VK_SUCCESS";
			case VK_TIMEOUT: return "VK_TIMEOUT";
			case VK_NOT_READY: return "VK_NOT_READY";
			case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
			case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
			case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
			case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
			case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
			case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
			case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
			case VK_ERROR_INVALID_EXTERNAL_HANDLE: return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
			case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
			default: return "VkResult(other)";
		}
	}

#define VK_CHECK(call, what) do { VkResult r_ = (call); if (r_ != VK_SUCCESS) { Status("Vulkan: %s failed: %s (%d)", what, VkResultName(r_), (int)r_); return false; } } while (0)

	uint16_t FloatToHalf(float value) {
		uint32_t f;
		memcpy(&f, &value, 4);
		uint32_t sign = (f >> 16) & 0x8000u;
		int exponent = (int)((f >> 23) & 0xFF) - 127 + 15;
		uint32_t mantissa = f & 0x7FFFFFu;
		if (exponent <= 0) return (uint16_t)sign;
		if (exponent >= 31) return (uint16_t)(sign | 0x7C00u);
		return (uint16_t)(sign | ((uint32_t)exponent << 10) | (mantissa >> 13));
	}

	float HalfToFloat(uint16_t h) {
		uint32_t sign = (uint32_t)(h & 0x8000u) << 16;
		uint32_t exponent = (h >> 10) & 0x1F;
		uint32_t mantissa = h & 0x3FF;
		uint32_t f;
		if (exponent == 0) f = sign;	// zero / denormals as zero (enough here)
		else if (exponent == 31) f = sign | 0x7F800000u | (mantissa << 13);
		else f = sign | ((exponent - 15 + 127) << 23) | (mantissa << 13);
		float out;
		memcpy(&out, &f, 4);
		return out;
	}

	bool HasExtension(const std::vector<VkExtensionProperties>& list, const char* name) {
		for (const VkExtensionProperties& e : list) if (!strcmp(e.extensionName, name)) return true;
		return false;
	}

	void AddUnique(std::vector<const char*>& list, const char* name) {
		for (const char* n : list) if (!strcmp(n, name)) return;
		list.push_back(name);
	}

	const uint32_t kNvidiaVendor = 0x10DE;
	const uint32_t kCmdRing = 4;
	const uint32_t kMarkerSize = 24;	// px, the pass-through test's marker boxes

	// UNOFFICIAL 2026-10-30 protocol 2: the images' names (log lines) for the whole table; imageCount says how many exist.
	const char* const kImageNames[NVR_DLSS_IMAGE_COUNT] = { "colour", "depth", "motion", "output", "diffuse albedo", "specular albedo",
		"normals", "roughness" };
	const uint32_t kGuideCount = NVR_DLSS_IMAGE_COUNT - NVR_DLSS_IMAGE_COUNT_SR;	// the RR guides: images 4..7

	bool FileNextToHelper(const wchar_t* name) {
		const DWORD attributes = GetFileAttributesW((g_exeDir + L"\\" + name).c_str());
		return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
	}

	// UNOFFICIAL 2026-10-30 the pass-through RR test's guide thumbnails: along the output's bottom edge from the left, each a sixth
	// of the output's width with the render rectangle's aspect, 16 px apart. False: the output is too small for the four.
	bool GuideThumbnail(uint32_t outW, uint32_t outH, uint32_t renderW, uint32_t renderH, uint32_t index, VkOffset2D& at, VkExtent2D& size) {
		const uint32_t gap = 16;
		size.width = outW / 6;
		size.height = renderW ? (uint32_t)((uint64_t)size.width * renderH / renderW) : 0;
		if (!size.width || !size.height || gap + kGuideCount * (size.width + gap) > outW || size.height + 2 * gap > outH) return false;
		at.x = (int32_t)(gap + index * (size.width + gap));
		at.y = (int32_t)(outH - gap - size.height);
		return true;
	}

	// ---------------------------------------------------------------------------------------------------------------------
	// A Vulkan device on the GPU NVR's DXVK uses (matched by LUID, else UUID), with what the exchange and NGX need.

	struct VulkanDevice {
		VkInstance			instance = VK_NULL_HANDLE;
		VkPhysicalDevice	physical = VK_NULL_HANDLE;
		VkDevice			device = VK_NULL_HANDLE;
		uint32_t			queueFamily = 0;
		VkQueue				queue = VK_NULL_HANDLE;
		VkPhysicalDeviceProperties	properties = {};
		VkPhysicalDeviceIDProperties ids = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
		VkPhysicalDeviceMemoryProperties memory = {};
		uint32_t			timestampBits = 0;
		bool				ngxExtensions = false;	// every device extension NGX asked for is enabled
		bool				rrExtensions = false;	// UNOFFICIAL 2026-10-30: and every one NGX asked for Ray Reconstruction (only with wantRr)
		PFN_vkGetMemoryWin32HandleKHR		getMemoryWin32Handle = nullptr;
		PFN_vkGetSemaphoreWin32HandleKHR	getSemaphoreWin32Handle = nullptr;
		PFN_vkImportSemaphoreWin32HandleKHR	importSemaphoreWin32Handle = nullptr;
	};

#ifndef NVR_DLSS_NO_SDK
	// NVR's own project id for NGX's custom-engine identification (NVR has no NVIDIA application id).
	const char* const kNgxProjectId = "4f1c2b7e-9a63-4d0e-8b25-6e3d1a7c9f40";
	const char* const kNgxEngineVersion = "1.0";
	std::wstring	g_ngxDataPath;
	const wchar_t*	g_ngxPaths[1] = {};
	NVSDK_NGX_FeatureCommonInfo g_ngxFeatureInfo = {};

	void NVSDK_CONV NgxLog(const char* message, NVSDK_NGX_Logging_Level, NVSDK_NGX_Feature) {
		std::string line(message ? message : "");
		while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
		Log("NGX: %s", line.c_str());
	}

	// UNOFFICIAL 2026-10-30: feature = NVSDK_NGX_Feature_RayReconstruction for DLSS-D's own extension needs (the same project id,
	// paths and log as DLSS SR's).
	NVSDK_NGX_FeatureDiscoveryInfo NgxDiscoveryInfo(NVSDK_NGX_Feature feature = NVSDK_NGX_Feature_SuperSampling) {
		if (g_ngxDataPath.empty()) {
			wchar_t temp[MAX_PATH] = {};
			GetTempPathW(MAX_PATH, temp);
			g_ngxDataPath = std::wstring(temp) + L"NvrDlssHelper";
			CreateDirectoryW(g_ngxDataPath.c_str(), nullptr);
			g_ngxPaths[0] = g_exeDir.c_str();	// nvngx_dlss.dll next to the helper
			g_ngxFeatureInfo.PathListInfo.Path = g_ngxPaths;
			g_ngxFeatureInfo.PathListInfo.Length = 1;
			g_ngxFeatureInfo.LoggingInfo.LoggingCallback = NgxLog;
			g_ngxFeatureInfo.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_ON;
			g_ngxFeatureInfo.LoggingInfo.DisableOtherLoggingSinks = false;
		}
		NVSDK_NGX_FeatureDiscoveryInfo info = {};
		info.SDKVersion = NVSDK_NGX_Version_API;
		info.FeatureID = feature;
		info.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
		info.Identifier.v.ProjectDesc.ProjectId = kNgxProjectId;
		info.Identifier.v.ProjectDesc.EngineType = NVSDK_NGX_ENGINE_TYPE_CUSTOM;
		info.Identifier.v.ProjectDesc.EngineVersion = kNgxEngineVersion;
		info.ApplicationDataPath = g_ngxDataPath.c_str();
		info.FeatureInfo = &g_ngxFeatureInfo;
		return info;
	}

	std::string NgxResultString(NVSDK_NGX_Result result) {
		const wchar_t* text = GetNGXResultAsString(result);
		std::string out;
		if (text) for (const wchar_t* c = text; *c; c++) out.push_back((char)(*c < 128 ? *c : '?'));
		char code[32];
		snprintf(code, sizeof(code), " (0x%08X)", (unsigned)result);
		return out + code;
	}
#endif

	// match: LUID (8 bytes, when valid) or device UUID; null = the first NVIDIA GPU (probe / self-test exporter).
	// UNOFFICIAL 2026-10-30 wantRr (with wantNgx): the instance and the device also get the extensions NGX asks for Ray
	// Reconstruction (the union with DLSS SR's). Only when NVR wants RR, so a helper without it makes the same device as before.
	bool CreateVulkanDevice(VulkanDevice& vk, const uint8_t* luid, bool luidValid, const uint8_t* uuid, bool wantNgx, bool wantRr = false) {
		// UNOFFICIAL 2026-10-30 (review): NGX's extension names are copied here before they are listed: with Ray Reconstruction a second
		// NGX query runs before vkCreateInstance / vkCreateDevice read the list, and NGX does not say how long its returned array stays
		// valid (the device one comes from the driver). A deque: growing it never moves the strings the list points to.
		std::deque<std::string> ownedNames;
		auto owned = [&ownedNames](const char* name) { ownedNames.emplace_back(name); return ownedNames.back().c_str(); };
		uint32_t loaderVersion = VK_API_VERSION_1_0;
		PFN_vkEnumerateInstanceVersion enumerateVersion = (PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion");
		if (enumerateVersion) enumerateVersion(&loaderVersion);
		if (loaderVersion < VK_API_VERSION_1_2) {
			Status("Vulkan: the Vulkan loader is %u.%u, 1.2 is needed (timeline semaphores)", VK_VERSION_MAJOR(loaderVersion), VK_VERSION_MINOR(loaderVersion));
			return false;
		}

		uint32_t count = 0;
		vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
		std::vector<VkExtensionProperties> instanceExts(count);
		vkEnumerateInstanceExtensionProperties(nullptr, &count, instanceExts.data());
		std::vector<const char*> enabledInstance;
#ifndef NVR_DLSS_NO_SDK
		if (wantNgx) {
			NVSDK_NGX_FeatureDiscoveryInfo discovery = NgxDiscoveryInfo();
			uint32_t ngxCount = 0;
			VkExtensionProperties* ngxExts = nullptr;
			NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(&discovery, &ngxCount, &ngxExts);
			if (NVSDK_NGX_FAILED(r)) Log("NGX: instance extension query failed: %s", NgxResultString(r).c_str());
			for (uint32_t i = 0; i < ngxCount && ngxExts; i++) {
				if (HasExtension(instanceExts, ngxExts[i].extensionName)) AddUnique(enabledInstance, owned(ngxExts[i].extensionName));
				else Log("NGX wants the instance extension %s, which the loader does not have", ngxExts[i].extensionName);
			}
		}
		if (wantNgx && wantRr) {
			// UNOFFICIAL 2026-10-30 DLSS Ray Reconstruction's instance extensions, added to DLSS SR's. A failed query is only logged:
			// RR then tries with SR's list, and a feature creation that fails turns RR off, never SR.
			NVSDK_NGX_FeatureDiscoveryInfo discovery = NgxDiscoveryInfo(NVSDK_NGX_Feature_RayReconstruction);
			uint32_t ngxCount = 0;
			VkExtensionProperties* ngxExts = nullptr;
			NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(&discovery, &ngxCount, &ngxExts);
			if (NVSDK_NGX_FAILED(r)) Log("NGX: Ray Reconstruction's instance extension query failed: %s", NgxResultString(r).c_str());
			for (uint32_t i = 0; i < ngxCount && ngxExts; i++) {
				if (HasExtension(instanceExts, ngxExts[i].extensionName)) AddUnique(enabledInstance, owned(ngxExts[i].extensionName));
				else Log("NGX wants the instance extension %s for Ray Reconstruction, which the loader does not have", ngxExts[i].extensionName);
			}
		}
#else
		(void)wantNgx;
		(void)wantRr;
#endif

		VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
		app.pApplicationName = "NvrDlssHelper";
		app.applicationVersion = 1;
		app.pEngineName = "New Vegas Reloaded";
		app.engineVersion = 1;
		app.apiVersion = VK_API_VERSION_1_2;
		VkInstanceCreateInfo instanceInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
		instanceInfo.pApplicationInfo = &app;
		instanceInfo.enabledExtensionCount = (uint32_t)enabledInstance.size();
		instanceInfo.ppEnabledExtensionNames = enabledInstance.data();
		VK_CHECK(vkCreateInstance(&instanceInfo, nullptr, &vk.instance), "vkCreateInstance");

		count = 0;
		vkEnumeratePhysicalDevices(vk.instance, &count, nullptr);
		std::vector<VkPhysicalDevice> devices(count);
		vkEnumeratePhysicalDevices(vk.instance, &count, devices.data());
		for (VkPhysicalDevice d : devices) {
			VkPhysicalDeviceIDProperties ids = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
			VkPhysicalDeviceProperties2 props = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &ids };
			vkGetPhysicalDeviceProperties2(d, &props);
			bool match;
			if (luid && luidValid && ids.deviceLUIDValid) match = !memcmp(ids.deviceLUID, luid, VK_LUID_SIZE);
			else if (uuid) match = !memcmp(ids.deviceUUID, uuid, VK_UUID_SIZE);
			else match = props.properties.vendorID == kNvidiaVendor;
			Log("Vulkan device: %s (vendor 0x%04X, device 0x%04X, API %u.%u.%u)%s", props.properties.deviceName, props.properties.vendorID,
				props.properties.deviceID, VK_VERSION_MAJOR(props.properties.apiVersion), VK_VERSION_MINOR(props.properties.apiVersion),
				VK_VERSION_PATCH(props.properties.apiVersion), match && !vk.physical ? "  <- this one" : "");
			if (match && !vk.physical) {
				vk.physical = d;
				vk.properties = props.properties;
				vk.ids = ids;
				vk.ids.pNext = nullptr;
			}
		}
		if (!vk.physical) {
			Status("Vulkan: the GPU the game uses was not found among %u Vulkan devices", count);
			return false;
		}
		if (vk.properties.vendorID != kNvidiaVendor) {
			Status("not an NVIDIA GPU (vendor 0x%04X): the helper runs on NVIDIA GPUs only (NVIDIA's DLSS SDK licence)", vk.properties.vendorID);
			return false;
		}
		if (vk.properties.apiVersion < VK_API_VERSION_1_2) {
			Status("Vulkan: the driver offers Vulkan %u.%u, 1.2 is needed", VK_VERSION_MAJOR(vk.properties.apiVersion), VK_VERSION_MINOR(vk.properties.apiVersion));
			return false;
		}
		vkGetPhysicalDeviceMemoryProperties(vk.physical, &vk.memory);

		count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(vk.physical, &count, nullptr);
		std::vector<VkQueueFamilyProperties> families(count);
		vkGetPhysicalDeviceQueueFamilyProperties(vk.physical, &count, families.data());
		bool found = false;
		for (uint32_t i = 0; i < count; i++) {
			if ((families[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
				vk.queueFamily = i;
				vk.timestampBits = families[i].timestampValidBits;
				found = true;
				break;
			}
		}
		if (!found) { Status("Vulkan: no graphics + compute queue"); return false; }

		count = 0;
		vkEnumerateDeviceExtensionProperties(vk.physical, nullptr, &count, nullptr);
		std::vector<VkExtensionProperties> deviceExts(count);
		vkEnumerateDeviceExtensionProperties(vk.physical, nullptr, &count, deviceExts.data());
		std::vector<const char*> enabled;
		const char* required[] = { VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME };
		for (const char* name : required) {
			if (!HasExtension(deviceExts, name)) { Status("Vulkan: the driver lacks %s (needed to share images with the game)", name); return false; }
			AddUnique(enabled, name);
		}
		vk.ngxExtensions = false;
#ifndef NVR_DLSS_NO_SDK
		if (wantNgx) {
			NVSDK_NGX_FeatureDiscoveryInfo discovery = NgxDiscoveryInfo();
			uint32_t ngxCount = 0;
			VkExtensionProperties* ngxExts = nullptr;
			NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(vk.instance, vk.physical, &discovery, &ngxCount, &ngxExts);
			if (NVSDK_NGX_FAILED(r)) Log("NGX: device extension query failed: %s", NgxResultString(r).c_str());
			else {
				vk.ngxExtensions = true;
				for (uint32_t i = 0; i < ngxCount && ngxExts; i++) {
					if (HasExtension(deviceExts, ngxExts[i].extensionName)) AddUnique(enabled, owned(ngxExts[i].extensionName));
					else { vk.ngxExtensions = false; Log("NGX wants the device extension %s, which the driver does not have", ngxExts[i].extensionName); }
				}
			}
		}
		vk.rrExtensions = false;
		if (wantNgx && wantRr) {
			// UNOFFICIAL 2026-10-30 DLSS Ray Reconstruction's device extensions, added to DLSS SR's. One the driver lacks turns RR off
			// (InitNgx says why); a failed query is only logged (RR then tries with SR's list; a failed creation turns RR off).
			vk.rrExtensions = true;
			NVSDK_NGX_FeatureDiscoveryInfo discovery = NgxDiscoveryInfo(NVSDK_NGX_Feature_RayReconstruction);
			uint32_t ngxCount = 0;
			VkExtensionProperties* ngxExts = nullptr;
			NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(vk.instance, vk.physical, &discovery, &ngxCount, &ngxExts);
			if (NVSDK_NGX_FAILED(r)) Log("NGX: Ray Reconstruction's device extension query failed: %s (DLSS SR's extensions are used)", NgxResultString(r).c_str());
			else {
				for (uint32_t i = 0; i < ngxCount && ngxExts; i++) {
					if (HasExtension(deviceExts, ngxExts[i].extensionName)) AddUnique(enabled, owned(ngxExts[i].extensionName));
					else { vk.rrExtensions = false; Log("NGX wants the device extension %s for Ray Reconstruction, which the driver does not have", ngxExts[i].extensionName); }
				}
			}
		}
#endif
		std::string list;
		for (const char* n : enabled) { list += " "; list += n; }
		Log("Vulkan device extensions:%s", list.c_str());

		VkPhysicalDeviceVulkan12Features supported12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
		VkPhysicalDeviceFeatures2 supported = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &supported12 };
		vkGetPhysicalDeviceFeatures2(vk.physical, &supported);
		if (!supported12.timelineSemaphore) { Status("Vulkan: no timeline semaphores"); return false; }
		VkPhysicalDeviceVulkan12Features features12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
		features12.timelineSemaphore = VK_TRUE;
		features12.bufferDeviceAddress = supported12.bufferDeviceAddress;	// NGX may use it
		features12.hostQueryReset = supported12.hostQueryReset;
		VkPhysicalDeviceFeatures2 features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &features12 };
		features.features.shaderStorageImageWriteWithoutFormat = supported.features.shaderStorageImageWriteWithoutFormat;
		features.features.shaderStorageImageReadWithoutFormat = supported.features.shaderStorageImageReadWithoutFormat;
		features.features.shaderInt16 = supported.features.shaderInt16;

		float priority = 1.0f;
		VkDeviceQueueCreateInfo queueInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
		queueInfo.queueFamilyIndex = vk.queueFamily;
		queueInfo.queueCount = 1;
		queueInfo.pQueuePriorities = &priority;
		VkDeviceCreateInfo deviceInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &features };
		deviceInfo.queueCreateInfoCount = 1;
		deviceInfo.pQueueCreateInfos = &queueInfo;
		deviceInfo.enabledExtensionCount = (uint32_t)enabled.size();
		deviceInfo.ppEnabledExtensionNames = enabled.data();
		VK_CHECK(vkCreateDevice(vk.physical, &deviceInfo, nullptr, &vk.device), "vkCreateDevice");
		vkGetDeviceQueue(vk.device, vk.queueFamily, 0, &vk.queue);
		vk.getMemoryWin32Handle = (PFN_vkGetMemoryWin32HandleKHR)vkGetDeviceProcAddr(vk.device, "vkGetMemoryWin32HandleKHR");
		vk.getSemaphoreWin32Handle = (PFN_vkGetSemaphoreWin32HandleKHR)vkGetDeviceProcAddr(vk.device, "vkGetSemaphoreWin32HandleKHR");
		vk.importSemaphoreWin32Handle = (PFN_vkImportSemaphoreWin32HandleKHR)vkGetDeviceProcAddr(vk.device, "vkImportSemaphoreWin32HandleKHR");
		if (!vk.getMemoryWin32Handle || !vk.getSemaphoreWin32Handle || !vk.importSemaphoreWin32Handle) { Status("Vulkan: the Win32 handle functions are missing"); return false; }
		return true;
	}

	void DestroyVulkanDevice(VulkanDevice& vk) {
		if (vk.device) vkDestroyDevice(vk.device, nullptr);
		if (vk.instance) vkDestroyInstance(vk.instance, nullptr);
		vk = VulkanDevice();
	}

	int32_t FindMemoryType(const VulkanDevice& vk, uint32_t bits, VkMemoryPropertyFlags flags) {
		for (uint32_t i = 0; i < vk.memory.memoryTypeCount; i++)
			if ((bits & (1u << i)) && (vk.memory.memoryTypes[i].propertyFlags & flags) == flags) return (int32_t)i;
		return -1;
	}

	struct SharedImage {
		VkImage			image = VK_NULL_HANDLE;
		VkDeviceMemory	memory = VK_NULL_HANDLE;
		VkImageView		view = VK_NULL_HANDLE;
		VkFormat		format = VK_FORMAT_UNDEFINED;
		uint32_t		width = 0, height = 0;
	};

	VkImageCreateInfo ImageInfo(const NvrDlssImageDesc& d, void* next) {
		VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, next };
		info.flags = d.flags;
		info.imageType = VK_IMAGE_TYPE_2D;
		info.format = (VkFormat)d.vkFormat;
		info.extent = { d.width, d.height, 1 };
		info.mipLevels = 1;
		info.arrayLayers = 1;
		info.samples = VK_SAMPLE_COUNT_1_BIT;
		info.tiling = (VkImageTiling)d.tiling;
		info.usage = d.usage;
		info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		return info;
	}

	// The importer's side of the fork's ID3D9NvrExtSharedImage: the identical image, its memory imported (dedicated, same size
	// and memory type, as Vulkan requires for OPAQUE_WIN32 handles).
	bool ImportImage(const VulkanDevice& vk, const NvrDlssImageDesc& d, const char* name, SharedImage& out) {
		VkExternalMemoryImageCreateInfo external = { VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO };
		external.handleTypes = (VkExternalMemoryHandleTypeFlags)d.handleType;
		VkImageCreateInfo info = ImageInfo(d, &external);
		VK_CHECK(vkCreateImage(vk.device, &info, nullptr, &out.image), "vkCreateImage (shared)");
		VkMemoryRequirements requirements = {};
		vkGetImageMemoryRequirements(vk.device, out.image, &requirements);
		if (requirements.size > d.allocationSize || !(requirements.memoryTypeBits & (1u << d.memoryTypeIndex))) {
			Status("Vulkan: the %s image does not fit the game's allocation (needs %llu bytes, types 0x%X; the game's: %llu bytes, type %u)",
				name, (unsigned long long)requirements.size, requirements.memoryTypeBits, (unsigned long long)d.allocationSize, d.memoryTypeIndex);
			return false;
		}
		VkMemoryDedicatedAllocateInfo dedicated = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
		dedicated.image = out.image;
		VkImportMemoryWin32HandleInfoKHR import = { VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR, d.dedicated ? &dedicated : nullptr };
		import.handleType = (VkExternalMemoryHandleTypeFlagBits)d.handleType;
		import.handle = (HANDLE)(uintptr_t)d.handle;
		VkMemoryAllocateInfo alloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &import };
		alloc.allocationSize = d.allocationSize;
		alloc.memoryTypeIndex = d.memoryTypeIndex;
		VK_CHECK(vkAllocateMemory(vk.device, &alloc, nullptr, &out.memory), "vkAllocateMemory (import)");
		CloseHandle((HANDLE)(uintptr_t)d.handle);	// importing does not take ownership of an NT handle
		VK_CHECK(vkBindImageMemory(vk.device, out.image, out.memory, 0), "vkBindImageMemory (shared)");
		VkImageViewCreateInfo view = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		view.image = out.image;
		view.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view.format = (VkFormat)d.vkFormat;
		view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		VK_CHECK(vkCreateImageView(vk.device, &view, nullptr, &out.view), "vkCreateImageView (shared)");
		out.format = (VkFormat)d.vkFormat;
		out.width = d.width;
		out.height = d.height;
		Log("imported the %s image: %ux%u, format %u, %llu bytes, memory type %u", name, d.width, d.height, d.vkFormat,
			(unsigned long long)d.allocationSize, d.memoryTypeIndex);
		return true;
	}

	bool ImportTimeline(const VulkanDevice& vk, uint64_t handle, const char* name, VkSemaphore& out) {
		VkSemaphoreTypeCreateInfo type = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
		type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
		type.initialValue = 0;
		VkSemaphoreCreateInfo info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type };
		VK_CHECK(vkCreateSemaphore(vk.device, &info, nullptr, &out), "vkCreateSemaphore (shared)");
		VkImportSemaphoreWin32HandleInfoKHR import = { VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR };
		import.semaphore = out;
		import.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
		import.handle = (HANDLE)(uintptr_t)handle;
		VK_CHECK(vk.importSemaphoreWin32Handle(vk.device, &import), "vkImportSemaphoreWin32HandleKHR");
		CloseHandle((HANDLE)(uintptr_t)handle);
		Log("imported the %s semaphore", name);
		return true;
	}

	// ---------------------------------------------------------------------------------------------------------------------
	// The helper proper (--shm).

	struct Helper {
		VulkanDevice	vk;
		SharedImage		images[NVR_DLSS_IMAGE_COUNT];
		VkSemaphore		inputsReady = VK_NULL_HANDLE, outputReady = VK_NULL_HANDLE;
		VkCommandPool	pool = VK_NULL_HANDLE;
		VkCommandBuffer	cmd[kCmdRing] = {};
		uint64_t		cmdFrame[kCmdRing] = {};
		VkQueryPool		queries = VK_NULL_HANDLE;
		VkBuffer		marker = VK_NULL_HANDLE;
		VkDeviceMemory	markerMemory = VK_NULL_HANDLE;
		bool			markerReady = false;
		uint32_t		mode = NVR_DLSS_MODE_PASSTHROUGH;
		uint64_t		gpuUs[64] = {}, responseUs[64] = {};
		uint32_t		gpuCount = 0, responseCount = 0;
		bool			loggedFirst = false;
		// UNOFFICIAL 2026-10-30 protocol 2: the images NVR made (4, or 8 with the RR guides) and whether RR frames can come.
		uint32_t		imageCount = NVR_DLSS_IMAGE_COUNT_SR;
		bool			rrWanted = false;		// NVR_DLSS_FEATURE_RR with the eight images
		bool			loggedFirstRr = false;
#ifndef NVR_DLSS_NO_SDK
		bool			ngxInit = false;
		bool			dlssAvailable = false;
		NVSDK_NGX_Parameter* ngxParams = nullptr;
		NVSDK_NGX_Handle* dlss = nullptr;
		uint32_t		dlssKey[7] = {};		// render, display, HDR, quality mode, preset (UNOFFICIAL 2026-10-30)
		bool			dlssFailed = false;
		bool			srPresetHints = false;	// UNOFFICIAL 2026-10-30: the preset hints were written once (a non-default preset)
		// UNOFFICIAL 2026-10-30 DLSS Ray Reconstruction (DLSS-D): its own feature handle and its own parameter map, so no RR key
		// (DLSS.Denoise.Mode and the others, which nvngx_dlss.dll's SR creation reads too) ever reaches a DLSS SR creation or
		// evaluate. Both features stay alive while frames alternate (re-creating one costs a vkDeviceWaitIdle).
		NVSDK_NGX_Parameter* rrParams = nullptr;
		NVSDK_NGX_Handle* dlssd = nullptr;
		uint32_t		dlssdKey[7] = {};		// render, display, quality mode, RR preset, depth option
		bool			rrFailed = false;
		float			rrWorldToView[16] = {}, rrViewToClip[16] = {};	// the frame's matrices; NGX gets pointers to these
#endif
	};

	bool CreateMarker(Helper& h) {
		// Two boxes in the output's format (RGBA16F): magenta, which moves along 8 places with the frame number (frame order),
		// and green, which stays (the helper is in the loop). Values above 1 so they stay visible after tonemapping.
		if (h.images[NVR_DLSS_IMAGE_OUTPUT].format != VK_FORMAT_R16G16B16A16_SFLOAT) {
			Log("the output is not RGBA16F: no marker in the pass-through test");
			return true;
		}
		const VkDeviceSize pixels = (VkDeviceSize)kMarkerSize * kMarkerSize, bytes = pixels * 8 * 2;
		VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		info.size = bytes;
		info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		VK_CHECK(vkCreateBuffer(h.vk.device, &info, nullptr, &h.marker), "vkCreateBuffer (marker)");
		VkMemoryRequirements req = {};
		vkGetBufferMemoryRequirements(h.vk.device, h.marker, &req);
		int32_t type = FindMemoryType(h.vk, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		if (type < 0) { Status("Vulkan: no host-visible memory for the marker"); return false; }
		VkMemoryAllocateInfo alloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		alloc.allocationSize = req.size;
		alloc.memoryTypeIndex = (uint32_t)type;
		VK_CHECK(vkAllocateMemory(h.vk.device, &alloc, nullptr, &h.markerMemory), "vkAllocateMemory (marker)");
		VK_CHECK(vkBindBufferMemory(h.vk.device, h.marker, h.markerMemory, 0), "vkBindBufferMemory (marker)");
		uint16_t* data = nullptr;
		VK_CHECK(vkMapMemory(h.vk.device, h.markerMemory, 0, bytes, 0, (void**)&data), "vkMapMemory (marker)");
		const float colors[2][4] = { { 4.0f, 0.0f, 4.0f, 1.0f }, { 0.0f, 4.0f, 0.0f, 1.0f } };
		for (int box = 0; box < 2; box++)
			for (VkDeviceSize p = 0; p < pixels; p++)
				for (int c = 0; c < 4; c++) data[(box * pixels + p) * 4 + c] = FloatToHalf(colors[box][c]);
		vkUnmapMemory(h.vk.device, h.markerMemory);
		h.markerReady = true;
		return true;
	}

#ifndef NVR_DLSS_NO_SDK
	// UNOFFICIAL 2026-10-30 DLSS Ray Reconstruction (DLSS-D): the NGX string keys the helper uses that have no #define in the local
	// SDK headers (NVIDIA's nvsdk_ngx_defs_dlssd.h and nvsdk_ngx_helpers_dlssd_vk.h are not in the local copy), each looked for as an
	// exact line in the string dumps of nvngx_dlss.dll 310.9.16 ("rel") and of the driver's NGX core _nvngx.dll 32.0.15.8266
	// ("core") on 2026-10-30:
	//   SuperSamplingDenoising.Available              core: found verbatim (rel: not there)
	//   SuperSamplingDenoising.NeedsUpdatedDriver     core: found verbatim
	//   SuperSamplingDenoising.MinDriverVersionMajor  core: found verbatim
	//   SuperSamplingDenoising.MinDriverVersionMinor  core: found verbatim
	//   SuperSamplingDenoising.FeatureInitResult      core: found verbatim
	//   DLSS.Denoise.Mode                             rel: found verbatim (core too). The value 1 (NVSDK_NGX_DLSS_Denoise_Mode_DLUnified) is
	//                                                 from memory; rel holds the enum's names as strings, _Off then _DLUnified, next to
	//                                                 "DLSS-D Mode %s (%d)", which fits Off 0 / DLUnified 1.
	//   DLSS.Use.HW.Depth                             rel: found verbatim. 1 = hardware depth, 0 = linear depth: from memory.
	//   DLSS.Roughness.Mode                           rel: found verbatim; deliberately not set (0, its default, is "unpacked": a roughness
	//                                                 image of its own; from memory)
	//   DLSS.Input.DiffuseAlbedo                      rel: found verbatim
	//   DLSS.Input.SpecularAlbedo                     rel: found verbatim
	//   DLSS.Input.Normals                            NOT found as such (only DLSS.Input.Normals.Subrect.Base.X/Y are). Believed (memory) to be
	//                                                 a macro alias of GBuffer.Normals (#defined locally, found verbatim in rel): both are set.
	//   DLSS.Input.Roughness                          NOT found as such (likewise); believed an alias of GBuffer.Roughness (#defined locally,
	//                                                 found verbatim in rel): both are set.
	//   WorldToViewMatrix                             rel: found verbatim (a float[16] by void pointer: memory)
	//   ViewToClipMatrix                              rel: found verbatim (likewise)
	//   RayReconstruction.Hint.Render.Preset.DLAA, .Quality, .Balanced, .Performance, .UltraPerformance, .UltraQuality
	//                                                 rel: all six found verbatim (nvsdk_ngx_s.lib too). The values (Default 0, D 4, E 5) are
	//                                                 the static library's debug types (NVSDK_NGX_RayReconstruction_Hint_Render_Preset).
	// Every other key (Width, OutWidth, PerfQualityValue, DLSS.Feature.Create.Flags, Color, Depth, MotionVectors, Jitter.Offset.X,
	// GBuffer.Normals, the DLSS SR preset hints, ...) is a #define of the local nvsdk_ngx_defs.h and is used by its macro name.
	const char* const kRrKeyAvailable          = "SuperSamplingDenoising.Available";
	const char* const kRrKeyNeedsUpdatedDriver = "SuperSamplingDenoising.NeedsUpdatedDriver";
	const char* const kRrKeyMinDriverMajor     = "SuperSamplingDenoising.MinDriverVersionMajor";
	const char* const kRrKeyMinDriverMinor     = "SuperSamplingDenoising.MinDriverVersionMinor";
	const char* const kRrKeyFeatureInitResult  = "SuperSamplingDenoising.FeatureInitResult";
	const char* const kRrKeyDenoiseMode        = "DLSS.Denoise.Mode";
	const char* const kRrKeyUseHwDepth         = "DLSS.Use.HW.Depth";
	const char* const kRrKeyDiffuseAlbedo      = "DLSS.Input.DiffuseAlbedo";
	const char* const kRrKeySpecularAlbedo     = "DLSS.Input.SpecularAlbedo";
	const char* const kRrKeyInputNormals       = "DLSS.Input.Normals";
	const char* const kRrKeyInputRoughness     = "DLSS.Input.Roughness";
	const char* const kRrKeyWorldToView        = "WorldToViewMatrix";
	const char* const kRrKeyViewToClip         = "ViewToClipMatrix";
	const int kRrDenoiseModeDLUnified = 1;

	// UNOFFICIAL 2026-10-30 the presets: one hint per DLSS quality mode, in this order: DLAA, Quality, Balanced, Performance,
	// Ultra Performance, Ultra Quality. DLSS SR's keys and letters are the local nvsdk_ngx_defs.h's; RR's keys are listed above.
	const char* const kSrPresetKeys[6] = { NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,
		NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance,
		NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality };
	const unsigned int kSrPresetHints[NVR_DLSS_PRESET_COUNT][6] = {
		{ NVSDK_NGX_DLSS_Hint_Render_Preset_Default, NVSDK_NGX_DLSS_Hint_Render_Preset_Default, NVSDK_NGX_DLSS_Hint_Render_Preset_Default,
		  NVSDK_NGX_DLSS_Hint_Render_Preset_Default, NVSDK_NGX_DLSS_Hint_Render_Preset_Default, NVSDK_NGX_DLSS_Hint_Render_Preset_Default },
		{ NVSDK_NGX_DLSS_Hint_Render_Preset_F, NVSDK_NGX_DLSS_Hint_Render_Preset_E, NVSDK_NGX_DLSS_Hint_Render_Preset_E,	// CNN
		  NVSDK_NGX_DLSS_Hint_Render_Preset_E, NVSDK_NGX_DLSS_Hint_Render_Preset_F, NVSDK_NGX_DLSS_Hint_Render_Preset_E },
		{ NVSDK_NGX_DLSS_Hint_Render_Preset_K, NVSDK_NGX_DLSS_Hint_Render_Preset_K, NVSDK_NGX_DLSS_Hint_Render_Preset_K,	// transformer
		  NVSDK_NGX_DLSS_Hint_Render_Preset_M, NVSDK_NGX_DLSS_Hint_Render_Preset_L, NVSDK_NGX_DLSS_Hint_Render_Preset_K },
		{ NVSDK_NGX_DLSS_Hint_Render_Preset_J, NVSDK_NGX_DLSS_Hint_Render_Preset_J, NVSDK_NGX_DLSS_Hint_Render_Preset_J,	// transformer J
		  NVSDK_NGX_DLSS_Hint_Render_Preset_J, NVSDK_NGX_DLSS_Hint_Render_Preset_J, NVSDK_NGX_DLSS_Hint_Render_Preset_J },
	};
	const char* const kSrPresetNames[NVR_DLSS_PRESET_COUNT] = { "default (no hint)", "CNN (E; F at DLAA and Ultra Performance)",
		"transformer (K; M at Performance, L at Ultra Performance)", "transformer J" };
	const char* const kRrPresetKeys[6] = { "RayReconstruction.Hint.Render.Preset.DLAA", "RayReconstruction.Hint.Render.Preset.Quality",
		"RayReconstruction.Hint.Render.Preset.Balanced", "RayReconstruction.Hint.Render.Preset.Performance",
		"RayReconstruction.Hint.Render.Preset.UltraPerformance", "RayReconstruction.Hint.Render.Preset.UltraQuality" };
	const unsigned int kRrPresetHints[NVR_DLSS_RR_PRESET_COUNT] = { 0, 4, 5 };	// default (no hint), D, E: the same in every mode
	const char* const kRrPresetNames[NVR_DLSS_RR_PRESET_COUNT] = { "default (no hint)", "D", "E" };

	// NVR's numbers, anything out of range = the default (no hint).
	uint32_t SrPreset(uint32_t preset) { return preset < NVR_DLSS_PRESET_COUNT ? preset : (uint32_t)NVR_DLSS_PRESET_DEFAULT; }
	uint32_t RrPreset(uint32_t preset) { return preset < NVR_DLSS_RR_PRESET_COUNT ? preset : (uint32_t)NVR_DLSS_RR_PRESET_DEFAULT; }

	// UNOFFICIAL 2026-10-30 DLSS Ray Reconstruction's capability keys (the driver NGX core's), from the capability parameters DLSS
	// SR's check reads. True: RR can be tried (its feature creation still decides); false with the reason. Reads only (the probe
	// uses it too).
	bool RayReconstructionCapability(Helper& h, std::string& why) {
		if (!h.ngxParams) { why = "no NGX capability parameters"; return false; }
		int available = 0, needsDriver = 0, initResult = 0;
		unsigned int minMajor = 0, minMinor = 0;
		const NVSDK_NGX_Result r = NVSDK_NGX_Parameter_GetI(h.ngxParams, kRrKeyAvailable, &available);
		NVSDK_NGX_Parameter_GetI(h.ngxParams, kRrKeyNeedsUpdatedDriver, &needsDriver);
		NVSDK_NGX_Parameter_GetUI(h.ngxParams, kRrKeyMinDriverMajor, &minMajor);
		NVSDK_NGX_Parameter_GetUI(h.ngxParams, kRrKeyMinDriverMinor, &minMinor);
		NVSDK_NGX_Parameter_GetI(h.ngxParams, kRrKeyFeatureInitResult, &initResult);
		const bool dll = FileNextToHelper(L"nvngx_dlssd.dll");
		char text[384];
		if (NVSDK_NGX_FAILED(r))
			snprintf(text, sizeof(text), "the driver's NGX does not report it: no SuperSamplingDenoising.Available, %s%s", NgxResultString(r).c_str(),
				dll ? "" : "; nvngx_dlssd.dll is not next to the helper either");
		else if (!available) {
			if (needsDriver) snprintf(text, sizeof(text), "the driver is too old: %u.%u or newer needed", minMajor, minMinor);
			else if (!dll) snprintf(text, sizeof(text), "nvngx_dlssd.dll, NVIDIA's Ray Reconstruction runtime, is not next to the helper");
			else snprintf(text, sizeof(text), "SuperSamplingDenoising not available, init result %s", NgxResultString((NVSDK_NGX_Result)initResult).c_str());
		}
		else if (!h.vk.rrExtensions) snprintf(text, sizeof(text), "the driver lacks a Vulkan extension NGX asked for it, see the log");
		else {
			if (!dll) Log("NGX: Ray Reconstruction is reported available although nvngx_dlssd.dll is not next to the helper (NGX found one elsewhere)");
			return true;
		}
		why = text;
		return false;
	}

	// NGX init and the capability check. Not available (any GTX card, an old driver, no nvngx_dlss.dll): logged, false.
	bool InitNgx(Helper& h) {
		NVSDK_NGX_FeatureDiscoveryInfo discovery = NgxDiscoveryInfo();
		NVSDK_NGX_FeatureRequirement requirement = {};
		NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_GetFeatureRequirements(h.vk.instance, h.vk.physical, &discovery, &requirement);
		if (g_shared) g_shared->ngxResult = (int32_t)r;
		if (r == NVSDK_NGX_Result_FAIL_FeatureNotSupported) {
			// What NGX answers on a GPU without DLSS (any GTX card: no tensor cores).
			Status("NGX: DLSS not available on this GPU (%s: NGX says DLSS is not supported here; it needs a GeForce RTX 20 series or newer)",
				h.vk.properties.deviceName);
			return false;
		}
		if (NVSDK_NGX_FAILED(r)) Log("NGX: feature requirement query failed: %s", NgxResultString(r).c_str());
		else if (requirement.FeatureSupported != 0) {
			const unsigned s = (unsigned)requirement.FeatureSupported;
			Status("NGX: DLSS not available on this GPU (%s%s%s%s%s; minimum GPU architecture 0x%X)",
				(s & NVSDK_NGX_FeatureSupportResult_AdapterUnsupported) ? "the GPU has no DLSS support (RTX 20 series or newer needed) " : "",
				(s & NVSDK_NGX_FeatureSupportResult_DriverVersionUnsupported) ? "the driver is too old " : "",
				(s & NVSDK_NGX_FeatureSupportResult_OSVersionBelowMinimumSupported) ? "Windows is too old " : "",
				(s & NVSDK_NGX_FeatureSupportResult_CheckNotPresent) ? "no check present " : "",
				(s & NVSDK_NGX_FeatureSupportResult_NotImplemented) ? "not implemented " : "", requirement.MinHWArchitecture);
			return false;
		}
		if (!h.vk.ngxExtensions) {
			Status("NGX: DLSS not available on this GPU (NGX refused this GPU's Vulkan device, or the driver lacks an extension NGX needs)");
			return false;
		}
		r = NVSDK_NGX_VULKAN_Init_with_ProjectID(kNgxProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, kNgxEngineVersion, g_ngxDataPath.c_str(),
			h.vk.instance, h.vk.physical, h.vk.device, vkGetInstanceProcAddr, vkGetDeviceProcAddr, &g_ngxFeatureInfo, NVSDK_NGX_Version_API);
		if (g_shared) g_shared->ngxResult = (int32_t)r;
		if (NVSDK_NGX_FAILED(r)) {
			Status("NGX: DLSS not available on this GPU (NGX init failed: %s)", NgxResultString(r).c_str());
			return false;
		}
		h.ngxInit = true;
		r = NVSDK_NGX_VULKAN_GetCapabilityParameters(&h.ngxParams);
		if (NVSDK_NGX_FAILED(r) || !h.ngxParams) {
			Status("NGX: DLSS not available on this GPU (no capability parameters: %s)", NgxResultString(r).c_str());
			return false;
		}
		int available = 0, needsDriver = 0;
		unsigned int minMajor = 0, minMinor = 0;
		int initResult = 0;
		NVSDK_NGX_Parameter_GetI(h.ngxParams, NVSDK_NGX_Parameter_SuperSampling_Available, &available);
		NVSDK_NGX_Parameter_GetI(h.ngxParams, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needsDriver);
		NVSDK_NGX_Parameter_GetUI(h.ngxParams, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, &minMajor);
		NVSDK_NGX_Parameter_GetUI(h.ngxParams, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, &minMinor);
		NVSDK_NGX_Parameter_GetI(h.ngxParams, NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, &initResult);
		if (!available) {
			if (needsDriver) Status("NGX: DLSS not available on this GPU (the driver is too old: %u.%u or newer needed)", minMajor, minMinor);
			else Status("NGX: DLSS not available on this GPU (SuperSampling not available, init result %s)", NgxResultString((NVSDK_NGX_Result)initResult).c_str());
			return false;
		}
		h.dlssAvailable = true;
		Status("NGX: DLSS available on %s", h.vk.properties.deviceName);
		if (h.mode == NVR_DLSS_MODE_DLSS && h.rrWanted && g_shared) {
			// UNOFFICIAL 2026-10-30 DLSS Ray Reconstruction, only when NVR made the RR images: available or not, DLSS SR runs on (this
			// never returns false). RR's own parameter map is made here, so a failure shows before the first frame.
			std::string why;
			bool rr = RayReconstructionCapability(h, why);
			if (rr) {
				r = NVSDK_NGX_VULKAN_AllocateParameters(&h.rrParams);
				if (NVSDK_NGX_FAILED(r) || !h.rrParams) {
					rr = false;
					h.rrParams = nullptr;
					why = "no parameter map of its own: " + NgxResultString(r);
				}
			}
			g_shared->rrAvailable = rr ? 1u : 0u;
			if (rr) Status("NGX: Ray Reconstruction available on %s", h.vk.properties.deviceName);
			else Status("NGX: Ray Reconstruction not available (%s); DLSS SR goes on", why.c_str());
		}
		return true;
	}

	// DLSS's quality mode from render / display width (NVR's RenderScale; DLSS's own ratios: Quality 0.667, Balanced 0.58,
	// Performance 0.5, Ultra Performance 0.333, DLAA 1). NVR sends quality 0 = "pick it here".
	NVSDK_NGX_PerfQuality_Value QualityFor(uint32_t renderWidth, uint32_t displayWidth) {
		const float r = displayWidth ? (float)renderWidth / (float)displayWidth : 1.0f;
		if (r >= 0.99f) return NVSDK_NGX_PerfQuality_Value_DLAA;
		if (r >= 0.66f) return NVSDK_NGX_PerfQuality_Value_MaxQuality;
		if (r >= 0.57f) return NVSDK_NGX_PerfQuality_Value_Balanced;
		if (r >= 0.49f) return NVSDK_NGX_PerfQuality_Value_MaxPerf;
		return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
	}

	// The DLSS feature for these sizes / HDR / quality, (re)created inside this frame's command buffer when they change.
	// UNOFFICIAL 2026-10-30: and NVR's model preset ([Shaders.TAA.DLSS] Preset), whose hints are read at creation only.
	bool EnsureDlss(Helper& h, VkCommandBuffer cmd, const NvrDlssFrame& f) {
		const SharedImage& out = h.images[NVR_DLSS_IMAGE_OUTPUT];
		const uint32_t quality = (uint32_t)QualityFor(f.renderWidth, out.width);
		const uint32_t preset = SrPreset(f.preset);
		const uint32_t key[7] = { f.renderWidth, f.renderHeight, out.width, out.height, f.hdr, quality, preset };
		if (h.dlss && !memcmp(key, h.dlssKey, sizeof(key))) return true;
		if (h.dlss) {
			vkDeviceWaitIdle(h.vk.device);
			NVSDK_NGX_VULKAN_ReleaseFeature(h.dlss);
			h.dlss = nullptr;
		}
		// UNOFFICIAL 2026-10-30 all six hints, once a preset other than the default was asked for (the default then writes 0 = no
		// hint, which clears the earlier choice on this parameter map). Until then nothing is written: the parameter map is exactly
		// what it was before presets existed.
		if (preset != NVR_DLSS_PRESET_DEFAULT) h.srPresetHints = true;
		if (h.srPresetHints)
			for (int mode = 0; mode < 6; mode++) NVSDK_NGX_Parameter_SetUI(h.ngxParams, kSrPresetKeys[mode], kSrPresetHints[preset][mode]);
		NVSDK_NGX_DLSS_Create_Params create = {};
		create.Feature.InWidth = f.renderWidth;
		create.Feature.InHeight = f.renderHeight;
		create.Feature.InTargetWidth = out.width;
		create.Feature.InTargetHeight = out.height;
		create.Feature.InPerfQualityValue = (NVSDK_NGX_PerfQuality_Value)quality;
		create.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
			(f.hdr ? (NVSDK_NGX_DLSS_Feature_Flags_IsHDR | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure) : 0);
		create.InEnableOutputSubrects = false;
		NVSDK_NGX_Result r = NGX_VULKAN_CREATE_DLSS_EXT1(h.vk.device, cmd, 1, 1, &h.dlss, h.ngxParams, &create);
		if (NVSDK_NGX_FAILED(r) || !h.dlss) {
			Status("NGX: DLSS feature creation failed (%ux%u -> %ux%u, %s, quality mode %u, preset %s): %s", f.renderWidth, f.renderHeight, out.width,
				out.height, f.hdr ? "HDR" : "LDR", quality, kSrPresetNames[preset], NgxResultString(r).c_str());
			h.dlss = nullptr;
			return false;
		}
		memcpy(h.dlssKey, key, sizeof(key));
		Log("NGX: DLSS feature made: %ux%u -> %ux%u, %s, quality mode %u, preset %s", f.renderWidth, f.renderHeight, out.width, out.height,
			f.hdr ? "HDR" : "LDR", quality, kSrPresetNames[preset]);
		return true;
	}

	// UNOFFICIAL 2026-10-30 DLSS Ray Reconstruction: the DLSS-D feature for these sizes / quality mode / RR preset / depth kind,
	// (re)created inside this frame's command buffer when they change, on RR's own parameter map. The keys are the ones NVIDIA's
	// NGX_VULKAN_CREATE_DLSS_EXT1 sets (nvsdk_ngx_helpers_vk.h) plus DLSS-D's denoise mode, depth kind and preset hints; NVIDIA's
	// own DLSS-D helper (not in the local SDK) is believed to set the same, with NVSDK_NGX_Feature_RayReconstruction.
	bool EnsureDlssd(Helper& h, VkCommandBuffer cmd, const NvrDlssFrame& f) {
		const SharedImage& out = h.images[NVR_DLSS_IMAGE_OUTPUT];
		if (!h.rrParams) {
			Status("NGX: Ray Reconstruction feature creation failed (no parameter map); DLSS SR goes on");
			return false;
		}
		const uint32_t quality = (uint32_t)QualityFor(f.renderWidth, out.width);
		const uint32_t rrPreset = RrPreset(f.rrPreset);
		const uint32_t depthLinear = (f.rrOptions & NVR_DLSS_RR_DEPTH_LINEAR) ? 1u : 0u;
		const uint32_t key[7] = { f.renderWidth, f.renderHeight, out.width, out.height, quality, rrPreset, depthLinear };
		if (h.dlssd && !memcmp(key, h.dlssdKey, sizeof(key))) return true;
		if (h.dlssd) {
			vkDeviceWaitIdle(h.vk.device);
			NVSDK_NGX_VULKAN_ReleaseFeature(h.dlssd);
			h.dlssd = nullptr;
		}
		NVSDK_NGX_Parameter* p = h.rrParams;
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_CreationNodeMask, 1);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_VisibilityNodeMask, 1);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_Width, f.renderWidth);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_Height, f.renderHeight);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_OutWidth, out.width);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_OutHeight, out.height);
		NVSDK_NGX_Parameter_SetI(p, NVSDK_NGX_Parameter_PerfQualityValue, (int)quality);
		// RR frames are always linear HDR (the pre-tonemap RR point), with DLSS's own exposure as for DLSS SR's HDR frames.
		NVSDK_NGX_Parameter_SetI(p, NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,
			NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_IsHDR | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure);
		NVSDK_NGX_Parameter_SetI(p, NVSDK_NGX_Parameter_DLSS_Enable_Output_Subrects, 0);
		NVSDK_NGX_Parameter_SetI(p, kRrKeyDenoiseMode, kRrDenoiseModeDLUnified);
		NVSDK_NGX_Parameter_SetI(p, kRrKeyUseHwDepth, depthLinear ? 0 : 1);
		// DLSS.Roughness.Mode is not set: its default (unpacked) is a roughness image of its own, which NVR sends.
		for (int mode = 0; mode < 6; mode++) NVSDK_NGX_Parameter_SetUI(p, kRrPresetKeys[mode], kRrPresetHints[rrPreset]);
		NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_CreateFeature1(h.vk.device, cmd, NVSDK_NGX_Feature_RayReconstruction, p, &h.dlssd);
		if (g_shared) g_shared->rrResult = (int32_t)r;
		if (NVSDK_NGX_FAILED(r) || !h.dlssd) {
			Status("NGX: Ray Reconstruction feature creation failed (%ux%u -> %ux%u, quality mode %u, RR preset %s, %s depth): %s; DLSS SR goes on",
				f.renderWidth, f.renderHeight, out.width, out.height, quality, kRrPresetNames[rrPreset], depthLinear ? "linear" : "hardware",
				NgxResultString(r).c_str());
			h.dlssd = nullptr;
			return false;
		}
		memcpy(h.dlssdKey, key, sizeof(key));
		Log("NGX: Ray Reconstruction feature made: %ux%u -> %ux%u, quality mode %u, RR preset %s, %s depth", f.renderWidth, f.renderHeight,
			out.width, out.height, quality, kRrPresetNames[rrPreset], depthLinear ? "linear" : "hardware");
		return true;
	}
#endif

	void Barrier(VkCommandBuffer cmd, VkImage image, VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkImageLayout from, VkImageLayout to,
		VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
		VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
		b.srcAccessMask = srcAccess;
		b.dstAccessMask = dstAccess;
		b.oldLayout = from;
		b.newLayout = to;
		b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b.image = image;
		b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
	}

	// Pass-through: the colour input's render rectangle stretched (bilinear) over the whole output, plus the marker boxes.
	void RecordPassThrough(Helper& h, VkCommandBuffer cmd, const NvrDlssFrame& f, uint64_t frame) {
		const SharedImage& in = h.images[NVR_DLSS_IMAGE_COLOR];
		const SharedImage& out = h.images[NVR_DLSS_IMAGE_OUTPUT];
		const uint32_t rw = std::max(1u, std::min(f.renderWidth, in.width)), rh = std::max(1u, std::min(f.renderHeight, in.height));
		Barrier(cmd, in.image, VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
		Barrier(cmd, out.image, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
		VkImageBlit blit = {};
		blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		blit.srcOffsets[1] = { (int32_t)rw, (int32_t)rh, 1 };
		blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		blit.dstOffsets[1] = { (int32_t)out.width, (int32_t)out.height, 1 };
		vkCmdBlitImage(cmd, in.image, VK_IMAGE_LAYOUT_GENERAL, out.image, VK_IMAGE_LAYOUT_GENERAL, 1, &blit, VK_FILTER_LINEAR);
		if (h.markerReady && out.width >= 16 + 32 * 9 && out.height >= 16 + kMarkerSize) {
			Barrier(cmd, out.image, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
			VkBufferImageCopy regions[2] = {};
			for (int i = 0; i < 2; i++) {
				regions[i].bufferOffset = (VkDeviceSize)i * kMarkerSize * kMarkerSize * 8;
				regions[i].imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
				regions[i].imageExtent = { kMarkerSize, kMarkerSize, 1 };
			}
			regions[0].imageOffset = { (int32_t)(16 + 32 * (frame % 8)), 16, 0 };
			regions[1].imageOffset = { 16 + 32 * 8, 16, 0 };
			vkCmdCopyBufferToImage(cmd, h.marker, out.image, VK_IMAGE_LAYOUT_GENERAL, 2, regions);
		}
		Barrier(cmd, out.image, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
	}

	// UNOFFICIAL 2026-10-30 the pass-through test of an RR frame (NVR's HelperTest with RayReconstruction, any NVIDIA GPU): the same
	// stretch and marker, then the four guides' render rectangles as thumbnails along the bottom edge (GuideThumbnail), so a look
	// at the game shows that NVR's guides arrive. The blits convert the formats: roughness (R16F) shows as red, and the normals'
	// negative parts as black.
	void RecordPassThroughRR(Helper& h, VkCommandBuffer cmd, const NvrDlssFrame& f, uint64_t frame) {
		RecordPassThrough(h, cmd, f, frame);
		const SharedImage& out = h.images[NVR_DLSS_IMAGE_OUTPUT];
		const SharedImage& in = h.images[NVR_DLSS_IMAGE_COLOR];
		const uint32_t rw = std::max(1u, std::min(f.renderWidth, in.width)), rh = std::max(1u, std::min(f.renderHeight, in.height));
		VkOffset2D at = {};
		VkExtent2D size = {};
		if (h.imageCount < NVR_DLSS_IMAGE_COUNT || !GuideThumbnail(out.width, out.height, rw, rh, 0, at, size)) return;
		// The pass-through's last barrier already made its writes available; this one orders the blits after them explicitly.
		Barrier(cmd, out.image, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
		for (uint32_t g = 0; g < kGuideCount; g++) {
			const SharedImage& guide = h.images[NVR_DLSS_IMAGE_DIFFUSE_ALBEDO + g];
			if (!GuideThumbnail(out.width, out.height, rw, rh, g, at, size)) break;
			Barrier(cmd, guide.image, VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
			VkImageBlit blit = {};
			blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
			blit.srcOffsets[1] = { (int32_t)std::min(rw, guide.width), (int32_t)std::min(rh, guide.height), 1 };
			blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
			blit.dstOffsets[0] = { at.x, at.y, 0 };
			blit.dstOffsets[1] = { at.x + (int32_t)size.width, at.y + (int32_t)size.height, 1 };
			vkCmdBlitImage(cmd, guide.image, VK_IMAGE_LAYOUT_GENERAL, out.image, VK_IMAGE_LAYOUT_GENERAL, 1, &blit, VK_FILTER_LINEAR);
		}
		Barrier(cmd, out.image, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
	}

#ifndef NVR_DLSS_NO_SDK
	// DLSS: inputs to SHADER_READ_ONLY_OPTIMAL and the output to GENERAL for NGX, everything back to GENERAL (the fork's layout).
	// UNOFFICIAL 2026-10-30 protocol 2: DLSS SR's inputs are named (colour, depth, motion), not "every index below OUTPUT": the RR
	// guides come after OUTPUT and are not DLSS SR's.
	const uint32_t kSrInputs[3] = { NVR_DLSS_IMAGE_COLOR, NVR_DLSS_IMAGE_DEPTH, NVR_DLSS_IMAGE_MOTION };

	bool RecordDlss(Helper& h, VkCommandBuffer cmd, const NvrDlssFrame& f) {
		if (!EnsureDlss(h, cmd, f)) return false;
		const VkImageLayout read = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		for (uint32_t i : kSrInputs)
			Barrier(cmd, h.images[i].image, VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, read,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		Barrier(cmd, h.images[NVR_DLSS_IMAGE_OUTPUT].image, 0, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

		const VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		NVSDK_NGX_Resource_VK res[NVR_DLSS_IMAGE_COUNT] = {};
		for (uint32_t i = 0; i < NVR_DLSS_IMAGE_COUNT_SR; i++) {
			const SharedImage& s = h.images[i];
			res[i] = NVSDK_NGX_Create_ImageView_Resource_VK(s.view, s.image, range, s.format, s.width, s.height, i == NVR_DLSS_IMAGE_OUTPUT);
		}
		NVSDK_NGX_VK_DLSS_Eval_Params eval = {};
		eval.Feature.pInColor = &res[NVR_DLSS_IMAGE_COLOR];
		eval.Feature.pInOutput = &res[NVR_DLSS_IMAGE_OUTPUT];
		eval.pInDepth = &res[NVR_DLSS_IMAGE_DEPTH];
		eval.pInMotionVectors = &res[NVR_DLSS_IMAGE_MOTION];
		eval.InJitterOffsetX = f.jitterX;
		eval.InJitterOffsetY = f.jitterY;
		eval.InRenderSubrectDimensions.Width = f.renderWidth;
		eval.InRenderSubrectDimensions.Height = f.renderHeight;
		eval.InReset = f.reset ? 1 : 0;
		eval.InMVScaleX = f.mvScaleX;
		eval.InMVScaleY = f.mvScaleY;
		eval.InPreExposure = f.preExposure > 0.0f ? f.preExposure : 1.0f;
		eval.InFrameTimeDeltaInMsec = f.frameTimeMs;
		NVSDK_NGX_Result r = NGX_VULKAN_EVALUATE_DLSS_EXT(cmd, h.dlss, h.ngxParams, &eval);
		if (g_shared) g_shared->ngxResult = (int32_t)r;

		for (uint32_t i : kSrInputs)
			Barrier(cmd, h.images[i].image, VK_ACCESS_SHADER_READ_BIT, 0, read, VK_IMAGE_LAYOUT_GENERAL,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
		Barrier(cmd, h.images[NVR_DLSS_IMAGE_OUTPUT].image, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
			VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
		if (NVSDK_NGX_FAILED(r)) {
			Status("NGX: DLSS evaluate failed: %s", NgxResultString(r).c_str());
			return false;
		}
		return true;
	}

	// UNOFFICIAL 2026-10-30 DLSS Ray Reconstruction (DLSS-D) on an RR frame: as RecordDlss, with all seven inputs (every image but
	// OUTPUT) and the keys NVIDIA's NGX_VULKAN_EVALUATE_DLSS_EXT sets (the same names), plus the guides and the matrices, on RR's own
	// parameter map. False: logged, RR is off from here on (the caller runs DLSS SR on the same colour into the same command buffer:
	// the inputs are back in GENERAL whatever the evaluate did).
	bool RecordDlssd(Helper& h, VkCommandBuffer cmd, const NvrDlssFrame& f) {
		if (!EnsureDlssd(h, cmd, f)) return false;
		const VkImageLayout read = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		for (uint32_t i = 0; i < h.imageCount; i++) {
			if (i == NVR_DLSS_IMAGE_OUTPUT) continue;
			Barrier(cmd, h.images[i].image, VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, read,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		}
		Barrier(cmd, h.images[NVR_DLSS_IMAGE_OUTPUT].image, 0, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

		const VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		NVSDK_NGX_Resource_VK res[NVR_DLSS_IMAGE_COUNT] = {};
		for (uint32_t i = 0; i < h.imageCount; i++) {
			const SharedImage& s = h.images[i];
			res[i] = NVSDK_NGX_Create_ImageView_Resource_VK(s.view, s.image, range, s.format, s.width, s.height, i == NVR_DLSS_IMAGE_OUTPUT);
		}
		// NGX takes the matrices by pointer: copies that outlive this call (the frame ring's slot may be rewritten by NVR meanwhile).
		memcpy(h.rrWorldToView, f.worldToView, sizeof(h.rrWorldToView));
		memcpy(h.rrViewToClip, f.viewToClip, sizeof(h.rrViewToClip));

		NVSDK_NGX_Parameter* p = h.rrParams;
		NVSDK_NGX_Parameter_SetVoidPointer(p, NVSDK_NGX_Parameter_Color, &res[NVR_DLSS_IMAGE_COLOR]);
		NVSDK_NGX_Parameter_SetVoidPointer(p, NVSDK_NGX_Parameter_Output, &res[NVR_DLSS_IMAGE_OUTPUT]);
		NVSDK_NGX_Parameter_SetVoidPointer(p, NVSDK_NGX_Parameter_Depth, &res[NVR_DLSS_IMAGE_DEPTH]);
		NVSDK_NGX_Parameter_SetVoidPointer(p, NVSDK_NGX_Parameter_MotionVectors, &res[NVR_DLSS_IMAGE_MOTION]);
		NVSDK_NGX_Parameter_SetVoidPointer(p, kRrKeyDiffuseAlbedo, &res[NVR_DLSS_IMAGE_DIFFUSE_ALBEDO]);
		NVSDK_NGX_Parameter_SetVoidPointer(p, kRrKeySpecularAlbedo, &res[NVR_DLSS_IMAGE_SPECULAR_ALBEDO]);
		// (also under the GBuffer names, as the normals and roughness are: NGX ignores keys it does not read)
		NVSDK_NGX_Parameter_SetVoidPointer(p, NVSDK_NGX_Parameter_GBuffer_DiffuseAlbedo, &res[NVR_DLSS_IMAGE_DIFFUSE_ALBEDO]);
		NVSDK_NGX_Parameter_SetVoidPointer(p, NVSDK_NGX_Parameter_GBuffer_SpecularAlbedo, &res[NVR_DLSS_IMAGE_SPECULAR_ALBEDO]);
		NVSDK_NGX_Parameter_SetVoidPointer(p, NVSDK_NGX_Parameter_GBuffer_Normals, &res[NVR_DLSS_IMAGE_NORMALS]);
		NVSDK_NGX_Parameter_SetVoidPointer(p, kRrKeyInputNormals, &res[NVR_DLSS_IMAGE_NORMALS]);
		NVSDK_NGX_Parameter_SetVoidPointer(p, NVSDK_NGX_Parameter_GBuffer_Roughness, &res[NVR_DLSS_IMAGE_ROUGHNESS]);
		NVSDK_NGX_Parameter_SetVoidPointer(p, kRrKeyInputRoughness, &res[NVR_DLSS_IMAGE_ROUGHNESS]);
		NVSDK_NGX_Parameter_SetVoidPointer(p, kRrKeyWorldToView, h.rrWorldToView);
		NVSDK_NGX_Parameter_SetVoidPointer(p, kRrKeyViewToClip, h.rrViewToClip);
		// The scalars, as NGX_VULKAN_EVALUATE_DLSS_EXT sets them for DLSS SR (its zero-or-one defaults included).
		NVSDK_NGX_Parameter_SetF(p, NVSDK_NGX_Parameter_Jitter_Offset_X, f.jitterX);
		NVSDK_NGX_Parameter_SetF(p, NVSDK_NGX_Parameter_Jitter_Offset_Y, f.jitterY);
		NVSDK_NGX_Parameter_SetF(p, NVSDK_NGX_Parameter_Sharpness, 0.0f);
		NVSDK_NGX_Parameter_SetI(p, NVSDK_NGX_Parameter_Reset, f.reset ? 1 : 0);
		NVSDK_NGX_Parameter_SetF(p, NVSDK_NGX_Parameter_MV_Scale_X, f.mvScaleX == 0.0f ? 1.0f : f.mvScaleX);
		NVSDK_NGX_Parameter_SetF(p, NVSDK_NGX_Parameter_MV_Scale_Y, f.mvScaleY == 0.0f ? 1.0f : f.mvScaleY);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_TonemapperType, 0);
		NVSDK_NGX_Parameter_SetF(p, NVSDK_NGX_Parameter_FrameTimeDeltaInMsec, f.frameTimeMs);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X, 0);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y, 0);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X, 0);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y, 0);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X, 0);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y, 0);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_DLSS_Input_Translucency_SubrectBase_X, 0);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_DLSS_Input_Translucency_SubrectBase_Y, 0);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_SubrectBase_X, 0);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_SubrectBase_Y, 0);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X, 0);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y, 0);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, f.renderWidth);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, f.renderHeight);
		NVSDK_NGX_Parameter_SetF(p, NVSDK_NGX_Parameter_DLSS_Pre_Exposure, f.preExposure > 0.0f ? f.preExposure : 1.0f);
		NVSDK_NGX_Parameter_SetF(p, NVSDK_NGX_Parameter_DLSS_Exposure_Scale, 1.0f);
		NVSDK_NGX_Parameter_SetI(p, NVSDK_NGX_Parameter_DLSS_Indicator_Invert_X_Axis, 0);
		NVSDK_NGX_Parameter_SetI(p, NVSDK_NGX_Parameter_DLSS_Indicator_Invert_Y_Axis, 0);
		NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_EvaluateFeature_C(cmd, h.dlssd, p, nullptr);
		if (g_shared) g_shared->rrResult = (int32_t)r;

		for (uint32_t i = 0; i < h.imageCount; i++) {
			if (i == NVR_DLSS_IMAGE_OUTPUT) continue;
			Barrier(cmd, h.images[i].image, VK_ACCESS_SHADER_READ_BIT, 0, read, VK_IMAGE_LAYOUT_GENERAL,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
		}
		Barrier(cmd, h.images[NVR_DLSS_IMAGE_OUTPUT].image, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
			VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
		if (NVSDK_NGX_FAILED(r)) {
			Status("NGX: Ray Reconstruction evaluate failed: %s; DLSS SR goes on", NgxResultString(r).c_str());
			return false;
		}
		return true;
	}
#endif

	uint32_t Average(const uint64_t* values, uint32_t count) {
		const uint32_t n = std::min(count, 64u);
		if (!n) return 0;
		uint64_t sum = 0;
		for (uint32_t i = 0; i < n; i++) sum += values[i];
		return (uint32_t)(sum / n);
	}

	bool WaitSlot(Helper& h, uint32_t slot, HANDLE parent) {
		if (!h.cmdFrame[slot]) return true;
		VkSemaphoreWaitInfo wait = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
		wait.semaphoreCount = 1;
		wait.pSemaphores = &h.outputReady;
		wait.pValues = &h.cmdFrame[slot];
		for (;;) {
			VkResult r = vkWaitSemaphores(h.vk.device, &wait, 250ull * 1000 * 1000);
			if (r == VK_SUCCESS) break;
			if (r != VK_TIMEOUT) { Status("Vulkan: waiting for an earlier frame failed: %s", VkResultName(r)); return false; }
			if (WaitForSingleObject(parent, 0) == WAIT_OBJECT_0 || g_shared->quit) return false;
		}
		if (h.queries) {
			uint64_t stamps[2] = {};
			if (vkGetQueryPoolResults(h.vk.device, h.queries, slot * 2, 2, sizeof(stamps), stamps, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
				const uint64_t mask = h.vk.timestampBits >= 64 ? ~0ull : ((1ull << h.vk.timestampBits) - 1);
				const double us = (double)((stamps[1] - stamps[0]) & mask) * h.vk.properties.limits.timestampPeriod / 1000.0;
				h.gpuUs[h.gpuCount++ % 64] = (uint64_t)us;
				g_shared->gpuTimeUsLast = (uint32_t)us;
				g_shared->gpuTimeUsAvg = Average(h.gpuUs, h.gpuCount);
			}
		}
		return true;
	}

	// One frame: GPU wait for inputsReady >= N, the work, signal outputReady = N. Always submits (even when the work could not be
	// recorded) so the game's GPU wait for N is released.
	bool ProcessFrame(Helper& h, uint64_t frame, HANDLE parent) {
		const NvrDlssFrame f = g_shared->frames[frame % NVR_DLSS_FRAME_RING];
		NvrDlssFrame use = f;
		if (f.frame != frame) {
			// Overwritten before it was read (NVR ran far ahead): stretch with the latest sizes; the picture is still whole.
			g_shared->paramMismatches++;
			use.renderWidth = f.renderWidth ? f.renderWidth : h.images[NVR_DLSS_IMAGE_COLOR].width;
			use.renderHeight = f.renderHeight ? f.renderHeight : h.images[NVR_DLSS_IMAGE_COLOR].height;
			use.reset = 1;
		}
		const uint32_t slot = (uint32_t)(frame % kCmdRing);
		if (!WaitSlot(h, slot, parent)) return false;
		VkCommandBuffer cmd = h.cmd[slot];
		vkResetCommandBuffer(cmd, 0);
		VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		VK_CHECK(vkBeginCommandBuffer(cmd, &begin), "vkBeginCommandBuffer");
		if (h.queries) {
			vkCmdResetQueryPool(cmd, h.queries, slot * 2, 2);
			vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, h.queries, slot * 2);
		}
		bool dlssRan = false;
		// UNOFFICIAL 2026-10-30 DLSS Ray Reconstruction: an RR frame (only possible with the eight images) goes to DLSS-D while RR is
		// available, else to DLSS SR on the same colour (the picture stays whole; NVR stops sending RR frames once it sees
		// rrAvailable 0). In the pass-through test it is the stretch plus the guide thumbnails. Either way the frame is submitted.
		const bool rrFrame = (use.flags & NVR_DLSS_FRAME_RR) != 0 && h.rrWanted;
		bool rrRan = false;
#ifndef NVR_DLSS_NO_SDK
		if (h.mode == NVR_DLSS_MODE_DLSS && h.dlssAvailable && !h.dlssFailed && f.frame == frame) {
			if (rrFrame && g_shared->rrAvailable && !h.rrFailed) {
				rrRan = RecordDlssd(h, cmd, use);
				if (!rrRan) {
					h.rrFailed = true;
					MemoryBarrier();
					g_shared->rrAvailable = 0;	// never FAILED: that would end DLSS SR too
					if (g_statusEvent) SetEvent(g_statusEvent);
				}
			}
			if (!rrRan) {
				dlssRan = RecordDlss(h, cmd, use);
				if (!dlssRan) {
					h.dlssFailed = true;
					SetState(NVR_DLSS_STATE_FAILED);	// NVR goes back to FSR 2; this and the frames in flight are stretched
				}
			}
		}
#endif
		if (!dlssRan && !rrRan) {
			if (rrFrame && h.mode == NVR_DLSS_MODE_PASSTHROUGH) {
				RecordPassThroughRR(h, cmd, use, frame);
				rrRan = true;
			}
			else RecordPassThrough(h, cmd, use, frame);
		}
		if (h.queries) vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, h.queries, slot * 2 + 1);
		VK_CHECK(vkEndCommandBuffer(cmd), "vkEndCommandBuffer");

		const uint64_t waitValue = frame, signalValue = frame;
		VkTimelineSemaphoreSubmitInfo timeline = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
		timeline.waitSemaphoreValueCount = 1;
		timeline.pWaitSemaphoreValues = &waitValue;
		timeline.signalSemaphoreValueCount = 1;
		timeline.pSignalSemaphoreValues = &signalValue;
		const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO, &timeline };
		submit.waitSemaphoreCount = 1;
		submit.pWaitSemaphores = &h.inputsReady;
		submit.pWaitDstStageMask = &waitStage;
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &cmd;
		submit.signalSemaphoreCount = 1;
		submit.pSignalSemaphores = &h.outputReady;
		VK_CHECK(vkQueueSubmit(h.vk.queue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit");
		h.cmdFrame[slot] = frame;

		LARGE_INTEGER now, frequency;
		QueryPerformanceCounter(&now);
		QueryPerformanceFrequency(&frequency);
		if (f.frame == frame && f.writtenQpc) {
			const uint64_t us = (uint64_t)((now.QuadPart - f.writtenQpc) * 1000000 / frequency.QuadPart);
			h.responseUs[h.responseCount++ % 64] = us;
			g_shared->responseUsAvg = Average(h.responseUs, h.responseCount);
			if (us > g_shared->responseUsMax) g_shared->responseUsMax = (uint32_t)us;
		}
		if (dlssRan) g_shared->dlssEvaluated++;
		if (rrRan) g_shared->rrEvaluated++;
		g_shared->framesSubmitted++;
		MemoryBarrier();
		g_shared->submittedFrame = (int64_t)frame;
		const char* const work = rrRan ? (h.mode == NVR_DLSS_MODE_DLSS ? "DLSS Ray Reconstruction" : "pass-through RR") : dlssRan ? "DLSS" : "pass-through";
		if (!h.loggedFirst) {
			h.loggedFirst = true;
			Log("first frame %llu submitted (%s, render %ux%u -> %ux%u, %s)", (unsigned long long)frame, work,
				use.renderWidth, use.renderHeight, h.images[NVR_DLSS_IMAGE_OUTPUT].width, h.images[NVR_DLSS_IMAGE_OUTPUT].height, use.hdr ? "HDR" : "LDR");
		}
		if (rrRan && !h.loggedFirstRr) {
			h.loggedFirstRr = true;
			Log("first RR frame %llu submitted (%s, render %ux%u -> %ux%u, %s normals, %s depth)", (unsigned long long)frame, work, use.renderWidth,
				use.renderHeight, h.images[NVR_DLSS_IMAGE_OUTPUT].width, h.images[NVR_DLSS_IMAGE_OUTPUT].height,
				(use.rrOptions & NVR_DLSS_RR_NORMALS_VIEW) ? "view-space" : "world-space", (use.rrOptions & NVR_DLSS_RR_DEPTH_LINEAR) ? "linear" : "hardware");
		}
		if ((frame % 600) == 0) {
			char rr[64] = "";
			if (h.rrWanted) snprintf(rr, sizeof(rr), ", %u RR (%s)", g_shared->rrEvaluated, g_shared->rrAvailable ? "available" : "off");
			Log("frame %llu: %llu submitted, helper GPU %.3f ms avg, response %.3f ms avg (max %.3f), %u overwritten, %u DLSS%s",
				(unsigned long long)frame, (unsigned long long)g_shared->framesSubmitted, g_shared->gpuTimeUsAvg / 1000.0,
				g_shared->responseUsAvg / 1000.0, g_shared->responseUsMax / 1000.0, g_shared->paramMismatches, g_shared->dlssEvaluated, rr);
		}
		return true;
	}

	int RunHelper(const wchar_t* base) {
		OpenLog(L"NvrDlssHelper.log");
		Log("NvrDlssHelper (protocol %u, %s) for New Vegas Reloaded, shared block %ls", NVR_DLSS_PROTOCOL_VERSION,
#ifndef NVR_DLSS_NO_SDK
			"with NVIDIA DLSS (NGX)",
#else
			"built without NVIDIA's SDK: pass-through only",
#endif
			base);
		HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, base);
		if (!mapping) { Log("the shared block %ls does not exist (error %lu)", base, GetLastError()); return 2; }
		NvrDlssShared* shared = (NvrDlssShared*)MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(NvrDlssShared));
		if (!shared) { Log("could not map the shared block (error %lu)", GetLastError()); return 2; }
		if (shared->magic != NVR_DLSS_MAGIC || shared->version != NVR_DLSS_PROTOCOL_VERSION || shared->size != sizeof(NvrDlssShared)) {
			Log("the shared block is of another version (magic 0x%08X, version %u, size %u; this helper: version %u, size %u)", shared->magic,
				shared->version, shared->size, NVR_DLSS_PROTOCOL_VERSION, (unsigned)sizeof(NvrDlssShared));
			return 2;
		}
		const std::wstring frameName = std::wstring(base) + NVR_DLSS_EVENT_FRAME_SUFFIX, statusName = std::wstring(base) + NVR_DLSS_EVENT_STATUS_SUFFIX;
		HANDLE frameEvent = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, frameName.c_str());
		g_statusEvent = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, statusName.c_str());
		g_shared = shared;
		if (!frameEvent || !g_statusEvent) { Status("the helper's events do not exist"); SetState(NVR_DLSS_STATE_FAILED); return 2; }
		HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, shared->parentPid);
		if (!parent) { Status("the game's process %u cannot be opened", shared->parentPid); SetState(NVR_DLSS_STATE_FAILED); return 2; }

		// NVR fills in the handles after it started this process.
		for (int i = 0; i < 100 && !shared->setupDone; i++) {
			HANDLE waits[2] = { frameEvent, parent };
			if (WaitForMultipleObjects(2, waits, FALSE, 100) == WAIT_OBJECT_0 + 1) return 0;
		}
		MemoryBarrier();
		if (!shared->setupDone) { Status("NVR did not finish the setup in 10 seconds"); SetState(NVR_DLSS_STATE_FAILED); return 2; }

		Helper h;
		h.mode = shared->mode;
		Log("mode: %s, display %ux%u, inputs %ux%u", h.mode == NVR_DLSS_MODE_DLSS ? "DLSS" : "pass-through test", shared->displayWidth,
			shared->displayHeight, shared->inputWidth, shared->inputHeight);
		auto fail = [&](const char* why) { if (why) Status("%s", why); SetState(NVR_DLSS_STATE_FAILED); return 3; };
		if (shared->vendorId != kNvidiaVendor) return fail("not an NVIDIA GPU: the helper runs on NVIDIA GPUs only (NVIDIA's DLSS SDK licence)");
		// UNOFFICIAL 2026-10-30 protocol 2: four images, or eight (the RR guides) only together with NVR_DLSS_FEATURE_RR. The RR bit
		// without the guides only means no RR (DLSS SR as before).
		shared->rrAvailable = 0;
		h.imageCount = shared->imageCount;
		if (h.imageCount != NVR_DLSS_IMAGE_COUNT_SR && h.imageCount != NVR_DLSS_IMAGE_COUNT) {
			char why[160];
			snprintf(why, sizeof(why), "the shared block names %u images (%u or %u expected)", h.imageCount, (unsigned)NVR_DLSS_IMAGE_COUNT_SR,
				(unsigned)NVR_DLSS_IMAGE_COUNT);
			return fail(why);
		}
		const bool rrFeature = (shared->features & NVR_DLSS_FEATURE_RR) != 0;
		if (h.imageCount == NVR_DLSS_IMAGE_COUNT && !rrFeature) return fail("the shared block has the RR guide images but not the RR feature bit");
		h.rrWanted = rrFeature && h.imageCount == NVR_DLSS_IMAGE_COUNT;
		if (rrFeature && !h.rrWanted) Log("NVR wants Ray Reconstruction but made no RR guide images: no Ray Reconstruction (DLSS SR as before)");
		if (h.rrWanted) Log("Ray Reconstruction wanted: %u images (the colour, depth, motion and output, plus %u RR guides)", h.imageCount, kGuideCount);
		// (RR's extensions only for real DLSS-D: the pass-through test's RR frames need no NGX.)
		if (!CreateVulkanDevice(h.vk, shared->deviceLuid, shared->deviceLuidValid != 0, shared->deviceUuid, true,
			h.rrWanted && h.mode == NVR_DLSS_MODE_DLSS)) return fail(nullptr);
		if (memcmp(h.vk.ids.driverUUID, shared->driverUuid, VK_UUID_SIZE))
			return fail("the helper's Vulkan driver is not the game's (driver UUIDs differ): images cannot be shared");
		for (uint32_t i = 0; i < h.imageCount; i++)
			if (!ImportImage(h.vk, shared->images[i], kImageNames[i], h.images[i])) return fail(nullptr);
		if (!ImportTimeline(h.vk, shared->semInputsReady, "inputs-ready", h.inputsReady) ||
			!ImportTimeline(h.vk, shared->semOutputReady, "output-ready", h.outputReady)) return fail(nullptr);

		VkCommandPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		poolInfo.queueFamilyIndex = h.vk.queueFamily;
		if (vkCreateCommandPool(h.vk.device, &poolInfo, nullptr, &h.pool) != VK_SUCCESS) return fail("Vulkan: vkCreateCommandPool failed");
		VkCommandBufferAllocateInfo cmdInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		cmdInfo.commandPool = h.pool;
		cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cmdInfo.commandBufferCount = kCmdRing;
		if (vkAllocateCommandBuffers(h.vk.device, &cmdInfo, h.cmd) != VK_SUCCESS) return fail("Vulkan: vkAllocateCommandBuffers failed");
		if (h.vk.timestampBits) {
			VkQueryPoolCreateInfo queryInfo = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
			queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
			queryInfo.queryCount = kCmdRing * 2;
			if (vkCreateQueryPool(h.vk.device, &queryInfo, nullptr, &h.queries) != VK_SUCCESS) h.queries = VK_NULL_HANDLE;
		}
		if (!CreateMarker(h)) return fail(nullptr);

#ifndef NVR_DLSS_NO_SDK
		shared->ngxAvailable = InitNgx(h) ? 1u : 0u;
		if (h.mode == NVR_DLSS_MODE_DLSS && !h.dlssAvailable) return fail(nullptr);	// NVR logs the reason and runs FSR 2
#else
		shared->ngxAvailable = 0;
		if (h.mode == NVR_DLSS_MODE_DLSS) return fail("NGX: this helper was built without NVIDIA's DLSS SDK (pass-through test only)");
		Status("NGX: not built in (pass-through test only)");
#endif
		// UNOFFICIAL 2026-10-30 the pass-through test takes RR frames on any NVIDIA GPU (no NGX: the stretch plus the guide thumbnails).
		if (h.mode == NVR_DLSS_MODE_PASSTHROUGH && h.rrWanted) shared->rrAvailable = 1;
		char rrNote[64] = "";
		if (h.rrWanted) snprintf(rrNote, sizeof(rrNote), ", Ray Reconstruction %s", shared->rrAvailable ? "available" : "not available");
		Status("ready: %s on %s (%ux%u -> %ux%u)%s", h.mode == NVR_DLSS_MODE_DLSS ? "DLSS" : "pass-through test", h.vk.properties.deviceName,
			shared->inputWidth, shared->inputHeight, shared->displayWidth, shared->displayHeight, rrNote);
		SetState(NVR_DLSS_STATE_READY);

		uint64_t next = 1;
		bool parentGone = false;
		for (;;) {
			HANDLE waits[2] = { frameEvent, parent };
			const DWORD w = WaitForMultipleObjects(2, waits, FALSE, 250);
			if (w == WAIT_OBJECT_0 + 1) { Log("the game exited"); parentGone = true; break; }
			if (shared->quit) {
				// Frames already submitted finish first (their GPU waits are released by the game's own signals), so nothing the
				// game's queue waits for is left behind; NVR signals the semaphore itself if this takes too long.
				Log("NVR asked the helper to exit");
				if (next > 1) {
					const uint64_t last = next - 1;
					VkSemaphoreWaitInfo wait = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
					wait.semaphoreCount = 1;
					wait.pSemaphores = &h.outputReady;
					wait.pValues = &last;
					vkWaitSemaphores(h.vk.device, &wait, 2000ull * 1000 * 1000);
				}
				break;
			}
			const int64_t latest = InterlockedCompareExchange64((volatile LONG64*)&shared->latestFrame, 0, 0);
			MemoryBarrier();
			while ((int64_t)next <= latest) {
				if (!ProcessFrame(h, next, parent)) { Log("stopping at frame %llu", (unsigned long long)next); goto done; }
				next++;
			}
		}
	done:
		if (shared->state != NVR_DLSS_STATE_FAILED) SetState(NVR_DLSS_STATE_EXITED);
		Log("exit after %llu frames%s", (unsigned long long)shared->framesSubmitted, parentGone ? " (the game is gone)" : "");
		// No vkDeviceWaitIdle / NGX shutdown here: a submission may still wait for a signal the game will never send (it exited);
		// the process exit releases everything.
		// UNOFFICIAL 2026-10-30: the DLSS SR and RR features likewise (ReleaseFeature may touch the GPU); only RR's own parameter map
		// (CPU memory, no GPU work) is destroyed by hand.
#ifndef NVR_DLSS_NO_SDK
		if (h.rrParams) { NVSDK_NGX_VULKAN_DestroyParameters(h.rrParams); h.rrParams = nullptr; }
#endif
		if (g_log) fclose(g_log);
		ExitProcess(0);
	}

	// ---------------------------------------------------------------------------------------------------------------------
	// --selftest: this process does what NVR + the fork do (export, signal N, wait N, read back), a child copy is the helper in
	// pass-through mode. Checks every 50th frame's pixels: the stretched input pattern and the marker box at frame % 8.

	struct Exported {
		VkImage image = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		NvrDlssImageDesc desc = {};
	};

	bool ExportImage(const VulkanDevice& vk, VkFormat format, uint32_t w, uint32_t h, Exported& out) {
		out.desc.vkFormat = (uint32_t)format;
		out.desc.width = w;
		out.desc.height = h;
		out.desc.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
			VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		out.desc.flags = 0;
		out.desc.tiling = VK_IMAGE_TILING_OPTIMAL;
		out.desc.layout = VK_IMAGE_LAYOUT_GENERAL;
		out.desc.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
		out.desc.dedicated = 1;
		VkExternalMemoryImageCreateInfo external = { VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO };
		external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
		VkImageCreateInfo info = ImageInfo(out.desc, &external);
		VK_CHECK(vkCreateImage(vk.device, &info, nullptr, &out.image), "vkCreateImage (export)");
		VkMemoryRequirements req = {};
		vkGetImageMemoryRequirements(vk.device, out.image, &req);
		int32_t type = FindMemoryType(vk, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		if (type < 0) { Status("selftest: no device-local memory type"); return false; }
		VkMemoryDedicatedAllocateInfo dedicated = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
		dedicated.image = out.image;
		VkExportMemoryAllocateInfo exportInfo = { VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO, &dedicated };
		exportInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
		VkMemoryAllocateInfo alloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &exportInfo };
		alloc.allocationSize = req.size;
		alloc.memoryTypeIndex = (uint32_t)type;
		VK_CHECK(vkAllocateMemory(vk.device, &alloc, nullptr, &out.memory), "vkAllocateMemory (export)");
		VK_CHECK(vkBindImageMemory(vk.device, out.image, out.memory, 0), "vkBindImageMemory (export)");
		out.desc.allocationSize = req.size;
		out.desc.memoryTypeIndex = (uint32_t)type;
		return true;
	}

	bool ExportHandle(const VulkanDevice& vk, VkDeviceMemory memory, HANDLE child, uint64_t& out) {
		VkMemoryGetWin32HandleInfoKHR info = { VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR };
		info.memory = memory;
		info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
		HANDLE handle = nullptr;
		VK_CHECK(vk.getMemoryWin32Handle(vk.device, &info, &handle), "vkGetMemoryWin32HandleKHR");
		HANDLE remote = nullptr;
		if (!DuplicateHandle(GetCurrentProcess(), handle, child, &remote, 0, FALSE, DUPLICATE_SAME_ACCESS | DUPLICATE_CLOSE_SOURCE)) {
			Status("selftest: DuplicateHandle failed (%lu)", GetLastError());
			return false;
		}
		out = (uint64_t)(uintptr_t)remote;
		return true;
	}

	bool ExportSemaphore(const VulkanDevice& vk, VkSemaphore& sem, HANDLE child, uint64_t& out) {
		VkExportSemaphoreCreateInfo exportInfo = { VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO };
		exportInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
		VkSemaphoreTypeCreateInfo type = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO, &exportInfo };
		type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
		VkSemaphoreCreateInfo info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type };
		VK_CHECK(vkCreateSemaphore(vk.device, &info, nullptr, &sem), "vkCreateSemaphore (export)");
		VkSemaphoreGetWin32HandleInfoKHR get = { VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR };
		get.semaphore = sem;
		get.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
		HANDLE handle = nullptr;
		VK_CHECK(vk.getSemaphoreWin32Handle(vk.device, &get, &handle), "vkGetSemaphoreWin32HandleKHR");
		HANDLE remote = nullptr;
		if (!DuplicateHandle(GetCurrentProcess(), handle, child, &remote, 0, FALSE, DUPLICATE_SAME_ACCESS | DUPLICATE_CLOSE_SOURCE)) {
			Status("selftest: DuplicateHandle failed (%lu)", GetLastError());
			return false;
		}
		out = (uint64_t)(uintptr_t)remote;
		return true;
	}

	// ---------------------------------------------------------------------------------------------------------------------
	// --probe: what this PC offers.

	int RunProbe() {
		g_console = true;
		OpenLog(L"NvrDlssHelper-probe.log");
		Log("NvrDlssHelper probe (%s)",
#ifndef NVR_DLSS_NO_SDK
			"with NVIDIA DLSS (NGX)"
#else
			"built without NVIDIA's SDK"
#endif
		);
		Helper h;
		// UNOFFICIAL 2026-10-30: with Ray Reconstruction's extension needs too (logged), so its line below sees the same device.
		if (!CreateVulkanDevice(h.vk, nullptr, false, nullptr, true, true)) { Log("probe: no usable NVIDIA Vulkan device"); return 1; }
		Log("probe: %s, Vulkan %u.%u, external memory + semaphores (Win32): yes, timeline semaphores: yes", h.vk.properties.deviceName,
			VK_VERSION_MAJOR(h.vk.properties.apiVersion), VK_VERSION_MINOR(h.vk.properties.apiVersion));
		{
			// Vulkan allows one NT-handle export per semaphore; what this driver does with a second one (the 2026-10-27 bug: DXVK
			// exported once and closed it, NVR's export got a dead handle).
			VkExportSemaphoreCreateInfo exportInfo = { VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO };
			exportInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
			VkSemaphoreTypeCreateInfo type = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO, &exportInfo };
			type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
			VkSemaphoreCreateInfo info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type };
			VkSemaphore sem = VK_NULL_HANDLE;
			if (vkCreateSemaphore(h.vk.device, &info, nullptr, &sem) == VK_SUCCESS) {
				VkSemaphoreGetWin32HandleInfoKHR get = { VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR };
				get.semaphore = sem;
				get.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
				HANDLE first = nullptr, second = nullptr, copy = nullptr;
				const VkResult r1 = h.vk.getSemaphoreWin32Handle(h.vk.device, &get, &first);
				CloseHandle(first);
				const VkResult r2 = h.vk.getSemaphoreWin32Handle(h.vk.device, &get, &second);
				const BOOL dup = DuplicateHandle(GetCurrentProcess(), second, GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS);
				const DWORD error = dup ? 0 : GetLastError();
				Log("probe: semaphore NT handle exported twice (first closed): %d / %d, handles %p / %p (%s), second %s (Win32 error %lu)",
					(int)r1, (int)r2, first, second, first == second ? "the SAME value" : "different", dup ? "usable" : "DEAD", error);
				if (dup) CloseHandle(copy);
				if (second != first) CloseHandle(second);
				vkDestroySemaphore(h.vk.device, sem, nullptr);
			}
			// The same for an image's dedicated memory.
			Exported image;
			if (ExportImage(h.vk, VK_FORMAT_R16G16B16A16_SFLOAT, 64, 64, image)) {
				VkMemoryGetWin32HandleInfoKHR get = { VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR };
				get.memory = image.memory;
				get.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
				HANDLE first = nullptr, second = nullptr, copy = nullptr;
				const VkResult r1 = h.vk.getMemoryWin32Handle(h.vk.device, &get, &first);
				CloseHandle(first);
				const VkResult r2 = h.vk.getMemoryWin32Handle(h.vk.device, &get, &second);
				const BOOL dup = DuplicateHandle(GetCurrentProcess(), second, GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS);
				const DWORD error = dup ? 0 : GetLastError();
				Log("probe: memory NT handle exported twice (first closed): %d / %d, handles %p / %p (%s), second %s (Win32 error %lu)",
					(int)r1, (int)r2, first, second, first == second ? "the SAME value" : "different", dup ? "usable" : "DEAD", error);
				if (dup) CloseHandle(copy);
				if (second != first) CloseHandle(second);
				vkDestroyImage(h.vk.device, image.image, nullptr);
				vkFreeMemory(h.vk.device, image.memory, nullptr);
			}
		}
		// UNOFFICIAL 2026-10-30 DLSS Ray Reconstruction: NVIDIA's runtime next to the helper, and NGX's own answer (only asked when
		// DLSS SR is available: RR needs it).
		const bool dlssdHere = FileNextToHelper(L"nvngx_dlssd.dll");
#ifndef NVR_DLSS_NO_SDK
		const bool dlss = InitNgx(h);
		Log("probe: DLSS %s", dlss ? "AVAILABLE" : "not available (see the line above)");
		std::string rrWhy = "DLSS is not available";
		const bool rr = dlss && RayReconstructionCapability(h, rrWhy);
		Log("probe: Ray Reconstruction (SuperSamplingDenoising.Available) %s%s%s; nvngx_dlssd.dll next to the helper: %s", rr ? "AVAILABLE" : "not available",
			rr ? "" : ": ", rr ? "" : rrWhy.c_str(), dlssdHere ? "yes" : "no");
		if (h.ngxInit) NVSDK_NGX_VULKAN_Shutdown1(h.vk.device);
#else
		Log("probe: Ray Reconstruction not built in; nvngx_dlssd.dll next to the helper: %s", dlssdHere ? "yes" : "no");
#endif
		DestroyVulkanDevice(h.vk);
		return 0;
	}

	// dlssMode: the child runs in DLSS mode instead. On a GPU without DLSS it must give up cleanly (FAILED + the NGX reason, the
	// fallback NVR relies on): that is a PASS there; on an RTX GPU it must get READY and run the frames.
	// UNOFFICIAL 2026-10-30 rrMode: protocol 2's RR exchange, as NVR's HelperTest with RayReconstruction makes it: eight images (the
	// guides cleared to known colours once), features RR, every frame an RR frame (identity matrices). The pass-through child must
	// report rrAvailable, count every frame in rrEvaluated and draw each guide's colour in its thumbnail. In DLSS mode (any of
	// them) the output is DLSS's, not the pass-through's: the frames must come back and be counted as DLSS SR or RR frames.
	int RunSelfTest(bool dlssMode, bool rrMode) {
		g_console = true;
		OpenLog(dlssMode ? (rrMode ? L"NvrDlssHelper-selftest-dlss-rr.log" : L"NvrDlssHelper-selftest-dlss.log")
			: (rrMode ? L"NvrDlssHelper-selftest-rr.log" : L"NvrDlssHelper-selftest.log"));
		Log("NvrDlssHelper self-test: this process plays NVR + the fork, a child copy of itself is the helper (%s%s)", dlssMode ? "DLSS mode" : "pass-through",
			rrMode ? ", Ray Reconstruction frames" : "");
		const uint32_t displayW = 1280, displayH = 720, renderW = 960, renderH = 540;	// RenderScale 0.75
		const uint32_t imageCount = rrMode ? NVR_DLSS_IMAGE_COUNT : NVR_DLSS_IMAGE_COUNT_SR;
		// The guides' colours (diffuse albedo, specular albedo, normals, roughness: R16F keeps only the red one).
		const float guideColours[kGuideCount][4] = { { 0.8f, 0.4f, 0.2f, 1.0f }, { 0.1f, 0.5f, 0.9f, 1.0f }, { 0.25f, 0.75f, 0.5f, 1.0f }, { 0.6f, 0.0f, 0.0f, 1.0f } };
		VulkanDevice vk;
		if (!CreateVulkanDevice(vk, nullptr, false, nullptr, false)) { Log("selftest FAILED: no NVIDIA Vulkan device"); return 1; }

		// Shared block + events, then the child helper.
		wchar_t base[128];
		swprintf_s(base, L"Local\\NvrDlssSelfTest-%lu-%llu", GetCurrentProcessId(), (unsigned long long)GetTickCount64());
		HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(NvrDlssShared), base);
		NvrDlssShared* shared = mapping ? (NvrDlssShared*)MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(NvrDlssShared)) : nullptr;
		if (!shared) { Log("selftest FAILED: no shared block"); return 1; }
		memset(shared, 0, sizeof(*shared));
		shared->magic = NVR_DLSS_MAGIC;
		shared->version = NVR_DLSS_PROTOCOL_VERSION;
		shared->size = sizeof(NvrDlssShared);
		shared->mode = dlssMode ? NVR_DLSS_MODE_DLSS : NVR_DLSS_MODE_PASSTHROUGH;
		shared->parentPid = GetCurrentProcessId();
		shared->vendorId = vk.properties.vendorID;
		memcpy(shared->deviceLuid, vk.ids.deviceLUID, 8);
		shared->deviceLuidValid = vk.ids.deviceLUIDValid;
		memcpy(shared->deviceUuid, vk.ids.deviceUUID, 16);
		memcpy(shared->driverUuid, vk.ids.driverUUID, 16);
		shared->imageCount = imageCount;	// UNOFFICIAL 2026-10-30 protocol 2
		shared->features = rrMode ? NVR_DLSS_FEATURE_RR : 0u;
		HANDLE frameEvent = CreateEventW(nullptr, FALSE, FALSE, (std::wstring(base) + NVR_DLSS_EVENT_FRAME_SUFFIX).c_str());
		HANDLE statusEvent = CreateEventW(nullptr, FALSE, FALSE, (std::wstring(base) + NVR_DLSS_EVENT_STATUS_SUFFIX).c_str());
		wchar_t exe[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, exe, MAX_PATH);
		std::wstring cmdLine = L"\"" + std::wstring(exe) + L"\" --shm " + base;
		STARTUPINFOW si = { sizeof(si) };
		PROCESS_INFORMATION pi = {};
		if (!CreateProcessW(exe, &cmdLine[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, g_exeDir.c_str(), &si, &pi)) {
			Log("selftest FAILED: could not start the child (%lu)", GetLastError());
			return 1;
		}
		CloseHandle(pi.hThread);

		// What the fork does: four exportable images (eight with RR), two exportable timeline semaphores, handles duplicated into the child.
		const VkFormat formats[NVR_DLSS_IMAGE_COUNT] = { VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R32_SFLOAT, VK_FORMAT_R16G16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT,
			VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16_SFLOAT };
		Exported images[NVR_DLSS_IMAGE_COUNT];
		VkSemaphore inputsReady = VK_NULL_HANDLE, outputReady = VK_NULL_HANDLE;
		bool ok = true;
		for (uint32_t i = 0; i < imageCount && ok; i++) {
			const bool display = i == NVR_DLSS_IMAGE_OUTPUT;
			ok = ExportImage(vk, formats[i], displayW, displayH, images[i]) && ExportHandle(vk, images[i].memory, pi.hProcess, images[i].desc.handle);
			shared->images[i] = images[i].desc;
			(void)display;
		}
		ok = ok && ExportSemaphore(vk, inputsReady, pi.hProcess, shared->semInputsReady) && ExportSemaphore(vk, outputReady, pi.hProcess, shared->semOutputReady);
		if (!ok) { Log("selftest FAILED: export"); TerminateProcess(pi.hProcess, 1); return 1; }
		shared->displayWidth = displayW;
		shared->displayHeight = displayH;
		shared->inputWidth = displayW;
		shared->inputHeight = displayH;
		MemoryBarrier();
		shared->setupDone = 1;
		SetEvent(frameEvent);

		// Wait for READY (or FAILED).
		const ULONGLONG start = GetTickCount64();
		while (shared->state == NVR_DLSS_STATE_STARTING && GetTickCount64() - start < 15000) {
			HANDLE waits[2] = { statusEvent, pi.hProcess };
			if (WaitForMultipleObjects(2, waits, FALSE, 200) == WAIT_OBJECT_0 + 1) break;
		}
		Log("selftest: helper state %u after %llu ms: %s", shared->state, (unsigned long long)(GetTickCount64() - start), shared->message);
		if (dlssMode && shared->state == NVR_DLSS_STATE_FAILED && strstr((const char*)shared->message, "not available")) {
			WaitForSingleObject(pi.hProcess, 3000);
			Log("selftest PASSED (DLSS mode on a GPU without DLSS: the helper gave up cleanly, NVR would run FSR 2): %s", shared->message);
			return 0;
		}
		if (shared->state != NVR_DLSS_STATE_READY) { Log("selftest FAILED: the helper did not get ready"); TerminateProcess(pi.hProcess, 1); return 1; }

		// Exporter-side command buffers: clear the colour input to a per-frame colour, signal N (the helper works), wait N, read back.
		VkCommandPool pool = VK_NULL_HANDLE;
		VkCommandPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		poolInfo.queueFamilyIndex = vk.queueFamily;
		vkCreateCommandPool(vk.device, &poolInfo, nullptr, &pool);
		VkCommandBuffer cmds[2] = {};
		VkCommandBufferAllocateInfo cmdInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		cmdInfo.commandPool = pool;
		cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cmdInfo.commandBufferCount = 2;
		vkAllocateCommandBuffers(vk.device, &cmdInfo, cmds);
		const VkDeviceSize readBytes = (VkDeviceSize)displayW * displayH * 8;
		VkBuffer readback = VK_NULL_HANDLE;
		VkDeviceMemory readMemory = VK_NULL_HANDLE;
		{
			VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
			info.size = readBytes;
			info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			vkCreateBuffer(vk.device, &info, nullptr, &readback);
			VkMemoryRequirements req = {};
			vkGetBufferMemoryRequirements(vk.device, readback, &req);
			VkMemoryAllocateInfo alloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
			alloc.allocationSize = req.size;
			alloc.memoryTypeIndex = (uint32_t)FindMemoryType(vk, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			vkAllocateMemory(vk.device, &alloc, nullptr, &readMemory);
			vkBindBufferMemory(vk.device, readback, readMemory, 0);
		}
		// Initialise every image to GENERAL once (as the fork's initImage does).
		{
			VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
			vkBeginCommandBuffer(cmds[0], &begin);
			for (uint32_t i = 0; i < imageCount; i++)
				Barrier(cmds[0], images[i].image, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
					VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
			// UNOFFICIAL 2026-10-30 the RR guides: one known colour each, for the whole run.
			for (uint32_t g = 0; rrMode && g < kGuideCount; g++) {
				const VkImage guide = images[NVR_DLSS_IMAGE_DIFFUSE_ALBEDO + g].image;
				VkClearColorValue colour = {};
				for (int c = 0; c < 4; c++) colour.float32[c] = guideColours[g][c];
				const VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
				vkCmdClearColorImage(cmds[0], guide, VK_IMAGE_LAYOUT_GENERAL, &colour, 1, &range);
				Barrier(cmds[0], guide, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
					VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
			}
			vkEndCommandBuffer(cmds[0]);
			VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
			submit.commandBufferCount = 1;
			submit.pCommandBuffers = &cmds[0];
			vkQueueSubmit(vk.queue, 1, &submit, VK_NULL_HANDLE);
			vkQueueWaitIdle(vk.queue);
		}

		LARGE_INTEGER frequency;
		QueryPerformanceFrequency(&frequency);
		const uint64_t frames = 300;
		uint32_t checked = 0, failures = 0;
		double roundTripMs = 0.0, worstMs = 0.0;
		for (uint64_t n = 1; n <= frames && ok; n++) {
			// 1. inputs (the left half of the render rectangle red-ish, the right half blue-ish, brightness by frame), signal N
			const float level = 0.25f + 0.5f * (float)(n % 4) / 3.0f;
			VkCommandBuffer cmd = cmds[0];
			vkResetCommandBuffer(cmd, 0);
			VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
			vkBeginCommandBuffer(cmd, &begin);
			VkClearColorValue color = {};
			color.float32[0] = level; color.float32[1] = 0.0f; color.float32[2] = 1.0f - level; color.float32[3] = 1.0f;
			const VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			vkCmdClearColorImage(cmd, images[NVR_DLSS_IMAGE_COLOR].image, VK_IMAGE_LAYOUT_GENERAL, &color, 1, &range);
			Barrier(cmd, images[NVR_DLSS_IMAGE_COLOR].image, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
				VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
			vkEndCommandBuffer(cmd);
			NvrDlssFrame& f = shared->frames[n % NVR_DLSS_FRAME_RING];
			f = NvrDlssFrame();
			f.frame = n;
			f.renderWidth = renderW;
			f.renderHeight = renderH;
			f.reset = n == 1;
			f.mvScaleX = (float)renderW;
			f.mvScaleY = (float)renderH;
			f.preExposure = 1.0f;
			f.frameTimeMs = 16.7f;
			if (rrMode) {
				// UNOFFICIAL 2026-10-30 an RR frame as NVR sends one: linear HDR colour, the default presets, identity matrices.
				f.flags = NVR_DLSS_FRAME_RR;
				f.hdr = 1;
				for (int i = 0; i < 4; i++) f.worldToView[i * 5] = f.viewToClip[i * 5] = 1.0f;
			}
			LARGE_INTEGER written;
			QueryPerformanceCounter(&written);
			f.writtenQpc = written.QuadPart;
			MemoryBarrier();
			InterlockedExchange64((volatile LONG64*)&shared->latestFrame, (LONG64)n);
			SetEvent(frameEvent);
			const uint64_t value = n;
			VkTimelineSemaphoreSubmitInfo timeline = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
			timeline.signalSemaphoreValueCount = 1;
			timeline.pSignalSemaphoreValues = &value;
			VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO, &timeline };
			submit.commandBufferCount = 1;
			submit.pCommandBuffers = &cmd;
			submit.signalSemaphoreCount = 1;
			submit.pSignalSemaphores = &inputsReady;
			vkQueueSubmit(vk.queue, 1, &submit, VK_NULL_HANDLE);

			// 2. GPU wait for the helper's N, then (every 50th frame) copy the output back and look at it.
			const bool check = (n % 50) == 0 || n == frames;
			VkCommandBuffer cmd2 = cmds[1];
			vkResetCommandBuffer(cmd2, 0);
			vkBeginCommandBuffer(cmd2, &begin);
			if (check) {
				VkBufferImageCopy copy = {};
				copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
				copy.imageExtent = { displayW, displayH, 1 };
				vkCmdCopyImageToBuffer(cmd2, images[NVR_DLSS_IMAGE_OUTPUT].image, VK_IMAGE_LAYOUT_GENERAL, readback, 1, &copy);
			}
			vkEndCommandBuffer(cmd2);
			const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			VkTimelineSemaphoreSubmitInfo timeline2 = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
			timeline2.waitSemaphoreValueCount = 1;
			timeline2.pWaitSemaphoreValues = &value;
			VkSubmitInfo submit2 = { VK_STRUCTURE_TYPE_SUBMIT_INFO, &timeline2 };
			submit2.waitSemaphoreCount = 1;
			submit2.pWaitSemaphores = &outputReady;
			submit2.pWaitDstStageMask = &stage;
			submit2.commandBufferCount = 1;
			submit2.pCommandBuffers = &cmd2;
			vkQueueSubmit(vk.queue, 1, &submit2, VK_NULL_HANDLE);
			// The test's own CPU wait (NVR never does this): the round trip, and the command buffers can be reused.
			VkSemaphoreWaitInfo wait = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
			wait.semaphoreCount = 1;
			wait.pSemaphores = &outputReady;
			wait.pValues = &value;
			if (vkWaitSemaphores(vk.device, &wait, 5ull * 1000 * 1000 * 1000) != VK_SUCCESS) {
				Log("selftest FAILED: frame %llu did not come back within 5 s (helper: %s)", (unsigned long long)n, shared->message);
				ok = false;
				break;
			}
			vkQueueWaitIdle(vk.queue);
			LARGE_INTEGER done;
			QueryPerformanceCounter(&done);
			const double ms = (double)(done.QuadPart - written.QuadPart) * 1000.0 / (double)frequency.QuadPart;
			roundTripMs += ms;
			worstMs = std::max(worstMs, ms);
			if (check && dlssMode) {
				// UNOFFICIAL 2026-10-30 DLSS mode: the output is DLSS's own picture, not the pass-through's pattern: the frame came back,
				// and the counters below say which feature ran it.
				checked++;
				Log("selftest frame %llu: back (DLSS SR %u, Ray Reconstruction %u so far); round trip %.3f ms", (unsigned long long)n,
					shared->dlssEvaluated, shared->rrEvaluated, ms);
			}
			else if (check) {
				checked++;
				const uint16_t* px = nullptr;
				vkMapMemory(vk.device, readMemory, 0, readBytes, 0, (void**)&px);
				auto at = [&](uint32_t x, uint32_t y, int c) { return HalfToFloat(px[((size_t)y * displayW + x) * 4 + c]); };
				if (rrMode) {
					// UNOFFICIAL 2026-10-30 each guide's thumbnail centre shows its colour (the pass-through RR drawing, GuideThumbnail).
					for (uint32_t g = 0; g < kGuideCount; g++) {
						VkOffset2D tAt = {};
						VkExtent2D tSize = {};
						bool good = GuideThumbnail(displayW, displayH, renderW, renderH, g, tAt, tSize);
						float c[3] = {};
						if (good) {
							for (int k = 0; k < 3; k++) {
								c[k] = at((uint32_t)tAt.x + tSize.width / 2, (uint32_t)tAt.y + tSize.height / 2, k);
								good = good && fabsf(c[k] - guideColours[g][k]) < 0.01f;
							}
						}
						if (!good) failures++;
						Log("selftest frame %llu: %s thumbnail (%.3f, %.3f, %.3f) want (%.3f, %.3f, %.3f) %s", (unsigned long long)n,
							kImageNames[NVR_DLSS_IMAGE_DIFFUSE_ALBEDO + g], c[0], c[1], c[2], guideColours[g][0], guideColours[g][1], guideColours[g][2],
							good ? "ok" : "WRONG");
					}
				}
				// the stretched input (uniform colour) in the middle, the moving magenta box at frame % 8, the green box
				const float r = at(displayW / 2, displayH / 2, 0), b = at(displayW / 2, displayH / 2, 2);
				const uint32_t mx = 16 + 32 * (uint32_t)(n % 8) + 12, other = 16 + 32 * (uint32_t)((n + 4) % 8) + 12;
				const bool picture = fabsf(r - level) < 0.01f && fabsf(b - (1.0f - level)) < 0.01f;
				const bool marker = at(mx, 28, 0) > 3.5f && at(mx, 28, 1) < 0.5f && at(16 + 32 * 8 + 12, 28, 1) > 3.5f;
				const bool notStale = !(at(other, 28, 0) > 3.5f && at(other, 28, 1) < 0.5f);
				if (!picture || !marker || !notStale) failures++;
				Log("selftest frame %llu: centre (%.3f, %.3f) want (%.3f, %.3f) %s; marker %s; old marker gone %s; round trip %.3f ms",
					(unsigned long long)n, r, b, level, 1.0f - level, picture ? "ok" : "WRONG", marker ? "ok" : "WRONG", notStale ? "ok" : "WRONG", ms);
				vkUnmapMemory(vk.device, readMemory);
			}
		}
		shared->quit = 1;
		SetEvent(frameEvent);
		WaitForSingleObject(pi.hProcess, 3000);
		Log("selftest: helper submitted %llu frames, its GPU time %.3f ms avg, its response %.3f ms avg (max %.3f), %u overwritten; "
			"test round trip (CPU write -> output back, with the test's own waits) %.3f ms avg, %.3f max",
			(unsigned long long)shared->framesSubmitted, shared->gpuTimeUsAvg / 1000.0, shared->responseUsAvg / 1000.0, shared->responseUsMax / 1000.0,
			shared->paramMismatches, roundTripMs / (double)frames, worstMs);
		bool passed = ok && checked > 0 && failures == 0 && shared->framesSubmitted == frames;
		// UNOFFICIAL 2026-10-30 the counters: in DLSS mode every frame ran DLSS SR or RR (RR not available on this PC: all SR, which
		// is NVR's fallback); the pass-through RR test evaluated every frame as an RR frame and said so.
		if (dlssMode) {
			const bool counted = shared->dlssEvaluated + shared->rrEvaluated == frames;
			Log("selftest: DLSS SR ran %u frames, Ray Reconstruction %u (rrAvailable %u, last RR result 0x%08X)%s", shared->dlssEvaluated,
				shared->rrEvaluated, shared->rrAvailable, (unsigned)shared->rrResult, counted ? "" : ": NOT every frame");
			passed = passed && counted;
		}
		else if (rrMode) {
			const bool counted = shared->rrAvailable == 1 && shared->rrEvaluated == frames;
			Log("selftest: rrAvailable %u, %u RR frames evaluated (want 1, %llu)%s", shared->rrAvailable, shared->rrEvaluated, (unsigned long long)frames,
				counted ? "" : ": WRONG");
			passed = passed && counted;
		}
		Log("selftest %s (%u frames checked, %u wrong)", passed ? "PASSED" : "FAILED", checked, failures);
		vkDeviceWaitIdle(vk.device);
		return passed ? 0 : 1;
	}
}

int wmain(int argc, wchar_t** argv) {
	InitializeCriticalSection(&g_logLock);
	g_exeDir = ExeDir();
	for (int i = 1; i < argc; i++) {
		if (!wcscmp(argv[i], L"--shm") && i + 1 < argc) return RunHelper(argv[i + 1]);
		if (!wcscmp(argv[i], L"--probe")) return RunProbe();
		if (!wcscmp(argv[i], L"--selftest")) {
			// UNOFFICIAL 2026-10-30: "dlss" and "rr" after it, in either order (--selftest rr, --selftest dlss rr).
			bool dlss = false, rr = false;
			for (int j = i + 1; j < argc; j++) {
				if (!wcscmp(argv[j], L"dlss")) dlss = true;
				else if (!wcscmp(argv[j], L"rr")) rr = true;
			}
			return RunSelfTest(dlss, rr);
		}
	}
	printf("NvrDlssHelper: started by New Vegas Reloaded. By hand: --probe (is DLSS available here?) or --selftest [dlss] [rr] (the exchange without the game).\n");
	return 1;
}
