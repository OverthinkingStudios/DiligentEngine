/***************************************************************************
 # Copyright (c) 2015-22, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/

#include "terrain.h"

#include <algorithm>
#include <cstdlib>   // __min/__max (MSVC macros, used in the JP2 decode)
#include <cstring>
#include <random>
#include <chrono>
using namespace std::chrono;

#include "glm/gtc/matrix_transform.hpp"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "ots/Log.hpp"

using Diligent::BIND_SHADER_RESOURCE;
using Diligent::BIND_UNORDERED_ACCESS;
using Diligent::BIND_RENDER_TARGET;
using Diligent::BIND_DEPTH_STENCIL;
using Diligent::BIND_INDIRECT_DRAW_ARGS;


_lastFile terrainManager::lastfile;
std::string terrainManager::sTerrainOverride;


namespace
{

#if defined(_WIN32)
extern "C" __declspec(dllimport) int __stdcall IsDebuggerPresent(void);
#endif

// Missing-required-file handler: loud log, and break straight into the debugger
// when one is attached so the failing path is still on the callstack. Returns
// false so call sites can bail out.
bool requireFile(const std::filesystem::path& path, const char* what)
{
    if (std::filesystem::exists(path))
        return true;
    spdlog::error("terrain: required {} not found - '{}'", what, path.string());
#if defined(_WIN32)
    if (IsDebuggerPresent())
        __debugbreak();
#endif
    return false;
}

// The terrain settings file is named "<terrainName>.terrainSettings.json"
// (e.g. switserland_Steg.terrainSettings.json) - scan the terrain directory
// instead of hardcoding the prefix.
std::filesystem::path findTerrainSettingsJson(const std::filesystem::path& terrain_root)
{
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(terrain_root, ec))
    {
        const std::string name = entry.path().filename().string();
        const std::string suffix = ".terrainSettings.json";
        if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
            return entry.path();
    }
    return {};
}

} // namespace


_lastFile::_lastFile(const std::filesystem::path& terrain_root, const std::filesystem::path& resources_root)
{
    std::filesystem::path settingsJson = findTerrainSettingsJson(terrain_root);
    terrain = settingsJson.empty() ? (terrain_root / "terrainSettings.json").string()
                                   : settingsJson.string();
    road = (terrain_root / "roads/steg_010.roadnetwork").string();
    stamps = "";
    roadMaterial = (resources_root / "roadMaterials" / "asphalt" / "asphalt_17.roadMaterial").string();
    terrafectorMaterial = "";
    texture = "";
    fbx = "";
    EVO = "";

    weed = (resources_root / "vegetation_weeds").string();
    twig = (resources_root / "vegetation_twigs").string();
    leaves = (resources_root / "vegetation_leaves").string();
    trees = (resources_root / "vegetation_trees").string();
    vegMaterial = (resources_root / "vegetation_trees").string();

    // The terrainSettings.json dir fields are gameroot-relative ("/terrains/
    // switserland_Steg", "/terrains/_resources", ...) and onLoad joins them
    // onto these bases. Derive the gameroot from the terrain dir:
    // <gameroot>/terrains/<terrainName> -> <gameroot>.
    std::filesystem::path gameroot = terrain_root.parent_path().parent_path();
    dir_Resource = resources_root.string();
    dir_Terrains = gameroot.string();
    dir_GIS = gameroot.string();    // onLoad copies this into dirGis unchanged; GIS stays unused until the terrain generator lands

    mode = (int)_terrainMode::roads;    // a mode whose update() streams terrain
}


// Not implemented: setupVert, the paraglider ribbon setup (glider scope).


// --- _shadowEdges: CPU semi-static terrain shadow ---

_shadowEdges::_shadowEdges()
{
    // Zero the shadow field so a texture upload before the first load() is
    // defined; the rest is fully written by load()/solve().
    height  = new float[4096][4096];
    Nx      = new float[4095][4095];
    edge    = new unsigned char[4096][4096];
    shadowH = new float2[4096][4096];
    std::memset(shadowH, 0, sizeof(float2) * 4096 * 4096);
}

_shadowEdges::~_shadowEdges()
{
    m_terminate = true;
    if (m_solveThread.joinable())
        m_solveThread.join();
    delete[] height;
    delete[] Nx;
    delete[] edge;
    delete[] shadowH;
}

void _shadowEdges::launchSolveThread()
{
    if (!m_solveThread.joinable())
        m_solveThread = std::thread(&_shadowEdges::solveThread, this);
}

void _shadowEdges::solveThread()
{
    while (!m_terminate)
    {
        if (requestNewShadow)
        {
            sunAngle += dAngle;
            sunAng.x = -cos(sunAngle);
            sunAng.y = -sin(sunAngle);
            sunAng.z = 0;
            solve(-sunAng.y, sunAng.x > 0);
            shadowReady = true;
            requestNewShadow = false;
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

void _shadowEdges::solve(float _angle, bool dx)
{
    for (int y = 0; y < 4095; y++)
    {
        for (int x = 0; x < 4095; x++)
        {
            //shadowH[y][x] = float2(height[y][x] - 15.f, 0.f);
            shadowH[y][x] = float2(-5000.f, 0.f);   // when splittign the two
        }
    }

    float a_min = _angle + 0.02f;
    float a_max = _angle - 0.02f;

    for (int y = 1; y < 4094; y++)
    {
        for (int x = 1; x < 4094; x++)
        {

            if (dx && (Nx[y][x - 1] < a_min) && (Nx[y][x] > a_max))
            {
                float H = height[y][x];

                for (int j = x + 1; j < 4096; j++)
                {
                    H -= _angle * 9.765625f;
                    if (H > shadowH[y][j].x)
                    {
                        float softDepth = (float)(j - x) * 9.765625f / 10.f;
                        shadowH[y][j] = float2(H - 0.f, softDepth);
                    }
                    else break;
                }
            }
            else if (!dx && (-Nx[y][x + 1] < a_min) && (-Nx[y][x] > a_max))
            {
                float H = height[y][x];

                for (int j = x - 1; j > 0; j--)
                {
                    H -= _angle * 9.765625f;
                    if (H > shadowH[y][j].x)
                    {
                        float softDepth = (float)(x - j) * 9.765625f / 10.f;
                        shadowH[y][j] = float2(H - 0.f, softDepth);
                    }
                    else break;
                }
            }
        }
    }
}

void _shadowEdges::load(std::string filename, float _angle, const buildingsRenderer* _buildings)
{
    (void)_angle;   // unused

    std::ifstream ifs;
    ifs.open(filename, std::ios::binary);

    if (ifs)
    {
        ifs.read((char*)height, 4096 * 4096 * 4);
        ifs.close();

        // Buildings become part of the caster heightfield: their walls show
        // up as steep Nx slopes below, so the existing edge-detect + march in
        // solve() casts their shadows with no extra code (step-6 salvage).
        if (_buildings)
        {
            _buildings->overlayShadowHeights(&height[0][0], 4096, 9.765625f);
        }

        for (int y = 0; y < 4095; y++)
        {
            for (int x = 0; x < 4095; x++)
            {
                Nx[y][x] = (height[y][x] - height[y][x + 1]) / 9.765625f;    // 10 meter between pixels SHIT NOT TRUE

                shadowH[y][x] = float2(height[y][x] - 5.f, 0.f);
                // remove this and pass terrein height seperate
                // (until the first solve reaches the GPU this placeholder means
                //  "shadowed iff geometry sits more than 5 m below the
                //  heightfield")
            }
        }
    }
    else
    {
        spdlog::error("terrain: _shadowEdges::load cannot open '{}' - terrain shadows stay at the load placeholder", filename);
    }
}


namespace
{

//mimic hlsl saturate()
inline float4 saturate(float4 v)
{
    return glm::clamp(v, float4(0.f), float4(1.f));
}

// Hole detector: plausible elevation range of the whole terrain (Steg spans
// roughly 400..2200m). Used for the column visibility test - "is there ANY
// height at which this tile's footprint would be in the frustum".
constexpr float kTerrainYMin = 0.f;
constexpr float kTerrainYMax = 3000.f;

// The directory fields in terrainSettings.json may or may not carry a leading
// slash; concatenate when they do, join through std::filesystem when they
// don't.
std::string joinPath(const std::string& base, const std::string& rel)
{
    if (rel.empty()) return base;
    if (rel.front() == '/' || rel.front() == '\\') return base + rel;
    return (std::filesystem::path(base) / rel).string();
}

} // namespace


void quadtree_tile::init(uint _index)
{
    index = _index;
    parent = nullptr;
    child[0] = nullptr;
    child[1] = nullptr;
    child[2] = nullptr;
    child[3] = nullptr;
}

void quadtree_tile::set(uint _lod, uint _x, uint _y, float _size, float4 _origin, quadtree_tile* _parent)
{
    lod = _lod;
    x = _x;
    y = _y;
    size = _size;
    origin = _origin;

    boundingSphere = origin + float4(size / 2, 0, size / 2, 0);
    boundingSphere.w = 1.0f;

    parent = _parent;
    child[0] = nullptr;
    child[1] = nullptr;
    child[2] = nullptr;
    child[3] = nullptr;

    if (parent)
    {
        origin.y = parent->boundingSphere.y - size * 2;
        boundingSphere.y = parent->boundingSphere.y;
    }
    else
    {
        origin.y = size * 2;
        boundingSphere.y = 0;
    }

    numQuads = 0;
    numPlants = 0;

    forSplit = false;
    forRemove = false;
    heightPatched = false;      // Y above is inherited - see tileInFrustum

    elevationHash = 0;
}





terrainManager::terrainManager()
{

}



terrainManager::~terrainManager()
{
    if (m_loaded)   // never let a failed bootstrap clobber a good lastFile.xml
    {
        std::ofstream os("lastFile.xml");
        if (os.good()) {
            cereal::XMLOutputArchive archive(os);
            lastfile.mode = (int)terrainMode;
            lastfile.road = mRoadNetwork.lastUsedFilename.string();
            lastfile.stamps = mRoadStampCollection.lastUsedFilename.string();
            archive(CEREAL_NVP(lastfile));
        }
    }

    if (terrafectorSystem::_logfile && terrafectorSystem::_logfile != stderr)
    {
        fclose(terrafectorSystem::_logfile);
        terrafectorSystem::_logfile = nullptr;
    }
}



void terrainManager::onLoad(ew::GpuContext* pRenderContext)
{
    std::filesystem::path currentPath = std::filesystem::current_path();

    // The whole terrafector/roads load fabric fprintf's into this FILE*, which
    // lands in the cwd (= gameroot). The stderr fallback keeps that fabric from
    // ever seeing a null FILE*.
    if (!terrafectorSystem::_logfile)
    {
        terrafectorSystem::_logfile = fopen("earthworks_terrafectors.log", "w");
        if (!terrafectorSystem::_logfile)
            terrafectorSystem::_logfile = stderr;
        terrafectorSystem::logStartTime = std::chrono::high_resolution_clock::now();
    }

    if (!sTerrainOverride.empty())
    {
        // Explicit terrain requested from outside (command line `-terrain
        // <dir-or-settings.json>`); bypasses lastFile.xml entirely.
        std::filesystem::path o = sTerrainOverride;
        std::filesystem::path terrainDir = std::filesystem::is_directory(o) ? o : o.parent_path();
        spdlog::info("terrain: override requested - '{}' (terrain dir '{}')", o.string(), terrainDir.string());
        lastfile = _lastFile(terrainDir, terrainDir.parent_path() / "_resources");
        if (!std::filesystem::is_directory(o))
            lastfile.terrain = o.string();      // an explicit settings file wins over the scan
        terrainMode = (_terrainMode)lastfile.mode;
        requireFile(lastfile.terrain, "terrain settings (from -terrain override)");
    }
    else
    {
        // Move the constructor code here
        std::ifstream is("lastFile.xml");
        if (is.good()) {
            cereal::XMLInputArchive archive(is);
            archive(CEREAL_NVP(lastfile));

            terrainMode = (_terrainMode)lastfile.mode;
        }
        else
        {
            spdlog::warn("terrain: lastFile.xml not found in {}, bootstrapping Steg defaults", currentPath.string());
            lastfile = _lastFile(currentPath / "terrains" / "switserland_Steg",
                                 currentPath / "terrains" / "_resources");
            terrainMode = (_terrainMode)lastfile.mode;
        }
    }

    // The vegetation/glider/terrainBuilder/textureTool modes early-out of
    // update() and never stream tiles - force a terrain-rendering mode.
    if (terrainMode == _terrainMode::vegetation || terrainMode == _terrainMode::glider ||
        terrainMode == _terrainMode::terrainBuilder || terrainMode == _terrainMode::textureTool)
    {
        spdlog::info("terrain: mode {} does not render terrain, forcing 'roads'", (int)terrainMode);
        terrainMode = _terrainMode::roads;
    }
    ew::gDebug.live.terrainMode = ew::gDebug.shown.terrainMode = (int)terrainMode;

    mRoadNetwork.lastUsedFilename = lastfile.road;
    mRoadStampCollection.lastUsedFilename = lastfile.stamps;

    std::string appendedName = currentPath.string() + lastfile.terrain;
    std::ifstream isT(lastfile.terrain);
    std::ifstream isT_2(appendedName);

    spdlog::info("terrain: last loaded terrain - {}", lastfile.terrain);
    spdlog::info("terrain: appended terrain - {}", appendedName);

    if (isT.good()) {
        spdlog::info("terrain: loading absolute terrain {}", lastfile.terrain);
        cereal::JSONInputArchive archive(isT);
        settings.serialize(archive, 100);

        if (!std::filesystem::exists(lastfile.dir_Terrains))
            spdlog::error("terrain: lastFile dir_Terrains does not exist - '{}' (assets under <dirRoot> will not resolve)", lastfile.dir_Terrains);
        if (!std::filesystem::exists(lastfile.dir_Resource))
            spdlog::error("terrain: lastFile dir_Resource does not exist - '{}'", lastfile.dir_Resource);
        if (!std::filesystem::exists(lastfile.dir_GIS))
            spdlog::error("terrain: lastFile dir_GIS does not exist - '{}'", lastfile.dir_GIS);

        settings.dirRoot = joinPath(lastfile.dir_Terrains, settings.dirRoot);
        settings.dirResource = lastfile.dir_Resource;
        settings.dirGis = lastfile.dir_GIS;
    }
    else if (isT_2.good())
    {
        cereal::JSONInputArchive archive(isT_2);
        settings.serialize(archive, 100);

        spdlog::info("terrain: loading relative terrain, prepending root directory - {}", appendedName);

        settings.dirRoot = joinPath(currentPath.string(), settings.dirRoot);
        settings.dirGis = joinPath(currentPath.string(), settings.dirGis);
        settings.dirResource = joinPath(currentPath.string(), settings.dirResource);
    }
    else
    {
        // Keep running without terrain - the debug grid stays usable.
        requireFile(lastfile.terrain, "terrain settings file");
        spdlog::error("terrain: unable to load a terrain settings file ('{}' / '{}') - terrain disabled",
                      lastfile.terrain, appendedName);
        return;
    }

    spdlog::info("terrain: root - {}", settings.dirRoot);
    spdlog::info("terrain: gis - {}", settings.dirGis);
    spdlog::info("terrain: resource - {}", settings.dirResource);

    if (!requireFile(std::filesystem::path(settings.dirRoot) / "elevations.txt", "elevation index (elevations.txt)"))
    {
        spdlog::error("terrain: terrain disabled");
        return;
    }

    terrafectorSystem::pEcotopes = &mEcosystem;
    ecotopeSystem::pVegetation = &plants_Root;

    {
        Diligent::SamplerDesc samplerDesc;
        samplerDesc.MinFilter = Diligent::FILTER_TYPE_ANISOTROPIC;
        samplerDesc.MagFilter = Diligent::FILTER_TYPE_ANISOTROPIC;
        samplerDesc.MipFilter = Diligent::FILTER_TYPE_ANISOTROPIC;
        samplerDesc.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressW = Diligent::TEXTURE_ADDRESS_CLAMP;
        samplerDesc.MaxAnisotropy = 8;
        sampler_Clamp = ew::Sampler::create(samplerDesc);

        samplerDesc.AddressU = Diligent::TEXTURE_ADDRESS_WRAP;
        samplerDesc.AddressV = Diligent::TEXTURE_ADDRESS_WRAP;
        samplerDesc.AddressW = Diligent::TEXTURE_ADDRESS_WRAP;
        samplerDesc.MaxAnisotropy = 8;
        sampler_Trilinear = ew::Sampler::create(samplerDesc);

        samplerDesc.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressW = Diligent::TEXTURE_ADDRESS_CLAMP;
        samplerDesc.MaxAnisotropy = 4;
        sampler_ClampAnisotropic = ew::Sampler::create(samplerDesc);

        samplerDesc.MinFilter = Diligent::FILTER_TYPE_LINEAR;
        samplerDesc.MagFilter = Diligent::FILTER_TYPE_LINEAR;
        samplerDesc.MipFilter = Diligent::FILTER_TYPE_LINEAR;
        samplerDesc.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressV = Diligent::TEXTURE_ADDRESS_WRAP;
        samplerDesc.AddressW = Diligent::TEXTURE_ADDRESS_WRAP;
        samplerDesc.MaxAnisotropy = 1;
        sampler_Ribbons = ew::Sampler::create(samplerDesc);
    }

    {
        split.debug_texture = ew::Texture::create2D(tile_numPixels, tile_numPixels, Diligent::TEX_FORMAT_RGBA8_UNORM, 1, 1, nullptr, BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE, "split.debug_texture");
        split.normals_texture = ew::Texture::create2D(tile_numPixels, tile_numPixels, Diligent::TEX_FORMAT_R11G11B10_FLOAT, 1, 1, nullptr, BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE, "split.normals_texture");
        split.vertex_A_texture = ew::Texture::create2D(tile_numPixels / 2, tile_numPixels / 2, Diligent::TEX_FORMAT_R16_UINT, 1, 1, nullptr, BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS, "split.vertex_A_texture");
        split.vertex_B_texture = ew::Texture::create2D(tile_numPixels / 2, tile_numPixels / 2, Diligent::TEX_FORMAT_R16_UINT, 1, 1, nullptr, BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS, "split.vertex_B_texture");
    }

    {
        std::vector<std::uint16_t> vertexData(tile_numPixels / 2 * tile_numPixels / 2);
        memset(vertexData.data(), 0, tile_numPixels / 2 * tile_numPixels / 2 * sizeof(std::uint16_t));	  // set to zero's
        split.vertex_clear = ew::Texture::create2D(tile_numPixels / 2, tile_numPixels / 2, Diligent::TEX_FORMAT_R16_UINT, 1, 1, vertexData.data(), BIND_SHADER_RESOURCE, "split.vertex_clear");

        // kante
        // The seed lattice below is the guaranteed-minimum vertex set of every
        // tile mesh - hyper-tuned, do not touch.
        for (uint i = 1; i < 128; i += 2)
        {
            vertexData[(1 << 7) + i] = (1 << 7) + i;
            vertexData[(127 << 7) + i] = (127 << 7) + i;
            vertexData[(i << 7) + 1] = (i << 7) + 1;
            vertexData[(i << 7) + 127] = (i << 7) + 127;
        }

        for (uint y = 9; y < 128; y += 8)
        {
            for (uint x = 9; x < 128; x += 8)
            {
                vertexData[(y << 7) + x] = static_cast<std::uint16_t>((y << 7) + x);
            }
        }

        for (uint i = 5; i < 128; i += 4)
        {
            vertexData[(5 << 7) + i] = (5 << 7) + i;
            vertexData[(125 << 7) + i] = (125 << 7) + i;
            vertexData[(i << 7) + 5] = (i << 7) + 5;
            vertexData[(i << 7) + 125] = (i << 7) + 125;
        }
        split.vertex_preload = ew::Texture::create2D(tile_numPixels / 2, tile_numPixels / 2, Diligent::TEX_FORMAT_R16_UINT, 1, 1, vertexData.data(), BIND_SHADER_RESOURCE, "split.vertex_preload");
    }

    {
        // Diligent does NOT zero default-heap buffers, and the per-frame
        // readback patch below is gated on .x > 0 - so zero this explicitly.
        std::vector<float4> zeroCenters(numTiles, float4(0.f));
        split.buffer_tileCenters = ew::Buffer::createStructured(sizeof(float4), numTiles,
            BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS, zeroCenters.data(), "buffer_tileCenters");
        split.buffer_tileCenter_readback = ew::ReadbackBuffer::create(sizeof(float4) * numTiles, "buffer_tileCenter_readback");
    }


    {
        // u16 noise texture
        std::mt19937 generator(2);      // deterministic - same seed => same world (do not reseed)
        std::uniform_int_distribution<> distribution(0, 65535);
        std::vector<unsigned short> random(256 * 256);
        for (int i = 0; i < 256 * 256; i++)
        {
            random[i] = static_cast<unsigned short>(distribution(generator));    // FIXME for 12 ecotopes
        }
        split.noise_u16 = ew::Texture::create2D(256, 256, Diligent::TEX_FORMAT_R16_UINT, 1, 1, random.data(), BIND_SHADER_RESOURCE, "split.noise_u16");

        // frame buffer: the 8-MRT tile bake target. The colours are also
        // written as UAVs by the bicubic/ecotope computes, so the UAV bind flag
        // is load-bearing. Single-mip - nothing samples a mip of these.
        {
            const Diligent::BIND_FLAGS rtFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS;
            split.tileFbo = ew::Fbo::create();
            split.tileFbo->attachColorTarget(ew::Texture::create2D(tile_numPixels, tile_numPixels, Diligent::TEX_FORMAT_R32_FLOAT, 1, 1, nullptr, rtFlags, "tileFbo elevation"), 0);		// elevation
            split.tileFbo->attachColorTarget(ew::Texture::create2D(tile_numPixels, tile_numPixels, Diligent::TEX_FORMAT_R11G11B10_FLOAT, 1, 1, nullptr, rtFlags, "tileFbo albedo"), 1);	// albedo
            split.tileFbo->attachColorTarget(ew::Texture::create2D(tile_numPixels, tile_numPixels, Diligent::TEX_FORMAT_R11G11B10_FLOAT, 1, 1, nullptr, rtFlags, "tileFbo pbr"), 2);		// pbr
            split.tileFbo->attachColorTarget(ew::Texture::create2D(tile_numPixels, tile_numPixels, Diligent::TEX_FORMAT_R11G11B10_FLOAT, 1, 1, nullptr, rtFlags, "tileFbo alpha"), 3);		// alpha
            split.tileFbo->attachColorTarget(ew::Texture::create2D(tile_numPixels, tile_numPixels, Diligent::TEX_FORMAT_RGBA8_UNORM, 1, 1, nullptr, rtFlags, "tileFbo ecotope0"), 4);		// ecotopes  ? R11G11B10Float
            split.tileFbo->attachColorTarget(ew::Texture::create2D(tile_numPixels, tile_numPixels, Diligent::TEX_FORMAT_RGBA8_UNORM, 1, 1, nullptr, rtFlags, "tileFbo ecotope1"), 5);		// ecotopes
            split.tileFbo->attachColorTarget(ew::Texture::create2D(tile_numPixels, tile_numPixels, Diligent::TEX_FORMAT_RGBA8_UNORM, 1, 1, nullptr, rtFlags, "tileFbo ecotope2"), 6);		// ecotopes
            split.tileFbo->attachColorTarget(ew::Texture::create2D(tile_numPixels, tile_numPixels, Diligent::TEX_FORMAT_RGBA8_UNORM, 1, 1, nullptr, rtFlags, "tileFbo ecotope3"), 7);		// ecotopes
            split.tileFbo->attachDepthStencilTarget(ew::Texture::create2D(tile_numPixels, tile_numPixels, Diligent::TEX_FORMAT_D24_UNORM_S8_UINT, 1, 1, nullptr, BIND_DEPTH_STENCIL, "tileFbo depth"));	// keep for now, not sure why, but maybe usefult for cuts
        }
        // Not implemented: bakeFbo / bake.copy_texture / bakeFbo_plants (the
        // offline export and vegetation bake targets)

        compressed_Normals_Array = ew::Texture::create2D(tile_numPixels, tile_numPixels, Diligent::TEX_FORMAT_R11G11B10_FLOAT, numTiles, 1, nullptr, BIND_SHADER_RESOURCE, "compressed_Normals_Array");	  // Now an array	  at 1024 tiles its 256 Mb , Fair bit but do-ablwe
        compressed_Albedo_Array = ew::Texture::create2D(tile_numPixels, tile_numPixels, Diligent::TEX_FORMAT_R11G11B10_FLOAT, numTiles, 1, nullptr, BIND_SHADER_RESOURCE, "compressed_Albedo_Array");
        compressed_PBR_Array = ew::Texture::create2D(tile_numPixels, tile_numPixels, Diligent::TEX_FORMAT_BC6H_UF16, numTiles, 1, nullptr, BIND_SHADER_RESOURCE, "compressed_PBR_Array");
        height_Array = ew::Texture::create2D(tile_numPixels, tile_numPixels, Diligent::TEX_FORMAT_R32_FLOAT, numTiles, 1, nullptr, BIND_SHADER_RESOURCE, "height_Array");	  // Now an array	  1024 tiles is 64 MB - really nice and small

        // The indirect-arg buffers must be created ZEROED: the shaders never
        // write startVertexLocation/startInstanceLocation and rely on it.
        {
            std::vector<t_DrawArguments> zeroDraw(numRenderViews, t_DrawArguments{0, 0, 0, 0});
            std::vector<t_DispatchArguments> zeroDispatch(numRenderViews, t_DispatchArguments{0, 0, 0, 0});
            split.drawArgs_quads = ew::Buffer::createStructured(sizeof(t_DrawArguments), numRenderViews, BIND_UNORDERED_ACCESS | BIND_INDIRECT_DRAW_ARGS, zeroDraw.data(), "drawArgs_quads");
            split.drawArgs_tiles = ew::Buffer::createStructured(sizeof(t_DrawArguments), numRenderViews, BIND_UNORDERED_ACCESS | BIND_INDIRECT_DRAW_ARGS, zeroDraw.data(), "drawArgs_tiles");
            split.dispatchArgs_plants = ew::Buffer::createStructured(sizeof(t_DispatchArguments), numRenderViews, BIND_UNORDERED_ACCESS | BIND_INDIRECT_DRAW_ARGS, zeroDispatch.data(), "dispatchArgs_plants");
        }

        {
            GC_feedback zeroFeedback = {};
            split.buffer_feedback = ew::Buffer::createStructured(sizeof(GC_feedback), 1, BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS, &zeroFeedback, "buffer_feedback");
            split.buffer_feedback_read = ew::ReadbackBuffer::create(sizeof(GC_feedback), "buffer_feedback_read");
        }

        split.buffer_tiles = ew::Buffer::createStructured(sizeof(gpuTile), numTiles, BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS, nullptr, "buffer_tiles");
        split.buffer_instance_quads = ew::Buffer::createStructured(sizeof(instance_PLANT), numTiles * numQuadsPerTile, BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS, nullptr, "buffer_instance_quads");
        split.buffer_instance_plants = ew::Buffer::createStructured(sizeof(instance_PLANT), numTiles * numPlantsPerTile, BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS, nullptr, "buffer_instance_plants");
        split.buffer_clippedloddedplants = ew::Buffer::createStructured(sizeof(xformed_PLANT), 1024 * 1024, BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS, nullptr, "buffer_clippedloddedplants"); //32 bytes

        for (uint i = 0; i < numRenderViews; i++)
        {
            const std::string nameT = "buffer_lookup_terrain[" + std::to_string(i) + "]";
            const std::string nameQ = "buffer_lookup_quads[" + std::to_string(i) + "]";
            const std::string nameP = "buffer_lookup_plants[" + std::to_string(i) + "]";
            split.buffer_lookup_terrain[i] = ew::Buffer::createStructured(sizeof(tileLookupStruct), lookupSizeTerrain[i], BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS, nullptr, nameT.c_str());
            split.buffer_lookup_quads[i] = ew::Buffer::createStructured(sizeof(tileLookupStruct), lookupSizeBillboard[i], BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS, nullptr, nameQ.c_str());
            split.buffer_lookup_plants[i] = ew::Buffer::createStructured(sizeof(tileLookupStruct), lookupSizePlants[i], BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS, nullptr, nameP.c_str());
        }

        // No UAV-counter twin for buffer_terrain: no shader calls
        // IncrementCounter on it.
        split.buffer_terrain = ew::Buffer::createStructured(sizeof(Terrain_vertex), numVertPerTile * numTiles, BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS, nullptr, "buffer_terrain");

        terrainShader.load("hlsl/terrain/render_Tiles.hlsl", "vsMain", "psMain", ew::Topology::TriangleList);
        terrainShader.setBuffer("tiles", split.buffer_tiles);
        // not here, irt depends on teh view we render
        //terrainShader.setBuffer("tileLookup", split.buffer_lookup_terrain);

        terrainShader.setTexture("gAlbedoArray", compressed_Albedo_Array);
        terrainShader.setTexture("gPBRArray", compressed_PBR_Array);
        terrainShader.setTexture("gNormArray", compressed_Normals_Array);
        terrainShader.setSampler("gSmpAniso", sampler_ClampAnisotropic);
        terrainShader.setBuffer("VB", split.buffer_terrain);

        terrainShader.setVariable("PerFrameCB", "gisOverlayStrength", gis_overlay.strenght);
        terrainShader.setVariable("PerFrameCB", "redStrength", gis_overlay.redStrength);
        terrainShader.setVariable("PerFrameCB", "redScale", gis_overlay.redScale);
        terrainShader.setVariable("PerFrameCB", "redOffset", gis_overlay.redOffset);
        terrainShader.setSampler("gSmpLinearClamp", sampler_Clamp);
        // render_Common.hlsli's shadow() samples terrainShadow through
        // gSmpLinear (s1), so it has to be bound even though nothing here reads
        // it directly. The shadow UV is saturate()d, so wrap-vs-clamp only
        // differs on the exact border texel.
        terrainShader.setSampler("gSmpLinear", sampler_Trilinear);

        // Cull NONE for the terrain draw: the delaunay winding has never been
        // verified against back-face culling.
        {
            Diligent::RasterizerStateDesc rsDesc;
            rsDesc.CullMode = Diligent::CULL_MODE_NONE;
            terrainShader.setRasterizerState(rsDesc);
        }

        // terrainSpiteShader - billboards over terrain. Its plant_buffer and
        // materials binds follow after plants_Root.onLoad below.
        terrainSpiteShader.load("hlsl/terrain/render_tile_sprite.hlsl", "vsMain", "psMain", ew::Topology::PointList, "gsMain");
        terrainSpiteShader.setBuffer("tiles", split.buffer_tiles);
        //terrainSpiteShader.setBuffer("tileLookup", split.buffer_lookup_quads);
        terrainSpiteShader.setBuffer("instanceBuffer", split.buffer_instance_quads);        // WHY BOTH
        terrainSpiteShader.setSampler("gSampler", sampler_ClampAnisotropic);
        terrainSpiteShader.setSampler("gSmpLinearClamp", sampler_Ribbons);
        // render_Common.hlsli's shadow()/sunLight() sample through
        // gSmpLinear/gSmpLinearClamp (s1/s3) here, same as render_Tiles.
        terrainSpiteShader.setSampler("gSmpLinear", sampler_Trilinear);

        // Not implemented: ribbonShader (the render_ribbons grass pass) and
        // veghumanShader (a render_triangles twin) - neither has a live draw.

        // Far-LOD buildings (step-6 salvage): loud-and-graceful when the
        // rappersville data set is absent (most terrains ship none).
        buildings.load(settings.dirRoot + "/buildings/rappersville");

        compute_bakeFloodfill.load("hlsl/terrain/compute_bakeFloodfill.hlsl");


        triangleData = ew::Buffer::createStructured(sizeof(triangleVertex), 16384, BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS, nullptr, "triangleData"); // just a nice amount for now

        triangleShader.load("hlsl/terrain/render_triangles.hlsl", "vsMain", "psMain", ew::Topology::TriangleList);
        triangleShader.setBuffer("instanceBuffer", triangleData);        // WHY BOTH
        triangleShader.setBuffer("instances", split.buffer_clippedloddedplants);
        triangleShader.setSampler("gSampler", sampler_ClampAnisotropic);
        triangleShader.setSampler("gSmpLinearClamp", sampler_ClampAnisotropic);


        // Sky/env/dappled resources. Terrains without a skies/ folder (Steg is
        // one) fall back to a dummy CUBE: the ew layer's automatic fallback is
        // 2D-only, and gSky is reachable through a runtime branch, so the view
        // type has to be right or the shader fails validation.
        {
            const std::filesystem::path skyPath = std::filesystem::path(settings.dirResource) / "skies/alps_bc.dds";
            const std::filesystem::path envPath = std::filesystem::path(settings.dirResource) / "skies/alps_IR_bc.dds";
            const uint32_t blackTexel = 0xff000000;
            if (std::filesystem::exists(skyPath))
                vegetation.skyTexture = ew::Texture::createFromFile(skyPath, false, true);
            else
            {
                spdlog::warn("terrain: '{}' missing - skydome gSky uses a black dummy cube (live sky comes from the atmosphere anyway)", skyPath.string());
                vegetation.skyTexture = ew::Texture::createCube(1, Diligent::TEX_FORMAT_RGBA8_UNORM, &blackTexel, BIND_SHADER_RESOURCE, "sky dummy cube");
            }
            if (std::filesystem::exists(envPath))
                vegetation.envTexture = ew::Texture::createFromFile(envPath, false, true);
            else
            {
                spdlog::warn("terrain: '{}' missing - vegetation gEnv uses a black dummy cube", envPath.string());
                vegetation.envTexture = ew::Texture::createCube(1, Diligent::TEX_FORMAT_RGBA8_UNORM, &blackTexel, BIND_SHADER_RESOURCE, "env dummy cube");
            }
            vegetation.dappledLightTexture = ew::Texture::createFromFile(std::filesystem::path(settings.dirResource) / "vegetation/dappled_noise_01.jpg", false, true);
            triangleShader.setTexture("gSky", vegetation.skyTexture);
        }


        // Loadss the sky triangles - DO BERTEER ###########################################################################################################
        {
            unsigned int flags =
                aiProcess_FlipUVs |
                aiProcess_Triangulate |
                aiProcess_PreTransformVertices |
                //aiProcess_JoinIdenticalVertices |
                aiProcess_GenBoundingBoxes;


            std::vector<triangleVertex> testribbonsFile(50 * 128);
            memset(testribbonsFile.data(), 0, 50 * 128 * sizeof(triangleVertex));
            uint vertCount = 0;
            Assimp::Importer importer;
            const aiScene* scene = nullptr;

            {
                const std::string name = settings.dirResource + "/cube.fbx";
                requireFile(name, "skydome mesh (cube.fbx)");   // missing mesh = black sky; the sun only reaches the screen through the skydome draw

                scene = importer.ReadFile(name.c_str(), flags);
                if (scene)
                {
                    aiMesh* M = scene->mMeshes[0];
                    uint numSegments = M->mNumFaces;
                    for (uint j = 0; j < numSegments; j++)
                    {
                        aiFace face = M->mFaces[j];
                        for (int idx = 0; idx < 3; idx++)
                        {
                            aiVector3D V = M->mVertices[face.mIndices[idx]];
                            aiVector3D N = M->mNormals[face.mIndices[idx]];
                            aiVector3D U = M->mTextureCoords[0][face.mIndices[idx]];
                            testribbonsFile[vertCount].pos = float3(V.x, V.y, V.z) * 0.01f;
                            testribbonsFile[vertCount].norm = float3(N.x, N.y, N.z);
                            testribbonsFile[vertCount].u = U.x;
                            testribbonsFile[vertCount].v = U.y;
                            vertCount++;
                        }
                    }
                }
            }


            triangleData->setBlob(testribbonsFile.data(), 0, 50 * 128 * sizeof(triangleVertex));
        }

        // Skydome render states - init_TopdownRender reuses these two.
        {
            split.rasterstateSplines = Diligent::RasterizerStateDesc{};
            split.rasterstateSplines.FillMode = Diligent::FILL_MODE_SOLID;
            split.rasterstateSplines.CullMode = Diligent::CULL_MODE_NONE;

            split.blendstateSplines = Diligent::BlendStateDesc{};
            auto& rt0 = split.blendstateSplines.RenderTargets[0];
            rt0.BlendEnable    = Diligent::True;
            rt0.BlendOp        = Diligent::BLEND_OPERATION_ADD;
            rt0.BlendOpAlpha   = Diligent::BLEND_OPERATION_ADD;
            rt0.SrcBlend       = Diligent::BLEND_FACTOR_SRC_ALPHA;
            rt0.DestBlend      = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
            rt0.SrcBlendAlpha  = Diligent::BLEND_FACTOR_ZERO;
            rt0.DestBlendAlpha = Diligent::BLEND_FACTOR_ZERO;
        }

        {
            compute_TerrainUnderMouse.load("hlsl/terrain/compute_terrain_under_mouse.hlsl");
            compute_TerrainUnderMouse.setSampler("gSampler", sampler_Clamp);
            compute_TerrainUnderMouse.setTexture("gHeight", height_Array);
            compute_TerrainUnderMouse.setBuffer("tiles", split.buffer_tiles);
            compute_TerrainUnderMouse.setBuffer("groundcover_feedback", split.buffer_feedback);

            // clear
            split.compute_tileClear.load("hlsl/terrain/compute_tileClear.hlsl");
            split.compute_tileClear.setBuffer("feedback", split.buffer_feedback);
            split.compute_tileClear.setBuffer("DrawArgs_Terrain", split.drawArgs_tiles);
            split.compute_tileClear.setBuffer("DrawArgs_Quads", split.drawArgs_quads);
            split.compute_tileClear.setBuffer("DispatchArgs_Plants", split.dispatchArgs_plants);
            // (DrawArgs_Plants / feedback_Veg are bound to the plants_Root
            // buffers after plants_Root.onLoad below)

            split.compute_clipLodAnimatePlants.load("hlsl/terrain/compute_clipLodAnimatePlants.hlsl");
            split.compute_clipLodAnimatePlants.setBuffer("tiles", split.buffer_tiles);
            //split.compute_clipLodAnimatePlants.setBuffer("tileLookup", split.buffer_lookup_plants); FO later per view
            split.compute_clipLodAnimatePlants.setBuffer("plantBuffer", split.buffer_instance_plants);
            split.compute_clipLodAnimatePlants.setBuffer("output", split.buffer_clippedloddedplants);
            //split.compute_clipLodAnimatePlants.setBuffer("drawArgs_Plants", split.drawArgs_clippedloddedplants);
            split.compute_clipLodAnimatePlants.setBuffer("feedback", split.buffer_feedback);

            // split merge
            split.compute_tileSplitMerge.load("hlsl/terrain/compute_tileSplitMerge.hlsl");
            split.compute_tileSplitMerge.setBuffer("tiles", split.buffer_tiles);
            split.compute_tileSplitMerge.setBuffer("feedback", split.buffer_feedback);

            // passthrough
            split.compute_tilePassthrough.load("hlsl/terrain/compute_tilePassthrough.hlsl");
            split.compute_tilePassthrough.setBuffer("quad_instance", split.buffer_instance_quads);
            split.compute_tilePassthrough.setBuffer("plant_i", split.buffer_instance_plants);
            split.compute_tilePassthrough.setBuffer("feedback", split.buffer_feedback);
            split.compute_tilePassthrough.setBuffer("tiles", split.buffer_tiles);
            split.compute_tilePassthrough.setTexture("gHgt", split.tileFbo->getColorTexture(0));
            split.compute_tilePassthrough.setTexture("gNoise", split.noise_u16);
            // (plant_buffer is bound to plants_Root.plantData after plants_Root.onLoad below)

            // build lookup
            split.compute_tileBuildLookup.load("hlsl/terrain/compute_tileBuildLookup.hlsl");
            split.compute_tileBuildLookup.setBuffer("tiles", split.buffer_tiles);
            split.compute_tileBuildLookup.setBuffer("DrawArgs_Quads", split.drawArgs_quads);
            split.compute_tileBuildLookup.setBuffer("DrawArgs_Terrain", split.drawArgs_tiles);
            split.compute_tileBuildLookup.setBuffer("feedback", split.buffer_feedback);
            split.compute_tileBuildLookup.setBuffer("DispatchArgs_Plants", split.dispatchArgs_plants);
            split.compute_tileBuildLookup.setBuffer("tileCenters", split.buffer_tileCenters);  // to clear unused tile
            // The shader's 3x18 lookup-buffer arrays are flattened into 54
            // individually named buffers (viewRenderData_lookupBuffers.hlsli).
            for (uint i = 0; i < numRenderViews; i++)
            {
                const std::string idx = std::to_string(i);
                split.compute_tileBuildLookup.setBuffer("viewRenderData_terrainLookup_" + idx, split.buffer_lookup_terrain[i]);
                split.compute_tileBuildLookup.setBuffer("viewRenderData_plantLookup_" + idx, split.buffer_lookup_plants[i]);
                split.compute_tileBuildLookup.setBuffer("viewRenderData_quadLookup_" + idx, split.buffer_lookup_quads[i]);
            }

            // bicubic
            split.compute_tileBicubic.load("hlsl/terrain/compute_tileBicubic.hlsl");
            split.compute_tileBicubic.setSampler("linearSampler", sampler_Clamp);
            split.compute_tileBicubic.setTexture("gOutput", split.tileFbo->getColorTexture(0));
            split.compute_tileBicubic.setTexture("gOutputAlbedo", split.tileFbo->getColorTexture(1));
            split.compute_tileBicubic.setTexture("gOutputPermanence", split.tileFbo->getColorTexture(3));
            split.compute_tileBicubic.setTexture("gDebug", split.debug_texture);

            // ecotopes
            split.compute_tileEcotopes.load("hlsl/terrain/compute_tileEcotopes.hlsl");
            split.compute_tileEcotopes.setSampler("linearSampler", sampler_Clamp);
            split.compute_tileEcotopes.setTexture("gHeight", split.tileFbo->getColorTexture(0));
            split.compute_tileEcotopes.setTexture("gAlbedo", split.tileFbo->getColorTexture(1));
            split.compute_tileEcotopes.setTexture("gInPermanence", split.tileFbo->getColorTexture(3));
            split.compute_tileEcotopes.setTexture("gInEct_0", split.tileFbo->getColorTexture(4));
            split.compute_tileEcotopes.setTexture("gInEct_1", split.tileFbo->getColorTexture(5));
            split.compute_tileEcotopes.setTexture("gInEct_2", split.tileFbo->getColorTexture(6));
            split.compute_tileEcotopes.setTexture("gInEct_3", split.tileFbo->getColorTexture(7));
            split.compute_tileEcotopes.setTexture("gNoise", split.noise_u16);
            split.compute_tileEcotopes.setBuffer("tiles", split.buffer_tiles);
            split.compute_tileEcotopes.setBuffer("quad_instance", split.buffer_instance_quads);
            split.compute_tileEcotopes.setBuffer("feedback", split.buffer_feedback);

            // normals
            split.compute_tileNormals.load("hlsl/terrain/compute_tileNormals.hlsl");
            split.compute_tileNormals.setTexture("gInHgt", split.tileFbo->getColorTexture(0));
            split.compute_tileNormals.setTexture("gOutNormals", split.normals_texture);
            split.compute_tileNormals.setTexture("gOutput", split.debug_texture);
            split.compute_tileNormals.setBuffer("tiles", split.buffer_tiles);

            // vertices
            split.compute_tileVerticis.load("hlsl/terrain/compute_tileVertices.hlsl");
            split.compute_tileVerticis.setSampler("linearSampler", sampler_Clamp);
            split.compute_tileVerticis.setTexture("gInHgt", split.tileFbo->getColorTexture(0));
            split.compute_tileVerticis.setTexture("gOutVerts", split.vertex_A_texture);
            split.compute_tileVerticis.setTexture("gDebug", split.debug_texture);
            split.compute_tileVerticis.setBuffer("tileCenters", split.buffer_tileCenters);
            split.compute_tileVerticis.setBuffer("tiles", split.buffer_tiles);

            // jumpflood
            // It may even be faster to set this up twice and hop between the two
            split.compute_tileJumpFlood.load("hlsl/terrain/compute_tileJumpFlood.hlsl");
            split.compute_tileJumpFlood.setTexture("gDebug", split.debug_texture);

            // delaunay
            split.compute_tileDelaunay.load("hlsl/terrain/compute_tileDelaunay.hlsl");
            split.compute_tileDelaunay.setTexture("gInHgt", split.tileFbo->getColorTexture(0));
            split.compute_tileDelaunay.setTexture("gInVerts", split.vertex_B_texture);
            split.compute_tileDelaunay.setBuffer("VB", split.buffer_terrain);
            split.compute_tileDelaunay.setBuffer("tiles", split.buffer_tiles);

            // BC6H compressor
            split.bc6h_texture = ew::Texture::create2D(tile_numPixels / 4, tile_numPixels / 4, Diligent::TEX_FORMAT_RGBA32_UINT, 1, 1, nullptr, BIND_UNORDERED_ACCESS, "split.bc6h_texture");
            split.compute_bc6h.load("hlsl/terrain/compute_bc6h.hlsl");
            split.compute_bc6h.setTexture("gOutput", split.bc6h_texture);
        }

    }



    {
        allocateTiles(numTiles);

        // These LRU sizes are load-bearing: undersizing them crashes streaming
        // (a tile gets evicted and is then needed again at another resolution).
        elevationCache.resize(45);
        loadElevationHash(pRenderContext);

        imageCache.resize(45);
        loadImageHash(pRenderContext);

        init_TopdownRender();
    }

    terrafectorEditorMaterial::rootFolder = settings.dirResource + "/";
    // Normalize to forward slashes ONCE, here.
    // roadMaterialCache::find_insert_material prefix-compares cleaned paths
    // against this string VERBATIM, so a rootFolder with backslashes (which is
    // what std::filesystem hands us) makes EVERY road material fail the
    // rootFolder test and silently fall back to material 0.
    materialCache::cleanPath(terrafectorEditorMaterial::rootFolder);
    ecotopeSystem::resPath = settings.dirResource + "/";
    spdlog::info("terrain: terrafectorEditorMaterial::rootFolder = {}", terrafectorEditorMaterial::rootFolder);

    {
        // sb_vegetation_Materials must be created ZEROED - Diligent does not
        // zero-init, and the ribbon PS indexes it before any material has been
        // imported.
        std::vector<uint8_t> zeroMaterials(sizeof(sprite_material) * 1024 * 8, 0);
        _plantMaterial::static_materials_veg.sb_vegetation_Materials = ew::Buffer::createStructured(sizeof(sprite_material), 1024 * 8,
            BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS, zeroMaterials.data(), "sb_vegetation_Materials");      // just a lot


        plants_Root.envTexture = vegetation.envTexture;
        plants_Root.dappledLightTexture = vegetation.dappledLightTexture;


        plants_Root.onLoad();


        terrainSpiteShader.setBuffer("plant_buffer", plants_Root.plantData);
        terrainSpiteShader.setBuffer("materials", _plantMaterial::static_materials_veg.sb_vegetation_Materials);


        split.compute_clipLodAnimatePlants.setBuffer("block_buffer", plants_Root.blockData);
        split.compute_clipLodAnimatePlants.setBuffer("plant_buffer", plants_Root.plantData);
        split.compute_clipLodAnimatePlants.setBuffer("drawArgs_Plants", plants_Root.drawArgs_vegetation);
        split.compute_clipLodAnimatePlants.setBuffer("feedback_Veg", plants_Root.buffer_feedback);
        split.compute_clipLodAnimatePlants.setBuffer("instance_out", plants_Root.instanceData);

        split.compute_tilePassthrough.setBuffer("plant_buffer", plants_Root.plantData);

        split.compute_tileClear.setBuffer("feedback_Veg", plants_Root.buffer_feedback);
        split.compute_tileClear.setBuffer("DrawArgs_Plants", plants_Root.drawArgs_vegetation);
    }

    {
        mEcosystem.terrainSize = settings.size;
        // Data-driven: scan <dirRoot>/ecosystem/ for a *.ecosystem file. With
        // one, tiles get procedural albedo + billboard spawning; without one
        // numEcotopes stays 0 and the whole ecotope bake pass is dormant.
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(settings.dirRoot) / "ecosystem", ec))
        {
            if (entry.path().extension() == ".ecosystem")
            {
                spdlog::info("terrain: loading ecosystem '{}'", entry.path().string());
                mEcosystem.load(entry.path().string(), settings.dirResource + "/");
                break;
            }
        }
        if (mEcosystem.ecotopes.empty())
            spdlog::info("terrain: no *.ecosystem in '{}/ecosystem' - ecotope bake pass dormant (orthophoto albedo only)", settings.dirRoot);
    }

    {
        spdlog::info("terrain: terrafectors.loadPath '{}'", settings.dirRoot + "/terrafectors");
        terrafectors.loadPath(settings.dirRoot + "/terrafectors", settings.dirRoot + "/bake", false);
        mRoadNetwork.rootPath = settings.dirRoot + "/";
    }

    {
        // lastfile.road / lastfile.stamps are loaded at startup when they
        // exist, so the live bake has data to work with; a missing file is a
        // soft skip.
        //
        // bezierRoadstoLOD writes its EVO .gpu side-files into <dirRoot>/bake
        // AND performs the GPU upload of the LOD bins inside the same
        // `if (file)` blocks - so the directory must exist, or roads silently
        // never reach the LOD-binned bake buffers.
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(settings.dirRoot) / "bake", ec);

        if (!mRoadNetwork.lastUsedFilename.empty() && std::filesystem::exists(mRoadNetwork.lastUsedFilename))
        {
            spdlog::info("terrain: loading road network '{}'", mRoadNetwork.lastUsedFilename.string());
            mRoadNetwork.load(mRoadNetwork.lastUsedFilename, ROADNETWORK_CEREAL_VERSION);
            mRoadNetwork.updateAllRoads();      // -> isDirty, update() uploads + bins
        }
        else
        {
            spdlog::warn("terrain: no road network to load ('{}') - roads dormant", lastfile.road);
        }

        if (!mRoadStampCollection.lastUsedFilename.empty() && std::filesystem::exists(mRoadStampCollection.lastUsedFilename))
        {
            spdlog::info("terrain: loading stamps '{}'", mRoadStampCollection.lastUsedFilename.string());
            loadStamp();
        }
        else
        {
            spdlog::info("terrain: no stamp collection to load ('{}') - stamps dormant", lastfile.stamps);
        }
    }

    m_loaded = true;
}


void terrainManager::init_TopdownRender()
{
    using namespace Diligent;

    // sb_Terrafector_Materials must be created explicitly ZEROED. Diligent does
    // not zero-init default-heap buffers, and a garbage record reached through a
    // stray layer index could carry useElevation=1, which flattens the tile's
    // elevation to zero. A zeroed record is a provable no-op in the bake:
    // useElevation=0 -> Elevation=(0,a=0), useAlpha=0 -> alpha=1, so it touches
    // only the permanence RT.
    {
        std::vector<uint8_t> zeroMaterials(sizeof(TF_material) * 2048, 0);
        terrafectorEditorMaterial::static_materials.sb_Terrafector_Materials =
            ew::Buffer::createStructured(sizeof(TF_material), 2048,
                BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS, zeroMaterials.data(), "sb_Terrafector_Materials"); // FIXME hardcoded
    }

    split.shader_spline3D.load("hlsl/terrain/render_spline.hlsl", "vsMain", "psMain", ew::Topology::TriangleList);
    split.shader_spline3D.setBuffer("materials", terrafectorEditorMaterial::static_materials.sb_Terrafector_Materials);
    split.shader_spline3D.setSampler("gSmpLinear", sampler_Trilinear);

    split.shader_splineTerrafector.load("hlsl/terrain/render_splineTerrafector.hlsl", "vsMain", "psMain", ew::Topology::TriangleList);
    split.shader_splineTerrafector.setBuffer("materials", terrafectorEditorMaterial::static_materials.sb_Terrafector_Materials);
    // solveAlpha samples with world UVs, so this sampler has to wrap.
    split.shader_splineTerrafector.setSampler("gSmpLinear", sampler_Trilinear);


    // mesh terrafector shader
    split.shader_meshTerrafector.load("hlsl/terrain/render_meshTerrafector.hlsl", "vsMain", "psMain", ew::Topology::TriangleList);
    split.shader_meshTerrafector.setSampler("gSmpLinear", sampler_Trilinear);
    //split.shader_meshTerrafector.Vars()["PerFrameCB"]["gConstColor"] = false;
    split.shader_meshTerrafector.setFbo(split.tileFbo);
    split.shader_meshTerrafector.setBuffer("materials", terrafectorEditorMaterial::static_materials.sb_Terrafector_Materials);

    {
        DepthStencilStateDesc depthDesc;
        depthDesc.DepthEnable = True;
        depthDesc.DepthWriteEnable = False;
        depthDesc.StencilEnable = False;

        depthDesc.DepthFunc = COMPARISON_FUNC_GREATER;
        split.depthstateFuther = depthDesc;

        depthDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;
        split.depthstateCloser = depthDesc;

        depthDesc.DepthFunc = COMPARISON_FUNC_ALWAYS;
        split.depthstateAll = depthDesc;
    }

    // (rasterstateSplines - solid, cull NONE - and blendstateSplines are built
    // in onLoad, because the skydome needs them earlier.)

    // THE elevation blend trick: independent per-RT blend, with RT1-7 on
    // SrcAlpha/OneMinusSrcAlpha for colour AND alpha, and RT0 (the R32F
    // elevation) overridden to One/OneMinusSrcAlpha. The shader pre-multiplies
    // elevation by alpha, which lets one blend state serve three cases:
    //   relative material:  Elevation=(h*a, a=0)   -> dst += h*a   (additive)
    //   absolute material:  Elevation=(H*a, a=a)   -> dst = H*a+(1-a)*dst (feathered REPLACE)
    //   useElevation == 0:  Elevation=(0,   a=0)   -> dst unchanged (no-op)
    // Requires IndependentBlendEnable AND the IndependentBlend device feature,
    // which the app shell enables - Vulkan does NOT have it by default.
    {
        BlendStateDesc blendDesc;
        blendDesc.IndependentBlendEnable = True;
        for (int i = 0; i < 8; i++)
        {
            auto& rt = blendDesc.RenderTargets[i];
            rt.BlendEnable = True;
            rt.RenderTargetWriteMask = COLOR_MASK_ALL;
            rt.BlendOp = BLEND_OPERATION_ADD;
            rt.BlendOpAlpha = BLEND_OPERATION_ADD;
            rt.SrcBlend = BLEND_FACTOR_SRC_ALPHA;
            rt.DestBlend = BLEND_FACTOR_INV_SRC_ALPHA;
            rt.SrcBlendAlpha = BLEND_FACTOR_SRC_ALPHA;
            rt.DestBlendAlpha = BLEND_FACTOR_INV_SRC_ALPHA;
        }
        //??? hoekom het ek dit gedoen
        blendDesc.RenderTargets[0].SrcBlend = BLEND_FACTOR_ONE;
        blendDesc.RenderTargets[0].DestBlend = BLEND_FACTOR_INV_SRC_ALPHA;
        blendDesc.RenderTargets[0].SrcBlendAlpha = BLEND_FACTOR_ONE;
        blendDesc.RenderTargets[0].DestBlendAlpha = BLEND_FACTOR_INV_SRC_ALPHA;
        split.blendstateRoadsCombined = blendDesc;
    }

    // The spline buffers are created ZEROED for the same reason as the material
    // SB above: the bake draws index `iId + startOffset` records, and zero-init
    // turns any off-by-one read into a provable no-op (bezierIndex 0 with
    // material 0) instead of garbage geometry.
    {
        std::vector<uint8_t> zeroBez((size_t)sizeof(cubicDouble) * splines.maxBezier, 0);
        std::vector<uint8_t> zeroIdx((size_t)sizeof(bezierLayer) * splines.maxIndex * 2, 0);
        splines.bezierData = ew::Buffer::createStructured(sizeof(cubicDouble), splines.maxBezier, BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS, zeroBez.data(), "splines.bezierData");
        splines.indexData = ew::Buffer::createStructured(sizeof(bezierLayer), splines.maxIndex, BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS, zeroIdx.data(), "splines.indexData");
        splines.indexDataBakeOnly = ew::Buffer::createStructured(sizeof(bezierLayer), splines.maxIndex, BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS, zeroIdx.data(), "splines.indexDataBakeOnly");
        splines.indexData_LOD4 = ew::Buffer::createStructured(sizeof(bezierLayer), splines.maxIndex * 2, BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS, zeroIdx.data(), "splines.indexData_LOD4"); //*2 for safety
        splines.indexData_LOD6 = ew::Buffer::createStructured(sizeof(bezierLayer), splines.maxIndex * 2, BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS, zeroIdx.data(), "splines.indexData_LOD6"); //*2 for safety
        splines.indexData_LOD8 = ew::Buffer::createStructured(sizeof(bezierLayer), splines.maxIndex * 2, BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS, zeroIdx.data(), "splines.indexData_LOD8"); //*2 for safety   // 8Mb for now 1M points 8bytes per bez

        std::vector<uint8_t> zeroDynBez((size_t)sizeof(cubicDouble) * splines.maxDynamicBezier, 0);
        std::vector<uint8_t> zeroDynIdx((size_t)sizeof(bezierLayer) * splines.maxDynamicIndex, 0);
        splines.dynamic_bezierData = ew::Buffer::createStructured(sizeof(cubicDouble), splines.maxDynamicBezier, BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS, zeroDynBez.data(), "splines.dynamic_bezierData");
        splines.dynamic_indexData = ew::Buffer::createStructured(sizeof(bezierLayer), splines.maxDynamicIndex, BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS, zeroDynIdx.data(), "splines.dynamic_indexData");
    }
}


void terrainManager::allocateTiles(uint numT)
{
    quadtree_tile tile;

    m_tiles.clear();
    m_tiles.reserve(numT);
    split.cpuTiles.clear();
    split.cpuTiles.resize(numT);


    for (uint i = 0; i < numT; i++)
    {
        tile.init(i);
        m_tiles.push_back(tile);
    }

    reset();
}

void terrainManager::reset(bool _fullReset)
{
    fullResetDoNotRender = _fullReset;

    m_free.clear();
    m_used.clear();

    for (uint i = 0; i < m_tiles.size(); i++)
    {
        m_free.push_back(&m_tiles[i]);
    }

    quadtree_tile* root = m_free.front();
    m_free.pop_front();
    root->set(0, 0, 0, settings.size, float4(-0.5f * settings.size, 0, -0.5f * settings.size, 0.0f), nullptr);
    root->bornFrame = m_frameCounter;

    m_used.push_back(root);

    // FIXME this could be done faster by changing the cache compute shader to alwas clear if its Tile zero thats caching
    // or add a special shader for that
    if (split.buffer_tiles) {
        for (uint i = 0; i < m_tiles.size(); i++)
        {
            split.cpuTiles[i].lod = 0;
            split.cpuTiles[i].Y = 0;
            split.cpuTiles[i].X = 0;
            split.cpuTiles[i].flags = 0;

            split.cpuTiles[i].origin = float3(0, 0, 0);
            split.cpuTiles[i].scale_1024 = 0;

            split.cpuTiles[i].numQuads = 0;
            split.cpuTiles[i].numPlants = 0;
            split.cpuTiles[i].numTriangles = 0;
            split.cpuTiles[i].numVerticis = 0;
        }
        split.buffer_tiles->setBlob(split.cpuTiles.data(), 0, m_tiles.size() * sizeof(gpuTile));
    }
}

uint32_t getHashFromTileCoords(unsigned int lod, unsigned int y, unsigned int x) {
    return (lod << 28) + (y << 14) + (x);
}

void terrainManager::loadElevationHash(ew::GpuContext* pRenderContext)
{
    std::string fullpath = settings.dirRoot + "/elevations.txt";
    spdlog::info("terrain: loadElevationHash {}", fullpath);
    elevationTileHashmap.clear();
    reset();


    FILE* pFileHgt = fopen(fullpath.c_str(), "r");
    if (pFileHgt)
    {
        heightMap map;
        int texSize;
        char filename[256];
        int items = 1;
        uint linesProcessed = 0;
        do {
            items = fscanf(pFileHgt, "%u %u %u %d %f %f %f %f %f %255s\n", &map.lod, &map.y, &map.x, &texSize, &map.origin.x, &map.origin.y, &map.size, &map.hgt_offset, &map.hgt_scale, filename);
            if (items > 0)
            {
                linesProcessed++;
                uint32_t hash = getHashFromTileCoords(map.lod, map.y, map.x);

                fullpath = settings.dirRoot + "/" + filename;
                if (map.lod == 0)
                {
                    std::vector<float> data;
                    data.resize(static_cast<size_t>(texSize) * texSize * 2);  //the extra ios space for the miopmaps


                    FILE* pData = fopen(fullpath.c_str(), "rb");
                    if (pData)
                    {
                        fread(data.data(), sizeof(float), static_cast<size_t>(texSize) * texSize, pData);
                        fclose(pData);
                    }
                    else
                    {
                        requireFile(fullpath, "root elevation tile");
                    }
                    split.rootElevation = ew::Texture::create2D(texSize, texSize, Diligent::TEX_FORMAT_R32_FLOAT, 1, 8, data.data(), BIND_SHADER_RESOURCE | BIND_RENDER_TARGET, "split.rootElevation");
                    split.rootElevation->generateMips(pRenderContext);
                    map.hgt_offset = 0;
                    map.hgt_scale = 1;
                    elevationTileHashmap[hash] = map;
                }
                else
                {
                    map.filename = fullpath;
                    elevationTileHashmap[hash] = map;
                }
            }
        } while (items > 0);

        spdlog::info("terrain: elevations.txt - {} lines", linesProcessed);
        fclose(pFileHgt);
    }
    else
    {
        spdlog::error("terrain: cannot open '{}'", fullpath);
    }

}


void terrainManager::loadImageHash(ew::GpuContext* pRenderContext)
{
    (void)pRenderContext;
    std::string fullpath = settings.dirRoot + "/orthophotos.json";
    spdlog::info("terrain: loadImageHash {}", fullpath);
    imageDirectory.load(fullpath);
    imageDirectory.cache0(settings.dirRoot + "/orthophoto/");
    //and load 0, 0, 0
    if (imageDirectory.files.size() == 0)
        spdlog::warn("terrain: no orthophotos ({} missing or empty) - terrain renders from the bicubic clear colour", fullpath);
}



void terrainManager::onShutdown()
{
}



void jp2Map::set(uint _lod, uint _y, uint _x, float _wSize, float _wOffset)
{
    lod = _lod;
    y = _y;
    x = _x;

    float scale = 1.f / (float)pow(2, lod);
    float sizeT = _wSize * scale;
    float sizeTotal = sizeT * tile_toBorder;
    float sizeBorder = (sizeTotal - sizeT) / 2.f;

    size = sizeTotal;
    origin.x = _wOffset - sizeBorder + (x * sizeT);
    origin.y = _wOffset - sizeBorder + (y * sizeT);
}


void jp2Map::save(std::ofstream& _os)
{
    _os << lod << " " << y << " " << x << " " << origin.x << " " << origin.y << " " << size << " ";
    _os << hgt_offset << " " << hgt_scale << " " << fileOffset << "\n";
}

void jp2Map::saveBinary(std::ofstream& _os)
{
    _os << lod << y << x << origin.x << origin.y << size << hgt_offset << hgt_scale << fileOffset;
}

void jp2Map::loadBinary(std::ifstream& _is)
{
    _is >> lod >> y >> x >> origin.x >> origin.y >> size >> hgt_offset >> hgt_scale >> fileOffset;
}




void jp2File::save(std::ofstream& _os)
{
    _os << filename << "\n";
    for (auto& T : tiles)
    {
        T.save(_os);
    }
}

void jp2File::saveBinary(std::ofstream& _os)
{
    _os << filename << "\n";
    uint numTiles = (uint)tiles.size();
    _os << numTiles;
    for (auto& T : tiles)
    {
        T.saveBinary(_os);
    }
}

void jp2File::loadBinary(std::ifstream& _is)
{
    uint numTiles;
    _is >> filename;
    _is >> numTiles;
    for (uint i = 0; i < numTiles; i++)
    {
        tiles.emplace_back();
        tiles.back().loadBinary(_is);
    }
}





void jp2Dir::save(std::string _name)
{
    std::ofstream os(_name.c_str());
    if (os.good()) {
        cereal::JSONOutputArchive archive(os);
        serialize(archive);
    }
}

void jp2Dir::load(std::string _name)
{
    std::ifstream is(_name.c_str());
    if (is.good()) {
        cereal::JSONInputArchive archive(is);
        serialize(archive);
    }
    else
    {
        requireFile(_name, "orthophoto directory (orthophotos.json)");
    }

    fileHashmap.clear();
    tileHash.clear();
    for (size_t i = 0; i < files.size(); i++)
    {
        fileHashmap[files[i].hash] = (uint)i;


        //if (i == 0)// TMEP totdat ek .bin strem en cache
        {
            for (size_t j = 0; j < files[i].tiles.size(); j++)
            {
                uint32_t hash = getHashFromTileCoords(files[i].tiles[j].lod, files[i].tiles[j].y, files[i].tiles[j].x);
                tileHash[hash] = uint2(i, j);
            }
        }
    }

    cache.resize(40);   // can add up if I get this wrong
}


void jp2Dir::cache0(std::string _path)
{
    if (files.size() > 0)
    {
        path = _path;   // save for rest of session

        dataRoot.clear();
        dataRoot.resize(files[0].sizeInBytes);
        std::ifstream is(_path + files[0].filename, std::ios::binary);
        if (is.good()) {
            is.read((char*)dataRoot.data(), files[0].sizeInBytes);
            is.close();
        }
        else
        {
            spdlog::error("terrain: jp2Dir::cache0 cannot open '{}'", _path + files[0].filename);
        }
    }
}

//std::thread t1(imageDirectory.cacheHash(), hash);
void jp2Dir::cacheHash(uint32_t hash)
{
    if (hash == 0) return;  // because that is all pre loaded

    std::map<uint32_t, uint>::iterator file_it = fileHashmap.find(hash);
    if (file_it != fileHashmap.end())
    {
        // such a file exists
        uint idx = file_it->second;
        std::shared_ptr<std::vector<unsigned char>> data;
        if (!cache.get(hash, data))
        {
            // but not cahched, so cache it
            std::vector<unsigned char> dataRootLocal;
            dataRootLocal.resize(files[idx].sizeInBytes);
            std::ifstream is(path + files[idx].filename, std::ios::binary);
            if (is.good()) {
                is.read((char*)dataRootLocal.data(), files[idx].sizeInBytes);
                is.close();
            }
            auto share = std::make_shared<std::vector<unsigned char>>(dataRootLocal);
            cache.set(hash, share);

            spdlog::info("terrain: cacheHash() {} {}", path, files[idx].filename);
        }
    }
}


void jp2Dir::saveBinary(std::string _name)
{
    std::ofstream os(_name.c_str(), std::ios::binary);
    if (os.good()) {
        uint numFiles = (uint)files.size();
        os << numFiles;
        for (auto F : files)
        {
            F.saveBinary(os);
        }
    }
}

void jp2Dir::loadBinary(std::string _name)
{
    std::ifstream is(_name.c_str(), std::ios::binary);
    if (is.good()) {
        uint numFiles;
        is >> numFiles;
        for (uint i = 0; i < numFiles; i++)
        {
            files.emplace_back();
            files.back().loadBinary(is);
        }
    }
}




//mimic hlsl all()
static bool all(float4 in)
{
    if (in.x == 0) return false;
    if (in.y == 0) return false;
    if (in.z == 0) return false;
    if (in.w == 0) return false;
    return true;
}


// Frustum test used for tile culling and split decisions.
//
// A tile's bounding-sphere HEIGHT comes from an async GPU readback
// (tileCenters) and is inherited from the parent until the first patch lands
// (quadtree_tile::heightPatched). Testing an unpatched tile at that inherited
// height is what punched tile-shaped holes into the terrain: under readback
// starvation the wrong height persisted for seconds and the tile was culled
// while on screen. Until a tile's height is patched it is therefore tested as
// a COLUMN over the terrain's plausible elevation range: each plane distance
// is LINEAR in Y, so evaluating both endpoints and keeping the per-plane
// maximum is exact. Conservative by construction - an unpatched tile can never
// be culled by a wrong height, at the cost of processing a few off-screen
// tiles until its readback lands.
// Plane distances for the tile tested as a COLUMN over the terrain elevation
// range: each plane distance is LINEAR in Y, so evaluating both endpoints and
// keeping the per-plane maximum is exact.
static float4 tileColumnClip(const terrainCamera& _cam, const quadtree_tile& _tile)
{
    float4 lo = _tile.boundingSphere;
    float4 hi = _tile.boundingSphere;
    lo.y = kTerrainYMin;
    hi.y = kTerrainYMax;
    return glm::max((_cam.view * lo) * _cam.frustumMatrix,
                    (_cam.view * hi) * _cam.frustumMatrix);
}

static bool tileInFrustum(const terrainCamera& _cam, const quadtree_tile& _tile, float _radius)
{
    const float4 clip = _tile.heightPatched
        ? (_cam.view * _tile.boundingSphere) * _cam.frustumMatrix
        : tileColumnClip(_cam, _tile);
    return all(saturate(clip + float4(_radius, _radius, _radius, _radius)));
}


// FIXME NOT GREAT to rede every frame but also likely reaaly fast
void terrainManager::calculateSurfaceFlags()
{
    memset(frustumFlags, 0, sizeof(uint4) * 1024);		// clear all

    for (auto& tile : m_used)
    {
        frustumFlags[tile->index].x |= 1u << 31;   // just makr as active
        float boundingSphereSize = tile->size * 1.0f;// very generous but missing here is FATAL
        for (int i = 0; i < CameraType_MAX; i++) {
            if (cameraViews[i].bUse) {
                if (tileInFrustum(cameraViews[i], *tile, boundingSphereSize))
                {
                    frustumFlags[tile->index].y |= 1u << i;  // visible for plants

                    bool surface = tile->parent && tile->parent->main_ShouldSplit && tile->child[0] == nullptr;
                    // FIXME surface should depend on camera type, or mianshouldSplit should be per surface
                    if (surface)
                    {
                        frustumFlags[tile->index].x |= 1u << i;
                    }
                }
            }
        }
    }

    // Hole detector (purely additive - a separate read-only pass so the flag
    // logic above stays untouched; skipped entirely unless armed). Flags
    // CULLED SURFACE TILES - a leaf whose parent wants to split, i.e. a tile
    // that is supposed to be drawn - that fails the sphere test at its current
    // height although SOME height would put it in the frustum (column test).
    // With tileInFrustum's column fallback for unpatched tiles this should
    // stay at zero: any hit means a PATCHED tile with a wrong height, which
    // renders as a tile-shaped hole with the skybox behind it.
    if (ew::gDebug.holeStats.enabled && cameraViews[CameraType_Main_Center].bUse)
    {
        const terrainCamera& cam = cameraViews[CameraType_Main_Center];
        for (auto& tile : m_used)
        {
            if (!tile->parent || tile->child[0] || !tile->parent->main_ShouldSplit)
                continue;

            // Unpatched tiles are column-culled by tileInFrustum - a wrong
            // height cannot hide them any more.
            if (!tile->heightPatched)
                continue;

            const float tight = tile->size * 1.0f;

            const float4 clip = (cam.view * tile->boundingSphere) * cam.frustumMatrix;
            if (all(saturate(clip + float4(tight, tight, tight, tight))))
                continue;                       // rendered - not a hole

            if (!all(saturate(tileColumnClip(cam, *tile) + float4(tight, tight, tight, tight))))
                continue;                       // no height could make it visible

            // Culled at its current Y although some height would be visible.
            // The GPU-baked centre height decides the verdict: a mismatch
            // means the sphere provably sits at a stale/inherited height.
            ew::gDebug.holeStats.addHoleTile(tile->index, tile->lod, tile->boundingSphere.y, tile->size,
                                             split.tileCenters[tile->index].x);
        }
    }
}

void terrainManager::testForSurfaceEnv()
{
}


#define mainMaxLod 15
bool terrainManager::testForSplit(quadtree_tile* _tile)
{
    _tile->main_ShouldSplit = false;
    _tile->env_ShouldSplit = false;

    if (_tile->lod > mainMaxLod)
        return false;

    float boundingSphereSize = _tile->size * 0.9f;

    for (int i = 0; i < CameraType_MAX; i++) {
        if (cameraViews[i].bUse) {

            bool inFrust = tileInFrustum(cameraViews[i], *_tile, boundingSphereSize);

            float4 viewBS = cameraViews[i].view * _tile->boundingSphere;
            viewBS.w = 0;
            float distance = glm::length(viewBS) + 0.01f;
            float fovscale = glm::length(cameraViews[i].proj[0]);
            float m_halfAngle_to_Pixels = cameraViews[i].resolution * fovscale / 4.0f;
            float lod_Pix = _tile->size / distance * m_halfAngle_to_Pixels;

            // debug instrumentation (additive - ew::gDebug)
            if (i == CameraType_Main_Center)
            {
                ew::gDebug.live.splitMaxLodPix = std::max(ew::gDebug.live.splitMaxLodPix, lod_Pix);
                ew::gDebug.live.splitAnyInFrust = ew::gDebug.live.splitAnyInFrust || inFrust;
            }

            if (lod_Pix > 150 && inFrust)
            {
                _tile->main_ShouldSplit = true;
            }
            else if (lod_Pix > 300)
            {
                _tile->main_ShouldSplit = true;
            }
        }
    }

    if (_tile->main_ShouldSplit && !_tile->child[0]) {
        _tile->forSplit = true;
        ew::gDebug.live.splitCandidates++;
        return true;
    }



    return false;
}

bool terrainManager::testFrustum(quadtree_tile* _tile)
{
    (void)_tile;
    return true;
}

void terrainManager::markChildrenForRemove(quadtree_tile* _tile)
{
    for (uint i = 0; i < 4; i++) {
        if (_tile->child[i]) {
            markChildrenForRemove(_tile->child[i]);
            _tile->child[i]->forRemove = true;
            _tile->child[i] = nullptr;
        }
    }
}




void terrainManager::clearCameras()
{
    for (uint i = 0; i < CameraType_MAX; i++) {
        cameraViews[i].bUse = false;
    }
}

void terrainManager::setCamera(unsigned int _index, glm::mat4 viewMatrix, glm::mat4 projMatrix, float3 position, bool b_use, float _resolution)
{
    if (_index < CameraType_MAX)
    {
        cameraOrigin = position;	// SURE so last set camera does this, but they should just about all be the same right?

        cameraViews[_index].bUse = b_use;
        cameraViews[_index].resolution = _resolution;
        cameraViews[_index].view = viewMatrix;
        cameraViews[_index].proj = projMatrix;
        cameraViews[_index].viewProj = cameraViews[_index].view * cameraViews[_index].proj;
        cameraViews[_index].position = position;

        // remeber that these are transposed as well here
        cameraViews[_index].frustumPlane[0].x = cameraViews[_index].proj[0][3] + cameraViews[_index].proj[0][0];
        cameraViews[_index].frustumPlane[0].y = cameraViews[_index].proj[1][3] + cameraViews[_index].proj[1][0];
        cameraViews[_index].frustumPlane[0].z = cameraViews[_index].proj[2][3] + cameraViews[_index].proj[2][0];
        cameraViews[_index].frustumPlane[0].w = cameraViews[_index].proj[3][3] + cameraViews[_index].proj[3][0];

        cameraViews[_index].frustumPlane[1].x = cameraViews[_index].proj[0][3] - cameraViews[_index].proj[0][0];
        cameraViews[_index].frustumPlane[1].y = cameraViews[_index].proj[1][3] - cameraViews[_index].proj[1][0];
        cameraViews[_index].frustumPlane[1].z = cameraViews[_index].proj[2][3] - cameraViews[_index].proj[2][0];
        cameraViews[_index].frustumPlane[1].w = cameraViews[_index].proj[3][3] - cameraViews[_index].proj[3][0];

        cameraViews[_index].frustumPlane[2].x = cameraViews[_index].proj[0][3] + cameraViews[_index].proj[0][1];
        cameraViews[_index].frustumPlane[2].y = cameraViews[_index].proj[1][3] + cameraViews[_index].proj[1][1];
        cameraViews[_index].frustumPlane[2].z = cameraViews[_index].proj[2][3] + cameraViews[_index].proj[2][1];
        cameraViews[_index].frustumPlane[2].w = cameraViews[_index].proj[3][3] + cameraViews[_index].proj[3][1];

        cameraViews[_index].frustumPlane[3].x = cameraViews[_index].proj[0][3] - cameraViews[_index].proj[0][1];
        cameraViews[_index].frustumPlane[3].y = cameraViews[_index].proj[1][3] - cameraViews[_index].proj[1][1];
        cameraViews[_index].frustumPlane[3].z = cameraViews[_index].proj[2][3] - cameraViews[_index].proj[2][1];
        cameraViews[_index].frustumPlane[3].w = cameraViews[_index].proj[3][3] - cameraViews[_index].proj[3][1];

        cameraViews[_index].frustumMatrix = glm::mat4x4(
            cameraViews[_index].frustumPlane[0].x, cameraViews[_index].frustumPlane[0].y, cameraViews[_index].frustumPlane[0].z, cameraViews[_index].frustumPlane[0].w,
            cameraViews[_index].frustumPlane[1].x, cameraViews[_index].frustumPlane[1].y, cameraViews[_index].frustumPlane[1].z, cameraViews[_index].frustumPlane[1].w,
            cameraViews[_index].frustumPlane[2].x, cameraViews[_index].frustumPlane[2].y, cameraViews[_index].frustumPlane[2].z, cameraViews[_index].frustumPlane[2].w,
            cameraViews[_index].frustumPlane[3].x, cameraViews[_index].frustumPlane[3].y, cameraViews[_index].frustumPlane[3].z, cameraViews[_index].frustumPlane[3].w);
    }

    viewMask = 0;
    for (uint i = 0; i < CameraType_MAX; i++)
    {
        if (cameraViews[i].bUse)
        {
            viewMask |= 1 << i;
        }
    }
}



void terrainManager::updateShaderConstants(ew::Texture::SharedPtr _previousFrame, shaderLightBuffer _buffer)
{
    if (!m_loaded) return;

    // FXIME pretty sure we can do this in onLoad
    terrainShader.setTexture("terrainShadow", terrainShadowTexture);
    terrainSpiteShader.setTexture("terrainShadow", terrainShadowTexture);

    // JHFAA temporal feedback: billboards + vegetation ribbons lerp their soft
    // alpha edges toward LAST frame's half-res colour (render_Common.hlsli).
    terrainSpiteShader.setTexture("gPreviousFrame", _previousFrame);

    terrainShader.setVariable("LightsCB", "sunDirection", _buffer.sunDirection);
    terrainShader.setVariable("LightsCB", "sunRightVector", _buffer.sunRightVector);
    terrainShader.setVariable("LightsCB", "sunUpVector", _buffer.sunUpVector);
    terrainShader.setVariable("LightsCB", "screenSize", _buffer.screenSize);
    terrainShader.setVariable("LightsCB", "fog_far_Start", _buffer.fog_far_Start);
    terrainShader.setVariable("LightsCB", "fog_far_log_F", _buffer.fog_far_log_F);
    terrainShader.setVariable("LightsCB", "fog_far_one_over_k", _buffer.fog_far_one_over_k);

    terrainSpiteShader.setVariable("LightsCB", "sunDirection", _buffer.sunDirection);
    terrainSpiteShader.setVariable("LightsCB", "sunRightVector", _buffer.sunRightVector);
    terrainSpiteShader.setVariable("LightsCB", "sunUpVector", _buffer.sunUpVector);
    terrainSpiteShader.setVariable("LightsCB", "screenSize", _buffer.screenSize);
    terrainSpiteShader.setVariable("LightsCB", "fog_far_Start", _buffer.fog_far_Start);
    terrainSpiteShader.setVariable("LightsCB", "fog_far_log_F", _buffer.fog_far_log_F);
    terrainSpiteShader.setVariable("LightsCB", "fog_far_one_over_k", _buffer.fog_far_one_over_k);

    buildings.updateShaderConstants(terrainShadowTexture, _buffer);   // step-6 salvage

    plants_Root.updateShaderConstants(_previousFrame, terrainShadowTexture, _buffer);
}



bool terrainManager::update(ew::GpuContext* _renderContext)
{
    if (!m_loaded) return false;

    ++m_frameCounter;

    // Hole detector: start a fresh (uncommitted) record. The early-outs below
    // simply never commit it.
    ew::gDebug.holeStats.beginTerrainFrame(m_frameCounter);

    if ((terrainMode == _terrainMode::vegetation) ||
        (terrainMode == _terrainMode::glider) ||
        (terrainMode == _terrainMode::terrainBuilder) ||
        (terrainMode == _terrainMode::textureTool))
    {
        fullResetDoNotRender = false;
        ew::gDebug.live.updateEarlyOut = true;
        return false;
    }

    bool dirty = false;

    {
        // Road baking is driven from the debug panel. A toggle change only
        // affects NEWLY split tiles, which is what the panel's "rebake" button
        // (tfRebake) is for - it forces a full quadtree rebuild.
        if (bSplineAsTerrafector != ew::gDebug.toggles.tfBakeRoads)
        {
            bSplineAsTerrafector = ew::gDebug.toggles.tfBakeRoads;
            spdlog::info("terrain: bSplineAsTerrafector -> {} (affects newly split tiles; use 'rebake' to rebuild)", bSplineAsTerrafector);
        }
        showRoadSpline = ew::gDebug.toggles.tfShowRoadSpline;
        if (ew::gDebug.toggles.tfRebake)
        {
            ew::gDebug.toggles.tfRebake = false;
            reset(true);
        }

        if (mEcosystem.change) {
            mEcosystem.change = false;
            reset(true);
        }

        {
            //fprintf(terrafectorSystem::_logfile, "update_roads\n");
            //fflush(terrafectorSystem::_logfile);
            updateDynamicRoad(false);
            mRoadNetwork.testHit(split.feedback.tum_Position);


            if (mRoadNetwork.isDirty)
            {
                splines.numStaticSplines = __min((uint)roadNetwork::staticBezierData.size(), splines.maxBezier);
                splines.numStaticSplinesIndex = __min((uint)roadNetwork::staticIndexData.size(), splines.maxIndex);
                if (splines.numStaticSplines > 0)
                {
                    splines.bezierData->setBlob(roadNetwork::staticBezierData.data(), 0, (uint64_t)splines.numStaticSplines * sizeof(cubicDouble));
                    splines.indexData->setBlob(roadNetwork::staticIndexData.data(), 0, (uint64_t)splines.numStaticSplinesIndex * sizeof(bezierLayer));
                    mRoadNetwork.isDirty = false;
                }
                splines.numStaticSplinesBakeOnlyIndex = __min((uint)roadNetwork::staticIndexData_BakeOnly.size(), splines.maxIndex);
                if (splines.numStaticSplinesBakeOnlyIndex > 0)
                {
                    splines.indexDataBakeOnly->setBlob(roadNetwork::staticIndexData_BakeOnly.data(), 0, (uint64_t)splines.numStaticSplinesBakeOnlyIndex * sizeof(bezierLayer));
                    mRoadNetwork.isDirty = false;
                }

                bezierRoadstoLOD(4);
                mRoadNetwork.isDirty = false;

                spdlog::info("terrain: road splines uploaded - {} beziers, {} layers, {} bake-only layers",
                             splines.numStaticSplines, splines.numStaticSplinesIndex, splines.numStaticSplinesBakeOnlyIndex);
            }

            ew::gDebug.live.staticSplines = splines.numStaticSplinesIndex;
            ew::gDebug.live.dynamicSplines = splines.numDynamicSplinesIndex;
        }

        {
            for (auto& tile : m_used)
            {
                tile->reset();
            }

            for (auto& tile : m_used)
            {
                dirty |= testForSplit(tile);

                if (!tile->main_ShouldSplit && tile->child[0]) {
                    markChildrenForRemove(tile);
                }
            }

            if (!dirty) fullResetDoNotRender = false;

            for (auto itt = m_used.begin(); itt != m_used.end();)               // do al merges
            {
                if ((*itt)->forRemove) {
                    (*itt)->forRemove = false;
                    m_free.push_back(*itt);
                    itt = m_used.erase(itt);
                    ew::gDebug.holeStats.current.merges++;
                }
                else {
                    ++itt;
                }
            }
        }
    }

    splitOne(_renderContext);

    ew::gDebug.live.tilesUsed = (uint32_t)m_used.size();
    ew::gDebug.live.tilesFree = (uint32_t)m_free.size();
    ew::gDebug.live.cameraMainInUse = cameraViews[CameraType_Main_Center].bUse;




    //if (dirty)    // for now update every frame - its fast
    {
        {
            // Readback of the tile centers through a fence-checked latency
            // ring rather than a stalling map(Read): these values feed a
            // refinement loop, so 1-2 frames of lag are invisible.
            split.buffer_tileCenter_readback->enqueueCopy(_renderContext, split.buffer_tileCenters, m_frameCounter);
            const void* pTileData = split.buffer_tileCenter_readback->mapCompleted(_renderContext);
            // Starvation instrumentation (readback_starvation_handoff §3):
            // record WHICH null path fired, per frame, for holes.txt.
            ew::gDebug.holeStats.current.mapFailReason =
                static_cast<uint8_t>(split.buffer_tileCenter_readback->lastMapFail());
            ew::gDebug.live.tileRbCalls   = split.buffer_tileCenter_readback->mapCalls();
            ew::gDebug.live.tileRbOk      = split.buffer_tileCenter_readback->mapOk();
            ew::gDebug.live.tileRbNoSlot  = split.buffer_tileCenter_readback->mapFailNoSlot();
            ew::gDebug.live.tileRbMapBusy = split.buffer_tileCenter_readback->mapFailBusy();
            if (const void* pData = pTileData)
            {
                // Age of the mapped data: pool indices are recycled on split/
                // merge, so data older than a tile's allocation belongs to the
                // slot's PREVIOUS occupant - patching from it puts the
                // bounding sphere at a foreign height and the frustum test
                // culls a perfectly visible tile (bottom-of-screen flicker).
                const uint32_t dataFrame = static_cast<uint32_t>(split.buffer_tileCenter_readback->completedTag());
                ew::gDebug.holeStats.current.readbackMapped = true;
                ew::gDebug.holeStats.current.readbackLag    = m_frameCounter - dataFrame;
                std::memcpy(split.tileCenters.data(), pData, sizeof(float4) * numTiles);
                split.buffer_tileCenter_readback->unmap(_renderContext);
                for (auto& tile : m_used)
                {
                    // Hole detector: which tiles kept a stale bounding sphere,
                    // and why (no GPU centre yet vs. age-gated by bornFrame).
                    if (split.tileCenters[tile->index].x <= 0)
                        ew::gDebug.holeStats.current.nonPositiveX++;
                    else if (tile->bornFrame >= dataFrame)
                        ew::gDebug.holeStats.current.bornGuardSkips++;

                    if (split.tileCenters[tile->index].x > 0 && tile->bornFrame < dataFrame)
                    {
                        tile->origin.y = split.tileCenters[tile->index].x;           // THIS is very wrong, .x contains the middl;em but i also think its unused
                        tile->boundingSphere.y = split.tileCenters[tile->index].x;
                        tile->heightPatched = true;
                    }
                }
            }
        }

        split.compute_tileClear.dispatch(_renderContext, 1, 1);


        calculateSurfaceFlags();



        split.compute_tileBuildLookup.setBlob("gConstants", frustumFlags, 1024 * sizeof(uint4));	// FIXME number of tiles
        uint cnt = (numTiles + 31) >> 5;
        {
            split.compute_tileBuildLookup.dispatch(_renderContext, cnt, 1);
        }
        //FIXME this of coarse adds it in teh worst fashion, interleaving 64 triangles or quads of diffirent tiles with one another
        // see what to do diffirent VERY bad for materials and texture reads

        // FIXME THIS SHPULD BE IN RENDER - its not update specific but camera specific

    }

    // Hole detector: commit the record for this frame (no-op unless armed).
    ew::gDebug.holeStats.endTerrainFrame((uint32_t)m_used.size());


    if (hasChanged) {
        hasChanged = false;
        return true;
    }
    return false;
}







void terrainManager::hashAndCache_Thread(quadtree_tile* pTile)
{
    auto start = high_resolution_clock::now();

    if (jphData.size() < 1048576) jphData.resize(1048576);

    ojph::codestream codestream;
    ojph::j2c_infile j2c_file;
    requireFile(elevationTileHashmap[pTile->elevationHash].filename, "elevation jp2 tile");
    j2c_file.open(elevationTileHashmap[pTile->elevationHash].filename.c_str());
    codestream.enable_resilience();
    codestream.set_planar(false);
    codestream.read_headers(&j2c_file);
    codestream.create();
    ojph::ui32 next_comp = 0;   // OpenJPH pull() takes ojph::ui32&



    for (int i = 0; i < 1024; ++i)
    {
        ojph::line_buf* line = codestream.pull(next_comp);
        int32_t* dp = line->i32;
        for (int j = 0; j < 1024; j++) {
            int16_t val = (int16_t)*dp++;
            jphData[i * 1024 + j] = val;
        }
    }
    codestream.close();
    auto start_b = high_resolution_clock::now();

    auto stop = high_resolution_clock::now();
    stream.terrainCacheTime = (double)duration_cast<microseconds>(stop - start).count() / 1000.;
    stream.terrainCacheJPHTime = (double)duration_cast<microseconds>(stop - start_b).count() / 1000.;
    hashCount = 2;
    cacheHash = pTile->elevationHash;
}



bool terrainManager::hashAndCache(quadtree_tile* pTile)
{
    if (hashCount == 2)
    {
        textureCacheElement map;
        map.texture = ew::Texture::create2D(1024, 1024, Diligent::TEX_FORMAT_R16_UNORM, 1, 1, jphData.data(), BIND_SHADER_RESOURCE, "elevation tile");
        elevationCache.set(cacheHash, map);
        cacheTexture.reset();
        hashCount = 0;
    }

    uint32_t hash = getHashFromTileCoords(pTile->lod, pTile->y, pTile->x);
    std::map<uint32_t, heightMap>::iterator it = elevationTileHashmap.find(hash);
    if (it != elevationTileHashmap.end()) {
        pTile->elevationHash = hash;
    }

    if (pTile->elevationHash > 0)
    {
        textureCacheElement map;
        if (!elevationCache.get(pTile->elevationHash, map))
        {
            if (hashCount == 0)
            {
                hashCount++;
                hashFuture = std::async(std::launch::async, &terrainManager::hashAndCache_Thread, this, pTile);
            }
            return false;
        }
        else
        {

            split.compute_tileBicubic.setTexture("gInput", map.texture);
            return true;
        }
    }

    return true;
}



void terrainManager::hashAndCacheImages_Thread(quadtree_tile* pTile)
{
    if (imageDirectory.files.size() == 0) return;

    auto start = high_resolution_clock::now();

    if (jphImageData.size() < 1024 * 1024 * 4) jphImageData.resize(1024 * 1024 * 4);
    std::shared_ptr<std::vector<unsigned char>> dataCache;

    ojph::codestream codestream;
    ojph::mem_infile j2c_file;
    std::map<uint32_t, uint2>::iterator itH = imageDirectory.tileHash.find(pTile->imageHash);
    if (itH == imageDirectory.tileHash.end()) {
        spdlog::error("terrain: FAILED tileHash.find");
        hashCountImage = 2;     // fail soft rather than dereference end()
        cacheHashImage = pTile->imageHash;
        return;
    }
    jp2File& file = imageDirectory.files[itH->second.x];
    jp2Map& mapTile = imageDirectory.files[itH->second.x].tiles[itH->second.y];

    if (itH->second.x == 0)
    {
        j2c_file.open(&imageDirectory.dataRoot[mapTile.fileOffset], mapTile.sizeInBytes);
    }
    else
    {
        // TODO: the failure path only logs - j2c_file is never opened, yet
        // read_headers/create below still run on it.
        if (imageDirectory.cache.get(file.hash, dataCache))
        {
            j2c_file.open(dataCache->data() + mapTile.fileOffset, mapTile.sizeInBytes);
        }
        else
        {
            spdlog::error("terrain: FIX imageCache.resize(55);  its still too small");
            spdlog::error("terrain: offset {}, lod {}, {}", mapTile.fileOffset, mapTile.lod, file.filename);
            spdlog::error("terrain: itH file {}, tile {}", itH->second.x, itH->second.y);
            // "Bug is likely here at the ned void jp2Dir::load(). The thing that
            // changed is that I changed FOV, so diffirent amount of tiles vissible"
        }
    }
    codestream.enable_resilience();
    codestream.set_planar(false);
    codestream.read_headers(&j2c_file);
    codestream.create();
    ojph::ui32 next_comp = 0;   // OpenJPH pull() takes ojph::ui32&



    for (int i = 0; i < 1024; i++)
    {
        for (int j = 0; j < 3; j++)
        {
            ojph::line_buf* line = codestream.pull(next_comp);
            int32_t* dp = line->i32;
            for (int k = 0; k < 1024; k++)
            {
                int DATA = *dp;
                jphImageData[(i * 1024 * 4) + (k * 4) + next_comp] = (unsigned char)__min(255, __max(0, DATA));
                dp++;
            }
        }
    }
    codestream.close();
    auto start_b = high_resolution_clock::now();

    auto stop = high_resolution_clock::now();
    stream.imageCacheTime = (double)duration_cast<microseconds>(stop - start).count() / 1000.;
    stream.imageCacheJPHTime = (double)duration_cast<microseconds>(stop - start_b).count() / 1000.;
    hashCountImage = 2;
    cacheHashImage = pTile->imageHash;
}

/*  The weird carsh comes from my cache being too small, so tiles gets deleted tehn needed at another resolution
*/
bool terrainManager::hashAndCacheImages(quadtree_tile* pTile)
{
    if (imageDirectory.files.size() == 0) return true;

    if (hashCountImage == 2)
    {
        textureCacheElement map;
        map.texture = ew::Texture::create2D(1024, 1024, Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB, 1, 1, jphImageData.data(), BIND_SHADER_RESOURCE, "ortho tile");
        imageCache.set(cacheHashImage, map);
        cacheTextureImage.reset();
        hashCountImage = 0;
    }

    uint32_t hash = getHashFromTileCoords(pTile->lod, pTile->y, pTile->x);

    // First load the JP2 Data, BUT 0,0,0 IS  PRELOADED, thisis n own thread so should look like no time
    auto start_c = high_resolution_clock::now();
    imageDirectory.cacheHash(hash);
    auto stop_c = high_resolution_clock::now();
    double time = (double)duration_cast<microseconds>(stop_c - start_c).count() / 1000.;
    if (time > 1.f)     stream.imageCacheIOTime = time;

    std::map<uint32_t, uint2>::iterator it = imageDirectory.tileHash.find(hash);
    if (it != imageDirectory.tileHash.end()) {
        pTile->imageHash = hash;
    }

    {
        textureCacheElement map;
        if (!imageCache.get(pTile->imageHash, map))     // so we dont have teh image on GPU
        {
            if (hashCountImage == 0)
            {
                hashCountImage++;
                hashIFuture = std::async(std::launch::async, &terrainManager::hashAndCacheImages_Thread, this, pTile);
            }
            return false;
        }
        else
        {
            split.compute_tileBicubic.setTexture("gInputAlbedo", map.texture);
            return true;
        }
    }
}



void terrainManager::setChild(quadtree_tile* pTile, int y, int x)
{
    int childIdx = (y << 1) + x;
    float origX = pTile->size / 2.0f * x;
    float origY = pTile->size / 2.0f * y;

    pTile->child[childIdx] = m_free.front();
    m_free.pop_front();
    pTile->child[childIdx]->set(pTile->lod + 1, pTile->x * 2 + x, pTile->y * 2 + y, pTile->size / 2.0f, pTile->origin + float4(origX, 0, origY, 0), pTile);
    pTile->child[childIdx]->bornFrame = m_frameCounter;
    m_used.push_back(pTile->child[childIdx]);
    pTile->child[childIdx]->elevationHash = pTile->elevationHash;
    pTile->child[childIdx]->imageHash = pTile->imageHash;
}


void terrainManager::splitOne(ew::GpuContext* _renderContext)
{
    /* Bloody hell, pick the best one to do*/
    if (m_free.size() < 8)
    {
        ew::gDebug.live.splitBlockedFree = true;
        return;
    }


    // FIXME PICK A BETTER ONE HERE
    for (auto& tile : m_used)
    {
        if (tile->forSplit)
        {
            bool dataReady = true;
            {
                hasChanged = true;
                dataReady &= hashAndCache(tile);
            }

            {
                dataReady &= hashAndCacheImages(tile);
            }

            if (dataReady)
            {
                setChild(tile, 0, 0);
                setChild(tile, 0, 1);
                setChild(tile, 1, 0);
                setChild(tile, 1, 1);

                {
                    tileForSplit children[4];
                    for (int i = 0; i < 4; i++) {
                        children[i].index = tile->child[i]->index;
                        children[i].lod = tile->child[i]->lod;
                        children[i].y = tile->child[i]->y;
                        children[i].x = tile->child[i]->x;

                        children[i].origin = tile->child[i]->origin;
                        children[i].scale = tile->child[i]->size;
                    }

                    // Raw cbuffer blob - this layout must match the HLSL
                    // cbuffer exactly. It is THE split-critical upload.
                    split.compute_tileSplitMerge.setBlob("gConstants", children, 4 * sizeof(tileForSplit));
                    split.compute_tileSplitMerge.dispatch(_renderContext, 1, 1);
                }


                {
                    for (int i = 0; i < 4; i++) {
                        splitChild(tile->child[i], _renderContext);
                        testForSplit(tile->child[i]);		// so its frustum flags are set
                    }
                }
                ew::gDebug.live.splitsPerformed++;
                ew::gDebug.holeStats.current.splits++;
                return;
            }

            ew::gDebug.live.splitBlockedData++;
        }
    }
}

void terrainManager::splitChild(quadtree_tile* _tile, ew::GpuContext* _renderContext)
{
    const uint32_t cs_w = tile_numPixels / tile_cs_ThreadSize;
    const float2 origin = float2(_tile->origin.x, _tile->origin.z);
    const float outerSize = _tile->size * tile_numPixels / tile_InnerPixels;
    const float pixelSize = outerSize / tile_numPixels;
    float halfsize = ecotopeSystem::terrainSize / 2.f;


    {
        // Not nessesary but nice where we lack data for now
        _renderContext->clearFbo(split.tileFbo.get(), glm::vec4(0.3f, 0.3f, 0.3f, 1.0f), 1.0f, 0, ew::FboAttachmentType::All);

        _renderContext->clearRtv(split.tileFbo->getRenderTargetView(3), glm::vec4(1.0f, 0.07f, 1.0f, 0.0f)); // PBR

        _renderContext->clearRtv(split.tileFbo->getRenderTargetView(4), glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
        _renderContext->clearRtv(split.tileFbo->getRenderTargetView(5), glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
        _renderContext->clearRtv(split.tileFbo->getRenderTargetView(6), glm::vec4(1.0f, 1.0f, 0.0f, 0.0f));
        _renderContext->clearRtv(split.tileFbo->getRenderTargetView(7), glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
    }


    {
        //bicubic
        heightMap& elevationMap = elevationTileHashmap[_tile->elevationHash];

        float2 bicubicOffset = (origin - elevationMap.origin) / elevationMap.size;
        float S = pixelSize / elevationMap.size;
        float2 bicubicSize = float2(S, S);

        if (_tile->elevationHash == 0)
        {
            split.compute_tileBicubic.setTexture("gInput", split.rootElevation);
        }
        split.compute_tileBicubic.setVariable("gConstants", "offset", bicubicOffset);
        split.compute_tileBicubic.setVariable("gConstants", "size", bicubicSize);
        split.compute_tileBicubic.setVariable("gConstants", "hgt_offset", elevationMap.hgt_offset);
        split.compute_tileBicubic.setVariable("gConstants", "hgt_scale", elevationMap.hgt_scale);
        split.compute_tileBicubic.setVariable("gConstants", "isHeight", (int)1);
        split.compute_tileBicubic.dispatch(_renderContext, cs_w, cs_w);
    }

    if (imageDirectory.files.size() > 0)
    {
        // copy the image tiles to diffuse
        // TODO: this lookup is dereferenced without comparing against end(), so a
        // missing imageHash is undefined behaviour. hashAndCacheImages_Thread does
        // check the same lookup and logs the failure.
        std::map<uint32_t, uint2>::iterator itH = imageDirectory.tileHash.find(_tile->imageHash);
        jp2Map& mapTile = imageDirectory.files[itH->second.x].tiles[itH->second.y];

        float S = pixelSize / mapTile.size;
        split.compute_tileBicubic.setVariable("gConstants", "offset", (origin - mapTile.origin) / mapTile.size);
        split.compute_tileBicubic.setVariable("gConstants", "size", float2(S, S));
        split.compute_tileBicubic.setVariable("gConstants", "isHeight", (int)0);
        split.compute_tileBicubic.dispatch(_renderContext, cs_w, cs_w);
    }

    {
        // The terrafector/road/stamp topdown bake, which REWRITES elevation.
        // Its position in the chain is load-bearing: after bicubic, before the
        // height_Array copy and everything downstream, because ecotopes /
        // normals / vertices all re-derive origin.y from the CENTRE of the
        // baked elevation.
        splitRenderTopdown(_tile, _renderContext);
        _renderContext->copySubresource(height_Array.get(), _tile->index, split.tileFbo->getColorTexture(0).get(), 0);  // for picking only
    }

    {
        {
            //ecotope
            heightMap& rootMap = elevationTileHashmap[0];
            split.compute_tileEcotopes.setTexture("gLowresHgt", split.rootElevation);
            split.compute_tileEcotopes.setBuffer("plantIndex", mEcosystem.getPLantBuffer());
            split.compute_tileEcotopes.setBuffer("plantDensity", mEcosystem.getPLantDesityBuffer());

            // bad herer but lets set the textures
            // gmyTextures_T is one flat Texture2D[256]: albedo in slots [0..11],
            // noise in [12..23], unused slots fall back to the layer's dummy.
            {
                std::vector<ew::Texture::SharedPtr> ecotopeTextures(256);
                for (size_t i = 0; i < mEcosystem.ecotopes.size() && i < 12; i++)
                {
                    ecotopeTextures[i] = mEcosystem.ecotopes[i].texAlbedo;
                    ecotopeTextures[12 + i] = mEcosystem.ecotopes[i].texNoise;
                }
                split.compute_tileEcotopes.setTextureArray("gmyTextures_T", ecotopeTextures);
            }

            ecotopeGpuConstants C = *mEcosystem.getConstants();
            C.pixelSize = pixelSize;
            C.tileXY = int2(_tile->x, _tile->y);
            C.padd2 = float2(rootMap.hgt_offset, rootMap.hgt_scale);
            C.lowResSize = _tile->size / settings.size / 248.0f;
            C.lowResOffset = float2(_tile->origin.x + halfsize, _tile->origin.z + halfsize) / settings.size;
            C.lod = _tile->lod;
            C.tileIndex = _tile->index;
            // Raw cbuffer blob - layout must match the HLSL cbuffer exactly.
            split.compute_tileEcotopes.setBlob("gConstants", &C, sizeof(ecotopeGpuConstants));
            if (C.numEcotopes > 0)
            {
                split.compute_tileEcotopes.dispatch(_renderContext, cs_w, cs_w);
            }
        }

        {
            //passthrough
            uint cnt = (numQuadsPerTile) >> 8;	  // FIXME - hiesdie oordoen is es dan stadig - dit behoort Compute indoirect te wees  en die regte getal te gebruik
            split.compute_tilePassthrough.setVariable("gConstants", "parent_index", _tile->parent->index);
            split.compute_tilePassthrough.setVariable("gConstants", "child_index", _tile->index);
            split.compute_tilePassthrough.setVariable("gConstants", "dX", _tile->x & 0x1);
            split.compute_tilePassthrough.setVariable("gConstants", "dY", _tile->y & 0x1);
            split.compute_tilePassthrough.dispatch(_renderContext, cnt, 1);
        }

        {
            // Do this early to avoid stalls
            _renderContext->copyResource(split.vertex_B_texture.get(), split.vertex_clear.get());			// not 100% sure this clear is needed
            _renderContext->copyResource(split.vertex_A_texture.get(), split.vertex_preload.get());
        }

        {
            // compress and copy colour data
            // (albedo BC6H compression is deliberately disabled)
            _renderContext->copySubresource(compressed_Albedo_Array.get(), _tile->index, split.tileFbo->getColorTexture(1).get(), 0);
        }

        {
            //normals
            split.compute_tileNormals.setVariable("gConstants", "pixSize", pixelSize);
            split.compute_tileNormals.dispatch(_renderContext, cs_w, cs_w);
        }

        {
            //verticies
            float scale = 1.0f;
            if (_tile->lod < 7)  scale = 1.3f;
            if (_tile->lod == 13)  scale = 1.2f;
            if (_tile->lod == 14)  scale = 1.5f;
            if (_tile->lod == 15)  scale = 2.0f;
            if (_tile->lod >= 16)  scale = 3.2f;
            scale *= 2.5;

            split.compute_tileVerticis.setSampler("linearSampler", sampler_Clamp);
            split.compute_tileVerticis.setVariable("gConstants", "constants", float4(pixelSize * scale, 0, 0, _tile->index));
            split.compute_tileVerticis.dispatch(_renderContext, cs_w / 2, cs_w / 2);
        }

        {
            //copy normals
            _renderContext->copySubresource(compressed_Normals_Array.get(), _tile->index, split.normals_texture.get(), 0);
        }

        // jumpflood algorithm (1+JFA+1) tp build voroinoi diagram ------------------------------------------------------------------------
        // ek weet 32 en 6 loops is goed
        {
            uint step = 4;
            for (int j = 0; j < 3; j++) {
                split.compute_tileJumpFlood.setVariable("gConstants", "step", step);
                if (j & 0x1) {
                    split.compute_tileJumpFlood.setTexture("gInVerts", split.vertex_B_texture);
                    split.compute_tileJumpFlood.setTexture("gOutVerts", split.vertex_A_texture);
                }
                else {
                    split.compute_tileJumpFlood.setTexture("gInVerts", split.vertex_A_texture);
                    split.compute_tileJumpFlood.setTexture("gOutVerts", split.vertex_B_texture);

                }

                split.compute_tileJumpFlood.dispatch(_renderContext, cs_w / 2, cs_w / 2);
                step /= 2;
                if (step < 1) step = 1;
            }
        }

        {
            //copy_PBR
            split.compute_bc6h.setTexture("gSource", split.tileFbo->getColorTexture(2));
            split.compute_bc6h.dispatch(_renderContext, cs_w / 4, cs_w / 4);
            _renderContext->copySubresource(compressed_PBR_Array.get(), _tile->index, split.bc6h_texture.get(), 0);
        }


        {
            //delaunay
            // No UAV-counter reset here - no shader increments buffer_terrain's
            // counter, so it has none.
            split.compute_tileDelaunay.setVariable("gConstants", "tile_Index", _tile->index);
            split.compute_tileDelaunay.dispatch(_renderContext, cs_w / 2, cs_w / 2);
        }
    }


}



void terrainManager::splitRenderTopdown(quadtree_tile* _pTile, ew::GpuContext* _renderContext)
{
    //FALCOR_PROFILE("renderTopdown");

    // Debug probe, part 1: snapshot the elevation centre AFTER bicubic and
    // BEFORE any terrafector draw, to compare against the post-bake value at
    // the end of this function. A good bicubic height collapsing to ~0 under a
    // terrafector footprint is the classic elevation-blend failure.
    float probeCenterBefore = 0.f;
    const bool probeActive = ew::gDebug.toggles.tfBakeElevationStatsLeft > 0;
    if (probeActive)
        probeCenterBefore = _renderContext->debugReadTexelR32F(split.tileFbo->getColorTexture(0).get(), tile_numPixels / 2, tile_numPixels / 2);

    // set up the camera -----------------------
    float s = _pTile->size / 2.0f;
    float x = _pTile->origin.x + s;
    float z = _pTile->origin.z + s;
    glm::mat4 V, P, VP;
    V[0] = glm::vec4(1, 0, 0, 0);
    V[1] = glm::vec4(0, 0, 1, 0);
    V[2] = glm::vec4(0, -1, 0, 0);
    V[3] = glm::vec4(-x, z, 0, 1);

    s *= 256.0f / 248.0f;
    P = glm::orthoLH(-s, s, -s, s, -10000.0f, 10000.0f);

    //viewproj = view * proj;
    VP = P * V;    //??? order

    // Bake-camera matrix convention:
    //   * the glm columns above are the mathematical view [X->x, Z->y (negated
    //     through the translation sign), Y->depth], and orthoLH under
    //     GLM_FORCE_DEPTH_ZERO_TO_ONE gives [0,1] depth. VP = P*V is the
    //     mathematical clip transform: clip = VP * world.
    //   * the bake shaders do HLSL's row-vector `mul(float4(posW,1), viewproj)`
    //     with column-major cbuffer packing, so the upload convention is
    //     glm::transpose(M) - the same convention ewCamera.h uses.
    // Getting this wrong sends every bake vertex out of clip space, which shows
    // up as an EMPTY bake (terrain stays bicubic-only), not as a distorted one.
    glm::mat4 viewproj = glm::transpose(VP);


    {
        split.shader_meshTerrafector.setFbo(split.tileFbo);
        split.shader_meshTerrafector.setRasterizerState(split.rasterstateSplines);
        split.shader_meshTerrafector.setBlendState(ew::gDebug.toggles.tfBakeNoElevationBlend
            ? split.blendstateSplines : split.blendstateRoadsCombined);
        split.shader_meshTerrafector.setDepthStencilState(split.depthstateAll);

        split.shader_meshTerrafector.setVariable("gConstantBuffer", "viewproj", viewproj);
        split.shader_meshTerrafector.setVariable("gConstantBuffer", "overlayAlpha", 1.0f);

        terrafectorEditorMaterial::static_materials.setTextures(split.shader_meshTerrafector);
    }

    if (bSplineAsTerrafector)           // Now render the roadNetwork
    {
        split.shader_splineTerrafector.setFbo(split.tileFbo);
        split.shader_splineTerrafector.setRasterizerState(split.rasterstateSplines);
        split.shader_splineTerrafector.setDepthStencilState(split.depthstateAll);
        split.shader_splineTerrafector.setBlendState(ew::gDebug.toggles.tfBakeNoElevationBlend
            ? split.blendstateSplines : split.blendstateRoadsCombined);

        split.shader_splineTerrafector.setVariable("gConstantBuffer", "viewproj", viewproj);
        split.shader_splineTerrafector.setVariable("gConstantBuffer", "startOffset", (uint)0);
        split.shader_splineTerrafector.setBuffer("materials", terrafectorEditorMaterial::static_materials.sb_Terrafector_Materials);

        terrafectorEditorMaterial::static_materials.setTextures(split.shader_splineTerrafector);

        split.shader_splineTerrafector.setBuffer("splineData", splines.bezierData);     // not created yet
    }


    // Mesh bake low
    if (gis_overlay.bakeBakeOnlyData)
    {
        if (_pTile->lod >= 4 && ew::gDebug.toggles.tfStageBakeLow)
        {
            gpuTileTerrafector* tile = terrafectorSystem::loadCombine_LOD4_bakeLow.getTile((_pTile->y >> (_pTile->lod - 4)) * 16 + (_pTile->x >> (_pTile->lod - 4)));
            if (tile)
            {
                if (tile->numBlocks > 0)
                {
                    split.shader_meshTerrafector.setBuffer("vertexData", tile->vertex);
                    split.shader_meshTerrafector.setBuffer("indexData", tile->index);
                    split.shader_meshTerrafector.drawInstanced(_renderContext, 128 * 3, tile->numBlocks);
                }
            }
        }

        if (bSplineAsTerrafector && ew::gDebug.toggles.tfStageRoadBakeOnly)           // Now render the roadNetwork
        {
            split.shader_splineTerrafector.setBuffer("indexData", splines.indexDataBakeOnly);
            split.shader_splineTerrafector.drawIndexedInstanced(_renderContext, 64 * 6, splines.numStaticSplinesBakeOnlyIndex);
        }

        if (_pTile->lod >= 4 && ew::gDebug.toggles.tfStageBakeHigh)
        {
            gpuTileTerrafector* tile = terrafectorSystem::loadCombine_LOD4_bakeHigh.getTile((_pTile->y >> (_pTile->lod - 4)) * 16 + (_pTile->x >> (_pTile->lod - 4)));
            if (tile)
            {
                if (tile->numBlocks > 0)
                {
                    split.shader_meshTerrafector.setBuffer("vertexData", tile->vertex);
                    split.shader_meshTerrafector.setBuffer("indexData", tile->index);
                    split.shader_meshTerrafector.drawInstanced(_renderContext, 128 * 3, tile->numBlocks);
                }
            }
        }
    }





    if (ew::gDebug.toggles.tfStageMeshes)
    {
    if (_pTile->lod >= 6)
    {
        gpuTileTerrafector* tile = terrafectorSystem::loadCombine_LOD6.getTile((_pTile->y >> (_pTile->lod - 6)) * 64 + (_pTile->x >> (_pTile->lod - 6)));
        if (tile)
        {
            if (tile->numBlocks > 0)
            {
                split.shader_meshTerrafector.setBuffer("vertexData", tile->vertex);
                split.shader_meshTerrafector.setBuffer("indexData", tile->index);
                split.shader_meshTerrafector.drawInstanced(_renderContext, 128 * 3, tile->numBlocks);
            }
        }
    }
    else if (_pTile->lod >= 4)
    {
        gpuTileTerrafector* tile = terrafectorSystem::loadCombine_LOD4.getTile((_pTile->y >> (_pTile->lod - 4)) * 16 + (_pTile->x >> (_pTile->lod - 4)));
        if (tile)
        {
            if (tile->numBlocks > 0)
            {
                split.shader_meshTerrafector.setBuffer("vertexData", tile->vertex);
                split.shader_meshTerrafector.setBuffer("indexData", tile->index);
                split.shader_meshTerrafector.drawInstanced(_renderContext, 128 * 3, tile->numBlocks);
            }
        }
    }
    else if (_pTile->lod >= 2)
    {
        gpuTileTerrafector* tile = terrafectorSystem::loadCombine_LOD2.getTile((_pTile->y >> (_pTile->lod - 2)) * 4 + (_pTile->x >> (_pTile->lod - 2)));
        if (tile)
        {
            if (tile->numBlocks > 0)
            {
                split.shader_meshTerrafector.setBuffer("vertexData", tile->vertex);
                split.shader_meshTerrafector.setBuffer("indexData", tile->index);
                split.shader_meshTerrafector.drawInstanced(_renderContext, 128 * 3, tile->numBlocks);
            }
        }
    }
    }


    // OVER:AY ######################################################
    if (gis_overlay.terrafectorOverlayStrength > 0 && ew::gDebug.toggles.tfStageOverlay)
        if (_pTile->lod >= 4)
        {
            gpuTileTerrafector* tile = terrafectorSystem::loadCombine_LOD4_overlay.getTile((_pTile->y >> (_pTile->lod - 4)) * 16 + (_pTile->x >> (_pTile->lod - 4)));
            if (tile)
            {
                if (tile->numBlocks > 0)
                {
                    split.shader_meshTerrafector.setVariable("gConstantBuffer", "overlayAlpha", gis_overlay.terrafectorOverlayStrength);
                    split.shader_meshTerrafector.setBuffer("vertexData", tile->vertex);
                    split.shader_meshTerrafector.setBuffer("indexData", tile->index);
                    split.shader_meshTerrafector.drawInstanced(_renderContext, 128 * 3, tile->numBlocks);
                }
            }
        }


    //??? should probably be in the roadnetwork code, but look at the optimize step first
    if (bSplineAsTerrafector && ew::gDebug.toggles.tfStageRoadBins)           // Now render the roadNetwork
    {
        if (_pTile->lod >= 8)
        {
            quadtree_tile* P8 = _pTile;
            while (P8->lod > 8) P8 = P8->parent;
            split.shader_splineTerrafector.setVariable("gConstantBuffer", "startOffset", splines.startOffset_LOD8[P8->y][P8->x]);
            split.shader_splineTerrafector.setBuffer("indexData", splines.indexData_LOD8);
            split.shader_splineTerrafector.drawIndexedInstanced(_renderContext, 64 * 6, splines.numIndex_LOD8[P8->y][P8->x]);
        }
        else if (_pTile->lod >= 6)
        {
            quadtree_tile* P6 = _pTile;
            while (P6->lod > 6) P6 = P6->parent;
            split.shader_splineTerrafector.setVariable("gConstantBuffer", "startOffset", splines.startOffset_LOD6[P6->y][P6->x]);
            split.shader_splineTerrafector.setBuffer("indexData", splines.indexData_LOD6);
            split.shader_splineTerrafector.drawIndexedInstanced(_renderContext, 64 * 6, splines.numIndex_LOD6[P6->y][P6->x]);
        }
        else if (_pTile->lod >= 4)
        {
            quadtree_tile* P4 = _pTile;
            while (P4->lod > 4) P4 = P4->parent;
            split.shader_splineTerrafector.setVariable("gConstantBuffer", "startOffset", splines.startOffset_LOD4[P4->y][P4->x]);
            split.shader_splineTerrafector.setBuffer("indexData", splines.indexData_LOD4);
            split.shader_splineTerrafector.drawIndexedInstanced(_renderContext, 64 * 6, splines.numIndex_LOD4[P4->y][P4->x]);
        }
    }


    // STAMPS #################################################################################################################
    if (_pTile->lod >= 7 && ew::gDebug.toggles.tfStageStamps)
    {
        gpuTileTerrafector* tile = terrafectorSystem::loadCombine_LOD7_stamps.getTile((_pTile->y >> (_pTile->lod - 7)) * 128 + (_pTile->x >> (_pTile->lod - 7)));
        if (tile)
        {
            if (tile->numBlocks > 0)
            {
                split.shader_meshTerrafector.setBuffer("vertexData", tile->vertex);
                split.shader_meshTerrafector.setBuffer("indexData", tile->index);
                split.shader_meshTerrafector.drawInstanced(_renderContext, 128 * 3, tile->numBlocks);
            }
        }
    }



    // TOP #################################################################################################################
    if (ew::gDebug.toggles.tfStageTop)
    {
    if (_pTile->lod >= 6)
    {
        gpuTileTerrafector* tile = terrafectorSystem::loadCombine_LOD6_top.getTile((_pTile->y >> (_pTile->lod - 6)) * 64 + (_pTile->x >> (_pTile->lod - 6)));
        if (tile)
        {
            if (tile->numBlocks > 0)
            {
                split.shader_meshTerrafector.setBuffer("vertexData", tile->vertex);
                split.shader_meshTerrafector.setBuffer("indexData", tile->index);
                split.shader_meshTerrafector.drawInstanced(_renderContext, 128 * 3, tile->numBlocks);
            }
        }
    }
    else if (_pTile->lod >= 4)
    {
        gpuTileTerrafector* tile = terrafectorSystem::loadCombine_LOD4_top.getTile((_pTile->y >> (_pTile->lod - 4)) * 16 + (_pTile->x >> (_pTile->lod - 4)));
        if (tile)
        {
            if (tile->numBlocks > 0)
            {
                split.shader_meshTerrafector.setBuffer("vertexData", tile->vertex);
                split.shader_meshTerrafector.setBuffer("indexData", tile->index);
                split.shader_meshTerrafector.drawInstanced(_renderContext, 128 * 3, tile->numBlocks);
            }
        }
    }
    }

    // Debug probe, part 2 - a FULL GPU STALL, so it only runs while the panel
    // counter is above zero. Compares the elevation centre before/after the bake
    // and logs the tile. A bicubic height of hundreds of metres collapsing to ~0
    // is the elevation-blend failure signature; MATERIAL_SOLID roads legitimately
    // REPLACE height with the road surface, but stay in the terrain's range.
    if (probeActive)
    {
        ew::gDebug.toggles.tfBakeElevationStatsLeft--;
        const float after = _renderContext->debugReadTexelR32F(split.tileFbo->getColorTexture(0).get(), tile_numPixels / 2, tile_numPixels / 2);
        ew::gDebug.live.tfProbeLod = _pTile->lod;
        ew::gDebug.live.tfProbeBefore = probeCenterBefore;
        ew::gDebug.live.tfProbeAfter = after;
        spdlog::info("TFBAKE probe tile lod {} ({},{}): elevation centre {:.2f} -> {:.2f}{}",
                     _pTile->lod, _pTile->x, _pTile->y, probeCenterBefore, after,
                     (fabs(probeCenterBefore) > 50.f && fabs(after) < 1.f) ? "  <-- y=0 SIGNATURE" : "");
    }
}



void terrainManager::onFrameRender(ew::GpuContext* _renderContext, const ew::Fbo::SharedPtr& _fbo, ew::Camera::SharedPtr _camera)
{
    if (!m_loaded) return;

    // Upload convention (ewCamera.h): HLSL does mul(float4(pos,1), M) with
    // column-major cbuffer packing, so every matrix goes up transposed.
    glm::mat4 view = glm::transpose(_camera->getViewMatrix());
    glm::mat4 proj = glm::transpose(_camera->getProjMatrix());
    glm::mat4 viewproj = glm::transpose(_camera->getViewProjMatrix());

    if (terrainMode == _terrainMode::vegetation)
    {
        // Single-plant editor preview path - unreachable while onLoad forces a
        // terrain-rendering mode.
        {
            //skydome - veg
            triangleShader.setFbo(_fbo);
            triangleShader.setVariable("gConstantBuffer", "viewproj", viewproj);
            triangleShader.setVariable("gConstantBuffer", "eye", _camera->getPosition());
            triangleShader.setVariable("gConstantBuffer", "useSkyDome", (int)0);
            triangleShader.setRasterizerState(split.rasterstateSplines);
            triangleShader.setBlendState(split.blendstateSplines);
            triangleShader.drawInstanced(_renderContext, 36, 1);
            ew::gDebug.live.skydomeDraws++;
        }

        glm::mat4 clipPlanes = glm::transpose(cameraViews[CameraType_Main_Center].frustumMatrix);

        float fovscale = glm::length(cameraViews[CameraType_Main_Center].proj[1]);
        float m_halfAngle_to_Pixels = cameraViews[CameraType_Main_Center].resolution * fovscale / 2.f;


        plants_Root.render(_renderContext, _fbo, viewproj, _camera->getPosition(), view, clipPlanes, m_halfAngle_to_Pixels);



        return;
    }


    if (ew::gDebug.toggles.plants)
    {
        //PLANTS_clip_lod: tile plant instances -> visible plant_instance/
        //block/drawArgs, dispatched INDIRECTLY per lookup block.
        glm::mat4 clipLodView = glm::transpose(cameraViews[1].view);
        glm::mat4 clipLodClip = glm::transpose(cameraViews[1].frustumMatrix);

        float fovscale = glm::length(cameraViews[CameraType_Main_Center].proj[1]);
        float m_halfAngle_to_Pixels = cameraViews[CameraType_Main_Center].resolution * fovscale / 2.f;

        split.compute_clipLodAnimatePlants.setVariable("gConstantBuffer", "view", clipLodView);
        split.compute_clipLodAnimatePlants.setVariable("gConstantBuffer", "clip", clipLodClip);
        split.compute_clipLodAnimatePlants.setVariable("gConstantBuffer", "halfAngle_to_Pixels", m_halfAngle_to_Pixels);
        split.compute_clipLodAnimatePlants.setBuffer("tileLookup", split.buffer_lookup_plants[CameraType_Main_Center]);
        split.compute_clipLodAnimatePlants.dispatchIndirect(_renderContext, split.dispatchArgs_plants.get(), 0);//225
    }

    if (ew::gDebug.toggles.terrainTiles)
    {
        terrainShader.setFbo(_fbo);
        terrainShader.setVariable("gConstantBuffer", "view", view);
        terrainShader.setVariable("gConstantBuffer", "proj", proj);
        terrainShader.setVariable("gConstantBuffer", "viewproj", viewproj);
        terrainShader.setVariable("gConstantBuffer", "eye", _camera->getPosition());
        terrainShader.setVariable("PerFrameCB", "gConstColor", (int)(ew::gDebug.toggles.terrainConstColor ? 1 : 0));
        terrainShader.setBuffer("tileLookup", split.buffer_lookup_terrain[CameraType_Main_Center]);      // FIXME set the ccorrect view not 0
        terrainShader.renderIndirect(_renderContext, split.drawArgs_tiles, nullptr, CameraType_Main_Center, 1);
        ew::gDebug.live.terrainTileDraws++;
    }

    {
        // Far-LOD buildings (step-6 salvage), culled against the visible tile
        // rects. calculateSurfaceFlags() re-runs here exactly like the legacy
        // integration did - update() may have early-outed for the current
        // mode, and stale frustum flags would cull live building chunks.
        // PORT-REVIEW: the function-local static matches the legacy shape
        // (single renderer instance; avoids a per-frame allocation).
        calculateSurfaceFlags();
        static std::vector<float4> visibleTileRects;
        getVisibleTileRects(visibleTileRects);
        buildings.render(_renderContext, _fbo, view, viewproj, _camera->getPosition(),
                         visibleTileRects);
    }

    if (ew::gDebug.toggles.billboards)
    {
        {
            terrainSpiteShader.setVariable("gConstantBuffer", "alpha_pass", (int)0);
            terrainSpiteShader.setTexture("gEnv", vegetation.envTexture);

            // FIXME need a way to do only on change
            _plantMaterial::static_materials_veg.setTextures(terrainSpiteShader);

        }
        {
            //billboards
            terrainSpiteShader.setFbo(_fbo);
            terrainSpiteShader.setVariable("gConstantBuffer", "viewproj", viewproj);
            terrainSpiteShader.setVariable("gConstantBuffer", "eye", _camera->getPosition());

            glm::mat4 viewPlain = _camera->getViewMatrix();
            glm::vec3 D = glm::vec3(viewPlain[2][0], viewPlain[2][1], -viewPlain[2][2]);
            glm::vec3 R = glm::normalize(glm::cross(glm::vec3(0, 1, 0), D));
            terrainSpiteShader.setVariable("gConstantBuffer", "right", R);

            terrainSpiteShader.setBuffer("tileLookup", split.buffer_lookup_quads[CameraType_Main_Center]);

            terrainSpiteShader.renderIndirect(_renderContext, split.drawArgs_quads, nullptr, CameraType_Main_Center, 1);
            ew::gDebug.live.billboardDraws++;
        }
    }

    if (ew::gDebug.toggles.plants)
    {
        //vegetation ribbons - terrain-driven mode
        glm::mat4 clip = glm::transpose(cameraViews[CameraType_Main_Center].frustumMatrix);
        float fovscale = glm::length(cameraViews[CameraType_Main_Center].proj[1]);
        float m_halfAngle_to_Pixels = cameraViews[CameraType_Main_Center].resolution * fovscale / 2.f;
        plants_Root.render(_renderContext, _fbo, viewproj, _camera->getPosition(), view, clip, m_halfAngle_to_Pixels, true);

        // vegetation counters for the debug panel
        ew::gDebug.live.vegInstances = plants_Root.feedback.numInstanceAddedComputeClipLod;
        ew::gDebug.live.vegBlocks = plants_Root.feedback.numBlocks;
        ew::gDebug.live.vegBillboards = plants_Root.feedback.numBillboard;
        ew::gDebug.live.vegFrustDiscard = plants_Root.feedback.numFrustDiscard;
        ew::gDebug.live.vegFeedbackAge = plants_Root.feedbackAgeFrames;
    }

    if (ew::gDebug.toggles.skydome)
    {
        //skydome - drawn BEFORE the spline overlays, which alpha-blend over it
        triangleShader.setFbo(_fbo);
        triangleShader.setVariable("gConstantBuffer", "viewproj", viewproj);
        triangleShader.setVariable("gConstantBuffer", "eye", _camera->getPosition());
        triangleShader.setVariable("gConstantBuffer", "useSkyDome", (int)0);
        triangleShader.setRasterizerState(split.rasterstateSplines);
        triangleShader.setBlendState(split.blendstateSplines);
        triangleShader.drawInstanced(_renderContext, 36, 1);
        ew::gDebug.live.skydomeDraws++;
    }

    if (terrainMode == _terrainMode::terrafector && ew::gDebug.toggles.splines)
    {
        // dynamic stamp preview
        split.shader_spline3D.setFbo(_fbo);
        split.shader_spline3D.setRasterizerState(split.rasterstateSplines);
        split.shader_spline3D.setBlendState(split.blendstateSplines);
        split.shader_spline3D.setDepthStencilState(split.depthstateAll);

        split.shader_spline3D.setVariable("gConstantBuffer", "viewproj", viewproj);
        split.shader_spline3D.setVariable("gConstantBuffer", "alpha", gis_overlay.splineOverlayStrength);

        split.shader_spline3D.setBuffer("materials", terrafectorEditorMaterial::static_materials.sb_Terrafector_Materials);

        terrafectorEditorMaterial::static_materials.setTextures(split.shader_spline3D);

        split.shader_spline3D.setBuffer("splineData", splines.dynamic_bezierData);
        split.shader_spline3D.setBuffer("indexData", splines.dynamic_indexData);
        split.shader_spline3D.drawIndexedInstanced(_renderContext, 64 * 6, splines.numDynamicStampIndex);
        if (splines.numDynamicStampIndex > 0) ew::gDebug.live.splineDraws++;
    }

    if ((splines.numStaticSplines || splines.numDynamicSplines) && showRoadSpline && !bSplineAsTerrafector && ew::gDebug.toggles.splines)
    {
        // 3D road overlay - an editing visualization, and the fastest "did the
        // road data load" check there is: translucent road ribbons floating
        // over the terrain.
        split.shader_spline3D.setFbo(_fbo);
        split.shader_spline3D.setRasterizerState(split.rasterstateSplines);
        split.shader_spline3D.setBlendState(split.blendstateSplines);
        split.shader_spline3D.setDepthStencilState(split.depthstateAll);

        //split.shader_spline3D.Vars()["gConstantBuffer"]["view"] = view;
        //split.shader_spline3D.Vars()["gConstantBuffer"]["proj"] = proj;
        split.shader_spline3D.setVariable("gConstantBuffer", "viewproj", viewproj);
        split.shader_spline3D.setVariable("gConstantBuffer", "alpha", gis_overlay.splineOverlayStrength);

        split.shader_spline3D.setBuffer("materials", terrafectorEditorMaterial::static_materials.sb_Terrafector_Materials);

        terrafectorEditorMaterial::static_materials.setTextures(split.shader_spline3D);

        if (splines.numDynamicSplines > 0)
        {
            split.shader_spline3D.setBuffer("splineData", splines.dynamic_bezierData);
            split.shader_spline3D.setBuffer("indexData", splines.dynamic_indexData);
            split.shader_spline3D.drawIndexedInstanced(_renderContext, 64 * 6, splines.numDynamicSplinesIndex);
        }
        else if (splines.numStaticSplines > 0)
        {
            split.shader_spline3D.setBuffer("splineData", splines.bezierData);
            split.shader_spline3D.setBuffer("indexData", splines.indexData);
            split.shader_spline3D.drawIndexedInstanced(_renderContext, 64 * 6, splines.numStaticSplinesIndex);
        }
        ew::gDebug.live.splineDraws++;
    }

    {
        //terrain_under_mouse
        compute_TerrainUnderMouse.setVariable("gConstants", "mousePos", mousePosition);
        compute_TerrainUnderMouse.setVariable("gConstants", "mouseDir", mouseDirection);
        compute_TerrainUnderMouse.setVariable("gConstants", "mouseCoords", mouseCoord);
        if (_fbo->getColorTexture(0))
            compute_TerrainUnderMouse.setTexture("gHDRBackbuffer", _fbo->getColorTexture(0));
        // When _fbo is the swap-chain proxy there is no colour texture to bind
        // and the layer's dummy fallback covers gHDRBackbuffer - it only feeds
        // the debug colourUnderMouse.

        // This compute samples the texture that is still bound as the scene
        // render target, so unbind first: Diligent would otherwise do it
        // implicitly and log an Info message every frame.
        _renderContext->unbindRenderTargets();

        compute_TerrainUnderMouse.dispatch(_renderContext, 1, 1);

        split.buffer_feedback_read->enqueueCopy(_renderContext, split.buffer_feedback);
        if (const void* pData = split.buffer_feedback_read->mapCompleted(_renderContext))
        {
            std::memcpy(&split.feedback, pData, sizeof(GC_feedback));
            split.buffer_feedback_read->unmap(_renderContext);
        }
        // Second-ring health mirror (step 6): does GC_feedback starve like
        // tileCenters (systemic) or not (per-buffer)?
        ew::gDebug.live.gcRbCalls   = split.buffer_feedback_read->mapCalls();
        ew::gDebug.live.gcRbOk      = split.buffer_feedback_read->mapOk();
        ew::gDebug.live.gcRbNoSlot  = split.buffer_feedback_read->mapFailNoSlot();
        ew::gDebug.live.gcRbMapBusy = split.buffer_feedback_read->mapFailBusy();

        // GPU-side proof of what buildLookup packed for the main view.
        ew::gDebug.live.gpuTerrainTiles = split.feedback.numTerrainTiles[CameraType_Main_Center];
        ew::gDebug.live.gpuTerrainBlocks = split.feedback.numTerrainBlocks[CameraType_Main_Center];
        ew::gDebug.live.gpuTerrainTris = split.feedback.numTerrainVerts[CameraType_Main_Center];
        ew::gDebug.live.gpuQuads = split.feedback.numQuads[CameraType_Main_Center];

        mouse.hit = false;
        if (split.feedback.tum_idx > 0)
        {
            // Only the picking state is updated here - the sample's
            // FirstPersonCamera drives the camera.
            mouse.hit = true;
            mouse.terrain = split.feedback.tum_Position;
            mouse.cameraHeight = split.feedback.heightUnderCamera;
            mouse.toGround = mouse.terrain - _camera->getPosition();
            mouse.mouseToHeightRatio = glm::length(mouse.toGround) / (_camera->getPosition().y - mouse.cameraHeight);

            mouse.pan = mouse.terrain;
            mouse.panDistance = glm::length(mouse.toGround);
            mouse.orbit = mouse.terrain;
        }
    }

    if (debug && ew::gDebug.toggles.debugEarthworksShader)     // gated: these 9 blits clutter the screen
    {
        glm::vec4 srcRect = glm::vec4(0, 0, tile_numPixels, tile_numPixels);
        glm::vec4 dstRect;

        for (uint32_t i = 0; i < 8; i++)
        {
            dstRect = glm::vec4(250 + i * 150, 60, 250 + i * 150 + 128, 60 + 128);
            if (split.tileFbo->getColorTexture(i) && _fbo->getRenderTargetView(0))
                _renderContext->blit(split.tileFbo->getColorTexture(i)->getSRV(), _fbo->getRenderTargetView(0), srcRect, dstRect, true);
        }

        dstRect = glm::vec4(250 + 8 * 150, 60, 250 + 8 * 150 + tile_numPixels * 2, 60 + tile_numPixels * 2);
        if (_fbo->getRenderTargetView(0))
            _renderContext->blit(split.debug_texture->getSRV(), _fbo->getRenderTargetView(0), srcRect, dstRect, false);
    }
}


void terrainManager::updateDynamicStamp()
{
}


void terrainManager::stamp_to_Bezier(stamp& S, cubicDouble* BEZ, bezierLayer* IDX, int _index)
{
    float3 A = S.pos - S.right - S.dir;
    float3 B = S.pos + S.right - S.dir;
    float3 C = S.pos - S.right + S.dir;
    float3 D = S.pos + S.right + S.dir;

    BEZ->data[0][0] = { A, 0 };
    BEZ->data[0][1] = { A, 0 };
    BEZ->data[0][2] = { C, 1 };
    BEZ->data[0][3] = { C, 1 };

    BEZ->data[1][0] = { B, 0 };
    BEZ->data[1][1] = { B, 0 };
    BEZ->data[1][2] = { D, 1 };
    BEZ->data[1][3] = { D, 1 };

    *IDX = bezierLayer(bezier_edge::outside, bezier_edge::center, S.material, _index, true, 0, 0);
    IDX->B |= 0x1u << 29; // isQuad
}


void terrainManager::currentStamp_to_Bezier()
{
    splineTest.bSegment = false;
    splineTest.bVertex = false;
    splineTest.testDistance = 1000;
    splineTest.bCenter = false;
    splineTest.cornerNum = -1;
    splineTest.pos = split.feedback.tum_Position;
    splineTest.bStreetview = false;

    mCurrentStamp.material = terrafectorEditorMaterial::static_materials.selectedMaterial;
    int matIdx = mCurrentStamp.material;
    if (matIdx >= 0)
    {
        terrafectorEditorMaterial mat = terrafectorEditorMaterial::static_materials.materialVector[matIdx];

        float3 N = split.feedback.tum_Normal;
        mCurrentStamp.right = { cos(mCurrentStamp.rotation), 0, -sin(mCurrentStamp.rotation) };
        mCurrentStamp.dir = glm::normalize(glm::cross(mCurrentStamp.right, N)) * 0.5f * mat.stampHeight * mCurrentStamp.scale.y;
        mCurrentStamp.right = glm::normalize(glm::cross(N, mCurrentStamp.dir)) * 0.5f * mat.stampWidth * mCurrentStamp.scale.x;

        mCurrentStamp.pos = split.feedback.tum_Position + (N * mCurrentStamp.height);

        // FIXME scale dir and right here

        cubicDouble BEZ;
        bezierLayer IDX;
        stamp_to_Bezier(mCurrentStamp, &BEZ, &IDX, 0);

        splines.numDynamicStampIndex = 1;
        splines.dynamic_bezierData->setBlob(&BEZ, 0, sizeof(cubicDouble));
        splines.dynamic_indexData->setBlob(&IDX, 0, sizeof(bezierLayer));
    }
    else
    {
        splines.numDynamicStampIndex = 0;
    }

}


void terrainManager::allStamps_to_Bezier()
{
    /*
    int size = mRoadStampCollection.stamps.size();
    roadNetwork::staticBezierData.resize(size);
    roadNetwork::staticIndexData.resize(size);

    for (int i = 0; i < size; i++)
    {
        stamp_to_Bezier(mRoadStampCollection.stamps[i], &roadNetwork::staticBezierData.at(i), &roadNetwork::staticIndexData.at(i), i);
    }

    splines.numStaticSplines = size;
    splines.numStaticSplinesIndex = size;
    splines.bezierData->setBlob(roadNetwork::staticBezierData.data(), 0, splines.numStaticSplines * sizeof(cubicDouble));
    splines.indexData->setBlob(roadNetwork::staticIndexData.data(), 0, splines.numStaticSplinesIndex * sizeof(bezierLayer));

    allStamps_to_Terrafector();
    */
}

void terrainManager::allStamps_to_Terrafector()
{
    terrafectorSystem::loadCombine_LOD7_stamps.create(7);   // the create clears it
    lodTriangleMesh lodder_stamp;
    lodder_stamp.create(7);

    float3 pos[3];
    float2 uv[3];
    for (auto& S : mRoadStampCollection.stamps)
    {
        if (S.scale.x != 0)
        {
            pos[0] = S.pos - S.right - S.dir;
            pos[1] = S.pos + S.right - S.dir;
            pos[2] = S.pos + S.right + S.dir;
            uv[0] = { 0, 0 };
            uv[1] = { 1, 0 };
            uv[2] = { 1, 1 };
            lodder_stamp.insertTriangle(S.material, pos, uv);

            pos[0] = S.pos - S.right - S.dir;
            pos[1] = S.pos + S.right + S.dir;
            pos[2] = S.pos - S.right + S.dir;
            uv[0] = { 0, 0 };
            uv[1] = { 1, 1 };
            uv[2] = { 0, 1 };
            lodder_stamp.insertTriangle(S.material, pos, uv);
        }
    }

    terrafectorSystem::loadCombine_LOD7_stamps.addMesh("", lodder_stamp, false);
    terrafectorSystem::loadCombine_LOD7_stamps.loadToGPU("", true);
}



void terrainManager::updateDynamicRoad(bool _bezierChanged) {

    if (this->terrainMode == _terrainMode::terrafector)
    {
        currentStamp_to_Bezier();
        return;
    }

    // active road ----------------------------------------------------------------------------------------------------------------
    splineTest.bSegment = false;
    splineTest.bVertex = false;
    splineTest.testDistance = 1000;
    splineTest.bCenter = false;
    splineTest.cornerNum = -1;
    splineTest.pos = split.feedback.tum_Position;
    splineTest.bStreetview = false;

    //mRoadNetwork.testHit(feedback.tum_Position);

    static bool bRefresh;
    if (_bezierChanged) { bRefresh = true; }
    if (mRoadNetwork.currentRoad || mRoadNetwork.currentIntersection)
    {
        if (bRefresh || mRoadNetwork.isDirty)
        {
            mRoadNetwork.updateDynamicRoad();
            splines.numDynamicSplines = __min(splines.maxDynamicBezier, (uint)roadNetwork::staticBezierData.size());
            splines.numDynamicSplinesIndex = __min(splines.maxDynamicIndex, (uint)roadNetwork::staticIndexData.size());
            if (splines.numDynamicSplines > 0)
            {
                splines.dynamic_bezierData->setBlob(roadNetwork::staticBezierData.data(), 0, (uint64_t)splines.numDynamicSplines * sizeof(cubicDouble));
                splines.dynamic_indexData->setBlob(roadNetwork::staticIndexData.data(), 0, (uint64_t)splines.numDynamicSplinesIndex * sizeof(bezierLayer));

                mRoadNetwork.isDirty = false;
            }
        }
        if (!_bezierChanged) { bRefresh = false; }
    }
    else
    {
        splines.numDynamicSplines = 0;
        splines.numDynamicSplinesIndex = 0;
    }

    mRoadNetwork.intersectionSelectedRoad = nullptr;

    // Not implemented: the marker-sprite feedback for the current road /
    // current intersection - it belongs to the editing UI, and
    // currentRoad/currentIntersection are never non-null in a bare runtime.
}




bool testBezier(cubicDouble& _bez, glm::vec3 _pos, float _size)
{
    (void)_bez; (void)_pos; (void)_size;
    return false;
}



void terrainManager::bezierRoadstoLOD(uint _lod)
{
    (void)_lod;     // unused - the loop below is fixed at lods 4, 6 and 8
#define outsideLine 	(roadNetwork::staticIndexData[i].A >> 31) & 0x1
#define insideLine 		(roadNetwork::staticIndexData[i].A >> 30) & 0x1
#define index  			roadNetwork::staticIndexData[i].A & 0x1ffff

    //std::vector<bezierLayer> lod4[16][16];
    //std::vector<bezierLayer> lod6[64][64];
    //std::vector<bezierLayer> lod8[256][256];
    // clear
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            splines.lod4[y][x].clear();
        }
    }
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            splines.lod6[y][x].clear();
        }
    }
    for (int y = 0; y < 256; y++) {
        for (int x = 0; x < 256; x++) {
            splines.lod8[y][x].clear();
        }
    }

    fprintf(terrafectorSystem::_logfile, "\n\n\nbezierRoadstoLOD\n");

    for (uint i = 0; i < splines.numStaticSplinesIndex; i++)
    {
        cubicDouble& BEZ = roadNetwork::staticBezierData[index];

        float3 perpStart = glm::normalize(BEZ.data[1][0] - BEZ.data[0][0]);

        float w0 = ((roadNetwork::staticIndexData[i].B >> 14) & 0x3fff) * 0.002f - 16.0f;			// -32 .. 33.536m in mm resolution
        float w1 = (roadNetwork::staticIndexData[i].B & 0x3fff) * 0.002f - 16.0f;

        float3 startInside = float3(BEZ.data[insideLine][0]) + w0 * perpStart;
        float3 startOutside = float3(BEZ.data[outsideLine][0]) + w1 * perpStart;
        // endInside/endOutside deliberately use perpSTART, not a perpEnd. The
        // consequence is limited to the bin-margin width at segment ends, and
        // "fixing" it changes which bins layers land in.
        float3 endInside = float3(BEZ.data[insideLine][3]) + w0 * perpStart;
        float3 endOutside = float3(BEZ.data[outsideLine][3]) + w1 * perpStart;

        float splineWidth = __max(glm::length(startOutside - startInside), glm::length(endOutside - endInside));

        for (int lod = 4; lod <= 8; lod += 2)
        {
            float scale = 1.0f / (float)pow(2, lod);
            float tileSize = settings.size * scale;
            float pixelSize = tileSize / 248.0f;
            float borderSize = (pixelSize * 4.0f) + splineWidth;    // add splineWidth to compensate for curve
            //??? How to boos tarmac since left right that one is double
            // boost lod4 a little since I dont k ow trarmact yert
            if (lod == 4) splineWidth *= 2.0f; // doesnt work, we need Special splines that includes runoff areas
            if ((lod == 8) || splineWidth > pixelSize)
            {
                float xMin = __min(__min(BEZ.data[0][0].x, BEZ.data[0][3].x), __min(BEZ.data[1][0].x, BEZ.data[1][3].x)) - 80;      // 39 is the buffer size for lod4
                float xMax = __max(__max(BEZ.data[0][0].x, BEZ.data[0][3].x), __max(BEZ.data[1][0].x, BEZ.data[1][3].x)) + 80;
                float yMin = __min(__min(BEZ.data[0][0].z, BEZ.data[0][3].z), __min(BEZ.data[1][0].z, BEZ.data[1][3].z)) - 80;
                float yMax = __max(__max(BEZ.data[0][0].z, BEZ.data[0][3].z), __max(BEZ.data[1][0].z, BEZ.data[1][3].z)) + 80;

                float halfsize = ecotopeSystem::terrainSize / 2.f;
                // TODO: gMinX/gMinY come from (uint)floor(<negative>), which wraps to a
                // huge unsigned, so the __min against grid below yields grid and the
                // loop is skipped entirely instead of clamping to 0. Roads just off the
                // terrain's low edge are silently dropped from those bins.
                uint gMinX = (uint)floor((xMin - borderSize + halfsize) / tileSize);
                uint gMaxX = (uint)ceil((xMax + borderSize + halfsize) / tileSize);
                uint gMinY = (uint)floor((yMin - borderSize + halfsize) / tileSize);
                uint gMaxY = (uint)ceil((yMax + borderSize + halfsize) / tileSize);

                // Clamp the bin window to the grid: a road within
                // 80 m + borderSize of the terrain edge would otherwise index
                // past splines.lodN[][], and a network may legally touch the
                // terrain edge.
                const uint grid = 1u << lod;
                gMinX = __min(gMinX, grid); gMaxX = __min(gMaxX, grid);
                gMinY = __min(gMinY, grid); gMaxY = __min(gMaxY, grid);

                for (uint y = gMinY; y < gMaxY; y++) {
                    for (uint x = gMinX; x < gMaxX; x++)
                    {
                        switch (lod)
                        {
                        case 4: splines.lod4[y][x].push_back(roadNetwork::staticIndexData[i]); break;
                        case 6: splines.lod6[y][x].push_back(roadNetwork::staticIndexData[i]); break;
                        case 8: splines.lod8[y][x].push_back(roadNetwork::staticIndexData[i]); break;
                        }

                    }
                }
            }
        }
    }

    FILE* file = fopen((settings.dirRoot + "/bake/roadbeziers_lod4.gpu").c_str(), "wb");
    FILE* datafile = fopen((settings.dirRoot + "/bake/roadbeziers_lod4_data.gpu").c_str(), "wb");
    if (file && datafile)
    {
        //uint lod = 4;
        //fwrite(&lod, sizeof(uint), 1, file);

        uint start = 0;
        uint largest = 0;
        for (int y = 0; y < 16; y++) {
            for (int x = 0; x < 16; x++) {
                uint size = (uint)splines.lod4[y][x].size();
                fwrite(&size, sizeof(uint), 1, file);
                fwrite(&start, sizeof(uint), 1, file);

                if (size > 0)
                {
                    largest = __max(largest, size);
                    splines.indexData_LOD4->setBlob(splines.lod4[y][x].data(), (uint64_t)start * sizeof(bezierLayer), (uint64_t)size * sizeof(bezierLayer));
                    fwrite(splines.lod4[y][x].data(), sizeof(bezierLayer), size, datafile);
                }
                splines.startOffset_LOD4[y][x] = start;
                splines.numIndex_LOD4[y][x] = size;
                start += size;
                //fprintf(terrafectorSystem::_logfile, "%6d", size);
            }
            //fprintf(terrafectorSystem::_logfile, "\n");
        }
        fclose(file);
        fclose(datafile);

        fprintf(terrafectorSystem::_logfile, "\nLOD 4. Total beziers %d from %d.   Most beziers in a block = %d\n", start, splines.numStaticSplinesIndex, largest);
        fprintf(terrafectorSystem::_logfile, "using %3.1f Mb\n", ((float)(start * sizeof(bezierLayer)) / 1024.0f / 1024.0f));
    }
    else
    {
        // The GPU setBlob of the LOD bins lives INSIDE the
        // `if (file && datafile)` blocks, so a missing <dirRoot>/bake directory
        // silently produces an EMPTY road bake. onLoad creates the directory; if
        // opening still fails, say so loudly.
        spdlog::error("terrain: cannot write '{}' - road LOD bins NOT uploaded, road bake will be empty (the upload lives inside the file-write block)",
                      settings.dirRoot + "/bake/roadbeziers_lod4.gpu");
        if (file) fclose(file);
        if (datafile) fclose(datafile);
    }



    file = fopen((settings.dirRoot + "/bake/roadbeziers_lod6.gpu").c_str(), "wb");
    datafile = fopen((settings.dirRoot + "/bake/roadbeziers_lod6_data.gpu").c_str(), "wb");
    if (file && datafile)
    {
        //uint lod = 6;
        //fwrite(&lod, sizeof(uint), 1, file);

        uint start = 0;
        uint largest = 0;
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
                uint size = (uint)splines.lod6[y][x].size();
                fwrite(&size, sizeof(uint), 1, file);
                fwrite(&start, sizeof(uint), 1, file);


                if (size > 0)
                {
                    largest = __max(largest, size);
                    splines.indexData_LOD6->setBlob(splines.lod6[y][x].data(), (uint64_t)start * sizeof(bezierLayer), (uint64_t)size * sizeof(bezierLayer));
                    fwrite(splines.lod6[y][x].data(), sizeof(bezierLayer), size, datafile);
                }
                splines.startOffset_LOD6[y][x] = start;
                splines.numIndex_LOD6[y][x] = size;
                start += size;
            }
        }
        fclose(file);
        fclose(datafile);

        fprintf(terrafectorSystem::_logfile, "\nLOD 6. Total beziers %d from %d.   Most beziers in a block = %d\n", start, splines.numStaticSplinesIndex, largest);
        fprintf(terrafectorSystem::_logfile, "using %3.1f Mb\n", ((float)(start * sizeof(bezierLayer)) / 1024.0f / 1024.0f));
    }
    else
    {
        spdlog::error("terrain: cannot write roadbeziers_lod6 .gpu files - LOD6 road bins NOT uploaded");
        if (file) fclose(file);
        if (datafile) fclose(datafile);
    }


    file = fopen((settings.dirRoot + "/bake/roadbeziers_lod8.gpu").c_str(), "wb");
    datafile = fopen((settings.dirRoot + "/bake/roadbeziers_lod8_data.gpu").c_str(), "wb");
    if (file && datafile)
    {
        //uint lod = 8;
        //fwrite(&lod, sizeof(uint), 1, file);

        uint start = 0;
        uint largest = 0;
        for (int y = 0; y < 256; y++) {
            for (int x = 0; x < 256; x++) {
                uint size = (uint)splines.lod8[y][x].size();
                fwrite(&size, sizeof(uint), 1, file);
                fwrite(&start, sizeof(uint), 1, file);

                if (size > 0)
                {
                    largest = __max(largest, size);
                    splines.indexData_LOD8->setBlob(splines.lod8[y][x].data(), (uint64_t)start * sizeof(bezierLayer), (uint64_t)size * sizeof(bezierLayer));
                    fwrite(splines.lod8[y][x].data(), sizeof(bezierLayer), size, datafile);
                }
                splines.startOffset_LOD8[y][x] = start;
                splines.numIndex_LOD8[y][x] = size;
                start += size;
            }
        }
        fclose(file);
        fclose(datafile);

        fprintf(terrafectorSystem::_logfile, "\nLOD 8. Total beziers %d from %d.   Most beziers in a block = %d\n", start, splines.numStaticSplinesIndex, largest);
        fprintf(terrafectorSystem::_logfile, "using %3.1f Mb\n", ((float)(start * sizeof(bezierLayer)) / 1024.0f / 1024.0f));
    }
    else
    {
        spdlog::error("terrain: cannot write roadbeziers_lod8 .gpu files - LOD8 road bins NOT uploaded");
        if (file) fclose(file);
        if (datafile) fclose(datafile);
    }





    file = fopen((settings.dirRoot + "/bake/roadbeziers_bezier.gpu").c_str(), "wb");
    if (file)
    {
        fwrite(roadNetwork::staticBezierData.data(), sizeof(cubicDouble), splines.numStaticSplines, file);
        fclose(file);
    }

#undef outsideLine
#undef insideLine
#undef index
}


bool terrainManager::onKeyEvent(const ew::KeyboardEvent& keyEvent)
{
    // Not implemented: the editor mode keys and road/stamp editing.
    (void)keyEvent;
    return false;
}


bool terrainManager::onMouseEvent(const ew::MouseEvent& mouseEvent, glm::vec2 _screenSize, glm::vec2 _mouseScale, glm::vec2 _mouseOffset, ew::Camera::SharedPtr _camera)
{
    // Picking anchor only. The pan/orbit/zoom camera moves are not implemented
    // - the sample's FirstPersonCamera drives the camera instead.
    glm::vec2 pos = (mouseEvent.pos * _mouseScale) + _mouseOffset;
    if (pos.x > 0 && pos.x < 1 && pos.y > 0 && pos.y < 1)
    {
        pos.y = 1.0f - pos.y;
        glm::vec3 N = glm::unProject(glm::vec3(pos * _screenSize, 0.0f), _camera->getViewMatrix(), _camera->getProjMatrix(), glm::vec4(0, 0, _screenSize));
        glm::vec3 F = glm::unProject(glm::vec3(pos * _screenSize, 1.0f), _camera->getViewMatrix(), _camera->getProjMatrix(), glm::vec4(0, 0, _screenSize));
        mouseDirection = glm::normalize(F - N);
        screenSize = _screenSize;
        mousePosition = _camera->getPosition();
        mouseCoord = mouseEvent.pos * _screenSize;
        mousePositionOld = pos;
    }
    return false;
}
