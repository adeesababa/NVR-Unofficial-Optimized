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
#define NVR_EXT_VERSION_MINOR 6

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

/** Depth and stencil compare functions (since 1.6; the values are Vulkan's, D3DCMPFUNC minus 1). */
enum NVR_EXT_COMPARE {
  NVR_EXT_COMPARE_NEVER         = 0,
  NVR_EXT_COMPARE_LESS          = 1,
  NVR_EXT_COMPARE_EQUAL         = 2,
  NVR_EXT_COMPARE_LESS_EQUAL    = 3,
  NVR_EXT_COMPARE_GREATER       = 4,
  NVR_EXT_COMPARE_NOT_EQUAL     = 5,
  NVR_EXT_COMPARE_GREATER_EQUAL = 6,
  NVR_EXT_COMPARE_ALWAYS        = 7,
};

/** Stencil operations (since 1.6; the values are Vulkan's, D3DSTENCILOP minus 1). */
enum NVR_EXT_STENCIL_OP {
  NVR_EXT_STENCIL_KEEP       = 0,
  NVR_EXT_STENCIL_ZERO       = 1,
  NVR_EXT_STENCIL_REPLACE    = 2,
  NVR_EXT_STENCIL_INCR_CLAMP = 3,
  NVR_EXT_STENCIL_DECR_CLAMP = 4,
  NVR_EXT_STENCIL_INVERT     = 5,
  NVR_EXT_STENCIL_INCR_WRAP  = 6,
  NVR_EXT_STENCIL_DECR_WRAP  = 7,
};

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

/**
 * A depth-stencil attachment for a full-screen pass, with its tests (since 1.6, DrawDepthStencil): e.g. a pass that runs
 * only where an earlier pass left a stencil mark, as a D3D9 technique with StencilEnable does. Clear it with D3D9's Clear
 * (it stays in order with the passes). Both faces use the same stencil state; the triangle's depth is Depth.
 */
struct NVR_EXT_DEPTH_STENCIL {
  IUnknown*           pResource;          // a D3D9 depth-stencil surface (or depth texture) of the targets' size, single-sample
  FLOAT               Depth;              // the full-screen triangle's depth, 0-1
  BOOL                DepthTest;
  BOOL                DepthWrite;
  NVR_EXT_COMPARE     DepthCompare;
  BOOL                StencilTest;
  NVR_EXT_COMPARE     StencilCompare;
  NVR_EXT_STENCIL_OP  StencilPassOp;      // the stencil and depth tests passed
  NVR_EXT_STENCIL_OP  StencilFailOp;
  NVR_EXT_STENCIL_OP  StencilDepthFailOp;
  UINT                StencilReference;
  UINT                StencilReadMask;
  UINT                StencilWriteMask;
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

/** Profiling counters since SetProfiling(TRUE) (since 1.5). Ticks: the CPU's time stamp counter (Tsc, the same clock). */
struct NVR_EXT_PROFILE {
  UINT64  TimeUs;             // steady clock now (microseconds)
  UINT64  Tsc;                // time stamp counter now (with TimeUs: its rate)
  UINT64  Frames;             // presents
  UINT64  TimedFrames;        // frames whose D3D9 calls were timed (every 8th)
  UINT64  ApiTicks;           // the game thread's outermost D3D9 calls on timed frames (ticks; waits included)
  UINT64  ApiCalls;           // those calls
  UINT64  OverheadTicks100;   // the timer's own cost per timed call, x 100 (subtract ApiCalls x this / 100)
  UINT64  PresentWaitTicks;   // the frame-latency wait in Present, every frame (ticks)
  UINT64  TimedWaitTicks;     // that wait on the timed frames (it is inside ApiTicks)
  UINT64  CsSyncUs;           // the game thread waiting for DXVK's command thread (microseconds)
  UINT64  CsIdleUs;           // DXVK's command thread idle
  UINT64  GpuSyncUs;          // the game thread waiting for the GPU
  UINT64  GpuIdleUs;          // the GPU idle
  UINT64  Draws;
  UINT64  RenderPasses;
  UINT64  Barriers;
  UINT64  Submits;
  UINT64  CsChunks;
  UINT64  GraphicsPipelines;
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

  /** Profiling on / off (since 1.5): on, every 8th frame times the game thread's D3D9 calls. */
  virtual HRESULT STDMETHODCALLTYPE SetProfiling(
          BOOL                            Enable) = 0;

  /** The profiling counters (since 1.5); cumulative, read twice and subtract. */
  virtual HRESULT STDMETHODCALLTYPE GetProfile(
          NVR_EXT_PROFILE*                pProfile) = 0;

  /** Draw (or DrawShifted, a shift of 0 for none) with a depth-stencil attachment and its tests (since 1.6). */
  virtual HRESULT STDMETHODCALLTYPE DrawDepthStencil(
    const NVR_EXT_DRAW_DESC*              pDesc,
    const NVR_EXT_DEPTH_STENCIL*          pDepthStencil,
          FLOAT                           ShiftX,
          FLOAT                           ShiftY) = 0;
};

/*
 * Tessellation displacement ("1.7-displace", fork patch 0021). A separate interface, so ID3D9NvrExt and
 * its version stay as they are: query it from the device (or from ID3D9NvrExt) with
 * QueryInterface(__uuidof(ID3D9NvrExtTess)); a d3d9.dll without it answers E_NOINTERFACE.
 *
 * NVR registers, before the D3D9 shaders are created (as for 1.2's replacements):
 *  - a vertex shader with a tessellation control shader (TCS, 3 control points) under a family number;
 *  - pixel shaders of the same family, each with its tessellation evaluation shader (TES), one per
 *    output winding.
 * A draw whose vertex and pixel shaders were registered under the same family, drawn as a triangle list
 * (indexed or not) while SetTessState enables it, runs VS -> TCS -> TES -> PS from a patch list of 3
 * control points. A triangle strip from DrawPrimitive / DrawIndexedPrimitive is drawn as three patch lists
 * over its index windows from its first, second and third index (together every strip triangle once):
 * the TCS's p[7] (ControlParams[7], DXVK's own) is then the window 0 / 1 / 2, and -1 for a triangle list;
 * patch gl_PrimitiveID of window k is strip triangle k + 3 * gl_PrimitiveID, and the TCS must swap the first
 * two control points of the odd ones (their winding is reversed in a strip). Every other draw is untouched
 * (fans, strips from the UP draws: counted in the stats, drawn as they are).
 * The vertex shader is still DXVK's conversion of the D3D9 bytecode: the TCS reads its outputs, the TES
 * writes what the pixel shader reads, at the D3D9 semantic locations (0 NORMAL0, 1..8 TEXCOORD0..7,
 * 9/10 COLOR0/1, 11 FOG; vec4 each). At the first such draw of a pair DXVK checks that the TCS reads only
 * what the vertex shader writes and that the TES writes everything the pixel shader reads from the vertex
 * shader; a pair that fails is logged once and always drawn untessellated.
 *
 * GLSL contract (glslangValidator -V --target-env vulkan1.1):
 *   TCS: layout(vertices = 3) out;
 *        layout(push_constant) uniform Push { float p[8]; };           // NVR_EXT_TESS_STATE.ControlParams[0..6]; p[7] the strip window
 *   TES: layout(triangles, ..., cw) in for Winding 0 (keeps the input triangles' winding: Vulkan's tessellation
 *        domain origin is upper left), ccw for Winding 1. May declare:
 *        layout(set = 0, binding = 0) uniform sampler s_samplers[];                  // DXVK's sampler heap
 *        layout(set = 1, binding = i) uniform texture2D t_i;                          // pixel shader sampler s_i, every bit of TextureMask
 *        layout(set = 2, binding = 0, std140) uniform VsConsts { vec4 c[256]; };      // the vertex shader's float constants (compacted)
 *        layout(set = 2, binding = 1, std140) uniform ClipPlanes { vec4 plane[6]; };  // D3D9 user clip planes (clip space; 0 = off)
 *        layout(push_constant) uniform Push {
 *          uint  samplerIndex[4]; // heap index of pixel shader sampler i (i < 8): bits 16 * (i & 1) of [i / 2]
 *          uint  vsIndex;         // byte k: the compacted index of VsFloatRegisters[k] in VsConsts.c (0xFF: not used by the VS)
 *          uint  psBits;          // bit 4 * j + k: component k of pixel shader register PsFloatRegisters[j] is > 0 (read per draw)
 *          float evalParams[4];   // NVR_EXT_TESS_STATE.EvalParams
 *        };
 *        The TES writes gl_Position (and should write gl_ClipDistance[i] = dot(gl_Position, plane[i])).
 */
struct NVR_EXT_TESS_VS_DESC {
  const void*                 pBytecode;            // the D3D9 vertex shader
  UINT                        BytecodeSize;
  UINT                        Family;               // nonzero
  const void*                 pControlSpirv;        // the TCS
  UINT                        ControlSpirvSize;
  UINT                        VsFloatRegisters[4];  // vertex shader constant registers the TES reads (0xFFFFFFFF: none)
  const char*                 pDebugName;
};

struct NVR_EXT_TESS_PS_DESC {
  const void*                 pBytecode;            // the D3D9 pixel shader
  UINT                        BytecodeSize;
  UINT                        Family;
  const void*                 pEvalSpirv[2];        // the TES for Winding 0 and 1 ([1] may be null: [0] for both)
  UINT                        EvalSpirvSize[2];
  UINT                        TextureMask;          // pixel shader samplers s0..s7 the TES reads; each must be one the pixel shader uses
  UINT                        PsFloatRegisters[2];  // pixel shader registers whose components' signs go to psBits (0xFFFFFFFF: none)
  const char*                 pDebugName;
};

struct NVR_EXT_TESS_STATE {
  BOOL                        Enable;
  UINT                        Winding;              // 0 or 1: which TES of each pixel shader
  UINT                        Wireframe;            // 1: tessellated draws are drawn as lines (debug view)
  FLOAT                       ControlParams[8];     // the TCS's push data ([7] is replaced by DXVK: the strip window, -1 for lists)
  FLOAT                       EvalParams[4];        // the TES's push data (after its 24 bytes of indices)
};

struct NVR_EXT_TESS_STATS {
  UINT64                      TessellatedDraws;     // draws that ran with tessellation (a strip counts once)
  UINT64                      OtherTopologyDraws;   // family draws left as they are: fans, strips of the UP draws, other types
  UINT64                      Bindings;             // times the tessellation shaders were (re)bound
  UINT                        VertexShaders;        // registered shaders that were created
  UINT                        PixelShaders;
  UINT                        RefusedPairs;         // shaders the interface check turned away (logged)
};

MIDL_INTERFACE("6e2b0f3a-9c14-4d57-8a41-b2f0c7a9d321")
ID3D9NvrExtTess : public IUnknown {
  /** Registers a vertex shader and its TCS. D3DERR_INVALIDCALL when the bytecode or SPIR-V is unusable (logged). */
  virtual HRESULT STDMETHODCALLTYPE RegisterTessVertexShader(
    const NVR_EXT_TESS_VS_DESC*           pDesc) = 0;

  /** Registers a pixel shader and its TES. */
  virtual HRESULT STDMETHODCALLTYPE RegisterTessPixelShader(
    const NVR_EXT_TESS_PS_DESC*           pDesc) = 0;

  /** Switches tessellation on or off and sets its parameters; applies from the next draw. Call it once a frame. */
  virtual HRESULT STDMETHODCALLTYPE SetTessState(
    const NVR_EXT_TESS_STATE*             pState) = 0;

  /** Cumulative counters (read twice and subtract). */
  virtual HRESULT STDMETHODCALLTYPE GetTessStats(
          NVR_EXT_TESS_STATS*             pStats) = 0;
};

#ifndef _MSC_VER
__CRT_UUID_DECL(ID3D9NvrExtTess,     0x6e2b0f3a,0x9c14,0x4d57,0x8a,0x41,0xb2,0xf0,0xc7,0xa9,0xd3,0x21);
__CRT_UUID_DECL(ID3D9NvrExt,         0x6e2b0f3a,0x9c14,0x4d57,0x8a,0x41,0xb2,0xf0,0xc7,0xa9,0xd3,0x00);
__CRT_UUID_DECL(ID3D9NvrExtPipeline, 0x6e2b0f3a,0x9c14,0x4d57,0x8a,0x41,0xb2,0xf0,0xc7,0xa9,0xd3,0x01);
__CRT_UUID_DECL(ID3D9NvrExtBuffer,   0x6e2b0f3a,0x9c14,0x4d57,0x8a,0x41,0xb2,0xf0,0xc7,0xa9,0xd3,0x02);
__CRT_UUID_DECL(ID3D9NvrExtSampler,  0x6e2b0f3a,0x9c14,0x4d57,0x8a,0x41,0xb2,0xf0,0xc7,0xa9,0xd3,0x03);
#endif
