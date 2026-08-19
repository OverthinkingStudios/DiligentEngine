#include "ewResources.h"
#include "ewGpuContext.h"

#include <algorithm>

#include "GraphicsAccessories.hpp"
#include "TextureUtilities.h"

#include "ots/Log.hpp"

namespace ew
{

namespace
{

void createTextureView(Diligent::ITexture* pTex, Diligent::TEXTURE_VIEW_TYPE viewType,
                       Diligent::RefCntAutoPtr<Diligent::ITextureView>& outView)
{
    if (!pTex)
        return;
    Diligent::TextureViewDesc viewDesc;
    viewDesc.ViewType = viewType;
    // Manually created views do NOT inherit mip-generation support from the
    // texture's MISC_TEXTURE_FLAG_GENERATE_MIPS (default views do). Without
    // this flag the D3D12 backend crashes in GenerateMips (null
    // m_MipGenerationDescriptors); Vulkan happens to tolerate it.
    if (viewType == Diligent::TEXTURE_VIEW_SHADER_RESOURCE &&
        (pTex->GetDesc().MiscFlags & Diligent::MISC_TEXTURE_FLAG_GENERATE_MIPS) != 0)
        viewDesc.Flags |= Diligent::TEXTURE_VIEW_FLAG_ALLOW_MIP_MAP_GENERATION;
    pTex->CreateView(viewDesc, &outView);
}

} // namespace

// --- Texture ---------------------------------------------------------------------

void Texture::createViews(Diligent::BIND_FLAGS bindFlags)
{
    if (!m_pTexture)
        return;
    if (bindFlags & Diligent::BIND_SHADER_RESOURCE)
        createTextureView(m_pTexture, Diligent::TEXTURE_VIEW_SHADER_RESOURCE, m_SRV);
    if (bindFlags & Diligent::BIND_RENDER_TARGET)
        createTextureView(m_pTexture, Diligent::TEXTURE_VIEW_RENDER_TARGET, m_RTV);
    if (bindFlags & Diligent::BIND_DEPTH_STENCIL)
        createTextureView(m_pTexture, Diligent::TEXTURE_VIEW_DEPTH_STENCIL, m_DSV);
    if (bindFlags & Diligent::BIND_UNORDERED_ACCESS)
        createTextureView(m_pTexture, Diligent::TEXTURE_VIEW_UNORDERED_ACCESS, m_UAV);
}

Texture::SharedPtr Texture::create2D(uint32_t width, uint32_t height, Diligent::TEXTURE_FORMAT format,
                                     uint32_t arraySize, uint32_t mipLevels, const void* pInitData,
                                     Diligent::BIND_FLAGS bindFlags, const char* name)
{
    auto tex        = std::make_shared<Texture>();
    tex->m_Width    = width;
    tex->m_Height   = height;
    tex->m_Format   = format;

    GpuContext* pGpu = GpuContext::get();

    // arraySize > 1 MUST be a real texture array: tiles stream in and out of
    // individual array slices.
    Diligent::TextureDesc desc;
    desc.Name      = name ? name : "ew Texture2D";
    desc.Type      = arraySize > 1 ? Diligent::RESOURCE_DIM_TEX_2D_ARRAY : Diligent::RESOURCE_DIM_TEX_2D;
    desc.ArraySize = std::max(arraySize, 1u);
    desc.Width     = width;
    desc.Height    = height;
    desc.Format    = format;
    desc.BindFlags = bindFlags;
    desc.MipLevels = std::max(mipLevels, 1u);
    // Mipped textures get the GenerateMips capability (rootElevation).
    // Diligent requires RENDER_TARGET for the flag.
    if (desc.MipLevels > 1 && (bindFlags & Diligent::BIND_RENDER_TARGET))
        desc.MiscFlags = Diligent::MISC_TEXTURE_FLAG_GENERATE_MIPS;

    Diligent::TextureSubResData subres;
    Diligent::TextureData       initData;
    Diligent::TextureData*      pData          = nullptr;
    bool                        uploadAfterCreate = false;
    if (pInitData)
    {
        if (arraySize > 1)
        {
            // TODO: Diligent requires init data for EVERY subresource; no caller
            // needs that yet. Flag it loudly if one appears. Either support array
            // init data or make this a hard failure instead of an empty texture.
            spdlog::error("ew::Texture::create2D('{}'): init data with {} slices not implemented - created empty",
                          desc.Name, arraySize);
        }
        else if (desc.MipLevels > 1)
        {
            // Fill mip 0 after creation (rootElevation: data + generateMips).
            uploadAfterCreate = true;
        }
        else
        {
            const auto& fmtAttribs = Diligent::GetTextureFormatAttribs(format);
            subres.pData           = pInitData;
            subres.Stride          = static_cast<Diligent::Uint64>(width) * fmtAttribs.GetElementSize();
            initData.pSubResources   = &subres;
            initData.NumSubresources = 1;
            initData.pContext        = pGpu->context();
            pData                    = &initData;
        }
    }

    pGpu->device()->CreateTexture(desc, pData, &tex->m_pTexture);
    if (!tex->m_pTexture)
    {
        spdlog::error("ew::Texture::create2D('{}'): creation failed ({}x{} fmt {})",
                      desc.Name, width, height, static_cast<int>(format));
        return tex;
    }
    tex->createViews(bindFlags);
    if (uploadAfterCreate)
        tex->upload(pGpu, pInitData);
    return tex;
}

void Texture::generateMips(GpuContext* pCtx)
{
    if (!pCtx || !m_pTexture)
        return;
    if ((m_pTexture->GetDesc().MiscFlags & Diligent::MISC_TEXTURE_FLAG_GENERATE_MIPS) == 0 || !m_SRV)
    {
        spdlog::error("ew::Texture::generateMips('{}'): texture lacks GENERATE_MIPS capability (mipLevels > 1 + RENDER_TARGET bind required)",
                      m_pTexture->GetDesc().Name);
        return;
    }
    pCtx->context()->GenerateMips(m_SRV);
}

Texture::SharedPtr Texture::create3D(uint32_t width, uint32_t height, uint32_t depth,
                                     Diligent::TEXTURE_FORMAT format, const void* pInitData,
                                     Diligent::BIND_FLAGS bindFlags, const char* name)
{
    auto tex      = std::make_shared<Texture>();
    tex->m_Width  = width;
    tex->m_Height = height;
    tex->m_Format = format;

    GpuContext* pGpu = GpuContext::get();

    Diligent::TextureDesc desc;
    desc.Name      = name ? name : "ew Texture3D";
    desc.Type      = Diligent::RESOURCE_DIM_TEX_3D;
    desc.Width     = width;
    desc.Height    = height;
    desc.Depth     = depth;
    desc.Format    = format;
    desc.BindFlags = bindFlags;
    desc.MipLevels = 1;

    Diligent::TextureSubResData subres;
    Diligent::TextureData       initData;
    Diligent::TextureData*      pData = nullptr;
    if (pInitData)
    {
        const auto& fmtAttribs = Diligent::GetTextureFormatAttribs(format);
        subres.pData           = pInitData;
        subres.Stride          = static_cast<Diligent::Uint64>(width) * fmtAttribs.GetElementSize();
        subres.DepthStride     = subres.Stride * height;
        initData.pSubResources   = &subres;
        initData.NumSubresources = 1;
        initData.pContext        = pGpu->context();
        pData                    = &initData;
    }

    pGpu->device()->CreateTexture(desc, pData, &tex->m_pTexture);
    if (!tex->m_pTexture)
    {
        spdlog::error("ew::Texture::create3D('{}'): creation failed", desc.Name);
        return tex;
    }
    tex->createViews(bindFlags);
    return tex;
}

Texture::SharedPtr Texture::createCube(uint32_t size, Diligent::TEXTURE_FORMAT format,
                                       const void* pInitData, Diligent::BIND_FLAGS bindFlags,
                                       const char* name)
{
    auto tex      = std::make_shared<Texture>();
    tex->m_Width  = size;
    tex->m_Height = size;
    tex->m_Format = format;

    GpuContext* pGpu = GpuContext::get();

    Diligent::TextureDesc desc;
    desc.Name      = name ? name : "ew TextureCube";
    desc.Type      = Diligent::RESOURCE_DIM_TEX_CUBE;
    desc.ArraySize = 6;
    desc.Width     = size;
    desc.Height    = size;
    desc.Format    = format;
    desc.BindFlags = bindFlags;
    desc.MipLevels = 1;

    // One face worth of data replicated into all 6 faces (Diligent wants init
    // data for every subresource or none).
    Diligent::TextureSubResData subres[6];
    Diligent::TextureData       initData;
    Diligent::TextureData*      pData = nullptr;
    if (pInitData)
    {
        const auto& fmtAttribs = Diligent::GetTextureFormatAttribs(format);
        for (auto& s : subres)
        {
            s.pData  = pInitData;
            s.Stride = static_cast<Diligent::Uint64>(size) * fmtAttribs.GetElementSize();
        }
        initData.pSubResources   = subres;
        initData.NumSubresources = 6;
        initData.pContext        = pGpu->context();
        pData                    = &initData;
    }

    pGpu->device()->CreateTexture(desc, pData, &tex->m_pTexture);
    if (!tex->m_pTexture)
    {
        spdlog::error("ew::Texture::createCube('{}'): creation failed", desc.Name);
        return tex;
    }
    tex->createViews(bindFlags);
    return tex;
}

Texture::SharedPtr Texture::createFromFile(const std::filesystem::path& path,
                                           bool generateMipLevels, bool loadAsSrgb,
                                           Diligent::BIND_FLAGS bindFlags)
{
    GpuContext* pGpu = GpuContext::get();

    const std::filesystem::path resolved = pGpu->resolvePath(path);
    if (std::filesystem::exists(resolved) && !std::filesystem::is_directory(resolved))
    {
        const std::string resolvedStr = resolved.string();
        const std::string name        = resolved.filename().string();

        Diligent::TextureLoadInfo loadInfo;
        loadInfo.Name         = name.c_str();
        loadInfo.IsSRGB       = loadAsSrgb;
        loadInfo.GenerateMips = generateMipLevels;
        loadInfo.BindFlags    = bindFlags;

        Diligent::RefCntAutoPtr<Diligent::ITexture> pDiligentTex;
        Diligent::CreateTextureFromFile(resolvedStr.c_str(), loadInfo, pGpu->device(), &pDiligentTex);
        if (pDiligentTex)
        {
            auto tex        = std::make_shared<Texture>();
            const Diligent::TextureDesc& desc = pDiligentTex->GetDesc();
            tex->m_pTexture = pDiligentTex;
            tex->m_Width    = desc.Width;
            tex->m_Height   = desc.Height;
            tex->m_Format   = desc.Format;
            tex->createViews(bindFlags);
            return tex;
        }
        spdlog::error("ew::Texture::createFromFile: failed to decode '{}'", resolvedStr);
    }
    else
    {
        spdlog::error("ew::Texture::createFromFile: file not found '{}'", path.string());
    }

    // Fallback: 1x1 fully TRANSPARENT black so alpha-blended consumers never
    // paint solid garbage from a missing texture.
    const uint32_t zeroTexel = 0;
    return create2D(1, 1, Diligent::TEX_FORMAT_RGBA8_UNORM, 1, 1, &zeroTexel, bindFlags,
                    "ew missing-file fallback");
}

Texture::SharedPtr Texture::wrap(Diligent::ITexture* pTexture)
{
    auto tex = std::make_shared<Texture>();
    if (!pTexture)
        return tex;
    const Diligent::TextureDesc& desc = pTexture->GetDesc();
    tex->m_pTexture = pTexture;
    tex->m_Width    = desc.Width;
    tex->m_Height   = desc.Height;
    tex->m_Format   = desc.Format;
    tex->createViews(desc.BindFlags);
    return tex;
}

void Texture::upload(GpuContext* pCtx, const void* pData)
{
    if (!pCtx || !m_pTexture || !pData)
        return;

    const auto& fmtAttribs = Diligent::GetTextureFormatAttribs(m_Format);

    Diligent::TextureSubResData subres;
    subres.pData  = pData;
    subres.Stride = static_cast<Diligent::Uint64>(m_Width) * fmtAttribs.GetElementSize();

    Diligent::Box box;
    box.MaxX = m_Width;
    box.MaxY = m_Height;

    pCtx->context()->UpdateTexture(m_pTexture, 0, 0, box, subres,
                                   Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                   Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

// --- Buffer ----------------------------------------------------------------------

Buffer::SharedPtr Buffer::createStructured(uint32_t elementSize, uint32_t elementCount,
                                           Diligent::BIND_FLAGS bindFlags, const void* pInitData,
                                           const char* name)
{
    auto buf    = std::make_shared<Buffer>();
    buf->m_Size = static_cast<uint64_t>(elementSize) * elementCount;

    GpuContext* pGpu = GpuContext::get();

    Diligent::BufferDesc desc;
    desc.Name              = name ? name : "ew structured buffer";
    desc.Size              = buf->m_Size;
    desc.BindFlags         = bindFlags;
    desc.Usage             = Diligent::USAGE_DEFAULT;
    desc.Mode              = Diligent::BUFFER_MODE_STRUCTURED;
    desc.ElementByteStride = elementSize;

    Diligent::BufferData init{};
    if (pInitData)
    {
        init.pData    = pInitData;
        init.DataSize = buf->m_Size;
    }
    pGpu->device()->CreateBuffer(desc, pInitData ? &init : nullptr, &buf->m_pBuffer);
    if (!buf->m_pBuffer)
    {
        spdlog::error("ew::Buffer::createStructured('{}'): creation failed ({} x {} B)",
                      desc.Name, elementCount, elementSize);
        return buf;
    }

    if (bindFlags & Diligent::BIND_SHADER_RESOURCE)
    {
        Diligent::BufferViewDesc viewDesc;
        viewDesc.ViewType = Diligent::BUFFER_VIEW_SHADER_RESOURCE;
        buf->m_pBuffer->CreateView(viewDesc, &buf->m_SRV);
    }
    if (bindFlags & Diligent::BIND_UNORDERED_ACCESS)
    {
        Diligent::BufferViewDesc viewDesc;
        viewDesc.ViewType = Diligent::BUFFER_VIEW_UNORDERED_ACCESS;
        buf->m_pBuffer->CreateView(viewDesc, &buf->m_UAV);
    }
    return buf;
}

Buffer::SharedPtr Buffer::createTypedUint(uint32_t elementCount, Diligent::BIND_FLAGS bindFlags,
                                          const void* pInitData, const char* name)
{
    auto buf    = std::make_shared<Buffer>();
    buf->m_Size = static_cast<uint64_t>(elementCount) * sizeof(uint32_t);

    GpuContext* pGpu = GpuContext::get();

    Diligent::BufferDesc desc;
    desc.Name              = name ? name : "ew typed uint buffer";
    desc.Size              = buf->m_Size;
    desc.BindFlags         = bindFlags;
    desc.Usage             = Diligent::USAGE_DEFAULT;
    desc.Mode              = Diligent::BUFFER_MODE_FORMATTED;
    desc.ElementByteStride = sizeof(uint32_t);

    Diligent::BufferData init{};
    if (pInitData)
    {
        init.pData    = pInitData;
        init.DataSize = buf->m_Size;
    }
    pGpu->device()->CreateBuffer(desc, pInitData ? &init : nullptr, &buf->m_pBuffer);
    if (!buf->m_pBuffer)
    {
        spdlog::error("ew::Buffer::createTypedUint('{}'): creation failed ({} elements)",
                      desc.Name, elementCount);
        return buf;
    }

    // Formatted buffer views need an explicit value type.
    Diligent::BufferViewDesc viewDesc;
    viewDesc.Format.ValueType     = Diligent::VT_UINT32;
    viewDesc.Format.NumComponents = 1;
    if (bindFlags & Diligent::BIND_SHADER_RESOURCE)
    {
        viewDesc.ViewType = Diligent::BUFFER_VIEW_SHADER_RESOURCE;
        buf->m_pBuffer->CreateView(viewDesc, &buf->m_SRV);
    }
    if (bindFlags & Diligent::BIND_UNORDERED_ACCESS)
    {
        viewDesc.ViewType = Diligent::BUFFER_VIEW_UNORDERED_ACCESS;
        buf->m_pBuffer->CreateView(viewDesc, &buf->m_UAV);
    }
    return buf;
}

Buffer::SharedPtr Buffer::create(uint64_t sizeInBytes, Diligent::BIND_FLAGS bindFlags,
                                 Diligent::USAGE usage, const void* pInitData, const char* name)
{
    auto buf    = std::make_shared<Buffer>();
    buf->m_Size = sizeInBytes;

    GpuContext* pGpu = GpuContext::get();

    Diligent::BufferDesc desc;
    desc.Name      = name ? name : "ew buffer";
    desc.Size      = sizeInBytes;
    desc.BindFlags = bindFlags;
    desc.Usage     = usage;
    if (usage == Diligent::USAGE_DYNAMIC)
        desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    else if (usage == Diligent::USAGE_STAGING)
        desc.CPUAccessFlags = Diligent::CPU_ACCESS_READ;

    Diligent::BufferData init{};
    const bool uploadViaInit = pInitData != nullptr &&
        usage != Diligent::USAGE_STAGING && usage != Diligent::USAGE_DYNAMIC;
    if (uploadViaInit)
    {
        init.pData    = pInitData;
        init.DataSize = sizeInBytes;
    }
    pGpu->device()->CreateBuffer(desc, uploadViaInit ? &init : nullptr, &buf->m_pBuffer);
    if (!buf->m_pBuffer)
        spdlog::error("ew::Buffer::create('{}'): creation failed ({} B)", desc.Name, sizeInBytes);
    return buf;
}

void Buffer::setBlob(const void* pData, uint64_t offset, uint64_t size)
{
    if (!m_pBuffer || !pData || size == 0)
        return;
    // USAGE_DEFAULT only in this layer - dynamic buffers must never take this
    // path: a CopyBufferRegion into a D3D12 upload heap removes the device.
    GpuContext::get()->context()->UpdateBuffer(m_pBuffer, offset, size, pData,
                                               Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

// --- ReadbackBuffer ----------------------------------------------------------------

ReadbackBuffer::SharedPtr ReadbackBuffer::create(uint64_t sizeInBytes, const char* name)
{
    auto rb    = std::make_shared<ReadbackBuffer>();
    rb->m_Size = sizeInBytes;

    GpuContext* pGpu = GpuContext::get();

    for (uint32_t i = 0; i < kSlots; ++i)
    {
        Diligent::BufferDesc desc;
        const std::string slotName = std::string(name ? name : "ew readback") + " slot " + std::to_string(i);
        desc.Name           = slotName.c_str();
        desc.Size           = sizeInBytes;
        desc.BindFlags      = Diligent::BIND_NONE;
        desc.Usage          = Diligent::USAGE_STAGING;
        desc.CPUAccessFlags = Diligent::CPU_ACCESS_READ;
        pGpu->device()->CreateBuffer(desc, nullptr, &rb->m_Staging[i]);
        if (!rb->m_Staging[i])
            spdlog::error("ew::ReadbackBuffer('{}'): staging slot {} creation failed ({} B)",
                          name ? name : "?", i, sizeInBytes);
    }

    Diligent::FenceDesc fenceDesc;
    fenceDesc.Name = name ? name : "ew readback fence";
    pGpu->device()->CreateFence(fenceDesc, &rb->m_Fence);
    return rb;
}

void ReadbackBuffer::enqueueCopy(GpuContext* pCtx, const Buffer::SharedPtr& src, uint64_t tag)
{
    if (!pCtx || !src || !src->handle() || !m_Fence)
        return;
    if (src->getSize() < m_Size)
    {
        // TODO: one static shared across every ReadbackBuffer instance, so only the
        // first offending buffer in the whole process is ever reported.
        static bool s_Warned = false;
        if (!s_Warned)
        {
            spdlog::warn("ew::ReadbackBuffer: source buffer smaller than readback size ({} < {})",
                         src->getSize(), m_Size);
            s_Warned = true;
        }
        return;
    }
    if (!m_Staging[m_NextSlot])
        return;

    pCtx->context()->CopyBuffer(src->handle(), 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                m_Staging[m_NextSlot], 0, m_Size,
                                Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    // EnqueueSignal flushes the command buffer, so keep this to once or twice
    // per frame.
    pCtx->context()->EnqueueSignal(m_Fence, m_NextFenceValue);
    m_SlotFenceValue[m_NextSlot] = m_NextFenceValue;
    m_SlotTag[m_NextSlot]        = tag;
    ++m_NextFenceValue;
    m_NextSlot = (m_NextSlot + 1) % kSlots;
}

const void* ReadbackBuffer::mapCompleted(GpuContext* pCtx)
{
    if (!pCtx || !m_Fence || m_MappedSlot >= 0)
        return nullptr;

    const uint64_t completed = m_Fence->GetCompletedValue();

    // Newest slot whose copy the GPU has finished.
    int32_t  best      = -1;
    uint64_t bestValue = 0;
    for (uint32_t i = 0; i < kSlots; ++i)
    {
        if (m_SlotFenceValue[i] != 0 && m_SlotFenceValue[i] <= completed && m_SlotFenceValue[i] > bestValue)
        {
            bestValue = m_SlotFenceValue[i];
            best      = static_cast<int32_t>(i);
        }
    }
    if (best < 0 || !m_Staging[best])
        return nullptr;

    Diligent::PVoid pMapped = nullptr;
    pCtx->context()->MapBuffer(m_Staging[best], Diligent::MAP_READ, Diligent::MAP_FLAG_DO_NOT_WAIT, pMapped);
    if (!pMapped)
        return nullptr;
    m_MappedSlot = best;
    return pMapped;
}

void ReadbackBuffer::unmap(GpuContext* pCtx)
{
    if (!pCtx || m_MappedSlot < 0)
        return;
    pCtx->context()->UnmapBuffer(m_Staging[m_MappedSlot], Diligent::MAP_READ);
    m_MappedSlot = -1;
}

// --- Sampler ---------------------------------------------------------------------

Sampler::SharedPtr Sampler::create(const Diligent::SamplerDesc& desc)
{
    auto smp = std::make_shared<Sampler>();
    GpuContext::get()->device()->CreateSampler(desc, &smp->m_pSampler);
    return smp;
}

// --- Fbo -------------------------------------------------------------------------

Fbo::SharedPtr Fbo::create()
{
    return std::make_shared<Fbo>();
}

Fbo::SharedPtr Fbo::createFromSwapChain(Diligent::ISwapChain* pSwapChain)
{
    auto fbo          = std::make_shared<Fbo>();
    fbo->m_pSwapChain = pSwapChain;
    return fbo;
}

void Fbo::attachColorTarget(const Texture::SharedPtr& tex, uint32_t slot)
{
    if (slot >= kMaxColorTargets)
    {
        spdlog::error("ew::Fbo::attachColorTarget: slot {} out of range", slot);
        return;
    }
    m_Colors[slot] = tex;
}

void Fbo::attachDepthStencilTarget(const Texture::SharedPtr& tex)
{
    m_Depth = tex;
}

const Texture::SharedPtr& Fbo::getColorTexture(uint32_t slot) const
{
    static const Texture::SharedPtr s_Null;
    if (slot >= kMaxColorTargets)
        return s_Null;
    return m_Colors[slot];
}

Diligent::ITextureView* Fbo::getRenderTargetView(uint32_t slot) const
{
    if (m_pSwapChain)
        return slot == 0 ? m_pSwapChain->GetCurrentBackBufferRTV() : nullptr;
    if (slot >= kMaxColorTargets || !m_Colors[slot])
        return nullptr;
    return m_Colors[slot]->getRTV();
}

Diligent::ITextureView* Fbo::getDepthStencilView() const
{
    if (m_pSwapChain)
        return m_pSwapChain->GetDepthBufferDSV();
    return m_Depth ? m_Depth->getDSV() : nullptr;
}

uint32_t Fbo::getWidth() const
{
    if (m_pSwapChain)
        return m_pSwapChain->GetDesc().Width;
    for (const auto& color : m_Colors)
    {
        if (color)
            return color->getWidth();
    }
    return m_Depth ? m_Depth->getWidth() : 0;
}

uint32_t Fbo::getHeight() const
{
    if (m_pSwapChain)
        return m_pSwapChain->GetDesc().Height;
    for (const auto& color : m_Colors)
    {
        if (color)
            return color->getHeight();
    }
    return m_Depth ? m_Depth->getHeight() : 0;
}

bool Fbo::Formats::operator==(const Formats& rhs) const
{
    if (NumRenderTargets != rhs.NumRenderTargets || DSV != rhs.DSV)
        return false;
    for (uint32_t i = 0; i < NumRenderTargets; ++i)
    {
        if (RTVs[i] != rhs.RTVs[i])
            return false;
    }
    return true;
}

Fbo::Formats Fbo::getFormats() const
{
    Formats fmts;
    if (m_pSwapChain)
    {
        const Diligent::SwapChainDesc& scDesc = m_pSwapChain->GetDesc();
        fmts.RTVs[0]          = scDesc.ColorBufferFormat;
        fmts.NumRenderTargets = 1;
        fmts.DSV              = scDesc.DepthBufferFormat;
        return fmts;
    }
    for (uint32_t slot = 0; slot < kMaxColorTargets; ++slot)
    {
        if (m_Colors[slot])
        {
            fmts.RTVs[slot]       = m_Colors[slot]->getFormat();
            fmts.NumRenderTargets = slot + 1;
        }
    }
    if (m_Depth)
        fmts.DSV = m_Depth->getFormat();
    return fmts;
}

} // namespace ew
