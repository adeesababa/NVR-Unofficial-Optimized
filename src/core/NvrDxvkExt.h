#pragma once
// NVR copy of the NVR DXVK fork's src/d3d9/d3d9_nvr_ext_api.h (tools/nvr-dxvk). Keep both files identical.

/*
 * NVR extension for DXVK's D3D9 device ("NVR DXVK").
 *
 * This header is the whole public contract between New Vegas Reloaded (NVR) and
 * an NVR-built DXVK d3d9.dll. It is kept free of Vulkan and DXVK types so that
 * NVR can include it as is (MSVC, 32-bit) next to <d3d9.h>. A copy of this file
 * lives in the NVR repository; keep both identical.
 *
 * How it works: everything an NVR call records goes through DXVK's own command
 * stream (the "dxvk-cs" thread) and so runs in order with the D3D9 work before
 * and after it. DXVK handles image layouts, barriers and resource lifetimes.
 * No CPU/GPU wait is involved unless a method says so.
 *
 * Usage: query ID3D9NvrExt from the IDirect3DDevice9 with QueryInterface.
 * Stock DXVK and native D3D9 return E_NOINTERFACE; callers fall back to their
 * D3D9 path.
 *
 * Resource slots: a pipeline's bindings name a Slot (0..NVR_EXT_MAX_SLOTS-1)
 * per binding type family. Images, buffers and samplers each have their own
 * slot space. A dispatch binds resources to those slots. The slots are
 * independent of anything D3D9 binds, so a dispatch never disturbs the game's
 * or NVR's D3D9 state.
 *
 * Push constants: the shader's push_constant block starts at offset 0 and may
 * hold up to NVR_EXT_MAX_PUSH_BYTES. Sampler bindings take their sampler-heap
 * index from a uint member of that block (PushOffset). The low 16 bits hold the
 * index; mask with 0xFFFF in the shader. The host side fills those dwords in,
 * the push data passed to Dispatch is copied over the whole block first.
 *
 * Shader conventions (GLSL, compiled with glslangValidator -V --target-env vulkan1.1):
 *   layout(set = S, binding = B) uniform sampler s_samplers[];   // sampler heap, if samplers are used
 *   layout(set = S, binding = B) uniform texture2D  t_src;       // NVR_EXT_BIND_SAMPLED_IMAGE
 *   layout(set = S, binding = B, rgba16f) uniform image2D i_dst; // NVR_EXT_BIND_STORAGE_IMAGE
 *   layout(set = S, binding = B, std430) buffer Foo { ... };    // NVR_EXT_BIND_STORAGE_BUFFER
 *   layout(set = S, binding = B, std140) uniform Bar { ... };   // NVR_EXT_BIND_UNIFORM_BUFFER
 *   layout(push_constant) uniform Push { ...; uint sampler_index; ... };
 * The set/binding numbers are the shader's own; DXVK remaps them. Use any
 * values, but each (set, binding) pair must be unique and listed in the
 * pipeline description.
 */

#include <d3d9.h>
#include <unknwn.h>

#define NVR_EXT_VERSION_MAJOR 1
#define NVR_EXT_VERSION_MINOR 4

#define NVR_EXT_MAX_SLOTS       32
#define NVR_EXT_MAX_BINDINGS    32
#define NVR_EXT_MAX_PUSH_BYTES  240

enum NVR_EXT_BIND_TYPE {
  NVR_EXT_BIND_SAMPLED_IMAGE  = 0,  // texture2D/3D/Cube in the shader; any D3D9 texture or surface
  NVR_EXT_BIND_STORAGE_IMAGE  = 1,  // image2D etc.; D3D9 render-target textures (DXVK adds storage usage once)
  NVR_EXT_BIND_STORAGE_BUFFER = 2,  // buffer block; an ID3D9NvrExtBuffer created with NVR_EXT_BUFFER_STORAGE
  NVR_EXT_BIND_UNIFORM_BUFFER = 3,  // uniform block; an ID3D9NvrExtBuffer created with NVR_EXT_BUFFER_UNIFORM
  NVR_EXT_BIND_SAMPLER        = 4,  // one entry of the sampler heap, index via push data
};

enum NVR_EXT_VIEW_TYPE {
  NVR_EXT_VIEW_2D       = 0,
  NVR_EXT_VIEW_3D       = 1,
  NVR_EXT_VIEW_CUBE     = 2,
  NVR_EXT_VIEW_2D_ARRAY = 3,
};

enum NVR_EXT_BUFFER_FLAGS {
  NVR_EXT_BUFFER_STORAGE      = 0x1,
  NVR_EXT_BUFFER_UNIFORM      = 0x2,
  NVR_EXT_BUFFER_TRANSFER_SRC = 0x4,  // CopyBufferToTexture source, CopyBuffer source, ReadBuffer source
  NVR_EXT_BUFFER_TRANSFER_DST = 0x8,  // UpdateBuffer, ClearBuffer, CopyTextureToBuffer, CopyBuffer destination
};

enum NVR_EXT_FILTER {
  NVR_EXT_FILTER_NEAREST = 0,
  NVR_EXT_FILTER_LINEAR  = 1,
  NVR_EXT_FILTER_LINEAR_MIPMAPS = 2,  // (1.3) linear min/mag and between mip levels, all mips (file textures with mips)
  NVR_EXT_FILTER_ANISOTROPIC    = 3,  // (1.3) as LINEAR_MIPMAPS with 16x anisotropic filtering (D3D9's ANISOTROPIC)
};

enum NVR_EXT_ADDRESS {
  NVR_EXT_ADDRESS_CLAMP  = 0,
  NVR_EXT_ADDRESS_WRAP   = 1,
  NVR_EXT_ADDRESS_MIRROR = 2,
  NVR_EXT_ADDRESS_BORDER = 3,  // transparent black
};

enum NVR_EXT_BLEND {
  NVR_EXT_BLEND_NONE     = 0,  // write
  NVR_EXT_BLEND_ALPHA    = 1,  // src.a * src + (1 - src.a) * dst
  NVR_EXT_BLEND_ADD      = 2,  // src + dst
  NVR_EXT_BLEND_MULTIPLY = 3,  // src * dst
};

#define NVR_EXT_MAX_TARGETS 4

/** One descriptor the SPIR-V declares. */
struct NVR_EXT_BINDING_DESC {
  UINT              Set;        // as in the SPIR-V
  UINT              Binding;    // as in the SPIR-V
  NVR_EXT_BIND_TYPE Type;
  UINT              Slot;       // resource slot a dispatch binds to (0..NVR_EXT_MAX_SLOTS-1)
  NVR_EXT_VIEW_TYPE ViewType;   // images only
  BOOL              Writes;     // storage image/buffer: the shader writes it
  UINT              PushOffset; // samplers only: byte offset of the uint index in the push block (4-aligned)
};

struct NVR_EXT_COMPUTE_PIPELINE_DESC {
  const void*                 pSpirv;         // SPIR-V words
  UINT                        SpirvSize;      // bytes, multiple of 4
  UINT                        BindingCount;
  const NVR_EXT_BINDING_DESC* pBindings;
  UINT                        PushConstantSize; // size of the shader's push_constant block, 4-aligned, <= NVR_EXT_MAX_PUSH_BYTES; 0 = none
  BOOL                        UsesSamplerHeap;  // the shader declares the sampler heap
  UINT                        SamplerHeapSet;
  UINT                        SamplerHeapBinding;
  const char*                 pDebugName;       // optional
};

struct NVR_EXT_BUFFER_DESC {
  UINT        Size;      // bytes
  UINT        Flags;     // NVR_EXT_BUFFER_FLAGS
  const char* pDebugName;
};

struct NVR_EXT_SAMPLER_DESC {
  NVR_EXT_FILTER  Filter;   // min and mag; mips are not filtered (NVR's buffers have none)
  NVR_EXT_ADDRESS Address;  // U, V and W
};

/** A resource bound for one dispatch. */
struct NVR_EXT_RESOURCE_BINDING {
  UINT              Slot;
  NVR_EXT_BIND_TYPE Type;
  IUnknown*         pResource;  // IDirect3DBaseTexture9*, IDirect3DSurface9*, ID3D9NvrExtBuffer* or ID3D9NvrExtSampler*
  UINT              MipLevel;   // textures: first mip of the view (surfaces: ignored, the surface's own)
  UINT              Offset;     // buffers: byte offset
  UINT              Size;       // buffers: byte size, 0 = to the end
  BOOL              Srgb;       // sampled images: sRGB view of a sRGB-capable format
};

struct NVR_EXT_DISPATCH_DESC {
  struct ID3D9NvrExtPipeline*     pPipeline;
  UINT                            BindingCount;
  const NVR_EXT_RESOURCE_BINDING* pBindings;
  const void*                     pPushData;     // PushDataSize bytes, copied at offset 0 of the push block
  UINT                            PushDataSize;
  UINT                            GroupsX;
  UINT                            GroupsY;
  UINT                            GroupsZ;
};

/**
 * A full-screen graphics pass: a fragment shader over a triangle that covers the
 * target(s). The vertex shader is built in (location 0 input in the fragment
 * shader: vec2 uv, (0,0) top left) unless one is given. Bindings are the
 * fragment stage's, same types and rules as for compute. The push block is the
 * fragment shader's, offset 0. Color outputs: location 0..ColorTargetCount-1.
 */
struct NVR_EXT_GRAPHICS_PIPELINE_DESC {
  const void*                 pVertexSpirv;     // optional
  UINT                        VertexSpirvSize;
  const void*                 pFragmentSpirv;
  UINT                        FragmentSpirvSize;
  UINT                        BindingCount;
  const NVR_EXT_BINDING_DESC* pBindings;
  UINT                        PushConstantSize;
  BOOL                        UsesSamplerHeap;
  UINT                        SamplerHeapSet;
  UINT                        SamplerHeapBinding;
  UINT                        ColorTargetCount; // 1..NVR_EXT_MAX_TARGETS
  NVR_EXT_BLEND               Blend;            // same for every target
  UINT                        ColorWriteMask;   // bits R=1 G=2 B=4 A=8; 0 = all
  const char*                 pDebugName;
};

struct NVR_EXT_TARGET {
  IUnknown* pResource;  // IDirect3DSurface9* or IDirect3DBaseTexture9* (render-target usage)
  UINT      MipLevel;   // textures only
  UINT      Layer;      // cube faces / array layers, textures only
  BOOL      Srgb;       // write through the sRGB view of a sRGB-capable format
};

struct NVR_EXT_DRAW_DESC {
  struct ID3D9NvrExtPipeline*     pPipeline;
  UINT                            TargetCount;      // = the pipeline's ColorTargetCount; all the same size
  NVR_EXT_TARGET                  Targets[NVR_EXT_MAX_TARGETS];
  INT                             ViewportX;        // 0,0,0,0 = the whole target
  INT                             ViewportY;
  UINT                            ViewportWidth;
  UINT                            ViewportHeight;
  UINT                            BindingCount;
  const NVR_EXT_RESOURCE_BINDING* pBindings;
  const void*                     pPushData;
  UINT                            PushDataSize;
};

/**
 * A SPIR-V replacement for one of the game's (or NVR's own) D3D9 pixel shaders (since 1.2).
 *
 * Registered before the D3D9 shader object is created (NVR: right before its CreatePixelShader
 * for that shader). When any CreatePixelShader call later presents the same bytecode, DXVK builds
 * the shader from this SPIR-V instead of converting the bytecode; everything the game sets for the
 * shader (constants, textures, sampler states, render states) keeps flowing. The original
 * bytecode's analysis (which samplers and render targets it uses) stays in force, so the
 * replacement must not read D3D9 samplers the original did not use.
 *
 * GLSL contract (fragment shader, compiled with glslangValidator -V --target-env vulkan1.1):
 *   layout(set = 0, binding = 0) uniform sampler s_samplers[];           // DXVK's sampler heap
 *   layout(set = 1, binding = i) uniform texture2D t_i;                   // D3D9 sampler s_i (the original's type: 2D / cube / 3D)
 *   layout(set = 2, binding = 6, std140) uniform Consts { vec4 c[N]; };   // the float constants, ascending register order
 *                                                                        // (N = bits set in FloatRegisterMask, or the original's set)
 *   layout(set = 3, binding = 0, scalar) uniform SpecConsts { ... };      // D3D9's spec data (alpha test, fog, sampler types; see the
 *                                                                        // fork's d3d9_fixed_function_common.glsl); optional
 *   layout(set = 4, binding = j) ...;                                     // NVR's own resources (pExtra[j]), bound with SetReplacementResources
 *   layout(push_constant) uniform Push {
 *     uint packedFogColorAndAlphaRef; float fogDistanceScale; float fogDistanceEnd; float fogDensity;   // D3D9's shared data, offset 0
 *     layout(offset = 32) uint d3d9SamplerIndices[8];   // heap index of D3D9 sampler i: bits 16*(i&1).. of [i/2]
 *     uint extraSamplerIndices[4];                       // heap index of extra sampler k (pExtra entries of type sampler), same packing
 *   };
 *   Inputs by D3D9 semantic: location 0 NORMAL0, 1..8 TEXCOORD0..7, 9 COLOR0, 10 COLOR1, 11 FOG; outputs location 0..3 = oC0..oC3.
 *   The alpha test and fixed-function fog that DXVK adds to a D3D9 pixel shader are NOT added here: a
 *   replacement that needs them does them itself from the spec data and the shared push data.
 */
struct NVR_EXT_SHADER_REPLACEMENT_DESC {
  const void*                 pBytecode;            // the D3D9 pixel shader bytecode this replaces
  UINT                        BytecodeSize;         // bytes (may be larger than the shader; the shader's own length is used)
  const void*                 pSpirv;
  UINT                        SpirvSize;
  UINT                        TextureMask;          // D3D9 samplers s0..s15 the replacement declares (set 1, binding i)
  UINT                        FloatRegisterMask[7]; // c0..c223 the replacement reads; all zero = the original's own set
  UINT                        ExtraCount;           // NVR's own resources: set 4, binding j; Slot in the extension's slot space
  const NVR_EXT_BINDING_DESC* pExtra;               // (sampled image, uniform buffer, read-only storage buffer, sampler; PushOffset ignored)
  const char*                 pDebugName;
};

struct NVR_EXT_INFO {
  UINT  ExtVersionMajor;
  UINT  ExtVersionMinor;
  char  DxvkVersion[32];     // DXVK's own version string of this build
  UINT  MaxPushBytes;
  UINT  MaxSlots;
  UINT  SubgroupSize;        // the GPU's subgroup size (0 if unknown)
  UINT  MaxWorkgroupInvocations;
};

MIDL_INTERFACE("6e2b0f3a-9c14-4d57-8a41-b2f0c7a9d301")
ID3D9NvrExtPipeline : public IUnknown {
};

MIDL_INTERFACE("6e2b0f3a-9c14-4d57-8a41-b2f0c7a9d302")
ID3D9NvrExtBuffer : public IUnknown {
  virtual UINT STDMETHODCALLTYPE GetSize() = 0;
};

MIDL_INTERFACE("6e2b0f3a-9c14-4d57-8a41-b2f0c7a9d303")
ID3D9NvrExtSampler : public IUnknown {
};

/**
 * \brief NVR extension on the D3D9 device
 *
 * All methods are called from the thread that uses the D3D9 device (the game's
 * render thread), like any other device method. Objects created here are owned
 * by the caller (Release them) and are not affected by a D3D9 device Reset.
 */
MIDL_INTERFACE("6e2b0f3a-9c14-4d57-8a41-b2f0c7a9d300")
ID3D9NvrExt : public IUnknown {
  /** Version and limits of this build. */
  virtual void STDMETHODCALLTYPE GetInfo(
          NVR_EXT_INFO*                   pInfo) = 0;

  /**
   * Creates a compute pipeline from SPIR-V. Fails (D3DERR_INVALIDCALL) when the
   * description is inconsistent; the DXVK log says why. The pipeline is compiled
   * on first use (one-time stall on the CS thread).
   */
  virtual HRESULT STDMETHODCALLTYPE CreateComputePipeline(
    const NVR_EXT_COMPUTE_PIPELINE_DESC*  pDesc,
          ID3D9NvrExtPipeline**           ppPipeline) = 0;

  /** Creates a full-screen graphics pipeline (since 1.1). */
  virtual HRESULT STDMETHODCALLTYPE CreateGraphicsPipeline(
    const NVR_EXT_GRAPHICS_PIPELINE_DESC* pDesc,
          ID3D9NvrExtPipeline**           ppPipeline) = 0;

  /**
   * Records a full-screen pass (since 1.1): the pipeline's fragment shader over
   * the given targets, after everything the D3D9 device recorded so far. The
   * device's own state (render targets, viewport, shaders, blend, depth ...) is
   * untouched afterwards. A target must not also be bound as an input, and must
   * not be the device's current render target.
   */
  virtual HRESULT STDMETHODCALLTYPE Draw(
    const NVR_EXT_DRAW_DESC*              pDesc) = 0;

  /** Registers a pixel shader replacement (since 1.2). D3DERR_INVALIDCALL when the bytecode or description is unusable. */
  virtual HRESULT STDMETHODCALLTYPE RegisterShaderReplacement(
    const NVR_EXT_SHADER_REPLACEMENT_DESC* pDesc) = 0;

  /**
   * Binds NVR's own resources for the replacements' extra slots (since 1.2): sampled images, buffers and
   * samplers by Slot, as every replacement's pExtra names them. They stay bound until set again.
   */
  virtual HRESULT STDMETHODCALLTYPE SetReplacementResources(
          UINT                            Count,
    const NVR_EXT_RESOURCE_BINDING*       pBindings) = 0;

  /** Creates a device-local buffer. Contents are undefined until written. */
  virtual HRESULT STDMETHODCALLTYPE CreateBuffer(
    const NVR_EXT_BUFFER_DESC*            pDesc,
          ID3D9NvrExtBuffer**             ppBuffer) = 0;

  /** Creates a sampler. Equal descriptions share one sampler-heap entry. */
  virtual HRESULT STDMETHODCALLTYPE CreateSampler(
    const NVR_EXT_SAMPLER_DESC*           pDesc,
          ID3D9NvrExtSampler**            ppSampler) = 0;

  /**
   * Records a compute dispatch after everything the D3D9 device recorded so far.
   * D3D9 textures bound here may be render targets of earlier passes; DXVK
   * orders and transitions them. The dispatch must not bind a texture that is
   * currently set as a render target on the device.
   */
  virtual HRESULT STDMETHODCALLTYPE Dispatch(
    const NVR_EXT_DISPATCH_DESC*          pDesc) = 0;

  /** Writes Size bytes at Offset (staged through DXVK, no wait). Size <= 64 KiB per call. */
  virtual HRESULT STDMETHODCALLTYPE UpdateBuffer(
          ID3D9NvrExtBuffer*              pBuffer,
          UINT                            Offset,
          UINT                            Size,
    const void*                           pData) = 0;

  /** Fills Size bytes (4-aligned) at Offset with a 32-bit value. Size 0 = to the end. */
  virtual HRESULT STDMETHODCALLTYPE ClearBuffer(
          ID3D9NvrExtBuffer*              pBuffer,
          UINT                            Offset,
          UINT                            Size,
          UINT                            Value) = 0;

  virtual HRESULT STDMETHODCALLTYPE CopyBuffer(
          ID3D9NvrExtBuffer*              pDst,
          UINT                            DstOffset,
          ID3D9NvrExtBuffer*              pSrc,
          UINT                            SrcOffset,
          UINT                            Size) = 0;

  /**
   * Copies tightly packed texels in the texture's own format from the buffer
   * into one whole subresource (mip MipLevel, face/layer Layer) of a D3D9
   * texture, or into a surface (then MipLevel and Layer are ignored).
   */
  virtual HRESULT STDMETHODCALLTYPE CopyBufferToTexture(
          IUnknown*                       pTexture,
          UINT                            MipLevel,
          UINT                            Layer,
          ID3D9NvrExtBuffer*              pSrc,
          UINT                            SrcOffset) = 0;

  /** The reverse of CopyBufferToTexture. */
  virtual HRESULT STDMETHODCALLTYPE CopyTextureToBuffer(
          ID3D9NvrExtBuffer*              pDst,
          UINT                            DstOffset,
          IUnknown*                       pTexture,
          UINT                            MipLevel,
          UINT                            Layer) = 0;

  /**
   * TEST ONLY: copies Size bytes of a buffer to pData, waiting for the GPU to
   * finish everything recorded so far. This stalls the CPU for a whole frame's
   * GPU work; use it for checks, never per frame in play.
   */
  virtual HRESULT STDMETHODCALLTYPE ReadBuffer(
          ID3D9NvrExtBuffer*              pBuffer,
          UINT                            Offset,
          UINT                            Size,
          void*                           pData) = 0;

  /**
   * As Draw, with the viewport moved by (ShiftX, ShiftY) pixels (since 1.4; fractions, -1 < shift < 1).
   * The scissor stays the viewport rectangle, so the same pixels are written; only the interpolated
   * location 0 texture coordinate moves. A D3D9 full-screen quad whose half-pixel uv offset was made
   * for a size S, drawn into a viewport of width W, reads at x / W + 0.5 / S; the shift 0.5 - 0.5 * W / S
   * gives the built-in coordinate the same values (0 when W = S).
   */
  virtual HRESULT STDMETHODCALLTYPE DrawShifted(
    const NVR_EXT_DRAW_DESC*              pDesc,
          FLOAT                           ShiftX,
          FLOAT                           ShiftY) = 0;
};

#ifndef _MSC_VER
__CRT_UUID_DECL(ID3D9NvrExt,         0x6e2b0f3a,0x9c14,0x4d57,0x8a,0x41,0xb2,0xf0,0xc7,0xa9,0xd3,0x00);
__CRT_UUID_DECL(ID3D9NvrExtPipeline, 0x6e2b0f3a,0x9c14,0x4d57,0x8a,0x41,0xb2,0xf0,0xc7,0xa9,0xd3,0x01);
__CRT_UUID_DECL(ID3D9NvrExtBuffer,   0x6e2b0f3a,0x9c14,0x4d57,0x8a,0x41,0xb2,0xf0,0xc7,0xa9,0xd3,0x02);
__CRT_UUID_DECL(ID3D9NvrExtSampler,  0x6e2b0f3a,0x9c14,0x4d57,0x8a,0x41,0xb2,0xf0,0xc7,0xa9,0xd3,0x03);
#endif
