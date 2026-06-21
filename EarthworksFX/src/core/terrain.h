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
#include "FalcorCompat.hpp"
#include "computeShader.h"
#include "pixelShader.h"

#include <thread>
#include "barrier.hpp"

#include"terrafector.h"
#include "roadNetwork.h"
#include "Sprites.h"
#include "ecotope.h"

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

#if defined(EARTHWORKS_WITH_OPENJPH)
#if defined(__has_include) && __has_include(<openjph/ojph_codestream.h>)
#include <openjph/ojph_arg.h>
#include <openjph/ojph_mem.h>
#include <openjph/ojph_file.h>
#include <openjph/ojph_codestream.h>
#include <openjph/ojph_params.h>
#include <openjph/ojph_message.h>
#else
#include <ojph_arg.h>
#include <ojph_mem.h>
#include <ojph_file.h>
#include <ojph_codestream.h>
#include <ojph_params.h>
#include <ojph_message.h>
#endif
#else
#error "EarthworksFX requires EARTHWORKS_WITH_OPENJPH (OpenJPH via Earthworks)."
#endif

#include "vegetationBuilder.h"
 //#include"hlsl/terrain/vegetation_defines.hlsli"
 //#pragma optimize("", off)

//#include "cfd.h"
#include <future> // Required for std::async and std::future
#include <thread> // Required for std::this_thread::sleep_for

using namespace Falcor;




struct _shadowEdges
{
    float height[4096][4096];
    float Nx[4095][4095];   // temp
    //float3 norm[4095][4095];
    unsigned char edge[4096][4096];

    float2 shadowH[4096][4096];

    void load(std::string filename, float _angle);
    void solve(float _angle, bool dx);

    float sunAngle = 0.02f;  // just afetr sunruise
    float dAngle = 0.01f;
    float3 sunAng;
    bool shadowReady = false;
    bool requestNewShadow = false;
    void solveThread();
};








template<typename K, typename V = K>
class LRUCache
{

private:
    std::list<K>items;
    std::unordered_map <K, std::pair<V, typename std::list<K>::iterator>> keyValuesMap;
    uint csize = 50;	// arbitrary default

public:
    LRUCache() { ; }

    void resize(uint s) {
        csize = s;
        items.clear();
        keyValuesMap.clear();
    }

    void set(const K key, const V value) {
        auto pos = keyValuesMap.find(key);
        if (pos == keyValuesMap.end()) {
            items.push_front(key);
            keyValuesMap[key] = { value, items.begin() };
            if (keyValuesMap.size() > csize) {
                keyValuesMap.erase(items.back());
                items.pop_back();
            }
        }
        else {
            items.erase(pos->second.second);
            items.push_front(key);
            keyValuesMap[key] = { value, items.begin() };
        }
    }

    bool get(const K key, V& value) {
        auto pos = keyValuesMap.find(key);
        if (pos == keyValuesMap.end())
            return false;
        items.erase(pos->second.second);
        items.push_front(key);
        keyValuesMap[key] = { pos->second.first, items.begin() };
        value = pos->second.first;
        return true;
    }
};


struct _lastFile
{
    // Default test terrain: folder under pwd (see terrains/<id>/elevations.txt).
    std::string terrain = "terrains/switserland_Steg/terrainSettings.json";
    std::string road = "";
    std::string stamps = "";
    std::string roadMaterial = "";
    std::string terrafectorMaterial = "";
    std::string texture = "";
    std::string fbx = "";
    std::string EVO = "";

    std::string weed = "terrains/_resources/vegetation_weeds/";
    std::string twig = "terrains/_resources/vegetation_twigs/";
    std::string leaves = "terrains/_resources/vegetation_leaves/";
    std::string trees = "terrains/_resources/vegetation_trees/";
    std::string vegMaterial = "terrains/_resources/vegetation_trees/";


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
        if (_version > 100)
        {
            _archive(CEREAL_NVP(weed));
            _archive(CEREAL_NVP(twig));
            _archive(CEREAL_NVP(leaves));
            _archive(CEREAL_NVP(trees));
            _archive(CEREAL_NVP(vegMaterial));
        }
        if (_version > 101)
        {
            _archive(CEREAL_NVP(stamps));
        }
    }
};
CEREAL_CLASS_VERSION(_lastFile, 102);


struct _terrainSettings
{
    std::string name = "switserland_Steg";
    std::string projection = "\" + proj = tmerc + lat_0 = 46.8 + lon_0 = 8.5 + k_0 = 1 + x_0 = 0 + y_0 = 0 + ellps = WGS84 + units = m\"";
    float size = 40000.f;

    // Folder root: terrains/<id>/elevations.txt and terrains/<id>/elevations/*
    std::string dirRoot = "terrains/switserland_Steg";
    std::string dirExport = "terrains/switserland_Steg";
    std::string dirGis = "terrains/switserland_Steg";
    std::string dirResource = "terrains/_resources";

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

    void renderGui(Gui* _gui);
};
CEREAL_CLASS_VERSION(_terrainSettings, 100);



// FOR binary export of tiles
struct elevationMap {
    float heightOffset;
    float heightScale;
    float2 origin;
    float tileSize;
    uint lod;
    uint y;
    uint x;
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

private:
public:
    uint index;
    quadtree_tile* parent;
    quadtree_tile* child[4];

    float4 boundingSphere;		// position has to be power of two to allow us to store large world offsets using float rather than double
    float4 origin;
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


enum CameraType {
    CameraType_Main_Left,
    CameraType_Main_Center,
    CameraType_Main_Right,
    CameraType_Rear_Left,
    CameraType_Rear_Center,
    CameraType_Rear_Right,
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

#include "atmosphere.h"

// CPU mirror of hlsl/terrain/render_Buildings_Far.hlsl _buildingVertex
struct _buildingVertex
{
    glm::vec3  pos;
    uint32_t   material;
    glm::vec3  normal;
    float      something;
    glm::vec2  uv;
    glm::vec2  somethignesle;
};

// CPU mirror of hlsl/terrain/render_GliderWing.hlsl _gliderwingVertex
struct _gliderwingVertex
{
    glm::vec3  pos;
    uint32_t   material;
    glm::vec3  normal;
    float      something;
    glm::vec2  uv;
    glm::vec2  somethignesle;
};

enum class _terrainMode { vegetation, ecotope, terrafector, roads, glider };

class terrainManager
{
public:
    terrainManager();
    virtual ~terrainManager();

    void onLoad(RenderContext* _renderContext, FILE* _logfile);
    void onShutdown();
    void onGuiRender(Gui* pGui, fogAtmosphericParams* pAtmosphere);
    //void onGuiRendercfd(Gui::Window& _window, Gui* pGui, float2 _screen);
    //void onGuiRendercfd_params(Gui::Window& _window, Gui* pGui, float2 _screen);
    //void onGuiRendercfd_skewT(Gui::Window& _window, Gui* pGui, float2 _screen);
    bool renderGui_Menu = false;
    bool renderGui_Hud = true;
    void onGuiMenubar(Gui* pGui);
    void onFrameRender(RenderContext* pRenderContext, const Fbo::SharedPtr& _fbo, Camera::SharedPtr _camera, GraphicsState::SharedPtr _graphicsState, GraphicsState::Viewport _viewport, Texture::SharedPtr _hdrHalfCopy);
    bool onKeyEvent(const KeyboardEvent& keyEvent);
    bool onMouseEvent(const MouseEvent& mouseEvent, glm::vec2 _screenSize, glm::vec2 _mouseScale, glm::vec2 _mouseOffset, Camera::SharedPtr _camera);
    bool onMouseEvent_Roads(const MouseEvent& mouseEvent, glm::vec2 _screenSize, Camera::SharedPtr _camera);
    bool onMouseEvent_Stamps(const MouseEvent& mouseEvent, glm::vec2 _screenSize, Camera::SharedPtr _camera);
    void onHotReload(HotReloadFlags reloaded);

    void init_TopdownRender();
    void allocateTiles(uint numT);			// ??? FIXME pass shader in as well to allocate GPU memory
    void reset(bool _fullReset = false);
    void loadElevationHash(RenderContext* pRenderContext);
    void loadImageHash(RenderContext* pRenderContext);

    void bil_to_jp2();
    void bil_to_jp2(std::string file, const uint size, FILE* summary, uint _lod, uint _y, uint _x, float _xstart, float _ystart, float _size);

    void generateGdalPhotos();
    void writeGdal(jp2Map _map, std::ofstream& _gdal, std::string _input);
    void writeGdalClear(std::ofstream& _gdal);
    void bil_to_jp2Photos();
    void bil_to_jp2Photos(std::string file, const uint size, uint _lod, uint _y, uint _x);
    uint bil_to_jp2PhotosMemory(std::ofstream& _file, std::string filename, const uint size, uint _lod, uint _y, uint _x);

    void clearCameras();
    void setCamera(unsigned int _index, glm::mat4 viewMatrix, glm::mat4 projMatrix, float3 position, bool b_use, float _resolution);
    bool update(RenderContext* pRenderContext);

    static _lastFile lastfile;

private:
    void testForSurfaceMain();
    void testForSurfaceEnv();
    bool testForSplit(quadtree_tile* _tile);
    bool testFrustum(quadtree_tile* _tile);
    void markChildrenForRemove(quadtree_tile* _tile);

public:
    int hashCount = 0;
    Texture::SharedPtr cacheTexture;
    uint cacheHash;
    std::future<void> hashFuture;
    std::vector<unsigned short> jphData;

    int hashCountImage = 0;
    Texture::SharedPtr cacheTextureImage;
    uint cacheHashImage;
    std::future<void> hashIFuture;
    std::vector<unsigned char> jphImageData;

    void hashAndCache_Thread(quadtree_tile* pTile);
    void hashAndCacheImages_Thread(quadtree_tile* pTile);
private:
    bool hashAndCache(quadtree_tile* pTile);
    bool hashAndCacheImages(quadtree_tile* pTile);
    void setChild(quadtree_tile* pTile, int y, int x);
    void splitOne(RenderContext* _renderContext);
    void splitChild(quadtree_tile* _pTile, RenderContext* _renderContext);
    void splitRenderTopdown(quadtree_tile* _pTile, RenderContext* _renderContext);

    uint gisHash(glm::vec3 _position);
    void gisReload(glm::vec3 _position);

    void bake_start(bool _toMAX);
    bool bakeToMax;
    void bake_frame();
    void bake_Setup(float _size, uint lod, uint y, uint x, RenderContext* _renderContext);
    void bake_RenderTopdown(float _size, uint lod, uint y, uint x, RenderContext* _renderContext);
    void sceneToMax();
    int exportLodMin = 10;
    int exportLodMax = 10;

    void updateDynamicRoad(bool _bezierChanged);
    void updateDynamicStamp();

    void bezierRoadstoLOD(uint _lod);

    

    uint                        numTiles = 997;
    std::vector<quadtree_tile>	m_tiles;
    std::list<quadtree_tile*>	m_free;
    std::list<quadtree_tile*>	m_used;
    unsigned int frustumFlags[2048];
public:
    bool fullResetDoNotRender = false;
private:
#ifdef COMPUTE_DEBUG_OUTPUT
    bool debug = true;
#else
    bool debug = false;
#endif

    std::array<terrainCamera, CameraType_MAX> cameraViews;
    float3 cameraOrigin;

    Texture::SharedPtr compressed_Normals_Array;
    Texture::SharedPtr compressed_Albedo_Array;
    Texture::SharedPtr compressed_PBR_Array;
    Texture::SharedPtr height_Array;
public:
    pixelShader terrainShader;
    pixelShader terrainSpiteShader;
    pixelShader triangleShader;
    _terrainSettings settings;

private:

    Texture::SharedPtr	  spriteTexture = nullptr;
    Texture::SharedPtr	  spriteNormalsTexture = nullptr;


    pixelShader ribbonShader;
    uint numLoadedRibbons;  // paraglider only
    Buffer::SharedPtr       ribbonData[2];  // also paraglider  - split these into seperate block at least, not true groveTree.bChanged writes to this, so duplicate maybe
    uint bufferidx = 0;

    Buffer::SharedPtr       triangleData;
    Fbo::SharedPtr		bakeFbo_plants;
    GraphicsState::Viewport     viewportVegbake;
    //bool bakeOneVeg = false;
    BlendState::SharedPtr           blendstateVegBake;
    computeShader		compute_bakeFloodfill;
    int ribbonInstanceNumber = 1;
    float ribbonSpacing = 3.0f;             // the size fo the extents
    bool spacingFromExtents = true;

    pixelShader thermalsShader;
    Buffer::SharedPtr       thermalsData;
    uint numThermals = 200;

    pixelShader cfdSliceShader;

public:
    pixelShader         rappersvilleShader;
    Buffer::SharedPtr   rappersvilleData;
    //Buffer::SharedPtr   drawArgs_rappersville;
    int numrapperstri;

    pixelShader         gliderwingShader;
    uint    wingloadedCnt;
    Buffer::SharedPtr   gliderwingData[2];
    RasterizerState::SharedPtr      rasterstateGliderWing;

    bool useFreeCamWhileGliding = false;
    bool GliderDebugVisuals = false;

    
    struct
    {
        //void cacheTerrain();
        //void cacheImage();
        double terrainCacheTime;
        double terrainCacheJPHTime;
        double imageCacheTime;
        double imageCacheJPHTime;
        double imageCacheIOTime;
    } stream;   // IO and feedback

    _terrainMode terrainMode = _terrainMode::roads;
private:
    bool hasChanged = false;

    bool requestPopupSettings = false;
    bool requestPopupDebug = false;

    glm::vec3 mouseDirection;
    glm::vec3 mousePosition;
    glm::vec2 mousePositionOld;
    glm::vec2 screenSize;
    glm::vec2 mouseCoord;
    float mouseVegOrbit = 10;
    float mouseVegPitch = 0.1f;
    float mouseVegYaw = 0.0f;
    float mouseVegRoll = 0.0f;
    computeShader compute_TerrainUnderMouse;

    std::map<uint32_t, heightMap> elevationTileHashmap;
    struct textureCacheElement {
        Texture::SharedPtr	  texture = nullptr;
    };
    LRUCache<uint32_t, textureCacheElement> elevationCache;

    jp2Dir imageDirectory;
    //std::map<uint32_t, heightMap> imageTileHashmap;
    LRUCache<uint32_t, textureCacheElement> imageCache; // move t indese jp2 calss

    terrafectorSystem		terrafectors;
    ecotopeSystem			mEcosystem;

    roadNetwork			    mRoadNetwork;
    splineTest			    splineTest;
    spriteRender			mSpriteRenderer;

public:
    _rootPlant       plants_Root;
    float               billboardGpuTime;
private:


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


    bool bSplineAsTerrafector = false;
    bool showRoadOverlay = true;
    bool showRoadSpline = true;

    bool bLeftButton = false;
    bool bMiddelButton = false;
    bool bRightButton = false;

    struct {
        Texture::SharedPtr texture = nullptr;
        uint hash = 0;
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





    struct {
        uint                bakeSize = 1024;
        Fbo::SharedPtr		tileFbo;
        Fbo::SharedPtr		bakeFbo;

        Texture::SharedPtr	noise_u16;

        computeShader		compute_tileClear;
        computeShader		compute_tileSplitMerge;
        computeShader		compute_tileGenerate;
        computeShader		compute_tilePassthrough;
        computeShader		compute_tileBuildLookup;
        computeShader		compute_tileBicubic;		// upsample heights with bicubic filter
        computeShader		compute_tileEcotopes;
        computeShader		compute_tileNormals;
        computeShader		compute_tileVerticis;
        computeShader		compute_tileJumpFlood;
        computeShader		compute_tileDelaunay;
        //computeShader		compute_tileElevationMipmap;
        computeShader		compute_clipLodAnimatePlants;

        // BC6H compressor
        computeShader           compute_bc6h;
        Texture::SharedPtr      bc6h_texture;

        Buffer::SharedPtr       dispatchArgs_plants;
        Buffer::SharedPtr       drawArgs_quads;
        Buffer::SharedPtr       drawArgs_plants;
        Buffer::SharedPtr       drawArgs_clippedloddedplants;
        Buffer::SharedPtr       drawArgs_tiles;         // block based
        Buffer::SharedPtr       buffer_feedback;
        Buffer::SharedPtr		buffer_feedback_read;
        GC_feedback             feedback;

        std::vector<gpuTile>    cpuTiles;
        Buffer::SharedPtr       buffer_tiles;
        Buffer::SharedPtr       buffer_tiles_readback;
        Buffer::SharedPtr       buffer_instance_quads;
        Buffer::SharedPtr       buffer_instance_plants;
        Buffer::SharedPtr       buffer_clippedloddedplants;

        Buffer::SharedPtr       buffer_lookup_terrain;
        Buffer::SharedPtr       buffer_lookup_quads;
        Buffer::SharedPtr       buffer_lookup_plants;

        Buffer::SharedPtr	    buffer_tileCenters;
        Buffer::SharedPtr		buffer_tileCenter_readback;
        std::array<float4, 2048>		tileCenters;

        Buffer::SharedPtr       buffer_terrain;

        Texture::SharedPtr      debug_texture;
        //Texture::SharedPtr      bicubic_upsample_texture;
        Texture::SharedPtr      normals_texture;
        Texture::SharedPtr      vertex_A_texture;
        Texture::SharedPtr      vertex_B_texture;
        Texture::SharedPtr      vertex_clear;		                // 0 for fast clear
        Texture::SharedPtr      vertex_preload;	                    // a pre allocated 1/8 verts

        Texture::SharedPtr	    rootElevation;

        pixelShader             shader_spline3D;
        pixelShader             shader_splineTerrafector;
        pixelShader             shader_meshTerrafector;             // these two should merge
        DepthStencilState::SharedPtr    depthstateCloser;
        DepthStencilState::SharedPtr    depthstateFuther;
        DepthStencilState::SharedPtr    depthstateAll;
        RasterizerState::SharedPtr      rasterstateSplines;
        BlendState::SharedPtr           blendstateSplines;
        BlendState::SharedPtr		    blendstateRoadsCombined;
    } split;

    struct
    {
        uint32_t maxBezier = 131072;            // 17 bits packed - likely to change soon
        uint32_t maxIndex = 524288;             // *4 seems enough, 2022 at *1.7 for Nurburg
        uint32_t numStaticSplines = 0;
        uint32_t numStaticSplinesIndex = 0;
        uint32_t numStaticSplinesBakeOnlyIndex = 0;
        Buffer::SharedPtr bezierData;
        Buffer::SharedPtr indexData;
        Buffer::SharedPtr indexDataBakeOnly;
        Buffer::SharedPtr indexData_LOD4;
        Buffer::SharedPtr indexData_LOD6;
        Buffer::SharedPtr indexData_LOD8;
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
        Buffer::SharedPtr dynamic_bezierData;
        Buffer::SharedPtr dynamic_indexData;
        uint numIndex = 0;
    }splines;

    struct
    {
        std::vector<uint32_t> tileHash;
        float quality = 0.0002f;
        int roadMaxSplits = 16;
        FILE* txt_file = nullptr;
        bool inProgress = false;
        std::vector<elevationMap> tileInfoForBinaryExport;
        std::vector<uint32_t>::iterator itterator;
        RenderContext* renderContext;
        Texture::SharedPtr	  copy_texture = nullptr;
    }bake;

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
    Sampler::SharedPtr			sampler_Trilinear;
    Sampler::SharedPtr			sampler_Clamp;
    Sampler::SharedPtr			sampler_ClampAnisotropic;
    Sampler::SharedPtr			sampler_Ribbons;


    _shadowEdges shadowEdges;
    Texture::SharedPtr	  terrainShadowTexture;



public:
    struct
    {
        bool show = false;
        Texture::SharedPtr	  skyTexture;
        Texture::SharedPtr	  envTexture;
        Texture::SharedPtr	  dappledLightTexture;
    }vegetation;

private:
    bool showGUI = true;




    //_gliderBuilder paraBuilder;
    //_gliderRuntime paraRuntime;
    //_new_gliderRuntime newGliderRuntime;
    //_airSim AirSim;
    //_cfd CFD;
    //_cfdClipmap cfdClip;
public:


    //void cfdStart();
    //void cfdThread();
    //void paragliderThread(BarrierThrd& bar);
    //void paragliderThread_B(BarrierThrd& bar);
    bool requestParaPack = false;
    float3 paraCamPos;
    float3 paraEyeLocal;

    struct
    {
        //_cfdClipmap clipmap; // deferred: port _cfdClipmap from Falcor Earthworks
        std::string rootPath;
        std::string rootFile;
        bool recordingCFD = false;
        int cfd_play_k = 0;

        // instead fo mutex, I am going to do oneay coms for now
        // input
        float stepTime;
        bool realTime = true;   // if false we run as fast as posible to solve as much time
        uint exportFrameStep = 0;   // 0 does not export, typical is repeating frame 2^lod - 1 every time we have sync

        std::array<float3, 10> velocityRequets;
        float3 originRequest = float3(-1425 - 0, 425 + 140, 14533 - 2000);
        bool pause = false;


        // output
        bool newFlowLines = false;
        uint numLines = 0;
        std::vector<float4> flowlines;

        std::array<float3, 10> velocityAnswers;

        // skewT editor ---------------------------------
        std::array<float3, 100> skewT_data;
        std::array<float3, 100> skewT_V;
        bool editMode = false;
        bool editChanged = false;


        Texture::SharedPtr	  sliceVTexture[2];
        Texture::SharedPtr	  sliceDataTexture[2];
        Texture::SharedPtr	  sliceVolumeTexture[6][3];
        //uint sliceOrder[6] = { 0, 0, 0, 0, 0, 0 };
        float sliceTime[6] = { 0, 0, 0, 0, 0, 0 };
        float lodLerp[6] = { 0, 0, 0, 0, 0, 0 };
    } cfd;      // guess this needs to become its own class or move into _cfdClipmap


    struct
    {
        bool newGliderLoaded = false;

        bool loaded = false;
        float frameTime;
        float packTime;

        float cellsTime[2];
        float preTime[2];
        float wingTime[2];
        float postTime[2];
    } glider;
};
