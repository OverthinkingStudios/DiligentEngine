#pragma once

// ---------------------------------------------------------------------------
// TestFlightController: replays a ew::TestFlight (fixed camera shots) against
// the live application, captures a screenshot per shot, collects performance
// and renderer metrics, and writes a self-contained timestamped run folder
// under <UserData>/testflights/<flight>/<timestamp>/ containing
//   grid.jpg      - all shots composited into one contact sheet (q~90)
//   NN.png        - optional lossless per-shot captures (--tf_lossless)
//   <flight>.json - verbatim copy of the flight definition used
//   metrics.json  - run + per-shot timing and ew::gDebug metrics
//   run.log       - copy of the application log
//
// Owned and driven by EarthworksFXApplicationBase when --testflight is on the
// command line. All logic lives here in the app layer; the ported Earthworks
// core is only read through ew::gDebug and the public Falcor camera API.
// ---------------------------------------------------------------------------

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "TestFlightData.h"
#include "EarthworksDebug.h"

#include "RefCntAutoPtr.hpp"
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "SwapChain.h"

namespace Falcor
{
class Camera;
}

namespace Diligent
{

class FirstPersonCamera;
class ScreenCapture;

class TestFlightController
{
public:
    struct Options
    {
        /// Flight name (resolved against GetTestflightsDir()) or explicit json path.
        std::string FlightArg;
        /// If >= 0, only this camera index is flown.
        int OnlyCamera = -1;
        /// Additionally write one lossless PNG per shot.
        bool Lossless = false;
        /// Overrides the flight's settleMs when >= 0.
        int SettleMsOverride = -1;
        /// JPEG quality of the composited grid.
        int JpgQuality = 90;
    };

    /// Flight definitions: EarthworksFX/testflights in the source tree this
    /// binary was built from (baked in at compile time; the jsons are versioned
    /// with the code). Falls back to <cwd>/testflights when unavailable.
    static std::filesystem::path GetFlightsDir();

    /// Run outputs: <UserData>/testflights (created on demand).
    static std::filesystem::path GetRunsDir();

    /// Resolves a flight name ("steg_bringup") or path ("d:/x/f.json") to a file path.
    static std::filesystem::path ResolveFlightPath(const std::string& NameOrPath);

    /// Applies a shot to the live cameras. Shared by the run mode and the
    /// ImGui testflights editor ("view" button).
    static void ApplyShotToCamera(const ew::TestFlightShot& Shot, FirstPersonCamera& Camera, Falcor::Camera* pSceneCamera);

    /// Snapshots the live cameras into a shot ("add current" in the editor).
    static ew::TestFlightShot CaptureShotFromCamera(const FirstPersonCamera& Camera, const Falcor::Camera* pSceneCamera);

    /// Sets one ew::gDebug toggle by json field name. Returns false for unknown names.
    static bool ApplyDebugToggle(const std::string& Name, bool Value);

    explicit TestFlightController(const Options& Opts);
    ~TestFlightController();

    /// Loads the flight definition; on failure logs the reason and returns false
    /// (the application should then abort startup).
    bool LoadFlight();

    const ew::TestFlight& GetFlight() const { return m_Flight; }

    /// Called once the device/swap chain exist. DeviceString is a human-readable
    /// device description for metrics.json.
    void OnGraphicsReady(IRenderDevice* pDevice, ISwapChain* pSwapChain, IDeviceContext* pContext, const std::string& DeviceString);

    /// Per-frame tick, called from the app base before OnUpdate. Applies cameras,
    /// runs the settle/capture state machine, accumulates frame stats.
    void Update(double CurrTime, double ElapsedTime, FirstPersonCamera& Camera, Falcor::Camera* pSceneCamera, bool SceneReady);

    /// Called right before ISwapChain::Present - issues the GPU capture copy.
    void PrePresent();

    /// Called right after Present - drains finished captures.
    void PostPresent();

    /// Minimal on-screen badge (flight, shot, timestamp) burned into captures.
    void DrawBadgeUI() const;

    bool IsFinished() const { return m_State == State::Done; }
    int  GetExitCode() const { return m_ExitCode; }

private:
    enum class State
    {
        WaitReady,
        ApplyShot,
        Settling,
        WaitCapture,
        Finalize,
        Done,
    };

    struct ShotRun
    {
        int         index = 0;
        std::string name;

        double      startSec   = 0.0;
        double      settleSec  = 0.0;
        double      captureSec = 0.0; // offset from run start
        std::string settleReason;

        uint32_t frames = 0;
        double   sumMs  = 0.0;
        double   minMs  = 1e9;
        double   maxMs  = 0.0;

        ew::DebugMetrics debugAtCapture{};

        bool                 captured = false;
        uint32_t             width    = 0;
        uint32_t             height   = 0;
        std::vector<uint8_t> rgb; // tightly packed RGB8 at capture size
    };

    void BeginShot(double CurrTime, FirstPersonCamera& Camera, Falcor::Camera* pSceneCamera);
    void AccumulateFrameStats(double ElapsedTime);
    bool ProcessCapture(); // returns true when a capture was consumed this frame
    void AdvanceShot(double CurrTime, FirstPersonCamera& Camera, Falcor::Camera* pSceneCamera);
    void Finalize(double CurrTime);

    void WriteShotPng(const ShotRun& Shot) const;
    void WriteGridJpg() const;
    void WriteMetricsJson(double TotalRunSec) const;
    void CopyFlightJsonAndLog() const;

    const ew::TestFlightShot& CurrentShotDef() const { return m_Flight.shots[static_cast<size_t>(m_ShotOrder[m_CurrentShot])]; }

    Options        m_Opts;
    ew::TestFlight m_Flight;
    std::filesystem::path m_FlightPath;

    std::vector<int> m_ShotOrder; // shot indices to fly (all, or just Opts.OnlyCamera)
    size_t           m_CurrentShot = 0;

    State m_State = State::WaitReady;

    RefCntAutoPtr<IRenderDevice>   m_pDevice;
    RefCntAutoPtr<ISwapChain>      m_pSwapChain;
    RefCntAutoPtr<IDeviceContext>  m_pContext;
    std::unique_ptr<ScreenCapture> m_ScreenCapture;
    std::string                    m_DeviceString;

    std::filesystem::path m_RunDir;
    std::string           m_RunTimestamp;

    std::vector<ShotRun> m_Runs;

    double m_RunStartSec       = -1.0; // first Update() CurrTime
    double m_ReadySec          = -1.0; // scene became ready
    // Wall-clock start (construction happens during command-line processing,
    // before any graphics init), so launch->first-shot includes the scene load.
    std::chrono::steady_clock::time_point m_LaunchTime = std::chrono::steady_clock::now();
    double m_LaunchToReadySec = 0.0;
    double m_ShotStartSec      = 0.0;
    double m_LastCurrTime      = 0.0;
    int    m_WarmupFrames      = 0;
    int    m_StableFrames      = 0;
    uint32_t m_LastTilesUsed   = 0xFFFFFFFFu;

    bool   m_CaptureRequested = false;
    bool   m_CaptureInFlight  = false;
    double m_CaptureIssuedSec = 0.0;

    int m_NumTimeouts = 0;
    int m_NumFailures = 0;
    int m_ExitCode    = 0;
};

} // namespace Diligent
