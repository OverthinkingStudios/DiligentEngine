#include "Falcor.h"
#include "FalcorGpuInternal.hpp"

#include "GraphicsAccessories.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstring>
#include <unordered_set>
#include "ots/Log.hpp"
#include "terrafector.h"

namespace Falcor
{

std::vector<std::filesystem::path> g_DataDirectories;

namespace
{

Diligent::IRenderDevice*  g_pDevice     = nullptr;
Diligent::IDeviceContext* g_pContext    = nullptr;
Diligent::ISwapChain*     g_pSwapChain  = nullptr;
bool                      g_VSync       = true;

FrameworkInterface                 g_DefaultFramework;
DeviceInterface                    g_DefaultDevice;

void EFX_LOG_STUB(const char* msg) {
    static std::unordered_set<std::string> s_Logged;
    if (s_Logged.insert(msg).second)
        spdlog::debug(msg);
}

TEXTURE_FORMAT ToDiligentFormat(Falcor::ResourceFormat fmt) {
    const TEXTURE_FORMAT d = (TEXTURE_FORMAT)fmt;
    switch (d)
    {
        // 3-component 96-bit formats are valid on D3D12 (DXGI) but are NOT
        // supported as sampled/optimal-tiling images on Vulkan (e.g.
        // VK_FORMAT_R32G32B32_SFLOAT -> VK_ERROR_FORMAT_NOT_SUPPORTED), which
        // is why this only tripped Vulkan validation. Promote to the
        // 4-component equivalent. When init data is supplied it is repacked
        // from the tight 3-component layout to the 4-component layout in
        // BuildTextureInitData(); shaders that sample float3 just read .rgb and
        // ignore the added alpha channel.
      case Diligent::TEX_FORMAT_RGB32_FLOAT:
        return Diligent::TEX_FORMAT_RGBA32_FLOAT;
      case Diligent::TEX_FORMAT_RGB32_UINT:
        return Diligent::TEX_FORMAT_RGBA32_UINT;
      case Diligent::TEX_FORMAT_RGB32_SINT:
        return Diligent::TEX_FORMAT_RGBA32_SINT;
        default:                     
            return d;
    }
}

Falcor::ResourceFormat FromDiligentFormat(TEXTURE_FORMAT fmt) {
    return (Falcor::ResourceFormat)fmt;
}

void CreateTextureView(Diligent::ITexture* pTex, Diligent::TEXTURE_VIEW_TYPE viewType, Diligent::RefCntAutoPtr<Diligent::ITextureView>& outView)
{
    if (!pTex)
        return;
    Diligent::TextureViewDesc viewDesc;
    viewDesc.ViewType = viewType;
    pTex->CreateView(viewDesc, &outView);
}

} // namespace

DeviceInterface*    gpDevice    = &g_DefaultDevice;
FrameworkInterface* gpFramework = &g_DefaultFramework;

void SetFalcorDevice(Diligent::IRenderDevice* pDevice, Diligent::IDeviceContext* pContext, Diligent::ISwapChain* pSwapChain,
                     Diligent::IEngineFactory* pFactory)
{
    g_pDevice    = pDevice;
    g_pContext   = pContext;
    g_pSwapChain = pSwapChain;
    Gpu::OnSetFalcorDevice(pDevice, pContext, pSwapChain, pFactory);
}

void SetFalcorFramework(FrameworkInterface* pFramework)
{
    gpFramework = pFramework ? pFramework : &g_DefaultFramework;
}

std::vector<MonitorInfo::Desc> MonitorInfo::getMonitorDescs()
{
    Desc desc;
    if (g_pSwapChain)
    {
        const auto& scDesc = g_pSwapChain->GetDesc();
        desc.resolution    = glm::float2(static_cast<float>(scDesc.Width), static_cast<float>(scDesc.Height));
    }
#if defined(_WIN32) || defined(PLATFORM_WIN32)
    else
    {
        desc.resolution = glm::float2(static_cast<float>(GetSystemMetrics(SM_CXSCREEN)),
                                      static_cast<float>(GetSystemMetrics(SM_CYSCREEN)));
    }
#endif
    return {desc};
}

void addDataDirectory(const std::filesystem::path& path, bool prepend)
{
    if (prepend)
        g_DataDirectories.insert(g_DataDirectories.begin(), path);
    else
        g_DataDirectories.push_back(path);
    Gpu::RebuildShaderSearchPaths();
}

std::filesystem::path RemapShaderPath(const std::filesystem::path& falcorPath)
{
    std::string s = falcorPath.generic_string();
    const std::string kOldPrefix = "Samples/Earthworks_4/";
    if (s.rfind(kOldPrefix, 0) == 0)
        return std::filesystem::path(s.substr(kOldPrefix.size()));

    const std::string kOldPrefix2 = "Samples/Earthworks_4";
    if (s.rfind(kOldPrefix2, 0) == 0)
    {
        auto rem = s.substr(kOldPrefix2.size());
        if (!rem.empty() && rem.front() == '/')
            rem.erase(rem.begin());
        return std::filesystem::path(rem);
    }

    return falcorPath;
}

bool openFileDialog(const FileDialogFilterVec& filters, std::filesystem::path& selectedPath)
{
    (void)filters;
    (void)selectedPath;
    return false;
}

bool saveFileDialog(const FileDialogFilterVec& filters, std::filesystem::path& selectedPath)
{
    (void)filters;
    (void)selectedPath;
    return false;
}

double FrameRate::getAverageFrameTime() const
{
    if (gpFramework)
        return gpFramework->getAverageFrameTimeMs();
    return 16.0;
}

void WindowInterface::shutdown()
{
    // TODO: request SampleBase / AppBase shutdown.
}

FrameRate FrameworkInterface::getFrameRate() const
{
    return FrameRate{};
}

WindowInterface* FrameworkInterface::getWindow()
{
    static WindowInterface s_Window;
    return &s_Window;
}

void DeviceInterface::toggleVSync(bool enabled)
{
    g_VSync = enabled;
    if (g_pSwapChain)
        g_pSwapChain->SetMaximumFrameLatency(enabled ? 1 : 2);
}

RenderContext* DeviceInterface::getRenderContext()
{
    static RenderContext s_Context(nullptr);
    s_Context.attach(g_pContext);
    return &s_Context;
}

// --- Camera ------------------------------------------------------------------

Camera::SharedPtr Camera::create()
{
    return std::make_shared<Camera>();
}

void Camera::setDepthRange(float zNear, float zFar)
{
    m_Data.NearPlane = zNear;
    m_Data.FarPlane  = zFar;
}

void Camera::setAspectRatio(float aspect)
{
    m_Data.AspectRatio = aspect;
}

void Camera::setFocalLength(float focalMm)
{
    m_Data.FocalLength = focalMm;
}

void Camera::setPosition(const float3& pos)
{
    m_Data.Position = pos;
}

void Camera::setTarget(const float3& target)
{
    m_Target = target;
}

void Camera::setUpVector(const float3& up)
{
    (void)up;
    // View matrix is rebuilt from position/target; up is implicit in getViewMatrix().
}

void Camera::setFarPlane(float zFar)
{
    m_Data.FarPlane = zFar;
}

void Camera::setNearPlane(float zNear) {
    m_Data.NearPlane = zNear;
}

float4x4 Camera::getViewMatrix() const
{
    const float3 up{0.f, 1.f, 0.f};
    const float3 zAxis = glm::normalize(m_Target - m_Data.Position);
    const float3 xAxis = glm::normalize(glm::cross(up, zAxis));
    const float3 yAxis = glm::cross(zAxis, xAxis);

    float4x4 view = float4x4::Identity();
    view._11        = xAxis.x;
    view._12        = yAxis.x;
    view._13        = zAxis.x;
    view._21        = xAxis.y;
    view._22        = yAxis.y;
    view._23        = zAxis.y;
    view._31        = xAxis.z;
    view._32        = yAxis.z;
    view._33        = zAxis.z;
    view._41        = -glm::dot(xAxis, m_Data.Position);
    view._42        = -glm::dot(yAxis, m_Data.Position);
    view._43        = -glm::dot(zAxis, m_Data.Position);
    return view;
}

float4x4 Camera::getProjMatrix() const
{
    // Falcor convention (focalLengthToFovY): FrameHeight is the film-back
    // height in millimetres (24mm default), NOT a screen height in pixels.
    // fovY = 2*atan(0.5*frameHeight/focalLength) -> ~77.3 deg at 15mm.
    const float fovY = 2.f * std::atan(0.5f * m_Data.FrameHeight / m_Data.FocalLength);
    // Last argument is NegativeOneToOneZ, i.e. the OpenGL [-1,1] depth
    // convention. It MUST be false on Vulkan/D3D ([0,1] depth). It was
    // previously `m_Data.Position.y > 0.f`, which silently switched the depth
    // convention with camera height (BRINGUP_NOTES.md, F3).
    return float4x4::Projection(fovY, m_Data.AspectRatio, m_Data.NearPlane, m_Data.FarPlane, false);
}

float4x4 Camera::getViewProjMatrix() const
{
    return getViewMatrix() * getProjMatrix();
}

float3 Camera::getPosition() const
{
    return m_Data.Position;
}

bool Camera::isObjectCulled(const AABB&) const
{
    return false;
}

// --- ShaderVar / vars --------------------------------------------------------

ShaderVar::ShaderVar(std::shared_ptr<Node> node) : m_pNode(std::move(node)) {}

ShaderVar ShaderVar::operator[](const char* name)
{
    if (!m_pNode)
        m_pNode = std::make_shared<Node>();
    auto& child = m_pNode->Children[name];
    if (!child)
        child = std::make_shared<Node>();
    child->Name = name;
    return ShaderVar(child);
}

ShaderVar ShaderVar::operator[](const char* name) const
{
    return const_cast<ShaderVar*>(this)->operator[](name);
}

ShaderVar ShaderVar::operator[](size_t index)
{
    const std::string key = std::to_string(index);
    return (*this)[key.c_str()];
}

ShaderVar ShaderVar::operator[](size_t index) const
{
    return const_cast<ShaderVar*>(this)->operator[](index);
}

ShaderVar& ShaderVar::operator=(const Falcor::SharedPtr<Texture>& tex)
{
    if (!m_pNode)
        m_pNode = std::make_shared<Node>();
    m_pNode->TextureValue = tex;
    m_pNode->BufferValue  = nullptr;
    m_pNode->ScalarData.clear();
    return *this;
}

ShaderVar& ShaderVar::operator=(const Falcor::SharedPtr<Buffer>& buf)
{
    if (!m_pNode)
        m_pNode = std::make_shared<Node>();
    m_pNode->BufferValue  = buf;
    m_pNode->TextureValue = nullptr;
    m_pNode->ScalarData.clear();
    return *this;
}

ShaderVar& ShaderVar::assignScalar(const void* data, size_t size)
{
    if (!data || size == 0)
        return *this;
    if (!m_pNode)
        m_pNode = std::make_shared<Node>();
    m_pNode->TextureValue = nullptr;
    m_pNode->BufferValue  = nullptr;
    m_pNode->ScalarData.resize(size);
    std::memcpy(m_pNode->ScalarData.data(), data, size);
    return *this;
}

ShaderVar::operator Falcor::SharedPtr<Texture>() const
{
    return m_pNode ? m_pNode->TextureValue : nullptr;
}

ShaderVar::operator Falcor::SharedPtr<Buffer>() const
{
    return m_pNode ? m_pNode->BufferValue : nullptr;
}

ComputeVars::SharedPtr ComputeVars::create(ComputeProgram*)
{
    return std::make_shared<ComputeVars>();
}

ShaderVar ComputeVars::getRootVar()
{
    if (!m_pData)
        m_pData = std::make_shared<ComputeVarsData>();
    return ShaderVar(m_pBlockRoot ? m_pBlockRoot : m_pData->Root);
}

ShaderVar ComputeVars::getRootVar() const
{
    return const_cast<ComputeVars*>(this)->getRootVar();
}

ShaderVar ComputeVars::operator[](const char* name)
{
    return getRootVar()[name];
}

ShaderVar ComputeVars::operator[](const char* name) const
{
    return const_cast<ComputeVars*>(this)->operator[](name);
}

void ComputeVars::setTexture(const char* name, const Falcor::SharedPtr<Texture>& pTex)
{
    if (!m_pData)
        m_pData = std::make_shared<ComputeVarsData>();
    m_pData->Textures[name] = pTex;
}

void ComputeVars::setSampler(const char* name, const Falcor::SharedPtr<Sampler>& pSampler)
{
    if (!m_pData)
        m_pData = std::make_shared<ComputeVarsData>();
    m_pData->Samplers[name] = pSampler;
}

void ComputeVars::setBuffer(const char* name, const Falcor::SharedPtr<Buffer>& pBuffer)
{
    if (!m_pData)
        m_pData = std::make_shared<ComputeVarsData>();
    m_pData->Buffers[name] = pBuffer;
}

void ComputeVars::setBlob(const void* pData, size_t offset, size_t size)
{
    if (!pData || size == 0)
        return;
    if (!m_pData)
        m_pData = std::make_shared<ComputeVarsData>();
    // Key the bytes by the parameter block (cbuffer) this vars object was
    // created for. Previously all raw blobs landed in ONE shared vector whose
    // target cbuffer was unknown at bind time, so BindComputeVars silently
    // dropped them and the shader got zeros (this is what starved
    // compute_tileBuildLookup of frustumFlags -> instanceCount 0 -> no terrain).
    const std::string blockName = m_pBlockRoot ? m_pBlockRoot->Name : std::string();
    if (blockName.empty())
        spdlog::warn("ComputeVars::setBlob without a parameter block - data will not reach any cbuffer");
    auto&        blob = m_pData->Blobs[blockName];
    const size_t end  = offset + size;
    if (blob.size() < end)
        blob.resize(end);
    std::memcpy(blob.data() + offset, pData, size);
}

ComputeVars::SharedPtr ComputeVars::getParameterBlock(const char* name)
{
    const ShaderVar blockNode = getRootVar()[name];
    auto          block       = std::make_shared<ComputeVars>();
    block->m_pData            = m_pData;
    block->m_pBlockRoot       = blockNode.m_pNode;
    return block;
}

ShaderVar ComputeVars::findMember(const char* name)
{
    return getRootVar()[name];
}

ShaderVar ComputeVars::findMember(const char* name) const
{
    return const_cast<ComputeVars*>(this)->findMember(name);
}

GraphicsVars::SharedPtr GraphicsVars::create(const ProgramReflection*)
{
    return std::make_shared<GraphicsVars>();
}

GraphicsVars::SharedPtr GraphicsVars::create(ProgramReflection::SharedConstPtr pReflection)
{
    return create(pReflection.get());
}

ShaderVar GraphicsVars::getRootVar()
{
    if (!m_pData)
        m_pData = std::make_shared<GraphicsVarsData>();
    return ShaderVar(m_pBlockRoot ? m_pBlockRoot : m_pData->Root);
}

ShaderVar GraphicsVars::getRootVar() const
{
    return const_cast<GraphicsVars*>(this)->getRootVar();
}

ShaderVar GraphicsVars::operator[](const char* name)
{
    return getRootVar()[name];
}

ShaderVar GraphicsVars::operator[](const char* name) const
{
    return const_cast<GraphicsVars*>(this)->operator[](name);
}

void GraphicsVars::setTexture(const char* name, const Falcor::SharedPtr<Texture>& pTex)
{
    if (!m_pData)
        m_pData = std::make_shared<GraphicsVarsData>();
    m_pData->Textures[name] = pTex;
}

void GraphicsVars::setSampler(const char* name, const Falcor::SharedPtr<Sampler>& pSampler)
{
    if (!m_pData)
        m_pData = std::make_shared<GraphicsVarsData>();
    m_pData->Samplers[name] = pSampler;
}

void GraphicsVars::setBuffer(const char* name, const Falcor::SharedPtr<Buffer>& pBuffer)
{
    if (!m_pData)
        m_pData = std::make_shared<GraphicsVarsData>();
    m_pData->Buffers[name] = pBuffer;
}

GraphicsVars::SharedPtr GraphicsVars::getParameterBlock(const char* name)
{
    const ShaderVar blockNode = getRootVar()[name];
    auto          block       = std::make_shared<GraphicsVars>();
    block->m_pData            = m_pData;
    block->m_pBlockRoot       = blockNode.m_pNode;
    return block;
}

ShaderVar GraphicsVars::findMember(const char* name)
{
    return getRootVar()[name];
}

ShaderVar GraphicsVars::findMember(const char* name) const
{
    return const_cast<GraphicsVars*>(this)->findMember(name);
}

// --- Resources ---------------------------------------------------------------

namespace
{
// Builds single-subresource (mip 0, slice 0) init data for a texture being
// created with CreateTexture(). When the requested format was promoted by
// ToDiligentFormat() (e.g. RGB32 -> RGBA32), the tightly-packed source texels
// are repacked into the wider destination layout in 'repackStorage'; otherwise
// the source pointer is used directly. Returns false when there is nothing to
// upload (null data or unknown/zero pixel size).
bool BuildTextureInitData(Falcor::ResourceFormat       srcFormat,
                          Diligent::TEXTURE_FORMAT      dstFormat,
                          uint32_t                      width,
                          uint32_t                      height,
                          uint32_t                      depth,
                          const void*                   pData,
                          std::vector<uint8_t>&         repackStorage,
                          Diligent::TextureSubResData&  subres)
{
    if (!pData)
        return false;

    const Diligent::TEXTURE_FORMAT srcDiligent = static_cast<Diligent::TEXTURE_FORMAT>(srcFormat);

    const auto&    dstAttribs   = Diligent::GetTextureFormatAttribs(dstFormat);
    const uint32_t dstPixelSize = uint32_t(dstAttribs.ComponentSize) * uint32_t(dstAttribs.NumComponents);
    if (dstPixelSize == 0)
        return false; // compressed / unknown layout - not handled here

    const void* uploadPtr = pData;

    if (srcDiligent != dstFormat)
    {
        // Format was promoted: repack each texel from the tight source layout
        // into the wider destination layout, zero-filling the extra channel.
        const auto&    srcAttribs   = Diligent::GetTextureFormatAttribs(srcDiligent);
        const uint32_t srcPixelSize = uint32_t(srcAttribs.ComponentSize) * uint32_t(srcAttribs.NumComponents);
        if (srcPixelSize == 0)
            return false;

        const uint32_t copyBytes  = std::min(srcPixelSize, dstPixelSize);
        const size_t   texelCount = size_t(width) * size_t(height) * size_t(std::max<uint32_t>(depth, 1u));

        repackStorage.assign(texelCount * dstPixelSize, uint8_t{0});
        const uint8_t* src = static_cast<const uint8_t*>(pData);
        uint8_t*       dst = repackStorage.data();
        for (size_t i = 0; i < texelCount; ++i)
            std::memcpy(dst + i * dstPixelSize, src + i * srcPixelSize, copyBytes);

        uploadPtr = repackStorage.data();
    }

    subres.pData       = uploadPtr;
    subres.Stride      = uint64_t(width) * dstPixelSize;
    subres.DepthStride = uint64_t(width) * uint64_t(height) * dstPixelSize;
    return true;
}
} // namespace

Texture::SharedPtr Texture::create2D(uint32_t width, uint32_t height, TEXTURE_FORMAT format, uint32_t arraySize, uint32_t mipLevels, const void* pData, uint32_t bindFlags)
{
    return create2D(width, height, static_cast<ResourceFormat>(format), arraySize, mipLevels, pData, bindFlags);
}

Texture::SharedPtr Texture::create2D(uint32_t width, uint32_t height, Falcor::ResourceFormat format, uint32_t arraySize, uint32_t mipLevels, const void* pData, uint32_t bindFlags)
{
    (void)arraySize;
    (void)mipLevels;
    auto tex     = std::make_shared<Texture>();
    tex->m_Width = width;
    tex->m_Height = height;
    tex->m_Format = ToDiligentFormat(format);
    tex->m_MipCount = 1;
    if (!g_pDevice)
    {
        EFX_LOG_STUB("Texture::create2D without device");
        return tex;
    }

    Diligent::TextureDesc desc;
    desc.Type      = Diligent::RESOURCE_DIM_TEX_2D;
    desc.Width     = width;
    desc.Height    = height;
    desc.Format    = ToDiligentFormat(format);
    desc.BindFlags = static_cast<Diligent::BIND_FLAGS>(bindFlags);
    desc.MipLevels = 1;

    std::vector<uint8_t>         initStorage;
    Diligent::TextureSubResData  subres;
    Diligent::TextureData        initData;
    Diligent::TextureData*       pInitData = nullptr;
    if (BuildTextureInitData(format, desc.Format, width, height, 1, pData, initStorage, subres))
    {
        initData.pSubResources   = &subres;
        initData.NumSubresources = 1;
        initData.pContext        = g_pContext;
        pInitData                = &initData;
    }
    g_pDevice->CreateTexture(desc, pInitData, &tex->m_pTexture);
    if (tex->m_pTexture)
    {
        if (bindFlags & Resource::BindFlags::ShaderResource)
            CreateTextureView(tex->m_pTexture, Diligent::TEXTURE_VIEW_SHADER_RESOURCE, tex->m_SRV);
        if (bindFlags & Resource::BindFlags::RenderTarget)
            CreateTextureView(tex->m_pTexture, Diligent::TEXTURE_VIEW_RENDER_TARGET, tex->m_RTV);
        if (desc.BindFlags & Diligent::BIND_DEPTH_STENCIL)
            CreateTextureView(tex->m_pTexture, Diligent::TEXTURE_VIEW_DEPTH_STENCIL, tex->m_DSV);
        if (bindFlags & Resource::BindFlags::UnorderedAccess)
            CreateTextureView(tex->m_pTexture, Diligent::TEXTURE_VIEW_UNORDERED_ACCESS, tex->m_UAV);
    }
    return tex;
}

Texture::SharedPtr Texture::create3D(uint32_t width, uint32_t height, uint32_t depth, Falcor::ResourceFormat format, uint32_t, const void* pData, uint32_t bindFlags)
{
    auto tex     = std::make_shared<Texture>();
    tex->m_Width = width;
    tex->m_Height = height;
    tex->m_Format = ToDiligentFormat(format);
    tex->m_MipCount = 1;
    if (!g_pDevice)
        return tex;

    Diligent::TextureDesc desc;
    desc.Type      = Diligent::RESOURCE_DIM_TEX_3D;
    desc.Width     = width;
    desc.Height    = height;
    desc.Depth     = depth;
    desc.Format    = ToDiligentFormat(format);
    desc.BindFlags = static_cast<Diligent::BIND_FLAGS>(bindFlags);
    desc.MipLevels = 1;

    std::vector<uint8_t>         initStorage;
    Diligent::TextureSubResData  subres;
    Diligent::TextureData        initData;
    Diligent::TextureData*       pInitData = nullptr;
    if (BuildTextureInitData(format, desc.Format, width, height, depth, pData, initStorage, subres))
    {
        initData.pSubResources   = &subres;
        initData.NumSubresources = 1;
        initData.pContext        = g_pContext;
        pInitData                = &initData;
    }
    g_pDevice->CreateTexture(desc, pInitData, &tex->m_pTexture);
    if (tex->m_pTexture && (bindFlags & Resource::BindFlags::ShaderResource))
        CreateTextureView(tex->m_pTexture, Diligent::TEXTURE_VIEW_SHADER_RESOURCE, tex->m_SRV);
    if (tex->m_pTexture && (bindFlags & Resource::BindFlags::UnorderedAccess))
        CreateTextureView(tex->m_pTexture, Diligent::TEXTURE_VIEW_UNORDERED_ACCESS, tex->m_UAV);
    return tex;
}

Texture::SharedPtr Texture::createCube(uint32_t size, Falcor::ResourceFormat format, uint32_t bindFlags)
{
    auto tex      = std::make_shared<Texture>();
    tex->m_Width  = size;
    tex->m_Height = size;
    tex->m_Format = ToDiligentFormat(format);
    tex->m_MipCount = 1;
    if (!g_pDevice)
        return tex;

    Diligent::TextureDesc desc;
    desc.Type      = Diligent::RESOURCE_DIM_TEX_CUBE;
    desc.Width     = size;
    desc.Height    = size;
    desc.ArraySize = 6;
    desc.Format    = ToDiligentFormat(format);
    desc.BindFlags = static_cast<Diligent::BIND_FLAGS>(bindFlags);
    desc.MipLevels = 1;
    g_pDevice->CreateTexture(desc, nullptr, &tex->m_pTexture);
    if (tex->m_pTexture && (bindFlags & Resource::BindFlags::ShaderResource))
        CreateTextureView(tex->m_pTexture, Diligent::TEXTURE_VIEW_SHADER_RESOURCE, tex->m_SRV);
    if (tex->m_pTexture && (bindFlags & Resource::BindFlags::UnorderedAccess))
        CreateTextureView(tex->m_pTexture, Diligent::TEXTURE_VIEW_UNORDERED_ACCESS, tex->m_UAV);
    return tex;
}

Texture::SharedPtr Texture::createFromFile(const char* path, bool, bool, Resource::BindFlags bind_flags)
{
    (void)path;
    return create2D(1, 1, static_cast<TEXTURE_FORMAT>(ResourceFormat::RGBA8Unorm), 1, 1, nullptr, bind_flags);
}

Texture::SharedPtr Texture::createFromFile(const std::filesystem::path& path, bool srgb, bool generateMips, Resource::BindFlags bind_flags)
{
    return createFromFile(path.string().c_str(), srgb, generateMips, bind_flags);
}

uint32_t Texture::getWidth() const { return m_Width; }
uint32_t Texture::getHeight() const { return m_Height; }

Diligent::ITextureView* Texture::getSRV(uint32_t, uint32_t, uint32_t, uint32_t) const { return m_SRV; }
Diligent::ITextureView* Texture::getRTV() const { return m_RTV; }
Diligent::ITextureView* Texture::getDSV() const { return m_DSV; }
Diligent::ITextureView* Texture::getUAV(uint32_t) const { return m_UAV; }

void Texture::generateMips(RenderContext* pContext)
{
    (void)pContext;
    EFX_LOG_STUB("Texture::generateMips stub");
}

void Texture::captureToFile(uint32_t mipLevel, uint32_t slice, const std::string& path)
{
    (void)mipLevel;
    (void)slice;
    (void)path;
    EFX_LOG_STUB("Texture::captureToFile stub");
}

void Texture::captureToFile(uint32_t mipLevel, uint32_t slice, const std::string& path, Bitmap::FileFormat format, Bitmap::ExportFlags flags)
{
    (void)format;
    (void)flags;
    captureToFile(mipLevel, slice, path);
}

uint32_t Texture::getSubresourceIndex(uint32_t mipLevel, uint32_t arraySlice) const
{
    return mipLevel + arraySlice * m_MipCount;
}

namespace
{
Diligent::USAGE GetBufferUsage(Buffer::CpuAccess cpuAccess)
{
    if (cpuAccess == Buffer::CpuAccess::Read)
        return Diligent::USAGE_STAGING;
    if (cpuAccess == Buffer::CpuAccess::Write)
        return Diligent::USAGE_DYNAMIC;
    return Diligent::USAGE_DEFAULT;
}

Diligent::CPU_ACCESS_FLAGS GetCpuAccessFlags(Buffer::CpuAccess cpuAccess)
{
    if (cpuAccess == Buffer::CpuAccess::Read)
        return Diligent::CPU_ACCESS_READ;
    if (cpuAccess == Buffer::CpuAccess::Write)
        return Diligent::CPU_ACCESS_WRITE;
    return Diligent::CPU_ACCESS_NONE;
}
} // namespace
Falcor::SharedPtr<Buffer> CreateFalcorBuffer(size_t size, Diligent::BIND_FLAGS bindFlags, Buffer::CpuAccess cpuAccess, const void* pInitData, bool structured, uint32_t structStride)
{
    auto buf = std::make_shared<Buffer>();
    buf->m_Size = size;
    if (pInitData && size > 0)
        buf->m_CpuShadow.assign(static_cast<const uint8_t*>(pInitData), static_cast<const uint8_t*>(pInitData) + size);

    if (!g_pDevice)
    {
        EFX_LOG_STUB("CreateFalcorBuffer without device");
        return buf;
    }

    Diligent::BufferDesc desc;
    desc.Size           = size;
    desc.BindFlags      = bindFlags;
    desc.Usage          = GetBufferUsage(cpuAccess);
    desc.CPUAccessFlags = GetCpuAccessFlags(cpuAccess);
    if (structured)
    {
        desc.Mode              = Diligent::BUFFER_MODE_STRUCTURED;
        desc.ElementByteStride = structStride;
    }

    Diligent::BufferData init{};
    const bool           uploadViaInit = pInitData &&
        desc.Usage != Diligent::USAGE_STAGING &&
        desc.Usage != Diligent::USAGE_DYNAMIC;
    if (uploadViaInit)
    {
        init.pData    = pInitData;
        init.DataSize = size;
    }

    g_pDevice->CreateBuffer(desc, uploadViaInit ? &init : nullptr, &buf->m_pBuffer);

    if (pInitData && buf->m_pBuffer && g_pContext)
    {
        if (desc.Usage == Diligent::USAGE_STAGING)
        {
            g_pContext->UpdateBuffer(buf->m_pBuffer, 0, static_cast<Diligent::Uint64>(size), pInitData, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }
        else if (desc.Usage == Diligent::USAGE_DYNAMIC)
        {
            void* pMapped = nullptr;
            g_pContext->MapBuffer(buf->m_pBuffer, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD, pMapped);
            if (pMapped)
            {
                std::memcpy(pMapped, pInitData, size);
                g_pContext->UnmapBuffer(buf->m_pBuffer, Diligent::MAP_WRITE);
            }
        }
    }
    return buf;
}

Buffer::SharedPtr Buffer::create(size_t size, BindFlags bindFlags, CpuAccess cpuAccess, const void* pInitData)
{
    return CreateFalcorBuffer(size, static_cast<Diligent::BIND_FLAGS>(bindFlags), cpuAccess, pInitData, false, 0);
}

Buffer::SharedPtr Buffer::create(size_t size, Resource::BindFlags bindFlags, CpuAccess cpuAccess, const void* pInitData)
{
    return CreateFalcorBuffer(size, static_cast<Diligent::BIND_FLAGS>(bindFlags), cpuAccess, pInitData, false, 0);
}

Buffer::SharedPtr Buffer::createStructured(size_t structSize, size_t elementCount)
{
    return createStructured(structSize, elementCount, static_cast<Resource::BindFlags>(Resource::BindFlags::UnorderedAccess | Resource::BindFlags::ShaderResource));
}

Buffer::SharedPtr Buffer::createStructured(size_t structSize, size_t elementCount, Resource::BindFlags bindFlags)
{
    const size_t size = structSize * elementCount;
    return CreateFalcorBuffer(size, static_cast<Diligent::BIND_FLAGS>(bindFlags), CpuAccess::None, nullptr, true, static_cast<uint32_t>(structSize));
}

Buffer::SharedPtr Buffer::createStructured(size_t structSize, size_t elementCount, uint32_t bindFlags)
{
    return createStructured(structSize, elementCount, static_cast<Resource::BindFlags>(bindFlags));
}

Buffer::SharedPtr Buffer::createStructured(size_t structSize, size_t elementCount, Resource::BindFlags bindFlags, CpuAccess cpuAccess, const void* pInitData)
{
    const size_t size = structSize * elementCount;
    return CreateFalcorBuffer(size, static_cast<Diligent::BIND_FLAGS>(bindFlags), cpuAccess, pInitData, true, static_cast<uint32_t>(structSize));
}

Buffer::SharedPtr Buffer::createTyped(Falcor::ResourceFormat format, size_t sizeInBytes, Resource::BindFlags bindFlags)
{
    (void)format;
    return CreateFalcorBuffer(sizeInBytes, static_cast<Diligent::BIND_FLAGS>(bindFlags), CpuAccess::None, nullptr, false, 0);
}



void Buffer::ensureCpuShadow(size_t size)
{
    if (m_CpuShadow.size() < size)
        m_CpuShadow.resize(size);
}

void Buffer::uploadShadowRange(size_t offset, size_t size)
{
    if (!g_pContext || !m_pBuffer || size == 0)
        return;
    if (offset + size > m_CpuShadow.size())
        return;

    // USAGE_DYNAMIC buffers live in an upload heap (GENERIC_READ) and CANNOT be
    // updated with UpdateBuffer: on D3D12 that turns into a CopyBufferRegion into
    // the dynamic page (src==dst / invalid COPY_DEST state -> device removed).
    // They must be refreshed via Map(WRITE, DISCARD). DISCARD invalidates the old
    // contents, so we re-upload the whole buffer from the full CPU shadow copy
    // (which always holds the complete intended contents). Vulkan tolerated the
    // illegal UpdateBuffer, which is why this only crashed on D3D12.
    if (m_pBuffer->GetDesc().Usage == Diligent::USAGE_DYNAMIC)
    {
        void* pMapped = nullptr;
        g_pContext->MapBuffer(m_pBuffer, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD, pMapped);
        if (pMapped)
        {
            const size_t bufSize  = static_cast<size_t>(m_pBuffer->GetDesc().Size);
            const size_t copySize = m_CpuShadow.size() < bufSize ? m_CpuShadow.size() : bufSize;
            std::memcpy(pMapped, m_CpuShadow.data(), copySize);
            g_pContext->UnmapBuffer(m_pBuffer, Diligent::MAP_WRITE);
        }
        return;
    }

    g_pContext->UpdateBuffer(m_pBuffer, offset, static_cast<Diligent::Uint64>(size), m_CpuShadow.data() + offset, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void Buffer::setBlob(const void* pData, size_t offset, size_t size)
{
    if (!pData || size == 0)
        return;
    ensureCpuShadow(offset + size);
    std::memcpy(m_CpuShadow.data() + offset, pData, size);
    uploadShadowRange(offset, size);
}

void Buffer::setBlob(const void* pData, size_t offset, size_t size, size_t count)
{
    (void)count;
    setBlob(pData, offset, size);
}

void Buffer::uploadToGPU(size_t offset, size_t size)
{
    if (size == 0)
        size = m_CpuShadow.size() - offset;
    uploadShadowRange(offset, size);
}

void* Buffer::map(MapType type)
{
    m_LastMapType = type;
    if (m_pBuffer && g_pContext)
    {
        void* pData = nullptr;
        if (type == MapType::Read)
        {
            // Diligent's D3D12/Vulkan backends do NOT auto-synchronize CPU readback:
            // MapBuffer(MAP_READ) returns immediately with whatever currently sits in
            // the staging buffer and logs a warning on every call. The Earthworks
            // readback pattern is copyResource(staging, gpuBuffer) followed straight
            // away by map(Read) (terrain tile-center + GC_feedback readback in
            // terrainManager::update / onFrameRender). Without an explicit wait the CPU
            // reads stale/zero data - corrupting tile bounding-sphere heights, split/
            // merge and mouse-pick decisions - and floods the log. WaitForIdle()
            // guarantees the preceding copy has completed (this is the sync method the
            // Diligent docs mandate for readback, and it matches Falcor's original
            // blocking map semantics; it also implicitly flushes). MAP_FLAG_DO_NOT_WAIT
            // then tells Diligent we've synchronized ourselves, silencing the warning.
            //
            // NOTE: WaitForIdle() is a full GPU stall. That is acceptable for these
            // once-per-frame readbacks during bring-up; a fence + N-frame-latency ring
            // of staging buffers would remove the stall later if it shows up in profiling.
            g_pContext->WaitForIdle();
            g_pContext->MapBuffer(m_pBuffer, Diligent::MAP_READ, Diligent::MAP_FLAG_DO_NOT_WAIT, pData);
            return pData;
        }
        g_pContext->MapBuffer(m_pBuffer, Diligent::MAP_WRITE, Diligent::MAP_FLAG_NONE, pData);
        return pData;
    }
    ensureCpuShadow(m_Size);
    return m_CpuShadow.data();
}

void Buffer::unmap()
{
    if (m_pBuffer && g_pContext)
        g_pContext->UnmapBuffer(m_pBuffer, m_LastMapType == MapType::Read ? Diligent::MAP_READ : Diligent::MAP_WRITE);
}

Buffer::SharedPtr Buffer::getUAVCounter()
{
    if (!m_pUavCounter)
        m_pUavCounter = createStructured(sizeof(uint32_t), 1, Resource::BindFlags::UnorderedAccess);
    return m_pUavCounter;
}


Fbo::Desc& Fbo::Desc::setColorTarget(uint32_t slot, Falcor::ResourceFormat format)
{
    if (slot < m_ColorFormats.size())
        m_ColorFormats[slot] = ToDiligentFormat(format);
    return *this;
}

Fbo::Desc& Fbo::Desc::setColorTarget(uint32_t slot, Diligent::TEXTURE_FORMAT format)
{
    if (slot < m_ColorFormats.size())
        m_ColorFormats[slot] = format;
    return *this;
}

Fbo::Desc& Fbo::Desc::setColorTarget(uint32_t slot, Falcor::ResourceFormat format, bool)
{
    return setColorTarget(slot, format);
}

Fbo::Desc& Fbo::Desc::setDepthStencilTarget(Falcor::ResourceFormat falcor_format)
{
    m_DepthFormat = ToDiligentFormat(falcor_format);
    m_HasDepth    = true;
    return *this;
}

Fbo::Desc& Fbo::Desc::setDepthStencilTarget(Diligent::TEXTURE_FORMAT format)
{
    m_DepthFormat = format;
    m_HasDepth    = true;
    return *this;
}

Falcor::ResourceFormat Fbo::Desc::GetColorFormat(uint32_t slot) const
{
    return FromDiligentFormat(slot < m_ColorFormats.size() ? m_ColorFormats[slot] : TEX_FORMAT_UNKNOWN);
}

Falcor::ResourceFormat Fbo::Desc::GetDepthFormat() const
{
    return  FromDiligentFormat(m_DepthFormat);
}

Fbo::SharedPtr Fbo::create2D(uint32_t width, uint32_t height, const Desc& desc)
{
    auto fbo     = std::make_shared<Fbo>();
    fbo->m_Width = width;
    fbo->m_Height = height;
    for (uint32_t slot = 0; slot < fbo->m_ColorTextures.size(); ++slot)
    {
        const TEXTURE_FORMAT fmt = ToDiligentFormat(desc.GetColorFormat(slot));
        if (fmt != TEX_FORMAT_UNKNOWN)
        {
            // Earthworks binds FBO color targets as compute UAVs too
            // (compute_tileBicubic writes gOutput = tileFbo color0, the tile
            // ecotope/normal passes write colors 1..7). Falcor created FBO
            // textures with AllColorViews, so replicate that here whenever the
            // device supports UAV access for the format; without it getUAV()
            // is null and the compute writes silently go nowhere (this kept
            // the elevation target at its 0.3 clear value -> flat terrain).
            uint32_t bind = Resource::BindFlags::RenderTarget | Resource::BindFlags::ShaderResource;
            if (g_pDevice && (g_pDevice->GetTextureFormatInfoExt(fmt).BindFlags & Diligent::BIND_UNORDERED_ACCESS))
                bind |= Resource::BindFlags::UnorderedAccess;
            fbo->m_ColorTextures[slot] = Texture::create2D(width, height, fmt, 1, 1, nullptr, bind);
        }
    }
    if (desc.GetDepthFormat() != TEX_FORMAT_UNKNOWN)
    {
        // BIND_SHADER_RESOURCE in addition to BIND_DEPTH_STENCIL so depth FBOs
        // (e.g. shadow maps) can be sampled in shaders. Diligent automatically
        // creates the texture with a typeless resource format and selects the
        // depth-readable SRV format (R32_FLOAT / R24_UNORM_X8 / ...).
        fbo->m_DepthTexture = Texture::create2D(
            width, height, desc.GetDepthFormat(), 1, 1, nullptr,
            Diligent::BIND_DEPTH_STENCIL | Diligent::BIND_SHADER_RESOURCE);
    }
    return fbo;
}

Fbo::SharedPtr Fbo::create2D(uint32_t width, uint32_t height, const Desc& desc, uint32_t arraySize, uint32_t sampleCount)
{
    (void)arraySize;
    (void)sampleCount;
    return create2D(width, height, desc);
}

Fbo::SharedPtr Fbo::createFromSwapChain(Diligent::ISwapChain* pSwapChain)
{
    auto fbo = std::make_shared<Fbo>();
    if (!pSwapChain)
        return fbo;

    const auto& scDesc = pSwapChain->GetDesc();
    fbo->m_Width           = scDesc.Width;
    fbo->m_Height          = scDesc.Height;
    fbo->m_pSwapChain      = pSwapChain;
    fbo->m_IsSwapChainProxy = true;
    return fbo;
}

Texture::SharedPtr Fbo::getColorTexture(uint32_t slot) const
{
    return slot < m_ColorTextures.size() ? m_ColorTextures[slot] : nullptr;
}

Texture::SharedPtr Fbo::getDepthStencilTexture() const
{
    return m_DepthTexture;
}

Diligent::ITextureView* Fbo::getRenderTargetView(uint32_t slot) const
{
    if (m_IsSwapChainProxy && m_pSwapChain && slot == 0)
        return m_pSwapChain->GetCurrentBackBufferRTV();
    const auto tex = getColorTexture(slot);
    return tex ? tex->getRTV() : nullptr;
}

Diligent::ITextureView* Fbo::getDepthStencilView() const
{
    if (m_IsSwapChainProxy && m_pSwapChain)
        return m_pSwapChain->GetDepthBufferDSV();
    return m_DepthTexture ? m_DepthTexture->getDSV() : nullptr;
}

Sampler::Desc& Sampler::Desc::setAddressingMode(AddressMode u, AddressMode v, AddressMode w)
{
    auto mapMode = [](AddressMode mode) {
        switch (mode)
        {
        case AddressMode::Wrap: return Diligent::TEXTURE_ADDRESS_WRAP;
        case AddressMode::Mirror: return Diligent::TEXTURE_ADDRESS_MIRROR;
        case AddressMode::Clamp: return Diligent::TEXTURE_ADDRESS_CLAMP;
        default: return Diligent::TEXTURE_ADDRESS_BORDER;
        }
    };
    m_Desc.AddressU = mapMode(u);
    m_Desc.AddressV = mapMode(v);
    m_Desc.AddressW = mapMode(w);
    return *this;
}

Sampler::Desc& Sampler::Desc::setFilterMode(Filter min, Filter mag, Filter mip)
{
    auto mapFilter = [](Filter f) { return f == Filter::Linear ? Diligent::FILTER_TYPE_LINEAR : Diligent::FILTER_TYPE_POINT; };
    m_Desc.MinFilter = mapFilter(min);
    m_Desc.MagFilter = mapFilter(mag);
    m_Desc.MipFilter = mapFilter(mip);
    return *this;
}

Sampler::Desc& Sampler::Desc::setMaxAnisotropy(uint32_t aniso)
{
    m_Desc.MaxAnisotropy = aniso;
    return *this;
}

Sampler::Desc& Sampler::Desc::setComparisonMode(ComparisonMode mode)
{
    switch (mode)
    {
    case ComparisonMode::LessEqual: m_Desc.ComparisonFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL; break;
    default: m_Desc.ComparisonFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL; break;
    }
    // A comparison sampler (HLSL SamplerComparisonState / SampleCmp) requires the
    // filter type itself to be a COMPARISON filter, otherwise D3D12 ignores the
    // comparison func and the shadow lookup returns garbage. Promote whatever
    // filter was previously selected to its comparison variant. Note this relies
    // on setComparisonMode() being called after setFilterMode().
    auto toComparison = [](Diligent::FILTER_TYPE f) {
        switch (f)
        {
        case Diligent::FILTER_TYPE_POINT:
        case Diligent::FILTER_TYPE_COMPARISON_POINT:
            return Diligent::FILTER_TYPE_COMPARISON_POINT;
        default:
            return Diligent::FILTER_TYPE_COMPARISON_LINEAR;
        }
    };
    m_Desc.MinFilter = toComparison(m_Desc.MinFilter);
    m_Desc.MagFilter = toComparison(m_Desc.MagFilter);
    m_Desc.MipFilter = toComparison(m_Desc.MipFilter);
    return *this;
}

Sampler::SharedPtr Sampler::create(const Desc& desc)
{
    auto sampler = std::make_shared<Sampler>();
    if (g_pDevice)
        g_pDevice->CreateSampler(desc.m_Desc, &sampler->m_pSampler);
    return sampler;
}

BlendState::Desc& BlendState::Desc::setRtBlend(uint32_t rt, bool enable)
{
    if (rt < Diligent::MAX_RENDER_TARGETS)
        m_Desc.RenderTargets[rt].BlendEnable = enable;
    return *this;
}

BlendState::Desc& BlendState::Desc::setRtParams(uint32_t rt, BlendOp, BlendOp, BlendFunc srcColor, BlendFunc dstColor, BlendFunc srcAlpha, BlendFunc dstAlpha)
{
    if (rt >= Diligent::MAX_RENDER_TARGETS)
        return *this;
    auto& target = m_Desc.RenderTargets[rt];
    auto mapFunc = [](BlendFunc f) {
        switch (f)
        {
        case BlendFunc::Zero: return Diligent::BLEND_FACTOR_ZERO;
        case BlendFunc::One: return Diligent::BLEND_FACTOR_ONE;
        case BlendFunc::OneMinusSrcAlpha: return Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
        case BlendFunc::SrcAlphaSaturate: return Diligent::BLEND_FACTOR_SRC_ALPHA_SAT;
        default: return Diligent::BLEND_FACTOR_SRC_ALPHA;
        }
    };
    target.SrcBlend  = mapFunc(srcColor);
    target.DestBlend = mapFunc(dstColor);
    target.SrcBlendAlpha = mapFunc(srcAlpha);
    target.DestBlendAlpha = mapFunc(dstAlpha);
    return *this;
}

BlendState::Desc& BlendState::Desc::setIndependentBlend(bool enabled)
{
    m_Desc.IndependentBlendEnable = enabled;
    return *this;
}

BlendState::Desc& BlendState::Desc::setRenderTargetWriteMask(uint32_t rt, bool red, bool green, bool blue, bool alpha)
{
    if (rt < Diligent::MAX_RENDER_TARGETS)
    {
        auto& target = m_Desc.RenderTargets[rt];
        target.RenderTargetWriteMask = static_cast<Diligent::COLOR_MASK>(
            (red ? Diligent::COLOR_MASK_RED : 0) |
            (green ? Diligent::COLOR_MASK_GREEN : 0) |
            (blue ? Diligent::COLOR_MASK_BLUE : 0) |
            (alpha ? Diligent::COLOR_MASK_ALPHA : 0));
    }
    return *this;
}

BlendState::Desc& BlendState::Desc::setAlphaToCoverage(bool enabled)
{
    (void)enabled;
    spdlog::error("BlendState::Desc::setAlphaToCoverage is not implemented");
    return *this;
}

BlendState::SharedPtr BlendState::create(const Desc& desc)
{
    auto state = std::make_shared<BlendState>();
    state->m_Desc = desc;
    return state;
}

DepthStencilState::Desc& DepthStencilState::Desc::setDepthEnabled(bool enabled)
{
    m_Desc.DepthEnable = enabled;
    return *this;
}

DepthStencilState::Desc& DepthStencilState::Desc::setDepthWriteMask(bool enabled)
{
    m_Desc.DepthWriteEnable = enabled;
    return *this;
}

DepthStencilState::Desc& DepthStencilState::Desc::setStencilEnabled(bool enabled)
{
    m_Desc.StencilEnable = enabled;
    return *this;
}

DepthStencilState::Desc& DepthStencilState::Desc::setDepthFunc(Func func)
{
    switch (func)
    {
    case Func::Never:         m_Desc.DepthFunc = Diligent::COMPARISON_FUNC_NEVER; break;
    case Func::Less:          m_Desc.DepthFunc = Diligent::COMPARISON_FUNC_LESS; break;
    case Func::Equal:         m_Desc.DepthFunc = Diligent::COMPARISON_FUNC_EQUAL; break;
    case Func::LessEqual:    m_Desc.DepthFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL; break;
    case Func::Greater:      m_Desc.DepthFunc = Diligent::COMPARISON_FUNC_GREATER; break;
    case Func::NotEqual:     m_Desc.DepthFunc = Diligent::COMPARISON_FUNC_NOT_EQUAL; break;
    case Func::GreaterEqual: m_Desc.DepthFunc = Diligent::COMPARISON_FUNC_GREATER_EQUAL; break;
    default:                 m_Desc.DepthFunc = Diligent::COMPARISON_FUNC_ALWAYS; break;
    }
    return *this;
}

DepthStencilState::SharedPtr DepthStencilState::create(const Desc& desc)
{
    auto state = std::make_shared<DepthStencilState>();
    state->m_Desc = desc;
    return state;
}

RasterizerState::Desc& RasterizerState::Desc::setCullMode(CullMode mode)
{
    switch (mode)
    {
    case CullMode::None: m_Desc.CullMode = Diligent::CULL_MODE_NONE; break;
    case CullMode::Front: m_Desc.CullMode = Diligent::CULL_MODE_FRONT; break;
    default: m_Desc.CullMode = Diligent::CULL_MODE_BACK; break;
    }
    return *this;
}

RasterizerState::Desc& RasterizerState::Desc::setFillMode(FillMode mode)
{
    m_Desc.FillMode = mode == FillMode::Wireframe ? Diligent::FILL_MODE_WIREFRAME : Diligent::FILL_MODE_SOLID;
    return *this;
}

RasterizerState::SharedPtr RasterizerState::create(const Desc& desc)
{
    auto state = std::make_shared<RasterizerState>();
    state->m_Desc = desc;
    return state;
}

Falcor::SharedPtr<GraphicsState> GraphicsState::create()
{
    return std::make_shared<GraphicsState>();
}

void GraphicsState::setBlendState(const Falcor::SharedPtr<BlendState>& pState) { m_pBlendState = pState; }
void GraphicsState::setDepthStencilState(const Falcor::SharedPtr<DepthStencilState>& pState) { m_pDepthStencilState = pState; }
void GraphicsState::setRasterizerState(const Falcor::SharedPtr<RasterizerState>& pState) { m_pRasterizerState = pState; }
void GraphicsState::setFbo(const Falcor::SharedPtr<Fbo>& pFbo) { m_pFbo = pFbo; }
void GraphicsState::setVao(const Falcor::SharedPtr<Vao>& pVao) { m_pVao = pVao; }
void GraphicsState::setProgram(const Falcor::SharedPtr<GraphicsProgram>& pProgram) { m_pProgram = pProgram; }
void GraphicsState::setViewport(uint32_t, const Viewport& vp, bool) { m_Viewport = vp; }

Falcor::SharedPtr<ComputeState> ComputeState::create() { return std::make_shared<ComputeState>(); }
void ComputeState::setProgram(const Falcor::SharedPtr<ComputeProgram>& pProgram) { m_pProgram = pProgram; }

Program::DefineList& Program::DefineList::add(const std::string& name, const std::string& value)
{
    m_Defines.emplace_back(name, value);
    return *this;
}

Program::DefineList& Program::DefineList::remove(const std::string& name)
{
    m_Defines.erase(
        std::remove_if(m_Defines.begin(), m_Defines.end(),
                       [&name](const auto& p) { return p.first == name; }),
        m_Defines.end());
    return *this;
}


ComputeProgram::SharedPtr ComputeProgram::createFromFile(const std::filesystem::path& path, const std::string& csEntry, const Program::DefineList& defines)
{
    return Gpu::CompileComputeProgram(path, csEntry, defines);
}

GraphicsProgram::Desc& GraphicsProgram::Desc::vsEntry(const std::string& entry)
{
    m_VsEntry = entry;
    return *this;
}

GraphicsProgram::Desc& GraphicsProgram::Desc::psEntry(const std::string& entry)
{
    m_PsEntry = entry;
    return *this;
}

GraphicsProgram::Desc& GraphicsProgram::Desc::gsEntry(const std::string& entry)
{
    m_GsEntry = entry;
    return *this;
}

GraphicsProgram::Desc& GraphicsProgram::Desc::setShaderModel(const std::string& model)
{
    m_ShaderModel = model;
    return *this;
}

GraphicsProgram::SharedPtr GraphicsProgram::create(const Desc& desc, const Program::DefineList& defines)
{
    return Gpu::CompileGraphicsProgram(desc, defines);
}

GraphicsProgram::SharedPtr GraphicsProgram::createFromFile(const std::filesystem::path& path, const std::string& vsEntry, const std::string& psEntry, const Program::DefineList& defines)
{
    Desc desc(path);
    desc.vsEntry(vsEntry).psEntry(psEntry);
    return create(desc, defines);
}

VertexLayout::SharedPtr VertexLayout::create() { return std::make_shared<VertexLayout>(); }

Vao::SharedPtr Vao::create(Topology topology, const VertexLayout::SharedPtr&, const BufferVec& vbos, const Buffer::SharedPtr& ib, Falcor::ResourceFormat indexFormat)
{
    auto vao = std::make_shared<Vao>();
    Gpu::RegisterVao(vao.get(), topology, vbos, ib, indexFormat);
    return vao;
}

RenderContext::RenderContext(Diligent::IDeviceContext* pContext) : m_pContext(pContext) {}

void RenderContext::dispatch(ComputeState* pState, ComputeVars* pVars, const uint3& groups)
{
    Gpu::Dispatch(m_pContext, pState, pVars, groups);
}

void RenderContext::dispatchIndirect(ComputeState* pState, ComputeVars* pVars, const Buffer* pArgBuffer, uint64_t argBufferOffset)
{
    Gpu::DispatchIndirect(m_pContext, pState, pVars, pArgBuffer, argBufferOffset);
}

void RenderContext::drawIndirect(GraphicsState* pState, GraphicsVars* pVars, uint32_t numArgs, Buffer* pArgBuffer, uint64_t argBufferOffset, Buffer* pCountBuffer, uint64_t countBufferOffset)
{
    Gpu::DrawIndirect(m_pContext, pState, pVars, numArgs, pArgBuffer, argBufferOffset, pCountBuffer, countBufferOffset);
}

void RenderContext::drawIndexedInstanced(GraphicsState* pState, GraphicsVars* pVars, uint32_t indexCount, uint32_t instanceCount, uint32_t startIndex, int32_t baseVertex, uint32_t startInstance)
{
    Gpu::DrawIndexedInstanced(m_pContext, pState, pVars, indexCount, instanceCount, startIndex, baseVertex, startInstance);
}

void RenderContext::drawInstanced(GraphicsState* pState, GraphicsVars* pVars, uint32_t vertexCount, uint32_t instanceCount, uint32_t startVertex, uint32_t startInstance)
{
    Gpu::DrawInstanced(m_pContext, pState, pVars, vertexCount, instanceCount, startVertex, startInstance);
}

void RenderContext::clearFbo(Fbo* pFbo, const float4& color, float depth, uint8_t stencil, FboAttachmentType attachments)
{
    Gpu::ClearFbo(m_pContext, pFbo, color, depth, stencil, attachments);
}

void RenderContext::clearTexture(Texture* tx)
{
    (void)tx;
    EFX_LOG_STUB("RenderContext::clearTexture stub");
}

void RenderContext::blit(Diligent::ITextureView* pSrc, Diligent::ITextureView* pDst, const glm::vec4& srcRect, const glm::vec4& dstRect, Sampler::Filter filter, BlendState::SharedPtr blend)
{
    Gpu::Blit(m_pContext, pSrc, pDst, srcRect, dstRect, filter, blend);
}

void RenderContext::updateTextureData(Texture*, const void*)
{
    EFX_LOG_STUB("RenderContext::updateTextureData stub");
}

void RenderContext::clearRtv(Diligent::ITextureView* pRtv, const float4& color)
{
    if (m_pContext && pRtv)
    {
        const float clearColor[4] = {color.x, color.y, color.z, color.w};
        m_pContext->ClearRenderTarget(pRtv, clearColor, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }
}

// NOTE: matches Falcor's CopyContext::copyResource(pDst, pSrc) semantics - the
// destination is the FIRST argument. Getting this backwards is fatal on D3D12:
// a readback (USAGE_STAGING / CPU_ACCESS_READ) buffer lives on a READBACK heap
// that must stay in COPY_DEST forever, so using it as a copy source transitions
// it illegally and removes the device (subsequent map() then returns nullptr).
void RenderContext::copyResource(Texture* pDst, Texture* pSrc)
{
    if (!m_pContext || !pSrc || !pDst)
        return;
    auto* pSrcTex = pSrc->GetDiligentTexture();
    auto* pDstTex = pDst->GetDiligentTexture();
    if (pSrcTex && pDstTex)
    {
        Diligent::CopyTextureAttribs copyAttribs;
        copyAttribs.pSrcTexture = pSrcTex;
        copyAttribs.pDstTexture = pDstTex;
        m_pContext->CopyTexture(copyAttribs);
    }
}

void RenderContext::copyResource(Buffer* pDst, Buffer* pSrc)
{
    if (!m_pContext || !pSrc || !pDst)
        return;
    auto* pSrcBuf = pSrc->GetDiligentBuffer();
    auto* pDstBuf = pDst->GetDiligentBuffer();
    if (!pSrcBuf || !pDstBuf)
        return;

    const auto srcSize = pSrcBuf->GetDesc().Size;
    const auto dstSize = pDstBuf->GetDesc().Size;
    if (srcSize == 0 || dstSize == 0)
        return;

    const auto copySize = std::min(srcSize, dstSize);
    m_pContext->CopyBuffer(pSrcBuf,
                           0,
                           Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                           pDstBuf,
                           0,
                           copySize,
                           Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void RenderContext::flush()
{
    if (m_pContext)
        m_pContext->Flush();
}

void RenderContext::flush(bool waitForGpu)
{
    flush();
    (void)waitForGpu;
}

void RenderContext::copySubresource(Texture* pDst, uint32_t dstSubresource, Texture* pSrc, uint32_t srcSubresource)
{
    if (!m_pContext || !pDst || !pSrc)
        return;
    auto* pDstTex = pDst->GetDiligentTexture();
    auto* pSrcTex = pSrc->GetDiligentTexture();
    if (!pDstTex || !pSrcTex)
        return;

    Diligent::CopyTextureAttribs copyAttribs;
    copyAttribs.pSrcTexture             = pSrcTex;
    copyAttribs.pDstTexture             = pDstTex;
    copyAttribs.SrcMipLevel             = 0;
    copyAttribs.DstMipLevel             = 0;
    copyAttribs.SrcSlice                = srcSubresource;
    copyAttribs.DstSlice                = dstSubresource;
    copyAttribs.SrcTextureTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    copyAttribs.DstTextureTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    m_pContext->CopyTexture(copyAttribs);
}

void RenderContext::resourceBarrier(Texture* pTexture, Resource::State state)
{
    (void)pTexture;
    (void)state;
}

std::vector<uint8_t> RenderContext::readTextureSubresource(Texture* pTexture, uint32_t subresource)
{
    (void)subresource;
    if (!m_pContext || !pTexture)
        return {};
    auto* pTex = pTexture->GetDiligentTexture();
    if (!pTex)
        return {};

    Diligent::TextureDesc desc = pTex->GetDesc();
    const size_t bytesPerPixel = 4;
    const size_t size         = static_cast<size_t>(desc.Width) * desc.Height * bytesPerPixel;
    std::vector<uint8_t> data(size, 0);
    // TODO: wire Diligent texture staging readback.
    (void)data;
    return data;
}

Profiler& Profiler::instance()
{
    static Profiler s_Instance;
    return s_Instance;
}

std::shared_ptr<Profiler::Event> Profiler::getEvent(const char* path)
{
    static std::unordered_map<std::string, std::shared_ptr<Event>> s_Events;
    const std::string key = path ? path : "";
    auto it = s_Events.find(key);
    if (it != s_Events.end())
        return it->second;
    auto event = std::make_shared<Event>();
    s_Events.emplace(key, event);
    return event;
}

void TextRenderer::setColor(const float3& color)
{
    (void)color;
}

void TextRenderer::render(RenderContext* pContext, const std::string& text, const Fbo::SharedPtr& pFbo, const float2& pos)
{
    (void)pContext;
    (void)text;
    (void)pFbo;
    (void)pos;
    EFX_LOG_STUB("TextRenderer::render stub");
}

// --- Gui ---------------------------------------------------------------------

namespace
{
ImGuiWindowFlags MapGuiWindowFlags(Gui::WindowFlags flags)
{
    ImGuiWindowFlags f = 0;
    if (static_cast<uint32_t>(flags) & static_cast<uint32_t>(Gui::WindowFlags::Empty))
        f |= ImGuiWindowFlags_NoDecoration;
    if (static_cast<uint32_t>(flags) & static_cast<uint32_t>(Gui::WindowFlags::NoResize))
        f |= ImGuiWindowFlags_NoResize;
    return f;
}
} // namespace

// size/pos use FirstUseEver (not Always): Falcor windows are user-resizable and
// movable. Forcing the size every frame crammed e.g. the terrain "##debuginfo"
// panel (3 columns of tile stats) into its 200x200 creation size
// (AUTHOR_DEBUG_STEPS.md #1). NoDecoration is now only applied for
// WindowFlags::Empty instead of unconditionally, so windows get a resize grip.
Gui::Window::Window(Gui*, const char* name, bool show_window, float2 size, float2 pos, WindowFlags flags)
{
    m_Open = show_window;
    ImGui::SetNextWindowSize(ImVec2(size.x, size.y), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y), ImGuiCond_FirstUseEver);
    m_Open = ImGui::Begin(name, &m_Open, MapGuiWindowFlags(flags));
}

Gui::Window::Window(Gui*, const char* name, float2 size, float2 pos, WindowFlags flags)
{
    ImGui::SetNextWindowSize(ImVec2(size.x, size.y), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y), ImGuiCond_FirstUseEver);
    m_Open = ImGui::Begin(name, nullptr, MapGuiWindowFlags(flags));
}

Gui::Window::~Window()
{
    release();
}

// Falcor semantics: release() closes the window early so subsequent ImGui calls
// target the parent window again. ImGui requires End() for every Begin(),
// REGARDLESS of Begin()'s return value (the old code skipped End() when Begin
// returned false -> unbalanced window stack when a window was collapsed).
void Gui::Window::release()
{
    if (!m_Ended)
    {
        ImGui::End();
        m_Ended = true;
    }
}

void Gui::Window::windowPos(int x, int y)
{
    ImGui::SetWindowPos(ImVec2(static_cast<float>(x), static_cast<float>(y)));
}

void Gui::Window::windowSize(int w, int h)
{
    ImGui::SetWindowSize(ImVec2(static_cast<float>(w), static_cast<float>(h)));
}

void Gui::Window::image(const char* id, const Texture::SharedPtr&, float2 size)
{
    ImGui::Dummy(ImVec2(size.x, size.y));
    (void)id;
}

bool Gui::Window::imageButton(const char* id, const Texture::SharedPtr&, float2 size)
{
    ImGui::Dummy(ImVec2(size.x, size.y));
    (void)id;
    return false;
}

ImFont* Gui::getFont(const char* name)
{
    auto it = m_Fonts.find(name);
    return it != m_Fonts.end() ? it->second : ImGui::GetFont();
}

void Gui::addFont(const char*, const std::string&, float)
{
    // TODO: load fonts through ImGuiImplDiligent / SampleBase.
}

EarthworksWrapper::EarthworksWrapper() {
    terrafectorSystem::logStartTime = high_resolution_clock::now();
    JLogger::instancePtr()->open("log.cpp");
}

EarthworksWrapper::~EarthworksWrapper() {
    JLogger::instancePtr()->close();
}

void IRenderer::initGui(Gui* _gui)
{
    (void)_gui;
}

void IRenderer::onGuiMenubar(Gui* _gui)
{
    (void)_gui;
}

} // namespace Falcor
