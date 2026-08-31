#include "ewShader.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

#include "GraphicsTypes.h"

#include "ots/Log.hpp"

#include "EarthworksDebug.h"   // PSO/SRB creation counters (step-6 perf pass)

// ---------------------------------------------------------------------------
// Implementation notes:
//
//   * DXC compile path with forward-slash file paths (backslashes break DXC's
//     #line handling), per-shader include search paths, constant-buffer
//     reflection enabled.
//   * dx/dy builtin-name workaround: recent DXC puts 'dx' in the global
//     scope; the fog shaders declare cbuffer members named dx/dy. They are
//     renamed for the compiler via -D and the rename is undone when matching
//     reflected member names (ewMemberName).
//   * PSO resource layout: default variable type DYNAMIC (one cached SRB is
//     re-bound and re-committed per draw; fresh per-commit descriptors), with
//     NO_DYNAMIC_BUFFERS on all structured buffers (Vulkan
//     maxDescriptorSetStorageBuffersDynamic is ~8-16 and
//     compute_tileBuildLookup binds 60 RWStructuredBuffers).
//   * Resources are bound to EVERY stage that declares them (Diligent keeps a
//     separate variable per stage; binding only the first hit leaves GS
//     descriptors dangling).
//   * Graphics PSOs declare ALL color attachments of the FBO (8-MRT bakes).
//   * Front faces are COUNTER-clockwise - forced here, the engine never uses
//     CW front faces.
// ---------------------------------------------------------------------------

namespace ew
{

using namespace Diligent;

namespace
{

// A failed shader compile is never recoverable, so fail fast.
[[noreturn]] void fatalGpuError(const std::string& msg)
{
    spdlog::critical("EarthworksFX fatal: {}", msg);
    throw std::runtime_error(msg);
}

// --- DXC builtin-name collision workaround ------------------------------------
struct BuiltinNameWorkaround
{
    const char* engineName;   // name used in HLSL source + C++ binds
    const char* compilerName; // collision-free substitute the compiler sees
};
const BuiltinNameWorkaround g_BuiltinNameWorkarounds[] = {
    {"dx", "ew_dx"},
    {"dy", "ew_dy"},
};

// Reflected (compiler-visible) member name -> engine binding name.
std::string ewMemberName(const char* reflectedName)
{
    for (const auto& w : g_BuiltinNameWorkarounds)
    {
        if (std::strcmp(reflectedName, w.compilerName) == 0)
            return w.engineName;
    }
    return reflectedName;
}

ShaderVersion parseShaderModel(const std::string& model)
{
    ShaderVersion ver{6, 0};
    if (model.size() >= 3 && model[0] == '6' && model[1] == '_')
    {
        const int minor = model[2] - '0';
        if (minor >= 0 && minor <= 9)
            ver.Minor = static_cast<Uint8>(minor);
    }
    return ver;
}

RefCntAutoPtr<IShader> createShaderFromFile(const std::filesystem::path& path,
                                            const char* entry,
                                            SHADER_TYPE type,
                                            const DefineList& defines,
                                            const std::string& shaderModel)
{
    GpuContext* pGpu = GpuContext::get();

    const auto resolved = pGpu->resolvePath(path);
    if (!std::filesystem::exists(resolved))
        fatalGpuError("shader not found: " + path.string());

    // Forward slashes (generic_string): with native backslashes DXC's clang
    // preprocessor reads '\d', '\g', ... in the '#line' path as unknown
    // escape sequences and mangles the reported file name.
    const std::string shaderPath = resolved.generic_string();
    const std::string shaderName = resolved.filename().string() + "_" + entry;

    std::string       perShaderSearch;
    const std::string shaderDir = resolved.parent_path().generic_string();
    if (!shaderDir.empty() && !pGpu->shaderSearchPaths().empty())
        perShaderSearch = shaderDir + ';' + pGpu->shaderSearchPaths();
    else if (!shaderDir.empty())
        perShaderSearch = shaderDir;
    else
        perShaderSearch = pGpu->shaderSearchPaths();

    RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderFactory;
    if (pGpu->factory() && !perShaderSearch.empty())
        pGpu->factory()->CreateDefaultShaderSourceStreamFactory(perShaderSearch.c_str(), &pShaderFactory);

    std::vector<std::pair<std::string, std::string>> macroStorage;
    std::vector<ShaderMacro>                         macros;
    for (const auto& def : defines.get())
        macroStorage.emplace_back(def.first, def.second);
    for (const auto& w : g_BuiltinNameWorkarounds)
        macroStorage.emplace_back(w.engineName, w.compilerName);
    macros.reserve(macroStorage.size());
    for (const auto& macro : macroStorage)
        macros.push_back(ShaderMacro{macro.first.c_str(), macro.second.c_str()});

    ShaderCreateInfo shaderCI;
    shaderCI.SourceLanguage                  = SHADER_SOURCE_LANGUAGE_HLSL;
    shaderCI.Desc.UseCombinedTextureSamplers = false;
    shaderCI.Desc.ShaderType                 = type;
    shaderCI.EntryPoint                      = entry;
    shaderCI.Desc.Name                       = shaderName.c_str();
    shaderCI.FilePath                        = shaderPath.c_str();
    shaderCI.pShaderSourceStreamFactory      = pShaderFactory;
    shaderCI.LoadConstantBufferReflection    = true;
    shaderCI.HLSLVersion                     = parseShaderModel(shaderModel);
    shaderCI.Macros                          = ShaderMacroArray{macros.data(), static_cast<Uint32>(macros.size())};
    shaderCI.ShaderCompiler                  = SHADER_COMPILER_DXC;

    RefCntAutoPtr<IShader> pShader;
    try
    {
        pGpu->device()->CreateShader(shaderCI, &pShader);
    }
    catch (const std::exception& ex)
    {
        fatalGpuError("shader compile failed: " + shaderPath + " [" + entry + "]: " + ex.what());
    }
    if (!pShader)
        fatalGpuError("shader compile failed (null result): " + shaderPath + " [" + entry + "]");
    return pShader;
}

void collectShaderResources(IShader* pShader, SHADER_TYPE stage, std::vector<detail::ResourceInfo>& outResources)
{
    if (!pShader)
        return;

    const Uint32 count = pShader->GetResourceCount();
    for (Uint32 i = 0; i < count; ++i)
    {
        ShaderResourceDesc desc;
        pShader->GetResourceDesc(i, desc);
        if (!desc.Name)
            continue;

        bool found = false;
        for (auto& r : outResources)
        {
            if (r.Name == desc.Name)
            {
                r.stages = static_cast<SHADER_TYPE>(r.stages | stage);
                found    = true;
                break;
            }
        }
        if (!found)
            outResources.push_back({desc.Name, desc.Type, stage, std::max(desc.ArraySize, 1u)});
    }
}

void collectCBuffers(IShader* pShader, SHADER_TYPE stage, std::vector<detail::CBufferLayout>& outCBuffers)
{
    if (!pShader)
        return;

    const Uint32 count = pShader->GetResourceCount();
    for (Uint32 i = 0; i < count; ++i)
    {
        ShaderResourceDesc desc;
        pShader->GetResourceDesc(i, desc);
        if (desc.Type != SHADER_RESOURCE_TYPE_CONSTANT_BUFFER || !desc.Name)
            continue;

        bool exists = false;
        for (auto& existing : outCBuffers)
        {
            if (existing.name == desc.Name)
            {
                existing.stages = static_cast<SHADER_TYPE>(existing.stages | stage);
                exists          = true;
                break;
            }
        }
        if (exists)
            continue;

        const ShaderCodeBufferDesc* pCB = pShader->GetConstantBufferDesc(i);
        if (!pCB)
            continue;

        detail::CBufferLayout layout;
        layout.name   = desc.Name;
        layout.size   = pCB->Size;
        layout.stages = stage;
        for (Uint32 v = 0; v < pCB->NumVariables; ++v)
        {
            const ShaderCodeVariableDesc& var = pCB->pVariables[v];
            if (!var.Name)
                continue;
            detail::CBufferMember member;
            member.name   = ewMemberName(var.Name);
            member.offset = var.Offset;
            Uint32 memberSize = 4;
            if (var.Class == SHADER_CODE_VARIABLE_CLASS_MATRIX_COLUMNS ||
                var.Class == SHADER_CODE_VARIABLE_CLASS_MATRIX_ROWS)
                memberSize = static_cast<Uint32>(var.NumRows) * static_cast<Uint32>(var.NumColumns) * 4;
            else if (var.NumColumns > 0)
                memberSize = static_cast<Uint32>(var.NumColumns) * 4;
            member.size = memberSize;
            layout.members.push_back(std::move(member));
        }
        outCBuffers.push_back(std::move(layout));
    }
}

// PSO resource layout: flag all structured buffers NO_DYNAMIC_BUFFERS so they
// use regular (non-dynamic-offset) Vulkan descriptors. Contract: no
// USAGE_DYNAMIC buffer is ever bound to these slots - ew::Buffer only creates
// USAGE_DEFAULT storage.
std::vector<ShaderResourceVariableDesc>
makeStorageBuffersNonDynamic(const std::vector<detail::ResourceInfo>& resources)
{
    std::vector<ShaderResourceVariableDesc> vars;
    vars.reserve(resources.size());
    for (const auto& r : resources)
    {
        if (r.type == SHADER_RESOURCE_TYPE_BUFFER_SRV || r.type == SHADER_RESOURCE_TYPE_BUFFER_UAV)
            vars.push_back({r.stages, r.Name.c_str(), SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC,
                            SHADER_VARIABLE_FLAG_NO_DYNAMIC_BUFFERS});
    }
    return vars;
}

// --- dummy resources for unbound slots ------------------------------------------
// Safety net: an unbound texture/sampler slot gets a 1x1 2D dummy (and a
// warn-once log) instead of undefined descriptors. There is NO name-based
// dimension guessing, so a TextureCube/Texture3D slot left unbound trips
// Vulkan's view-type validation - the warn-once log names the slot, bind it
// properly at the call site.

Texture::SharedPtr& dummyTexture2D()
{
    static Texture::SharedPtr s_Tex;
    if (!s_Tex)
    {
        const uint32_t zero = 0;
        s_Tex = Texture::create2D(1, 1, TEX_FORMAT_RGBA8_UNORM, 1, 1, &zero,
                                  BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS, "ew dummy 2D");
    }
    return s_Tex;
}

Sampler::SharedPtr& dummySampler()
{
    static Sampler::SharedPtr s_Smp;
    if (!s_Smp)
    {
        SamplerDesc desc;
        desc.MinFilter = FILTER_TYPE_LINEAR;
        desc.MagFilter = FILTER_TYPE_LINEAR;
        desc.MipFilter = FILTER_TYPE_LINEAR;
        desc.AddressU  = TEXTURE_ADDRESS_CLAMP;
        desc.AddressV  = TEXTURE_ADDRESS_CLAMP;
        desc.AddressW  = TEXTURE_ADDRESS_CLAMP;
        s_Smp          = Sampler::create(desc);
    }
    return s_Smp;
}

void warnUnboundOnce(const std::string& shaderName, const std::string& resourceName)
{
    static std::unordered_set<std::string> s_Warned;
    if (s_Warned.insert(shaderName + ":" + resourceName).second)
        spdlog::warn("ew shader '{}': no resource bound to '{}' - using a dummy", shaderName, resourceName);
}

/// Sets a variable in EVERY stage that declares it.
void bindToStages(IShaderResourceBinding* pSRB, const detail::ResourceInfo& res, IDeviceObject* pObject)
{
    if (!pSRB || !pObject)
        return;
    static const SHADER_TYPE kStages[] = {SHADER_TYPE_VERTEX, SHADER_TYPE_PIXEL,
                                          SHADER_TYPE_GEOMETRY, SHADER_TYPE_COMPUTE};
    for (SHADER_TYPE stage : kStages)
    {
        if ((res.stages & stage) == 0)
            continue;
        if (IShaderResourceVariable* pVar = pSRB->GetVariableByName(stage, res.Name.c_str()))
            pVar->Set(pObject);
    }
}

void bindArrayToStages(IShaderResourceBinding* pSRB, const detail::ResourceInfo& res,
                       const std::vector<IDeviceObject*>& objects)
{
    if (!pSRB || objects.empty())
        return;
    static const SHADER_TYPE kStages[] = {SHADER_TYPE_VERTEX, SHADER_TYPE_PIXEL,
                                          SHADER_TYPE_GEOMETRY, SHADER_TYPE_COMPUTE};
    for (SHADER_TYPE stage : kStages)
    {
        if ((res.stages & stage) == 0)
            continue;
        if (IShaderResourceVariable* pVar = pSRB->GetVariableByName(stage, res.Name.c_str()))
            pVar->SetArray(objects.data(), 0, static_cast<Uint32>(objects.size()));
    }
}

/// Uploads dirty cbuffers and binds every reflected resource of the program
/// into the SRB from the wrapper's named binding maps.
void bindProgram(GpuContext* pCtx, IShaderResourceBinding* pSRB, detail::ProgramData& data)
{
    // Constant buffers: per-instance GPU buffer + CPU shadow, updated when dirty.
    for (const auto& cbLayout : data.cbuffers)
    {
        detail::CBufferState& state = data.cbufferState[cbLayout.name];
        if (state.shadow.size() < cbLayout.size)
        {
            state.shadow.resize(cbLayout.size, 0);
            state.dirty = true;
        }
        if (!state.gpuBuffer)
        {
            BufferDesc desc;
            const std::string name = data.debugName + ":" + cbLayout.name;
            desc.Name              = name.c_str();
            desc.Size              = cbLayout.size;
            desc.Usage             = USAGE_DEFAULT;
            desc.BindFlags         = BIND_UNIFORM_BUFFER;
            pCtx->device()->CreateBuffer(desc, nullptr, &state.gpuBuffer);
            state.dirty = true;
        }
        if (state.dirty && state.gpuBuffer)
        {
            pCtx->context()->UpdateBuffer(state.gpuBuffer, 0, cbLayout.size, state.shadow.data(),
                                          RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            state.dirty = false;
        }
    }

    for (const auto& res : data.resources)
    {
        switch (res.type)
        {
            case SHADER_RESOURCE_TYPE_CONSTANT_BUFFER:
            {
                auto it = data.cbufferState.find(res.Name);
                if (it != data.cbufferState.end() && it->second.gpuBuffer)
                    bindToStages(pSRB, res, it->second.gpuBuffer);
                break;
            }
            case SHADER_RESOURCE_TYPE_TEXTURE_SRV:
            case SHADER_RESOURCE_TYPE_TEXTURE_UAV:
            {
                if (res.arraySize > 1)
                {
                    // Arrayed texture slot (textures_T[4096] et al): every
                    // element must be written or Vulkan rejects the draw.
                    Texture::SharedPtr dummy   = dummyTexture2D();
                    IDeviceObject*     pDummy = res.type == SHADER_RESOURCE_TYPE_TEXTURE_UAV
                        ? static_cast<IDeviceObject*>(dummy->getUAV())
                        : static_cast<IDeviceObject*>(dummy->getSRV());
                    std::vector<IDeviceObject*> objects(res.arraySize, pDummy);

                    auto arrIt = data.textureArrays.find(res.Name);
                    if (arrIt != data.textureArrays.end())
                    {
                        const size_t n = std::min<size_t>(arrIt->second.size(), objects.size());
                        for (size_t i = 0; i < n; ++i)
                        {
                            const Texture::SharedPtr& tex = arrIt->second[i];
                            if (!tex)
                                continue;
                            IDeviceObject* pView = res.type == SHADER_RESOURCE_TYPE_TEXTURE_UAV
                                ? static_cast<IDeviceObject*>(tex->getUAV())
                                : static_cast<IDeviceObject*>(tex->getSRV());
                            if (pView)
                                objects[i] = pView;
                        }
                    }
                    else
                    {
                        warnUnboundOnce(data.debugName, res.Name);
                    }
                    bindArrayToStages(pSRB, res, objects);
                    break;
                }

                IDeviceObject* pView = nullptr;
                auto           it    = data.textures.find(res.Name);
                if (it != data.textures.end() && it->second)
                {
                    pView = res.type == SHADER_RESOURCE_TYPE_TEXTURE_UAV
                        ? static_cast<IDeviceObject*>(it->second->getUAV())
                        : static_cast<IDeviceObject*>(it->second->getSRV());
                    if (res.type == SHADER_RESOURCE_TYPE_TEXTURE_UAV && !pView)
                        // Texture without BIND_UNORDERED_ACCESS on a RWTexture
                        // slot: GPU writes silently disappear (this is what
                        // flat terrain looks like). Never mask it.
                        spdlog::error("ew shader '{}': texture bound to UAV slot '{}' lacks BIND_UNORDERED_ACCESS",
                                      data.debugName, res.Name);
                }
                if (!pView)
                {
                    warnUnboundOnce(data.debugName, res.Name);
                    Texture::SharedPtr dummy = dummyTexture2D();
                    pView                    = res.type == SHADER_RESOURCE_TYPE_TEXTURE_UAV
                        ? static_cast<IDeviceObject*>(dummy->getUAV())
                        : static_cast<IDeviceObject*>(dummy->getSRV());
                }
                bindToStages(pSRB, res, pView);
                break;
            }
            case SHADER_RESOURCE_TYPE_BUFFER_SRV:
            case SHADER_RESOURCE_TYPE_BUFFER_UAV:
            {
                auto it = data.buffers.find(res.Name);
                if (it != data.buffers.end() && it->second)
                {
                    IDeviceObject* pView = res.type == SHADER_RESOURCE_TYPE_BUFFER_UAV
                        ? static_cast<IDeviceObject*>(it->second->getUAV())
                        : static_cast<IDeviceObject*>(it->second->getSRV());
                    if (pView)
                        bindToStages(pSRB, res, pView);
                    else
                        spdlog::error("ew shader '{}': buffer for '{}' lacks the required view (bind flags?)",
                                      data.debugName, res.Name);
                }
                else
                {
                    warnUnboundOnce(data.debugName, res.Name);
                }
                break;
            }
            case SHADER_RESOURCE_TYPE_SAMPLER:
            {
                auto it = data.samplers.find(res.Name);
                if (it != data.samplers.end() && it->second && it->second->handle())
                {
                    bindToStages(pSRB, res, it->second->handle());
                }
                else
                {
                    warnUnboundOnce(data.debugName, res.Name);
                    if (dummySampler() && dummySampler()->handle())
                        bindToStages(pSRB, res, dummySampler()->handle());
                }
                break;
            }
            default:
                break;
        }
    }
}

PRIMITIVE_TOPOLOGY mapTopology(Topology topology)
{
    switch (topology)
    {
        case Topology::PointList: return PRIMITIVE_TOPOLOGY_POINT_LIST;
        case Topology::LineList: return PRIMITIVE_TOPOLOGY_LINE_LIST;
        case Topology::LineStrip: return PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case Topology::TriangleStrip: return PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case Topology::TriangleList: break;
    }
    return PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

/// Binds render targets + viewport from the FBO (all MRT slots).
void setRenderTargets(GpuContext* pCtx, Fbo* pFbo, const Viewport* pViewportOverride)
{
    ITextureView* pRTVs[Fbo::kMaxColorTargets] = {};
    Uint32        numRTVs                      = 0;
    for (Uint32 slot = 0; slot < Fbo::kMaxColorTargets; ++slot)
    {
        if (ITextureView* pRTV = pFbo->getRenderTargetView(slot))
        {
            pRTVs[slot] = pRTV;
            numRTVs     = slot + 1;
        }
    }
    ITextureView* pDSV = pFbo->getDepthStencilView();
    pCtx->context()->SetRenderTargets(numRTVs, pRTVs, pDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    const float w = static_cast<float>(pFbo->getWidth());
    const float h = static_cast<float>(pFbo->getHeight());
    if (w > 0.f && h > 0.f)
    {
        Viewport vp;
        if (pViewportOverride)
        {
            vp = *pViewportOverride;
        }
        else
        {
            vp.Width    = w;
            vp.Height   = h;
            vp.MinDepth = 0.f;
            vp.MaxDepth = 1.f;
        }
        pCtx->context()->SetViewports(1, &vp, static_cast<Uint32>(w), static_cast<Uint32>(h));
    }
}

} // namespace

// --- detail::ProgramData ---------------------------------------------------------

const detail::CBufferLayout* detail::ProgramData::findCBuffer(const std::string& name) const
{
    for (const auto& cb : cbuffers)
    {
        if (cb.name == name)
            return &cb;
    }
    return nullptr;
}

void detail::ProgramData::setVariableBytes(const std::string& cb, const std::string& member,
                                           const void* pData, size_t size)
{
    const CBufferLayout* pLayout = findCBuffer(cb);
    if (!pLayout)
    {
        static std::unordered_set<std::string> s_Warned;
        if (s_Warned.insert(debugName + ":" + cb).second)
            spdlog::warn("ew shader '{}': setVariable on unknown cbuffer '{}'", debugName, cb);
        return;
    }

    for (const auto& m : pLayout->members)
    {
        if (m.name != member)
            continue;
        CBufferState& state = cbufferState[cb];
        if (state.shadow.size() < pLayout->size)
            state.shadow.resize(pLayout->size, 0);
        const size_t copySize = std::min<size_t>(size, m.size);
        if (m.offset + copySize <= state.shadow.size())
        {
            std::memcpy(state.shadow.data() + m.offset, pData, copySize);
            state.dirty = true;
        }
        return;
    }

    static std::unordered_set<std::string> s_WarnedMember;
    if (s_WarnedMember.insert(debugName + ":" + cb + ":" + member).second)
        spdlog::warn("ew shader '{}': cbuffer '{}' has no member '{}'", debugName, cb, member);
}

void detail::ProgramData::setBlob(const std::string& cb, const void* pData, size_t size)
{
    const CBufferLayout* pLayout = findCBuffer(cb);
    if (!pLayout)
    {
        static std::unordered_set<std::string> s_Warned;
        if (s_Warned.insert(debugName + ":" + cb).second)
            spdlog::warn("ew shader '{}': setBlob on unknown cbuffer '{}'", debugName, cb);
        return;
    }
    CBufferState& state = cbufferState[cb];
    if (state.shadow.size() < pLayout->size)
        state.shadow.resize(pLayout->size, 0);
    const size_t copySize = std::min<size_t>(size, state.shadow.size());
    if (copySize > 0 && pData)
    {
        std::memcpy(state.shadow.data(), pData, copySize);
        state.dirty = true;
    }
}

// --- DefineList --------------------------------------------------------------------

void DefineList::add(const std::string& name, const std::string& value)
{
    for (auto& def : m_Defines)
    {
        if (def.first == name)
        {
            def.second = value;
            return;
        }
    }
    m_Defines.emplace_back(name, value);
}

void DefineList::remove(const std::string& name)
{
    m_Defines.erase(std::remove_if(m_Defines.begin(), m_Defines.end(),
                                   [&](const auto& def) { return def.first == name; }),
                    m_Defines.end());
}

// --- computeShader -----------------------------------------------------------------

void computeShader::load(const std::filesystem::path& path)
{
    // Dummy define so reflection data always exists; the compute entry point
    // is always "main".
    m_Defines.add("CHUNK_SIZE", "256");

    m_Data           = detail::ProgramData{};
    m_Data.debugName = path.filename().string();
    m_PSO.Release();
    m_SRB.Release();

    m_CS = createShaderFromFile(path, "main", SHADER_TYPE_COMPUTE, m_Defines, "6_5");

    collectShaderResources(m_CS, SHADER_TYPE_COMPUTE, m_Data.resources);
    collectCBuffers(m_CS, SHADER_TYPE_COMPUTE, m_Data.cbuffers);
}

bool computeShader::commit(GpuContext* pCtx)
{
    if (!m_CS)
        return false;

    if (!m_PSO)
    {
        ComputePipelineStateCreateInfo psoCI;
        psoCI.PSODesc.Name         = m_Data.debugName.c_str();
        psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;
        psoCI.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;
        const auto nonDynBufferVars = makeStorageBuffersNonDynamic(m_Data.resources);
        if (!nonDynBufferVars.empty())
        {
            psoCI.PSODesc.ResourceLayout.Variables    = nonDynBufferVars.data();
            psoCI.PSODesc.ResourceLayout.NumVariables = static_cast<Uint32>(nonDynBufferVars.size());
        }
        psoCI.pCS = m_CS;

        try
        {
            pCtx->device()->CreateComputePipelineState(psoCI, &m_PSO);
        }
        catch (const std::exception& ex)
        {
            spdlog::error("ew::computeShader '{}': PSO creation threw: {}", m_Data.debugName, ex.what());
        }
        // Non-fatal: a shader used only in some terrain modes must not
        // hard-crash the app on a mode switch - log once and skip its
        // dispatches.
        if (!m_PSO)
        {
            spdlog::error("ew::computeShader '{}': PSO unavailable - dispatches are skipped", m_Data.debugName);
            return false;
        }
        m_PSO->CreateShaderResourceBinding(&m_SRB, true);

        // PORT-REVIEW (step 6 perf pass): cache-size / churn visibility. A
        // compute PSO is created exactly once per shader, on its first dispatch.
        ++gDebug.gpuObjects.computePSOs;
        ++gDebug.gpuObjects.srbs;
        ++gDebug.live.psoCreations;
    }
    if (!m_PSO || !m_SRB)
        return false;

    bindProgram(pCtx, m_SRB, m_Data);

    pCtx->context()->SetPipelineState(m_PSO);
    pCtx->context()->CommitShaderResources(m_SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    return true;
}

void computeShader::dispatch(GpuContext* pCtx, uint32_t width, uint32_t height, uint32_t slices)
{
    if (!pCtx || width == 0 || height == 0 || slices == 0)
        return;
    if (!commit(pCtx))
        return;

    DispatchComputeAttribs attribs;
    attribs.ThreadGroupCountX = width;
    attribs.ThreadGroupCountY = height;
    attribs.ThreadGroupCountZ = slices;
    pCtx->context()->DispatchCompute(attribs);
}

void computeShader::dispatchIndirect(GpuContext* pCtx, const Buffer* pArgBuffer, uint64_t byteOffset)
{
    if (!pCtx || !pArgBuffer || !pArgBuffer->handle())
        return;
    if (!commit(pCtx))
        return;

    DispatchComputeIndirectAttribs attribs;
    attribs.pAttribsBuffer         = pArgBuffer->handle();
    attribs.DispatchArgsByteOffset = byteOffset;
    pCtx->context()->DispatchComputeIndirect(attribs);
}

// --- pixelShader --------------------------------------------------------------------

void pixelShader::load(const std::filesystem::path& path, const std::string& vsEntry,
                       const std::string& psEntry, Topology topology, const std::string& gsEntry)
{
    m_Topology       = topology;
    m_Data           = detail::ProgramData{};
    m_Data.debugName = path.filename().string();
    m_PsoCache.clear();

    // GS passes need SM 6.5.
    const std::string shaderModel = gsEntry.empty() ? "6_0" : "6_5";

    m_VS = createShaderFromFile(path, vsEntry.c_str(), SHADER_TYPE_VERTEX, m_Defines, shaderModel);
    m_PS = createShaderFromFile(path, psEntry.c_str(), SHADER_TYPE_PIXEL, m_Defines, shaderModel);
    if (!gsEntry.empty())
        m_GS = createShaderFromFile(path, gsEntry.c_str(), SHADER_TYPE_GEOMETRY, m_Defines, shaderModel);

    collectShaderResources(m_VS, SHADER_TYPE_VERTEX, m_Data.resources);
    collectShaderResources(m_PS, SHADER_TYPE_PIXEL, m_Data.resources);
    if (m_GS)
        collectShaderResources(m_GS, SHADER_TYPE_GEOMETRY, m_Data.resources);
    collectCBuffers(m_VS, SHADER_TYPE_VERTEX, m_Data.cbuffers);
    collectCBuffers(m_PS, SHADER_TYPE_PIXEL, m_Data.cbuffers);
    if (m_GS)
        collectCBuffers(m_GS, SHADER_TYPE_GEOMETRY, m_Data.cbuffers);

    // The shared static 128-quad-pattern index buffer: (0,1,2, 1,3,2), +2 per
    // quad, R16. Used ONLY by drawIndexedInstanced (spline/ribbon quads over a
    // 65-vertex strip). Written once -> immutable, never dynamic.
    if (!m_QuadPatternIB)
    {
        std::vector<Uint16> ibData(128 * 6);
        for (Uint32 i = 0; i < 128; ++i)
        {
            ibData[i * 6 + 0] = static_cast<Uint16>(0 + i * 2);
            ibData[i * 6 + 1] = static_cast<Uint16>(1 + i * 2);
            ibData[i * 6 + 2] = static_cast<Uint16>(2 + i * 2);
            ibData[i * 6 + 3] = static_cast<Uint16>(1 + i * 2);
            ibData[i * 6 + 4] = static_cast<Uint16>(3 + i * 2);
            ibData[i * 6 + 5] = static_cast<Uint16>(2 + i * 2);
        }

        BufferDesc desc;
        desc.Name      = "ew 128-quad pattern IB";
        desc.Size      = static_cast<Uint64>(ibData.size() * sizeof(Uint16));
        desc.BindFlags = BIND_INDEX_BUFFER;
        desc.Usage     = USAGE_IMMUTABLE;

        BufferData init;
        init.pData    = ibData.data();
        init.DataSize = desc.Size;
        GpuContext::get()->device()->CreateBuffer(desc, &init, &m_QuadPatternIB);
    }
}

pixelShader::PsoCacheEntry* pixelShader::getOrCreatePso(const Diligent::BlendStateDesc& blend,
                                                        const Fbo::Formats& formats)
{
    for (auto& entry : m_PsoCache)
    {
        if (entry.formats == formats && entry.blend == blend &&
            entry.depth == m_DepthDesc && entry.raster == m_RasterDesc)
            return &entry;
    }

    GraphicsPipelineStateCreateInfo psoCI;
    psoCI.PSODesc.Name         = m_Data.debugName.c_str();
    psoCI.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
    // DYNAMIC variables: one cached SRB per PSO, safely re-bound and
    // re-committed for every draw in a frame (fresh per-commit descriptors).
    psoCI.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;
    const auto nonDynBufferVars = makeStorageBuffersNonDynamic(m_Data.resources);
    if (!nonDynBufferVars.empty())
    {
        psoCI.PSODesc.ResourceLayout.Variables    = nonDynBufferVars.data();
        psoCI.PSODesc.ResourceLayout.NumVariables = static_cast<Uint32>(nonDynBufferVars.size());
    }

    auto& gp = psoCI.GraphicsPipeline;
    // ALL color attachments, not just slot 0 (the terrafector bake writes 8).
    gp.NumRenderTargets = static_cast<Uint8>(formats.NumRenderTargets);
    for (Uint32 i = 0; i < formats.NumRenderTargets; ++i)
        gp.RTVFormats[i] = formats.RTVs[i];
    gp.DSVFormat         = formats.DSV;
    gp.PrimitiveTopology = mapTopology(m_Topology);

    gp.BlendDesc        = blend;
    gp.DepthStencilDesc = m_DepthDesc;
    gp.RasterizerDesc   = m_RasterDesc;
    // Invariant: front faces are COUNTER-clockwise everywhere in this engine
    // - with the right-handed camera, CW-front PSOs back-cull the terrain.
    // Diligent defaults to CW, so force it. No pass uses CW front faces.
    gp.RasterizerDesc.FrontCounterClockwise = True;

    psoCI.pVS = m_VS;
    psoCI.pPS = m_PS;
    psoCI.pGS = m_GS;

    RefCntAutoPtr<IPipelineState> pPSO;
    GpuContext::get()->device()->CreateGraphicsPipelineState(psoCI, &pPSO);
    if (!pPSO)
    {
        spdlog::error("ew::pixelShader '{}': graphics PSO creation failed", m_Data.debugName);
        return nullptr;
    }

    // Independent-blend PSOs (the 8-MRT terrafector bake): log the EFFECTIVE
    // per-RT blend once at PSO build. The RT0 One/OneMinusSrcAlpha elevation
    // override is the load-bearing detail of that bake - if this log ever
    // shows RT0 == RT1 the blend translation broke. Runs once per cache entry
    // by construction.
    if (blend.IndependentBlendEnable)
    {
        static auto factorName = [](BLEND_FACTOR f) -> const char* {
            switch (f)
            {
                case BLEND_FACTOR_ZERO:           return "Zero";
                case BLEND_FACTOR_ONE:            return "One";
                case BLEND_FACTOR_SRC_ALPHA:      return "SrcAlpha";
                case BLEND_FACTOR_INV_SRC_ALPHA:  return "InvSrcAlpha";
                case BLEND_FACTOR_SRC_ALPHA_SAT:  return "SrcAlphaSat";
                default:                          return "<other>";
            }
        };
        for (Uint32 i = 0; i < formats.NumRenderTargets; ++i)
        {
            const auto& rt = blend.RenderTargets[i];
            spdlog::info("ew::pixelShader '{}': PSO blend RT{} enable={} colour {}/{} alpha {}/{}",
                         m_Data.debugName, i, rt.BlendEnable ? 1 : 0,
                         factorName(rt.SrcBlend), factorName(rt.DestBlend),
                         factorName(rt.SrcBlendAlpha), factorName(rt.DestBlendAlpha));
        }
    }

    PsoCacheEntry entry;
    entry.blend   = blend;
    entry.depth   = m_DepthDesc;
    entry.raster  = m_RasterDesc;
    entry.formats = formats;
    entry.PSO     = pPSO;
    pPSO->CreateShaderResourceBinding(&entry.SRB, true);

    m_PsoCache.push_back(std::move(entry));

    // PORT-REVIEW (step 6 perf pass): cache churn visibility. Every cache MISS logs -
    // a miss during steady-state rendering is a hitch source (PSO creation is
    // milliseconds), and the per-frame count is in the debug panel
    // (live.psoCreations).
    ++gDebug.gpuObjects.graphicsPSOs;
    ++gDebug.gpuObjects.srbs;
    ++gDebug.live.psoCreations;
    spdlog::info("ew::pixelShader '{}': graphics PSO created (cache size now {}; process totals: {} gfx / {} comp)",
                 m_Data.debugName, m_PsoCache.size(),
                 gDebug.gpuObjects.graphicsPSOs, gDebug.gpuObjects.computePSOs);

    return &m_PsoCache.back();
}

bool pixelShader::commit(GpuContext* pCtx, const Diligent::BlendStateDesc& blend)
{
    if (!m_VS || !m_PS)
        return false;
    if (!m_Fbo)
    {
        static std::unordered_set<std::string> s_Warned;
        if (s_Warned.insert(m_Data.debugName).second)
            spdlog::error("ew::pixelShader '{}': draw without an FBO - skipped", m_Data.debugName);
        return false;
    }

    PsoCacheEntry* pEntry = getOrCreatePso(blend, m_Fbo->getFormats());
    if (!pEntry || !pEntry->PSO || !pEntry->SRB)
        return false;

    setRenderTargets(pCtx, m_Fbo.get(), m_HasViewport ? &m_Viewport : nullptr);

    bindProgram(pCtx, pEntry->SRB, m_Data);

    pCtx->context()->SetPipelineState(pEntry->PSO);
    pCtx->context()->CommitShaderResources(pEntry->SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    return true;
}

void pixelShader::drawInstanced(GpuContext* pCtx, uint32_t vertexCount, uint32_t instanceCount)
{
    if (!pCtx || vertexCount == 0 || instanceCount == 0)
        return;
    if (!commit(pCtx, m_BlendDesc))
        return;

    // ALWAYS non-indexed: the quad-pattern IB belongs to
    // drawIndexedInstanced only.
    DrawAttribs attribs;
    attribs.NumVertices  = vertexCount;
    attribs.NumInstances = instanceCount;
    pCtx->context()->Draw(attribs);
}

void pixelShader::drawIndexedInstanced(GpuContext* pCtx, uint32_t indexCount, uint32_t instanceCount)
{
    if (!pCtx || indexCount == 0 || instanceCount == 0 || !m_QuadPatternIB)
        return;
    if (!commit(pCtx, m_BlendDesc))
        return;

    pCtx->context()->SetIndexBuffer(m_QuadPatternIB, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    DrawIndexedAttribs attribs;
    attribs.NumIndices   = indexCount;
    attribs.NumInstances = instanceCount;
    attribs.IndexType    = VT_UINT16;
    pCtx->context()->DrawIndexed(attribs);
}

void pixelShader::renderIndirect(GpuContext* pCtx, const Buffer::SharedPtr& argBuffer,
                                 const Diligent::BlendStateDesc* pBlendOverride,
                                 uint32_t startArg, uint32_t numArgs)
{
    if (!pCtx || !argBuffer || !argBuffer->handle() || numArgs == 0)
        return;
    if (!commit(pCtx, pBlendOverride ? *pBlendOverride : m_BlendDesc))
        return;

    // NON-indexed 16-byte D3D12_DRAW_ARGUMENTS records; the offset is the
    // CameraType view index * 16. Routing this through DrawIndexedIndirect
    // (20-byte records) silently misreads the args and the terrain
    // disappears - never do that.
    DrawIndirectAttribs attribs;
    attribs.pAttribsBuffer                   = argBuffer->handle();
    attribs.DrawArgsOffset                   = static_cast<Uint64>(startArg) * 16u;
    attribs.DrawCount                        = numArgs;
    attribs.DrawArgsStride                   = 16u; // 4 uints - D3D12_DRAW_ARGUMENTS
    attribs.AttribsBufferStateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    pCtx->context()->DrawIndirect(attribs);
}

} // namespace ew
