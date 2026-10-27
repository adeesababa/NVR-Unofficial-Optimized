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

/*
 * 1.5-particles: passes with their own geometry (GPU particles, any instanced quads).
 *
 * A separate interface, queried from ID3D9NvrExt (QueryInterface with its GUID), so a d3d9.dll
 * without it simply returns E_NOINTERFACE and ID3D9NvrExt's own method table is untouched.
 *
 * The pipeline is a full-screen pipeline whose vertex shader is required and may read
 * resources of its own: storage buffers (read only) and uniform buffers, by slot like
 * every other binding (no samplers, no push block in the vertex stage). A slot may be
 * listed for both stages. The draw is DrawShifted's with a vertex and an instance count:
 * VertexCount vertices x InstanceCount instances as a triangle list, no vertex input
 * (gl_VertexIndex / gl_InstanceIndex), no depth buffer (a shader that needs the scene's
 * depth samples it), no culling. Same state save/restore as Draw.
 */
struct NVR_EXT_GEOMETRY_PIPELINE_DESC {
  NVR_EXT_GRAPHICS_PIPELINE_DESC Graphics;           // pVertexSpirv must be given
  UINT                        VertexBindingCount;
  const NVR_EXT_BINDING_DESC* pVertexBindings;       // STORAGE_BUFFER (Writes = FALSE) or UNIFORM_BUFFER
};

struct NVR_EXT_GEOMETRY_DRAW_DESC {
  NVR_EXT_DRAW_DESC           Draw;                  // bindings: the fragment's and the vertex stage's together
  UINT                        VertexCount;           // per instance
  UINT                        InstanceCount;
};

MIDL_INTERFACE("6e2b0f3a-9c14-4d57-8a41-b2f0c7a9d309")
ID3D9NvrExtGeometry : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE CreateGeometryPipeline(
    const NVR_EXT_GEOMETRY_PIPELINE_DESC* pDesc,
          ID3D9NvrExtPipeline**           ppPipeline) = 0;

  virtual HRESULT STDMETHODCALLTYPE DrawGeometry(
    const NVR_EXT_GEOMETRY_DRAW_DESC*     pDesc) = 0;
};

#ifndef _MSC_VER
__CRT_UUID_DECL(ID3D9NvrExtGeometry, 0x6e2b0f3a,0x9c14,0x4d57,0x8a,0x41,0xb2,0xf0,0xc7,0xa9,0xd3,0x09);
#endif

/*
 * HDR output (extension 1.5-hdr; its own interface, so the main vtable is untouched).
 *
 * Query ID3D9NvrExtHdr from the IDirect3DDevice9 (E_NOINTERFACE on native D3D9, stock DXVK
 * and older forks). With a mode set, DXVK creates the Vulkan swap chain in HDR10 (ST.2084,
 * 10-bit) or scRGB (FP16 linear) and sends HDR metadata; the game's D3D9 back buffer stays
 * as it is (8-bit, SDR). At present the SDR back buffer is decoded (sRGB curve or 2.2), put
 * at PaperWhiteNits and, wherever it still equals the picture NVR marked with MarkHdrScene
 * (so the HUD and menus drawn after NVR are left at paper white), multiplied by NVR's
 * per-pixel highlight gain (>= 1, clamped to PeakNits / PaperWhiteNits).
 * Frames without a MarkHdrScene call (menus, loading screens) are shown at paper white.
 */
enum NVR_EXT_HDR_MODE {
  NVR_EXT_HDR_OFF   = 0,
  NVR_EXT_HDR_HDR10 = 1,  // A2B10G10R10 + VK_COLOR_SPACE_HDR10_ST2084_EXT
  NVR_EXT_HDR_SCRGB = 2,  // R16G16B16A16_SFLOAT + VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT
};

enum NVR_EXT_HDR_FLAGS {
  NVR_EXT_HDR_FLAG_SHOW_HUD_MASK = 0x1,  // debug: tint the pixels taken as HUD/menu (shown without gain)
  NVR_EXT_HDR_FLAG_GAMMA22       = 0x2,  // decode the SDR picture with a pure 2.2 power instead of the sRGB curve
};

struct NVR_EXT_HDR_INFO {
  BOOL  SupportsHdr10;          // the window's surface offers HDR10 (some drivers list it with Windows HDR off too)
  BOOL  SupportsScRgb;
  FLOAT DisplayMinNits;         // from the monitor's EDID (0 when unknown)
  FLOAT DisplayMaxNits;
  FLOAT DisplayMaxFullFrameNits;
  UINT  ActiveMode;             // NVR_EXT_HDR_MODE in force
};

struct NVR_EXT_HDR_OUTPUT {
  UINT  Mode;                   // NVR_EXT_HDR_MODE
  FLOAT PaperWhiteNits;         // where SDR white (back buffer 1.0) lands, e.g. 200
  FLOAT PeakNits;               // gain ceiling (PeakNits / PaperWhiteNits) and HDR metadata max luminance / MaxCLL
  FLOAT MaxFrameAverageNits;    // HDR metadata MaxFALL (0 = PaperWhiteNits)
  FLOAT HudThreshold;           // per channel, 0..1: a back buffer pixel further than this from the marked picture is HUD
  UINT  Flags;                  // NVR_EXT_HDR_FLAGS
};

MIDL_INTERFACE("6e2b0f3a-9c14-4d57-8a41-b2f0c7a9d310")
ID3D9NvrExtHdr : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE GetHdrInfo(
          NVR_EXT_HDR_INFO*               pInfo) = 0;

  /**
   * Sets the output mode and its parameters (call again to change them; Mode OFF goes back
   * to DXVK's SDR swap chain). D3DERR_NOTAVAILABLE when the surface does not offer the colour
   * space: the output stays SDR. The swap chain is re-created at the next present.
   */
  virtual HRESULT STDMETHODCALLTYPE SetHdrOutput(
    const NVR_EXT_HDR_OUTPUT*             pOutput) = 0;

  /**
   * Marks this frame's finished picture before the HUD: pScene (a surface or texture of the
   * back buffer's size and format; usually the back buffer itself) is copied now, pGain (a
   * R16F/R32F/A16B16G16R16F texture or surface of the same size, red channel) is copied now.
   * Valid until the next present.
   */
  virtual HRESULT STDMETHODCALLTYPE MarkHdrScene(
          IUnknown*                       pScene,
          IUnknown*                       pGain) = 0;

  /** TEST ONLY: the next present also writes its HDR output, as scRGB, into a readback buffer. */
  virtual HRESULT STDMETHODCALLTYPE RequestHdrCapture() = 0;

  /**
   * TEST ONLY: waits for the GPU and copies the last capture (RGBA16F scRGB, 1.0 = 80 nits,
   * rows top to bottom, Width * 8 bytes each). pData null = sizes only. S_FALSE: no capture yet.
   */
  virtual HRESULT STDMETHODCALLTYPE ReadHdrCapture(
          UINT*                           pWidth,
          UINT*                           pHeight,
          void*                           pData,
          UINT                            Size) = 0;
};

/*
 * 1.5-debug: readback that never waits (debugging tools: pixel inspector, shader counters).
 *
 * A separate interface, queried from ID3D9NvrExt (QueryInterface with its GUID), so a d3d9.dll
 * without it simply returns E_NOINTERFACE and ID3D9NvrExt's own method table is untouched.
 *
 * A readback buffer lives in host-visible memory (TRANSFER_DST only). CopyToReadback records a
 * copy into it, in order with everything recorded before, and remembers where in the command
 * stream that copy sits. TryReadback then copies the bytes out if and only if that copy has run
 * on the GPU; otherwise it returns S_FALSE at once (no flush, no wait). Use a ring of a few
 * buffers and read each one a few frames after its copy: the normal case never stalls.
 * ReadBuffer (above) stays the waiting, test-only path.
 *
 * Also since 1.5-debug: buffers made with NVR_EXT_BUFFER_STORAGE are usable (read and write) by
 * the vertex and fragment stages of the full-screen and geometry passes too, with DXVK's barriers
 * covering those stages (a fragment shader can atomicAdd into a counter buffer).
 */
MIDL_INTERFACE("6e2b0f3a-9c14-4d57-8a41-b2f0c7a9d315")
ID3D9NvrExtReadback : public IUnknown {
  /** A host-visible buffer for CopyToReadback / TryReadback only (not bindable to a pass). */
  virtual HRESULT STDMETHODCALLTYPE CreateReadbackBuffer(
          UINT                            Size,
    const char*                           pDebugName,
          ID3D9NvrExtBuffer**             ppBuffer) = 0;

  /**
   * Records a copy of Size bytes (4-aligned) from pSrc (a buffer made with NVR_EXT_BUFFER_TRANSFER_SRC)
   * at SrcOffset into the readback buffer pDst at DstOffset. Any earlier copy into pDst that has not
   * been read is superseded (TryReadback then waits for this one).
   */
  virtual HRESULT STDMETHODCALLTYPE CopyToReadback(
          ID3D9NvrExtBuffer*              pDst,
          UINT                            DstOffset,
          ID3D9NvrExtBuffer*              pSrc,
          UINT                            SrcOffset,
          UINT                            Size) = 0;

  /**
   * S_OK: the last CopyToReadback into pBuffer has finished on the GPU; Size bytes at Offset are in pData.
   * S_FALSE: not finished yet (nothing copied; never waits). D3DERR_INVALIDCALL: not a readback buffer,
   * no copy recorded into it, or a bad range.
   */
  virtual HRESULT STDMETHODCALLTYPE TryReadback(
          ID3D9NvrExtBuffer*              pBuffer,
          UINT                            Offset,
          UINT                            Size,
          void*                           pData) = 0;
};

#ifndef _MSC_VER
__CRT_UUID_DECL(ID3D9NvrExtReadback, 0x6e2b0f3a,0x9c14,0x4d57,0x8a,0x41,0xb2,0xf0,0xc7,0xa9,0xd3,0x15);
#endif

#ifndef _MSC_VER
__CRT_UUID_DECL(ID3D9NvrExtHdr,      0x6e2b0f3a,0x9c14,0x4d57,0x8a,0x41,0xb2,0xf0,0xc7,0xa9,0xd3,0x10);
__CRT_UUID_DECL(ID3D9NvrExt,         0x6e2b0f3a,0x9c14,0x4d57,0x8a,0x41,0xb2,0xf0,0xc7,0xa9,0xd3,0x00);
__CRT_UUID_DECL(ID3D9NvrExtPipeline, 0x6e2b0f3a,0x9c14,0x4d57,0x8a,0x41,0xb2,0xf0,0xc7,0xa9,0xd3,0x01);
__CRT_UUID_DECL(ID3D9NvrExtBuffer,   0x6e2b0f3a,0x9c14,0x4d57,0x8a,0x41,0xb2,0xf0,0xc7,0xa9,0xd3,0x02);
__CRT_UUID_DECL(ID3D9NvrExtSampler,  0x6e2b0f3a,0x9c14,0x4d57,0x8a,0x41,0xb2,0xf0,0xc7,0xa9,0xd3,0x03);
#endif

/*
 * 1.7-dlss: GPU images and timeline semaphores shared with another process (ID3D9NvrExtShare).
 *
 * For a 64-bit helper process (NVR's DLSS helper, tools/nvr-dlss-helper in the NVR repository)
 * that works on the same GPU with its own Vulkan device: NVIDIA's DLSS runtime is 64-bit only,
 * the game and this d3d9.dll are 32-bit. Nothing NVIDIA-specific is in DXVK: this is plain
 * Vulkan external memory / external semaphores (VK_KHR_external_memory_win32 and
 * VK_KHR_external_semaphore_win32, which DXVK enables whenever the driver has them) and works
 * with any Vulkan helper.
 *
 * A separate interface, queried from ID3D9NvrExt (QueryInterface with its GUID), so a d3d9.dll
 * without it returns E_NOINTERFACE and ID3D9NvrExt's own method table is untouched.
 *
 * Shared images are DXVK images of their own (not D3D9 textures) with a dedicated, exportable
 * allocation, kept in VK_IMAGE_LAYOUT_GENERAL between uses. CopyToShared / CopyFromShared move
 * pixels between them and D3D9 textures in DXVK's command stream. Shared fences are exportable
 * timeline semaphores: SignalShared submits everything recorded so far with a signal (it flushes),
 * WaitShared makes the GPU wait in the command list being recorded (work recorded since the last
 * flush and everything after it, up to the next flush). No CPU waits anywhere. The importer must
 * create its VkImage exactly as GetInfo describes it and import the memory with the same size and
 * memory type (dedicated, handle type OPAQUE_WIN32).
 *
 * Usage per frame: CopyToShared (inputs) -> SignalShared(ready, N) -> [the other process waits
 * for N, works, signals done = N] -> WaitShared(done, N) -> CopyFromShared (output).
 */
enum NVR_EXT_SHARED_FORMAT {
  NVR_EXT_SHARED_RGBA16F = 0,  // VK_FORMAT_R16G16B16A16_SFLOAT
  NVR_EXT_SHARED_RG16F   = 1,  // VK_FORMAT_R16G16_SFLOAT
  NVR_EXT_SHARED_R32F    = 2,  // VK_FORMAT_R32_SFLOAT
  NVR_EXT_SHARED_RG32F   = 3,  // VK_FORMAT_R32G32_SFLOAT
  NVR_EXT_SHARED_RGBA8   = 4,  // VK_FORMAT_R8G8B8A8_UNORM
  NVR_EXT_SHARED_R16F    = 5,  // VK_FORMAT_R16_SFLOAT
};

struct NVR_EXT_SHARE_INFO {
  BOOL  Supported;          // external memory + external semaphores (Win32 handles) and timeline semaphores are there
  UINT  VendorId;           // the Vulkan device's (0x10DE NVIDIA, 0x1002 AMD, 0x8086 Intel)
  UINT  DeviceId;
  UINT  DriverVersion;      // VkPhysicalDeviceProperties::driverVersion (vendor encoding)
  UINT  ApiVersion;         // the Vulkan version of DXVK's device
  BYTE  DeviceUUID[16];     // VkPhysicalDeviceIDProperties: the importer picks the same GPU by these
  BYTE  DriverUUID[16];
  BYTE  DeviceLUID[8];
  BOOL  DeviceLUIDValid;
  char  DeviceName[256];
};

struct NVR_EXT_SHARED_IMAGE_DESC {
  UINT                  Width;
  UINT                  Height;
  NVR_EXT_SHARED_FORMAT Format;
  const char*           pDebugName;
};

/** Everything an importer needs to create the identical VkImage and import its memory. */
struct NVR_EXT_SHARED_IMAGE_INFO {
  UINT    VkFormat;         // VkFormat
  UINT    Width;
  UINT    Height;
  UINT    MipLevels;        // 1
  UINT    ArrayLayers;      // 1
  UINT    Samples;          // VK_SAMPLE_COUNT_1_BIT
  UINT    Usage;            // VkImageUsageFlags
  UINT    Flags;            // VkImageCreateFlags (0)
  UINT    Tiling;           // VkImageTiling (OPTIMAL)
  UINT    Layout;           // VkImageLayout it is in between uses (GENERAL)
  UINT    HandleType;       // VkExternalMemoryHandleTypeFlagBits (OPAQUE_WIN32)
  UINT    MemoryTypeIndex;  // of DXVK's device (the same GPU and driver: the same index in the importer)
  UINT64  AllocationSize;   // bytes of the dedicated allocation
  BOOL    Dedicated;        // TRUE: import with VkMemoryDedicatedAllocateInfo for the importer's image
};

/*
 * CreateHandle / CreateShared* failures say which step failed: NVR_EXT_HRESULT_FROM_VK(VkResult) for a Vulkan call
 * (the export), HRESULT_FROM_WIN32(error) for a Win32 call (the handle duplication), D3DERR_NOTAVAILABLE when the
 * object is not shared at all. The DXVK log has a line for each.
 */
#define NVR_EXT_FACILITY_VK 0x7E1
#define NVR_EXT_HRESULT_FROM_VK(vr) ((HRESULT)(0x80000000u | (0x7E1u << 16) | ((UINT)(-(INT)(vr)) & 0xFFFFu)))

MIDL_INTERFACE("6e2b0f3a-9c14-4d57-8a41-b2f0c7a9d327")
ID3D9NvrExtSharedImage : public IUnknown {
  virtual void STDMETHODCALLTYPE GetInfo(
          NVR_EXT_SHARED_IMAGE_INFO*      pInfo) = 0;

  /**
   * A new NT handle to the image's memory, owned by the caller (DuplicateHandle it into the other process, then CloseHandle).
   * Every call returns a duplicate of the one handle exported when the image was made (Vulkan allows one NT export).
   */
  virtual HRESULT STDMETHODCALLTYPE CreateHandle(
          HANDLE*                         pHandle) = 0;
};

MIDL_INTERFACE("6e2b0f3a-9c14-4d57-8a41-b2f0c7a9d328")
ID3D9NvrExtSharedFence : public IUnknown {
  /** A new NT handle to the timeline semaphore (VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT), owned by the caller (a duplicate, as above). */
  virtual HRESULT STDMETHODCALLTYPE CreateHandle(
          HANDLE*                         pHandle) = 0;

  /** The semaphore's current value (vkGetSemaphoreCounterValue). Any thread, never waits. */
  virtual HRESULT STDMETHODCALLTYPE GetCompletedValue(
          UINT64*                         pValue) = 0;

  /**
   * Signals the semaphore from the CPU now (vkSignalSemaphore), Value above the current one. Any thread.
   * For recovery only: when the other process died or hung, this releases GPU work waiting on it.
   */
  virtual HRESULT STDMETHODCALLTYPE SignalFromHost(
          UINT64                          Value) = 0;
};

MIDL_INTERFACE("6e2b0f3a-9c14-4d57-8a41-b2f0c7a9d326")
ID3D9NvrExtShare : public IUnknown {
  virtual void STDMETHODCALLTYPE GetShareInfo(
          NVR_EXT_SHARE_INFO*             pInfo) = 0;

  /** D3DERR_NOTAVAILABLE when the device cannot export images of that format and size. */
  virtual HRESULT STDMETHODCALLTYPE CreateSharedImage(
    const NVR_EXT_SHARED_IMAGE_DESC*      pDesc,
          ID3D9NvrExtSharedImage**        ppImage) = 0;

  /** An exportable timeline semaphore. D3DERR_NOTAVAILABLE without external semaphores. */
  virtual HRESULT STDMETHODCALLTYPE CreateSharedFence(
          UINT64                          InitialValue,
          ID3D9NvrExtSharedFence**        ppFence) = 0;

  /**
   * Records a copy of pSrcRect (null = the whole subresource) of a D3D9 texture or surface (mip 0, layer 0
   * of a texture) to (DstX, DstY) of the shared image. Same VkFormat: an image copy; otherwise a 1:1 blit
   * (format conversion, nearest).
   */
  virtual HRESULT STDMETHODCALLTYPE CopyToShared(
          ID3D9NvrExtSharedImage*         pDst,
          UINT                            DstX,
          UINT                            DstY,
          IUnknown*                       pSrc,
    const RECT*                           pSrcRect) = 0;

  /** The reverse: pSrcRect (null = whole) of the shared image to (DstX, DstY) of a D3D9 texture or surface. */
  virtual HRESULT STDMETHODCALLTYPE CopyFromShared(
          IUnknown*                       pDst,
          UINT                            DstX,
          UINT                            DstY,
          ID3D9NvrExtSharedImage*         pSrc,
    const RECT*                           pSrcRect) = 0;

  /** Records a signal of Value and submits everything recorded so far with it (a flush). */
  virtual HRESULT STDMETHODCALLTYPE SignalShared(
          ID3D9NvrExtSharedFence*         pFence,
          UINT64                          Value) = 0;

  /** Records a GPU wait for Value in the command list being recorded (see above). Never waits on the CPU. */
  virtual HRESULT STDMETHODCALLTYPE WaitShared(
          ID3D9NvrExtSharedFence*         pFence,
          UINT64                          Value) = 0;
};

#ifndef _MSC_VER
__CRT_UUID_DECL(ID3D9NvrExtShare,       0x6e2b0f3a,0x9c14,0x4d57,0x8a,0x41,0xb2,0xf0,0xc7,0xa9,0xd3,0x26);
__CRT_UUID_DECL(ID3D9NvrExtSharedImage, 0x6e2b0f3a,0x9c14,0x4d57,0x8a,0x41,0xb2,0xf0,0xc7,0xa9,0xd3,0x27);
__CRT_UUID_DECL(ID3D9NvrExtSharedFence, 0x6e2b0f3a,0x9c14,0x4d57,0x8a,0x41,0xb2,0xf0,0xc7,0xa9,0xd3,0x28);
#endif
