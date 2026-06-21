#pragma once
#include "FalcorCompat.hpp"
#include "computeShader.h"
#include "pixelShader.h"

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
//#include"hlsl/terrain/vegetation_defines.hlsli"
//#include"hlsl/terrain/groundcover_defines.hlsli"    // FIXME combine these two

#include "ribbonBuilder.h"

using namespace Falcor;

#define MAX_PLANT_BLOCKS 1048576
#define MAX_PLANT_INSTANCES 65536
#define MAX_PLANT_BILLBOARDS 65536
#define MAX_PLANT_PLANTS 1024
#define MAX_PLANT_PIVOTS MAX_PLANT_PLANTS * 256
#define MAX_PLANT_VERTS 524288

class _plantMaterial;

class materialCache_plants {
public:
    static std::string lastFile;   // fixme, try to copy this to and from vegMaterial on start and exit to remember

    int find_insert_material(const std::string _path, const std::string _name, bool _forceReload = false);
    int find_insert_material(const std::filesystem::path _path, bool _forceReload = false);
    int find_insert_texture(const std::filesystem::path _path, bool isSRGB, bool _forceReload = false);
    std::string clean(const std::string _s);

    void setTextures(ShaderVar& var);

    void rebuildStructuredBuffer();

    std::vector<_plantMaterial>	materialVector;
    int selectedMaterial = -1;
    std::vector<Texture::SharedPtr>			textureVector;
    float texMb = 0;

    void renderGui(Gui* mpGui, Gui::Window& _window);
    void renderGuiTextures(Gui* mpGui, Gui::Window& _window);
    bool renderGuiSelect(Gui* mpGui);

public:
    int dispTexIndex = -1;
    Texture::SharedPtr getDisplayTexture();

    Buffer::SharedPtr sb_vegetation_Materials = nullptr;

    bool modified = false;
    bool modifiedData = false;
};



class _plantMaterial
{
public:
    void renderGui(Gui* _gui);

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
    void import(bool _replacePath = true);
    void save();
    void eXport(std::filesystem::path _path);
    void eXport();
    void reloadTextures();
    void loadTexture(int idx);

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
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

        if (_version >= 101)
        {
            archive_float3(_constData.albedoScale[0]);
            archive_float3(_constData.albedoScale[1]);
            archive(CEREAL_NVP(_constData.roughness[0]));
            archive(CEREAL_NVP(_constData.roughness[1]));
        }
    }


    sprite_material _constData;
};
CEREAL_CLASS_VERSION(_plantMaterial, 101);




struct _vegetationMaterial {
    std::string name;
    std::string displayname;
    int index = -1;

    template<class Archive>
    void serialize(Archive& archive)
    {
        archive(CEREAL_NVP(name));
        archive(CEREAL_NVP(displayname));
    }
};









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
    }

    float3      parentStemDir = { 0, 1, 0 };
    glm::mat4   root; // expand for the root stem direction as well to avoid growing through
    float       numSegments;
    float       pixelSize = 0.001f;  //1 cm
    int         seed = 1000;
    float       node_age = -1.f; // dont use for root    // this one is a node age, like 12, good for building twigs
    float       normalized_age = 1.f;  // this one is a 0..1 age useful for leaves etc
    bool        isBaking = false;    // for billboard baking
    bool        doNotAddPivot = false;  // for replacement stems

    // PIVOT POIUTNS
    uint    pivotIndex[4];
    int     pivotDepth = 0;

    // new values for more complex builds
    int max_verts = 1000000;
    int callDepth = 0;

};

struct packSettings
{
    float objectSize = 15.0f;
    float radiusScale = 5.0f;                   //  so biggest radius
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

    void loadFromFile();
    void reload();
    bool renderGui(uint& gui_id);

    template<class Archive>
    void serialize(Archive& archive)
    {
        archive(name);
        archive(path);
        archive_float2(albedoScale);
        archive_float2(translucencyScale);
    }
};



enum bakeType { BAKE_NONE, BAKE_DIAMOND, BAKE_4, BAKE_N };
class levelOfDetail
{
public:
    levelOfDetail() { ; }
    levelOfDetail(uint _numPix) { numPixels = _numPix; }

    int numPixels = 100;        // this is the number of height pixels to use for this lod. Used to calculate pixel size
    float pixelSize = 0.1f;      // This is for plant on GPU - determines when to split

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
        archive(CEREAL_NVP(numPixels));
        archive(CEREAL_NVP(pixelSize));

        if (_version >= 101)
        {
            archive(CEREAL_NVP(geometryPixelScale));
            archive(CEREAL_NVP(useGeometry));
            archive(CEREAL_NVP(bakeIndex));
            archive(CEREAL_NVP(bakeType));
        }
    }
};
CEREAL_CLASS_VERSION(levelOfDetail, 101);



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
    bool forceDiamond = false;
    bool faceCamera = false;

    template<class Archive>
    void serialize(Archive& archive)
    {
        archive(material);
        archive_float2(extents);
        archive(pixHeight);
        archive(bakeWidth);
        archive(dU);

        archive(CEREAL_NVP(translucency));
        archive(CEREAL_NVP(alphaPow));
        archive(CEREAL_NVP(bakeAOToAlbedo));
        archive_float2(bake_V);

        archive(CEREAL_NVP(forceDiamond));
        archive(CEREAL_NVP(faceCamera));
    }
};





class _plantBuilder
{
public:
    virtual void loadPath() { ; }
    virtual void savePath() { ; }
    virtual void load() { ; }
    virtual void save() { ; }
    virtual void saveas() { ; }
    virtual void renderGui() { ; }
    virtual void treeView() { ; }
    virtual void incrementLods() { ; }
    virtual void decrementLods() { ; }
    virtual glm::mat4 build(buildSetting _settings, bool _addVerts) { return glm::mat4(1.f); }
    virtual float2 calculate_extents(glm::mat4 view) { return float2(0, 0); }
    virtual glm::mat4 getTip(bool includeChildren = true) { return glm::mat4(1.f); }
    virtual lodBake* getBakeInfo(uint i) { return nullptr; }    // so does nothing if not implimented
    virtual levelOfDetail* getLodInfo(uint i) { return nullptr; }    // so does nothing if not implimented

    std::string name = "not set";
    std::string path = "no path either";   // relative
    bool changed = true;
    bool changedForSave = false;
    static Gui* _gui;

    // lighting information
    float shadowUVScale = 1.f;
    float shadowSoftness = 0.15f;
    float shadowDepth = 1.f;
    float shadowPenetationHeight = 0.3f;

    // packing debug info
    uint numInstancePacked = 0;
    uint numVertsPacked = 0;
    uint numPivots = 0;
    virtual void clear_build_info() { ; }

    // ossilations
    float ossilation_stiffness = 1.f;   // this affects how far it bends for certain wind types, but also
    float ossilation_constant_sqrt = 10.f;     //freq = (1 / 2π) * √(g / L). so this is g/L per meter, we scale by a furher √(1/L)
    float ossilation_power = 1.f;       // DE{RECATED shifts the bend towards the root or the tip
    float rootFrequency() { return 0.1591549430f * ossilation_constant_sqrt * sqrt(1.f / ossilation_stiffness); }
    int     deepest_pivot_pack_level = 3;
};

enum plantType { P_LEAF, P_STEM, P_CLUMP, P_AGREGATE, P_GROVE, PLANT_END };

// planmaterial repackadged for dandom_arrays
class _plantRND
{
public:
    std::string name;
    std::string path;   // relative
    plantType  type = PLANT_END;
    std::shared_ptr<_plantBuilder> plantPtr;

    void loadFromFile();
    void reload();
    void renderGui(uint& gui_id);

    template<class Archive>
    void serialize(Archive& archive)
    {
        archive(name);
        archive(path);
        archive(type);
    }
};





// would be nice if we have more controll over distribution including with numSegments
template <class T> class randomVector
{
public:
    randomVector() { data.resize(1); }      // minimum size has to be one, otherwise get() is dangerous, does not explain clear()
    std::vector<T> data;
    int rnd_idx = 0;

    void renderGui(char* name, uint& gui_id);
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

    void loadFromFile();
    void reload();
    bool renderGui(uint& gui_id);

    bool operator < (const _randomBranch& _b) const
    {
        return (params.y < _b.params.y);
    }

    template<class Archive>
    void serialize(Archive& archive)
    {
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
    bool renderGui(char* name, uint& gui_id);
    void clear() { branchData.clear(); }
    _randomBranch* get(float _val);

    template<class Archive>
    void serialize(Archive& archive)
    {
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
    void load();
    void save();
    void saveas();
    void renderGui();
    void treeView();
    void clear_build_info();
    glm::mat4 build(buildSetting _settings, bool _addVerts);

    FileDialogFilterVec filters = { {"leaf"} };

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


public:
    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        archive_float2(stem_length);
        archive_float2(stem_width);
        archive_float2(stem_curve);
        archive_float2(stem_to_leaf);
        archive_float2(stemVerts);

        archive(cameraFacing);
        archive_float2(leaf_length);
        archive_float2(leaf_width);
        archive_float2(leaf_curve);
        archive_float2(leaf_twist);
        archive_float2(width_offset);
        archive_float2(gravitrophy);
        archive_float2(numVerts);  // teh define works for ints as well

        archive(stem_Material);
        archive(materials.data); //??? moce to randomVector, keep up to date there? more contained

        if (_version >= 101)
        {
            archive_float2(perlinCurve);
            archive_float2(perlinTwist);
        }

        stem_Material.reload();
        for (auto& M : materials.data) M.reload();

        archive(CEREAL_NVP(ossilation_stiffness));
        archive(CEREAL_NVP(ossilation_constant_sqrt));
        archive(CEREAL_NVP(ossilation_power));
        archive(CEREAL_NVP(deepest_pivot_pack_level));
        archive(CEREAL_NVP(pivotType));
        archive(CEREAL_NVP(useTwoVertDiamond));
        archive(CEREAL_NVP(leafLengthSplit));

        if (_version >= 102)
        {
            archive_float2(stem_to_leaf_Roll);
        }

    }
};
CEREAL_CLASS_VERSION(_leafBuilder, 102);




class _stemBuilder : public _plantBuilder
{
public:
    void loadPath();
    void savePath();
    void load();
    void save();
    void saveas();
    void renderGui();
    void treeView();
    virtual void incrementLods() { lodInfo.resize(lodInfo.size() + 1); }
    virtual void decrementLods() { if (lodInfo.size() > 3) lodInfo.resize(lodInfo.size() - 1); }
    lodBake* getBakeInfo(uint i);//bakeInfo
    levelOfDetail* getLodInfo(uint i);
    glm::mat4  build_2(buildSetting _settings, uint _bakeIndex, bool _faceCamera);
    glm::mat4  build_4(buildSetting _settings, uint _bakeIndex, bool _faceCamera);
    glm::mat4  build_n(buildSetting _settings, uint _bakeIndex, bool _faceCamera);
    void build_leaves(buildSetting _settings, uint _max, bool _addVerts);
    void build_tip(buildSetting _settings, bool _addVerts);
    //void build_extents(buildSetting _settings);
    float2 calculate_extents(glm::mat4 view);
    glm::mat4 getTip(bool includeChildren = true);
    void build_NODES(buildSetting _settings, bool _addVerts);
    glm::mat4 build(buildSetting _settings, bool _addVerts);
    void clear_build_info();



    //void build_Nodes(buildSetting& _settings);
    std::vector<glm::mat4> NODES;
    glm::mat4 tip_NODE;
    //std::vector<glm::mat4> NODES_PREV; // bad bad leroy brown do without, so build twice, alwasy
    float age;  // number of segments floatign pouint, after randomize, needed for leaves build
    float tip_width = 0;
    float root_width = 0;
    int     firstLiveSegment = 1;

    FileDialogFilterVec filters = { {"stem"} };

    //Stem has to maintain a minimum of 3 of these or will crash
    std::vector<levelOfDetail> lodInfo = { levelOfDetail(10), levelOfDetail(14), levelOfDetail(40), levelOfDetail(100), levelOfDetail(300), levelOfDetail(500) };
    std::array<lodBake, 3> lod_bakeInfo = { lodBake(32, 1.f), lodBake(64, 0.6f), lodBake(128, 0.2f) };

    // stem
    float2  numSegments = { 5.3f, 0.3f };
    float2  max_live_segments = { 10.f, 0.2f };  // curve for width growth
    float2  stem_length = { 50.f, 0.2f };   // in mm
    float2  stem_width = { 1.f, 0.2f };     // width at the start
    float2  stem_d_width = { 0.1f, 0.1f };  // width growth per node
    float2  stem_pow_width = { 0.1f, 0.1f };  // curve for width growth
    float2  stem_curve = { 0.2f, 0.3f };      // radian bend over lenth
    float2  stem_phototropism = { 0.0f, 0.3f };
    float2  node_rotation = { 0.7f, 0.3f };   // like 2 leaves 90 degrees
    float2  node_angle = float2(0.2f, 0.2f);    // andgle that the stem bends at the node ??? always away fromt he leaf angle if there is such a thing
    _vegMaterial stem_Material;
    bool    bake2_replaces_stem = false;    // deprecated
    float   nodeLengthSplit = 32.f;
    randomVector<_plantRND> stemReplacement;    // If set this replaces the stem completely
    bool roll_horizontal = false;
    float rollOffset = 0.f;
    float2  perlinCurve = { 0.f, 4.0f };
    float2  perlinTwist = { 0.f, 4.0f };
    bool    hasPivot = true;


    // branches, can be leavea
    float2  numLeaves = { 3.f, 0.f };  // per segment
    float2  leaf_angle = float2(0.5f, 0.3f);   // angle that the leaves come out of
    float2  leaf_rnd = float2(0.3f, 0.3f);
    float   leaf_age_power = 2.f;
    bool    twistAway = false;      // I think deprecated, cant  do in rameworkif single leaf, activelt twist stem to the other side
    randomVector<_plantRND> leaves;

    semiRandomBranch    branches;   // can be leases or clusters as well as stems, basically anything

    // tip
    bool unique_tip;
    randomVector<_plantRND> tip;
    float2  tip_age = float2(-1.f, 0.f);

    bool leaf_age_override = false; // If set false we pass in -1 and teh leaf can set its own age, if true we set the age

    // feedback
    uint numLeavesBuilt = 0;

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
        archive(stem_Material);
        stem_Material.reload();

        archive_float2(numLeaves);

        archive_float2(leaf_angle);
        archive_float2(leaf_rnd);
        archive(leaf_age_power);
        archive(twistAway);
        archive(leaves.data);
        for (auto& M : leaves.data) M.reload();

        archive(unique_tip);
        archive(tip.data);
        if (unique_tip) {
            for (auto& M : tip.data) M.reload();
        }

        archive(lod_bakeInfo);
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

        if (_version >= 101)
        {
            archive(CEREAL_NVP(leaf_age_override));
            archive(stemReplacement.data);
            for (auto& M : stemReplacement.data) M.reload();
        }

        if (_version >= 102)
        {
            archive(roll_horizontal);
        }

        if (_version >= 103)
        {
            archive(rollOffset);
        }

        if (_version >= 104)
        {
            archive_float2(perlinCurve);
            archive_float2(perlinTwist);
        }
        if (_version >= 105)
        {
            archive(hasPivot);
        }

        if (_version >= 106)
        {
            archive(CEREAL_NVP(branches));
        }
        else
        {
            // convert leabes to brancges
            branches.branchData.clear();
            for (auto& M : leaves.data)
            {
                _randomBranch b;
                b.name = M.name;
                b.path = M.path;
                b.type = M.type;
                b.reload();
                branches.branchData.push_back(b);
            };
            branches.buildArray();
        }
        
    }
};
CEREAL_CLASS_VERSION(_stemBuilder, 106);




class _clumpBuilder : public _plantBuilder
{
public:
    void loadPath();
    void savePath();
    void load();
    void save();
    void saveas();
    void renderGui();
    void treeView();
    virtual void incrementLods() { lodInfo.resize(lodInfo.size() + 1); }
    virtual void decrementLods() { if (lodInfo.size() > 3) lodInfo.resize(lodInfo.size() - 1); }
    lodBake* getBakeInfo(uint i);//bakeInfo
    levelOfDetail* getLodInfo(uint i);

    void clear_build_info();
    float2 calculate_extents(glm::mat4 view);
    glm::mat4 getTip(bool includeChildren = true);
    glm::mat4 build(buildSetting _settings, bool _addVerts);
    glm::mat4 buildChildren(buildSetting _settings, bool _addVerts);
    glm::mat4 build_2(buildSetting _settings, uint _bakeIndex, bool _faceCamera);
    glm::mat4 build_4(buildSetting _settings, uint _bakeIndex, bool _faceCamera);



    FileDialogFilterVec filters = { {"clump"} };
    std::vector<glm::mat4> ROOTS;
    glm::mat4 START;
    glm::mat4 TIP_CENTER;

    std::vector<levelOfDetail> lodInfo = { levelOfDetail(10), levelOfDetail(14), levelOfDetail(40), levelOfDetail(100), levelOfDetail(300), levelOfDetail(500) };
    //Stem has to maintain a minimum of 3 of these or will crash
    std::array<lodBake, 3> lod_bakeInfo = { lodBake(32, 1.f), lodBake(64, 0.6f), lodBake(128, 0.2f) };

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



    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        // clump
        archive_float2(size);
        archive_float2(aspect);
        archive(radial);

        // child
        archive_float2(numChildren);
        archive_float2(child_angle);    // from circle
        archive_float2(child_rnd);
        archive_float2(child_age);
        archive(child_age_power);
        archive(children.data);
        for (auto& M : children.data) M.reload();

        // baking lodding and sway
        archive(lod_bakeInfo);
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
CEREAL_CLASS_VERSION(_clumpBuilder, 100);


class binaryPlantOnDisk
{
public:
    _vegMaterial    billboardMaterial;
    std::map<int, _vegMaterial> materials;
    std::vector<plant> plantData;           // for laoding onlt
    std::vector<ribbonVertex8> vertexData;
    std::vector < _plant_anim_pivot> pivotData;

    uint numP;
    uint numV;

    void onLoad(std::string path, uint vOffset);

    template<class Archive>
    void serialize(Archive& archive)
    {
        archive(billboardMaterial);
        archive(materials);
        archive(numP);
        archive(numV);

    }
};
CEREAL_CLASS_VERSION(binaryPlantOnDisk, 100);

class recentFiles
{
    /*
    struct
    {
        bool operator < (const _pathSort& pth) const
        {
            return (age < pth.age);
        }

        std::string path = "";
        int age = 0;
    }_pathSort;
    //void get(std::string pth);
    std::array<_pathSort, 10> paths;


    std::sort(paths.begin(), paths.end(), [](float3 a, float3 b) {
        // Custom comparison logic
        return a.x > b.x; // this sorts in ascending order
        });
        */
    template<class Archive>
    void serialize(Archive& archive)
    {
        //  archive(paths);
    }
};
CEREAL_CLASS_VERSION(recentFiles, 100);

class _rootPlant
{
public:
    void onLoad();
    bool onKeyEvent(const KeyboardEvent& keyEvent);
    void renderGui_perf(Gui* _gui);
    void renderGui_lodbake(Gui* _gui);
    void renderGui_other(Gui* _gui);
    void renderGui_rightPanel(Gui* _gui);
    void renderGui_load(Gui* _gui);
    void renderGui(Gui* _gui);
    void buildAllLods();
    void build(uint pivotOffset = 0);
    void loadMaterials();
    int importBinary(std::filesystem::path filepath);       // FIXME this needs a cache
    void importBinary();
    void buildFullResolution();
    std::vector<std::string>	importPathVector;



    void bake(std::string _path, std::string _seed, lodBake* _info, glm::mat4 VIEW);
    int bake_Reload_Count = 0;
    void render(RenderContext* _renderContext, const Fbo::SharedPtr& _fbo, GraphicsState::Viewport _viewport, Texture::SharedPtr _hdrHalfCopy,
        rmcv::mat4  _viewproj, float3 camPos, rmcv::mat4  _view, rmcv::mat4  _clipFrustum, float halfAngle_to_Pixels, bool terrainMode = false);

    RenderContext* renderContext;

    //??? lodding info, so it needs all the runtime data

    bool displayModeSinglePlant = true;

    float instanceArea[4] = { 10.f, 10.f, 10.f, 10.f };
    bool cropLines = false;
    void builInstanceBuffer();

    _plantBuilder* root = nullptr;
    buildSetting settings;
    float rootPitch = 0.01f; // so we can go sideways to test brancges
    float rootYaw = 0.0f; // so we can go sideways to test brancges
    float rootRoll = 0.0f; // so we can go sideways to test brancges
    glm::mat4 bakeViewMatrix, bakeViewAdjusted;
    packSettings vertex_pack_Settings;
    float2 extents;

    // Binary viewer
    int numBinaryPlants = 0;
    int binVertexOffset = 0;
    int binPlantOffset = 0;
    int binPivotOffset = 0;

    float3 windDir = { 1, 0, 0 };
    float windStrength = 1.f;      // m/s

    // render and bake
    Sampler::SharedPtr			sampler_ClampAnisotropic;
    Sampler::SharedPtr			sampler_Ribbons;
    RasterizerState::SharedPtr      rasterstate;
    BlendState::SharedPtr           blendstate;
    BlendState::SharedPtr           blendstateBake;
    pixelShader vegetationShader;
    pixelShader vegetationShader_GOURAUD;
    pixelShader vegetationShader_DEBUG_PIVOTS;
    pixelShader vegetationShader_DEBUG_PIXELS;
    pixelShader billboardShader;
    pixelShader bakeShader;
    ShaderVar varVegTextures;
    ShaderVar varTextures_Gauraud;
    ShaderVar varTextures_Debug_Pivots;
    ShaderVar varTextures_Debug_Pixels;
    ShaderVar varBBTextures;
    ShaderVar varBakeTextures;

    computeShader		compute_bakeFloodfill;



    Buffer::SharedPtr blockData;
    Buffer::SharedPtr instanceData;
    Buffer::SharedPtr instanceData_Billboards;
    Buffer::SharedPtr plantData;
    Buffer::SharedPtr plantpivotData;               // this is loaded from ribbonBuilder, or imported
    Buffer::SharedPtr vertexData;                   // this is loaded from ribbonBuilder, or imported
    std::array<plant, 256> plantBuf;
    uint totalBlocksToRender = 0;
    uint unusedVerts = 0;
    void updateMaterialsAndTextures();
    float gputime;
    float gputimeBB;   // GPU time
    float buildTime = 0;


    Buffer::SharedPtr  buffer_feedback;
    Buffer::SharedPtr  buffer_feedback_read;
    vegetation_feedback feedback;
    float loddingBias = 1.f;

    Buffer::SharedPtr   drawArgs_vegetation;
    Buffer::SharedPtr   drawArgs_billboards;
    computeShader		compute_clearBuffers;
    computeShader		compute_calulate_lod;

    int currentLOD = -1;
    float3 camVector;
    float pixelSize = 0.f;
    float pixelmm = 0.f;
    uint expectedLod = 0;
    float m_halfAngle_to_Pixels;

    bool showDebugInShader = false;
    bool showNumPivots = false;

    static _plantBuilder* selectedPart;
    static _plantMaterial* selectedMaterial;

    // randomizer
    static std::mt19937 generator;
    static std::uniform_int_distribution<> rand_int;

    // cleanup for left side
    bool showPerformance = true;
    bool showBaking = true;
    bool showLodding = true;
    bool showRest = false;

    int firstPlant = 0;
    int lastPlant = 10000; // just big
    int firstLod = 0;
    int lastLod = 100;

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
    }
};
CEREAL_CLASS_VERSION(_rootPlant, 100);








































/*  Backyup using first al;d last vbis

glm::mat4 _leafBuilder::build(buildSetting _settings, bool _addVerts)
{
    uint startVerts = _ribbonBuilder.numVerts();

    std::uniform_real_distribution<> d50(0.5f, 1.5f);
    std::uniform_int_distribution<> distAlbedo(-50, 50);
    std::uniform_int_distribution<> distPerlin(1, 50000);

    const siv::PerlinNoise::seed_type seed = distPerlin(_rootPlant::generator);
    const siv::PerlinNoise perlin{ seed };
    const siv::PerlinNoise::seed_type seedT = distPerlin(_rootPlant::generator);
    const siv::PerlinNoise perlinTWST{ seedT };

    glm::mat4 node = _settings.root;
    float age = pow(_settings.normalized_age, 1.f);
    bool stemVisible = false;

    float lengthS = RND_B(leaf_length) * 0.001f * age;                  // freq and stiffness needs to apply to both stem and leaf
    float widthS = RND_B(leaf_width) * 0.001f * age;
    float freq = rootFrequency() * sqrt(lengthS) / sqrt(widthS);
    float stiffness = 1.f / ossilation_stiffness;

    // stem
    if (stem_length.x > 0)
    {
        float length = RND_B(stem_length) / 100.f * 0.001f * age;   // to meters and numSegments
        float width = stem_width.x * 0.001f * age;
        float curve = RND_CRV(stem_curve) / 100.f * age;

        // Lodding stem, but use length instead............................................................
        int numStem = glm::clamp((int)((length / _settings.pixelSize) / 8.f * 100.f), 1, stemVerts.y);     // 1 for every 8 pixels, clampped
        float step = 99.f / (numStem);
        float cnt = 0.f;

        if (_addVerts && (width > _settings.pixelSize))
        {
            _ribbonBuilder.startRibbon(true, _settings.pivotIndex);
            _ribbonBuilder.set(node, width * 0.5f, stem_Material.index, float2(1.f, 0.f), 1.f, 1.f, !(pivotType == pivot_leaf), stiffness, freq);
            stemVisible = true;
        }

        for (int i = 0; i < 100; i++)
        {
            PITCH(node, curve);
            GROW(node, length);

            cnt++;
            if (stemVisible && cnt >= step)
            {
                _ribbonBuilder.set(node, width * 0.5f, stem_Material.index, float2(1.f, (float)i / 99.f), 1.f, 1.f, !(pivotType == pivot_leaf), stiffness, freq);
                cnt -= step;
            }
        }

        GROW(node, -width * 0.5f);  // Now move ever so slghtly backwards for better penetration of stem to leaf
    }


    ROLL(node, RND_CRV(stem_to_leaf_Roll));                         // rotation from stem to leaf
    PITCH(node, RND_CRV(stem_to_leaf));


    // build the leaf
    {
        _vegMaterial mat = materials.get();
        float albedoScale = RND_ALBEDO(glm::lerp(mat.albedoScale.y, mat.albedoScale.x, age));
        float translucentScale = glm::lerp(mat.translucencyScale.y, mat.translucencyScale.x, age);

        float length = RND_B(leaf_length) / 100.f * 0.001f * age;   // to meters and numSegments  // FIXME scale to neter built into macro, rename macro for distamce only
        float width = RND_B(leaf_width) * 0.001f * age;
        float gravi = RND_CRV(gravitrophy) / 100.f * age;
        float curve = RND_CRV(leaf_curve) / 100.f * age;
        float twist = RND_CRV(leaf_twist) / 100.f * age;

        // Lodding leaf............................................................
        bool showLeaf = (width * d50(_rootPlant::generator)) > _settings.pixelSize;
        if (showLeaf)
        {
            int numLeaf = glm::clamp((int)((length / _settings.pixelSize) / leafLengthSplit * 100.f), numVerts.x - 1, numVerts.y - 1);
            uint firstVis = 0;
            uint lastVis = 99;
            bool first = true;

            for (int i = 0; i < 100; i++)
            {
                float t = (float)i / 100.f;
                float du = __min(1.f, sin(pow(t, width_offset.x) * 3.1415f) + width_offset.y);
                if (first)
                {
                    if (width * du <= _settings.pixelSize)
                    {
                        firstVis = i;
                        lastVis = i;
                    }
                    else first = false;
                }
                else
                {
                    if (width * du > _settings.pixelSize)
                    {
                        lastVis = i;
                    }
                }

            }
            float step = (float)(lastVis - firstVis) / numLeaf;
            float cnt = 0;

            // Fixme search for first and last vertex on size
            bool useDiamond = (useTwoVertDiamond && (numLeaf == 1));
            if (useDiamond)
            {
                firstVis = 0;
                lastVis = 99;
                step = 99;
            }



            for (int i = 0; i < 100; i++)
            {
                float t = (float)i / 99.f;
                float du = __min(1.f, sin(pow(t, width_offset.x) * 3.1415f) + width_offset.y);
                if (numLeaf == 1) du = 1.f;  //??? use 3 mqybe, 2 still curts dcorners

                float perlinScale = glm::smoothstep(0.f, 0.3f, t) * age;
                float noise = (float)perlin.normalizedOctave1D(perlinCurve.y * t, 4);
                PITCH(node, noise * perlinCurve.x * perlinScale);

                noise = (float)perlinTWST.normalizedOctave1D(perlinTwist.y * t, 4) * age;
                ROLL(node, noise * perlinTwist.x * perlinScale);

                ROLL(node, twist);
                PITCH(node, curve);
                GROW(node, length);


                if (_addVerts && i == firstVis)
                {
                    uint oldRoot = _ribbonBuilder.getRoot();
                    _ribbonBuilder.startRibbon(cameraFacing, _settings.pivotIndex);
                    if (stemVisible)
                    {
                        _ribbonBuilder.setRoot(oldRoot);
                        // uglu but means that the two ribbon-s share the one root
                    }

                    _ribbonBuilder.set(node, width * 0.5f * du, mat.index, float2(du, 1.f - t), albedoScale, translucentScale, !(pivotType == pivot_leaf), stiffness, freq, pow((float)i / 99.f, ossilation_power), useDiamond);
                    cnt = 0;
                }
                else if (i > firstVis)
                {
                    cnt++;
                    if (_addVerts && (i <= lastVis) && cnt >= step)
                    {
                        _ribbonBuilder.set(node, width * 0.5f * du, mat.index, float2(du, 1.f - t), albedoScale, translucentScale, !(pivotType == pivot_leaf), stiffness, freq, pow((float)i / 99.f, ossilation_power), useDiamond);
                        cnt -= step;
                    }
                }
            }
        }
    }

    uint numVerts = _ribbonBuilder.numVerts() - startVerts;
    if (numVerts > 0) numInstancePacked++;
    numVertsPacked += numVerts;
    changedForSave |= changed;
    return node;
}

*/











