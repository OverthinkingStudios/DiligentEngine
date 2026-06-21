#include "vegetationBuilder.h"
#include "imgui.h"
#include "PerlinNoise.hpp"          //https://github.com/Reputeless/PerlinNoise/blob/master/PerlinNoise.hpp

using namespace std::chrono;

#define TOOLTIP(x)  if (ImGui::IsItemHovered()) {ImGui::SetTooltip(x);}


#pragma optimize("", off)

std::array<std::string, 77> resourceString =
{
    "Unknown",    "R8Unorm",    "R8Snorm",    "R16Unorm",    "R16Snorm",    "RG8Unorm",    "RG8Snorm",    "RG16Unorm",    "RG16Snorm",    "RGB16Unorm",    "RGB16Snorm",
    "R24UnormX8",    "RGB5A1Unorm",    "RGBA8Unorm",    "RGBA8Snorm",    "RGB10A2Unorm",    "RGB10A2Uint",    "RGBA16Unorm",    "RGBA8UnormSrgb",    "R16Float",
    "RG16Float",    "RGB16Float",    "RGBA16Float",    "R32Float",    "R32FloatX32",    "RG32Float",    "RGB32Float",    "RGBA32Float",    "R11G11B10Float",
    "RGB9E5Float",    "R8Int",    "R8Uint",    "R16Int",    "R16Uint",    "R32Int",    "R32Uint",    "RG8Int",    "RG8Uint",    "RG16Int",    "RG16Uint",
    "RG32Int",    "RG32Uint",    "RGB16Int",    "RGB16Uint",    "RGB32Int",    "RGB32Uint",    "RGBA8Int",    "RGBA8Uint",    "RGBA16Int",    "RGBA16Uint",
    "RGBA32Int",    "RGBA32Uint",    "BGRA8Unorm",    "BGRA8UnormSrgb",    "BGRX8Unorm",    "BGRX8UnormSrgb",    "Alpha8Unorm",    "Alpha32Float",    "R5G6B5Unorm",
    "D32Float",    "D16Unorm",    "D32FloatS8X24",    "D24UnormS8",    "BC1Unorm",    "BC1UnormSrgb",    "BC2Unorm",    "BC2UnormSrgb",    "BC3Unorm",    "BC3UnormSrgb",
    "BC4Unorm",    "BC4Snorm",    "BC5Unorm",    "BC5Snorm",    "BC6HS16",    "BC6HU16",    "BC7Unorm",    "BC7UnormSrgb",
};


float GLOBAL_RND_SIZE = 0.3f;

bool LOGTHEBUILD = false;




float perlinData[1024];

materialCache_plants _plantMaterial::static_materials_veg;
std::string materialCache_plants::lastFile;


ribbonBuilder _ribbonBuilder;




// _rootPlant
_plantBuilder* _rootPlant::selectedPart = nullptr;
_plantMaterial* _rootPlant::selectedMaterial = nullptr;
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






void materialCache_plants::renderGui(Gui* mpGui, Gui::Window& _window)
{
    ImGui::PushFont(mpGui->getFont("default"));
    {
        auto& style = ImGui::GetStyle();
        ImGuiStyle oldStyle = style;
        float width = ImGui::GetWindowWidth();
        int numColumns = __max(2, (int)floor(width / 140));
        int columnCount = 0;
        float W = width / numColumns - 10;

        ImGui::PushItemWidth(128);

        if (ImGui::Button("import")) {
            std::filesystem::path path = lastFile;
            FileDialogFilterVec filters = { {"vegetationMaterial"} };
            if (openFileDialog(filters, path))
            {
                find_insert_material(path);
                lastFile = path.string();
            }
        }

        if (ImGui::Button("rebuild")) {
            rebuildStructuredBuffer();
        }


        ImVec2 rootPos = ImGui::GetCursorPos();

        struct sortDisplay
        {
            bool operator < (const sortDisplay& str) const
            {
                return (name < str.name);
            }
            std::string name;
            std::string path;
            std::string root;
            std::string dir;
            int index;
        };

        std::vector<sortDisplay> displaySortMap;

        sortDisplay S;
        int cnt = 0;
        for (auto& material : materialVector)
        {
            S.name = material.fullPath.string().substr(terrafectorEditorMaterial::rootFolder.length() - 1);
            replaceAllVEG(S.name, "\\", "/");
            S.path = S.name.substr(0, S.name.find_last_of("\\/"));
            S.root = S.path.substr(0, S.path.find_first_of("\\/", 12));
            S.dir = S.name.substr(0, S.name.find("bake") - 1);  // remove all bakes here
            S.index = cnt;
            displaySortMap.push_back(S);
            cnt++;
        }
        std::sort(displaySortMap.begin(), displaySortMap.end());


        {
            std::string path = "";

            int subCount = 0;
            for (cnt = 0; cnt < materialVector.size(); cnt++)
            {
                std::string  thisPath = "other";
                if (displaySortMap[cnt].name.find("vegetationMaterial") != std::string::npos)
                {
                    thisPath = displaySortMap[cnt].path;// .substr(12, displaySortMap[cnt].name.find_last_of("\\/") - 12);  // -12 removes the first vegetation
                }
                if (thisPath != path) {
                    //ImGui::NewLine();
                    //ImGui::Text(thisPath.c_str());
                    //ImGui::Text(displaySortMap[cnt].name.c_str());
                    //ImGui::Text(displaySortMap[cnt].path.c_str());
                    //ImGui::Text(displaySortMap[cnt].root.c_str());
                    ImGui::Text(displaySortMap[cnt].path.c_str());
                    path = displaySortMap[cnt].path;
                    subCount = 0;
                    rootPos = ImGui::GetCursorPos();
                    columnCount = 0;
                }
                _plantMaterial& material = materialVector[displaySortMap[cnt].index];

                ImGui::PushID(777 + cnt);
                {
                    if ((columnCount % numColumns) == 0) ImGui::NewLine();

                    ImGui::SameLine(0, 10);
                    if (ImGui::Button(material.displayName.c_str(), ImVec2(W, 0)))
                    {
                        selectedMaterial = displaySortMap[cnt].index;
                    }
                    if (ImGui::IsItemHovered())
                    {
                        //ImGui::SetTooltip("NORMAL  {%d}", _constData.normalTexture);
                        _plantMaterial::static_materials_veg.dispTexIndex = material._constData.albedoTexture;
                    }
                    TOOLTIP((material.fullPath.string() + "\n" + std::to_string(displaySortMap[cnt].index)).c_str());
                    columnCount++;
                }
                ImGui::PopID();
                subCount++;
            }
        }
        style = oldStyle;
    }
    ImGui::PopFont();

}







void materialCache_plants::renderGuiTextures(Gui* mpGui, Gui::Window& _window)
{
    auto& style = ImGui::GetStyle();
    ImGuiStyle oldStyle = style;
    float width = ImGui::GetWindowWidth();
    int numColumns = __max(2, (int)floor(width / 200));
    float W = width / numColumns - 10;


    char text[1024];
    ImGui::PushFont(mpGui->getFont("header2"));
    ImGui::Text("Tex [%d]   %3.1fMb", (int)textureVector.size(), texMb);
    ImGui::PopFont();
    ImGui::NewLine();

    ImGui::PushFont(mpGui->getFont("default"));
    {
        for (uint i = 0; i < textureVector.size(); i++)
        {
            if (i % numColumns == 0)
            {
                ImGui::NewLine();
                ImGui::SameLine(20);
            }
            else
            {
                ImGui::SameLine(0, 10);
            }

            Texture* pT = textureVector[i].get();
            sprintf(text, "%s##%d", pT->getName().c_str(), i);

            if (ImGui::Selectable(text, false, 0, ImVec2(W, 0))) {}

            if (ImGui::IsItemHovered())
            {
                uint32_t format = (int)pT->getFormat();
                //ImGui::PushFont(mpGui->getFont("header2"));
                ImGui::SetTooltip("%d  :  %s \n%d x %d {%d mips} \n%s", i, pT->getSourcePath().string().c_str(), pT->getWidth(), pT->getHeight(), pT->getMipCount(), resourceString[format].c_str());
                //ImGui::PopFont();

                _plantMaterial::static_materials_veg.dispTexIndex = i;
            }

            //if (ImGui::BeginPopup(text)) {
            if (ImGui::BeginPopupContextItem())
            {
                std::string cmdExp = "explorer " + pT->getSourcePath().string();
                std::string cmdPs = "\"C:\\Program Files\\Adobe\\Adobe Photoshop 2022\\Photoshop.exe\" " + pT->getSourcePath().string();
                if (ImGui::Selectable("Explorer")) { system(cmdExp.c_str()); }
                if (ImGui::Selectable("Photoshop")) { system(cmdPs.c_str()); }
                ImGui::Separator();

                /*
                std::string texPath = pT->getSourcePath().string();
                texPath = texPath.substr(terrafectorEditorMaterial::rootFolder.size());
                int matCnt = 0;
                for (auto material : materialVector)
                {
                    for (int j = 0; j < 9; j++)
                    {
                        if (material.textureNames[j].size() > 0)
                        {
                            if (texPath.find(material.textureNames[j]) != std::string::npos)
                            {
                                if (ImGui::Selectable(material.displayName.c_str())) { selectedMaterial = matCnt; }
                            }
                        }
                    }
                    matCnt++;
                }
                */
                ImGui::EndPopup();
            }

        }
    }
    ImGui::PopFont();


    style = oldStyle;
}



bool materialCache_plants::renderGuiSelect(Gui* mpGui)
{
    if (selectedMaterial >= 0 && selectedMaterial < materialVector.size())
    {
        materialVector[selectedMaterial].renderGui(mpGui);
        return true;
    }
    else
    {
        ImGui::Text("default material");
        static _plantMaterial M;
        M.renderGui(mpGui);
    }

    return false;
}









void _plantMaterial::renderGui(Gui* _gui)
{
    bool changed = false;

    ImGui::PushFont(_gui->getFont("default"));
    {

        ImGui::PushFont(_gui->getFont("header1"));
        {
            ImGui::Text(displayName.c_str());


            ImGui::SetNextItemWidth(100);
            if (ImGui::Button("load")) { import(); }


            if (changedForSave) {
                ImGui::SameLine(0, 30);
                ImGui::SetNextItemWidth(100);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.3f, 0.0f, 0.5f));
                ImGui::SameLine(0, 20);
                if (ImGui::Button("Save"))
                {
                    save();
                    changedForSave = false;
                }
                ImGui::PopStyleColor();
            }

            ImGui::SameLine(0, 30);
            ImGui::SetNextItemWidth(100);
            ImGui::SameLine(0, 20);
            if (ImGui::Button("Save as"))
            {
                eXport();
            }
        }
        ImGui::PopFont();

        ImGui::PushID(9990);
        if (ImGui::Selectable(albedoName.c_str())) { loadTexture(0); changed = true; }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("ALBEDO  {%d}", _constData.albedoTexture);
            _plantMaterial::static_materials_veg.dispTexIndex = _constData.albedoTexture;
        }
        ImGui::PopID();


        ImGui::PushID(9991);
        if (ImGui::Selectable(alphaName.c_str())) { loadTexture(1); changed = true; }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("ALPHA  {%d}", _constData.alphaTexture);
            _plantMaterial::static_materials_veg.dispTexIndex = _constData.alphaTexture;
        }
        ImGui::PopID();


        ImGui::PushID(9992);
        if (ImGui::Selectable(translucencyName.c_str())) { loadTexture(2); changed = true; }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("TRANSLUCENCY  {%d}", _constData.translucencyTexture);
            _plantMaterial::static_materials_veg.dispTexIndex = _constData.translucencyTexture;
        }
        ImGui::PopID();


        ImGui::PushID(9993);
        if (ImGui::Selectable(normalName.c_str())) { loadTexture(3); changed = true; }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("NORMAL  {%d}", _constData.normalTexture);
            _plantMaterial::static_materials_veg.dispTexIndex = _constData.normalTexture;
        }
        ImGui::PopID();

        ImGui::NewLine();

        ImGui::SetNextItemWidth(80);
        if (ImGui::DragFloat("Translucency", &_constData.translucency, 0.01f, 0, 10)) changed = true;
        ImGui::SetNextItemWidth(80);
        if (ImGui::DragFloat("alphaPow", &_constData.alphaPow, 0.01f, 0.1f, 3.f)) changed = true;

        //ImGui::NewLine();

        if (ImGui::ColorEdit3("##albedo front", &_constData.albedoScale[0].x)) changed = true;
        ImGui::SetNextItemWidth(80);
        if (ImGui::DragFloat("roughness front", &_constData.roughness[0], 0.01f, 0, 1)) changed = true;
        if (ImGui::ColorEdit3("##albedo back", &_constData.albedoScale[1].x)) changed = true;
        ImGui::SetNextItemWidth(80);
        if (ImGui::DragFloat("roughness back", &_constData.roughness[1], 0.01f, 0, 1)) changed = true;

        //ImGui::NewLine();
        //ImGui::Text("index a, alpha, trans, norm(%d, %d, %d, %d) rgb", _constData.albedoTexture, _constData.alphaTexture, _constData.translucencyTexture, _constData.normalTexture);
    }


    if (changed)
    {
        changedForSave = true;
        _plantMaterial::static_materials_veg.rebuildStructuredBuffer();
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
void _plantMaterial::import(bool _replacePath)
{
    std::filesystem::path path = materialCache_plants::lastFile;
    FileDialogFilterVec filters = { {"vegetationMaterial"} };
    if (openFileDialog(filters, path))
    {
import(path, _replacePath);
        materialCache_plants::lastFile = path.string();
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
void _plantMaterial::eXport()
{
    std::filesystem::path path = materialCache_plants::lastFile;
    FileDialogFilterVec filters = { {"vegetationMaterial"} };
    if (saveFileDialog(filters, path))
    {
        eXport(path);
        materialCache_plants::lastFile = path.string();
    }
}
void _plantMaterial::reloadTextures()
{
    bool FORCE_RELOAD = true;
    _constData.albedoTexture = _plantMaterial::static_materials_veg.find_insert_texture(terrafectorEditorMaterial::rootFolder + albedoPath, true, FORCE_RELOAD);
    _constData.alphaTexture = _plantMaterial::static_materials_veg.find_insert_texture(terrafectorEditorMaterial::rootFolder + alphaPath, true, FORCE_RELOAD);
    _constData.normalTexture = _plantMaterial::static_materials_veg.find_insert_texture(terrafectorEditorMaterial::rootFolder + normalPath, false, FORCE_RELOAD);
    _constData.translucencyTexture = _plantMaterial::static_materials_veg.find_insert_texture(terrafectorEditorMaterial::rootFolder + translucencyPath, false, FORCE_RELOAD);
}





void _plantMaterial::loadTexture(int idx)
{
    std::filesystem::path path;
    FileDialogFilterVec filters = { {"png"}, {"jpg"}, {"tga"}, {"exr"}, {"dds"}, {"tif"} }; //??? only DDS
    if (openFileDialog(filters, path))
    {
        std::string P = path.string();
        replaceAllVEG(P, "\\", "/");
        if (P.find(terrafectorEditorMaterial::rootFolder) == 0) {
            std::string relative = P.substr(terrafectorEditorMaterial::rootFolder.length());
            switch (idx)
            {
            case 0:
                albedoPath = relative;
                albedoName = path.filename().string();
                _constData.albedoTexture = static_materials_veg.find_insert_texture(P, true);
                break;
            case 1:
                alphaPath = relative;
                alphaName = path.filename().string();
                _constData.alphaTexture = static_materials_veg.find_insert_texture(P, false);
                break;
            case 2:
                translucencyPath = relative;
                translucencyName = path.filename().string();
                _constData.translucencyTexture = static_materials_veg.find_insert_texture(P, false);
                break;
            case 3:
                normalPath = relative;
                normalName = path.filename().string();
                _constData.normalTexture = static_materials_veg.find_insert_texture(P, false);
                break;
            }
        }
    }
}







void _vegMaterial::loadFromFile()
{
    std::filesystem::path filepath;
    FileDialogFilterVec filters = { {"vegetationMaterial"} };
    if (openFileDialog(filters, filepath))
    {
        index = _plantMaterial::static_materials_veg.find_insert_material(filepath);
        path = _plantMaterial::static_materials_veg.materialVector[index].relativePath;
        name = filepath.filename().string().substr(0, filepath.filename().string().length() - 19);
    }
}

void _vegMaterial::reload()
{
    // force a reload
    bool FORCE_RELOAD = true;
    index = _plantMaterial::static_materials_veg.find_insert_material(terrafectorEditorMaterial::rootFolder + path, FORCE_RELOAD);
}

bool _vegMaterial::renderGui(uint& gui_id)
{
    ImGui::PushID(gui_id);
    if (ImGui::Selectable(name.c_str())) loadFromFile();
    std::string tool = path + "\n{" + std::to_string(index) + "}";
    TOOLTIP(tool.c_str());
    ImGui::DragFloat2("albedo", &albedoScale.x, 0.01f, 0.1f, 2.0f);
    ImGui::DragFloat2("translucency", &translucencyScale.x, 0.01f, 0.1f, 2.0f);
    ImGui::PopID();
    gui_id++;

    return false;
}


// FXME can I amstarct this away mnore, and combine it with the load at the bottom of the file that is amlmost identical
void _plantRND::loadFromFile()
{
    std::filesystem::path filepath;
    FileDialogFilterVec filters = { {"leaf"}, {"stem"}, {"clump"} };
    if (openFileDialog(filters, filepath))
    {
        if (filepath.string().find("leaf") != std::string::npos) { plantPtr.reset(new _leafBuilder); type = P_LEAF; }
        if (filepath.string().find("stem") != std::string::npos) { plantPtr.reset(new _stemBuilder);  type = P_STEM; }
        if (filepath.string().find("clump") != std::string::npos) { plantPtr.reset(new _clumpBuilder);  type = P_CLUMP; }

        path = materialCache::getRelative(filepath.string());
        name = filepath.filename().string();
        plantPtr->path = path;
        plantPtr->name = name;
        plantPtr->loadPath();
    }
}


void _plantRND::reload()
{
    switch (type)
    {
    case P_LEAF:   plantPtr.reset(new _leafBuilder);   break;
    case P_STEM:   plantPtr.reset(new _stemBuilder);   break;
    case P_CLUMP:   plantPtr.reset(new _clumpBuilder);   break;
    default:  plantPtr.reset();  break;
    }

    if (plantPtr)
    {
        plantPtr->path = path;
        plantPtr->name = name;
        plantPtr->loadPath();
    }
}


void _plantRND::renderGui(uint& gui_id)
{
    if (ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Framed))
    {
        TOOLTIP(path.c_str());
        if (ImGui::IsItemClicked())  loadFromFile();
        ImGui::TreePop();
    }
}


Gui* _plantBuilder::_gui;



template <class T>     T randomVector<T>::get()
{    
    rnd_idx += _rootPlant::rand_int(_rootPlant::generator);
    rnd_idx %= data.size();
    return data[rnd_idx];
}

template <class T>     void randomVector<T>::renderGui(char* name, uint& gui_id)
{
    bool first = true;

    {
        ImGui::Text(name);
        ImGui::SameLine(180, 0);
        if (ImGui::Button("-", ImVec2(20, 0))) { data.pop_back(); if (data.size() == 0) { data.emplace_back(); } }
        ImGui::SameLine(0, 10);
        if (ImGui::Button("+", ImVec2(20, 0))) { data.emplace_back(); }

        for (auto& D : data) D.renderGui(gui_id);
    }
}


bool anyChange = true;

// New random branches with age factor


// FXME can I amstarct this away mnore, and combine it with the load at the bottom of the file that is amlmost identical
void _randomBranch::loadFromFile()
{
    std::filesystem::path filepath;
    FileDialogFilterVec filters = { {"leaf"}, {"stem"}, {"clump"} };
    if (openFileDialog(filters, filepath))
    {
        if (filepath.string().find("leaf") != std::string::npos) { plantPtr.reset(new _leafBuilder); type = P_LEAF; }
        if (filepath.string().find("stem") != std::string::npos) { plantPtr.reset(new _stemBuilder);  type = P_STEM; }
        if (filepath.string().find("clump") != std::string::npos) { plantPtr.reset(new _clumpBuilder);  type = P_CLUMP; }

        path = materialCache::getRelative(filepath.string());
        name = filepath.filename().string();
        plantPtr->path = path;
        plantPtr->name = name;
        plantPtr->loadPath();
    }
}


void _randomBranch::reload()
{
    switch (type)
    {
    case P_LEAF:   plantPtr.reset(new _leafBuilder);   break;
    case P_STEM:   plantPtr.reset(new _stemBuilder);   break;
    case P_CLUMP:   plantPtr.reset(new _clumpBuilder);   break;
    default:  plantPtr.reset();  break;
    }

    if (plantPtr)
    {
        plantPtr->path = path;
        plantPtr->name = name;
        plantPtr->loadPath();
    }
}


bool _randomBranch::renderGui(uint& gui_id)
{
    bool changed = false;
    if (ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Framed))
    {
        TOOLTIP(path.c_str());
        if (ImGui::IsItemClicked()) { loadFromFile(); changed = true; }

        if (ImGui::DragFloat3("params", &params.x, 0.001f, 0.f, 1.f)) { changed = true; }
        TOOLTIP("density, midd, range");
        ImGui::TreePop();
    }
    return changed;
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


bool semiRandomBranch::renderGui(char* name, uint& gui_id)
{
    bool first = true;
    bool changed = false;

    {
        ImGui::Text(name);
        ImGui::SameLine(180, 0);
        if (ImGui::Button("-", ImVec2(20, 0))) { branchData.pop_back();        }
        ImGui::SameLine(0, 10);
        if (ImGui::Button("+", ImVec2(20, 0))) { branchData.emplace_back();      }

        for (int i = 0; i < 1024; i+=100)
        {
            ImGui::Text("%d, ", RND[i]);
            ImGui::SameLine(0, 5);
        }
        ImGui::NewLine();

        for (auto& D : branchData) if (D.renderGui(gui_id)) { std::sort(branchData.begin(), branchData.end()); buildArray(); changed = true;        }
    }

    return changed;
}



ImVec4 selected_color = ImVec4(0.4f, 0.1f, 0.0f, 1);
ImVec4 stem_color = ImVec4(0.05f, 0.02f, 0.0f, 1);
ImVec4 leaf_color = ImVec4(0.01f, 0.04f, 0.01f, 1);
ImVec4 mat_color = ImVec4(0.0f, 0.01f, 0.07f, 1);

#define CLICK_PART if (ImGui::IsItemClicked()) { _rootPlant::selectedPart = this; _rootPlant::selectedMaterial = nullptr; }
#define CLICK_MATERIAL if (ImGui::IsItemClicked()) { _rootPlant::selectedPart = nullptr; _rootPlant::selectedMaterial = &mat; }
#define TOOLTIP_PART(x)  if (ImGui::IsItemHovered()) {ImGui::SetTooltip(x);}
#define TOOLTIP_MATERIAL(x)  if (ImGui::IsItemHovered()) {ImGui::SetTooltip(x);}
//#define TREE_MAT(x)  style.Colors[ImGuiCol_HeaderActive] = mat_color; ImGui::TreeNodeEx(x, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Framed);
//#define TREE_LEAF(x)  style.Colors[ImGuiCol_HeaderActive] = leaf_color; if(ImGui::TreeNodeEx(x, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Framed)) 

#define TREE_LINE(x,t,f)  if(ImGui::TreeNodeEx(x, f))

#define textWIDTH 120
#define itemWIDTH 80

#define R_LENGTH(name,data,t1,t2)    ImGui::PushID(gui_id); \
                    ImGui::Text(name);    \
                    ImGui::SameLine(textWIDTH, 0); \
                    ImGui::SetNextItemWidth(itemWIDTH);    \
                    if (ImGui::DragFloat("##X", &data.x, 1.f, 0, 2000, "%2.2fmm")) changed = true; \
                    TOOLTIP(t1); \
                    ImGui::SameLine(0, 10); \
                    ImGui::SetNextItemWidth(itemWIDTH);    \
                    if (ImGui::DragFloat("##Y", &data.y, 0.01f, 0, 1, "%1.3f")) changed = true; \
                    TOOLTIP(t2); \
                    ImGui::PopID(); \
                    gui_id ++;

#define R_LENGTH_EX(name,data, scl, min, max, t1, t2)    ImGui::PushID(gui_id); \
                    ImGui::Text(name);    \
                    ImGui::SameLine(textWIDTH, 0); \
                    ImGui::SetNextItemWidth(itemWIDTH);    \
                    if (ImGui::DragFloat("##X", &data.x, scl, min, max, "%2.2fmm")) changed = true; \
                    TOOLTIP(t1); \
                    ImGui::SameLine(0, 10); \
                    ImGui::SetNextItemWidth(itemWIDTH);    \
                    if (ImGui::DragFloat("##Y", &data.y, 0.01f, 0, 1, "%1.3f")) changed = true; \
                    TOOLTIP(t2); \
                    ImGui::PopID(); \
                    gui_id ++;

#define R_CURVE(name,data,t1,t2)    ImGui::PushID(gui_id); \
                    ImGui::Text(name);    \
                    ImGui::SameLine(textWIDTH, 0); \
                    ImGui::SetNextItemWidth(itemWIDTH);    \
                    if (ImGui::DragFloat("##X", &data.x, 0.01f, -5, 5, "%1.2f")) changed = true; \
                    TOOLTIP(t1); \
                    ImGui::SameLine(0, 10); \
                    ImGui::SetNextItemWidth(itemWIDTH);    \
                    if (ImGui::DragFloat("##Y", &data.y, 0.01f, 0, 5, "%1.3f")) changed = true; \
                    TOOLTIP(t2); \
                    ImGui::PopID(); \
                    gui_id ++;


#define R_VERTS(name,data)    ImGui::PushID(gui_id); \
                    ImGui::Text(name);    \
                    ImGui::SameLine(textWIDTH, 0); \
                    ImGui::SetNextItemWidth(itemWIDTH);    \
                    if (ImGui::DragInt("##X", &data.x, 0.1f, 2, 10)) changed = true; \
                    ImGui::SameLine(0, 10); \
                    ImGui::SetNextItemWidth(itemWIDTH);    \
                    if (ImGui::DragInt("##Y", &data.y, 0.1f, 3, 32)) changed = true; \
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


#define FONT_TEXT(f, s) ImGui::PushFont(_gui->getFont(f));   ImGui::Text(s);   ImGui::PopFont();

#define CHECKBOX(name, val, tooltip)  ImGui::PushID(gui_id); \
                    ImGui::Text(name);    \
                    ImGui::SameLine(textWIDTH + itemWIDTH, 0); \
                    style.FrameBorderSize = 1; \
                    if (ImGui::Checkbox("", val)) changed = true;    \
                    style.FrameBorderSize = 0; \
                    TOOLTIP(tooltip); \
                    ImGui::PopID(); \
                    gui_id ++;




// _leafBuilder
// -----------------------------------------------------------------------------------------------------------------------------------
void _leafBuilder::loadPath()
{
    std::ifstream is(terrafectorEditorMaterial::rootFolder + path);
    cereal::JSONInputArchive archive(is);
    archive(*this);
    changed = false;
}

void _leafBuilder::savePath()
{
    std::ofstream os(terrafectorEditorMaterial::rootFolder + path);
    cereal::JSONOutputArchive archive(os);
    archive(*this);
    changed = false;
}

void _leafBuilder::load()
{
    if (ImGui::Button("Load##_leafBuilder", ImVec2(60, 0)))
    {
        std::filesystem::path filepath;
        if (openFileDialog(filters, filepath))
        {
            path = materialCache::getRelative(filepath.string());
            name = filepath.filename().string();
            loadPath();
        }
    }
}

void _leafBuilder::save()
{
    if (changedForSave) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.1f, 0.0f, 0.8f));
        if (ImGui::Button("Save##_leafBuilder", ImVec2(60, 0)))
        {
            savePath();
            changedForSave = false;
        }
        ImGui::PopStyleColor();
    }
}

void _leafBuilder::saveas()
{
    if (ImGui::Button("Save as##_leafBuilder", ImVec2(60, 0)))
    {
        std::filesystem::path filepath;
        if (saveFileDialog(filters, filepath))
        {
            path = materialCache::getRelative(filepath.string());
            name = filepath.filename().string();
            savePath();
        }
    }
}


void _leafBuilder::renderGui()
{
    auto& style = ImGui::GetStyle();
    int flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_OpenOnArrow;
    unsigned int gui_id = 1001;

    ImGui::PushFont(_gui->getFont("header1"));
    {
        ImGui::Text(name.c_str());
    }
    ImGui::PopFont();
    TOOLTIP(path.c_str());

    ImGui::PushFont(_gui->getFont("header2"));
    {
        load();
        ImGui::SameLine(70, 0);
        save();
        ImGui::SameLine(140, 0);
        saveas();
    }
    ImGui::PopFont();



    ImGui::NewLine();
    style.Colors[ImGuiCol_Header] = ImVec4(0.01f, 0.01f, 0.01f, 1.f);
    if (ImGui::TreeNodeEx("animation", flags))
    {
        if (ImGui::Combo("pivot type", (int*)&pivotType, "off\0leaf\0full 1\0full 2\0")) { changed = true; }

        R_FLOAT("stiffness", ossilation_stiffness, 0.1f, 0.8f, 20.f, "");
        R_FLOAT("sqrt(sway)", ossilation_constant_sqrt, 0.1f, 1.01f, 100.f, "");
        ImGui::SameLine(0, 20);
        ImGui::Text("%2.3fHz", 1.f / (rootFrequency() * sqrt(leaf_length.x * 0.001f) / sqrt(leaf_width.x * 0.001f)));
        //R_FLOAT("root-tip", ossilation_power, 0.01f, 0.02f, 1.8f, "");
        ImGui::TreePop();
    }

    ImGui::NewLine();
    style.Colors[ImGuiCol_Header] = ImVec4(0.01f, 0.01f, 0.01f, 1.f);
    if (ImGui::TreeNodeEx("stem", flags))
    {

        R_LENGTH("length", stem_length, "stem length in mm", "random");
        R_LENGTH("width", stem_width, "stem width in mm", "random");
        if (stem_length.x > 0 && stem_width.x > 0)
        {
            R_CURVE("curve", stem_curve, "stem curvature", "random");

            R_VERTS("num verts", stemVerts);
            stem_Material.renderGui(gui_id);
        }
        R_CURVE("angle", stem_to_leaf, "stem to leaf angle in radians", "random");
        R_CURVE("roll", stem_to_leaf_Roll, "roll like ferns to make them all face one side", "random");


        ImGui::TreePop();
    }

    ImGui::NewLine();
    style.Colors[ImGuiCol_Header] = ImVec4(0.01f, 0.01f, 0.01f, 1.f);
    if (ImGui::TreeNodeEx("leaf", flags))
    {
        CHECKBOX("face camera", &cameraFacing, "");
        //if (ImGui::Checkbox("face camera", &cameraFacing)) changed = true;
        R_LENGTH("length", leaf_length, "mm", "random");
        R_LENGTH("width", leaf_width, "mm", "random");
        R_LENGTH("offset", width_offset, "", "random");
        R_CURVE("curve", leaf_curve, "radians", "random");
        R_CURVE("perlin C", perlinCurve, "amount", "repeats");
        R_CURVE("twist", leaf_twist, "radians", "random");
        R_CURVE("perlin T", perlinTwist, "amount", "repeats");
        R_CURVE("gavity", gravitrophy, "rotate towards the ground or sky", "random");
        R_VERTS("num verts", numVerts);
        CHECKBOX("use diamond", &useTwoVertDiamond, "use diamond pattern when the leaf uses only 2 vertices");
        R_FLOAT("length split", leafLengthSplit, 0.1f, 4.f, 100.f, "Number of pixels to insert a new node in this leaf, pusg it higher for very curved leaves \nWill still be clamped by min and max above");


        ImGui::NewLine();
        style.Colors[ImGuiCol_Header] = ImVec4(0.03f, 0.03f, 0.03f, 1.f);
        materials.renderGui("materials", gui_id);
        ImGui::TreePop();
    }

    changedForSave |= changed;
    anyChange |= changed;
    changed = false;
}


void _leafBuilder::treeView()
{
    auto& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Header] = leaf_color;
    int flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_OpenOnArrow;
    if (_rootPlant::selectedPart == this) {
        style.FrameBorderSize = 3;
        flags = flags | ImGuiTreeNodeFlags_Selected;
        //style.Colors[ImGuiCol_Header] = selected_color;
        style.Colors[ImGuiCol_Border] = ImVec4(0.7f, 0.7f, 0.7f, 1);
    }

    if (changedForSave) style.Colors[ImGuiCol_Header] = selected_color;

    TREE_LINE(name.c_str(), path.c_str(), flags)
    {
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%d instances \n%d verts", numInstancePacked, numVertsPacked);
        }

        style.FrameBorderSize = 0;
        CLICK_PART;
        ImGui::TreePop();
    }
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
    numPivots = 0;
}


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
            //fprintf(terrafectorSystem::_logfile, "  leaf-stem : mat %d  -  % \n", stem_Material.index, stem_Material.name.c_str());
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
        //fprintf(terrafectorSystem::_logfile, "  leaf : mat %d  -  % \n", mat.index, mat.name.c_str());
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
            float step = 99.f / numLeaf;
            float cnt = 0;
            bool useDiamond = (useTwoVertDiamond && (numLeaf == 1));

            for (int i = 0; i < 100; i++)
            {
                float t = (float)i / 99.f;
                float du = __min(1.f, sin(pow(t, width_offset.x) * 3.1415f) + width_offset.y);
                if (numLeaf == 1) du = 1.f;

                float perlinScale = glm::smoothstep(0.f, 0.3f, t) * age;
                float noise = (float)perlin.normalizedOctave1D(perlinCurve.y * t, 4);
                PITCH(node, noise * perlinCurve.x * perlinScale);

                noise = (float)perlinTWST.normalizedOctave1D(perlinTwist.y * t, 4) * age;
                ROLL(node, noise * perlinTwist.x * perlinScale);

                ROLL(node, twist);
                PITCH(node, curve);
                GROW(node, length);

                if (_addVerts)
                {
                    if (i == 0)
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

    uint numVerts = _ribbonBuilder.numVerts() - startVerts;
    if (numVerts > 0) numInstancePacked++;
    numVertsPacked += numVerts;
    changedForSave |= changed;
    return node;
}






// _stemBuilder
// -----------------------------------------------------------------------------------------------------------------------------------

void _stemBuilder::loadPath()
{
    std::ifstream is(terrafectorEditorMaterial::rootFolder + path);
    cereal::JSONInputArchive archive(is);
    archive(*this);
    changed = false;
}

void _stemBuilder::savePath()
{
    std::ofstream os(terrafectorEditorMaterial::rootFolder + path);
    cereal::JSONOutputArchive archive(os);
    archive(*this);
    changed = false;
}


void _stemBuilder::load()
{
    if (ImGui::Button("Load##_stemBuilder", ImVec2(80, 0)))
    {
        std::filesystem::path filepath;
        if (openFileDialog(filters, filepath))
        {
            path = materialCache::getRelative(filepath.string());
            name = filepath.filename().string();
            loadPath();
        }
    }
}


void _stemBuilder::save()
{
    if (changedForSave) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.1f, 0.0f, 0.8f));
        if (ImGui::Button("Save##_stemBuilder", ImVec2(80, 0)))
        {
            savePath();
            changedForSave = false;
        }
        ImGui::PopStyleColor();
    }
}


void _stemBuilder::saveas()
{
    if (ImGui::Button("Save as##_stemBuilder", ImVec2(80, 0)))
    {
        std::filesystem::path filepath;
        if (saveFileDialog(filters, filepath))
        {
            path = materialCache::getRelative(filepath.string());
            name = filepath.filename().string();
            savePath();
        }
    }
}


void _stemBuilder::renderGui()
{
    auto& style = ImGui::GetStyle();
    int flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_OpenOnArrow;
    int flagsB = ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_OpenOnArrow;

    ImGui::PushFont(_gui->getFont("header1"));
    {
        ImGui::Text(name.c_str());
    }
    ImGui::PopFont();
    TOOLTIP(path.c_str());

    ImGui::PushFont(_gui->getFont("header2"));
    {
        load();
        ImGui::SameLine(90, 0);
        save();
        ImGui::SameLine(180, 0);
        saveas();
    }
    ImGui::PopFont();


    unsigned int gui_id = 1001;

    ImGui::NewLine();
    style.Colors[ImGuiCol_Header] = ImVec4(0.01f, 0.01f, 0.01f, 1.f);
    if (ImGui::TreeNodeEx("light", flagsB))
    {

        ImGui::PushFont(_gui->getFont("italic"));
        {
            R_FLOAT("uv scale", shadowUVScale, 0.001f, 0.001f, 10.f, "");
            R_FLOAT("softness", shadowSoftness, 0.01f, 0.01f, 0.3f, "");
            R_FLOAT("depth", shadowDepth, 0.01f, 0.01f, 10.f, "");
            R_FLOAT("hgt", shadowPenetationHeight, 0.01f, 0.05f, 0.95f, "");
        }
        ImGui::PopFont();

        ImGui::TreePop();
    }


    ImGui::NewLine();
    style.Colors[ImGuiCol_Header] = ImVec4(0.01f, 0.01f, 0.01f, 1.f);
    if (ImGui::TreeNodeEx("animation", flags))
    {
        ImGui::SameLine(0, 50);
        float l = 1;
        if (NODES.size() >= 2)
        {
            l = glm::length(NODES.back()[3] - NODES.front()[3]);
        }
        if (l == 0) l = 1; // avoid devide by zero
        ImGui::Text("%2.3fHz", 1.f / (rootFrequency() / sqrt(l)));

        CHECKBOX("hasPivot", &hasPivot, "");
        R_FLOAT("stiffness", ossilation_stiffness, 0.1f, 0.8f, 20.f, "");
        R_FLOAT("sqrt(sway)", ossilation_constant_sqrt, 0.1f, 1.01f, 100.f, "");
        //R_FLOAT("root-tip", ossilation_power, 0.01f, 0.02f, 1.8f, "");

        ImGui::TreePop();
    }



    ImGui::NewLine();
    style.Colors[ImGuiCol_Header] = ImVec4(0.01f, 0.01f, 0.01f, 1.f);
    if (ImGui::TreeNodeEx("stem / trunk", flags))
    {

        ImGui::SameLine(0, 10);
        ImGui::Text("%2.1f - %2.1f)mm, [%d, %d]", root_width * 1000.f, tip_width * 1000.f, (int)age, firstLiveSegment);

        R_LENGTH_EX("age", numSegments, 0.1f, 0.5f, 500.f, "total number of nodes", "random is used for automatic variations");
        if (numSegments.x < 0.5f) numSegments.x = 0.5f;
        R_LENGTH("living", max_live_segments, "how many of these support growth, can be bigger than totalm then all except very first one do", "random is used for automatic variations");

        R_LENGTH("length", stem_length, "stem length in mm", "random");
        R_LENGTH_EX("width", stem_width, 0.002f, 0.5f, 20.f, "stem width in mm", "random");
        R_LENGTH_EX("growth", stem_d_width, 0.001f, 0.1f, 20.f, "amount to grow for each node", "random");
        R_LENGTH_EX("pow", stem_pow_width, 0.01f, 0.1f, 10.f, "scale growth towards tip or root", "random");
        R_CURVE("curve", stem_curve, "curve in a single segment", "random");
        R_CURVE("phototropism", stem_phototropism, "", "random");
        R_CURVE("node twist", node_rotation, "twist of the stalk", "random");
        R_CURVE("node bend", node_angle, "stem bend at a node", "random");
        R_CURVE("perlin Pitch", perlinCurve, "amount", "repeats");
        R_CURVE("perlin Yaw", perlinTwist, "amount", "repeats");
        style.Colors[ImGuiCol_Header] = ImVec4(0.03f, 0.03f, 0.03f, 1.f);
        stem_Material.renderGui(gui_id);
        R_FLOAT("length split", nodeLengthSplit, 0.1f, 4.f, 100.f, "Number of pixels to insert a new node in this leaf, pusg it higher for very curved leaves");

        stemReplacement.renderGui("stem override", gui_id);

        ImGui::TreePop();
    }


    // leaves
    ImGui::NewLine();
    style.Colors[ImGuiCol_Header] = ImVec4(0.01f, 0.01f, 0.01f, 1.f);
    if (ImGui::TreeNodeEx("branches / leaves", flags))
    {

        ImGui::SameLine(0, 20);
        ImGui::Text("[%d]", numLeavesBuilt);

        R_LENGTH_EX("#", numLeaves, 0.1f, 0.5f, 10.f, "number of leaves per node", "random");
        numLeaves.x = __max(0.5f, numLeaves.x);
        R_CURVE("angle", leaf_angle, "angle of the stalk at the tip", "change of angle as it ages, usually drooping");
        R_CURVE("random", leaf_rnd, "randomness in the angles", "random");
        CHECKBOX("roll horizontal", &roll_horizontal, "roll the branch do that the first leaf exists horizontally like many pine and fir trees");
        R_FLOAT("roll offset", rollOffset, 0.01f, -3.5f, 3.5f, "in itial roll angle, relative is horizon is set");
        R_FLOAT("age_power", leaf_age_power, 0.1f, 1.f, 25.f, "");
        CHECKBOX("age override", &leaf_age_override, " If set false we pass in -1 and the leaf can set its own age, if true we set the age");
        style.Colors[ImGuiCol_Header] = ImVec4(0.03f, 0.03f, 0.03f, 1.f);
        //leaves.renderGui("leaves", gui_id);
        if (branches.renderGui("branches", gui_id)) { changed = true; }

        ImGui::NewLine();
        CHECKBOX("unique tip", &unique_tip, "load a different part for the tip \nif off it will reuse one of the breanch / leaf parts");

        if (unique_tip)
        {
            R_LENGTH("tip age", tip_age, "age of the tip\n-1 means that the tip plant will decide", "random");
            style.Colors[ImGuiCol_Header] = ImVec4(0.03f, 0.03f, 0.03f, 1.f);
            tip.renderGui("tip", gui_id);
        }
        ImGui::TreePop();
    }

    // tip
    ImGui::NewLine();

    changedForSave |= changed;
    anyChange |= changed;
    changed = false;
}

void _stemBuilder::treeView()
{
    auto& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Header] = stem_color;
    int flags = ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_OpenOnArrow;
    if (_rootPlant::selectedPart == this) {
        style.FrameBorderSize = 3;
        flags = flags | ImGuiTreeNodeFlags_Selected;
        style.Colors[ImGuiCol_Border] = ImVec4(0.7f, 0.7f, 0.7f, 1);
    }

    if (changedForSave) style.Colors[ImGuiCol_Header] = selected_color;

    TREE_LINE(name.c_str(), path.c_str(), flags)
    {
        if (ImGui::IsItemHovered()) {
            if (_ribbonBuilder.tooManyPivots)     ImGui::SetTooltip("ERROR - more than 255 pivots");
            else ImGui::SetTooltip("%d instances \n%d verts \n%d / %d pivots ", numInstancePacked, numVertsPacked, numPivots, _ribbonBuilder.numPivots());
        }

        style.FrameBorderSize = 0;
        CLICK_PART;
        ImGui::Text("leaves");
        for (auto& L : leaves.data) { if (L.plantPtr) L.plantPtr->treeView(); }
        if (unique_tip)
        {
            ImGui::Text("tip");
            for (auto& L : tip.data) { if (L.plantPtr) L.plantPtr->treeView(); }
        }
        ImGui::TreePop();
    }
}


lodBake* _stemBuilder::getBakeInfo(uint i)
{
    if (i < lod_bakeInfo.size()) return &lod_bakeInfo[i];
    else return nullptr;
}

levelOfDetail* _stemBuilder::getLodInfo(uint i)
{
    if (i < lodInfo.size()) return &lodInfo[i];
    else return nullptr;
}



glm::mat4  _stemBuilder::build_2(buildSetting _settings, uint _bakeIndex, bool _faceCamera)
{
    lodBake& lB = lod_bakeInfo[_bakeIndex];
    glm::mat4 node = NODES.front();
    glm::mat4 last = NODES.back();
    if (lB.pixHeight > 0)
    {
        float w = lB.extents.x * lB.bakeWidth;
        uint mat = lB.material.index;

        float tipLength = glm::dot(tip_NODE[3] - last[3], node[1]);
        GROW(last, tipLength);

        // adjust start end
        float totalLenght = glm::length(last[3] - node[3]);
        GROW(node, totalLenght * lB.bake_V.x);
        GROW(last, -totalLenght * (1 - lB.bake_V.y));

        // ROTATE node and last so axis lines up
        float4 tangent = glm::normalize(last[3] - node[3]);
        node[1] = tangent;
        last[1] = tangent;

        //fprintf(terrafectorSystem::_logfile, "  _stemBuilder::build_2 : mat %d  -  % \n", lB.material.index, lB.material.name.c_str());
        _ribbonBuilder.startRibbon(lB.faceCamera, _settings.pivotIndex);
        _ribbonBuilder.set(node, w, mat, float2(1.f, 0.f), 1.f, 1.f, true, 0.5f, 0.1f, 0.0f, true);
        _ribbonBuilder.set(last, w, mat, float2(1.f, 1.f), 1.f, 1.f, true, 0.5f, 0.1f, 0.0f, true);
    }

    return last;
}


glm::mat4  _stemBuilder::build_4(buildSetting _settings, uint _bakeIndex, bool _faceCamera)
{
    lodBake& lB = lod_bakeInfo[_bakeIndex];
    float w = lB.extents.x * lB.bakeWidth;
    uint mat = lB.material.index;

    glm::mat4 node = NODES.front();
    glm::mat4 last = NODES.back();
    float tipLength = glm::dot(tip_NODE[3] - last[3], node[1]); 
    GROW(last, tipLength);

    glm::vec4 step = (last[3] - node[3]);
    float4 binorm_step = (last[1] - node[1]);

    node[3] += step * lB.bake_V.x;
    node[1] += binorm_step * lB.bake_V.x;
    step *= (lB.bake_V.y - lB.bake_V.x) / 3.f;
    binorm_step *= (lB.bake_V.y - lB.bake_V.x) / 3.f;

    //fprintf(terrafectorSystem::_logfile, "  _stemBuilder::build_4 : mat %d  -  % \n", lB.material.index, lB.material.name.c_str());
    _ribbonBuilder.startRibbon(lB.faceCamera, _settings.pivotIndex);

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
            }
        }

        _ribbonBuilder.set(CURRENT, w * lod_bakeInfo[_bakeIndex].dU[i], mat, float2(lod_bakeInfo[_bakeIndex].dU[i], 1.f - (0.3333333f * i)), 1.f, 1.f);
        node[3] += step;
        node[1] += binorm_step;
    }

    return last;
}


glm::mat4  _stemBuilder::build_n(buildSetting _settings, uint _bakeIndex, bool _faceCamera)
{
    return NODES.front();
}


void _stemBuilder::build_tip(buildSetting _settings, bool _addVerts)
{
    tip.reset();
    _settings.isBaking = false;
    _settings.seed = (_settings.seed % 5000) + 30001;
    _rootPlant::generator.seed(_settings.seed + 1999);

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

        if (unique_tip && tip.get().plantPtr)
        {
            _settings.node_age = RND_B(tip_age);
            tip_NODE = tip.get().plantPtr->build(_settings, _addVerts);
        }
        else
        {
            //_plantRND LEAF = leaves.get();
        }
    }
}


void _stemBuilder::build_leaves(buildSetting _settings, uint _max, bool _addVerts)
{
    //leaves.reset(); // so order remains the same
    _settings.isBaking = false;
    numLeavesBuilt = 0;
    // reset teh seed so all lods build teh same here
    _rootPlant::generator.seed(_settings.seed + 999);
    int oldSeed = _settings.seed * 10 + 1;//
    std::mt19937 MT(_settings.seed + 999);
    
    // side nodes
    uint end = NODES.size() - 1; // - do not include the tip

    float nodeRoll = 0;
    for (int i = 0; i <= end; i++)
    {
        if (i >= firstLiveSegment)
        {
            float leafAge = 1.f - pow(i / age, leaf_age_power);
            int numL = (int)RND_B(numLeaves);
            float t = (float)i / age;
            float t_live = glm::clamp((i - firstLiveSegment) / (age - firstLiveSegment), 0.f, 1.f);
            t_live = 1.f - pow(t_live, leaf_age_power);
            float W = root_width - (root_width - tip_width) * pow(t, stem_pow_width.x);

            float rndRoll = 6.28f * d_1_1(MT);
            float halfTwist = 6.283185307f / (float)numL / 2.f;
            nodeRoll += halfTwist;
            //nodeRoll += rndRoll / (float)numL;

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
                    if (j % 2)    ROLL(node, rollOffset);
                    else        ROLL(node, -rollOffset);
                }
                else
                {
                    ROLL(node, rollOffset);
                }

                float nodeTwist = 6.283185307f / (float)numL * (float)j;    // this takes us around
                nodeTwist += d_1_1(MT) * leaf_rnd.x;
                ROLL(node, nodeTwist);
                
                if ((numL == 1) && (i & 0x1))  ROLL(node, 3.14f);   // 180 degrees is 1 leaf
                PITCH(node, -A);
                GROW(node, W * 0.45f / fabs(sin(A)));   // 70% out, has to make up for alpha etcDman I want to grow here to some % of branch width
                // likely dependent on pitch

                // And now rotate upwards again.
                // ZERO is not acceptable for an age, if true, dont acll anything
                _settings.seed = oldSeed + (i * 100) + j;
                _settings.root = node;
                if (leaf_age_override)  _settings.node_age = age - i + 1;
                else                    _settings.node_age = -leafAge;
                _settings.normalized_age = t_live;
                if (_settings.normalized_age > 0.05f)
                {
                    float branchAge = (i / (float)age) + (j / (float)age / (float)numL);
                    _rootPlant::generator.seed(_settings.seed);
                    _randomBranch *pBranch = branches.get(branchAge);
                    if (pBranch->plantPtr) pBranch->plantPtr->build(_settings, _addVerts);
                }
            }
        }
    }
}


void _stemBuilder::build_NODES(buildSetting _settings, bool _addVerts)
{
    _rootPlant::generator.seed(_settings.seed + 33);
    std::uniform_int_distribution<> distAlbedo(-50, 50);
    glm::mat4 node = _settings.root;
    if (roll_horizontal)    {   ROLL_HORIZONTAL(node);    }
    NODES.clear();
    NODES.push_back(node);

    age = RND_B(numSegments);
    if (_settings.node_age > 0.f)
    {
        age = _settings.node_age * RND_B(float2(1, numSegments.y));
    }// if passed in from root use that
    else                                {        age *= abs(_settings.node_age);    } // negative values are relative
    int iAge = __max(1, (int)age);

    tip_width = RND_B(stem_width) * 0.001f;                         // tip radius
    root_width = tip_width + RND_B(stem_d_width) * 0.001f * age;    // root radius  //?? iAge
    float rootPow = stem_pow_width.x;
    float dR = (root_width - tip_width);

    int numLiveNodes = (int)RND_B(max_live_segments);
    firstLiveSegment = __max(1, iAge - numLiveNodes);

    std::uniform_real_distribution<> d50(0.5f, 1.5f);
    float pixRandFoViz = _settings.pixelSize * d50(_rootPlant::generator);

    bool visible = root_width > pixRandFoViz && (stem_Material.index >= 0);
    if (_addVerts && visible) {
        //fprintf(terrafectorSystem::_logfile, "  _stemBuilder::build_NODES : mat %d  -  % \n", stem_Material.index, stem_Material.name.c_str());
        _ribbonBuilder.startRibbon(true, _settings.pivotIndex);
        _ribbonBuilder.set(node, root_width * 0.5f, stem_Material.index, float2(1.f, 0.f), 1.f, 1.f);   // set very first one
    }

    // FIXME - VERY bad need way to control ut a bit with LOD but puxels make no sense on this scale
    float stemLenght = glm::length(tip_NODE[3] - node[3]);
    float stemPixels = (stemLenght * 20) / _settings.pixelSize;
    int stemNumSegments = (int)nodeLengthSplit;// glm::clamp((int)(stemPixels / nodeLengthSplit), 1, 10);     // 1 for every 8 pixels, clampped
    float totalStep = 20.f * iAge / (float)stemNumSegments;
    float cnt = 0;

    float V = 0.f;

    for (int i = 0; i < iAge; i++)
    {
        if (stemReplacement.get().plantPtr)
        {
            _settings.doNotAddPivot = true;
            _settings.root = stemReplacement.get().plantPtr->build(_settings, _addVerts);
            NODES.push_back(_settings.root);
            _settings.doNotAddPivot = false;
        }
        else
        {

            float nodeAge = glm::clamp((age - i) / numLiveNodes, 0.f, 1.f);
            float L = RND_B(stem_length) * 0.001f / 20.f;
            float C = RND_CRV(stem_curve) / 20.f / age;
            //float P = RND_CRV(stem_phototropism) / 20.f / age;
            float t = (float)i / age;
            float W = root_width - dR * pow(t, rootPow);
            //float nodePixels = (L * 20) / _settings.pixelSize;
            //int nodeNumSegments = glm::clamp((int)(nodePixels / nodeLengthSplit), 1, 20);     // 1 for every 8 pixels, clampped
            //float segStep = 19.f / (float)nodeNumSegments;

            // Phototropism with changes on age
            float pScale = (nodeAge * stem_phototropism.y) + ((1.f - nodeAge) * (1.f - stem_phototropism.y));
            float P = pScale * stem_phototropism.x / 20.f / age;


            //float cnt = 0;
            for (int j = 0; j < 20; j++)
            {
                V += L / W;
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
                        //PITCH(node, noise * perlinCurve.x * perlinScale * 0.01f);
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
                if (_addVerts && visible && cnt >= totalStep)
                {
                    //float V = (i + (float)j / 20.f) * 0.5f;
                    _ribbonBuilder.set(node, W * 0.5f, stem_Material.index, float2(1.f, V), 1.f, 1.f);
                    cnt -= totalStep;
                }
            }
            NODES.push_back(node);

            // now rotate for teh next segment
            ROLL(node, RND_CRV(node_rotation) / age);
            YAW(node, RND_CRV(node_angle) / age);
        }
    }
}


void _stemBuilder::clear_build_info()
{
    numInstancePacked = 0;
    numVertsPacked = 0;
    numPivots = 0;

    for (auto& L : leaves.data)
    {
        if (L.plantPtr)  L.plantPtr->clear_build_info();
    }

    for (auto& L : tip.data)
    {
        if (L.plantPtr)  L.plantPtr->clear_build_info();
    }
}



glm::mat4 _stemBuilder::build(buildSetting _settings, bool _addVerts)
{
    _settings.callDepth++;

    uint startVerts = _ribbonBuilder.numVerts();
    uint leafVerts = _ribbonBuilder.numVerts();
    uint tipVerts = _ribbonBuilder.numVerts();

    // build the root spine and tip for extents to work from
    {
        build_NODES(_settings, false);
        tip_NODE = NODES.back();    // just in case build tip adds nothing
        build_tip(_settings, false);
    }

    if (hasPivot && _addVerts && !_settings.doNotAddPivot)
    {
        float3 extent = (float3)tip_NODE[3] - (float3)NODES.front()[3]; // do this above and save in stem
        float ext_L = glm::length(extent);
        extent = glm::normalize(extent) / ext_L;

        _plant_anim_pivot p;
        p.root = (float3)NODES.front()[3];
        p.extent = extent;
        p.frequency = rootFrequency() * sqrt(ext_L);
        p.stiffness = ossilation_stiffness;
        p.shift = ossilation_power;           // DEPRECATED
        p.offset = DD_0_255(_rootPlant::generator);

        _settings.pivotIndex[_settings.pivotDepth] = _ribbonBuilder.pushPivot(_settings.seed, p);
        _settings.pivotDepth += 1;
        numPivots++;
    }

    if (_addVerts)
    {
        bool found = false;
        levelOfDetail lod = lodInfo.front();             // no parent can push us past this, similar when I do sun Parts, there will be a lod set for that add
        for (int i = 0; i < lodInfo.size() - 1; i++)
        {
            // FIXME we want random pixelSize here for smoother crossovers but
            // But this is VERY bad, it ashould take overall lengrth into account
            GLOBAL_RND_SIZE = 0;
            if (_settings.callDepth > 1) GLOBAL_RND_SIZE = 0.2f;
            if (_settings.pixelSize * RND_B(float2(1.f, GLOBAL_RND_SIZE)) <= lodInfo[i].pixelSize)
            {
                lod = lodInfo[i];
                found = true;
            }
        }

        if (found)
        {

            if (!_settings.isBaking)    // so do not use any of these during a bake
            {
                if (lod.bakeType == BAKE_DIAMOND)       build_2(_settings, lod.bakeIndex, true);
                if (lod.bakeType == BAKE_4)             build_4(_settings, lod.bakeIndex, true);
                if (lod.bakeType == BAKE_N)             build_n(_settings, lod.bakeIndex, true);    // FIXME
            }

            if (lod.useGeometry || _settings.isBaking)
            {
                build_tip(_settings, true);
                tipVerts = _ribbonBuilder.numVerts();

                if (lod.bakeType == BAKE_NONE || _settings.isBaking)      // Only build nodes if we dont use the core bake at all, or we are Baking
                {
                    build_NODES(_settings, true);
                }

                leafVerts = _ribbonBuilder.numVerts();
                build_leaves(_settings, 100000, true);
            }

            if ((_ribbonBuilder.numVerts() - startVerts) > 0) numInstancePacked++;
            numVertsPacked += leafVerts - tipVerts;
        }
    }

    _settings.callDepth--;
    return tip_NODE;
}


glm::mat4 _stemBuilder::getTip(bool includeChildren)
{
    return NODES.back();    // since its only direction test with this
    //if (includeChildren)    return tip_NODE;
    //else                    return NODES.back();
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
    lod_bakeInfo[0].material.name = "bake_0";
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
    std::ifstream is(terrafectorEditorMaterial::rootFolder + path);
    cereal::JSONInputArchive archive(is);
    archive(*this);
    changed = false;
}

void _clumpBuilder::savePath()
{
    std::ofstream os(terrafectorEditorMaterial::rootFolder + path);
    cereal::JSONOutputArchive archive(os);
    archive(*this);
    changed = false;
}

void _clumpBuilder::load()
{
    if (ImGui::Button("Load##_clumpBuilder", ImVec2(80, 0)))
    {
        std::filesystem::path filepath;
        if (openFileDialog(filters, filepath))
        {
            path = materialCache::getRelative(filepath.string());
            name = filepath.filename().string();
            loadPath();
        }
    }
}


void _clumpBuilder::save()
{
    if (changedForSave) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.1f, 0.0f, 0.8f));
        if (ImGui::Button("Save##_clumpBuilder", ImVec2(80, 0)))
        {
            savePath();
            changedForSave = false;
        }
        ImGui::PopStyleColor();
    }
}


void _clumpBuilder::saveas()
{
    if (ImGui::Button("Save as##_clumpBuilder", ImVec2(80, 0)))
    {
        std::filesystem::path filepath;
        if (saveFileDialog(filters, filepath))
        {
            path = materialCache::getRelative(filepath.string());
            name = filepath.filename().string();
            savePath();
        }
    }
}


void _clumpBuilder::renderGui()
{
    unsigned int gui_id = 1001;

    ImGui::PushFont(_gui->getFont("header1"));
    {
        ImGui::Text(name.c_str());
    }
    ImGui::PopFont();
    TOOLTIP(path.c_str());

    ImGui::PushFont(_gui->getFont("header2"));
    {
        load();
        ImGui::SameLine(90, 0);
        save();
        ImGui::SameLine(180, 0);
        saveas();
    }
    ImGui::PopFont();


    ImGui::NewLine();
    ImGui::PushFont(_gui->getFont("bold"));
    {
        ImGui::Text("light");
    }
    ImGui::PopFont();

    ImGui::PushFont(_gui->getFont("italic"));
    {
        R_FLOAT("uv scale", shadowUVScale, 0.001f, 0.001f, 10.f, "");
        R_FLOAT("softness", shadowSoftness, 0.01f, 0.01f, 0.3f, "");
        R_FLOAT("depth", shadowDepth, 0.01f, 0.01f, 10.f, "");
        R_FLOAT("hgt", shadowPenetationHeight, 0.01f, 0.05f, 0.95f, "");
    }
    ImGui::PopFont();

    ImGui::NewLine();
    ImGui::PushFont(_gui->getFont("bold"));
    {
        ImGui::Text("sway");
    }
    ImGui::PopFont();

    ImGui::SameLine(0, 20);
    ImGui::NewLine();
    
    R_FLOAT("stiffness", ossilation_stiffness, 0.1f, 0.8f, 20.f, "");
    R_FLOAT("sqrt(sway)", ossilation_constant_sqrt, 0.1f, 1.01f, 100.f, "");
    
    ImGui::NewLine();
    ImGui::PushFont(_gui->getFont("bold"));
    {
        ImGui::Text("root");
    }
    ImGui::PopFont();
    {
        ImGui::SameLine(0, 10);
        float a = sqrt(aspect.x);
        ImGui::Text("%2.3f, %2.3fm", size.x * a, size.x / a);

        R_LENGTH_EX("size", size, 0.001f, 0.0001f, 1.f, "circle radius in meters", "random");
        R_LENGTH_EX("aspect", aspect, 0.01f, 0.01f, 10.f, "xz aspect ratio", "random");
    }

    // leaves
    ImGui::NewLine();
    ImGui::PushFont(_gui->getFont("bold"));
    {
        ImGui::Text("children");
    }
    ImGui::PopFont();
    {
        ImGui::SameLine(0, 10);
        ImGui::Text("%d added", ROOTS.size());

        if (ImGui::Checkbox("radial", &radial)) changed = true;
        TOOLTIP("radial will rotate elements to the sides, and set age from the inside out\noff is linear, will set age and angle in the X direction");
        R_LENGTH("numChildren", numChildren, "stem length in mm", "random");
        R_CURVE("age", child_age, "age of the children", "random");
        R_CURVE("child_angle", child_angle, "angle of the sub element relative to middle of clump", "change of angle as it ages, usually drooping");
        R_CURVE("random", child_rnd, "randomness in the angles", "random");
        R_FLOAT("age_power", child_age_power, 0.1f, 1.f, 25.f, "");
        ImGui::GetStyle().Colors[ImGuiCol_Header] = leaf_color;
        children.renderGui("children", gui_id);
    }
}


void _clumpBuilder::treeView()
{
    auto& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Header] = stem_color;
    int flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_OpenOnArrow;

    if (_rootPlant::selectedPart == this) {
        style.FrameBorderSize = 3;
        flags = flags | ImGuiTreeNodeFlags_Selected;
        style.Colors[ImGuiCol_Border] = ImVec4(0.7f, 0.7f, 0.7f, 1);
    }

    if (changedForSave) style.Colors[ImGuiCol_Header] = selected_color;

    TREE_LINE(name.c_str(), path.c_str(), flags)
    {
        if (ImGui::IsItemHovered()) {
            if (_ribbonBuilder.tooManyPivots)     ImGui::SetTooltip("ERROR - more than 255 pivots");
            else ImGui::SetTooltip("%d instances \n%d verts \n%d / %d pivots ", numInstancePacked, numVertsPacked, numPivots, _ribbonBuilder.numPivots());
        }

        style.FrameBorderSize = 0;
        CLICK_PART;
        ImGui::Text("children");
        for (auto& L : children.data) { if (L.plantPtr) L.plantPtr->treeView(); }
        ImGui::TreePop();
    }

    changedForSave |= changed;
    anyChange |= changed;
    changed = false;
}


lodBake* _clumpBuilder::getBakeInfo(uint i)
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
    numPivots = 0;

    for (auto& L : children.data)
    {
        if (L.plantPtr)  L.plantPtr->clear_build_info();
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
    lod_bakeInfo[0].material.name = "bake_0";
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

glm::mat4 _clumpBuilder::buildChildren(buildSetting _settings, bool _addVerts)
{
    bool forcePHOTOTROPY = _settings.isBaking;
    glm::mat4 ORIGINAL = _settings.root;

    int oldSeed = _settings.seed;
    std::mt19937 MT(_settings.seed + 999);
    _rootPlant::generator.seed(_settings.seed + 333);

    _ribbonBuilder.startRibbon(true, _settings.pivotIndex);

    glm::mat4 nodeCENTERLINE = ORIGINAL;
    if (!radial)
    {
        ROLL(nodeCENTERLINE, 3.14f);
        if (!forcePHOTOTROPY)              // for clumps, gettign rid of this part makes tehm upright
        {
            PITCH(nodeCENTERLINE, child_angle.x);  // add rnd
        }
    }
    float3 CENTER_DIR = glm::normalize((float3)nodeCENTERLINE[1]);
    float tipDistance = 0;

    float centerTest = 1.5;

    ROOTS.clear();
    int numC = (int)RND_B(numChildren);
    float s = RND_B(size);
    float a = sqrt(RND_B(aspect));
    float w = s * a;
    float h = s / a;

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
        } while (l >= 1.f);

        glm::mat4 node = ORIGINAL;

        node[3] += node[0] * rndPos.x * w;
        node[3] += node[2] * rndPos.y * h;

        //float c_angle = d_1_1(MT) * child_angle.x;
        //float c_angle_age = d_1_1(MT) * child_angle.y;
        float c_rnd = d_1_1(MT) * child_rnd.x;
        float c_rndp = d_1_1(MT) * child_rnd.x;
        float c_age = RND_B(child_age);
        float c_rot = atan2(rndPos.x, rndPos.y);
        float t = l;     // center to edge
        float t_live = 1.f - pow(t, child_age_power);

        if (radial)
        {
            ROLL(node, c_rot + 3.14f);
            PITCH(node, child_angle.x + (child_angle.y * l));  // add rnd
            //PITCH(node, c_angle * t_live + c_rndp);  // add rnd
            YAW(node, c_rnd);
        }
        else
        {
            t = rndPos.x * 0.5f + 0.5f;     // left to right  but needs controls how much it changes
            t_live = 1.f - pow(t, child_age_power);

            ROLL(node, 3.14f);

            if (!forcePHOTOTROPY)              // for clumps, gettign rid of this part makes tehm upright
            {
                PITCH(node, child_angle.x);  // add rnd
            }
            YAW(node, RND_B(child_rnd));
        }



        ROOTS.push_back(node);

        _settings.isBaking = false;      // MAKE A COOPY this is just messign em up
        _settings.seed = oldSeed + i;
        _settings.root = node;
        _settings.node_age = c_age;
        _settings.normalized_age = t_live;
        _plantRND CHILD = children.get();
        glm::mat4 TIP;
        if (CHILD.plantPtr) TIP = CHILD.plantPtr->build(_settings, _addVerts);

        tipDistance = __max(tipDistance, glm::dot((float3)TIP[3] - (float3)ORIGINAL[3], CENTER_DIR));
    }


    START = nodeCENTERLINE;
    TIP_CENTER = nodeCENTERLINE;

    GROW(TIP_CENTER, tipDistance);

    return TIP_CENTER;
}

glm::mat4 _clumpBuilder::build(buildSetting _settings, bool _addVerts)
{
    uint startVerts = _ribbonBuilder.numVerts();
    buildChildren(_settings, false);

    if (_addVerts)
    {
        float3 extent = (float3)TIP_CENTER[3] - (float3)START[3];
        float ext_L = glm::length(extent);
        extent = glm::normalize(extent) / ext_L;

        _plant_anim_pivot p;
        p.root = (float3)START[3];
        p.extent = extent;
        p.frequency = rootFrequency() * sqrt(ext_L);
        p.stiffness = ossilation_stiffness;
        p.shift = ossilation_power;           // DEPRECATED
        p.offset = DD_0_255(_rootPlant::generator);

        // clumps do not indent whne addign a pivot, all children are at the same level, saves us a silly level
        _settings.pivotIndex[_settings.pivotDepth] = _ribbonBuilder.pushPivot(_settings.seed, p);
        numPivots++;        
    }


    if (_addVerts)
    {
        levelOfDetail lod = lodInfo.back();             // no parent can push us past this, similar when I do sun Parts, there will be a lod set for that add
        for (int i = lodInfo.size() - 1; i >= 0; i--)
        {
            if (_settings.pixelSize >= lodInfo[i].pixelSize)
            {
                lod = lodInfo[i];
            }
        }

        if (lod.bakeType == BAKE_DIAMOND)       build_2(_settings, lod.bakeIndex, true);
        if (lod.bakeType == BAKE_4)             build_4(_settings, lod.bakeIndex, true);
        //if (lod.bakeType == BAKE_N)             build_n(_settings, lod.bakeIndex, true);    // FIXME

        if (lod.useGeometry || _settings.isBaking)
        {
            buildChildren(_settings, true);
        }

        uint numVerts = _ribbonBuilder.numVerts() - startVerts;
        if (numVerts > 0) numInstancePacked++;
        numVertsPacked += numVerts;
    }

    return TIP_CENTER;
}


glm::mat4  _clumpBuilder::build_2(buildSetting _settings, uint _bakeIndex, bool _faceCamera)
{
    lodBake& lB = lod_bakeInfo[_bakeIndex];
    if (lB.pixHeight > 0)
    {
        float w = lB.extents.x * lB.bakeWidth;
        uint mat = lB.material.index;

        glm::mat4 node = START;
        glm::mat4 last = TIP_CENTER;

        // adjust start end
        float totalLenght = glm::length(last[3] - node[3]);
        GROW(node, totalLenght * lB.bake_V.x);
        GROW(last, -totalLenght * (1 - lB.bake_V.y));

        _ribbonBuilder.startRibbon(_faceCamera, _settings.pivotIndex);
        _ribbonBuilder.set(node, w * 0.8f, mat, float2(0.8f, 0.f), 1.f, 1.f, true, 0.5f, 0.1f, 0.0f, true);  // only 60% width, save pixels but test
        _ribbonBuilder.set(last, w * 0.8f, mat, float2(0.8f, 1.f), 1.f, 1.f, true, 0.5f, 0.1f, 0.0f, true);
    }
    return TIP_CENTER;
}


glm::mat4  _clumpBuilder::build_4(buildSetting _settings, uint _bakeIndex, bool _faceCamera)
{
    lodBake& lB = lod_bakeInfo[_bakeIndex];
    if (lB.pixHeight > 0)
    {
        float w = lB.extents.x * lB.bakeWidth;
        uint mat = lB.material.index;

        glm::mat4 node = START;
        glm::mat4 last = TIP_CENTER;

        // adjust start end
        float totalLenght = glm::length(last[3] - node[3]);
        GROW(node, totalLenght * lB.bake_V.x);
        GROW(last, -totalLenght * (1 - lB.bake_V.y));

        glm::vec4 step = (last[3] - node[3]);
        float4 binorm_step = (last[1] - node[1]);

        node[3] += step * lB.bake_V.x;
        node[1] += binorm_step * lB.bake_V.x;
        step *= (lB.bake_V.y - lB.bake_V.x);
        binorm_step *= (lB.bake_V.y - lB.bake_V.x);
        step /= 3.f;
        binorm_step /= 3.f;

        _ribbonBuilder.startRibbon(_faceCamera, _settings.pivotIndex);

        for (int i = 0; i < 4; i++)
        {
            _ribbonBuilder.set(node, w * lod_bakeInfo[_bakeIndex].dU[i] * 0.99f, mat, float2(lod_bakeInfo[_bakeIndex].dU[i] * 0.99f, 1.f - (0.3333333f * i)), 1.f, 1.f);
            node[3] += step;
            node[1] += binorm_step;
        }
    }
    return TIP_CENTER;
}






bool _rootPlant::onKeyEvent(const KeyboardEvent& keyEvent)
{
    bool keyPressed = (keyEvent.type == KeyboardEvent::Type::KeyPressed);

    if (root && keyPressed)
    {
        if (keyEvent.key == Input::Key::Space)
        {
            anyChange = true;       // SPACE
        }
        if (keyEvent.key == Input::Key::Enter)
        {
            root->loadPath();
            anyChange = true;       // ENTER
        }


        if (keyEvent.key == Input::Key::Up)
        {
            currentLOD++;       // ENTER
            //??? clamh how
            anyChange = true;
            levelOfDetail* lodInfo = selectedPart->getLodInfo(currentLOD);
            if (lodInfo)
            {
                //lodInfo->pixelSize = extents.y / lodInfo->numPixels;
                settings.pixelSize = lodInfo->pixelSize;
            }
            else
            {
                currentLOD--;
            }
        }
        if (keyEvent.key == Input::Key::Down)
        {
            currentLOD--;       // ENTER
            if (currentLOD < 0) currentLOD = 0;
            anyChange = true;
            levelOfDetail* lodInfo = selectedPart->getLodInfo(currentLOD);
            //lodInfo->pixelSize = extents.y / lodInfo->numPixels;
            settings.pixelSize = lodInfo->pixelSize;
        }
        if (keyEvent.key == Input::Key::X)
        {
            showDebugInShader = !showDebugInShader;
        }

        return true;
    }
    return false;
}


void _rootPlant::onLoad()
{
    plantData = Buffer::createStructured(sizeof(plant), MAX_PLANT_PLANTS);
    plantpivotData = Buffer::createStructured(sizeof(_plant_anim_pivot), MAX_PLANT_PIVOTS);
    instanceData = Buffer::createStructured(sizeof(plant_instance), MAX_PLANT_INSTANCES);
    instanceData_Billboards = Buffer::createStructured(sizeof(plant_instance), MAX_PLANT_BILLBOARDS, Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess | Resource::BindFlags::IndirectArg);
    blockData = Buffer::createStructured(sizeof(block_data), MAX_PLANT_BLOCKS, Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess | Resource::BindFlags::IndirectArg);        // big enough to house inatnces * blocks per instance   8 Mb for now
    vertexData = Buffer::createStructured(sizeof(ribbonVertex8), MAX_PLANT_VERTS);

    drawArgs_vegetation = Buffer::createStructured(sizeof(t_DrawArguments), 1, Resource::BindFlags::UnorderedAccess | Resource::BindFlags::IndirectArg);
    drawArgs_billboards = Buffer::createStructured(sizeof(t_DrawArguments), 1, Resource::BindFlags::UnorderedAccess | Resource::BindFlags::IndirectArg);

    buffer_feedback = Buffer::createStructured(sizeof(vegetation_feedback), 1);
    buffer_feedback_read = Buffer::createStructured(sizeof(vegetation_feedback), 1, Resource::BindFlags::None, Buffer::CpuAccess::Read);
    // compute
    //split.compute_tileClear.Vars()->setBuffer("DrawArgs_ClippedLoddedPlants", split.drawArgs_clippedloddedplants);


    compute_clearBuffers.load("hlsl/terrain/compute_vegetation_clear.hlsl");
    compute_clearBuffers.Vars()->setBuffer("DrawArgs_Quads", drawArgs_billboards);
    compute_clearBuffers.Vars()->setBuffer("DrawArgs_Plants", drawArgs_vegetation);
    compute_clearBuffers.Vars()->setBuffer("feedback", buffer_feedback);

    compute_calulate_lod.load("hlsl/terrain/compute_vegetation_lod.hlsl");
    compute_calulate_lod.Vars()->setBuffer("DrawArgs_Quads", drawArgs_billboards);
    compute_calulate_lod.Vars()->setBuffer("DrawArgs_Plants", drawArgs_vegetation);
    compute_calulate_lod.Vars()->setBuffer("plant_buffer", plantData);
    compute_calulate_lod.Vars()->setBuffer("instance_buffer", instanceData);
    compute_calulate_lod.Vars()->setBuffer("instance_buffer_billboard", instanceData_Billboards);
    compute_calulate_lod.Vars()->setBuffer("block_buffer", blockData);
    compute_calulate_lod.Vars()->setBuffer("feedback", buffer_feedback);

    std::cout << "Buffers created, building instance buffer" << std::endl;

    builInstanceBuffer();

    Sampler::Desc samplerDesc;
    samplerDesc.setAddressingMode(Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp).setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear).setMaxAnisotropy(4);
    sampler_ClampAnisotropic = Sampler::create(samplerDesc);

    samplerDesc.setAddressingMode(Sampler::AddressMode::Clamp, Sampler::AddressMode::Wrap, Sampler::AddressMode::Wrap).setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear).setMaxAnisotropy(1);
    sampler_Ribbons = Sampler::create(samplerDesc);


    vegetationShader.load("hlsl/terrain/render_vegetation_ribbons.hlsl", "vsMain", "psMain", Vao::Topology::LineStrip, "gsMain");
    auto& starts = vegetationShader.Program()->getGlobalCompilationStats();
    vegetationShader.Vars()->setBuffer("plant_buffer", plantData);
    vegetationShader.Vars()->setBuffer("plant_pivot_buffer", plantpivotData);
    vegetationShader.Vars()->setBuffer("instance_buffer", instanceData);
    vegetationShader.Vars()->setBuffer("block_buffer", blockData);
    vegetationShader.Vars()->setBuffer("vertex_buffer", vertexData);
    vegetationShader.Vars()->setBuffer("materials", _plantMaterial::static_materials_veg.sb_vegetation_Materials);
    vegetationShader.Vars()->setSampler("gSampler", sampler_Ribbons);              // fixme only cvlamlX
    vegetationShader.Vars()->setSampler("gSamplerClamp", sampler_ClampAnisotropic);              // fixme only cvlamlX
    auto& block = vegetationShader.Vars()->getParameterBlock("textures");
    varVegTextures = block->findMember("T");


    vegetationShader_GOURAUD.add("_GOURAUD_SHADING", "");
    vegetationShader_GOURAUD.load("hlsl/terrain/render_vegetation_ribbons.hlsl", "vsMain", "psMain", Vao::Topology::LineStrip, "gsMain");
    vegetationShader_GOURAUD.Vars()->setBuffer("plant_buffer", plantData);
    vegetationShader_GOURAUD.Vars()->setBuffer("plant_pivot_buffer", plantpivotData);
    vegetationShader_GOURAUD.Vars()->setBuffer("instance_buffer", instanceData);
    vegetationShader_GOURAUD.Vars()->setBuffer("block_buffer", blockData);
    vegetationShader_GOURAUD.Vars()->setBuffer("vertex_buffer", vertexData);
    vegetationShader_GOURAUD.Vars()->setBuffer("materials", _plantMaterial::static_materials_veg.sb_vegetation_Materials);
    vegetationShader_GOURAUD.Vars()->setSampler("gSampler", sampler_Ribbons);              // fixme only cvlamlX
    vegetationShader_GOURAUD.Vars()->setSampler("gSamplerClamp", sampler_ClampAnisotropic);              // fixme only cvlamlX
    auto& blockGouraud = vegetationShader_GOURAUD.Vars()->getParameterBlock("textures");
    varTextures_Gauraud = blockGouraud->findMember("T");

    vegetationShader_DEBUG_PIVOTS.add("_DEBUG_PIVOTS", "");
    vegetationShader_DEBUG_PIVOTS.load("hlsl/terrain/render_vegetation_ribbons.hlsl", "vsMain", "psMain", Vao::Topology::LineStrip, "gsMain");
    vegetationShader_DEBUG_PIVOTS.Vars()->setBuffer("plant_buffer", plantData);
    vegetationShader_DEBUG_PIVOTS.Vars()->setBuffer("plant_pivot_buffer", plantpivotData);
    vegetationShader_DEBUG_PIVOTS.Vars()->setBuffer("instance_buffer", instanceData);
    vegetationShader_DEBUG_PIVOTS.Vars()->setBuffer("block_buffer", blockData);
    vegetationShader_DEBUG_PIVOTS.Vars()->setBuffer("vertex_buffer", vertexData);
    vegetationShader_DEBUG_PIVOTS.Vars()->setBuffer("materials", _plantMaterial::static_materials_veg.sb_vegetation_Materials);
    vegetationShader_DEBUG_PIVOTS.Vars()->setSampler("gSampler", sampler_Ribbons);              // fixme only cvlamlX
    vegetationShader_DEBUG_PIVOTS.Vars()->setSampler("gSamplerClamp", sampler_ClampAnisotropic);              // fixme only cvlamlX
    auto& blockPivots = vegetationShader_DEBUG_PIVOTS.Vars()->getParameterBlock("textures");
    varTextures_Debug_Pivots = blockPivots->findMember("T");

    vegetationShader_DEBUG_PIXELS.add("_DEBUG_PIXELS", "");
    vegetationShader_DEBUG_PIXELS.load("hlsl/terrain/render_vegetation_ribbons.hlsl", "vsMain", "psMain", Vao::Topology::LineStrip, "gsMain");
    vegetationShader_DEBUG_PIXELS.Vars()->setBuffer("plant_buffer", plantData);
    vegetationShader_DEBUG_PIXELS.Vars()->setBuffer("plant_pivot_buffer", plantpivotData);
    vegetationShader_DEBUG_PIXELS.Vars()->setBuffer("instance_buffer", instanceData);
    vegetationShader_DEBUG_PIXELS.Vars()->setBuffer("block_buffer", blockData);
    vegetationShader_DEBUG_PIXELS.Vars()->setBuffer("vertex_buffer", vertexData);
    vegetationShader_DEBUG_PIXELS.Vars()->setBuffer("materials", _plantMaterial::static_materials_veg.sb_vegetation_Materials);
    vegetationShader_DEBUG_PIXELS.Vars()->setSampler("gSampler", sampler_Ribbons);              // fixme only cvlamlX
    vegetationShader_DEBUG_PIXELS.Vars()->setSampler("gSamplerClamp", sampler_ClampAnisotropic);              // fixme only cvlamlX
    auto& blockPixels = vegetationShader_DEBUG_PIXELS.Vars()->getParameterBlock("textures");
    varTextures_Debug_Pixels = blockPixels->findMember("T");


    billboardShader.add("_BILLBOARD", "");
    billboardShader.load("hlsl/terrain/render_vegetation_ribbons.hlsl", "vsMain", "psMain", Vao::Topology::PointList, "gsMain");
    billboardShader.Vars()->setBuffer("plant_buffer", plantData);
    billboardShader.Vars()->setBuffer("instance_buffer", instanceData_Billboards);
    billboardShader.Vars()->setBuffer("block_buffer", blockData);
    billboardShader.Vars()->setBuffer("vertex_buffer", vertexData);
    billboardShader.Vars()->setBuffer("materials", _plantMaterial::static_materials_veg.sb_vegetation_Materials);
    billboardShader.Vars()->setSampler("gSampler", sampler_Ribbons);              // fixme only cvlamlX
    billboardShader.Vars()->setSampler("gSamplerClamp", sampler_ClampAnisotropic);              // fixme only cvlamlX
    auto& blockBB = billboardShader.Vars()->getParameterBlock("textures");
    varBBTextures = blockBB->findMember("T");

    bakeShader.add("_BAKE", "");
    bakeShader.load("hlsl/terrain/render_vegetation_ribbons.hlsl", "vsMain", "psMain", Vao::Topology::LineStrip, "gsMain");
    bakeShader.Vars()->setBuffer("plant_buffer", plantData);
    bakeShader.Vars()->setBuffer("instance_buffer", instanceData);
    bakeShader.Vars()->setBuffer("block_buffer", blockData);
    bakeShader.Vars()->setBuffer("vertex_buffer", vertexData);
    bakeShader.Vars()->setBuffer("materials", _plantMaterial::static_materials_veg.sb_vegetation_Materials);
    bakeShader.Vars()->setSampler("gSampler", sampler_Ribbons);              // fixme only cvlamlX
    auto& blockB = bakeShader.Vars()->getParameterBlock("textures");
    varBakeTextures = blockB->findMember("T");


    RasterizerState::Desc rsDesc;
    rsDesc.setFillMode(RasterizerState::FillMode::Solid).setCullMode(RasterizerState::CullMode::None);
    rasterstate = RasterizerState::create(rsDesc);

    BlendState::Desc blendDesc;
    blendDesc.setRtBlend(0, true);
    blendDesc.setRtParams(0, BlendState::BlendOp::Add, BlendState::BlendOp::Add, BlendState::BlendFunc::SrcAlpha, BlendState::BlendFunc::OneMinusSrcAlpha, BlendState::BlendFunc::Zero, BlendState::BlendFunc::Zero);
    blendstate = BlendState::create(blendDesc);

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

    compute_bakeFloodfill.load("hlsl/terrain/compute_bakeFloodfill.hlsl");

    billboardShader.State()->setRasterizerState(rasterstate);
    billboardShader.State()->setBlendState(blendstate);

    // Perlin lookup buffer
    const siv::PerlinNoise perlin{ (uint)101 };
    float sum = 0;
    for (int i = 0; i < 1024; i++)
    {
        perlinData[i] = (float)perlin.normalizedOctave1D((float)i / 8.f, 4, 0.5);
        fprintf(terrafectorSystem::_logfile, "%f, ", perlinData[i]);
        sum += perlinData[i];
    }
    fprintf(terrafectorSystem::_logfile, "\n sum %f, \n\n", sum);

    sum /= 1024.f;
    for (int i = 0; i < 1024; i++)
    {
        perlinData[i] -= sum;
    }
}



void _rootPlant::renderGui_perf(Gui* _gui)
{
    uint gui_id = 14;
    bool changed;

    ImGui::PushFont(_gui->getFont("header2"));
    {
        //            timeAvs += 0.01f * gpFramework->getFrameRate().getAverageFrameTime();
        ImGui::Text("gpu %1.3fms + %1.3fms", gputime, gputimeBB);

        float numTri = (float)feedback.numBlocks * VEG_BLOCK_SIZE * 2.f / 1000000.f;
        if (displayModeSinglePlant)
        {
            numTri = (float)totalBlocksToRender * VEG_BLOCK_SIZE * 2.f / 1000000.f;
        }

        ImGui::Text("%2.2f M tri ", numTri);

        if (gputime > 0)
        {
            ImGui::SameLine(0, 10);
            ImGui::Text("%2.2f tri / ms", numTri / gputime);
        }
    }
    ImGui::PopFont();

    if (displayModeSinglePlant)
    {

        //ImGui::Text("%2.2f M tris", (float)totalBlocksToRender * VEG_BLOCK_SIZE * 2.f / 1000000.f);
    }
    else
    {
        for (int i = 0; i < 5; i++)
        {
            ImGui::Text("%2d:  %d plants", i, feedback.numPlantsType[i]);
        }

        R_FLOAT("lod Bias", loddingBias, 0.01f, 0.1f, 10.f, "");
        ImGui::Text("plantZero, %2.2fpix - lod %d", feedback.plantZero_pixeSize, feedback.plantZeroLod);

        {
            ImGui::Text("billboard %6d", feedback.numLod[0]);
            for (int i = 1; i < 10; i++)
            {
                if (root)
                {
                    if (root->getLodInfo(i))
                    {
                        ImGui::Text("%2d:", i);
                        ImGui::SameLine(30);
                        ImGui::Text("%6d", feedback.numLod[i]);
                        ImGui::SameLine(130);
                        ImGui::Text("%3d", root->getLodInfo(i)->numBlocks);
                        ImGui::SameLine(200);
                        ImGui::Text("%3d", root->getLodInfo(i)->numVerts);
                        ImGui::SameLine(300);
                        ImGui::Text("%2.2f", feedback.numLod[i] * root->getLodInfo(i)->numVerts * 2 / 1000000.f);
                    }
                }
                else
                {
                    ImGui::Text("%2d:", i);
                    ImGui::SameLine(30);
                    ImGui::Text("%6d", feedback.numLod[i]);
                }
            }
        }

        /*
        ImGui::PushFont(_gui->getFont("small"));
        {

            //ImGui::PushFont(_gui->getFont("small"));
            {
                ImGui::NewLine();
                for (int i = 1; i < 10; i++)
                {
                    ImGui::SameLine((float)(i * 40), 0);
                    ImGui::Text("%d, ", feedback.numLod[i]);
                }

                ImGui::NewLine();
                for (int i = 1; i < 10; i++)
                {
                    if (root->getLodInfo(i))
                    {
                        ImGui::SameLine((float)(i * 40), 0);
                        ImGui::Text("%d, ", root->getLodInfo(i)->numBlocks);
                    }
                }

                ImGui::NewLine();
                for (int i = 1; i < 10; i++)
                {
                    if (root->getLodInfo(i))
                    {
                        ImGui::SameLine((float)(i * 40), 0);
                        ImGui::Text("%d, ", root->getLodInfo(i)->numVerts);
                    }
                }


                ImGui::NewLine();
                for (int i = 1; i < 10; i++)
                {
                    if (root->getLodInfo(i))
                    {
                        ImGui::SameLine((float)(i * 40), 0);
                        ImGui::Text("%1.2f, ", feedback.numLod[i] * root->getLodInfo(i)->numVerts * 2 / 1000000.f);
                    }
                }
                ImGui::SameLine(0, 5);
                ImGui::Text(" M tris");
            }
        }
        ImGui::PopFont();
        */


    }
    auto& style = ImGui::GetStyle();
    style.FrameBorderSize = 1;
    ImGui::Checkbox("show debug colours", &showDebugInShader);
    ImGui::Checkbox("show pivots", &showNumPivots);
    style.FrameBorderSize = 0;

    ImGui::DragInt2("plants", &firstPlant, 0.1f, 0, 1000);
    ImGui::DragInt("lods", &firstLod, 0.1f, -1, 1000);
}

void _rootPlant::renderGui_lodbake(Gui* _gui)
{
}

void _rootPlant::renderGui_other(Gui* _gui)
{
}

void _rootPlant::buildFullResolution()
{
    settings.pixelSize = 0.00005f;   // half a mm
    build(true);   // to generate new extents
    displayModeSinglePlant = true;
    anyChange = true;
    currentLOD = -1;

    for (uint lod = 0; lod < 100; lod++)
    {
        levelOfDetail* lodInfo = selectedPart->getLodInfo(lod);
        //if (lodInfo)    lodInfo->pixelSize = extents.y / lodInfo->numPixels;
    }
}




void _rootPlant::renderGui_rightPanel(Gui* _gui)
{
    renderGui_load(_gui);

    if (root)
    {
        pixelmm = extents.y / pixelSize;
        for (uint lod = 0; lod < 100; lod++)   // backwards since it fixes abug in build()
        {
            levelOfDetail* lodInfo = root->getLodInfo(lod);
            if (lodInfo && (pixelmm < lodInfo->pixelSize))
            {
                expectedLod = lod;
            }
        }





        FONT_TEXT("header1", root->name.c_str());
        ImGui::Text("( %3.2f, %3.2f )m", extents.x, extents.y);
        ImGui::Text("%d pix,  %3.1f mm/pixel", (int)pixelSize, 1000.f * pixelmm);
        ImGui::Text("lod %d  {exp - %d}", currentLOD, expectedLod);

        ImGui::PushFont(_gui->getFont("header2"));
        {
            ImGui::Text("%d verts", totalBlocksToRender * VEG_BLOCK_SIZE);
        }
        ImGui::PopFont();

        ImGui::Text("build time %2.2f ms", buildTime);

        ImGui::NewLine();


    }
    ImGui::NewLine();
    renderGui_perf(_gui);

    ImGui::NewLine();
    ImGui::Text("recent ...");

}

void _rootPlant::renderGui_load(Gui* _gui)
{
    ImGui::PushFont(_gui->getFont("header2"));
    {
        if (ImGui::Button("Load")) {
            std::filesystem::path filepath;
            FileDialogFilterVec filters = { {"leaf"}, {"stem"}, {"clump"} };
            if (openFileDialog(filters, filepath))
            {
                _rootPlant::selectedMaterial = nullptr;
                if (root) delete root;
                if (filepath.string().find(".leaf") != std::string::npos) { root = new _leafBuilder;  _rootPlant::selectedPart = root; }
                if (filepath.string().find(".stem") != std::string::npos) { root = new _stemBuilder;  _rootPlant::selectedPart = root; }
                if (filepath.string().find(".clump") != std::string::npos) { root = new _clumpBuilder;  _rootPlant::selectedPart = root; }

                if (root)
                {
                    root->path = materialCache::getRelative(filepath.string());
                    root->name = filepath.filename().string();
                    root->loadPath();
                    fprintf(terrafectorSystem::_logfile, "expand compete about to build\n");
                    fflush(terrafectorSystem::_logfile);
                    build(true);    //to get extetns
                    buildFullResolution();
                    extents = root->calculate_extents(bakeViewAdjusted);
                    builInstanceBuffer();
                }
            }
        }
        ImGui::SameLine(0, 30);
        if (ImGui::Button("import"))
        {
            importBinary();
        }
        ImGui::SameLine(0, 30);
        if (ImGui::Button("clearBinary"))
        {
            numBinaryPlants = 0;
            binVertexOffset = 0;
            binPlantOffset = 0;
            binPivotOffset = 0;
        }
    }
    ImGui::PopFont();

    if (ImGui::Button("new Stem")) { if (root) delete root; root = new _stemBuilder;  _rootPlant::selectedPart = root; _rootPlant::selectedMaterial = nullptr; }
    ImGui::SameLine(0, 10);
    if (ImGui::Button("new Clump")) { if (root) delete root; root = new _clumpBuilder;  _rootPlant::selectedPart = root; _rootPlant::selectedMaterial = nullptr; }
    ImGui::SameLine(0, 10);
    if (ImGui::Button("new Leaf")) { if (root) delete root; root = new _leafBuilder;  _rootPlant::selectedPart = root; _rootPlant::selectedMaterial = nullptr; }


    ImGui::NewLine();
}

void _rootPlant::renderGui(Gui* _gui)
{
    uint gui_id = 99994;
    _plantBuilder::_gui = _gui;

    renderGui_rightPanel(_gui);

    Gui::Window builderPanel(_gui, "vegetation builder", { 900, 900 }, { 100, 100 });
    {
        ImGui::Columns(2);
        float columnWidth = ImGui::GetWindowWidth() / 2 - 10;
        auto& style = ImGui::GetStyle();
        style.Colors[ImGuiCol_Header] = ImVec4(0.01f, 0.01f, 0.01f, 1.f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.05f, 0.0f, 0.0f);
        int flags = ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_OpenOnArrow; //ImGuiTreeNodeFlags_DefaultOpen | 

        style.FrameBorderSize = 0;

        ImGui::PushFont(_gui->getFont("default"));
        {
            if (root)
            {
                root->treeView();
                style.FrameBorderSize = 0;
            }


            ImGui::NewLine();
            style.Colors[ImGuiCol_Header] = ImVec4(0.01f, 0.01f, 0.01f, 1.f);
            if (ImGui::TreeNodeEx("lodding", ImGuiTreeNodeFlags_DefaultOpen | flags))
            {
                if (selectedPart)
                {
                    if (ImGui::Button("Build full", ImVec2(columnWidth / 2 - 10, 0)))
                    {
                        buildFullResolution();
                    }

                    if (selectedPart == root)
                    {
                        ImGui::SameLine(0, 0);
                        if (ImGui::Button("calculate extents", ImVec2(columnWidth / 2 - 10, 0)))
                        {
                            settings.pixelSize = 0.00005f;   // half a mm
                            settings.isBaking = true;
                            build(true);   // to generate new extents
                            settings.isBaking = false;
                            displayModeSinglePlant = true;
                            anyChange = true;
                            selectedPart->changed = true;
                            extents = selectedPart->calculate_extents(bakeViewAdjusted);
                        }
                    }

                    //ImGui::SameLine(columnWidth / 2, 0);
                    if (ImGui::Button("Build.all lods", ImVec2(columnWidth / 2 - 10, 0)))
                    {
                        fprintf(terrafectorSystem::_logfile, "about to buildAllLods();\n");
                        fflush(terrafectorSystem::_logfile);
                        buildAllLods();
                    }

                    {
                        if (ImGui::Button("   -   ")) { selectedPart->decrementLods(); }
                        ImGui::SameLine(0, 50);
                        if (ImGui::Button("   +   ")) { selectedPart->incrementLods(); }
                    }

                    for (uint lod = 0; lod < 100; lod++)
                    {
                        levelOfDetail* lodInfo = selectedPart->getLodInfo(lod);
                        if (lodInfo)
                        {
                            if (currentLOD == lod)
                            {
                                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.16f, 0.05f, 0.0f, 0.9f));
                            }
                            else
                            {
                                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.5f));
                            }

                            ImGui::BeginChildFrame(5678 + lod, ImVec2(columnWidth - 40, 95));
                            {
                                ImGui::Text("%d)", lod);

                                ImGui::SameLine(0, 20);
                                ImGui::SetNextItemWidth(60);
                                if (ImGui::DragInt("##numPix", &(lodInfo->numPixels), 0.1f, 8, 2000))
                                {
                                    anyChange = true;
                                    //lodInfo->pixelSize = extents.y / lodInfo->numPixels;// *lodInfo->geometryPixelScale;;
                                    settings.pixelSize = lodInfo->pixelSize;
                                    selectedPart->changed = true;
                                }

                                ImGui::SameLine(0, 10);
                                ImGui::SetNextItemWidth(100);
                                //ImGui::Text("%5.1fmm", lodInfo->pixelSize * 1000.f);
                                if (ImGui::DragFloat("##pixelSize", &(lodInfo->pixelSize), 0.001f, 0.001f, 1.f, "%1.3fm", 5.f))
                                {
                                    anyChange = true;
                                    settings.pixelSize = lodInfo->pixelSize;
                                    selectedPart->changed = true;
                                }

                                ImGui::SameLine(columnWidth - 40 - 60, 0);
                                //if (ImGui::Button("build", ImVec2(60, 0)));
                                if (ImGui::Button("build", ImVec2(60, 0)))
                                {
                                    displayModeSinglePlant = true;
                                    anyChange = true;
                                    //lodInfo->pixelSize = extents.y / lodInfo->numPixels;// *lodInfo->geometryPixelScale;;
                                    settings.pixelSize = lodInfo->pixelSize;
                                    currentLOD = lod;
                                }


                                // middle line
                                ImGui::NewLine();
                                ImGui::SameLine(0, 10);
                                ImGui::SetNextItemWidth(60);
                                if (ImGui::Checkbox("geo", &lodInfo->useGeometry)) { selectedPart->changed = true; }
                                TOOLTIP("Use 3d geometry, or only baked billboards")

                                    ImGui::SameLine(0, 10);
                                ImGui::SetNextItemWidth(60);
                                if (ImGui::Combo("type", &lodInfo->bakeType, "none\0diamond\0'4'\0N\0")) { selectedPart->changed = true; }
                                TOOLTIP("Type of baked content to use \nnone \ndianmond - 2 triangles in diamond pattern\n4 - 6 triangle ribbon using 4 verts \nN - not currently used")

                                    ImGui::SameLine(0, 10);
                                ImGui::SetNextItemWidth(60);
                                if (ImGui::DragInt("##bakeIdx", &(lodInfo->bakeIndex), 0.1f, 0, 2)) { selectedPart->changed = true; }
                                TOOLTIP("Which bake to use here")

                                    ImGui::Text("%d: verts", lodInfo->numVerts);
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




                }
                ImGui::TreePop();
            }





            ImGui::NewLine();

            if (selectedPart)
            {
                if (ImGui::Button(" Bake materials "))
                {
                    fprintf(terrafectorSystem::_logfile, "\n New bake started - %s \n", root->name.c_str());

                    std::filesystem::path PT = selectedPart->path;
                    std::string resource = terrafectorEditorMaterial::rootFolder;

                    std::string newDir = resource + PT.parent_path().string() + "\\bake_" + PT.stem().string();
                    replaceAllVEG(newDir, "/", "\\");
                    newDir = "rmdir /S /Q " + newDir;
                    system(newDir.c_str());

                    newDir = "mkdir " + resource + PT.parent_path().string() + "\\bake_" + PT.stem().string();
                    replaceAllVEG(newDir, "/", "\\");
                    system(newDir.c_str());

                    for (int seed = 1000; seed < 1001; seed++)
                    {
                        for (int j = 0; j < 100; j++)
                        {
                            levelOfDetail* lod = root->getLodInfo(j);
                            if (lod)
                            {
                                settings.pixelSize = lod->pixelSize;
                                fprintf(terrafectorSystem::_logfile, "lod found - %d, %2.2fmm \n", j, lod->pixelSize * 1000.f);
                            }
                        }

                        fflush(terrafectorSystem::_logfile);
                        settings.seed = seed;
                        //settings.pixelSize = 0.01f;
                        settings.isBaking = true;    //??????
                        _ribbonBuilder.clear();     //??? Mayeb unnesesary, I think buiild will do thois
                        build(true);
                        settings.isBaking = false;
                        fprintf(terrafectorSystem::_logfile, "build done - %d verts \n", (int)_ribbonBuilder.numPacked());

                        glm::mat4 tip = selectedPart->getTip(true); // FIXME later bool for stem length only
                        bakeViewAdjusted = bakeViewMatrix;
                        float3 u = glm::normalize((float3)tip[3] - (float3)bakeViewMatrix[3]);

                        float3 d = glm::cross((float3)bakeViewAdjusted[0], u);
                        float3 r = glm::cross(u, d);

                        // now its tilted so tip is int eh middle
                        bakeViewAdjusted[0][0] = r.x;
                        bakeViewAdjusted[0][1] = r.y;
                        bakeViewAdjusted[0][2] = r.z;

                        bakeViewAdjusted[1][0] = u.x;
                        bakeViewAdjusted[1][1] = u.y;
                        bakeViewAdjusted[1][2] = u.z;

                        bakeViewAdjusted[2][0] = d.x;
                        bakeViewAdjusted[2][1] = d.y;
                        bakeViewAdjusted[2][2] = d.z;

                        fprintf(terrafectorSystem::_logfile, "\n\nBAKE\n");

                        
                        extents = selectedPart->calculate_extents(bakeViewAdjusted);

                        for (int i = 0; i < 10; i++)
                        {
                            if (selectedPart->getBakeInfo(i) && (selectedPart->getBakeInfo(i)->pixHeight > 0))
                            {
                                bake(selectedPart->path, std::to_string(settings.seed), selectedPart->getBakeInfo(i), bakeViewAdjusted);
                                selectedPart->getBakeInfo(i)->material.reload();
                                selectedPart->changed = true;
                            }
                        }
                    }
                    //selectedPart->save();
                }

                for (int i = 0; i < 10; i++)
                {
                    lodBake* bake = selectedPart->getBakeInfo(i);
                    if (bake)
                    {
                        float columnWidth = ImGui::GetWindowWidth() / 2 - 5;
                        style.Colors[ImGuiCol_Header] = ImVec4(0.01f, 0.01f, 0.01f, 1.f);
                        std::string bakeStr = "bake-" + std::to_string(i);
                        if (ImGui::TreeNodeEx(bakeStr.c_str(), ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_OpenOnArrow))
                        {
                            bool changed = false;


                            R_INT("height", bake->pixHeight, 0, 512, "");
                            R_FLOAT("width", bake->bakeWidth, 0.01f, 0.1f, 10.f, "");
                            R_CURVE("start / stop", bake->bake_V, "", "");
                            R_FLOAT("translucency", bake->translucency, 0.01f, 0.1f, 10.f, "");
                            R_FLOAT("alpha", bake->alphaPow, 0.01f, 0.1f, 10.f, "");
                            CHECKBOX("bake AO", &bake->bakeAOToAlbedo, "if used as a billboard, bake the ao to the texture itself for added depth\nif used as a code with vertex ao, leave out");
                            CHECKBOX("forceDiamond", &bake->forceDiamond, "");
                            CHECKBOX("faceCamera", &bake->faceCamera, "");

                            if (changed) selectedPart->changed = true;
                            //anyChange |= changed;

                            ImGui::TreePop();
                        }
                    }
                }
            }








            ImGui::NewLine();

            if (ImGui::DragFloat3("wind dir", &windDir.x, 0.01f, -1.f, 1.f))
            {
                windDir.y = 0;
                windDir = glm::normalize(windDir);
            }
            ImGui::DragFloat("strength", &windStrength, 0.1f, 0.f, 100.f, "%3.2fm/s");
            ImGui::SameLine(0, 20);
            ImGui::Text("%3.2f km/h", windStrength * 3.6f);




            ImGui::NewLine();
            ImGui::DragFloat("pitch", &rootPitch, 0.01f, 0.01f, 3.f, "%3.2fm");
            ImGui::DragFloat("yaw", &rootYaw, 0.01f, 0.f, 6.f, "%3.2fm");
            ImGui::DragFloat("roll", &rootRoll, 0.01f, -3.f, 3.f, "%3.2fm");

            static float numPix = 100;
            numPix = vertex_pack_Settings.objectSize / settings.pixelSize;
            /*
            if (ImGui::DragFloat("size", &vertex_pack_Settings.objectSize, 0.01f, 1.f, 64.f, "%3.2fm")) anyChange = true;
            if (ImGui::DragFloat("radius", &vertex_pack_Settings.radiusScale, 0.001f, 0.1f, 8.f, "%3.2fm")) anyChange = true;
            if (ImGui::DragFloat("num pix", &numPix, 1.f, 1.f, 1000.f, "%3.4f")) { settings.pixelSize = vertex_pack_Settings.objectSize / numPix; anyChange = true; }
            ImGui::Text("pixel is %2.1f mm", settings.pixelSize * 1000.f);
            if (ImGui::DragFloat("pix SZ", &settings.pixelSize, 0.001f, 0.001f, 1.f, "%3.2fm")) {
                numPix = vertex_pack_Settings.objectSize / settings.pixelSize; anyChange = true;
            }
            */
            if (ImGui::DragInt("seed", &settings.seed, 1, 0, 1000)) { anyChange = true; }



            ImGui::NewLine();
            style.FrameBorderSize = 1;
            ImGui::Checkbox("cropLines", &cropLines);
            style.FrameBorderSize = 0;


            if (ImGui::DragFloat("rnd area", &instanceArea[0], 0.1f, 5.f, 100.f, "%3.2fm"))
            {
                builInstanceBuffer();
            }
            if (ImGui::DragFloat("rnd area1", &instanceArea[1], 0.1f, 5.f, 100.f, "%3.2fm"))
            {
                builInstanceBuffer();
            }
            if (ImGui::DragFloat("rnd area2", &instanceArea[2], 0.1f, 5.f, 100.f, "%3.2fm"))
            {
                builInstanceBuffer();
            }
            if (ImGui::DragFloat("rnd area3", &instanceArea[3], 0.1f, 5.f, 100.f, "%3.2fm"))
            {
                builInstanceBuffer();
            }







            if (root && anyChange)
            {
                ImGui::Text("re building...");
                if (displayModeSinglePlant)
                {
                    _ribbonBuilder.clear(); // why every ti,e just before a build - AH its to acommodate buildAll maybe
                    build(false);

                    levelOfDetail* lod = root->getLodInfo(currentLOD);
                    if (lod)
                    {
                        lod->numVerts = (int)_ribbonBuilder.numVerts();
                        lod->numBlocks = _ribbonBuilder.numPacked() / VEG_BLOCK_SIZE;
                        lod->unused = _ribbonBuilder.numPacked() - _ribbonBuilder.numVerts();
                    }
                }
                else
                {
                    //buildAllLods();
                }
                anyChange = false;
            }






            ImGui::NextColumn();
            if (selectedPart)
            {
                float columnWidth = ImGui::GetWindowWidth() / 2 - 10;
                float columnHeight = ImGui::GetWindowHeight() - 25;

                //ImVec2 CP = ImGui::GetCursorPos();
                if (selectedPart->changedForSave)
                {
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.16f, 0.05f, 0.0f, 0.9f));
                }
                else
                {
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.16f, 0.05f, 0.0f, 0.0f));
                }
                ImGui::BeginChildFrame(123450, ImVec2(columnWidth, columnHeight));
                {
                    selectedPart->renderGui();  // now render the selected item.
                }
                ImGui::EndChildFrame();
                ImGui::PopStyleColor();
                //ImGui::SetCursorPos(CP);

            }
        }
        ImGui::PopFont();
    }
    builderPanel.release();
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
    float Y = extents.y;
    fprintf(terrafectorSystem::_logfile, "buildAllLods() Y = %2.2fm\n", Y);
    fflush(terrafectorSystem::_logfile);

    _ribbonBuilder.clear();

    uint startBlock[16][16];
    uint numBlocks[16][16];
    for (int pIndex = 0; pIndex < 1; pIndex++)
    {
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
                fprintf(terrafectorSystem::_logfile, "lod %d : pixelSize = %2.2fm\n", lod, lodInfo->pixelSize);
                fflush(terrafectorSystem::_logfile);

                //lodInfo->pixelSize = Y / lodInfo->numPixels;// *lodInfo->geometryPixelScale;
                settings.pixelSize = lodInfo->pixelSize;
                settings.seed = 1000 + pIndex;
                build(pIndex * 256 * sizeof(_plant_anim_pivot));
                lodInfo->numVerts = (int)_ribbonBuilder.numVerts();
                lodInfo->numBlocks = _ribbonBuilder.numPacked() / VEG_BLOCK_SIZE - start;
                lodInfo->unused = _ribbonBuilder.numPacked() - _ribbonBuilder.numVerts();
                lodInfo->startBlock = start;

                fprintf(terrafectorSystem::_logfile, "post build %d verts\n", lodInfo->numVerts);
                fflush(terrafectorSystem::_logfile);

                startBlock[pIndex][lod] = start;
                numBlocks[pIndex][lod] = lodInfo->numBlocks;


                plantBuf[pIndex].numLods = __max(plantBuf[pIndex].numLods, lod);    // omdat ons nou agteruit gaan
                plantBuf[pIndex].lods[lod - 1].pixSize = (float)lodInfo->numPixels;
                plantBuf[pIndex].lods[lod - 1].numBlocks = lodInfo->numBlocks;
                plantBuf[pIndex].lods[lod - 1].startVertex = start;



                start += lodInfo->numBlocks;

                fprintf(terrafectorSystem::_logfile, "plant lod : %d, %fmm, %d blocks\n", lod, (float)lodInfo->pixelSize * 1000.f, lodInfo->numBlocks);
            }
        }
        // need to make a copy of the pivot data here after most detailed build I guess
        //plantpivotData->setBlob(ribbonVertex::pivotPoints.data(), , ribbonVertex::pivotPoints.size() * sizeof(_plant_anim_pivot));
    }

    // Now log this
    fprintf(terrafectorSystem::_logfile, "\nbuildAllLods() : %s\n", root->name.c_str());
    fprintf(terrafectorSystem::_logfile, "  size : %2.2f, %2.2f\n", plantBuf[0].size.x, plantBuf[0].size.y);
    fprintf(terrafectorSystem::_logfile, "  lod, blocks, startV, pixSize\n");
    for (int i = 0; i < plantBuf[0].numLods; i++)
    {
        fprintf(terrafectorSystem::_logfile, "  %d : %d, %d, %2.2f\n", i, plantBuf[0].lods[i].numBlocks, plantBuf[0].lods[i].startVertex, plantBuf[0].lods[i].pixSize);
    }


    plantData->setBlob(plantBuf.data(), 0, 1 * sizeof(plant));
    numBinaryPlants = 1;
    builInstanceBuffer();
    fprintf(terrafectorSystem::_logfile, "  just set plants\n");
    fflush(terrafectorSystem::_logfile);

    int numV = __min(65536 * 8, _ribbonBuilder.numPacked());
    vertexData->setBlob(_ribbonBuilder.getPackedData(), 0, numV * sizeof(ribbonVertex8));                // FIXME uploads should be smaller
    fprintf(terrafectorSystem::_logfile, "  just set verts (%d), packed %d, numMaterials %d\n", numV, (int)_ribbonBuilder.packed.size(), (int)_plantMaterial::static_materials_veg.materialVector.size());
    for (int i = 0; i < (int)_plantMaterial::static_materials_veg.materialVector.size(); i++)
    {
        fprintf(terrafectorSystem::_logfile, "    material %d, %s\n", i, _plantMaterial::static_materials_veg.materialVector[i].displayName.c_str());
    }
    fflush(terrafectorSystem::_logfile);

    settings.seed = 1000;

    binaryPlantOnDisk OnDisk;

    OnDisk.numP = 1;
    OnDisk.numV = numV;
    for (int i = 0; i < numV; i++)
    {
        int idx = (_ribbonBuilder.packed[i].b >> 8) & 0x3ff;
        fprintf(terrafectorSystem::_logfile, "  %d,", idx);
        fflush(terrafectorSystem::_logfile);

        
        _vegMaterial M;
        M.path = _plantMaterial::static_materials_veg.materialVector[idx].relativePath;
        M.name = _plantMaterial::static_materials_veg.materialVector[idx].displayName;
        M.index = idx;
        OnDisk.materials[idx] = M;
    }
    fprintf(terrafectorSystem::_logfile, "  found %d materials % \n", (int)OnDisk.materials.size());
    fflush(terrafectorSystem::_logfile);

    lodBake* lodZero = root->getBakeInfo(0);
    if (lodZero)
    {
        OnDisk.billboardMaterial = lodZero->material;
    }

    fprintf(terrafectorSystem::_logfile, "  about to save\n");
    fflush(terrafectorSystem::_logfile);

    std::string resource = terrafectorEditorMaterial::rootFolder;
    std::ofstream os(resource + root->path + ".binary");
    cereal::JSONOutputArchive archive(os);
    archive(OnDisk);

    fprintf(terrafectorSystem::_logfile, "  about to save bianry\n");
    fflush(terrafectorSystem::_logfile);

    std::ofstream osData(resource + root->path + ".binaryData", std::ios::binary);
    osData.write((const char*)plantBuf.data(), OnDisk.numP * sizeof(plant));
    osData.write((const char*)_ribbonBuilder.getPackedData(), numV * sizeof(ribbonVertex8));
    // If n ot existing en pand the ivot points
    if (_ribbonBuilder.pivotPoints.size() < 256) _ribbonBuilder.pivotPoints.resize(256);
    osData.write((const char*)_ribbonBuilder.pivotPoints.data(), OnDisk.numP * 256 * sizeof(_plant_anim_pivot));
}



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

    fprintf(terrafectorSystem::_logfile, "%d plants, %d verts, %d pivots\n", numP, numV, numP*256);

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
    else {
        throw std::runtime_error("file does not exist");
    }

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
}



void _rootPlant::importBinary()
{
    FileDialogFilterVec filters = { {"binary"} };
    std::filesystem::path filepath;
    if (openFileDialog(filters, filepath))
    {
        importBinary(filepath);
    }
}



void _rootPlant::build(uint pivotOffset)
{
    if (!root) return;
    auto start = high_resolution_clock::now();
    
    // Clear some data beforehand - only on single plant, all lods have to call this as well
    _ribbonBuilder.setup(vertex_pack_Settings.getScale(), vertex_pack_Settings.radiusScale, vertex_pack_Settings.getOffset());
    _ribbonBuilder.clearStats(9999);     // just very large for now
    if (displayModeSinglePlant)
    {
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

#pragma optimize("", off)

#define bakeSuperSample 8
#define bakeMipToSave 3
void _rootPlant::bake(std::string _path, std::string _seed, lodBake* _info, glm::mat4 VIEW)
{
    if (!root) return;

    int maxMIPY = (int)log2(_info->pixHeight / 16); // how many mips to get to 16
    int wScaler = 4;// _info->pixHeight / 16;

    float W = _info->extents.x * _info->bakeWidth;      // this is half width
    float H0 = _info->extents.y * _info->bake_V.x;
    float H1 = _info->extents.y * _info->bake_V.y;
    float delH = H1 - H0;
    int iW = wScaler * (int)ceil(_info->pixHeight * W * 2.f / delH / wScaler) * bakeSuperSample;      // *4  /4 is to keep blocks of 4
    iW = __max(wScaler, iW);
    int iH = _info->pixHeight * bakeSuperSample;

    // new try
    float fW = _info->pixHeight * W * 2.f / delH;   // pefect fractional pixels
    //fW /= pow(2, maxMIPY);  // fractional at max mip
    //fW /= 4;    // fractional in block sizes
    int newIW = (int)floor(fW + 0.7f);  //so we mostly go larger but sometimes smaller
    iW = newIW * 4 * (int)pow(2, maxMIPY) * bakeSuperSample;

    /*  New new new code
    *   Lets stick with width for now, pick it like above, but then scale the VIEW matrix to make the content fit the new width
    */
    // BIG IF
    float HorizontalScale = 1.f;
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
        else if (fW < 24) newIW = 16; //16, 8, 4
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
        desc.setDepthStencilTarget(Diligent::TEX_FORMAT_D24_UNORM_S8_UINT);			// keep for now, not sure why, but maybe usefult for cuts
        desc.setColorTarget(0u, Diligent::TEX_FORMAT_RGBA8_UNORM, true);		// albedo
        desc.setColorTarget(1u, Diligent::TEX_FORMAT_RGBA16_FLOAT, true);	    // normal_16
        desc.setColorTarget(2u, Diligent::TEX_FORMAT_RGBA8_UNORM, true);		// normal_8
        desc.setColorTarget(3u, Diligent::TEX_FORMAT_RGBA8_UNORM, true);	    // pbr
        desc.setColorTarget(4u, Diligent::TEX_FORMAT_RGBA8_UNORM, true);	    // extra
        fbo = Fbo::create2D(iW, iH, desc, 1, 4);
        const glm::vec4 clearColor(0.5, 0.5f, 1.0f, 0.0f);
        renderContext->clearFbo(fbo.get(), clearColor, 1.0f, 0, FboAttachmentType::All);    // depth
        renderContext->clearRtv(fbo->getRenderTargetView(0), glm::vec4(0.0, 0.0f, 0.0f, 0.0f));
        renderContext->clearRtv(fbo->getRenderTargetView(1), glm::vec4(0.5, 0.5f, 1.0f, 0.0f));
        renderContext->clearRtv(fbo->getRenderTargetView(2), glm::vec4(0.5, 0.5f, 1.0f, 0.0f));
        renderContext->clearRtv(fbo->getRenderTargetView(3), glm::vec4(0.0, 0.0f, 0.0f, 0.0f));
        renderContext->clearRtv(fbo->getRenderTargetView(4), glm::vec4(1.0, 1.0f, 1.0f, 1.0f));
    }

    VIEW[0] *= HorizontalScale;
    glm::mat4 V, VP;
    V = glm::inverse(VIEW);
    VP = glm::orthoLH(-W, W, H0, H1, -1000.0f, 1000.0f) * V;

    rmcv::mat4 viewproj, view;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            viewproj[j][i] = VP[j][i];
            view[j][i] = VIEW[j][i];
        }
    }

    {
        bakeShader.State()->setFbo(fbo);
        bakeShader.State()->setViewport(0, GraphicsState::Viewport(0, 0, (float)iW, (float)iH, 0, 1), true);
        bakeShader.State()->setRasterizerState(rasterstate);
        bakeShader.State()->setBlendState(blendstateBake);
        bakeShader.Vars()["gConstantBuffer"]["view"] = view;
        bakeShader.Vars()["gConstantBuffer"]["viewproj"] = viewproj;
        bakeShader.Vars()["gConstantBuffer"]["eyePos"] = (float3)VIEW[2] * 10000.f;
        bakeShader.Vars()["gConstantBuffer"]["bake_radius_alpha"] = W;
        bakeShader.Vars()["gConstantBuffer"]["bake_height_alpha"] = H1;
        bakeShader.Vars()["gConstantBuffer"]["bake_AoToAlbedo"] = _info->bakeAOToAlbedo;
        _plantMaterial::static_materials_veg.setTextures(varBakeTextures);
        _plantMaterial::static_materials_veg.rebuildStructuredBuffer();

        bakeShader.drawInstanced(renderContext, 32, totalBlocksToRender);
    }
    renderContext->flush();

    {
        compute_bakeFloodfill.Vars()->setTexture("gAlbedo", fbo->getColorTexture(0));
        compute_bakeFloodfill.Vars()->setTexture("gNormal", fbo->getColorTexture(2));
        compute_bakeFloodfill.Vars()->setTexture("gTranslucency", fbo->getColorTexture(4));
        compute_bakeFloodfill.Vars()->setTexture("gpbr", fbo->getColorTexture(3));
        for (int i = 0; i < 128; i++)
        {
            compute_bakeFloodfill.dispatch(renderContext, iW / 4, iH / 4);
        }
    }
    renderContext->flush();


    
    {
        std::filesystem::path PT = _path;
        std::string resource = terrafectorEditorMaterial::rootFolder;
        std::string newRelative = PT.parent_path().string() + "/bake_" + PT.stem().string() + "/";
        std::string newDir = resource + newRelative;
        replaceAllVEG(resource, "/", "\\");

        _plantMaterial Mat;
        Mat._constData.translucency = 1;
        Mat.albedoPath = newRelative + _info->material.name + "_albedo.dds";
        Mat.albedoName = _info->material.name + "_albedo.dds";
        Mat.normalPath = newRelative + _info->material.name + "_normal.dds";
        Mat.normalName = _info->material.name + "_normal.dds";
        Mat.translucencyPath = newRelative + _info->material.name + "_translucency.dds";
        Mat.translucencyName = _info->material.name + "_translucency.dds";

        fbo->getColorTexture(0).get()->generateMips(renderContext);
        fbo->getColorTexture(1).get()->generateMips(renderContext);
        fbo->getColorTexture(2).get()->generateMips(renderContext);
        fbo->getColorTexture(3).get()->generateMips(renderContext);
        fbo->getColorTexture(4).get()->generateMips(renderContext);

        fbo->getColorTexture(0).get()->captureToFile(0, 0, newDir + "_FULL_albedo.png", Bitmap::FileFormat::PngFile, Bitmap::ExportFlags::ExportAlpha);
        fbo->getColorTexture(2).get()->captureToFile(0, 0, newDir + "_FULL_normal.png", Bitmap::FileFormat::PngFile, Bitmap::ExportFlags::None);


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
    _plantMaterial::static_materials_veg.setTextures(varTextures_Gauraud);
    _plantMaterial::static_materials_veg.setTextures(varTextures_Debug_Pivots);
    _plantMaterial::static_materials_veg.setTextures(varTextures_Debug_Pixels);
    _plantMaterial::static_materials_veg.rebuildStructuredBuffer();
    _plantMaterial::static_materials_veg.setTextures(varBBTextures);
}



void _rootPlant::render(RenderContext* _renderContext, const Fbo::SharedPtr& _fbo, GraphicsState::Viewport _viewport, Texture::SharedPtr _hdrHalfCopy,
    rmcv::mat4  _viewproj, float3 camPos, rmcv::mat4  _view, rmcv::mat4  _clipFrustum, float halfAngle_to_Pixels, bool terrainMode)
{
    FALCOR_PROFILE("vegetation");
    renderContext = _renderContext;

    camVector = (float3(0, 1000, 0) + (float3)settings.root[1] * extents.y / 2.f) - camPos;
    m_halfAngle_to_Pixels = halfAngle_to_Pixels;
    pixelSize = m_halfAngle_to_Pixels * extents.y / glm::length(camVector);


    if (!terrainMode && _ribbonBuilder.numPacked() > 1 && !displayModeSinglePlant)
    {
        FALCOR_PROFILE("compute_veg_lods");
        compute_clearBuffers.dispatch(_renderContext, 1, 1);

        compute_calulate_lod.Vars()["gConstantBuffer"]["view"] = _view;
        compute_calulate_lod.Vars()["gConstantBuffer"]["frustum"] = _clipFrustum;
        compute_calulate_lod.Vars()["gConstantBuffer"]["lodBias"] = loddingBias;
        compute_calulate_lod.Vars()["gConstantBuffer"]["halfAngle_to_Pixels"] = halfAngle_to_Pixels;
        compute_calulate_lod.Vars()["gConstantBuffer"]["firstPlant"] = firstPlant;
        compute_calulate_lod.Vars()["gConstantBuffer"]["lastPlant"] = lastPlant;
        compute_calulate_lod.Vars()["gConstantBuffer"]["firstLod"] = firstLod;
        compute_calulate_lod.Vars()["gConstantBuffer"]["lastLod"] = lastLod;
        compute_calulate_lod.dispatch(_renderContext, MAX_PLANT_INSTANCES / 256, 1);
    }

    


    if (!terrainMode && _ribbonBuilder.numPacked() > 1 && !displayModeSinglePlant)
    {
        FALCOR_PROFILE("billboards");

        billboardShader.State()->setFbo(_fbo);
        billboardShader.State()->setViewport(0, _viewport, true);
        billboardShader.State()->setRasterizerState(rasterstate);
        billboardShader.State()->setBlendState(blendstate);

        billboardShader.Vars()["gConstantBuffer"]["viewproj"] = _viewproj;
        billboardShader.Vars()["gConstantBuffer"]["eyePos"] = camPos;

        billboardShader.Vars()->setTexture("gHalfBuffer", _hdrHalfCopy);   // it chences every time we loose focus


        billboardShader.renderIndirect(_renderContext, drawArgs_billboards);

        auto& profiler = Profiler::instance();
        auto eventBB = profiler.getEvent("/onFrameRender/vegetation/billboards");
        gputimeBB = eventBB->getGpuTimeAverage();
    }

    if (terrainMode || _ribbonBuilder.numPacked() > 1)
    {
        FALCOR_PROFILE("ribbonShader");

        static float time = 0.0f;
        time += gpFramework->getFrameRate().getAverageFrameTime() * 0.001f;

        {
            vegetationShader.State()->setFbo(_fbo);
            vegetationShader.State()->setViewport(0, _viewport, true);
            vegetationShader.State()->setRasterizerState(rasterstate);
            vegetationShader.State()->setBlendState(blendstate);
            vegetationShader.Vars()["gConstantBuffer"]["viewproj"] = _viewproj;
            vegetationShader.Vars()["gConstantBuffer"]["eyePos"] = camPos;
            vegetationShader.Vars()["gConstantBuffer"]["time"] = time;
            vegetationShader.Vars()["gConstantBuffer"]["windDir"] = windDir;
            vegetationShader.Vars()["gConstantBuffer"]["windStrength"] = windStrength;
            vegetationShader.Vars()->setTexture("gHalfBuffer", _hdrHalfCopy);   // it changes every time we loose focus
        }

        {
            vegetationShader_GOURAUD.State()->setFbo(_fbo);
            vegetationShader_GOURAUD.State()->setViewport(0, _viewport, true);
            vegetationShader_GOURAUD.State()->setRasterizerState(rasterstate);
            vegetationShader_GOURAUD.State()->setBlendState(blendstate);
            vegetationShader_GOURAUD.Vars()["gConstantBuffer"]["viewproj"] = _viewproj;
            vegetationShader_GOURAUD.Vars()["gConstantBuffer"]["eyePos"] = camPos;
            vegetationShader_GOURAUD.Vars()["gConstantBuffer"]["time"] = time;
            vegetationShader_GOURAUD.Vars()["gConstantBuffer"]["windDir"] = windDir;
            vegetationShader_GOURAUD.Vars()["gConstantBuffer"]["windStrength"] = windStrength;
            vegetationShader_GOURAUD.Vars()->setTexture("gHalfBuffer", _hdrHalfCopy);   // it changes every time we loose focus
        }

        {
            vegetationShader_DEBUG_PIVOTS.State()->setFbo(_fbo);
            vegetationShader_DEBUG_PIVOTS.State()->setViewport(0, _viewport, true);
            vegetationShader_DEBUG_PIVOTS.State()->setRasterizerState(rasterstate);
            vegetationShader_DEBUG_PIVOTS.State()->setBlendState(blendstate);
            vegetationShader_DEBUG_PIVOTS.Vars()["gConstantBuffer"]["viewproj"] = _viewproj;
            vegetationShader_DEBUG_PIVOTS.Vars()["gConstantBuffer"]["eyePos"] = camPos;
            vegetationShader_DEBUG_PIVOTS.Vars()["gConstantBuffer"]["time"] = time;
            vegetationShader_DEBUG_PIVOTS.Vars()["gConstantBuffer"]["windDir"] = windDir;
            vegetationShader_DEBUG_PIVOTS.Vars()["gConstantBuffer"]["windStrength"] = windStrength;
            vegetationShader_DEBUG_PIVOTS.Vars()->setTexture("gHalfBuffer", _hdrHalfCopy);   // it changes every time we loose focus
        }

        {
            vegetationShader_DEBUG_PIXELS.State()->setFbo(_fbo);
            vegetationShader_DEBUG_PIXELS.State()->setViewport(0, _viewport, true);
            vegetationShader_DEBUG_PIXELS.State()->setRasterizerState(rasterstate);
            vegetationShader_DEBUG_PIXELS.State()->setBlendState(blendstate);
            vegetationShader_DEBUG_PIXELS.Vars()["gConstantBuffer"]["viewproj"] = _viewproj;
            vegetationShader_DEBUG_PIXELS.Vars()["gConstantBuffer"]["eyePos"] = camPos;
            vegetationShader_DEBUG_PIXELS.Vars()["gConstantBuffer"]["time"] = time;
            vegetationShader_DEBUG_PIXELS.Vars()["gConstantBuffer"]["windDir"] = windDir;
            vegetationShader_DEBUG_PIXELS.Vars()["gConstantBuffer"]["windStrength"] = windStrength;
            vegetationShader_DEBUG_PIXELS.Vars()->setTexture("gHalfBuffer", _hdrHalfCopy);   // it changes every time we loose focus
        }

        if (terrainMode)
        {
            vegetationShader.renderIndirect(_renderContext, drawArgs_vegetation);
        }
        else
        {
            if (displayModeSinglePlant)
            {
                // single plant
                if (showDebugInShader)
                    vegetationShader_DEBUG_PIXELS.drawInstanced(_renderContext, 32, totalBlocksToRender);
                else if (showNumPivots)
                    vegetationShader_DEBUG_PIVOTS.drawInstanced(_renderContext, 32, totalBlocksToRender);
                else
                    vegetationShader.drawInstanced(_renderContext, 32, totalBlocksToRender);
            }
            else
            {
                if (showDebugInShader)
                    vegetationShader_DEBUG_PIXELS.renderIndirect(_renderContext, drawArgs_vegetation);
                else if (showNumPivots)
                    vegetationShader_DEBUG_PIVOTS.renderIndirect(_renderContext, drawArgs_vegetation);
                else
                    vegetationShader.renderIndirect(_renderContext, drawArgs_vegetation);
            }
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
}


void _rootPlant::builInstanceBuffer()
{
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
                float noise = perlin.octave2D_01(pos.x / 2.f, pos.z / 2.f, 3);
                sum += pow(noise, 3);
            }
            sum -= 1.f;

            instanceBuf[i].plant_idx = 0;
            instanceBuf[i].position = pos;
            instanceBuf[i].scale = 1.f + d_1_1(generator) * 0.83f;
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
                pos = { d_1_1(generator) * instanceArea[type], 1000.f, d_1_1(generator) * instanceArea[type] };
                float noise = perlin.octave2D_01(pos.x / 2.f + type, pos.z / 2.f, 3);
                sum += pow(noise, 5);
            }
            sum -= 1;

            instanceBuf[i].plant_idx = type;
            instanceBuf[i].position = pos;
            instanceBuf[i].scale = 1.f + d_1_1(generator) * 0.2f;
            instanceBuf[i].rotation = d_1_1(generator) * 3.14f;
            //instanceBuf[i].time_offset = d_1_1(generator) * 100;
        }
    }

    // plant zero is always fixed in the middle
    instanceBuf[0].position = { 0, 1000, 0 };
    instanceBuf[0].scale = 1.f;
    instanceBuf[0].rotation = 0;

    instanceData->setBlob(instanceBuf.data(), 0, MAX_PLANT_INSTANCES * sizeof(plant_instance));
}
