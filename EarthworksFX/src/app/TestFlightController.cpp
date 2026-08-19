#include "TestFlightController.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <unordered_map>

#include <spdlog/spdlog.h>

#include "imgui.h"

#include "DataBlobImpl.hpp"
#include "ScreenCapture.hpp"
#include "JPEGCodec.h"
#include "PNGCodec.h"

#include "ewCamera.h"
#include "EarthworksDebug.h"
#include "FirstPersonCamera.hpp"
#include "InputController.hpp"

#include "overthinkingEnv.h"
#include "StringUtility.h"

#include "json.hpp"

namespace Diligent
{

namespace
{

constexpr float kPi = 3.14159265358979323846f;

// Frames of identical tile state required (on top of settleMs) before a shot
// counts as converged.
constexpr int kStableFramesRequired = 8;
// Frames rendered after the scene reports ready before the first shot starts.
constexpr int kWarmupFrames = 10;
// Give up waiting for a GPU capture readback after this long.
constexpr double kCaptureTimeoutSec = 5.0;

bool WriteBlobToFile(const std::filesystem::path& Path, const IDataBlob* pBlob)
{
    std::ofstream os{Path, std::ios::binary};
    if (!os)
        return false;
    os.write(static_cast<const char*>(pBlob->GetConstDataPtr()), static_cast<std::streamsize>(pBlob->GetSize()));
    return static_cast<bool>(os);
}

nlohmann::json DebugMetricsToJson(const ew::DebugMetrics& m)
{
    return nlohmann::json{
        {"terrainMode", ew::TerrainModeName(m.terrainMode)},
        {"skydomeDraws", m.skydomeDraws},
        {"terrainTileDraws", m.terrainTileDraws},
        {"billboardDraws", m.billboardDraws},
        {"plantDraws", m.plantDraws},
        {"ribbonDraws", m.ribbonDraws},
        {"splineDraws", m.splineDraws},
        {"tonemapperDraws", m.tonemapperDraws},
        {"overlayDraws", m.overlayDraws},
        {"tilesUsed", m.tilesUsed},
        {"tilesFree", m.tilesFree},
        {"gpuTerrainTiles", m.gpuTerrainTiles},
        {"gpuTerrainBlocks", m.gpuTerrainBlocks},
        {"gpuTerrainTris", m.gpuTerrainTris},
        {"gpuQuads", m.gpuQuads},
        {"cameraMainInUse", m.cameraMainInUse},
        {"splitMaxLodPix", m.splitMaxLodPix},
        {"splitCandidates", m.splitCandidates},
        {"splitsPerformed", m.splitsPerformed},
    };
}

/// Converts a mapped staging texture to tightly packed RGB8. Returns false for
/// unsupported formats.
bool MappedToRGB8(const TextureDesc& Desc, const void* pData, size_t Stride, std::vector<uint8_t>& OutRGB)
{
    bool SwapRB = false;
    switch (Desc.Format)
    {
        case TEX_FORMAT_BGRA8_UNORM:
        case TEX_FORMAT_BGRA8_UNORM_SRGB:
        case TEX_FORMAT_BGRA8_TYPELESS:
            SwapRB = true;
            break;
        case TEX_FORMAT_RGBA8_UNORM:
        case TEX_FORMAT_RGBA8_UNORM_SRGB:
        case TEX_FORMAT_RGBA8_TYPELESS:
            SwapRB = false;
            break;
        default:
            return false;
    }

    OutRGB.resize(size_t{Desc.Width} * Desc.Height * 3);
    for (Uint32 y = 0; y < Desc.Height; ++y)
    {
        const uint8_t* Src = static_cast<const uint8_t*>(pData) + size_t{y} * Stride;
        uint8_t*       Dst = OutRGB.data() + size_t{y} * Desc.Width * 3;
        for (Uint32 x = 0; x < Desc.Width; ++x)
        {
            const uint8_t c0 = Src[x * 4 + 0];
            const uint8_t c1 = Src[x * 4 + 1];
            const uint8_t c2 = Src[x * 4 + 2];
            Dst[x * 3 + 0] = SwapRB ? c2 : c0;
            Dst[x * 3 + 1] = c1;
            Dst[x * 3 + 2] = SwapRB ? c0 : c2;
        }
    }
    return true;
}

/// 2x downscale with 2x2 box filter (edges clamp for odd sizes).
void DownscaleRGB8Half(const std::vector<uint8_t>& Src, uint32_t W, uint32_t H,
                       std::vector<uint8_t>& Dst, uint32_t& OutW, uint32_t& OutH)
{
    OutW = std::max(1u, W / 2);
    OutH = std::max(1u, H / 2);
    Dst.resize(size_t{OutW} * OutH * 3);
    for (uint32_t y = 0; y < OutH; ++y)
    {
        const uint32_t y0 = y * 2;
        const uint32_t y1 = std::min(y0 + 1, H - 1);
        for (uint32_t x = 0; x < OutW; ++x)
        {
            const uint32_t x0 = x * 2;
            const uint32_t x1 = std::min(x0 + 1, W - 1);
            for (int c = 0; c < 3; ++c)
            {
                const uint32_t Sum =
                    Src[(size_t{y0} * W + x0) * 3 + c] +
                    Src[(size_t{y0} * W + x1) * 3 + c] +
                    Src[(size_t{y1} * W + x0) * 3 + c] +
                    Src[(size_t{y1} * W + x1) * 3 + c];
                Dst[(size_t{y} * OutW + x) * 3 + c] = static_cast<uint8_t>(Sum / 4);
            }
        }
    }
}

} // namespace

// --- statics -----------------------------------------------------------------

std::filesystem::path TestFlightController::GetFlightsDir()
{
    std::error_code ec;
#ifdef EFX_TESTFLIGHTS_DIR
    const std::filesystem::path SourceDir{EFX_TESTFLIGHTS_DIR};
    if (std::filesystem::exists(SourceDir.parent_path(), ec))
    {
        std::filesystem::create_directories(SourceDir, ec);
        return SourceDir;
    }
#endif
    // Deployed builds without the source tree: flights next to the working dir.
    const std::filesystem::path Dir = std::filesystem::current_path() / "testflights";
    std::filesystem::create_directories(Dir, ec);
    return Dir;
}

std::filesystem::path TestFlightController::GetRunsDir()
{
    const std::filesystem::path Dir =
        (overthinking::Env::getPath(overthinking::Env::SpecialFolder::UserData) / "testflights").lexically_normal();
    std::error_code ec;
    std::filesystem::create_directories(Dir, ec);
    return Dir;
}

std::filesystem::path TestFlightController::ResolveFlightPath(const std::string& NameOrPath)
{
    std::filesystem::path AsPath{NameOrPath};
    if (AsPath.extension() == ".json" || std::filesystem::exists(AsPath))
        return AsPath;
    return GetFlightsDir() / (NameOrPath + ".json");
}

void TestFlightController::ApplyShotToCamera(const ew::TestFlightShot& Shot, FirstPersonCamera& Camera, ew::Camera* pSceneCamera)
{
    Camera.SetPos(float3{Shot.posX, Shot.posY, Shot.posZ});
    Camera.SetRotation(Shot.yaw, Shot.pitch);

    // The camera only rebuilds its matrices in Update(); tick it with a neutral
    // controller so the new pose is effective immediately.
    static InputController NullInput;
    Camera.Update(NullInput, 0.f);

    if (pSceneCamera != nullptr)
    {
        pSceneCamera->setDepthRange(Shot.nearPlane, Shot.farPlane);
        const float FovY = Shot.fovYDeg * kPi / 180.f;
        if (FovY > 0.001f && FovY < kPi)
        {
            // The scene camera encodes FOV as focal length over film-back height.
            const float FrameH = pSceneCamera->getFrameHeight();
            pSceneCamera->setFocalLength(0.5f * FrameH / std::tan(FovY * 0.5f));
        }
    }
}

ew::TestFlightShot TestFlightController::CaptureShotFromCamera(const FirstPersonCamera& Camera, const ew::Camera* pSceneCamera)
{
    ew::TestFlightShot Shot;
    const float3 Pos = Camera.GetPos();
    Shot.posX  = Pos.x;
    Shot.posY  = Pos.y;
    Shot.posZ  = Pos.z;
    Shot.yaw   = Camera.GetYaw();
    Shot.pitch = Camera.GetPitch();

    if (pSceneCamera != nullptr)
    {
        const float Focal = pSceneCamera->getFocalLength();
        if (Focal > 0.001f)
            Shot.fovYDeg = 2.f * std::atan(0.5f * pSceneCamera->getFrameHeight() / Focal) * 180.f / kPi;
        Shot.nearPlane = pSceneCamera->getNearPlane();
        Shot.farPlane  = pSceneCamera->getFarPlane();
    }
    return Shot;
}

bool TestFlightController::ApplyDebugToggle(const std::string& Name, bool Value)
{
    using T = ew::DebugToggles;
    static const std::unordered_map<std::string, bool T::*> kToggles = {
        {"atmosphere", &T::atmosphere},
        {"terrainUpdate", &T::terrainUpdate},
        {"syncCamera", &T::syncCamera},
        {"skydome", &T::skydome},
        {"terrainTiles", &T::terrainTiles},
        {"buildings", &T::buildings},
        {"billboards", &T::billboards},
        {"plants", &T::plants},
        {"ribbons", &T::ribbons},
        {"splines", &T::splines},
        {"terrainConstColor", &T::terrainConstColor},
        {"bypassHdr", &T::bypassHdr},
        {"tonemapper", &T::tonemapper},
        {"overlay", &T::overlay},
        {"debugGlobe", &T::debugGlobe},
        {"debugGroundGrid", &T::debugGroundGrid},
        {"debugEarthworksShader", &T::debugEarthworksShader},
        {"debugEarthworksInfoGui", &T::debugEarthworksInfoGui},
        {"earthworksGui", &T::earthworksGui},
    };

    const auto it = kToggles.find(Name);
    if (it == kToggles.end())
        return false;
    ew::gDebug.toggles.*(it->second) = Value;
    return true;
}

// --- lifecycle -----------------------------------------------------------------

TestFlightController::TestFlightController(const Options& Opts) :
    m_Opts{Opts}
{
}

TestFlightController::~TestFlightController() = default;

bool TestFlightController::LoadFlight()
{
    m_FlightPath = ResolveFlightPath(m_Opts.FlightArg);

    std::string Error;
    if (!ew::LoadTestFlight(m_FlightPath.string(), m_Flight, Error))
    {
        spdlog::error("[testflight] {}", Error);
        return false;
    }

    if (m_Flight.name.empty())
        m_Flight.name = m_FlightPath.stem().string();

    if (m_Flight.shots.empty())
    {
        spdlog::error("[testflight] '{}' contains no cameras", m_FlightPath.string());
        return false;
    }

    if (m_Opts.OnlyCamera >= 0)
    {
        if (m_Opts.OnlyCamera >= static_cast<int>(m_Flight.shots.size()))
        {
            spdlog::error("[testflight] camera index {} out of range (flight has {} cameras)",
                          m_Opts.OnlyCamera, m_Flight.shots.size());
            return false;
        }
        m_ShotOrder = {m_Opts.OnlyCamera};
    }
    else
    {
        for (int i = 0; i < static_cast<int>(m_Flight.shots.size()); ++i)
            m_ShotOrder.push_back(i);
    }

    spdlog::info("[testflight] loaded '{}' ({} cameras, window {}x{}, settle {} ms, timeout {} ms{})",
                 m_FlightPath.string(), m_Flight.shots.size(),
                 m_Flight.windowWidth, m_Flight.windowHeight,
                 m_Opts.SettleMsOverride >= 0 ? m_Opts.SettleMsOverride : m_Flight.settleMs,
                 m_Flight.settleTimeoutMs,
                 m_Opts.OnlyCamera >= 0 ? ", single camera " + std::to_string(m_Opts.OnlyCamera) : "");
    return true;
}

void TestFlightController::OnGraphicsReady(IRenderDevice* pDevice, ISwapChain* pSwapChain, IDeviceContext* pContext, const std::string& DeviceString)
{
    m_pDevice       = pDevice;
    m_pSwapChain    = pSwapChain;
    m_pContext      = pContext;
    m_DeviceString  = DeviceString;
    m_ScreenCapture = std::make_unique<ScreenCapture>(pDevice);
}

// --- per-frame -----------------------------------------------------------------

void TestFlightController::Update(double CurrTime, double ElapsedTime, FirstPersonCamera& Camera, ew::Camera* pSceneCamera, bool SceneReady)
{
    m_LastCurrTime = CurrTime;
    if (m_RunStartSec < 0.0)
        m_RunStartSec = CurrTime;

    switch (m_State)
    {
        case State::WaitReady:
        {
            if (!m_pDevice || !SceneReady)
                break;
            if (m_ReadySec < 0.0)
            {
                m_ReadySec        = CurrTime;
                m_LaunchToReadySec = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_LaunchTime).count();
            }
            if (++m_WarmupFrames < kWarmupFrames)
                break;

            m_RunTimestamp = ots::formatUtcTimestamp();
            m_RunDir       = GetRunsDir() / m_Flight.name / m_RunTimestamp;
            std::error_code ec;
            std::filesystem::create_directories(m_RunDir, ec);
            if (ec)
            {
                spdlog::error("[testflight] cannot create run folder '{}': {}", m_RunDir.string(), ec.message());
                m_NumFailures += static_cast<int>(m_ShotOrder.size());
                m_State = State::Finalize;
                break;
            }
            spdlog::info("[testflight] run folder: {}", m_RunDir.string());

            m_State = State::ApplyShot;
            BeginShot(CurrTime, Camera, pSceneCamera);
            break;
        }

        case State::ApplyShot:
            // BeginShot transitions straight to Settling; nothing to do here.
            break;

        case State::Settling:
        {
            AccumulateFrameStats(ElapsedTime);

            const auto& dm     = ew::gDebug.shown;
            const bool  Stable = dm.splitsPerformed == 0 && dm.tilesUsed == m_LastTilesUsed;
            m_StableFrames     = Stable ? m_StableFrames + 1 : 0;
            m_LastTilesUsed    = dm.tilesUsed;

            const double Elapsed   = CurrTime - m_ShotStartSec;
            const double SettleMin = (m_Opts.SettleMsOverride >= 0 ? m_Opts.SettleMsOverride : m_Flight.settleMs) / 1000.0;
            const double Timeout   = std::max(static_cast<double>(m_Flight.settleTimeoutMs) / 1000.0, SettleMin);

            const char* Reason = nullptr;
            if (Elapsed >= SettleMin && m_StableFrames >= kStableFramesRequired)
                Reason = "stable";
            else if (Elapsed >= Timeout)
            {
                Reason = "timeout";
                ++m_NumTimeouts;
            }

            if (Reason != nullptr)
            {
                ShotRun& Run     = m_Runs.back();
                Run.settleSec    = Elapsed;
                Run.settleReason = Reason;

                m_CaptureRequested = true;
                m_CaptureIssuedSec = CurrTime;
                m_State            = State::WaitCapture;
            }
            break;
        }

        case State::WaitCapture:
        {
            AccumulateFrameStats(ElapsedTime);

            ShotRun& Run = m_Runs.back();
            if (Run.captured)
            {
                Run.captureSec     = CurrTime - m_RunStartSec;
                Run.debugAtCapture = ew::gDebug.shown;
                spdlog::info("[testflight] cam {} '{}': captured ({}x{}, settle {:.2f}s [{}], avg {:.1f} fps)",
                             Run.index, Run.name, Run.width, Run.height,
                             Run.settleSec, Run.settleReason,
                             Run.sumMs > 0.0 ? 1000.0 * Run.frames / Run.sumMs : 0.0);
                AdvanceShot(CurrTime, Camera, pSceneCamera);
            }
            else if (!m_CaptureRequested && !m_CaptureInFlight)
            {
                // Capture was consumed but processing failed.
                ++m_NumFailures;
                spdlog::error("[testflight] cam {} '{}': capture processing failed", Run.index, Run.name);
                AdvanceShot(CurrTime, Camera, pSceneCamera);
            }
            else if (CurrTime - m_CaptureIssuedSec > kCaptureTimeoutSec)
            {
                ++m_NumFailures;
                m_CaptureRequested = false;
                m_CaptureInFlight  = false;
                spdlog::error("[testflight] cam {} '{}': capture timed out", Run.index, Run.name);
                AdvanceShot(CurrTime, Camera, pSceneCamera);
            }
            break;
        }

        case State::Finalize:
            Finalize(CurrTime);
            m_State = State::Done;
            break;

        case State::Done:
            break;
    }
}

void TestFlightController::BeginShot(double CurrTime, FirstPersonCamera& Camera, ew::Camera* pSceneCamera)
{
    const int                 ShotIdx = m_ShotOrder[m_CurrentShot];
    const ew::TestFlightShot& Def     = m_Flight.shots[static_cast<size_t>(ShotIdx)];

    // Reproducible toggle baseline: defaults, then flight-level, then per-shot
    // overrides. The Earthworks editor GUI stays off (it draws into the frame).
    ew::gDebug.toggles               = ew::DebugToggles{};
    ew::gDebug.toggles.earthworksGui = false;
    for (const auto& [Name, Value] : m_Flight.toggles)
        if (!ApplyDebugToggle(Name, Value))
            spdlog::warn("[testflight] unknown flight toggle '{}'", Name);
    for (const auto& [Name, Value] : Def.toggles)
        if (!ApplyDebugToggle(Name, Value))
            spdlog::warn("[testflight] unknown shot toggle '{}'", Name);

    ApplyShotToCamera(Def, Camera, pSceneCamera);

    ShotRun Run;
    Run.index    = ShotIdx;
    Run.name     = !Def.name.empty() ? Def.name : "cam_" + std::to_string(ShotIdx);
    Run.startSec = CurrTime - m_RunStartSec;
    m_Runs.push_back(std::move(Run));

    m_ShotStartSec  = CurrTime;
    m_StableFrames  = 0;
    m_LastTilesUsed = 0xFFFFFFFFu;
    m_State         = State::Settling;

    spdlog::info("[testflight] cam {} '{}' ({}/{}): settling...",
                 ShotIdx, m_Runs.back().name, m_CurrentShot + 1, m_ShotOrder.size());
}

void TestFlightController::AccumulateFrameStats(double ElapsedTime)
{
    if (m_Runs.empty() || ElapsedTime <= 0.0)
        return;
    ShotRun&     Run = m_Runs.back();
    const double Ms  = ElapsedTime * 1000.0;
    ++Run.frames;
    Run.sumMs += Ms;
    Run.minMs = std::min(Run.minMs, Ms);
    Run.maxMs = std::max(Run.maxMs, Ms);
}

void TestFlightController::AdvanceShot(double CurrTime, FirstPersonCamera& Camera, ew::Camera* pSceneCamera)
{
    ++m_CurrentShot;
    if (m_CurrentShot >= m_ShotOrder.size())
    {
        m_State = State::Finalize;
        return;
    }
    m_State = State::ApplyShot;
    BeginShot(CurrTime, Camera, pSceneCamera);
}

void TestFlightController::PrePresent()
{
    if (!m_CaptureRequested || !m_ScreenCapture || !m_pSwapChain)
        return;
    m_ScreenCapture->Capture(m_pSwapChain, m_pContext, static_cast<Uint32>(m_Runs.back().index));
    m_CaptureRequested = false;
    m_CaptureInFlight  = true;
}

void TestFlightController::PostPresent()
{
    if (!m_CaptureInFlight || !m_ScreenCapture)
        return;
    if (!m_ScreenCapture->HasCapture())
        return;

    ScreenCapture::CaptureInfo Capture = m_ScreenCapture->GetCapture();
    if (!Capture)
        return;

    m_CaptureInFlight = false;

    ShotRun& Run = m_Runs.back();

    MappedTextureSubresource TexData;
    m_pContext->MapTextureSubresource(Capture.pTexture, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT, nullptr, TexData);
    if (TexData.pData != nullptr)
    {
        const TextureDesc& Desc = Capture.pTexture->GetDesc();
        if (MappedToRGB8(Desc, TexData.pData, static_cast<size_t>(TexData.Stride), Run.rgb))
        {
            Run.width    = Desc.Width;
            Run.height   = Desc.Height;
            Run.captured = true;
            if (m_Opts.Lossless)
                WriteShotPng(Run);
        }
        else
        {
            spdlog::error("[testflight] unsupported swap chain format {} for capture", static_cast<int>(Desc.Format));
        }
        m_pContext->UnmapTextureSubresource(Capture.pTexture, 0, 0);
    }
    else
    {
        spdlog::error("[testflight] failed to map capture staging texture");
    }

    m_ScreenCapture->RecycleStagingTexture(std::move(Capture.pTexture));
}

// --- outputs -----------------------------------------------------------------

void TestFlightController::WriteShotPng(const ShotRun& Shot) const
{
    char Name[32];
    std::snprintf(Name, sizeof(Name), "%02d.png", Shot.index);
    const std::filesystem::path Path = m_RunDir / Name;

    RefCntAutoPtr<DataBlobImpl> pBlob = DataBlobImpl::Create();
    // 2 == PNG_COLOR_TYPE_RGB (libpng constant, stable ABI; png.h is not on our include path)
    if (EncodePng(Shot.rgb.data(), Shot.width, Shot.height, Shot.width * 3, 2, pBlob) != ENCODE_PNG_RESULT_OK ||
        !WriteBlobToFile(Path, pBlob))
    {
        spdlog::error("[testflight] failed to write '{}'", Path.string());
    }
}

void TestFlightController::WriteGridJpg() const
{
    std::vector<const ShotRun*> Captured;
    for (const ShotRun& Run : m_Runs)
        if (Run.captured)
            Captured.push_back(&Run);
    if (Captured.empty())
    {
        spdlog::warn("[testflight] no captures - grid.jpg not written");
        return;
    }

    uint32_t TileW = 0, TileH = 0;
    std::vector<std::vector<uint8_t>> Tiles(Captured.size());
    for (size_t i = 0; i < Captured.size(); ++i)
    {
        uint32_t W = 0, H = 0;
        DownscaleRGB8Half(Captured[i]->rgb, Captured[i]->width, Captured[i]->height, Tiles[i], W, H);
        // All shots share the enforced window size; take the tile size from the first.
        if (i == 0)
        {
            TileW = W;
            TileH = H;
        }
    }

    const uint32_t Cols = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(Captured.size()))));
    const uint32_t Rows = (static_cast<uint32_t>(Captured.size()) + Cols - 1) / Cols;
    const uint32_t GridW = Cols * TileW;
    const uint32_t GridH = Rows * TileH;

    std::vector<uint8_t> Canvas(size_t{GridW} * GridH * 3, 24);
    for (size_t i = 0; i < Tiles.size(); ++i)
    {
        const uint32_t Col = static_cast<uint32_t>(i) % Cols;
        const uint32_t Row = static_cast<uint32_t>(i) / Cols;
        for (uint32_t y = 0; y < TileH; ++y)
        {
            const size_t DstOff = ((size_t{Row} * TileH + y) * GridW + size_t{Col} * TileW) * 3;
            const size_t SrcOff = size_t{y} * TileW * 3;
            std::copy_n(Tiles[i].data() + SrcOff, size_t{TileW} * 3, Canvas.data() + DstOff);
        }
    }

    const std::filesystem::path Path  = m_RunDir / "grid.jpg";
    RefCntAutoPtr<DataBlobImpl> pBlob = DataBlobImpl::Create();
    if (EncodeJpeg(Canvas.data(), GridW, GridH, m_Opts.JpgQuality, pBlob) != ENCODE_JPEG_RESULT_OK ||
        !WriteBlobToFile(Path, pBlob))
    {
        spdlog::error("[testflight] failed to write '{}'", Path.string());
    }
}

void TestFlightController::WriteMetricsJson(double TotalRunSec) const
{
    nlohmann::json j;
    j["testflight"]   = m_Flight.name;
    j["terrain"]      = m_Flight.terrain;
    j["runTimestamp"] = m_RunTimestamp;
    j["device"]       = m_DeviceString;
    j["window"]       = {m_Flight.windowWidth, m_Flight.windowHeight};
    j["vsync"]        = false;
    j["lossless"]     = m_Opts.Lossless;
    j["settleMs"]     = m_Opts.SettleMsOverride >= 0 ? m_Opts.SettleMsOverride : m_Flight.settleMs;
    j["totalRunSec"]          = TotalRunSec;
    j["launchToFirstShotSec"] = m_LaunchToReadySec;
    j["numTimeouts"] = m_NumTimeouts;
    j["numFailures"] = m_NumFailures;
    j["exitCode"]    = m_NumTimeouts + m_NumFailures;

    if (!m_Runs.empty())
    {
        const uint32_t N    = static_cast<uint32_t>(std::count_if(m_Runs.begin(), m_Runs.end(), [](const ShotRun& r) { return r.captured; }));
        const uint32_t Cols = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(std::max(N, 1u)))));
        j["grid"] = {
            {"cols", Cols},
            {"rows", (std::max(N, 1u) + Cols - 1) / Cols},
            {"order", "row-major, captured cameras by flight order"},
        };
    }

    nlohmann::json Scenes = nlohmann::json::array();
    for (const ShotRun& Run : m_Runs)
    {
        nlohmann::json s;
        s["index"]        = Run.index;
        s["name"]         = Run.name;
        s["captured"]     = Run.captured;
        s["startSec"]     = Run.startSec;
        s["settleSec"]    = Run.settleSec;
        s["settleReason"] = Run.settleReason;
        s["captureSec"]   = Run.captureSec;
        s["frames"]       = Run.frames;
        s["avgFps"]       = Run.sumMs > 0.0 ? 1000.0 * Run.frames / Run.sumMs : 0.0;
        s["minFrameMs"]   = Run.frames > 0 ? Run.minMs : 0.0;
        s["maxFrameMs"]   = Run.maxMs;
        s["resolution"]   = {Run.width, Run.height};
        s["debug"]        = DebugMetricsToJson(Run.debugAtCapture);
        Scenes.push_back(std::move(s));
    }
    j["scenes"] = std::move(Scenes);

    const std::filesystem::path Path = m_RunDir / "metrics.json";
    std::ofstream os{Path};
    if (os)
        os << j.dump(4) << "\n";
    if (!os)
        spdlog::error("[testflight] failed to write '{}'", Path.string());
}

void TestFlightController::CopyFlightJsonAndLog() const
{
    std::error_code ec;
    std::filesystem::copy_file(m_FlightPath, m_RunDir / m_FlightPath.filename(),
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec)
        spdlog::warn("[testflight] could not copy flight json: {}", ec.message());

    // The application log lives at <UserData>/Logs/log.txt (see overthinkingEnv).
    if (auto* Logger = spdlog::default_logger_raw())
        Logger->flush();
    const std::filesystem::path LogSrc =
        overthinking::Env::getPath(overthinking::Env::SpecialFolder::UserData) / "Logs" / "log.txt";
    std::filesystem::copy_file(LogSrc, m_RunDir / "run.log",
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec)
        spdlog::warn("[testflight] could not copy log file '{}': {}", LogSrc.string(), ec.message());
}

void TestFlightController::Finalize(double CurrTime)
{
    const double TotalRunSec = CurrTime - m_RunStartSec;

    if (!m_RunDir.empty())
    {
        WriteGridJpg();
        WriteMetricsJson(TotalRunSec);
        CopyFlightJsonAndLog();
    }

    m_ExitCode = m_NumTimeouts + m_NumFailures;

    // The summary an agent (or human) grabs from the console / log.
    spdlog::info("[testflight] ============================================================");
    spdlog::info("[testflight] run complete: {} ({} cameras, {} timeouts, {} failures, exit code {})",
                 m_Flight.name, m_ShotOrder.size(), m_NumTimeouts, m_NumFailures, m_ExitCode);
    spdlog::info("[testflight] total {:.1f}s, launch to first shot {:.1f}s", TotalRunSec, m_LaunchToReadySec);
    spdlog::info("[testflight] output folder: {}", m_RunDir.string());
    spdlog::info("[testflight]   grid.jpg     - contact sheet of all cameras");
    spdlog::info("[testflight]   metrics.json - per-camera timing + renderer metrics");
    if (m_Opts.Lossless)
        spdlog::info("[testflight]   NN.png       - lossless per-camera captures");
    spdlog::info("[testflight]   {} - flight definition used", m_FlightPath.filename().string());
    spdlog::info("[testflight]   run.log      - application log copy");
    spdlog::info("[testflight] ============================================================");
}

// --- UI ------------------------------------------------------------------------

void TestFlightController::DrawBadgeUI() const
{
    constexpr ImGuiWindowFlags Flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;

    ImGui::SetNextWindowPos(ImVec2(8.f, 8.f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.6f);
    if (ImGui::Begin("##testflight_badge", nullptr, Flags))
    {
        ImGui::Text("TESTFLIGHT %s  %s", m_Flight.name.c_str(),
                    !m_RunTimestamp.empty() ? m_RunTimestamp.c_str() : "(warmup)");
        if (!m_Runs.empty() && (m_State == State::Settling || m_State == State::WaitCapture))
        {
            const ShotRun& Run = m_Runs.back();
            ImGui::Text("cam %d  %s  (%zu/%zu)%s",
                        Run.index, Run.name.c_str(), m_CurrentShot + 1, m_ShotOrder.size(),
                        m_State == State::WaitCapture ? "  [capture]" : "");
        }
        else if (m_State == State::Done || m_State == State::Finalize)
        {
            ImGui::TextUnformatted("finishing...");
        }
    }
    ImGui::End();
}

} // namespace Diligent
