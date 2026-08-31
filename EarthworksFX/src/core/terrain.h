/***************************************************************************
 # Copyright (c) 2015-21, NVIDIA CORPORATION. All rights reserved.
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
#pragma once

// ---------------------------------------------------------------------------
// The terrain renderer. terrainManager owns the whole pipeline: quadtree tile
// streaming out of JP2 elevation + orthophotos, the per-tile GPU bake chain
// (bicubic upsample -> terrafector/road topdown bake -> ecotopes -> normals ->
// vertices -> jumpflood -> delaunay), the tile/billboard/plant draws, the
// vegetation hub (plants_Root), terrafectors and the road network, the CPU
// terrain-shadow solver and the skydome.
//
// Not implemented here: the offline bake/export machinery (bake_start/
// bake_frame/bake_Setup/bake_RenderTopdown/sceneToMax), the road and stamp
// editing flows, the sprite marker renderer, the render_ribbons grass pass and
// veghumanShader, cascade shadow maps, and the terrain generator.
// ---------------------------------------------------------------------------

#include "ewTypes.h"
#include "ewCamera.h"
#include "ewGpuContext.h"
#include "ewResources.h"
#include "ewShader.h"
#include "EarthworksDebug.h"

#include <atomic>
#include <thread>

#include "lru_cache.h"

#include "cereal/cereal.hpp"
#include "cereal/archives/binary.hpp"
#include "cereal/archives/json.hpp"
#include "cereal/archives/xml.hpp"
#include "cereal/types/map.hpp"
#include "cereal/types/vector.hpp"
#include "cereal/types/list.hpp"
#include "cereal/types/array.hpp"
#include "cereal/types/string.hpp"

#include <fstream>
#include <array>
#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <string>
#include <vector>

// OpenJPH (JP2 elevation/orthophoto decode) - this include shape works both
// with the vendored copy and with an installed package.
#if defined(__has_include) && __has_include(<openjph/ojph_codestream.h>)
#    include <openjph/ojph_arg.h>
#    include <openjph/ojph_mem.h>
#    include <openjph/ojph_file.h>
#    include <openjph/ojph_codestream.h>
#    include <openjph/ojph_params.h>
#    include <openjph/ojph_message.h>
#else
#    include <ojph_arg.h>
#    include <ojph_mem.h>
#    include <ojph_file.h>
#    include <ojph_codestream.h>
#    include <ojph_params.h>
#    include <ojph_message.h>
#endif

#include <future> // Required for std::async and std::future

// --- HLSL-shared struct definitions ------------------------------------------
// The .hlsli files below are compiled by BOTH MSVC and DXC. The global aliases
// stand in for the HLSL built-in type names; they denote the exact same glm
// types as the ew:: aliases, so mixing with `using namespace ew` stays
// unambiguous.
using float2   = glm::vec2;
using float3   = glm::vec3;
using float4   = glm::vec4;
using float4x4 = glm::mat4;
using uint     = std::uint32_t;
using uint2    = glm::uvec2;
using uint4    = glm::uvec4;
using int2     = glm::ivec2;   // ecotopeGpuConstants::tileXY

#include "hlsl/terrain/terrainDefines.hlsli"
#include "hlsl/terrain/groundcover_defines.hlsli"
#include "hlsl/terrain/vegetation_defines.hlsli"
#include "hlsl/terrain/gpuLights_defines.hlsli"

// After the hlsli block - these headers use the shared structs/aliases above.
// Order matters: ecotope.h (forward-declares _rootPlant) -> ribbonBuilder.h
// (packed-vertex builder) -> vegetationBuilder.h (defines _rootPlant AND
// shaderLightBuffer).
#include "ecotope.h"
#include "ribbonBuilder.h"
#include "vegetationBuilder.h"

// Terrafectors + roads. terrafector.h pulls materials.hlsli (the TF_material
// byte contract, include-guarded); roadNetwork.h pulls the whole roads stack
// (roads_bezier/road/Intersection/materials/physics).
#include "terrafector.h"
#include "roadNetwork.h"

// Far-LOD buildings delegate (step-6 salvage from the previous port - the
// original had this commented out in terrain.cpp; see buildings.h).
#include "buildings.h"


// MOVE TO SHDOPW CLASS, but inititlaize data from here
// MOVE TO TEMP SHADWO CLASS TO BE REPLACED WITH GPU DATA
//
// CPU semi-static terrain shadow: an async scanline ray-march over the 4096^2
// root heightfield producing a "shadow-line height + softness" texture that
// every lit shader and the fog compute sample analytically. There is NO
// realtime shadow map in this engine. The solver's sun path is locked to the
// X axis (sunAng = (-cos a, -sin a, 0), rows-only march) - an accepted
// constraint.
struct _shadowEdges
{
    // The four fields below are ~270 MB of tables, heap-allocated so they do
    // not sit inline in the terrainManager object. `edge` has no live reader.
    _shadowEdges();
    ~_shadowEdges();
    _shadowEdges(const _shadowEdges&) = delete;
    _shadowEdges& operator=(const _shadowEdges&) = delete;

    float (*height)[4096] = nullptr;
    float (*Nx)[4095] = nullptr;   // temp
    unsigned char (*edge)[4096] = nullptr;
    float2 (*shadowH)[4096] = nullptr;

    // Loads the 4096x4096 terrain heightfield; when _buildings is given, its
    // triangles are splatted into the caster heightfield so buildings cast
    // (and receive) the same baked shadows as the terrain (step-6 salvage).
    // Call before launchSolveThread() - buildings are static, so no
    // synchronization is needed.
    void load(std::string filename, float _angle, const buildingsRenderer* _buildings = nullptr);
    void solve(float _angle, bool dx);

    float sunAngle = 0.02f;  // just afetr sunruise
    float dAngle = 0.01f;
    float3 sunAng{ 0, 0, 0 };
    // shadowReady is stored AFTER shadowH/sunAng are written (seq_cst) so the
    // render thread's handoff always sees complete data.
    std::atomic<bool> shadowReady{ false };
    std::atomic<bool> requestNewShadow{ false };
    void solveThread();

    void launchSolveThread();
    std::thread m_solveThread;
    std::atomic<bool> m_terminate{ false };
};


struct _lastFile
{
    _lastFile() = default;
    // First-run bootstrap: point at the Steg data relative to the process
    // working directory (= ACSMP_GAMEROOT).
    _lastFile(const std::filesystem::path& terrain_root, const std::filesystem::path& resources_root);

    // these are for quick load
    std::string terrain = "F:/terrains/sonoma/sonoma.terrain";
    std::string road = "X:/resources/terrains/eifel/roads/day6.roadnetwork";
    std::string stamps = "";
    std::string roadMaterial = "X:/resources/terrafectors_and_road_materials/roads/sidewalk_Asphalt.roadMaterial";
    std::string terrafectorMaterial = "";
    std::string texture = "";
    std::string fbx = "";
    std::string EVO = "";

    std::string weed = "F:/terrains/_resources/vegetation_weeds/";
    std::string twig = "F:/terrains/_resources/vegetation_twigs/";
    std::string leaves = "F:/terrains/_resources/vegetation_leaves/";
    std::string trees = "F:/terrains/_resources/vegetation_trees/";
    std::string vegMaterial = "F:/terrains/_resources/vegetation_trees/";

    std::string dir_Resource = "";
    std::string dir_Terrains = "";
    std::string dir_GIS = "";

    int mode = 0;

    template<class Archive>
    void serialize(Archive& _archive, std::uint32_t const _version)
    {
        _archive(CEREAL_NVP(terrain));
        _archive(CEREAL_NVP(road));
        _archive(CEREAL_NVP(roadMaterial));
        _archive(CEREAL_NVP(terrafectorMaterial));
        _archive(CEREAL_NVP(texture));
        _archive(CEREAL_NVP(fbx));
        _archive(CEREAL_NVP(EVO));
        if (_version >= 101)
        {
            _archive(CEREAL_NVP(weed));
            _archive(CEREAL_NVP(twig));
            _archive(CEREAL_NVP(leaves));
            _archive(CEREAL_NVP(trees));
            _archive(CEREAL_NVP(vegMaterial));
        }
        if (_version >= 102)
        {
            _archive(CEREAL_NVP(stamps));
        }
        if (_version >= 103)
        {
            _archive(CEREAL_NVP(dir_Resource));
            _archive(CEREAL_NVP(dir_Terrains));
            _archive(CEREAL_NVP(dir_GIS));
        }
        if (_version >= 104)
        {
            _archive(CEREAL_NVP(mode));
        }
    }
};
CEREAL_CLASS_VERSION(_lastFile, 104);


struct _terrainSettings
{
    // these are for quick load
    std::string name = "eifel";
    std::string projection = "\" + proj = tmerc + lat_0 = 50.39 + lon_0 = 6.91 + k_0 = 1 + x_0 = 0 + y_0 = 0 + ellps = GRS80 + units = m\"";
    float size = 40000.f;

    std::string dirRoot = "X:/resources/terrains/eifel";
    std::string dirExport = "/terrains/Eifel";
    std::string dirGis = "X:/resources/terrains/eifel";
    std::string dirResource = "X:/resources";

    template<class Archive>
    void serialize(Archive& _archive, std::uint32_t const _version)
    {
        _archive(CEREAL_NVP(name));
        _archive(CEREAL_NVP(projection));
        _archive(CEREAL_NVP(size));
        _archive(CEREAL_NVP(dirRoot));
        _archive(CEREAL_NVP(dirExport));
        _archive(CEREAL_NVP(dirGis));
        _archive(CEREAL_NVP(dirResource));
    }

};
CEREAL_CLASS_VERSION(_terrainSettings, 100);


// FOR binary export of tiles
struct elevationMap {
    float   heightOffset;
    float   heightScale;
    float2  origin;
    float   tileSize;
    uint    lod;
    uint    y;
    uint    x;
};



class quadtree_tile
{
public:
    void init(uint _index);
    void set(uint _lod, uint _x, uint _y, float _size, float4 _origin, quadtree_tile* _parent);
    void reset() {
        forSplit = false;
        forRemove = false;
    }

    uint index;
    // Frame this pool slot was (re)allocated. Guards the async tileCenters
    // readback against patching a fresh tile's bounding sphere with the height
    // of the slot's PREVIOUS occupant.
    uint32_t bornFrame = 0;
    // False until the tileCenters readback patched boundingSphere.y with this
    // tile's own baked centre height. While false the frustum tests treat the
    // tile as a column over the whole terrain elevation range (see
    // tileInFrustum) - its inherited height must never cull it.
    bool heightPatched = false;
    quadtree_tile* parent;
    quadtree_tile* child[4];

    float4  boundingSphere;		// position has to be power of two to allow us to store large world offsets using float rather than double
    float4  origin;
    float   size;
    uint    lod;
    uint    y;
    uint    x;

    bool    	forSplit = false;
    bool	    forRemove = false;          // the children
    bool    	main_ShouldSplit = false;
    bool	    env_ShouldSplit = false;

    uint    numQuads = 0;
    uint    numPlants = 0;
    uint    elevationHash;
    uint    imageHash;
};


// These are all the camera flags that terrain tiles on the GPU needs to understand in order to render the correct tiles
enum CameraType {
    CameraType_Main_Left,
    CameraType_Main_Center,
    CameraType_Main_Right,
    CameraType_Rear_Left,
    CameraType_Rear_Center,
    CameraType_Rear_Right,
    CameraType_Cascade_0,       // likely to i=only update 1 but maybe this is better
    CameraType_Cascade_1,
    CameraType_Cascade_2,
    CameraType_Cascade_3,
    CameraType_Cube_1,
    CameraType_Cube_2,
    CameraType_Cube_3,
    CameraType_Cube_4,
    CameraType_Cube_5,
    CameraType_Cube_6,
    CameraType_Parabolic_low,       // really just a scale factor, but all round
    CameraType_Parabolic_medium,    // really just a scale factor, but all round
    CameraType_MAX,
};



struct terrainCamera {
    bool bUse;
    float3 position;
    glm::mat4x4 view;
    glm::mat4x4 proj;
    glm::mat4x4 viewProj;
    glm::mat4x4 viewProjTranspose;
    float resolution;
    std::array<float4, 4> frustumPlane;
    glm::mat4x4 frustumMatrix;
};

struct heightMap {
    float2 origin;
    float size;
    float hgt_offset;
    float hgt_scale;
    std::string filename;	// jpeg2000 file

    // for export
    uint lod;
    uint y;
    uint x;
};

struct jp2Map {
    void set(uint lod, uint y, uint x, float wSize = 40000.f, float wOffset = -20000.f);
    void save(std::ofstream& _os);
    void saveBinary(std::ofstream& _os);
    void loadBinary(std::ifstream& _is);

    uint lod;
    uint y;
    uint x;

    float2 origin;
    float size;

    float hgt_offset = 0;       // elevation on;y
    float hgt_scale = 0;

    uint fileOffset = 0;
    uint sizeInBytes = 0;


    template<class Archive>
    void serialize(Archive& archive)
    {
        archive(CEREAL_NVP(lod));
        archive(CEREAL_NVP(y));
        archive(CEREAL_NVP(x));

        archive(CEREAL_NVP(origin.x));
        archive(CEREAL_NVP(origin.y));
        archive(CEREAL_NVP(size));

        archive(CEREAL_NVP(hgt_offset));
        archive(CEREAL_NVP(hgt_scale));

        archive(CEREAL_NVP(fileOffset));
        archive(CEREAL_NVP(sizeInBytes));
    }
};

struct jp2File
{
    void save(std::ofstream& _os);
    void saveBinary(std::ofstream& _os);
    void loadBinary(std::ifstream& _is);
    std::string filename;
    std::vector<jp2Map> tiles;
    uint sizeInBytes;
    uint32_t hash;


    template<class Archive>
    void serialize(Archive& archive)
    {
        archive(CEREAL_NVP(filename));
        archive(CEREAL_NVP(hash));
        archive(CEREAL_NVP(sizeInBytes));
        archive(CEREAL_NVP(tiles));
    }
};


struct jp2data {
    std::vector<unsigned char> data;
};

struct jp2Dir
{
    void save(std::string _name);
    void load(std::string _name);
    void cache0(std::string _path);
    void cacheHash(uint32_t hash);
    std::string path;


    void saveBinary(std::string _name);
    void loadBinary(std::string _name);


    std::vector<jp2File> files;

    std::map<uint32_t, uint> fileHashmap;
    std::map<uint32_t, uint2> tileHash;
    LRUCache<uint32_t, std::shared_ptr<std::vector<unsigned char>>> cache;
    //LRUCache<uint32_t, std::vector<unsigned char>> cache;
    std::vector<unsigned char> dataRoot;

    template<class Archive>
    void serialize(Archive& archive)
    {
        archive(files);
    }
};



enum class _terrainMode { vegetation, ecotope, terrafector, roads, glider, terrainBuilder, textureTool };

class terrainManager
{
public:
    terrainManager();
    virtual ~terrainManager();

    void onLoad(ew::GpuContext* _renderContext);
    void onShutdown();


    void onFrameRender(ew::GpuContext* pRenderContext, const ew::Fbo::SharedPtr& _fbo, ew::Camera::SharedPtr _camera);
    bool onKeyEvent(const ew::KeyboardEvent& keyEvent);
    bool onMouseEvent(const ew::MouseEvent& mouseEvent, glm::vec2 _screenSize, glm::vec2 _mouseScale, glm::vec2 _mouseOffset, ew::Camera::SharedPtr _camera);
    // Not implemented: the onMouseEvent_Roads / onMouseEvent_Stamps / onHotReload editor flows

    void init_TopdownRender();
    void allocateTiles(uint numT);			// ??? FIXME pass shader in as well to allocate GPU memory
    void reset(bool _fullReset = false);
    void loadElevationHash(ew::GpuContext* pRenderContext);
    void loadImageHash(ew::GpuContext* pRenderContext);


    void clearCameras();
    void setCamera(unsigned int _index, glm::mat4 viewMatrix, glm::mat4 projMatrix, float3 position, bool b_use, float _resolution);

    void updateShaderConstants(ew::Texture::SharedPtr _previousFrame, shaderLightBuffer _buffer);
    bool update(ew::GpuContext* pRenderContext);

    // No shadowSetup/shadowRender* and no shadow-map type - terrain shadows come from _shadowEdges below

    static _lastFile lastfile;

    /// Explicit terrain to load (directory or *.terrainSettings.json path).
    /// Set from the command line (`-terrain X`) before onLoad; bypasses
    /// lastFile.xml when non-empty.
    static std::string sTerrainOverride;

    /// True once lastFile.xml + terrainSettings resolved and the GPU pools exist.
    bool isLoaded() const { return m_loaded; }
    std::string getTerrainName() const { return m_loaded ? settings.name : std::string{}; }

    /// Read-only view of the live quadtree (host debug ground grid).
    const std::list<quadtree_tile*>& usedTiles() const { return m_used; }

    // World-space rectangles of the quadtree LEAF tiles that passed the
    // main-camera frustum test in calculateSurfaceFlags (frustumFlags .y bit),
    // one float4 per tile as (origin.x, origin.z, size, lod). Used to skip
    // building chunks over invisible terrain. Empty when update() has not run
    // for the current mode - callers treat that as "no info" and draw all.
    void getVisibleTileRects(std::vector<float4>& _out) const
    {
        _out.clear();
        for (const quadtree_tile* t : m_used)
        {
            if (!t->child[0] && (frustumFlags[t->index].y & (1u << CameraType_Main_Center)))
                _out.push_back(float4(t->origin.x, t->origin.z, t->size, (float)t->lod));
        }
    }

private:
    void calculateSurfaceFlags();
    void testForSurfaceEnv();
    bool testForSplit(quadtree_tile* _tile);
    bool testFrustum(quadtree_tile* _tile);
    void markChildrenForRemove(quadtree_tile* _tile);

public:
    int hashCount = 0;
    ew::Texture::SharedPtr cacheTexture;
    uint cacheHash;
    std::future<void> hashFuture;
    std::vector<unsigned short> jphData;

    int hashCountImage = 0;
    ew::Texture::SharedPtr cacheTextureImage;
    uint cacheHashImage;
    std::future<void> hashIFuture;
    std::vector<unsigned char> jphImageData;

    void hashAndCache_Thread(quadtree_tile* pTile);
    void hashAndCacheImages_Thread(quadtree_tile* pTile);
private:
    bool hashAndCache(quadtree_tile* pTile);
    bool hashAndCacheImages(quadtree_tile* pTile);
    void setChild(quadtree_tile* pTile, int y, int x);
    void splitOne(ew::GpuContext* _renderContext);
    void splitChild(quadtree_tile* _pTile, ew::GpuContext* _renderContext);
    void splitRenderTopdown(quadtree_tile* _pTile, ew::GpuContext* _renderContext);

    // Not implemented: the offline EVO export (bake_start/bake_frame/
    // bake_Setup/bake_RenderTopdown/sceneToMax). It needs a synchronous texture
    // readback plus a JP2 re-encode. Its bake_RenderTopdown is
    // splitRenderTopdown with the bSplineAsTerrafector gates always taken - an
    // offline bake ALWAYS bakes roads; keep that difference exact if it lands.

    void updateDynamicRoad(bool _bezierChanged);
    void updateDynamicStamp();

    void bezierRoadstoLOD(uint _lod);



    uint                        numTiles = 997;
    std::vector<quadtree_tile>	m_tiles;
    std::list<quadtree_tile*>	m_free;
    std::list<quadtree_tile*>	m_used;
    uint4 frustumFlags[1024];
public:
    bool fullResetDoNotRender = false;
private:
#ifdef COMPUTE_DEBUG_OUTPUT
    bool debug = true;
#else
    bool debug = false;
#endif

    bool m_loaded = false;

    /// Monotonic update() counter - pairs with quadtree_tile::bornFrame and
    /// the readback ring's tag to age-gate bounding-sphere patches.
    uint32_t m_frameCounter = 0;

    std::array<terrainCamera, CameraType_MAX> cameraViews;
    float3 cameraOrigin;

    ew::Texture::SharedPtr compressed_Normals_Array;
    ew::Texture::SharedPtr compressed_Albedo_Array;
    ew::Texture::SharedPtr compressed_PBR_Array;
    ew::Texture::SharedPtr height_Array;
public:
    ew::pixelShader terrainShader;
    ew::pixelShader terrainSpiteShader;
    ew::pixelShader triangleShader;
    _terrainSettings settings;

private:

    ew::Buffer::SharedPtr       triangleData;        // skydome cube.fbx triangles
    ew::computeShader		compute_bakeFloodfill;   // loaded but never dispatched from terrain - the live dispatch is vegetationBuilder's billboard bake gutter fill

    // Not implemented: the ribbon (render_ribbons) / veghuman / rappersville
    // members and bakeFbo_plants.

    ecotopeSystem			mEcosystem;

public:
    // The vegetation runtime hub. Public because the app hands it the
    // atmosphere textures BEFORE terrain.onLoad runs.
    _rootPlant              plants_Root;
private:

public:
    struct
    {
        double terrainCacheTime;
        double terrainCacheJPHTime;
        double imageCacheTime;
        double imageCacheJPHTime;
        double imageCacheIOTime;
    } stream = {};   // IO and feedback

    _terrainMode terrainMode = _terrainMode::roads;   // default to a mode that renders terrain (vegetation mode early-outs update())
private:
    bool hasChanged = false;


    glm::vec3 mouseDirection;
    glm::vec3 mousePosition;
    glm::vec2 mousePositionOld;
    glm::vec2 screenSize;
    glm::vec2 mouseCoord;
    ew::computeShader compute_TerrainUnderMouse;    // picking - feeds split.feedback.tum_*, used by runtime camera pan/orbit/zoom and by road/stamp editing

    std::map<uint32_t, heightMap> elevationTileHashmap;
    struct textureCacheElement {
        ew::Texture::SharedPtr	  texture = nullptr;
    };
    LRUCache<uint32_t, textureCacheElement> elevationCache;

    jp2Dir imageDirectory;
    //std::map<uint32_t, heightMap> imageTileHashmap;
    LRUCache<uint32_t, textureCacheElement> imageCache; // move t indese jp2 calss

    terrafectorSystem		terrafectors;

    roadNetwork			    mRoadNetwork;
    splineTest			    splineTest;
    // Not implemented: the sprite marker renderer (road/stamp editing markers
    // plus the static sprite world). The marker calls below are stubbed
    // in-place.

    stampCollection         mRoadStampCollection;   // all of teh terrafector stamps going over roads
    stamp                   mCurrentStamp;
    int                     stampEditPosisiton = 0;
    void loadStamp()
    {
        std::ifstream is(mRoadStampCollection.lastUsedFilename, std::ios::binary);
        if (!is.fail()) {
            cereal::BinaryInputArchive archive(is);
            archive(mRoadStampCollection);


            mRoadStampCollection.reloadMaterials();
            terrafectorEditorMaterial::static_materials.rebuildAll();
            allStamps_to_Terrafector();
        }
    }
    void saveStamp()
    {
        std::ofstream os(mRoadStampCollection.lastUsedFilename, std::ios::binary);
        if (!os.fail()) {
            cereal::BinaryOutputArchive archive(os);
            archive(mRoadStampCollection);
        }
    }
    void stamp_to_Bezier(stamp& S, cubicDouble* BEZ, bezierLayer* IDX, int _index);
    void currentStamp_to_Bezier();
    void allStamps_to_Bezier();
    void allStamps_to_Terrafector();


    // Mirrored from ew::gDebug.toggles.tfBakeRoads on every update() so roads
    // can be A/B'd live; that toggle defaults to ON, so the value below only
    // holds until the first update().
    bool bSplineAsTerrafector = false;
    bool showRoadOverlay = true;
    bool showRoadSpline = true;

    struct {
        glm::vec4 box;
        bool show = true;
        float strenght = 0.3f;

        float redStrength = 0.0f;
        float redScale = 5.0f;
        float redOffset = 0.05f;

        float terrafectorOverlayStrength = 0.1f;
        float splineOverlayStrength = 1.f;
        bool bakeBakeOnlyData = true;
    }gis_overlay;



public:
    struct
    {
        uint                bakeSize = 1024;
        ew::Fbo::SharedPtr		tileFbo;
        // Not implemented: bakeFbo, the 1024^2 offline export twin (see the
        // offline-bake note above)

        ew::Texture::SharedPtr	noise_u16;

        ew::computeShader		compute_tileClear;
        ew::computeShader		compute_tileSplitMerge;
        // (no compute_tileGenerate member - that shader has no live dispatch)
        ew::computeShader		compute_tilePassthrough;
        ew::computeShader		compute_tileBuildLookup;
        ew::computeShader		compute_tileBicubic;		// upsample heights with bicubic filter
        ew::computeShader		compute_tileEcotopes;
        ew::computeShader		compute_tileNormals;
        ew::computeShader		compute_tileVerticis;
        ew::computeShader		compute_tileJumpFlood;
        ew::computeShader		compute_tileDelaunay;
        ew::computeShader		compute_clipLodAnimatePlants;

        // BC6H compressor
        ew::computeShader           compute_bc6h;
        ew::Texture::SharedPtr      bc6h_texture;

        ew::Buffer::SharedPtr       dispatchArgs_plants;    // numRenderViews in size
        ew::Buffer::SharedPtr       drawArgs_quads;
        ew::Buffer::SharedPtr       drawArgs_tiles;         // block based

        ew::Buffer::SharedPtr       buffer_feedback;
        ew::ReadbackBuffer::SharedPtr buffer_feedback_read;   // fence-checked ring, 1-2 frames of latency - never stalls
        GC_feedback             feedback;

        std::vector<gpuTile>    cpuTiles;
        ew::Buffer::SharedPtr       buffer_tiles;
        ew::Buffer::SharedPtr       buffer_instance_quads;
        ew::Buffer::SharedPtr       buffer_instance_plants;
        ew::Buffer::SharedPtr       buffer_clippedloddedplants;     // clipLod xformed_PLANT output (32 MB); no live draw consumes it

        ew::Buffer::SharedPtr       buffer_lookup_terrain[numRenderViews];
        ew::Buffer::SharedPtr       buffer_lookup_quads[numRenderViews];
        ew::Buffer::SharedPtr       buffer_lookup_plants[numRenderViews];

        ew::Buffer::SharedPtr	    buffer_tileCenters;
        ew::ReadbackBuffer::SharedPtr buffer_tileCenter_readback;  // latency ring, see buffer_feedback_read
        std::array<float4, 2048>		tileCenters;

        ew::Buffer::SharedPtr       buffer_terrain;

        ew::Texture::SharedPtr      debug_texture;
        ew::Texture::SharedPtr      normals_texture;
        ew::Texture::SharedPtr      vertex_A_texture;
        ew::Texture::SharedPtr      vertex_B_texture;
        ew::Texture::SharedPtr      vertex_clear;		                // 0 for fast clear
        ew::Texture::SharedPtr      vertex_preload;	                    // a pre allocated 1/8 verts

        ew::Texture::SharedPtr	    rootElevation;

        // The terrafector/road bake + overlay shaders and states, all built in
        // init_TopdownRender. depthstateCloser/Futher have no user - they are
        // cheap descs kept for the missing bake paths.
        ew::pixelShader             shader_spline3D;
        ew::pixelShader             shader_splineTerrafector;
        ew::pixelShader             shader_meshTerrafector;             // these two should merge
        Diligent::DepthStencilStateDesc depthstateCloser;
        Diligent::DepthStencilStateDesc depthstateFuther;
        Diligent::DepthStencilStateDesc depthstateAll;
        Diligent::RasterizerStateDesc   rasterstateSplines;
        Diligent::BlendStateDesc        blendstateSplines;
        Diligent::BlendStateDesc        blendstateRoadsCombined;
    } split;

    struct
    {
        uint32_t maxBezier = 131072;            // 17 bits packed - likely to change soon
        uint32_t maxIndex = 524288;             // *4 seems enough, 2022 at *1.7 for Nurburg
        uint32_t numStaticSplines = 0;
        uint32_t numStaticSplinesIndex = 0;
        uint32_t numStaticSplinesBakeOnlyIndex = 0;
        ew::Buffer::SharedPtr bezierData;
        ew::Buffer::SharedPtr indexData;
        ew::Buffer::SharedPtr indexDataBakeOnly;
        ew::Buffer::SharedPtr indexData_LOD4;
        ew::Buffer::SharedPtr indexData_LOD6;
        ew::Buffer::SharedPtr indexData_LOD8;
        uint startOffset_LOD4[16][16];
        uint numIndex_LOD4[16][16];
        uint startOffset_LOD6[64][64];
        uint numIndex_LOD6[64][64];
        uint startOffset_LOD8[256][256];
        uint numIndex_LOD8[256][256];
        std::vector<bezierLayer> lod4[16][16];
        std::vector<bezierLayer> lod6[64][64];
        std::vector<bezierLayer> lod8[256][256];

        uint32_t maxDynamicBezier = 4096;            // 17 bits packed - likely to change soon
        uint32_t maxDynamicIndex = 16384;             // *4 seems enough, 2022 at *1.7 for Nurburg
        uint32_t numDynamicSplines = 0;
        uint32_t numDynamicSplinesIndex = 0;
        uint32_t numDynamicStampIndex = 0;
        ew::Buffer::SharedPtr dynamic_bezierData;
        ew::Buffer::SharedPtr dynamic_indexData;
        uint numIndex = 0;
    }splines;

    struct
    {
        bool        hit;
        glm::vec3   terrain;
        float       panDistance;
        float       cameraHeight;
        glm::vec3   toGround;
        glm::vec3   pan;
        glm::vec3   orbit;
        float       orbitRadius;
        float       mouseToHeightRatio;

        glm::vec3   newPosition;
        glm::vec3   newTarget;
    }mouse;
public:
    ew::Sampler::SharedPtr			sampler_Trilinear;
    ew::Sampler::SharedPtr			sampler_Clamp;
    ew::Sampler::SharedPtr			sampler_ClampAnisotropic;
    ew::Sampler::SharedPtr			sampler_Ribbons;


    _shadowEdges shadowEdges;
    ew::Texture::SharedPtr	  terrainShadowTexture;

    // Far-LOD buildings (step-6 salvage; replaces the original's commented-out
    // rappersville blocks - see buildings.h). Public: Earthworks_4 hands it
    // the atmosphere textures and the shadow-caster overlay.
    buildingsRenderer   buildings;


    struct
    {
        bool show = false;
        ew::Texture::SharedPtr	  skyTexture;
        ew::Texture::SharedPtr	  envTexture;
        ew::Texture::SharedPtr	  dappledLightTexture;
    }vegetation;

public:

    // Zero is not allowed and these are small so stick to 256 maybe

    uint lookupSizeTerrain[numRenderViews] =
    { 524288, 524288, 256, 256, 256, 256, 1024, 1024, 1024, 1024, 65536, 65536, 65536, 65536, 65536, 65536, 16384, 32768 };
    // 524288 = 33M triangles
    uint lookupSizeBillboard[numRenderViews] =
    { 256, 131072, 256, 256, 256, 256, 1024, 1024, 1024, 1024, 16384, 16384, 16384, 16384, 16384, 16384, 8000, 16384 };
    //131072 = 8 million billboards
    // 16384  = 1 million maybe define them as susch
    // cube views also need less both u[ and down

    uint lookupSizePlants[numRenderViews] =
    { 256, 131072, 256, 256, 256, 256, 1024, 1024, 1024, 1024, 16384, 16384, 16384, 16384, 16384, 16384, 8000, 16384 };
    std::string viewNames[numRenderViews] = { "left", "main", "right", "rearL", "rear", "rearR", "casc0", "casc1" , "casc2" , "casc3",
        "cube0", "cube1", "cube2", "cube3", "cube4", "cube5", "para_1", "para_2" };

    uint viewMask = main_LEFT | main_CENTER | cascade_0 | cascade_1 | cascade_2 | parabolic_low | parabolic_medium;

    // Not implemented: the terrainGenerator / newTerrainBuilder pipeline tool
};
