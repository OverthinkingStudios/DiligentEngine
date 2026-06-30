#pragma once

#include "EarthworksFXApplicationBase.hpp"
#include "Falcor.h"
#include "Earthworks_4.h"

namespace Diligent
{

class EarthworksFXSample final : public EarthworksFXApplicationBase
{
public:
    EarthworksFXSample();
    ~EarthworksFXSample() override;

    void ModifyEngineInitInfo(const ModifyEngineInitInfoAttribs& Attribs) override final;
    EarthworksFXAppSettings GetAppSettings(bool IsInitialization) override final;
    void OnGraphicsInitialized() override final;
    void RenderSample() override final;
    void UpdateSample(double CurrTime, double ElapsedTime, bool DoUpdateUI) override final;
    void OnWindowResized(Uint32 Width, Uint32 Height) override final;

protected:
    void UpdateUI() override final;

private:
    class SampleFramework final : public Falcor::FrameworkInterface
    {
    public:
        void SetAverageFrameTimeMs(double ms) { m_AvgFrameTimeMs = ms; }
        double getAverageFrameTimeMs() const override { return m_AvgFrameTimeMs; }

        Falcor::FrameRate getFrameRate() const override;
        Falcor::WindowInterface* getWindow() override;

    private:
        double m_AvgFrameTimeMs = 16.0;
        Falcor::WindowInterface m_Window;
    };

    void SyncInput();
    void SyncFirstPersonCameraToEarthworks();

    /// App-specific ImGui (debug grid toggle + camera readout). Bring-up aid
    /// kept out of Earthworks_4 so re-porting that file stays clean.
    void DrawDebugUI();

    std::unique_ptr<Falcor::EarthworksWrapper> m_FalcorWrapper;
    Earthworks_4              m_Earthworks;
    Falcor::Gui               m_Gui;
    Falcor::RenderContext     m_RenderContext;
    SampleFramework           m_Framework;
    Falcor::Fbo::SharedPtr    m_TargetFbo;
    bool                      m_Initialized = false;
};

} // namespace Diligent
