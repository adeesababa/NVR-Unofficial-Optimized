
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
#include <string>
#include <vector>

#include "NvrDlssProtocol.h"

namespace {


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
		if (exponent == 0) f = sign;
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
	const uint32_t kMarkerSize = 24;


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
		bool				ngxExtensions = false;
		PFN_vkGetMemoryWin32HandleKHR		getMemoryWin32Handle = nullptr;
		PFN_vkGetSemaphoreWin32HandleKHR	getSemaphoreWin32Handle = nullptr;
		PFN_vkImportSemaphoreWin32HandleKHR	importSemaphoreWin32Handle = nullptr;
	};

#ifndef NVR_DLSS_NO_SDK
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

	NVSDK_NGX_FeatureDiscoveryInfo NgxDiscoveryInfo() {
		if (g_ngxDataPath.empty()) {
			wchar_t temp[MAX_PATH] = {};
			GetTempPathW(MAX_PATH, temp);
			g_ngxDataPath = std::wstring(temp) + L"NvrDlssHelper";
			CreateDirectoryW(g_ngxDataPath.c_str(), nullptr);
			g_ngxPaths[0] = g_exeDir.c_str();
			g_ngxFeatureInfo.PathListInfo.Path = g_ngxPaths;
			g_ngxFeatureInfo.PathListInfo.Length = 1;
			g_ngxFeatureInfo.LoggingInfo.LoggingCallback = NgxLog;
			g_ngxFeatureInfo.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_ON;
			g_ngxFeatureInfo.LoggingInfo.DisableOtherLoggingSinks = false;
		}
		NVSDK_NGX_FeatureDiscoveryInfo info = {};
		info.SDKVersion = NVSDK_NGX_Version_API;
		info.FeatureID = NVSDK_NGX_Feature_SuperSampling;
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

	bool CreateVulkanDevice(VulkanDevice& vk, const uint8_t* luid, bool luidValid, const uint8_t* uuid, bool wantNgx) {
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
				if (HasExtension(instanceExts, ngxExts[i].extensionName)) AddUnique(enabledInstance, ngxExts[i].extensionName);
				else Log("NGX wants the instance extension %s, which the loader does not have", ngxExts[i].extensionName);
			}
		}
#else
		(void)wantNgx;
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
					if (HasExtension(deviceExts, ngxExts[i].extensionName)) AddUnique(enabled, ngxExts[i].extensionName);
					else { vk.ngxExtensions = false; Log("NGX wants the device extension %s, which the driver does not have", ngxExts[i].extensionName); }
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
		features12.bufferDeviceAddress = supported12.bufferDeviceAddress;
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
		CloseHandle((HANDLE)(uintptr_t)d.handle);
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
#ifndef NVR_DLSS_NO_SDK
		bool			ngxInit = false;
		bool			dlssAvailable = false;
		NVSDK_NGX_Parameter* ngxParams = nullptr;
		NVSDK_NGX_Handle* dlss = nullptr;
		uint32_t		dlssKey[6] = {};
		bool			dlssFailed = false;
#endif
	};

	bool CreateMarker(Helper& h) {
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
	bool InitNgx(Helper& h) {
		NVSDK_NGX_FeatureDiscoveryInfo discovery = NgxDiscoveryInfo();
		NVSDK_NGX_FeatureRequirement requirement = {};
		NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_GetFeatureRequirements(h.vk.instance, h.vk.physical, &discovery, &requirement);
		if (g_shared) g_shared->ngxResult = (int32_t)r;
		if (r == NVSDK_NGX_Result_FAIL_FeatureNotSupported) {
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
		return true;
	}

	NVSDK_NGX_PerfQuality_Value QualityFor(uint32_t renderWidth, uint32_t displayWidth) {
		const float r = displayWidth ? (float)renderWidth / (float)displayWidth : 1.0f;
		if (r >= 0.99f) return NVSDK_NGX_PerfQuality_Value_DLAA;
		if (r >= 0.66f) return NVSDK_NGX_PerfQuality_Value_MaxQuality;
		if (r >= 0.57f) return NVSDK_NGX_PerfQuality_Value_Balanced;
		if (r >= 0.49f) return NVSDK_NGX_PerfQuality_Value_MaxPerf;
		return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
	}

	bool EnsureDlss(Helper& h, VkCommandBuffer cmd, const NvrDlssFrame& f) {
		const SharedImage& out = h.images[NVR_DLSS_IMAGE_OUTPUT];
		const uint32_t quality = (uint32_t)QualityFor(f.renderWidth, out.width);
		const uint32_t key[6] = { f.renderWidth, f.renderHeight, out.width, out.height, f.hdr, quality };
		if (h.dlss && !memcmp(key, h.dlssKey, sizeof(key))) return true;
		if (h.dlss) {
			vkDeviceWaitIdle(h.vk.device);
			NVSDK_NGX_VULKAN_ReleaseFeature(h.dlss);
			h.dlss = nullptr;
		}
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
			Status("NGX: DLSS feature creation failed (%ux%u -> %ux%u, %s, quality mode %u): %s", f.renderWidth, f.renderHeight, out.width, out.height,
				f.hdr ? "HDR" : "LDR", quality, NgxResultString(r).c_str());
			h.dlss = nullptr;
			return false;
		}
		memcpy(h.dlssKey, key, sizeof(key));
		Log("NGX: DLSS feature made: %ux%u -> %ux%u, %s, quality mode %u", f.renderWidth, f.renderHeight, out.width, out.height, f.hdr ? "HDR" : "LDR", quality);
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

#ifndef NVR_DLSS_NO_SDK
	bool RecordDlss(Helper& h, VkCommandBuffer cmd, const NvrDlssFrame& f) {
		if (!EnsureDlss(h, cmd, f)) return false;
		const VkImageLayout read = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		for (uint32_t i = 0; i < NVR_DLSS_IMAGE_OUTPUT; i++)
			Barrier(cmd, h.images[i].image, VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, read,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		Barrier(cmd, h.images[NVR_DLSS_IMAGE_OUTPUT].image, 0, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

		const VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		NVSDK_NGX_Resource_VK res[NVR_DLSS_IMAGE_COUNT];
		for (uint32_t i = 0; i < NVR_DLSS_IMAGE_COUNT; i++) {
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

		for (uint32_t i = 0; i < NVR_DLSS_IMAGE_OUTPUT; i++)
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

	bool ProcessFrame(Helper& h, uint64_t frame, HANDLE parent) {
		const NvrDlssFrame f = g_shared->frames[frame % NVR_DLSS_FRAME_RING];
		NvrDlssFrame use = f;
		if (f.frame != frame) {
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
#ifndef NVR_DLSS_NO_SDK
		if (h.mode == NVR_DLSS_MODE_DLSS && h.dlssAvailable && !h.dlssFailed && f.frame == frame) {
			dlssRan = RecordDlss(h, cmd, use);
			if (!dlssRan) {
				h.dlssFailed = true;
				SetState(NVR_DLSS_STATE_FAILED);
			}
		}
#endif
		if (!dlssRan) RecordPassThrough(h, cmd, use, frame);
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
		g_shared->framesSubmitted++;
		MemoryBarrier();
		g_shared->submittedFrame = (int64_t)frame;
		if (!h.loggedFirst) {
			h.loggedFirst = true;
			Log("first frame %llu submitted (%s, render %ux%u -> %ux%u, %s)", (unsigned long long)frame, dlssRan ? "DLSS" : "pass-through",
				use.renderWidth, use.renderHeight, h.images[NVR_DLSS_IMAGE_OUTPUT].width, h.images[NVR_DLSS_IMAGE_OUTPUT].height, use.hdr ? "HDR" : "LDR");
		}
		if ((frame % 600) == 0)
			Log("frame %llu: %llu submitted, helper GPU %.3f ms avg, response %.3f ms avg (max %.3f), %u overwritten, %u DLSS",
				(unsigned long long)frame, (unsigned long long)g_shared->framesSubmitted, g_shared->gpuTimeUsAvg / 1000.0,
				g_shared->responseUsAvg / 1000.0, g_shared->responseUsMax / 1000.0, g_shared->paramMismatches, g_shared->dlssEvaluated);
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
		if (!CreateVulkanDevice(h.vk, shared->deviceLuid, shared->deviceLuidValid != 0, shared->deviceUuid, true)) return fail(nullptr);
		if (memcmp(h.vk.ids.driverUUID, shared->driverUuid, VK_UUID_SIZE))
			return fail("the helper's Vulkan driver is not the game's (driver UUIDs differ): images cannot be shared");
		const char* names[NVR_DLSS_IMAGE_COUNT] = { "colour", "depth", "motion", "output" };
		for (uint32_t i = 0; i < NVR_DLSS_IMAGE_COUNT; i++)
			if (!ImportImage(h.vk, shared->images[i], names[i], h.images[i])) return fail(nullptr);
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
		if (h.mode == NVR_DLSS_MODE_DLSS && !h.dlssAvailable) return fail(nullptr);
#else
		shared->ngxAvailable = 0;
		if (h.mode == NVR_DLSS_MODE_DLSS) return fail("NGX: this helper was built without NVIDIA's DLSS SDK (pass-through test only)");
		Status("NGX: not built in (pass-through test only)");
#endif
		Status("ready: %s on %s (%ux%u -> %ux%u)", h.mode == NVR_DLSS_MODE_DLSS ? "DLSS" : "pass-through test", h.vk.properties.deviceName,
			shared->inputWidth, shared->inputHeight, shared->displayWidth, shared->displayHeight);
		SetState(NVR_DLSS_STATE_READY);

		uint64_t next = 1;
		bool parentGone = false;
		for (;;) {
			HANDLE waits[2] = { frameEvent, parent };
			const DWORD w = WaitForMultipleObjects(2, waits, FALSE, 250);
			if (w == WAIT_OBJECT_0 + 1) { Log("the game exited"); parentGone = true; break; }
			if (shared->quit) {
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
		if (g_log) fclose(g_log);
		ExitProcess(0);
	}


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
		if (!CreateVulkanDevice(h.vk, nullptr, false, nullptr, true)) { Log("probe: no usable NVIDIA Vulkan device"); return 1; }
		Log("probe: %s, Vulkan %u.%u, external memory + semaphores (Win32): yes, timeline semaphores: yes", h.vk.properties.deviceName,
			VK_VERSION_MAJOR(h.vk.properties.apiVersion), VK_VERSION_MINOR(h.vk.properties.apiVersion));
		{
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
#ifndef NVR_DLSS_NO_SDK
		const bool dlss = InitNgx(h);
		Log("probe: DLSS %s", dlss ? "AVAILABLE" : "not available (see the line above)");
		if (h.ngxInit) NVSDK_NGX_VULKAN_Shutdown1(h.vk.device);
#endif
		DestroyVulkanDevice(h.vk);
		return 0;
	}

	int RunSelfTest(bool dlssMode) {
		g_console = true;
		OpenLog(dlssMode ? L"NvrDlssHelper-selftest-dlss.log" : L"NvrDlssHelper-selftest.log");
		Log("NvrDlssHelper self-test: this process plays NVR + the fork, a child copy of itself is the helper (%s)", dlssMode ? "DLSS mode" : "pass-through");
		const uint32_t displayW = 1280, displayH = 720, renderW = 960, renderH = 540;
		VulkanDevice vk;
		if (!CreateVulkanDevice(vk, nullptr, false, nullptr, false)) { Log("selftest FAILED: no NVIDIA Vulkan device"); return 1; }

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

		const VkFormat formats[NVR_DLSS_IMAGE_COUNT] = { VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R32_SFLOAT, VK_FORMAT_R16G16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT };
		Exported images[NVR_DLSS_IMAGE_COUNT];
		VkSemaphore inputsReady = VK_NULL_HANDLE, outputReady = VK_NULL_HANDLE;
		bool ok = true;
		for (uint32_t i = 0; i < NVR_DLSS_IMAGE_COUNT && ok; i++) {
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
		{
			VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
			vkBeginCommandBuffer(cmds[0], &begin);
			for (Exported& e : images)
				Barrier(cmds[0], e.image, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
					VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
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
			if (check) {
				checked++;
				const uint16_t* px = nullptr;
				vkMapMemory(vk.device, readMemory, 0, readBytes, 0, (void**)&px);
				auto at = [&](uint32_t x, uint32_t y, int c) { return HalfToFloat(px[((size_t)y * displayW + x) * 4 + c]); };
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
		const bool passed = ok && checked > 0 && failures == 0 && shared->framesSubmitted == frames;
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
		if (!wcscmp(argv[i], L"--selftest")) return RunSelfTest(i + 1 < argc && !wcscmp(argv[i + 1], L"dlss"));
	}
	printf("NvrDlssHelper: started by New Vegas Reloaded. By hand: --probe (is DLSS available here?) or --selftest (the exchange without the game).\n");
	return 1;
}
