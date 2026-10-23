## VkComputeSpike.h

```cpp
#pragma once

struct IDirect3DDevice9;
struct IDirect3DTexture9;

namespace VkComputeSpike {
    void Initialize(IDirect3DDevice9* device);
    void Run(IDirect3DTexture9* source, IDirect3DTexture9* avgLumaTexture);
    void Shutdown();
    bool Enabled();
}
```

## VkComputeSpike.cpp

```cpp
#include <windows.h>
#include <d3d9.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include "../d3d9/d3d9_interfaces.h"
#include "VkComputeSpike.h"
#include "Logger.h"
#include "SettingsManager.h"
#include "TextureManager.h"

// DXVK interop on 32-bit: non-dispatchable handles (VkImage, VkBuffer, etc.)
// are uint64_t. The Vulkan headers already use that for x86; keep all handles
// in their native Vk* types and never cast them to pointers.
namespace {
    constexpr UINT32 kMaxFramesInFlight = 4;
    constexpr UINT32 kLogSampleCount = 120;

    struct VulkanApi {
        PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
        PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = nullptr;
        PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties = nullptr;
        PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties = nullptr;
        PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties = nullptr;
        PFN_vkCreateCommandPool vkCreateCommandPool = nullptr;
        PFN_vkDestroyCommandPool vkDestroyCommandPool = nullptr;
        PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers = nullptr;
        PFN_vkFreeCommandBuffers vkFreeCommandBuffers = nullptr;
        PFN_vkResetCommandBuffer vkResetCommandBuffer = nullptr;
        PFN_vkBeginCommandBuffer vkBeginCommandBuffer = nullptr;
        PFN_vkEndCommandBuffer vkEndCommandBuffer = nullptr;
        PFN_vkCmdDispatch vkCmdDispatch = nullptr;
        PFN_vkCmdBindPipeline vkCmdBindPipeline = nullptr;
        PFN_vkCmdBindDescriptorSets vkCmdBindDescriptorSets = nullptr;
        PFN_vkCmdPushConstants vkCmdPushConstants = nullptr;
        PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier = nullptr;
        PFN_vkCmdCopyBufferToImage vkCmdCopyBufferToImage = nullptr;
        PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout = nullptr;
        PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout = nullptr;
        PFN_vkCreateDescriptorPool vkCreateDescriptorPool = nullptr;
        PFN_vkDestroyDescriptorPool vkDestroyDescriptorPool = nullptr;
        PFN_vkAllocateDescriptorSets vkAllocateDescriptorSets = nullptr;
        PFN_vkUpdateDescriptorSets vkUpdateDescriptorSets = nullptr;
        PFN_vkCreatePipelineLayout vkCreatePipelineLayout = nullptr;
        PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout = nullptr;
        PFN_vkCreateComputePipelines vkCreateComputePipelines = nullptr;
        PFN_vkDestroyPipeline vkDestroyPipeline = nullptr;
        PFN_vkCreateShaderModule vkCreateShaderModule = nullptr;
        PFN_vkDestroyShaderModule vkDestroyShaderModule = nullptr;
        PFN_vkCreateBuffer vkCreateBuffer = nullptr;
        PFN_vkDestroyBuffer vkDestroyBuffer = nullptr;
        PFN_vkAllocateMemory vkAllocateMemory = nullptr;
        PFN_vkFreeMemory vkFreeMemory = nullptr;
        PFN_vkBindBufferMemory vkBindBufferMemory = nullptr;
        PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements = nullptr;
        PFN_vkCreateImageView vkCreateImageView = nullptr;
        PFN_vkDestroyImageView vkDestroyImageView = nullptr;
        PFN_vkCreateSampler vkCreateSampler = nullptr;
        PFN_vkDestroySampler vkDestroySampler = nullptr;
        PFN_vkCreateFence vkCreateFence = nullptr;
        PFN_vkDestroyFence vkDestroyFence = nullptr;
        PFN_vkWaitForFences vkWaitForFences = nullptr;
        PFN_vkResetFences vkResetFences = nullptr;
        PFN_vkCreateQueryPool vkCreateQueryPool = nullptr;
        PFN_vkDestroyQueryPool vkDestroyQueryPool = nullptr;
        PFN_vkGetQueryPoolResults vkGetQueryPoolResults = nullptr;
        PFN_vkCmdWriteTimestamp vkCmdWriteTimestamp = nullptr;
        PFN_vkCmdResetQueryPool vkCmdResetQueryPool = nullptr;
        PFN_vkQueueSubmit vkQueueSubmit = nullptr;
        PFN_vkDeviceWaitIdle vkDeviceWaitIdle = nullptr;
    };

    struct FrameSlot {
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        bool everSubmitted = false;
    };

    struct SpikeState {
        bool enabled = false;
        bool initialized = false;
        bool failed = false;
        bool resourcesCreated = false;

        ID3D9VkInteropDevice* interopDevice = nullptr;
        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue queue = VK_NULL_HANDLE;
        UINT32 queueIndex = 0;
        UINT32 queueFamilyIndex = 0;

        VulkanApi api;
        VkPhysicalDeviceProperties deviceProperties{};
        double timestampPeriod = 1.0;
        bool timestampQueries = false;

        IDirect3DTexture9* destinationTexture = nullptr;
        IDirect3DSurface9* destinationSurface = nullptr;

        VkImage sourceImage = VK_NULL_HANDLE;
        VkImage destinationImage = VK_NULL_HANDLE;
        VkImageView sourceView = VK_NULL_HANDLE;
        VkFormat sourceFormat = VK_FORMAT_UNDEFINED;

        VkShaderModule shaderModule = VK_NULL_HANDLE;
        VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;

        VkBuffer partialSumsBuffer = VK_NULL_HANDLE;
        VkDeviceMemory partialSumsMemory = VK_NULL_HANDLE;
        VkBuffer outputBuffer = VK_NULL_HANDLE;
        VkDeviceMemory outputMemory = VK_NULL_HANDLE;

        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkQueryPool queryPool = VK_NULL_HANDLE;
        FrameSlot frames[kMaxFramesInFlight];

        UINT32 sourceWidth = 0;
        UINT32 sourceHeight = 0;
        UINT32 numGroupsX = 0;
        UINT32 numGroupsY = 0;
        UINT32 partialSumCount = 0;
        UINT32 frameIndex = 0;

        // Timing accumulators (CPU/time and GPU timestamp ticks).
        UINT32 samples = 0;
        double totalCpuSeconds = 0.0;
        double flushSeconds = 0.0;
        double lockSeconds = 0.0;
        double submitSeconds = 0.0;
        double totalGpuTicks = 0.0;
        UINT32 gpuSamples = 0;
    };

    SpikeState s;

    struct PushConstants {
        UINT32 width;
        UINT32 height;
        UINT32 numGroupsX;
        UINT32 numGroupsY;
        UINT32 phase;
    };

    double SecondsBetween(LARGE_INTEGER a, LARGE_INTEGER b, LARGE_INTEGER freq) {
        return static_cast<double>(b.QuadPart - a.QuadPart) / static_cast<double>(freq.QuadPart);
    }

    float HalfToFloat(UINT16 h) {
        UINT32 sign = (h & 0x8000u) << 16;
        UINT32 exponent = (h >> 10) & 0x1Fu;
        UINT32 mantissa = h & 0x3FFu;
        UINT32 f = 0;

        if (exponent == 0) {
            if (mantissa == 0) {
                f = sign;
            } else {
                int e = -14;
                while ((mantissa & 0x400u) == 0) {
                    mantissa <<= 1;
                    --e;
                }
                mantissa &= 0x3FFu;
                f = sign | (static_cast<UINT32>(e + 127) << 23) | (mantissa << 13);
            }
        } else if (exponent == 31) {
            f = sign | (0xFFu << 23) | (mantissa << 12);
        } else {
            f = sign | ((exponent - 15 + 127) << 23) | (mantissa << 13);
        }

        float out;
        std::memcpy(&out, &f, sizeof(out));
        return out;
    }

    UINT32 FindMemoryType(UINT32 typeFilter, VkMemoryPropertyFlags props) {
        VkPhysicalDeviceMemoryProperties memProps{};
        s.api.vkGetPhysicalDeviceMemoryProperties(s.physicalDevice, &memProps);

        for (UINT32 i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((typeFilter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
                return i;
        }

        // Fallback: first memory type accepted by the buffer, regardless of this property.
        for (UINT32 i = 0; i < memProps.memoryTypeCount; ++i) {
            if (typeFilter & (1u << i))
                return i;
        }
        return UINT32_MAX;
    }

    bool CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer, VkDeviceMemory& memory) {
        VkBufferCreateInfo bci{};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = size;
        bci.usage = usage;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (s.api.vkCreateBuffer(s.device, &bci, nullptr, &buffer) != VK_SUCCESS) {
            Logger::Log("VK COMPUTE SPIKE: vkCreateBuffer failed");
            return false;
        }

        VkMemoryRequirements requirements{};
        s.api.vkGetBufferMemoryRequirements(s.device, buffer, &requirements);

        UINT32 memoryType = FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (memoryType == UINT32_MAX) {
            s.api.vkDestroyBuffer(s.device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
            Logger::Log("VK COMPUTE SPIKE: no suitable buffer memory type");
            return false;
        }

        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = requirements.size;
        mai.memoryTypeIndex = memoryType;

        if (s.api.vkAllocateMemory(s.device, &mai, nullptr, &memory) != VK_SUCCESS) {
            s.api.vkDestroyBuffer(s.device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
            Logger::Log("VK COMPUTE SPIKE: vkAllocateMemory failed");
            return false;
        }

        VkResult bindResult = s.api.vkBindBufferMemory(s.device, buffer, memory, 0);
        if (bindResult != VK_SUCCESS) {
            s.api.vkFreeMemory(s.device, memory, nullptr);
            memory = VK_NULL_HANDLE;
            s.api.vkDestroyBuffer(s.device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
            Logger::Log("VK COMPUTE SPIKE: vkBindBufferMemory failed (VkResult %d)", (int)bindResult);
            return false;
        }
        return true;
    }

    bool LoadShader() {
        // Offline-compiled by glslangValidator -V into avgluma.spv.
        const char* path = "Data\\Shaders\\NewVegasReloaded\\Compute\\avgluma.spv";
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            Logger::Log("VK COMPUTE SPIKE: cannot open shader file %s", path);
            return false;
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        if (size <= 0 || (size % 4) != 0) {
            Logger::Log("VK COMPUTE SPIKE: shader file invalid size (%d)", (int)size);
            return false;
        }

        std::vector<UINT32> code(static_cast<size_t>(size) / 4);
        file.read(reinterpret_cast<char*>(code.data()), size);

        VkShaderModuleCreateInfo smci{};
        smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smci.codeSize = code.size() * sizeof(UINT32);
        smci.pCode = code.data();

        VkResult result = s.api.vkCreateShaderModule(s.device, &smci, nullptr, &s.shaderModule);
        if (result != VK_SUCCESS) {
            Logger::Log("VK COMPUTE SPIKE: vkCreateShaderModule failed (VkResult %d)", (int)result);
            return false;
        }
        return true;
    }

    bool CreateVulkanObjects(VkFormat srcFormat, VkImageUsageFlags srcUsage, VkImageUsageFlags dstUsage) {
        // DXVK must have created these D3D9 textures with the Vulkan usage bits
        // required by our compute/path. If not, the Vulkan validation layer will
        // reject the descriptor/copy, so bail out here instead.
        if ((srcUsage & VK_IMAGE_USAGE_SAMPLED_BIT) == 0) {
            Logger::Log("VK COMPUTE SPIKE: source D3D9 texture missing VK_IMAGE_USAGE_SAMPLED_BIT (0x%X)", srcUsage);
            return false;
        }
        if ((dstUsage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0) {
            Logger::Log("VK COMPUTE SPIKE: destination D3D9 texture missing VK_IMAGE_USAGE_TRANSFER_DST_BIT (0x%X)", dstUsage);
            return false;
        }

        s.sourceFormat = srcFormat;

        if (!LoadShader())
            return false;

        // Sampler used only for texelFetch; state is irrelevant but required by
        // VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER.
        VkSamplerCreateInfo samplerCI{};
        samplerCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerCI.magFilter = VK_FILTER_NEAREST;
        samplerCI.minFilter = VK_FILTER_NEAREST;
        samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (s.api.vkCreateSampler(s.device, &samplerCI, nullptr, &s.sampler) != VK_SUCCESS) {
            Logger::Log("VK COMPUTE SPIKE: vkCreateSampler failed");
            return false;
        }

        // Descriptor set layout: source sampled image, partial sums SSBO, output packed SSBO.
        VkDescriptorSetLayoutBinding bindings[3] = {};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo dslci{};
        dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.bindingCount = 3;
        dslci.pBindings = bindings;

        if (s.api.vkCreateDescriptorSetLayout(s.device, &dslci, nullptr, &s.descriptorLayout) != VK_SUCCESS) {
            Logger::Log("VK COMPUTE SPIKE: vkCreateDescriptorSetLayout failed");
            return false;
        }

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(PushConstants);

        VkPipelineLayoutCreateInfo plci{};
        plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount = 1;
        plci.pSetLayouts = &s.descriptorLayout;
        plci.pushConstantRangeCount = 1;
        plci.pPushConstantRanges = &pushRange;

        if (s.api.vkCreatePipelineLayout(s.device, &plci, nullptr, &s.pipelineLayout) != VK_SUCCESS) {
            Logger::Log("VK COMPUTE SPIKE: vkCreatePipelineLayout failed");
            return false;
        }

        VkComputePipelineCreateInfo cpci{};
        cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cpci.stage.module = s.shaderModule;
        cpci.stage.pName = "main";
        cpci.layout = s.pipelineLayout;

        if (s.api.vkCreateComputePipelines(s.device, VK_NULL_HANDLE, 1, &cpci, nullptr, &s.pipeline) != VK_SUCCESS) {
            Logger::Log("VK COMPUTE SPIKE: vkCreateComputePipelines failed");
            return false;
        }

        // Partial sums: one float per 16x16 workgroup.
        if (!CreateBuffer(s.partialSumCount * sizeof(float),
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                          s.partialSumsBuffer, s.partialSumsMemory)) {
            return false;
        }

        // Output packed as two 4-byte words: R16G16B16A16_SFLOAT, 8 bytes.
        if (!CreateBuffer(2 * sizeof(UINT32),
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          s.outputBuffer, s.outputMemory)) {
            return false;
        }

        VkImageViewCreateInfo ivci{};
        ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image = s.sourceImage;
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format = s.sourceFormat;
        ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.baseMipLevel = 0;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.baseArrayLayer = 0;
        ivci.subresourceRange.layerCount = 1;

        if (s.api.vkCreateImageView(s.device, &ivci, nullptr, &s.sourceView) != VK_SUCCESS) {
            Logger::Log("VK COMPUTE SPIKE: vkCreateImageView failed");
            return false;
        }

        // One descriptor set, updated once because the source/dest images are fixed
        // until a device reset (which disables this spike).
        VkDescriptorPoolSize poolSizes[2] = {};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[0].descriptorCount = 1;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSizes[1].descriptorCount = 2;

        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets = 1;
        dpci.poolSizeCount = 2;
        dpci.pPoolSizes = poolSizes;

        if (s.api.vkCreateDescriptorPool(s.device, &dpci, nullptr, &s.descriptorPool) != VK_SUCCESS) {
            Logger::Log("VK COMPUTE SPIKE: vkCreateDescriptorPool failed");
            return false;
        }

        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = s.descriptorPool;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts = &s.descriptorLayout;

        if (s.api.vkAllocateDescriptorSets(s.device, &dsai, &s.descriptorSet) != VK_SUCCESS) {
            Logger::Log("VK COMPUTE SPIKE: vkAllocateDescriptorSets failed");
            return false;
        }

        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler = s.sampler;
        imageInfo.imageView = s.sourceView;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorBufferInfo partialInfo{};
        partialInfo.buffer = s.partialSumsBuffer;
        partialInfo.offset = 0;
        partialInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo outputInfo{};
        outputInfo.buffer = s.outputBuffer;
        outputInfo.offset = 0;
        outputInfo.range = VK_WHOLE_SIZE;

        VkWriteDescriptorSet writes[3] = {};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = s.descriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &imageInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = s.descriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].pBufferInfo = &partialInfo;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = s.descriptorSet;
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].pBufferInfo = &outputInfo;

        s.api.vkUpdateDescriptorSets(s.device, 3, writes, 0, nullptr);

        VkCommandPoolCreateInfo cpci2{};
        cpci2.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cpci2.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cpci2.queueFamilyIndex = s.queueFamilyIndex;

        if (s.api.vkCreateCommandPool(s.device, &cpci2, nullptr, &s.commandPool) != VK_SUCCESS) {
            Logger::Log("VK COMPUTE SPIKE: vkCreateCommandPool failed");
            return false;
        }

        VkCommandBufferAllocateInfo cbai{};
        cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbai.commandPool = s.commandPool;
        cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = kMaxFramesInFlight;

        VkCommandBuffer cmds[kMaxFramesInFlight] = {};
        if (s.api.vkAllocateCommandBuffers(s.device, &cbai, cmds) != VK_SUCCESS) {
            Logger::Log("VK COMPUTE SPIKE: vkAllocateCommandBuffers failed");
            return false;
        }

        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        for (UINT32 i = 0; i < kMaxFramesInFlight; ++i) {
            s.frames[i].cmd = cmds[i];
            s.frames[i].fence = VK_NULL_HANDLE;
            s.frames[i].everSubmitted = false;
            if (s.api.vkCreateFence(s.device, &fci, nullptr, &s.frames[i].fence) != VK_SUCCESS) {
                Logger::Log("VK COMPUTE SPIKE: vkCreateFence failed");
                return false;
            }
        }

        if (s.timestampQueries) {
            VkQueryPoolCreateInfo qpci{};
            qpci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            qpci.queryType = VK_QUERY_TYPE_TIMESTAMP;
            qpci.queryCount = kMaxFramesInFlight * 2;

            if (s.api.vkCreateQueryPool(s.device, &qpci, nullptr, &s.queryPool) != VK_SUCCESS) {
                Logger::Log("VK COMPUTE SPIKE: vkCreateQueryPool failed");
                return false;
            }
        }

        return true;
    }

    void DestroyVulkanObjects(bool waitForIdle) {
        if (s.device != VK_NULL_HANDLE && waitForIdle && s.api.vkDeviceWaitIdle) {
            s.api.vkDeviceWaitIdle(s.device);
        }

        if (s.commandPool != VK_NULL_HANDLE) {
            if (s.api.vkFreeCommandBuffers) {
                VkCommandBuffer cmds[kMaxFramesInFlight] = {};
                for (UINT32 i = 0; i < kMaxFramesInFlight; ++i) cmds[i] = s.frames[i].cmd;
                s.api.vkFreeCommandBuffers(s.device, s.commandPool, kMaxFramesInFlight, cmds);
            }
            if (s.api.vkDestroyCommandPool) s.api.vkDestroyCommandPool(s.device, s.commandPool, nullptr);
            s.commandPool = VK_NULL_HANDLE;
        }

        for (UINT32 i = 0; i < kMaxFramesInFlight; ++i) {
            if (s.frames[i].fence != VK_NULL_HANDLE && s.api.vkDestroyFence) {
                s.api.vkDestroyFence(s.device, s.frames[i].fence, nullptr);
                s.frames[i].fence = VK_NULL_HANDLE;
            }
            s.frames[i].cmd = VK_NULL_HANDLE;
            s.frames[i].everSubmitted = false;
        }

        if (s.queryPool != VK_NULL_HANDLE && s.api.vkDestroyQueryPool) {
            s.api.vkDestroyQueryPool(s.device, s.queryPool, nullptr);
            s.queryPool = VK_NULL_HANDLE;
        }

        if (s.pipeline != VK_NULL_HANDLE && s.api.vkDestroyPipeline) {
            s.api.vkDestroyPipeline(s.device, s.pipeline, nullptr);
            s.pipeline = VK_NULL_HANDLE;
        }
        if (s.pipelineLayout != VK_NULL_HANDLE && s.api.vkDestroyPipelineLayout) {
            s.api.vkDestroyPipelineLayout(s.device, s.pipelineLayout, nullptr);
            s.pipelineLayout = VK_NULL_HANDLE;
        }
        if (s.descriptorPool != VK_NULL_HANDLE && s.api.vkDestroyDescriptorPool) {
            s.api.vkDestroyDescriptorPool(s.device, s.descriptorPool, nullptr);
            s.descriptorPool = VK_NULL_HANDLE;
            s.descriptorSet = VK_NULL_HANDLE;
        }
        if (s.descriptorLayout != VK_NULL_HANDLE && s.api.vkDestroyDescriptorSetLayout) {
            s.api.vkDestroyDescriptorSetLayout(s.device, s.descriptorLayout, nullptr);
            s.descriptorLayout = VK_NULL_HANDLE;
        }
        if (s.shaderModule != VK_NULL_HANDLE && s.api.vkDestroyShaderModule) {
            s.api.vkDestroyShaderModule(s.device, s.shaderModule, nullptr);
            s.shaderModule = VK_NULL_HANDLE;
        }
        if (s.sampler != VK_NULL_HANDLE && s.api.vkDestroySampler) {
            s.api.vkDestroySampler(s.device, s.sampler, nullptr);
            s.sampler = VK_NULL_HANDLE;
        }
        if (s.sourceView != VK_NULL_HANDLE && s.api.vkDestroyImageView) {
            s.api.vkDestroyImageView(s.device, s.sourceView, nullptr);
            s.sourceView = VK_NULL_HANDLE;
        }
        if (s.outputBuffer != VK_NULL_HANDLE && s.api.vkDestroyBuffer) {
            s.api.vkDestroyBuffer(s.device, s.outputBuffer, nullptr);
            s.outputBuffer = VK_NULL_HANDLE;
        }
        if (s.outputMemory != VK_NULL_HANDLE && s.api.vkFreeMemory) {
            s.api.vkFreeMemory(s.device, s.outputMemory, nullptr);
            s.outputMemory = VK_NULL_HANDLE;
        }
        if (s.partialSumsBuffer != VK_NULL_HANDLE && s.api.vkDestroyBuffer) {
            s.api.vkDestroyBuffer(s.device, s.partialSumsBuffer, nullptr);
            s.partialSumsBuffer = VK_NULL_HANDLE;
        }
        if (s.partialSumsMemory != VK_NULL_HANDLE && s.api.vkFreeMemory) {
            s.api.vkFreeMemory(s.device, s.partialSumsMemory, nullptr);
            s.partialSumsMemory = VK_NULL_HANDLE;
        }

        s.sourceImage = VK_NULL_HANDLE;
        s.destinationImage = VK_NULL_HANDLE;
    }

    void DisableInternal(const char* reason, VkResult vkr = VK_SUCCESS, HRESULT hr = S_OK) {
        if (s.failed) return;

        s.failed = true;
        if (vkr != VK_SUCCESS) {
            Logger::Log("VK COMPUTE SPIKE disabled: %s (VkResult %d)", reason, (int)vkr);
        } else if (FAILED(hr)) {
            Logger::Log("VK COMPUTE SPIKE disabled: %s (HRESULT 0x%08X)", reason, (unsigned int)hr);
        } else {
            Logger::Log("VK COMPUTE SPIKE disabled: %s", reason);
        }

        DestroyVulkanObjects(false);

        if (s.interopDevice) {
            s.interopDevice->Release();
            s.interopDevice = nullptr;
        }
        s.initialized = false;
        s.enabled = false;
        s.resourcesCreated = false;
    }

    bool QIInteropTexture(IDirect3DTexture9* texture, ID3D9VkInteropTexture** out) {
        *out = nullptr;
        if (!texture) return false;
        HRESULT hr = texture->QueryInterface(__uuidof(ID3D9VkInteropTexture), reinterpret_cast<void**>(out));
        return SUCCEEDED(hr) && *out != nullptr;
    }

    bool GetInteropImageInfo(IDirect3DTexture9* /*texture*/, ID3D9VkInteropTexture* interopTexture,
                             VkImage& image, VkImageLayout& layout, VkImageCreateInfo& info) {
        UINT32 queueFamilyIndices[4] = {};
        VkImageCreateInfo ci = {};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.pNext = nullptr;
        ci.queueFamilyIndexCount = 4;
        ci.pQueueFamilyIndices = queueFamilyIndices;

        VkImageLayout outLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImage outImage = VK_NULL_HANDLE;

        HRESULT hr = interopTexture->GetVulkanImageInfo(&outImage, &outLayout, &ci);
        if (FAILED(hr)) {
            Logger::Log("VK COMPUTE SPIKE: GetVulkanImageInfo failed (HRESULT 0x%08X)", (unsigned int)hr);
            return false;
        }

        image = outImage;
        layout = outLayout;
        info = ci;
        return true;
    }

    bool PrepareResources(IDirect3DTexture9* source, IDirect3DTexture9* destination) {
        ID3D9VkInteropTexture* srcInterop = nullptr;
        ID3D9VkInteropTexture* dstInterop = nullptr;
        if (!QIInteropTexture(source, &srcInterop)) {
            Logger::Log("VK COMPUTE SPIKE: source texture won't give ID3D9VkInteropTexture");
            return false;
        }
        if (!QIInteropTexture(destination, &dstInterop)) {
            srcInterop->Release();
            Logger::Log("VK COMPUTE SPIKE: destination texture won't give ID3D9VkInteropTexture");
            return false;
        }

        s.interopDevice->LockDevice();
        s.interopDevice->FlushRenderingCommands();

        VkImageLayout srcLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageLayout dstLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageCreateInfo srcInfo{};
        VkImageCreateInfo dstInfo{};
        VkImage srcImage = VK_NULL_HANDLE;
        VkImage dstImage = VK_NULL_HANDLE;

        bool ok = GetInteropImageInfo(source, srcInterop, srcImage, srcLayout, srcInfo) &&
                  GetInteropImageInfo(destination, dstInterop, dstImage, dstLayout, dstInfo);

        s.interopDevice->UnlockDevice();
        srcInterop->Release();
        dstInterop->Release();

        if (!ok)
            return false;

        s.sourceImage = srcImage;
        s.destinationImage = dstImage;
        s.sourceFormat = srcInfo.format;
        s.numGroupsX = (s.sourceWidth + 15) / 16;
        s.numGroupsY = (s.sourceHeight + 15) / 16;
        s.partialSumCount = s.numGroupsX * s.numGroupsY;

        Logger::Log("VK COMPUTE SPIKE: source image %llu format %d usage 0x%X, dest image %llu usage 0x%X",
                    (unsigned long long)srcImage, (int)srcInfo.format, srcInfo.usage,
                    (unsigned long long)dstImage, dstInfo.usage);

        if (!CreateVulkanObjects(s.sourceFormat, srcInfo.usage, dstInfo.usage))
            return false;

        s.resourcesCreated = true;
        return true;
    }

    bool RecordCommandBuffer(VkCommandBuffer cmd, UINT32 slot) {
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        VkResult beginResult = s.api.vkBeginCommandBuffer(cmd, &begin);
        if (beginResult != VK_SUCCESS) {
            Logger::Log("VK COMPUTE SPIKE: vkBeginCommandBuffer failed (VkResult %d)", (int)beginResult);
            return false;
        }

        if (s.timestampQueries) {
            s.api.vkCmdResetQueryPool(cmd, s.queryPool, slot * 2, 2);
            s.api.vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, s.queryPool, slot * 2);
        }

        s.api.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s.pipeline);
        s.api.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s.pipelineLayout,
                                      0, 1, &s.descriptorSet, 0, nullptr);

        PushConstants pc0{};
        pc0.width = s.sourceWidth;
        pc0.height = s.sourceHeight;
        pc0.numGroupsX = s.numGroupsX;
        pc0.numGroupsY = s.numGroupsY;
        pc0.phase = 0;
        s.api.vkCmdPushConstants(cmd, s.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc0), &pc0);
        s.api.vkCmdDispatch(cmd, s.numGroupsX, s.numGroupsY, 1);

        // Phase 0 wrote partial sums; phase 1 reads them.
        VkBufferMemoryBarrier partialBarrier{};
        partialBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        partialBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        partialBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        partialBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        partialBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        partialBarrier.buffer = s.partialSumsBuffer;
        partialBarrier.offset = 0;
        partialBarrier.size = VK_WHOLE_SIZE;
        s.api.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                                   0, nullptr, 1, &partialBarrier, 0, nullptr);

        PushConstants pc1{};
        pc1.width = s.sourceWidth;
        pc1.height = s.sourceHeight;
        pc1.numGroupsX = s.numGroupsX;
        pc1.numGroupsY = s.numGroupsY;
        pc1.phase = 1;
        s.api.vkCmdPushConstants(cmd, s.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc1), &pc1);
        s.api.vkCmdDispatch(cmd, 1, 1, 1);

        // Prepare output packed buffer for vkCmdCopyBufferToImage.
        VkBufferMemoryBarrier outputBarrier{};
        outputBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        outputBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        outputBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        outputBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        outputBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        outputBarrier.buffer = s.outputBuffer;
        outputBarrier.offset = 0;
        outputBarrier.size = VK_WHOLE_SIZE;
        s.api.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                   0, nullptr, 1, &outputBarrier, 0, nullptr);

        VkBufferImageCopy copyRegion{};
        copyRegion.bufferOffset = 0;
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageOffset.x = 0;
        copyRegion.imageOffset.y = 0;
        copyRegion.imageOffset.z = 0;
        copyRegion.imageExtent.width = 1;
        copyRegion.imageExtent.height = 1;
        copyRegion.imageExtent.depth = 1;

        s.api.vkCmdCopyBufferToImage(cmd, s.outputBuffer, s.destinationImage,
                                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        if (s.timestampQueries) {
            s.api.vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, s.queryPool, slot * 2 + 1);
        }

        VkResult endResult = s.api.vkEndCommandBuffer(cmd);
        if (endResult != VK_SUCCESS) {
            Logger::Log("VK COMPUTE SPIKE: vkEndCommandBuffer failed (VkResult %d)", (int)endResult);
            return false;
        }
        return true;
    }

    bool ReadTextureLuma(IDirect3DTexture9* texture, float& outLuma) {
        if (!texture || !s.interopDevice) return false;

        // WaitForResource only waits for writes; it may flush D3D9 commands. LockRect
        // may also wait. This is called once every 120 frames, not per frame, so the
        // stall is acceptable for the go/no-go measurement.
        s.interopDevice->WaitForResource(texture, D3DLOCK_READONLY);

        D3DLOCKED_RECT rect;
        HRESULT hr = texture->LockRect(0, &rect, nullptr, D3DLOCK_READONLY);
        if (FAILED(hr)) {
            Logger::Log("VK COMPUTE SPIKE: LockRect for readback failed (HRESULT 0x%08X)", (unsigned int)hr);
            return false;
        }

        // A16B16G16R16F is four half floats.
        const UINT16* halfs = static_cast<const UINT16*>(rect.pBits);
        outLuma = HalfToFloat(halfs[0]);
        texture->UnlockRect(0);
        return true;
    }

    bool LoadVulkanApi() {
        HMODULE vulkan = GetModuleHandleA("vulkan-1.dll");
        if (!vulkan) vulkan = LoadLibraryA("vulkan-1.dll");
        if (!vulkan) {
            Logger::Log("VK COMPUTE SPIKE: cannot find vulkan-1.dll");
            return false;
        }

        s.api.vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            GetProcAddress(vulkan, "vkGetInstanceProcAddr"));
        if (!s.api.vkGetInstanceProcAddr) {
            Logger::Log("VK COMPUTE SPIKE: cannot find vkGetInstanceProcAddr");
            return false;
        }

        s.interopDevice->GetVulkanHandles(&s.instance, &s.physicalDevice, &s.device);
        s.interopDevice->GetSubmissionQueue(&s.queue, &s.queueIndex, &s.queueFamilyIndex);

        if (s.instance == VK_NULL_HANDLE || s.device == VK_NULL_HANDLE || s.queue == VK_NULL_HANDLE) {
            Logger::Log("VK COMPUTE SPIKE: DXVK Vulkan handles are null");
            return false;
        }

        s.api.vkGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
            s.api.vkGetInstanceProcAddr(s.instance, "vkGetDeviceProcAddr"));
        if (!s.api.vkGetDeviceProcAddr) {
            Logger::Log("VK COMPUTE SPIKE: cannot find vkGetDeviceProcAddr");
            return false;
        }

#define LOAD_INSTANCE(fn) do { \
    s.api.fn = reinterpret_cast<PFN_##fn>(s.api.vkGetInstanceProcAddr(s.instance, #fn)); \
    if (!s.api.fn) { Logger::Log("VK COMPUTE SPIKE: missing instance function " #fn); return false; } \
} while (0)

#define LOAD_DEVICE(fn) do { \
    s.api.fn = reinterpret_cast<PFN_##fn>(s.api.vkGetDeviceProcAddr(s.device, #fn)); \
    if (!s.api.fn) { Logger::Log("VK COMPUTE SPIKE: missing device function " #fn); return false; } \
} while (0)

        LOAD_INSTANCE(vkGetPhysicalDeviceProperties);
        LOAD_INSTANCE(vkGetPhysicalDeviceFormatProperties);
        LOAD_INSTANCE(vkGetPhysicalDeviceMemoryProperties);

        LOAD_DEVICE(vkCreateCommandPool);
        LOAD_DEVICE(vkDestroyCommandPool);
        LOAD_DEVICE(vkAllocateCommandBuffers);
        LOAD_DEVICE(vkFreeCommandBuffers);
        LOAD_DEVICE(vkResetCommandBuffer);
        LOAD_DEVICE(vkBeginCommandBuffer);
        LOAD_DEVICE(vkEndCommandBuffer);
        LOAD_DEVICE(vkCmdDispatch);
        LOAD_DEVICE(vkCmdBindPipeline);
        LOAD_DEVICE(vkCmdBindDescriptorSets);
        LOAD_DEVICE(vkCmdPushConstants);
        LOAD_DEVICE(vkCmdPipelineBarrier);
        LOAD_DEVICE(vkCmdCopyBufferToImage);
        LOAD_DEVICE(vkCreateDescriptorSetLayout);
        LOAD_DEVICE(vkDestroyDescriptorSetLayout);
        LOAD_DEVICE(vkCreateDescriptorPool);
        LOAD_DEVICE(vkDestroyDescriptorPool);
        LOAD_DEVICE(vkAllocateDescriptorSets);
        LOAD_DEVICE(vkUpdateDescriptorSets);
        LOAD_DEVICE(vkCreatePipelineLayout);
        LOAD_DEVICE(vkDestroyPipelineLayout);
        LOAD_DEVICE(vkCreateComputePipelines);
        LOAD_DEVICE(vkDestroyPipeline);
        LOAD_DEVICE(vkCreateShaderModule);
        LOAD_DEVICE(vkDestroyShaderModule);
        LOAD_DEVICE(vkCreateBuffer);
        LOAD_DEVICE(vkDestroyBuffer);
        LOAD_DEVICE(vkAllocateMemory);
        LOAD_DEVICE(vkFreeMemory);
        LOAD_DEVICE(vkBindBufferMemory);
        LOAD_DEVICE(vkGetBufferMemoryRequirements);
        LOAD_DEVICE(vkCreateImageView);
        LOAD_DEVICE(vkDestroyImageView);
        LOAD_DEVICE(vkCreateSampler);
        LOAD_DEVICE(vkDestroySampler);
        LOAD_DEVICE(vkCreateFence);
        LOAD_DEVICE(vkDestroyFence);
        LOAD_DEVICE(vkWaitForFences);
        LOAD_DEVICE(vkResetFences);
        LOAD_DEVICE(vkCreateQueryPool);
        LOAD_DEVICE(vkDestroyQueryPool);
        LO
