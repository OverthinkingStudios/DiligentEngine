#pragma once

// ---------------------------------------------------------------------------
// TestFlight data model: a named list of camera "shots" that an application
// replays to produce a reproducible series of screenshots + performance
// metrics (see TestFlightController in src/app).
//
// DELIBERATELY engine-agnostic: standard library + nlohmann json only, no
// Diligent or glm types. This header, and the json format it defines, can be
// dropped into another renderer unchanged so the same flights can be shot
// there to produce ground-truth reference images.
// ---------------------------------------------------------------------------

#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "json.hpp"

namespace ew
{

/// One camera position of a testflight. Angles in radians (matching
/// FirstPersonCamera), FOV in degrees (human-editable json).
struct TestFlightShot
{
    std::string name;

    float posX = 0.f, posY = 0.f, posZ = 0.f;
    float yaw   = 0.f;
    float pitch = 0.f;

    float fovYDeg   = 60.f;
    float nearPlane = 0.1f;
    float farPlane  = 40000.f;

    /// Optional per-shot overrides of ew::DebugToggles (by field name, e.g.
    /// "terrainConstColor": true). Applied on top of the flight-level toggles.
    std::map<std::string, bool> toggles;
};

/// A complete testflight scenario: enforced window size, settle behaviour and
/// the ordered list of shots. The terrain is NOT part of the flight yet; the
/// application loads whatever lastFile.xml points to (future task).
struct TestFlight
{
    std::string name;

    /// Terrain the flight was authored on (informational for now: the app still
    /// loads whatever lastFile.xml points to). Stamped by the in-app editor on
    /// every save so runs are traceable to their terrain.
    std::string terrain;

    int windowWidth  = 1280;
    int windowHeight = 768;

    /// Minimum time to hold each camera before capturing.
    int settleMs = 1000;
    /// Hard cap per shot when the convergence criterion never triggers.
    int settleTimeoutMs = 15000;

    /// Flight-level ew::DebugToggles overrides (baseline for every shot).
    std::map<std::string, bool> toggles;

    std::vector<TestFlightShot> shots;
};

// --- json ------------------------------------------------------------------

inline void to_json(nlohmann::json& j, const TestFlightShot& s)
{
    j = nlohmann::json{
        {"name", s.name},
        {"pos", {s.posX, s.posY, s.posZ}},
        {"yaw", s.yaw},
        {"pitch", s.pitch},
        {"fovYDeg", s.fovYDeg},
        {"near", s.nearPlane},
        {"far", s.farPlane},
    };
    if (!s.toggles.empty())
        j["toggles"] = s.toggles;
}

inline void from_json(const nlohmann::json& j, TestFlightShot& s)
{
    s.name = j.value("name", std::string{});
    if (const auto it = j.find("pos"); it != j.end() && it->is_array() && it->size() >= 3)
    {
        s.posX = (*it)[0].get<float>();
        s.posY = (*it)[1].get<float>();
        s.posZ = (*it)[2].get<float>();
    }
    s.yaw       = j.value("yaw", 0.f);
    s.pitch     = j.value("pitch", 0.f);
    s.fovYDeg   = j.value("fovYDeg", 60.f);
    s.nearPlane = j.value("near", 0.1f);
    s.farPlane  = j.value("far", 40000.f);
    s.toggles   = j.value("toggles", std::map<std::string, bool>{});
}

inline void to_json(nlohmann::json& j, const TestFlight& f)
{
    j = nlohmann::json{
        {"name", f.name},
        {"terrain", f.terrain},
        {"window", {f.windowWidth, f.windowHeight}},
        {"settleMs", f.settleMs},
        {"settleTimeoutMs", f.settleTimeoutMs},
        {"cameras", f.shots},
    };
    if (!f.toggles.empty())
        j["toggles"] = f.toggles;
}

inline void from_json(const nlohmann::json& j, TestFlight& f)
{
    f.name    = j.value("name", std::string{});
    f.terrain = j.value("terrain", std::string{});
    if (const auto it = j.find("window"); it != j.end() && it->is_array() && it->size() >= 2)
    {
        f.windowWidth  = (*it)[0].get<int>();
        f.windowHeight = (*it)[1].get<int>();
    }
    f.settleMs        = j.value("settleMs", 1000);
    f.settleTimeoutMs = j.value("settleTimeoutMs", 15000);
    f.toggles         = j.value("toggles", std::map<std::string, bool>{});
    f.shots           = j.value("cameras", std::vector<TestFlightShot>{});
}

// --- file IO ----------------------------------------------------------------

/// Loads a flight from disk. Returns false and fills OutError on failure.
inline bool LoadTestFlight(const std::string& FilePath, TestFlight& OutFlight, std::string& OutError)
{
    std::ifstream is{FilePath};
    if (!is)
    {
        OutError = "cannot open '" + FilePath + "'";
        return false;
    }
    try
    {
        nlohmann::json j;
        is >> j;
        OutFlight = j.get<TestFlight>();
    }
    catch (const std::exception& ex)
    {
        OutError = "failed to parse '" + FilePath + "': " + ex.what();
        return false;
    }
    return true;
}

/// Saves a flight to disk (pretty-printed). Returns false and fills OutError on failure.
inline bool SaveTestFlight(const std::string& FilePath, const TestFlight& Flight, std::string& OutError)
{
    std::ofstream os{FilePath};
    if (!os)
    {
        OutError = "cannot write '" + FilePath + "'";
        return false;
    }
    os << nlohmann::json(Flight).dump(4) << "\n";
    if (!os)
    {
        OutError = "write to '" + FilePath + "' failed";
        return false;
    }
    return true;
}

} // namespace ew
