#include "ewGpuTimer.h"

#include "ewGpuContext.h"

#include "GraphicsTypes.h"
#include "RenderDevice.h"
#include "DeviceContext.h"

#include "ots/Log.hpp"

namespace ew
{

using namespace Diligent;

bool GpuFrameTimer::initialize(GpuContext* pCtx)
{
    if (m_Initialized)
        return m_Supported;
    if (!pCtx || !pCtx->device())
        return false;
    m_Initialized = true;

    // The app shell requests every feature OPTIONAL, so where the hardware
    // supports duration queries the created device has them ENABLED.
    if (pCtx->device()->GetDeviceInfo().Features.DurationQueries != DEVICE_FEATURE_STATE_ENABLED)
    {
        spdlog::info("ew::GpuFrameTimer: duration queries not supported on this device - GPU frame time unavailable");
        m_Supported = false;
        return false;
    }

    QueryDesc desc;
    desc.Type = QUERY_TYPE_DURATION;
    for (uint32_t i = 0; i < kSlots; ++i)
    {
        const std::string name = "ew frame duration " + std::to_string(i);
        desc.Name              = name.c_str();
        pCtx->device()->CreateQuery(desc, &m_Query[i]);
        if (!m_Query[i])
        {
            spdlog::warn("ew::GpuFrameTimer: query creation failed - GPU frame time unavailable");
            shutdown();
            m_Initialized = true; // stay inert, do not retry every frame
            m_Supported   = false;
            return false;
        }
        m_State[i] = SlotState::Free;
    }
    m_Supported = true;
    return true;
}

void GpuFrameTimer::begin(GpuContext* pCtx)
{
    if (!m_Supported || !pCtx || m_ActiveSlot >= 0)
        return;

    // Drain resolved queries (oldest first is irrelevant - the ring is tiny
    // and we only keep the newest value; polling all pending slots keeps them
    // returning to Free).
    for (uint32_t i = 0; i < kSlots; ++i)
    {
        if (m_State[i] != SlotState::Pending)
            continue;
        QueryDataDuration data;
        // AutoInvalidate=true: a successful read frees the query for reuse.
        if (m_Query[i]->GetData(&data, sizeof(data), true))
        {
            if (data.Frequency > 0)
            {
                m_LastMs    = static_cast<double>(data.Duration) * 1000.0 / static_cast<double>(data.Frequency);
                m_HasResult = true;
            }
            m_State[i] = SlotState::Free;
        }
    }

    for (uint32_t i = 0; i < kSlots; ++i)
    {
        if (m_State[i] == SlotState::Free)
        {
            pCtx->context()->BeginQuery(m_Query[i]);
            m_State[i]   = SlotState::Active;
            m_ActiveSlot = static_cast<int32_t>(i);
            return;
        }
    }
    // No slot free: skip this frame's measurement rather than stall.
    ++m_Dropouts;
}

void GpuFrameTimer::end(GpuContext* pCtx)
{
    if (!m_Supported || !pCtx || m_ActiveSlot < 0)
        return;
    pCtx->context()->EndQuery(m_Query[m_ActiveSlot]);
    m_State[m_ActiveSlot] = SlotState::Pending;
    m_ActiveSlot          = -1;
}

void GpuFrameTimer::shutdown()
{
    for (uint32_t i = 0; i < kSlots; ++i)
    {
        m_Query[i].Release();
        m_State[i] = SlotState::Free;
    }
    m_ActiveSlot = -1;
    m_Supported  = false;
}

} // namespace ew
