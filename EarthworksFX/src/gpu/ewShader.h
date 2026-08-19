#pragma once

// ---------------------------------------------------------------------------
// ew::computeShader / ew::pixelShader - the two shader wrapper classes every
// draw and dispatch in the Earthworks engine funnels through. Resources are
// bound through EXPLICIT NAMED methods:
//
//     shader.setTexture("gAlbedo", tex);
//     shader.setBuffer("tiles", buffer);
//     shader.setVariable("gConstantBuffer", "viewproj", mat);   // cbuffer member
//     shader.setBlob("gConstants", data, size);                 // whole cbuffer
//
// Contract invariants baked in:
//   * binding is BY NAME via reflection - register annotations in the HLSL
//     sources are frequently wrong and must be ignored.
//   * compute entry point is always "main"; graphics vsMain/gsMain/psMain.
//   * dispatch() takes thread GROUP counts (callers pre-divide).
//   * renderIndirect() issues NON-indexed draws from 16-byte
//     D3D12_DRAW_ARGUMENTS records at byte offset startArg*16.
//   * drawInstanced() is always non-indexed, even though every pixelShader
//     carries the shared 128-quad index buffer; only drawIndexedInstanced()
//     binds that IB.
//   * front faces are COUNTER-clockwise - forced at PSO creation.
// ---------------------------------------------------------------------------

#include <cstring>
#include <filesystem>
#include <type_traits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ewGpuContext.h"
#include "ewResources.h"

namespace ew
{

enum class Topology
{
    PointList,
    LineList,
    LineStrip,
    TriangleList,
    TriangleStrip,
};

namespace detail
{

struct CBufferMember
{
    std::string     name;
    Diligent::Uint32 offset = 0;
    Diligent::Uint32 size   = 0;
};

struct CBufferLayout
{
    std::string          name;
    Diligent::Uint32     size   = 0;
    Diligent::SHADER_TYPE stages = Diligent::SHADER_TYPE_UNKNOWN;
    std::vector<CBufferMember> members;
};

struct ResourceInfo
{
    std::string                    Name;
    Diligent::SHADER_RESOURCE_TYPE type      = Diligent::SHADER_RESOURCE_TYPE_UNKNOWN;
    Diligent::SHADER_TYPE          stages    = Diligent::SHADER_TYPE_UNKNOWN;
    Diligent::Uint32               arraySize = 1;
};

/// Per-instance CPU shadow + GPU buffer of one cbuffer.
struct CBufferState
{
    Diligent::RefCntAutoPtr<Diligent::IBuffer> gpuBuffer;
    std::vector<uint8_t>                       shadow;
    bool                                       dirty = true;
};

/// Reflection data + user bindings shared by both wrapper classes.
struct ProgramData
{
    std::string debugName;

    std::vector<CBufferLayout> cbuffers;
    std::vector<ResourceInfo>  resources;

    std::unordered_map<std::string, Texture::SharedPtr>              textures;
    std::unordered_map<std::string, std::vector<Texture::SharedPtr>> textureArrays;
    std::unordered_map<std::string, Buffer::SharedPtr>               buffers;
    std::unordered_map<std::string, Sampler::SharedPtr>              samplers;
    std::unordered_map<std::string, CBufferState>                    cbufferState;

    const CBufferLayout* findCBuffer(const std::string& name) const;

    void setVariableBytes(const std::string& cb, const std::string& member,
                          const void* pData, size_t size);
    void setBlob(const std::string& cb, const void* pData, size_t size);
};

} // namespace detail

class DefineList
{
public:
    void add(const std::string& name, const std::string& value);
    void remove(const std::string& name);
    const std::vector<std::pair<std::string, std::string>>& get() const { return m_Defines; }

private:
    std::vector<std::pair<std::string, std::string>> m_Defines;
};

// ---------------------------------------------------------------------------

class computeShader
{
public:
    /// Compiles <path> with entry point "main" (always) via DXC. Defines added
    /// with add() BEFORE load() are baked in; a dummy CHUNK_SIZE=256 define is
    /// always present so reflection data always exists.
    void load(const std::filesystem::path& path);

    void add(const std::string& name, const std::string& value) { m_Defines.add(name, value); }
    void remove(const std::string& name) { m_Defines.remove(name); }

    /// Dispatches thread GROUPS (callers pre-divide by the group size).
    void dispatch(GpuContext* pCtx, uint32_t width, uint32_t height, uint32_t slices = 1);
    void dispatchIndirect(GpuContext* pCtx, const Buffer* pArgBuffer, uint64_t byteOffset);

    // --- named binding --------------------------------------------------------
    void setTexture(const std::string& name, const Texture::SharedPtr& tex) { m_Data.textures[name] = tex; }
    void setTextureArray(const std::string& name, const std::vector<Texture::SharedPtr>& textures) { m_Data.textureArrays[name] = textures; }
    void setBuffer(const std::string& name, const Buffer::SharedPtr& buf) { m_Data.buffers[name] = buf; }
    void setSampler(const std::string& name, const Sampler::SharedPtr& smp) { m_Data.samplers[name] = smp; }

    /// Whole-cbuffer upload (frustumflags, tileForSplit,
    /// ecotopeGpuConstants).
    void setBlob(const std::string& cbName, const void* pData, size_t size) { m_Data.setBlob(cbName, pData, size); }

    /// Single cbuffer member by name.
    template <typename T>
    void setVariable(const std::string& cbName, const std::string& member, const T& value)
    {
        static_assert(!std::is_pointer<T>::value, "pass the value, not a pointer");
        m_Data.setVariableBytes(cbName, member, &value, sizeof(T));
    }

    bool isLoaded() const { return m_CS != nullptr; }

private:
    bool commit(GpuContext* pCtx);

    DefineList                                 m_Defines;
    detail::ProgramData                        m_Data;
    Diligent::RefCntAutoPtr<Diligent::IShader> m_CS;

    Diligent::RefCntAutoPtr<Diligent::IPipelineState>         m_PSO;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> m_SRB;
};

// ---------------------------------------------------------------------------

class pixelShader
{
public:
    /// Compiles vs+ps (+gs when given; a GS forces shader model 6_5 - the
    /// tile-sprite/ribbon passes need it). Also creates the shared static
    /// 128-quad-pattern index buffer used ONLY by drawIndexedInstanced().
    void load(const std::filesystem::path& path, const std::string& vsEntry,
              const std::string& psEntry, Topology topology, const std::string& gsEntry = "");

    void add(const std::string& name, const std::string& value) { m_Defines.add(name, value); }
    void remove(const std::string& name) { m_Defines.remove(name); }

    // --- retained pipeline state: set once, sticks until overwritten ----------
    void setFbo(const Fbo::SharedPtr& fbo) { m_Fbo = fbo; }
    void setBlendState(const Diligent::BlendStateDesc& desc) { m_BlendDesc = desc; }
    void setDepthStencilState(const Diligent::DepthStencilStateDesc& desc) { m_DepthDesc = desc; }
    void setRasterizerState(const Diligent::RasterizerStateDesc& desc) { m_RasterDesc = desc; }
    /// Explicit viewport override; by default draws use the full FBO size.
    void setViewport(const Diligent::Viewport& vp) { m_Viewport = vp; m_HasViewport = true; }
    void clearViewport() { m_HasViewport = false; }

    const Fbo::SharedPtr& getFbo() const { return m_Fbo; }

    // --- draws ------------------------------------------------------------------
    /// Non-indexed instanced draw. NEVER binds the index buffer.
    void drawInstanced(GpuContext* pCtx, uint32_t vertexCount, uint32_t instanceCount);

    /// Indexed draw using the shared 128-quad-pattern IB (spline/ribbon quads).
    void drawIndexedInstanced(GpuContext* pCtx, uint32_t indexCount, uint32_t instanceCount);

    /// Indirect NON-indexed draw: numArgs 16-byte D3D12_DRAW_ARGUMENTS records
    /// starting at byte offset startArg*16 (startArg = CameraType view index).
    /// Optional per-call blend override.
    void renderIndirect(GpuContext* pCtx, const Buffer::SharedPtr& argBuffer,
                        const Diligent::BlendStateDesc* pBlendOverride = nullptr,
                        uint32_t startArg = 0, uint32_t numArgs = 1);

    // --- named binding (same surface as computeShader) ---------------------------
    void setTexture(const std::string& name, const Texture::SharedPtr& tex) { m_Data.textures[name] = tex; }
    void setTextureArray(const std::string& name, const std::vector<Texture::SharedPtr>& textures) { m_Data.textureArrays[name] = textures; }
    void setBuffer(const std::string& name, const Buffer::SharedPtr& buf) { m_Data.buffers[name] = buf; }
    void setSampler(const std::string& name, const Sampler::SharedPtr& smp) { m_Data.samplers[name] = smp; }
    void setBlob(const std::string& cbName, const void* pData, size_t size) { m_Data.setBlob(cbName, pData, size); }

    template <typename T>
    void setVariable(const std::string& cbName, const std::string& member, const T& value)
    {
        static_assert(!std::is_pointer<T>::value, "pass the value, not a pointer");
        m_Data.setVariableBytes(cbName, member, &value, sizeof(T));
    }

    bool isLoaded() const { return m_VS != nullptr && m_PS != nullptr; }

private:
    struct PsoCacheEntry
    {
        Diligent::BlendStateDesc        blend;
        Diligent::DepthStencilStateDesc depth;
        Diligent::RasterizerStateDesc   raster;
        Fbo::Formats                    formats;

        Diligent::RefCntAutoPtr<Diligent::IPipelineState>         PSO;
        Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> SRB;
    };

    /// Common draw setup: PSO/SRB (cached per state+formats), render targets,
    /// viewport, bindings, commit. Returns false when the draw must be skipped.
    bool commit(GpuContext* pCtx, const Diligent::BlendStateDesc& blend);

    PsoCacheEntry* getOrCreatePso(const Diligent::BlendStateDesc& blend, const Fbo::Formats& formats);

    DefineList          m_Defines;
    detail::ProgramData m_Data;

    Diligent::RefCntAutoPtr<Diligent::IShader> m_VS;
    Diligent::RefCntAutoPtr<Diligent::IShader> m_PS;
    Diligent::RefCntAutoPtr<Diligent::IShader> m_GS;

    Topology           m_Topology = Topology::TriangleList;
    Fbo::SharedPtr     m_Fbo;
    Diligent::Viewport m_Viewport;
    bool               m_HasViewport = false;

    Diligent::BlendStateDesc        m_BlendDesc;   // Diligent defaults = blend off
    Diligent::DepthStencilStateDesc m_DepthDesc;   // depth on, write on, Less
    Diligent::RasterizerStateDesc   m_RasterDesc;  // cull back (CCW front forced at PSO build)

    Diligent::RefCntAutoPtr<Diligent::IBuffer> m_QuadPatternIB;

    std::vector<PsoCacheEntry> m_PsoCache;
};

} // namespace ew
