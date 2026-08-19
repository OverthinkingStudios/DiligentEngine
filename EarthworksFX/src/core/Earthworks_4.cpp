#include "Earthworks_4.h"

#include <algorithm>
#include <fstream>
#include <vector>

#include "glm/gtc/matrix_transform.hpp"

#include "ots/Log.hpp"

using namespace ew;

namespace
{

#if defined(_WIN32)
extern "C" __declspec(dllimport) int __stdcall IsDebuggerPresent(void);
#endif

// Missing-required-file handler (twin of the one in terrain.cpp): loud log plus
// a break into an attached debugger, and returns false so call sites can still
// fall back gracefully.
bool requireFile(const std::filesystem::path& path, const char* what)
{
    if (std::filesystem::exists(path))
        return true;
    spdlog::error("Earthworks_4: required {} not found - '{}'", what, path.string());
#if defined(_WIN32)
    if (IsDebuggerPresent())
        __debugbreak();
#endif
    return false;
}

} // namespace

// Line counts / world extents shared by the globe and ground-grid draws.
// The globe values must match the ranges in hlsl/debugGrid.hlsl.
namespace
{
constexpr int kLatLines = 3;  // horizon + faint +/-45 deg pitch markers
constexpr int kLonLines = 8;  // meridians every 45 deg (N/E/S/W + faint diagonals)
constexpr int kSeg      = 96; // segments per circle

// Globe radius = half-diagonal of the 40 km x 40 km terrain area (jp2Map::set:
// wSize = 40000, wOffset = -20000): sqrt(20000^2 + 20000^2). With the camera
// at the world origin the globe passes exactly through the area corners.
constexpr float kGlobeRad = 28284.271f;

constexpr float kGroundY = 0.0f; // ground-plane height for the tile grid (sea level)
} // namespace

void Earthworks_4::onLoad(GpuContext* pGpu)
{
    spdlog::info("Earthworks_4::onLoad()");

    // The 15 mm focal length against the camera's 24 mm film back is roughly a
    // 77.3 degree vertical FOV.
    camera = Camera::create();
    camera->setDepthRange(0.1f, 40000.0f);
    camera->setAspectRatio(1920.0f / 1080.0f);
    camera->setFocalLength(15.0f);
    camera->setPosition(float3(0.f, 900.f, 0.f));
    camera->setTarget(float3(0.f, 900.f, 100.f));

    // The load order is load-bearing: atmosphere.onLoad FIRST, then
    // terrain.onLoad, then the atmosphere -> terrain shader texture wiring.
    {
        atmosphere.onLoad(pGpu);

        // Vegetation gets the atmosphere textures BEFORE terrain.onLoad -
        // plants_Root.onLoad runs inside terrain.onLoad and binds them as its
        // shaders load.
        terrain.plants_Root.inscatter = atmosphere.getFar().inscatter;
        terrain.plants_Root.outscatter = atmosphere.getFar().outscatter;
        terrain.plants_Root.sunlightTexture = atmosphere.sunlightTexture;
    }

    terrain.onLoad(pGpu);

    if (terrain.isLoaded())
    {
        terrain.plants_Root.vegetationShader.setTexture("gAtmosphereInscatter", atmosphere.getFar().inscatter);
        terrain.plants_Root.vegetationShader.setTexture("gAtmosphereOutscatter", atmosphere.getFar().outscatter);
        terrain.plants_Root.vegetationShader.setTexture("SunInAtmosphere", atmosphere.sunlightTexture);

        terrain.plants_Root.billboardShader.setTexture("gAtmosphereInscatter", atmosphere.getFar().inscatter);
        terrain.plants_Root.billboardShader.setTexture("gAtmosphereOutscatter", atmosphere.getFar().outscatter);
        terrain.plants_Root.billboardShader.setTexture("SunInAtmosphere", atmosphere.sunlightTexture);

        terrain.terrainShader.setTexture("gAtmosphereInscatter", atmosphere.getFar().inscatter);
        terrain.terrainShader.setTexture("gAtmosphereOutscatter", atmosphere.getFar().outscatter);
        terrain.terrainShader.setTexture("SunInAtmosphere", atmosphere.sunlightTexture);

        terrain.terrainSpiteShader.setTexture("gAtmosphereInscatter", atmosphere.getFar().inscatter);
        terrain.terrainSpiteShader.setTexture("gAtmosphereOutscatter", atmosphere.getFar().outscatter);
        terrain.terrainSpiteShader.setTexture("SunInAtmosphere", atmosphere.sunlightTexture);

        terrain.triangleShader.setTexture("gAtmosphereInscatter_Sky", atmosphere.getFar().inscatter_sky);
        // render_triangles' live path also calls
        // gAtmosphereInscatter.GetDimensions and throws the result away, so the
        // real 3D volume is bound in case DXC keeps the reference: the automatic
        // dummy fallback is 2D and would trip Vulkan's view-type validation.
        terrain.triangleShader.setTexture("gAtmosphereInscatter", atmosphere.getFar().inscatter);
    }

    // Not implemented: camera.bin / earthworks4_presets.xml persistence. The
    // current CameraData layout is binary-incompatible with old camera.bin
    // files, so neither is read or written.

    postProcess.tonemapper.load("hlsl/compute_tonemapper.hlsl", "vsMain", "psMain", Topology::TriangleList);
    {
        // Load-bearing: the fullscreen triangle built from `(vId<<1)&2` winds
        // CLOCKWISE in screen space and this layer forces front=CCW, so the
        // default cull-back would eat the entire pass - a blue screen, even in
        // the solid-colour debug view.
        Diligent::RasterizerStateDesc rsDesc;
        rsDesc.CullMode = Diligent::CULL_MODE_NONE;
        postProcess.tonemapper.setRasterizerState(rsDesc);

        // Depth is irrelevant for a fullscreen pass, and the write in
        // particular has to stay off so the swap-chain depth buffer is still
        // clean for the post-tonemap overlays.
        Diligent::DepthStencilStateDesc dsDesc;
        dsDesc.DepthEnable      = Diligent::False;
        dsDesc.DepthWriteEnable = Diligent::False;
        postProcess.tonemapper.setDepthStencilState(dsDesc);
    }
    //loadColorCube("F:/terrains/colorcubes/K_TONE Vintage_KODACHROME.cube");
    if (terrain.isLoaded())
        loadColorCube(terrain.settings.dirResource + "/colorcubes/ColdChrome.cube");
    else
        loadColorCube("");  // no terrain -> identity LUT

    if (terrain.isLoaded())
    {
        // Which root4096.bil exists is terrain-data-dependent: some data sets
        // ship <dirRoot>/elevation/, others only gis/_export/. Prefer
        // elevation/, fall back, and complain loudly if neither is there.
        std::filesystem::path shadowBil = std::filesystem::path(terrain.settings.dirRoot) / "elevation/root4096.bil";
        if (!std::filesystem::exists(shadowBil))
        {
            const std::filesystem::path gisBil = std::filesystem::path(terrain.settings.dirRoot) / "gis/_export/root4096.bil";
            if (std::filesystem::exists(gisBil))
            {
                spdlog::info("Earthworks_4: '{}' missing, using '{}' (data-layout variant)", shadowBil.string(), gisBil.string());
                shadowBil = gisBil;
            }
        }
        // TODO: the requireFile result is ignored, so when neither root4096.bil path
        // exists this logs, breaks, and then still hands the missing path to
        // shadowEdges.load() below.
        requireFile(shadowBil, "shadow heightfield (root4096.bil)");

        terrain.shadowEdges.load(shadowBil.string(), -global_sun_direction.y);
        terrain.shadowEdges.sunAngle = 0.95605f;
        terrain.shadowEdges.dAngle = 0.0001f;
        terrain.shadowEdges.requestNewShadow = true;
        ew::gDebug.toggles.shadowSunAngle = terrain.shadowEdges.sunAngle;

        terrain.terrainShadowTexture = Texture::create2D(4096, 4096, Diligent::TEX_FORMAT_RG32_FLOAT, 1, 1, terrain.shadowEdges.shadowH,
                                                         Diligent::BIND_UNORDERED_ACCESS | Diligent::BIND_SHADER_RESOURCE, "terrainShadowTexture");

        // The solver thread is owned by _shadowEdges and joined in its
        // destructor, not detached - it runs for the whole lifetime of the
        // terrain and keeps re-solving on request.
        terrain.shadowEdges.launchSolveThread();

        atmosphere.setTerrainShadow(terrain.terrainShadowTexture);
    }

    // --- debug orientation / movement grid -----------------------------------
    debugGridShader.load("hlsl/debugGrid.hlsl", "vsMain", "psMain", Topology::LineList);
    debugGridLoaded = debugGridShader.isLoaded();
    if (debugGridLoaded)
    {
        // Cull off for line rendering; blend stays default (opaque).
        Diligent::RasterizerStateDesc rsDesc;
        rsDesc.CullMode = Diligent::CULL_MODE_NONE;
        debugGridShader.setRasterizerState(rsDesc);

        debugTileRectBuffer = Buffer::createStructured(
            static_cast<uint32_t>(sizeof(float4)), kMaxDebugTileRects,
            Diligent::BIND_SHADER_RESOURCE, nullptr, "debugGrid tileRects");
        debugTileHeightBuffer = Buffer::createStructured(
            static_cast<uint32_t>(sizeof(float)), kMaxDebugTileRects,
            Diligent::BIND_SHADER_RESOURCE, nullptr, "debugGrid tileHeights");
        debugGridShader.setBuffer("tileRects", debugTileRectBuffer);
        debugGridShader.setBuffer("tileHeights", debugTileHeightBuffer);

        // A static 8x8 grid over the 40 km x 40 km world area, used whenever no
        // terrain is loaded; renderDebugGroundGrid swaps in the live quadtree
        // tiles when there is one.
        constexpr int   kGridN    = 8;
        constexpr float kWorld    = 40000.f;
        constexpr float kOffset   = -20000.f;
        constexpr float kTileSize = kWorld / static_cast<float>(kGridN);
        debugTileRects.reserve(static_cast<size_t>(kGridN) * kGridN);
        for (int y = 0; y < kGridN; ++y)
        {
            for (int x = 0; x < kGridN; ++x)
            {
                debugTileRects.push_back(float4(kOffset + static_cast<float>(x) * kTileSize,
                                                kOffset + static_cast<float>(y) * kTileSize,
                                                kTileSize,
                                                3.f /*lod colour*/));
            }
        }
        debugTileHeights.assign(debugTileRects.size(), 0.f);
        debugTilesDirty = true;
    }

    // The globe stays on by default so a run without terrain still shows
    // geometry rather than a bare clear colour.
    ew::gDebug.toggles.debugGlobe      = true;
    ew::gDebug.toggles.debugGroundGrid = false;
}

// move to a postprocess class
void Earthworks_4::loadColorCube(std::string name)
{
    // RGB32Float (3 channels) is not filterable on most APIs, so the .cube data
    // is repacked into RGBA32Float. A missing file falls back to an IDENTITY
    // LUT rather than a null texture: lerp(aces, identity(aces), 0.2) == aces,
    // so the tonemapper output is unchanged.
    std::string line;
    std::ifstream cube(name);
    std::vector<float4> colorcube(33 * 33 * 33);
    if (!name.empty())
        requireFile(name, "tonemap colour cube (.cube LUT)");
    if (cube.good())
    {
        std::getline(cube, line);
        std::getline(cube, line);
        std::getline(cube, line);
        std::getline(cube, line);
        std::getline(cube, line);

        for (int i = 0; i < 33; i++)
        {
            for (int j = 0; j < 33; j++)
            {
                for (int k = 0; k < 33; k++)
                {
                    float4& texel = colorcube[(static_cast<size_t>(i) * 33 + j) * 33 + k];
                    cube >> texel.x >> texel.y >> texel.z;
                    texel.w = 0.f;
                }
            }
        }
    }
    else
    {
        spdlog::error("ERROR - loadColorCube() - failed ('{}') - using identity LUT", name);
        // identity: LUT(rgb) = rgb. The .cube layout is z(=red) fastest; the
        // shader samples cube at aces.rgb, so x axis = red = k here.
        for (int i = 0; i < 33; i++)
            for (int j = 0; j < 33; j++)
                for (int k = 0; k < 33; k++)
                    colorcube[(static_cast<size_t>(i) * 33 + j) * 33 + k] =
                        float4(k / 32.f, j / 32.f, i / 32.f, 0.f);
    }
    postProcess.colorCube = Texture::create3D(33, 33, 33, Diligent::TEX_FORMAT_RGBA32_FLOAT, colorcube.data(),
                                              Diligent::BIND_SHADER_RESOURCE, "postProcess.colorCube");
}


void Earthworks_4::onFrameUpdate(GpuContext* pGpu)
{
    if (!terrain.isLoaded())
        return;

    {
        // Shadow-thread handoff: the sun direction and the shadow field that
        // matches it change ATOMICALLY together. Never decouple these two
        // writes - a mismatched pair shows up as shadows from the wrong sun.
        if (terrain.shadowEdges.shadowReady)
        {
            spdlog::info("terrain.shadowEdges.shadowReady");
            terrain.terrainShadowTexture->upload(pGpu, terrain.shadowEdges.shadowH);
            global_sun_direction = terrain.shadowEdges.sunAng;
            terrain.shadowEdges.shadowReady = false;
        }

        // Debug-panel re-solve request: one-shot, consumed here.
        if (ew::gDebug.toggles.shadowResolve)
        {
            ew::gDebug.toggles.shadowResolve = false;
            terrain.shadowEdges.sunAngle = ew::gDebug.toggles.shadowSunAngle;
            terrain.shadowEdges.requestNewShadow = true;
        }
    }

    shaderLightBuffer lightBuffer;
    lightBuffer.sunRightVector = glm::normalize(glm::cross(float3(0, 1, 0), global_sun_direction));
    lightBuffer.sunUpVector = glm::normalize(glm::cross(global_sun_direction, lightBuffer.sunRightVector));
    lightBuffer.sunDirection = global_sun_direction;

    lightBuffer.screenSize = screenSize;

    lightBuffer.fog_far_log_F = atmosphere.getFar().m_logEnd;
    lightBuffer.fog_far_one_over_k = atmosphere.getFar().m_oneOverK;
    lightBuffer.fog_far_Start = atmosphere.getFar().m_params._near;

    lightBuffer.fog_near_log_F = atmosphere.getNear().m_logEnd;
    lightBuffer.fog_near_one_over_k = atmosphere.getNear().m_oneOverK;
    lightBuffer.fog_near_Start = atmosphere.getNear().m_params._near;

    terrain.updateShaderConstants(hdrPreviousFrame, lightBuffer);

    // The 1920 is hardcoded on purpose: it feeds the lod_Pix split thresholds,
    // so tying it to the actual window width would change terrain LOD selection
    // with the window size. Do NOT "fix" it.
    if (ew::gDebug.toggles.syncCamera)
        // setCamera expects STANDARD glm matrices, not transposed ones: the
        // frustum-plane extraction inside is textbook Gribb-Hartmann on a
        // standard glm projection, and a transposed feed inverts the culling.
        // The bottom-tile flicker that looks like a matrix problem is not one -
        // it comes from stale bounding-sphere patches out of the readback ring
        // landing on reused pool slots (see the bornFrame guard at the
        // tileCenters readback).
        terrain.setCamera(CameraType_Main_Center, camera->getViewMatrix(), camera->getProjMatrix(), camera->getPosition(), true, 1920);

    if (ew::gDebug.toggles.terrainUpdate)
        terrain.update(pGpu);

    if (ew::gDebug.toggles.atmosphere)
    {
        atmosphere.setSunDirection(global_sun_direction);
        atmosphere.getFar().setCamera(camera);
        atmosphere.computeSunInAtmosphere(pGpu);
        atmosphere.computeVolumetric(pGpu);
    }
}

void Earthworks_4::onFrameRender(GpuContext* pGpu, const Fbo::SharedPtr& pTargetFbo)
{
    ew::gDebug.beginFrame();

    // onFrameUpdate is NOT called by the framework; onFrameRender calls it.
    onFrameUpdate(pGpu);

    // While terrain is doing a full reset NOTHING renders - the tile pools are
    // half-initialized and drawing from them is undefined.
    if (terrain.isLoaded() && terrain.fullResetDoNotRender)
    {
        ew::gDebug.endFrame();
        return;
    }

    // BOTH the swap chain and the hdrFbo get cleared. The clear colour is
    // deliberately not black, so "nothing rendered at all" is unambiguous.
    const float4 clearColor(0.05f, 0.10f, 0.18f, 1.f);
    pGpu->clearFbo(pTargetFbo.get(), clearColor, 1.0f, 0, FboAttachmentType::All);
    if (hdrFbo)
        pGpu->clearFbo(hdrFbo.get(), clearColor, 1.0f, 0, FboAttachmentType::All);

    // The full path is scene -> hdrFbo -> tonemapper -> pTargetFbo -> half-res
    // hdrPreviousFrame blit. ew::gDebug.toggles.bypassHdr renders the scene
    // straight into the swap chain instead, which bisects HDR-vs-scene bugs.
    const bool useHdr = hdrFbo && !ew::gDebug.toggles.bypassHdr;
    const Fbo::SharedPtr& sceneFbo = useHdr ? hdrFbo : pTargetFbo;

    if (terrain.isLoaded())
        terrain.onFrameRender(pGpu, sceneFbo, camera);

    // Debug orientation aids draw into the scene FBO so the terrain's depth
    // buffer still occludes the globe (they get tonemapped with the scene).
    if (showDebugGrid && ew::gDebug.toggles.debugGlobe)
        renderDebugGlobe(pGpu, sceneFbo);

    if (showDebugGrid && ew::gDebug.toggles.debugGroundGrid)
        renderDebugGroundGrid(pGpu, sceneFbo);

    if (useHdr)
    {
        if (ew::gDebug.toggles.tonemapper)
        {
            postProcess.tonemapper.setTexture("hdr", hdrFbo->getColorTexture(0));
            postProcess.tonemapper.setTexture("cube", postProcess.colorCube);
            if (terrain.isLoaded())
                postProcess.tonemapper.setSampler("linearSampler", terrain.sampler_Clamp);
            postProcess.tonemapper.setVariable("gConstants", "debugView", ew::gDebug.toggles.tonemapperView);
            postProcess.tonemapper.setFbo(pTargetFbo);
            // One oversized fullscreen triangle, non-indexed - drawInstanced
            // never takes the indexed path in this layer.
            postProcess.tonemapper.drawInstanced(pGpu, 3, 1);
            ew::gDebug.live.tonemapperDraws++;
        }

        // hdrFbo colour -> HALF-res hdrPreviousFrame, which feeds the JHFAA
        // temporal-alpha consumers in vegetation.
        if (hdrPreviousFrame && hdrFbo->getColorTexture(0))
        {
            const float4 srcRect(0.f, 0.f, (float)hdrFbo->getWidth(), (float)hdrFbo->getHeight());
            const float4 dstRect(0.f, 0.f, (float)hdrPreviousFrame->getWidth(), (float)hdrPreviousFrame->getHeight());
            pGpu->blit(hdrFbo->getColorTexture(0)->getSRV(), hdrPreviousFrame->getRTV(), srcRect, dstRect, true);
        }
    }

    ew::gDebug.endFrame();
}

void Earthworks_4::onShutdown()
{
    spdlog::info("Earthworks_4::onShutdown()");
    terrain.onShutdown();
    // The actual terrain persistence (lastFile.xml) happens in ~terrainManager,
    // not here.
}

void Earthworks_4::onResizeSwapChain(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
        return;
    screenSize = float2(static_cast<float>(width), static_cast<float>(height));
    if (camera)
        camera->setAspectRatio(screenSize.x / screenSize.y);
    screenMouseScale  = float2(1.f, 1.f);
    screenMouseOffset = float2(0.f, 0.f);

    // HDR scene target: R11G11B10Float colour + D24S8 depth.
    hdrFbo = Fbo::create();
    hdrFbo->attachColorTarget(Texture::create2D(width, height, Diligent::TEX_FORMAT_R11G11B10_FLOAT, 1, 1, nullptr,
                                                Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE, "hdrFbo color"), 0);
    hdrFbo->attachDepthStencilTarget(Texture::create2D(width, height, Diligent::TEX_FORMAT_D24_UNORM_S8_UINT, 1, 1, nullptr,
                                                       Diligent::BIND_DEPTH_STENCIL, "hdrFbo depth"));

    // HALF-res previous-frame feedback. RTV+SRV only: the blit writes the RTV
    // and consumers sample it, no UAV consumer exists, and R11G11B10 UAV
    // support is not universal across devices.
    hdrPreviousFrame = Texture::create2D(width / 2, height / 2, Diligent::TEX_FORMAT_R11G11B10_FLOAT, 1, 1, nullptr,
                                         Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE, "hdrPreviousFrame");

    spdlog::info("Earthworks_4::onResizeSwapChain() {}, {}", width, height);
}

bool Earthworks_4::onKeyEvent(const KeyboardEvent& keyEvent)
{
    // Terrain gets first look at the keyboard and can swallow the event.
    if (terrain.onKeyEvent(keyEvent))
        return true;

    if (keyEvent.type == KeyboardEvent::Type::KeyPressed)
    {
        if (keyEvent.key == Input::Key::G)
        {
            showDebugGrid = !showDebugGrid;
            return true;
        }
    }
    return false;
}

bool Earthworks_4::onMouseEvent(const MouseEvent& mouseEvent)
{
    // Feeds the terrain-under-mouse picking anchors (GPU picking compute).
    if (terrain.isLoaded() && camera)
        return terrain.onMouseEvent(mouseEvent, screenSize, screenMouseScale, screenMouseOffset, camera);
    return false;
}

void Earthworks_4::onGuiRender()
{
}

// --- debug grid ---------------------------------------------------------------

void Earthworks_4::setDebugGridConstants(int drawMode)
{
    // Upload convention (ewCamera.h): HLSL does mul(float4(pos,1), viewproj)
    // with default column-major cbuffer packing, so the bytes must be
    // row-major(P*V) == glm::transpose(proj*view).
    const glm::mat4 viewproj = glm::transpose(camera->getViewProjMatrix());
    debugGridShader.setVariable("gConstantBuffer", "viewproj", viewproj);
    debugGridShader.setVariable("gConstantBuffer", "eye", camera->getPosition());
    debugGridShader.setVariable("gConstantBuffer", "globeRadius", kGlobeRad);
    debugGridShader.setVariable("gConstantBuffer", "groundY", kGroundY);
    debugGridShader.setVariable("gConstantBuffer", "drawMode", drawMode);
    debugGridShader.setVariable("gConstantBuffer", "latLines", kLatLines);
    debugGridShader.setVariable("gConstantBuffer", "lonLines", kLonLines);
    debugGridShader.setVariable("gConstantBuffer", "segments", kSeg);
}

void Earthworks_4::renderDebugGlobe(GpuContext* pGpu, const Fbo::SharedPtr& pFbo)
{
    if (!debugGridLoaded || !camera)
        return;

    // Depth-tested (LessEqual) but not writing, so the terrain depth occludes
    // the globe and what remains visible is the terrain silhouette.
    Diligent::DepthStencilStateDesc dsDesc;
    dsDesc.DepthEnable      = Diligent::True;
    dsDesc.DepthWriteEnable = Diligent::False;
    dsDesc.DepthFunc        = Diligent::COMPARISON_FUNC_LESS_EQUAL;
    debugGridShader.setDepthStencilState(dsDesc);

    debugGridShader.setFbo(pFbo);
    setDebugGridConstants(0);

    const uint32_t globeVerts = static_cast<uint32_t>(kLatLines + kLonLines) * kSeg * 2u;
    debugGridShader.drawInstanced(pGpu, globeVerts, 1);
    ew::gDebug.live.debugGlobeDraws++;
}

void Earthworks_4::renderDebugGroundGrid(GpuContext* pGpu, const Fbo::SharedPtr& pFbo)
{
    if (!debugGridLoaded || !camera || !debugTileRectBuffer || debugTileRects.empty())
        return;

    // Live quadtree tiles, colour-coded by LOD, refreshed every frame; the
    // static 8x8 grid built in onLoad stays as the no-terrain fallback.
    if (terrain.isLoaded())
    {
        debugTileRects.clear();
        debugTileHeights.clear();
        for (const quadtree_tile* tile : terrain.usedTiles())
        {
            if (debugTileRects.size() >= kMaxDebugTileRects)
                break;
            debugTileRects.push_back(float4(tile->origin.x, tile->origin.z, tile->size, static_cast<float>(tile->lod)));
            debugTileHeights.push_back(tile->boundingSphere.y);
        }
        debugTilesDirty = !debugTileRects.empty();
        if (debugTileRects.empty())
            return;
    }

    if (debugTilesDirty)
    {
        const size_t count = std::min<size_t>(debugTileRects.size(), kMaxDebugTileRects);
        debugTileRectBuffer->setBlob(debugTileRects.data(), 0, count * sizeof(float4));
        debugTileHeightBuffer->setBlob(debugTileHeights.data(), 0, count * sizeof(float));
        debugTilesDirty = false;
    }

    // On top of everything: no depth test.
    Diligent::DepthStencilStateDesc dsDesc;
    dsDesc.DepthEnable      = Diligent::False;
    dsDesc.DepthWriteEnable = Diligent::False;
    debugGridShader.setDepthStencilState(dsDesc);

    debugGridShader.setFbo(pFbo);
    setDebugGridConstants(1);
    debugGridShader.setVariable("gConstantBuffer", "tileCount", static_cast<int>(debugTileRects.size()));

    const uint32_t groundVerts = static_cast<uint32_t>(debugTileRects.size()) * 8u; // 4 edges x 2 endpoints
    debugGridShader.drawInstanced(pGpu, groundVerts, 1);
    ew::gDebug.live.debugGridDraws++;
}
