#pragma once

// ---------------------------------------------------------------------------
// ew::Texture / ew::Buffer / ew::Sampler / ew::Fbo - thin RefCntAutoPtr
// wrappers around Diligent resources with the handful of operations the
// Earthworks engine actually uses.
//
// Formats and bind flags are Diligent's own enums (TEX_FORMAT_*, BIND_*) -
// no re-mapping layer. Views are created eagerly for the requested bind
// flags.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "ewTypes.h"

#include "RefCntAutoPtr.hpp"
#include "GraphicsTypes.h"
#include "Texture.h"
#include "TextureView.h"
#include "Buffer.h"
#include "BufferView.h"
#include "Sampler.h"
#include "SwapChain.h"
#include "Fence.h"

namespace ew
{

class GpuContext;

// ---------------------------------------------------------------------------

class Texture
{
public:
    using SharedPtr = std::shared_ptr<Texture>;

    /// pInitData fills mip 0 of slice 0 (the only case the engine needs); with
    /// mipLevels > 1 the remaining mips start undefined - call generateMips().
    static SharedPtr create2D(uint32_t width, uint32_t height, Diligent::TEXTURE_FORMAT format,
                              uint32_t arraySize = 1, uint32_t mipLevels = 1,
                              const void* pInitData = nullptr,
                              Diligent::BIND_FLAGS bindFlags = Diligent::BIND_SHADER_RESOURCE,
                              const char* name = nullptr);

    static SharedPtr create3D(uint32_t width, uint32_t height, uint32_t depth,
                              Diligent::TEXTURE_FORMAT format,
                              const void* pInitData = nullptr,
                              Diligent::BIND_FLAGS bindFlags = Diligent::BIND_SHADER_RESOURCE,
                              const char* name = nullptr);

    /// Cube map (6-slice RESOURCE_DIM_TEX_CUBE). pInitData, when given, is ONE
    /// face worth of pixels replicated into all 6 faces - enough for the
    /// 1x1 dummy cubes TextureCube shader slots need (envMap, gSky fallback).
    static SharedPtr createCube(uint32_t size, Diligent::TEXTURE_FORMAT format,
                                const void* pInitData = nullptr,
                                Diligent::BIND_FLAGS bindFlags = Diligent::BIND_SHADER_RESOURCE,
                                const char* name = nullptr);

    /// Image-file load through Diligent's TextureLoader (dds/png/jpg): the
    /// sky/env/ecotope/dappled textures. Falls back to a 1x1 fully TRANSPARENT
    /// black texture when the file is missing or fails to decode, with a loud
    /// log - alpha-blended consumers must never paint solid garbage.
    static SharedPtr createFromFile(const std::filesystem::path& path,
                                    bool generateMipLevels, bool loadAsSrgb,
                                    Diligent::BIND_FLAGS bindFlags = Diligent::BIND_SHADER_RESOURCE);

    /// Wraps an existing Diligent texture (swap-chain back buffer, loader output).
    static SharedPtr wrap(Diligent::ITexture* pTexture);

    Diligent::ITexture* handle() const { return m_pTexture; }

    Diligent::ITextureView* getSRV() const { return m_SRV; }
    Diligent::ITextureView* getRTV() const { return m_RTV; }
    Diligent::ITextureView* getDSV() const { return m_DSV; }
    Diligent::ITextureView* getUAV() const { return m_UAV; }

    uint32_t getWidth() const { return m_Width; }
    uint32_t getHeight() const { return m_Height; }
    Diligent::TEXTURE_FORMAT getFormat() const { return m_Format; }

    /// Full-subresource upload, e.g. the 4096^2 RG32Float shadow texture
    /// refresh on shadowReady.
    void upload(GpuContext* pCtx, const void* pData);

    /// Mip-chain generation (rootElevation).
    /// Only valid on textures created with mipLevels > 1 (those get
    /// MISC_TEXTURE_FLAG_GENERATE_MIPS + the required RTV bind automatically).
    void generateMips(GpuContext* pCtx);

private:
    void createViews(Diligent::BIND_FLAGS bindFlags);

    Diligent::RefCntAutoPtr<Diligent::ITexture>     m_pTexture;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> m_SRV;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> m_RTV;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> m_DSV;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> m_UAV;
    uint32_t                 m_Width  = 0;
    uint32_t                 m_Height = 0;
    Diligent::TEXTURE_FORMAT m_Format = Diligent::TEX_FORMAT_UNKNOWN;
};

// ---------------------------------------------------------------------------

class Buffer
{
public:
    using SharedPtr = std::shared_ptr<Buffer>;

    /// StructuredBuffer / RWStructuredBuffer backing store. Default bind flags
    /// cover the common SRV+UAV case; indirect-arg buffers add
    /// BIND_INDIRECT_DRAW_ARGS. USAGE_DEFAULT always - never USAGE_DYNAMIC for
    /// once-written buffers: UpdateBuffer into an upload heap removes the
    /// D3D12 device.
    static SharedPtr createStructured(uint32_t elementSize, uint32_t elementCount,
                                      Diligent::BIND_FLAGS bindFlags =
                                          Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_UNORDERED_ACCESS,
                                      const void* pInitData = nullptr,
                                      const char* name = nullptr);

    /// Typed R32_UINT buffer (HLSL `Buffer<uint>`) - the ecotope plant tables.
    static SharedPtr createTypedUint(uint32_t elementCount,
                                     Diligent::BIND_FLAGS bindFlags = Diligent::BIND_SHADER_RESOURCE,
                                     const void* pInitData = nullptr,
                                     const char* name = nullptr);

    /// Raw buffer (index buffers, readback staging, ...).
    static SharedPtr create(uint64_t sizeInBytes, Diligent::BIND_FLAGS bindFlags,
                            Diligent::USAGE usage = Diligent::USAGE_DEFAULT,
                            const void* pInitData = nullptr,
                            const char* name = nullptr);

    Diligent::IBuffer* handle() const { return m_pBuffer; }

    Diligent::IBufferView* getSRV() const { return m_SRV; }
    Diligent::IBufferView* getUAV() const { return m_UAV; }

    uint64_t getSize() const { return m_Size; }

    /// UpdateBuffer-based upload through the immediate context.
    void setBlob(const void* pData, uint64_t offset, uint64_t size);

private:
    Diligent::RefCntAutoPtr<Diligent::IBuffer>     m_pBuffer;
    Diligent::RefCntAutoPtr<Diligent::IBufferView> m_SRV;
    Diligent::RefCntAutoPtr<Diligent::IBufferView> m_UAV;
    uint64_t m_Size = 0;
};

// ---------------------------------------------------------------------------

/// GPU->CPU buffer readback with explicit fence-checked latency (tileCenters
/// culling heights, GC_feedback picking/metrics).
///
/// Mapping the live buffer for reading would be a full GPU sync every frame.
/// Instead this keeps a small ring of staging buffers and a fence:
/// enqueueCopy() records the copy + signals the fence (this flushes the
/// command buffer - call it at most once or twice per frame), and
/// mapCompleted() maps the NEWEST slot whose copy the GPU has finished. The
/// consumer therefore sees data 1..kSlots-1 frames old, which is acceptable
/// for both users.
class ReadbackBuffer
{
public:
    using SharedPtr = std::shared_ptr<ReadbackBuffer>;

    static constexpr uint32_t kSlots = 3;

    static SharedPtr create(uint64_t sizeInBytes, const char* name = nullptr);

    /// Copies the source buffer into the current ring slot and signals the
    /// fence. Skips (with a warn-once) when the source size does not match.
    /// `tag` is an opaque caller value (e.g. a frame counter) returned by
    /// completedTag() for the slot a later mapCompleted() yields - lets the
    /// caller detect how old the mapped data is.
    void enqueueCopy(GpuContext* pCtx, const Buffer::SharedPtr& src, uint64_t tag = 0);

    /// Maps the newest GPU-completed slot for reading; nullptr when no copy
    /// has completed yet. Every successful call must be paired with unmap().
    const void* mapCompleted(GpuContext* pCtx);
    /// Tag passed to enqueueCopy for the currently mapped slot (valid between
    /// a successful mapCompleted() and unmap()).
    uint64_t completedTag() const { return m_MappedSlot >= 0 ? m_SlotTag[m_MappedSlot] : 0; }
    void unmap(GpuContext* pCtx);

private:
    Diligent::RefCntAutoPtr<Diligent::IBuffer> m_Staging[kSlots];
    Diligent::RefCntAutoPtr<Diligent::IFence>  m_Fence;
    uint64_t m_SlotFenceValue[kSlots] = {};
    uint64_t m_SlotTag[kSlots]        = {};
    uint64_t m_NextFenceValue         = 1;
    uint32_t m_NextSlot               = 0;
    int32_t  m_MappedSlot             = -1;
    uint64_t m_Size                   = 0;
};

// ---------------------------------------------------------------------------

class Sampler
{
public:
    using SharedPtr = std::shared_ptr<Sampler>;

    static SharedPtr create(const Diligent::SamplerDesc& desc);

    Diligent::ISampler* handle() const { return m_pSampler; }

private:
    Diligent::RefCntAutoPtr<Diligent::ISampler> m_pSampler;
};

// ---------------------------------------------------------------------------

/// Render-target set. Either an attachment FBO (textures created with
/// BIND_RENDER_TARGET / BIND_DEPTH_STENCIL) or a swap-chain proxy that always
/// resolves to the CURRENT back buffer at draw time.
class Fbo
{
public:
    using SharedPtr = std::shared_ptr<Fbo>;

    static constexpr uint32_t kMaxColorTargets = 8;

    static SharedPtr create();
    static SharedPtr createFromSwapChain(Diligent::ISwapChain* pSwapChain);

    void attachColorTarget(const Texture::SharedPtr& tex, uint32_t slot);
    void attachDepthStencilTarget(const Texture::SharedPtr& tex);

    bool isSwapChainProxy() const { return m_pSwapChain != nullptr; }
    Diligent::ISwapChain* swapChain() const { return m_pSwapChain; }

    const Texture::SharedPtr& getColorTexture(uint32_t slot) const;
    const Texture::SharedPtr& getDepthStencilTexture() const { return m_Depth; }

    /// Live views (swap-chain proxy: current back buffer / depth buffer).
    Diligent::ITextureView* getRenderTargetView(uint32_t slot) const;
    Diligent::ITextureView* getDepthStencilView() const;

    uint32_t getWidth() const;
    uint32_t getHeight() const;

    /// Attachment formats for PSO creation - ALL color targets, not just
    /// slot 0 (the terrafector bake writes 8 MRTs).
    struct Formats
    {
        Diligent::TEXTURE_FORMAT RTVs[kMaxColorTargets] = {}; // TEX_FORMAT_UNKNOWN
        uint32_t                 NumRenderTargets       = 0;
        Diligent::TEXTURE_FORMAT DSV = Diligent::TEX_FORMAT_UNKNOWN;

        bool operator==(const Formats& rhs) const;
    };
    Formats getFormats() const;

private:
    Diligent::ISwapChain* m_pSwapChain = nullptr; // proxy mode when set
    Texture::SharedPtr    m_Colors[kMaxColorTargets];
    Texture::SharedPtr    m_Depth;
};

} // namespace ew
