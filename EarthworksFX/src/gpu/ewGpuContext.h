#pragma once

// ---------------------------------------------------------------------------
// ew::GpuContext - the one object holding the Diligent device/context/swap
// chain/factory pointers for the Earthworks renderer (set by the host shell).
//
// Every draw and dispatch call in the renderer takes a GpuContext*, and
// resource creation reaches the device through GpuContext::get(). Exactly one
// instance exists (owned by EarthworksFXApplicationBase); get() asserts on
// that.
// ---------------------------------------------------------------------------

#include <filesystem>
#include <string>
#include <vector>

#include "ewTypes.h"

#include "RefCntAutoPtr.hpp"
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "SwapChain.h"
#include "EngineFactory.h"
#include "Shader.h"
#include "PipelineState.h"
#include "ShaderResourceBinding.h"

namespace ew
{

class Fbo;
class Texture;

class GpuContext
{
public:
    GpuContext(Diligent::IRenderDevice*  pDevice,
               Diligent::IDeviceContext* pContext,
               Diligent::ISwapChain*     pSwapChain,
               Diligent::IEngineFactory* pFactory);
    ~GpuContext();

    GpuContext(const GpuContext&) = delete;
    GpuContext& operator=(const GpuContext&) = delete;

    /// The single live instance (set in the constructor, cleared in the
    /// destructor). Used by resource/shader creation statics.
    static GpuContext* get();

    Diligent::IRenderDevice*  device() const { return m_pDevice; }
    Diligent::IDeviceContext* context() const { return m_pContext; }
    Diligent::ISwapChain*     swapChain() const { return m_pSwapChain; }
    Diligent::IEngineFactory* factory() const { return m_pFactory; }

    // --- data / shader search paths ------------------------------------------

    void addDataDirectory(const std::filesystem::path& path, bool prepend);
    const std::vector<std::filesystem::path>& dataDirectories() const { return m_DataDirectories; }

    /// Resolves a relative asset/shader path against the data directories;
    /// returns the input unchanged when nothing matches (caller logs).
    std::filesystem::path resolvePath(const std::filesystem::path& path) const;

    /// Semicolon-separated include search paths handed to the shader compiler
    /// (every data dir + its hlsl/, hlsl/terrain, hlsl/atmosphere subdirs).
    const std::string& shaderSearchPaths() const { return m_ShaderSearchPaths; }

    // --- frame operations -----------------------------------------------------

    /// Clears the selected attachments of an FBO (color: every bound target).
    void clearFbo(Fbo* pFbo, const float4& color, float depth, uint8_t stencil,
                  FboAttachmentType attachments = FboAttachmentType::All);

    /// Clears a single render-target view - the per-plane tileFbo clears at
    /// tile-bake time.
    void clearRtv(Diligent::ITextureView* pRtv, const float4& color);

    /// Whole-texture copy, same dimensions/format (vertex_clear /
    /// vertex_preload seeding).
    void copyResource(Texture* pDst, Texture* pSrc);

    /// Copies src subresource (srcSlice, mip 0) into dst array slice dstSlice
    /// - the tile-bake publish into the 997-slice texture arrays.
    void copySubresource(Texture* pDst, uint32_t dstSlice, Texture* pSrc, uint32_t srcSlice = 0);

    /// Unbinds all render targets from the immediate context. Call before a
    /// compute pass samples a texture that is still bound as a render target -
    /// otherwise Diligent implicitly unbinds it and emits the (cosmetic but
    /// noisy) "texture bound as render target will be unset" Info message.
    void unbindRenderTargets();

    /// Scaling blit via fullscreen draw (Diligent CopyTexture cannot scale).
    /// Rects are (x0, y0, x1, y1) in pixels. Used by the hdrPreviousFrame
    /// half-res feedback copy and the thumbnail overlay.
    void blit(Diligent::ITextureView* pSrc, Diligent::ITextureView* pDst,
              const float4& srcRect, const float4& dstRect,
              bool linearFilter = true, bool alphaBlend = false);

    /// DEBUG ONLY - reads one texel of an R32_FLOAT texture back to the CPU
    /// through a 1x1 staging texture with a FULL GPU STALL (Flush +
    /// WaitForIdle + map). Returns 0 and logs on failure. Never call it on a
    /// per-frame path.
    float debugReadTexelR32F(Texture* pSrc, uint32_t x, uint32_t y);

private:
    void rebuildShaderSearchPaths();

    Diligent::IRenderDevice*  m_pDevice    = nullptr;
    Diligent::IDeviceContext* m_pContext   = nullptr;
    Diligent::ISwapChain*     m_pSwapChain = nullptr;
    Diligent::IEngineFactory* m_pFactory   = nullptr;

    std::vector<std::filesystem::path> m_DataDirectories;
    std::string                        m_ShaderSearchPaths;

    // --- blit pipeline (lazy, cached per destination format + blend) ---------
    struct BlitPipeline
    {
        Diligent::RefCntAutoPtr<Diligent::IPipelineState>         PSO;
        Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> SRB;
        Diligent::IShaderResourceVariable*                        VarSrc = nullptr;
        Diligent::IShaderResourceVariable*                        VarSmp = nullptr;
    };
    BlitPipeline* getOrCreateBlitPipeline(Diligent::TEXTURE_FORMAT dstFormat, bool blend);
    Diligent::IBuffer*  getBlitConstantBuffer();
    Diligent::ISampler* getBlitSampler(bool linear);
    void getBlitShaders(Diligent::RefCntAutoPtr<Diligent::IShader>& vs,
                        Diligent::RefCntAutoPtr<Diligent::IShader>& ps);

    std::vector<std::pair<uint64_t, BlitPipeline>> m_BlitPipelines;
    Diligent::RefCntAutoPtr<Diligent::IBuffer>     m_BlitCB;
    Diligent::RefCntAutoPtr<Diligent::ISampler>    m_BlitSamplerLinear;
    Diligent::RefCntAutoPtr<Diligent::ISampler>    m_BlitSamplerPoint;
    Diligent::RefCntAutoPtr<Diligent::IShader>     m_BlitVS;
    Diligent::RefCntAutoPtr<Diligent::IShader>     m_BlitPS;
};

} // namespace ew
