#pragma once

// ---------------------------------------------------------------------------
// PORT-REVIEW (step 6, new instrumentation - no original counterpart):
// ew::GpuFrameTimer - whole-frame GPU duration measurement (step 6 perf pass).
//
// A small ring of QUERY_TYPE_DURATION queries: begin() is called at the top of
// the host's Render(), end() after the last GPU commands of the frame. Results
// arrive a few frames later; begin() polls the pending slots (non-blocking)
// and keeps the newest resolved duration. If the device does not support
// duration queries (Features.DurationQueries) the timer stays inert and
// supported() returns false - the debug panel then shows "n/a".
//
// No Flush/WaitForIdle anywhere - this must never change frame pacing.
// ---------------------------------------------------------------------------

#include <cstdint>

#include "RefCntAutoPtr.hpp"
#include "Query.h"

namespace ew
{

class GpuContext;

class GpuFrameTimer
{
public:
    /// Creates the query ring. Safe to call once the device exists; returns
    /// supported(). Idempotent.
    bool initialize(GpuContext* pCtx);

    /// Polls pending queries (non-blocking), then begins a new frame query if
    /// a slot is free. Call once per frame before the frame's GPU commands.
    void begin(GpuContext* pCtx);

    /// Ends the frame query begun by the matching begin(). Safe to call when
    /// begin() skipped (no-op).
    void end(GpuContext* pCtx);

    /// Releases the queries. Call while the device is still alive.
    void shutdown();

    bool supported() const { return m_Supported; }
    /// True once at least one query has resolved.
    bool hasResult() const { return m_HasResult; }
    /// Newest resolved GPU frame duration in milliseconds (a few frames old).
    double lastMs() const { return m_LastMs; }
    /// Frames where no free query slot existed (results not draining) - a few
    /// at startup are normal, steady growth is not.
    uint32_t dropouts() const { return m_Dropouts; }

private:
    static constexpr uint32_t kSlots = 6;   // > max frames in flight + resolve latency

    enum class SlotState : uint8_t
    {
        Free,     // may Begin
        Active,   // Begin recorded, End not yet
        Pending,  // End recorded, waiting for GetData
    };

    Diligent::RefCntAutoPtr<Diligent::IQuery> m_Query[kSlots];
    SlotState m_State[kSlots] = {};

    bool     m_Initialized = false;
    bool     m_Supported   = false;
    bool     m_HasResult   = false;
    int32_t  m_ActiveSlot  = -1;
    double   m_LastMs      = 0.0;
    uint32_t m_Dropouts    = 0;
};

} // namespace ew
