// Phase-1 Earthworks terrain sample. Wiring follows the Atmosphere sample
// (SampleBase lifecycle); the terrain itself is driven through
// earthworksfx::TerrainSystem. See docs/porting/KICKOFF.md section 7.

#include "EarthworksSample.hpp"

#include "MapHelper.hpp"
#include "ColorConversion.h"

namespace Diligent
{

SampleBase* CreateSample()
{
    return new EarthworksSample();
}

DesiredApplicationSettings EarthworksSample::GetDesiredApplicationSettings(bool IsInitialization)
{
    DesiredApplicationSettings Settings;
    if (IsInitialization)
    {
        // Skip the API-selection popup and default to Vulkan. The framework only
        // shows the dialog when the device type is still UNDEFINED after command
        // line parsing, so this is still overridden by passing e.g. "--mode d3d12".
        Settings.SetDeviceType(RENDER_DEVICE_TYPE_VULKAN);
    }
    return Settings;
}

void EarthworksSample::ModifyEngineInitInfo(const ModifyEngineInitInfoAttribs& Attribs)
{
    SampleBase::ModifyEngineInitInfo(Attribs);
    // The tile bake chain is compute-driven; the draw is indirect.
    Attribs.EngineCI.Features.ComputeShaders = DEVICE_FEATURE_STATE_ENABLED;
}

void EarthworksSample::Initialize(const SampleInitInfo& InitInfo)
{
    SampleBase::Initialize(InitInfo);

    const SwapChainDesc& scDesc = m_pSwapChain->GetDesc();

    earthworksfx::TerrainInitInfo ci;
    ci.pDevice     = m_pDevice;
    ci.pContext    = m_pImmediateContext;
    ci.ColorFormat = scDesc.ColorBufferFormat;
    ci.DepthFormat = scDesc.DepthBufferFormat;
    ci.ShadersDir  = "shaders";
    //ci.DataDir     = nullptr; // synthesize a procedural heightmap for phase 1
    ci.DataDir   = "terrains/switserland_Steg";
    ci.WorldSize   = m_WorldSize;
    m_Terrain.Initialize(ci);

    // Frame the camera on the real terrain: look at its center, sitting at 1.5x
    // the center elevation, pulled back along the XZ diagonal so the whole tile
    // is in view. Avoids the previous "hunt for the terrain" with a too-fast,
    // hyper-sensitive camera. (Terrain is centered on the XZ origin.)
    const earthworksfx::TerrainView view = m_Terrain.GetView();
    if (view.WorldSize > 0.f)
        m_WorldSize = view.WorldSize;

    const float3 target = view.Center;            // (0, centerElevation, 0)
    const float  camY   = view.Center.y * 1.5f;   // camera height = 1.5x center elevation
    const float  back   = m_WorldSize * 0.6f;     // pull back to frame the tile
    m_Camera.SetPos(float3{target.x - back, camY, target.z - back});
    m_Camera.SetLookAt(target);                   // orient toward the center
    m_Camera.SetMoveSpeed(m_WorldSize * 0.15f);
    m_Camera.SetSpeedUpScales(5.f, 10.f);
    m_Camera.SetRotationSpeed(0.0025f);           // calmer mouse (default 0.01 felt hyper-sensitive)

    UpdateProjection();
}

void EarthworksSample::UpdateProjection()
{
    const SwapChainDesc& scDesc = m_pSwapChain->GetDesc();
    const float          aspect = static_cast<float>(scDesc.Width) / static_cast<float>(scDesc.Height);
    const bool           isGL   = m_pDevice->GetDeviceInfo().IsGLDevice();
    m_Camera.SetProjAttribs(1.0f, m_WorldSize * 8.0f, aspect, PI_F / 4.0f, scDesc.PreTransform, isGL);
}

void EarthworksSample::WindowResize(Uint32 Width, Uint32 Height)
{
    UpdateProjection();
}

void EarthworksSample::Update(double CurrTime, double ElapsedTime, bool DoUpdateUI)
{
    SampleBase::Update(CurrTime, ElapsedTime, DoUpdateUI);
    m_Camera.Update(m_InputController, static_cast<float>(ElapsedTime));
}

void EarthworksSample::Render()
{
    // Bake the static tile on the first frame (records compute on the immediate
    // context before any render targets are bound).
    const float4x4 view     = m_Camera.GetViewMatrix();
    const float4x4 proj     = m_Camera.GetProjMatrix();
    const float4x4 viewProj = view * proj;
    const float3   camPos   = m_Camera.GetPos();

    const SwapChainDesc& scDesc = m_pSwapChain->GetDesc();
    m_Terrain.Update(m_pImmediateContext, view, proj, camPos, static_cast<float>(scDesc.Height));

    ITextureView* pRTV = m_pSwapChain->GetCurrentBackBufferRTV();
    ITextureView* pDSV = m_pSwapChain->GetDepthBufferDSV();

    float4 clearColor = {0.45f, 0.62f, 0.85f, 1.0f}; // sky
    if (m_ConvertPSOutputToGamma)
        clearColor = LinearToSRGB(clearColor);

    m_pImmediateContext->SetRenderTargets(1, &pRTV, pDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_pImmediateContext->ClearRenderTarget(pRTV, clearColor.Data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_pImmediateContext->ClearDepthStencil(pDSV, CLEAR_DEPTH_FLAG, 1.f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    earthworksfx::TerrainFrameAttribs attribs;
    attribs.pColorRTV = pRTV;
    attribs.pDepthDSV = pDSV;
    attribs.ViewProj  = viewProj;
    attribs.CameraPos = camPos;
    attribs.SunDir    = m_SunDir;
    m_Terrain.Render(m_pImmediateContext, attribs);
}

} // namespace Diligent
