#pragma once

#include "SampleBase.hpp"
#include "Falcor.h"
#include "Earthworks_4.h"

namespace Diligent
{

class EarthworksFXSample final : public SampleBase
{
public:
    EarthworksFXSample();
    ~EarthworksFXSample() override;

    void ModifyEngineInitInfo(const ModifyEngineInitInfoAttribs& Attribs) override final;
    DesiredApplicationSettings GetDesiredApplicationSettings(bool IsInitialization) override final;
    void Initialize(const SampleInitInfo& InitInfo) override final;
    void Render() override final;
    void Update(double CurrTime, double ElapsedTime, bool DoUpdateUI) override final;
    void WindowResize(Uint32 Width, Uint32 Height) override final;

    const Char* GetSampleName() const override final { return "EarthworksFX"; }

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

    std::unique_ptr<Falcor::EarthworksWrapper> m_FalcorWrapper;
    Earthworks_4              m_Earthworks;
    Falcor::Gui               m_Gui;
    Falcor::RenderContext     m_RenderContext;
    SampleFramework           m_Framework;
    Falcor::Fbo::SharedPtr    m_TargetFbo;
    bool                      m_Initialized = false;
};

SampleBase* CreateSample();

} // namespace Diligent
