#include "Falcor.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstring>
#include "ots/Log.hpp"
#include "terrafector.h"

namespace Falcor
{

namespace
{

Diligent::IRenderDevice*  g_pDevice     = nullptr;
Diligent::IDeviceContext* g_pContext    = nullptr;
Diligent::ISwapChain*     g_pSwapChain  = nullptr;
bool                      g_VSync       = true;

std::vector<std::filesystem::path> g_DataDirectories;
FrameworkInterface                 g_DefaultFramework;
DeviceInterface                    g_DefaultDevice;

void EFX_LOG_STUB(const char* msg) {
    spdlog::info(msg);
}

TEXTURE_FORMAT ToDiligentFormat(Falcor::ResourceFormat fmt) {
    return (TEXTURE_FORMAT)fmt;
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

void SetFalcorDevice(Diligent::IRenderDevice* pDevice, Diligent::IDeviceContext* pContext, Diligent::ISwapChain* pSwapChain)
{
    g_pDevice    = pDevice;
    g_pContext   = pContext;
    g_pSwapChain = pSwapChain;
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
    const float fovY = std::atan(0.5f * m_Data.FrameHeight / m_Data.FocalLength);
    return float4x4::Projection(fovY, m_Data.AspectRatio, m_Data.NearPlane, m_Data.FarPlane, m_Data.Position.y > 0.f);
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

struct ShaderVar::Node
{
    std::string                                            Name;
    std::unordered_map<std::string, std::shared_ptr<Node>> Children;
    Texture::SharedPtr                                     TextureValue;
    Buffer::SharedPtr                                      BufferValue;
    std::vector<uint8_t>                                   ScalarData;
};

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

struct ComputeVarsData
{
    std::unordered_map<std::string, Texture::SharedPtr> Textures;
    std::unordered_map<std::string, Sampler::SharedPtr> Samplers;
    std::unordered_map<std::string, Buffer::SharedPtr>  Buffers;
    std::vector<uint8_t>                                Blob;
    std::shared_ptr<ShaderVar::Node>                    Root = std::make_shared<ShaderVar::Node>();
};

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
    const size_t end = offset + size;
    if (m_pData->Blob.size() < end)
        m_pData->Blob.resize(end);
    std::memcpy(m_pData->Blob.data() + offset, pData, size);
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

struct GraphicsVarsData
{
    std::unordered_map<std::string, Texture::SharedPtr> Textures;
    std::unordered_map<std::string, Sampler::SharedPtr> Samplers;
    std::unordered_map<std::string, Buffer::SharedPtr>  Buffers;
    std::shared_ptr<ShaderVar::Node>                    Root = std::make_shared<ShaderVar::Node>();
};

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

Texture::SharedPtr Texture::create2D(uint32_t width, uint32_t height, TEXTURE_FORMAT format, uint32_t arraySize, uint32_t mipLevels, const void* pData, uint32_t bindFlags)
{
    return create2D(width, height, static_cast<ResourceFormat>(format), arraySize, mipLevels, pData, bindFlags);
}

Texture::SharedPtr Texture::create2D(uint32_t width, uint32_t height, Falcor::ResourceFormat format, uint32_t arraySize, uint32_t mipLevels, const void* pData, uint32_t bindFlags)
{
    (void)arraySize;
    (void)mipLevels;
    (void)pData;
    auto tex     = std::make_shared<Texture>();
    tex->m_Width = width;
    tex->m_Height = height;
    tex->m_Format = static_cast<TEXTURE_FORMAT>(format);
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
    desc.Format    = static_cast<Diligent::TEXTURE_FORMAT>(format);
    desc.BindFlags = static_cast<Diligent::BIND_FLAGS>(bindFlags);
    desc.MipLevels = 1;
    g_pDevice->CreateTexture(desc, nullptr, &tex->m_pTexture);
    if (tex->m_pTexture)
    {
        if (bindFlags & Resource::BindFlags::ShaderResource)
            CreateTextureView(tex->m_pTexture, Diligent::TEXTURE_VIEW_SHADER_RESOURCE, tex->m_SRV);
        if (bindFlags & Resource::BindFlags::RenderTarget)
            CreateTextureView(tex->m_pTexture, Diligent::TEXTURE_VIEW_RENDER_TARGET, tex->m_RTV);
        if (bindFlags & Resource::BindFlags::UnorderedAccess)
            CreateTextureView(tex->m_pTexture, Diligent::TEXTURE_VIEW_UNORDERED_ACCESS, tex->m_UAV);
    }
    return tex;
}

Texture::SharedPtr Texture::create3D(uint32_t width, uint32_t height, uint32_t depth, Falcor::ResourceFormat format, uint32_t, const void*, uint32_t bindFlags)
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
        const auto mapType = type == MapType::Read ? Diligent::MAP_READ : Diligent::MAP_WRITE;
        g_pContext->MapBuffer(m_pBuffer, mapType, Diligent::MAP_FLAG_NONE, pData);
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
            fbo->m_ColorTextures[slot] = Texture::create2D(
                width, height, fmt, 1, 1, nullptr,
                Resource::BindFlags::RenderTarget | Resource::BindFlags::ShaderResource);
        }
    }
    if (desc.GetDepthFormat() != TEX_FORMAT_UNKNOWN)
    {
        fbo->m_DepthTexture = Texture::create2D(
            width, height, desc.GetDepthFormat(), 1, 1, nullptr, Diligent::BIND_DEPTH_STENCIL);
    }
    return fbo;
}

Fbo::SharedPtr Fbo::create2D(uint32_t width, uint32_t height, const Desc& desc, uint32_t arraySize, uint32_t sampleCount)
{
    (void)arraySize;
    (void)sampleCount;
    return create2D(width, height, desc);
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
    const auto tex = getColorTexture(slot);
    return tex ? tex->getRTV() : nullptr;
}

Diligent::ITextureView* Fbo::getDepthStencilView() const
{
    return m_DepthTexture ? m_DepthTexture->getRTV() : nullptr;
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
    (void)mode;
    spdlog::error("Sampler::Desc::setComparisonMode is not implemented");
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
    auto program    = std::make_shared<ComputeProgram>();
    program->m_Path   = RemapShaderPath(path);
    program->m_Entry  = csEntry;
    program->m_Defines = defines;
    program->m_pReflection = std::make_shared<ProgramReflection>();
    EFX_LOG_STUB("ComputeProgram::createFromFile stub");
    return program;
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
    auto program         = std::make_shared<GraphicsProgram>();
    program->m_Path      = RemapShaderPath(desc.getPath());
    program->m_VsEntry   = desc.getVsEntry();
    program->m_PsEntry   = desc.getPsEntry();
    program->m_GsEntry   = desc.getGsEntry();
    program->m_ShaderModel = desc.getShaderModel();
    program->m_Defines   = defines;
    program->m_pReflection = std::make_shared<ProgramReflection>();
    EFX_LOG_STUB("GraphicsProgram::create stub");
    return program;
}

GraphicsProgram::SharedPtr GraphicsProgram::createFromFile(const std::filesystem::path& path, const std::string& vsEntry, const std::string& psEntry, const Program::DefineList& defines)
{
    Desc desc(path);
    desc.vsEntry(vsEntry).psEntry(psEntry);
    return create(desc, defines);
}

VertexLayout::SharedPtr VertexLayout::create() { return std::make_shared<VertexLayout>(); }

Vao::SharedPtr Vao::create(Topology, const VertexLayout::SharedPtr&, const BufferVec&, const Buffer::SharedPtr&, Falcor::ResourceFormat indexFormat)
{
    return std::make_shared<Vao>();
}

RenderContext::RenderContext(Diligent::IDeviceContext* pContext) : m_pContext(pContext) {}

void RenderContext::dispatch(ComputeState*, ComputeVars*, const uint3& groups)
{
    (void)groups;
    EFX_LOG_STUB("RenderContext::dispatch stub");
}

void RenderContext::dispatchIndirect(ComputeState*, ComputeVars*, const Buffer*, uint64_t)
{
    EFX_LOG_STUB("RenderContext::dispatchIndirect stub");
}

void RenderContext::drawIndirect(GraphicsState*, GraphicsVars*, uint32_t, Buffer*, uint64_t, Buffer*, uint64_t)
{
    EFX_LOG_STUB("RenderContext::drawIndirect stub");
}

void RenderContext::drawIndexedInstanced(GraphicsState*, GraphicsVars*, uint32_t indexCount, uint32_t instanceCount, uint32_t, int32_t, uint32_t)
{
    (void)indexCount;
    (void)instanceCount;
    EFX_LOG_STUB("RenderContext::drawIndexedInstanced stub");
}

void RenderContext::drawInstanced(GraphicsState*, GraphicsVars*, uint32_t vertexCount, uint32_t instanceCount, uint32_t, uint32_t)
{
    (void)vertexCount;
    (void)instanceCount;
    EFX_LOG_STUB("RenderContext::drawInstanced stub");
}

void RenderContext::clearFbo(Fbo*, const float4& color, float depth, uint8_t stencil, FboAttachmentType attachments)
{
    if (!m_pContext)
        return;
    (void)color;
    (void)depth;
    (void)stencil;
    (void)attachments;
    EFX_LOG_STUB("RenderContext::clearFbo stub");
}

void RenderContext::clearTexture(Texture* tx)
{
    (void)tx;
    EFX_LOG_STUB("RenderContext::clearTexture stub");
}

void RenderContext::blit(Diligent::ITextureView* pSrc, Diligent::ITextureView* pDst, const glm::vec4&, const glm::vec4&, Sampler::Filter, BlendState::SharedPtr)
{
    (void)pSrc;
    (void)pDst;
    EFX_LOG_STUB("RenderContext::blit stub");
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

void RenderContext::copyResource(Texture* pSrc, Texture* pDst)
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

void RenderContext::copyResource(Buffer* pSrc, Buffer* pDst)
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

Gui::Window::Window(Gui*, const char* name, bool show_window, float2 size, float2 pos, WindowFlags)
{
    m_Open = show_window;
    ImGui::SetNextWindowSize(ImVec2(size.x, size.y), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y), ImGuiCond_Always);
    m_Open = ImGui::Begin(name, &m_Open, ImGuiWindowFlags_NoDecoration);
}

Gui::Window::Window(Gui*, const char* name, float2 size, float2 pos, WindowFlags)
{
    ImGui::SetNextWindowSize(ImVec2(size.x, size.y), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y), ImGuiCond_Always);
    m_Open = ImGui::Begin(name, nullptr, ImGuiWindowFlags_NoDecoration);
}

Gui::Window::~Window()
{
    if (m_Open)
        ImGui::End();
}

void Gui::Window::release() {}

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
