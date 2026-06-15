#pragma once

#include "SampleBase.hpp"
#include "FirstPersonCamera.hpp"
#include "BasicMath.hpp"

#include "TerrainSystem.hpp"

namespace Diligent
{

class EarthworksSample final : public SampleBase
{
public:
    EarthworksSample()  = default;
    ~EarthworksSample() = default;

    virtual DesiredApplicationSettings GetDesiredApplicationSettings(bool IsInitialization) override final;
    virtual void ModifyEngineInitInfo(const ModifyEngineInitInfoAttribs& Attribs) override final;
    virtual void Initialize(const SampleInitInfo& InitInfo) override final;
    virtual void Render() override final;
    virtual void Update(double CurrTime, double ElapsedTime, bool DoUpdateUI) override final;
    virtual void WindowResize(Uint32 Width, Uint32 Height) override final;

    virtual const Char* GetSampleName() const override final { return "Earthworks Terrain"; }

private:
    void UpdateProjection();

    earthworksfx::TerrainSystem m_Terrain;
    FirstPersonCamera           m_Camera;

    float  m_WorldSize = 2000.0f;
    float3 m_SunDir    = {0.4f, 0.7f, 0.3f};
};

} // namespace Diligent
