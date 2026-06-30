#pragma once

// Falcor -> DiligentEngine compatibility shim for the Earthworks 1:1 port.
// Algorithm code must stay untouched; only API surface is reimplemented here.

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef GLM_ENABLE_EXPERIMENTAL
#    define GLM_ENABLE_EXPERIMENTAL
#endif
#ifndef GLM_FORCE_SWIZZLE
#    define GLM_FORCE_SWIZZLE
#endif
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/compatibility.hpp>

#include "BasicMath.hpp"
#include "DeviceContext.h"
#include "RefCntAutoPtr.hpp"
#include "RenderDevice.h"
#include "SwapChain.h"
#include "Texture.h"
#include "Buffer.h"
#include "GraphicsTypes.h"
#include "BlendState.h"
#include "EngineFactory.h"

#if defined(_WIN32) || defined(PLATFORM_WIN32)
#    include "WinHPreface.h"
#    include <Windows.h>
#    include "WinHPostface.h"
#endif
#include "imgui.h"

// ImGui 1.92+ renamed ImFont::FontSize to LegacySize; keep Falcor-era member access working.
#define FontSize LegacySize

// ImGui 1.92+ removed BeginChildFrame/EndChildFrame; keep Falcor-era calls working.
namespace ImGui
{
inline bool BeginChildFrame(ImGuiID id, const ImVec2& size, ImGuiWindowFlags extra_flags = 0)
{
    return BeginChild(id, size, ImGuiChildFlags_FrameStyle, extra_flags);
}

inline void EndChildFrame()
{
    EndChild();
}
} // namespace ImGui

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#ifndef UCHAR
using UCHAR = unsigned char;
#endif
#ifndef CHAR
using CHAR = char;
#endif
#ifndef UINT
using UINT = unsigned int;
#endif

template<typename T>
inline T lerp(const T& a, const T& b, float t)
{
    return a + (b - a) * t;
}

template<typename T>
inline T clamp(const T& x, const T& lo, const T& hi)
{
    return glm::clamp(x, lo, hi);
}

template<typename T>
inline T saturate(const T& x)
{
    return glm::clamp(x, T(0), T(1));
}

namespace Falcor
{

namespace Gpu
{
class Internal;
}

namespace rmcv
{
using mat4 = glm::mat4;
}

using Diligent::TEXTURE_FORMAT;
using Diligent::TEX_FORMAT_UNKNOWN;
using Diligent::BIND_FLAGS;

using float2 = glm::vec2;
using float3 = glm::vec3;
using float4 = glm::vec4;
using uint2  = glm::uvec2;
using uint3  = glm::uvec3;
using uint4  = glm::uvec4;
using uint   = std::uint32_t;
using int2   = glm::ivec2;
using int4   = glm::ivec4;

template<typename T>
using SharedPtr = std::shared_ptr<T>;

template<typename T>
using SharedConstPtr = std::shared_ptr<const T>;

template<typename T>
using UniquePtr = std::unique_ptr<T>;

#define FALCOR_PROFILE(name) do { } while (0)

struct float4x4 : public Diligent::float4x4
{
    using Base = Diligent::float4x4;
    using Base::Base;

    float4x4() = default;
    float4x4(const Base& m) : Base(m) {}

    float4x4 getTranspose() const
    {
        float4x4 r;
        const Diligent::float4x4 t = Transpose();
        std::memcpy(r.Data(), t.Data(), sizeof(float) * 16);
        return r;
    }

    operator glm::mat4() const { return glm::make_mat4x4(Data()); }
};

class TriangleMesh
{
public:
    using SharedPtr = Falcor::SharedPtr<TriangleMesh>;
};

inline glm::mat4 toGLM(const float4x4& m)
{
    return glm::make_mat4x4(m.Data());
}

struct AABB
{
    float3 minPoint{};
    float3 maxPoint{};

    AABB() = default;
    AABB(const float3& minPt, const float3& maxPt) : minPoint(minPt), maxPoint(maxPt) {}
};

enum class HotReloadFlags : uint32_t
{
    None = 0,
};

enum class FboAttachmentType
{
    Color   = 1,
    Depth   = 2,
    Stencil = 4,
    All     = Color | Depth | Stencil,
};

inline FboAttachmentType operator|(FboAttachmentType a, FboAttachmentType b)
{
    return static_cast<FboAttachmentType>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

struct CameraData
{
    float4x4 ViewMatrix{};
    float4x4 ProjMatrix{};
    float3   Position{};
    float    FocalLength  = 15.f;
    float    NearPlane    = 0.1f;
    float    FarPlane     = 40000.f;
    float    AspectRatio  = 16.f / 9.f;
    float    FrameHeight  = 1080.f;
};

class Camera
{
public:
    using SharedPtr = Falcor::SharedPtr<Camera>;

    static Falcor::SharedPtr<Camera> create();

    void setDepthRange(float zNear, float zFar);
    void setAspectRatio(float aspect);
    void setFocalLength(float focalMm);
    void setPosition(const float3& pos);
    void setTarget(const float3& target);
    void setUpVector(const float3& up);
    void setFarPlane(float zFar);
    void setNearPlane(float zNear);

    float4x4 getViewMatrix() const;
    float4x4 getProjMatrix() const;
    float4x4 getViewProjMatrix() const;
    float3   getPosition() const;
    float3   getTarget() const { return m_Target; }
    bool     isObjectCulled(const AABB& box) const;

    float getFocalLength() const { return m_Data.FocalLength; }
    float getAspectRatio() const { return m_Data.AspectRatio; }
    float getFrameHeight() const { return m_Data.FrameHeight; }
    float getNearPlane() const { return m_Data.NearPlane; }
    float getFarPlane() const { return m_Data.FarPlane; }

    CameraData&       getData() { return m_Data; }
    const CameraData& getData() const { return m_Data; }

private:
    CameraData m_Data{};
    float3     m_Target{};
};

enum ResourceFormat : Diligent::Uint16
{
    RG32Float       = Diligent::TEX_FORMAT_RG32_FLOAT,
    R11G11B10Float  = Diligent::TEX_FORMAT_R11G11B10_FLOAT,
    RGB32Float      = Diligent::TEX_FORMAT_RGB32_FLOAT,
    RGBA32Float     = Diligent::TEX_FORMAT_RGBA32_FLOAT,
    RGBA16Float     = Diligent::TEX_FORMAT_RGBA16_FLOAT,
    RGBA8Unorm      = Diligent::TEX_FORMAT_RGBA8_UNORM,
    RGBA8UnormSrgb  = Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB,
    R8Uint          = Diligent::TEX_FORMAT_R8_UINT,
    R16Uint         = Diligent::TEX_FORMAT_R16_UINT,
    R32Float        = Diligent::TEX_FORMAT_R32_FLOAT,
    R32Uint         = Diligent::TEX_FORMAT_R32_UINT,
    R16Unorm        = Diligent::TEX_FORMAT_R16_UNORM,
    RGB10A2Unorm    = Diligent::TEX_FORMAT_RGB10A2_UNORM,
    RGBA32Uint      = Diligent::TEX_FORMAT_RGBA32_UINT,
    R5G6B5Unorm     = Diligent::TEX_FORMAT_B5G6R5_UNORM,
    BC6HU16         = Diligent::TEX_FORMAT_BC6H_UF16,
    D24UnormS8      = Diligent::TEX_FORMAT_D24_UNORM_S8_UINT,
    BGRA8UnormSrgb  = Diligent::TEX_FORMAT_BGRA8_UNORM_SRGB,
    R8Unorm         = Diligent::TEX_FORMAT_R8_UNORM,
};

namespace Resource
{
enum BindFlags : uint32_t
{
    None            = 0,
    ShaderResource  = Diligent::BIND_SHADER_RESOURCE,
    RenderTarget    = Diligent::BIND_RENDER_TARGET,
    UnorderedAccess = Diligent::BIND_UNORDERED_ACCESS,
    Index           = Diligent::BIND_INDEX_BUFFER,
    Vertex          = Diligent::BIND_VERTEX_BUFFER,
    Constant        = Diligent::BIND_UNIFORM_BUFFER,
    IndirectArg     = Diligent::BIND_INDIRECT_DRAW_ARGS,
    AllColorViews   = ShaderResource | RenderTarget | UnorderedAccess,
};

inline BindFlags operator|(BindFlags a, BindFlags b)
{
    return static_cast<BindFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

enum class State
{
    Common,
    CopySource,
    CopyDest,
    ShaderResource,
    RenderTarget,
    UnorderedAccess,
};
} // namespace Resource

class Texture;
class Fbo;
class Buffer;
class Sampler;
class BlendState;
class DepthStencilState;
class RasterizerState;
class GraphicsState;
class ComputeState;
class ComputeVars;
class ComputeProgram;
class GraphicsProgram;
class GraphicsVars;
class VertexLayout;
class Vao;

class ProgramReflection
{
public:
    using SharedConstPtr = Falcor::SharedConstPtr<ProgramReflection>;
    // TODO: parse shader reflection for root signature / variable layout.
};

class GraphicsState
{
public:
    using SharedPtr = Falcor::SharedPtr<GraphicsState>;

    struct Viewport
    {
        float originX  = 0;
        float originY  = 0;
        float width    = 0;
        float height   = 0;
        float minDepth = 0;
        float maxDepth = 1;

        Viewport() = default;
        Viewport(float ox, float oy, float w, float h, float minD = 0.f, float maxD = 1.f)
            : originX(ox), originY(oy), width(w), height(h), minDepth(minD), maxDepth(maxD)
        {}
    };

    static SharedPtr create();

    void setBlendState(const Falcor::SharedPtr<BlendState>& pState);
    void setDepthStencilState(const Falcor::SharedPtr<DepthStencilState>& pState);
    void setRasterizerState(const Falcor::SharedPtr<RasterizerState>& pState);
    void setFbo(const Falcor::SharedPtr<Fbo>& pFbo);
    void setVao(const Falcor::SharedPtr<Vao>& pVao);
    void setProgram(const Falcor::SharedPtr<GraphicsProgram>& pProgram);
    void setViewport(uint32_t index, const Viewport& vp, bool setScissor);

    Falcor::SharedPtr<BlendState>        getBlendState() const { return m_pBlendState; }
    Falcor::SharedPtr<DepthStencilState> getDepthStencilState() const { return m_pDepthStencilState; }
    Falcor::SharedPtr<RasterizerState>   getRasterizerState() const { return m_pRasterizerState; }
    Falcor::SharedPtr<Fbo>               getFbo() const { return m_pFbo; }
    Falcor::SharedPtr<GraphicsProgram>   getProgram() const { return m_pProgram; }
    Falcor::SharedPtr<Vao>               getVao() const { return m_pVao; }

private:
    friend class Gpu::Internal;
    Falcor::SharedPtr<BlendState>        m_pBlendState;
    Falcor::SharedPtr<DepthStencilState> m_pDepthStencilState;
    Falcor::SharedPtr<RasterizerState>   m_pRasterizerState;
    Falcor::SharedPtr<Fbo>               m_pFbo;
    Falcor::SharedPtr<Vao>               m_pVao;
    Falcor::SharedPtr<GraphicsProgram>   m_pProgram;
    Viewport                     m_Viewport{};
};

class ComputeState
{
public:
    using SharedPtr = Falcor::SharedPtr<ComputeState>;

    static SharedPtr create();
    void setProgram(const Falcor::SharedPtr<ComputeProgram>& pProgram);

    Falcor::SharedPtr<ComputeProgram> getProgram() const { return m_pProgram; }

private:
    Falcor::SharedPtr<ComputeProgram> m_pProgram;
};

class RenderContext;

class ShaderVar
{
public:
    ShaderVar() = default;

    ShaderVar operator[](const char* name);
    ShaderVar operator[](const char* name) const;
    ShaderVar operator[](size_t index);
    ShaderVar operator[](size_t index) const;

    ShaderVar& operator=(const Falcor::SharedPtr<Texture>& tex);
    ShaderVar& operator=(const Falcor::SharedPtr<Buffer>& buf);

    template<typename T>
    ShaderVar& operator=(const T& value)
    {
        return assignScalar(&value, sizeof(T));
    }

    operator Falcor::SharedPtr<Texture>() const;
    operator Falcor::SharedPtr<Buffer>() const;

    struct Node
    {
        std::string                                            Name;
        std::unordered_map<std::string, std::shared_ptr<Node>> Children;
        SharedPtr<Texture>                                     TextureValue;
        SharedPtr<Buffer>                                      BufferValue;
        std::vector<uint8_t>                                   ScalarData;
    };

    std::shared_ptr<Node> m_pNode;
    explicit ShaderVar(std::shared_ptr<Node> node);

    friend class ComputeVars;
    friend class GraphicsVars;
    friend class Gpu::Internal;

private:
    ShaderVar& assignScalar(const void* data, size_t size);
};

template<typename VarsT>
class VarsPtr : public std::shared_ptr<VarsT>
{
public:
    using std::shared_ptr<VarsT>::shared_ptr;
    VarsPtr(std::shared_ptr<VarsT> p) : std::shared_ptr<VarsT>(std::move(p)) {}

    ShaderVar operator[](const char* name) { return (*this->get())[name]; }
    ShaderVar operator[](const char* name) const { return (*this->get())[name]; }
};

struct ComputeVarsData;
struct GraphicsVarsData;

class ComputeVars
{
public:
    using SharedPtr = VarsPtr<ComputeVars>;

    static SharedPtr create(ComputeProgram* pProgram);

    ComputeVars* operator->() { return this; }
    const ComputeVars* operator->() const { return this; }

    ShaderVar operator[](const char* name);
    ShaderVar operator[](const char* name) const;

    void setTexture(const char* name, const Falcor::SharedPtr<Texture>& pTex);
    void setSampler(const char* name, const Falcor::SharedPtr<Sampler>& pSampler);
    void setBuffer(const char* name, const Falcor::SharedPtr<Buffer>& pBuffer);
    void setBlob(const void* pData, size_t offset, size_t size);

    SharedPtr getParameterBlock(const char* name);

    ShaderVar findMember(const char* name);
    ShaderVar findMember(const char* name) const;

private:
    std::shared_ptr<ComputeVarsData> m_pData;
    std::shared_ptr<ShaderVar::Node> m_pBlockRoot;

    ShaderVar getRootVar();
    ShaderVar getRootVar() const;

    friend class Gpu::Internal;
};

class GraphicsVars
{
public:
    using SharedPtr = VarsPtr<GraphicsVars>;

    static SharedPtr create(const ProgramReflection* pReflection);
    static SharedPtr create(ProgramReflection::SharedConstPtr pReflection);

    GraphicsVars* operator->() { return this; }
    const GraphicsVars* operator->() const { return this; }

    ShaderVar operator[](const char* name);
    ShaderVar operator[](const char* name) const;

    void setTexture(const char* name, const Falcor::SharedPtr<Texture>& pTex);
    void setSampler(const char* name, const Falcor::SharedPtr<Sampler>& pSampler);
    void setBuffer(const char* name, const Falcor::SharedPtr<Buffer>& pBuffer);

    SharedPtr getParameterBlock(const char* name);

    ShaderVar findMember(const char* name);
    ShaderVar findMember(const char* name) const;

private:
    std::shared_ptr<GraphicsVarsData> m_pData;
    std::shared_ptr<ShaderVar::Node>  m_pBlockRoot;

    ShaderVar getRootVar();
    ShaderVar getRootVar() const;

    friend class Gpu::Internal;
};


class Bitmap
{
public:
    enum class FileFormat
    {
        PngFile,
        JpegFile,
        ExrFile,
        PfmFile,
    };

    enum class ExportFlags : uint32_t
    {
        None        = 0,
        ExportAlpha = 1u << 0,
    };
};

class Texture
{
public:
    using SharedPtr = Falcor::SharedPtr<Texture>;

    static SharedPtr create2D(uint32_t width, uint32_t height, TEXTURE_FORMAT format, uint32_t arraySize, uint32_t mipLevels, const void* pData, uint32_t bindFlags = (uint32_t)Resource::BindFlags::ShaderResource);
    static SharedPtr create2D(uint32_t width, uint32_t height, Falcor::ResourceFormat format, uint32_t arraySize, uint32_t mipLevels, const void* pData, uint32_t bindFlags = (uint32_t)Resource::BindFlags::ShaderResource);
    static SharedPtr create3D(uint32_t width, uint32_t height, uint32_t depth, Falcor::ResourceFormat format, uint32_t mipLevels, const void* pData, uint32_t bindFlags = (uint32_t)Resource::BindFlags::ShaderResource);
    static SharedPtr createCube(uint32_t size, Falcor::ResourceFormat format, uint32_t bindFlags = (uint32_t)Resource::BindFlags::ShaderResource);
    static SharedPtr createFromFile(const char* path, bool srgb, bool generateMips, Resource::BindFlags bind_flags = Resource::BindFlags::ShaderResource);
    static SharedPtr createFromFile(const std::filesystem::path& path, bool srgb, bool generateMips, Resource::BindFlags bind_flags = Resource::BindFlags::ShaderResource);

    const std::string& getName() const { return m_Name; }
    void setName(const std::string& name) { m_Name = name; }
    const std::filesystem::path& getSourcePath() const { return m_SourcePath; }
    void setSourcePath(const std::filesystem::path& path) { m_SourcePath = path; }
    void setSourcePath(const std::string& path) { m_SourcePath = path; }
    TEXTURE_FORMAT getFormat() const { return m_Format; }
    uint32_t getMipCount() const { return m_MipCount; }

    uint32_t getWidth() const;
    uint32_t getHeight() const;

    // TODO: return real Diligent views once resource binding is wired.
    Diligent::ITextureView* getSRV(uint32_t, uint32_t, uint32_t, uint32_t) const;
    Diligent::ITextureView* getRTV() const;
    Diligent::ITextureView* getDSV() const;
    Diligent::ITextureView* getUAV(uint32_t mip = 0) const;

    void generateMips(RenderContext* pContext = nullptr);
    void captureToFile(uint32_t mipLevel, uint32_t slice, const std::string& path);
    void captureToFile(uint32_t mipLevel, uint32_t slice, const std::string& path, Bitmap::FileFormat format, Bitmap::ExportFlags flags);

    uint32_t getSubresourceIndex(uint32_t mipLevel, uint32_t arraySlice) const;

    Diligent::ITexture* GetDiligentTexture() const { return m_pTexture; }

private:
    Diligent::RefCntAutoPtr<Diligent::ITexture> m_pTexture;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> m_SRV;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> m_RTV;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> m_DSV;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> m_UAV;
    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
    uint32_t m_MipCount = 1;
    TEXTURE_FORMAT m_Format = TEX_FORMAT_UNKNOWN;
    std::string m_Name;
    std::filesystem::path m_SourcePath;
};

class Buffer
{
public:
    using SharedPtr = Falcor::SharedPtr<Buffer>;

    enum class BindFlags : uint32_t
    {
        None  = 0,
        Index = Falcor::Resource::Index,
        Vertex = Falcor::Resource::Vertex,
        Constant = Falcor::Resource::Constant,
        UnorderedAccess = Falcor::Resource::UnorderedAccess,
        ShaderResource = Falcor::Resource::ShaderResource,
        IndirectArg = Falcor::Resource::IndirectArg,
    };

    enum class CpuAccess : uint32_t
    {
        None  = 0,
        Write = 1,
        Read  = 2,
    };

    enum class MapType
    {
        Read,
        Write,
    };

    static SharedPtr create(size_t size, BindFlags bindFlags, CpuAccess cpuAccess, const void* pInitData = nullptr);
    static SharedPtr create(size_t size, Resource::BindFlags bindFlags, CpuAccess cpuAccess, const void* pInitData = nullptr);

    static SharedPtr createStructured(size_t structSize, size_t elementCount);
    static SharedPtr createStructured(size_t structSize, size_t elementCount, Resource::BindFlags bindFlags);
    static SharedPtr createStructured(size_t structSize, size_t elementCount, uint32_t bindFlags);
    static SharedPtr createStructured(size_t structSize, size_t elementCount, Resource::BindFlags bindFlags, CpuAccess cpuAccess, const void* pInitData = nullptr);

    static SharedPtr createTyped(Falcor::ResourceFormat format, size_t sizeInBytes, Resource::BindFlags bindFlags);

    void setBlob(const void* pData, size_t offset, size_t size);
    void setBlob(const void* pData, size_t offset, size_t size, size_t count);
    void uploadToGPU(size_t offset = 0, size_t size = 0);

    void* map(MapType type);
    void unmap();

    SharedPtr getUAVCounter();

    Diligent::IBuffer* GetDiligentBuffer() const { return m_pBuffer; }

private:
    friend Falcor::SharedPtr<Buffer> CreateFalcorBuffer(size_t size, Diligent::BIND_FLAGS bindFlags, CpuAccess cpuAccess, const void* pInitData, bool structured, uint32_t structStride);
    void ensureCpuShadow(size_t size);
    void uploadShadowRange(size_t offset, size_t size);

    Diligent::RefCntAutoPtr<Diligent::IBuffer> m_pBuffer;
    std::vector<uint8_t>                     m_CpuShadow;
    SharedPtr                        m_pUavCounter;
    size_t                                   m_Size = 0;
    MapType                                  m_LastMapType = MapType::Read;
};

struct ComputeVarsData
{
    std::unordered_map<std::string, SharedPtr<Texture>> Textures;
    std::unordered_map<std::string, SharedPtr<Sampler>> Samplers;
    std::unordered_map<std::string, SharedPtr<Buffer>>  Buffers;
    std::vector<uint8_t>                                Blob;
    std::shared_ptr<ShaderVar::Node>                    Root = std::make_shared<ShaderVar::Node>();
};

struct GraphicsVarsData
{
    std::unordered_map<std::string, SharedPtr<Texture>> Textures;
    std::unordered_map<std::string, SharedPtr<Sampler>> Samplers;
    std::unordered_map<std::string, SharedPtr<Buffer>>  Buffers;
    std::shared_ptr<ShaderVar::Node>                    Root = std::make_shared<ShaderVar::Node>();
};

class Fbo
{
public:
    using SharedPtr = Falcor::SharedPtr<Fbo>;

    class Desc
    {
    public:
        Desc& setColorTarget(uint32_t slot, Falcor::ResourceFormat falcor_format);
        Desc& setColorTarget(uint32_t slot, Diligent::TEXTURE_FORMAT format);
        Desc& setColorTarget(uint32_t slot, Falcor::ResourceFormat falcor_format, bool srgb);
        Desc& setDepthStencilTarget(Falcor::ResourceFormat falcor_format);
        Desc& setDepthStencilTarget(Diligent::TEXTURE_FORMAT format);
        Falcor::ResourceFormat GetColorFormat(uint32_t slot) const;
        Falcor::ResourceFormat GetDepthFormat() const;

    private:
        std::array<TEXTURE_FORMAT, 8> m_ColorFormats{};
        TEXTURE_FORMAT                m_DepthFormat = TEX_FORMAT_UNKNOWN;
        bool                          m_HasDepth    = false;
    };

    static SharedPtr create2D(uint32_t width, uint32_t height, const Desc& desc);
    static SharedPtr create2D(uint32_t width, uint32_t height, const Desc& desc, uint32_t arraySize, uint32_t sampleCount);
    static SharedPtr createFromSwapChain(Diligent::ISwapChain* pSwapChain);

    Texture::SharedPtr getColorTexture(uint32_t slot) const;
    Texture::SharedPtr getDepthStencilTexture() const;
    Diligent::ITextureView* getRenderTargetView(uint32_t slot) const;
    Diligent::ITextureView* getDepthStencilView() const;
            
    float getWidth() const { return m_Width; }
    float getHeight() const { return m_Height; }
    bool  isSwapChainProxy() const { return m_IsSwapChainProxy; }
    Diligent::ISwapChain* getSwapChain() const { return m_pSwapChain; }

private:
    friend class Gpu::Internal;

    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
    std::array<Texture::SharedPtr, 8> m_ColorTextures{};
    Texture::SharedPtr                m_DepthTexture;
    Diligent::ISwapChain*           m_pSwapChain = nullptr;
    bool                              m_IsSwapChainProxy = false;
};

class Sampler
{
public:
    using SharedPtr = Falcor::SharedPtr<Sampler>;

    enum class AddressMode
    {
        Wrap,
        Mirror,
        Clamp,
        Border,
    };

    enum class ComparisonMode
    {
        LessEqual
    };

    enum class Filter
    {
        Point,
        Linear,
    };

    class Desc
    {
    public:
        Desc& setAddressingMode(AddressMode u, AddressMode v, AddressMode w);
        Desc& setFilterMode(Filter min, Filter mag, Filter mip);
        Desc& setMaxAnisotropy(uint32_t aniso);
        Desc& setComparisonMode(ComparisonMode mode);

    private:
        friend class Sampler;
        Diligent::SamplerDesc m_Desc{};
    };

    static SharedPtr create(const Desc& desc);

    Diligent::ISampler* GetDiligentSampler() const { return m_pSampler; }

private:
    Diligent::RefCntAutoPtr<Diligent::ISampler> m_pSampler;
};

class BlendState
{
public:
    using SharedPtr = Falcor::SharedPtr<BlendState>;

    enum class BlendOp
    {
        Add,
        Subtract,
    };

    enum class BlendFunc
    {
        Zero,
        One,
        SrcAlpha,
        OneMinusSrcAlpha,
        SrcAlphaSaturate,
    };

    class Desc
    {
    public:
        Desc& setRtBlend(uint32_t rt, bool enable);
        Desc& setRtParams(uint32_t rt, BlendOp colorOp, BlendOp alphaOp, BlendFunc srcColor, BlendFunc dstColor, BlendFunc srcAlpha, BlendFunc dstAlpha);
        Desc& setIndependentBlend(bool enabled);
        Desc& setRenderTargetWriteMask(uint32_t rt, bool red, bool green, bool blue, bool alpha);
        Desc& setAlphaToCoverage(bool enabled);

        const Diligent::BlendStateDesc& GetDiligentDesc() const { return m_Desc; }

    private:
        friend class BlendState;
        Diligent::BlendStateDesc m_Desc{};
    };

    static SharedPtr create(const Desc& desc);

    const Desc& getDesc() const { return m_Desc; }

private:
    Desc m_Desc{};
};

class DepthStencilState
{
public:
    using SharedPtr = Falcor::SharedPtr<DepthStencilState>;

    enum class Func
    {
        Never,
        Less,
        Equal,
        LessEqual,
        Greater,
        NotEqual,
        GreaterEqual,
        Always,
    };

    class Desc
    {
    public:
        Desc& setDepthEnabled(bool enabled);
        Desc& setDepthWriteMask(bool enabled);
        Desc& setStencilEnabled(bool enabled);
        Desc& setDepthFunc(Func func);

        const Diligent::DepthStencilStateDesc& GetDiligentDesc() const { return m_Desc; }

    private:
        friend class DepthStencilState;
        Diligent::DepthStencilStateDesc m_Desc{};
    };

    static SharedPtr create(const Desc& desc);

    const Desc& getDesc() const { return m_Desc; }

private:
    Desc m_Desc{};
};

class RasterizerState
{
public:
    using SharedPtr = Falcor::SharedPtr<RasterizerState>;

    enum class CullMode
    {
        None,
        Back,
        Front,
    };

    enum class FillMode
    {
        Solid,
        Wireframe,
    };

    class Desc
    {
    public:
        Desc& setCullMode(CullMode mode);
        Desc& setFillMode(FillMode mode);

        const Diligent::RasterizerStateDesc& GetDiligentDesc() const { return m_Desc; }

    private:
        friend class RasterizerState;
        Diligent::RasterizerStateDesc m_Desc{};
    };

    static SharedPtr create(const Desc& desc);

    const Desc& getDesc() const { return m_Desc; }

private:
    Desc m_Desc{};
};

namespace Program
{
class DefineList
{
public:
    DefineList& add(const std::string& name, const std::string& value);
    DefineList& remove(const std::string& name);

    const std::vector<std::pair<std::string, std::string>>& get() const { return m_Defines; }

private:
    std::vector<std::pair<std::string, std::string>> m_Defines;
};
} // namespace Program

class ComputeProgram
{
public:
    using SharedPtr = Falcor::SharedPtr<ComputeProgram>;

    static SharedPtr createFromFile(const std::filesystem::path& path, const std::string& csEntry, const Program::DefineList& defines = {});

    ComputeProgram* getActiveVersion() { return this; }
    ProgramReflection::SharedConstPtr getReflector() const { return m_pReflection; }
    static const std::vector<std::string>& getGlobalCompilationStats() { static const std::vector<std::string> s_Empty; return s_Empty; }

private:
    friend class Gpu::Internal;

    std::shared_ptr<ProgramReflection> m_pReflection;
    std::filesystem::path              m_Path;
    std::string                        m_Entry;
    Program::DefineList                m_Defines;
};

class GraphicsProgram
{
public:
    using SharedPtr = Falcor::SharedPtr<GraphicsProgram>;

    class Desc
    {
    public:
        explicit Desc(const std::filesystem::path& path) : m_Path(path) {}
        Desc& vsEntry(const std::string& entry);
        Desc& psEntry(const std::string& entry);
        Desc& gsEntry(const std::string& entry);
        Desc& setShaderModel(const std::string& model);

        const std::filesystem::path& getPath() const { return m_Path; }
        const std::string& getVsEntry() const { return m_VsEntry; }
        const std::string& getPsEntry() const { return m_PsEntry; }
        const std::string& getGsEntry() const { return m_GsEntry; }
        const std::string& getShaderModel() const { return m_ShaderModel; }

    private:
        std::filesystem::path m_Path;
        std::string           m_VsEntry = "vsMain";
        std::string           m_PsEntry = "psMain";
        std::string           m_GsEntry;
        std::string           m_ShaderModel = "6_0";
    };

    static SharedPtr create(const Desc& desc, const Program::DefineList& defines = {});
    static SharedPtr createFromFile(const std::filesystem::path& path, const std::string& vsEntry, const std::string& psEntry, const Program::DefineList& defines = {});

    GraphicsProgram* getActiveVersion() { return this; }
    ProgramReflection::SharedConstPtr getReflector() const { return m_pReflection; }
    static const std::vector<std::string>& getGlobalCompilationStats() { static const std::vector<std::string> s_Empty; return s_Empty; }

private:
    friend class Gpu::Internal;

    std::shared_ptr<ProgramReflection> m_pReflection;
    std::filesystem::path              m_Path;
    std::string                        m_VsEntry;
    std::string                        m_PsEntry;
    std::string                        m_GsEntry;
    std::string                        m_ShaderModel;
    Program::DefineList                m_Defines;
};

class VertexLayout
{
public:
    using SharedPtr = Falcor::SharedPtr<VertexLayout>;

    static SharedPtr create();
};

class Vao
{
public:
    using SharedPtr = Falcor::SharedPtr<Vao>;

    enum class Topology
    {
        PointList,
        LineList,
        TriangleList,
        TriangleStrip,
        LineStrip,
    };

    using BufferVec = std::vector<Buffer::SharedPtr>;

    static SharedPtr create(Topology topology, const VertexLayout::SharedPtr& layout, const BufferVec& vbos, const Buffer::SharedPtr& ib, Falcor::ResourceFormat indexFormat);
};

class RenderContext
{
public:
    explicit RenderContext(Diligent::IDeviceContext* pContext);
    void attach(Diligent::IDeviceContext* pContext) { m_pContext = pContext; }

    void dispatch(ComputeState* pState, ComputeVars* pVars, const uint3& groups);
    void dispatchIndirect(ComputeState* pState, ComputeVars* pVars, const Buffer* pArgBuffer, uint64_t argBufferOffset);
    void drawIndirect(GraphicsState* pState, GraphicsVars* pVars, uint32_t numArgs, Buffer* pArgBuffer, uint64_t argBufferOffset, Buffer* pCountBuffer, uint64_t countBufferOffset);
    void drawIndexedInstanced(GraphicsState* pState, GraphicsVars* pVars, uint32_t indexCount, uint32_t instanceCount, uint32_t startIndex, int32_t baseVertex, uint32_t startInstance);
    void drawInstanced(GraphicsState* pState, GraphicsVars* pVars, uint32_t vertexCount, uint32_t instanceCount, uint32_t startVertex, uint32_t startInstance);

    void clearFbo(Fbo* pFbo, const float4& color, float depth, uint8_t stencil, FboAttachmentType attachments = FboAttachmentType::All);
    void clearRtv(Diligent::ITextureView* pRtv, const float4& color);
    void clearTexture(Texture* tx);
    void blit(Diligent::ITextureView* pSrc, Diligent::ITextureView* pDst, const glm::vec4& srcRect, const glm::vec4& dstRect, Sampler::Filter filter, BlendState::SharedPtr blend = nullptr);
    void updateTextureData(Texture* pTexture, const void* pData);
    void copyResource(Texture* pSrc, Texture* pDst);
    void copyResource(Buffer* pSrc, Buffer* pDst);
    void copySubresource(Texture* pDst, uint32_t dstSubresource, Texture* pSrc, uint32_t srcSubresource);
    void resourceBarrier(Texture* pTexture, Resource::State state);
    void flush();
    void flush(bool waitForGpu);

    std::vector<uint8_t> readTextureSubresource(Texture* pTexture, uint32_t subresource);

    Diligent::IDeviceContext* GetDiligentContext() const { return m_pContext; }

private:
    Diligent::IDeviceContext* m_pContext = nullptr;
};

class Gui
{
public:
    enum class WindowFlags : uint32_t
    {
        None     = 0,
        Empty    = 1u << 0,
        NoResize = 1u << 1,
    };

    class Window
    {
    public:
        Window(Gui* pGui, const char* name, bool show_window, float2 size, float2 pos, WindowFlags flags = WindowFlags::None);
        Window(Gui* pGui, const char* name, float2 size, float2 pos, WindowFlags flags = WindowFlags::None);
        ~Window();

        void release();
        void windowPos(int x, int y);
        void windowSize(int w, int h);
        void image(const char* id, const Texture::SharedPtr& tex, float2 size);
        bool imageButton(const char* id, const Texture::SharedPtr& tex, float2 size);

    private:
        Gui*  m_pGui = nullptr;
        bool  m_Open = false;
    };

    ImFont* getFont(const char* name);
    void    addFont(const char* name, const std::string& path, float sizePx);

private:
    std::unordered_map<std::string, ImFont*> m_Fonts;
};

inline Gui::WindowFlags operator|(Gui::WindowFlags a, Gui::WindowFlags b)
{
    return static_cast<Gui::WindowFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

namespace Input
{
enum class Modifier
{
    Ctrl  = 1u << 0,
    Shift = 1u << 1,
    Alt   = 1u << 2,
};

enum class Key
{
    A,
    B,
    C,
    D,
    F,
    G,
    H,
    J,
    K,
    M,
    N,
    O,
    Q,
    R,
    S,
    T,
    V,
    X,
    Y, W,
    Del,
    Escape,
    Space,
    Enter,
    Up,
    Down,
    Left,
    Right,
    Key0,
    Key1,
    Key2,
    Key3,
    Key4,
    Key5,
    Key6,
    Key7,
    LeftControl,
    LeftShift
};

enum class MouseButton
{
    Left,
    Right,
    Middle,
};
} // namespace Input

struct KeyboardEvent
{
    enum class Type
    {
        KeyPressed,
        KeyReleased,
    };

    Type         type = Type::KeyPressed;
    Input::Key   key  = Input::Key::D;
    uint32_t     modifiers = 0;

    bool hasModifier(Input::Modifier mod) const
    {
        return (modifiers & static_cast<uint32_t>(mod)) != 0;
    }
};

struct MouseEvent
{
    enum class Type
    {
        Move,
        ButtonDown,
        ButtonUp,
        Wheel,
    };

    enum class Buttons
    {
        Left   = 1,
        Right  = 2,
        Middle = 4,
    };

    Type    type    = Type::Move;
    Buttons buttons = Buttons::Left;
    Input::MouseButton button = Input::MouseButton::Left;
    float2  pos{};
    float2  wheelDelta{};
};

struct FrameRate
{
    double getAverageFrameTime() const;
};

class MonitorInfo
{
public:
    struct Desc
    {
        glm::float2 resolution{1920.f, 1080.f};
    };

    static std::vector<Desc> getMonitorDescs();
};

class WindowInterface
{
public:
    void shutdown();
};

class FrameworkInterface
{
public:
    virtual ~FrameworkInterface() = default;
    virtual FrameRate getFrameRate() const;
    virtual WindowInterface* getWindow();
    virtual double getAverageFrameTimeMs() const { return 16.0; }
};

class DeviceInterface
{
public:
    void toggleVSync(bool enabled);
    RenderContext* getRenderContext();
};

extern DeviceInterface*     gpDevice;
extern FrameworkInterface*  gpFramework;

void SetFalcorDevice(Diligent::IRenderDevice* pDevice, Diligent::IDeviceContext* pContext, Diligent::ISwapChain* pSwapChain,
                     Diligent::IEngineFactory* pFactory = nullptr);
void SetFalcorFramework(FrameworkInterface* pFramework);

void addDataDirectory(const std::filesystem::path& path, bool prepend);

extern std::vector<std::filesystem::path> g_DataDirectories;

std::filesystem::path RemapShaderPath(const std::filesystem::path& falcorPath);

struct FileDialogFilterDesc
{
    std::string name;
    std::string spec;
};

using FileDialogFilterVec = std::vector<FileDialogFilterDesc>;

// Returns false until a native file dialog is wired (headless / CI safe).
bool openFileDialog(const FileDialogFilterVec& filters, std::filesystem::path& selectedPath);
bool saveFileDialog(const FileDialogFilterVec& filters, std::filesystem::path& selectedPath);

class Profiler
{
public:
    class Event
    {
    public:
        double getGpuTimeAverage() const { return 0.0; }
    };

    static Profiler& instance();
    std::shared_ptr<Event> getEvent(const char* name);
};

class TextRenderer
{
public:
    static void setColor(const float3& color);
    static void render(RenderContext* pContext, const std::string& text, const Fbo::SharedPtr& pFbo, const float2& pos);
};

class IRenderer {

public:
    virtual void onLoad(RenderContext* _renderContext) = 0;
    //virtual void onFrameUpdate(RenderContext* _renderContext) = 0;
    virtual void onFrameRender(RenderContext* _renderContext, const Fbo::SharedPtr& pTargetFbo) = 0;
    //virtual void onRenderOverlay(RenderContext* _renderContext, const Fbo::SharedPtr& pTargetFbo);
    virtual void onShutdown() = 0;
    virtual void onResizeSwapChain(uint32_t _width, uint32_t _height) = 0;
    virtual bool onKeyEvent(const KeyboardEvent& keyEvent)            = 0;
    virtual bool onMouseEvent(const MouseEvent& mouseEvent)           = 0;
    virtual void initGui(Gui* _gui);
    virtual void onGuiRender(Gui* _gui) = 0;
    virtual void onGuiMenubar(Gui* _gui);
};

class EarthworksWrapper {
public:
    EarthworksWrapper();
    ~EarthworksWrapper();
};

inline void reportError(const std::string& msg)
{
    spdlog::error("{}", msg);
}

} // namespace Falcor
