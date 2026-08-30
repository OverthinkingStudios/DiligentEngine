#include "ewGpuContext.h"
#include "ewResources.h"

#include <cstring>
#include <sstream>

#include "GraphicsTypes.h"
#include "GraphicsAccessories.hpp" // GetTextureFormatAttribs (stencil-aspect check in clearFbo)
#include "DebugUtilities.hpp"

#include "ots/Log.hpp"

namespace ew
{

namespace
{
GpuContext* g_pInstance = nullptr;
} // namespace

GpuContext::GpuContext(Diligent::IRenderDevice*  pDevice,
                       Diligent::IDeviceContext* pContext,
                       Diligent::ISwapChain*     pSwapChain,
                       Diligent::IEngineFactory* pFactory) :
    m_pDevice{pDevice},
    m_pContext{pContext},
    m_pSwapChain{pSwapChain},
    m_pFactory{pFactory}
{
    VERIFY(g_pInstance == nullptr, "ew::GpuContext must be a singleton");
    g_pInstance = this;
}

GpuContext::~GpuContext()
{
    if (g_pInstance == this)
        g_pInstance = nullptr;
}

GpuContext* GpuContext::get()
{
    VERIFY(g_pInstance != nullptr, "ew::GpuContext used before the host created it");
    return g_pInstance;
}

// --- data / shader search paths ------------------------------------------------

void GpuContext::addDataDirectory(const std::filesystem::path& path, bool prepend)
{
    if (prepend)
        m_DataDirectories.insert(m_DataDirectories.begin(), path);
    else
        m_DataDirectories.push_back(path);
    rebuildShaderSearchPaths();
}

std::filesystem::path GpuContext::resolvePath(const std::filesystem::path& path) const
{
    if (path.is_absolute() && std::filesystem::exists(path))
        return path;
    for (const auto& dir : m_DataDirectories)
    {
        const auto candidate = dir / path;
        if (std::filesystem::exists(candidate))
            return candidate;
    }
    return path;
}

void GpuContext::rebuildShaderSearchPaths()
{
    std::ostringstream search;
    bool               first  = true;
    auto               append = [&](const std::filesystem::path& p) {
        const std::string s = p.string();
        if (s.empty())
            return;
        if (!first)
            search << ';';
        first = false;
        search << s;
    };

    // Every data directory plus its hlsl/ layout: shader #includes resolve
    // across hlsl/, hlsl/terrain and hlsl/atmosphere.
    static const char* kHlslSubdirs[] = {"atmosphere", "terrain"};
    for (const auto& dir : m_DataDirectories)
    {
        append(dir);
        append(dir / "hlsl");
        for (const char* subdir : kHlslSubdirs)
            append(dir / "hlsl" / subdir);
    }

    m_ShaderSearchPaths = search.str();
}

// --- clear ----------------------------------------------------------------------

void GpuContext::clearFbo(Fbo* pFbo, const float4& color, float depth, uint8_t stencil,
                          FboAttachmentType attachments)
{
    if (!m_pContext || !pFbo)
        return;

    const float clearColor[4] = {color.x, color.y, color.z, color.w};

    if (hasAttachment(attachments, FboAttachmentType::Color))
    {
        for (uint32_t slot = 0; slot < Fbo::kMaxColorTargets; ++slot)
        {
            if (Diligent::ITextureView* pRTV = pFbo->getRenderTargetView(slot))
                m_pContext->ClearRenderTarget(pRTV, clearColor,
                                              Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }
    }

    const bool clearDepth   = hasAttachment(attachments, FboAttachmentType::Depth);
    const bool clearStencil = hasAttachment(attachments, FboAttachmentType::Stencil);
    if (clearDepth || clearStencil)
    {
        if (Diligent::ITextureView* pDSV = pFbo->getDepthStencilView())
        {
            Diligent::CLEAR_DEPTH_STENCIL_FLAGS flags = Diligent::CLEAR_DEPTH_FLAG_NONE;
            if (clearDepth)
                flags = flags | Diligent::CLEAR_DEPTH_FLAG;
            // Only request a stencil clear if the format has a stencil plane:
            // clearing the stencil aspect of a depth-only format (D32) is
            // invalid Vulkan (VUID-vkCmdClearDepthStencilImage-image-02825),
            // D3D12 silently ignores it.
            const Diligent::TextureFormatAttribs& fmtAttribs =
                Diligent::GetTextureFormatAttribs(pDSV->GetDesc().Format);
            if (clearStencil && fmtAttribs.ComponentType == Diligent::COMPONENT_TYPE_DEPTH_STENCIL)
                flags = flags | Diligent::CLEAR_STENCIL_FLAG;
            m_pContext->ClearDepthStencil(pDSV, flags, depth, stencil,
                                          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }
    }
}

void GpuContext::clearRtv(Diligent::ITextureView* pRtv, const float4& color)
{
    if (!m_pContext || !pRtv)
        return;
    const float clearColor[4] = {color.x, color.y, color.z, color.w};
    m_pContext->ClearRenderTarget(pRtv, clearColor, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void GpuContext::unbindRenderTargets()
{
    if (!m_pContext)
        return;
    m_pContext->SetRenderTargets(0, nullptr, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void GpuContext::copyResource(Texture* pDst, Texture* pSrc)
{
    if (!m_pContext || !pDst || !pSrc || !pDst->handle() || !pSrc->handle())
        return;
    Diligent::CopyTextureAttribs attribs(pSrc->handle(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                         pDst->handle(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_pContext->CopyTexture(attribs);
}

void GpuContext::copySubresource(Texture* pDst, uint32_t dstSlice, Texture* pSrc, uint32_t srcSlice)
{
    if (!m_pContext || !pDst || !pSrc || !pDst->handle() || !pSrc->handle())
        return;
    // The source (e.g. a tileFbo colour plane) may still be bound as render
    // target; unbind explicitly so the copy's state transition doesn't spam
    // Diligent's implicit-unbind Info message.
    unbindRenderTargets();
    Diligent::CopyTextureAttribs attribs(pSrc->handle(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                         pDst->handle(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    attribs.SrcSlice = srcSlice;
    attribs.DstSlice = dstSlice;
    m_pContext->CopyTexture(attribs);
}

float GpuContext::debugReadTexelR32F(Texture* pSrc, uint32_t x, uint32_t y)
{
    if (!m_pContext || !m_pDevice || !pSrc || !pSrc->handle())
        return 0.f;
    if (pSrc->getFormat() != Diligent::TEX_FORMAT_R32_FLOAT)
    {
        spdlog::error("ew debugReadTexelR32F: '{}' is not R32_FLOAT", pSrc->handle()->GetDesc().Name);
        return 0.f;
    }

    // 1x1 staging texture, created fresh per call - this is a debug-panel
    // path that runs a handful of times, never per frame.
    Diligent::TextureDesc desc;
    desc.Name           = "ew debug readback 1x1";
    desc.Type           = Diligent::RESOURCE_DIM_TEX_2D;
    desc.Width          = 1;
    desc.Height         = 1;
    desc.Format         = Diligent::TEX_FORMAT_R32_FLOAT;
    desc.Usage          = Diligent::USAGE_STAGING;
    desc.CPUAccessFlags = Diligent::CPU_ACCESS_READ;
    Diligent::RefCntAutoPtr<Diligent::ITexture> pStaging;
    m_pDevice->CreateTexture(desc, nullptr, &pStaging);
    if (!pStaging)
        return 0.f;

    unbindRenderTargets();  // the source is typically a live render target

    Diligent::CopyTextureAttribs attribs(pSrc->handle(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                         pStaging, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Diligent::Box srcBox;
    srcBox.MinX = x; srcBox.MaxX = x + 1;
    srcBox.MinY = y; srcBox.MaxY = y + 1;
    attribs.pSrcBox = &srcBox;
    m_pContext->CopyTexture(attribs);

    // FULL STALL - acceptable for the explicit debug probe only.
    m_pContext->Flush();
    m_pContext->WaitForIdle();

    Diligent::MappedTextureSubresource mapped;
    m_pContext->MapTextureSubresource(pStaging, 0, 0, Diligent::MAP_READ,
                                      Diligent::MAP_FLAG_NONE, nullptr, mapped);
    float value = 0.f;
    if (mapped.pData)
    {
        value = *reinterpret_cast<const float*>(mapped.pData);
        m_pContext->UnmapTextureSubresource(pStaging, 0, 0);
    }
    return value;
}

// --- blit -------------------------------------------------------------------------
// Scaling blit via fullscreen draw: Diligent's CopyTexture is 1:1 only, but the
// hdrPreviousFrame feedback copy is full-res -> half-res.

namespace
{

struct BlitConstants
{
    float uvScale[2];
    float uvOffset[2];
};

const char* g_BlitShaderSource = R"(
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

} // namespace

Diligent::IBuffer* GpuContext::getBlitConstantBuffer()
{
    if (!m_BlitCB && m_pDevice)
    {
        Diligent::BufferDesc cbDesc;
        cbDesc.Name           = "ew blit CB";
        cbDesc.Size           = sizeof(BlitConstants);
        cbDesc.Usage          = Diligent::USAGE_DYNAMIC;
        cbDesc.BindFlags      = Diligent::BIND_UNIFORM_BUFFER;
        cbDesc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
        m_pDevice->CreateBuffer(cbDesc, nullptr, &m_BlitCB);
    }
    return m_BlitCB;
}

Diligent::ISampler* GpuContext::getBlitSampler(bool linear)
{
    Diligent::RefCntAutoPtr<Diligent::ISampler>& slot = linear ? m_BlitSamplerLinear : m_BlitSamplerPoint;
    if (!slot && m_pDevice)
    {
        Diligent::SamplerDesc sd;
        sd.MinFilter = linear ? Diligent::FILTER_TYPE_LINEAR : Diligent::FILTER_TYPE_POINT;
        sd.MagFilter = linear ? Diligent::FILTER_TYPE_LINEAR : Diligent::FILTER_TYPE_POINT;
        sd.MipFilter = linear ? Diligent::FILTER_TYPE_LINEAR : Diligent::FILTER_TYPE_POINT;
        sd.AddressU  = Diligent::TEXTURE_ADDRESS_CLAMP;
        sd.AddressV  = Diligent::TEXTURE_ADDRESS_CLAMP;
        sd.AddressW  = Diligent::TEXTURE_ADDRESS_CLAMP;
        m_pDevice->CreateSampler(sd, &slot);
    }
    return slot;
}

void GpuContext::getBlitShaders(Diligent::RefCntAutoPtr<Diligent::IShader>& vs,
                                Diligent::RefCntAutoPtr<Diligent::IShader>& ps)
{
    if (!m_BlitVS && m_pDevice)
    {
        Diligent::ShaderCreateInfo sci;
        sci.SourceLanguage                  = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
        sci.Desc.UseCombinedTextureSamplers = false;
        sci.Source                          = g_BlitShaderSource;
        sci.ShaderCompiler                  = Diligent::SHADER_COMPILER_DXC;

        sci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
        sci.Desc.Name       = "ew blit VS";
        sci.EntryPoint      = "vsMain";
        m_pDevice->CreateShader(sci, &m_BlitVS);

        sci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
        sci.Desc.Name       = "ew blit PS";
        sci.EntryPoint      = "psMain";
        m_pDevice->CreateShader(sci, &m_BlitPS);
    }
    vs = m_BlitVS;
    ps = m_BlitPS;
}

GpuContext::BlitPipeline* GpuContext::getOrCreateBlitPipeline(Diligent::TEXTURE_FORMAT dstFormat, bool blend)
{
    const uint64_t key = (static_cast<uint64_t>(dstFormat) << 1) | (blend ? 1u : 0u);
    for (auto& entry : m_BlitPipelines)
    {
        if (entry.first == key)
            return &entry.second;
    }

    Diligent::RefCntAutoPtr<Diligent::IShader> vs, ps;
    getBlitShaders(vs, ps);
    Diligent::IBuffer* pCB = getBlitConstantBuffer();
    if (!vs || !ps || !pCB)
        return nullptr;

    Diligent::GraphicsPipelineStateCreateInfo psoCI;
    psoCI.PSODesc.Name         = "ew blit PSO";
    psoCI.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;

    auto& gp                        = psoCI.GraphicsPipeline;
    gp.NumRenderTargets             = 1;
    gp.RTVFormats[0]                = dstFormat;
    gp.DSVFormat                    = Diligent::TEX_FORMAT_UNKNOWN;
    gp.PrimitiveTopology            = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    gp.RasterizerDesc.CullMode      = Diligent::CULL_MODE_NONE;
    gp.DepthStencilDesc.DepthEnable = Diligent::False;

    if (blend)
    {
        auto& rt0          = gp.BlendDesc.RenderTargets[0];
        rt0.BlendEnable    = Diligent::True;
        rt0.SrcBlend       = Diligent::BLEND_FACTOR_SRC_ALPHA;
        rt0.DestBlend      = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
        rt0.BlendOp        = Diligent::BLEND_OPERATION_ADD;
        rt0.SrcBlendAlpha  = Diligent::BLEND_FACTOR_ONE;
        rt0.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
        rt0.BlendOpAlpha   = Diligent::BLEND_OPERATION_ADD;
    }

    Diligent::ShaderResourceVariableDesc vars[] = {
        {Diligent::SHADER_TYPE_PIXEL, "g_Src", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {Diligent::SHADER_TYPE_PIXEL, "g_Smp", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {Diligent::SHADER_TYPE_VERTEX, "BlitCB", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
    };
    psoCI.PSODesc.ResourceLayout.Variables    = vars;
    psoCI.PSODesc.ResourceLayout.NumVariables = static_cast<Diligent::Uint32>(sizeof(vars) / sizeof(vars[0]));

    psoCI.pVS = vs;
    psoCI.pPS = ps;

    BlitPipeline entry;
    m_pDevice->CreateGraphicsPipelineState(psoCI, &entry.PSO);
    if (!entry.PSO)
    {
        spdlog::error("ew::GpuContext: failed to create blit PSO (dst format {})", static_cast<int>(dstFormat));
        return nullptr;
    }

    if (auto* pVar = entry.PSO->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "BlitCB"))
        pVar->Set(pCB);

    entry.PSO->CreateShaderResourceBinding(&entry.SRB, true);
    entry.VarSrc = entry.SRB->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_Src");
    entry.VarSmp = entry.SRB->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_Smp");

    m_BlitPipelines.emplace_back(key, std::move(entry));
    return &m_BlitPipelines.back().second;
}

void GpuContext::blit(Diligent::ITextureView* pSrc, Diligent::ITextureView* pDst,
                      const float4& srcRect, const float4& dstRect,
                      bool linearFilter, bool alphaBlend)
{
    if (!m_pContext || !pSrc || !pDst)
        return;

    Diligent::ITexture* pSrcTex = pSrc->GetTexture();
    Diligent::ITexture* pDstTex = pDst->GetTexture();
    if (!pSrcTex || !pDstTex)
        return;

    const Diligent::TextureDesc& srcDesc = pSrcTex->GetDesc();
    const Diligent::TextureDesc& dstDesc = pDstTex->GetDesc();

    BlitPipeline* pPipe = getOrCreateBlitPipeline(dstDesc.Format, alphaBlend);
    if (!pPipe || !pPipe->PSO || !pPipe->SRB)
        return;

    const float srcW = static_cast<float>(srcDesc.Width);
    const float srcH = static_cast<float>(srcDesc.Height);

    BlitConstants consts;
    consts.uvScale[0]  = (srcRect.z - srcRect.x) / srcW;
    consts.uvScale[1]  = (srcRect.w - srcRect.y) / srcH;
    consts.uvOffset[0] = srcRect.x / srcW;
    consts.uvOffset[1] = srcRect.y / srcH;
    if (Diligent::IBuffer* pCB = getBlitConstantBuffer())
    {
        void* pMapped = nullptr;
        m_pContext->MapBuffer(pCB, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD, pMapped);
        if (pMapped)
        {
            std::memcpy(pMapped, &consts, sizeof(consts));
            m_pContext->UnmapBuffer(pCB, Diligent::MAP_WRITE);
        }
    }

    m_pContext->SetRenderTargets(1, &pDst, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    Diligent::Viewport vp;
    vp.TopLeftX = dstRect.x;
    vp.TopLeftY = dstRect.y;
    vp.Width    = dstRect.z - dstRect.x;
    vp.Height   = dstRect.w - dstRect.y;
    vp.MinDepth = 0.f;
    vp.MaxDepth = 1.f;
    m_pContext->SetViewports(1, &vp, dstDesc.Width, dstDesc.Height);

    if (pPipe->VarSrc)
        pPipe->VarSrc->Set(pSrc);
    if (pPipe->VarSmp)
        pPipe->VarSmp->Set(getBlitSampler(linearFilter));

    m_pContext->SetPipelineState(pPipe->PSO);
    m_pContext->CommitShaderResources(pPipe->SRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    Diligent::DrawAttribs da;
    da.NumVertices = 3;
    da.Flags       = Diligent::DRAW_FLAG_VERIFY_ALL;
    m_pContext->Draw(da);
}

} // namespace ew
