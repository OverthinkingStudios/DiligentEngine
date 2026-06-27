#pragma once

#include "GraphicsTypes.h"
#include "InputController.hpp"
#include "RefCntAutoPtr.hpp"
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "PipelineState.h"
#include "Buffer.h"
#include "ShaderResourceBinding.h"

#include <memory>

namespace Diligent
{

/// Minimal Diligent renderer: one PSO, one draw, blue background + world grid overlay.
class ShaderGridRenderer
{
public:
    ShaderGridRenderer();
    ~ShaderGridRenderer();

    void Initialize(IRenderDevice* pDevice, TEXTURE_FORMAT RTVFormat, TEXTURE_FORMAT DSVFormat);
    void UpdateCamera(InputController& Input, double ElapsedSeconds, Uint32 Width, Uint32 Height);
    void Render(IDeviceContext* pCtx, ITextureView* pRTV, ITextureView* pDSV, Uint32 Width, Uint32 Height);

private:
    struct CameraState;

    RefCntAutoPtr<IPipelineState>         m_pPSO;
    RefCntAutoPtr<IShaderResourceBinding> m_pSRB;
    RefCntAutoPtr<IBuffer>                m_pConstantsCB;

    std::unique_ptr<CameraState> m_pCamera;
};

} // namespace Diligent
