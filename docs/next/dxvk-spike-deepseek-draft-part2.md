```cpp
        LOAD_DEVICE(vkGetQueryPoolResults);
        LOAD_DEVICE(vkCmdWriteTimestamp);
        LOAD_DEVICE(vkCmdResetQueryPool);
        LOAD_DEVICE(vkQueueSubmit);
        LOAD_DEVICE(vkDeviceWaitIdle);

#undef LOAD_INSTANCE
#undef LOAD_DEVICE

        return true;
    }

    bool Initialize() {
        if (s.enabled || s.initialized)
            return true;

        if (TheSettingManager->GetSettingI("Main.Main.Performance", "VulkanComputeSpike") != 1)
            return false;

        HRESULT hr = TheRenderManager->device->QueryInterface(__uuidof(ID3D9VkInteropDevice),
                                                              reinterpret_cast<void**>(&s.interopDevice));
        if (FAILED(hr) || !s.interopDevice) {
            Logger::Log("VK COMPUTE SPIKE: D3D9 Vulkan interop device is not available (native D3D9); disabled.");
            return false;
        }

        if (!LoadVulkanApi()) {
            DisableInternal("could not load Vulkan entry points");
            return false;
        }

        s.api.vkGetPhysicalDeviceProperties(s.physicalDevice, &s.deviceProperties);
        s.timestampPeriod = static_cast<double>(s.deviceProperties.limits.timestampPeriod);
        s.timestampQueries = s.deviceProperties.limits.timestampValidBits != 0;
        if (!s.timestampQueries)
            Logger::Log("VK COMPUTE SPIKE: Vulkan timestamp queries are unavailable; GPU timing will not be reported.");

        // The scene texture is A16B16G16R16F. DXVK exposes it as R16G16B16A16_SFLOAT.
        // Check the format's sampled-image feature up front; if the GPU does not support
        // it through the optimal tiling DXVK uses, bail out before touching anything.
        VkFormatProperties formatProps{};
        s.api.vkGetPhysicalDeviceFormatProperties(s.physicalDevice, VK_FORMAT_R16G16B16A16_SFLOAT,
                                                  &formatProps);
        bool sampledOk = (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
        Logger::Log("VK COMPUTE SPIKE: VK_FORMAT_R16G16B16A16_SFLOAT optimal sampled-image feature: %s.",
                    sampledOk ? "yes" : "NO");
        if (!sampledOk) {
            DisableInternal("source format lacks VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT");
            return false;
        }

        // Create the 1x1 destination texture through the normal D3D9 path so existing
        // D3D9 shaders can read it just like any other NVR texture.
        TheTextureManager->InitTexture("NVR_VkAvgLuma", &s.destinationTexture, &s.destinationSurface,
                                       1, 1, D3DFMT_A16B16G16R16F);
        if (!s.destinationTexture) {
            DisableInternal("could not create NVR_VkAvgLuma texture");
            return false;
        }

        s.initialized = true;
        s.enabled = true;
        Logger::Log("VK COMPUTE SPIKE: initialized. DXVK queue family %u, queue index %u.",
                    s.queueFamilyIndex, s.queueIndex);
        return true;
    }

    void Shutdown() {
        if (s.device != VK_NULL_HANDLE && s.api.vkDeviceWaitIdle)
            s.api.vkDeviceWaitIdle(s.device);

        DestroyVulkanObjects(false); // device is already idle from the wait above

        if (s.interopDevice) {
            s.interopDevice->Release();
            s.interopDevice = nullptr;
        }
        if (s.destinationTexture) {
            s.destinationTexture->Release();
            s.destinationTexture = nullptr;
        }
        if (s.destinationSurface) {
            s.destinationSurface->Release();
            s.destinationSurface = nullptr;
        }

        s.initialized = false;
        s.enabled = false;
        s.failed = false;
        s.resourcesCreated = false;
        s.sourceImage = VK_NULL_HANDLE;
        s.destinationImage = VK_NULL_HANDLE;
    }

    bool Enabled() {
        return s.enabled && !s.failed;
    }

    void Run(IDirect3DTexture9* source, IDirect3DTexture9* avgLumaTexture) {
        if (!Enabled())
            return;

        if (!s.resourcesCreated) {
            if (!PrepareResources(source, s.destinationTexture))
                DisableInternal("could not prepare Vulkan resources");
            return;
        }

        // Never block the frame. The ring is four submissions deep; if the oldest
        // command buffer is still executing, skip this frame's compute pass instead
        // of waiting for the GPU. The measured average will come from successful frames.
        const UINT32 slotIndex = s.frameIndex % kMaxFramesInFlight;
        FrameSlot& slot = s.frames[slotIndex];

        if (slot.everSubmitted) {
            VkResult waitResult = s.api.vkWaitForFences(s.device, 1, &slot.fence, VK_TRUE, 0);
            if (waitResult == VK_TIMEOUT)
                return;
            if (waitResult != VK_SUCCESS) {
                DisableInternal("vkWaitForFences failed", waitResult);
                return;
            }

            if (s.timestampQueries) {
                UINT64 timestamps[2] = {};
                VkResult qr = s.api.vkGetQueryPoolResults(s.device, s.queryPool, slotIndex * 2, 2,
                                                          sizeof(timestamps), timestamps, sizeof(UINT64),
                                                          VK_QUERY_RESULT_64_BIT);
                if (qr == VK_SUCCESS) {
                    const UINT64 mask = (1ULL << s.deviceProperties.limits.timestampValidBits) - 1ULL;
                    UINT64 delta = (timestamps[1] & mask) - (timestamps[0] & mask);
                    s.totalGpuTicks += static_cast<double>(delta);
                    s.gpuSamples++;
                } else if (qr != VK_NOT_READY) {
                    DisableInternal("vkGetQueryPoolResults failed", qr);
                    return;
                }
            }

            s.api.vkResetFences(s.device, 1, &slot.fence);
        }

        if (s.api.vkResetCommandBuffer(slot.cmd, 0) != VK_SUCCESS) {
            DisableInternal("vkResetCommandBuffer failed");
            return;
        }

        if (!RecordCommandBuffer(slot.cmd, slotIndex)) {
            DisableInternal("failed to record Vulkan command buffer");
            return;
        }

        ID3D9VkInteropTexture* srcInterop = nullptr;
        ID3D9VkInteropTexture* dstInterop = nullptr;
        if (!QIInteropTexture(source, &srcInterop) || !QIInteropTexture(s.destinationTexture, &dstInterop)) {
            if (srcInterop) srcInterop->Release();
            if (dstInterop) dstInterop->Release();
            DisableInternal("source or destination texture is not a DXVK interop texture");
            return;
        }

        LARGE_INTEGER freq, t0, t1;
        QueryPerformanceFrequency(&freq);

        // DXVK interop contract:
        //  1. LockDevice prevents other threads from issuing D3D9 commands while we
        //     are inspecting or manipulating backing Vulkan images.
        //  2. FlushRenderingCommands ensures the D3D9 commands that rendered the
        //     source scene texture have been submitted to the Vulkan queue before we
        //     submit our own command buffer.
        //  3. GetVulkanImageInfo returns the layouts DXVK currently expects after
        //     its own pending work.
        s.interopDevice->LockDevice();

        QueryPerformanceCounter(&t0);
        s.interopDevice->FlushRenderingCommands();
        QueryPerformanceCounter(&t1);
        s.flushSeconds += SecondsBetween(t0, t1, freq);

        VkImageLayout srcInitialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageLayout dstInitialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageCreateInfo srcInfo{};
        VkImageCreateInfo dstInfo{};
        VkImage srcImage = VK_NULL_HANDLE;
        VkImage dstImage = VK_NULL_HANDLE;

        bool imageInfoOk =
            GetInteropImageInfo(source, srcInterop, srcImage, srcInitialLayout, srcInfo) &&
            GetInteropImageInfo(s.destinationTexture, dstInterop, dstImage, dstInitialLayout, dstInfo);
        if (!imageInfoOk) {
            s.interopDevice->UnlockDevice();
            srcInterop->Release();
            dstInterop->Release();
            DisableInternal("GetVulkanImageInfo failed");
            return;
        }

        if (srcImage != s.sourceImage || dstImage != s.destinationImage) {
            s.interopDevice->UnlockDevice();
            srcInterop->Release();
            dstInterop->Release();
            DisableInternal("Vulkan images changed unexpectedly");
            return;
        }

        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel = 0;
        range.levelCount = 1;
        range.baseArrayLayer = 0;
        range.layerCount = 1;

        // Move the images into the layouts our command buffer needs.
        s.interopDevice->TransitionTextureLayout(srcInterop, &range, srcInitialLayout,
                                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        s.interopDevice->TransitionTextureLayout(dstInterop, &range, dstInitialLayout,
                                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        // The layout transitions above are recorded on DXVK's internal command stream.
        // Flush again so they are actually submitted before our external queue submit.
        QueryPerformanceCounter(&t0);
        s.interopDevice->FlushRenderingCommands();
        QueryPerformanceCounter(&t1);
        s.flushSeconds += SecondsBetween(t0, t1, freq);

        QueryPerformanceCounter(&t0);
        s.interopDevice->LockSubmissionQueue();
        QueryPerformanceCounter(&t1);
        s.lockSeconds += SecondsBetween(t0, t1, freq);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &slot.cmd;

        QueryPerformanceCounter(&t0);
        VkResult submitResult = s.api.vkQueueSubmit(s.queue, 1, &submitInfo, slot.fence);
        QueryPerformanceCounter(&t1);
        s.submitSeconds += SecondsBetween(t0, t1, freq);

        s.interopDevice->ReleaseSubmissionQueue();

        if (submitResult != VK_SUCCESS) {
            s.interopDevice->UnlockDevice();
            srcInterop->Release();
            dstInterop->Release();
            DisableInternal("vkQueueSubmit failed", submitResult);
            return;
        }
        slot.everSubmitted = true;

        // Our command buffer is now queued on DXVK's own queue. Record transitions
        // back to DXVK's expected layouts. These commands will be submitted by DXVK
        // after our external submission, so ordering is preserved.
        s.interopDevice->TransitionTextureLayout(srcInterop, &range,
                                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                 srcInitialLayout);
        s.interopDevice->TransitionTextureLayout(dstInterop, &range,
                                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                 dstInitialLayout);

        s.interopDevice->UnlockDevice();
        srcInterop->Release();
        dstInterop->Release();

        s.frameIndex++;
        s.samples++;

        if (s.samples >= kLogSampleCount) {
            float computeLuma = 0.0f;
            float d3d9Luma = 0.0f;
            if (ReadTextureLuma(s.destinationTexture, computeLuma) &&
                ReadTextureLuma(avgLumaTexture, d3d9Luma)) {
                double avgCpu = s.totalCpuSeconds / static_cast<double>(s.samples) * 1000.0;
                double avgFlush = s.flushSeconds / static_cast<double>(s.samples) * 1000.0;
                double avgLock = s.lockSeconds / static_cast<double>(s.samples) * 1000.0;
                double avgSubmit = s.submitSeconds / static_cast<double>(s.samples) * 1000.0;
                double avgGpu = s.gpuSamples > 0
                    ? s.totalGpuTicks * s.timestampPeriod / static_cast<double>(s.gpuSamples) * 1000.0
                    : 0.0;

                Logger::Log("VK COMPUTE: avg gpu %.3f ms, cpu %.3f ms (flush %.3f, locks %.3f, submit %.3f), "
                            "compute luma %.5f vs D3D9 %.5f",
                            avgGpu, avgCpu, avgFlush, avgLock, avgSubmit, computeLuma, d3d9Luma);
            }

            s.samples = 0;
            s.totalCpuSeconds = 0.0;
            s.flushSeconds = 0.0;
            s.lockSeconds = 0.0;
            s.submitSeconds = 0.0;
            s.totalGpuTicks = 0.0;
            s.gpuSamples = 0;
        }
    }
}
```

---

### `Compute/avgluma.comp`

```glsl
#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D TESR_RenderedBuffer;

layout(std430, binding = 1) buffer PartialSums {
    float partialSums[];
};

layout(std430, binding = 2) buffer OutputBuffer {
    uvec2 words;
} outputBuffer;

layout(push_constant) uniform PushConstants {
    uint width;
    uint height;
    uint numGroupsX;
    uint numGroupsY;
    uint phase;
} pc;

shared float sharedLuma[256];

vec3 linearize(vec3 c)
{
    vec3 lo = c / 12.92;
    vec3 hi = pow(abs((c + 0.055) / 1.055), vec3(2.4));
    // Returns lo where c <= 0.04045, hi otherwise.
    return mix(hi, lo, lessThanEqual(c, vec3(0.04045)));
}

float luma(vec3 c)
{
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

void main()
{
    uint tileIndex = gl_WorkGroupID.y * pc.numGroupsX + gl_WorkGroupID.x;

    if (pc.phase == 0u) {
        // Phase 0: each 16x16 workgroup reduces its tile to one partial sum.
        float localLuma = 0.0;
        ivec2 pos = ivec2(gl_GlobalInvocationID.xy);

        if (pos.x < int(pc.width) && pos.y < int(pc.height)) {
            vec3 color = texelFetch(TESR_RenderedBuffer, pos, 0).rgb;
            localLuma = luma(linearize(color));
        }

        sharedLuma[gl_LocalInvocationIndex] = localLuma;
        barrier();

        for (uint s = 128u; s > 0u; s >>= 1u) {
            if (gl_LocalInvocationIndex < s)
                sharedLuma[gl_LocalInvocationIndex] += sharedLuma[gl_LocalInvocationIndex + s];
            barrier();
        }

        if (gl_LocalInvocationIndex == 0u)
            partialSums[tileIndex] = sharedLuma[0];
    } else {
        // Phase 1: a single workgroup reduces all partial sums and writes the
        // exact average to the output buffer as two packed half-float words.
        float sum = 0.0;
        for (uint i = gl_LocalInvocationIndex; i < pc.numGroupsX * pc.numGroupsY; i += 256u)
            sum += partialSums[i];

        sharedLuma[gl_LocalInvocationIndex] = sum;
        barrier();

        for (uint s = 128u; s > 0u; s >>= 1u) {
            if (gl_LocalInvocationIndex < s)
                sharedLuma[gl_LocalInvocationIndex] += sharedLuma[gl_LocalInvocationIndex + s];
            barrier();
        }

        if (gl_LocalInvocationIndex == 0u) {
            float avg = sharedLuma[0] / float(pc.width * pc.height);
            // R16G16B16A16_SFLOAT: word 0 is R (low half) and G (high half),
            // word 1 is B (low half) and A (high half).
            outputBuffer.words.x = packHalf2x16(vec2(avg, avg));
            outputBuffer.words.y = packHalf2x16(vec2(1.0, 1.0));
        }
    }
}
```

---

### Unified diffs

#### 1. Initialization in `RenderManager.cpp`

Add `#include "VkComputeSpike.h"` near the other includes, then after the existing DXVK detection and initial D3D9 setup:

```diff
--- a/src/render/RenderManager.cpp
+++ b/src/render/RenderManager.cpp
@@
 #include "ShaderWarmUp2.h"
+#include "VkComputeSpike.h"
 
@@
 	BackBuffer = CreateHDRRenderTarget();
 	ShaderWarmUp2::Install(device);	// UNOFFICIAL ShaderWarmUp2 (off unless [Main.Main.Performance] ShaderWarmUp2 is 1 or 2): before any shader is created
+	VkComputeSpike::Initialize(device);	// UNOFFICIAL Vulkan compute spike for AvgLuma, DXVK only
 }
```

#### 2. Per-frame run in `ShaderManager.cpp`

Add `#include "VkComputeSpike.h"` and insert the spike run directly after the existing Average luma block:

```diff
--- a/src/render/ShaderManager.cpp
+++ b/src/render/ShaderManager.cpp
@@
 #include "AvgLuma.h"
+#include "VkComputeSpike.h"
 
@@
 		// calculate average luma for use by shaders
 		if (avglumaRequired) {
 			GpuProfileScope gpuLuma(avgLumaTimer, Device);
 			RenderEffectToRT(Effects.AvgLuma->Textures.AvgLumaSurface, Effects.AvgLuma, NULL);
 			Device->SetRenderTarget(0, RenderTarget); 	// restore device used for effects
+			if (VkComputeSpike::Enabled()) {
+				VkComputeSpike::Run(TheTextureManager->RenderedTexture,
+				                    Effects.AvgLuma->Textures.AvgLumaTexture);
+			}
 		}
```

#### 3. Shutdown in `RenderManager.cpp`

Add the shutdown call just before the D3D9 device is released in the RenderManager destructor/shutdown path:

```diff
--- a/src/render/RenderManager.cpp
+++ b/src/render/RenderManager.cpp
@@
 	// The depth resolve path follows what the D3D9 layer offers...
 
+	VkComputeSpike::Shutdown();
 	if (device) {
 		device->Release();
 		device = nullptr;
 	}
```

#### 4. Defaults TOML

Add under the existing `[Main.Main.Performance]` section:

```diff
--- a/Data/Configs/NewVegasReloaded/settings.toml
+++ b/Data/Configs/NewVegasReloaded/settings.toml
@@
 [Main.Main.Performance]
 ShaderWarmUp2 = 0
+VulkanComputeSpike = 0
```

#### 5. `vcxproj` include path

Add the Vulkan-Headers vendor directory to both `AdditionalIncludeDirectories` lines:

```diff
--- a/src/NewVegasReloaded.vcxproj
+++ b/src/NewVegasReloaded.vcxproj
@@
-      <AdditionalIncludeDirectories>$(SolutionDir)\Core;$(SolutionDir)lib\imgui;$(SolutionDir)lib\imgui\backends;$(SolutionDir)lib\reshade;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
+      <AdditionalIncludeDirectories>$(SolutionDir)\Core;$(SolutionDir)lib\imgui;$(SolutionDir)lib\imgui\backends;$(SolutionDir)lib\reshade;$(SolutionDir)lib\Vulkan-Headers\include;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
@@
-      <AdditionalIncludeDirectories>$(SolutionDir)\Core;$(SolutionDir)lib\imgui;$(SolutionDir)lib\imgui\backends;$(SolutionDir)lib\reshade;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
+      <AdditionalIncludeDirectories>$(SolutionDir)\Core;$(SolutionDir)lib\imgui;$(SolutionDir)lib\imgui\backends;$(SolutionDir)lib\reshade;$(SolutionDir)lib\Vulkan-Headers\include;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
```

Add `src/core/VkComputeSpike.cpp` to the project’s source list.

---

### Risks and diagnostic signs

1. **DXVK interop layout/submit ordering**  
   If DXVK does not flush after `TransitionTextureLayout` before `LockSubmissionQueue`, our command buffer could sample the source in the wrong layout, producing black or corrupted luma. The log will show compute luma near 0 or NaN while D3D9 luma is sane. The CPU/GPU timings will also likely be suspiciously low.

2. **DXVK queue submission synchronization**  
   DXVK’s `FlushRenderingCommands` must ensure its previous submissions are ordered before our external submit. If it merely flushes the command stream without submitting, our command buffer could race with DXVK’s own work on the same queue. The symptom would be intermittent flickering or GPU hangs. This spike avoids waiting, so a race may appear as a skipped/failed frame or corrupted `NVR_VkAvgLuma`.

3. **Vulkan non-dispatchable handle size on 32-bit**  
   All Vulkan handles are kept in their native `Vk*` types, which are `uint64_t` on x86 by the Vulkan headers. The code never casts them to pointers. If DXVK’s interop header or loader accidentally used 32-bit handles, object creation would fail immediately; the log will show VkResult errors from `vkCreate*`.

4. **Format support**  
   DXVK maps `D3DFMT_A16B16G16R16F` to `VK_FORMAT_R16G16B16A16_SFLOAT`, but the sampled-image feature may be absent on some mobile/old Vulkan devices. The initialization logs the feature and disables the spike with that exact line.

5. **Query pool timestamp validity**  
   Some drivers report `timestampValidBits > 0` but still return zero or garbage. The spike guards with `timestampValidBits`, but if the GPU timestamps are broken, the reported GPU average will be nonsense (0 ms or extremely large). The CPU timings remain reliable.

6. **Readback stall every 120 successful frames**  
   The code calls D3D9 `LockRect` / `WaitForResource` on the 1x1 textures only when logging. That can introduce a small hitch once every 120 successful submissions. The log line “cpu Y ms” includes only the pre-submission interop overhead, not this readback. If the hitch is visible, the frame time graph will show a periodic spike.

7. **DXVK interop device lifetime**  
   The spike holds `ID3D9VkInteropDevice` until shutdown. If the game recreates its D3D9 device (alt-tab/resolution reset), that interface may become invalid. The log will show `GetVulkanImageInfo` or `vkQueueSubmit` failures and the spike disables itself. Full device-reset handling is not implemented for this spike.
