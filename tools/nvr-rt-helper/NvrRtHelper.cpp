// NvrRtHelper.exe -- the 64-bit helper that owns New Vegas Reloaded's ray tracing scene. MIT licence (LICENSE here).
//
// Why a separate process: Fallout New Vegas, NVR and the NVR DXVK d3d9.dll are 32-bit, and Windows' 32-bit Vulkan drivers
// offer no ray tracing (AMD 25.10 / NVIDIA 582 measured 2026-10-28). This helper makes its own Vulkan 1.2 device on the GPU
// the game uses (matched by LUID), imports the images and the two timeline semaphores the fork exports (patch 0026), keeps
// the bottom-level structures of every geometry NVR sends through the arena image (fork patch 0028, StageGeometry), rebuilds
// the scene (top level) from NVR's instance list every frame and runs NVR's trace passes (compute shaders with
// GL_EXT_ray_query) between a GPU wait for "inputs ready" = N and a signal of "output ready" = N. Protocol: NvrRtProtocol.h;
// design and the pass rules: docs/next/FORK-RT-CORE.md in the NVR repository.
//
// Usage:
//   NvrRtHelper.exe --shm <name>   started by NVR (never by hand)
//   NvrRtHelper.exe --probe        prints every GPU and whether it can run the helper, then exits
//
// The pass binding layout (set 0), the same for every trace shader:
//   binding 0      accelerationStructureEXT     the scene
//   binding 1      uniform block (std140)        the pass's constants (NvrRtPass::constants, up to 512 bytes)
//   binding 2 + i  image2D (storage, GENERAL)    the pass's images[i] (declare the format, e.g. r32f, rgba16f, r16f)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "NvrRtProtocol.h"

namespace {

	// ---------------------------------------------------------------------------------------------------------------------
	// Log: NvrRtHelper.log next to the exe (or in the temp folder), plus the last status line into the shared block.

	CRITICAL_SECTION	g_logLock;
	FILE*				g_log = nullptr;
	NvrRtShared*		g_shared = nullptr;
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

	void SetState(NvrRtState state) {
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
			case VK_ERROR_INVALID_EXTERNAL_HANDLE: return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
			case VK_ERROR_INVALID_SHADER_NV: return "VK_ERROR_INVALID_SHADER_NV";
			default: return "VkResult(other)";
		}
	}

#define VK_CHECK(call, what) do { VkResult r_ = (call); if (r_ != VK_SUCCESS) { Status("Vulkan: %s failed: %s (%d)", what, VkResultName(r_), (int)r_); return false; } } while (0)

	bool HasExtension(const std::vector<VkExtensionProperties>& list, const char* name) {
		for (const VkExtensionProperties& e : list) if (!strcmp(e.extensionName, name)) return true;
		return false;
	}

	VkDeviceSize AlignUp(VkDeviceSize v, VkDeviceSize a) { return (v + a - 1) / a * a; }

	const uint32_t kCmdRing = 4;
	const uint32_t kQueriesPerSlot = 4;	// start, after the builds, after the scene, end
	const VkDeviceSize kPoolBlock = 64ull << 20;

	// ---------------------------------------------------------------------------------------------------------------------
	// The device: the GPU NVR's DXVK uses, with ray queries, acceleration structures, buffer addresses, timeline semaphores and
	// Win32 external memory / semaphores.

	struct VulkanDevice {
		VkInstance			instance = VK_NULL_HANDLE;
		VkPhysicalDevice	physical = VK_NULL_HANDLE;
		VkDevice			device = VK_NULL_HANDLE;
		uint32_t			queueFamily = 0;
		VkQueue				queue = VK_NULL_HANDLE;
		VkPhysicalDeviceProperties	properties = {};
		VkPhysicalDeviceIDProperties ids = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
		VkPhysicalDeviceAccelerationStructurePropertiesKHR asProperties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
		VkPhysicalDeviceMemoryProperties memory = {};
		uint32_t			timestampBits = 0;
		PFN_vkImportSemaphoreWin32HandleKHR			importSemaphoreWin32Handle = nullptr;
		PFN_vkCreateAccelerationStructureKHR		createAs = nullptr;
		PFN_vkDestroyAccelerationStructureKHR		destroyAs = nullptr;
		PFN_vkGetAccelerationStructureBuildSizesKHR	buildSizes = nullptr;
		PFN_vkGetAccelerationStructureDeviceAddressKHR asAddress = nullptr;
		PFN_vkCmdBuildAccelerationStructuresKHR		cmdBuildAs = nullptr;
	};

	// Whether a physical device can run the helper; why not in `why`.
	bool DeviceUsable(VkPhysicalDevice d, std::string& why) {
		uint32_t count = 0;
		vkEnumerateDeviceExtensionProperties(d, nullptr, &count, nullptr);
		std::vector<VkExtensionProperties> exts(count);
		vkEnumerateDeviceExtensionProperties(d, nullptr, &count, exts.data());
		const char* required[] = { VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_RAY_QUERY_EXTENSION_NAME,
			VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME, VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME };
		for (const char* name : required)
			if (!HasExtension(exts, name)) { why = std::string("no ") + name; return false; }
		VkPhysicalDeviceRayQueryFeaturesKHR rq = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
		VkPhysicalDeviceAccelerationStructureFeaturesKHR as = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &rq };
		VkPhysicalDeviceVulkan12Features f12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &as };
		VkPhysicalDeviceFeatures2 f = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f12 };
		vkGetPhysicalDeviceFeatures2(d, &f);
		if (!rq.rayQuery) { why = "rayQuery feature off"; return false; }
		if (!as.accelerationStructure) { why = "accelerationStructure feature off"; return false; }
		if (!f12.bufferDeviceAddress) { why = "no bufferDeviceAddress"; return false; }
		if (!f12.timelineSemaphore) { why = "no timeline semaphores"; return false; }
		return true;
	}

	// luid / uuid: the game's GPU; null: the first usable one (probe).
	bool CreateVulkanDevice(VulkanDevice& vk, const uint8_t* luid, bool luidValid, const uint8_t* uuid) {
		uint32_t loaderVersion = VK_API_VERSION_1_0;
		PFN_vkEnumerateInstanceVersion enumerateVersion = (PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion");
		if (enumerateVersion) enumerateVersion(&loaderVersion);
		if (loaderVersion < VK_API_VERSION_1_2) { Status("Vulkan: the loader is older than 1.2"); return false; }

		VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
		app.pApplicationName = "NvrRtHelper";
		app.applicationVersion = 1;
		app.pEngineName = "New Vegas Reloaded";
		app.engineVersion = 1;
		app.apiVersion = VK_API_VERSION_1_2;
		VkInstanceCreateInfo instanceInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
		instanceInfo.pApplicationInfo = &app;
		VK_CHECK(vkCreateInstance(&instanceInfo, nullptr, &vk.instance), "vkCreateInstance");

		uint32_t count = 0;
		vkEnumeratePhysicalDevices(vk.instance, &count, nullptr);
		std::vector<VkPhysicalDevice> devices(count);
		vkEnumeratePhysicalDevices(vk.instance, &count, devices.data());
		std::string whyNot;
		for (VkPhysicalDevice d : devices) {
			VkPhysicalDeviceIDProperties ids = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
			VkPhysicalDeviceProperties2 props = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &ids };
			vkGetPhysicalDeviceProperties2(d, &props);
			std::string why;
			const bool usable = DeviceUsable(d, why);
			bool match;
			if (luid && luidValid && ids.deviceLUIDValid) match = !memcmp(ids.deviceLUID, luid, VK_LUID_SIZE);
			else if (uuid) match = !memcmp(ids.deviceUUID, uuid, VK_UUID_SIZE);
			else match = usable;
			Log("Vulkan device: %s (vendor 0x%04X, API %u.%u.%u): %s%s", props.properties.deviceName, props.properties.vendorID,
				VK_VERSION_MAJOR(props.properties.apiVersion), VK_VERSION_MINOR(props.properties.apiVersion), VK_VERSION_PATCH(props.properties.apiVersion),
				usable ? "ray tracing OK" : why.c_str(), match && !vk.physical ? "  <- this one" : "");
			if (match && !vk.physical) {
				if (!usable) whyNot = std::string(props.properties.deviceName) + ": " + why;
				vk.physical = d;
				vk.properties = props.properties;
				vk.ids = ids;
				vk.ids.pNext = nullptr;
			}
		}
		if (!vk.physical) { Status("Vulkan: the GPU the game uses was not found among %u Vulkan devices", count); return false; }
		if (!whyNot.empty()) { Status("no ray tracing on the game's GPU (%s)", whyNot.c_str()); return false; }

		VkPhysicalDeviceProperties2 props2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &vk.asProperties };
		vkGetPhysicalDeviceProperties2(vk.physical, &props2);
		vkGetPhysicalDeviceMemoryProperties(vk.physical, &vk.memory);

		count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(vk.physical, &count, nullptr);
		std::vector<VkQueueFamilyProperties> families(count);
		vkGetPhysicalDeviceQueueFamilyProperties(vk.physical, &count, families.data());
		bool found = false;
		for (uint32_t i = 0; i < count && !found; i++) {
			if ((families[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
				vk.queueFamily = i;
				vk.timestampBits = families[i].timestampValidBits;
				found = true;
			}
		}
		if (!found) { Status("Vulkan: no graphics + compute queue"); return false; }

		const char* enabled[] = { VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_RAY_QUERY_EXTENSION_NAME, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
			VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME };

		VkPhysicalDeviceVulkan12Features supported12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
		VkPhysicalDeviceFeatures2 supported = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &supported12 };
		vkGetPhysicalDeviceFeatures2(vk.physical, &supported);

		VkPhysicalDeviceRayQueryFeaturesKHR rq = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
		rq.rayQuery = VK_TRUE;
		VkPhysicalDeviceAccelerationStructureFeaturesKHR as = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &rq };
		as.accelerationStructure = VK_TRUE;
		VkPhysicalDeviceVulkan12Features features12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &as };
		features12.timelineSemaphore = VK_TRUE;
		features12.bufferDeviceAddress = VK_TRUE;
		features12.hostQueryReset = supported12.hostQueryReset;
		features12.descriptorBindingPartiallyBound = supported12.descriptorBindingPartiallyBound;
		VkPhysicalDeviceFeatures2 features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &features12 };
		features.features.shaderStorageImageExtendedFormats = supported.features.shaderStorageImageExtendedFormats;
		features.features.shaderStorageImageWriteWithoutFormat = supported.features.shaderStorageImageWriteWithoutFormat;
		features.features.shaderStorageImageReadWithoutFormat = supported.features.shaderStorageImageReadWithoutFormat;
		features.features.shaderInt64 = supported.features.shaderInt64;

		float priority = 1.0f;
		VkDeviceQueueCreateInfo queueInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
		queueInfo.queueFamilyIndex = vk.queueFamily;
		queueInfo.queueCount = 1;
		queueInfo.pQueuePriorities = &priority;
		VkDeviceCreateInfo deviceInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &features };
		deviceInfo.queueCreateInfoCount = 1;
		deviceInfo.pQueueCreateInfos = &queueInfo;
		deviceInfo.enabledExtensionCount = (uint32_t)(sizeof(enabled) / sizeof(enabled[0]));
		deviceInfo.ppEnabledExtensionNames = enabled;
		VK_CHECK(vkCreateDevice(vk.physical, &deviceInfo, nullptr, &vk.device), "vkCreateDevice");
		vkGetDeviceQueue(vk.device, vk.queueFamily, 0, &vk.queue);
		vk.importSemaphoreWin32Handle = (PFN_vkImportSemaphoreWin32HandleKHR)vkGetDeviceProcAddr(vk.device, "vkImportSemaphoreWin32HandleKHR");
		vk.createAs = (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(vk.device, "vkCreateAccelerationStructureKHR");
		vk.destroyAs = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(vk.device, "vkDestroyAccelerationStructureKHR");
		vk.buildSizes = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(vk.device, "vkGetAccelerationStructureBuildSizesKHR");
		vk.asAddress = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(vk.device, "vkGetAccelerationStructureDeviceAddressKHR");
		vk.cmdBuildAs = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(vk.device, "vkCmdBuildAccelerationStructuresKHR");
		if (!vk.importSemaphoreWin32Handle || !vk.createAs || !vk.destroyAs || !vk.buildSizes || !vk.asAddress || !vk.cmdBuildAs) {
			Status("Vulkan: entry points missing");
			return false;
		}
		Log("device: %s, max %llu instances, %llu triangles a geometry, scratch alignment %u", vk.properties.deviceName,
			(unsigned long long)vk.asProperties.maxInstanceCount, (unsigned long long)vk.asProperties.maxPrimitiveCount,
			vk.asProperties.minAccelerationStructureScratchOffsetAlignment);
		return true;
	}

	int32_t FindMemoryType(const VulkanDevice& vk, uint32_t bits, VkMemoryPropertyFlags flags) {
		for (uint32_t i = 0; i < vk.memory.memoryTypeCount; i++)
			if ((bits & (1u << i)) && (vk.memory.memoryTypes[i].propertyFlags & flags) == flags) return (int32_t)i;
		return -1;
	}

	struct Buffer {
		VkBuffer		buffer = VK_NULL_HANDLE;
		VkDeviceMemory	memory = VK_NULL_HANDLE;
		VkDeviceSize	size = 0;
		uint64_t		address = 0;
		void*			mapped = nullptr;
	};

	bool CreateBuffer(const VulkanDevice& vk, VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible, Buffer& out) {
		VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		info.size = std::max<VkDeviceSize>(size, 256);
		info.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		VK_CHECK(vkCreateBuffer(vk.device, &info, nullptr, &out.buffer), "vkCreateBuffer");
		VkMemoryRequirements req = {};
		vkGetBufferMemoryRequirements(vk.device, out.buffer, &req);
		int32_t type = FindMemoryType(vk, req.memoryTypeBits, hostVisible ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
			: VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		if (type < 0) type = FindMemoryType(vk, req.memoryTypeBits, 0);
		if (type < 0) { Status("Vulkan: no memory type for a buffer"); return false; }
		VkMemoryAllocateFlagsInfo flags = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
		flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
		VkMemoryAllocateInfo alloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &flags };
		alloc.allocationSize = req.size;
		alloc.memoryTypeIndex = (uint32_t)type;
		VK_CHECK(vkAllocateMemory(vk.device, &alloc, nullptr, &out.memory), "vkAllocateMemory (buffer)");
		VK_CHECK(vkBindBufferMemory(vk.device, out.buffer, out.memory, 0), "vkBindBufferMemory");
		VkBufferDeviceAddressInfo addressInfo = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
		addressInfo.buffer = out.buffer;
		out.address = vkGetBufferDeviceAddress(vk.device, &addressInfo);
		out.size = info.size;
		if (hostVisible) VK_CHECK(vkMapMemory(vk.device, out.memory, 0, VK_WHOLE_SIZE, 0, &out.mapped), "vkMapMemory");
		return true;
	}

	void DestroyBuffer(const VulkanDevice& vk, Buffer& b) {
		if (b.buffer) vkDestroyBuffer(vk.device, b.buffer, nullptr);
		if (b.memory) vkFreeMemory(vk.device, b.memory, nullptr);
		b = Buffer();
	}

	// The memory of every bottom-level structure: 64 MB blocks, first fit, 256-byte aligned pieces.
	struct StructurePool {
		struct Block { Buffer buffer; std::map<VkDeviceSize, VkDeviceSize> free; };
		std::vector<Block> blocks;
		VkDeviceSize used = 0;

		bool Allocate(const VulkanDevice& vk, VkDeviceSize size, uint32_t& block, VkDeviceSize& offset) {
			size = AlignUp(size, 256);
			for (uint32_t b = 0; b < blocks.size(); b++) {
				for (auto it = blocks[b].free.begin(); it != blocks[b].free.end(); ++it) {
					if (it->second < size) continue;
					block = b;
					offset = it->first;
					const VkDeviceSize rest = it->second - size, at = it->first + size;
					blocks[b].free.erase(it);
					if (rest) blocks[b].free[at] = rest;
					used += size;
					return true;
				}
			}
			Block nb;
			const VkDeviceSize blockSize = std::max(kPoolBlock, size);
			if (!CreateBuffer(vk, blockSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false, nb.buffer)) return false;
			if (blockSize > size) nb.free[size] = blockSize - size;
			blocks.push_back(nb);
			block = (uint32_t)blocks.size() - 1;
			offset = 0;
			used += size;
			Log("structure memory: block %u of %llu MB", block, (unsigned long long)(blockSize >> 20));
			return true;
		}

		void Free(uint32_t block, VkDeviceSize offset, VkDeviceSize size) {
			size = AlignUp(size, 256);
			auto& free = blocks[block].free;
			auto it = free.emplace(offset, size).first;
			auto next = std::next(it);
			if (next != free.end() && it->first + it->second == next->first) { it->second += next->second; free.erase(next); }
			if (it != free.begin()) {
				auto prev = std::prev(it);
				if (prev->first + prev->second == it->first) { prev->second += it->second; free.erase(it); }
			}
			used -= size;
		}
	};

	struct Geometry {
		VkAccelerationStructureKHR	as = VK_NULL_HANDLE;
		uint64_t					address = 0;
		uint32_t					block = 0;
		VkDeviceSize				offset = 0, size = 0;
		uint32_t					triangles = 0;
	};

	struct SharedImage {
		VkImage			image = VK_NULL_HANDLE;
		VkDeviceMemory	memory = VK_NULL_HANDLE;
		VkImageView		view = VK_NULL_HANDLE;
		VkFormat		format = VK_FORMAT_UNDEFINED;
		uint32_t		width = 0, height = 0;
	};

	bool ImportImage(const VulkanDevice& vk, const NvrRtImageDesc& d, uint32_t index, SharedImage& out) {
		VkExternalMemoryImageCreateInfo external = { VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO };
		external.handleTypes = (VkExternalMemoryHandleTypeFlags)d.handleType;
		VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, &external };
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
		VK_CHECK(vkCreateImage(vk.device, &info, nullptr, &out.image), "vkCreateImage (shared)");
		VkMemoryRequirements requirements = {};
		vkGetImageMemoryRequirements(vk.device, out.image, &requirements);
		if (requirements.size > d.allocationSize || !(requirements.memoryTypeBits & (1u << d.memoryTypeIndex))) {
			Status("Vulkan: shared image %u does not fit the game's allocation (needs %llu bytes, types 0x%X; the game's: %llu bytes, type %u)",
				index, (unsigned long long)requirements.size, requirements.memoryTypeBits, (unsigned long long)d.allocationSize, d.memoryTypeIndex);
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
		Log("imported shared image %u: %ux%u, format %u, %llu bytes, memory type %u", index, d.width, d.height, d.vkFormat,
			(unsigned long long)d.allocationSize, d.memoryTypeIndex);
		return true;
	}

	bool ImportTimeline(const VulkanDevice& vk, uint64_t handle, const char* name, VkSemaphore& out) {
		VkSemaphoreTypeCreateInfo type = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
		type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
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

	void FullBarrier(VkCommandBuffer cmd) {
		VkMemoryBarrier b = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
		b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &b, 0, nullptr, 0, nullptr);
	}

	uint32_t Average(const uint64_t* values, uint32_t count) {
		const uint32_t n = std::min(count, 64u);
		if (!n) return 0;
		uint64_t sum = 0;
		for (uint32_t i = 0; i < n; i++) sum += values[i];
		return (uint32_t)(sum / n);
	}

	// ---------------------------------------------------------------------------------------------------------------------
	// The helper proper.

	struct Slot {
		VkCommandBuffer	cmd = VK_NULL_HANDLE;
		uint64_t		frame = 0;			// the frame last submitted from this slot (0 = none)
		Buffer			instances;			// host-visible VkAccelerationStructureInstanceKHR[NVR_RT_MAX_INSTANCES]
		Buffer			constants;			// host-visible, NVR_RT_PASS_CONSTANTS per pass
		VkDescriptorSet	sets[NVR_RT_MAX_PASSES] = {};
		bool			measured[3] = {};	// which sections had work (build, scene, trace) in that frame
	};

	struct PendingFree { uint64_t frame; Geometry geometry; };

	struct Helper {
		VulkanDevice	vk;
		SharedImage		images[NVR_RT_MAX_IMAGES];
		uint32_t		imageCount = 0;
		VkSemaphore		inputsReady = VK_NULL_HANDLE, outputReady = VK_NULL_HANDLE;
		VkCommandPool	pool = VK_NULL_HANDLE;
		VkQueryPool		queries = VK_NULL_HANDLE;
		Slot			slots[kCmdRing];
		Buffer			staging;			// the arena's rows, copied out every frame that builds
		Buffer			scratch;			// build scratch (bottom levels, then the top level)
		Buffer			tlasStorage;
		VkAccelerationStructureKHR tlas = VK_NULL_HANDLE;
		VkDeviceSize	tlasScratch = 0;
		StructurePool	structures;
		std::unordered_map<uint32_t, Geometry> geometries;
		std::vector<PendingFree> pendingFrees;
		uint64_t		triangles = 0;
		VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
		VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
		VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
		std::vector<VkPipeline> pipelines;	// per shader index (null: not loaded yet or failed)
		std::vector<bool>	pipelineTried;
		uint64_t		gpuUs[64] = {}, buildUs[64] = {}, sceneUs[64] = {}, traceUs[64] = {}, responseUs[64] = {};
		uint32_t		gpuCount = 0, buildCount = 0, sceneCount = 0, traceCount = 0, responseCount = 0;
		bool			loggedFirst = false;
		NvrRtFrame		frame;				// this frame's record, copied out of the ring (2 MB: on the heap via Helper)
	};

	bool CreateTlas(Helper& h) {
		VkAccelerationStructureGeometryKHR geometry = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
		geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
		geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
		VkAccelerationStructureBuildGeometryInfoKHR build = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
		build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
		build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		build.geometryCount = 1;
		build.pGeometries = &geometry;
		const uint32_t maxCount = NVR_RT_MAX_INSTANCES;
		VkAccelerationStructureBuildSizesInfoKHR sizes = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
		h.vk.buildSizes(h.vk.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &maxCount, &sizes);
		if (!CreateBuffer(h.vk, sizes.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false, h.tlasStorage)) return false;
		VkAccelerationStructureCreateInfoKHR info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
		info.buffer = h.tlasStorage.buffer;
		info.size = sizes.accelerationStructureSize;
		info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		VK_CHECK(h.vk.createAs(h.vk.device, &info, nullptr, &h.tlas), "vkCreateAccelerationStructureKHR (scene)");
		h.tlasScratch = sizes.buildScratchSize;
		Log("scene: up to %u instances, %llu KB structure, %llu KB scratch", maxCount, (unsigned long long)(sizes.accelerationStructureSize >> 10),
			(unsigned long long)(sizes.buildScratchSize >> 10));
		return true;
	}

	bool EnsureScratch(Helper& h, VkDeviceSize size) {
		size += h.vk.asProperties.minAccelerationStructureScratchOffsetAlignment;
		if (h.scratch.size >= size) return true;
		vkQueueWaitIdle(h.vk.queue);	// rare: the old scratch may be in use by a frame in flight
		DestroyBuffer(h.vk, h.scratch);
		return CreateBuffer(h.vk, std::max<VkDeviceSize>(size, 16ull << 20), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, h.scratch);
	}

	bool CreatePipelineLayout(Helper& h) {
		VkDescriptorSetLayoutBinding bindings[2 + NVR_RT_PASS_IMAGES] = {};
		VkDescriptorBindingFlags flags[2 + NVR_RT_PASS_IMAGES] = {};
		bindings[0] = { 0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
		bindings[1] = { 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
		for (uint32_t i = 0; i < NVR_RT_PASS_IMAGES; i++) {
			bindings[2 + i] = { 2 + i, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
			flags[2 + i] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
		}
		VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlags = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
		bindingFlags.bindingCount = 2 + NVR_RT_PASS_IMAGES;
		bindingFlags.pBindingFlags = flags;
		VkDescriptorSetLayoutCreateInfo setInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, &bindingFlags };
		setInfo.bindingCount = 2 + NVR_RT_PASS_IMAGES;
		setInfo.pBindings = bindings;
		VK_CHECK(vkCreateDescriptorSetLayout(h.vk.device, &setInfo, nullptr, &h.setLayout), "vkCreateDescriptorSetLayout");
		VkPipelineLayoutCreateInfo layoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		layoutInfo.setLayoutCount = 1;
		layoutInfo.pSetLayouts = &h.setLayout;
		VK_CHECK(vkCreatePipelineLayout(h.vk.device, &layoutInfo, nullptr, &h.pipelineLayout), "vkCreatePipelineLayout");
		const uint32_t sets = kCmdRing * NVR_RT_MAX_PASSES;
		VkDescriptorPoolSize sizes[3] = {
			{ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, sets },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, sets },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, sets * NVR_RT_PASS_IMAGES } };
		VkDescriptorPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		poolInfo.maxSets = sets;
		poolInfo.poolSizeCount = 3;
		poolInfo.pPoolSizes = sizes;
		VK_CHECK(vkCreateDescriptorPool(h.vk.device, &poolInfo, nullptr, &h.descriptorPool), "vkCreateDescriptorPool");
		for (Slot& s : h.slots) {
			VkDescriptorSetLayout layouts[NVR_RT_MAX_PASSES];
			for (uint32_t i = 0; i < NVR_RT_MAX_PASSES; i++) layouts[i] = h.setLayout;
			VkDescriptorSetAllocateInfo alloc = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
			alloc.descriptorPool = h.descriptorPool;
			alloc.descriptorSetCount = NVR_RT_MAX_PASSES;
			alloc.pSetLayouts = layouts;
			VK_CHECK(vkAllocateDescriptorSets(h.vk.device, &alloc, s.sets), "vkAllocateDescriptorSets");
		}
		return true;
	}

	// The pipeline of shader `index` (loaded from NVR's path on first use; a failure is logged once and skips its passes).
	VkPipeline Pipeline(Helper& h, uint32_t index) {
		if (index >= NVR_RT_MAX_SHADERS || index >= g_shared->shaderCount) return VK_NULL_HANDLE;
		if (h.pipelines.size() <= index) { h.pipelines.resize(index + 1, VK_NULL_HANDLE); h.pipelineTried.resize(index + 1, false); }
		if (h.pipelineTried[index]) return h.pipelines[index];
		h.pipelineTried[index] = true;
		char path[261] = {};
		memcpy(path, g_shared->shaderPaths[index], 260);
		FILE* f = nullptr;
		std::vector<uint32_t> code;
		if (fopen_s(&f, path, "rb") == 0 && f) {
			fseek(f, 0, SEEK_END);
			long n = ftell(f);
			fseek(f, 0, SEEK_SET);
			if (n > 0 && (n % 4) == 0) { code.resize((size_t)n / 4); fread(code.data(), 1, (size_t)n, f); }
			fclose(f);
		}
		if (code.empty() || code[0] != 0x07230203u) {
			Status("trace shader %u: cannot read SPIR-V from %s", index, path);
			InterlockedOr((volatile LONG*)&g_shared->shaderFailures, (LONG)(1u << index));
			return VK_NULL_HANDLE;
		}
		VkShaderModuleCreateInfo moduleInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
		moduleInfo.codeSize = code.size() * 4;
		moduleInfo.pCode = code.data();
		VkShaderModule module = VK_NULL_HANDLE;
		VkResult r = vkCreateShaderModule(h.vk.device, &moduleInfo, nullptr, &module);
		VkPipeline pipeline = VK_NULL_HANDLE;
		if (r == VK_SUCCESS) {
			VkComputePipelineCreateInfo info = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
			info.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
			info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
			info.stage.module = module;
			info.stage.pName = "main";
			info.layout = h.pipelineLayout;
			r = vkCreateComputePipelines(h.vk.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
			vkDestroyShaderModule(h.vk.device, module, nullptr);
		}
		if (r != VK_SUCCESS) {
			Status("trace shader %u (%s): pipeline creation failed: %s", index, path, VkResultName(r));
			InterlockedOr((volatile LONG*)&g_shared->shaderFailures, (LONG)(1u << index));
			return VK_NULL_HANDLE;
		}
		Log("trace shader %u loaded: %s", index, path);
		h.pipelines[index] = pipeline;
		return pipeline;
	}

	// Frees geometries whose last possible user (a frame in flight) has finished.
	void ReleaseFinished(Helper& h) {
		if (h.pendingFrees.empty()) return;
		uint64_t done = 0;
		vkGetSemaphoreCounterValue(h.vk.device, h.outputReady, &done);
		auto it = std::remove_if(h.pendingFrees.begin(), h.pendingFrees.end(), [&](const PendingFree& p) {
			if (p.frame > done) return false;
			h.vk.destroyAs(h.vk.device, p.geometry.as, nullptr);
			h.structures.Free(p.geometry.block, p.geometry.offset, p.geometry.size);
			return true;
		});
		h.pendingFrees.erase(it, h.pendingFrees.end());
	}

	void ReleaseGeometry(Helper& h, uint32_t id, uint64_t frame) {
		auto it = h.geometries.find(id);
		if (it == h.geometries.end()) return;
		h.triangles -= it->second.triangles;
		h.pendingFrees.push_back({ frame, it->second });	// frames up to this one may still trace it
		h.geometries.erase(it);
	}

	bool WaitSlot(Helper& h, Slot& s, HANDLE parent) {
		if (!s.frame) return true;
		VkSemaphoreWaitInfo wait = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
		wait.semaphoreCount = 1;
		wait.pSemaphores = &h.outputReady;
		wait.pValues = &s.frame;
		for (;;) {
			VkResult r = vkWaitSemaphores(h.vk.device, &wait, 250ull * 1000 * 1000);
			if (r == VK_SUCCESS) break;
			if (r != VK_TIMEOUT) { Status("Vulkan: waiting for an earlier frame failed: %s", VkResultName(r)); return false; }
			if (WaitForSingleObject(parent, 0) == WAIT_OBJECT_0 || g_shared->quit) return false;
		}
		if (h.queries) {
			const uint32_t slot = (uint32_t)(&s - h.slots);
			uint64_t stamps[kQueriesPerSlot] = {};
			if (vkGetQueryPoolResults(h.vk.device, h.queries, slot * kQueriesPerSlot, kQueriesPerSlot, sizeof(stamps), stamps, sizeof(uint64_t),
				VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
				const uint64_t mask = h.vk.timestampBits >= 64 ? ~0ull : ((1ull << h.vk.timestampBits) - 1);
				auto us = [&](uint32_t a, uint32_t b) { return (uint64_t)((double)((stamps[b] - stamps[a]) & mask) * h.vk.properties.limits.timestampPeriod / 1000.0); };
				const uint64_t total = us(0, 3);
				h.gpuUs[h.gpuCount++ % 64] = total;
				g_shared->gpuTimeUsLast = (uint32_t)total;
				g_shared->gpuTimeUsAvg = Average(h.gpuUs, h.gpuCount);
				if (s.measured[0]) { h.buildUs[h.buildCount++ % 64] = us(0, 1); g_shared->buildUsAvg = Average(h.buildUs, h.buildCount); }
				if (s.measured[1]) { h.sceneUs[h.sceneCount++ % 64] = us(1, 2); g_shared->sceneUsAvg = Average(h.sceneUs, h.sceneCount); }
				if (s.measured[2]) { h.traceUs[h.traceCount++ % 64] = us(2, 3); g_shared->traceUsAvg = Average(h.traceUs, h.traceCount); }
			}
		}
		return true;
	}

	// Records this frame's work into s.cmd (open). False: something could not be recorded (logged); the caller still submits.
	bool RecordFrame(Helper& h, Slot& s, uint64_t frameNumber) {
		const NvrRtFrame& f = h.frame;
		const uint32_t slot = (uint32_t)(&s - h.slots);
		VkCommandBuffer cmd = s.cmd;
		s.measured[0] = s.measured[1] = s.measured[2] = false;
		FullBarrier(cmd);	// the previous frame's builds / passes and the game's writes (the semaphore makes them visible)

		// 1. new geometry: the arena's rows into the staging buffer, then every build in one call
		std::vector<VkAccelerationStructureBuildGeometryInfoKHR> infos;
		std::vector<VkAccelerationStructureGeometryKHR> geoms;
		std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges;
		std::vector<uint32_t> builtIds;
		const SharedImage& arena = h.images[NVR_RT_ARENA_IMAGE];
		uint32_t builds = 0;
		for (uint32_t i = 0; i < f.opCount; i++) if (f.ops[i].op == NVR_RT_OP_BUILD) builds++;
		if (builds && f.arenaTexels) {
			const uint64_t capacity = (uint64_t)arena.width * arena.height;
			const uint32_t rows = (uint32_t)std::min<uint64_t>((f.arenaTexels + arena.width - 1) / arena.width, arena.height);
			VkBufferImageCopy copy = {};
			copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
			copy.imageExtent = { arena.width, rows, 1 };
			vkCmdCopyImageToBuffer(cmd, arena.image, VK_IMAGE_LAYOUT_GENERAL, h.staging.buffer, 1, &copy);
			FullBarrier(cmd);

			geoms.reserve(builds);
			infos.reserve(builds);
			ranges.reserve(builds);
			VkDeviceSize scratchTotal = 0;
			std::vector<VkDeviceSize> scratchOffsets;
			const VkDeviceSize align = h.vk.asProperties.minAccelerationStructureScratchOffsetAlignment;
			for (uint32_t i = 0; i < f.opCount; i++) {
				const NvrRtGeometryOp& op = f.ops[i];
				if (op.op != NVR_RT_OP_BUILD) continue;
				const uint64_t texels = 3ull * ((uint64_t)op.vertexCount + op.triangleCount);
				if (!op.id || !op.vertexCount || !op.triangleCount || (uint64_t)op.arenaTexel + texels > std::min<uint64_t>(capacity, f.arenaTexels) ||
					op.triangleCount > h.vk.asProperties.maxPrimitiveCount) {
					g_shared->failedOps++;
					Log("geometry %u refused (%u vertices, %u triangles at texel %u, arena %u texels)", op.id, op.vertexCount, op.triangleCount, op.arenaTexel, f.arenaTexels);
					continue;
				}
				ReleaseGeometry(h, op.id, frameNumber);	// rebuilt under the same id: the old one goes
				VkAccelerationStructureGeometryKHR g = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
				g.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
				g.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
				g.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
				g.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
				g.geometry.triangles.vertexData.deviceAddress = h.staging.address + (VkDeviceSize)op.arenaTexel * 4;
				g.geometry.triangles.vertexStride = 12;
				g.geometry.triangles.maxVertex = op.vertexCount - 1;
				g.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
				g.geometry.triangles.indexData.deviceAddress = h.staging.address + ((VkDeviceSize)op.arenaTexel + 3ull * op.vertexCount) * 4;
				VkAccelerationStructureBuildGeometryInfoKHR b = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
				b.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
				b.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
				b.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
				b.geometryCount = 1;
				b.pGeometries = &g;
				VkAccelerationStructureBuildSizesInfoKHR sizes = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
				h.vk.buildSizes(h.vk.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &b, &op.triangleCount, &sizes);
				Geometry geo;
				geo.size = sizes.accelerationStructureSize;
				geo.triangles = op.triangleCount;
				if (!h.structures.Allocate(h.vk, geo.size, geo.block, geo.offset)) { g_shared->failedOps++; continue; }
				VkAccelerationStructureCreateInfoKHR info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
				info.buffer = h.structures.blocks[geo.block].buffer.buffer;
				info.offset = geo.offset;
				info.size = geo.size;
				info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
				if (h.vk.createAs(h.vk.device, &info, nullptr, &geo.as) != VK_SUCCESS) {
					h.structures.Free(geo.block, geo.offset, geo.size);
					g_shared->failedOps++;
					continue;
				}
				VkAccelerationStructureDeviceAddressInfoKHR addressInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
				addressInfo.accelerationStructure = geo.as;
				geo.address = h.vk.asAddress(h.vk.device, &addressInfo);
				h.geometries[op.id] = geo;
				h.triangles += geo.triangles;
				geoms.push_back(g);
				b.dstAccelerationStructure = geo.as;
				infos.push_back(b);
				ranges.push_back({ op.triangleCount, 0, 0, 0 });
				scratchOffsets.push_back(scratchTotal);
				scratchTotal += AlignUp(sizes.buildScratchSize, align);
			}
			if (!infos.empty()) {
				if (!EnsureScratch(h, std::max(scratchTotal, h.tlasScratch))) return false;
				const uint64_t base = AlignUp(h.scratch.address, align);
				std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> rangePointers(infos.size());
				for (size_t i = 0; i < infos.size(); i++) {
					infos[i].pGeometries = &geoms[i];
					infos[i].scratchData.deviceAddress = base + scratchOffsets[i];
					rangePointers[i] = &ranges[i];
				}
				h.vk.cmdBuildAs(cmd, (uint32_t)infos.size(), infos.data(), rangePointers.data());
				FullBarrier(cmd);
				s.measured[0] = true;
			}
		}
		for (uint32_t i = 0; i < f.opCount; i++)
			if (f.ops[i].op == NVR_RT_OP_RELEASE) ReleaseGeometry(h, f.ops[i].id, frameNumber);
		if (h.queries) vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, h.queries, slot * kQueriesPerSlot + 1);

		// 2. the scene from this frame's instances
		VkAccelerationStructureInstanceKHR* out = (VkAccelerationStructureInstanceKHR*)s.instances.mapped;
		uint32_t count = 0;
		for (uint32_t i = 0; i < std::min<uint32_t>(f.instanceCount, NVR_RT_MAX_INSTANCES); i++) {
			const NvrRtInstance& in = f.instances[i];
			auto it = h.geometries.find(in.geometryId);
			if (it == h.geometries.end()) continue;
			VkAccelerationStructureInstanceKHR& o = out[count++];
			memcpy(&o.transform, in.transform, sizeof(o.transform));
			o.instanceCustomIndex = in.customIndex & 0xFFFFFF;
			o.mask = in.mask & 0xFF;
			o.instanceShaderBindingTableRecordOffset = 0;
			o.flags = in.flags & 0xFF;
			o.accelerationStructureReference = it->second.address;
		}
		g_shared->lastInstances = count;
		if (!EnsureScratch(h, h.tlasScratch)) return false;
		{
			VkAccelerationStructureGeometryKHR g = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
			g.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
			g.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
			g.geometry.instances.data.deviceAddress = s.instances.address;
			VkAccelerationStructureBuildGeometryInfoKHR b = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
			b.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
			b.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
			b.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
			b.dstAccelerationStructure = h.tlas;
			b.geometryCount = 1;
			b.pGeometries = &g;
			b.scratchData.deviceAddress = AlignUp(h.scratch.address, h.vk.asProperties.minAccelerationStructureScratchOffsetAlignment);
			VkAccelerationStructureBuildRangeInfoKHR range = { count, 0, 0, 0 };
			const VkAccelerationStructureBuildRangeInfoKHR* rangePointer = &range;
			h.vk.cmdBuildAs(cmd, 1, &b, &rangePointer);
			FullBarrier(cmd);
			s.measured[1] = true;
		}
		if (h.queries) vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, h.queries, slot * kQueriesPerSlot + 2);

		// 3. the passes
		for (uint32_t p = 0; p < std::min<uint32_t>(f.passCount, NVR_RT_MAX_PASSES); p++) {
			const NvrRtPass& pass = f.passes[p];
			VkPipeline pipeline = Pipeline(h, pass.shader);
			if (!pipeline || pass.imageCount > NVR_RT_PASS_IMAGES) continue;
			bool imagesOk = true;
			for (uint32_t i = 0; i < pass.imageCount; i++) imagesOk = imagesOk && pass.images[i] < h.imageCount && h.images[pass.images[i]].view;
			if (!imagesOk) { Log("pass %u: an image index is not a shared image", p); continue; }
			memcpy((uint8_t*)s.constants.mapped + (size_t)p * NVR_RT_PASS_CONSTANTS, pass.constants, std::min<uint32_t>(pass.constantsSize, NVR_RT_PASS_CONSTANTS));

			VkWriteDescriptorSetAccelerationStructureKHR asWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
			asWrite.accelerationStructureCount = 1;
			asWrite.pAccelerationStructures = &h.tlas;
			VkDescriptorBufferInfo ubo = { s.constants.buffer, (VkDeviceSize)p * NVR_RT_PASS_CONSTANTS, NVR_RT_PASS_CONSTANTS };
			VkDescriptorImageInfo imageInfos[NVR_RT_PASS_IMAGES] = {};
			VkWriteDescriptorSet writes[2 + NVR_RT_PASS_IMAGES] = {};
			writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, &asWrite, s.sets[p], 0, 0, 1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR };
			writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s.sets[p], 1, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &ubo };
			for (uint32_t i = 0; i < pass.imageCount; i++) {
				imageInfos[i] = { VK_NULL_HANDLE, h.images[pass.images[i]].view, VK_IMAGE_LAYOUT_GENERAL };
				writes[2 + i] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s.sets[p], 2 + i, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &imageInfos[i] };
			}
			vkUpdateDescriptorSets(h.vk.device, 2 + pass.imageCount, writes, 0, nullptr);
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, h.pipelineLayout, 0, 1, &s.sets[p], 0, nullptr);
			vkCmdDispatch(cmd, std::max(1u, pass.groupsX), std::max(1u, pass.groupsY), std::max(1u, pass.groupsZ));
			FullBarrier(cmd);	// the next pass may read this one's output; the game reads them after the signal
			s.measured[2] = true;
		}
		return true;
	}

	// One frame: GPU wait for inputsReady >= N, the work, signal outputReady = N. Always submits (even when the work could not be
	// recorded) so the game's GPU wait for N is released.
	bool ProcessFrame(Helper& h, uint64_t frameNumber, HANDLE parent) {
		const NvrRtFrame& ring = g_shared->frames[frameNumber % NVR_RT_FRAME_RING];
		const bool intact = ring.frame == frameNumber;
		if (intact) {
			// Copy the record out (only as much as it uses), then tell NVR the slot is free again
			memcpy(&h.frame, &ring, offsetof(NvrRtFrame, ops));
			h.frame.opCount = std::min<uint32_t>(h.frame.opCount, NVR_RT_MAX_OPS);
			h.frame.instanceCount = std::min<uint32_t>(h.frame.instanceCount, NVR_RT_MAX_INSTANCES);
			h.frame.passCount = std::min<uint32_t>(h.frame.passCount, NVR_RT_MAX_PASSES);
			memcpy(h.frame.ops, ring.ops, sizeof(NvrRtGeometryOp) * h.frame.opCount);
			memcpy(h.frame.instances, ring.instances, sizeof(NvrRtInstance) * h.frame.instanceCount);
			memcpy(h.frame.passes, ring.passes, sizeof(NvrRtPass) * h.frame.passCount);
		} else {
			// NVR waits before reusing a slot, so this means NVR skipped the frame: nothing to do but release its wait
			memset(&h.frame, 0, offsetof(NvrRtFrame, ops));
		}
		MemoryBarrier();
		g_shared->consumedFrame = (int64_t)frameNumber;

		Slot& s = h.slots[frameNumber % kCmdRing];
		if (!WaitSlot(h, s, parent)) return false;
		ReleaseFinished(h);
		VkCommandBuffer cmd = s.cmd;
		vkResetCommandBuffer(cmd, 0);
		VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		VK_CHECK(vkBeginCommandBuffer(cmd, &begin), "vkBeginCommandBuffer");
		const uint32_t slot = (uint32_t)(frameNumber % kCmdRing);
		if (h.queries) {
			vkCmdResetQueryPool(cmd, h.queries, slot * kQueriesPerSlot, kQueriesPerSlot);
			vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, h.queries, slot * kQueriesPerSlot);
		}
		if (intact && !RecordFrame(h, s, frameNumber)) {
			// Start over with an empty command buffer: the signal must still go out
			vkResetCommandBuffer(cmd, 0);
			VK_CHECK(vkBeginCommandBuffer(cmd, &begin), "vkBeginCommandBuffer");
			if (h.queries) {
				vkCmdResetQueryPool(cmd, h.queries, slot * kQueriesPerSlot, kQueriesPerSlot);
				vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, h.queries, slot * kQueriesPerSlot);
				vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, h.queries, slot * kQueriesPerSlot + 1);
				vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, h.queries, slot * kQueriesPerSlot + 2);
			}
		} else if (!intact && h.queries) {
			vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, h.queries, slot * kQueriesPerSlot + 1);
			vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, h.queries, slot * kQueriesPerSlot + 2);
		}
		if (h.queries) vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, h.queries, slot * kQueriesPerSlot + 3);
		VK_CHECK(vkEndCommandBuffer(cmd), "vkEndCommandBuffer");

		const uint64_t waitValue = frameNumber, signalValue = frameNumber;
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
		s.frame = frameNumber;

		LARGE_INTEGER now, frequency;
		QueryPerformanceCounter(&now);
		QueryPerformanceFrequency(&frequency);
		if (intact && h.frame.writtenQpc) {
			const uint64_t us = (uint64_t)((now.QuadPart - h.frame.writtenQpc) * 1000000 / frequency.QuadPart);
			h.responseUs[h.responseCount++ % 64] = us;
			g_shared->responseUsAvg = Average(h.responseUs, h.responseCount);
			if (us > g_shared->responseUsMax) g_shared->responseUsMax = (uint32_t)us;
		}
		g_shared->geometries = (uint32_t)h.geometries.size();
		g_shared->triangles = h.triangles;
		g_shared->geometryBytes = h.structures.used;
		g_shared->framesSubmitted++;
		MemoryBarrier();
		g_shared->submittedFrame = (int64_t)frameNumber;
		if (!h.loggedFirst) {
			h.loggedFirst = true;
			Log("first frame %llu submitted (%u ops, %u instances, %u passes)", (unsigned long long)frameNumber, h.frame.opCount, h.frame.instanceCount, h.frame.passCount);
		}
		if ((frameNumber % 600) == 0)
			Log("frame %llu: %u geometries (%llu triangles, %llu MB), %u instances; GPU %.3f ms avg (builds %.3f, scene %.3f, passes %.3f), response %.3f ms avg (max %.3f)",
				(unsigned long long)frameNumber, g_shared->geometries, (unsigned long long)g_shared->triangles, (unsigned long long)(g_shared->geometryBytes >> 20),
				g_shared->lastInstances, g_shared->gpuTimeUsAvg / 1000.0, g_shared->buildUsAvg / 1000.0, g_shared->sceneUsAvg / 1000.0, g_shared->traceUsAvg / 1000.0,
				g_shared->responseUsAvg / 1000.0, g_shared->responseUsMax / 1000.0);
		return true;
	}

	int RunHelper(const wchar_t* base) {
		OpenLog(L"NvrRtHelper.log");
		Log("NvrRtHelper (protocol %u) for New Vegas Reloaded, shared block %ls", NVR_RT_PROTOCOL_VERSION, base);
		HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, base);
		if (!mapping) { Log("the shared block %ls does not exist (error %lu)", base, GetLastError()); return 2; }
		NvrRtShared* shared = (NvrRtShared*)MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(NvrRtShared));
		if (!shared) { Log("could not map the shared block (error %lu)", GetLastError()); return 2; }
		if (shared->magic != NVR_RT_MAGIC || shared->version != NVR_RT_PROTOCOL_VERSION || shared->size != sizeof(NvrRtShared)) {
			Log("the shared block is of another version (magic 0x%08X, version %u, size %u; this helper: version %u, size %u)", shared->magic,
				shared->version, shared->size, NVR_RT_PROTOCOL_VERSION, (unsigned)sizeof(NvrRtShared));
			return 2;
		}
		const std::wstring frameName = std::wstring(base) + NVR_RT_EVENT_FRAME_SUFFIX, statusName = std::wstring(base) + NVR_RT_EVENT_STATUS_SUFFIX;
		HANDLE frameEvent = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, frameName.c_str());
		g_statusEvent = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, statusName.c_str());
		g_shared = shared;
		if (!frameEvent || !g_statusEvent) { Status("the helper's events do not exist"); SetState(NVR_RT_STATE_FAILED); return 2; }
		HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, shared->parentPid);
		if (!parent) { Status("the game's process %u cannot be opened", shared->parentPid); SetState(NVR_RT_STATE_FAILED); return 2; }

		for (int i = 0; i < 100 && !shared->setupDone; i++) {
			HANDLE waits[2] = { frameEvent, parent };
			if (WaitForMultipleObjects(2, waits, FALSE, 100) == WAIT_OBJECT_0 + 1) return 0;
		}
		MemoryBarrier();
		if (!shared->setupDone) { Status("NVR did not finish the setup in 10 seconds"); SetState(NVR_RT_STATE_FAILED); return 2; }

		Helper* hp = new Helper();
		Helper& h = *hp;
		auto fail = [&](const char* why) { if (why) Status("%s", why); SetState(NVR_RT_STATE_FAILED); return 3; };
		if (!CreateVulkanDevice(h.vk, shared->deviceLuid, shared->deviceLuidValid != 0, shared->deviceUuid)) return fail(nullptr);
		if (memcmp(h.vk.ids.driverUUID, shared->driverUuid, VK_UUID_SIZE))
			return fail("the helper's Vulkan driver is not the game's (driver UUIDs differ): images cannot be shared");
		h.imageCount = std::min<uint32_t>(shared->imageCount, NVR_RT_MAX_IMAGES);
		if (!h.imageCount) return fail("no arena image");
		for (uint32_t i = 0; i < h.imageCount; i++)
			if (!ImportImage(h.vk, shared->images[i], i, h.images[i])) return fail(nullptr);
		if (h.images[NVR_RT_ARENA_IMAGE].format != VK_FORMAT_R32_SFLOAT) return fail("the arena (image 0) is not R32F");
		if (!ImportTimeline(h.vk, shared->semInputsReady, "inputs-ready", h.inputsReady) ||
			!ImportTimeline(h.vk, shared->semOutputReady, "output-ready", h.outputReady)) return fail(nullptr);

		VkCommandPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		poolInfo.queueFamilyIndex = h.vk.queueFamily;
		if (vkCreateCommandPool(h.vk.device, &poolInfo, nullptr, &h.pool) != VK_SUCCESS) return fail("Vulkan: vkCreateCommandPool failed");
		for (Slot& s : h.slots) {
			VkCommandBufferAllocateInfo cmdInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
			cmdInfo.commandPool = h.pool;
			cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			cmdInfo.commandBufferCount = 1;
			if (vkAllocateCommandBuffers(h.vk.device, &cmdInfo, &s.cmd) != VK_SUCCESS) return fail("Vulkan: vkAllocateCommandBuffers failed");
			if (!CreateBuffer(h.vk, sizeof(VkAccelerationStructureInstanceKHR) * NVR_RT_MAX_INSTANCES,
					VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, true, s.instances) ||
				!CreateBuffer(h.vk, (VkDeviceSize)NVR_RT_PASS_CONSTANTS * NVR_RT_MAX_PASSES, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, s.constants))
				return fail(nullptr);
		}
		if (h.vk.timestampBits) {
			VkQueryPoolCreateInfo queryInfo = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
			queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
			queryInfo.queryCount = kCmdRing * kQueriesPerSlot;
			if (vkCreateQueryPool(h.vk.device, &queryInfo, nullptr, &h.queries) != VK_SUCCESS) h.queries = VK_NULL_HANDLE;
		}
		const SharedImage& arena = h.images[NVR_RT_ARENA_IMAGE];
		if (!CreateBuffer(h.vk, (VkDeviceSize)arena.width * arena.height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT |
				VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, false, h.staging) ||
			!CreateTlas(h) || !EnsureScratch(h, h.tlasScratch) || !CreatePipelineLayout(h))
			return fail(nullptr);

		// The shaders NVR registered at setup are compiled now (a first use mid-game would hold the game's GPU ~2 s on the iGPU)
		for (uint32_t i = 0; i < std::min<uint32_t>(shared->shaderCount, NVR_RT_MAX_SHADERS); i++) Pipeline(h, i);

		Status("ready: ray tracing on %s (%u shared images, arena %ux%u)", h.vk.properties.deviceName, h.imageCount, arena.width, arena.height);
		SetState(NVR_RT_STATE_READY);

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
			for (uint32_t i = 0; i < std::min<uint32_t>(shared->shaderCount, NVR_RT_MAX_SHADERS); i++) Pipeline(h, i);	// shaders registered later
			const int64_t latest = InterlockedCompareExchange64((volatile LONG64*)&shared->latestFrame, 0, 0);
			MemoryBarrier();
			while ((int64_t)next <= latest) {
				if (!ProcessFrame(h, next, parent)) { Log("stopping at frame %llu", (unsigned long long)next); goto done; }
				next++;
			}
		}
	done:
		if (shared->state != NVR_RT_STATE_FAILED) SetState(NVR_RT_STATE_EXITED);
		Log("exit after %llu frames%s", (unsigned long long)shared->framesSubmitted, parentGone ? " (the game is gone)" : "");
		if (g_log) fclose(g_log);
		ExitProcess(0);
	}

	int RunProbe() {
		g_console = true;
		OpenLog(L"NvrRtHelper-probe.log");
		Log("NvrRtHelper probe (protocol %u, shared block %u bytes)", NVR_RT_PROTOCOL_VERSION, (unsigned)sizeof(NvrRtShared));
		VulkanDevice vk;
		if (!CreateVulkanDevice(vk, nullptr, false, nullptr)) { Log("probe: no GPU here can run the ray tracing helper"); return 1; }
		Log("probe: the helper can run on %s", vk.properties.deviceName);
		vkDestroyDevice(vk.device, nullptr);
		vkDestroyInstance(vk.instance, nullptr);
		return 0;
	}
}

int wmain(int argc, wchar_t** argv) {
	InitializeCriticalSection(&g_logLock);
	g_exeDir = ExeDir();
	for (int i = 1; i < argc; i++) {
		if (!wcscmp(argv[i], L"--shm") && i + 1 < argc) return RunHelper(argv[i + 1]);
		if (!wcscmp(argv[i], L"--probe")) return RunProbe();
	}
	printf("NvrRtHelper: started by New Vegas Reloaded. By hand: --probe (can this PC run it?).\n");
	return 1;
}
