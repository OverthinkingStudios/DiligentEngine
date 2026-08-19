#include "vegetationBuilder.h"
//#include "imgui.h"
#include "PerlinNoise.hpp"          //https://github.com/Reputeless/PerlinNoise/blob/master/PerlinNoise.hpp

#include <iostream>
#include <memory>



using namespace std::chrono;

#define TOOLTIP(x)  if (ImGui::IsItemHovered()) {ImGui::SetTooltip(x);}
#pragma optimize("", off)




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
        for (const auto& entry : std::filesystem::recursive_directory_iterator(fullPath))
        {
            std::string subPath = clean(entry.path().string());
            if (subPath.find(fullName) != std::string::npos)
            {
                return find_insert_material(subPath, _forceReload);
            }
        }
    }

    fprintf(terrafectorSystem::_logfile, "error : vegetation material - %s does not exist\n", _name.c_str());
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
        fprintf(terrafectorSystem::_logfile, "add vegeatation material[%d] - %s\n", materialIndex, _path.filename().string().c_str());
        return materialIndex;
    }
    else
    {
        fprintf(terrafectorSystem::_logfile, " NOT FOUND add vegeatation- %s\n", _path.filename().string().c_str());

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
        if (textureVector[i]->getSourcePath().compare(_path) == 0)
        {
            if (_forceReload)
            {
                textureVector[i] = Texture::createFromFile(textureVector[i]->getSourcePath(), true, isSRGB);
                textureVector[i]->setSourcePath(_path);
                textureVector[i]->setName(_path.filename().string());
                // FIXME can we save a timestamp and only relaod if that has changed
            }
            return i;
        }
    }

    Texture::SharedPtr tempTexture = Texture::createFromFile(_path, false, false);
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
            fprintf(terrafectorSystem::_logfile, "%s\n", cmdExp.c_str());
            system(cmdExp.c_str());
            if (isSRGB)
            {
                std::string cmdExp2 = comprs + " -fd BC7 -Quality 0.01 " + temp + ddsFilename;
                replaceAllVEG(cmdExp2, "/", "\\");
                fprintf(terrafectorSystem::_logfile, "%s\n", cmdExp2.c_str());
                system(cmdExp2.c_str());
            }
            else
            {
                std::string cmdExp2 = comprs + " -fd BC6H " + temp + ddsFilename;
                replaceAllVEG(cmdExp2, "/", "\\");
                fprintf(terrafectorSystem::_logfile, "%s\n", cmdExp2.c_str());
                system(cmdExp2.c_str());

            }
        }
    }
    Texture::SharedPtr tex = Texture::createFromFile(ddsFilename, true, isSRGB);
    //Texture::SharedPtr tex = Texture::createFromFile(_path.string(), true, isSRGB);
    if (tex)
    {
        tex->setSourcePath(_path);
        tex->setName(_path.filename().string());
        textureVector.emplace_back(tex);

        float compression = 4.0f;
        if (isSRGB) compression = 4.0f;

        texMb += (float)(tex->getWidth() * tex->getHeight() * 4.0f * 1.333f) / 1024.0f / 1024.0f / compression;	// for 4:1 compression + MIPS

        fprintf(terrafectorSystem::_logfile, "%s\n", tex->getName().c_str());

        return (uint)(textureVector.size() - 1);
    }
    else
    {
        fprintf(terrafectorSystem::_logfile, "failed %s \n", _path.string().c_str());
        return -1;
    }


}


Texture::SharedPtr materialCache_plants::getDisplayTexture()
{
    if (dispTexIndex >= 0) {
        return textureVector.at(dispTexIndex);
    }
    return nullptr;
}



void materialCache_plants::setTextures(ShaderVar& _var)
{
    for (size_t i = 0; i < textureVector.size(); i++)
    {
        _var[i] = textureVector[i];
    }

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
        cereal::JSONInputArchive archive(is);
        //serialize(archive, 100);
        archive(*this);

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
    _view[1].xyz = glm::normalize(_end - origin);
    _view[2].xyz = glm::normalize(glm::cross((float3)_view[0], (float3)_view[1]));
    _view[0].xyz = glm::normalize(glm::cross((float3)_view[1], (float3)_view[2]));
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
            fprintf(terrafectorSystem::_logfile, "_plantRND   _leafBuilder  %s\n", path.c_str());
            break;
        case P_STEM:
            plantPtr.reset(new _stemBuilder);
            fprintf(terrafectorSystem::_logfile, "_plantRND   _stemBuilder  %s\n", path.c_str());
            break;
        case P_CLUMP:
            plantPtr.reset(new _clumpBuilder);
            fprintf(terrafectorSystem::_logfile, "_plantRND   _clumpBuilder  %s\n", path.c_str());
            break;
        case P_FLOWER:
            plantPtr.reset(new _flowerBuilder);
            fprintf(terrafectorSystem::_logfile, "_plantRND   _flowerBuilder  %s\n", path.c_str());
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
    rnd_idx %= data.size();
    return data[rnd_idx];
}



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

        fprintf(terrafectorSystem::_logfile, "_randomBranch::reload  %s\n", path.c_str());
        fflush(terrafectorSystem::_logfile);

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
            for (int j = 0; j < branchData.size(); j++)
            {
                float offset = fabs(branchData[j].params.y - _t) / branchData[j].params.z;
                float val = branchData[j].params.x * glm::smoothstep(1.f, 0.f, offset) * d_30(_rootPlant::generator);
                if (val > percentage)
                {
                    percentage = val;
                    idx = j;
                }
            }
            RND[i] = idx;
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
        std::ifstream is(terrafectorEditorMaterial::rootFolder + path);
        cereal::JSONInputArchive archive(is);
        archive(*this);
        changed = false;
        fileNotFound = false;
    }
    else
    {
        reportError(fmt::format("{}\nError: {}", "File does not exists in the relative tree structure", ""));
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
        std::ifstream is(terrafectorEditorMaterial::rootFolder + path);
        cereal::JSONInputArchive archive(is);
        archive(*this);
        changed = false;
        fileNotFound = false;
    }
    else
    {
        reportError(fmt::format("{}\nError: {}", "File does not exists in the relative tree structure", ""));
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
    if (i < lod_bakeInfo.size()) return &lod_bakeInfo[i];
    else return nullptr;
}


levelOfDetail* _flowerBuilder::getLodInfo(uint i)
{
    if (i < lodInfo.size()) return &lodInfo[i];
    else return nullptr;
}

glm::mat4  _flowerBuilder::build_2(buildSetting _settings, uint _bakeIndex, bool _faceCamera, bool _diamond)
{
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
        for (int i = 0; i < lodInfo.size(); i++)
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
                debugmaxLOD = lodInfo.size() - 1;
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
                for (int i = 0; i < rings.size(); i++)
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
                                rndPos.x = d_1_1(MT);
                                rndPos.y = d_1_1(MT);
                                rndPos.z = d_1_1(MT);
                                l = glm::length(rndPos);
                                //for (auto& P : points)
                                //{
                                //    if (glm::length(rndPos * rings[i].sphere_size - P) < 0.01f) l = 1000;   // reject if too closebut needs tocome from child
                                //}
                            } while (l >= 1.f || l < 0.75f);

                            rndPos.y += 0.5f;   // ependsa if we start full spehere
                            float3 rndAxis = (rndPos.x * root_node[0] * rings[i].sphere_size.x) + (rndPos.y * root_node[1] * rings[i].sphere_size.y) + (rndPos.z * root_node[2] * rings[i].sphere_size.z);
                            //rndPos *= rings[i].sphere_size;
                            points.push_back(rndAxis);
                            node = root_node;
                            node[3].xyz += rndAxis;

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
    //return NODES.back();    // since its only direction test with this
    //-- need t make this so it returns soemthign proepr
    glm::mat4 tip;
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
        std::ifstream is(terrafectorEditorMaterial::rootFolder + path);
        cereal::JSONInputArchive archive(is);
        archive(*this);
        changed = false;
        fileNotFound = false;
    }
    else
    {
        reportError(fmt::format("{}\nError: {}", "File does not exists in the relative tree structure", ""));
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
    if (i < lod_bakeInfo.size()) return &lod_bakeInfo[i];
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
    float random = RND_B(float2(1.f, _rnd ? 0.2f : 0.));

    for (int i = 1; i < lodInfo.size(); i++)
    {
        if (_pixelSize * random <= (lodInfo[i].pixelSize * 0.001f))          lodIndex = i;
    }

    debugLOD = lodIndex;
    debugmaxLOD = lodInfo.size() - 1;
    debugBAKETYPE = lodInfo[lodIndex].bakeType;

    return &lodInfo[lodIndex];
}

#pragma optimize("", off)

/*  Diamond overshoots 10 % inside teh shader so shrink by 10 %
*/
glm::mat4  _stemBuilder::build_2(buildSetting _settings, lodBake* pBake, bool _faceCamera, bool _diamond)
{
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
                int currentIdx = 0;
                for (int iN = 0; iN < NODES.size() - 1; iN++)
                {
                    float3 rel = NODES[iN][3] - node[3];
                    float3 rel2 = NODES[iN + 1][3] - node[3];
                    float d = glm::dot(rel, (float3)node[1]);
                    float d2 = glm::dot(rel2, (float3)node[1]);

                    if (d <= 0 && d2 > 0)
                    {
                        float total = (d2 - d) + 0.0000001;   // add a samll bit for when we have no tip and this becomes zero
                        CURRENT[3] = glm::lerp(NODES[iN][3], NODES[iN + 1][3], -d / total);
                        break;
                    }
                    // seach last to tip, we didnt find it
                    {
                        float d = glm::dot((float3)(NODES.back()[3] - node[3]), (float3)node[1]);
                        float d2 = glm::dot((float3)(tip_NODE[3] - node[3]), (float3)node[1]);
                        float total = (d2 - d) + 0.0000001;
                        CURRENT[3] = glm::lerp(NODES.back()[3], tip_NODE[3], -d / total);
                    }
                }
            }

            if (i == 0) CURRENT = node;
            if (i == 3) CURRENT = last;

            float w = EXTENTS.du4[i] * pBake->bakeWidth;
            float dU = EXTENTS.du4[i] / EXTENTS.width();
            _ribbonBuilder.set(CURRENT, w, mat, float2(dU, 1.f - (0.3333333f * i)), 1.f, 1.f);
            node[3] += step;
            node[1] += binorm_step;
        }
    }

    return last;
}

// really build7
glm::mat4  _stemBuilder::build_n(buildSetting _settings, lodBake* pBake, bool _faceCamera)
{
    float w = EXTENTS.width() * pBake->bakeWidth;
    uint mat = pBake->material.index;

    glm::mat4 node = NODES.front();
    glm::mat4 last = NODES.back();
    float tipLength = glm::dot(tip_NODE[3] - last[3], node[1]);
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
            int currentIdx = 0;
            for (int iN = 0; iN < NODES.size() - 1; iN++)
            {
                float3 rel = NODES[iN][3] - node[3];
                float3 rel2 = NODES[iN + 1][3] - node[3];
                float d = glm::dot(rel, (float3)node[1]);
                float d2 = glm::dot(rel2, (float3)node[1]);

                if (d <= 0 && d2 > 0)
                {
                    float total = (d2 - d) + 0.0000001;   // add a samll bit for when we have no tip and this becomes zero
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
    /*for (int i = 0; i < _settings.callDepth; i++) fprintf(terrafectorSystem::_logfile, "    ");
    fprintf(terrafectorSystem::_logfile, "build_leaves SEED  %d\n", _settings.seed);*/
    _rootPlant::generator.seed(_settings.seed);
    int oldSeed = _settings.seed;
    std::mt19937 MT(_settings.seed * 35696 + 193489);

    // side nodes
    uint end = NODES.size();
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

            float rndRoll = 6.28f * d_1_1(MT);
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
                    nodeTwist += d_1_1(MT) * leaf_rnd.x;
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
                    /*for (int i = 0; i < _settings.callDepth; i++) fprintf(terrafectorSystem::_logfile, "    ");
                    fprintf(terrafectorSystem::_logfile, "build_leaves SEED  %d  not sure about this one\n", _settings.seed);*/
                    _randomBranch* pBranch = branches.get(branchAge);
                    if (pBranch && pBranch->plantPtr) pBranch->plantPtr->build(_settings, _addVerts, _extents);
                }
            }
        }
    }
}


void _stemBuilder::build_NODES(buildSetting _settings, bool _addVerts, bool _extents)
{
    /*if (_addVerts)
    {
        for (int i = 0; i < _settings.callDepth; i++) fprintf(terrafectorSystem::_logfile, "    ");
        fprintf(terrafectorSystem::_logfile, "  - nodes {%d} %d    %s\n", _settings.pivotDepth, _settings.pivotIndex[_settings.pivotDepth - 1], name.c_str());
    }*/

    /*for (int i = 0; i < _settings.callDepth; i++) fprintf(terrafectorSystem::_logfile, "    ");
    fprintf(terrafectorSystem::_logfile, "stem node SEED  %d\n", _settings.seed);*/
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
    else { age *= abs(_settings.node_age); } // negative values are relative
    int iAge = __max(1, (int)age);

    tip_width = RND_B(stem_width) * 0.001f;                         // tip radius
    root_width = tip_width + RND_B(stem_d_width) * 0.001f * age;    // root radius  //?? iAge
    float rootPow = stem_pow_width.x;
    float dR = (root_width - tip_width);

    int numLiveNodes = (int)RND_B(max_live_segments);
    firstLiveSegment = __max(1, iAge - numLiveNodes);

    std::uniform_real_distribution<> d50(0.5f, 1.5f);
    float pixRandFoViz = _settings.pixelSize * d50(_rootPlant::generator);

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
    float stemLenght = glm::length(tip_NODE[3] - node[3]);
    float stemPixels = (stemLenght * 20) / _settings.pixelSize;
    int stemNumSegments = (int)nodeLengthSplit;// glm::clamp((int)(stemPixels / nodeLengthSplit), 1, 10);     // 1 for every 8 pixels, clampped
    float totalStep = 20.f * iAge / (float)stemNumSegments;
    float cnt = 0;


    float W;

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
                if (!_settings.isBaking)
                {
                    PITCH(node, C);

                    // Phototropy - custom axis
                    float pScale = 1.f - fabs(node[1][1]);
                    if (pScale > 0.05f)
                    {
                        float3 axis = glm::cross(float3(0, 1, 0), (glm::vec3)node[1]);
                        float3 XX = float3(0, 0, 0);
                        XX.x = glm::dot(axis, (glm::vec3)node[0]);
                        XX.z = glm::dot(axis, (glm::vec3)node[2]);
                        node = glm::rotate(node, -P * pScale, glm::normalize(XX));
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

                float t = (float)i / age + ((float)j / 20.f * (1.f / age));
                float W = root_width - dR * pow(t, rootPow);
                visible = W > pixRandFoViz && (stem_Material.index >= 0);
                bool weareinthelastbit = (i == iAge) && (j > (20 - totalStep));
                if (_addVerts && visible && cnt >= totalStep && !weareinthelastbit)
                {
                    _ribbonBuilder.set(node, W * 0.5f, stem_Material.index, float2(1.f, V), 1.f, 1.f);
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

    if (p_settings->pivotDepth < 4)
    {
        p_settings->pivotIndex[p_settings->pivotDepth] = _ribbonBuilder.pushPivot(p_settings->seed, p);
        p_settings->pivotDepth += 1;
        debugnumPivots++;

        debugLastPivots[0] = p_settings->pivotIndex[0];
        debugLastPivots[1] = p_settings->pivotIndex[1];
        debugLastPivots[2] = p_settings->pivotIndex[2];
        debugLastPivots[3] = p_settings->pivotIndex[3];
        for (int i = 0; i < p_settings->callDepth; i++) fprintf(terrafectorSystem::_logfile, "    ");
        fprintf(terrafectorSystem::_logfile, "add pivot {%d} %d [seed %d]   %s\n", p_settings->pivotDepth, p_settings->pivotIndex[p_settings->pivotDepth - 1], p_settings->seed, name.c_str());
    }
    else
    {
        fprintf(terrafectorSystem::_logfile, "pivot depth exceeded\n");
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
        std::ifstream is(terrafectorEditorMaterial::rootFolder + path);
        cereal::JSONInputArchive archive(is);
        archive(*this);
        changed = false;
        fileNotFound = false;
    }
    else
    {
        reportError(fmt::format("{}\nError: {}", "File does not exists in the relative tree structure", ""));
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
    if (i < lod_bakeInfo.size()) return &lod_bakeInfo[i];

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
    /*if (_addVerts)
    {
        for (int i = 0; i < _settings.callDepth; i++) fprintf(terrafectorSystem::_logfile, "    ");
        fprintf(terrafectorSystem::_logfile, "clump SEED  %d\n", _settings.seed);
    }*/

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


        float centerTest = 1.5;

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
                rndPos.x = d_1_1(MT);
                rndPos.y = d_1_1(MT);
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
            float c_rnd = d_1_1(MT) * CLUMP.child_rnd.x;
            float c_rndp = d_1_1(MT) * CLUMP.child_rnd.x;
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
            glm::mat4 TIP;
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
    float random = RND_B(float2(1.f, _rnd ? 0.2f : 0.));

    for (int i = 1; i < lodInfo.size(); i++)
    {
        if (_pixelSize * random <= (lodInfo[i].pixelSize * 0.001f))          lodIndex = i;
    }

    debugLOD = lodIndex;
    debugmaxLOD = lodInfo.size() - 1;
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

    if (p_settings->pivotDepth < 4)
    {
        p_settings->pivotIndex[p_settings->pivotDepth] = _ribbonBuilder.pushPivot(p_settings->seed, p);
        p_settings->pivotDepth += 1;
        debugnumPivots++;

        debugLastPivots[0] = p_settings->pivotIndex[0];
        debugLastPivots[1] = p_settings->pivotIndex[1];
        debugLastPivots[2] = p_settings->pivotIndex[2];
        debugLastPivots[3] = p_settings->pivotIndex[3];
        for (int i = 0; i < p_settings->callDepth; i++) fprintf(terrafectorSystem::_logfile, "    ");
        fprintf(terrafectorSystem::_logfile, "add pivot {%d} %d [seed %d]   %s\n", p_settings->pivotDepth, p_settings->pivotIndex[p_settings->pivotDepth - 1], p_settings->seed, name.c_str());
    }
    else
    {
        fprintf(terrafectorSystem::_logfile, "pivot depth exceeded\n");
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
        EXTENTS.start(start, start[3] + start[1]);
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
    texture = Texture::createFromFile(tp, true, true);
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



    vegetationShader.load("Samples/Earthworks_4/hlsl/terrain/render_vegetation_ribbons.hlsl", "vsMain", "psMain", Vao::Topology::LineStrip, "gsMain");
    auto& starts = vegetationShader.Program()->getGlobalCompilationStats();
    vegetationShader.Vars()->setBuffer("plant_buffer", plantData);
    vegetationShader.Vars()->setBuffer("plant_pivot_buffer", plantpivotData);
    vegetationShader.Vars()->setBuffer("instance_buffer", instanceData);
    vegetationShader.Vars()->setBuffer("block_buffer", blockData_preSort);
    vegetationShader.Vars()->setBuffer("vertex_buffer", vertexData);
    vegetationShader.Vars()->setBuffer("sort", buffer_gpuSort);
    vegetationShader.Vars()->setBuffer("materials", _plantMaterial::static_materials_veg.sb_vegetation_Materials);
    vegetationShader.Vars()->setSampler("gSmpLinear", sampler_Ribbons);              // fixme only cvlamlX
    vegetationShader.Vars()->setSampler("gSamplerDepth", sampler_Depth);
    vegetationShader.Vars()->setSampler("gSmpLinearClamp", sampler_ClampAnisotropic);              // fixme only cvlamlX
    vegetationShader.Vars()->setTexture("highResShadow", shadowFbo->getDepthStencilTexture());
    auto& block = vegetationShader.Vars()->getParameterBlock("textures");
    varVegTextures = block->findMember("T");

    _plantMaterial::static_materials_veg.setTextures(varVegTextures);

    vegetationShader.Vars()->setTexture("gAtmosphereInscatter", inscatter);
    vegetationShader.Vars()->setTexture("gAtmosphereOutscatter", outscatter);
    vegetationShader.Vars()->setTexture("SunInAtmosphere", sunlightTexture);

    vegetationShader.Vars()->setTexture("gEnv", envTexture);
    vegetationShader.Vars()->setBuffer("feedback_Veg", buffer_feedback);
    vegetationShader.Vars()->setTexture("gDappledLight", dappledLightTexture);


    billboardShader.Vars()->setTexture("gAtmosphereInscatter", inscatter);
    billboardShader.Vars()->setTexture("gAtmosphereOutscatter", outscatter);
    billboardShader.Vars()->setTexture("SunInAtmosphere", sunlightTexture);

    billboardShader.Vars()->setTexture("gEnv", envTexture);
    billboardShader.Vars()->setBuffer("feedback_Veg", buffer_feedback);
}

void _rootPlant::onLoad()
{
    //DXGI_FORMAT_R32_TYPELESS
    Fbo::Desc desc;
    desc.setDepthStencilTarget(ResourceFormat::D24UnormS8);			// keep for now, not sure why, but maybe usefult for cuts
    desc.setColorTarget(0u, ResourceFormat::R8Unorm, true);		    // albedo    so I can test my soft shadow ideas
    shadowFbo = Fbo::create2D(8192, 8192, desc, 1, 1);

    desc.setColorTarget(0u, ResourceFormat::RGBA8Unorm, true);
    rgbFbo = Fbo::create2D(1024, 256, desc, 1, 1);

    RGB_MAP = Texture::create2D(128, 64, Falcor::ResourceFormat::RGBA8Unorm, 1, 1, nullptr, Falcor::Resource::BindFlags::UnorderedAccess | Falcor::Resource::BindFlags::ShaderResource);


    plantData = Buffer::createStructured(sizeof(plant), MAX_PLANT_PLANTS);
    plantpivotData = Buffer::createStructured(sizeof(_plant_anim_pivot), MAX_PLANT_PIVOTS);
    instanceData = Buffer::createStructured(sizeof(plant_instance), MAX_PLANT_INSTANCES);
    instanceData_Billboards = Buffer::createStructured(sizeof(plant_instance), MAX_PLANT_BILLBOARDS, Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess | Resource::BindFlags::IndirectArg);

    blockData_preSort = Buffer::createStructured(sizeof(block_data), MAX_PLANT_BLOCKS * 3, Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess | Resource::BindFlags::IndirectArg);        // big enough to house inatnces * blocks per instance   8 Mb for now
    blockData = Buffer::createStructured(sizeof(block_data), MAX_PLANT_BLOCKS, Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess | Resource::BindFlags::IndirectArg);        // big enough to house inatnces * blocks per instance   8 Mb for now
    vertexData = Buffer::createStructured(sizeof(ribbonVertex8), MAX_PLANT_VERTS);

    drawArgs_vegetation = Buffer::createStructured(sizeof(t_DrawArguments), numRenderViews * 128, Resource::BindFlags::UnorderedAccess | Resource::BindFlags::IndirectArg);
    drawArgs_billboards = Buffer::createStructured(sizeof(t_DrawArguments), numRenderViews, Resource::BindFlags::UnorderedAccess | Resource::BindFlags::IndirectArg);

    buffer_gpuSort = Buffer::createStructured(sizeof(uint4), 1024);
    buffer_feedback = Buffer::createStructured(sizeof(vegetation_feedback), 1);
    buffer_feedback_read = Buffer::createStructured(sizeof(vegetation_feedback), 1, Resource::BindFlags::None, Buffer::CpuAccess::Read);


    compute_clearBuffers.load("Samples/Earthworks_4/hlsl/terrain/compute_vegetation_clear.hlsl");
    compute_clearBuffers.Vars()->setBuffer("DrawArgs_Quads", drawArgs_billboards);
    compute_clearBuffers.Vars()->setBuffer("DrawArgs_Plants", drawArgs_vegetation);
    compute_clearBuffers.Vars()->setBuffer("feedback_Veg", buffer_feedback);

    compute_calulate_lod.load("Samples/Earthworks_4/hlsl/terrain/compute_vegetation_lod.hlsl");
    compute_calulate_lod.Vars()->setBuffer("DrawArgs_Quads", drawArgs_billboards);
    compute_calulate_lod.Vars()->setBuffer("DrawArgs_Plants", drawArgs_vegetation);
    compute_calulate_lod.Vars()->setBuffer("plant_buffer", plantData);
    compute_calulate_lod.Vars()->setBuffer("instance_buffer", instanceData);
    compute_calulate_lod.Vars()->setBuffer("instance_buffer_billboard", instanceData_Billboards);
    compute_calulate_lod.Vars()->setBuffer("block_buffer", blockData_preSort);
    compute_calulate_lod.Vars()->setBuffer("feedback", buffer_feedback);
    compute_calulate_lod.Vars()->setBuffer("sort", buffer_gpuSort);


    compute_sortCombine.add("_PRE", "");
    compute_sortCombine.load("Samples/Earthworks_4/hlsl/terrain/compute_vegetation_sortCombine.hlsl");
    compute_sortCombine.Vars()->setBuffer("DrawArgs_Plants", drawArgs_vegetation);
    compute_sortCombine.Vars()->setBuffer("pre_block_buffer", blockData_preSort);
    compute_sortCombine.Vars()->setBuffer("post_block_buffer", blockData);
    compute_sortCombine.Vars()->setBuffer("feedback", buffer_feedback);
    compute_sortCombine.Vars()->setBuffer("sort", buffer_gpuSort);


    compute_sortCombine_POST.load("Samples/Earthworks_4/hlsl/terrain/compute_vegetation_sortCombine.hlsl");
    compute_sortCombine_POST.Vars()->setBuffer("DrawArgs_Plants", drawArgs_vegetation);
    compute_sortCombine_POST.Vars()->setBuffer("pre_block_buffer", blockData_preSort);
    compute_sortCombine_POST.Vars()->setBuffer("post_block_buffer", blockData);
    compute_sortCombine_POST.Vars()->setBuffer("feedback", buffer_feedback);
    compute_sortCombine_POST.Vars()->setBuffer("sort", buffer_gpuSort);



    {
        rgb_data = Buffer::createStructured(sizeof(uint4), 256 * 128);

        compute_sampleRGBtoPixel.load("Samples/Earthworks_4/hlsl/terrain/compute_sampleRGBtoPixel.hlsl");
        compute_sampleRGBtoPixel.Vars()->setTexture("gIn", rgbFbo->getColorTexture(0));
        compute_sampleRGBtoPixel.Vars()->setTexture("gOut", RGB_MAP);
        compute_sampleRGBtoPixel.Vars()->setBuffer("data", rgb_data);

        compute_sampleRGBtoPixel_ToTexture.add("_TO_TEXTURE", "");
        compute_sampleRGBtoPixel_ToTexture.load("Samples/Earthworks_4/hlsl/terrain/compute_sampleRGBtoPixel.hlsl");
        compute_sampleRGBtoPixel_ToTexture.Vars()->setTexture("gIn", rgbFbo->getColorTexture(0));
        compute_sampleRGBtoPixel_ToTexture.Vars()->setTexture("gOut", RGB_MAP);
        compute_sampleRGBtoPixel_ToTexture.Vars()->setBuffer("data", rgb_data);
    }

    builInstanceBuffer();

    Sampler::Desc samplerDesc;
    samplerDesc.setAddressingMode(Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp).setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear).setMaxAnisotropy(4);
    sampler_ClampAnisotropic = Sampler::create(samplerDesc);

    samplerDesc.setAddressingMode(Sampler::AddressMode::Clamp, Sampler::AddressMode::Wrap, Sampler::AddressMode::Wrap).setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear).setMaxAnisotropy(1);
    sampler_Ribbons = Sampler::create(samplerDesc);

    samplerDesc.setAddressingMode(Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp).setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear).setMaxAnisotropy(1);
    samplerDesc.setComparisonMode(Sampler::ComparisonMode::LessEqual);
    sampler_Depth = Sampler::create(samplerDesc);


    vegetationShader_RGB_SAMPLE.add("_RGB_SAMPLE", "");
    vegetationShader_RGB_SAMPLE.load("Samples/Earthworks_4/hlsl/terrain/render_vegetation_ribbons.hlsl", "vsMain", "psMain", Vao::Topology::LineStrip, "gsMain");
    vegetationShader_RGB_SAMPLE.Vars()->setBuffer("plant_buffer", plantData);
    vegetationShader_RGB_SAMPLE.Vars()->setBuffer("plant_pivot_buffer", plantpivotData);
    vegetationShader_RGB_SAMPLE.Vars()->setBuffer("instance_buffer", instanceData);
    vegetationShader_RGB_SAMPLE.Vars()->setBuffer("block_buffer", blockData);
    vegetationShader_RGB_SAMPLE.Vars()->setBuffer("vertex_buffer", vertexData);
    vegetationShader_RGB_SAMPLE.Vars()->setBuffer("materials", _plantMaterial::static_materials_veg.sb_vegetation_Materials);
    vegetationShader_RGB_SAMPLE.Vars()->setSampler("gSmpLinear", sampler_Ribbons);              // fixme only cvlamlX
    vegetationShader_RGB_SAMPLE.Vars()->setSampler("gSamplerDepth", sampler_Depth);
    vegetationShader_RGB_SAMPLE.Vars()->setSampler("gSmpLinearClamp", sampler_ClampAnisotropic);              // fixme only cvlamlX
    vegetationShader_RGB_SAMPLE.Vars()->setTexture("highResShadow", shadowFbo->getDepthStencilTexture());
    auto& blockRGBsample = vegetationShader_RGB_SAMPLE.Vars()->getParameterBlock("textures");
    varTextures_RGBSample = blockRGBsample->findMember("T");

    vegetationShader_DEPTH.add("_DEPTH", "");
    vegetationShader_DEPTH.load("Samples/Earthworks_4/hlsl/terrain/render_vegetation_ribbons.hlsl", "vsMain", "psMain", Vao::Topology::LineStrip, "gsMain");
    vegetationShader_DEPTH.Vars()->setBuffer("plant_buffer", plantData);
    vegetationShader_DEPTH.Vars()->setBuffer("plant_pivot_buffer", plantpivotData);
    vegetationShader_DEPTH.Vars()->setBuffer("instance_buffer", instanceData);
    vegetationShader_DEPTH.Vars()->setBuffer("block_buffer", blockData);
    vegetationShader_DEPTH.Vars()->setBuffer("vertex_buffer", vertexData);
    vegetationShader_DEPTH.Vars()->setBuffer("materials", _plantMaterial::static_materials_veg.sb_vegetation_Materials);
    vegetationShader_DEPTH.Vars()->setSampler("gSmpLinear", sampler_Ribbons);              // fixme only cvlamlX
    vegetationShader_DEPTH.Vars()->setSampler("gSmpLinearClamp", sampler_ClampAnisotropic);              // fixme only cvlamlX
    auto& blockDepth = vegetationShader_DEPTH.Vars()->getParameterBlock("textures");
    varTextures_Depth = blockDepth->findMember("T");



    billboardShader.add("_BILLBOARD", "");
    billboardShader.load("Samples/Earthworks_4/hlsl/terrain/render_vegetation_ribbons.hlsl", "vsMain", "psMain", Vao::Topology::PointList, "gsMain");
    billboardShader.Vars()->setBuffer("plant_buffer", plantData);
    billboardShader.Vars()->setBuffer("instance_buffer", instanceData_Billboards);
    billboardShader.Vars()->setBuffer("block_buffer", blockData);
    billboardShader.Vars()->setBuffer("vertex_buffer", vertexData);
    billboardShader.Vars()->setBuffer("materials", _plantMaterial::static_materials_veg.sb_vegetation_Materials);
    billboardShader.Vars()->setSampler("gSmpLinear", sampler_Ribbons);              // fixme only cvlamlX
    billboardShader.Vars()->setSampler("gSmpLinearClamp", sampler_ClampAnisotropic);              // fixme only cvlamlX
    auto& blockBB = billboardShader.Vars()->getParameterBlock("textures");
    varBBTextures = blockBB->findMember("T");

    bakeShader.add("_BAKE", "");
    bakeShader.load("Samples/Earthworks_4/hlsl/terrain/render_vegetation_ribbons.hlsl", "vsMain", "psMain", Vao::Topology::LineStrip, "gsMain");
    bakeShader.Vars()->setBuffer("plant_buffer", plantData);
    bakeShader.Vars()->setBuffer("instance_buffer", instanceData);
    bakeShader.Vars()->setBuffer("block_buffer", blockData);
    bakeShader.Vars()->setBuffer("vertex_buffer", vertexData);
    bakeShader.Vars()->setBuffer("materials", _plantMaterial::static_materials_veg.sb_vegetation_Materials);
    bakeShader.Vars()->setSampler("gSmpLinear", sampler_Ribbons);              // fixme only cvlamlX
    auto& blockB = bakeShader.Vars()->getParameterBlock("textures");
    varBakeTextures = blockB->findMember("T");

    reloadShader();

    RasterizerState::Desc rsDesc;
    rsDesc.setFillMode(RasterizerState::FillMode::Solid).setCullMode(RasterizerState::CullMode::None);
    rasterstate = RasterizerState::create(rsDesc);

    BlendState::Desc blendDesc;
    blendDesc.setRtBlend(0, true);
    blendDesc.setAlphaToCoverage(false);
    //blendDesc.setRtParams(0, BlendState::BlendOp::Add, BlendState::BlendOp::Add, BlendState::BlendFunc::SrcAlpha, BlendState::BlendFunc::OneMinusSrcAlpha, BlendState::BlendFunc::Zero, BlendState::BlendFunc::Zero);
    //remove ROP
    blendDesc.setRtParams(0, BlendState::BlendOp::Add, BlendState::BlendOp::Add, BlendState::BlendFunc::One, BlendState::BlendFunc::Zero, BlendState::BlendFunc::Zero, BlendState::BlendFunc::Zero);
    blendstate = BlendState::create(blendDesc);

    blendDesc.setRtBlend(0, true);
    blendDesc.setAlphaToCoverage(true);
    blendstate_withAlpha = BlendState::create(blendDesc);

    //blendDesc.setRtBlend(0, false); onnodig
    blendDesc.setAlphaToCoverage(false);
    blendDesc.setIndependentBlend(true);
    for (int i = 0; i < 8; i++)
    {
        // clear all
        blendDesc.setRenderTargetWriteMask(i, true, true, true, true);
        blendDesc.setRtBlend(i, true);
        blendDesc.setRtParams(i, BlendState::BlendOp::Add, BlendState::BlendOp::Add, BlendState::BlendFunc::SrcAlpha, BlendState::BlendFunc::OneMinusSrcAlpha, BlendState::BlendFunc::SrcAlphaSaturate, BlendState::BlendFunc::One);
    }
    blendDesc.setRtParams(3, BlendState::BlendOp::Add, BlendState::BlendOp::Add, BlendState::BlendFunc::SrcAlpha, BlendState::BlendFunc::OneMinusSrcAlpha, BlendState::BlendFunc::SrcAlphaSaturate, BlendState::BlendFunc::One);
    blendDesc.setRtParams(0, BlendState::BlendOp::Add, BlendState::BlendOp::Add, BlendState::BlendFunc::One, BlendState::BlendFunc::Zero, BlendState::BlendFunc::SrcAlphaSaturate, BlendState::BlendFunc::One);
    blendstateBake = BlendState::create(blendDesc);



    compute_bakeFloodfill.load("Samples/Earthworks_4/hlsl/terrain/compute_bakeFloodfill.hlsl");

    billboardShader.State()->setRasterizerState(rasterstate);
    billboardShader.State()->setBlendState(blendstate);

    // Perlin lookup buffer
    const siv::PerlinNoise perlin{ (uint)101 };
    float sum = 0;
    for (int i = 0; i < 1024; i++)
    {
        perlinData[i] = (float)perlin.normalizedOctave1D((float)i / 8.f, 4, 0.5);
        //fprintf(terrafectorSystem::_logfile, "%f, ", perlinData[i]);
        sum += perlinData[i];
    }
    //fprintf(terrafectorSystem::_logfile, "\n sum %f, \n\n", sum);

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
        }
    }
}







// STRIP-REVIEW: kept during editor-GUI strip â€” GUI function, but body contains bake/build orchestration that mutates persistent build state (settings, lodBake, saved files). Only reachable from removed editor GUI.
void _rootPlant::renderGui_Lodding(Gui* _gui)
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
            //ImGui::PushFont(_gui->getFont("default"));

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
                            if (ImGui::DragFloat("##pixelSize-mm", &(lodInfo->pixelSize), 0.05f, 0.5f, 500.f, "%2.1fmm", 2.f))
                            {
                                settings.pixelSize = lodInfo->pixelSize * 0.001f;
                                selectedPart->changed = anyChange = true;
                            }
                            TOOLTIP("build detail");
                        }
                        ImGui::PopID();
                    }



                    ImGui::PushStyleColor(ImGuiCol_FrameBg, (currentLOD == lod) ? highlight : normal);
                    ImGui::BeginChildFrame(5678 + lod, ImVec2(columnWidth - 25, lineHeight * 2));
                    {
                        if (ImGui::BeginPopupContextWindow(false))
                        {
                            if (ImGui::Selectable("delete")) { selectedPart->deleteLod(lod); }
                            if (ImGui::Selectable("insert - before")) { selectedPart->insertLod(lod); }
                            ImGui::EndPopup();
                        }

                        ImGui::PushFont(_gui->getFont("header2"));
                        {
                            ImGui::Text("%d)", lod);
                        }
                        ImGui::PopFont();

                        ImGui::SameLine(100, 0);
                        ImGui::Text("%d: verts", lodInfo->numVerts);

                        ImGui::PushFont(_gui->getFont("header2"));
                        {
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.1f, 0.1f, 1.0f));
                            //ImGui::PushFont(_gui->getFont("header2"));
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
                        ImGui::PopFont();


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


                        //ImGui::Text("%d: verts", lodInfo->numVerts);
                        /*ImGui::SameLine(0, 30);
                        ImGui::SetNextItemWidth(60);
                        if (ImGui::DragFloat("scale", &lodInfo->geometryPixelScale, 0.01f, 0.1f, 10)) { selectedPart->changed = true; }
                        TOOLTIP("Not used but will add extra scaling to geometry to help select whats added, seperates what we add with the distance we split at")
                        */
                    }
                    ImGui::EndChildFrame();
                    ImGui::PopStyleColor();
                }
            }

            //ImGui::PopFont();
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



// STRIP-REVIEW: kept during editor-GUI strip â€” GUI function, but body contains bake/build orchestration that mutates persistent build state (settings, lodBake, saved files). Only reachable from removed editor GUI.
void _rootPlant::renderGui_Baking(Gui* _gui)
{
    if (!selectedPart) return;

    uint gui_id = 199994;
    float columnWidth = ImGui::GetWindowWidth() - 10;
    int flags = ImGuiTreeNodeFlags_Framed;
    auto& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Header] = ImVec4(0.06f, 0.01f, 0.01f, 1.f);

    ImGui::PushFont(_gui->getFont("header2"));
    if (ImGui::TreeNodeEx("Baking", flags))
    {
        ImGui::PushFont(_gui->getFont("default"));
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
                        glm::mat4 root = selectedPart->getRoot(bakeLod->clipDead);
                        bakeViewAdjusted = bakeViewMatrix = root;
                        float3 u = glm::normalize((float3)tip[3] - (float3)root[3]);
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
                    float columnWidth = ImGui::GetWindowWidth() - 10;
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, (i == showBake) ? highlight : normal);
                    ImGui::BeginChildFrame(15678 + i, ImVec2(columnWidth - 25, lineHeight));
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

                            //CHECKBOX("include tip", &bake->includeTip, "Include the tip if this is a stem");
                            //CHECKBOX("remove dead", &bake->clipDead, "Cull the bottom bit if its dead");
                            // 
                            //R_FLOAT("pitch", bake->pitch, 0.01f, 0.1f, 10.f, "0 for all large plants \nslightly tilted for smaller plants especially bake - 0");
                            //R_FLOAT("yaw", bake->yaw, 0.01f, 0.1f, 10.f, "try to keep this 0, it can break random variations");
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
                            //CHECKBOX("bake AO", &bake->bakeAOToAlbedo, "if used as a billboard, bake the ao to the texture itself for added depth\nif used as a code with vertex ao, leave out");
                            //CHECKBOX("bake alpha", &bake->useAlphaInBake, "Bake an alpha oval for smoother side and top crossovers");


                        }

                        selectedPart->changed |= changed;
                    }
                    ImGui::EndChildFrame();
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemClicked(0)) showBake = i;
                }
            }

        }
        ImGui::PopFont();
        ImGui::TreePop();
    }
    ImGui::PopFont();
}




// STRIP-REVIEW: texture-tool cluster (initTextureTool/GenerateATexture/exportTextures + largeTexture/oneTexture) â€” authoring-only, reachable only from removed GUI, but substantive render-to-texture logic (TextureSplitTool derives from it). Kept for reference.
void _rootPlant::initTextureTool()
{
    static bool first = true;
    if (first)
    {
        //???LineStrip
        textureExtractShader.load("Samples/Earthworks_4/hlsl/terrain/extractTextures.hlsl", "vsMain", "psMain", Vao::Topology::PointList, "gsMain");
        textureExtractShader.Vars()->setSampler("gSmpLinear", sampler_Ribbons);              // fixme only cvlamlX

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

    if (!textureToolData.fbo || textureToolData.fbo->getWidth() != w || textureToolData.fbo->getHeight() != h)
    {

        Fbo::Desc desc;
        desc.setDepthStencilTarget(ResourceFormat::D24UnormS8);			// keep for now, not sure why, but maybe usefult for cuts
        desc.setColorTarget(0u, ResourceFormat::RGBA8Unorm, true);		// albedo
        desc.setColorTarget(1u, ResourceFormat::RGBA8Unorm, true);		// normal_8
        desc.setColorTarget(2u, ResourceFormat::RGBA8Unorm, true);	    // translucnecy
        desc.setColorTarget(3u, ResourceFormat::RGBA8Unorm, true);	    // extra
        desc.setColorTarget(4u, ResourceFormat::RGBA8Unorm, true);	    // 45degree lit

        textureToolData.fbo = Fbo::create2D(w, h, desc, 1, 4);
    }

    const glm::vec4 clearColor(0.5, 0.5f, 1.0f, 0.0f);
    renderInfo.context->clearFbo(textureToolData.fbo.get(), clearColor, 1.0f, 0, FboAttachmentType::All);    // depth
    renderInfo.context->clearRtv(textureToolData.fbo->getRenderTargetView(0).get(), glm::vec4(0.0, 0.0f, 0.0f, 0.0f));
    renderInfo.context->clearRtv(textureToolData.fbo->getRenderTargetView(1).get(), glm::vec4(0.5, 0.5f, 1.0f, 0.0f));
    renderInfo.context->clearRtv(textureToolData.fbo->getRenderTargetView(2).get(), glm::vec4(0.0, 0.0f, 0.0f, 0.0f));
    renderInfo.context->clearRtv(textureToolData.fbo->getRenderTargetView(3).get(), glm::vec4(0.0, 0.0f, 0.0f, 0.0f));
    renderInfo.context->clearRtv(textureToolData.fbo->getRenderTargetView(4).get(), glm::vec4(0.0, 0.0f, 0.0f, 0.0f));

    float2 dir = glm::normalize(M.stop - M.start);
    float2 norm = float2(dir.y, -dir.x);

    float2 A = M.start + norm * M.width;
    float2 B = M.start - norm * M.width;
    float2 C = M.stop - norm * M.width;
    float2 D = M.stop + norm * M.width;

    //bakeShader
    textureExtractShader.State()->setFbo(textureToolData.fbo);
    textureExtractShader.State()->setViewport(0, GraphicsState::Viewport(0, 0, (float)w, (float)h, 0, 1), true);
    textureExtractShader.State()->setRasterizerState(rasterstate);
    textureExtractShader.State()->setBlendState(blendstateBake);

    textureExtractShader.Vars()["gConstantBuffer"]["flipRed"] = textureToolData.flipRed;
    textureExtractShader.Vars()["gConstantBuffer"]["flipGreen"] = textureToolData.flipGreen;
    textureExtractShader.Vars()["gConstantBuffer"]["nStrength"] = textureToolData.normalStrenth;
    textureExtractShader.Vars()["gConstantBuffer"]["toSRGB"] = toSRGB;



    textureExtractShader.Vars()["gConstantBuffer"]["A"] = A;
    textureExtractShader.Vars()["gConstantBuffer"]["B"] = B;
    textureExtractShader.Vars()["gConstantBuffer"]["C"] = C;
    textureExtractShader.Vars()["gConstantBuffer"]["D"] = D;

    textureExtractShader.Vars()["gConstantBuffer"]["start"] = M.start;
    textureExtractShader.Vars()["gConstantBuffer"]["stop"] = M.stop;
    textureExtractShader.Vars()["gConstantBuffer"]["bezier"] = M.bezier;
    textureExtractShader.Vars()["gConstantBuffer"]["width"] = M.width;

    textureExtractShader.Vars()->setTexture("galbedo", textureToolData.tex_albedo);
    textureExtractShader.Vars()->setTexture("galpha", textureToolData.tex_alpha);
    textureExtractShader.Vars()->setTexture("gnormal", textureToolData.tex_normal);
    textureExtractShader.Vars()->setTexture("gtranslucency", textureToolData.tex_translucency);

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
        GenerateATexture(idx, true);

        textureToolData.fbo->getColorTexture(0)->captureToFile(0, 0, terrafectorEditorMaterial::rootFolder + textureToolData.path + "_" + std::to_string(idx) + "_albedo.png", Bitmap::FileFormat::PngFile, Falcor::Bitmap::ExportFlags::ExportAlpha);
        std::string cmdExp = "del " + terrafectorEditorMaterial::rootFolder + textureToolData.path + "_" + std::to_string(idx) + "_albedo.png.earthworks.dds";
        replaceAllveg(cmdExp, "/", "\\");
        system(cmdExp.c_str());

        if (textureToolData.tex_normal)
        {
            textureToolData.fbo->getColorTexture(1)->captureToFile(0, 0, terrafectorEditorMaterial::rootFolder + textureToolData.path + "_" + std::to_string(idx) + "_normal.png");
            std::string cmdExp = "del " + terrafectorEditorMaterial::rootFolder + textureToolData.path + "_" + std::to_string(idx) + "_albedo.png.earthworks.dds";
            replaceAllveg(cmdExp, "/", "\\");
            system(cmdExp.c_str());

        }
        if (textureToolData.tex_translucency)
        {
            textureToolData.fbo->getColorTexture(2)->captureToFile(0, 0, terrafectorEditorMaterial::rootFolder + textureToolData.path + "_" + std::to_string(idx) + "_translucency.png");
            std::string cmdExp = "del " + terrafectorEditorMaterial::rootFolder + textureToolData.path + "_" + std::to_string(idx) + "_albedo.png.earthworks.dds";
            replaceAllveg(cmdExp, "/", "\\");
            system(cmdExp.c_str());
        }




        idx++;
    }

}























void _rootPlant::buildAllLods()
{
    _ribbonBuilder.clearPivot();
    LOGTHEBUILD = true;
    if (LOGTHEBUILD)
    {
        fprintf(terrafectorSystem::_logfile, "\nbuildAllLods()n");
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
        fprintf(terrafectorSystem::_logfile, "\n\npIndex=%d\n", pIndex);
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

                for (int pvt = 0; pvt < 256; pvt++)
                {
                    allPivots[pIndex * 256 + pvt] = _ribbonBuilder.pivotPoints[pvt];
                }


                startBlock[pIndex][lod] = start;
                numBlocks[pIndex][lod] = lodInfo->numBlocks;

                plantBuf[pIndex].numLods = __max(plantBuf[pIndex].numLods, lod);    // omdat ons nou agteruit gaan
                plantBuf[pIndex].lods[lod - 1].pixSize = (float)lodInfo->numPixels;
                plantBuf[pIndex].lods[lod - 1].numBlocks = lodInfo->numBlocks;
                plantBuf[pIndex].lods[lod - 1].startVertex = start;

                start += lodInfo->numBlocks;

                fprintf(terrafectorSystem::_logfile, "plant lod : %d, %fmm, %d verts, %d blocks\n", lod, (float)lodInfo->pixelSize, lodInfo->numVerts, lodInfo->numBlocks);
            }
        }
    }

    // Now log this
    fprintf(terrafectorSystem::_logfile, "\n\nbuildAllLods() : %s\n", root->name.c_str());
    fprintf(terrafectorSystem::_logfile, "  size : %2.2f, %2.2f\n", plantBuf[0].size.x, plantBuf[0].size.y);
    fprintf(terrafectorSystem::_logfile, "  lod, blocks, startV, pixSize\n");
    for (int i = 0; i < plantBuf[0].numLods; i++)
    {
        fprintf(terrafectorSystem::_logfile, "  %d : %d, %d, %2.2f\n", i, plantBuf[0].lods[i].numBlocks, plantBuf[0].lods[i].startVertex, plantBuf[0].lods[i].pixSize);
    }


    plantData->setBlob(plantBuf.data(), 0, 8 * sizeof(plant));

    builInstanceBuffer();
    fprintf(terrafectorSystem::_logfile, "  just set plants\n");

    int numV = __min(65536 * 8, _ribbonBuilder.numPacked());
    vertexData->setBlob(_ribbonBuilder.getPackedData(), 0, numV * sizeof(ribbonVertex8));                // FIXME uploads should be smaller
    fprintf(terrafectorSystem::_logfile, "  just set verts (%d), packed %d, numMaterials %d\n", numV, (int)_ribbonBuilder.packed.size(), (int)_plantMaterial::static_materials_veg.materialVector.size());
    for (int i = 0; i < (int)_plantMaterial::static_materials_veg.materialVector.size(); i++)
    {
        fprintf(terrafectorSystem::_logfile, "    material %d, %s\n", i, _plantMaterial::static_materials_veg.materialVector[i].displayName.c_str());
    }

    settings.seed = 1000;


    {
        // NEW VERSION - binary CEREAL
        exportPlant exp;
        int plantCnt = 0;
        for (int p = 0; p < numBinaryPlants; p++)
        {
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
            exp.plants.push_back(plantBuf[p]);
            plantCnt++;
        }

        for (int i = 0; i < numV; i++)
        {
            int idx = (_ribbonBuilder.packed[i].b >> 8) & 0x3ff;
            _vegMaterial M;
            if (idx >= 0 && idx < _plantMaterial::static_materials_veg.materialVector.size())
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


    {

    }

    {

        /*/ LaTex
        using namespace LatexGen;
        Article article("Using Indexes in LatexGenCpp", "Earthworks - 4", "April 19, 2025");
        //article.addPackage("listings");
        {
            Section intro("Introduction", Section::Level::SECTION);
            intro.addContent("This document presents how to use indexing features with LatexGenCpp.");
            article.addSection(intro);
        }
        article.saveToFile("output", "F:/example_index.tex");
        */
    }
    /*
    binaryPlantOnDisk OnDisk;

    OnDisk.numP = numBinaryPlants;
    OnDisk.numV = numV;
    for (int i = 0; i < numV; i++)
    {
        int idx = (_ribbonBuilder.packed[i].b >> 8) & 0x3ff;
        _vegMaterial M;
        M.path = _plantMaterial::static_materials_veg.materialVector[idx].relativePath;
        M.name = _plantMaterial::static_materials_veg.materialVector[idx].displayName;
        M.index = idx;
        OnDisk.materials[idx] = M;
    }
    fprintf(terrafectorSystem::_logfile, "  found %d materials % \n", (int)OnDisk.materials.size());

    lodBake* lodZero = root->getBakeInfo(0);
    if (lodZero)
    {
        OnDisk.billboardMaterial = lodZero->material;
    }


    std::string resource = terrafectorEditorMaterial::rootFolder;
    std::ofstream os(resource + root->path + ".binary");
    cereal::JSONOutputArchive archive(os);
    archive(OnDisk);


    std::ofstream osData(resource + root->path + ".binaryData", std::ios::binary);
    osData.write((const char*)plantBuf.data(), OnDisk.numP * sizeof(plant));
    osData.write((const char*)_ribbonBuilder.getPackedData(), numV * sizeof(ribbonVertex8));
    // If n ot existing en pand the ivot points
    //if (_ribbonBuilder.pivotPoints.size() < 256) _ribbonBuilder.pivotPoints.resize(256);
    osData.write((const char*)allPivots.data(), OnDisk.numP * 256 * sizeof(_plant_anim_pivot));
    */
}

/*
void binaryPlantOnDisk::onLoad(std::string path, uint vOffset)
{
    fprintf(terrafectorSystem::_logfile, "\n\n onLoad()  %s\n", path.c_str());
    plantData.resize(numP);
    vertexData.resize(numV);
    pivotData.resize(numP * 256);

    std::ifstream osData(path + "Data", std::ios::binary);
    osData.read((char*)plantData.data(), numP * sizeof(plant));
    osData.read((char*)vertexData.data(), numV * sizeof(ribbonVertex8));
    osData.read((char*)pivotData.data(), numP * 256 * sizeof(_plant_anim_pivot));

    fprintf(terrafectorSystem::_logfile, "%d plants, %d verts, %d pivots\n", numP, numV, numP * 256);

    // load materials, and build remapper
    std::string resource = terrafectorEditorMaterial::rootFolder;
    int indexLookup[4096];// just big, bad code
    for (auto& M : materials)
    {
        indexLookup[M.first] = _plantMaterial::static_materials_veg.find_insert_material(std::filesystem::path(resource + M.second.path), false); //terrafectorEditorMaterial::rootFolder +
    }

    int billboardIndex = _plantMaterial::static_materials_veg.find_insert_material(std::filesystem::path(resource + billboardMaterial.path), false);

    for (auto& V : vertexData)
    {
        int idx = (V.b >> 8) & 0x3ff;
        V.b ^= (idx << 8);  // xor clears
        V.b += (indexLookup[idx] << 8);
    }

    vOffset /= VEG_BLOCK_SIZE;
    fprintf(terrafectorSystem::_logfile, "set voFFSET %d BLOCKS\n", vOffset);
    for (auto& P : plantData)
    {
        P.billboardMaterialIndex = billboardIndex;
        for (int i = 0; i < P.numLods; i++)
        {
            fprintf(terrafectorSystem::_logfile, "LOD %d, start %d, size %d, pixSize %2.2f\n", i, P.lods[i].startVertex, P.lods[i].numBlocks, P.lods[i].pixSize);
            P.lods[i].startVertex += vOffset;  // count in blocks
        }
    }
}
*/





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
    // its should also say how many variations it has, ort we settle on 3, still say, treesl likely 1 unless yhou caounty werid over years

    exportPlant exp;
    std::ifstream is(filepath, std::ios::binary);
    cereal::BinaryInputArchive archive(is);
    archive(exp);

    fprintf(terrafectorSystem::_logfile, "importBinary %s\n", filepath.string().c_str());
    fprintf(terrafectorSystem::_logfile, "{%d}p {%d}pvt {%d}v\n", (int)exp.plants.size(), (int)exp.pivots.size(), (int)exp.vertexbuff.size());
    // This one shoudl reslolve duplicate materials and modiffy the vertex buffer and load textures
    // how the hgell ddidi the previous version work
    {
        // load materials, and build remapper
        std::string resource = terrafectorEditorMaterial::rootFolder;
        int indexLookup[4096];// just big, bad code
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

        fprintf(terrafectorSystem::_logfile, "set voFFSET %d BLOCKS\n", blockOffset);
        int cntP = 0;
        for (auto& P : exp.plants)
        {
            // FIXME different for each plant
            P.billboardMaterialIndex = _plantMaterial::static_materials_veg.find_insert_material(std::filesystem::path(resource + exp.billboardMaterials[cntP].path), false);
            for (int i = 0; i < P.numLods; i++)
            {
                fprintf(terrafectorSystem::_logfile, "LOD %d, start %d, size %d, pixSize %2.2f\n", i, P.lods[i].startVertex, P.lods[i].numBlocks, P.lods[i].pixSize);
                P.lods[i].startVertex += blockOffset;  // count in blocks
            }
            cntP++;
        }
    }

    plantData->setBlob(exp.plants.data(), binPlantOffset, exp.plants.size() * sizeof(plant));
    vertexData->setBlob(exp.vertexbuff.data(), binVertexOffset, exp.vertexbuff.size() * sizeof(ribbonVertex8));
    plantpivotData->setBlob(exp.pivots.data(), binPivotOffset, exp.pivots.size() * sizeof(_plant_anim_pivot));  //??? dow I save 256 blocks

    binVertexOffset += exp.vertexbuff.size() * sizeof(ribbonVertex8);
    cntV_Offset += exp.vertexbuff.size();
    binPlantOffset += exp.plants.size() * sizeof(plant);
    binPivotOffset += exp.plants.size() * 256 * sizeof(_plant_anim_pivot);  // LIKELE veryy wrone., we should 
    numBinaryPlants++;

    displayModeSinglePlant = false;
    _ribbonBuilder.packed.resize(exp.vertexbuff.size());  //??? WHY this is just to fool my render function later that checks this size to see if its loaded
    updateMaterialsAndTextures(); // this loads it to GPU

    importPathVector.push_back(filepath.string());
    return importPathVector.size() - 1; // Bit wrong since we load 3 variations, return more info

    /*
    binaryPlantOnDisk OnDisk;
    std::ifstream os(filepath);
    cereal::JSONInputArchive archive(os);
    archive(OnDisk);

    OnDisk.onLoad(filepath.string(), binVertexOffset / sizeof(ribbonVertex8));

    plantData->setBlob(OnDisk.plantData.data(), binPlantOffset, OnDisk.numP * sizeof(plant));

    int numV = __min(65536 * 8, OnDisk.numV);
    vertexData->setBlob(OnDisk.vertexData.data(), binVertexOffset, numV * sizeof(ribbonVertex8));

    plantpivotData->setBlob(OnDisk.pivotData.data(), binPivotOffset, OnDisk.numP * 256 * sizeof(_plant_anim_pivot));

    fprintf(terrafectorSystem::_logfile, "sizeof(ribbonVertex8) %d\n", (int)sizeof(ribbonVertex8));
    fprintf(terrafectorSystem::_logfile, "sizeof(plant) %d\n", (int)sizeof(plant));
    fprintf(terrafectorSystem::_logfile, "sizeof(_plant_anim_pivot) %d\n", (int)sizeof(_plant_anim_pivot));

    binVertexOffset += numV * sizeof(ribbonVertex8);
    binPlantOffset += OnDisk.numP * sizeof(plant);
    binPivotOffset += OnDisk.numP * 256 * sizeof(_plant_anim_pivot);
    numBinaryPlants++;

    displayModeSinglePlant = false;
    _ribbonBuilder.packed.resize(OnDisk.numV);  //??? WHY this is just to fool my render function later that checks this size to see if its loaded
    updateMaterialsAndTextures();

    importPathVector.push_back(filepath.string());
    return importPathVector.size() - 1; // Bit wrong since we load 4 variations
    */
}






void _rootPlant::build(uint pivotOffset)
{
    if (!root) return;
    auto start = high_resolution_clock::now();

    // Clear some data beforehand - only on single plant, all lods have to call this as well
    _ribbonBuilder.mat_vector_size_Sanity = _plantMaterial::static_materials_veg.materialVector.size();
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
    //fprintf(terrafectorSystem::_logfile, "ROOT SEED  %d\n", settings.seed);


    root->clear_build_info();
    root->build(settings, true);

    // Now light the plant
    _ribbonBuilder.lightBasic(extents, root->shadowDepth, root->shadowPenetationHeight);
    _ribbonBuilder.pack();
    _ribbonBuilder.finalizeAndFillLastBlock();

    if (_ribbonBuilder.numPacked() > 0)
    {
        updateMaterialsAndTextures();

        int numV = __min(65536 * 8, _ribbonBuilder.numPacked());
        vertexData->setBlob(_ribbonBuilder.getPackedData(), 0, numV * sizeof(ribbonVertex8));

        std::array<block_data, 65536> blockBuf; // allows 2 million triangles
        totalBlocksToRender = __min(65536, _ribbonBuilder.numPacked() / VEG_BLOCK_SIZE);   // move to ribbonvertex
        for (int j = 0; j < totalBlocksToRender; j++)
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






// STRIP-REVIEW: bake64kplants â€” only caller was buildAllLods; output jpg was consumed by the removed PDF catalog export. Kept: full multi-view indirect-render example incl. frustum-plane extraction.
void _rootPlant::bake64kplants(std::string _path)
{
    Fbo::Desc desc;
    desc.setDepthStencilTarget(ResourceFormat::D24UnormS8);			// keep for now, not sure why, but maybe usefult for cuts
    desc.setColorTarget(0u, ResourceFormat::RGBA8Unorm, true);		// albedo
    Fbo::SharedPtr fbo = Fbo::create2D(1800, 600, desc, 1, 4);
    const glm::vec4 clearColor(0.2, 0.2f, 0.2f, 0.0f);
    

    glm::mat4 CAM = glm::mat4(1);
    //GROW(CAM, -extents.y * 15.f);
    PITCH(CAM, -0.3f);
    GROW(CAM, extents.y * 5.f);
    CAM[3][1] += 1000.f;
    glm::mat4 V = glm::inverse(CAM);
    glm::mat4 P = glm::perspectiveRH(0.5f, 3.f, 1.0f, 10000.0f);
    glm::mat4 VP = P * V;

    rmcv::mat4 viewproj, view, _clipFrustum;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            viewproj[j][i] = VP[j][i];
            view[j][i] = V[j][i];
        }
    }

    _clipFrustum[0][0] = P[0][3] + P[0][0];
    _clipFrustum[0][1] = P[1][3] + P[1][0];
    _clipFrustum[0][2] = P[2][3] + P[2][0];
    _clipFrustum[0][3] = P[3][3] + P[3][0];

    _clipFrustum[1][0] = P[0][3] - P[0][0];
    _clipFrustum[1][1] = P[1][3] - P[1][0];
    _clipFrustum[1][2] = P[2][3] - P[2][0];
    _clipFrustum[1][3] = P[3][3] - P[3][0];

    _clipFrustum[2][0] = P[0][3] + P[0][1];
    _clipFrustum[2][1] = P[1][3] + P[1][1];
    _clipFrustum[2][2] = P[2][3] + P[2][1];
    _clipFrustum[2][3] = P[3][3] + P[3][1];

    _clipFrustum[3][0] = P[0][3] - P[0][1];
    _clipFrustum[3][1] = P[1][3] - P[1][1];
    _clipFrustum[3][2] = P[2][3] - P[2][1];
    _clipFrustum[3][3] = P[3][3] - P[3][1];

    for (int i = 0; i < 3; i++)
    {
        compute_clearBuffers.dispatch(renderInfo.context, 1, 1);
        {
            compute_calulate_lod.Vars()["gConstantBuffer"]["view"] = view;
            compute_calulate_lod.Vars()["gConstantBuffer"]["frustum"] = _clipFrustum;
            compute_calulate_lod.Vars()["gConstantBuffer"]["eyePos"] = (float3)CAM[3];
            compute_calulate_lod.Vars()["gConstantBuffer"]["lodBias"] = loddingBias;
            compute_calulate_lod.Vars()["gConstantBuffer"]["halfAngle_to_Pixels"] = renderInfo.half_to_Pixels;
            compute_calulate_lod.Vars()["gConstantBuffer"]["firstPlant"] = firstPlant;
            compute_calulate_lod.Vars()["gConstantBuffer"]["lastPlant"] = lastPlant;
            compute_calulate_lod.Vars()["gConstantBuffer"]["firstLod"] = firstLod;
            compute_calulate_lod.Vars()["gConstantBuffer"]["lastLod"] = lastLod;
            compute_calulate_lod.dispatch(renderInfo.context, MAX_PLANT_INSTANCES / 256, 1);
        }
        compute_sortCombine.dispatch(renderInfo.context, 1, 1);

        {
            renderInfo.context->clearFbo(fbo.get(), clearColor, 1.0f, 0, FboAttachmentType::All);    // depth
            vegetationShader.State()->setFbo(fbo);
            vegetationShader.State()->setViewport(0, GraphicsState::Viewport(0, 0, 1800.f, 600.f, 0, 1), true);
            vegetationShader.State()->setRasterizerState(rasterstate);
            vegetationShader.State()->setBlendState(blendstateBake);
            vegetationShader.Vars()["gConstantBuffer"]["view"] = view;
            vegetationShader.Vars()["gConstantBuffer"]["viewproj"] = viewproj;
            vegetationShader.Vars()["gConstantBuffer"]["eyePos"] = (float3)CAM[3];
            vegetationShader.Vars()["gConstantBuffer"]["camRight"] = CAM[0];
            vegetationShader.Vars()["gConstantBuffer"]["camUp"] = CAM[1];
            vegetationShader.Vars()["gConstantBuffer"]["toneMap"] = 1;


            for (int idx = 0; idx < 128; idx++)
            {
                vegetationShader.Vars()["gConstantBuffer"]["drawIndex"] = idx;
                vegetationShader.renderIndirect(renderInfo.context, drawArgs_vegetation, nullptr, idx, 1);
            }
        }

        renderInfo.context->flush(true);

        renderInfo.context->copyResource(buffer_feedback_read.get(), buffer_feedback.get());

        const uint8_t* pData = (uint8_t*)buffer_feedback_read->map(Buffer::MapType::Read);
        std::memcpy(&feedback, pData, sizeof(vegetation_feedback));
        buffer_feedback_read->unmap();
    }

    {
        

        std::filesystem::path PT = _path;
        std::string resource = terrafectorEditorMaterial::rootFolder;
        std::string newRelative = PT.parent_path().string() + "/bake_" + PT.stem().string() + "/";
        std::string newDir = resource + newRelative;
        replaceAllVEG(resource, "/", "\\");
        fbo->getColorTexture(0).get()->captureToFile(0, 0, newDir + "64kplants.jpg", Bitmap::FileFormat::JpegFile, Bitmap::ExportFlags::None);
        Sleep(1000);
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
    if (!root) return;

    fprintf(terrafectorSystem::_logfile, "_rootPlant::bake()\n");

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

    fprintf(terrafectorSystem::_logfile, "bake hgt %d, width %d, lods %d, %2.2f horScale, %2.2f fW pix\n", _info->pixHeight, newIW, maxMIPY, HorizontalScale, fW);


    Fbo::SharedPtr fbo;
    {
        Fbo::Desc desc;
        desc.setDepthStencilTarget(ResourceFormat::D24UnormS8);			// keep for now, not sure why, but maybe usefult for cuts
        desc.setColorTarget(0u, ResourceFormat::RGBA8Unorm, true);		// albedo
        desc.setColorTarget(1u, ResourceFormat::RGBA16Float, true);	    // normal_16
        desc.setColorTarget(2u, ResourceFormat::RGBA8Unorm, true);		// normal_8
        desc.setColorTarget(3u, ResourceFormat::RGBA8Unorm, true);	    // pbr
        desc.setColorTarget(4u, ResourceFormat::RGBA8Unorm, true);	    // extra
        fbo = Fbo::create2D(iW, iH, desc, 1, 4);
        const glm::vec4 clearColor(0.5, 0.5f, 1.0f, 0.0f);
        renderInfo.context->clearFbo(fbo.get(), clearColor, 1.0f, 0, FboAttachmentType::All);    // depth
        renderInfo.context->clearRtv(fbo->getRenderTargetView(0).get(), glm::vec4(0.3, 0.3f, 0.3f, 0.0f));
        renderInfo.context->clearRtv(fbo->getRenderTargetView(1).get(), glm::vec4(0.5, 0.5f, 1.0f, 0.0f));
        renderInfo.context->clearRtv(fbo->getRenderTargetView(2).get(), glm::vec4(0.5, 0.5f, 1.0f, 0.0f));
        renderInfo.context->clearRtv(fbo->getRenderTargetView(3).get(), glm::vec4(0.0, 0.0f, 0.0f, 0.0f));
        renderInfo.context->clearRtv(fbo->getRenderTargetView(4).get(), glm::vec4(1.0, 1.0f, 1.0f, 1.0f));
    }

    //VIEW[0] *= HorizontalScale;
    glm::mat4 V, VP;
    V = glm::inverse(VIEW);
    VP = glm::orthoRH(-W, W, H0, H1, -100.0f, 100.0f) * V;

    rmcv::mat4 viewproj, view;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            viewproj[j][i] = VP[j][i];
            view[j][i] = VIEW[j][i];
        }
    }

    {
        glm::mat4 T = toGLM(view);
        glm::mat4 Tinv = glm::inverse(T);
        rmcv::mat4  _view_T;
        float4 camRight = glm::normalize(T[0]);
        camRight.w = 0;

        float4 camUp = glm::normalize(T[1]);
        camUp.w = 0;
        bakeShader.Vars()["gConstantBuffer"]["camRight"] = camRight;
        bakeShader.Vars()["gConstantBuffer"]["camUp"] = camUp;
    }

    {
        bakeShader.State()->setFbo(fbo);
        bakeShader.State()->setViewport(0, GraphicsState::Viewport(0, 0, (float)iW, (float)iH, 0, 1), true);
        bakeShader.State()->setRasterizerState(rasterstate);
        bakeShader.State()->setBlendState(blendstateBake);
        bakeShader.Vars()["gConstantBuffer"]["view"] = view;
        bakeShader.Vars()["gConstantBuffer"]["viewproj"] = viewproj;
        bakeShader.Vars()["gConstantBuffer"]["eyePos"] = (float3)VIEW[3] + ((float3)VIEW[2] * 1000.f);
        bakeShader.Vars()["gConstantBuffer"]["bake_radius_alpha"] = W;
        bakeShader.Vars()["gConstantBuffer"]["bake_height_alpha"] = H1;
        bakeShader.Vars()["gConstantBuffer"]["bake_AoToAlbedo"] = _info->bakeAOToAlbedo;
        if (_info->useAlphaInBake)
        {
            bakeShader.Vars()["gConstantBuffer"]["bake_AlphaOval"] = _info->alphaOval;
        }
        else
        {
            bakeShader.Vars()["gConstantBuffer"]["bake_AlphaOval"] = float2(0, 0);
        }
        //bakeShader.Vars()["gConstantBuffer"]["bake_Aobake_AlphaOvalToAlbedo"] = _info->
        _plantMaterial::static_materials_veg.setTextures(varBakeTextures);
        _plantMaterial::static_materials_veg.rebuildStructuredBuffer();

        bakeShader.drawInstanced(renderInfo.context, 32, totalBlocksToRender);
    }
    renderInfo.context->flush(true);

    std::filesystem::path PT = _path;
    std::string resource = terrafectorEditorMaterial::rootFolder;
    std::string newRelative = PT.parent_path().string() + "/bake_" + PT.stem().string() + "/";
    std::string newDir = resource + newRelative;
    replaceAllVEG(resource, "/", "\\");
    if (lod_0)
    {
        fbo->getColorTexture(0).get()->captureToFile(0, 0, newDir + _info->material.name + "_FULL_albedo.jpg", Bitmap::FileFormat::JpegFile, Bitmap::ExportFlags::None);
        fbo->getColorTexture(0).get()->captureToFile(0, 0, newDir + _info->material.name + "_FULL_albedo.png", Bitmap::FileFormat::PngFile, Bitmap::ExportFlags::ExportAlpha);
    }

    {
        compute_bakeFloodfill.Vars()->setTexture("gAlbedo", fbo->getColorTexture(0));
        compute_bakeFloodfill.Vars()->setTexture("gNormal", fbo->getColorTexture(2));
        compute_bakeFloodfill.Vars()->setTexture("gTranslucency", fbo->getColorTexture(4));
        compute_bakeFloodfill.Vars()->setTexture("gpbr", fbo->getColorTexture(3));
        for (int i = 0; i < 128; i++)
        {
            compute_bakeFloodfill.dispatch(renderInfo.context, iW / 4, iH / 4);
        }
    }
    renderInfo.context->flush(true);



    {


        _plantMaterial Mat;
        Mat._constData.translucency = 1;
        Mat.albedoPath = newRelative + _info->material.name + "_albedo.dds";
        Mat.albedoName = _info->material.name + "_albedo.dds";
        Mat.normalPath = newRelative + _info->material.name + "_normal.dds";
        Mat.normalName = _info->material.name + "_normal.dds";
        Mat.translucencyPath = newRelative + _info->material.name + "_translucency.dds";
        Mat.translucencyName = _info->material.name + "_translucency.dds";

        fbo->getColorTexture(0).get()->generateMips(renderInfo.context);
        fbo->getColorTexture(1).get()->generateMips(renderInfo.context);
        fbo->getColorTexture(2).get()->generateMips(renderInfo.context);
        fbo->getColorTexture(3).get()->generateMips(renderInfo.context);
        fbo->getColorTexture(4).get()->generateMips(renderInfo.context);

        /*
        std::filesystem::path PT = _path;
        std::string resource = terrafectorEditorMaterial::rootFolder;
        std::string newRelative = PT.parent_path().string() + "/bake_" + PT.stem().string() + "/";
        std::string newDir = resource + newRelative;
        replaceAllVEG(resource, "/", "\\");
        newDir + _info->material.name + "_FULL_albedo.png"

        */

        //fbo->getColorTexture(2).get()->captureToFile(0, 0, newDir + "_FULL_normal.png", Bitmap::FileFormat::PngFile, Bitmap::ExportFlags::None);

        fbo->getColorTexture(0).get()->captureToFile(bakeMipToSave, 0, newDir + "_albedo.png", Bitmap::FileFormat::PngFile, Bitmap::ExportFlags::ExportAlpha);
        fbo->getColorTexture(1).get()->captureToFile(bakeMipToSave, 0, newDir + "_normal_float16.pfm", Bitmap::FileFormat::PfmFile, Bitmap::ExportFlags::None);
        fbo->getColorTexture(2).get()->captureToFile(bakeMipToSave, 0, newDir + "_normal.png", Bitmap::FileFormat::PngFile, Bitmap::ExportFlags::None);
        fbo->getColorTexture(4).get()->captureToFile(bakeMipToSave, 0, newDir + "_translucency.png", Bitmap::FileFormat::PngFile, Bitmap::ExportFlags::None);

        // FIXME total num MIps to build
        //int maxHmip = (int)log2(iH / 4);
        //int maxWmip = (int)log2(iW / 4);
        int totalMIP = maxMIPY;// __min(maxHmip, maxWmip) - bakeMipToSave; // -bakeMipToSave for the supersampling
        std::string mipNumber = std::to_string(totalMIP);
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

        Mat._constData.translucency = _info->translucency;
        Mat._constData.alphaPow = _info->alphaPow;
        Mat._constData.roughness[0] = 0.8f;
        Mat._constData.roughness[1] = 0.8f;

        std::ofstream os(resource + _info->material.path);
        cereal::JSONOutputArchive archive(os);
        archive(Mat);
    }
}



void _rootPlant::updateMaterialsAndTextures()
{
    _plantMaterial::static_materials_veg.modified = false;
    _plantMaterial::static_materials_veg.modifiedData = false;
    _plantMaterial::static_materials_veg.setTextures(varVegTextures);
    //_plantMaterial::static_materials_veg.setTextures(varTextures_Gauraud);
    //_plantMaterial::static_materials_veg.setTextures(varTextures_Debug_Pivots);
    //_plantMaterial::static_materials_veg.setTextures(varTextures_Debug_Pixels);
    _plantMaterial::static_materials_veg.setTextures(varTextures_RGBSample);
    _plantMaterial::static_materials_veg.setTextures(varTextures_Depth);
    _plantMaterial::static_materials_veg.rebuildStructuredBuffer();
    _plantMaterial::static_materials_veg.setTextures(varBBTextures);
}


void _rootPlant::updateShaderConstants(Texture::SharedPtr _previousFrame, Texture::SharedPtr shadow, shaderLightBuffer _buffer)
{
    billboardShader.Vars()->setTexture("gPreviousFrame", _previousFrame);
    vegetationShader.Vars()->setTexture("gPreviousFrame", _previousFrame);

    billboardShader.Vars()->setTexture("terrainShadow", shadow);
    vegetationShader.Vars()->setTexture("terrainShadow", shadow);

    billboardShader.Vars()["LightsCB"]["sunDirection"] = _buffer.sunDirection;
    billboardShader.Vars()["LightsCB"]["sunRightVector"] = _buffer.sunRightVector;
    billboardShader.Vars()["LightsCB"]["sunUpVector"] = _buffer.sunUpVector;
    billboardShader.Vars()["LightsCB"]["screenSize"] = _buffer.screenSize;
    billboardShader.Vars()["LightsCB"]["fog_far_Start"] = _buffer.fog_far_Start;
    billboardShader.Vars()["LightsCB"]["fog_far_log_F"] = _buffer.fog_far_log_F;
    billboardShader.Vars()["LightsCB"]["fog_far_one_over_k"] = _buffer.fog_far_one_over_k;

    vegetationShader.Vars()["LightsCB"]["sunDirection"] = _buffer.sunDirection;
    vegetationShader.Vars()["LightsCB"]["sunRightVector"] = _buffer.sunRightVector;
    vegetationShader.Vars()["LightsCB"]["sunUpVector"] = _buffer.sunUpVector;
    vegetationShader.Vars()["LightsCB"]["screenSize"] = _buffer.screenSize;
    vegetationShader.Vars()["LightsCB"]["fog_far_Start"] = _buffer.fog_far_Start;
    vegetationShader.Vars()["LightsCB"]["fog_far_log_F"] = _buffer.fog_far_log_F;
    vegetationShader.Vars()["LightsCB"]["fog_far_one_over_k"] = _buffer.fog_far_one_over_k;
}



void _rootPlant::render(RenderContext* _renderContext, const Fbo::SharedPtr& _fbo, GraphicsState::Viewport _viewport,
    rmcv::mat4  _viewproj, float3 camPos, rmcv::mat4  _view, rmcv::mat4  _clipFrustum, float halfAngle_to_Pixels, bool terrainMode)
{
    // store information that we need for overlay
    renderInfo.context = _renderContext;
    renderInfo.viewport = _viewport;
    renderInfo.viewproj = _viewproj;
    renderInfo.cameraPos = camPos;
    float3 camVector = (float3(0, 1000, 0) + (float3)settings.root[1] * extents.y / 2.f) - camPos;
    renderInfo.half_to_Pixels_SinglePlant = halfAngle_to_Pixels / glm::length(camVector);
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

        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                _viewproj[j][i] = VP[j][i];
                _view[j][i] = view[j][i];
            }
        }

        _clipFrustum[0][0] = P[0][3] + P[0][0];
        _clipFrustum[0][1] = P[1][3] + P[1][0];
        _clipFrustum[0][2] = P[2][3] + P[2][0];
        _clipFrustum[0][3] = P[3][3] + P[3][0];

        _clipFrustum[1][0] = P[0][3] - P[0][0];
        _clipFrustum[1][1] = P[1][3] - P[1][0];
        _clipFrustum[1][2] = P[2][3] - P[2][0];
        _clipFrustum[1][3] = P[3][3] - P[3][0];

        _clipFrustum[2][0] = P[0][3] + P[0][1];
        _clipFrustum[2][1] = P[1][3] + P[1][1];
        _clipFrustum[2][2] = P[2][3] + P[2][1];
        _clipFrustum[2][3] = P[3][3] + P[3][1];

        _clipFrustum[3][0] = P[0][3] - P[0][1];
        _clipFrustum[3][1] = P[1][3] - P[1][1];
        _clipFrustum[3][2] = P[2][3] - P[2][1];
        _clipFrustum[3][3] = P[3][3] - P[3][1];
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
            glm::mat4 root = selectedPart->getRoot(bakeLod->clipDead);
            bakeViewAdjusted = bakeViewMatrix = root;
            float3 u = glm::normalize((float3)tip[3] - (float3)root[3]);

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

            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 4; j++) {
                    _viewproj[j][i] = VP[j][i];
                    _view[j][i] = bakeViewAdjusted[j][i];// rename to camerqa or pass in vextors
                }
            }

            _renderContext->clearFbo(&(*_fbo), float4(0.02f, 0.02f, 0.015f, 0), 1.f, 0);
            _viewport = GraphicsState::Viewport(bakeViewportTL.x, bakeViewportTL.y, bakeViewportSize, bakeViewportSize, 0.f, 1.f);
        }
    }
    else
    {
        settings.includeTip = true;
    }

    FALCOR_PROFILE("vegetation");



    // kest try alwasy clear
    {
        FALCOR_PROFILE("compute_veg_clear");
        compute_clearBuffers.dispatch(_renderContext, 1, 1);
    }

    if (!terrainMode && _ribbonBuilder.numPacked() > 1 && !displayModeSinglePlant)
    {
        FALCOR_PROFILE("compute_veg_lods");
        //compute_clearBuffers.dispatch(_renderContext, 1, 1);

        compute_calulate_lod.Vars()["gConstantBuffer"]["view"] = _view;
        compute_calulate_lod.Vars()["gConstantBuffer"]["frustum"] = _clipFrustum;
        compute_calulate_lod.Vars()["gConstantBuffer"]["eyePos"] = camPos;
        compute_calulate_lod.Vars()["gConstantBuffer"]["lodBias"] = loddingBias;
        compute_calulate_lod.Vars()["gConstantBuffer"]["halfAngle_to_Pixels"] = renderInfo.half_to_Pixels;
        compute_calulate_lod.Vars()["gConstantBuffer"]["firstPlant"] = firstPlant;
        compute_calulate_lod.Vars()["gConstantBuffer"]["lastPlant"] = lastPlant;
        compute_calulate_lod.Vars()["gConstantBuffer"]["firstLod"] = firstLod;
        compute_calulate_lod.Vars()["gConstantBuffer"]["lastLod"] = lastLod;
        compute_calulate_lod.dispatch(_renderContext, MAX_PLANT_INSTANCES / 256, 1);
        //compute_calulate_lod.dispatchIndirect()
    }



    {
        FALCOR_PROFILE("compute_veg_sortCombine");
        if (!displayModeSinglePlant)
        {
            compute_sortCombine.dispatch(_renderContext, 1, 1);
        }
    }




    if (terrainMode || _ribbonBuilder.numPacked() > 1)
    {


        static float time = 0.0f;
        time += gpFramework->getFrameRate().getAverageFrameTime() * 0.001f;

        {
            vegetationShader.State()->setFbo(_fbo);
            vegetationShader.State()->setViewport(0, _viewport, true);
            vegetationShader.State()->setRasterizerState(rasterstate);
            if (render_alphaBlend)  vegetationShader.State()->setBlendState(blendstate_withAlpha);
            else
            {
                vegetationShader.State()->setBlendState(blendstate);
            }
            vegetationShader.Vars()["gConstantBuffer"]["view"] = _view;
            vegetationShader.Vars()["gConstantBuffer"]["viewproj"] = _viewproj;
            vegetationShader.Vars()["gConstantBuffer"]["eyePos"] = camPos;
            vegetationShader.Vars()["gConstantBuffer"]["time"] = time;
            vegetationShader.Vars()["gConstantBuffer"]["windDir"] = windDir;
            vegetationShader.Vars()["gConstantBuffer"]["windStrength"] = windStrength;


            glm::mat4 T = toGLM(_view);
            glm::mat4 Tinv = glm::inverse(T);
            rmcv::mat4  _view_T;
            float4 camRight = glm::normalize(T[0]);
            camRight.w = 0;

            float4 camUp = glm::normalize(T[1]);
            camUp.w = 0;
            vegetationShader.Vars()["gConstantBuffer"]["camRight"] = camRight;
            vegetationShader.Vars()["gConstantBuffer"]["camUp"] = camUp;
            vegetationShader.Vars()["gConstantBuffer"]["toneMap"] = 0;
            if (bakingView)
            {
                camPos = bakeViewAdjusted[3].xyz + bakeViewAdjusted[2].xyz * 1000.f;
                vegetationShader.Vars()["gConstantBuffer"]["windStrength"] = 0;
                vegetationShader.Vars()["gConstantBuffer"]["eyePos"] = camPos;
            }

        }


        {
            vegetationShader_RGB_SAMPLE.State()->setFbo(_fbo);
            vegetationShader_RGB_SAMPLE.State()->setViewport(0, _viewport, true);
            vegetationShader_RGB_SAMPLE.State()->setRasterizerState(rasterstate);
            vegetationShader_RGB_SAMPLE.State()->setBlendState(blendstate);
            vegetationShader_RGB_SAMPLE.Vars()["gConstantBuffer"]["viewproj"] = _viewproj;
            vegetationShader_RGB_SAMPLE.Vars()["gConstantBuffer"]["eyePos"] = camPos;
            vegetationShader_RGB_SAMPLE.Vars()["gConstantBuffer"]["time"] = time;
            vegetationShader_RGB_SAMPLE.Vars()["gConstantBuffer"]["windDir"] = windDir;
            vegetationShader_RGB_SAMPLE.Vars()["gConstantBuffer"]["windStrength"] = windStrength;
            vegetationShader_RGB_SAMPLE.Vars()["LightsCB"]["sunDirection"] = sunDirectionShadowMap;//
        }

        if (terrainMode)
        {
            FALCOR_PROFILE("ribbonShaderTerrain");
            vegetationShader.renderIndirect(_renderContext, drawArgs_vegetation);
        }
        else
        {
            FALCOR_PROFILE("ribbonShader");
            if (displayModeSinglePlant)
            {
                vegetationShader.drawInstanced(_renderContext, VEG_BLOCK_SIZE, totalBlocksToRender);
            }
            else
            {
                if (render_FrontToback)
                {
                    for (int idx = 0; idx < 128; idx++)
                    {
                        vegetationShader.Vars()["gConstantBuffer"]["drawIndex"] = idx;
                        vegetationShader.renderIndirect(_renderContext, drawArgs_vegetation, nullptr, idx, 1);
                    }
                }
                else
                {
                    for (int idx = 127; idx >= 0; idx--)
                    {
                        vegetationShader.Vars()["gConstantBuffer"]["drawIndex"] = idx;
                        vegetationShader.renderIndirect(_renderContext, drawArgs_vegetation, nullptr, idx, 1);
                    }
                }
            }
        }

        if (!terrainMode && _ribbonBuilder.numPacked() > 1 && !displayModeSinglePlant)
        {
            billboardShader.State()->setFbo(_fbo);
            billboardShader.State()->setViewport(0, _viewport, true);
            billboardShader.State()->setRasterizerState(rasterstate);
            billboardShader.State()->setBlendState(blendstate);

            billboardShader.Vars()["gConstantBuffer"]["viewproj"] = _viewproj;
            billboardShader.Vars()["gConstantBuffer"]["eyePos"] = camPos;


            {
                FALCOR_PROFILE("billboards");
                billboardShader.renderIndirect(_renderContext, drawArgs_billboards);
            }

            auto& profiler = Profiler::instance();
            auto eventBB = profiler.getEvent("/onFrameRender/vegetation/billboards");
            gputimeBB = eventBB->getGpuTimeAverage();

        }

        {
            _renderContext->copyResource(buffer_feedback_read.get(), buffer_feedback.get());

            const uint8_t* pData = (uint8_t*)buffer_feedback_read->map(Buffer::MapType::Read);
            std::memcpy(&feedback, pData, sizeof(vegetation_feedback));
            buffer_feedback_read->unmap();
        }

        auto& profiler = Profiler::instance();
        auto event = profiler.getEvent("/onFrameRender/vegetation/ribbonShader");
        gputime = event->getGpuTimeAverage();
    }

    {
        FALCOR_PROFILE("compute_veg_sortCombine_POST");
        if (!displayModeSinglePlant)
        {
            compute_sortCombine_POST.dispatch(_renderContext, 1, 1);
        }
    }
}


void _rootPlant::builInstanceBuffer()
{
    std::array<plant_instance, MAX_PLANT_INSTANCES> instanceBuf;
    const siv::PerlinNoise perlin{ 100 };

    if (cropLines)
    {
        for (int j = 0; j < 64; j++)
        {
            for (int i = 0; i < 256; i++)
            {
                int index = j * 256 + i;
                instanceBuf[index].plant_idx = 0;
                instanceBuf[index].position = { (float)(j - 32) * 1.4 + d_1_1(generator) * 0.1f, 1000.f, (float)(i - 128) * 0.35f + d_1_1(generator) * 0.1f };
                instanceBuf[index].scale = 1.f + d_1_1(generator) * 0.15f;
                instanceBuf[index].rotation = d_1_1(generator) * 3.14f;
                //instanceBuf[index].time_offset = d_1_1(generator) * 100;
            }
        }
    }
    else if (numBinaryPlants == 0)
    {
        static float sum = 0;
        float3 pos;
        for (int i = 0; i < MAX_PLANT_INSTANCES; i++)
        {
            while (sum < 1.f)
            {
                pos = { d_1_1(generator) * instanceArea[0], 1000.f, d_1_1(generator) * instanceArea[0] };
                if (uniformSpread)
                {
                    sum += 1;
                }
                else
                {
                    float noise = perlin.octave2D_01(pos.x / 2.f, pos.z / 2.f, 3);
                    sum += pow(noise, 3);
                }
            }
            sum -= 1.f;

            instanceBuf[i].position = pos;
            instanceBuf[i].plant_idx = i % 3;
            instanceBuf[i].position = pos;
            instanceBuf[i].scale = 1.f + d_1_1(generator) * 0.15f;
            instanceBuf[i].rotation = d_1_1(generator) * 3.14f;
            //instanceBuf[i].time_offset = d_1_1(generator) * 100;
        }
    }
    else
    {
        static float sum = 0;
        float3 pos;
        for (int i = 0; i < MAX_PLANT_INSTANCES; i++)
        {
            int type = i % (numBinaryPlants);
            while (sum < 1)
            {
                pos = { d_1_1(generator) * instanceArea[0] * extents.x, 1000.f, d_1_1(generator) * instanceArea[0] * extents.x };
                float noise = perlin.octave2D_01(pos.x / 2.f + type, pos.z / 2.f, 3);

                if (uniformSpread)
                {
                    sum += 1;
                }
                else
                {
                    float noise = perlin.octave2D_01(pos.x / 2.f, pos.z / 2.f, 3);
                    sum += pow(noise, 5);
                }
            }
            sum -= 1;

            instanceBuf[i].plant_idx = type;
            instanceBuf[i].position = pos;
            instanceBuf[i].scale = 1.f + d_1_1(generator) * 0.2f;
            instanceBuf[i].rotation = d_1_1(generator) * 3.14f;
            //instanceBuf[i].time_offset = d_1_1(generator) * 100;
        }
    }


    float W = instanceArea[0];
    float O = W / 2;
    float dI = W / 256.f;
    for (int ix = 0; ix < 256; ix++)
    {
        for (int jx = 0; jx < 256; jx++)
        {
            int idx = ix * 256 + jx;
            //instanceBuf[idx].position = float3(ix * dI - O, 1000, jx * dI - O);
        }
    }



    // plant zero is always fixed in the middle
    instanceBuf[0].position = { 0, 1000, 0 };
    instanceBuf[0].scale = 1.f;
    instanceBuf[0].rotation = 0;

    instanceData->setBlob(instanceBuf.data(), 0, MAX_PLANT_INSTANCES * sizeof(plant_instance));
}



#pragma optimize("", off)

void _rootPlant::bakeShadowMap(RenderContext* _renderContext)
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

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            _shadow_viewproj[j][i] = VP[j][i];
            //_view[j][i] = view[j][i];
        }
    }

    vegetationShader.Vars()["gConstantBuffer"]["shadowViewProj"] = _shadow_viewproj;
    //vegetationShader_GOURAUD.Vars()["gConstantBuffer"]["shadowViewProj"] = _shadow_viewproj;
    vegetationShader_RGB_SAMPLE.Vars()["gConstantBuffer"]["shadowViewProj"] = _shadow_viewproj;


    // Now render shadow buffer
    {
        FALCOR_PROFILE("BAKE_SHADOW_2K");
        _renderContext->clearFbo(shadowFbo.get(), float4(0, 0, 0, 1), 1.0f, 0, FboAttachmentType::All);

        vegetationShader_DEPTH.State()->setFbo(shadowFbo);
        //vegetationShader.State()->setViewport(0, _viewport, true);
        vegetationShader_DEPTH.State()->setRasterizerState(rasterstate);
        vegetationShader_DEPTH.State()->setBlendState(blendstate);
        vegetationShader_DEPTH.Vars()["gConstantBuffer"]["viewproj"] = _shadow_viewproj;
        vegetationShader_DEPTH.Vars()["gConstantBuffer"]["eyePos"] = camPos;
        vegetationShader_DEPTH.Vars()["gConstantBuffer"]["time"] = 0;
        vegetationShader_DEPTH.Vars()["gConstantBuffer"]["windDir"] = windDir;
        vegetationShader_DEPTH.Vars()["gConstantBuffer"]["windStrength"] = 0;

        vegetationShader_DEPTH.renderIndirect(_renderContext, drawArgs_vegetation);
    }
}



void _rootPlant::buildOneMap(float _sunAngle)
{
    uint4 data[256 * 128];
    memset(data, 0, sizeof(uint4) * 256 * 128);
    rgb_data->setBlob(data, 0, sizeof(uint4) * 256 * 128);

    renderInfo.context->clearTexture(RGB_MAP.get());
    shadowPitch = _sunAngle;
    bakeShadowMap(renderInfo.context);

    for (int y = 0; y < 64; y++)
    {
        for (int x = 0; x < 128; x++)
        {

            float2 pix = float2(x, y) - float2(127.5f, 0.f);
            float dst = glm::length(pix);
            //if (dst < 120)  // 8 pix buffer
            {
                /*
                float EoN_req = 1.0 - dst / 120.f;
                float EoS_req = pix.x / dst;    // only works becuase we are alwasy offcenter

                // now search for this camera angle
                float3 SUN = float3(cos(_sunAngle), sin(_sunAngle), 0);
                float camPitch = atan(EoN_req);
                float3 EYE;
                EYE.y = EoN_req;
                EYE.x = cos(camPitch);
                EYE.z = 0;
                float EoS_mem = glm::dot(EYE, SUN);
                bool caught = false;
                for (float j = 0; j < 3.14159265359f; j += 0.01f)
                {
                    EYE.x = cos(camPitch) * cos(j);
                    EYE.z = cos(camPitch) * sin(j);
                    float EoS = glm::dot(EYE, SUN);
                    if ((EoS > EoS_req && EoS_mem < EoS_req) || (EoS < EoS_req && EoS_mem > EoS_req))
                    {
                        // crossover, but walk back a little
                        caught = true;
                        camRot = j;
                        break;
                    }
                }
                */
                float camPitch = 1.57079632679f * (1 - ((float)y / 64.f));;
                camRot = 3.141592653f * (1 - ((float)x / 128.f));

                // build a view and render
                //if (caught)
                {
                    glm::mat4 view = glm::mat4(1);
                    ROLL(view, camRot);    // yaw
                    PITCH(view, camPitch); // pitch
                    view[3] = float4(0, 1000.2, 0, 1);

                    float3 camPos = (float3)view[3] - (float3)view[2] * 10000.f;

                    glm::mat4 V, P, VP;
                    V = glm::inverse(view);
                    P = glm::orthoLH(-1.0f, 1.0f, -0.15f, 0.15f, -10.0f, 10.0f);
                    VP = P * V;

                    rmcv::mat4  _view, _viewproj, _clipFrustum;
                    for (int i = 0; i < 4; i++) {
                        for (int j = 0; j < 4; j++) {
                            _viewproj[j][i] = VP[j][i];
                            _view[j][i] = view[j][i];
                        }
                    }

                    _clipFrustum[0][0] = P[0][3] + P[0][0];
                    _clipFrustum[0][1] = P[1][3] + P[1][0];
                    _clipFrustum[0][2] = P[2][3] + P[2][0];
                    _clipFrustum[0][3] = P[3][3] + P[3][0];

                    _clipFrustum[1][0] = P[0][3] - P[0][0];
                    _clipFrustum[1][1] = P[1][3] - P[1][0];
                    _clipFrustum[1][2] = P[2][3] - P[2][0];
                    _clipFrustum[1][3] = P[3][3] - P[3][0];

                    _clipFrustum[2][0] = P[0][3] + P[0][1];
                    _clipFrustum[2][1] = P[1][3] + P[1][1];
                    _clipFrustum[2][2] = P[2][3] + P[2][1];
                    _clipFrustum[2][3] = P[3][3] + P[3][1];

                    _clipFrustum[3][0] = P[0][3] - P[0][1];
                    _clipFrustum[3][1] = P[1][3] - P[1][1];
                    _clipFrustum[3][2] = P[2][3] - P[2][1];
                    _clipFrustum[3][3] = P[3][3] - P[3][1];

                    {
                        compute_clearBuffers.dispatch(renderInfo.context, 1, 1);

                        compute_calulate_lod.Vars()["gConstantBuffer"]["view"] = _view;
                        compute_calulate_lod.Vars()["gConstantBuffer"]["frustum"] = _clipFrustum;
                        compute_calulate_lod.Vars()["gConstantBuffer"]["eyePos"] = camPos;
                        compute_calulate_lod.Vars()["gConstantBuffer"]["lodBias"] = loddingBias;
                        compute_calulate_lod.Vars()["gConstantBuffer"]["firstPlant"] = firstPlant;
                        compute_calulate_lod.Vars()["gConstantBuffer"]["lastPlant"] = lastPlant;
                        compute_calulate_lod.Vars()["gConstantBuffer"]["firstLod"] = firstLod;
                        compute_calulate_lod.Vars()["gConstantBuffer"]["lastLod"] = lastLod;
                        compute_calulate_lod.dispatch(renderInfo.context, MAX_PLANT_INSTANCES / 256, 1);
                    }



                    {
                        renderInfo.context->clearFbo(rgbFbo.get(), float4(0, 0, 0.0, 1), 1.0f, 0, FboAttachmentType::All);
                    }

                    {
                        GraphicsState::Viewport _viewport = GraphicsState::Viewport(0.f, 0.f, 1024.f, 256.f, 0.f, 1.f);

                        vegetationShader_RGB_SAMPLE.State()->setFbo(rgbFbo);
                        vegetationShader_RGB_SAMPLE.State()->setViewport(0, _viewport, true);
                        vegetationShader_RGB_SAMPLE.State()->setRasterizerState(rasterstate);
                        vegetationShader_RGB_SAMPLE.State()->setBlendState(blendstate);
                        vegetationShader_RGB_SAMPLE.Vars()["gConstantBuffer"]["viewproj"] = _viewproj;

                        vegetationShader_RGB_SAMPLE.Vars()["gConstantBuffer"]["eyePos"] = camPos;
                        float time1 = 0.0f;
                        vegetationShader_RGB_SAMPLE.Vars()["gConstantBuffer"]["time"] = time1;
                        vegetationShader_RGB_SAMPLE.Vars()["gConstantBuffer"]["windDir"] = windDir;
                        windStrength = 0;
                        vegetationShader_RGB_SAMPLE.Vars()["gConstantBuffer"]["windStrength"] = windStrength;
                        vegetationShader_RGB_SAMPLE.Vars()["LightsCB"]["sunDirection"] = sunDirectionShadowMap;


                        vegetationShader_RGB_SAMPLE.renderIndirect(renderInfo.context, drawArgs_vegetation);
                    }

                    renderInfo.context->flush(true);
                    // Now do compute into second tecture
                    {
                        compute_sampleRGBtoPixel.Vars()["gConstants"]["pix"] = uint2(x, y);
                        compute_sampleRGBtoPixel.dispatch(renderInfo.context, 32, 8, 1);
                    }
                    static int cnt = 0;
                    cnt++;
                    if (cnt % 2000 == 0)
                    {
                        char name[256];
                        sprintf(name, "e:/test_RGB/_view_%d.png", cnt);
                        rgbFbo->getColorTexture(0)->captureToFile(0, 0, name);
                    }
                }

            }

        }
    }

    compute_sampleRGBtoPixel_ToTexture.dispatch(renderInfo.context, 4, 2, 1);
    renderInfo.context->flush(true);
    RGB_MAP->captureToFile(0, 0, "e:/test_RGB/smallgrass.png");
}



float GeometrySchlickGGX(float NdotV, float k)
{
    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}

float GeometrySmith(float3 N, float3 V, float3 L, float k)
{
    float NdotV = __max(glm::dot(N, V), 0.1);	// ??? FIXME JOPHAN - clamp to zero causes black on perfect edge on pixels and that is just WRONG
    float NdotL = __max(glm::dot(N, L), 0.0);
    float ggx1 = GeometrySchlickGGX(NdotV, k);
    float ggx2 = GeometrySchlickGGX(NdotL, k);

    return ggx1 * ggx2;
}

float schlick(float f0, float V)
{
    return f0 + (1 - f0) * pow(V, 5);
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
            float pitch = 1.57079632679f * (1 - ((float)y / 64.f));;
            for (int x = 0; x < 128; x++)
            {
                float yaw = 3.141592653f * (1 - ((float)x / 128.f));
                float3 EYE = float3(cos(pitch) * cos(yaw), sin(pitch), cos(pitch) * sin(yaw));
                float3 Half = glm::normalize(SUN + EYE);

                float specGeom = GeometrySmith(N, EYE, SUN, k_direct);
                float slck = schlick(0.04f, 1.f - glm::dot(EYE, N));

                dataGGX[y][x][0] = (int)(255.f * (1.f - specGeom));
                dataGGX[y][x][1] = (int)(255.f * (glm::dot(N, SUN) * (1.f - slck)));
                dataGGX[y][x][2] = 0;
                dataGGX[y][x][3] = 255;

                // My guess
                float EoS = glm::clamp(glm::dot(EYE, SUN), 0.f, 1.f);
                float EoN = glm::clamp(glm::dot(EYE, N), 0.f, 1.f);
                float shadow = 0.25 + 0.75 * pow((1.f - sun / 1.58f), 2.5f);
                shadow *= 1.f - 0.5f * pow(EoS, 200.f);
                shadow *= 1.f - 0.85f * pow(1.f - EoN, 5.f);

                float light = 1.f - shadow;
                float split = glm::dot(EYE, SUN) * 0.5f + 0.5f;
                float grn = light * split;
                float blue = light * (1.f - split);
                data[y][x][0] = (int)(255.f * shadow);
                data[y][x][1] = (int)(255.f * grn);
                data[y][x][2] = (int)(255.f * blue);
                data[y][x][3] = 255;
            }
        }

        int sunI = (int)(sun * 57.2957795131f);
        std::string name = "e:/test_RGB/GGX_" + std::to_string(sunI) + ".png";
        renderInfo.context->updateTextureData(RGB_MAP.get(), dataGGX);
        renderInfo.context->flush(true);
        RGB_MAP->captureToFile(0, 0, name.c_str());

        name = "e:/test_RGB/guess_" + std::to_string(sunI) + ".png";
        renderInfo.context->updateTextureData(RGB_MAP.get(), data);
        renderInfo.context->flush(true);
        RGB_MAP->captureToFile(0, 0, name.c_str());
    }
}
