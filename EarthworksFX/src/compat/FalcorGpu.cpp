#include "FalcorGpuInternal.hpp"

#include "ShaderResourceBinding.h"
#include "DeviceContext.h"
#include "ots/Log.hpp"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace Falcor
{
namespace Gpu
{
namespace
{

using namespace Diligent;

Diligent::IRenderDevice*  g_pDevice    = nullptr;
Diligent::IDeviceContext* g_pContext   = nullptr;
Diligent::ISwapChain*     g_pSwapChain = nullptr;
Diligent::IEngineFactory* g_pFactory   = nullptr;

RefCntAutoPtr<IShaderSourceInputStreamFactory> g_pShaderFactory;
std::string                                    g_ShaderSearchPaths;

struct CBufferLayout
{
    std::string              Name;
    Uint32                   Size = 0;
    SHADER_TYPE              Stages = SHADER_TYPE_UNKNOWN;
    std::vector<std::string> MemberNames;
    std::vector<Uint32>      MemberOffsets;
    std::vector<Uint32>      MemberSizes;
};

struct ShaderResourceInfo
{
    std::string          Name;
    SHADER_RESOURCE_TYPE Type = SHADER_RESOURCE_TYPE_UNKNOWN;
    SHADER_TYPE          Stages = SHADER_TYPE_UNKNOWN;
};

struct GfxProgramGpu
{
    RefCntAutoPtr<IShader> VS;
    RefCntAutoPtr<IShader> PS;
    RefCntAutoPtr<IShader> GS;
    std::string            DebugName;
    std::vector<CBufferLayout>       CBuffers;
    std::vector<ShaderResourceInfo>  Resources;
};

struct ComputeProgramGpu
{
    RefCntAutoPtr<IShader> CS;
    std::string            DebugName;
    std::vector<CBufferLayout>      CBuffers;
    std::vector<ShaderResourceInfo> Resources;
};

struct VaoGpu
{
    Vao::Topology     Topology = Vao::Topology::TriangleList;
    Vao::BufferVec    VBs;
    Buffer::SharedPtr IB;
    ResourceFormat    IndexFormat = ResourceFormat::R16Uint;
};

struct GraphicsPipelineCacheEntry
{
    RefCntAutoPtr<IPipelineState>         PSO;
    RefCntAutoPtr<IShaderResourceBinding> SRB;
};

std::unordered_map<const GraphicsProgram*, GfxProgramGpu>     g_GfxPrograms;
std::unordered_map<const ComputeProgram*, ComputeProgramGpu>  g_ComputePrograms;
std::unordered_map<const Vao*, VaoGpu>                        g_Vaos;

using GfxPipelineCacheKey = Uint64;
std::unordered_map<GfxPipelineCacheKey, GraphicsPipelineCacheEntry> g_GfxPipelineCache;

struct ComputePipelineCacheEntry
{
    RefCntAutoPtr<IPipelineState>         PSO;
    RefCntAutoPtr<IShaderResourceBinding> SRB;
};
std::unordered_map<const ComputeProgram*, ComputePipelineCacheEntry> g_ComputePSOCache;

Texture::SharedPtr g_DummyTex2D;
Texture::SharedPtr g_DummyTex3D;
Texture::SharedPtr g_DummyTexCube;
Sampler::SharedPtr g_DummySampler;

void EnsureDummyTextures()
{
    if (g_DummyTex2D)
        return;
    const uint32_t bind = Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess;
    g_DummyTex2D   = Texture::create2D(1, 1, ResourceFormat::RGBA8Unorm, 1, 1, nullptr, bind);
    g_DummyTex3D   = Texture::create3D(1, 1, 1, ResourceFormat::RGBA8Unorm, 1, nullptr, bind);
    g_DummyTexCube = Texture::createCube(1, ResourceFormat::RGBA8Unorm, Resource::BindFlags::ShaderResource);

    Sampler::Desc samplerDesc;
    samplerDesc.setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear);
    samplerDesc.setAddressingMode(Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp);
    g_DummySampler = Sampler::create(samplerDesc);
}

IDeviceObject* GetDummyTextureObject(SHADER_RESOURCE_TYPE type, const std::string& name)
{
    EnsureDummyTextures();
    // A texture/UAV slot that has no resource bound still needs a dummy whose
    // dimension matches the shader's declared type (TextureCube/Texture3D/
    // Texture2D); otherwise Vulkan rejects the draw with a viewType mismatch
    // (VUID-vkCmd...-viewType-07752). The reflection type does not carry the
    // dimension, so it is inferred from the resource name.
    const bool useCube = (name == "envMap" || name == "gSky");
    const bool use3D   = useCube ? false
        : (name.find("gCfd_T_") == 0 || name == "gLightVolume" || name == "gInscatter" || name == "gOutscatter"
           || name == "cube"
           || name.find("Inscatter") != std::string::npos || name.find("Outscatter") != std::string::npos);

    Texture::SharedPtr tex = useCube ? g_DummyTexCube : (use3D ? g_DummyTex3D : g_DummyTex2D);
    if (!tex)
        return nullptr;
    return type == SHADER_RESOURCE_TYPE_TEXTURE_UAV ? tex->getUAV() : tex->getSRV(0, 1, 0, 1);
}

std::unordered_map<const Buffer*, RefCntAutoPtr<IBufferView>> g_BufferSRVs;
std::unordered_map<const Buffer*, RefCntAutoPtr<IBufferView>> g_BufferUAVs;
std::unordered_map<std::string, Buffer::SharedPtr>            g_ConstantBufferCache;

void LogGpuError(const char* msg)
{
    spdlog::error(msg);
}

// Fail-fast: during bring-up a failed shader/PSO is never recoverable, so make it
// loud and fatal instead of silently substituting empty objects.
[[noreturn]] void FatalGpuError(const std::string& msg)
{
    spdlog::critical("EarthworksFX fatal: {}", msg);
    throw std::runtime_error(msg);
}

std::filesystem::path ResolveShaderPath(const std::filesystem::path& remapped)
{
    if (remapped.is_absolute() && std::filesystem::exists(remapped))
        return remapped;

    for (const auto& dir : Falcor::g_DataDirectories)
    {
        const auto candidate = dir / remapped;
        if (std::filesystem::exists(candidate))
            return candidate;
    }
    return remapped;
}

ShaderVersion ParseShaderModel(const std::string& model)
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

void CollectShaderResources(IShader* pShader, SHADER_TYPE stage, std::vector<ShaderResourceInfo>& outResources)
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
                r.Stages = static_cast<SHADER_TYPE>(r.Stages | stage);
                found    = true;
                break;
            }
        }
        if (!found)
            outResources.push_back({desc.Name, desc.Type, stage});
    }
}

void CollectCBuffers(IShader* pShader, SHADER_TYPE stage, std::vector<CBufferLayout>& outCBuffers)
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
            if (existing.Name == desc.Name)
            {
                existing.Stages = static_cast<SHADER_TYPE>(existing.Stages | stage);
                exists          = true;
                break;
            }
        }
        if (exists)
            continue;

        const ShaderCodeBufferDesc* pCB = pShader->GetConstantBufferDesc(i);
        if (!pCB)
            continue;

        CBufferLayout layout;
        layout.Name   = desc.Name;
        layout.Size   = pCB->Size;
        layout.Stages = stage;
        for (Uint32 v = 0; v < pCB->NumVariables; ++v)
        {
            const ShaderCodeVariableDesc& var = pCB->pVariables[v];
            if (!var.Name)
                continue;
            layout.MemberNames.push_back(var.Name);
            layout.MemberOffsets.push_back(var.Offset);
            Uint32 memberSize = 4;
            if (var.Class == SHADER_CODE_VARIABLE_CLASS_MATRIX_COLUMNS ||
                var.Class == SHADER_CODE_VARIABLE_CLASS_MATRIX_ROWS)
                memberSize = static_cast<Uint32>(var.NumRows) * static_cast<Uint32>(var.NumColumns) * 4;
            else if (var.NumColumns > 0)
                memberSize = static_cast<Uint32>(var.NumColumns) * 4;
            layout.MemberSizes.push_back(memberSize);
        }
        outCBuffers.push_back(std::move(layout));
    }
}

// --- DXC builtin-name collision workaround --------------------------------
// The bundled DXC (reported as "shader model 6.10", i.e. an SM6.9+/Shader-
// Execution-Reordering preview) puts builtins such as the 'dx' namespace in
// the global scope. The Earthworks fog shaders declare cbuffer members named
// literally 'dx'/'dy' (eye-ray basis vectors), which now collide:
//     compute_volumeFog.hlsli:93: error: redefinition of 'dx' as different
//     kind of symbol
// Slang (the original toolchain) and the earlier FXC/glslang fallbacks had no
// such builtin, which is why it only appears now that real DXC is used. Rather
// than edit the ported shaders + the by-name binds in the ported core
// (atmosphere.cpp uses Vars()[...]["dx"]), we rename the offending tokens for
// the compiler via -D macros and undo the rename when matching reflected
// member names back to the Falcor-side ShaderVar nodes. Both ends live in the
// compat layer, so they survive a re-port of the shaders/core.
struct BuiltinNameWorkaround
{
    const char* falcorName;   // name used in HLSL source + C++ Vars()[...] binds
    const char* compilerName; // substitute the compiler sees (collision-free)
};
static const BuiltinNameWorkaround g_BuiltinNameWorkarounds[] = {
    {"dx", "ew_dx"},
    {"dy", "ew_dy"},
};

// Reflected (compiler-visible) member name -> Falcor binding name.
static std::string FalcorMemberName(const std::string& reflectedName)
{
    for (const auto& w : g_BuiltinNameWorkarounds)
        if (reflectedName == w.compilerName)
            return w.falcorName;
    return reflectedName;
}

RefCntAutoPtr<IShader> CreateShaderFromFile(const std::filesystem::path& path,
                                            const char* entry,
                                            SHADER_TYPE type,
                                            const Program::DefineList& defines,
                                            const std::string& shaderModel)
{
    if (!g_pDevice)
        return {};

    const auto resolved = ResolveShaderPath(path);
    if (!std::filesystem::exists(resolved))
        FatalGpuError("EarthworksFX shader not found: " + resolved.string());

    // Use forward slashes (generic_string) for the path Diligent embeds in the
    // generated '#line 1 "<path>"' directive. With native Windows backslashes,
    // DXC's clang preprocessor reads '\d', '\g', ... as unknown escape sequences
    // (warnings) and mangles the reported file name.
    const std::string shaderPath = resolved.generic_string();
    const std::string shaderName = resolved.filename().string() + "_" + entry;

    std::string perShaderSearch;
    const std::string shaderDir = resolved.parent_path().generic_string();
    if (!shaderDir.empty() && !g_ShaderSearchPaths.empty())
        perShaderSearch = shaderDir + ';' + g_ShaderSearchPaths;
    else if (!shaderDir.empty())
        perShaderSearch = shaderDir;
    else
        perShaderSearch = g_ShaderSearchPaths;

    RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderFactory;
    if (g_pFactory && !perShaderSearch.empty())
        g_pFactory->CreateDefaultShaderSourceStreamFactory(perShaderSearch.c_str(), &pShaderFactory);
    if (!pShaderFactory)
        pShaderFactory = g_pShaderFactory;

    std::vector<std::pair<std::string, std::string>> macroStorage;
    std::vector<ShaderMacro>                         macros;
    for (const auto& def : defines.get())
        macroStorage.emplace_back(def.first, def.second);
    // Rename cbuffer members that collide with DXC builtin global names (see
    // g_BuiltinNameWorkarounds). Applied to every shader; these tokens are not
    // used for anything else in the Earthworks HLSL, and the rename is undone
    // for by-name binding in FalcorMemberName()/PackShaderVarNode().
    for (const auto& w : g_BuiltinNameWorkarounds)
        macroStorage.emplace_back(w.falcorName, w.compilerName);
    for (const auto& macro : macroStorage)
        macros.push_back(ShaderMacro{macro.first.c_str(), macro.second.c_str()});
    macros.push_back(ShaderMacro{nullptr, nullptr});

    ShaderCreateInfo shaderCI;
    shaderCI.SourceLanguage                  = SHADER_SOURCE_LANGUAGE_HLSL;
    shaderCI.Desc.UseCombinedTextureSamplers = false;
    shaderCI.Desc.ShaderType                 = type;
    shaderCI.EntryPoint                      = entry;
    shaderCI.Desc.Name                       = shaderName.c_str();
    shaderCI.FilePath                        = shaderPath.c_str();
    shaderCI.pShaderSourceStreamFactory      = pShaderFactory;
    shaderCI.LoadConstantBufferReflection    = true;
    shaderCI.HLSLVersion                     = ParseShaderModel(shaderModel);
    shaderCI.Macros                          = ShaderMacroArray{macros.data(), static_cast<Uint32>(macros.size() - 1)};
    shaderCI.ShaderCompiler                  = SHADER_COMPILER_DXC;

    RefCntAutoPtr<IShader> pShader;
    try
    {
        g_pDevice->CreateShader(shaderCI, &pShader);
    }
    catch (const std::exception& ex)
    {
        FatalGpuError("Shader compile failed: " + shaderPath + " [" + entry + "]: " + ex.what());
    }
    if (!pShader)
        FatalGpuError("Shader compile failed (null result): " + shaderPath + " [" + entry + "]");
    return pShader;
}

IBufferView* GetBufferSRV(const Buffer* pBuffer)
{
    if (!pBuffer || !pBuffer->GetDiligentBuffer())
        return nullptr;
    auto it = g_BufferSRVs.find(pBuffer);
    if (it != g_BufferSRVs.end())
        return it->second;

    RefCntAutoPtr<IBufferView> pView;
    BufferViewDesc viewDesc;
    viewDesc.ViewType = BUFFER_VIEW_SHADER_RESOURCE;
    pBuffer->GetDiligentBuffer()->CreateView(viewDesc, &pView);
    g_BufferSRVs[pBuffer] = pView;
    return pView;
}

IBufferView* GetBufferUAV(const Buffer* pBuffer)
{
    if (!pBuffer || !pBuffer->GetDiligentBuffer())
        return nullptr;
    auto it = g_BufferUAVs.find(pBuffer);
    if (it != g_BufferUAVs.end())
        return it->second;

    RefCntAutoPtr<IBufferView> pView;
    BufferViewDesc viewDesc;
    viewDesc.ViewType = BUFFER_VIEW_UNORDERED_ACCESS;
    pBuffer->GetDiligentBuffer()->CreateView(viewDesc, &pView);
    g_BufferUAVs[pBuffer] = pView;
    return pView;
}

const CBufferLayout* FindCBufferLayout(const std::vector<CBufferLayout>& layouts, const char* name)
{
    for (const auto& cb : layouts)
    {
        if (cb.Name == name)
            return &cb;
    }
    return nullptr;
}

void PackShaderVarNode(const ShaderVar::Node* pNode, std::vector<uint8_t>& blob, const CBufferLayout& layout)
{
    if (!pNode)
        return;

    if (blob.size() < layout.Size)
        blob.resize(layout.Size, 0);

    for (size_t m = 0; m < layout.MemberNames.size(); ++m)
    {
        const auto it = pNode->Children.find(FalcorMemberName(layout.MemberNames[m]));
        if (it == pNode->Children.end())
            continue;
        const auto& member = it->second;
        if (!member || member->ScalarData.empty())
            continue;
        const Uint32 offset = layout.MemberOffsets[m];
        const Uint32 size   = std::min(static_cast<Uint32>(member->ScalarData.size()), layout.MemberSizes[m]);
        if (offset + size <= blob.size())
            std::memcpy(blob.data() + offset, member->ScalarData.data(), size);
    }
}

Buffer::SharedPtr GetOrCreateConstantBuffer(const CBufferLayout& layout, std::vector<uint8_t>& blob)
{
    if (blob.size() < layout.Size)
        blob.resize(layout.Size, 0);

    const std::string cacheKey = layout.Name + ":" + std::to_string(layout.Size);
    auto              it       = g_ConstantBufferCache.find(cacheKey);
    if (it == g_ConstantBufferCache.end())
    {
        auto cb = Buffer::create(layout.Size, Buffer::BindFlags::Constant, Buffer::CpuAccess::Write, blob.data());
        g_ConstantBufferCache.emplace(cacheKey, cb);
        return cb;
    }

    it->second->setBlob(blob.data(), 0, layout.Size);
    return it->second;
}

IDeviceObject* GetTextureBinding(const Texture* pTex, SHADER_RESOURCE_TYPE resType)
{
    if (!pTex)
        return nullptr;
    if (resType == SHADER_RESOURCE_TYPE_TEXTURE_UAV)
    {
        if (auto* pView = pTex->getUAV())
            return pView;
        // A texture without BIND_UNORDERED_ACCESS bound to a RWTexture slot is
        // always a bug: Diligent rejects the SRV fallback and the shader write
        // silently disappears (this hid the flat-terrain bug where
        // compute_tileBicubic could not write the tile elevation target). Warn
        // once per texture so it can't hide again.
        static std::unordered_set<const Texture*> s_WarnedNoUav;
        if (s_WarnedNoUav.insert(pTex).second)
            spdlog::error("Texture {}x{} bound to a UAV slot but created without UnorderedAccess bind flag - GPU writes to it are lost",
                          pTex->getWidth(), pTex->getHeight());
        return pTex->getSRV(0, 1, 0, 1);
    }
    if (auto* pView = pTex->getSRV(0, 1, 0, 1))
        return pView;
    return pTex->getUAV();
}

void BindShaderResource(IPipelineState* pPSO, IShaderResourceBinding* pSRB, const ShaderResourceInfo& res,
                        IDeviceObject* pObject, PIPELINE_TYPE pipelineType)
{
    if (!pObject || (!pPSO && !pSRB))
        return;

    auto tryBindStatic = [pPSO, &res, pObject](SHADER_TYPE stage) -> bool {
        if (!pPSO || stage == SHADER_TYPE_UNKNOWN)
            return false;
        if (IShaderResourceVariable* pVar = pPSO->GetStaticVariableByName(stage, res.Name.c_str()))
        {
            pVar->Set(pObject);
            return true;
        }
        return false;
    };

    auto tryBindDynamic = [pSRB, &res, pObject](SHADER_TYPE stage) -> bool {
        if (!pSRB || stage == SHADER_TYPE_UNKNOWN)
            return false;
        if (IShaderResourceVariable* pVar = pSRB->GetVariableByName(stage, res.Name.c_str()))
        {
            pVar->Set(pObject);
            return true;
        }
        return false;
    };

    auto tryBindStage = [&](SHADER_TYPE stage) -> bool {
        return tryBindStatic(stage) || tryBindDynamic(stage);
    };

    auto tryStages = [&](std::initializer_list<SHADER_TYPE> stages) -> bool {
        for (SHADER_TYPE stage : stages)
        {
            if (res.Stages != SHADER_TYPE_UNKNOWN && !(res.Stages & stage))
                continue;
            if (tryBindStage(stage))
                return true;
        }
        return false;
    };

    if (pipelineType == PIPELINE_TYPE_COMPUTE)
    {
        tryStages({SHADER_TYPE_COMPUTE});
        return;
    }

    if (!tryStages({SHADER_TYPE_VERTEX, SHADER_TYPE_PIXEL, SHADER_TYPE_GEOMETRY}))
        tryStages({SHADER_TYPE_VERTEX, SHADER_TYPE_PIXEL});
}

void BindGraphicsVars(IPipelineState* pPSO, IShaderResourceBinding* pSRB, GraphicsVars* pVars, const GfxProgramGpu& program)
{
    if (!pPSO || !pVars)
        return;

    const auto* pData = Internal::GfxData(*pVars);
    const auto  root  = Internal::GfxRoot(*pVars).m_pNode;

    for (const auto& res : program.Resources)
    {
        if (res.Type == SHADER_RESOURCE_TYPE_CONSTANT_BUFFER)
            continue;

        IDeviceObject* pObj = nullptr;
        if (pData)
        {
            if (res.Type == SHADER_RESOURCE_TYPE_TEXTURE_SRV || res.Type == SHADER_RESOURCE_TYPE_TEXTURE_UAV)
            {
                auto it = pData->Textures.find(res.Name);
                if (it != pData->Textures.end() && it->second)
                    pObj = GetTextureBinding(it->second.get(), res.Type);
            }
            else if (res.Type == SHADER_RESOURCE_TYPE_SAMPLER)
            {
                auto it = pData->Samplers.find(res.Name);
                if (it != pData->Samplers.end() && it->second)
                    pObj = it->second->GetDiligentSampler();
            }
            else if (res.Type == SHADER_RESOURCE_TYPE_BUFFER_SRV)
            {
                auto it = pData->Buffers.find(res.Name);
                if (it != pData->Buffers.end() && it->second)
                    pObj = GetBufferSRV(it->second.get());
            }
            else if (res.Type == SHADER_RESOURCE_TYPE_BUFFER_UAV)
            {
                auto it = pData->Buffers.find(res.Name);
                if (it != pData->Buffers.end() && it->second)
                    pObj = GetBufferUAV(it->second.get());
            }
        }

        if (!pObj && root)
        {
            const auto childIt = root->Children.find(res.Name);
            if (childIt != root->Children.end() && childIt->second)
            {
                if (childIt->second->TextureValue)
                    pObj = GetTextureBinding(childIt->second->TextureValue.get(), res.Type);
                else if (childIt->second->BufferValue)
                    pObj = res.Type == SHADER_RESOURCE_TYPE_BUFFER_UAV
                        ? static_cast<IDeviceObject*>(GetBufferUAV(childIt->second->BufferValue.get()))
                        : static_cast<IDeviceObject*>(GetBufferSRV(childIt->second->BufferValue.get()));
            }
        }

        if (pObj)
            BindShaderResource(pPSO, pSRB, res, pObj, PIPELINE_TYPE_GRAPHICS);
        else if (res.Type == SHADER_RESOURCE_TYPE_TEXTURE_SRV || res.Type == SHADER_RESOURCE_TYPE_TEXTURE_UAV)
        {
            if (IDeviceObject* pDummy = GetDummyTextureObject(res.Type, res.Name))
                BindShaderResource(pPSO, pSRB, res, pDummy, PIPELINE_TYPE_GRAPHICS);
        }
        else if (res.Type == SHADER_RESOURCE_TYPE_SAMPLER && g_DummySampler)
        {
            BindShaderResource(pPSO, pSRB, res, g_DummySampler->GetDiligentSampler(), PIPELINE_TYPE_GRAPHICS);
        }
    }

    for (const auto& cbLayout : program.CBuffers)
    {
        std::vector<uint8_t> blob(cbLayout.Size, 0);
        if (root)
        {
            const auto cbIt = root->Children.find(cbLayout.Name);
            if (cbIt != root->Children.end())
                PackShaderVarNode(cbIt->second.get(), blob, cbLayout);
        }
        auto cb = GetOrCreateConstantBuffer(cbLayout, blob);
        ShaderResourceInfo cbRes;
        cbRes.Name   = cbLayout.Name;
        cbRes.Type   = SHADER_RESOURCE_TYPE_CONSTANT_BUFFER;
        cbRes.Stages = cbLayout.Stages;
        BindShaderResource(pPSO, pSRB, cbRes, cb->GetDiligentBuffer(), PIPELINE_TYPE_GRAPHICS);
    }
}

void BindComputeVars(IPipelineState* pPSO, IShaderResourceBinding* pSRB, ComputeVars* pVars, const ComputeProgramGpu& program)
{
    if (!pPSO || !pVars)
        return;

    const auto* pData = Internal::CmpData(*pVars);
    const auto  root  = Internal::CmpRoot(*pVars).m_pNode;

    for (const auto& res : program.Resources)
    {
        if (res.Type == SHADER_RESOURCE_TYPE_CONSTANT_BUFFER)
            continue;

        IDeviceObject* pObj = nullptr;
        if (pData)
        {
            if (res.Type == SHADER_RESOURCE_TYPE_TEXTURE_SRV || res.Type == SHADER_RESOURCE_TYPE_TEXTURE_UAV)
            {
                auto it = pData->Textures.find(res.Name);
                if (it != pData->Textures.end() && it->second)
                    pObj = GetTextureBinding(it->second.get(), res.Type);
            }
            else if (res.Type == SHADER_RESOURCE_TYPE_SAMPLER)
            {
                auto it = pData->Samplers.find(res.Name);
                if (it != pData->Samplers.end() && it->second)
                    pObj = it->second->GetDiligentSampler();
            }
            else if (res.Type == SHADER_RESOURCE_TYPE_BUFFER_SRV)
            {
                auto it = pData->Buffers.find(res.Name);
                if (it != pData->Buffers.end() && it->second)
                    pObj = GetBufferSRV(it->second.get());
            }
            else if (res.Type == SHADER_RESOURCE_TYPE_BUFFER_UAV)
            {
                auto it = pData->Buffers.find(res.Name);
                if (it != pData->Buffers.end() && it->second)
                    pObj = GetBufferUAV(it->second.get());
            }
        }

        if (!pObj && root)
        {
            const auto childIt = root->Children.find(res.Name);
            if (childIt != root->Children.end() && childIt->second)
            {
                if (childIt->second->TextureValue)
                {
                    pObj = GetTextureBinding(childIt->second->TextureValue.get(), res.Type);
                }
                else if (childIt->second->BufferValue)
                    pObj = GetBufferUAV(childIt->second->BufferValue.get());
            }
        }

        if (pObj)
            BindShaderResource(pPSO, pSRB, res, pObj, PIPELINE_TYPE_COMPUTE);
        else if (res.Type == SHADER_RESOURCE_TYPE_TEXTURE_SRV || res.Type == SHADER_RESOURCE_TYPE_TEXTURE_UAV)
        {
            if (IDeviceObject* pDummy = GetDummyTextureObject(res.Type, res.Name))
                BindShaderResource(pPSO, pSRB, res, pDummy, PIPELINE_TYPE_COMPUTE);
        }
        else if (res.Type == SHADER_RESOURCE_TYPE_SAMPLER && g_DummySampler)
        {
            BindShaderResource(pPSO, pSRB, res, g_DummySampler->GetDiligentSampler(), PIPELINE_TYPE_COMPUTE);
        }
    }

    for (const auto& cbLayout : program.CBuffers)
    {
        std::vector<uint8_t> blob(cbLayout.Size, 0);
        // Seed with a raw blob upload if one exists for this cbuffer
        // (getParameterBlock("name")->setBlob(...) - e.g. the frustumFlags
        // array for compute_tileBuildLookup, the split children for
        // compute_tileSplitMerge, the ecotope constants). NOTE: the previous
        // implementation only honoured raw blobs when dispatch() was called on
        // the block-vars object itself - which Earthworks never does - so these
        // uploads were silently dropped and the shaders ran on zeros.
        if (pData)
        {
            const auto blobIt = pData->Blobs.find(cbLayout.Name);
            if (blobIt != pData->Blobs.end() && !blobIt->second.empty())
            {
                const size_t n = std::min<size_t>(blobIt->second.size(), blob.size());
                std::memcpy(blob.data(), blobIt->second.data(), n);
            }
        }
        // Per-member ShaderVar assignments overlay the raw blob.
        if (root)
        {
            const auto cbIt = root->Children.find(cbLayout.Name);
            if (cbIt != root->Children.end())
                PackShaderVarNode(cbIt->second.get(), blob, cbLayout);
        }
        auto               cb = GetOrCreateConstantBuffer(cbLayout, blob);
        ShaderResourceInfo cbRes;
        cbRes.Name   = cbLayout.Name;
        cbRes.Type   = SHADER_RESOURCE_TYPE_CONSTANT_BUFFER;
        cbRes.Stages = cbLayout.Stages != SHADER_TYPE_UNKNOWN ? cbLayout.Stages : SHADER_TYPE_COMPUTE;
        BindShaderResource(pPSO, pSRB, cbRes, cb->GetDiligentBuffer(), PIPELINE_TYPE_COMPUTE);
    }
}

PRIMITIVE_TOPOLOGY MapTopology(Vao::Topology topology)
{
    switch (topology)
    {
    case Vao::Topology::PointList: return PRIMITIVE_TOPOLOGY_POINT_LIST;
    case Vao::Topology::LineList: return PRIMITIVE_TOPOLOGY_LINE_LIST;
    case Vao::Topology::LineStrip: return PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case Vao::Topology::TriangleStrip: return PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    default: return PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

VALUE_TYPE MapIndexType(ResourceFormat format)
{
    return format == ResourceFormat::R32Uint ? VT_UINT32 : VT_UINT16;
}

GfxPipelineCacheKey MakeGfxPipelineKey(const GraphicsProgram* pProgram, TEXTURE_FORMAT rtvFmt, TEXTURE_FORMAT dsvFmt,
                                       const GraphicsState* pState, Vao::Topology topology)
{
    // hash-combine (boost style). The state objects are hashed by IDENTITY
    // (pointer), which is correct here because Earthworks creates its state
    // objects once at load and keeps them alive in shared_ptrs. Hashing only
    // the *presence* of a state (the previous scheme) made e.g. "blend on" and
    // "blend off" states collide onto one stale PSO.
    const auto mix = [](GfxPipelineCacheKey h, GfxPipelineCacheKey v) {
        return h ^ (v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2));
    };

    GfxPipelineCacheKey key = reinterpret_cast<GfxPipelineCacheKey>(pProgram);
    key = mix(key, static_cast<GfxPipelineCacheKey>(rtvFmt));
    key = mix(key, static_cast<GfxPipelineCacheKey>(dsvFmt));
    key = mix(key, static_cast<GfxPipelineCacheKey>(topology));
    if (pState)
    {
        key = mix(key, reinterpret_cast<GfxPipelineCacheKey>(pState->getBlendState().get()));
        key = mix(key, reinterpret_cast<GfxPipelineCacheKey>(pState->getDepthStencilState().get()));
        key = mix(key, reinterpret_cast<GfxPipelineCacheKey>(pState->getRasterizerState().get()));
    }
    return key;
}

// By default Diligent allocates a DYNAMIC-offset Vulkan descriptor
// (VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) for every structured/storage buffer, so that a
// USAGE_DYNAMIC buffer could be bound with a per-frame offset. The per-set limit for those
// (maxDescriptorSetStorageBuffersDynamic) is tiny - typically 8 (AMD) to 16 (NVIDIA). Several
// terrain shaders blow straight past it: compute_tileBuildLookup.hlsl binds 60 RWStructuredBuffers
// (18x3 per-view lookup tables + 6 work buffers) and the terrain render shaders read the same 18
// lookup tables as StructuredBuffer SRVs. PipelineLayoutVk::Create then fails with
// "number of dynamic storage buffers (60) exceeds device limit (16)".
//
// The dynamic-ness is NOT controlled by the STATIC/MUTABLE/DYNAMIC variable type - it is
// controlled by SHADER_VARIABLE_FLAG_NO_DYNAMIC_BUFFERS (which maps to
// PIPELINE_RESOURCE_FLAG_NO_DYNAMIC_BUFFERS in the implicit signature). Setting that flag makes
// the buffer use the regular VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, whose per-set limit is in the
// hundreds of thousands. We keep the variable type DYNAMIC (unchanged from the default) so the
// cached-SRB-re-committed-per-draw pattern still gets a fresh per-commit descriptor allocation.
//
// The flag's only contract is that a USAGE_DYNAMIC buffer must never be bound here. That holds:
// all of these structured SRVs/UAVs are persistent GPU-default pools (tile / lookup / feedback /
// draw-arg buffers written by compute); the only USAGE_DYNAMIC resources are constant buffers,
// which are SHADER_RESOURCE_TYPE_CONSTANT_BUFFER and therefore skipped below.
static std::vector<ShaderResourceVariableDesc>
MakeStorageBuffersNonDynamic(const std::vector<ShaderResourceInfo>& resources)
{
    std::vector<ShaderResourceVariableDesc> vars;
    vars.reserve(resources.size());
    for (const auto& r : resources)
    {
        if (r.Type == SHADER_RESOURCE_TYPE_BUFFER_SRV || r.Type == SHADER_RESOURCE_TYPE_BUFFER_UAV)
            vars.push_back({r.Stages, r.Name.c_str(), SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC,
                            SHADER_VARIABLE_FLAG_NO_DYNAMIC_BUFFERS});
    }
    return vars;
}

GraphicsPipelineCacheEntry* GetOrCreateGraphicsPSO(const GraphicsProgram* pProgram, const GfxProgramGpu& gpu,
                                                   const GraphicsState* pState, Vao::Topology topology,
                                                   TEXTURE_FORMAT rtvFmt, TEXTURE_FORMAT dsvFmt)
{
    if (!gpu.VS || !gpu.PS)
        return nullptr;

    const GfxPipelineCacheKey key = MakeGfxPipelineKey(pProgram, rtvFmt, dsvFmt, pState, topology);
    auto                      it  = g_GfxPipelineCache.find(key);
    if (it != g_GfxPipelineCache.end())
        return &it->second;

    GraphicsPipelineStateCreateInfo psoCI;
    psoCI.PSODesc.Name         = gpu.DebugName.empty() ? "EarthworksFX GfxPSO" : gpu.DebugName.c_str();
    psoCI.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
    // DYNAMIC (not MUTABLE) so a single SRB can be re-bound and re-committed for
    // every draw within a frame: dynamic variables get a fresh per-commit
    // descriptor allocation, so draw N+1's bindings can't clobber draw N's still
    // in-flight descriptors. This lets us cache one SRB per PSO instead of
    // creating a brand-new SRB per draw (which, with thousands of terrain-tile
    // draws per frame, exhausted the deferred-release queue -> std::bad_alloc ->
    // device hung on D3D12).
    psoCI.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;
    // ...but flag structured buffers NO_DYNAMIC_BUFFERS so they use non-dynamic Vulkan
    // descriptors, dodging the tiny maxDescriptorSetStorageBuffersDynamic limit
    // (see MakeStorageBuffersNonDynamic).
    const auto nonDynBufferVars = MakeStorageBuffersNonDynamic(gpu.Resources);
    if (!nonDynBufferVars.empty())
    {
        psoCI.PSODesc.ResourceLayout.Variables    = nonDynBufferVars.data();
        psoCI.PSODesc.ResourceLayout.NumVariables = static_cast<Uint32>(nonDynBufferVars.size());
    }

    auto& gp = psoCI.GraphicsPipeline;
    gp.NumRenderTargets  = 1;
    gp.RTVFormats[0]     = rtvFmt;
    gp.DSVFormat         = dsvFmt;
    gp.PrimitiveTopology = MapTopology(topology);

    if (pState && pState->getBlendState())
        gp.BlendDesc = pState->getBlendState()->getDesc().GetDiligentDesc();
    if (pState && pState->getDepthStencilState())
        gp.DepthStencilDesc = pState->getDepthStencilState()->getDesc().GetDiligentDesc();
    if (pState && pState->getRasterizerState())
        gp.RasterizerDesc = pState->getRasterizerState()->getDesc().GetDiligentDesc();

    psoCI.pVS = gpu.VS;
    psoCI.pPS = gpu.PS;
    psoCI.pGS = gpu.GS;

    RefCntAutoPtr<IPipelineState> pPSO;
    g_pDevice->CreateGraphicsPipelineState(psoCI, &pPSO);
    if (!pPSO)
        FatalGpuError("Failed to create graphics PSO: " + gpu.DebugName);

    GraphicsPipelineCacheEntry entry;
    entry.PSO = pPSO;
    pPSO->CreateShaderResourceBinding(&entry.SRB, true);

    auto res = g_GfxPipelineCache.emplace(key, std::move(entry));
    return &res.first->second;
}

ComputePipelineCacheEntry* GetOrCreateComputePSO(const ComputeProgram* pProgram, const ComputeProgramGpu& gpu)
{
    if (!gpu.CS)
        return nullptr;

    auto it = g_ComputePSOCache.find(pProgram);
    if (it != g_ComputePSOCache.end())
        return &it->second;

    ComputePipelineStateCreateInfo psoCI;
    psoCI.PSODesc.Name         = gpu.DebugName.empty() ? "EarthworksFX ComputePSO" : gpu.DebugName.c_str();
    psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;
    // DYNAMIC so the single cached SRB can be safely re-committed per dispatch
    // (see GetOrCreateGraphicsPSO for the rationale).
    psoCI.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;
    // ...but flag structured buffers NO_DYNAMIC_BUFFERS so they use non-dynamic Vulkan
    // descriptors. This is what makes compute_tileBuildLookup.hlsl (60 RWStructuredBuffers)
    // creatable at all - otherwise it exceeds maxDescriptorSetStorageBuffersDynamic (16)
    // (see MakeStorageBuffersNonDynamic).
    const auto nonDynBufferVars = MakeStorageBuffersNonDynamic(gpu.Resources);
    if (!nonDynBufferVars.empty())
    {
        psoCI.PSODesc.ResourceLayout.Variables    = nonDynBufferVars.data();
        psoCI.PSODesc.ResourceLayout.NumVariables = static_cast<Uint32>(nonDynBufferVars.size());
    }
    psoCI.pCS                  = gpu.CS;

    RefCntAutoPtr<IPipelineState> pPSO;
    try
    {
        g_pDevice->CreateComputePipelineState(psoCI, &pPSO);
    }
    catch (const std::exception& ex)
    {
        spdlog::error("EarthworksFX: failed to create compute PSO '{}': {}", gpu.DebugName, ex.what());
    }

    // NOTE: intentionally non-fatal during bring-up. A compute shader that only
    // gets dispatched in certain terrain modes can fail PSO creation when the
    // mode is first entered; making this fatal hard-crashes the app on mode
    // switch and prevents any further diagnosis. Instead we log the offending
    // shader name, cache an empty entry (so we complain exactly once), and let
    // Dispatch()/DispatchIndirect() skip it (they already guard on !PSO). The
    // pass simply does not run - which the metrics panel will reflect.
    if (!pPSO)
        spdlog::error("EarthworksFX: compute PSO '{}' unavailable - its dispatches are skipped this run", gpu.DebugName);

    ComputePipelineCacheEntry entry;
    entry.PSO = pPSO;
    if (pPSO)
        pPSO->CreateShaderResourceBinding(&entry.SRB, true);

    auto res = g_ComputePSOCache.emplace(pProgram, std::move(entry));
    return &res.first->second;
}

void SetRenderTargetsFromFbo(Fbo* pFbo)
{
    if (!g_pContext || !pFbo)
        return;

    ITextureView* pRTV = pFbo->getRenderTargetView(0);
    ITextureView* pDSV = pFbo->getDepthStencilView();
    g_pContext->SetRenderTargets(1, &pRTV, pDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    const float w = pFbo->getWidth();
    const float h = pFbo->getHeight();
    if (w > 0.f && h > 0.f)
    {
        Viewport vp;
        vp.Width    = w;
        vp.Height   = h;
        vp.MaxDepth = 1.f;
        g_pContext->SetViewports(1, &vp, static_cast<Uint32>(w), static_cast<Uint32>(h));
    }
}

} // namespace

GraphicsVarsData* Internal::GfxData(GraphicsVars& vars)
{
    return vars.m_pData.get();
}

ComputeVarsData* Internal::CmpData(ComputeVars& vars)
{
    return vars.m_pData.get();
}

std::shared_ptr<ShaderVar::Node> Internal::CmpBlockRoot(ComputeVars& vars)
{
    return vars.m_pBlockRoot;
}

ShaderVar Internal::GfxRoot(GraphicsVars& vars)
{
    return vars.getRootVar();
}

ShaderVar Internal::CmpRoot(ComputeVars& vars)
{
    return vars.getRootVar();
}

void OnSetFalcorDevice(IRenderDevice* pDevice, IDeviceContext* pContext, ISwapChain* pSwapChain, IEngineFactory* pFactory)
{
    g_pDevice    = pDevice;
    g_pContext   = pContext;
    g_pSwapChain = pSwapChain;
    g_pFactory   = pFactory;
    RebuildShaderSearchPaths();
}

void RebuildShaderSearchPaths()
{
    g_pShaderFactory.Release();
    g_ShaderSearchPaths.clear();
    if (!g_pFactory)
        return;

    std::ostringstream search;
    bool               first = true;
    auto               append = [&](const std::filesystem::path& p) {
        const std::string s = p.string();
        if (s.empty())
            return;
        if (!first)
            search << ';';
        first = false;
        search << s;
    };

    static const char* kHlslSubdirs[] = {"atmosphere", "terrain"};
    for (const auto& dir : Falcor::g_DataDirectories)
    {
        append(dir);
        append(dir / "hlsl");
        for (const char* subdir : kHlslSubdirs)
            append(dir / "hlsl" / subdir);
    }
    append("hlsl");
    append("hlsl/atmosphere");
    append("hlsl/terrain");
    append("EarthworksFX/hlsl");
    append("EarthworksFX/hlsl/atmosphere");
    append("EarthworksFX/hlsl/terrain");

    g_ShaderSearchPaths = search.str();
    g_pFactory->CreateDefaultShaderSourceStreamFactory(g_ShaderSearchPaths.c_str(), &g_pShaderFactory);
}

void Internal::RegisterGraphicsProgram(GraphicsProgram* pProgram, const GraphicsProgram::Desc& desc,
                                       const Program::DefineList& defines)
{
    if (!pProgram)
        return;

    pProgram->m_Path        = RemapShaderPath(desc.getPath());
    pProgram->m_VsEntry     = desc.getVsEntry();
    pProgram->m_PsEntry     = desc.getPsEntry();
    pProgram->m_GsEntry     = desc.getGsEntry();
    pProgram->m_ShaderModel = desc.getShaderModel();
    pProgram->m_Defines     = defines;
    pProgram->m_pReflection = std::make_shared<ProgramReflection>();

    GfxProgramGpu gpu;
    gpu.DebugName = pProgram->m_Path.filename().string();
    gpu.VS = CreateShaderFromFile(pProgram->m_Path, pProgram->m_VsEntry.c_str(), SHADER_TYPE_VERTEX, defines, pProgram->m_ShaderModel);
    gpu.PS = CreateShaderFromFile(pProgram->m_Path, pProgram->m_PsEntry.c_str(), SHADER_TYPE_PIXEL, defines, pProgram->m_ShaderModel);
    if (!pProgram->m_GsEntry.empty())
        gpu.GS = CreateShaderFromFile(pProgram->m_Path, pProgram->m_GsEntry.c_str(), SHADER_TYPE_GEOMETRY, defines, pProgram->m_ShaderModel);

    if (!gpu.VS || !gpu.PS)
        FatalGpuError("Failed to create graphics program: " + pProgram->m_Path.string() +
                      " [vs=" + pProgram->m_VsEntry + ", ps=" + pProgram->m_PsEntry + "]");

    g_GfxPipelineCache.clear();

    CollectShaderResources(gpu.VS, SHADER_TYPE_VERTEX, gpu.Resources);
    CollectShaderResources(gpu.PS, SHADER_TYPE_PIXEL, gpu.Resources);
    if (gpu.GS)
        CollectShaderResources(gpu.GS, SHADER_TYPE_GEOMETRY, gpu.Resources);
    CollectCBuffers(gpu.VS, SHADER_TYPE_VERTEX, gpu.CBuffers);
    CollectCBuffers(gpu.PS, SHADER_TYPE_PIXEL, gpu.CBuffers);
    if (gpu.GS)
        CollectCBuffers(gpu.GS, SHADER_TYPE_GEOMETRY, gpu.CBuffers);

    g_GfxPrograms[pProgram] = std::move(gpu);
}

void Internal::RegisterComputeProgram(ComputeProgram* pProgram, const std::filesystem::path& path, const std::string& csEntry,
                                    const Program::DefineList& defines)
{
    if (!pProgram)
        return;

    pProgram->m_Path       = RemapShaderPath(path);
    pProgram->m_Entry      = csEntry;
    pProgram->m_Defines    = defines;
    pProgram->m_pReflection = std::make_shared<ProgramReflection>();

    ComputeProgramGpu gpu;
    gpu.DebugName = pProgram->m_Path.filename().string();
    gpu.CS = CreateShaderFromFile(pProgram->m_Path, csEntry.c_str(), SHADER_TYPE_COMPUTE, defines, "6_5");
    g_ComputePSOCache.erase(pProgram);
    if (!gpu.CS)
        FatalGpuError("Failed to create compute program: " + pProgram->m_Path.string() + " [" + csEntry + "]");

    CollectShaderResources(gpu.CS, SHADER_TYPE_COMPUTE, gpu.Resources);
    CollectCBuffers(gpu.CS, SHADER_TYPE_COMPUTE, gpu.CBuffers);
    g_ComputePrograms[pProgram] = std::move(gpu);
}

GraphicsProgram::SharedPtr CompileGraphicsProgram(const GraphicsProgram::Desc& desc, const Program::DefineList& defines)
{
    auto program = std::make_shared<GraphicsProgram>();
    Internal::RegisterGraphicsProgram(program.get(), desc, defines);
    return program;
}

ComputeProgram::SharedPtr CompileComputeProgram(const std::filesystem::path& path, const std::string& csEntry,
                                               const Program::DefineList& defines)
{
    auto program = std::make_shared<ComputeProgram>();
    Internal::RegisterComputeProgram(program.get(), path, csEntry, defines);
    return program;
}

void RegisterVao(const Vao* pVao, Vao::Topology topology, const Vao::BufferVec& vbos,
                 const Buffer::SharedPtr& ib, ResourceFormat indexFormat)
{
    VaoGpu data;
    data.Topology    = topology;
    data.VBs         = vbos;
    data.IB          = ib;
    data.IndexFormat = indexFormat;
    g_Vaos[pVao]     = std::move(data);
}

void DrawInstanced(IDeviceContext* pCtx, GraphicsState* pState, GraphicsVars* pVars,
                   uint32_t vertexCount, uint32_t instanceCount, uint32_t startVertex, uint32_t startInstance)
{
    if (!pCtx || !pState || !pVars || vertexCount == 0)
        return;

    const auto* pProgram = pState->getProgram().get();
    auto        progIt   = g_GfxPrograms.find(pProgram);
    if (progIt == g_GfxPrograms.end())
        return;

    const auto& gpu = progIt->second;
    auto        fbo = pState->getFbo();
    if (!fbo)
        return;

    TEXTURE_FORMAT rtvFmt = TEX_FORMAT_UNKNOWN;
    if (fbo->isSwapChainProxy() && fbo->getSwapChain())
        rtvFmt = fbo->getSwapChain()->GetDesc().ColorBufferFormat;
    else if (auto color = fbo->getColorTexture(0))
        rtvFmt = color->getFormat();
    TEXTURE_FORMAT dsvFmt = TEX_FORMAT_UNKNOWN;
    if (fbo->isSwapChainProxy() && fbo->getSwapChain())
        dsvFmt = fbo->getSwapChain()->GetDesc().DepthBufferFormat;
    else if (auto depth = fbo->getDepthStencilTexture())
        dsvFmt = depth->getFormat();

    Vao::Topology topology = Vao::Topology::TriangleList;
    if (pState->getVao())
    {
        auto vaoIt = g_Vaos.find(pState->getVao().get());
        if (vaoIt != g_Vaos.end())
            topology = vaoIt->second.Topology;
    }

    GraphicsPipelineCacheEntry* pEntry = GetOrCreateGraphicsPSO(pProgram, gpu, pState, topology, rtvFmt, dsvFmt);
    if (!pEntry || !pEntry->PSO)
        return;
    IPipelineState*         pPSO = pEntry->PSO;
    IShaderResourceBinding* pSRB = pEntry->SRB;

    SetRenderTargetsFromFbo(fbo.get());

    BindGraphicsVars(pPSO, pSRB, pVars, gpu);

    pCtx->SetPipelineState(pPSO);
    pCtx->CommitShaderResources(pSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    if (pState->getVao())
    {
        auto vaoIt = g_Vaos.find(pState->getVao().get());
        if (vaoIt != g_Vaos.end() && vaoIt->second.IB)
        {
            const auto& vao = vaoIt->second;
            IBuffer*    pVBs[8] = {};
            Uint32      numVBs  = 0;
            for (const auto& vb : vao.VBs)
            {
                if (vb && numVBs < 8)
                    pVBs[numVBs++] = vb->GetDiligentBuffer();
            }
            if (numVBs > 0)
                pCtx->SetVertexBuffers(0, numVBs, pVBs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
            pCtx->SetIndexBuffer(vao.IB->GetDiligentBuffer(), 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

            DrawIndexedAttribs drawAttrs;
            drawAttrs.NumIndices            = vertexCount;
            drawAttrs.NumInstances          = instanceCount;
            drawAttrs.FirstIndexLocation    = startVertex;
            drawAttrs.FirstInstanceLocation = startInstance;
            drawAttrs.IndexType             = MapIndexType(vao.IndexFormat);
            pCtx->DrawIndexed(drawAttrs);
            return;
        }
    }

    DrawAttribs drawAttrs;
    drawAttrs.NumVertices           = vertexCount;
    drawAttrs.NumInstances          = instanceCount;
    drawAttrs.StartVertexLocation   = startVertex;
    drawAttrs.FirstInstanceLocation = startInstance;
    pCtx->Draw(drawAttrs);
}

void DrawIndexedInstanced(IDeviceContext* pCtx, GraphicsState* pState, GraphicsVars* pVars,
                          uint32_t indexCount, uint32_t instanceCount, uint32_t startIndex,
                          int32_t baseVertex, uint32_t startInstance)
{
    if (!pCtx || !pState || !pVars || indexCount == 0)
        return;

    const auto* pProgram = pState->getProgram().get();
    auto        progIt   = g_GfxPrograms.find(pProgram);
    if (progIt == g_GfxPrograms.end())
        return;

    const auto& gpu = progIt->second;
    auto        fbo = pState->getFbo();
    if (!fbo)
        return;

    TEXTURE_FORMAT rtvFmt = TEX_FORMAT_UNKNOWN;
    if (fbo->isSwapChainProxy() && fbo->getSwapChain())
        rtvFmt = fbo->getSwapChain()->GetDesc().ColorBufferFormat;
    else if (auto color = fbo->getColorTexture(0))
        rtvFmt = color->getFormat();
    TEXTURE_FORMAT dsvFmt = TEX_FORMAT_UNKNOWN;
    if (fbo->isSwapChainProxy() && fbo->getSwapChain())
        dsvFmt = fbo->getSwapChain()->GetDesc().DepthBufferFormat;
    else if (auto depth = fbo->getDepthStencilTexture())
        dsvFmt = depth->getFormat();

    Vao::Topology topology = Vao::Topology::TriangleList;
    if (pState->getVao())
    {
        auto vaoIt = g_Vaos.find(pState->getVao().get());
        if (vaoIt != g_Vaos.end())
            topology = vaoIt->second.Topology;
    }

    GraphicsPipelineCacheEntry* pEntry = GetOrCreateGraphicsPSO(pProgram, gpu, pState, topology, rtvFmt, dsvFmt);
    if (!pEntry || !pEntry->PSO)
        return;
    IPipelineState*         pPSO = pEntry->PSO;
    IShaderResourceBinding* pSRB = pEntry->SRB;

    SetRenderTargetsFromFbo(fbo.get());

    BindGraphicsVars(pPSO, pSRB, pVars, gpu);

    pCtx->SetPipelineState(pPSO);
    pCtx->CommitShaderResources(pSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    if (!pState->getVao())
        return;
    auto vaoIt = g_Vaos.find(pState->getVao().get());
    if (vaoIt == g_Vaos.end() || !vaoIt->second.IB)
        return;

    const auto& vao = vaoIt->second;
    IBuffer*    pVBs[8] = {};
    Uint32      numVBs  = 0;
    for (const auto& vb : vao.VBs)
    {
        if (vb && numVBs < 8)
            pVBs[numVBs++] = vb->GetDiligentBuffer();
    }
    if (numVBs > 0)
        pCtx->SetVertexBuffers(0, numVBs, pVBs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
    pCtx->SetIndexBuffer(vao.IB->GetDiligentBuffer(), 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    DrawIndexedAttribs drawAttrs;
    drawAttrs.NumIndices            = indexCount;
    drawAttrs.NumInstances          = instanceCount;
    drawAttrs.FirstIndexLocation    = startIndex;
    drawAttrs.BaseVertex            = baseVertex;
    drawAttrs.FirstInstanceLocation = startInstance;
    drawAttrs.IndexType             = MapIndexType(vao.IndexFormat);
    pCtx->DrawIndexed(drawAttrs);
}

void DrawIndirect(IDeviceContext* pCtx, GraphicsState* pState, GraphicsVars* pVars, uint32_t numArgs,
                  Buffer* pArgBuffer, uint64_t argBufferOffset, Buffer* pCountBuffer, uint64_t countBufferOffset)
{
    if (!pCtx || !pState || !pVars || !pArgBuffer || numArgs == 0)
        return;

    const auto* pProgram = pState->getProgram().get();
    auto        progIt   = g_GfxPrograms.find(pProgram);
    if (progIt == g_GfxPrograms.end())
        return;

    const auto& gpu = progIt->second;
    auto        fbo = pState->getFbo();
    if (!fbo)
        return;

    TEXTURE_FORMAT rtvFmt = TEX_FORMAT_UNKNOWN;
    if (fbo->isSwapChainProxy() && fbo->getSwapChain())
        rtvFmt = fbo->getSwapChain()->GetDesc().ColorBufferFormat;
    else if (auto color = fbo->getColorTexture(0))
        rtvFmt = color->getFormat();
    TEXTURE_FORMAT dsvFmt = TEX_FORMAT_UNKNOWN;
    if (fbo->isSwapChainProxy() && fbo->getSwapChain())
        dsvFmt = fbo->getSwapChain()->GetDesc().DepthBufferFormat;
    else if (auto depth = fbo->getDepthStencilTexture())
        dsvFmt = depth->getFormat();

    Vao::Topology topology = Vao::Topology::TriangleList;
    if (pState->getVao())
    {
        auto vaoIt = g_Vaos.find(pState->getVao().get());
        if (vaoIt != g_Vaos.end())
            topology = vaoIt->second.Topology;
    }

    GraphicsPipelineCacheEntry* pEntry = GetOrCreateGraphicsPSO(pProgram, gpu, pState, topology, rtvFmt, dsvFmt);
    if (!pEntry || !pEntry->PSO)
        return;
    IPipelineState*         pPSO = pEntry->PSO;
    IShaderResourceBinding* pSRB = pEntry->SRB;

    SetRenderTargetsFromFbo(fbo.get());

    BindGraphicsVars(pPSO, pSRB, pVars, gpu);

    pCtx->SetPipelineState(pPSO);
    pCtx->CommitShaderResources(pSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Earthworks' indirect draws are NON-indexed. The argument buffer holds
    // t_DrawArguments records {vertexCountPerInstance, instanceCount,
    // startVertexLocation, startInstanceLocation} - the 16-byte
    // D3D12_DRAW_ARGUMENTS layout (see groundcover_defines.hlsli and the
    // _startArg*16 stride in pixelShader::renderIndirect) - and the vertex
    // shaders synthesize all geometry from SV_VertexID (render_Tiles.hlsl,
    // the render_tile_sprite.hlsl point->quad GS, vegetation, ...). This must
    // map to DrawIndirect; DrawIndexedIndirect reads a 20-byte indexed layout
    // and requires a bound index buffer, so it misreads the args and draws
    // nothing (terrain/sprites/vegetation silently disappear).
    DrawIndirectAttribs drawAttrs;
    drawAttrs.pAttribsBuffer                   = pArgBuffer->GetDiligentBuffer();
    drawAttrs.DrawArgsOffset                   = argBufferOffset;
    drawAttrs.DrawCount                        = numArgs;
    drawAttrs.DrawArgsStride                   = sizeof(uint32_t) * 4;
    drawAttrs.AttribsBufferStateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    if (pCountBuffer)
    {
        drawAttrs.pCounterBuffer                   = pCountBuffer->GetDiligentBuffer();
        drawAttrs.CounterOffset                    = countBufferOffset;
        drawAttrs.CounterBufferStateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    }
    pCtx->DrawIndirect(drawAttrs);
}

void Dispatch(IDeviceContext* pCtx, ComputeState* pState, ComputeVars* pVars, const uint3& groups)
{
    if (!pCtx || !pState || !pVars)
        return;
    if (groups.x == 0 || groups.y == 0 || groups.z == 0)
        return;

    const auto* pProgram = pState->getProgram().get();
    auto        progIt   = g_ComputePrograms.find(pProgram);
    if (progIt == g_ComputePrograms.end())
        return;

    ComputePipelineCacheEntry* pEntry = GetOrCreateComputePSO(pProgram, progIt->second);
    if (!pEntry || !pEntry->PSO)
        return;
    IPipelineState*         pPSO = pEntry->PSO;
    IShaderResourceBinding* pSRB = pEntry->SRB;

    BindComputeVars(pPSO, pSRB, pVars, progIt->second);

    pCtx->SetPipelineState(pPSO);
    pCtx->CommitShaderResources(pSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    DispatchComputeAttribs dispatchAttrs;
    dispatchAttrs.ThreadGroupCountX = groups.x;
    dispatchAttrs.ThreadGroupCountY = groups.y;
    dispatchAttrs.ThreadGroupCountZ = groups.z;
    pCtx->DispatchCompute(dispatchAttrs);
}

void DispatchIndirect(IDeviceContext* pCtx, ComputeState* pState, ComputeVars* pVars,
                      const Buffer* pArgBuffer, uint64_t argBufferOffset)
{
    if (!pCtx || !pState || !pVars || !pArgBuffer)
        return;

    const auto* pProgram = pState->getProgram().get();
    auto        progIt   = g_ComputePrograms.find(pProgram);
    if (progIt == g_ComputePrograms.end())
        return;

    ComputePipelineCacheEntry* pEntry = GetOrCreateComputePSO(pProgram, progIt->second);
    if (!pEntry || !pEntry->PSO)
        return;
    IPipelineState*         pPSO = pEntry->PSO;
    IShaderResourceBinding* pSRB = pEntry->SRB;

    BindComputeVars(pPSO, pSRB, pVars, progIt->second);

    pCtx->SetPipelineState(pPSO);
    pCtx->CommitShaderResources(pSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    DispatchComputeIndirectAttribs dispatchAttrs;
    dispatchAttrs.pAttribsBuffer         = pArgBuffer->GetDiligentBuffer();
    dispatchAttrs.DispatchArgsByteOffset = argBufferOffset;
    pCtx->DispatchComputeIndirect(dispatchAttrs);
}

void ClearFbo(IDeviceContext* pCtx, Fbo* pFbo, const float4& color, float depth, uint8_t stencil, FboAttachmentType attachments)
{
    if (!pCtx || !pFbo)
        return;

    const float clearColor[4] = {color.x, color.y, color.z, color.w};

    if (attachments == FboAttachmentType::Color || attachments == FboAttachmentType::All)
    {
        for (uint32_t slot = 0; slot < 8; ++slot)
        {
            if (ITextureView* pRTV = pFbo->getRenderTargetView(slot))
                pCtx->ClearRenderTarget(pRTV, clearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }
    }

    if (attachments == FboAttachmentType::Depth || attachments == FboAttachmentType::All ||
        (static_cast<uint32_t>(attachments) & static_cast<uint32_t>(FboAttachmentType::Stencil)) != 0)
    {
        if (ITextureView* pDSV = pFbo->getDepthStencilView())
        {
            CLEAR_DEPTH_STENCIL_FLAGS flags = CLEAR_DEPTH_FLAG;
            if ((static_cast<uint32_t>(attachments) & static_cast<uint32_t>(FboAttachmentType::Stencil)) != 0)
                flags = flags | CLEAR_STENCIL_FLAG;
            pCtx->ClearDepthStencil(pDSV, flags, depth, stencil, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }
    }
}

namespace
{
// Scaling blit support. Diligent's CopyTexture is a 1:1 copy and cannot scale,
// so a blit between differently-sized rects (e.g. full-res HDR -> half-res
// previous-frame) must be done with a fullscreen draw that samples the source.
struct BlitConstants
{
    float uvScale[2];
    float uvOffset[2];
};

struct BlitPipeline
{
    RefCntAutoPtr<IPipelineState>         PSO;
    RefCntAutoPtr<IShaderResourceBinding> SRB;
    IShaderResourceVariable*              VarSrc = nullptr;
    IShaderResourceVariable*              VarSmp = nullptr;
};

static const char* g_BlitShaderSource = R"(
cbuffer BlitCB
{
    float2 uvScale;
    float2 uvOffset;
};

Texture2D    g_Src;
SamplerState g_Smp;

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

VSOut vsMain(uint id : SV_VertexID)
{
    VSOut o;
    float2 t = float2((id << 1) & 2, id & 2);
    o.uv  = t * uvScale + uvOffset;
    o.pos = float4(t * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

float4 psMain(VSOut i) : SV_Target
{
    return g_Src.Sample(g_Smp, i.uv);
}
)";

IBuffer* GetBlitConstantBuffer()
{
    static RefCntAutoPtr<IBuffer> s_CB;
    if (!s_CB && g_pDevice)
    {
        BufferDesc cbDesc;
        cbDesc.Name           = "EarthworksFX Blit CB";
        cbDesc.Size           = sizeof(BlitConstants);
        cbDesc.Usage          = USAGE_DYNAMIC;
        cbDesc.BindFlags      = BIND_UNIFORM_BUFFER;
        cbDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
        g_pDevice->CreateBuffer(cbDesc, nullptr, &s_CB);
    }
    return s_CB;
}

ISampler* GetBlitSampler(bool linear)
{
    static RefCntAutoPtr<ISampler> s_Linear;
    static RefCntAutoPtr<ISampler> s_Point;
    RefCntAutoPtr<ISampler>& slot = linear ? s_Linear : s_Point;
    if (!slot && g_pDevice)
    {
        SamplerDesc sd;
        sd.MinFilter = linear ? FILTER_TYPE_LINEAR : FILTER_TYPE_POINT;
        sd.MagFilter = linear ? FILTER_TYPE_LINEAR : FILTER_TYPE_POINT;
        sd.MipFilter = linear ? FILTER_TYPE_LINEAR : FILTER_TYPE_POINT;
        sd.AddressU  = TEXTURE_ADDRESS_CLAMP;
        sd.AddressV  = TEXTURE_ADDRESS_CLAMP;
        sd.AddressW  = TEXTURE_ADDRESS_CLAMP;
        g_pDevice->CreateSampler(sd, &slot);
    }
    return slot;
}

void GetBlitShaders(RefCntAutoPtr<IShader>& vs, RefCntAutoPtr<IShader>& ps)
{
    static RefCntAutoPtr<IShader> s_VS;
    static RefCntAutoPtr<IShader> s_PS;
    if (!s_VS && g_pDevice)
    {
        ShaderCreateInfo sci;
        sci.SourceLanguage                  = SHADER_SOURCE_LANGUAGE_HLSL;
        sci.Desc.UseCombinedTextureSamplers = false;
        sci.Source                          = g_BlitShaderSource;
        sci.ShaderCompiler                  = SHADER_COMPILER_DXC;

        sci.Desc.ShaderType = SHADER_TYPE_VERTEX;
        sci.Desc.Name       = "EarthworksFX Blit VS";
        sci.EntryPoint      = "vsMain";
        g_pDevice->CreateShader(sci, &s_VS);

        sci.Desc.ShaderType = SHADER_TYPE_PIXEL;
        sci.Desc.Name       = "EarthworksFX Blit PS";
        sci.EntryPoint      = "psMain";
        g_pDevice->CreateShader(sci, &s_PS);
    }
    vs = s_VS;
    ps = s_PS;
}

BlitPipeline* GetOrCreateBlitPipeline(TEXTURE_FORMAT dstFmt, bool blend)
{
    static std::unordered_map<Uint64, BlitPipeline> s_Cache;
    const Uint64 key = (static_cast<Uint64>(dstFmt) << 1) | (blend ? 1u : 0u);
    auto it = s_Cache.find(key);
    if (it != s_Cache.end())
        return &it->second;

    RefCntAutoPtr<IShader> vs, ps;
    GetBlitShaders(vs, ps);
    IBuffer* pCB = GetBlitConstantBuffer();
    if (!vs || !ps || !pCB)
        return nullptr;

    GraphicsPipelineStateCreateInfo psoCI;
    psoCI.PSODesc.Name         = "EarthworksFX Blit PSO";
    psoCI.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

    auto& gp                 = psoCI.GraphicsPipeline;
    gp.NumRenderTargets      = 1;
    gp.RTVFormats[0]         = dstFmt;
    gp.DSVFormat             = TEX_FORMAT_UNKNOWN;
    gp.PrimitiveTopology     = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    gp.RasterizerDesc.CullMode      = CULL_MODE_NONE;
    gp.DepthStencilDesc.DepthEnable = False;

    if (blend)
    {
        auto& rt0          = gp.BlendDesc.RenderTargets[0];
        rt0.BlendEnable    = True;
        rt0.SrcBlend       = BLEND_FACTOR_SRC_ALPHA;
        rt0.DestBlend      = BLEND_FACTOR_INV_SRC_ALPHA;
        rt0.BlendOp        = BLEND_OPERATION_ADD;
        rt0.SrcBlendAlpha  = BLEND_FACTOR_ONE;
        rt0.DestBlendAlpha = BLEND_FACTOR_INV_SRC_ALPHA;
        rt0.BlendOpAlpha   = BLEND_OPERATION_ADD;
    }

    ShaderResourceVariableDesc vars[] = {
        {SHADER_TYPE_PIXEL, "g_Src", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {SHADER_TYPE_PIXEL, "g_Smp", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {SHADER_TYPE_VERTEX, "BlitCB", SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
    };
    psoCI.PSODesc.ResourceLayout.Variables    = vars;
    psoCI.PSODesc.ResourceLayout.NumVariables = static_cast<Uint32>(sizeof(vars) / sizeof(vars[0]));

    psoCI.pVS = vs;
    psoCI.pPS = ps;

    BlitPipeline entry;
    g_pDevice->CreateGraphicsPipelineState(psoCI, &entry.PSO);
    if (!entry.PSO)
        FatalGpuError("Failed to create blit PSO");

    if (auto* pVar = entry.PSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "BlitCB"))
        pVar->Set(pCB);

    entry.PSO->CreateShaderResourceBinding(&entry.SRB, true);
    entry.VarSrc = entry.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_Src");
    entry.VarSmp = entry.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_Smp");

    auto res = s_Cache.emplace(key, std::move(entry));
    return &res.first->second;
}
} // namespace

void Blit(IDeviceContext* pCtx, ITextureView* pSrc, ITextureView* pDst, const glm::vec4& srcRect,
          const glm::vec4& dstRect, Sampler::Filter filter, BlendState::SharedPtr blend)
{
    if (!pCtx || !pSrc || !pDst)
        return;

    auto* pSrcTex = pSrc->GetTexture();
    auto* pDstTex = pDst->GetTexture();
    if (!pSrcTex || !pDstTex)
        return;

    const auto& srcDesc = pSrcTex->GetDesc();
    const auto& dstDesc = pDstTex->GetDesc();

    BlitPipeline* pPipe = GetOrCreateBlitPipeline(dstDesc.Format, blend != nullptr);
    if (!pPipe || !pPipe->PSO || !pPipe->SRB)
        return;

    // Source rect -> normalized UV transform (base UV runs 0..1 across the
    // visible viewport; map it onto the requested source sub-rectangle).
    const float srcW = static_cast<float>(srcDesc.Width);
    const float srcH = static_cast<float>(srcDesc.Height);
    BlitConstants consts;
    consts.uvScale[0]  = (srcRect.z - srcRect.x) / srcW;
    consts.uvScale[1]  = (srcRect.w - srcRect.y) / srcH;
    consts.uvOffset[0] = srcRect.x / srcW;
    consts.uvOffset[1] = srcRect.y / srcH;
    if (IBuffer* pCB = GetBlitConstantBuffer())
    {
        void* pMapped = nullptr;
        pCtx->MapBuffer(pCB, MAP_WRITE, MAP_FLAG_DISCARD, pMapped);
        if (pMapped)
        {
            std::memcpy(pMapped, &consts, sizeof(consts));
            pCtx->UnmapBuffer(pCB, MAP_WRITE);
        }
    }

    pCtx->SetRenderTargets(1, &pDst, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    Viewport vp;
    vp.TopLeftX = dstRect.x;
    vp.TopLeftY = dstRect.y;
    vp.Width    = dstRect.z - dstRect.x;
    vp.Height   = dstRect.w - dstRect.y;
    vp.MinDepth = 0.f;
    vp.MaxDepth = 1.f;
    pCtx->SetViewports(1, &vp, dstDesc.Width, dstDesc.Height);

    if (pPipe->VarSrc)
        pPipe->VarSrc->Set(pSrc);
    if (pPipe->VarSmp)
        pPipe->VarSmp->Set(GetBlitSampler(filter != Sampler::Filter::Point));

    pCtx->SetPipelineState(pPipe->PSO);
    pCtx->CommitShaderResources(pPipe->SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    DrawAttribs da;
    da.NumVertices = 3;
    da.Flags       = DRAW_FLAG_VERIFY_ALL;
    pCtx->Draw(da);
}

} // namespace Gpu
} // namespace Falcor
