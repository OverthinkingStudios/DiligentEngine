// vegetationBuilder.cpp - the procedural plant builders (leaf, stem, clump,
// flower), the material and texture cache, the LOD/billboard bake
// orchestration, and the runtime render path for all vegetation.
//
// The build pipeline - seed discipline, LOD loops, vertex packing - is
// hyper-tuned. Do not "clean up" anything in here that you cannot prove safe.

#include "terrain.h"    // brings in the hlsli-shared structs/aliases, ribbonBuilder.h and vegetationBuilder.h
//#include "imgui.h"
#include "PerlinNoise.hpp"          //https://github.com/Reputeless/PerlinNoise/blob/master/PerlinNoise.hpp

#include <iostream>
#include <memory>
#include <chrono>
#include <thread>
#include <cstdlib>      // __min/__max (MSVC macros), system()

#include "glm/gtx/compatibility.hpp"    // glm::saturate/lerp on vectors
#include "glm/gtc/matrix_transform.hpp"

#include "ots/Log.hpp"

using namespace std::chrono;

#define TOOLTIP(x)  if (ImGui::IsItemHovered()) {ImGui::SetTooltip(x);}

// TODO: Load-bearing until the underlying issue is understood - do not remove.
// There is no matching optimize("", on), so it covers the whole TU. Unlike the
// sibling build-time-only TUs that includes the per-frame _rootPlant::render(),
// so it is a live per-frame cost. Find the real issue and scope the pragma down.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif


using Diligent::BIND_SHADER_RESOURCE;
using Diligent::BIND_UNORDERED_ACCESS;
using Diligent::BIND_RENDER_TARGET;
using Diligent::BIND_DEPTH_STENCIL;
using Diligent::BIND_INDIRECT_DRAW_ARGS;


namespace
{
// Not implemented: capturing a texture to file. The bake and texture-tool
// exports log and skip. Returns false so call sites also skip the shell-outs
// that would have consumed the written file.
bool captureToFileStub(const char* what, const std::string& path)
{
    spdlog::error("vegetation: captureToFile is not implemented - '{}' export to '{}' skipped (authoring/bake path)", what, path);
    return false;
}

// Full GPU sync. Only the offline bake and sample paths use it, never
// per-frame.
void flushAndWait(ew::GpuContext* pCtx)
{
    pCtx->context()->Flush();
    pCtx->context()->WaitForIdle();
}
} // namespace




float GLOBAL_RND_SIZE = 0.3f;

bool LOGTHEBUILD = false;




float perlinData[1024];

materialCache_plants _plantMaterial::static_materials_veg;
std::string materialCache_plants::lastFile;


ribbonBuilder _ribbonBuilder;
extentsCalculator   EXTENTS;




// _rootPlant
_plantBuilder* _rootPlant::selectedPart = nullptr;
std::string _rootPlant::root_path;
std::mt19937 _rootPlant::generator(100);
std::uniform_int_distribution<> _rootPlant::rand_int(0, 65535);
std::uniform_int_distribution<> DD_0_255(0, 255);   // for pivot shifts



void replaceAllVEG(std::string& str, const std::string& from, const std::string& to) {
    if (from.empty())
        return;
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // In case 'to' contains 'from', like replacing 'x' with 'yx'
    }
}


std::string materialCache_plants::clean(const std::string _s)
{
    std::string str = _s;
    size_t start_pos = 0;
    while ((start_pos = str.find("\\", start_pos)) != std::string::npos) {
        str.replace(start_pos, 2, "/");
        start_pos += 1;
    }
    return str;
}

int materialCache_plants::find_insert_material(const std::string _path, const std::string _name, bool _forceReload)
{
    std::filesystem::path fullPath = _path + _name + ".vegetationMaterial";
    if (std::filesystem::exists(fullPath))
    {
        return find_insert_material(fullPath, _forceReload);
    }
    else
    {
        // Now we have to search, but use the first one we find
        std::string fullName = _name + ".vegetationMaterial";
        fullPath = terrafectorEditorMaterial::rootFolder + "/vegetationMaterial";
        // recursive_directory_iterator throws if the root does not exist, and
        // a missing vegetationMaterial folder is normal - take the error_code
        // overload so it reports empty instead.
        std::error_code ec;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(fullPath, ec))
        {
            std::string subPath = clean(entry.path().string());
            if (subPath.find(fullName) != std::string::npos)
            {
                return find_insert_material(subPath, _forceReload);
            }
        }
    }

    spdlog::error("vegetation: error : vegetation material - {} does not exist", _name);
    return -1;
}


// force a new one? copy last and force a save ?
int materialCache_plants::find_insert_material(const std::filesystem::path _path, bool _forceReload)
{
    for (uint i = 0; i < materialVector.size(); i++)
    {
        if (materialVector[i].fullPath.compare(_path) == 0)
        {
            if (_forceReload)
            {
                materialVector[i].import(_path);
                materialVector[i].makeRelative(_path);       // not sure f needed but its below, fols down deeper
            }
            return i;
        }
    }

    // else add new
    if ((_path.string().find("vegetationMaterial") != std::string::npos) && std::filesystem::exists(_path))
    {
        uint materialIndex = (uint)materialVector.size();
        _plantMaterial& material = materialVector.emplace_back();
        material.import(_path);
        material.makeRelative(_path);
        spdlog::info("vegetation: add vegeatation material[{}] - {}", materialIndex, _path.filename().string());
        return materialIndex;
    }
    else
    {
        spdlog::error("vegetation:  NOT FOUND add vegeatation- {}", _path.filename().string());
    }

    return -1;
}



int materialCache_plants::find_insert_texture(const std::filesystem::path _path, bool isSRGB, bool _forceReload)
{
    if (std::filesystem::is_directory(_path)) return -1;     // Its a directory not a file, ignore
    if (!std::filesystem::exists(_path)) return -1;             // it doesmt ecists - common when baking and a texture fails return -1


    modified = true;

    for (uint i = 0; i < textureVector.size(); i++)
    {
        // ew::Texture carries no source path, so the dedup/reload key comes
        // from the parallel texturePathVector.
        if (texturePathVector[i].compare(_path) == 0)
        {
            if (_forceReload)
            {
                textureVector[i] = ew::Texture::createFromFile(texturePathVector[i], true, isSRGB);
                // FIXME can we save a timestamp and only relaod if that has changed
            }
            return i;
        }
    }

    ew::Texture::SharedPtr tempTexture = ew::Texture::createFromFile(_path, false, false);
    int totalPixels = tempTexture->getWidth() * tempTexture->getHeight();
    bool pleaseCompress = totalPixels > (64 * 64);  // so dont compress teh small ones

    std::string ddsFilename = _path.string();
    if (pleaseCompress)
    {
        if (_path.string().find(".dds") == std::string::npos)
        {
            ddsFilename = _path.string() + ".earthworks.dds";
        }
        if (!std::filesystem::exists(ddsFilename))
        {
            std::string resource = terrafectorEditorMaterial::rootFolder;
            replaceAllVEG(resource, "/", "\\");
            std::string temp = resource + "Compressonator\\temp_mip.dds ";
            std::string comprs = resource + "Compressonator\\CompressonatorCLI  ";
            std::string pathOnly = ddsFilename.substr(0, ddsFilename.find_last_of("\\/") + 1);

            std::string cmdExp = comprs + " -miplevels 6  \"" + _path.string() + "\"  " + temp;
            replaceAllVEG(cmdExp, "/", "\\");
            spdlog::info("vegetation: {}", cmdExp);
            system(cmdExp.c_str());
            if (isSRGB)
            {
                std::string cmdExp2 = comprs + " -fd BC7 -Quality 0.01 " + temp + ddsFilename;
                replaceAllVEG(cmdExp2, "/", "\\");
                spdlog::info("vegetation: {}", cmdExp2);
                system(cmdExp2.c_str());
            }
            else
            {
                std::string cmdExp2 = comprs + " -fd BC6H " + temp + ddsFilename;
                replaceAllVEG(cmdExp2, "/", "\\");
                spdlog::info("vegetation: {}", cmdExp2);
                system(cmdExp2.c_str());

            }
        }
    }
    ew::Texture::SharedPtr tex = ew::Texture::createFromFile(ddsFilename, true, isSRGB);
    //Texture::SharedPtr tex = Texture::createFromFile(_path.string(), true, isSRGB);
    if (tex)
    {
        textureVector.emplace_back(tex);
        texturePathVector.emplace_back(_path);

        float compression = 4.0f;
        if (isSRGB) compression = 4.0f;

        texMb += (float)(tex->getWidth() * tex->getHeight() * 4.0f * 1.333f) / 1024.0f / 1024.0f / compression;	// for 4:1 compression + MIPS

        spdlog::info("vegetation: {}", _path.filename().string());

        return (uint)(textureVector.size() - 1);
    }
    else
    {
        spdlog::error("vegetation: failed {}", _path.string());
        return -1;
    }


}


ew::Texture::SharedPtr materialCache_plants::getDisplayTexture()
{
    if (dispTexIndex >= 0) {
        return textureVector.at(dispTexIndex);
    }
    return nullptr;
}



void materialCache_plants::setTextures(ew::pixelShader& _shader)
{
    // Binds the flattened textures_T[4096]; slots past textureVector's end get
    // the layer's dummy texture.
    _shader.setTextureArray("textures_T", textureVector);

    modified = false;
}


// FIXME could also just do the individual one
//terrafectorEditorMaterial::static_materials.sb_Terrafector_Materials = Buffer::createStructured(sizeof(TF_material), 2048); // FIXME hardcoded
void materialCache_plants::rebuildStructuredBuffer()
{
    size_t offset = 0;
    for (auto& mat : materialVector)
    {
        sb_vegetation_Materials->setBlob(&mat._constData, offset, sizeof(sprite_material));
        offset += sizeof(sprite_material);
    }
}




void _plantMaterial::makeRelative(std::filesystem::path _path)
{
    relativePath = materialCache::getRelative(_path.string());
}

void _plantMaterial::import(std::filesystem::path _path, bool _replacePath)
{
    std::ifstream is(_path);
    if (is.fail()) {
        displayName = "failed to load";
        fullPath = _path;
    }
    else
    {
        try
        {
            cereal::JSONInputArchive archive(is);
            //serialize(archive, 100);
            archive(*this);
        }
        catch (const std::exception& e)
        {
            spdlog::error("vegetation: _plantMaterial::import failed to parse '{}' - {}", _path.string(), e.what());
            displayName = "failed to load";
            fullPath = _path;
            return;
        }

        if (_replacePath) fullPath = _path;
        displayName = _path.stem().string();
        reloadTextures();
        isModified = false;
    }
}
void _plantMaterial::save()
{
    std::ofstream os(fullPath);
    cereal::JSONOutputArchive archive(os);
    //serialize(archive, 100);
    archive(*this);
}
void _plantMaterial::eXport(std::filesystem::path _path)
{
    std::ofstream os(_path);
    cereal::JSONOutputArchive archive(os);
    //serialize(archive, 100);
    archive(*this);
    isModified = false;
}
void _plantMaterial::reloadTextures()
{
    bool FORCE_RELOAD = true;
    _constData.albedoTexture = _plantMaterial::static_materials_veg.find_insert_texture(terrafectorEditorMaterial::rootFolder + albedoPath, true, FORCE_RELOAD);
    _constData.alphaTexture = _plantMaterial::static_materials_veg.find_insert_texture(terrafectorEditorMaterial::rootFolder + alphaPath, true, FORCE_RELOAD);
    _constData.normalTexture = _plantMaterial::static_materials_veg.find_insert_texture(terrafectorEditorMaterial::rootFolder + normalPath, false, FORCE_RELOAD);
    _constData.translucencyTexture = _plantMaterial::static_materials_veg.find_insert_texture(terrafectorEditorMaterial::rootFolder + translucencyPath, false, FORCE_RELOAD);
}




void _vegMaterial::reload()
{
    if (std::filesystem::exists(terrafectorEditorMaterial::rootFolder + path))
    {
        // force a reload
        bool FORCE_RELOAD = true;
        index = _plantMaterial::static_materials_veg.find_insert_material(terrafectorEditorMaterial::rootFolder + path, FORCE_RELOAD);
    }
    else
    {
    }
}





void extentsCalculator::start(glm::mat4 _view, float3 _end)
{
    origin = _view[3];
    float3 v1 = glm::normalize(_end - origin);
    _view[1] = float4(v1, _view[1].w);
    float3 v2 = glm::normalize(glm::cross((float3)_view[0], (float3)_view[1]));
    _view[2] = float4(v2, _view[2].w);
    float3 v0 = glm::normalize(glm::cross((float3)_view[1], (float3)_view[2]));
    _view[0] = float4(v0, _view[0].w);
    view = _view;
    extents = { 0, 0 };
    count = 0;
    du4 = { 0, 0, 0, 0 };
    du6 = { 0, 0, 0, 0, 0, 0 };

    data.clear();
    //data.reserve(8192); I am scaredthis makes itsmaller - just let it grow big
}


void extentsCalculator::push(float3 _pos, float _w)
{
    float2 proj;
    proj.x = fabs(glm::dot(_pos - origin, (float3)view[0])) + _w;
    proj.y = __max(0.f, glm::dot(_pos - origin, (float3)view[1])) + _w;               //  y is positive only

    data.push_back(proj);
    extents = glm::max(extents, proj);
}


void extentsCalculator::end()
{
    tip = view;
    tip[3] += view[1] * extents.y;

    //??? mau=ybe dor more buckets and combine properly
    for (auto& P : data)
    {
        uint idx6 = (int)floor(P.y / extents.y * 5.99f);
        du6[idx6] = __max(du6[idx6], P.x);
    }

    du4[0] = du6[0];
    du4[1] = __max(du6[1], du6[2]);
    du4[2] = __max(du6[3], du6[4]);
    du4[3] = du6[5];
}

glm::mat4 extentsCalculator::lerp(float _h)
{
    glm::mat4 L = view;
    L[3] = glm::lerp(view[3], tip[3], _h);
    return L;
}




void _plantRND::reload()
{
    if (std::filesystem::exists(terrafectorEditorMaterial::rootFolder + path))
    {
        switch (type)
        {
        case P_LEAF:
            plantPtr.reset(new _leafBuilder);
            spdlog::info("vegetation: _plantRND   _leafBuilder  {}", path);
            break;
        case P_STEM:
            plantPtr.reset(new _stemBuilder);
            spdlog::info("vegetation: _plantRND   _stemBuilder  {}", path);
            break;
        case P_CLUMP:
            plantPtr.reset(new _clumpBuilder);
            spdlog::info("vegetation: _plantRND   _clumpBuilder  {}", path);
            break;
        case P_FLOWER:
            plantPtr.reset(new _flowerBuilder);
            spdlog::info("vegetation: _plantRND   _flowerBuilder  {}", path);
            break;
        default:  plantPtr.reset();  break;
        }

        if (plantPtr)
        {
            plantPtr->path = path;
            plantPtr->name = name;
            plantPtr->loadPath();
        }
    }
    else
    {
        plantPtr.reset();   // should really prompt and load
        //loadFromFile();
    }
}







template <class T>     T randomVector<T>::get()
{
    rnd_idx = _rootPlant::rand_int(_rootPlant::generator);
    rnd_idx %= (int)data.size();
    return data[rnd_idx];
}

// get() is defined in this TU, so every specialization other TUs use has to be
// instantiated explicitly here.
template _vegMaterial randomVector<_vegMaterial>::get();
template _plantRND randomVector<_plantRND>::get();



bool anyChange = true;  // damn, should be deprecated

// New random branches with age factor




void _randomBranch::reload()
{
    if (std::filesystem::exists(terrafectorEditorMaterial::rootFolder + path))
    {
        switch (type)
        {
        case P_LEAF:   plantPtr.reset(new _leafBuilder);   break;
        case P_STEM:   plantPtr.reset(new _stemBuilder);   break;
        case P_CLUMP:   plantPtr.reset(new _clumpBuilder);   break;
        case P_FLOWER:   plantPtr.reset(new _flowerBuilder);   break;
        default:  plantPtr.reset();  break;
        }

        spdlog::info("vegetation: _randomBranch::reload  {}", path);

        if (plantPtr)
        {
            plantPtr->path = path;
            plantPtr->name = name;
            plantPtr->loadPath();
        }
    }
    else
    {
        plantPtr.reset();   // should really prompt and load
        //loadFromFile();
    }
}




void semiRandomBranch::buildArray()
{
    _rootPlant::generator.seed(1000);
    std::uniform_real_distribution<> d_30(0.75f, 1.333333f);

    for (int i = 0; i < 1024; i++)
    {
        RND[i] = -1;

        if (branchData.size() > 0)
        {
            float _t = (float)i / 1024.f;
            int idx = 0;
            float percentage = 0.f;
            for (int j = 0; j < (int)branchData.size(); j++)
            {
                float offset = fabs(branchData[j].params.y - _t) / branchData[j].params.z;
                float val = branchData[j].params.x * glm::smoothstep(1.f, 0.f, offset) * (float)d_30(_rootPlant::generator);
                if (val > percentage)
                {
                    percentage = val;
                    idx = j;
                }
            }
            RND[i] = (short)idx;
        }
    }

}

_randomBranch* semiRandomBranch::get(float _val)
{
    short idx = RND[(uint)(glm::fract(_val) * 1024.f)];
    if (idx >= 0) return &branchData[idx];
    else return nullptr;
}









// _leafBuilder
// -----------------------------------------------------------------------------------------------------------------------------------
void _leafBuilder::loadPath()
{
    if (std::filesystem::exists(terrafectorEditorMaterial::rootFolder + path))
    {
        try
        {
            std::ifstream is(terrafectorEditorMaterial::rootFolder + path);
            cereal::JSONInputArchive archive(is);
            archive(*this);
            changed = false;
            fileNotFound = false;
        }
        catch (const std::exception& e)
        {
            spdlog::error("vegetation: _leafBuilder::loadPath failed to parse '{}' - {}", path, e.what());
        }
    }
    else
    {
        spdlog::error("vegetation: File does not exists in the relative tree structure - {}", path);
    }
}

void _leafBuilder::savePath()
{
    std::ofstream os(terrafectorEditorMaterial::rootFolder + path);
    cereal::JSONOutputArchive archive(os);
    archive(*this);
    changed = false;
}






std::uniform_real_distribution<> d_1_1(-1.f, 1.f);
#define RND_ALBEDO(data) (data * (1.f + 0.3f * (float)d_1_1(_rootPlant::generator)))
#define RND_B(data) (data.x * (1.f + data.y * (float)d_1_1(_rootPlant::generator)))
#define RND_CRV(data) (data.x + (data.y * (float)d_1_1(_rootPlant::generator)))

#define GROW(_mat,_length)   _mat[3] += _mat[1] * _length
#define ROLL(_mat,_ang)  _mat = glm::rotate(_mat, _ang, glm::vec3(0, 1, 0))
#define PITCH(_mat,_ang)  _mat = glm::rotate(_mat, _ang, glm::vec3(1, 0, 0))
#define YAW(_mat,_ang)  _mat = glm::rotate(_mat, _ang, glm::vec3(0, 0, 1))

#define ROLL_HORIZONTAL(_mat)   while (fabs(_mat[0][1]) > 0.04f || (_mat[2][1] >= 0))   {    ROLL(_mat, 0.03f);     }
#define ROLL_HORIZONTAL_B(_mat)   while (fabs(_mat[2][1]) > 0.04f || (_mat[0][1] >= 0))   {    ROLL(_mat, 0.03f);     }

void _leafBuilder::clear_build_info()
{
    numInstancePacked = 0;
    numVertsPacked = 0;
    debugTotalVertsPacked = 0;
    debugnumPivots = 0;
    debugLOD = 0;
    debugmaxLOD = 0;
    debugSIZE = 0.f;
    debugBAKETYPE = 0;
    debugLastPivots[0] = 255;
    debugLastPivots[1] = 255;
    debugLastPivots[2] = 255;
    debugLastPivots[3] = 255;
}


glm::mat4 _leafBuilder::build(buildSetting _settings, bool _addVerts, bool _extents)
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
    //float age = pow(_settings.normalized_age, 1.f);
    float age = _settings.normalized_age;
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
            //fprintf(terrafectorSystem::_logfile, "  leaf-stem : mat %d  -  % \n", stem_Material.index, stem_Material.name.c_str());
        }

        for (int i = 0; i < 100; i++)
        {
            PITCH(node, curve);
            GROW(node, length);

            cnt++;
            if (_addVerts && stemVisible && cnt >= step)
            {
                _ribbonBuilder.set(node, width * 0.5f, stem_Material.index, float2(1.f, (float)i / 99.f), 1.f, 1.f, !(pivotType == pivot_leaf), stiffness, freq);
                cnt -= step;
            }
        }
        // FXIME add some stem that pushes into the leaf here

        GROW(node, -width * 0.5f);  // Now move ever so slghtly backwards for better penetration of stem to leaf
    }


    ROLL(node, RND_CRV(stem_to_leaf_Roll));                         // rotation from stem to leaf
    PITCH(node, RND_CRV(stem_to_leaf));


    // build the leaf
    {
        _vegMaterial mat = materials.get();
        //fprintf(terrafectorSystem::_logfile, "  leaf : mat %d  -  % \n", mat.index, mat.name.c_str());
        float albedoScale = RND_ALBEDO(glm::lerp(mat.albedoScale.y, mat.albedoScale.x, age));
        float translucentScale = glm::lerp(mat.translucencyScale.y, mat.translucencyScale.x, age);

        float length = RND_B(leaf_length) / 100.f * 0.001f * age;   // to meters and numSegments  // FIXME scale to neter built into macro, rename macro for distamce only
        float width = RND_B(leaf_width) * 0.001f * age;
        float gravi = RND_CRV(gravitrophy) / 100.f * age;
        (void)gravi;    // unused - gravitrophy.x gets applied further down instead
        float curve = RND_CRV(leaf_curve) / 100.f * age;
        float twist = RND_CRV(leaf_twist) / 100.f * age;

        // Lodding leaf............................................................
        //bool showLeaf = (width * d50(_rootPlant::generator)) > _settings.pixelSize;
        bool showLeaf = width > _settings.pixelSize;
        if (showLeaf || _extents)
        {
            if (_addVerts && pointSprite)
            {
                GROW(node, width * 0.5f);
                _ribbonBuilder.startRibbon(cameraFacing, _settings.pivotIndex);
                _ribbonBuilder.set(node, width * 0.5f, mat.index, float2(0.5f, 0.5f), albedoScale, translucentScale, !(pivotType == pivot_leaf), stiffness, freq, ossilation_power, false, true);
            }
            else
            {
                int numLeaf = glm::clamp((int)((length / _settings.pixelSize) / leafLengthSplit * 100.f), numVerts.x - 1, numVerts.y - 1);
                float step = 99.f / numLeaf;
                float cnt = 0;
                bool useDiamond = (useTwoVertDiamond && (numLeaf == 1));

                bool CylinderExclusion = false;
                bool firstNode = true;
                for (int i = 0; i < 100; i++)
                {
                    float t = (float)i / 99.f;
                    if (wideBase)
                    {
                        // start t later
                        t = glm::clamp(t, 0.33f, 1.f);
                    }
                    float du = __min(1.f, sin(pow(t, width_offset.x) * 3.1415f) + width_offset.y);
                    if (numLeaf == 1) du = 1.f;

                    float perlinScale = glm::smoothstep(0.f, 0.3f, t) * age;
                    float noise = (float)perlin.normalizedOctave1D(perlinCurve.y * t, 4);
                    PITCH(node, noise * perlinCurve.x * perlinScale);

                    noise = (float)perlinTWST.normalizedOctave1D(perlinTwist.y * t, 4) * age;
                    ROLL(node, noise * perlinTwist.x * perlinScale);

                    ROLL(node, twist);
                    PITCH(node, curve);

                    //if (gravitrophy > 0.05f)
                    {
                        float3 axis = glm::cross(float3(0, 1, 0), (glm::vec3)node[1]);
                        float3 XX = float3(0, 0, 0);
                        XX.x = glm::dot(axis, (glm::vec3)node[0]);
                        XX.z = glm::dot(axis, (glm::vec3)node[2]);
                        node = glm::rotate(node, -gravitrophy.x / 100.f, glm::normalize(XX));
                    }

                    GROW(node, length);

                    CylinderExclusion |= true;// _settings.testExclusion(node[3]);
                    //if (node[3].y > _settings.exclusionCylinder.y) CylinderExclusion = true;    // FIXME proper test, do we have xis as well
                    //CylinderExclusion = true;
                    if (_addVerts && CylinderExclusion)
                    {
                        if (firstNode)
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
                            firstNode = false;
                        }
                        else
                        {
                            cnt++;
                            if (cnt >= step)
                            {
                                _ribbonBuilder.set(node, width * 0.5f * du, mat.index, float2(du, 1.f - t), albedoScale, translucentScale, !(pivotType == pivot_leaf), stiffness, freq, pow((float)i / 99.f, ossilation_power), useDiamond);
                                cnt -= step;
                            }
                        }
                    }
                }
            }
        }
    }

    if (_extents) EXTENTS.push(node[3], 0.003f);    // just push thje tips

    uint numVerts = _ribbonBuilder.numVerts() - startVerts;
    if (numVerts > 0) numInstancePacked++;
    numVertsPacked += numVerts;
    debugSIZE = _settings.pixelSize;
    changedForSave |= changed;
    return node;
}










// _flowerBuilder
// -----------------------------------------------------------------------------------------------------------------------------------
void _flowerBuilder::loadPath()
{
    if (std::filesystem::exists(terrafectorEditorMaterial::rootFolder + path))
    {
        try
        {
            std::ifstream is(terrafectorEditorMaterial::rootFolder + path);
            cereal::JSONInputArchive archive(is);
            archive(*this);
            changed = false;
            fileNotFound = false;
        }
        catch (const std::exception& e)
        {
            spdlog::error("vegetation: _flowerBuilder::loadPath failed to parse '{}' - {}", path, e.what());
        }
    }
    else
    {
        spdlog::error("vegetation: File does not exists in the relative tree structure - {}", path);
    }
}

void _flowerBuilder::savePath()
{
    std::ofstream os(terrafectorEditorMaterial::rootFolder + path);
    cereal::JSONOutputArchive archive(os);
    archive(*this);
    changed = false;
}









void _flowerBuilder::clear_build_info()
{
    numInstancePacked = 0;
    numVertsPacked = 0;
    debugTotalVertsPacked = 0;
    debugnumPivots = 0;
    debugLOD = 0;
    debugmaxLOD = 0;
    debugSIZE = 0.f;
    debugBAKETYPE = 0;

    for (auto& R : rings)
    {
        _plantRND petal = R.petals.get();
        if (petal.plantPtr)  petal.plantPtr->clear_build_info();
    }

}


lodBake* _flowerBuilder::getBakeInfo(int i)
{
    if (i < (int)lod_bakeInfo.size()) return &lod_bakeInfo[i];
    else return nullptr;
}


levelOfDetail* _flowerBuilder::getLodInfo(uint i)
{
    if (i < lodInfo.size()) return &lodInfo[i];
    else return nullptr;
}

glm::mat4  _flowerBuilder::build_2(buildSetting _settings, uint _bakeIndex, bool _faceCamera, bool _diamond)
{
    (void)_faceCamera;
    lodBake& lB = lod_bakeInfo[_bakeIndex];
    glm::mat4 node = _settings.root;
    GROW(node, 0.01f);  // should come from main ring, can I calculkate


    if (lB.pixHeight > 0)
    {
        float w = lB.extents.x * lB.bakeWidth;
        uint mat = lB.material.index;

        //fprintf(terrafectorSystem::_logfile, "  _stemBuilder::build_2 : mat %d  -  % \n", lB.material.index, lB.material.name.c_str());
        _ribbonBuilder.startRibbon(lB.faceCamera, _settings.pivotIndex);
        PITCH(node, 1.57f);
        GROW(node, -w);
        _ribbonBuilder.set(node, w, mat, float2(1.f, 0.f), 1.f, 1.f, true, 0.5f, 0.1f, 0.0f, _diamond);
        GROW(node, w * 2.f);
        _ribbonBuilder.set(node, w, mat, float2(1.f, 1.f), 1.f, 1.f, true, 0.5f, 0.1f, 0.0f, _diamond);
    }

    return node;
}

glm::mat4 _flowerBuilder::build(buildSetting _settings, bool _addVerts, bool _extents)
{

    uint startVerts = _ribbonBuilder.numVerts();

    std::uniform_real_distribution<> d50(0.5f, 1.5f);
    std::uniform_int_distribution<> distAlbedo(-50, 50);
    std::uniform_int_distribution<> distPerlin(1, 50000);

    const siv::PerlinNoise::seed_type seed = distPerlin(_rootPlant::generator);
    const siv::PerlinNoise perlin{ seed };
    const siv::PerlinNoise::seed_type seedT = distPerlin(_rootPlant::generator);
    const siv::PerlinNoise perlinTWST{ seedT };
    // The two noise objects are never sampled, but the distPerlin() draws that
    // seeded them advance the shared generator - remove them and every plant
    // built afterwards comes out different.
    (void)perlin; (void)perlinTWST;

    glm::mat4 node = _settings.root;
    //float age = pow(_settings.normalized_age, 1.f);
    float age = _settings.normalized_age;
    bool stemVisible = false;

    float freq = rootFrequency();
    float stiffness = 1.f / ossilation_stiffness;

    if (_addVerts)
    {
        bool found = false;
        levelOfDetail lod = lodInfo.front();             // no parent can push us past this, similar when I do sun Parts, there will be a lod set for that add
        for (int i = 0; i < (int)lodInfo.size(); i++)
        {
            // FIXME we want random pixelSize here for smoother crossovers but
            // But this is VERY bad, it ashould take overall lengrth into account
            GLOBAL_RND_SIZE = 0;
            if (_settings.callDepth > 1) GLOBAL_RND_SIZE = 0.2f;
            if (_settings.pixelSize * RND_B(float2(1.f, GLOBAL_RND_SIZE)) <= lodInfo[i].pixelSize)
            {
                lod = lodInfo[i];
                found = true;
                debugLOD = i;
                debugmaxLOD = (uint)lodInfo.size() - 1;
                debugBAKETYPE = lod.bakeType;
            }
        }

        if (found)
        {

            // if (!_settings.isBaking)    // so do not use any of these during a bake
            {
                if (lod.bakeType == BAKE_DIAMOND)       build_2(_settings, lod.bakeIndex, true, true);
                if (lod.bakeType == BAKE_QUAD)       build_2(_settings, lod.bakeIndex, true, false);
                //if (lod.bakeType == BAKE_4)             build_4(_settings, lod.bakeIndex, true);
                //if (lod.bakeType == BAKE_N)             build_n(_settings, lod.bakeIndex, true);    // FIXME
            }

            if (lod.useGeometry || _settings.isBaking)
            {

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
                        _ribbonBuilder.set(node, width * 0.5f, stem_Material.index, float2(1.f, 0.f), 1.f, 1.f, !(pivot_leaf), stiffness, freq);
                        stemVisible = true;
                        //fprintf(terrafectorSystem::_logfile, "  leaf-stem : mat %d  -  % \n", stem_Material.index, stem_Material.name.c_str());
                    }

                    for (int i = 0; i < 100; i++)
                    {
                        PITCH(node, curve);
                        GROW(node, length);

                        cnt++;
                        if (stemVisible && cnt >= step)
                        {
                            _ribbonBuilder.set(node, width * 0.5f, stem_Material.index, float2(1.f, (float)i / 99.f), 1.f, 1.f, !(pivot_leaf), stiffness, freq);
                            cnt -= step;
                        }
                    }
                    // FXIME add some stem that pushes into the leaf here

                    GROW(node, -width * 1.5f);  // Now move ever so slghtly backwards for better penetration of stem to leaf
                }


                ROLL(node, RND_CRV(stem_to_leaf_Roll));                         // rotation from stem to leaf
                PITCH(node, RND_CRV(stem_to_leaf));

                glm::mat4 root_node = node;

                // build the flower rings
                // FIXME MOVE to rings,and split in two
                std::mt19937 MT(_settings.seed * 35696 + 193489);
                std::vector<float3> points;
                for (int i = 0; i < (int)rings.size(); i++)
                {
                    if (rings[i].spherical)
                    {
                        for (int pt = 0; pt < rings[i].numPetals.x; pt++)
                        {
                            float3 rndPos;
                            float l;
                            int cnt = 0;
                            do
                            {
                                cnt++;
                                rndPos.x = (float)d_1_1(MT);
                                rndPos.y = (float)d_1_1(MT);
                                rndPos.z = (float)d_1_1(MT);
                                l = glm::length(rndPos);
                                //for (auto& P : points)
                                //{
                                //    if (glm::length(rndPos * rings[i].sphere_size - P) < 0.01f) l = 1000;   // reject if too closebut needs tocome from child
                                //}
                            } while (l >= 1.f || l < 0.75f);

                            rndPos.y += 0.5f;   // ependsa if we start full spehere
                            float3 rndAxis = (rndPos.x * (float3)root_node[0] * rings[i].sphere_size.x) + (rndPos.y * (float3)root_node[1] * rings[i].sphere_size.y) + (rndPos.z * (float3)root_node[2] * rings[i].sphere_size.z);
                            //rndPos *= rings[i].sphere_size;
                            points.push_back(rndAxis);
                            node = root_node;
                            node[3] += float4(rndAxis, 0.f);

                            if (rings[i].petals.data.size() > 0)
                            {
                                _plantRND petal = rings[i].petals.get();
                                _settings.root = node;
                                if (petal.plantPtr) petal.plantPtr->build(_settings, _addVerts, _extents);
                            }
                        }
                    }
                    else
                    {
                        float radius = RND_B(rings[i].radius_mm) * 0.001f;

                        float RNG = RND_B(rings[i].range);
                        float A = 0.f;
                        if (rings[i].numPetals.x > 1)
                        {
                            if (rings[i].symmetrical)
                            {
                                A = RNG * 2.f / (rings[i].numPetals.x - 1);
                            }
                            else
                            {
                                A = RNG * 2.f / (rings[i].numPetals.x);
                            }
                        }
                        for (int j = 0; j < rings[i].numPetals.x; j++)
                        {
                            node = root_node;
                            GROW(node, rings[i].offset_mm.x * 0.001f);
                            ROLL(node, -RNG + A * j);
                            //PITCH(node, 1.5f);
                            PITCH(node, -1.57f);
                            GROW(node, radius);
                            PITCH(node, rings[i].pitch.x);

                            _plantRND petal = rings[i].petals.get();
                            _settings.root = node;
                            if (petal.plantPtr) petal.plantPtr->build(_settings, _addVerts, _extents);
                        }
                    }
                }


                // build the core - for now just a flat plane with texture
                if (center_Material.index >= 0)
                {
                    _ribbonBuilder.startRibbon(false, _settings.pivotIndex);

                    float albedoScale = RND_ALBEDO(glm::lerp(center_Material.albedoScale.y, center_Material.albedoScale.x, age));
                    float translucentScale = glm::lerp(center_Material.translucencyScale.y, center_Material.translucencyScale.x, age);

                    float width = center_size.x * 0.001f;
                    node = root_node;
                    GROW(node, center_offset.x * 0.001f);
                    PITCH(node, 1.57079632679f);
                    GROW(node, -width);
                    _ribbonBuilder.set(node, width, center_Material.index, float2(1, 0), albedoScale, translucentScale, true, stiffness, freq, 0, false);

                    GROW(node, width * 2.f);
                    _ribbonBuilder.set(node, width, center_Material.index, float2(1, 1.f), albedoScale, translucentScale, true, stiffness, freq, 0, false);
                    //center_Material
                }
            }
        }
    }

    uint numVerts = _ribbonBuilder.numVerts() - startVerts;
    if (numVerts > 0) numInstancePacked++;
    numVertsPacked += numVerts;
    debugTotalVertsPacked += numVerts;
    debugSIZE = _settings.pixelSize;
    changedForSave |= changed;

    return node;
}



float2 _flowerBuilder::calculate_extents(glm::mat4 view)
{
    float2 extents = _ribbonBuilder.calculate_extents(view);

    std::filesystem::path full_path = path;

    // lod 0
    float4 dd0 = glm::saturate(_ribbonBuilder.dU / lod_bakeInfo[0].bakeWidth);
    lod_bakeInfo[0].extents = extents;
    lod_bakeInfo[0].dU[0] = dd0[0];
    lod_bakeInfo[0].dU[1] = dd0[1];
    lod_bakeInfo[0].dU[2] = dd0[2];
    lod_bakeInfo[0].dU[3] = dd0[3];
    lod_bakeInfo[0].material.name = "bake_0_100";
    lod_bakeInfo[0].material.path = full_path.parent_path().string() + "\\bake_" + full_path.stem().string() + "\\" + lod_bakeInfo[0].material.name + ".vegetationMaterial";;

    // lod 1
    dd0 = glm::saturate(_ribbonBuilder.dU / lod_bakeInfo[1].bakeWidth);
    lod_bakeInfo[1].extents = extents;
    lod_bakeInfo[1].dU[0] = dd0[0];
    lod_bakeInfo[1].dU[1] = dd0[1];
    lod_bakeInfo[1].dU[2] = dd0[2];
    lod_bakeInfo[1].dU[3] = dd0[3];
    lod_bakeInfo[1].material.name = "bake_1";
    lod_bakeInfo[1].material.path = full_path.parent_path().string() + "\\bake_" + full_path.stem().string() + "\\" + lod_bakeInfo[1].material.name + ".vegetationMaterial";;

    // lod 2
    dd0 = glm::saturate(_ribbonBuilder.dU / lod_bakeInfo[2].bakeWidth);
    lod_bakeInfo[2].extents = extents;
    lod_bakeInfo[2].dU[0] = dd0[0];
    lod_bakeInfo[2].dU[1] = dd0[1];
    lod_bakeInfo[2].dU[2] = dd0[2];
    lod_bakeInfo[2].dU[3] = dd0[3];
    lod_bakeInfo[2].material.name = "bake_2";
    lod_bakeInfo[2].material.path = full_path.parent_path().string() + "\\bake_" + full_path.stem().string() + "\\" + lod_bakeInfo[2].material.name + ".vegetationMaterial";;

    return extents;
}

glm::mat4 _flowerBuilder::getTip(bool includeChildren)
{
    (void)includeChildren;
    //return NODES.back();    // since its only direction test with this
    //-- need t make this so it returns soemthign proepr
    glm::mat4 tip(1.f);
    PITCH(tip, 1.57f);
    GROW(tip, 1.f);
    return tip;
}






// _stemBuilder
// -----------------------------------------------------------------------------------------------------------------------------------

void _stemBuilder::loadPath()
{
    if (std::filesystem::exists(terrafectorEditorMaterial::rootFolder + path))
    {
        try
        {
            std::ifstream is(terrafectorEditorMaterial::rootFolder + path);
            cereal::JSONInputArchive archive(is);
            archive(*this);
            changed = false;
            fileNotFound = false;
        }
        catch (const std::exception& e)
        {
            spdlog::error("vegetation: _stemBuilder::loadPath failed to parse '{}' - {}", path, e.what());
        }
    }
    else
    {
        spdlog::error("vegetation: File does not exists in the relative tree structure - {}", path);
    }
}

void _stemBuilder::savePath()
{
    std::ofstream os(terrafectorEditorMaterial::rootFolder + path);
    cereal::JSONOutputArchive archive(os);
    archive(*this);
    changed = false;
}









lodBake* _stemBuilder::getBakeInfo(int i)
{
    if (i < (int)lod_bakeInfo.size()) return &lod_bakeInfo[i];
    else return nullptr;
}

levelOfDetail* _stemBuilder::getLodInfo(uint i)
{
    if (i < lodInfo.size()) return &lodInfo[i];
    else return nullptr;
}

levelOfDetail* _stemBuilder::getLOD(float _pixelSize, bool _rnd)
{
    int lodIndex = 0;
    float random = RND_B(float2(1.f, _rnd ? 0.2f : 0.f));

    for (int i = 1; i < (int)lodInfo.size(); i++)
    {
        if (_pixelSize * random <= (lodInfo[i].pixelSize * 0.001f))          lodIndex = i;
    }

    debugLOD = lodIndex;
    debugmaxLOD = (uint)lodInfo.size() - 1;
    debugBAKETYPE = lodInfo[lodIndex].bakeType;

    return &lodInfo[lodIndex];
}


/*  Diamond overshoots 10 % inside teh shader so shrink by 10 %
*/
glm::mat4  _stemBuilder::build_2(buildSetting _settings, lodBake* pBake, bool _faceCamera, bool _diamond)
{
    (void)_faceCamera;
    if (pBake->pixHeight == 0) return NODES.front();
    float w = EXTENTS.width() * pBake->bakeWidth;

    if (w > _settings.pixelSize)
    {
        _ribbonBuilder.startRibbon(pBake->faceCamera, _settings.pivotIndex);
        _ribbonBuilder.set(EXTENTS.lerp(pBake->bake_V.x), w, pBake->material.index, float2(1.f, 1.f), 1.f, 1.f, true, 0.5f, 0.1f, 0.0f, _diamond);
        _ribbonBuilder.set(EXTENTS.lerp(pBake->bake_V.y), w, pBake->material.index, float2(1.f, 0.f), 1.f, 1.f, true, 0.5f, 0.1f, 0.0f, _diamond);
    }

    return EXTENTS.lerp(pBake->bake_V.y);
}


glm::mat4  _stemBuilder::build_4(buildSetting _settings, lodBake* pBake, bool _faceCamera)
{
    (void)_faceCamera;
    uint mat = pBake->material.index;
    float w = EXTENTS.width() * pBake->bakeWidth;

    glm::mat4 node = EXTENTS.lerp(pBake->bake_V.x);
    glm::mat4 last = EXTENTS.lerp(pBake->bake_V.y);
    float4 step = (last[3] - node[3]) / 3.f;

    if (w > _settings.pixelSize)
    {

        float4 binorm_step = (last[1] - node[1]) / 3.f;

        _ribbonBuilder.startRibbon(pBake->faceCamera, _settings.pivotIndex);

        for (int i = 0; i < 4; i++)
        {
            glm::mat4 CURRENT = node;

            if (NODES.size() > 3)
            {
                for (int iN = 0; iN < (int)NODES.size() - 1; iN++)
                {
                    float3 rel = NODES[iN][3] - node[3];
                    float3 rel2 = NODES[iN + 1][3] - node[3];
                    float d = glm::dot(rel, (float3)node[1]);
                    float d2 = glm::dot(rel2, (float3)node[1]);

                    if (d <= 0 && d2 > 0)
                    {
                        float total = (d2 - d) + 0.0000001f;   // add a samll bit for when we have no tip and this becomes zero
                        CURRENT[3] = glm::lerp(NODES[iN][3], NODES[iN + 1][3], -d / total);
                        break;
                    }
                    // seach last to tip, we didnt find it
                    {
                        float dL = glm::dot((float3)(NODES.back()[3] - node[3]), (float3)node[1]);
                        float dL2 = glm::dot((float3)(tip_NODE[3] - node[3]), (float3)node[1]);
                        float total = (dL2 - dL) + 0.0000001f;
                        CURRENT[3] = glm::lerp(NODES.back()[3], tip_NODE[3], -dL / total);
                    }
                }
            }

            if (i == 0) CURRENT = node;
            if (i == 3) CURRENT = last;

            float wI = EXTENTS.du4[i] * pBake->bakeWidth;
            float dU = EXTENTS.du4[i] / EXTENTS.width();
            _ribbonBuilder.set(CURRENT, wI, mat, float2(dU, 1.f - (0.3333333f * i)), 1.f, 1.f);
            node[3] += step;
            node[1] += binorm_step;
        }
    }

    return last;
}

// really build7
glm::mat4  _stemBuilder::build_n(buildSetting _settings, lodBake* pBake, bool _faceCamera)
{
    (void)_settings; (void)_faceCamera;
    float w = EXTENTS.width() * pBake->bakeWidth;
    uint mat = pBake->material.index;

    glm::mat4 node = NODES.front();
    glm::mat4 last = NODES.back();
    float tipLength = glm::dot((float3)(tip_NODE[3] - last[3]), (float3)node[1]);
    GROW(last, tipLength);

    glm::vec4 step = (last[3] - node[3]);
    float4 binorm_step = (last[1] - node[1]);

    node[3] += step * pBake->bake_V.x;
    step *= (pBake->bake_V.y - pBake->bake_V.x) / 6.f;
    binorm_step *= (pBake->bake_V.y - pBake->bake_V.x) / 6.f;

    //fprintf(terrafectorSystem::_logfile, "  _stemBuilder::build_4 : mat %d  -  % \n", lB.material.index, lB.material.name.c_str());
    _ribbonBuilder.startRibbon(pBake->faceCamera, _settings.pivotIndex);

    for (int i = 0; i < 7; i++)
    {
        glm::mat4 CURRENT = node;

        if (NODES.size() > 3)
        {
            for (int iN = 0; iN < (int)NODES.size() - 1; iN++)
            {
                float3 rel = NODES[iN][3] - node[3];
                float3 rel2 = NODES[iN + 1][3] - node[3];
                float d = glm::dot(rel, (float3)node[1]);
                float d2 = glm::dot(rel2, (float3)node[1]);

                if (d <= 0 && d2 > 0)
                {
                    float total = (d2 - d) + 0.0000001f;   // add a samll bit for when we have no tip and this becomes zero
                    CURRENT[3] = glm::lerp(NODES[iN][3], NODES[iN + 1][3], -d / total);
                    break;
                }
            }
        }

        int duIdx = i / 2;
        float ddu = float(i % 2) / 2.f;
        float dU = glm::lerp(pBake->dU[duIdx], pBake->dU[duIdx + 1], ddu);
        _ribbonBuilder.set(CURRENT, w * dU, mat, float2(dU, 1.f - (0.16666666667f * i)), 1.f, 1.f);
        node[3] += step;
        node[1] += binorm_step;
    }

    return last;
}


void _stemBuilder::build_tip(buildSetting _settings, bool _addVerts, bool _extents)
{
    tip.reset();
    _settings.isBaking = false;
    _settings.seed = _settings.seed + 99;
    _rootPlant::generator.seed(_settings.seed);

    {
        // walk the tip back ever so slightly
        glm::mat4 node = NODES.back();
        GROW(node, -tip_width * 0.5f);  //???

        if (roll_horizontal)
        {
            ROLL_HORIZONTAL_B(node);
            ROLL(node, -1.570796326f);
        }

        _settings.root = node;
        _settings.node_age = 1.f;
        _settings.normalized_age = 1;

        if (unique_tip && _settings.includeTip && tip.get().plantPtr)
        {
            _settings.includeTip = true;
            _settings.node_age = RND_B(tip_age);
            if (tip.get().plantPtr)
            {
                tip_NODE = tip.get().plantPtr->build(_settings, _addVerts, _extents);
            }
        }
    }
}


void _stemBuilder::build_leaves(buildSetting _settings, bool _addVerts, bool _extents)
{
    //leaves.reset(); // so order remains the same
    _settings.isBaking = false;
    numLeavesBuilt = 0;
    // reset teh seed so all lods build teh same here
    _rootPlant::generator.seed(_settings.seed);
    int oldSeed = _settings.seed;
    std::mt19937 MT(_settings.seed * 35696 + 193489);

    // side nodes
    int end = (int)NODES.size();
    if (unique_tip) end -= 1;// - do not include the tip

    int numLeafCompound = 0;
    float nodeRoll = 0;
    for (int i = 0; i <= end; i++)
    {
        if (i >= firstLiveSegment)
        {
            float grownAge = RND_B(leaf_fully_grown_age);
            float leafAge = 1.f - pow(i / age, leaf_age_power);
            int numL = (int)RND_B(numLeaves);
            float t = (float)i / age;
            //float t_live = glm::clamp((i - firstLiveSegment) / (age - firstLiveSegment), 0.f, 1.f);
            //t_live = 1.f - pow(t_live, leaf_age_power);
            float t_live = glm::clamp((end - i) / grownAge, 0.f, 1.f);

            float W = root_width - (root_width - tip_width) * pow(t, stem_pow_width.x);

            float rndRoll = 6.28f * (float)d_1_1(MT);
            (void)rndRoll;      // only the commented-out nodeRoll line below uses this
            float halfTwist = 0;
            if (numL > 0) halfTwist = 6.283185307f / (float)numL / 2.f;

            if (compoundLeaf)
            {
                nodeRoll = 3.14159f / 2.f;
                if (numLeaves.x < 1.5f) numLeaves = { 1, 0 };       // 1 or 2 is acceptable
                else numLeaves = { 2, 0 };
                if (i % 2)
                {
                    nodeRoll += 3.14159f;
                }
            }
            else
            {
                nodeRoll += halfTwist;
            }
            //nodeRoll += rndRoll / (float)numL;
            if (compoundLeaf) nodeRoll = 0;

            for (int j = 0; j < numL; j++)
            {

                numLeavesBuilt++;
                glm::mat4 node = NODES[i];
                float A = leaf_angle.x + leaf_angle.y * leafAge;// +RND_B(leaf_rnd);   // +DDD(MT)
                //float nodeTwist = rndRoll * leaf_rnd.x + 6.283185307f / (float)numL * (float)j;



                ROLL(node, nodeRoll);

                if (roll_horizontal)
                {
                    ROLL_HORIZONTAL_B(node);
                    if (j % 2)
                    {
                        ROLL(node, rollOffset);
                    }
                    else
                    {
                        ROLL(node, -rollOffset);
                    }
                }
                else if (compoundLeaf)
                {
                    if (numLeafCompound % 2)
                    {
                        ROLL(node, -1.571f - rollOffset);
                    }
                    else
                    {
                        ///ROLL(node, -1.57f);
                        ROLL(node, 1.571f + rollOffset);
                    }

                }

                else
                {
                    ROLL(node, rollOffset);
                    float nodeTwist = 6.283185307f / (float)numL * (float)j;    // this takes us around
                    nodeTwist += (float)d_1_1(MT) * leaf_rnd.x;
                    ROLL(node, nodeTwist);
                }



                //if ((numL == 1) && (i & 0x1))  ROLL(node, 3.14f);   // 180 degrees is 1 leaf
                PITCH(node, -A);

                if (compoundLeaf)
                {
                    if (numLeafCompound % 2)
                    {
                        ROLL(node, 1.0571f);
                    }
                    else
                    {
                        ROLL(node, -1.0571f);
                    }

                    numLeafCompound++;
                }

                GROW(node, W * branchPush / fabs(sin(A)));   // 70% out, has to make up for alpha etcDman I want to grow here to some % of branch width
                // likely dependent on pitch




                // And now rotate upwards again.
                // ZERO is not acceptable for an age, if true, dont acll anything
                _settings.seed = oldSeed + (i * 10) + j + 3;
                _settings.root = node;
                if (leaf_age_override)  _settings.node_age = age - i + 1;
                else                    _settings.node_age = -leafAge;
                _settings.normalized_age = t_live;
                if (branches.branchData.size() > 0 && _settings.normalized_age > 0.05f)
                {
                    float branchAge = (i / (float)age) + (j / (float)age / (float)numL);
                    _rootPlant::generator.seed(_settings.seed);
                    _randomBranch* pBranch = branches.get(branchAge);
                    if (pBranch && pBranch->plantPtr) pBranch->plantPtr->build(_settings, _addVerts, _extents);
                }
            }
        }
    }
}


void _stemBuilder::build_NODES(buildSetting _settings, bool _addVerts, bool _extents)
{
    _rootPlant::generator.seed(_settings.seed);
    std::uniform_int_distribution<> distAlbedo(-50, 50);
    glm::mat4 node = _settings.root;
    if (roll_horizontal) { ROLL_HORIZONTAL(node); }
    NODES.clear();
    NODES.push_back(node);
    if (_extents) EXTENTS.push(node[3], 0.f);

    age = RND_B(numSegments);
    if (_settings.node_age > 0.f)
    {
        age = _settings.node_age * RND_B(float2(1, numSegments.y));
    }// if passed in from root use that
    else { age *= fabs(_settings.node_age); } // negative values are relative
    int iAge = __max(1, (int)age);

    tip_width = RND_B(stem_width) * 0.001f;                         // tip radius
    root_width = tip_width + RND_B(stem_d_width) * 0.001f * age;    // root radius  //?? iAge
    float rootPow = stem_pow_width.x;
    float dR = (root_width - tip_width);

    int numLiveNodes = (int)RND_B(max_live_segments);
    firstLiveSegment = __max(1, iAge - numLiveNodes);

    std::uniform_real_distribution<> d50(0.5f, 1.5f);
    float pixRandFoViz = _settings.pixelSize * (float)d50(_rootPlant::generator);

    float V = ribbonBuilder::V_MAX;
    float Vaspect = 1.f;
    if (stem_Material.index >= 0)
    {
        int texSlot = _plantMaterial::static_materials_veg.materialVector[stem_Material.index]._constData.albedoTexture;
        if (texSlot >= 0)
        {
            float hgt = (float)_plantMaterial::static_materials_veg.textureVector[texSlot]->getHeight();
            float width = (float)_plantMaterial::static_materials_veg.textureVector[texSlot]->getWidth();
            Vaspect = width / hgt;
        }
    }


    bool visible = root_width > pixRandFoViz && (stem_Material.index >= 0);
    if (_addVerts && visible) {
        _ribbonBuilder.startRibbon(true, _settings.pivotIndex);
        _ribbonBuilder.set(node, root_width * 0.5f, stem_Material.index, float2(1.f, V), 1.f, 1.f);   // set very first one
    }

    // FIXME - VERY bad need way to control ut a bit with LOD but puxels make no sense on this scale
    float stemLenght = glm::length((float3)(tip_NODE[3] - node[3]));
    float stemPixels = (stemLenght * 20) / _settings.pixelSize;
    (void)stemPixels;
    int stemNumSegments = (int)nodeLengthSplit;// glm::clamp((int)(stemPixels / nodeLengthSplit), 1, 10);     // 1 for every 8 pixels, clampped
    float totalStep = 20.f * iAge / (float)stemNumSegments;
    float cnt = 0;


    float W = root_width;   // must be initialized here - the stemReplacement path reads W before the loop body first writes it

    for (int i = 0; i < iAge; i++)
    {
        if (stemReplacement.get().plantPtr)
        {
            _settings.doNotAddPivot = true;
            _settings.root = stemReplacement.get().plantPtr->build(_settings, _addVerts, _extents);
            NODES.push_back(_settings.root);
            _settings.doNotAddPivot = false;
        }
        else
        {
            float nodeAge = glm::clamp((age - i) / numLiveNodes, 0.f, 1.f);
            float L = RND_B(stem_length) * 0.001f / 20.f;
            if (lengthFromBranchAge)
            {
                float grownAge = RND_B(leaf_fully_grown_age);
                float t_live = glm::clamp((iAge - i) / grownAge, 0.f, 1.f);
                L *= t_live;
            }
            float C = RND_CRV(stem_curve) / 20.f / age;
            float t = (float)i / age;
            W = root_width - dR * pow(t, rootPow);

            float pScale = (nodeAge * stem_phototropism.y) + ((1.f - nodeAge) * (1.f - stem_phototropism.y));
            float P = pScale * stem_phototropism.x / 20.f / age * 10;

            for (int j = 0; j < 20; j++)
            {
                float aspect = V -= L / W * Vaspect;
                (void)aspect;
                if (!_settings.isBaking)
                {
                    PITCH(node, C);

                    // Phototropy - custom axis
                    float pScale2 = 1.f - fabs(node[1][1]);
                    if (pScale2 > 0.05f)
                    {
                        float3 axis = glm::cross(float3(0, 1, 0), (glm::vec3)node[1]);
                        float3 XX = float3(0, 0, 0);
                        XX.x = glm::dot(axis, (glm::vec3)node[0]);
                        XX.z = glm::dot(axis, (glm::vec3)node[2]);
                        node = glm::rotate(node, -P * pScale2, glm::normalize(XX));
                    }

                    {
                        //perlinData
                        float step = ((float)i + (j * 0.05f)) / age;
                        float perlinScale = 10.f * glm::smoothstep(0.f, 0.3f, step);
                        step *= 1024.f;

                        float noise = perlinData[(int)(step * perlinCurve.y + _settings.seed) % 1024];// (float)perlin.normalizedOctave1D(perlinCurve.y * step, 4, 0.7);
                        PITCH(node, perlinScale * noise * perlinCurve.x * 0.01f);

                        noise = perlinData[(int)(step * perlinTwist.y + _settings.seed + 400) % 1024];// (float)perlinTWST.normalizedOctave1D(perlinTwist.y * step, 4, 0.7);
                        YAW(node, perlinScale * noise * perlinTwist.x * 0.01f);
                    }
                }

                GROW(node, L);
                cnt++;

                float t2 = (float)i / age + ((float)j / 20.f * (1.f / age));
                float W2 = root_width - dR * pow(t2, rootPow);
                visible = W2 > pixRandFoViz && (stem_Material.index >= 0);
                bool weareinthelastbit = (i == iAge) && (j > (20 - totalStep));
                if (_addVerts && visible && cnt >= totalStep && !weareinthelastbit)
                {
                    _ribbonBuilder.set(node, W2 * 0.5f, stem_Material.index, float2(1.f, V), 1.f, 1.f);
                    cnt -= totalStep;
                }
            }
            NODES.push_back(node);
            if (_extents) EXTENTS.push(node[3], W * 0.5f);

            // now rotate for the next segment
            ROLL(node, RND_CRV(node_rotation) / age);
            YAW(node, RND_CRV(node_angle) / age);
        }
    } //i -> age

    // do very last one
    if (_addVerts && visible)
    {
        _ribbonBuilder.set(node, W * 0.5f, stem_Material.index, float2(1.f, V), 1.f, 1.f);
    }

    if (!_addVerts) tip_NODE = NODES.back();
}


void _stemBuilder::clear_build_info()
{
    numInstancePacked = 0;
    numVertsPacked = 0;
    debugTotalVertsPacked = 0;
    debugnumPivots = 0;
    debugLOD = 0;
    debugmaxLOD = 0;
    debugSIZE = 0.f;
    debugBAKETYPE = 0;

    for (auto& L : branches.branchData)
    {
        if (L.plantPtr)  L.plantPtr->clear_build_info();
    }


    for (auto& L : tip.data)
    {
        if (L.plantPtr)  L.plantPtr->clear_build_info();
    }
}



void _stemBuilder::addPivot(buildSetting* p_settings)
{
    float3 extent = (float3)tip_NODE[3] - (float3)NODES.front()[3]; // do this above and save in stem
    float ext_L = glm::length(extent);
    extent = glm::normalize(extent) / ext_L;

    _plant_anim_pivot p;
    p.root = (float3)NODES.front()[3];
    p.extent = extent;
    p.frequency = rootFrequency() * sqrt(ext_L);
    p.stiffness = ossilation_stiffness;
    p.shift = ossilation_power;                     // DEPRECATED
    p.offset = DD_0_255(_rootPlant::generator);
    p.padd1 = 0;
    p.padd2 = 0;

    if (p_settings->pivotDepth < 4)
    {
        p_settings->pivotIndex[p_settings->pivotDepth] = _ribbonBuilder.pushPivot(p_settings->seed, p);
        p_settings->pivotDepth += 1;
        debugnumPivots++;

        debugLastPivots[0] = p_settings->pivotIndex[0];
        debugLastPivots[1] = p_settings->pivotIndex[1];
        debugLastPivots[2] = p_settings->pivotIndex[2];
        debugLastPivots[3] = p_settings->pivotIndex[3];
        if (LOGTHEBUILD)
            spdlog::info("vegetation: add pivot {{{}}} {} [seed {}]   {}", p_settings->pivotDepth, p_settings->pivotIndex[p_settings->pivotDepth - 1], p_settings->seed, name);
    }
    else
    {
        spdlog::info("vegetation: pivot depth exceeded");
    }
}


void _stemBuilder::build_extents(buildSetting _settings)
{
    _settings.pixelSize = 0.001f;
    build_NODES(_settings, false, true);
    build_leaves(_settings, false, true);
    if (_settings.includeTip) build_tip(_settings, false, true);
}

glm::mat4 _stemBuilder::build(buildSetting _settings, bool _addVerts, bool _extents)
{
    // lod
    levelOfDetail* LOD = getLOD(_settings.pixelSize, _settings.callDepth > 0);
    lodBake* BAKE = getBakeInfo(LOD->bakeIndex);

    _settings.callDepth++;
    if (_settings.callDepth > 10) return glm::mat4(1.f);    // guard against recursion where age doesnt decline

    build_NODES(_settings, false);
    if (_settings.includeTip) build_tip(_settings, false);

    if (_addVerts && LOD->bakeType != BAKE_NONE)
    {
        glm::mat4 start = NODES[BAKE->clipDead ? firstLiveSegment : 0];
        EXTENTS.start(start, tip_NODE[3]);
        build_extents(_settings);
        EXTENTS.end();
    }

    // has to recurse
    if (_extents)
    {
        build_extents(_settings);
    }


    if (_addVerts)
    {
        if (hasPivot && !_settings.doNotAddPivot)   addPivot(&_settings);

        startVerts = leafVerts = tipVerts = _ribbonBuilder.numVerts();

        if (LOD->bakeType == BAKE_DIAMOND)       build_2(_settings, BAKE, true, true);
        if (LOD->bakeType == BAKE_QUAD)          build_2(_settings, BAKE, true, false);
        if (LOD->bakeType == BAKE_4)             build_4(_settings, BAKE, true);
        if (LOD->bakeType == BAKE_N)             build_n(_settings, BAKE, true);

        if (LOD->useGeometry)
        {
            if (LOD->bakeType == BAKE_NONE)      build_NODES(_settings, true);

            leafVerts = _ribbonBuilder.numVerts();
            build_leaves(_settings, true);

            tipVerts = _ribbonBuilder.numVerts();
            build_tip(_settings, true);
        }

        if ((_ribbonBuilder.numVerts() - startVerts) > 0) numInstancePacked++;
        numVertsPacked += leafVerts - startVerts;
        debugTotalVertsPacked += _ribbonBuilder.numVerts() - startVerts;
        debugSIZE = _settings.pixelSize;
    }

    _settings.callDepth--;
    return tip_NODE;
}


glm::mat4 _stemBuilder::getTip(bool includeChildren)
{
    if (NODES.size() == 0)      return (glm::mat4(1.f));

    if (includeChildren)        return tip_NODE;
    else                        return NODES.back();    // since its only direction test with this
}


glm::mat4 _stemBuilder::getRoot(bool _cullDead)
{
    if (NODES.size() == 0)  return (glm::mat4(1.f));

    if (_cullDead)          return NODES[firstLiveSegment];
    else                    return NODES[0];
}

float2 _stemBuilder::calculate_extents(glm::mat4 view)
{
    float2 extents = _ribbonBuilder.calculate_extents(view);

    std::filesystem::path full_path = path;

    // lod 0
    float4 dd0 = glm::saturate(_ribbonBuilder.dU / lod_bakeInfo[0].bakeWidth);
    lod_bakeInfo[0].extents = extents;
    lod_bakeInfo[0].dU[0] = dd0[0];
    lod_bakeInfo[0].dU[1] = dd0[1];
    lod_bakeInfo[0].dU[2] = dd0[2];
    lod_bakeInfo[0].dU[3] = dd0[3];
    lod_bakeInfo[0].material.name = "bake_0_100";
    lod_bakeInfo[0].material.path = full_path.parent_path().string() + "\\bake_" + full_path.stem().string() + "\\" + lod_bakeInfo[0].material.name + ".vegetationMaterial";;

    // lod 1
    dd0 = glm::saturate(_ribbonBuilder.dU / lod_bakeInfo[1].bakeWidth);
    lod_bakeInfo[1].extents = extents;
    lod_bakeInfo[1].dU[0] = dd0[0];
    lod_bakeInfo[1].dU[1] = dd0[1];
    lod_bakeInfo[1].dU[2] = dd0[2];
    lod_bakeInfo[1].dU[3] = dd0[3];
    lod_bakeInfo[1].material.name = "bake_1";
    lod_bakeInfo[1].material.path = full_path.parent_path().string() + "\\bake_" + full_path.stem().string() + "\\" + lod_bakeInfo[1].material.name + ".vegetationMaterial";;

    // lod 2
    dd0 = glm::saturate(_ribbonBuilder.dU / lod_bakeInfo[2].bakeWidth);
    lod_bakeInfo[2].extents = extents;
    lod_bakeInfo[2].dU[0] = dd0[0];
    lod_bakeInfo[2].dU[1] = dd0[1];
    lod_bakeInfo[2].dU[2] = dd0[2];
    lod_bakeInfo[2].dU[3] = dd0[3];
    lod_bakeInfo[2].material.name = "bake_2";
    lod_bakeInfo[2].material.path = full_path.parent_path().string() + "\\bake_" + full_path.stem().string() + "\\" + lod_bakeInfo[2].material.name + ".vegetationMaterial";;

    return extents;
}









// _clumpBuilder
// -----------------------------------------------------------------------------------------------------------------------------------

void _clumpBuilder::loadPath()
{
    if (std::filesystem::exists(terrafectorEditorMaterial::rootFolder + path))
    {
        try
        {
            std::ifstream is(terrafectorEditorMaterial::rootFolder + path);
            cereal::JSONInputArchive archive(is);
            archive(*this);
            changed = false;
            fileNotFound = false;
        }
        catch (const std::exception& e)
        {
            spdlog::error("vegetation: _clumpBuilder::loadPath failed to parse '{}' - {}", path, e.what());
        }
    }
    else
    {
        spdlog::error("vegetation: File does not exists in the relative tree structure - {}", path);
    }
}

void _clumpBuilder::savePath()
{
    std::ofstream os(terrafectorEditorMaterial::rootFolder + path);
    cereal::JSONOutputArchive archive(os);
    archive(*this);
    changed = false;
}












lodBake* _clumpBuilder::getBakeInfo(int i)
{
    if (i < (int)lod_bakeInfo.size()) return &lod_bakeInfo[i];

    return nullptr;
}

levelOfDetail* _clumpBuilder::getLodInfo(uint i)
{
    if (i < lodInfo.size()) return &lodInfo[i];
    return nullptr;
}


void _clumpBuilder::clear_build_info()
{
    numInstancePacked = 0;
    numVertsPacked = 0;
    debugTotalVertsPacked = 0;
    debugnumPivots = 0;
    debugLOD = 0;
    debugmaxLOD = 0;
    debugSIZE = 0.f;
    debugBAKETYPE = 0;

    for (auto& C : clumps)
    {
        for (auto& L : C.children.data)
        {
            if (L.plantPtr)  L.plantPtr->clear_build_info();
        }
    }
}

glm::mat4 _clumpBuilder::getTip(bool includeChildren)
{
    (void)includeChildren;
    return TIP_CENTER;
}

float2 _clumpBuilder::calculate_extents(glm::mat4 view)
{
    float2 extents = _ribbonBuilder.calculate_extents(view);

    std::filesystem::path full_path = path;

    // lod 0
    float4 dd0 = glm::saturate(_ribbonBuilder.dU / lod_bakeInfo[0].bakeWidth);
    lod_bakeInfo[0].extents = extents;
    lod_bakeInfo[0].dU[0] = dd0[0];
    lod_bakeInfo[0].dU[1] = dd0[1];
    lod_bakeInfo[0].dU[2] = dd0[2];
    lod_bakeInfo[0].dU[3] = dd0[3];
    lod_bakeInfo[0].material.name = "bake_0_100";
    lod_bakeInfo[0].material.path = full_path.parent_path().string() + "\\bake_" + full_path.stem().string() + "\\" + lod_bakeInfo[0].material.name + ".vegetationMaterial";;

    // lod 1
    dd0 = glm::saturate(_ribbonBuilder.dU / lod_bakeInfo[1].bakeWidth);
    lod_bakeInfo[1].extents = extents;
    lod_bakeInfo[1].dU[0] = dd0[0];
    lod_bakeInfo[1].dU[1] = dd0[1];
    lod_bakeInfo[1].dU[2] = dd0[2];
    lod_bakeInfo[1].dU[3] = dd0[3];
    lod_bakeInfo[1].material.name = "bake_1";
    lod_bakeInfo[1].material.path = full_path.parent_path().string() + "\\bake_" + full_path.stem().string() + "\\" + lod_bakeInfo[1].material.name + ".vegetationMaterial";;

    // lod 2
    dd0 = glm::saturate(_ribbonBuilder.dU / lod_bakeInfo[2].bakeWidth);
    lod_bakeInfo[2].extents = extents;
    lod_bakeInfo[2].dU[0] = dd0[0];
    lod_bakeInfo[2].dU[1] = dd0[1];
    lod_bakeInfo[2].dU[2] = dd0[2];
    lod_bakeInfo[2].dU[3] = dd0[3];
    lod_bakeInfo[2].material.name = "bake_2";
    lod_bakeInfo[2].material.path = full_path.parent_path().string() + "\\bake_" + full_path.stem().string() + "\\" + lod_bakeInfo[2].material.name + ".vegetationMaterial";;

    return extents;
}

glm::mat4 _clumpBuilder::buildChildren(buildSetting _settings, bool _addVerts, bool _extents)
{
    if (!_addVerts) _settings.pixelSize = 0.0001f;   // do detail so we preserve the tip in calculations

    bool forcePHOTOTROPY = _settings.isBaking;
    glm::mat4 ORIGINAL = _settings.root;

    int oldSeed = _settings.seed * 10;
    std::mt19937 MT(_settings.seed + 193);
    _rootPlant::generator.seed(_settings.seed);

    _ribbonBuilder.startRibbon(true, _settings.pivotIndex);
    float tipDistance = 0;

    glm::mat4 nodeCENTERLINE = ORIGINAL;
    for (auto& CLUMP : clumps)
    {
        if (!CLUMP.radial)
        {
            ROLL(nodeCENTERLINE, 3.14f);
            if (!forcePHOTOTROPY)              // for clumps, gettign rid of this part makes tehm upright
            {
                PITCH(nodeCENTERLINE, CLUMP.child_angle.x);  // add rnd
            }
        }
        float3 CENTER_DIR = glm::normalize((float3)nodeCENTERLINE[1]);


        ROOTS.clear();
        int numC = (int)RND_B(CLUMP.numChildren);
        float s = RND_B(CLUMP.size);
        float a = sqrt(RND_B(CLUMP.aspect));
        float w = s * a;
        float h = s / a;

        std::vector<float2> PNTS;
        float rPT = sqrt(1.f / numC) * 0.8f;

        for (int i = 0; i < numC; i++)
        {
            // generate random points in a unit circle
            float l;
            float2 rndPos;

            do
            {
                rndPos.x = (float)d_1_1(MT);
                rndPos.y = (float)d_1_1(MT);
                l = glm::length(rndPos);
                for (auto& p : PNTS)
                {
                    float2 diff = rndPos - p;
                    float l2 = glm::length(diff);
                    if (l2 < rPT) l = 100;
                }
            } while (l >= 1.f);

            PNTS.push_back(rndPos);

            glm::mat4 node = ORIGINAL;

            node[3] += node[0] * rndPos.x * w;
            node[3] += node[2] * rndPos.y * h;

            //float c_angle = d_1_1(MT) * child_angle.x;
            //float c_angle_age = d_1_1(MT) * child_angle.y;
            float c_rnd = (float)d_1_1(MT) * CLUMP.child_rnd.x;
            (void)c_rnd;
            float c_rndp = (float)d_1_1(MT) * CLUMP.child_rnd.x;
            (void)c_rndp;
            float c_age = RND_B(CLUMP.child_age);
            float c_rot = atan2(rndPos.x, rndPos.y);
            float t = l;     // center to edge
            float t_live = 1.f - pow(t, CLUMP.child_age_power);

            if (CLUMP.radial)
            {
                GROW(node, w * 0.5f * pow((1 - l), 0.5f));  //cosine rather
                ROLL(node, c_rot + 3.14f);

                //float c_angle = RND_B(CLUMP.child_angle);
                //PITCH(node, c_angle * l);
                // try - 2
                PITCH(node, -CLUMP.child_angle.x * pow(l, 1.95f));   // fixed amount but scaled to be more upright in middle


                //PITCH(node, CLUMP.child_angle.x + (-CLUMP.child_angle.y * l));  // add rnd
                /////PITCH(node, c_angle * t_live + c_rndp);  // add rnd
                //YAW(node, c_rnd);
            }
            else
            {
                t = rndPos.x * 0.5f + 0.5f;     // left to right  but needs controls how much it changes
                t_live = 1.f - pow(t, CLUMP.child_age_power);

                ROLL(node, 3.14f);

                if (!forcePHOTOTROPY)              // for clumps, gettign rid of this part makes tehm upright
                {
                    PITCH(node, CLUMP.child_angle.x);  // add rnd
                }
                YAW(node, RND_B(CLUMP.child_rnd));
            }



            ROOTS.push_back(node);

            _settings.isBaking = false;      // MAKE A COOPY this is just messign em up
            _settings.seed = oldSeed + (i + 1) * 1000;
            _settings.root = node;
            _settings.node_age = c_age;
            _settings.normalized_age = t_live;
            _plantRND CHILD = CLUMP.children.get();
            glm::mat4 TIP(1.f);
            if (CHILD.plantPtr) TIP = CHILD.plantPtr->build(_settings, _addVerts, _extents);

            tipDistance = __max(tipDistance, glm::dot((float3)TIP[3] - (float3)ORIGINAL[3], CENTER_DIR));
        }
    }


    START = nodeCENTERLINE;
    TIP_CENTER = nodeCENTERLINE;

    GROW(TIP_CENTER, tipDistance);

    return TIP_CENTER;
}


//??? Is this universal and belongs toplantbuilder - currently identical to stembuilderr
levelOfDetail* _clumpBuilder::getLOD(float _pixelSize, bool _rnd)
{
    int lodIndex = 0;
    float random = RND_B(float2(1.f, _rnd ? 0.2f : 0.f));

    for (int i = 1; i < (int)lodInfo.size(); i++)
    {
        if (_pixelSize * random <= (lodInfo[i].pixelSize * 0.001f))          lodIndex = i;
    }

    debugLOD = lodIndex;
    debugmaxLOD = (uint)lodInfo.size() - 1;
    debugBAKETYPE = lodInfo[lodIndex].bakeType;

    return &lodInfo[lodIndex];
}



void _clumpBuilder::build_extents(buildSetting _settings)
{
    _settings.pixelSize = 0.001f;
    buildChildren(_settings, false, true);
}


void _clumpBuilder::addPivot(buildSetting* p_settings)
{
    //float3 extent = (float3)tip_NODE[3] - (float3)NODES.front()[3]; // do this above and save in stem
    //float ext_L = glm::length(extent);
    //extent = glm::normalize(extent) / ext_L;
    float3 extent = (float3)TIP_CENTER[3] - (float3)START[3];
    float ext_L = glm::length(extent);
    extent = glm::normalize(extent) / ext_L;

    _plant_anim_pivot p;
    //p.root = (float3)NODES.front()[3];
    p.root = (float3)START[3];
    p.extent = extent;
    p.frequency = rootFrequency() * sqrt(ext_L);
    p.stiffness = ossilation_stiffness;
    p.shift = ossilation_power;                     // DEPRECATED
    p.offset = DD_0_255(_rootPlant::generator);
    p.padd1 = 0;
    p.padd2 = 0;

    if (p_settings->pivotDepth < 4)
    {
        p_settings->pivotIndex[p_settings->pivotDepth] = _ribbonBuilder.pushPivot(p_settings->seed, p);
        p_settings->pivotDepth += 1;
        debugnumPivots++;

        debugLastPivots[0] = p_settings->pivotIndex[0];
        debugLastPivots[1] = p_settings->pivotIndex[1];
        debugLastPivots[2] = p_settings->pivotIndex[2];
        debugLastPivots[3] = p_settings->pivotIndex[3];
        if (LOGTHEBUILD)
            spdlog::info("vegetation: add pivot {{{}}} {} [seed {}]   {}", p_settings->pivotDepth, p_settings->pivotIndex[p_settings->pivotDepth - 1], p_settings->seed, name);
    }
    else
    {
        spdlog::info("vegetation: pivot depth exceeded");
    }
}


glm::mat4 _clumpBuilder::build(buildSetting _settings, bool _addVerts, bool _extents)
{
    // lod
    levelOfDetail* LOD = getLOD(_settings.pixelSize, _settings.callDepth > 0);
    lodBake* BAKE = getBakeInfo(LOD->bakeIndex);

    _settings.callDepth++;
    if (_settings.callDepth > 10) return glm::mat4(1.f);    // guard against recursion where age doesnt decline

    buildChildren(_settings, false);

    if (_addVerts && LOD->bakeType != BAKE_NONE)
    {
        glm::mat4 start = _settings.root;
        EXTENTS.start(start, (float3)start[3] + (float3)start[1]);
        build_extents(_settings);
        EXTENTS.end();
    }

    // has to recurse
    if (_extents)
    {
        build_extents(_settings);
    }


    if (_addVerts)
    {
        if (hasPivot && !_settings.doNotAddPivot)   addPivot(&_settings);

        uint startVerts = _ribbonBuilder.numVerts();

        if (LOD->bakeType == BAKE_DIAMOND)       build_2(_settings, BAKE, true, true);
        if (LOD->bakeType == BAKE_QUAD)          build_2(_settings, BAKE, true, false);
        //if (LOD->bakeType == BAKE_4)             build_4(_settings, BAKE, true);
        //if (LOD->bakeType == BAKE_N)             build_n(_settings, BAKE, true);

        if (LOD->useGeometry)
        {
            buildChildren(_settings, true);
        }

        uint numVerts = _ribbonBuilder.numVerts() - startVerts;
        if (numVerts > 0) numInstancePacked++;
        debugTotalVertsPacked = numVerts;
        debugSIZE = _settings.pixelSize;

    }

    _settings.callDepth--;
    return TIP_CENTER;
}


glm::mat4  _clumpBuilder::build_2(buildSetting _settings, lodBake* pBake, bool _faceCamera, bool _diamond)
{
    (void)_faceCamera;
    if (pBake->pixHeight == 0) return START;
    float w = EXTENTS.width() * pBake->bakeWidth;

    if (w > _settings.pixelSize)
    {
        _ribbonBuilder.startRibbon(pBake->faceCamera, _settings.pivotIndex);
        _ribbonBuilder.set(EXTENTS.lerp(pBake->bake_V.x), w, pBake->material.index, float2(1.f, 1.f), 1.f, 1.f, true, 0.5f, 0.1f, 0.0f, _diamond);
        _ribbonBuilder.set(EXTENTS.lerp(pBake->bake_V.y), w, pBake->material.index, float2(1.f, 0.f), 1.f, 1.f, true, 0.5f, 0.1f, 0.0f, _diamond);
    }

    return EXTENTS.lerp(pBake->bake_V.y);
}












void plant_single::loadTexture()
{
    std::filesystem::path texturePath = terrafectorEditorMaterial::rootFolder + path;
    std::string tp = texturePath.parent_path().string() + "\\bake_" + texturePath.stem().string() + "\\bake_0_100_FULL_albedo.png";
    texture = ew::Texture::createFromFile(tp, true, true);
}





void plant_species::sort_on_age()
{
}
















void _rootPlant::reloadShader()
{

    if (render_Clip)        vegetationShader.add("_DEBUG_PIXELS", "");
    else                    vegetationShader.remove("_DEBUG_PIXELS");

    if (render_PixelCount)  vegetationShader.add("_PIXEL_COUNT", "");
    else                    vegetationShader.remove("_PIXEL_COUNT");

    if (render_ZOnly)       vegetationShader.add("_Z_ONLY", "");
    else                    vegetationShader.remove("_Z_ONLY");

    if (render_EarlyZ)       vegetationShader.add("_EARLY_Z", "");
    else                    vegetationShader.remove("_EARLY_Z");

    // JHFAA toggle with alpha
    //if (render_EarlyZ)       vegetationShader.add("_EARLY_Z", "");
    //else                    vegetationShader.remove("_EARLY_Z");



    vegetationShader.load("hlsl/terrain/render_vegetation_ribbons.hlsl", "vsMain", "psMain", ew::Topology::LineStrip, "gsMain");
    vegetationShader.setBuffer("plant_buffer", plantData);
    vegetationShader.setBuffer("plant_pivot_buffer", plantpivotData);
    vegetationShader.setBuffer("instance_buffer", instanceData);
    vegetationShader.setBuffer("block_buffer", blockData_preSort);
    vegetationShader.setBuffer("vertex_buffer", vertexData);
    vegetationShader.setBuffer("sort", buffer_gpuSort);
    vegetationShader.setBuffer("materials", _plantMaterial::static_materials_veg.sb_vegetation_Materials);
    vegetationShader.setSampler("gSmpLinear", sampler_Ribbons);              // fixme only cvlamlX
    vegetationShader.setSampler("gSamplerDepth", sampler_Depth);
    vegetationShader.setSampler("gSmpLinearClamp", sampler_ClampAnisotropic);              // fixme only cvlamlX
    vegetationShader.setTexture("highResShadow", shadowFbo->getDepthStencilTexture());

    _plantMaterial::static_materials_veg.setTextures(vegetationShader);

    // Guarded because a Texture3D slot must never receive the layer's 2D dummy
    // texture - Vulkan rejects the mismatch. In practice these are always set:
    // the atmosphere is wired up before plants_Root.onLoad runs.
    if (inscatter)          vegetationShader.setTexture("gAtmosphereInscatter", inscatter);
    if (outscatter)         vegetationShader.setTexture("gAtmosphereOutscatter", outscatter);
    if (sunlightTexture)    vegetationShader.setTexture("SunInAtmosphere", sunlightTexture);

    if (envTexture)         vegetationShader.setTexture("gEnv", envTexture);
    vegetationShader.setBuffer("feedback_Veg", buffer_feedback);
    if (dappledLightTexture) vegetationShader.setTexture("gDappledLight", dappledLightTexture);


    if (inscatter)          billboardShader.setTexture("gAtmosphereInscatter", inscatter);
    if (outscatter)         billboardShader.setTexture("gAtmosphereOutscatter", outscatter);
    if (sunlightTexture)    billboardShader.setTexture("SunInAtmosphere", sunlightTexture);

    if (envTexture)         billboardShader.setTexture("gEnv", envTexture);
    billboardShader.setBuffer("feedback_Veg", buffer_feedback);
}

void _rootPlant::onLoad()
{
    //DXGI_FORMAT_R32_TYPELESS
    // shadowFbo is 8192^2 D24S8 + R8Unorm(UAV) and is allocated
    // unconditionally: roughly 320 MB for something only the SAMPLE_MODE
    // research path reads.
    {
        const Diligent::BIND_FLAGS rtFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS;
        shadowFbo = ew::Fbo::create();
        shadowFbo->attachColorTarget(ew::Texture::create2D(8192, 8192, Diligent::TEX_FORMAT_R8_UNORM, 1, 1, nullptr, rtFlags, "veg shadowFbo albedo"), 0);		    // albedo    so I can test my soft shadow ideas
        shadowFbo->attachDepthStencilTarget(ew::Texture::create2D(8192, 8192, Diligent::TEX_FORMAT_D24_UNORM_S8_UINT, 1, 1, nullptr, BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE, "veg shadowFbo depth"));			// keep for now, not sure why, but maybe usefult for cuts

        rgbFbo = ew::Fbo::create();
        rgbFbo->attachColorTarget(ew::Texture::create2D(1024, 256, Diligent::TEX_FORMAT_RGBA8_UNORM, 1, 1, nullptr, rtFlags, "veg rgbFbo"), 0);
        rgbFbo->attachDepthStencilTarget(ew::Texture::create2D(1024, 256, Diligent::TEX_FORMAT_D24_UNORM_S8_UINT, 1, 1, nullptr, BIND_DEPTH_STENCIL, "veg rgbFbo depth"));
    }

    RGB_MAP = ew::Texture::create2D(128, 64, Diligent::TEX_FORMAT_RGBA8_UNORM, 1, 1, nullptr, BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE, "veg RGB_MAP");


    // Diligent does not zero default-heap buffers. Anything a shader may read
    // before the first CPU or compute write is therefore created with explicit
    // zeroed initial data - plantData, drawArgs, sort and feedback below.
    {
        std::vector<uint8_t> zeroPlants(sizeof(plant) * MAX_PLANT_PLANTS, 0);
        plantData = ew::Buffer::createStructured(sizeof(plant), MAX_PLANT_PLANTS,
            BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS, zeroPlants.data(), "veg plantData");
    }
    plantpivotData = ew::Buffer::createStructured(sizeof(_plant_anim_pivot), MAX_PLANT_PIVOTS,
        BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS, nullptr, "veg plantpivotData");
    instanceData = ew::Buffer::createStructured(sizeof(plant_instance), MAX_PLANT_INSTANCES,
        BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS, nullptr, "veg instanceData");
    instanceData_Billboards = ew::Buffer::createStructured(sizeof(plant_instance), MAX_PLANT_BILLBOARDS,
        BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS | BIND_INDIRECT_DRAW_ARGS, nullptr, "veg instanceData_Billboards");

    blockData_preSort = ew::Buffer::createStructured(sizeof(block_data), MAX_PLANT_BLOCKS * 3,
        BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS | BIND_INDIRECT_DRAW_ARGS, nullptr, "veg blockData_preSort");        // big enough to house inatnces * blocks per instance   8 Mb for now
    blockData = ew::Buffer::createStructured(sizeof(block_data), MAX_PLANT_BLOCKS,
        BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS | BIND_INDIRECT_DRAW_ARGS, nullptr, "veg blockData");        // big enough to house inatnces * blocks per instance   8 Mb for now
    vertexData = ew::Buffer::createStructured(sizeof(ribbonVertex8), MAX_PLANT_VERTS,
        BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS, nullptr, "veg vertexData");

    {
        std::vector<t_DrawArguments> zeroDraw(numRenderViews * 128, t_DrawArguments{ 0, 0, 0, 0 });
        drawArgs_vegetation = ew::Buffer::createStructured(sizeof(t_DrawArguments), numRenderViews * 128,
            BIND_UNORDERED_ACCESS | BIND_INDIRECT_DRAW_ARGS, zeroDraw.data(), "veg drawArgs_vegetation");
        drawArgs_billboards = ew::Buffer::createStructured(sizeof(t_DrawArguments), numRenderViews,
            BIND_UNORDERED_ACCESS | BIND_INDIRECT_DRAW_ARGS, zeroDraw.data(), "veg drawArgs_billboards");
    }

    {
        std::vector<glm::uvec4> zeroSort(1024, glm::uvec4(0));
        buffer_gpuSort = ew::Buffer::createStructured(sizeof(glm::uvec4), 1024,
            BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS, zeroSort.data(), "veg buffer_gpuSort");

        vegetation_feedback zeroFeedback = {};
        buffer_feedback = ew::Buffer::createStructured(sizeof(vegetation_feedback), 1,
            BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS, &zeroFeedback, "veg buffer_feedback");
        buffer_feedback_read = ew::ReadbackBuffer::create(sizeof(vegetation_feedback), "veg buffer_feedback_read");
    }


    compute_clearBuffers.load("hlsl/terrain/compute_vegetation_clear.hlsl");
    compute_clearBuffers.setBuffer("DrawArgs_Quads", drawArgs_billboards);
    compute_clearBuffers.setBuffer("DrawArgs_Plants", drawArgs_vegetation);
    compute_clearBuffers.setBuffer("feedback_Veg", buffer_feedback);

    compute_calulate_lod.load("hlsl/terrain/compute_vegetation_lod.hlsl");
    compute_calulate_lod.setBuffer("DrawArgs_Quads", drawArgs_billboards);
    compute_calulate_lod.setBuffer("DrawArgs_Plants", drawArgs_vegetation);
    compute_calulate_lod.setBuffer("plant_buffer", plantData);
    compute_calulate_lod.setBuffer("instance_buffer", instanceData);
    compute_calulate_lod.setBuffer("instance_buffer_billboard", instanceData_Billboards);
    compute_calulate_lod.setBuffer("block_buffer", blockData_preSort);
    compute_calulate_lod.setBuffer("feedback", buffer_feedback);
    compute_calulate_lod.setBuffer("sort", buffer_gpuSort);


    compute_sortCombine.add("_PRE", "");
    compute_sortCombine.load("hlsl/terrain/compute_vegetation_sortCombine.hlsl");
    compute_sortCombine.setBuffer("DrawArgs_Plants", drawArgs_vegetation);
    compute_sortCombine.setBuffer("pre_block_buffer", blockData_preSort);
    compute_sortCombine.setBuffer("post_block_buffer", blockData);
    compute_sortCombine.setBuffer("feedback", buffer_feedback);
    compute_sortCombine.setBuffer("sort", buffer_gpuSort);


    compute_sortCombine_POST.load("hlsl/terrain/compute_vegetation_sortCombine.hlsl");
    compute_sortCombine_POST.setBuffer("DrawArgs_Plants", drawArgs_vegetation);
    compute_sortCombine_POST.setBuffer("pre_block_buffer", blockData_preSort);
    compute_sortCombine_POST.setBuffer("post_block_buffer", blockData);
    compute_sortCombine_POST.setBuffer("feedback", buffer_feedback);
    compute_sortCombine_POST.setBuffer("sort", buffer_gpuSort);



    {
        rgb_data = ew::Buffer::createStructured(sizeof(glm::uvec4), 256 * 128,
            BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS, nullptr, "veg rgb_data");

        compute_sampleRGBtoPixel.load("hlsl/terrain/compute_sampleRGBtoPixel.hlsl");
        compute_sampleRGBtoPixel.setTexture("gIn", rgbFbo->getColorTexture(0));
        compute_sampleRGBtoPixel.setTexture("gOut", RGB_MAP);
        compute_sampleRGBtoPixel.setBuffer("data", rgb_data);

        compute_sampleRGBtoPixel_ToTexture.add("_TO_TEXTURE", "");
        compute_sampleRGBtoPixel_ToTexture.load("hlsl/terrain/compute_sampleRGBtoPixel.hlsl");
        compute_sampleRGBtoPixel_ToTexture.setTexture("gIn", rgbFbo->getColorTexture(0));
        compute_sampleRGBtoPixel_ToTexture.setTexture("gOut", RGB_MAP);
        compute_sampleRGBtoPixel_ToTexture.setBuffer("data", rgb_data);
    }

    builInstanceBuffer();

    {
        Diligent::SamplerDesc samplerDesc;
        samplerDesc.MinFilter = Diligent::FILTER_TYPE_ANISOTROPIC;
        samplerDesc.MagFilter = Diligent::FILTER_TYPE_ANISOTROPIC;
        samplerDesc.MipFilter = Diligent::FILTER_TYPE_ANISOTROPIC;
        samplerDesc.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressW = Diligent::TEXTURE_ADDRESS_CLAMP;
        samplerDesc.MaxAnisotropy = 4;
        sampler_ClampAnisotropic = ew::Sampler::create(samplerDesc);

        // Clamp U / Wrap V is REQUIRED by the continuous stem-V scheme, and
        // aniso must stay 1.
        samplerDesc.MinFilter = Diligent::FILTER_TYPE_LINEAR;
        samplerDesc.MagFilter = Diligent::FILTER_TYPE_LINEAR;
        samplerDesc.MipFilter = Diligent::FILTER_TYPE_LINEAR;
        samplerDesc.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressV = Diligent::TEXTURE_ADDRESS_WRAP;
        samplerDesc.AddressW = Diligent::TEXTURE_ADDRESS_WRAP;
        samplerDesc.MaxAnisotropy = 1;
        sampler_Ribbons = ew::Sampler::create(samplerDesc);

        Diligent::SamplerDesc depthDesc;
        depthDesc.MinFilter = Diligent::FILTER_TYPE_COMPARISON_LINEAR;
        depthDesc.MagFilter = Diligent::FILTER_TYPE_COMPARISON_LINEAR;
        depthDesc.MipFilter = Diligent::FILTER_TYPE_COMPARISON_LINEAR;
        depthDesc.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
        depthDesc.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
        depthDesc.AddressW = Diligent::TEXTURE_ADDRESS_CLAMP;
        depthDesc.MaxAnisotropy = 1;
        depthDesc.ComparisonFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL;
        sampler_Depth = ew::Sampler::create(depthDesc);
    }


    vegetationShader_RGB_SAMPLE.add("_RGB_SAMPLE", "");
    vegetationShader_RGB_SAMPLE.load("hlsl/terrain/render_vegetation_ribbons.hlsl", "vsMain", "psMain", ew::Topology::LineStrip, "gsMain");
    vegetationShader_RGB_SAMPLE.setBuffer("plant_buffer", plantData);
    vegetationShader_RGB_SAMPLE.setBuffer("plant_pivot_buffer", plantpivotData);
    vegetationShader_RGB_SAMPLE.setBuffer("instance_buffer", instanceData);
    vegetationShader_RGB_SAMPLE.setBuffer("block_buffer", blockData);
    vegetationShader_RGB_SAMPLE.setBuffer("vertex_buffer", vertexData);
    vegetationShader_RGB_SAMPLE.setBuffer("sort", buffer_gpuSort);
    vegetationShader_RGB_SAMPLE.setBuffer("feedback_Veg", buffer_feedback);
    vegetationShader_RGB_SAMPLE.setBuffer("materials", _plantMaterial::static_materials_veg.sb_vegetation_Materials);
    vegetationShader_RGB_SAMPLE.setSampler("gSmpLinear", sampler_Ribbons);              // fixme only cvlamlX
    vegetationShader_RGB_SAMPLE.setSampler("gSamplerDepth", sampler_Depth);
    vegetationShader_RGB_SAMPLE.setSampler("gSmpLinearClamp", sampler_ClampAnisotropic);              // fixme only cvlamlX
    vegetationShader_RGB_SAMPLE.setTexture("highResShadow", shadowFbo->getDepthStencilTexture());
    _plantMaterial::static_materials_veg.setTextures(vegetationShader_RGB_SAMPLE);

    vegetationShader_DEPTH.add("_DEPTH", "");
    vegetationShader_DEPTH.load("hlsl/terrain/render_vegetation_ribbons.hlsl", "vsMain", "psMain", ew::Topology::LineStrip, "gsMain");
    vegetationShader_DEPTH.setBuffer("plant_buffer", plantData);
    vegetationShader_DEPTH.setBuffer("plant_pivot_buffer", plantpivotData);
    vegetationShader_DEPTH.setBuffer("instance_buffer", instanceData);
    vegetationShader_DEPTH.setBuffer("block_buffer", blockData);
    vegetationShader_DEPTH.setBuffer("vertex_buffer", vertexData);
    vegetationShader_DEPTH.setBuffer("sort", buffer_gpuSort);
    vegetationShader_DEPTH.setBuffer("feedback_Veg", buffer_feedback);
    vegetationShader_DEPTH.setBuffer("materials", _plantMaterial::static_materials_veg.sb_vegetation_Materials);
    vegetationShader_DEPTH.setSampler("gSmpLinear", sampler_Ribbons);              // fixme only cvlamlX
    vegetationShader_DEPTH.setSampler("gSmpLinearClamp", sampler_ClampAnisotropic);              // fixme only cvlamlX
    _plantMaterial::static_materials_veg.setTextures(vegetationShader_DEPTH);



    billboardShader.add("_BILLBOARD", "");
    billboardShader.load("hlsl/terrain/render_vegetation_ribbons.hlsl", "vsMain", "psMain", ew::Topology::PointList, "gsMain");
    billboardShader.setBuffer("plant_buffer", plantData);
    billboardShader.setBuffer("plant_pivot_buffer", plantpivotData);
    billboardShader.setBuffer("instance_buffer", instanceData_Billboards);
    billboardShader.setBuffer("block_buffer", blockData);
    billboardShader.setBuffer("vertex_buffer", vertexData);
    billboardShader.setBuffer("sort", buffer_gpuSort);
    billboardShader.setBuffer("materials", _plantMaterial::static_materials_veg.sb_vegetation_Materials);
    billboardShader.setSampler("gSmpLinear", sampler_Ribbons);              // fixme only cvlamlX
    billboardShader.setSampler("gSamplerDepth", sampler_Depth);
    billboardShader.setSampler("gSmpLinearClamp", sampler_ClampAnisotropic);              // fixme only cvlamlX
    billboardShader.setTexture("highResShadow", shadowFbo->getDepthStencilTexture());
    _plantMaterial::static_materials_veg.setTextures(billboardShader);

    bakeShader.add("_BAKE", "");
    bakeShader.load("hlsl/terrain/render_vegetation_ribbons.hlsl", "vsMain", "psMain", ew::Topology::LineStrip, "gsMain");
    bakeShader.setBuffer("plant_buffer", plantData);
    bakeShader.setBuffer("plant_pivot_buffer", plantpivotData);
    bakeShader.setBuffer("instance_buffer", instanceData);
    bakeShader.setBuffer("block_buffer", blockData);
    bakeShader.setBuffer("vertex_buffer", vertexData);
    bakeShader.setBuffer("sort", buffer_gpuSort);
    bakeShader.setBuffer("feedback_Veg", buffer_feedback);
    bakeShader.setBuffer("materials", _plantMaterial::static_materials_veg.sb_vegetation_Materials);
    bakeShader.setSampler("gSmpLinear", sampler_Ribbons);              // fixme only cvlamlX
    bakeShader.setSampler("gSamplerDepth", sampler_Depth);
    bakeShader.setSampler("gSmpLinearClamp", sampler_ClampAnisotropic);
    bakeShader.setTexture("highResShadow", shadowFbo->getDepthStencilTexture());
    _plantMaterial::static_materials_veg.setTextures(bakeShader);

    reloadShader();

    rasterstate = Diligent::RasterizerStateDesc{};
    rasterstate.FillMode = Diligent::FILL_MODE_SOLID;
    rasterstate.CullMode = Diligent::CULL_MODE_NONE;

    // One/Zero, i.e. no ROP blending at all. That is this renderer's whole
    // opacity strategy: alpha clip, front-to-back z-binning and
    // SV_DepthGreaterEqual do the work. Do NOT "fix" it into conventional
    // alpha blending.
    blendstate = Diligent::BlendStateDesc{};
    blendstate.RenderTargets[0].BlendEnable = Diligent::True;
    blendstate.RenderTargets[0].BlendOp = Diligent::BLEND_OPERATION_ADD;
    blendstate.RenderTargets[0].BlendOpAlpha = Diligent::BLEND_OPERATION_ADD;
    blendstate.RenderTargets[0].SrcBlend = Diligent::BLEND_FACTOR_ONE;
    blendstate.RenderTargets[0].DestBlend = Diligent::BLEND_FACTOR_ZERO;
    blendstate.RenderTargets[0].SrcBlendAlpha = Diligent::BLEND_FACTOR_ZERO;
    blendstate.RenderTargets[0].DestBlendAlpha = Diligent::BLEND_FACTOR_ZERO;

    blendstate_withAlpha = blendstate;
    blendstate_withAlpha.AlphaToCoverageEnable = Diligent::True;

    blendstateBake = Diligent::BlendStateDesc{};
    blendstateBake.AlphaToCoverageEnable = Diligent::False;
    blendstateBake.IndependentBlendEnable = Diligent::True;
    for (int i = 0; i < 8; i++)
    {
        // clear all
        blendstateBake.RenderTargets[i].RenderTargetWriteMask = Diligent::COLOR_MASK_ALL;
        blendstateBake.RenderTargets[i].BlendEnable = Diligent::True;
        blendstateBake.RenderTargets[i].BlendOp = Diligent::BLEND_OPERATION_ADD;
        blendstateBake.RenderTargets[i].BlendOpAlpha = Diligent::BLEND_OPERATION_ADD;
        blendstateBake.RenderTargets[i].SrcBlend = Diligent::BLEND_FACTOR_SRC_ALPHA;
        blendstateBake.RenderTargets[i].DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
        blendstateBake.RenderTargets[i].SrcBlendAlpha = Diligent::BLEND_FACTOR_SRC_ALPHA_SAT;
        blendstateBake.RenderTargets[i].DestBlendAlpha = Diligent::BLEND_FACTOR_ONE;
    }
    blendstateBake.RenderTargets[0].SrcBlend = Diligent::BLEND_FACTOR_ONE;
    blendstateBake.RenderTargets[0].DestBlend = Diligent::BLEND_FACTOR_ZERO;



    compute_bakeFloodfill.load("hlsl/terrain/compute_bakeFloodfill.hlsl");

    billboardShader.setRasterizerState(rasterstate);
    billboardShader.setBlendState(blendstate);

    // Perlin lookup buffer
    const siv::PerlinNoise perlin{ (uint)101 };
    float sum = 0;
    for (int i = 0; i < 1024; i++)
    {
        perlinData[i] = (float)perlin.normalizedOctave1D((float)i / 8.f, 4, 0.5);
        sum += perlinData[i];
    }

    sum /= 1024.f;
    for (int i = 0; i < 1024; i++)
    {
        perlinData[i] -= sum;
    }


}






void _rootPlant::buildFullResolution()
{
    if (root)
    {
        _ribbonBuilder.clear(); // why every ti,e just before a build - AH its to acommodate buildAll maybe
        settings.pixelSize = 0.00005f;
        settings.exclusionCylinder = { 0, 0 };
        build();   // to generate new extents
        root->calculate_extents(bakeViewAdjusted);
        displayModeSinglePlant = true;
        anyChange = true;
        currentLOD = -1;
        reloadShader();

        for (uint lod = 0; lod < 100; lod++)
        {
            levelOfDetail* lodInfo = selectedPart->getLodInfo(lod);
            (void)lodInfo;
        }
    }
}





// Nothing calls this yet. It is a GUI function, but the body is also the only
// copy of the lod bake/build orchestration, and that orchestration mutates
// persistent build state - settings, lodBake, files on disk.
void _rootPlant::renderGui_Lodding()
{
    float columnWidth = ImGui::GetWindowWidth() - 10;
    int flags = ImGuiTreeNodeFlags_Framed;// | ImGuiTreeNodeFlags_OpenOnArrow; //ImGuiTreeNodeFlags_DefaultOpen |
    auto& style = ImGui::GetStyle();

    style.Colors[ImGuiCol_Header] = ImVec4(0.01f, 0.01f, 0.06f, 1.f);
    ImVec4 highlight = ImVec4(0.16f, 0.05f, 0.0f, 1.0f);
    ImVec4 normal = ImVec4(0.01f, 0.01f, 0.01f, 1.0f);
    float lineHeight = ImGui::GetFontSize() * 1.5f;

    if (ImGui::TreeNodeEx("LOD", flags))
    {
        if (selectedPart)
        {
            style.Colors[ImGuiCol_Button] = ImVec4(0.01f, 0.01f, 0.01f, 1.f);
            if (ImGui::Button("Build all lods", ImVec2(columnWidth - 25, 40)))  buildAllLods();


            for (uint lod = 0; lod < 100; lod++)
            {
                levelOfDetail* lodInfo = selectedPart->getLodInfo(lod);
                if (lodInfo)
                {
                    //if (lod > 0)
                    {
                        ImGui::PushID(lod);
                        {
                            ImGui::NewLine();
                            ImGui::SameLine(100, 0);
                            ImGui::SetNextItemWidth(100);
                            if (ImGui::DragInt("##numPix", &(lodInfo->numPixels), 0.5f, 20, 1000, "%d pixels"))
                            {
                                selectedPart->changed = anyChange = true;
                            }
                            TOOLTIP("pixel height of crossover");

                            ImGui::SameLine(0, 20);
                            ImGui::SetNextItemWidth(100);
                            if (ImGui::DragFloat("##pixelSize-mm", &(lodInfo->pixelSize), 0.05f, 0.5f, 500.f, "%2.1fmm", ImGuiSliderFlags_Logarithmic))
                            {
                                settings.pixelSize = lodInfo->pixelSize * 0.001f;
                                selectedPart->changed = anyChange = true;
                            }
                            TOOLTIP("build detail");
                        }
                        ImGui::PopID();
                    }



                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ((uint)currentLOD == lod) ? highlight : normal);
                    ImGui::BeginChildFrame(5678 + lod, ImVec2(columnWidth - 25, lineHeight * 2));
                    {
                        if (ImGui::BeginPopupContextWindow(nullptr))
                        {
                            if (ImGui::Selectable("delete")) { selectedPart->deleteLod(lod); }
                            if (ImGui::Selectable("insert - before")) { selectedPart->insertLod(lod); }
                            ImGui::EndPopup();
                        }

                        {
                            ImGui::Text("%d)", lod);
                        }

                        ImGui::SameLine(100, 0);
                        ImGui::Text("%d: verts", lodInfo->numVerts);

                        {
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.1f, 0.1f, 1.0f));
                            ImGui::SameLine(columnWidth - 25 - 80, 0);
                            //if (ImGui::Button("build", ImVec2(60, 0)));
                            if (ImGui::Button("build", ImVec2(80, 0)))
                            {
                                displayModeSinglePlant = true;
                                settings.pixelSize = lodInfo->pixelSize * 0.001f * 0.95f;       // Need to scale it donw for rounmding reasons in float
                                settings.exclusionCylinder = { 0, 0 };
                                if (lodInfo->bakeType > BAKE_NONE)
                                {
                                    settings.exclusionCylinder = root->getBakeInfo(lodInfo->bakeIndex)->alphaOval;
                                }
                                currentLOD = lod;
                                _ribbonBuilder.clear();
                                build();
                                lodInfo->numVerts = (int)_ribbonBuilder.numVerts();
                                reloadShader();
                            }

                            ImGui::PopStyleColor();
                        }


                        // middle line
                        ImGui::NewLine();
                        ImGui::SameLine(40, 0);
                        ImGui::SetNextItemWidth(100);
                        if (ImGui::Checkbox("3d", &lodInfo->useGeometry)) { selectedPart->changed = true; }
                        TOOLTIP("Use 3d geometry and bakes, \nor otherwise only baked billboards")

                            ImGui::SameLine(0, 20);
                        ImGui::SetNextItemWidth(100);
                        if (ImGui::Combo("##type", &lodInfo->bakeType, "none\0diamond\0rectangle\0'6 tri'\0'12 tri'\0")) { selectedPart->changed = true; }
                        TOOLTIP("none - do not use any baked information \ndiamond - 2 triangles in diamond pattern.\n'6 tri' 6 triangle ribbon \n'12 tri' 12 triangle ribbon")

                            if (lodInfo->bakeType > 0)
                            {
                                ImGui::SameLine(0, 10);
                                ImGui::SetNextItemWidth(100);
                                if (ImGui::Combo("##bakenum", &lodInfo->bakeIndex, "bake-0\0bake-1\0bake-2\0")) { selectedPart->changed = true; }
                            }

                    }
                    ImGui::EndChildFrame();
                    ImGui::PopStyleColor();
                }
            }

            style.Colors[ImGuiCol_Button] = ImVec4(0.01f, 0.01f, 0.01f, 1.f);
            if (ImGui::Button("Build full", ImVec2(columnWidth - 25, 40)))
            {
                buildFullResolution();
            }


        }
        ImGui::TreePop();
    }
}





void _rootPlant::setBakeView(float3 _r, float3 _u, float3 _d)
{
    bakeViewAdjusted[0][0] = _r.x;
    bakeViewAdjusted[0][1] = _r.y;
    bakeViewAdjusted[0][2] = _r.z;

    bakeViewAdjusted[1][0] = _u.x;
    bakeViewAdjusted[1][1] = _u.y;
    bakeViewAdjusted[1][2] = _u.z;

    bakeViewAdjusted[2][0] = _d.x;
    bakeViewAdjusted[2][1] = _d.y;
    bakeViewAdjusted[2][2] = _d.z;
}


// Set up or delete the baking directories
// ??? FIXME this should delete unused bakes but doenst , likely because file is open
void _rootPlant::bakeDirectories()
{
    std::filesystem::path PT = selectedPart->path;
    std::string resource = terrafectorEditorMaterial::rootFolder;

    std::string newDir = "\"" + resource + PT.parent_path().string() + "\\bake_" + PT.stem().string() + "\"";
    replaceAllVEG(newDir, "/", "\\");
    std::string delCmd = "rmdir /S /Q " + newDir;
    system(delCmd.c_str());

    std::string makeCmd = "mkdir " + newDir;
    system(makeCmd.c_str());
}



// Nothing calls this yet. As with renderGui_Lodding the body is the only copy
// of an orchestration worth keeping: the billboard bake, with its backwards
// slot loop and the seed 100/101/102 triple bake of slot 0.
void _rootPlant::renderGui_Baking()
{
    if (!selectedPart) return;

    uint gui_id = 199994;
    float columnWidth = ImGui::GetWindowWidth() - 10;
    int flags = ImGuiTreeNodeFlags_Framed;
    auto& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Header] = ImVec4(0.06f, 0.01f, 0.01f, 1.f);

    if (ImGui::TreeNodeEx("Baking", flags))
    {
        {
            style.Colors[ImGuiCol_Button] = ImVec4(0.01f, 0.01f, 0.01f, 1.f);
            if (ImGui::Button("Bake materials", ImVec2(columnWidth - 25, 40)))
            {
                bakeDirectories();
                settings.isBaking = true;


                for (int i = 10; i >= 0; i--)    // backwards so the last PNG file is of lod-0
                {
                    auto bakeLod = selectedPart->getBakeInfo(i);

                    if (bakeLod && bakeLod->pixHeight > 0)
                    {
                        settings.includeTip = bakeLod->includeTip;  // remmeber to tuen off one level deep
                        settings.excludeDead = bakeLod->clipDead;
                        _ribbonBuilder.clear();
                        buildFullResolution();


                        glm::mat4 tip = selectedPart->getTip(bakeLod->includeTip);
                        glm::mat4 rootM = selectedPart->getRoot(bakeLod->clipDead);
                        bakeViewAdjusted = bakeViewMatrix = rootM;
                        float3 u = glm::normalize((float3)tip[3] - (float3)rootM[3]);
                        float3 d = glm::normalize(glm::cross((float3)bakeViewAdjusted[0], u));
                        float3 r = glm::normalize(glm::cross(u, d));
                        setBakeView(r, u, d);
                        ROLL(bakeViewAdjusted, bakeLod->yaw);
                        PITCH(bakeViewAdjusted, -bakeLod->pitch);

                        selectedPart->calculate_extents(bakeViewAdjusted);


                        if (i == 0)
                        {
                            std::filesystem::path full_path = selectedPart->path;

                            settings.seed = 100;

                            _ribbonBuilder.clear();
                            buildFullResolution();
                            bakeLod->material.name = std::string("bake_0_") + std::to_string(settings.seed);
                            bakeLod->material.path = full_path.parent_path().string() + "\\bake_" + full_path.stem().string() + "\\" + bakeLod->material.name + ".vegetationMaterial";
                            bake(selectedPart->path, std::to_string(settings.seed), bakeLod, bakeViewAdjusted, true);
                            selectedPart->getBakeInfo(i)->material.reload();

                            settings.seed = 101;

                            _ribbonBuilder.clear();
                            buildFullResolution();
                            bakeLod->material.name = std::string("bake_0_") + std::to_string(settings.seed);
                            bakeLod->material.path = full_path.parent_path().string() + "\\bake_" + full_path.stem().string() + "\\" + bakeLod->material.name + ".vegetationMaterial";
                            bake(selectedPart->path, std::to_string(settings.seed), bakeLod, bakeViewAdjusted, true);

                            settings.seed = 102;

                            _ribbonBuilder.clear();
                            buildFullResolution();
                            bakeLod->material.name = std::string("bake_0_") + std::to_string(settings.seed);
                            bakeLod->material.path = full_path.parent_path().string() + "\\bake_" + full_path.stem().string() + "\\" + bakeLod->material.name + ".vegetationMaterial";
                            bake(selectedPart->path, std::to_string(settings.seed), bakeLod, bakeViewAdjusted, true);
                        }
                        else
                        {
                            bake(selectedPart->path, std::to_string(settings.seed), bakeLod, bakeViewAdjusted);
                            bakeLod->material.reload();
                        }
                        selectedPart->changed = true;
                    }
                }

                settings.seed = 100;
                settings.excludeDead = false;
                settings.includeTip = true;
                _ribbonBuilder.clear();
                buildFullResolution();

                settings.isBaking = false;

                selectedPart->savePath();
                selectedPart->changedForSave = false;
            }

            ImVec4 highlight = ImVec4(0.16f, 0.05f, 0.0f, 1.0f);
            ImVec4 normal = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);

            for (int i = 0; i < 10; i++)
            {
                lodBake* bake = selectedPart->getBakeInfo(i);
                if (bake)
                {
                    bool isForbaking = bake->pixHeight > 0;
                    float lineHeight = ImGui::GetFontSize() * 1.5f * (1 + isForbaking * 12);
                    float columnWidth2 = ImGui::GetWindowWidth() - 10;
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, (i == showBake) ? highlight : normal);
                    ImGui::BeginChildFrame(15678 + i, ImVec2(columnWidth2 - 25, lineHeight));
                    {
                        bool changed = false;

                        std::string name = "bake - " + std::to_string(i);
                        changed |= ImGui::Checkbox(name.c_str(), &isForbaking);
                        if (isForbaking && bake->pixHeight == 0)          bake->pixHeight = 32;
                        if (!isForbaking)                                 bake->pixHeight = 0;

                        if (isForbaking)
                        {
                            R_INT("height", bake->pixHeight, 0, 512, "");
                            R_FLOAT("width", bake->bakeWidth, 0.01f, 0.1f, 10.f, "");
                            R_FLOAT_EX("bottom / top", bake->bake_V, 0.01f, 0.1f, 10.f, "", "");

                            ImGui::SetNextItemWidth(130);
                            if (ImGui::Checkbox("include tip", &bake->includeTip)) changed = true;;
                            ImGui::SetNextItemWidth(130);
                            if (ImGui::Checkbox("remove dead", &bake->clipDead)) changed = true;;

                            // Use normal ImGui becauseteh should not result in changed
                            ImGui::DragFloat("pitch", &bake->pitch, 0.01f, 0.0f, 1.5f);
                            ImGui::DragFloat("yaw", &bake->yaw, 0.01f, 0.0f, 6.4f);

                            ImGui::NewLine();
                            R_FLOAT("translucency", bake->translucency, 0.01f, 0.1f, 10.f, "");
                            R_FLOAT("alpha", bake->alphaPow, 0.01f, 0.1f, 10.f, "");
                            CHECKBOX("faceCamera", &bake->faceCamera, "");
                            ImGui::SetNextItemWidth(130);
                            ImGui::Checkbox("bake AO", &bake->bakeAOToAlbedo);
                            ImGui::SetNextItemWidth(130);
                            ImGui::Checkbox("bake alpha", &bake->useAlphaInBake);

                        }

                        selectedPart->changed |= changed;
                    }
                    ImGui::EndChildFrame();
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemClicked(0)) showBake = i;
                }
            }

        }
        ImGui::TreePop();
    }
}




// The texture-tool cluster - initTextureTool, GenerateATexture, exportTextures
// and the largeTexture/oneTexture data behind them - is authoring-only and has
// no caller: the GUI that drove it is not implemented.
void _rootPlant::initTextureTool()
{
    static bool first = true;
    if (first)
    {
        //???LineStrip
        textureExtractShader.load("hlsl/terrain/extractTextures.hlsl", "vsMain", "psMain", ew::Topology::PointList, "gsMain");
        textureExtractShader.setSampler("gSmpLinear", sampler_Ribbons);              // fixme only cvlamlX

        first = false;
    }
}

void _rootPlant::GenerateATexture(uint _idx, bool toSRGB)
{
    initTextureTool();

    auto& M = textureToolData.maps[_idx];
    int mm = (int)pow(2, M.numMips);
    int w = M.texWidth * 4 * mm;
    int h = M.texHeight * 4 * mm;
    textureToolData.w = w;
    textureToolData.h = h;

    if (!textureToolData.fbo || (int)textureToolData.fbo->getWidth() != w || (int)textureToolData.fbo->getHeight() != h)
    {
        const Diligent::BIND_FLAGS rtFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS;
        textureToolData.fbo = ew::Fbo::create();
        // The 1, 4 is arraySize 1 and FOUR MIP LEVELS - exportTextures captures
        // mips. Not MSAA.
        textureToolData.fbo->attachColorTarget(ew::Texture::create2D(w, h, Diligent::TEX_FORMAT_RGBA8_UNORM, 1, 4, nullptr, rtFlags, "texTool albedo"), 0);		// albedo
        textureToolData.fbo->attachColorTarget(ew::Texture::create2D(w, h, Diligent::TEX_FORMAT_RGBA8_UNORM, 1, 4, nullptr, rtFlags, "texTool normal_8"), 1);		// normal_8
        textureToolData.fbo->attachColorTarget(ew::Texture::create2D(w, h, Diligent::TEX_FORMAT_RGBA8_UNORM, 1, 4, nullptr, rtFlags, "texTool translucnecy"), 2);	    // translucnecy
        textureToolData.fbo->attachColorTarget(ew::Texture::create2D(w, h, Diligent::TEX_FORMAT_RGBA8_UNORM, 1, 4, nullptr, rtFlags, "texTool extra"), 3);	    // extra
        textureToolData.fbo->attachColorTarget(ew::Texture::create2D(w, h, Diligent::TEX_FORMAT_RGBA8_UNORM, 1, 4, nullptr, rtFlags, "texTool 45degree"), 4);	    // 45degree lit
        textureToolData.fbo->attachDepthStencilTarget(ew::Texture::create2D(w, h, Diligent::TEX_FORMAT_D24_UNORM_S8_UINT, 1, 1, nullptr, BIND_DEPTH_STENCIL, "texTool depth"));			// keep for now, not sure why, but maybe usefult for cuts
    }

    const glm::vec4 clearColor(0.5, 0.5f, 1.0f, 0.0f);
    renderInfo.context->clearFbo(textureToolData.fbo.get(), clearColor, 1.0f, 0, ew::FboAttachmentType::All);    // depth
    renderInfo.context->clearRtv(textureToolData.fbo->getRenderTargetView(0), glm::vec4(0.0, 0.0f, 0.0f, 0.0f));
    renderInfo.context->clearRtv(textureToolData.fbo->getRenderTargetView(1), glm::vec4(0.5, 0.5f, 1.0f, 0.0f));
    renderInfo.context->clearRtv(textureToolData.fbo->getRenderTargetView(2), glm::vec4(0.0, 0.0f, 0.0f, 0.0f));
    renderInfo.context->clearRtv(textureToolData.fbo->getRenderTargetView(3), glm::vec4(0.0, 0.0f, 0.0f, 0.0f));
    renderInfo.context->clearRtv(textureToolData.fbo->getRenderTargetView(4), glm::vec4(0.0, 0.0f, 0.0f, 0.0f));

    float2 dir = glm::normalize(M.stop - M.start);
    float2 norm = float2(dir.y, -dir.x);

    float2 A = M.start + norm * M.width;
    float2 B = M.start - norm * M.width;
    float2 C = M.stop - norm * M.width;
    float2 D = M.stop + norm * M.width;

    //bakeShader
    textureExtractShader.setFbo(textureToolData.fbo);
    {
        Diligent::Viewport vp;
        vp.TopLeftX = 0; vp.TopLeftY = 0;
        vp.Width = (float)w; vp.Height = (float)h;
        vp.MinDepth = 0; vp.MaxDepth = 1;
        textureExtractShader.setViewport(vp);
    }
    textureExtractShader.setRasterizerState(rasterstate);
    textureExtractShader.setBlendState(blendstateBake);

    textureExtractShader.setVariable("gConstantBuffer", "flipRed", (int)textureToolData.flipRed);
    textureExtractShader.setVariable("gConstantBuffer", "flipGreen", (int)textureToolData.flipGreen);
    textureExtractShader.setVariable("gConstantBuffer", "nStrength", textureToolData.normalStrenth);
    textureExtractShader.setVariable("gConstantBuffer", "toSRGB", (int)toSRGB);



    textureExtractShader.setVariable("gConstantBuffer", "A", A);
    textureExtractShader.setVariable("gConstantBuffer", "B", B);
    textureExtractShader.setVariable("gConstantBuffer", "C", C);
    textureExtractShader.setVariable("gConstantBuffer", "D", D);

    textureExtractShader.setVariable("gConstantBuffer", "start", M.start);
    textureExtractShader.setVariable("gConstantBuffer", "stop", M.stop);
    textureExtractShader.setVariable("gConstantBuffer", "bezier", M.bezier);
    textureExtractShader.setVariable("gConstantBuffer", "width", M.width);

    textureExtractShader.setTexture("galbedo", textureToolData.tex_albedo);
    textureExtractShader.setTexture("galpha", textureToolData.tex_alpha);
    textureExtractShader.setTexture("gnormal", textureToolData.tex_normal);
    textureExtractShader.setTexture("gtranslucency", textureToolData.tex_translucency);

    textureExtractShader.drawInstanced(renderInfo.context, 1, 1);
}

void replaceAllveg(std::string& str, const std::string& from, const std::string& to) {
    if (from.empty())
        return;
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // In case 'to' contains 'from', like replacing 'x' with 'yx'
    }
}


void _rootPlant::exportTextures()
{
    if (textureToolData.changed)
    {
        std::ofstream os(terrafectorEditorMaterial::rootFolder + textureToolData.path);
        cereal::JSONOutputArchive archive(os);
        archive(textureToolData);
        textureToolData.changed = false;
    }


    uint idx = 0;
    for (auto& M : textureToolData.maps)
    {
        (void)M;
        GenerateATexture(idx, true);

        captureToFileStub("textureTool albedo", terrafectorEditorMaterial::rootFolder + textureToolData.path + "_" + std::to_string(idx) + "_albedo.png");
        if (textureToolData.tex_normal)
            captureToFileStub("textureTool normal", terrafectorEditorMaterial::rootFolder + textureToolData.path + "_" + std::to_string(idx) + "_normal.png");
        if (textureToolData.tex_translucency)
            captureToFileStub("textureTool translucency", terrafectorEditorMaterial::rootFolder + textureToolData.path + "_" + std::to_string(idx) + "_translucency.png");

        idx++;
    }

}
















void _rootPlant::buildAllLods()
{
    _ribbonBuilder.clearPivot();
    LOGTHEBUILD = true;
    if (LOGTHEBUILD)
    {
        spdlog::info("vegetation: buildAllLods()");
    }

    displayModeSinglePlant = false;

    uint start = 0;

    _ribbonBuilder.clear();

    numBinaryPlants = 3;
    std::vector<_plant_anim_pivot>   allPivots;
    allPivots.resize(256 * numBinaryPlants);

    uint startBlock[16][16];
    uint numBlocks[16][16];
    for (int pIndex = 0; pIndex < numBinaryPlants; pIndex++)
    {
        spdlog::info("vegetation: pIndex={}", pIndex);
        plantBuf[pIndex].radiusScale = ribbonVertex::radiusScale;
        plantBuf[pIndex].scale = ribbonVertex::objectScale;
        plantBuf[pIndex].offset = ribbonVertex::objectOffset;
        plantBuf[pIndex].Ao_depthScale = 0.3f;  // FIXME unused
        plantBuf[pIndex].bDepth = 1;
        plantBuf[pIndex].bScale = 1;
        plantBuf[pIndex].sunTilt = -0.2f;
        plantBuf[pIndex].size = extents;
        plantBuf[pIndex].shadowUVScale = root->shadowUVScale;
        plantBuf[pIndex].shadowSoftness = root->shadowSoftness;

        plantBuf[pIndex].rootPivot.root = { 0, 0, 0 };
        plantBuf[pIndex].rootPivot.extent = { 0, 1.f / extents.y, 0 };
        plantBuf[pIndex].rootPivot.frequency = root->rootFrequency() * sqrt(extents.y);
        plantBuf[pIndex].rootPivot.stiffness = root->ossilation_stiffness;
        plantBuf[pIndex].rootPivot.shift = root->ossilation_power;

        lodBake* lodZero = root->getBakeInfo(0);
        if (lodZero)
        {
            plantBuf[pIndex].billboardMaterialIndex = lodZero->material.index;
        }

        plantBuf[pIndex].numLods = 0;
        for (uint lod = 100; lod >= 1; lod--)   // backwards since it fixes a bug in build() ??? is it still tehere
        {
            levelOfDetail* lodInfo = root->getLodInfo(lod);
            if (lodInfo)
            {
                settings.pixelSize = lodInfo->pixelSize * 0.001f;
                settings.seed = 1000 + pIndex;
                build(pIndex * 256 * sizeof(_plant_anim_pivot));
                lodInfo->numVerts = (int)_ribbonBuilder.numVerts();
                lodInfo->numBlocks = _ribbonBuilder.numPacked() / VEG_BLOCK_SIZE - start;
                lodInfo->unused = _ribbonBuilder.numPacked() - _ribbonBuilder.numVerts();
                lodInfo->startBlock = start;

                // Only the pivots that exist get copied; allPivots is
                // value-initialized, so the rest of the 256-slot window stays
                // zero.
                int numPvt = __min(256, _ribbonBuilder.numPivots());
                for (int pvt = 0; pvt < numPvt; pvt++)
                {
                    allPivots[pIndex * 256 + pvt] = _ribbonBuilder.pivotPoints[pvt];
                }


                startBlock[pIndex][__min(15u, lod)] = start;
                numBlocks[pIndex][__min(15u, lod)] = lodInfo->numBlocks;
                (void)startBlock; (void)numBlocks;

                plantBuf[pIndex].numLods = __max(plantBuf[pIndex].numLods, lod);    // omdat ons nou agteruit gaan
                plantBuf[pIndex].lods[lod - 1].pixSize = (float)lodInfo->numPixels;
                plantBuf[pIndex].lods[lod - 1].numBlocks = lodInfo->numBlocks;
                plantBuf[pIndex].lods[lod - 1].startVertex = start;

                start += lodInfo->numBlocks;

                spdlog::info("vegetation: plant lod : {}, {}mm, {} verts, {} blocks", lod, (float)lodInfo->pixelSize, lodInfo->numVerts, lodInfo->numBlocks);
            }
        }
    }

    // Now log this
    spdlog::info("vegetation: buildAllLods() : {}", root->name);
    spdlog::info("vegetation:   size : {:.2f}, {:.2f}", plantBuf[0].size.x, plantBuf[0].size.y);
    spdlog::info("vegetation:   lod, blocks, startV, pixSize");
    for (uint i = 0; i < plantBuf[0].numLods; i++)
    {
        spdlog::info("vegetation:   {} : {}, {}, {:.2f}", i, plantBuf[0].lods[i].numBlocks, plantBuf[0].lods[i].startVertex, plantBuf[0].lods[i].pixSize);
    }


    plantData->setBlob(plantBuf.data(), 0, 8 * sizeof(plant));

    builInstanceBuffer();
    spdlog::info("vegetation:   just set plants");

    int numV = __min(65536 * 8, (int)_ribbonBuilder.numPacked());
    vertexData->setBlob(_ribbonBuilder.getPackedData(), 0, numV * sizeof(ribbonVertex8));                // FIXME uploads should be smaller
    spdlog::info("vegetation:   just set verts ({}), packed {}, numMaterials {}", numV, (int)_ribbonBuilder.packed.size(), (int)_plantMaterial::static_materials_veg.materialVector.size());
    for (int i = 0; i < (int)_plantMaterial::static_materials_veg.materialVector.size(); i++)
    {
        spdlog::info("vegetation:     material {}, {}", i, _plantMaterial::static_materials_veg.materialVector[i].displayName);
    }

    settings.seed = 1000;


    {
        // NEW VERSION - binary CEREAL
        exportPlant exp;
        int plantCnt = 0;
        for (int p = 0; p < numBinaryPlants; p++)
        {
            (void)p;
            // FIXME VERY VERY BAD _ always loads just one - we HAVE to extend this
            if (root->getBakeInfo(0))
            {
                std::filesystem::path full_path = root->path;
                auto M = root->getBakeInfo(0)->material;
                M.name = std::string("bake_0_") + std::to_string(settings.seed);
                M.path = full_path.parent_path().string() + "\\bake_" + full_path.stem().string() + "\\bake_0_" + std::to_string(100 + plantCnt) + ".vegetationMaterial";
                exp.billboardMaterials.push_back(M);

            }
            else
            {
                exp.billboardMaterials.emplace_back();
                // BIG WARNIGN HERE
            }
            exp.plants.push_back(plantBuf[plantCnt]);
            plantCnt++;
        }

        for (int i = 0; i < numV; i++)
        {
            int idx = (_ribbonBuilder.packed[i].b >> 8) & 0x3ff;
            _vegMaterial M;
            if (idx >= 0 && idx < (int)_plantMaterial::static_materials_veg.materialVector.size())
            {
                M.path = _plantMaterial::static_materials_veg.materialVector[idx].relativePath;
                M.name = _plantMaterial::static_materials_veg.materialVector[idx].displayName;
                M.index = idx;
            }
            else
            {
                M.path = "missing";
                M.name = "missing";
                M.index = 0;
            }
            exp.materials[idx] = M;
        }

        exp.pivots = allPivots;
        exp.vertexbuff = _ribbonBuilder.packed;

        std::string resource = terrafectorEditorMaterial::rootFolder;
        std::ofstream os(resource + root->path + ".earthworksPlant", std::ios::binary);
        cereal::BinaryOutputArchive archive(os);
        archive(exp);

        bake64kplants(root->path);
    }

}





int _rootPlant::importBinary(std::filesystem::path filepath)
{
    if (std::filesystem::exists(filepath))
    {
        for (uint i = 0; i < importPathVector.size(); i++)
        {
            if (importPathVector[i].compare(filepath.string()) == 0)
            {
                return i;
            }
        }
    }
    else
    {
        spdlog::error("vegetation: importBinary - plant binary not found '{}' (plant stays dormant)", filepath.string());
        return -1;
    }
    // its should also say how many variations it has, ort we settle on 3, still say, treesl likely 1 unless yhou caounty werid over years

    exportPlant exp;
    try
    {
        std::ifstream is(filepath, std::ios::binary);
        cereal::BinaryInputArchive archive(is);
        archive(exp);
    }
    catch (const std::exception& e)
    {
        spdlog::error("vegetation: importBinary failed to parse '{}' - {} (plant stays dormant)", filepath.string(), e.what());
        return -1;
    }

    spdlog::info("vegetation: importBinary {}", filepath.string());
    spdlog::info("vegetation: {{{}}}p {{{}}}pvt {{{}}}v", (int)exp.plants.size(), (int)exp.pivots.size(), (int)exp.vertexbuff.size());

    // All imported plants share one set of buffers, so an import that does not
    // fit has to be refused rather than clipped.
    if (binVertexOffset + exp.vertexbuff.size() * sizeof(ribbonVertex8) > (size_t)MAX_PLANT_VERTS * sizeof(ribbonVertex8) ||
        binPlantOffset + exp.plants.size() * sizeof(plant) > (size_t)MAX_PLANT_PLANTS * sizeof(plant) ||
        binPivotOffset + exp.plants.size() * 256 * sizeof(_plant_anim_pivot) > (size_t)MAX_PLANT_PIVOTS * sizeof(_plant_anim_pivot))
    {
        spdlog::error("vegetation: importBinary - '{}' would overflow the shared plant buffers (verts {} @ {}, plants {} @ {}) - skipped",
                      filepath.string(), exp.vertexbuff.size(), binVertexOffset, exp.plants.size(), binPlantOffset);
        return -1;
    }

    // This one shoudl reslolve duplicate materials and modiffy the vertex buffer and load textures
    // how the hgell ddidi the previous version work
    {
        // load materials, and build remapper
        std::string resource = terrafectorEditorMaterial::rootFolder;
        int indexLookup[4096];// just big, bad code
        memset(indexLookup, 0, sizeof(indexLookup));    // vertices referencing a material the file never listed then resolve to 0, not garbage
        for (auto& M : exp.materials)
        {
            indexLookup[M.first] = _plantMaterial::static_materials_veg.find_insert_material(std::filesystem::path(resource + M.second.path), false); //terrafectorEditorMaterial::rootFolder +
        }


        for (auto& V : exp.vertexbuff)
        {
            int idx = (V.b >> 8) & 0x3ff;
            V.b ^= (idx << 8);  // xor clears
            V.b += (indexLookup[idx] << 8);
        }

        int blockOffset = cntV_Offset /= VEG_BLOCK_SIZE;

        spdlog::info("vegetation: set voFFSET {} BLOCKS", blockOffset);
        int cntP = 0;
        for (auto& P : exp.plants)
        {
            // FIXME different for each plant
            P.billboardMaterialIndex = _plantMaterial::static_materials_veg.find_insert_material(std::filesystem::path(resource + exp.billboardMaterials[cntP].path), false);
            for (uint i = 0; i < P.numLods; i++)
            {
                spdlog::info("vegetation: LOD {}, start {}, size {}, pixSize {:.2f}", i, P.lods[i].startVertex, P.lods[i].numBlocks, P.lods[i].pixSize);
                P.lods[i].startVertex += blockOffset;  // count in blocks
            }
            cntP++;
        }
    }

    plantData->setBlob(exp.plants.data(), binPlantOffset, exp.plants.size() * sizeof(plant));
    vertexData->setBlob(exp.vertexbuff.data(), binVertexOffset, exp.vertexbuff.size() * sizeof(ribbonVertex8));
    plantpivotData->setBlob(exp.pivots.data(), binPivotOffset, exp.pivots.size() * sizeof(_plant_anim_pivot));  //??? dow I save 256 blocks

    binVertexOffset += (int)(exp.vertexbuff.size() * sizeof(ribbonVertex8));
    cntV_Offset += (int)exp.vertexbuff.size();
    binPlantOffset += (int)(exp.plants.size() * sizeof(plant));
    binPivotOffset += (int)(exp.plants.size() * 256 * sizeof(_plant_anim_pivot));  // LIKELE veryy wrone., we should
    numBinaryPlants++;

    displayModeSinglePlant = false;
    _ribbonBuilder.packed.resize(exp.vertexbuff.size());  //??? WHY this is just to fool my render function later that checks this size to see if its loaded
    updateMaterialsAndTextures(); // this loads it to GPU

    importPathVector.push_back(filepath.string());
    return (int)importPathVector.size() - 1; // Bit wrong since we load 3 variations, return more info
}






void _rootPlant::build(uint pivotOffset)
{
    if (!root) return;
    auto start = high_resolution_clock::now();

    // Clear some data beforehand - only on single plant, all lods have to call this as well
    _ribbonBuilder.mat_vector_size_Sanity = (int)_plantMaterial::static_materials_veg.materialVector.size();
    _ribbonBuilder.setup(vertex_pack_Settings.getScale(), vertex_pack_Settings.radiusScale, vertex_pack_Settings.getOffset());
    _ribbonBuilder.clearStats(9999);     // just very large for now
    //if (displayModeSinglePlant)
    {
        // always cclearf we have the pivot offset, each plant starts new
        _ribbonBuilder.clearPivot();
    }

    bakeViewMatrix = glm::mat4(1.0);
    ROLL(bakeViewMatrix, rootYaw);
    PITCH(bakeViewMatrix, -rootPitch);
    ROLL(bakeViewMatrix, rootRoll);

    settings.clear();
    settings.root = bakeViewMatrix;
    generator.seed(settings.seed);


    root->clear_build_info();
    root->build(settings, true);

    // Now light the plant
    _ribbonBuilder.lightBasic(extents, root->shadowDepth, root->shadowPenetationHeight);
    _ribbonBuilder.pack();
    _ribbonBuilder.finalizeAndFillLastBlock();

    if (_ribbonBuilder.numPacked() > 0)
    {
        updateMaterialsAndTextures();

        int numV = __min(65536 * 8, (int)_ribbonBuilder.numPacked());
        vertexData->setBlob(_ribbonBuilder.getPackedData(), 0, numV * sizeof(ribbonVertex8));

        // Heap, not stack: 512 KB against MSVC's 1 MB default stack.
        std::vector<block_data> blockBuf(65536); // allows 2 million triangles
        totalBlocksToRender = __min(65536u, _ribbonBuilder.numPacked() / VEG_BLOCK_SIZE);   // move to ribbonvertex
        for (uint j = 0; j < totalBlocksToRender; j++)
        {
            blockBuf[j].vertex_offset = VEG_BLOCK_SIZE * j;
            blockBuf[j].instance_idx = 0;
        }
        blockData->setBlob(blockBuf.data(), 0, totalBlocksToRender * sizeof(block_data));
        blockData_preSort->setBlob(blockBuf.data(), 0, totalBlocksToRender * sizeof(block_data));


        plantBuf[0].radiusScale = ribbonVertex::radiusScale;
        plantBuf[0].scale = ribbonVertex::objectScale;
        plantBuf[0].offset = ribbonVertex::objectOffset;
        plantBuf[0].Ao_depthScale = 0.3f;
        plantBuf[0].bDepth = 1;
        plantBuf[0].bScale = 1;
        plantBuf[0].sunTilt = -0.2f;
        plantBuf[0].shadowUVScale = root->shadowUVScale;
        plantBuf[0].shadowSoftness = root->shadowSoftness;

        plantBuf[0].rootPivot.root = { 0, 0, 0 };
        plantBuf[0].rootPivot.extent = { 0, 1.f / extents.y, 0 };
        plantBuf[0].rootPivot.frequency = root->rootFrequency() * sqrt(extents.y);
        plantBuf[0].rootPivot.stiffness = root->ossilation_stiffness;
        plantBuf[0].rootPivot.shift = root->ossilation_power;
        plantData->setBlob(plantBuf.data(), 0, 1 * sizeof(plant));

        if (_ribbonBuilder.numPivots() > 0)
        {
            plantpivotData->setBlob(_ribbonBuilder.pivotPoints.data(), pivotOffset, _ribbonBuilder.numPivots() * sizeof(_plant_anim_pivot));
        }
    }

    anyChange = false;
    auto stop = high_resolution_clock::now();
    buildTime = (float)duration_cast<microseconds>(stop - start).count() / 1000.f;
}






// Only buildAllLods reaches bake64kplants, and its jpg output fed an export
// that no longer exists. It is worth keeping as the one complete multi-view
// indirect-render example, frustum-plane extraction included.
void _rootPlant::bake64kplants(std::string _path)
{
    const Diligent::BIND_FLAGS rtFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS;
    ew::Fbo::SharedPtr fbo = ew::Fbo::create();
    // 1, 4 is arraySize 1 with 4 mip levels, not MSAA
    fbo->attachColorTarget(ew::Texture::create2D(1800, 600, Diligent::TEX_FORMAT_RGBA8_UNORM, 1, 4, nullptr, rtFlags, "bake64k albedo"), 0);		// albedo
    fbo->attachDepthStencilTarget(ew::Texture::create2D(1800, 600, Diligent::TEX_FORMAT_D24_UNORM_S8_UINT, 1, 1, nullptr, BIND_DEPTH_STENCIL, "bake64k depth"));			// keep for now, not sure why, but maybe usefult for cuts
    const glm::vec4 clearColor(0.2, 0.2f, 0.2f, 0.0f);


    glm::mat4 CAM = glm::mat4(1);
    //GROW(CAM, -extents.y * 15.f);
    PITCH(CAM, -0.3f);
    GROW(CAM, extents.y * 5.f);
    CAM[3][1] += 1000.f;
    glm::mat4 V = glm::inverse(CAM);
    glm::mat4 P = glm::perspectiveRH(0.5f, 3.f, 1.0f, 10000.0f);
    glm::mat4 VP = P * V;

    // Transposed for upload - the ewCamera.h convention.
    glm::mat4 viewproj = glm::transpose(VP);
    glm::mat4 view = glm::transpose(V);

    glm::mat4 FR(1.f);
    FR[0] = float4(P[0][3] + P[0][0], P[1][3] + P[1][0], P[2][3] + P[2][0], P[3][3] + P[3][0]);
    FR[1] = float4(P[0][3] - P[0][0], P[1][3] - P[1][0], P[2][3] - P[2][0], P[3][3] - P[3][0]);
    FR[2] = float4(P[0][3] + P[0][1], P[1][3] + P[1][1], P[2][3] + P[2][1], P[3][3] + P[3][1]);
    FR[3] = float4(P[0][3] - P[0][1], P[1][3] - P[1][1], P[2][3] - P[2][1], P[3][3] - P[3][1]);
    glm::mat4 _clipFrustum = glm::transpose(FR);

    for (int i = 0; i < 3; i++)
    {
        compute_clearBuffers.dispatch(renderInfo.context, 1, 1);
        {
            compute_calulate_lod.setVariable("gConstantBuffer", "view", view);
            compute_calulate_lod.setVariable("gConstantBuffer", "frustum", _clipFrustum);
            compute_calulate_lod.setVariable("gConstantBuffer", "eyePos", (float3)CAM[3]);
            compute_calulate_lod.setVariable("gConstantBuffer", "lodBias", loddingBias);
            compute_calulate_lod.setVariable("gConstantBuffer", "halfAngle_to_Pixels", renderInfo.half_to_Pixels);
            compute_calulate_lod.setVariable("gConstantBuffer", "firstPlant", firstPlant);
            compute_calulate_lod.setVariable("gConstantBuffer", "lastPlant", lastPlant);
            compute_calulate_lod.setVariable("gConstantBuffer", "firstLod", firstLod);
            compute_calulate_lod.setVariable("gConstantBuffer", "lastLod", lastLod);
            compute_calulate_lod.dispatch(renderInfo.context, MAX_PLANT_INSTANCES / 256, 1);
        }
        compute_sortCombine.dispatch(renderInfo.context, 1, 1);

        {
            renderInfo.context->clearFbo(fbo.get(), clearColor, 1.0f, 0, ew::FboAttachmentType::All);    // depth
            vegetationShader.setFbo(fbo);
            {
                Diligent::Viewport vp;
                vp.TopLeftX = 0; vp.TopLeftY = 0;
                vp.Width = 1800.f; vp.Height = 600.f;
                vp.MinDepth = 0; vp.MaxDepth = 1;
                vegetationShader.setViewport(vp);
            }
            vegetationShader.setRasterizerState(rasterstate);
            vegetationShader.setBlendState(blendstateBake);
            vegetationShader.setVariable("gConstantBuffer", "view", view);
            vegetationShader.setVariable("gConstantBuffer", "viewproj", viewproj);
            vegetationShader.setVariable("gConstantBuffer", "eyePos", (float3)CAM[3]);
            vegetationShader.setVariable("gConstantBuffer", "camRight", (float4)CAM[0]);
            vegetationShader.setVariable("gConstantBuffer", "camUp", (float4)CAM[1]);
            vegetationShader.setVariable("gConstantBuffer", "toneMap", (int)1);


            for (uint idx = 0; idx < 128; idx++)
            {
                vegetationShader.setVariable("gConstantBuffer", "drawIndex", idx);
                vegetationShader.renderIndirect(renderInfo.context, drawArgs_vegetation, nullptr, idx, 1);
            }
            vegetationShader.clearViewport();
        }

        flushAndWait(renderInfo.context);

        buffer_feedback_read->enqueueCopy(renderInfo.context, buffer_feedback);
        // PORT-REVIEW (step-6 fence batching): this offline bake path maps the
        // ring back IMMEDIATELY after a full flush+wait instead of waiting for
        // the renderer's end-of-frame signal - signal the shared fence here so
        // the slot is fence-complete when mapCompleted runs.
        renderInfo.context->signalReadbackFrame();
        flushAndWait(renderInfo.context);
        if (const void* pData = buffer_feedback_read->mapCompleted(renderInfo.context))
        {
            std::memcpy(&feedback, pData, sizeof(vegetation_feedback));
            buffer_feedback_read->unmap(renderInfo.context);
        }
    }

    {
        std::filesystem::path PT = _path;
        std::string resource = terrafectorEditorMaterial::rootFolder;
        std::string newRelative = PT.parent_path().string() + "/bake_" + PT.stem().string() + "/";
        std::string newDir = resource + newRelative;
        replaceAllVEG(resource, "/", "\\");
        captureToFileStub("bake64kplants jpg", newDir + "64kplants.jpg");
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

void _rootPlant::bake_Setup()
{
}

void _rootPlant::bake_Export()
{
}

void _rootPlant::bake_Export_colour()
{
}











#define bakeSuperSample 8
#define bakeMipToSave 3
void _rootPlant::bake(std::string _path, std::string _seed, lodBake* _info, glm::mat4 VIEW, bool lod_0)
{
    (void)_seed;
    if (!root) return;

    spdlog::info("vegetation: _rootPlant::bake()");

    int maxMIPY = (int)log2(_info->pixHeight / 16); // how many mips to get to 16
    int wScaler = 4;

    float extentsY = _info->extents.y;     // this is for removeDead at the bottom
    float W = _info->extents.x * _info->bakeWidth;      // this is half width
    float H0 = extentsY * _info->bake_V.x;
    float H1 = extentsY * _info->bake_V.y;
    float delH = H1 - H0;
    int iW = wScaler * (int)ceil(_info->pixHeight * W * 2.f / delH / wScaler) * bakeSuperSample;      // *4  /4 is to keep blocks of 4
    iW = __max(wScaler, iW);
    int iH = _info->pixHeight * bakeSuperSample;

    // new try
    float fW = _info->pixHeight * W * 2.f / delH;   // pefect fractional pixels
    int newIW = (int)floor(fW + 0.7f);  //so we mostly go larger but sometimes smaller
    //iW = newIW * 4 * (int)pow(2, maxMIPY) * bakeSuperSample;

    /*  New new new code
    *   Lets stick with width for now, pick it like above, but then scale the VIEW matrix to make the content fit the new width
    */
    // BIG IF
    iW = newIW * bakeSuperSample;
    float HorizontalScale = (float)newIW / fW;

    if (_info->pixHeight == 32)
    {
        if (fW < 24) newIW = 16;
        else if (fW < 40) newIW = 32;
        else newIW = 48;

        iW = newIW * bakeSuperSample;
        HorizontalScale = (float)newIW / fW;
        maxMIPY = 2;    // down to 8 high or 2 blocks, single block is too restrictive
    }
    else if (_info->pixHeight == 64)
    {
        if (fW < 24) newIW = 16;
        else if (fW < 40) newIW = 32; // 31, 16, 8
        else if (fW < 56) newIW = 48; // 48, 24, 12  etc
        else if (fW < 72) newIW = 64;
        else if (fW < 88) newIW = 80;
        else if (fW < 104) newIW = 96;
        else newIW = 112;

        iW = newIW * bakeSuperSample;
        HorizontalScale = (float)newIW / fW;
        maxMIPY = 2;    // down to 16 high or 4 blocks, single block is too restrictive
    }
    else
    {
        maxMIPY = 3;    // down to 16 high or 4 blocks, single block is too restrictive

        if (fW < 12) { newIW = 8; maxMIPY = 1; }    // 8, 4
        else if (fW < 24) { newIW = 16; maxMIPY = 2; }  // 16, 8, 4
        else
        {
            for (int i = 32; i < 1024; i += 32)
            {
                if (fW > (i - 16))
                {
                    newIW = i;
                }
            }
        }

        iW = newIW * bakeSuperSample;
        HorizontalScale = (float)newIW / fW;
    }

    spdlog::info("vegetation: bake hgt {}, width {}, lods {}, {:.2f} horScale, {:.2f} fW pix", _info->pixHeight, newIW, maxMIPY, HorizontalScale, fW);


    ew::Fbo::SharedPtr fbo;
    {
        const Diligent::BIND_FLAGS rtFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS;
        // 1, 4 is arraySize 1 with FOUR MIP LEVELS, not MSAA. Mip 3, i.e.
        // bakeMipToSave, is what undoes the 8x supersample.
        fbo = ew::Fbo::create();
        fbo->attachColorTarget(ew::Texture::create2D(iW, iH, Diligent::TEX_FORMAT_RGBA8_UNORM, 1, 4, nullptr, rtFlags, "veg bake albedo"), 0);		// albedo
        fbo->attachColorTarget(ew::Texture::create2D(iW, iH, Diligent::TEX_FORMAT_RGBA16_FLOAT, 1, 4, nullptr, rtFlags, "veg bake normal_16"), 1);	    // normal_16
        fbo->attachColorTarget(ew::Texture::create2D(iW, iH, Diligent::TEX_FORMAT_RGBA8_UNORM, 1, 4, nullptr, rtFlags, "veg bake normal_8"), 2);		// normal_8
        fbo->attachColorTarget(ew::Texture::create2D(iW, iH, Diligent::TEX_FORMAT_RGBA8_UNORM, 1, 4, nullptr, rtFlags, "veg bake pbr"), 3);	    // pbr
        fbo->attachColorTarget(ew::Texture::create2D(iW, iH, Diligent::TEX_FORMAT_RGBA8_UNORM, 1, 4, nullptr, rtFlags, "veg bake extra"), 4);	    // extra
        fbo->attachDepthStencilTarget(ew::Texture::create2D(iW, iH, Diligent::TEX_FORMAT_D24_UNORM_S8_UINT, 1, 1, nullptr, BIND_DEPTH_STENCIL, "veg bake depth"));			// keep for now, not sure why, but maybe usefult for cuts
        const glm::vec4 clearColor(0.5, 0.5f, 1.0f, 0.0f);
        renderInfo.context->clearFbo(fbo.get(), clearColor, 1.0f, 0, ew::FboAttachmentType::All);    // depth
        renderInfo.context->clearRtv(fbo->getRenderTargetView(0), glm::vec4(0.3, 0.3f, 0.3f, 0.0f));
        renderInfo.context->clearRtv(fbo->getRenderTargetView(1), glm::vec4(0.5, 0.5f, 1.0f, 0.0f));
        renderInfo.context->clearRtv(fbo->getRenderTargetView(2), glm::vec4(0.5, 0.5f, 1.0f, 0.0f));
        renderInfo.context->clearRtv(fbo->getRenderTargetView(3), glm::vec4(0.0, 0.0f, 0.0f, 0.0f));
        renderInfo.context->clearRtv(fbo->getRenderTargetView(4), glm::vec4(1.0, 1.0f, 1.0f, 1.0f));
    }

    //VIEW[0] *= HorizontalScale;
    glm::mat4 V, VP;
    V = glm::inverse(VIEW);
    VP = glm::orthoRH(-W, W, H0, H1, -100.0f, 100.0f) * V;

    // Transposed for upload - the ewCamera.h convention.
    glm::mat4 viewproj = glm::transpose(VP);
    glm::mat4 view = glm::transpose(VIEW);

    {
        float4 camRight = float4(glm::normalize((float3)VIEW[0]), 0);
        float4 camUp = float4(glm::normalize((float3)VIEW[1]), 0);
        bakeShader.setVariable("gConstantBuffer", "camRight", camRight);
        bakeShader.setVariable("gConstantBuffer", "camUp", camUp);
    }

    {
        bakeShader.setFbo(fbo);
        {
            Diligent::Viewport vp;
            vp.TopLeftX = 0; vp.TopLeftY = 0;
            vp.Width = (float)iW; vp.Height = (float)iH;
            vp.MinDepth = 0; vp.MaxDepth = 1;
            bakeShader.setViewport(vp);
        }
        bakeShader.setRasterizerState(rasterstate);
        bakeShader.setBlendState(blendstateBake);
        bakeShader.setVariable("gConstantBuffer", "view", view);
        bakeShader.setVariable("gConstantBuffer", "viewproj", viewproj);
        bakeShader.setVariable("gConstantBuffer", "eyePos", (float3)VIEW[3] + ((float3)VIEW[2] * 1000.f));
        bakeShader.setVariable("gConstantBuffer", "bake_radius_alpha", W);
        bakeShader.setVariable("gConstantBuffer", "bake_height_alpha", H1);
        bakeShader.setVariable("gConstantBuffer", "bake_AoToAlbedo", (int)_info->bakeAOToAlbedo);
        if (_info->useAlphaInBake)
        {
            bakeShader.setVariable("gConstantBuffer", "bake_AlphaOval", _info->alphaOval);
        }
        else
        {
            bakeShader.setVariable("gConstantBuffer", "bake_AlphaOval", float2(0, 0));
        }
        //bakeShader.Vars()["gConstantBuffer"]["bake_Aobake_AlphaOvalToAlbedo"] = _info->
        _plantMaterial::static_materials_veg.setTextures(bakeShader);
        _plantMaterial::static_materials_veg.rebuildStructuredBuffer();

        bakeShader.drawInstanced(renderInfo.context, 32, totalBlocksToRender);
        bakeShader.clearViewport();
    }
    flushAndWait(renderInfo.context);

    std::filesystem::path PT = _path;
    std::string resource = terrafectorEditorMaterial::rootFolder;
    std::string newRelative = PT.parent_path().string() + "/bake_" + PT.stem().string() + "/";
    std::string newDir = resource + newRelative;
    replaceAllVEG(resource, "/", "\\");
    if (lod_0)
    {
        captureToFileStub("bake FULL albedo jpg", newDir + _info->material.name + "_FULL_albedo.jpg");
        captureToFileStub("bake FULL albedo png", newDir + _info->material.name + "_FULL_albedo.png");
    }

    {
        compute_bakeFloodfill.setTexture("gAlbedo", fbo->getColorTexture(0));
        compute_bakeFloodfill.setTexture("gNormal", fbo->getColorTexture(2));
        compute_bakeFloodfill.setTexture("gTranslucency", fbo->getColorTexture(4));
        compute_bakeFloodfill.setTexture("gpbr", fbo->getColorTexture(3));
        for (int i = 0; i < 128; i++)
        {
            compute_bakeFloodfill.dispatch(renderInfo.context, iW / 4, iH / 4);
        }
    }
    flushAndWait(renderInfo.context);



    {
        _plantMaterial Mat;
        Mat._constData.translucency = 1;
        Mat.albedoPath = newRelative + _info->material.name + "_albedo.dds";
        Mat.albedoName = _info->material.name + "_albedo.dds";
        Mat.normalPath = newRelative + _info->material.name + "_normal.dds";
        Mat.normalName = _info->material.name + "_normal.dds";
        Mat.translucencyPath = newRelative + _info->material.name + "_translucency.dds";
        Mat.translucencyName = _info->material.name + "_translucency.dds";

        fbo->getColorTexture(0)->generateMips(renderInfo.context);
        fbo->getColorTexture(1)->generateMips(renderInfo.context);
        fbo->getColorTexture(2)->generateMips(renderInfo.context);
        fbo->getColorTexture(3)->generateMips(renderInfo.context);
        fbo->getColorTexture(4)->generateMips(renderInfo.context);

        // The mip-3 PNG/PFM captures feed the Compressonator BC7/BC6H
        // shell-outs. Capture-to-file is not implemented, so the whole export
        // chain below skips; it stays as the reference for that flow.
        bool captured = captureToFileStub("bake albedo", newDir + "_albedo.png");
        captureToFileStub("bake normal f16", newDir + "_normal_float16.pfm");
        captureToFileStub("bake normal", newDir + "_normal.png");
        captureToFileStub("bake translucency", newDir + "_translucency.png");

        // FIXME total num MIps to build
        int totalMIP = maxMIPY;// __min(maxHmip, maxWmip) - bakeMipToSave; // -bakeMipToSave for the supersampling
        std::string mipNumber = std::to_string(totalMIP);
        if (captured)
        {
            {
                std::string png = newDir + "_albedo.png";
                std::string cmdExp = resource + "Compressonator\\CompressonatorCLI -miplevels " + mipNumber + " \"" + png + "\" " + resource + "Compressonator\\temp_mip.dds";
                replaceAllVEG(cmdExp, "/", "\\");
                system(cmdExp.c_str());
                std::string cmdExp2 = resource + "Compressonator\\CompressonatorCLI -fd BC7 " + resource + "Compressonator\\temp_mip.dds \"" + resource + Mat.albedoPath + "\"";
                replaceAllVEG(cmdExp2, "/", "\\");
                system(cmdExp2.c_str());
            }

            {
                std::string png = newDir + "_normal.png";
                std::string cmdExp = resource + "Compressonator\\CompressonatorCLI -miplevels " + mipNumber + " \"" + png + "\" " + resource + "Compressonator\\temp_mip.dds";
                replaceAllVEG(cmdExp, "/", "\\");
                system(cmdExp.c_str());
                std::string cmdExp2 = resource + "Compressonator\\CompressonatorCLI -fd BC6H " + resource + "Compressonator\\temp_mip.dds \"" + resource + Mat.normalPath + "\"";
                replaceAllVEG(cmdExp2, "/", "\\");
                system(cmdExp2.c_str());
            }

            {
                std::string png = newDir + "_translucency.png";
                std::string cmdExp = resource + "Compressonator\\CompressonatorCLI -miplevels " + mipNumber + " \"" + png + "\" " + resource + "Compressonator\\temp_mip.dds";
                replaceAllVEG(cmdExp, "/", "\\");
                system(cmdExp.c_str());
                std::string cmdExp2 = resource + "Compressonator\\CompressonatorCLI -fd BC7 " + resource + "Compressonator\\temp_mip.dds \"" + resource + Mat.translucencyPath + "\"";
                replaceAllVEG(cmdExp2, "/", "\\");
                system(cmdExp2.c_str());
            }
        }

        Mat._constData.translucency = _info->translucency;
        Mat._constData.alphaPow = _info->alphaPow;
        Mat._constData.albedoScale[0].w = 0.8f;    // roughness lives in .w since the std430 relayout
        Mat._constData.albedoScale[1].w = 0.8f;

        std::ofstream os(resource + _info->material.path);
        cereal::JSONOutputArchive archive(os);
        archive(Mat);
    }
}



void _rootPlant::updateMaterialsAndTextures()
{
    _plantMaterial::static_materials_veg.modified = false;
    _plantMaterial::static_materials_veg.modifiedData = false;
    _plantMaterial::static_materials_veg.setTextures(vegetationShader);
    _plantMaterial::static_materials_veg.setTextures(vegetationShader_RGB_SAMPLE);
    _plantMaterial::static_materials_veg.setTextures(vegetationShader_DEPTH);
    _plantMaterial::static_materials_veg.rebuildStructuredBuffer();
    _plantMaterial::static_materials_veg.setTextures(billboardShader);
    _plantMaterial::static_materials_veg.setTextures(bakeShader);
}


void _rootPlant::updateShaderConstants(ew::Texture::SharedPtr _previousFrame, ew::Texture::SharedPtr shadow, shaderLightBuffer _buffer)
{
    billboardShader.setTexture("gPreviousFrame", _previousFrame);
    vegetationShader.setTexture("gPreviousFrame", _previousFrame);

    billboardShader.setTexture("terrainShadow", shadow);
    vegetationShader.setTexture("terrainShadow", shadow);

    billboardShader.setVariable("LightsCB", "sunDirection", _buffer.sunDirection);
    billboardShader.setVariable("LightsCB", "sunRightVector", _buffer.sunRightVector);
    billboardShader.setVariable("LightsCB", "sunUpVector", _buffer.sunUpVector);
    billboardShader.setVariable("LightsCB", "screenSize", _buffer.screenSize);
    billboardShader.setVariable("LightsCB", "fog_far_Start", _buffer.fog_far_Start);
    billboardShader.setVariable("LightsCB", "fog_far_log_F", _buffer.fog_far_log_F);
    billboardShader.setVariable("LightsCB", "fog_far_one_over_k", _buffer.fog_far_one_over_k);

    vegetationShader.setVariable("LightsCB", "sunDirection", _buffer.sunDirection);
    vegetationShader.setVariable("LightsCB", "sunRightVector", _buffer.sunRightVector);
    vegetationShader.setVariable("LightsCB", "sunUpVector", _buffer.sunUpVector);
    vegetationShader.setVariable("LightsCB", "screenSize", _buffer.screenSize);
    vegetationShader.setVariable("LightsCB", "fog_far_Start", _buffer.fog_far_Start);
    vegetationShader.setVariable("LightsCB", "fog_far_log_F", _buffer.fog_far_log_F);
    vegetationShader.setVariable("LightsCB", "fog_far_one_over_k", _buffer.fog_far_one_over_k);
}



void _rootPlant::render(ew::GpuContext* _renderContext, const ew::Fbo::SharedPtr& _fbo,
    glm::mat4 _viewproj, float3 camPos, glm::mat4 _view, glm::mat4 _clipFrustum, float halfAngle_to_Pixels, bool terrainMode)
{
    // store information that we need for overlay
    renderInfo.context = _renderContext;
    renderInfo.viewproj = _viewproj;
    renderInfo.cameraPos = camPos;
    float3 camVector2 = (float3(0, 1000, 0) + (float3)settings.root[1] * extents.y / 2.f) - camPos;
    renderInfo.half_to_Pixels_SinglePlant = halfAngle_to_Pixels / glm::length(camVector2);
    renderInfo.half_to_Pixels = halfAngle_to_Pixels;


    if (SAMPLE_MODE)
    {
        bakeShadowMap(_renderContext);

        static float rot = 0;
        rot += 0.01f;
        glm::mat4 view = glm::mat4(1);
        ROLL(view, camRot);    // yaw
        PITCH(view, camPitch); // pitch
        view[3] = float4(0, 1000.2, 0, 1);

        camPos = (float3)view[3] - (float3)view[2] * 10000.f;


        glm::mat4 V, P, VP;
        V = glm::inverse(view);
        P = glm::orthoLH(-1.0f, 1.0f, -1.0f, 1.0f, -100.0f, 100.0f);
        VP = P * V;

        // Transposed for upload - the ewCamera.h convention.
        _viewproj = glm::transpose(VP);
        _view = glm::transpose(view);

        glm::mat4 FR(1.f);
        FR[0] = float4(P[0][3] + P[0][0], P[1][3] + P[1][0], P[2][3] + P[2][0], P[3][3] + P[3][0]);
        FR[1] = float4(P[0][3] - P[0][0], P[1][3] - P[1][0], P[2][3] - P[2][0], P[3][3] - P[3][0]);
        FR[2] = float4(P[0][3] + P[0][1], P[1][3] + P[1][1], P[2][3] + P[2][1], P[3][3] + P[3][1]);
        FR[3] = float4(P[0][3] - P[0][1], P[1][3] - P[1][1], P[2][3] - P[2][1], P[3][3] - P[3][1]);
        _clipFrustum = glm::transpose(FR);
    }


    if (root && bakingView)
    {
        auto bakeLod = root->getBakeInfo(showBake);
        if (bakeLod)
        {
            settings.includeTip = bakeLod->includeTip;
            settings.excludeDead = bakeLod->clipDead;
            // PITCH(bakeViewMatrix, -0.01f);
            glm::mat4 tip = selectedPart->getTip(bakeLod->includeTip);
            glm::mat4 rootM = selectedPart->getRoot(bakeLod->clipDead);
            bakeViewAdjusted = bakeViewMatrix = rootM;
            float3 u = glm::normalize((float3)tip[3] - (float3)rootM[3]);

            float3 d = glm::normalize(glm::cross((float3)bakeViewAdjusted[0], u));
            float3 r = glm::normalize(glm::cross(u, d));

            // now its tilted so tip is int eh middle
            setBakeView(r, u, d);

            ROLL(bakeViewAdjusted, bakeLod->yaw);
            PITCH(bakeViewAdjusted, -bakeLod->pitch);


            extents = selectedPart->calculate_extents(bakeViewAdjusted);
            float W = extents.y;
            bakeLod->alphaOval.x = extents.x * bakeLod->bakeWidth;
            bakeLod->alphaOval.y = W * bakeLod->bake_V.y;



            glm::mat4 V, P, VP;
            bakeViewAdjusted[3][1] += 1000;
            V = glm::inverse(bakeViewAdjusted);

            P = glm::orthoRH(-W, W, -W, W, -100.0f, 100.0f);
            VP = P * V;

            _viewproj = glm::transpose(VP);
            _view = glm::transpose(bakeViewAdjusted);// rename to camerqa or pass in vextors

            _renderContext->clearFbo(_fbo.get(), float4(0.02f, 0.02f, 0.015f, 0), 1.f, 0, ew::FboAttachmentType::All);
            // The bake preview covers the whole window - there is no
            // bakeViewportTL/bakeViewportSize sub-viewport any more.
        }
    }
    else
    {
        settings.includeTip = true;
    }




    // kest try alwasy clear
    {
        compute_clearBuffers.dispatch(_renderContext, 1, 1);
    }

    if (!terrainMode && _ribbonBuilder.numPacked() > 1 && !displayModeSinglePlant)
    {
        compute_calulate_lod.setVariable("gConstantBuffer", "view", _view);
        compute_calulate_lod.setVariable("gConstantBuffer", "frustum", _clipFrustum);
        compute_calulate_lod.setVariable("gConstantBuffer", "eyePos", camPos);
        compute_calulate_lod.setVariable("gConstantBuffer", "lodBias", loddingBias);
        compute_calulate_lod.setVariable("gConstantBuffer", "halfAngle_to_Pixels", renderInfo.half_to_Pixels);
        compute_calulate_lod.setVariable("gConstantBuffer", "firstPlant", firstPlant);
        compute_calulate_lod.setVariable("gConstantBuffer", "lastPlant", lastPlant);
        compute_calulate_lod.setVariable("gConstantBuffer", "firstLod", firstLod);
        compute_calulate_lod.setVariable("gConstantBuffer", "lastLod", lastLod);
        compute_calulate_lod.dispatch(_renderContext, MAX_PLANT_INSTANCES / 256, 1);
        //compute_calulate_lod.dispatchIndirect()
    }



    {
        if (!displayModeSinglePlant)
        {
            compute_sortCombine.dispatch(_renderContext, 1, 1);
        }
    }




    if (terrainMode || _ribbonBuilder.numPacked() > 1)
    {


        // Wall-clock delta, clamped: without the clamp a debugger stall would
        // jump the wind phase.
        static float time = 0.0f;
        {
            static auto lastStamp = high_resolution_clock::now();
            auto nowStamp = high_resolution_clock::now();
            float dt = (float)duration_cast<microseconds>(nowStamp - lastStamp).count() / 1000000.f;
            lastStamp = nowStamp;
            time += __min(dt, 0.1f);
        }

        {
            vegetationShader.setFbo(_fbo);
            vegetationShader.setRasterizerState(rasterstate);
            if (render_alphaBlend)  vegetationShader.setBlendState(blendstate_withAlpha);
            else
            {
                vegetationShader.setBlendState(blendstate);
            }
            vegetationShader.setVariable("gConstantBuffer", "view", _view);
            vegetationShader.setVariable("gConstantBuffer", "viewproj", _viewproj);
            vegetationShader.setVariable("gConstantBuffer", "eyePos", camPos);
            vegetationShader.setVariable("gConstantBuffer", "time", time);
            vegetationShader.setVariable("gConstantBuffer", "windDir", windDir);
            vegetationShader.setVariable("gConstantBuffer", "windStrength", windStrength);


            // _view is the TRANSPOSED (upload-form) camera view, so its glm
            // columns are the camera's world-space rows: [0] right, [1] up.
            float4 camRight = float4(glm::normalize((float3)_view[0]), 0);
            float4 camUp = float4(glm::normalize((float3)_view[1]), 0);
            vegetationShader.setVariable("gConstantBuffer", "camRight", camRight);
            vegetationShader.setVariable("gConstantBuffer", "camUp", camUp);
            vegetationShader.setVariable("gConstantBuffer", "toneMap", (int)0);
            if (bakingView)
            {
                camPos = (float3)bakeViewAdjusted[3] + (float3)bakeViewAdjusted[2] * 1000.f;
                vegetationShader.setVariable("gConstantBuffer", "windStrength", 0.f);
                vegetationShader.setVariable("gConstantBuffer", "eyePos", camPos);
            }

        }


        {
            vegetationShader_RGB_SAMPLE.setFbo(_fbo);
            vegetationShader_RGB_SAMPLE.setRasterizerState(rasterstate);
            vegetationShader_RGB_SAMPLE.setBlendState(blendstate);
            vegetationShader_RGB_SAMPLE.setVariable("gConstantBuffer", "viewproj", _viewproj);
            vegetationShader_RGB_SAMPLE.setVariable("gConstantBuffer", "eyePos", camPos);
            vegetationShader_RGB_SAMPLE.setVariable("gConstantBuffer", "time", time);
            vegetationShader_RGB_SAMPLE.setVariable("gConstantBuffer", "windDir", windDir);
            vegetationShader_RGB_SAMPLE.setVariable("gConstantBuffer", "windStrength", windStrength);
            vegetationShader_RGB_SAMPLE.setVariable("LightsCB", "sunDirection", sunDirectionShadowMap);//
        }

        if (terrainMode)
        {
            vegetationShader.renderIndirect(_renderContext, drawArgs_vegetation);
            ew::gDebug.live.plantDraws++;
        }
        else
        {
            if (displayModeSinglePlant)
            {
                vegetationShader.drawInstanced(_renderContext, VEG_BLOCK_SIZE, totalBlocksToRender);
                if (totalBlocksToRender > 0) ew::gDebug.live.plantDraws++;
            }
            else
            {
                if (render_FrontToback)
                {
                    for (uint idx = 0; idx < 128; idx++)
                    {
                        vegetationShader.setVariable("gConstantBuffer", "drawIndex", idx);
                        vegetationShader.renderIndirect(_renderContext, drawArgs_vegetation, nullptr, idx, 1);
                    }
                }
                else
                {
                    for (int idx = 127; idx >= 0; idx--)
                    {
                        vegetationShader.setVariable("gConstantBuffer", "drawIndex", (uint)idx);
                        vegetationShader.renderIndirect(_renderContext, drawArgs_vegetation, nullptr, idx, 1);
                    }
                }
                ew::gDebug.live.plantDraws += 128;
            }
        }

        if (!terrainMode && _ribbonBuilder.numPacked() > 1 && !displayModeSinglePlant)
        {
            billboardShader.setFbo(_fbo);
            billboardShader.setRasterizerState(rasterstate);
            billboardShader.setBlendState(blendstate);

            billboardShader.setVariable("gConstantBuffer", "viewproj", _viewproj);
            billboardShader.setVariable("gConstantBuffer", "eyePos", camPos);


            {
                billboardShader.renderIndirect(_renderContext, drawArgs_billboards);
                ew::gDebug.live.billboardDraws++;
            }

        }

        {
            // Fence-checked latency ring, never a blocking map: reading the
            // feedback buffer back every frame is a full GPU sync by
            // construction. The struct is frame-global counters with no
            // recyclable slots, so stale data only needs its age reported.
            feedbackFrameCounter++;
            buffer_feedback_read->enqueueCopy(_renderContext, buffer_feedback, feedbackFrameCounter);
            if (const void* pData = buffer_feedback_read->mapCompleted(_renderContext))
            {
                std::memcpy(&feedback, pData, sizeof(vegetation_feedback));
                feedbackAgeFrames = feedbackFrameCounter - (uint32_t)buffer_feedback_read->completedTag();
                buffer_feedback_read->unmap(_renderContext);
            }
        }

    }

    {
        if (!displayModeSinglePlant)
        {
            compute_sortCombine_POST.dispatch(_renderContext, 1, 1);
        }
    }
}


void _rootPlant::builInstanceBuffer()
{
    // Heap, not stack: 2 MB against MSVC's 1 MB default stack.
    std::vector<plant_instance> instanceBuf(MAX_PLANT_INSTANCES);
    const siv::PerlinNoise perlin{ 100 };

    if (cropLines)
    {
        for (int j = 0; j < 64; j++)
        {
            for (int i = 0; i < 256; i++)
            {
                int index = j * 256 + i;
                instanceBuf[index].plant_idx = 0;
                instanceBuf[index].position = { (float)(j - 32) * 1.4f + (float)d_1_1(generator) * 0.1f, 1000.f, (float)(i - 128) * 0.35f + (float)d_1_1(generator) * 0.1f };
                instanceBuf[index].scale = 1.f + (float)d_1_1(generator) * 0.15f;
                instanceBuf[index].rotation = (float)d_1_1(generator) * 3.14f;
                //instanceBuf[index].time_offset = d_1_1(generator) * 100;
            }
        }
    }
    else if (numBinaryPlants == 0)
    {
        static float sum = 0;
        float3 pos = { 0, 1000.f, 0 };
        for (int i = 0; i < MAX_PLANT_INSTANCES; i++)
        {
            while (sum < 1.f)
            {
                pos = { (float)d_1_1(generator) * instanceArea[0], 1000.f, (float)d_1_1(generator) * instanceArea[0] };
                if (uniformSpread)
                {
                    sum += 1;
                }
                else
                {
                    float noise = (float)perlin.octave2D_01(pos.x / 2.f, pos.z / 2.f, 3);
                    sum += pow(noise, 3.f);
                }
            }
            sum -= 1.f;

            instanceBuf[i].position = pos;
            instanceBuf[i].plant_idx = i % 3;
            instanceBuf[i].position = pos;
            instanceBuf[i].scale = 1.f + (float)d_1_1(generator) * 0.15f;
            instanceBuf[i].rotation = (float)d_1_1(generator) * 3.14f;
            //instanceBuf[i].time_offset = d_1_1(generator) * 100;
        }
    }
    else
    {
        static float sum = 0;
        float3 pos = { 0, 1000.f, 0 };
        for (int i = 0; i < MAX_PLANT_INSTANCES; i++)
        {
            int type = i % (numBinaryPlants);
            while (sum < 1)
            {
                pos = { (float)d_1_1(generator) * instanceArea[0] * extents.x, 1000.f, (float)d_1_1(generator) * instanceArea[0] * extents.x };
                float noise = (float)perlin.octave2D_01(pos.x / 2.f + type, pos.z / 2.f, 3);
                (void)noise;

                if (uniformSpread)
                {
                    sum += 1;
                }
                else
                {
                    float noise2 = (float)perlin.octave2D_01(pos.x / 2.f, pos.z / 2.f, 3);
                    sum += pow(noise2, 5.f);
                }
            }
            sum -= 1;

            instanceBuf[i].plant_idx = type;
            instanceBuf[i].position = pos;
            instanceBuf[i].scale = 1.f + (float)d_1_1(generator) * 0.2f;
            instanceBuf[i].rotation = (float)d_1_1(generator) * 3.14f;
            //instanceBuf[i].time_offset = d_1_1(generator) * 100;
        }
    }




    // plant zero is always fixed in the middle
    instanceBuf[0].position = { 0, 1000, 0 };
    instanceBuf[0].scale = 1.f;
    instanceBuf[0].rotation = 0;

    instanceData->setBlob(instanceBuf.data(), 0, MAX_PLANT_INSTANCES * sizeof(plant_instance));
}





void _rootPlant::bakeShadowMap(ew::GpuContext* _renderContext)
{
    glm::mat4 view = glm::mat4(1);
    ROLL(view, 0.f);    // yaw
    PITCH(view, shadowPitch); // pitch
    view[3] = float4(0, 1000.2, 0, 1);

    float3 camPos = (float3)view[3] - (float3)view[2] * 10000.f;
    sunDirectionShadowMap = (float3)view[2];

    glm::mat4 V, P, VP;
    V = glm::inverse(view);
    P = glm::orthoLH(-7.0f, 7.0f, -7.0f, 7.0f, -15.0f, 15.0f);
    VP = P * V;

    // Transposed for upload - the ewCamera.h convention.
    _shadow_viewproj = glm::transpose(VP);

    vegetationShader.setVariable("gConstantBuffer", "shadowViewProj", _shadow_viewproj);
    //vegetationShader_GOURAUD.Vars()["gConstantBuffer"]["shadowViewProj"] = _shadow_viewproj;
    vegetationShader_RGB_SAMPLE.setVariable("gConstantBuffer", "shadowViewProj", _shadow_viewproj);


    // Now render shadow buffer
    {
        _renderContext->clearFbo(shadowFbo.get(), float4(0, 0, 0, 1), 1.0f, 0, ew::FboAttachmentType::All);

        vegetationShader_DEPTH.setFbo(shadowFbo);
        vegetationShader_DEPTH.setRasterizerState(rasterstate);
        vegetationShader_DEPTH.setBlendState(blendstate);
        vegetationShader_DEPTH.setVariable("gConstantBuffer", "viewproj", _shadow_viewproj);
        vegetationShader_DEPTH.setVariable("gConstantBuffer", "eyePos", camPos);
        vegetationShader_DEPTH.setVariable("gConstantBuffer", "time", 0.f);
        vegetationShader_DEPTH.setVariable("gConstantBuffer", "windDir", windDir);
        vegetationShader_DEPTH.setVariable("gConstantBuffer", "windStrength", 0.f);

        vegetationShader_DEPTH.renderIndirect(_renderContext, drawArgs_vegetation);
    }
}



void _rootPlant::buildOneMap(float _sunAngle)
{
    // Heap, not stack: 512 KB against MSVC's 1 MB default stack.
    std::vector<glm::uvec4> data(256 * 128, glm::uvec4(0));
    rgb_data->setBlob(data.data(), 0, sizeof(glm::uvec4) * 256 * 128);

    {
        // zero RGB_MAP via upload
        std::vector<uint32_t> zero(128 * 64, 0);
        RGB_MAP->upload(renderInfo.context, zero.data());
    }
    shadowPitch = _sunAngle;
    bakeShadowMap(renderInfo.context);

    for (int y = 0; y < 64; y++)
    {
        for (int x = 0; x < 128; x++)
        {

            float2 pix = float2(x, y) - float2(127.5f, 0.f);
            float dst = glm::length(pix);
            (void)dst;
            //if (dst < 120)  // 8 pix buffer
            {
                float camPitch2 = 1.57079632679f * (1 - ((float)y / 64.f));
                camRot = 3.141592653f * (1 - ((float)x / 128.f));

                // build a view and render
                //if (caught)
                {
                    glm::mat4 view = glm::mat4(1);
                    ROLL(view, camRot);    // yaw
                    PITCH(view, camPitch2); // pitch
                    view[3] = float4(0, 1000.2, 0, 1);

                    float3 camPos = (float3)view[3] - (float3)view[2] * 10000.f;

                    glm::mat4 V, P, VP;
                    V = glm::inverse(view);
                    P = glm::orthoLH(-1.0f, 1.0f, -0.15f, 0.15f, -10.0f, 10.0f);
                    VP = P * V;

                    glm::mat4 _viewproj = glm::transpose(VP);
                    glm::mat4 _view = glm::transpose(view);

                    glm::mat4 FR(1.f);
                    FR[0] = float4(P[0][3] + P[0][0], P[1][3] + P[1][0], P[2][3] + P[2][0], P[3][3] + P[3][0]);
                    FR[1] = float4(P[0][3] - P[0][0], P[1][3] - P[1][0], P[2][3] - P[2][0], P[3][3] - P[3][0]);
                    FR[2] = float4(P[0][3] + P[0][1], P[1][3] + P[1][1], P[2][3] + P[2][1], P[3][3] + P[3][1]);
                    FR[3] = float4(P[0][3] - P[0][1], P[1][3] - P[1][1], P[2][3] - P[2][1], P[3][3] - P[3][1]);
                    glm::mat4 _clipFrustum = glm::transpose(FR);

                    {
                        compute_clearBuffers.dispatch(renderInfo.context, 1, 1);

                        compute_calulate_lod.setVariable("gConstantBuffer", "view", _view);
                        compute_calulate_lod.setVariable("gConstantBuffer", "frustum", _clipFrustum);
                        compute_calulate_lod.setVariable("gConstantBuffer", "eyePos", camPos);
                        compute_calulate_lod.setVariable("gConstantBuffer", "lodBias", loddingBias);
                        compute_calulate_lod.setVariable("gConstantBuffer", "firstPlant", firstPlant);
                        compute_calulate_lod.setVariable("gConstantBuffer", "lastPlant", lastPlant);
                        compute_calulate_lod.setVariable("gConstantBuffer", "firstLod", firstLod);
                        compute_calulate_lod.setVariable("gConstantBuffer", "lastLod", lastLod);
                        compute_calulate_lod.dispatch(renderInfo.context, MAX_PLANT_INSTANCES / 256, 1);
                    }



                    {
                        renderInfo.context->clearFbo(rgbFbo.get(), float4(0, 0, 0.0, 1), 1.0f, 0, ew::FboAttachmentType::All);
                    }

                    {
                        vegetationShader_RGB_SAMPLE.setFbo(rgbFbo);
                        vegetationShader_RGB_SAMPLE.setRasterizerState(rasterstate);
                        vegetationShader_RGB_SAMPLE.setBlendState(blendstate);
                        vegetationShader_RGB_SAMPLE.setVariable("gConstantBuffer", "viewproj", _viewproj);

                        vegetationShader_RGB_SAMPLE.setVariable("gConstantBuffer", "eyePos", camPos);
                        float time1 = 0.0f;
                        vegetationShader_RGB_SAMPLE.setVariable("gConstantBuffer", "time", time1);
                        vegetationShader_RGB_SAMPLE.setVariable("gConstantBuffer", "windDir", windDir);
                        windStrength = 0;
                        vegetationShader_RGB_SAMPLE.setVariable("gConstantBuffer", "windStrength", windStrength);
                        vegetationShader_RGB_SAMPLE.setVariable("LightsCB", "sunDirection", sunDirectionShadowMap);


                        vegetationShader_RGB_SAMPLE.renderIndirect(renderInfo.context, drawArgs_vegetation);
                    }

                    flushAndWait(renderInfo.context);
                    // Now do compute into second tecture
                    {
                        compute_sampleRGBtoPixel.setVariable("gConstants", "pix", glm::uvec2(x, y));
                        compute_sampleRGBtoPixel.dispatch(renderInfo.context, 32, 8, 1);
                    }
                    static int cnt = 0;
                    cnt++;
                    if (cnt % 2000 == 0)
                    {
                        char name[256];
                        snprintf(name, sizeof(name), "e:/test_RGB/_view_%d.png", cnt);
                        captureToFileStub("buildOneMap view", name);
                    }
                }

            }

        }
    }

    compute_sampleRGBtoPixel_ToTexture.dispatch(renderInfo.context, 4, 2, 1);
    flushAndWait(renderInfo.context);
    captureToFileStub("buildOneMap RGB_MAP", "e:/test_RGB/smallgrass.png");
}



float GeometrySchlickGGX_VEG(float NdotV, float k)
{
    float nom = NdotV;
    float denom = NdotV * (1.f - k) + k;

    return nom / denom;
}

float GeometrySmith_VEG(float3 N, float3 V, float3 L, float k)
{
    float NdotV = __max(glm::dot(N, V), 0.1f);	// ??? FIXME JOPHAN - clamp to zero causes black on perfect edge on pixels and that is just WRONG
    float NdotL = __max(glm::dot(N, L), 0.0f);
    float ggx1 = GeometrySchlickGGX_VEG(NdotV, k);
    float ggx2 = GeometrySchlickGGX_VEG(NdotL, k);

    return ggx1 * ggx2;
}

float schlick_VEG(float f0, float V)
{
    return f0 + (1 - f0) * pow(V, 5.f);
}

void _rootPlant::buildBDRF()
{
    unsigned char data[64][128][4];
    unsigned char dataGGX[64][128][4];

    float3 N = { 0, 1, 0 };

    // material GGX
    float roughness = 0.9f;
    float k_direct = (roughness + 1) * (roughness + 1) / 8;


    for (float sun = 0.1f; sun < 1.51f; sun += 0.1f)
    {
        float3 SUN = float3(cos(sun), sin(sun), 0);

        for (int y = 0; y < 64; y++)
        {
            float pitch = 1.57079632679f * (1 - ((float)y / 64.f));
            for (int x = 0; x < 128; x++)
            {
                float yaw = 3.141592653f * (1 - ((float)x / 128.f));
                float3 EYE = float3(cos(pitch) * cos(yaw), sin(pitch), cos(pitch) * sin(yaw));
                float3 Half = glm::normalize(SUN + EYE);
                (void)Half;

                float specGeom = GeometrySmith_VEG(N, EYE, SUN, k_direct);
                float slck = schlick_VEG(0.04f, 1.f - glm::dot(EYE, N));

                dataGGX[y][x][0] = (unsigned char)(255.f * (1.f - specGeom));
                dataGGX[y][x][1] = (unsigned char)(255.f * (glm::dot(N, SUN) * (1.f - slck)));
                dataGGX[y][x][2] = 0;
                dataGGX[y][x][3] = 255;

                // My guess
                float EoS = glm::clamp(glm::dot(EYE, SUN), 0.f, 1.f);
                float EoN = glm::clamp(glm::dot(EYE, N), 0.f, 1.f);
                float shadow = 0.25f + 0.75f * pow((1.f - sun / 1.58f), 2.5f);
                shadow *= 1.f - 0.5f * pow(EoS, 200.f);
                shadow *= 1.f - 0.85f * pow(1.f - EoN, 5.f);

                float light = 1.f - shadow;
                float split = glm::dot(EYE, SUN) * 0.5f + 0.5f;
                float grn = light * split;
                float blue = light * (1.f - split);
                data[y][x][0] = (unsigned char)(255.f * shadow);
                data[y][x][1] = (unsigned char)(255.f * grn);
                data[y][x][2] = (unsigned char)(255.f * blue);
                data[y][x][3] = 255;
            }
        }

        int sunI = (int)(sun * 57.2957795131f);
        std::string name = "e:/test_RGB/GGX_" + std::to_string(sunI) + ".png";
        RGB_MAP->upload(renderInfo.context, dataGGX);
        flushAndWait(renderInfo.context);
        captureToFileStub("buildBDRF GGX", name);

        name = "e:/test_RGB/guess_" + std::to_string(sunI) + ".png";
        RGB_MAP->upload(renderInfo.context, data);
        flushAndWait(renderInfo.context);
        captureToFileStub("buildBDRF guess", name);
    }
}
