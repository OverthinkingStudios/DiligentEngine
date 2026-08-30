#pragma once

// ---------------------------------------------------------------------------
// vegetationBuilder.h - the procedural plant builders (leaf, stem, clump,
// flower, tree), their material/texture cache, and the runtime that packs,
// lods and renders the result.
//
// This subsystem is hyper-tuned and easy to wreck. The load-bearing
// invariants: 32-vertex block unit, inverted startBit plus boundary
// duplication, 32-byte packed vertex, nested bezier wind pivots, deterministic
// mt19937 re-seeding, 128 log-spaced depth buckets drawn as 128 indirect
// draws, One/Zero blending for the opacity-matched set, baked egg lighting.
//
// Include from terrain.h AFTER the hlsli-defines block and ribbonBuilder.h -
// this header relies on the HLSL-shared structs and aliases already being in
// scope, like ecotope.h does.
// ---------------------------------------------------------------------------

#include "ewGpuContext.h"
#include "ewResources.h"
#include "ewShader.h"

#include "cereal/cereal.hpp"
#include "cereal/archives/binary.hpp"
#include "cereal/archives/json.hpp"
#include "cereal/types/map.hpp"
#include "cereal/types/vector.hpp"
#include "cereal/types/list.hpp"
#include "cereal/types/array.hpp"
#include "cereal/types/string.hpp"

//#include <fstream>

#include <random>

#include"terrafector.h"
#include "imgui.h"
//#include"hlsl/terrain/vegetation_defines.hlsli"
//#include"hlsl/terrain/groundcover_defines.hlsli"    // FIXME combine these two

// (ribbonBuilder.h comes in through terrain.h, before this header)

#define MAX_PLANT_BLOCKS 1048576
#define MAX_PLANT_INSTANCES 65536
#define MAX_PLANT_BILLBOARDS 65536
#define MAX_PLANT_PLANTS 1024
#define MAX_PLANT_PIVOTS MAX_PLANT_PLANTS * 256
#define MAX_PLANT_VERTS 524288

// Layout contracts with the HLSL twins (vegetation_defines.hlsli is compiled
// by both MSVC and DXC; structured-buffer strides are scalar-packed).
static_assert(sizeof(ribbonVertex8) == 32, "ribbonVertex8 must stay 32 bytes");
static_assert(sizeof(plant_instance) == 32, "plant_instance must stay 32 bytes");
static_assert(sizeof(_plant_anim_pivot) == 48, "_plant_anim_pivot must stay 48 bytes");
static_assert(sizeof(plant) == 384, "plant must stay 384 bytes (80 head + 16x16 lods + 48 rootPivot)");
static_assert(sizeof(veg_sort) == 16, "veg_sort must stay uint4");
static_assert(sizeof(vegetation_feedback) == 812, "vegetation_feedback layout drifted");
static_assert(sizeof(sprite_material) == 64, "sprite_material must match the HLSL structured-buffer stride");
static_assert(offsetof(sprite_material, albedoScale) == 32,
              "sprite_material albedoScale must sit on a 16-byte boundary (std430 float4-array alignment)");




#define textWIDTH 160
#define itemWIDTH 80


#define R_FLOAT_EX(name,data, scl, min, max, t1, t2)    ImGui::PushID(gui_id); \
                    ImGui::Text(name);    \
                    ImGui::SameLine(textWIDTH, 0); \
                    ImGui::SetNextItemWidth(itemWIDTH);    \
                    if (ImGui::DragFloat("##X", &data.x, scl, min, max, "%2.3f")) changed = true; \
                    TOOLTIP(t1); \
                    ImGui::SameLine(0, 10); \
                    ImGui::SetNextItemWidth(itemWIDTH);    \
                    if (ImGui::DragFloat("##Y", &data.y, 0.01f, 0, 1, "%1.2f")) changed = true; \
                    TOOLTIP(t2); \
                    ImGui::PopID(); \
                    gui_id ++;


#define R_INT(name,data,min,max,tooltip)    ImGui::PushID(gui_id); \
                    ImGui::Text(name);    \
                    ImGui::SameLine(textWIDTH, 0); \
                    ImGui::SetNextItemWidth(itemWIDTH);    \
                    if (ImGui::DragInt("##d", &data, 0.1f, min, max)) changed = true; \
                    TOOLTIP(tooltip); \
                    ImGui::PopID(); \
                    gui_id ++;

#define R_FLOAT(name,data,speed, min,max,tooltip)    ImGui::PushID(gui_id); \
                    ImGui::Text(name);    \
                    ImGui::SameLine(textWIDTH, 0); \
                    ImGui::SetNextItemWidth(itemWIDTH);    \
                    if (ImGui::DragFloat("##X", &data, speed, min, max)) changed = true; \
                    TOOLTIP(tooltip); \
                    ImGui::PopID(); \
                    gui_id ++;



#define CHECKBOX(name, val, tooltip)  ImGui::PushID(gui_id); \
                    ImGui::Text(name);    \
                    ImGui::SameLine(textWIDTH, 0); \
                    style.FrameBorderSize = 1; \
                    if (ImGui::Checkbox("", val)) changed = true;    \
                    style.FrameBorderSize = 0; \
                    TOOLTIP(tooltip); \
                    ImGui::PopID(); \
                    gui_id ++;



//??? should this not just be a constant somehwere where all can read and write to it
struct shaderLightBuffer
{
    float3 	sunDirection;
    int		numLights = 0;

    float3 	sunColour;			// unless this becomes a texture lookup
    float 	padd = 0;

    float3  sunRightVector;      // used for volumetric shadow projection code
    float   padd2 = 0;

    float3  sunUpVector;
    float   padd3 = 0;

    // all the fog params
    float2 screenSize;
    float fog_far_Start = 0;
    float fog_far_log_F = 0;            //(k-1 / k) / log(far)		FIXME might be k-2 to make up for half pixel offsets

    float fog_far_one_over_k = 0;       // 1.0 / k
    float fog_near_Start = 0;
    float fog_near_log_F = 0;           //(k-1 / k) / log(far)		FIXME might be k-2 to make up for half pixel offsets
    float fog_near_one_over_k = 0;      // 1.0 / k
};
static_assert(sizeof(shaderLightBuffer) == 96, "shaderLightBuffer must stay 96 bytes (cbuffer mirror, padds are load-bearing)");


class _plantMaterial;

class materialCache_plants {
public:
    static std::string lastFile;   // fixme, try to copy this to and from vegMaterial on start and exit to remember

    int find_insert_material(const std::string _path, const std::string _name, bool _forceReload = false);
    int find_insert_material(const std::filesystem::path _path, bool _forceReload = false);
    int find_insert_texture(const std::filesystem::path _path, bool isSRGB, bool _forceReload = false);
    std::string clean(const std::string _s);

    // Binds the whole textureVector element-wise as the shader's
    // textures_T[4096] array.
    void setTextures(ew::pixelShader& _shader);

    void rebuildStructuredBuffer();

    std::vector<_plantMaterial>	materialVector;
    std::vector<ew::Texture::SharedPtr>			textureVector;
    // ew::Texture carries no source path, so the paths are kept in a parallel
    // vector (same indices) for the reload/dedup logic.
    std::vector<std::filesystem::path>          texturePathVector;
    float texMb = 0;


public:
    int dispTexIndex = -1;
    ew::Texture::SharedPtr getDisplayTexture();

    ew::Buffer::SharedPtr sb_vegetation_Materials = nullptr;

    bool modified = false;
    bool modifiedData = false;
};



class _plantMaterial
{
public:

    static materialCache_plants static_materials_veg;

    std::filesystem::path 			  fullPath;     // FIXME remove ebentuall
    std::string        relativePath;
    void makeRelative(std::filesystem::path _path); // FIXME move to terrafector where we keep the path

    std::string			  displayName = "Not set!";
    bool				isModified = false;

    std::string         albedoName;
    std::string         normalName;
    std::string         translucencyName;
    std::string         alphaName;

    std::string         albedoPath;
    std::string         normalPath;
    std::string         translucencyPath;
    std::string         alphaPath;

    bool changedForSave = false;

    void import(std::filesystem::path _path, bool _replacePath = true);
    void save();
    void eXport(std::filesystem::path _path);
    void reloadTextures();

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        (void)_version;
        archive(CEREAL_NVP(albedoName));
        archive(CEREAL_NVP(alphaName));
        archive(CEREAL_NVP(normalName));
        archive(CEREAL_NVP(translucencyName));

        archive(CEREAL_NVP(albedoPath));
        archive(CEREAL_NVP(alphaPath));
        archive(CEREAL_NVP(normalPath));
        archive(CEREAL_NVP(translucencyPath));

        archive(CEREAL_NVP(_constData.translucency));
        archive(CEREAL_NVP(_constData.alphaPow));

        // albedoScale became float4 (.w = roughness, formerly its own array);
        // explicit NVP names keep existing .plantMaterial JSON assets loading.
        archive_float3(_constData.albedoScale[0]);
        archive_float3(_constData.albedoScale[1]);
        archive(cereal::make_nvp("_constData.roughness[0]", _constData.albedoScale[0].w));
        archive(cereal::make_nvp("_constData.roughness[1]", _constData.albedoScale[1].w));
    }


    sprite_material _constData = {};    // the hlsli twin carries no default initializers under DXC - zero it explicitly
};
CEREAL_CLASS_VERSION(_plantMaterial, 100);




struct _vegetationMaterial {
    std::string name;
    std::string displayname;
    int index = -1;

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        (void)_version;
        archive(CEREAL_NVP(name));
        archive(CEREAL_NVP(displayname));
    }
};
CEREAL_CLASS_VERSION(_vegetationMaterial, 100);








struct buildSetting
{
    void clear()
    {
        parentStemDir = { 0, 1, 0 };
        node_age = -1;
        normalized_age = 1;
        pivotIndex[0] = 255;
        pivotIndex[1] = 255;
        pivotIndex[2] = 255;
        pivotIndex[3] = 255;
        pivotDepth = 0;
        callDepth = 0;
        //exclusionCylinder = { 0, 0 };    // FIXME remove and set from clump
    }

    float3      parentStemDir = { 0, 1, 0 };
    glm::mat4   root{ 1.f }; // expand for the root stem direction as well to avoid growing through
    float       numSegments;
    float       pixelSize = 0.001f;  //1 cm
    int         seed = 100;
    float       node_age = -1.f; // dont use for root    // this one is a node age, like 12, good for building twigs
    float       normalized_age = 1.f;  // this one is a 0..1 age useful for leaves etc
    bool        isBaking = false;    // for billboard baking  - mainly used to straighten main stems
    bool        doNotAddPivot = false;  // for replacement stems

    bool        includeTip = false;    // stem only
    bool        excludeDead = true;    // stem only cut the bottom verts

    float2      exclusionCylinder = { 0, 0 };
    float3      exclusionNormal = { 0, 1, 0 };
    bool testExclusion(float3 pos)
    {
        float h = glm::dot(pos, exclusionNormal);
        float3 side = pos - (h * exclusionNormal);
        float r = glm::length(side);
        float2 RR = {r / exclusionCylinder.x, h / exclusionCylinder.y};
        float AR = glm::length(RR);
        if (AR > 0.7f) return true;
        //if (h > exclusionCylinder.y) return true;
        //if (r > exclusionCylinder.x) return true;
        return false;
    }

    // PIVOT POIUTNS
    uint    pivotIndex[4];
    int     pivotDepth = 0;

    // new values for more complex builds
    int max_verts = 1000000;
    int callDepth = 0;

};

struct packSettings
{
    float objectSize = 3.0f;
    float radiusScale = 0.5f;                   //  so biggest radius
    float3 objectOffset = float3(0.5, 0.1f, 0.5f);
    float getScale() { return objectSize / 16384.0f; }
    float3 getOffset() { return objectOffset * objectSize; }
};


class _vegMaterial
{
public:
    std::string name;
    std::string path;   // relative
    int index = -1;

    float2 albedoScale = { 1, 1 };
    float2 translucencyScale = { 1, 1 };

    void reload();

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        (void)_version;
        archive(CEREAL_NVP(name));
        archive(CEREAL_NVP(path));
        archive_float2(albedoScale);
        archive_float2(translucencyScale);
    }
};
CEREAL_CLASS_VERSION(_vegMaterial, 100);



enum bakeType { BAKE_NONE, BAKE_DIAMOND, BAKE_QUAD, BAKE_4, BAKE_N };
class levelOfDetail
{
public:
    levelOfDetail() { ; }
    levelOfDetail(uint _numPix) {  numPixels = _numPix; pixelSize = 1000.f / numPixels;    }

    int numPixels = 100;        // this is the number of height pixels to use for this lod. Used to calculate pixel size - for the upper limit
    float pixelSize = 10.f;      // now in mm going forwardThis is for plant on GPU - determines when to split

    float geometryPixelScale = 1.f;    // Deprecated
    bool useGeometry = true;
    int bakeIndex = 0;
    int bakeType = BAKE_NONE;

    // feedback
    uint numVerts = 0;
    uint numBlocks = 0;
    uint unused = 0;
    uint startBlock = 0;

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        (void)_version;
        archive(CEREAL_NVP(numPixels));
        archive(CEREAL_NVP(pixelSize));

        archive(CEREAL_NVP(geometryPixelScale));
        archive(CEREAL_NVP(useGeometry));
        archive(CEREAL_NVP(bakeIndex));
        archive(CEREAL_NVP(bakeType));
    }
};
CEREAL_CLASS_VERSION(levelOfDetail, 100);



class lodBake
{
public:
    lodBake(uint _h, float _w) { pixHeight = _h; bakeWidth = _w; }
    _vegMaterial material;
    float2 extents = { 0, 0 };
    int pixHeight = 64;
    float bakeWidth = 1.f;      // really a percentage
    float2 bake_V = { 0.f, 1.f };   // start and end offsets to cut it smaller
    std::array<float, 4> dU = { 1, 1, 1, 1 };

    float translucency = 1.f;
    float  alphaPow = 1.f;
    bool bakeAOToAlbedo = true;
    //bool renderGui(uint& gui_id);
    bool forceDiamond = false;          // DEPRECATED
    bool faceCamera = true;

    bool includeTip = false;
    bool clipDead = true;
    float overshootsEnd = 0.f;  // How much does the branches push past the last node in this bake, its stem only

    bool useAlphaInBake = false;
    float2 alphaOval = {0, 0};

    float pitch = 0.0f;
    float yaw = 0.0f;

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        archive(CEREAL_NVP(material));
        archive_float2(extents);
        archive(CEREAL_NVP(pixHeight));
        archive(CEREAL_NVP(bakeWidth));
        archive(CEREAL_NVP(dU));

        archive(CEREAL_NVP(translucency));
        archive(CEREAL_NVP(alphaPow));
        archive(CEREAL_NVP(bakeAOToAlbedo));
        archive_float2(bake_V);

        archive(CEREAL_NVP(forceDiamond));
        archive(CEREAL_NVP(faceCamera));

        archive(CEREAL_NVP(useAlphaInBake));
        archive_float2(alphaOval);

        if (_version >= 101)
        {
            archive(CEREAL_NVP(pitch));
            archive(CEREAL_NVP(yaw));
        }
        if (_version >= 102)
        {
            archive(CEREAL_NVP(includeTip));
            archive(CEREAL_NVP(clipDead));
            archive(CEREAL_NVP(overshootsEnd));
        }
    }
};
CEREAL_CLASS_VERSION(lodBake, 102);



struct extentsCalculator
{
    void start(glm::mat4 _view, float3 _end);
    void push(float3 _pos, float _w);
    void end();
    glm::mat4 lerp(float _h);
    float width() { return extents.x; }

    std::vector<float2>     data;

    glm::mat4               view;
    float3                  origin;
    glm::mat4               tip;
    float2                  extents;
    int                     count;
    std::array<float, 4>    du4;
    std::array<float, 6>    du6;
};


// TODO: debugLastPivots below, and _stemBuilder/_treeBuilder's age, startVerts,
// leafVerts, tipVerts, numVerts and branchMode, have no initialisers and rely on
// the owning object being zero-initialised at allocation. Give them defaults.
class _plantBuilder
{
public:
    virtual ~_plantBuilder() = default;     // derived builders are deleted through shared_ptr<_plantBuilder> - this must stay virtual
    virtual void loadPath() { ; }
    virtual void savePath() { ; }
    //virtual void load() { ; }
    //virtual void save() { ; }
    //virtual void saveas() { ; }
    virtual void incrementLods() { ; }
    virtual void decrementLods() { ; }
    virtual void deleteLod(uint _lod) { (void)_lod; }
    virtual void insertLod(uint _lod) { (void)_lod; }
    virtual glm::mat4 build(buildSetting _settings, bool _addVerts, bool _extents = false) { (void)_settings; (void)_addVerts; (void)_extents; return glm::mat4(1.f); }
    virtual float2 calculate_extents(glm::mat4 view) { (void)view; return float2(0, 0); }
    virtual glm::mat4 getTip(bool includeChildren = true) { (void)includeChildren; return glm::mat4(1.f); }
    virtual glm::mat4 getRoot(bool _cullDead) { (void)_cullDead; return glm::mat4(1.f); }
    virtual lodBake* getBakeInfo(int i) { (void)i; return nullptr; }    // so does nothing if not implimented
    virtual levelOfDetail* getLodInfo(uint i) { (void)i; return nullptr; }    // so does nothing if not implimented

    std::string name = "not set";
    std::string path = "no terrain_path either";   // relative
    bool changed = true;    // DEPRECATED
    bool changedForSave = false;
    bool fileNotFound = true;   // if weload a link and it is not found due to filesystem changes

    // lighting information
    float shadowUVScale = 0.01f;
    float shadowSoftness = 0.3f;
    float shadowDepth = 3.f;
    float shadowPenetationHeight = 0.3f;

    // packing debug info
    uint numInstancePacked = 0;
    uint numVertsPacked = 0;
    uint debugTotalVertsPacked = 0;
    uint debugnumPivots = 0;
    uint debugLOD = 0;
    uint debugmaxLOD = 0;
    float debugSIZE = 0;
    uint debugBAKETYPE = 0;
    uint debugLastPivots[4];
    virtual void clear_build_info() { ; }

    // ossilations
    float ossilation_stiffness = 1.f;   // this affects how far it bends for certain wind types, but also
    float ossilation_constant_sqrt = 10.f;     //freq = (1 / 2π) * √(g / L). so this is g/L per meter, we scale by a furher √(1/L)
    float ossilation_power = 1.f;       // DE{RECATED shifts the bend towards the root or the tip
    float rootFrequency() { return 0.1591549430f * ossilation_constant_sqrt * sqrt(1.f / ossilation_stiffness); }
    int     deepest_pivot_pack_level = 3;
};

// Always add at the edn here - This is saved to file and breaks all future loads
enum plantType { P_LEAF, P_STEM, P_CLUMP, P_AGREGATE, P_GROVE, P_FLOWER, PLANT_END = 100 };

// planmaterial repackadged for dandom_arrays
class _plantRND
{
public:
    std::string name;
    std::string path;   // relative
    plantType  type = PLANT_END;
    std::shared_ptr<_plantBuilder> plantPtr;

    void reload();

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        (void)_version;
        archive(CEREAL_NVP(name));
        archive(CEREAL_NVP(path));
        archive(CEREAL_NVP(type));
    }
};
CEREAL_CLASS_VERSION(_plantRND, 100);




// would be nice if we have more controll over distribution including with numSegments
template <class T> class randomVector
{
public:
    randomVector() { data.resize(1); }      // minimum size has to be one, otherwise get() is dangerous, does not explain clear()
    std::vector<T> data;
    int rnd_idx = 0;

    void clear() { data.clear(); }
    T get();
    void reset() { rnd_idx = 0; }
};



// Slow replace
class _randomBranch
{
public:
    std::string path;
    std::string name;
    plantType  type = PLANT_END;
    float3 params = {1.f, 0.5f, 2.f};           // everywhere
    std::shared_ptr<_plantBuilder> plantPtr;

    void reload();

    bool operator < (const _randomBranch& _b) const
    {
        return (params.y < _b.params.y);
    }

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        (void)_version;
        archive(CEREAL_NVP(path));
        archive(CEREAL_NVP(name));
        archive(CEREAL_NVP(type));
        archive_float3(params);
    }
};
CEREAL_CLASS_VERSION(_randomBranch, 100);

class semiRandomBranch
{
public:
    std::vector<_randomBranch> branchData;
    std::array<short, 1024> RND;

    void buildArray();
    void clear() { branchData.clear(); }
    _randomBranch* get(float _val);

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        (void)_version;
        archive(CEREAL_NVP(branchData));
        for (auto& M : branchData) M.reload();
        buildArray();
    }
};
CEREAL_CLASS_VERSION(semiRandomBranch, 100);



enum leafPivot { pivot_off, pivot_leaf, pivot_full_1, pivot_full_2 };

class _leafBuilder : public _plantBuilder
{
public:
    void loadPath();
    void savePath();
    void clear_build_info();
    glm::mat4 build(buildSetting _settings, bool _addVerts, bool _extents = false);


private:
    // going to do all this in mm
    float2  stem_length = { 0.f, 0.3f };
    float2  stem_width = { 4.f, 0.f };
    float2  stem_curve = { 0.0f, 0.3f };      // radian bend over lenth
    float2  stem_to_leaf = { 0.0f, 0.2f };      // radian bend over lenth
    float2  stem_to_leaf_Roll = { 0.0f, 0.0f };      // radian bend over lenth
    int2    stemVerts = { 2, 4 };

    bool    cameraFacing = false;
    float2  leaf_length = { 100.f, 0.2f };
    float2  leaf_width = { 60.f, 0.2f };
    float2  leaf_curve = { 0.0f, 0.2f };      // radian bend over lenth
    float2  leaf_twist = { 0.0f, 0.2f };      // radian bend over lenth
    float2  width_offset = { 1.0f, 0.1f };      // Y value pushes teh leaf edges outwards makign it fatter
    float2  gravitrophy = { 0, 0 };
    int2    numVerts = { 3, 12 };
    float2  perlinCurve = { 0.1f, 4.0f };
    float2  perlinTwist = { 0.1f, 4.0f };

    _vegMaterial stem_Material;
    randomVector<_vegMaterial> materials;

    leafPivot   pivotType = pivot_leaf;
    bool useTwoVertDiamond = false;
    float leafLengthSplit = 32.f;            // Number of pixels before we tru to insert a split, still clamped by min max
    int2  splitSize = { 30, 200 };              // works with numVerts and stem_length to give control on where we split
    bool pointSprite = false;

    bool wideBase = false;   // as in grass leaves that start wide.
    // will be superceded bu full graph draw control soon
public:
    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        archive_float2(stem_length);
        archive_float2(stem_width);
        archive_float2(stem_curve);
        archive_float2(stem_to_leaf);
        archive_float2(stemVerts);

        archive(CEREAL_NVP(cameraFacing));
        archive_float2(leaf_length);
        archive_float2(leaf_width);
        archive_float2(leaf_curve);
        archive_float2(leaf_twist);
        archive_float2(width_offset);
        archive(CEREAL_NVP(wideBase));
        archive_float2(gravitrophy);
        archive_float2(numVerts);  // teh define works for ints as well

        archive(CEREAL_NVP(stem_Material));
        archive(CEREAL_NVP(materials.data)); //??? moce to randomVector, keep up to date there? more contained

        archive_float2(perlinCurve);
        archive_float2(perlinTwist);

        stem_Material.reload();
        for (auto& M : materials.data) M.reload();

        archive(CEREAL_NVP(ossilation_stiffness));
        archive(CEREAL_NVP(ossilation_constant_sqrt));
        archive(CEREAL_NVP(ossilation_power));
        archive(CEREAL_NVP(deepest_pivot_pack_level));
        archive(CEREAL_NVP(pivotType));
        archive(CEREAL_NVP(useTwoVertDiamond));
        archive(CEREAL_NVP(leafLengthSplit));

        archive_float2(stem_to_leaf_Roll);

        if (_version >= 101)
        {
            archive_float2(splitSize);
        }

        if (_version >= 102)
        {
            archive(CEREAL_NVP(pointSprite));
        }

    }
};
CEREAL_CLASS_VERSION(_leafBuilder, 102);


class _flowerRing
{
public:
    //glm::mat4 build(buildSetting _settings, bool _addVerts);    //?????????

public:
    int2    numPetals = { 10, 12 };
    float2  pitch = { 0.f, 0.f };
    float2  offset_mm = { 0.f, 0.f };      // for pea flowers
    float2 radius_mm = { 5.f, 0.1f };
    float2 range = { 3.14159265359f, 0.f };           // pi means full circle

    bool    symmetrical = false;
    float2  angle_offset = { 0.f, 0.f };      // for pea flowers radians
    float2  twist = { 0.f, 0.f };      // for pea flowers

    bool    spherical = false;
    float3 sphere_size = { 0.1f, 0.1f, 0.1f };

    randomVector<_plantRND> petals;

public:
    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        archive_float2(numPetals);
        archive_float2(pitch);
        archive_float2(offset_mm);
        archive_float2(radius_mm);

        archive(CEREAL_NVP(symmetrical));
        archive_float2(angle_offset);
        archive_float2(twist);

        archive(CEREAL_NVP(petals.data));
        {
            for (auto& M : petals.data) M.reload();
        }

        if (_version >= 101)
        {
            archive_float2(range);
            archive(CEREAL_NVP(spherical));
            archive_float3(sphere_size);

        }
    }
};
CEREAL_CLASS_VERSION(_flowerRing, 101);


class _flowerBuilder : public _plantBuilder
{
public:
    void loadPath();
    void savePath();
    //void load();
    //void save();
    //void saveas();
    void clear_build_info();
    virtual void incrementLods() { lodInfo.resize(lodInfo.size() + 1); }
    virtual void decrementLods() { if (lodInfo.size() > 3) lodInfo.resize(lodInfo.size() - 1); }
    virtual void deleteLod(uint _lod) { lodInfo.erase(lodInfo.begin() + _lod); }
    virtual void insertLod(uint _lod) { lodInfo.emplace(lodInfo.begin() + _lod); }
    lodBake* getBakeInfo(int i);
    levelOfDetail* getLodInfo(uint i);
    glm::mat4  build_2(buildSetting _settings, uint _bakeIndex, bool _faceCamera, bool _diamond);
    glm::mat4 build(buildSetting _settings, bool _addVerts, bool _extents = false);
    float2 calculate_extents(glm::mat4 view);
    glm::mat4 getTip(bool includeChildren = true);


private:
    //Stem has to maintain a minimum of 3 of these or will crash ??? is this tryue
    std::vector<levelOfDetail> lodInfo = { levelOfDetail(10), levelOfDetail(40), levelOfDetail(500) };
    std::array<lodBake, 3> lod_bakeInfo = { lodBake(128, 1.f), lodBake(0, 0.6f), lodBake(0, 0.2f) };

    // going to do all this in mm
    float2  stem_length = { 0.f, 0.3f };
    float2  stem_width = { 4.f, 0.f };
    float2  stem_curve = { 0.0f, 0.3f };      // radian bend over lenth
    float2  stem_to_leaf = { 0.0f, 0.2f };      // radian bend over lenth
    float2  stem_to_leaf_Roll = { 0.0f, 0.0f };      // radian bend over lenth
    int2    stemVerts = { 2, 4 };
    _vegMaterial stem_Material;

    std::vector<_flowerRing> rings;

    float2  center_offset = { 20.0f, 0.f };
    float2  center_size = { 20.f, 0.f };
    _vegMaterial center_Material;

    int currentRing = 0;

public:
    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        (void)_version;
        archive_float2(stem_length);
        archive_float2(stem_width);
        archive_float2(stem_curve);
        archive_float2(stem_to_leaf);
        archive_float2(stemVerts);
        archive(CEREAL_NVP(stem_Material));
        stem_Material.reload();

        archive(CEREAL_NVP(rings));

        archive_float2(center_offset);
        archive_float2(center_size);
        archive(CEREAL_NVP(center_Material));
        center_Material.reload();

        archive(CEREAL_NVP(lod_bakeInfo));
        for (auto& M : lod_bakeInfo) M.material.reload();

        archive(CEREAL_NVP(lodInfo));
    }
};
CEREAL_CLASS_VERSION(_flowerBuilder, 100);




class _stemBuilder : public _plantBuilder
{
public:
    void loadPath();
    void savePath();
    virtual void incrementLods() { lodInfo.resize(lodInfo.size() + 1); }
    virtual void decrementLods() { if (lodInfo.size() > 3) lodInfo.resize(lodInfo.size() - 1); }
    virtual void deleteLod(uint _lod) { lodInfo.erase(lodInfo.begin() + _lod); }
    virtual void insertLod(uint _lod) { lodInfo.emplace(lodInfo.begin() + _lod); }
    lodBake* getBakeInfo(int i);//bakeInfo
    levelOfDetail* getLodInfo(uint i);
    levelOfDetail* getLOD(float _pixelSize, bool _rnd);
    glm::mat4  build_2(buildSetting _settings, lodBake* pBake, bool _faceCamera, bool _diamond);
    glm::mat4  build_4(buildSetting _settings, lodBake* pBake, bool _faceCamera);
    glm::mat4  build_n(buildSetting _settings, lodBake* pBake, bool _faceCamera);
    void build_leaves(buildSetting _settings, bool _addVerts, bool _extents = false);
    void build_tip(buildSetting _settings, bool _addVerts, bool _extents = false);
    //void build_extents(buildSetting _settings);
    float2 calculate_extents(glm::mat4 view);
    glm::mat4 getTip(bool includeChildren = true);
    glm::mat4 getRoot(bool _cullDead);
    void build_NODES(buildSetting _settings, bool _addVerts, bool _extents = false);
    void build_extents(buildSetting _settings);
    glm::mat4 build(buildSetting _settings, bool _addVerts, bool _extents = false);
    void addPivot(buildSetting *p_settings);
    void clear_build_info();



    //void build_Nodes(buildSetting& _settings);
    std::vector<glm::mat4> NODES;
    glm::mat4 tip_NODE;
    //std::vector<glm::mat4> NODES_PREV; // bad bad leroy brown do without, so build twice, alwasy
    float age;  // number of segments floatign pouint, after randomize, needed for leaves build
    float tip_width = 0;
    float root_width = 0;
    int     firstLiveSegment = 1;


    //Stem has to maintain a minimum of 3 of these or will crash
    std::vector<levelOfDetail> lodInfo = { levelOfDetail(30), levelOfDetail(60), levelOfDetail(100), levelOfDetail(300), levelOfDetail(500) };
    std::array<lodBake, 3> lod_bakeInfo = { lodBake(32, 1.f), lodBake(64, 0.6f), lodBake(128, 0.2f) };

    // stem
    float2  numSegments = { 5.3f, 0.3f };
    float2  max_live_segments = { 10.f, 0.2f };  // curve for width growth
    float2  stem_length = { 50.f, 0.2f };   // in mm
    float2  stem_width = { 5.f, 0.2f };     // width at the start
    float2  stem_d_width = { 1.f, 0.1f };  // width growth per node
    float2  stem_pow_width = { 1.0f, 0.1f };  // curve for width growth
    float2  stem_curve = { 0.2f, 0.3f };      // radian bend over lenth
    float2  stem_phototropism = { 0.0f, 0.3f };
    float2  node_rotation = { 0.7f, 0.3f };   // like 2 leaves 90 degrees
    float2  node_angle = float2(0.2f, 0.2f);    // andgle that the stem bends at the node ??? always away fromt he leaf angle if there is such a thing
    _vegMaterial stem_Material;
    bool    bake2_replaces_stem = false;    // deprecated
    float   nodeLengthSplit = 8.f;
    randomVector<_plantRND> stemReplacement;    // If set this replaces the stem completely
    bool roll_horizontal = false;
    float rollOffset = 0.f;
    float2  perlinCurve = { 0.f, 4.0f };
    float2  perlinTwist = { 0.f, 4.0f };
    bool    hasPivot = true;
    bool    fullLengthOptimize = true;
    bool    lengthFromBranchAge = false;    // scale the stem length with branch age - useful for fern leaves


    // branches, can be leavea
    float2  numLeaves = { 3.f, 0.f };  // per segment
    float2  leaf_angle = float2(0.5f, 0.3f);   // angle that the leaves come out of
    float2  leaf_rnd = float2(0.3f, 0.3f);
    float2  leaf_fully_grown_age = float2(15.0f, 0.3f);
    float   leaf_age_power = 2.f;
    bool    twistAway = false;      // I think deprecated, cant  do in rameworkif single leaf, activelt twist stem to the other side

    semiRandomBranch    branches;   // can be leases or clusters as well as stems, basically anything

    // new for ferns
    bool    compoundLeaf = false;   // will alternate and roll to flatter exits
    float branchPush = 0.15f;       // How far does brancged push outwards from ther center of the main stem. Small if main stem has lots of alpha

    // tip
    bool unique_tip;
    randomVector<_plantRND> tip;
    float2  tip_age = float2(-1.f, 0.f);

    bool leaf_age_override = false; // If set false we pass in -1 and teh leaf can set its own age, if true we set the age

    bool isPlantRoot = false;

    // feedback
    uint numLeavesBuilt = 0;
    uint startVerts;
    uint leafVerts;
    uint tipVerts;

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        archive_float2(numSegments);
        archive_float2(max_live_segments);
        archive_float2(stem_length);
        archive_float2(stem_width);
        archive_float2(stem_d_width);
        archive_float2(stem_pow_width);
        archive_float2(stem_curve);
        archive_float2(stem_phototropism);
        archive_float2(node_rotation);   // around the axis
        archive_float2(node_angle);
        archive(CEREAL_NVP(stem_Material));
        stem_Material.reload();

        archive_float2(numLeaves);

        archive_float2(leaf_angle);
        archive_float2(leaf_rnd);
        archive(CEREAL_NVP(leaf_age_power));
        archive(CEREAL_NVP(twistAway));

        archive(CEREAL_NVP(unique_tip));
        archive(CEREAL_NVP(tip.data));
        if (unique_tip) {
            for (auto& M : tip.data) M.reload();
        }

        archive(CEREAL_NVP(lod_bakeInfo));
        for (auto& M : lod_bakeInfo) M.material.reload();

        archive(CEREAL_NVP(lodInfo));

        archive(CEREAL_NVP(shadowUVScale));
        archive(CEREAL_NVP(shadowSoftness));
        archive(CEREAL_NVP(shadowDepth));
        archive(CEREAL_NVP(shadowPenetationHeight));

        archive(CEREAL_NVP(ossilation_stiffness));
        archive(CEREAL_NVP(ossilation_constant_sqrt));
        archive(CEREAL_NVP(ossilation_power));
        archive(CEREAL_NVP(deepest_pivot_pack_level));

        archive(CEREAL_NVP(bake2_replaces_stem));
        archive_float2(tip_age);
        archive(CEREAL_NVP(nodeLengthSplit));

        archive(CEREAL_NVP(leaf_age_override));
        archive(CEREAL_NVP(stemReplacement.data));
        for (auto& M : stemReplacement.data) M.reload();

        archive(CEREAL_NVP(roll_horizontal));

        archive(CEREAL_NVP(rollOffset));

        archive_float2(perlinCurve);
        archive_float2(perlinTwist);

        archive(CEREAL_NVP(hasPivot));

        archive(CEREAL_NVP(branches));

        archive(CEREAL_NVP(compoundLeaf));

        archive_float2(leaf_fully_grown_age);

        if (_version >= 101)
        {
            archive(CEREAL_NVP(lengthFromBranchAge));
        }

        if (_version >= 102)
        {
            archive(CEREAL_NVP(branchPush));
        }

        if (_version >= 103)
        {
            archive(CEREAL_NVP(isPlantRoot));
        }


    }
};
CEREAL_CLASS_VERSION(_stemBuilder, 103);



struct singleClump
{
    // clump
    float2  size = { 0.1f, 0.3f };
    float2  aspect = { 1.0f, 0.1f };
    bool radial = true;     // radial or X aligned

    // child
    float2  numChildren = { 3.f, 0.f };  // per segment
    float2  child_angle = float2(0.5f, 0.3f);   // angle that the leaves come out of
    float2  child_rnd = float2(0.3f, 0.3f);
    float2  child_age = float2(10.3f, 0.2f);
    float   child_age_power = 2.f;
    randomVector<_plantRND> children; // age in this conetxt is inside to outside

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        (void)_version;
        // clump
        archive_float2(size);
        archive_float2(aspect);
        archive(CEREAL_NVP(radial));

        // child
        archive_float2(numChildren);
        archive_float2(child_angle);    // from circle
        archive_float2(child_rnd);
        archive_float2(child_age);
        archive(CEREAL_NVP(child_age_power));
        archive(CEREAL_NVP(children.data));
        for (auto& M : children.data) M.reload();
    }
};
CEREAL_CLASS_VERSION(singleClump, 100);



class _clumpBuilder : public _plantBuilder
{
public:
    void loadPath();
    void savePath();
    //void load();
    //void save();
    //void saveas();
    virtual void incrementLods() { lodInfo.resize(lodInfo.size() + 1); }
    virtual void decrementLods() { if (lodInfo.size() > 3) lodInfo.resize(lodInfo.size() - 1); }
    virtual void deleteLod(uint _lod) { lodInfo.erase(lodInfo.begin() + _lod); }
    virtual void insertLod(uint _lod) { lodInfo.emplace(lodInfo.begin() + _lod); }
    lodBake* getBakeInfo(int i);//bakeInfo
    levelOfDetail* getLodInfo(uint i);

    void clear_build_info();
    float2 calculate_extents(glm::mat4 view);
    glm::mat4 getTip(bool includeChildren = true);
    void build_extents(buildSetting _settings);
    levelOfDetail* getLOD(float _pixelSize, bool _rnd);
    void addPivot(buildSetting* p_settings);
    glm::mat4 build(buildSetting _settings, bool _addVerts, bool _extents = false);
    glm::mat4 buildChildren(buildSetting _settings, bool _addVerts, bool _extents = false);
    glm::mat4 build_2(buildSetting _settings, lodBake* pBake, bool _faceCamera, bool _diamond);
    //glm::mat4 build_4(buildSetting _settings, lodBake* pBake, bool _faceCamera);



    std::vector<glm::mat4> ROOTS;
    glm::mat4 START;
    glm::mat4 TIP_CENTER;

    std::vector<levelOfDetail> lodInfo = { levelOfDetail(10), levelOfDetail(14), levelOfDetail(40), levelOfDetail(100), levelOfDetail(300), levelOfDetail(500) };
    //Stem has to maintain a minimum of 3 of these or will crash
    std::array<lodBake, 3> lod_bakeInfo = { lodBake(32, 1.f), lodBake(64, 0.6f), lodBake(128, 0.2f) };
    /*
    // stem
    float2  size = { 1.1f, 0.3f };
    float2  aspect = { 1.0f, 0.1f };
    bool radial = true;     // radial or X aligned

    // child
    float2  numChildren = { 3.f, 0.f };  // per segment
    float2  child_angle = float2(0.5f, 0.3f);   // angle that the leaves come out of
    float2  child_rnd = float2(0.3f, 0.3f);
    float2  child_age = float2(10.3f, 0.2f);
    float   child_age_power = 2.f;
    randomVector<_plantRND> children; // age in this conetxt is inside to outside
    */
    bool    hasPivot = false;
    bool isPlantRoot = false;

    std::vector<singleClump> clumps;

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        /*
        // clump
        archive_float2(size);
        archive_float2(aspect);
        archive(CEREAL_NVP(radial));

        // child
        archive_float2(numChildren);
        archive_float2(child_angle);    // from circle
        archive_float2(child_rnd);
        archive_float2(child_age);
        archive(CEREAL_NVP(child_age_power));
        archive(CEREAL_NVP(children.data));
        for (auto& M : children.data) M.reload();
        */


        archive(CEREAL_NVP(clumps));

        // baking lodding and sway
        archive(CEREAL_NVP(lod_bakeInfo));
        for (auto& M : lod_bakeInfo) M.material.reload();

        archive(CEREAL_NVP(lodInfo));

        archive(CEREAL_NVP(shadowUVScale));
        archive(CEREAL_NVP(shadowSoftness));
        archive(CEREAL_NVP(shadowDepth));
        archive(CEREAL_NVP(shadowPenetationHeight));

        archive(CEREAL_NVP(hasPivot));
        archive(CEREAL_NVP(ossilation_stiffness));
        archive(CEREAL_NVP(ossilation_constant_sqrt));
        archive(CEREAL_NVP(ossilation_power));
        archive(CEREAL_NVP(deepest_pivot_pack_level));

        if (_version >= 101)
        {
            archive(CEREAL_NVP(isPlantRoot));
        }

    }
};
CEREAL_CLASS_VERSION(_clumpBuilder, 101);


struct _leafNode
{
    float3 pos;
    float3 dir;
    int branchNode;
    int branchIndex;
};


struct _branchnode
{
    float3 pos;
    float radius;
    float3 dir;
    bool isVisible = true;

    glm::mat4 node;
};


struct _GroveBranch
{
    std::vector<_branchnode> nodes;
    std::vector <_leafNode> leaves;
    std::vector<int> sideBranches;
    int rootBranch = -1;
    int sideNode = 0;
    bool isVisible = true;
    bool isDead = false;

    float3 leavesAvsPosition = float3(0, 0, 0);
    uint numLeaves = 0;
    float leavesRadius = 0.f;
    float sumOfSticks  = 0.f;      // also for catching wind
    int branchDepth;

    void reset();

    // aal infor for least side branches
    uint start_node;
    std::vector<uint> all_twigs;
    uint numLeavesSmall = 0;
    bool deadRootBranch = false;
    float PITCH;
    float LENGTH;
};


struct _minimalNode
{
    float3  pos = float3(0, 0, 0);
    float   radius = 0.f;
    float3  dir = float3(0, 0, 0);

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        (void)_version;
        archive_float3(pos);
        archive(CEREAL_NVP(radius));
        archive_float3(dir);
    }
};
CEREAL_CLASS_VERSION(_minimalNode, 100);

struct _one_branch
{
    int                         parentIndex = -1;
    int                         parentNode = -1;
    std::vector<_minimalNode>   nodes;
    std::string                 tip_path;
    float                       tip_yaw = 0.f;
    float                       tip_pitch = 0.f;
    float                       tip_scale = 1.f;

    uint numLeaves;
    float3 leavesAVS;
    float3 extents;
    float leavesFurthest;

    bool    isDead = false;

    std::array<uint, 4>    pivots = {255, 255, 255, 255};
    uint tempPivot;
    uint    pivotDepth;

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        (void)_version;
        archive(CEREAL_NVP(parentIndex));
        archive(CEREAL_NVP(parentNode));
        archive(CEREAL_NVP(nodes));
        archive(CEREAL_NVP(tip_path));
        archive(CEREAL_NVP(pivots));

        archive(CEREAL_NVP(numLeaves));
        archive_float3(leavesAVS);
        archive_float3(extents);
        archive(CEREAL_NVP(leavesFurthest));


        archive(CEREAL_NVP(isDead));

    }
};
CEREAL_CLASS_VERSION(_one_branch, 100);


struct _branchStats
{
    float   rootPitch;
    float   rootRadius;

    int     numLeaves;
    float   leavesPitch;
    float   leavesDistance;
    float   width;
    float   height;

    //uint pivotDepth = 0;
    //std::array<uint, 4> pivotIndex = {255, 255, 255, 255};


    std::string name;

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        (void)_version;
        archive(CEREAL_NVP(rootPitch));
        archive(CEREAL_NVP(rootRadius));

        archive(CEREAL_NVP(numLeaves));
        archive(CEREAL_NVP(leavesPitch));
        archive(CEREAL_NVP(leavesDistance));
        archive(CEREAL_NVP(width));
        archive(CEREAL_NVP(height));

        //archive(CEREAL_NVP(pivotDepth));
        //archive(CEREAL_NVP(pivotIndex));

        archive(CEREAL_NVP(name));
    }
};
CEREAL_CLASS_VERSION(_branchStats, 100);

struct _branch
{
    void findParentBranches();
    void sumLeaves();
    void generatePivots();
    void propagatePivots();

    _branchStats    stats;

    std::vector<_one_branch>   branches;

    std::array<_plant_anim_pivot, 256> pivots;
    uint numPivots = 0;

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        (void)_version;
        archive(CEREAL_NVP(stats));
        //archive(CEREAL_NVP(pivots)); SHIT, how to write this in a sensible way
        archive(CEREAL_NVP(branches));
    }
};
CEREAL_CLASS_VERSION(_branch, 100);


struct _branchCollection
{
    std::vector<_branchStats>    branches;

    float compare(_branchStats _stats, _branchStats *closest); // returns closest match in the database with a match score - smaller is better


    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        (void)_version;
        archive(CEREAL_NVP(branches));
   }
};
CEREAL_CLASS_VERSION(_branchCollection, 100);



struct _treeRoot
{
    std::vector<_one_branch>   branches;

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        (void)_version;
        archive(CEREAL_NVP(branches));
    }
};
CEREAL_CLASS_VERSION(_treeRoot, 100);





struct _cubemap
{
    // this is about every 5 degrees
#define cubeHalfSize 32
    struct _data
    {
        float d;
        float sum;      // for averaging into buckets esencially
        float3 dir;
        float cone;     // cosTheta value
    };

    float3 center;
    float3 scale;
    float twigOffset = 0.2f;

    int face, y, x;
    float dy, dx;
    void toCube(float3 _v);
    float3 toVec(int face, int y, int x);
    float sampleDistance(float3 _v);
    void clear();
    void writeDistance(float3 _v);
    void solveEdges();
    void solve();

    float4 light(float3 p, float* _depth);

    _data data[6][cubeHalfSize * 2 + 2][cubeHalfSize * 2 + 2];
};


class _treeBuilder : public _plantBuilder
{
public:
    void loadPath();
    void savePath();
    virtual void incrementLods() { lodInfo.resize(lodInfo.size() + 1); }
    virtual void decrementLods() { if (lodInfo.size() > 3) lodInfo.resize(lodInfo.size() - 1); }
    virtual void deleteLod(uint _lod) { lodInfo.erase(lodInfo.begin() + _lod); }
    virtual void insertLod(uint _lod) { lodInfo.emplace(lodInfo.begin() + _lod); }
    lodBake* getBakeInfo(int i) {
        if (i < (int)lod_bakeInfo.size()) return &lod_bakeInfo[i];
        else return nullptr;
    }
    levelOfDetail* getLodInfo(uint i) {
        if (i < lodInfo.size()) return &lodInfo[i];
        else return nullptr;
    }

    void clear_build_info();
    float2 calculate_extents(glm::mat4 view);

    glm::mat4 build_2(buildSetting _settings, lodBake* pBake, bool _faceCamera, bool _diamond);
    glm::mat4 build_4(buildSetting _settings, lodBake* pBake, bool _faceCamera);
    glm::mat4 build(buildSetting _settings, bool _addVerts, bool _extents = false);
    void build_one_branch(uint _root, uint _idx, buildSetting _settings, bool bottom);
    void build_BRANCH(uint _idx, buildSetting _settings, bool bottom);

    glm::mat4 getTip(bool includeChildren = true);


    std::vector<glm::mat4> ROOTS;
    glm::mat4 START;
    glm::mat4 TIP_CENTER;

    std::vector<levelOfDetail> lodInfo = { levelOfDetail(40), levelOfDetail(80), levelOfDetail(140), levelOfDetail(300), levelOfDetail(500), levelOfDetail(700) };
    std::array<lodBake, 3> lod_bakeInfo = { lodBake(32, 1.f), lodBake(64, 0.6f), lodBake(128, 0.2f) };


    std::string tree_name = "click to load";
    std::string tree_path;   // relative

    randomVector<_plantRND> twigs;
    _vegMaterial            branch_Material;
    _vegMaterial            trunk_Material;


    // Not implemented: the no-argument overload is the editor file-dialog entry
    // into the Grove OBJ import. The explicit-path overload does the work.
    void loadFromFile();
    void loadFromFile(const std::filesystem::path& filepath);
    void load_obj();
    void readHeader();
    void read2();
    void readahead1();
    float3 readVertex();
    void testBranchLeaves();
    void findSideBranches();
    void propagateDead(int root);
    void calcLight();
    //void rebuildRibbons();
    bool enfOfFile;
    FILE* objfile;
    bool branchMode;
    bool isPlanar;
    uint numVerts;
    int oldNumVerts;
    float3 verts[100];
    int totalVerts;
    int numSideBranchesFound;
    int numDeadEnds;
    int numBadEnds;
    float3 nodeDir;
    float nodeOffset;
    std::vector<_GroveBranch> branches;
    std::vector<_leafNode> endLeaves;
    std::vector<_leafNode> branchLeaves;
    _GroveBranch* currentBranch;
    float endRadius = 0.00006f;
    float startRadius = 1000.f;
    float stepFactor = 5.0f;
    void disableFloating();
    int numFloating = 0;
    void countLeavesEtc();
    struct _sortLeaf
    {
        uint index;
        float radius;
        bool operator < (const _sortLeaf& srt) const
        {
            return (radius > srt.radius); // foiir reverse sort bigh top small
        }
    };
    std::vector<_sortLeaf> sorted;
    void calcSubTwigs();
    bool testBranch(uint _root, uint _idx, int *_bottomIndex);
    int numBranches = 250;
    bool drawRoot = true;
    bool drawBranches = true;
    bool drawOnlyOne = false;
    bool drawAllDropped = false;
    int  theOneToDraw = 0;
    float avsLeavesInBranch;
    float avsLeavesInBranch_High;
    bool showBranchSplitWide = false;
    std::vector<int> allDroppedTwigs;

    void buildTreeRootAndBranches();
    _branchCollection   myBranchCollection;       // ek weet nie of dit hier moet wees nie
    std::vector<_branch> tempActualBranches;
    _treeRoot           myTreeRoot;

    struct {
        glm::vec3 center;
        glm::vec3 Min;
        glm::vec3 Max;
        glm::vec3 scale;
        //float distCube[6][16][16];
        _cubemap cubemap;
    } light;


    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        (void)_version;
        archive(CEREAL_NVP(name));
        archive(CEREAL_NVP(path));

        archive(CEREAL_NVP(twigs.data));
        for (auto& M : twigs.data) M.reload();

        archive(CEREAL_NVP(branch_Material));
        branch_Material.reload();

        archive(CEREAL_NVP(trunk_Material));
        trunk_Material.reload();


        archive(CEREAL_NVP(lod_bakeInfo));
        for (auto& M : lod_bakeInfo) M.material.reload();

        archive(CEREAL_NVP(lodInfo));

        archive(CEREAL_NVP(shadowUVScale));
        archive(CEREAL_NVP(shadowSoftness));
        archive(CEREAL_NVP(shadowDepth));
        archive(CEREAL_NVP(shadowPenetationHeight));

        archive(CEREAL_NVP(ossilation_stiffness));
        archive(CEREAL_NVP(ossilation_constant_sqrt));
        archive(CEREAL_NVP(ossilation_power));
        archive(CEREAL_NVP(deepest_pivot_pack_level));
    }
};
CEREAL_CLASS_VERSION(_treeBuilder, 100);




class exportPlant
{
public:
    std::vector <_vegMaterial>          billboardMaterials;
    std::map<int, _vegMaterial>         materials;
    std::vector<plant>                  plants;
    std::vector<ribbonVertex8>          vertexbuff;
    std::vector<_plant_anim_pivot>      pivots;

    void onLoad(std::string path, uint vOffset);

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        (void)_version;
        int p = (int)plants.size();
        int v = (int)vertexbuff.size();
        int pvt = (int)pivots.size();

        archive(billboardMaterials);
        archive(materials);

        archive(p);
        plants.resize(p);
        archive(cereal::binary_data(plants.data(), p * sizeof(plant)));

        archive(v);
        vertexbuff.resize(v);
        archive(cereal::binary_data(vertexbuff.data(), v * sizeof(ribbonVertex8)));

        archive(pvt);
        pivots.resize(pvt);
        archive(cereal::binary_data(pivots.data(), pvt * sizeof(_plant_anim_pivot)));
    }
};
CEREAL_CLASS_VERSION(exportPlant, 100);


class recentFiles
{
    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        (void)archive; (void)_version;
        //  archive(paths);
    }
};
CEREAL_CLASS_VERSION(recentFiles, 100);




/*  This is a single "rectangle", can be curved*/
class oneTexture
{
public:
    int texWidth = 1;   // these are on teh small end but multiply by 4 still block based
    int texHeight = 2;
    // TODO: this default moved 5 -> 4 with version 103. Version 101 files never
    // stored numMips, so they now load as 4 instead of 5. Put it back to 5 if
    // existing texture sets must keep 5 mips - see the oneTexture note in
    // TextureSplitTool.hpp for the full picture.
    int numMips = 4;
    float2 start = float2(0.5f, 0.5f);
    float2 stop = float2(0.5f, 0.4f);
    float2 bezier = float2(0.f, 0.f);
    float width = 0.05f;    // all in UV coordinates
    float2 widthMarker = float2(0.5f, 0.5f);    // 4th control point, the width drag handle
    float bezierOffset = 0.f;

    std::array<float, 9> offset = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    std::array<float, 9> extents = { 1, 1, 1, 1, 1, 1, 1, 1, 1 };

    float2 a, b, c, d;

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        archive(texWidth);
        archive(texHeight);
        archive_float2(start);
        archive_float2(stop);
        archive_float2(bezier);
        archive(width);

        archive(offset);
        archive(extents);

        if (_version >= 101)
        {
            archive(bezierOffset);
        }
        if (_version >= 102)
        {
            archive(numMips);   // never archived before 102, so it silently reset on every load
        }
        if (_version >= 103)
        {
            archive_float2(widthMarker);
        }
    }
};
CEREAL_CLASS_VERSION(oneTexture, 103);

class largeTexture
{
public:
    std::string path;
    std::string albedo;
    std::string alpha;
    std::string normal;
    std::string translucency;

    ew::Texture::SharedPtr tex_albedo;
    ew::Texture::SharedPtr tex_alpha;
    ew::Texture::SharedPtr tex_normal;
    ew::Texture::SharedPtr tex_translucency;

    ew::Fbo::SharedPtr	    fbo;

    bool flipRed = false;
    bool flipGreen = false;
    float normalStrenth = 1.f;

    std::vector<oneTexture> maps;
    int w;
    int h;

    // matrial properties
    bool changed = false;

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        (void)_version;
        archive(path);
        archive(albedo);
        archive(alpha);
        archive(normal);
        archive(translucency);

        archive(flipRed);
        archive(flipGreen);
        archive(normalStrenth);

        archive(maps);
    }
};
CEREAL_CLASS_VERSION(largeTexture, 100);



class plant_single
{
public:
    void loadTexture();

    float seasonStart = 0;      // 0, 1, 2, 3  spring summer atumn, winter, 4 snow lights 5 snow heavy
    std::string path;
    std::string name;
    ew::Texture::SharedPtr			texture;


    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        (void)_version;
        archive(seasonStart);
        archive(path);
        archive(name);
    }
};
CEREAL_CLASS_VERSION(plant_single, 100);


class seasons
{
public:

    int age_in_years = 1;
    bool wraparound = true;     // winter plant extend into spring no gap
    std::vector<plant_single> normal;
    std::vector<plant_single> damaged;
    std::vector<plant_single> burnt;


    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        (void)_version;
        archive(age_in_years);
        archive(wraparound);

        archive(normal);
        archive(damaged);
        archive(burnt);

    }
};
CEREAL_CLASS_VERSION(seasons, 100);



class plant_species
{
public:
    void sort_on_age();

    std::string name;
    std::string latin_name;
    std::string family;
    std::string alternate_names;

    std::vector<seasons> plant_years_seasons;


    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        (void)_version;
        archive(name);
        archive(latin_name);
        archive(family);
        archive(alternate_names);

        archive(plant_years_seasons);
    }
};
CEREAL_CLASS_VERSION(plant_species, 100);




// Then extend _rootPlantg off this
class _rootPlant_Runtime
{
public:
};



class _rootPlant
{
public:
    void onLoad();

    void initTextureTool();
    void GenerateATexture(uint _idx, bool toSRGB);
    void exportTextures();
    // renderGui_Lodding and renderGui_Baking hold the only copy of the lod and
    // billboard bake orchestration. Nothing calls them yet - the editor flow
    // that drove them is not implemented.
    void renderGui_Lodding();
    void setBakeView(float3 _r, float3 _u, float3 _d);
    void bakeDirectories();
    void renderGui_Baking();
    void buildAllLods();
    void build(uint pivotOffset = 0);
    void loadMaterials();
    int importBinary(std::filesystem::path filepath);       // FIXME this needs a cache
    void buildFullResolution();
    std::vector<std::string>	importPathVector;



    void bake64kplants(std::string _path);
    void bake_Setup();
    void bake_Export();
    void bake_Export_colour();
    void bake(std::string _path, std::string _seed, lodBake* _info, glm::mat4 VIEW, bool lod_0 = false);
    int bake_Reload_Count = 0;
    void updateShaderConstants(ew::Texture::SharedPtr _previousFrame, ew::Texture::SharedPtr shadow, shaderLightBuffer _buffer);
    // The matrices arrive TRANSPOSED - upload-ready, the ewCamera.h convention.
    void render(ew::GpuContext* _renderContext, const ew::Fbo::SharedPtr& _fbo,
        glm::mat4 _viewproj, float3 camPos, glm::mat4 _view, glm::mat4 _clipFrustum, float halfAngle_to_Pixels, bool terrainMode = false);

    //RenderContext* renderContext;

    //??? lodding info, so it needs all the runtime data

    bool displayModeSinglePlant = true;

    float instanceArea[8] = { 100.f, 100.f, 100.f, 100.f, 100.f, 100.f, 100.f, 100.f };
    bool cropLines = false;
    void builInstanceBuffer();

    _plantBuilder* root = nullptr;
    static std::string root_path;
    buildSetting settings;
    float rootPitch = 0.01f; // so we can go sideways to test brancges
    float rootYaw = 0.0f; // so we can go sideways to test brancges
    float rootRoll = 0.0f; // so we can go sideways to test brancges
    glm::mat4 bakeViewMatrix{ 1.f }, bakeViewAdjusted{ 1.f };
    packSettings vertex_pack_Settings;
    float2 extents{ 0.f, 0.f };

    // Binary viewer
    int numBinaryPlants = 0;
    int binVertexOffset = 0;
    int cntV_Offset = 0;
    int binPlantOffset = 0;
    int binPivotOffset = 0;

    float3 windDir = { 1, 0, 0 };
    float windStrength = 1.f;      // m/s

    // render and bake
    ew::Sampler::SharedPtr			sampler_ClampAnisotropic;
    ew::Sampler::SharedPtr			sampler_Ribbons;
    ew::Sampler::SharedPtr			sampler_Depth;
    Diligent::RasterizerStateDesc   rasterstate;            // cull NONE (load-bearing for all veg passes)
    Diligent::BlendStateDesc        blendstate;             // One/Zero - the opacity matched set, no ROP blending
    Diligent::BlendStateDesc        blendstate_withAlpha;   // alpha-to-coverage variant
    Diligent::BlendStateDesc        blendstateBake;         // 8-target SrcAlphaSaturate accumulation
    ew::pixelShader vegetationShader;
    ew::pixelShader vegetationShader_RGB_SAMPLE;
    ew::pixelShader vegetationShader_DEPTH;
    ew::pixelShader billboardShader;
    ew::pixelShader bakeShader;
    ew::pixelShader textureExtractShader;

    ew::computeShader		compute_bakeFloodfill;

    bool bakingView = false;
    float2 bakeViewportTL = float2(600, 200);
    float bakeViewportSize = 700;
    int showBake = 0;   // for bakign view

    // render flags
    bool    render_Normal = true;
    bool    render_Clip = false;
    bool    render_Pivot = false;
    bool    render_PixelCount = false;
    bool    render_ZOnly = false;
    bool    render_EarlyZ = false;
    bool    render_FrontToback = true;
    bool    render_alphaBlend = false;
    void reloadShader();
    ew::Texture::SharedPtr  sunlightTexture = nullptr;
    ew::Texture::SharedPtr inscatter;
    ew::Texture::SharedPtr outscatter;
    ew::Texture::SharedPtr envTexture;
    ew::Texture::SharedPtr dappledLightTexture;

    //Beyond PBR
    ew::Buffer::SharedPtr blockData_preSort;
    ew::Buffer::SharedPtr blockData;
    ew::Buffer::SharedPtr instanceData;
    ew::Buffer::SharedPtr instanceData_Billboards;
    ew::Buffer::SharedPtr plantData;
    ew::Buffer::SharedPtr plantpivotData;               // this is loaded from ribbonBuilder, or imported
    ew::Buffer::SharedPtr vertexData;                   // this is loaded from ribbonBuilder, or imported
    std::array<plant, 256> plantBuf = {};   // the hlsli twin carries no default initializers under DXC - zero it explicitly
    uint totalBlocksToRender = 0;
    uint unusedVerts = 0;
    void updateMaterialsAndTextures();

    float gputime = 0;      // no GPU profiler wired up - stays 0
    float gputimeBB = 0;   // GPU time
    float buildTime = 0;


    ew::Buffer::SharedPtr  buffer_gpuSort;
    ew::Buffer::SharedPtr  buffer_feedback;
    ew::ReadbackBuffer::SharedPtr  buffer_feedback_read;   // fence-checked ring: costs 1-2 frames of latency, avoids a full GPU stall per frame
    vegetation_feedback feedback = {};
    uint32_t feedbackFrameCounter = 0;  // render() calls; tags the readback ring
    uint32_t feedbackAgeFrames = 0;     // render()-frames between enqueue and the mapped slot (HUD info; the struct is frame-global counters, no recyclable slots)
    float loddingBias = 1.f;

    ew::Buffer::SharedPtr   drawArgs_vegetation;
    ew::Buffer::SharedPtr   drawArgs_billboards;
    ew::computeShader		compute_clearBuffers;
    ew::computeShader		compute_calulate_lod;
    ew::computeShader		compute_sortCombine;
    ew::computeShader		compute_sortCombine_POST;

    int currentLOD = -1;
    float3 camVector;



    largeTexture textureToolData;


    //bool showDebugInShader = false;
    //bool showNumPivots = false;

    static _plantBuilder* selectedPart;

    // randomizer
    static std::mt19937 generator;
    static std::uniform_int_distribution<> rand_int;


    int firstPlant = 0;
    int lastPlant = 10000; // just big
    int firstLod = -1;
    int lastLod = 100;

    // Ortho render for sampling
    bool uniformSpread = false;
    bool SAMPLE_MODE = false;
    ew::Fbo::SharedPtr shadowFbo;
    ew::Fbo::SharedPtr rgbFbo;
    glm::mat4  _shadow_viewproj{ 1.f };
    void bakeShadowMap(ew::GpuContext* _renderContext);
    float camRot = 0;
    float camPitch = 0;
    float shadowPitch = 0;
    float3 sunDirectionShadowMap;
    ew::Texture::SharedPtr RGB_MAP;
    void buildOneMap(float _sunAngle);

    ew::computeShader	compute_sampleRGBtoPixel;
    ew::computeShader	compute_sampleRGBtoPixel_ToTexture;
    ew::Buffer::SharedPtr   rgb_data;
    void buildBDRF();

    // we need to grab information from the render for overlay
    struct
    {
        ew::GpuContext*             context = nullptr;
        glm::mat4                   viewproj;
        float3                      cameraPos;
        float                       half_to_Pixels_SinglePlant; // for origin
        float                       half_to_Pixels; // for origin
    }renderInfo;




    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        (void)archive; (void)_version;
    }
};
CEREAL_CLASS_VERSION(_rootPlant, 100);
