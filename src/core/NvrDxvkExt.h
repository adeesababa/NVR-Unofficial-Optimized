#pragma once


#include <d3d9.h>
#include <unknwn.h>

#define NVR_EXT_VERSION_MAJOR 1
#define NVR_EXT_VERSION_MINOR 6

#define NVR_EXT_MAX_SLOTS       64
#define NVR_EXT_MAX_BINDINGS    96
#define NVR_EXT_MAX_PUSH_BYTES  240

enum NVR_EXT_BIND_TYPE {
  NVR_EXT_BIND_SAMPLED_IMAGE  = 0,
  NVR_EXT_BIND_STORAGE_IMAGE  = 1,
  NVR_EXT_BIND_STORAGE_BUFFER = 2,
  NVR_EXT_BIND_UNIFORM_BUFFER = 3,
  NVR_EXT_BIND_SAMPLER        = 4,
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
  NVR_EXT_BUFFER_TRANSFER_SRC = 0x4,
  NVR_EXT_BUFFER_TRANSFER_DST = 0x8,
  NVR_EXT_BUFFER_INDIRECT     = 0x10,
};

enum NVR_EXT_FILTER {
  NVR_EXT_FILTER_NEAREST = 0,
  NVR_EXT_FILTER_LINEAR  = 1,
  NVR_EXT_FILTER_LINEAR_MIPMAPS = 2,
  NVR_EXT_FILTER_ANISOTROPIC    = 3,
};

enum NVR_EXT_ADDRESS {
  NVR_EXT_ADDRESS_CLAMP  = 0,
  NVR_EXT_ADDRESS_WRAP   = 1,
  NVR_EXT_ADDRESS_MIRROR = 2,
  NVR_EXT_ADDRESS_BORDER = 3,
};

enum NVR_EXT_BLEND {
  NVR_EXT_BLEND_NONE     = 0,
  NVR_EXT_BLEND_ALPHA    = 1,
  NVR_EXT_BLEND_ADD      = 2,
  NVR_EXT_BLEND_MULTIPLY = 3,
};

#define NVR_EXT_MAX_TARGETS 4

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

struct NVR_EXT_BINDING_DESC {
  UINT              Set;
  UINT              Binding;
  NVR_EXT_BIND_TYPE Type;
  UINT              Slot;
  NVR_EXT_VIEW_TYPE ViewType;
  BOOL              Writes;
  UINT              PushOffset;
};

struct NVR_EXT_COMPUTE_PIPELINE_DESC {
  const void*                 pSpirv;
  UINT                        SpirvSize;
  UINT                        BindingCount;
  const NVR_EXT_BINDING_DESC* pBindings;
  UINT                        PushConstantSize;
  BOOL                        UsesSamplerHeap;
  UINT                        SamplerHeapSet;
  UINT                        SamplerHeapBinding;
  const char*                 pDebugName;
};

struct NVR_EXT_BUFFER_DESC {
  UINT        Size;
  UINT        Flags;
  const char* pDebugName;
};

struct NVR_EXT_SAMPLER_DESC {
  NVR_EXT_FILTER  Filter;
  NVR_EXT_ADDRESS Address;
};

struct NVR_EXT_RESOURCE_BINDING {
  UINT              Slot;
  NVR_EXT_BIND_TYPE Type;
  IUnknown*         pResource;
  UINT              MipLevel;
  UINT              Offset;
  UINT              Size;
  BOOL              Srgb;
};

struct NVR_EXT_DISPATCH_DESC {
  struct ID3D9NvrExtPipeline*     pPipeline;
  UINT                            BindingCount;
  const NVR_EXT_RESOURCE_BINDING* pBindings;
  const void*                     pPushData;
  UINT                            PushDataSize;
  UINT                            GroupsX;
  UINT                            GroupsY;
  UINT                            GroupsZ;
};

struct NVR_EXT_GRAPHICS_PIPELINE_DESC {
  const void*                 pVertexSpirv;
  UINT                        VertexSpirvSize;
  const void*                 pFragmentSpirv;
  UINT                        FragmentSpirvSize;
  UINT                        BindingCount;
  const NVR_EXT_BINDING_DESC* pBindings;
  UINT                        PushConstantSize;
  BOOL                        UsesSamplerHeap;
  UINT                        SamplerHeapSet;
  UINT                        SamplerHeapBinding;
  UINT                        ColorTargetCount;
  NVR_EXT_BLEND               Blend;
  UINT                        ColorWriteMask;
  const char*                 pDebugName;
};

struct NVR_EXT_TARGET {
  IUnknown* pResource;
  UINT      MipLevel;
  UINT      Layer;
  BOOL      Srgb;
};

struct NVR_EXT_DEPTH_STENCIL {
  IUnknown*           pResource;
  FLOAT               Depth;
  BOOL                DepthTest;
  BOOL                DepthWrite;
  NVR_EXT_COMPARE     DepthCompare;
  BOOL                StencilTest;
  NVR_EXT_COMPARE     StencilCompare;
  NVR_EXT_STENCIL_OP  StencilPassOp;
  NVR_EXT_STENCIL_OP  StencilFailOp;
  NVR_EXT_STENCIL_OP  StencilDepthFailOp;
  UINT                StencilReference;
  UINT                StencilReadMask;
  UINT                StencilWriteMask;
};

struct NVR_EXT_DRAW_DESC {
  struct ID3D9NvrExtPipeline*     pPipeline;
  UINT                            TargetCount;
  NVR_EXT_TARGET                  Targets[NVR_EXT_MAX_TARGETS];
  INT                             ViewportX;
  INT                             ViewportY;
  UINT                            ViewportWidth;
  UINT                            ViewportHeight;
  UINT                            BindingCount;
  const NVR_EXT_RESOURCE_BINDING* pBindings;
  const void*                     pPushData;
  UINT                            PushDataSize;
};

struct NVR_EXT_SHADER_REPLACEMENT_DESC {
  const void*                 pBytecode;
  UINT                        BytecodeSize;
  const void*                 pSpirv;
  UINT                        SpirvSize;
  UINT                        TextureMask;
  UINT                        FloatRegisterMask[7];
  UINT                        ExtraCount;
  const NVR_EXT_BINDING_DESC* pExtra;
  const char*                 pDebugName;
};

struct NVR_EXT_PROFILE {
  UINT64  TimeUs;
  UINT64  Tsc;
  UINT64  Frames;
  UINT64  TimedFrames;
  UINT64  ApiTicks;
  UINT64  ApiCalls;
  UINT64  OverheadTicks100;
  UINT64  PresentWaitTicks;
  UINT64  TimedWaitTicks;
  UINT64  CsSyncUs;
  UINT64  CsIdleUs;
  UINT64  GpuSyncUs;
  UINT64  GpuIdleUs;
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
  char  DxvkVersion[32];
  UINT  MaxPushBytes;
  UINT  MaxSlots;
  UINT  SubgroupSize;
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

MIDL_INTERFACE("6e2b0f3a-9c14-4d57-8a41-b2f0c7a9d300")
ID3D9NvrExt : public IUnknown {
  virtual void STDMETHODCALLTYPE GetInfo(
          NVR_EXT_INFO*                   pInfo) = 0;

  virtual HRESULT STDMETHODCALLTYPE CreateComputePipeline(
    const NVR_EXT_COMPUTE_PIPELINE_DESC*  pDesc,
          ID3D9NvrExtPipeline**           ppPipeline) = 0;

  virtual HRESULT STDMETHODCALLTYPE CreateGraphicsPipeline(
    const NVR_EXT_GRAPHICS_PIPELINE_DESC* pDesc,
          ID3D9NvrExtPipeline**           ppPipeline) = 0;

  virtual HRESULT STDMETHODCALLTYPE Draw(
    const NVR_EXT_DRAW_DESC*              pDesc) = 0;

  virtual HRESULT STDMETHODCALLTYPE RegisterShaderReplacement(
    const NVR_EXT_SHADER_REPLACEMENT_DESC* pDesc) = 0;

  virtual HRESULT STDMETHODCALLTYPE SetReplacementResources(
          UINT                            Count,
    const NVR_EXT_RESOURCE_BINDING*       pBindings) = 0;

  virtual HRESULT STDMETHODCALLTYPE CreateBuffer(
    const NVR_EXT_BUFFER_DESC*            pDesc,
          ID3D9NvrExtBuffer**             ppBuffer) = 0;

  virtual HRESULT STDMETHODCALLTYPE CreateSampler(
    const NVR_EXT_SAMPLER_DESC*           pDesc,
          ID3D9NvrExtSampler**            ppSampler) = 0;

  virtual HRESULT STDMETHODCALLTYPE Dispatch(
    const NVR_EXT_DISPATCH_DESC*          pDesc) = 0;

  virtual HRESULT STDMETHODCALLTYPE UpdateBuffer(
          ID3D9NvrExtBuffer*              pBuffer,
          UINT                            Offset,
          UINT                            Size,
    const void*                           pData) = 0;

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

  virtual HRESULT STDMETHODCALLTYPE CopyBufferToTexture(
          IUnknown*                       pTexture,
          UINT                            MipLevel,
          UINT                            Layer,
          ID3D9NvrExtBuffer*              pSrc,
          UINT                            SrcOffset) = 0;

  virtual HRESULT STDMETHODCALLTYPE CopyTextureToBuffer(
          ID3D9NvrExtBuffer*              pDst,
          UINT                            DstOffset,
          IUnknown*                       pTexture,
          UINT                            MipLevel,
          UINT                            Layer) = 0;

  virtual HRESULT STDMETHODCALLTYPE ReadBuffer(
          ID3D9NvrExtBuffer*              pBuffer,
          UINT                            Offset,
          UINT                            Size,
          void*                           pData) = 0;

  virtual HRESULT STDMETHODCALLTYPE DrawShifted(
    const NVR_EXT_DRAW_DESC*              pDesc,
          FLOAT                           ShiftX,
          FLOAT                           ShiftY) = 0;

  virtual HRESULT STDMETHODCALLTYPE SetProfiling(
          BOOL                            Enable) = 0;

  virtual HRESULT STDMETHODCALLTYPE GetProfile(
          NVR_EXT_PROFILE*                pProfile) = 0;

  virtual HRESULT STDMETHODCALLTYPE DrawDepthStencil(
    const NVR_EXT_DRAW_DESC*              pDesc,
    const NVR_EXT_DEPTH_STENCIL*          pDepthStencil,
          FLOAT                           ShiftX,
          FLOAT                           ShiftY) = 0;
};

struct NVR_EXT_GEOMETRY_PIPELINE_DESC {
  NVR_EXT_GRAPHICS_PIPELINE_DESC Graphics;
  UINT                        VertexBindingCount;
  const NVR_EXT_BINDING_DESC* pVertexBindings;
};

struct NVR_EXT_GEOMETRY_DRAW_DESC {
  NVR_EXT_DRAW_DESC           Draw;
  UINT                        VertexCount;
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

enum NVR_EXT_CULL {
  NVR_EXT_CULL_NONE  = 0,
  NVR_EXT_CULL_BACK  = 1,
  NVR_EXT_CULL_FRONT = 2,
};

struct NVR_EXT_GEOMETRY_SCENE_DESC {
  NVR_EXT_GEOMETRY_DRAW_DESC   Geometry;
  const NVR_EXT_DEPTH_STENCIL* pDepthStencil;
  NVR_EXT_CULL                 CullMode;
  BOOL                         Multisample;
  ID3D9NvrExtBuffer*           pIndirectArgs;
  UINT                         IndirectOffset;
};

MIDL_INTERFACE("6e2b0f3a-9c14-4d57-8a41-b2f0c7a9d324")
ID3D9NvrExtGeometryScene : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE DrawGeometryScene(
    const NVR_EXT_GEOMETRY_SCENE_DESC*    pDesc) = 0;
};

#ifndef _MSC_VER
__CRT_UUID_DECL(ID3D9NvrExtGeometryScene, 0x6e2b0f3a,0x9c14,0x4d57,0x8a,0x41,0xb2,0xf0,0xc7,0xa9,0xd3,0x24);
#endif

enum NVR_EXT_HDR_MODE {
  NVR_EXT_HDR_OFF   = 0,
  NVR_EXT_HDR_HDR10 = 1,
  NVR_EXT_HDR_SCRGB = 2,
};

enum NVR_EXT_HDR_FLAGS {
  NVR_EXT_HDR_FLAG_SHOW_HUD_MASK = 0x1,
  NVR_EXT_HDR_FLAG_GAMMA22       = 0x2,
};

struct NVR_EXT_HDR_INFO {
  BOOL  SupportsHdr10;
  BOOL  SupportsScRgb;
  FLOAT DisplayMinNits;
  FLOAT DisplayMaxNits;
  FLOAT DisplayMaxFullFrameNits;
  UINT  ActiveMode;
};

struct NVR_EXT_HDR_OUTPUT {
  UINT  Mode;
  FLOAT PaperWhiteNits;
  FLOAT PeakNits;
  FLOAT MaxFrameAverageNits;
  FLOAT HudThreshold;
  UINT  Flags;
};

MIDL_INTERFACE("6e2b0f3a-9c14-4d57-8a41-b2f0c7a9d310")
ID3D9NvrExtHdr : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE GetHdrInfo(
          NVR_EXT_HDR_INFO*               pInfo) = 0;

  virtual HRESULT STDMETHODCALLTYPE SetHdrOutput(
    const NVR_EXT_HDR_OUTPUT*             pOutput) = 0;

  virtual HRESULT STDMETHODCALLTYPE MarkHdrScene(
          IUnknown*                       pScene,
          IUnknown*                       pGain) = 0;

  virtual HRESULT STDMETHODCALLTYPE RequestHdrCapture() = 0;

  virtual HRESULT STDMETHODCALLTYPE ReadHdrCapture(
          UINT*                           pWidth,
          UINT*                           pHeight,
          void*                           pData,
          UINT                            Size) = 0;
};

MIDL_INTERFACE("6e2b0f3a-9c14-4d57-8a41-b2f0c7a9d315")
ID3D9NvrExtReadback : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE CreateReadbackBuffer(
          UINT                            Size,
    const char*                           pDebugName,
          ID3D9NvrExtBuffer**             ppBuffer) = 0;

  virtual HRESULT STDMETHODCALLTYPE CopyToReadback(
          ID3D9NvrExtBuffer*              pDst,
          UINT                            DstOffset,
          ID3D9NvrExtBuffer*              pSrc,
          UINT                            SrcOffset,
          UINT                            Size) = 0;

  virtual HRESULT STDMETHODCALLTYPE TryReadback(
          ID3D9NvrExtBuffer*              pBuffer,
          UINT                            Offset,
          UINT                            Size,
          void*                           pData) = 0;
};

#ifndef _MSC_VER
__CRT_UUID_DECL(ID3D9NvrExtReadback, 0x6e2b0f3a,0x9c14,0x4d57,0x8a,0x41,0xb2,0xf0,0xc7,0xa9,0xd3,0x15);
#endif

struct NVR_EXT_TESS_VS_DESC {
  const void*                 pBytecode;
  UINT                        BytecodeSize;
  UINT                        Family;
  const void*                 pControlSpirv;
  UINT                        ControlSpirvSize;
  UINT                        VsFloatRegisters[4];
  const char*                 pDebugName;
};

struct NVR_EXT_TESS_PS_DESC {
  const void*                 pBytecode;
  UINT                        BytecodeSize;
  UINT                        Family;
  const void*                 pEvalSpirv[2];
  UINT                        EvalSpirvSize[2];
  UINT                        TextureMask;
  UINT                        PsFloatRegisters[2];
  const char*                 pDebugName;
};

struct NVR_EXT_TESS_STATE {
  BOOL                        Enable;
  UINT                        Winding;
  UINT                        Wireframe;
  FLOAT                       ControlParams[8];
  FLOAT                       EvalParams[4];
};

struct NVR_EXT_TESS_STATS {
  UINT64                      TessellatedDraws;
  UINT64                      OtherTopologyDraws;
  UINT64                      Bindings;
  UINT                        VertexShaders;
  UINT                        PixelShaders;
  UINT                        RefusedPairs;
};

MIDL_INTERFACE("6e2b0f3a-9c14-4d57-8a41-b2f0c7a9d321")
ID3D9NvrExtTess : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE RegisterTessVertexShader(
    const NVR_EXT_TESS_VS_DESC*           pDesc) = 0;

  virtual HRESULT STDMETHODCALLTYPE RegisterTessPixelShader(
    const NVR_EXT_TESS_PS_DESC*           pDesc) = 0;

  virtual HRESULT STDMETHODCALLTYPE SetTessState(
    const NVR_EXT_TESS_STATE*             pState) = 0;

  virtual HRESULT STDMETHODCALLTYPE GetTessStats(
          NVR_EXT_TESS_STATS*             pStats) = 0;
};

#ifndef _MSC_VER
__CRT_UUID_DECL(ID3D9NvrExtTess,     0x6e2b0f3a,0x9c14,0x4d57,0x8a,0x41,0xb2,0xf0,0xc7,0xa9,0xd3,0x21);
__CRT_UUID_DECL(ID3D9NvrExtHdr,      0x6e2b0f3a,0x9c14,0x4d57,0x8a,0x41,0xb2,0xf0,0xc7,0xa9,0xd3,0x10);
__CRT_UUID_DECL(ID3D9NvrExt,         0x6e2b0f3a,0x9c14,0x4d57,0x8a,0x41,0xb2,0xf0,0xc7,0xa9,0xd3,0x00);
__CRT_UUID_DECL(ID3D9NvrExtPipeline, 0x6e2b0f3a,0x9c14,0x4d57,0x8a,0x41,0xb2,0xf0,0xc7,0xa9,0xd3,0x01);
__CRT_UUID_DECL(ID3D9NvrExtBuffer,   0x6e2b0f3a,0x9c14,0x4d57,0x8a,0x41,0xb2,0xf0,0xc7,0xa9,0xd3,0x02);
__CRT_UUID_DECL(ID3D9NvrExtSampler,  0x6e2b0f3a,0x9c14,0x4d57,0x8a,0x41,0xb2,0xf0,0xc7,0xa9,0xd3,0x03);
#endif

enum NVR_EXT_SHARED_FORMAT {
  NVR_EXT_SHARED_RGBA16F = 0,
  NVR_EXT_SHARED_RG16F   = 1,
  NVR_EXT_SHARED_R32F    = 2,
  NVR_EXT_SHARED_RG32F   = 3,
  NVR_EXT_SHARED_RGBA8   = 4,
  NVR_EXT_SHARED_R16F    = 5,
};

struct NVR_EXT_SHARE_INFO {
  BOOL  Supported;
  UINT  VendorId;
  UINT  DeviceId;
  UINT  DriverVersion;
  UINT  ApiVersion;
  BYTE  DeviceUUID[16];
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

struct NVR_EXT_SHARED_IMAGE_INFO {
  UINT    VkFormat;
  UINT    Width;
  UINT    Height;
  UINT    MipLevels;
  UINT    ArrayLayers;
  UINT    Samples;
  UINT    Usage;
  UINT    Flags;
  UINT    Tiling;
  UINT    Layout;
  UINT    HandleType;
  UINT    MemoryTypeIndex;
  UINT64  AllocationSize;
  BOOL    Dedicated;
};

#define NVR_EXT_FACILITY_VK 0x7E1
#define NVR_EXT_HRESULT_FROM_VK(vr) ((HRESULT)(0x80000000u | (0x7E1u << 16) | ((UINT)(-(INT)(vr)) & 0xFFFFu)))

MIDL_INTERFACE("6e2b0f3a-9c14-4d57-8a41-b2f0c7a9d327")
ID3D9NvrExtSharedImage : public IUnknown {
  virtual void STDMETHODCALLTYPE GetInfo(
          NVR_EXT_SHARED_IMAGE_INFO*      pInfo) = 0;

  virtual HRESULT STDMETHODCALLTYPE CreateHandle(
          HANDLE*                         pHandle) = 0;
};

MIDL_INTERFACE("6e2b0f3a-9c14-4d57-8a41-b2f0c7a9d328")
ID3D9NvrExtSharedFence : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE CreateHandle(
          HANDLE*                         pHandle) = 0;

  virtual HRESULT STDMETHODCALLTYPE GetCompletedValue(
          UINT64*                         pValue) = 0;

  virtual HRESULT STDMETHODCALLTYPE SignalFromHost(
          UINT64                          Value) = 0;
};

MIDL_INTERFACE("6e2b0f3a-9c14-4d57-8a41-b2f0c7a9d326")
ID3D9NvrExtShare : public IUnknown {
  virtual void STDMETHODCALLTYPE GetShareInfo(
          NVR_EXT_SHARE_INFO*             pInfo) = 0;

  virtual HRESULT STDMETHODCALLTYPE CreateSharedImage(
    const NVR_EXT_SHARED_IMAGE_DESC*      pDesc,
          ID3D9NvrExtSharedImage**        ppImage) = 0;

  virtual HRESULT STDMETHODCALLTYPE CreateSharedFence(
          UINT64                          InitialValue,
          ID3D9NvrExtSharedFence**        ppFence) = 0;

  virtual HRESULT STDMETHODCALLTYPE CopyToShared(
          ID3D9NvrExtSharedImage*         pDst,
          UINT                            DstX,
          UINT                            DstY,
          IUnknown*                       pSrc,
    const RECT*                           pSrcRect) = 0;

  virtual HRESULT STDMETHODCALLTYPE CopyFromShared(
          IUnknown*                       pDst,
          UINT                            DstX,
          UINT                            DstY,
          ID3D9NvrExtSharedImage*         pSrc,
    const RECT*                           pSrcRect) = 0;

  virtual HRESULT STDMETHODCALLTYPE SignalShared(
          ID3D9NvrExtSharedFence*         pFence,
          UINT64                          Value) = 0;

  virtual HRESULT STDMETHODCALLTYPE WaitShared(
          ID3D9NvrExtSharedFence*         pFence,
          UINT64                          Value) = 0;
};

#ifndef _MSC_VER
__CRT_UUID_DECL(ID3D9NvrExtShare,       0x6e2b0f3a,0x9c14,0x4d57,0x8a,0x41,0xb2,0xf0,0xc7,0xa9,0xd3,0x26);
__CRT_UUID_DECL(ID3D9NvrExtSharedImage, 0x6e2b0f3a,0x9c14,0x4d57,0x8a,0x41,0xb2,0xf0,0xc7,0xa9,0xd3,0x27);
__CRT_UUID_DECL(ID3D9NvrExtSharedFence, 0x6e2b0f3a,0x9c14,0x4d57,0x8a,0x41,0xb2,0xf0,0xc7,0xa9,0xd3,0x28);
#endif

enum NVR_EXT_RT_POSITION_FORMAT {
  NVR_EXT_RT_POSITION_FLOAT3 = 0,
  NVR_EXT_RT_POSITION_FLOAT4 = 1,
  NVR_EXT_RT_POSITION_HALF4  = 2,
};

enum NVR_EXT_RT_TOPOLOGY {
  NVR_EXT_RT_TRIANGLE_LIST  = 0,
  NVR_EXT_RT_TRIANGLE_STRIP = 1,
};

struct NVR_EXT_RT_RANGE {
  NVR_EXT_RT_TOPOLOGY Topology;
  UINT                StartIndex;
  UINT                PrimitiveCount;
  INT                 BaseVertex;
};

#define NVR_EXT_RT_MAX_RANGES 1024

struct NVR_EXT_RT_GEOMETRY_DESC {
  IDirect3DVertexBuffer9*     pVertexBuffer;
  UINT                        PositionOffset;
  UINT                        VertexStride;
  UINT                        VertexCount;
  NVR_EXT_RT_POSITION_FORMAT  PositionFormat;
  IDirect3DIndexBuffer9*      pIndexBuffer;
  UINT                        RangeCount;
  const NVR_EXT_RT_RANGE*     pRanges;
  UINT                        Flags;
  const char*                 pDebugName;
};

enum NVR_EXT_RT_INSTANCE_FLAGS {
  NVR_EXT_RT_INSTANCE_CULL_DISABLE = 0x1,
  NVR_EXT_RT_INSTANCE_FLIP_FACING  = 0x2,
  NVR_EXT_RT_INSTANCE_FORCE_OPAQUE = 0x4,
};

struct NVR_EXT_RT_INSTANCE {
  struct ID3D9NvrExtRtGeometry* pGeometry;
  FLOAT                         Transform[3][4];
  UINT                          InstanceId;
  UINT                          Mask;
  UINT                          Flags;
};

struct NVR_EXT_RT_INFO {
  BOOL    Supported;
  UINT    VersionMinor;
  char    Reason[96];
  UINT    MaxInstances;
  UINT64  MaxPrimitiveCount;
};

struct NVR_EXT_RT_GEOMETRY_INFO {
  UINT    VertexCount;
  UINT    TriangleCount;
  UINT    Texels;
};

struct NVR_EXT_RT_STATS {
  UINT    Geometries;
  UINT64  Triangles;
  UINT64  GeometryBytes;
  UINT64  GeometriesBuilt;
  UINT    Scenes;
  UINT    LastInstances;
  UINT64  SceneBuilds;
  UINT64  SceneBytes;
};

MIDL_INTERFACE("6e2b0f3a-9c14-4d57-8a41-b2f0c7a9d331")
ID3D9NvrExtRtGeometry : public IUnknown {
  virtual UINT STDMETHODCALLTYPE GetTriangleCount() = 0;
};

MIDL_INTERFACE("6e2b0f3a-9c14-4d57-8a41-b2f0c7a9d332")
ID3D9NvrExtRtScene : public IUnknown {
  virtual UINT64 STDMETHODCALLTYPE GetAddress() = 0;

  virtual UINT STDMETHODCALLTYPE GetMaxInstances() = 0;
};

MIDL_INTERFACE("6e2b0f3a-9c14-4d57-8a41-b2f0c7a9d330")
ID3D9NvrExtRt : public IUnknown {
  virtual void STDMETHODCALLTYPE GetRtInfo(
          NVR_EXT_RT_INFO*                pInfo) = 0;

  virtual HRESULT STDMETHODCALLTYPE CreateGeometry(
    const NVR_EXT_RT_GEOMETRY_DESC*       pDesc,
          ID3D9NvrExtRtGeometry**         ppGeometry) = 0;

  virtual HRESULT STDMETHODCALLTYPE CreateScene(
          UINT                            MaxInstances,
    const char*                           pDebugName,
          ID3D9NvrExtRtScene**            ppScene) = 0;

  virtual HRESULT STDMETHODCALLTYPE BuildScene(
          ID3D9NvrExtRtScene*             pScene,
          UINT                            Count,
    const NVR_EXT_RT_INSTANCE*            pInstances) = 0;

  virtual void STDMETHODCALLTYPE GetRtStats(
          NVR_EXT_RT_STATS*               pStats) = 0;

  virtual HRESULT STDMETHODCALLTYPE MeasureGeometry(
    const NVR_EXT_RT_GEOMETRY_DESC*       pDesc,
          NVR_EXT_RT_GEOMETRY_INFO*       pInfo) = 0;

  virtual HRESULT STDMETHODCALLTYPE StageGeometry(
    const NVR_EXT_RT_GEOMETRY_DESC*       pDesc,
          ID3D9NvrExtSharedImage*         pArena,
          UINT                            DstTexel,
          NVR_EXT_RT_GEOMETRY_INFO*       pInfo) = 0;

  virtual HRESULT STDMETHODCALLTYPE FlushStagedGeometry(
          ID3D9NvrExtSharedImage*         pArena) = 0;
};

#ifndef _MSC_VER
__CRT_UUID_DECL(ID3D9NvrExtRt,         0x6e2b0f3a,0x9c14,0x4d57,0x8a,0x41,0xb2,0xf0,0xc7,0xa9,0xd3,0x30);
__CRT_UUID_DECL(ID3D9NvrExtRtGeometry, 0x6e2b0f3a,0x9c14,0x4d57,0x8a,0x41,0xb2,0xf0,0xc7,0xa9,0xd3,0x31);
__CRT_UUID_DECL(ID3D9NvrExtRtScene,    0x6e2b0f3a,0x9c14,0x4d57,0x8a,0x41,0xb2,0xf0,0xc7,0xa9,0xd3,0x32);
#endif
