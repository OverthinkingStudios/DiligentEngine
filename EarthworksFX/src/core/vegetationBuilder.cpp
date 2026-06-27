#include "vegetationBuilder.h"
//#include "imgui.h"
#include "PerlinNoise.hpp"          //https://github.com/Reputeless/PerlinNoise/blob/master/PerlinNoise.hpp
#include "ots/Log.hpp"

using namespace std::chrono;

#define TOOLTIP(x)  if (ImGui::IsItemHovered()) {ImGui::SetTooltip(x);}
//#pragma optimize("", off)

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

ImVec4 selected_color = ImVec4(0.4f, 0.1f, 0.0f, 1);
ImVec4 stem_color = ImVec4(0.05f, 0.02f, 0.0f, 1);
ImVec4 leaf_color = ImVec4(0.01f, 0.04f, 0.01f, 1);
ImVec4 mat_color = ImVec4(0.0f, 0.01f, 0.07f, 1);

ImVec4 error_color = ImVec4(0.6f, 0.0f, 0.0f, 1);
ImVec4 outofscope_color = ImVec4(0.4f, 0.1f, 0.0f, 1);


float perlinData[1024];

materialCache_plants _plantMaterial::static_materials_veg;
std::string materialCache_plants::lastFile;


ribbonBuilder _ribbonBuilder;




// _rootPlant
_plantBuilder* _rootPlant::selectedPart = nullptr;
_plantMaterial* _rootPlant::selectedMaterial = nullptr;
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

    spdlog::error("error : vegetation material - {} does not exist", _name.c_str());
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
        spdlog::info("add vegeatation material[{}] - {}", materialIndex, _path.filename().string().c_str());
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
            spdlog::info("{}", cmdExp.c_str());
            system(cmdExp.c_str());
            if (isSRGB)
            {
                std::string cmdExp2 = comprs + " -fd BC7 -Quality 0.01 " + temp + ddsFilename;
                replaceAllVEG(cmdExp2, "/", "\\");
                spdlog::info("{}", cmdExp2.c_str());
                system(cmdExp2.c_str());
            }
            else
            {
                std::string cmdExp2 = comprs + " -fd BC6H " + temp + ddsFilename;
                replaceAllVEG(cmdExp2, "/", "\\");
                spdlog::info("{}", cmdExp2.c_str());
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

        spdlog::info("{}", tex->getName().c_str());

        return (uint)(textureVector.size() - 1);
    }
    else
    {
        spdlog::error("failed {}", _path.string().c_str());
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
                    if (material.changedForSave) style.Colors[ImGuiCol_Button] = ImVec4(0.4f, 0.2f, 0, 1.0f);
                    else
                    {
                        if (selectedMaterial == displaySortMap[cnt].index)
                        {
                            style.Colors[ImGuiCol_Button] = ImVec4(0.0f, 0.4f, 0.4f, 0.8f);
                        }
                        else
                        {
                            style.Colors[ImGuiCol_Button] = ImVec4(0, 0, 0, 0.3f);
                        }
                    }
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
        // I dont liek thid, it does a terrain reset - need global counter and only affect veegtation
    }
    else
    {
        //ImGui::Text("default material");
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
        }
        ImGui::PopFont();

        ImGui::PushFont(_gui->getFont("default"));
        {
            float W = (ImGui::GetColumnWidth() - 30) / 3.f;
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.001f, 0.001f, 0.001f, 1.0f));

            ImGui::SetNextItemWidth(100);
            if (ImGui::Button("load", ImVec2(W, 0))) { import(); }


            if (changedForSave) {
                ImGui::SameLine(0, 10);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.0f, 0.7f));
                if (ImGui::Button("Save", ImVec2(W, 0)))
                {
                    save();
                    changedForSave = false;
                }
                ImGui::PopStyleColor();
            }
            else
            {
                ImGui::SameLine(0, 10);
                ImGui::Button("Save", ImVec2(W, 0));  // just a placeholde , can be done neater
            }

            ImGui::SameLine(0, 10);
            if (ImGui::Button("Save as", ImVec2(W, 0)))
            {
                eXport();
            }
            ImGui::PopStyleColor();
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

        /*
        ImGui::PushID(9991);
        if (ImGui::Selectable(alphaName.c_str())) { loadTexture(1); changed = true; }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("ALPHA  {%d}", _constData.alphaTexture);
            _plantMaterial::static_materials_veg.dispTexIndex = _constData.alphaTexture;
        }
        ImGui::PopID();
        */

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
    ImGui::PopFont();


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
        fullPath = path;
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

bool _vegMaterial::renderGui(uint& gui_id)
{
    auto& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Header] = ImVec4(0.01f, 0.03f, 0.01f, 1.0f);  // selectable

    bool changed = false;
    ImGui::PushID(gui_id);
    if (ImGui::Selectable(name.c_str()))
    {
        changed = true;
        loadFromFile();
    }
    std::string tool = path + "\n{" + std::to_string(index) + "}";
    TOOLTIP(tool.c_str());
    changed |= ImGui::DragFloat2("albedo", &albedoScale.x, 0.01f, 0.1f, 2.0f);
    changed |= ImGui::DragFloat2("translucency", &translucencyScale.x, 0.01f, 0.1f, 2.0f);
    ImGui::PopID();
    gui_id++;

    return changed;
}


// FXME can I amstarct this away mnore, and combine it with the load at the bottom of the file that is amlmost identical
void _plantRND::loadFromFile()
{
    std::filesystem::path filepath;
    FileDialogFilterVec filters = { {"leaf"}, {"stem"}, {"clump"}, {"flower"} };
    if (openFileDialog(filters, filepath))
    {
        if (filepath.string().find(".leaf") != std::string::npos)
        {
            plantPtr.reset(new _leafBuilder); type = P_LEAF;
            spdlog::info("_plantRND   _leafBuilder  {}", filepath.string().c_str());
        }
        if (filepath.string().find(".stem") != std::string::npos)
        {
            plantPtr.reset(new _stemBuilder);  type = P_STEM;
            spdlog::info("_plantRND   _stemBuilder  {}", filepath.string().c_str());
        }
        if (filepath.string().find(".clump") != std::string::npos)
        {
            plantPtr.reset(new _clumpBuilder);  type = P_CLUMP;
            spdlog::info("_plantRND   _clumpBuilder  {}", filepath.string().c_str());
        }
        if (filepath.string().find(".flower") != std::string::npos)
        {
            plantPtr.reset(new _flowerBuilder);  type = P_FLOWER;
            spdlog::info("_plantRND   _flowerBuilder  {}", filepath.string().c_str());
        }

        path = materialCache::getRelative(filepath.string());
        name = filepath.filename().string();
        plantPtr->path = path;
        plantPtr->name = name;
        plantPtr->loadPath();
    }
}


void _plantRND::reload()
{
    if (std::filesystem::exists(terrafectorEditorMaterial::rootFolder + path))
    {
        switch (type)
        {
        case P_LEAF:
            plantPtr.reset(new _leafBuilder);
            spdlog::info("_plantRND   _leafBuilder  {}", path.c_str());
            break;
        case P_STEM:
            plantPtr.reset(new _stemBuilder);
            spdlog::info("_plantRND   _stemBuilder  {}", path.c_str());
            break;
        case P_CLUMP:
            plantPtr.reset(new _clumpBuilder);
            spdlog::info("_plantRND   _clumpBuilder  {}", path.c_str());
            break;
        case P_FLOWER:
            plantPtr.reset(new _flowerBuilder);
            spdlog::info("_plantRND   _flowerBuilder  {}", path.c_str());
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


void _plantRND::renderGui(uint& gui_id)
{
    ImGui::PushID(gui_id);
    if (ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Framed))
    {
        TOOLTIP(path.c_str());
        if (ImGui::IsItemClicked())  loadFromFile();
        ImGui::TreePop();
    }
    ImGui::PopID();
    gui_id++;
}


Gui* _plantBuilder::_gui;



template <class T>     T randomVector<T>::get()
{
    rnd_idx += _rootPlant::rand_int(_rootPlant::generator);
    rnd_idx %= data.size();
    return data[rnd_idx];
}

template <class T>     void  randomVector<T>::renderGui(char* name, uint& gui_id)
{
    bool first = true;
    bool changed = false;
    {
        ImGui::Text(name);
        ImGui::SameLine(180, 0);
        if (ImGui::Button("-", ImVec2(20, 0))) { data.pop_back(); if (data.size() == 0) { data.emplace_back(); } }
        ImGui::SameLine(0, 10);
        if (ImGui::Button("+", ImVec2(20, 0))) { data.emplace_back(); }

        for (auto& D : data) D.renderGui(gui_id);   //changed |=  should be consistemt so I can use it
    }
    //return changed;
}


bool anyChange = true;

// New random branches with age factor


// FXME can I amstarct this away mnore, and combine it with the load at the bottom of the file that is amlmost identical
void _randomBranch::loadFromFile()
{
    std::filesystem::path filepath;
    FileDialogFilterVec filters = { {"leaf"}, {"stem"}, {"clump"}, {"flower"} };
    if (openFileDialog(filters, filepath))
    {
        if (filepath.string().find(".leaf") != std::string::npos) { plantPtr.reset(new _leafBuilder); type = P_LEAF; }
        if (filepath.string().find(".stem") != std::string::npos) { plantPtr.reset(new _stemBuilder);  type = P_STEM; }
        if (filepath.string().find(".clump") != std::string::npos) { plantPtr.reset(new _clumpBuilder);  type = P_CLUMP; }
        if (filepath.string().find(".flower") != std::string::npos) { plantPtr.reset(new _flowerBuilder);  type = P_FLOWER; }

        path = materialCache::getRelative(filepath.string());
        name = filepath.filename().string();
        plantPtr->path = path;
        plantPtr->name = name;
        plantPtr->loadPath();
    }
}


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

        spdlog::info("_randomBranch::reload  {}", path.c_str());

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
        if (ImGui::Button("-", ImVec2(20, 0))) { branchData.pop_back(); }
        ImGui::SameLine(0, 10);
        if (ImGui::Button("+", ImVec2(20, 0))) { branchData.emplace_back(); }

        for (int i = 0; i < 1024; i += 100)
        {
            ImGui::Text("%d, ", RND[i]);
            ImGui::SameLine(0, 5);
        }
        ImGui::NewLine();

        for (auto& D : branchData) if (D.renderGui(gui_id)) { std::sort(branchData.begin(), branchData.end()); buildArray(); changed = true; }
    }

    return changed;
}




//ImGui::SameLine(textWIDTH + itemWIDTH, 0); \


void  _plantBuilder::renderGuiHeader(FileDialogFilterVec& _filters)
{
    float W = ImGui::GetColumnWidth();
    auto& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Header] = ImVec4(0.002f, 0.002f, 0.002f, 1.0f);  // selectable
    style.WindowPadding = ImVec2(0, 0);
    ImGui::PushFont(_gui->getFont("header1"));
    {
        ImGui::BeginChildFrame(37, ImVec2(W, 60));
        ImGui::Text(name.c_str());
        ImGui::EndChildFrame();
    }
    ImGui::PopFont();
    TOOLTIP(path.c_str());

    ImGui::PushFont(_gui->getFont("header2"));
    {
        //load();
        ImGui::NewLine();
        ImGui::SameLine(W / 5, 0);

        if (changedForSave) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.1f, 0.0f, 0.8f));
            if (ImGui::Button("Save##_plantBuilder", ImVec2(W * 0.38f, 0)))
            {
                savePath();
                changedForSave = false;
            }
            TOOLTIP("ctrl-S");
            ImGui::PopStyleColor();
        }


        ImGui::SameLine(W / 5 * 3, 0);
        if (ImGui::Button("Save as##_plantBuilder", ImVec2(W * 0.38f, 0)))
        {
            std::filesystem::path filepath;
            if (saveFileDialog(_filters, filepath))
            {
                path = materialCache::getRelative(filepath.string());
                name = filepath.filename().string();
                savePath();
            }
        }
    }
    ImGui::PopFont();
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
        //reportError(fmt::format("{}\nError: {}", "File does not exists in the relative tree structure", ""));
        spdlog::error("{}\nError: {}", "File does not exists in the relative tree structure", terrafectorEditorMaterial::rootFolder + path);
    }
}

void _leafBuilder::savePath()
{
    std::ofstream os(terrafectorEditorMaterial::rootFolder + path);
    cereal::JSONOutputArchive archive(os);
    archive(*this);
    changed = false;
}


bool _leafBuilder::renderGui()
{
    //ImGuiTreeNodeFlags_DefaultOpen | 
    int flags = ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_OpenOnArrow;
    unsigned int gui_id = 1001;

    renderGuiHeader(filters);



    ImGui::NewLine();
    auto& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Header] = ImVec4(0.01f, 0.01f, 0.01f, 1.f);
    if (ImGui::TreeNodeEx("animation", flags))
    {
        ImGui::PushFont(_gui->getFont("default"));
        {
            if (ImGui::Combo("pivot type", (int*)&pivotType, "off\0leaf\0full 1\0full 2\0")) { changed = true; }

            R_FLOAT("stiffness", ossilation_stiffness, 0.1f, 0.8f, 20.f, "");
            R_FLOAT("sqrt(sway)", ossilation_constant_sqrt, 0.1f, 1.01f, 100.f, "");
            ImGui::SameLine(0, 20);
            ImGui::Text("%2.3fHz", 1.f / (rootFrequency() * sqrt(leaf_length.x * 0.001f) / sqrt(leaf_width.x * 0.001f)));
            //R_FLOAT("root-tip", ossilation_power, 0.01f, 0.02f, 1.8f, "");
        }
        ImGui::PopFont();
        ImGui::TreePop();
    }

    ImGui::NewLine();
    style.Colors[ImGuiCol_Header] = ImVec4(0.01f, 0.01f, 0.01f, 1.f);
    if (ImGui::TreeNodeEx("stem", flags))
    {
        ImGui::PushFont(_gui->getFont("default"));
        {
            R_LENGTH("length", stem_length, "stem length in mm", "random");
            R_LENGTH("width", stem_width, "stem width in mm", "random");
            if (stem_length.x > 0 && stem_width.x > 0)
            {
                R_CURVE("curve", stem_curve, "stem curvature", "random");

                R_VERTS("num verts", stemVerts);
                changed |= stem_Material.renderGui(gui_id);
            }
            R_CURVE("angle", stem_to_leaf, "stem to leaf angle in radians", "random");
            R_CURVE("roll", stem_to_leaf_Roll, "roll like ferns to make them all face one side", "random");

        }
        ImGui::PopFont();
        ImGui::TreePop();
    }

    ImGui::NewLine();
    style.Colors[ImGuiCol_Header] = ImVec4(0.01f, 0.01f, 0.01f, 1.f);
    if (ImGui::TreeNodeEx("leaf", flags))
    {
        ImGui::PushFont(_gui->getFont("default"));
        {
            CHECKBOX("face camera", &cameraFacing, "");
            //if (ImGui::Checkbox("face camera", &cameraFacing)) changed = true;
            R_LENGTH("length", leaf_length, "mm", "random");
            R_LENGTH("width", leaf_width, "mm", "random");
            R_LENGTH("offset", width_offset, "", "random");
            CHECKBOX("wideBase", &wideBase, "for grasses etc that start wide\ntemporary till we have graph edit");

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
            materials.renderGui("materials", gui_id);   //changed |= 
        }
        ImGui::PopFont();
        ImGui::TreePop();
    }

    changedForSave |= changed;
    anyChange |= changed;
    bool RET = changed;
    changed = false;
    return RET;
}


void _leafBuilder::treeView()
{
    auto& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Header] = leaf_color;
    int flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_OpenOnArrow;
    //int flags = ImGuiTreeNodeFlags_Leaf;
    if (_rootPlant::selectedPart == this) {
        style.FrameBorderSize = 3;
        flags = flags | ImGuiTreeNodeFlags_Selected;
        //style.Colors[ImGuiCol_Header] = selected_color;
        style.Colors[ImGuiCol_Border] = ImVec4(0.7f, 0.7f, 0.7f, 1);
    }

    if (changedForSave) style.Colors[ImGuiCol_Header] = selected_color;

    bool spaceError = path.find(" ") != std::string::npos;
    bool outOfScope = path.find(_rootPlant::root_path) == std::string::npos;

    if (fileNotFound || spaceError)
    {
        style.Colors[ImGuiCol_Header] = error_color;
    }
    else if (outOfScope)
    {
        style.Colors[ImGuiCol_Header] = outofscope_color;
    }

    TREE_LINE(name.c_str(), path.c_str(), flags)
    {
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            {
                ImGui::PushFont(_gui->getFont("header1"));
                ImGui::Text(path.c_str());
                ImGui::PushStyleColor(ImGuiCol_Text, error_color);
                {
                    if (spaceError) ImGui::Text("error - filename or terrain_path contains spaces");
                    if (fileNotFound) ImGui::Text("error - file not found, please re-link");
                    if (outOfScope) ImGui::Text("warning - file not in the same folder struture");  //needs better warning
                }
                ImGui::PopStyleColor();
                ImGui::NewLine();


                if (_ribbonBuilder.tooManyPivots)     ImGui::Text("error - more than 255 pivots");
                else
                {
                    ImGui::Text("%d instances, %d verts, %d / %d pivots ", numInstancePacked, numVertsPacked, numPivots, _ribbonBuilder.numPivots());
                }
                ImGui::PopFont();
            }
            ImGui::EndTooltip();
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
        // FXIME add some stem that pushes into the leaf here

        GROW(node, -width * 1.5f);  // Now move ever so slghtly backwards for better penetration of stem to leaf
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
        //bool showLeaf = (width * d50(_rootPlant::generator)) > _settings.pixelSize;
        bool showLeaf = width > _settings.pixelSize;
        if (showLeaf)
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

                CylinderExclusion |= _settings.testExclusion(node[3]);
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

    uint numVerts = _ribbonBuilder.numVerts() - startVerts;
    if (numVerts > 0) numInstancePacked++;
    numVertsPacked += numVerts;
    changedForSave |= changed;
    return node;
}




// _flowerRing
// -----------------------------------------------------------------------------------------------------------------------------------
void _flowerRing::renderGui(bool& changed, bool& changedForSave)
{

    auto& style = ImGui::GetStyle();
    int flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_OpenOnArrow;
    unsigned int gui_id = 5001;

    ImGui::NewLine();
    style.Colors[ImGuiCol_Header] = ImVec4(0.01f, 0.01f, 0.01f, 1.f);
    if (ImGui::TreeNodeEx("ring", flags))
    {
        R_VERTS("petals", numPetals);
        R_CURVE("pitch", pitch, "angle in radians", "random");
        R_LENGTH("offset", offset_mm, "push forward in mm", "random");
        R_LENGTH("radius", radius_mm, "radius in mm", "random");


        CHECKBOX("symmetrical", &symmetrical, "true for pea flowers /nfalse for daisies");
        R_CURVE("angle", angle_offset, "stem to leaf angle in radians", "random");
        R_CURVE("twist", twist, "roll like ferns to make them all face one side", "random");

        petals.renderGui("petals", gui_id);

        ImGui::TreePop();
    }

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
        spdlog::error("{}\nError: {}", "File does not exists in the relative tree structure", terrafectorEditorMaterial::rootFolder + path);
        //reportError(fmt::format("{}\nError: {}", "File does not exists in the relative tree structure", ""));
    }
}

void _flowerBuilder::savePath()
{
    std::ofstream os(terrafectorEditorMaterial::rootFolder + path);
    cereal::JSONOutputArchive archive(os);
    archive(*this);
    changed = false;
}


bool _flowerBuilder::renderGui()
{
    int flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_OpenOnArrow;
    unsigned int gui_id = 6001;

    renderGuiHeader(filters);



    ImGui::NewLine();
    auto& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Header] = ImVec4(0.01f, 0.01f, 0.01f, 1.f);
    if (ImGui::TreeNodeEx("stem", flags))
    {

        R_LENGTH("length", stem_length, "stem length in mm", "random");
        R_LENGTH("width", stem_width, "stem width in mm", "random");
        if (stem_length.x > 0 && stem_width.x > 0)
        {
            R_CURVE("curve", stem_curve, "stem curvature", "random");

            R_VERTS("num verts", stemVerts);
            changed |= stem_Material.renderGui(gui_id);
        }
        R_CURVE("angle", stem_to_leaf, "stem to leaf angle in radians", "random");
        R_CURVE("roll", stem_to_leaf_Roll, "roll like ferns to make them all face one side", "random");


        ImGui::TreePop();
    }

    ImGui::NewLine();
    style.Colors[ImGuiCol_Header] = ImVec4(0.01f, 0.01f, 0.01f, 1.f);
    ImGui::Text("petal rings # %d", rings.size());

    {
        if (rings.size() == 0) rings.emplace_back();
        if (ImGui::Button("  -  "))
        {
            if (rings.size() > 1) { rings.pop_back(); }
            changed = true;
        }
        ImGui::SameLine(0, 20);
        if (ImGui::Button("  +  "))
        {
            rings.emplace_back();
            //rings.resize(rings.size() + 1);
            changed = true;
        }

        ImGui::SliderInt("ring#", &currentRing, 0, (int)rings.size() - 1);
        currentRing = clamp(currentRing, 0, (int)rings.size() - 1);
        rings[currentRing].renderGui(changed, changedForSave);

        ImGui::NewLine();
        ImGui::Text("Center");
        R_LENGTH("offset", center_offset, "mm", "random");
        R_LENGTH("size", center_size, "mm", "random");
        ImGui::Text("material");
        changed |= center_Material.renderGui(gui_id);

        //ImGui::TreePop();
    }

    changedForSave |= changed;
    anyChange |= changed;
    bool RET = changed;
    changed = false;
    return RET;
}


void _flowerBuilder::treeView()
{
    auto& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Header] = leaf_color;
    int flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_OpenOnArrow;
    //int flags = ImGuiTreeNodeFlags_Leaf;
    if (_rootPlant::selectedPart == this) {
        style.FrameBorderSize = 3;
        flags = flags | ImGuiTreeNodeFlags_Selected;
        //style.Colors[ImGuiCol_Header] = selected_color;
        style.Colors[ImGuiCol_Border] = ImVec4(0.7f, 0.7f, 0.7f, 1);
    }

    if (changedForSave) style.Colors[ImGuiCol_Header] = selected_color;

    bool spaceError = path.find(" ") != std::string::npos;
    bool outOfScope = path.find(_rootPlant::root_path) == std::string::npos;

    if (fileNotFound || spaceError)
    {
        style.Colors[ImGuiCol_Header] = error_color;
    }
    else if (outOfScope)
    {
        style.Colors[ImGuiCol_Header] = outofscope_color;
    }

    TREE_LINE(name.c_str(), path.c_str(), flags)
    {
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            {
                ImGui::PushFont(_gui->getFont("header1"));
                ImGui::Text(path.c_str());
                ImGui::PushStyleColor(ImGuiCol_Text, error_color);
                {
                    if (spaceError) ImGui::Text("error - filename or terrain_path contains spaces");
                    if (fileNotFound) ImGui::Text("error - file not found, please re-link");
                    if (outOfScope) ImGui::Text("warning - file not in the same folder struture");  //needs better warning
                }
                ImGui::PopStyleColor();
                ImGui::NewLine();


                if (_ribbonBuilder.tooManyPivots)     ImGui::Text("error - more than 255 pivots");
                else
                {
                    ImGui::Text("%d instances, %d verts, %d / %d pivots ", numInstancePacked, numVertsPacked, numPivots, _ribbonBuilder.numPivots());
                }
                ImGui::PopFont();
            }
            ImGui::EndTooltip();
        }

        style.FrameBorderSize = 0;
        CLICK_PART;

        for (auto& R : rings)
        {
            for (auto& P : R.petals.data)
            {
                if (P.plantPtr) P.plantPtr->treeView();
            }
        }


        ImGui::TreePop();
    }
}


void _flowerBuilder::clear_build_info()
{
    numInstancePacked = 0;
    numVertsPacked = 0;
    numPivots = 0;
}


lodBake* _flowerBuilder::getBakeInfo(uint i)
{
    if (i < lod_bakeInfo.size()) return &lod_bakeInfo[i];
    else return nullptr;
}


levelOfDetail* _flowerBuilder::getLodInfo(uint i)
{
    if (i < lodInfo.size()) return &lodInfo[i];
    else return nullptr;
}

glm::mat4  _flowerBuilder::build_2(buildSetting _settings, uint _bakeIndex, bool _faceCamera)
{
    lodBake& lB = lod_bakeInfo[_bakeIndex];
    glm::mat4 node = _settings.root;
    GROW(node, 0.01f);  // should come from main ring, can I calculkate


    if (lB.pixHeight > 0)
    {
        float w = lB.extents.x * lB.bakeWidth;
        uint mat = lB.material.index;

        _ribbonBuilder.startRibbon(lB.faceCamera, _settings.pivotIndex);
        PITCH(node, 1.57f);
        GROW(node, -w);
        _ribbonBuilder.set(node, w, mat, float2(1.f, 0.f), 1.f, 1.f, true, 0.5f, 0.1f, 0.0f, false);
        GROW(node, w * 2.f);
        _ribbonBuilder.set(node, w, mat, float2(1.f, 1.f), 1.f, 1.f, true, 0.5f, 0.1f, 0.0f, false);
    }

    return node;
}

glm::mat4 _flowerBuilder::build(buildSetting _settings, bool _addVerts)
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
            }
        }

        if (found)
        {

            if (!_settings.isBaking)    // so do not use any of these during a bake
            {
                if (lod.bakeType == BAKE_DIAMOND)       build_2(_settings, lod.bakeIndex, true);
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
                for (int i = 0; i < rings.size(); i++)
                {
                    float radius = RND_B(rings[i].radius_mm) * 0.001f;

                    float A = 6.2831853071f / rings[i].numPetals.x;
                    for (int j = 0; j < rings[i].numPetals.x; j++)
                    {
                        node = root_node;
                        GROW(node, rings[i].offset_mm.x * 0.001f);
                        ROLL(node, A * j);
                        //PITCH(node, 1.5f);
                        PITCH(node, -1.57f);
                        GROW(node, radius);
                        PITCH(node, rings[i].pitch.x);

                        _plantRND petal = rings[i].petals.get();
                        _settings.root = node;
                        if (petal.plantPtr) petal.plantPtr->build(_settings, _addVerts);
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
        spdlog::error("{}\nError: {}", "File does not exists in the relative tree structure", terrafectorEditorMaterial::rootFolder + path);
        //reportError(fmt::format("{}\nError: {}", "File does not exists in the relative tree structure", ""));
    }
}

void _stemBuilder::savePath()
{
    std::ofstream os(terrafectorEditorMaterial::rootFolder + path);
    cereal::JSONOutputArchive archive(os);
    archive(*this);
    changed = false;
}


bool _stemBuilder::renderGui()
{
    int flags = ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_OpenOnArrow;
    int flagsB = ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_OpenOnArrow;
    unsigned int gui_id = 1001;
    auto& style = ImGui::GetStyle();

    renderGuiHeader(filters);


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
        ImGui::PushFont(_gui->getFont("default"));
        {
            ImGui::SameLine(0, 10);
            ImGui::Text("(%2.1f - %2.1f)mm, [%d, %d]", root_width * 1000.f, tip_width * 1000.f, (int)age, firstLiveSegment);

            R_FLOAT_EX("age", numSegments, 0.01f, 0.9f, 500.f, "total number of nodes/sections between branches", "randomness");
            if (numSegments.x < 0.9f) numSegments.x = 0.9f;
            R_FLOAT_EX("living", max_live_segments, 0.01f, 0.5f, 500.f, "counting back from the tip, how many nodes/sections have live branches", "random variations");
            ImGui::Separator();

            R_LENGTH_EX("length", stem_length, 0.5f, 0.5f, 2000.f, "stem length in mm", "randomness");
            CHECKBOX("age-scale", &lengthFromBranchAge, "scales the length relative to the branch age. \n Good for some compound leaves etc.");

            ImGui::Separator();
            R_LENGTH_EX("width", stem_width, 0.002f, 0.5f, 20.f, "stem width in mm \nDistance between side branches", "randomness");
            R_LENGTH_EX("growth", stem_d_width, 0.01f, 0.1f, 20.f, "amount to grow for each node \n The stem gets wider by this much every node", "random variations");
            R_LENGTH_EX("pow", stem_pow_width, 0.01f, 0.1f, 10.f, "Bias the width towards the root or tip \n small - flair at the bottom \n1.0 - linear \nlarge - stay wide till close to the top", "random variations");
            ImGui::Separator();
            R_CURVE("node twist", node_rotation, "twist of the stalk", "randomness");
            R_CURVE("node bend", node_angle, "stem bend at a node", "randomness");

            ImGui::Separator();
            R_CURVE("curve", stem_curve, "curve in a single segment", "randomness");
            R_CURVE("phototropism", stem_phototropism, "", "randomness");

            R_CURVE("perlin Pitch", perlinCurve, "amount", "repeats");
            R_CURVE("perlin Yaw", perlinTwist, "amount", "repeats");

            ImGui::Separator();
            R_FLOAT("length split", nodeLengthSplit, 0.1f, 1.f, 32.f, "Number of pixels to insert a new node in this stem, push it higher for better curvature");

            ImGui::Separator();
            style.Colors[ImGuiCol_Header] = ImVec4(0.03f, 0.03f, 0.03f, 1.f);
            changed |= stem_Material.renderGui(gui_id);


            stemReplacement.renderGui("stem override", gui_id);
        }
        ImGui::PopFont();
        ImGui::TreePop();
    }


    // leaves
    ImGui::NewLine();
    style.Colors[ImGuiCol_Header] = ImVec4(0.01f, 0.01f, 0.01f, 1.f);
    if (ImGui::TreeNodeEx("branches / leaves", flags))
    {
        ImGui::PushFont(_gui->getFont("default"));
        {
            ImGui::SameLine(0, 20);
            ImGui::Text("[%d]", numLeavesBuilt);

            R_FLOAT_EX("#", numLeaves, 0.01f, 0.5f, 10.f, "number of leaves per node", "randomness");
            numLeaves.x = __max(0.5f, numLeaves.x);

            ImGui::Separator();
            R_CURVE("angle", leaf_angle, "angle of the stalk at the tip", "change of angle as it ages, usually drooping");
            R_CURVE("random", leaf_rnd, "randomness in the angles", "randomness");

            CHECKBOX("roll horizontal", &roll_horizontal, "roll the branch do that the first leaf exists horizontally like many pine and fir trees");
            R_FLOAT("roll offset", rollOffset, 0.01f, -3.5f, 3.5f, "initial roll angle, relative is horizon is set");

            R_FLOAT("outwards offset", branchPush, 0.01f, 0.f, 0.5f, "% offset away from the center of the parent branch");


            ImGui::Separator();
            CHECKBOX("compoundLeaf", &compoundLeaf, "A special case for fern leaves etc. \nIt forces teh number of leaves per node to 1, and\nalternates left right.");
            R_LENGTH_EX("full grown", leaf_fully_grown_age, 0.1f, 1.0f, 100.f, "age at which leaves are fully grown and does not get bigger", "random");

            ImGui::Separator();
            R_FLOAT("age_power", leaf_age_power, 0.1f, 1.f, 25.f, "");
            CHECKBOX("age override", &leaf_age_override, " If set false we pass in -1 and the leaf can set its own age, if true we set the age");

            ImGui::Separator();
            style.Colors[ImGuiCol_Header] = ImVec4(0.03f, 0.03f, 0.03f, 1.f);
            if (branches.renderGui("branches", gui_id)) { changed = true; }

            ImGui::Separator();
            CHECKBOX("unique tip", &unique_tip, "load a different part for the tip \nif off it will reuse one of the breanch / leaf parts");
            if (unique_tip)
            {
                R_LENGTH("tip age", tip_age, "age of the tip\n-1 means that the plant will decide", "randomness");
                style.Colors[ImGuiCol_Header] = ImVec4(0.03f, 0.03f, 0.03f, 1.f);
                tip.renderGui("tip", gui_id);
            }
        }
        ImGui::PopFont();
        ImGui::TreePop();
    }



    changedForSave |= changed;
    anyChange |= changed;
    bool RET = changed;
    changed = false;
    return RET;
}

void _stemBuilder::treeView()
{
    auto& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Header] = stem_color;
    int flags = ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
    //int flags = ImGuiTreeNodeFlags_Leaf;
    if (_rootPlant::selectedPart == this) {
        style.FrameBorderSize = 3;
        flags = flags | ImGuiTreeNodeFlags_Selected;
        style.Colors[ImGuiCol_Border] = ImVec4(0.7f, 0.7f, 0.7f, 1);
    }

    if (changedForSave) style.Colors[ImGuiCol_Header] = selected_color;

    bool spaceError = path.find(" ") != std::string::npos;
    bool outOfScope = path.find(_rootPlant::root_path) == std::string::npos;

    if (fileNotFound || spaceError)
    {
        style.Colors[ImGuiCol_Header] = error_color;
    }
    else if (outOfScope)
    {
        style.Colors[ImGuiCol_Header] = outofscope_color;
    }

    TREE_LINE(name.c_str(), path.c_str(), flags)
    {
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            {
                ImGui::PushFont(_gui->getFont("header1"));
                ImGui::Text(path.c_str());
                ImGui::PushStyleColor(ImGuiCol_Text, error_color);
                {
                    if (spaceError) ImGui::Text("error - filename or terrain_path contains spaces");
                    if (fileNotFound) ImGui::Text("error - file not found, please re-link");
                    if (outOfScope) ImGui::Text("warning - file not in the same folder struture");  //needs better warning
                }
                ImGui::PopStyleColor();
                ImGui::NewLine();


                if (_ribbonBuilder.tooManyPivots)     ImGui::Text("error - more than 255 pivots");
                else
                {
                    ImGui::Text("%d instances, %d verts, %d / %d pivots ", numInstancePacked, numVertsPacked, numPivots, _ribbonBuilder.numPivots());
                }
                ImGui::PopFont();
            }
            ImGui::EndTooltip();
        }

        style.FrameBorderSize = 0;
        CLICK_PART;
        ImGui::Text("branches");

        for (auto& L : branches.branchData) { if (L.plantPtr) L.plantPtr->treeView(); }

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

#pragma optimize("", off)

// Diamond overshoots 10% inside teh shider so shrink by 10%
glm::mat4  _stemBuilder::build_2(buildSetting _settings, uint _bakeIndex, bool _faceCamera)
{
    lodBake& lB = lod_bakeInfo[_bakeIndex];
    glm::mat4 node = NODES.front();
    glm::mat4 last = NODES.back();
    if (lB.pixHeight > 0)
    {
        float w = lB.extents.x * lB.bakeWidth;
        uint mat = lB.material.index;
        //float3 dir = glm::normalize(node[1]);

        float h0 = lB.extents.y * lB.bake_V.x;
        float h1 = lB.extents.y * lB.bake_V.y;


        float3 diff = last[3] - node[3];
        float T = glm::length(diff);
        float tipLength = h1 - T;// glm::length(last[3] - node[3]);

        GROW(node, h0);
        GROW(last, tipLength);
        //GROW(last, h1);

        // ROTATE node and last so axis lines up
        float4 tangent = glm::normalize(last[3] - node[3]);
        node[1] = tangent;
        last[1] = tangent;

        //float4 tangent = glm::normalize(last[3] - node[3]);
        _ribbonBuilder.startRibbon(lB.faceCamera, _settings.pivotIndex);
        _ribbonBuilder.set(node, w, mat, float2(1.f, 1.f), 1.f, 1.f, true, 0.5f, 0.1f, 0.0f, true);
        _ribbonBuilder.set(last, w, mat, float2(1.f, 0.f), 1.f, 1.f, true, 0.5f, 0.1f, 0.0f, true);
        /*
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

        _ribbonBuilder.startRibbon(lB.faceCamera, _settings.pivotIndex);
        _ribbonBuilder.set(node, w, mat, float2(1.f, 1.f), 1.f, 1.f, true, 0.5f, 0.1f, 0.0f, true);
        _ribbonBuilder.set(last, w, mat, float2(1.f, 0.f), 1.f, 1.f, true, 0.5f, 0.1f, 0.0f, true);
        */
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

    float h0 = lB.extents.y * lB.bake_V.x;
    float h1 = lB.extents.y * lB.bake_V.y;

    float tipLength = h1 - glm::length(last[3] - node[3]);

    //float tipLength = glm::dot(tip_NODE[3] - last[3], node[1]);
    GROW(node, h0);
    GROW(last, tipLength);

    glm::vec4 step = (last[3] - node[3]) / 3.f;
    float4 binorm_step = (last[1] - node[1]) / 3.f;

    //node[3] += step * lB.bake_V.x;
    //node[1] += binorm_step * lB.bake_V.x;
    //step *= (lB.bake_V.y - lB.bake_V.x) / 3.f;
   // binorm_step *= (lB.bake_V.y - lB.bake_V.x) / 3.f;

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

        float dU = lod_bakeInfo[_bakeIndex].dU[i];
        if (lB.bake_V.x > 0.2f) dU = 1; // OR L:OOK IT UP NBETTER
        _ribbonBuilder.set(CURRENT, w * dU, mat, float2(dU, 1.f - (0.3333333f * i)), 1.f, 1.f);
        node[3] += step;
        node[1] += binorm_step;
    }

    return last;
}

// really build7
glm::mat4  _stemBuilder::build_n(buildSetting _settings, uint _bakeIndex, bool _faceCamera)
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
    step *= (lB.bake_V.y - lB.bake_V.x) / 6.f;
    binorm_step *= (lB.bake_V.y - lB.bake_V.x) / 6.f;

    _ribbonBuilder.startRibbon(lB.faceCamera, _settings.pivotIndex);

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
        float dU = glm::lerp(lod_bakeInfo[_bakeIndex].dU[duIdx], lod_bakeInfo[_bakeIndex].dU[duIdx + 1], ddu);
        _ribbonBuilder.set(CURRENT, w * dU, mat, float2(dU, 1.f - (0.16666666667f * i)), 1.f, 1.f);
        node[3] += step;
        node[1] += binorm_step;
    }

    return last;
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
            if (_addVerts)
            {
            }
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
            float halfTwist = 6.283185307f / (float)numL / 2.f;
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
                _settings.seed = oldSeed + (i * 100) + j;
                _settings.root = node;
                if (leaf_age_override)  _settings.node_age = age - i + 1;
                else                    _settings.node_age = -leafAge;
                _settings.normalized_age = t_live;
                if (branches.branchData.size() > 0 && _settings.normalized_age > 0.05f)
                {
                    float branchAge = (i / (float)age) + (j / (float)age / (float)numL);
                    _rootPlant::generator.seed(_settings.seed);
                    _randomBranch* pBranch = branches.get(branchAge);
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
    if (roll_horizontal) { ROLL_HORIZONTAL(node); }
    NODES.clear();
    NODES.push_back(node);

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
            if (lengthFromBranchAge)
            {
                float grownAge = RND_B(leaf_fully_grown_age);
                //float leafAge = 1.f - pow(i / age, leaf_age_power);
                //int numL = (int)RND_B(numLeaves);
                //float t = (float)i / age;
                //float t_live = glm::clamp((i - firstLiveSegment) / (age - firstLiveSegment), 0.f, 1.f);
                //t_live = 1.f - pow(t_live, leaf_age_power);
                float t_live = glm::clamp((iAge - i) / grownAge, 0.f, 1.f);
                L *= t_live;
            }
            float C = RND_CRV(stem_curve) / 20.f / age;
            //float P = RND_CRV(stem_phototropism) / 20.f / age;
            float t = (float)i / age;
            float W = root_width - dR * pow(t, rootPow);
            //float nodePixels = (L * 20) / _settings.pixelSize;
            //int nodeNumSegments = glm::clamp((int)(nodePixels / nodeLengthSplit), 1, 20);     // 1 for every 8 pixels, clampped
            //float segStep = 19.f / (float)nodeNumSegments;

            // Phototropism with changes on age
            float pScale = (nodeAge * stem_phototropism.y) + ((1.f - nodeAge) * (1.f - stem_phototropism.y));
            float P = pScale * stem_phototropism.x / 20.f / age * 10;


            //float cnt = 0;
            for (int j = 0; j < 20; j++)
            {
                float aspect =
                    V -= L / W * Vaspect;
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
                bool weareinthelastbit = (j > 20 - totalStep);
                if (_addVerts && visible && cnt >= totalStep && !weareinthelastbit)
                    //if (_addVerts && visible)
                {
                    //float V = (i + (float)j / 20.f) * 0.5f;
                    _ribbonBuilder.set(node, W * 0.5f, stem_Material.index, float2(1.f, V), 1.f, 1.f);
                    cnt -= totalStep;
                }

                // do very last one
                if (_addVerts && visible && j == 19)
                {
                    _ribbonBuilder.set(node, W * 0.5f, stem_Material.index, float2(1.f, V), 1.f, 1.f);
                }
            }
            NODES.push_back(node);

            // push the last one
            //?? FIXME NEEDS TO DO BETTER WITH START ETC< AND ALMOST ALWASY PUSH TO THE END _
            // THIS IS REALLY FOR LAST VISIBLE VERT BEING CLOSDE TO THE END
            if (_addVerts)
            {
                //_ribbonBuilder.set(node, W * 0.5f, stem_Material.index, float2(1.f, V), 1.f, 1.f);
            }

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

    //   for (auto& L : leaves.data)
    //   {
    //       if (L.plantPtr)  L.plantPtr->clear_build_info();
    //   }

    for (auto& L : branches.branchData)
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
    if (_addVerts)
    {
        spdlog::info("    {}", std::string(static_cast<size_t>(_settings.callDepth) * 4, ' '));
        spdlog::info("{}", this->name.c_str());
    }

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

        if (_settings.pivotDepth < 4)
        {

            _settings.pivotIndex[_settings.pivotDepth] = _ribbonBuilder.pushPivot(_settings.seed, p);
            _settings.pivotDepth += 1;
            numPivots++;


            spdlog::info("    {}", std::string(static_cast<size_t>(_settings.callDepth) * 4, ' '));
            spdlog::info("add pivot {{}}", _settings.pivotDepth);
        }
        else
        {
            spdlog::info("pivot depth exceeded");
        }
    }

    if (_addVerts)
    {
        bool found = false;
        levelOfDetail lod = lodInfo.front();             // no parent can push us past this, similar when I do sun Parts, there will be a lod set for that add
        int lodIndex = 0;
        for (int i = 0; i < lodInfo.size(); i++)
        {
            // FIXME we want random pixelSize here for smoother crossovers but
            // But this is VERY bad, it ashould take overall lengrth into account
            GLOBAL_RND_SIZE = 0;
            if (_settings.callDepth > 1) GLOBAL_RND_SIZE = 0.2f;
            if (_settings.pixelSize * RND_B(float2(1.f, GLOBAL_RND_SIZE)) <= (lodInfo[i].pixelSize * 0.001f))
            {
                lod = lodInfo[i];
                lodIndex = i;
                found = true;
            }
        }

        spdlog::info("    {}", std::string(static_cast<size_t>(_settings.callDepth) * 4, ' '));
        spdlog::info("lod - {}, settings - {:.1f}mm,lod - {:.1f}mm", lodIndex, _settings.pixelSize * 1000.f, lod.pixelSize);

        // HUHGE FUCKING WARNING IF NOT TRUE
        if (lodInfo.size() > 0)
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
                    spdlog::info("    {}", std::string(static_cast<size_t>(_settings.callDepth) * 4, ' '));
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
    if (NODES.size() > 0)       return NODES.back();    // since its only direction test with this

    return (glm::mat4(1.f));
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
        spdlog::error("File does not exists in the relative tree structure: {}", terrafectorEditorMaterial::rootFolder + path);
        //reportError(fmt::format("{}\nError: {}", "File does not exists in the relative tree structure", ""));
    }
}

void _clumpBuilder::savePath()
{
    std::ofstream os(terrafectorEditorMaterial::rootFolder + path);
    cereal::JSONOutputArchive archive(os);
    archive(*this);
    changed = false;
}




bool _clumpBuilder::renderGui()
{
    unsigned int gui_id = 1001;

    renderGuiHeader(filters);

    /*
    float W = ImGui::GetColumnWidth();
    auto& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Header] = ImVec4(0.01f, 0.01f, 0.01f, 1.0f);  // selectable
    style.WindowPadding = ImVec2(0, 0);
    ImGui::PushFont(_gui->getFont("header1"));
    {
        ImGui::BeginChildFrame(37, ImVec2(W, 50));
        ImGui::Text(name.c_str());
        ImGui::EndChildFrame();
    }
    ImGui::PopFont();
    TOOLTIP(path.c_str());

    ImGui::PushFont(_gui->getFont("header2"));
    {
        //load();
        ImGui::NewLine();
        ImGui::SameLine(W / 3, 0);
        save();
        ImGui::SameLine(W / 3 * 2, 0);
        saveas();
    }
    ImGui::PopFont();
    */

    auto& style = ImGui::GetStyle();

    ImGui::NewLine();
    if (ImGui::TreeNodeEx("Light", 0))
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
    if (ImGui::TreeNodeEx("Sway", 0))
    {
        ImGui::PushFont(_gui->getFont("default"));
        {
            CHECKBOX("hasPivot", &hasPivot, "");
            R_FLOAT("stiffness", ossilation_stiffness, 0.1f, 0.8f, 20.f, "");
            R_FLOAT("sqrt(sway)", ossilation_constant_sqrt, 0.1f, 1.01f, 100.f, "");
        }
        ImGui::PopFont();
        ImGui::TreePop();
    }

    if (ImGui::BeginPopupContextWindow(false))
    {
        if (ImGui::Selectable("insert clump")) { clumps.emplace_back(); }
        ImGui::EndPopup();
    }

    for (int i = 0; i < clumps.size(); i++)
    {
        std::string name = "clump - " + std::to_string(i);
        ImGui::NewLine();
        if (ImGui::TreeNodeEx(name.c_str(), 0))
        {
            ImGui::PushFont(_gui->getFont("default"));
            {
                if (ImGui::BeginPopupContextWindow(false))
                {
                    if (ImGui::Selectable("delete this")) { clumps.erase(clumps.begin() + i); }
                    if (ImGui::Selectable("insert before")) { clumps.emplace(clumps.begin() + i); }
                    ImGui::EndPopup();
                }
                // ... submit contents ...
                ImGui::Text("root");
                ImGui::SameLine(0, 10);
                float a = sqrt(clumps[i].aspect.x);
                ImGui::Text("%2.3f, %2.3fm", clumps[i].size.x * a, clumps[i].size.x / a);

                R_LENGTH_EX("size", clumps[i].size, 0.001f, 0.0001f, 1.f, "circle radius in meters", "random");
                R_LENGTH_EX("aspect", clumps[i].aspect, 0.01f, 0.01f, 10.f, "xz aspect ratio", "random");


                ImGui::NewLine();
                ImGui::Text("children");
                ImGui::SameLine(0, 10);
                ImGui::Text("%d added", ROOTS.size());

                if (ImGui::Checkbox("radial", &clumps[i].radial)) changed = true;
                TOOLTIP("radial will rotate elements to the sides, and set age from the inside out\noff is linear, will set age and angle in the X direction");
                R_LENGTH("numChildren", clumps[i].numChildren, "stem length in mm", "random");
                R_LENGTH_EX("age", clumps[i].child_age, 0.1f, 1.0f, 100.0f, "age of the children", "random");
                R_CURVE("child_angle", clumps[i].child_angle, "angle of the sub element relative to middle of clump", "change of angle as it ages, usually drooping");
                R_CURVE("random", clumps[i].child_rnd, "randomness in the angles", "random");
                R_FLOAT("age_power", clumps[i].child_age_power, 0.1f, 1.f, 25.f, "");
                ImGui::GetStyle().Colors[ImGuiCol_Header] = leaf_color;
                clumps[i].children.renderGui("children", gui_id);
            }
            ImGui::PopFont();
            ImGui::TreePop();
        }
    }
    /*
    ImGui::NewLine();
    if (ImGui::TreeNodeEx("Root", 0))
    {
        ImGui::PushFont(_gui->getFont("default"));
        {
            ImGui::SameLine(0, 10);
            float a = sqrt(aspect.x);
            ImGui::Text("%2.3f, %2.3fm", size.x * a, size.x / a);

            R_LENGTH_EX("size", size, 0.001f, 0.0001f, 1.f, "circle radius in meters", "random");
            R_LENGTH_EX("aspect", aspect, 0.01f, 0.01f, 10.f, "xz aspect ratio", "random");
        }
        ImGui::PopFont();
        ImGui::TreePop();
    }


    // children
    ImGui::NewLine();
    if (ImGui::TreeNodeEx("Children", 0))
    {
        ImGui::PushFont(_gui->getFont("default"));
        {
            ImGui::SameLine(0, 10);
            ImGui::Text("%d added", ROOTS.size());

            if (ImGui::Checkbox("radial", &radial)) changed = true;
            TOOLTIP("radial will rotate elements to the sides, and set age from the inside out\noff is linear, will set age and angle in the X direction");
            R_LENGTH("numChildren", numChildren, "stem length in mm", "random");
            R_LENGTH_EX("age", child_age, 0.1f, 1.0f, 100.0f, "age of the children", "random");
            R_CURVE("child_angle", child_angle, "angle of the sub element relative to middle of clump", "change of angle as it ages, usually drooping");
            R_CURVE("random", child_rnd, "randomness in the angles", "random");
            R_FLOAT("age_power", child_age_power, 0.1f, 1.f, 25.f, "");
            ImGui::GetStyle().Colors[ImGuiCol_Header] = leaf_color;
            children.renderGui("children", gui_id);
        }
        ImGui::PopFont();
        ImGui::TreePop();
    }
    */

    changedForSave |= changed;
    anyChange |= changed;
    bool RET = changed;
    changed = false;
    return RET;
}


void _clumpBuilder::treeView()
{
    auto& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Header] = stem_color;
    int flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_OpenOnArrow;
    //int flags = ImGuiTreeNodeFlags_Leaf;

    if (_rootPlant::selectedPart == this) {
        style.FrameBorderSize = 3;
        flags = flags | ImGuiTreeNodeFlags_Selected;
        style.Colors[ImGuiCol_Border] = ImVec4(0.7f, 0.7f, 0.7f, 1);
    }

    if (changedForSave) style.Colors[ImGuiCol_Header] = selected_color;

    bool spaceError = path.find(" ") != std::string::npos;
    bool outOfScope = path.find(_rootPlant::root_path) == std::string::npos;

    if (fileNotFound || spaceError)
    {
        style.Colors[ImGuiCol_Header] = error_color;
    }
    else if (outOfScope)
    {
        style.Colors[ImGuiCol_Header] = outofscope_color;
    }

    TREE_LINE(name.c_str(), path.c_str(), flags)
    {
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            {
                ImGui::PushFont(_gui->getFont("header1"));
                ImGui::Text(path.c_str());
                ImGui::PushStyleColor(ImGuiCol_Text, error_color);
                {
                    if (spaceError) ImGui::Text("error - filename or terrain_path contains spaces");
                    if (fileNotFound) ImGui::Text("error - file not found, please re-link");
                    if (outOfScope) ImGui::Text("warning - file not in the same folder struture");  //needs better warning
                }
                ImGui::PopStyleColor();
                ImGui::NewLine();


                if (_ribbonBuilder.tooManyPivots)     ImGui::Text("error - more than 255 pivots");
                else
                {
                    ImGui::Text("%d instances, %d verts, %d / %d pivots ", numInstancePacked, numVertsPacked, numPivots, _ribbonBuilder.numPivots());
                }
                ImGui::PopFont();
            }
            ImGui::EndTooltip();
        }

        style.FrameBorderSize = 0;
        CLICK_PART;
        ImGui::Text("children");
        for (auto& C : clumps)
        {
            for (auto& L : C.children.data) { if (L.plantPtr) L.plantPtr->treeView(); }
        }
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
    if (!_addVerts) _settings.pixelSize = 0.0001f;   // do detail so we preserve teh tip in calcyulations

    bool forcePHOTOTROPY = _settings.isBaking;
    glm::mat4 ORIGINAL = _settings.root;

    int oldSeed = _settings.seed;
    std::mt19937 MT(_settings.seed + 999);
    _rootPlant::generator.seed(_settings.seed + 333);

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
            float c_rnd = d_1_1(MT) * CLUMP.child_rnd.x;
            float c_rndp = d_1_1(MT) * CLUMP.child_rnd.x;
            float c_age = RND_B(CLUMP.child_age);
            float c_rot = atan2(rndPos.x, rndPos.y);
            float t = l;     // center to edge
            float t_live = 1.f - pow(t, CLUMP.child_age_power);

            if (CLUMP.radial)
            {
                ROLL(node, c_rot + 3.14f);
                PITCH(node, CLUMP.child_angle.x + (-CLUMP.child_angle.y * l));  // add rnd
                //PITCH(node, c_angle * t_live + c_rndp);  // add rnd
                YAW(node, c_rnd);
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
            _settings.seed = oldSeed + i;
            _settings.root = node;
            _settings.node_age = c_age;
            _settings.normalized_age = t_live;
            _plantRND CHILD = CLUMP.children.get();
            glm::mat4 TIP;
            if (CHILD.plantPtr) TIP = CHILD.plantPtr->build(_settings, _addVerts);

            tipDistance = __max(tipDistance, glm::dot((float3)TIP[3] - (float3)ORIGINAL[3], CENTER_DIR));
        }
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

    if (hasPivot && _addVerts)
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
        _settings.pivotDepth += 1;
        numPivots++;
    }


    if (_addVerts)
    {
        levelOfDetail lod = lodInfo.back();             // no parent can push us past this, similar when I do sun Parts, there will be a lod set for that add
        for (int i = lodInfo.size() - 1; i >= 0; i--)
        {
            if (_settings.pixelSize >= (lodInfo[i].pixelSize * 0.001f))
            {
                lod = lodInfo[i];
            }
        }

        if (!_settings.isBaking)
        {
            if (lod.bakeType == BAKE_DIAMOND)       build_2(_settings, lod.bakeIndex, true);
            if (lod.bakeType == BAKE_4)             build_4(_settings, lod.bakeIndex, true);
            //if (lod.bakeType == BAKE_N)             build_n(_settings, lod.bakeIndex, true);    // FIXME
        }

        if (lod.useGeometry || _settings.isBaking)
        {
            buildChildren(_settings, true);
        }

        uint numVerts = _ribbonBuilder.numVerts() - startVerts;
        if (numVerts > 0) numInstancePacked++;
        numVertsPacked = 0;// clumps dfont have any of their own geometry += numVerts;
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
        //_ribbonBuilder.set(node, w * 0.8f, mat, float2(0.8f, 0.f), 1.f, 1.f, true, 0.5f, 0.1f, 0.0f, true);  // only 60% width, save pixels but test
        //_ribbonBuilder.set(last, w * 0.8f, mat, float2(0.8f, 1.f), 1.f, 1.f, true, 0.5f, 0.1f, 0.0f, true);
        w = 0.1f;
        _ribbonBuilder.set(node, w, mat, float2(1.0f, 1.f), 1.f, 1.f);
        _ribbonBuilder.set(last, w, mat, float2(1.0f, 0.f), 1.f, 1.f);
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

    if (keyPressed)
    {
        if (keyEvent.key == Input::Key::T)
        {
            textureTool = !textureTool;
        }
    }

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


        if (keyEvent.key == Input::Key::Down)
        {
            currentLOD++;       // ENTER
            //??? clamh how
            anyChange = true;
            levelOfDetail* lodInfo = selectedPart->getLodInfo(currentLOD);
            if (lodInfo)
            {
                //lodInfo->pixelSize = extents.y / lodInfo->numPixels;
                settings.pixelSize = lodInfo->pixelSize * 0.001f;
            }
            else
            {
                currentLOD--;
            }
        }
        if (keyEvent.key == Input::Key::Up)
        {
            currentLOD--;       // ENTER
            if (currentLOD < 0) currentLOD = 0;
            anyChange = true;
            levelOfDetail* lodInfo = selectedPart->getLodInfo(currentLOD);
            //lodInfo->pixelSize = extents.y / lodInfo->numPixels;
            settings.pixelSize = lodInfo->pixelSize * 0.001f;
        }
        /*
        if (keyEvent.key == Input::Key::X)
        {
            showDebugInShader = !showDebugInShader;
        }
        */
        if (keyEvent.key == Input::Key::M)
        {
            showMaterialsPanel = !showMaterialsPanel;
        }
        if (keyEvent.key == Input::Key::N)
        {
            showLargePanel = !showLargePanel;
        }
        if (keyEvent.key == Input::Key::B)
        {
            bakingView = !bakingView;
            if (bakingView) buildFullResolution();
        }



        if (selectedPart)
        {
            if (keyEvent.key == Input::Key::S && keyEvent.hasModifier(Input::Modifier::Ctrl))
            {
                //selectedPart->renderGui();  // now render the selected item.
                selectedPart->savePath();
                selectedPart->changedForSave = false;
            }
        }

        if (keyEvent.key == Input::Key::W && keyEvent.hasModifier(Input::Modifier::Ctrl))
        {
            if (render_Normal)
            {
                render_Normal = false;
                render_Clip = true;
                render_Pivot = false;
                reloadShader();
            }
            else
            {
                render_Normal = true;
                render_Clip = false;
                render_Pivot = false;
                reloadShader();
            }

        }




        return true;
    }
    return false;
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
    varVegTextures = (*vegetationShader.Vars())["textures_T"];

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
    // compute
    //split.compute_tileClear.Vars()->setBuffer("DrawArgs_ClippedLoddedPlants", split.drawArgs_clippedloddedplants);


    compute_clearBuffers.load("Samples/Earthworks_4/hlsl/terrain/compute_vegetation_clear.hlsl");
    compute_clearBuffers.Vars()->setBuffer("DrawArgs_Quads", drawArgs_billboards);
    compute_clearBuffers.Vars()->setBuffer("DrawArgs_Plants", drawArgs_vegetation);
    //compute_clearBuffers.Vars()->setBuffer("feedback", buffer_feedback);
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


    compute_sortCombine.load("Samples/Earthworks_4/hlsl/terrain/compute_vegetation_sortCombine.hlsl");
    compute_sortCombine.Vars()->setBuffer("DrawArgs_Plants", drawArgs_vegetation);
    compute_sortCombine.Vars()->setBuffer("pre_block_buffer", blockData_preSort);
    compute_sortCombine.Vars()->setBuffer("post_block_buffer", blockData);
    compute_sortCombine.Vars()->setBuffer("feedback", buffer_feedback);
    compute_sortCombine.Vars()->setBuffer("sort", buffer_gpuSort);




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





    vegetationShader_GOURAUD.add("_GOURAUD_SHADING", "");
    vegetationShader_GOURAUD.load("Samples/Earthworks_4/hlsl/terrain/render_vegetation_ribbons.hlsl", "vsMain", "psMain", Vao::Topology::LineStrip, "gsMain");
    vegetationShader_GOURAUD.Vars()->setBuffer("plant_buffer", plantData);
    vegetationShader_GOURAUD.Vars()->setBuffer("plant_pivot_buffer", plantpivotData);
    vegetationShader_GOURAUD.Vars()->setBuffer("instance_buffer", instanceData);
    vegetationShader_GOURAUD.Vars()->setBuffer("block_buffer", blockData);
    vegetationShader_GOURAUD.Vars()->setBuffer("vertex_buffer", vertexData);
    vegetationShader_GOURAUD.Vars()->setBuffer("materials", _plantMaterial::static_materials_veg.sb_vegetation_Materials);
    vegetationShader_GOURAUD.Vars()->setSampler("gSmpLinear", sampler_Ribbons);              // fixme only cvlamlX
    vegetationShader_GOURAUD.Vars()->setSampler("gSamplerDepth", sampler_Depth);
    vegetationShader_GOURAUD.Vars()->setSampler("gSmpLinearClamp", sampler_ClampAnisotropic);              // fixme only cvlamlX
    vegetationShader_GOURAUD.Vars()->setTexture("highResShadow", shadowFbo->getDepthStencilTexture());
    varTextures_Gauraud = (*vegetationShader_GOURAUD.Vars())["textures_T"];
    /*
        vegetationShader_DEBUG_PIVOTS.add("_DEBUG_PIVOTS", "");
        vegetationShader_DEBUG_PIVOTS.load("Samples/Earthworks_4/hlsl/terrain/render_vegetation_ribbons.hlsl", "vsMain", "psMain", Vao::Topology::LineStrip, "gsMain");
        vegetationShader_DEBUG_PIVOTS.Vars()->setBuffer("plant_buffer", plantData);
        vegetationShader_DEBUG_PIVOTS.Vars()->setBuffer("plant_pivot_buffer", plantpivotData);
        vegetationShader_DEBUG_PIVOTS.Vars()->setBuffer("instance_buffer", instanceData);
        vegetationShader_DEBUG_PIVOTS.Vars()->setBuffer("block_buffer", blockData);
        vegetationShader_DEBUG_PIVOTS.Vars()->setBuffer("vertex_buffer", vertexData);
        vegetationShader_DEBUG_PIVOTS.Vars()->setBuffer("materials", _plantMaterial::static_materials_veg.sb_vegetation_Materials);
        vegetationShader_DEBUG_PIVOTS.Vars()->setSampler("gSmpLinear", sampler_Ribbons);              // fixme only cvlamlX
        vegetationShader_DEBUG_PIVOTS.Vars()->setSampler("gSmpLinearClamp", sampler_ClampAnisotropic);              // fixme only cvlamlX
        vegetationShader_DEBUG_PIVOTS.Vars()->setTexture("highResShadow", shadowFbo->getDepthStencilTexture());
        auto& blockPivots = vegetationShader_DEBUG_PIVOTS.Vars()->getParameterBlock("textures");
        varTextures_Debug_Pivots = blockPivots->findMember("T");

        vegetationShader_DEBUG_PIXELS.add("_DEBUG_PIXELS", "");
        vegetationShader_DEBUG_PIXELS.load("Samples/Earthworks_4/hlsl/terrain/render_vegetation_ribbons.hlsl", "vsMain", "psMain", Vao::Topology::LineStrip, "gsMain");
        vegetationShader_DEBUG_PIXELS.Vars()->setBuffer("plant_buffer", plantData);
        vegetationShader_DEBUG_PIXELS.Vars()->setBuffer("plant_pivot_buffer", plantpivotData);
        vegetationShader_DEBUG_PIXELS.Vars()->setBuffer("instance_buffer", instanceData);
        vegetationShader_DEBUG_PIXELS.Vars()->setBuffer("block_buffer", blockData);
        vegetationShader_DEBUG_PIXELS.Vars()->setBuffer("vertex_buffer", vertexData);
        vegetationShader_DEBUG_PIXELS.Vars()->setBuffer("materials", _plantMaterial::static_materials_veg.sb_vegetation_Materials);
        vegetationShader_DEBUG_PIXELS.Vars()->setSampler("gSmpLinear", sampler_Ribbons);              // fixme only cvlamlX
        vegetationShader_DEBUG_PIXELS.Vars()->setSampler("gSmpLinearClamp", sampler_ClampAnisotropic);              // fixme only cvlamlX
        vegetationShader_DEBUG_PIXELS.Vars()->setTexture("highResShadow", shadowFbo->getDepthStencilTexture());
        auto& blockPixels = vegetationShader_DEBUG_PIXELS.Vars()->getParameterBlock("textures");
        varTextures_Debug_Pixels = blockPixels->findMember("T");
    */
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
    varTextures_RGBSample = (*vegetationShader_RGB_SAMPLE.Vars())["textures_T"];

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
    varTextures_Depth = (*vegetationShader_DEPTH.Vars())["textures_T"];



    billboardShader.add("_BILLBOARD", "");
    billboardShader.load("Samples/Earthworks_4/hlsl/terrain/render_vegetation_ribbons.hlsl", "vsMain", "psMain", Vao::Topology::PointList, "gsMain");
    billboardShader.Vars()->setBuffer("plant_buffer", plantData);
    billboardShader.Vars()->setBuffer("instance_buffer", instanceData_Billboards);
    billboardShader.Vars()->setBuffer("block_buffer", blockData);
    billboardShader.Vars()->setBuffer("vertex_buffer", vertexData);
    billboardShader.Vars()->setBuffer("materials", _plantMaterial::static_materials_veg.sb_vegetation_Materials);
    billboardShader.Vars()->setSampler("gSmpLinear", sampler_Ribbons);              // fixme only cvlamlX
    billboardShader.Vars()->setSampler("gSmpLinearClamp", sampler_ClampAnisotropic);              // fixme only cvlamlX
    varBBTextures = (*billboardShader.Vars())["textures_T"];

    bakeShader.add("_BAKE", "");
    bakeShader.load("Samples/Earthworks_4/hlsl/terrain/render_vegetation_ribbons.hlsl", "vsMain", "psMain", Vao::Topology::LineStrip, "gsMain");
    bakeShader.Vars()->setBuffer("plant_buffer", plantData);
    bakeShader.Vars()->setBuffer("instance_buffer", instanceData);
    bakeShader.Vars()->setBuffer("block_buffer", blockData);
    bakeShader.Vars()->setBuffer("vertex_buffer", vertexData);
    bakeShader.Vars()->setBuffer("materials", _plantMaterial::static_materials_veg.sb_vegetation_Materials);
    bakeShader.Vars()->setSampler("gSmpLinear", sampler_Ribbons);              // fixme only cvlamlX
    varBakeTextures = (*bakeShader.Vars())["textures_T"];

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
        sum += perlinData[i];
    }

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
    ImGui::Separator();
    {
        //            timeAvs += 0.01f * gpFramework->getFrameRate().getAverageFrameTime();
        ImGui::Text("  gpu - plants %1.3fms", gputime);

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

        static float clipAvs;
        static float passAvs;

        clipAvs *= 0.91f;
        clipAvs = (float)feedback.numPixClip;// *0.09f;

        passAvs *= 0.91f;
        passAvs = (float)feedback.numPixPass;// *0.09f;

        int sum = (int)clipAvs + (int)passAvs;
        if (sum < 1000)
        {
            ImGui::Text("pixels %2.2f, %2.2f ", clipAvs, passAvs);
        }
        else if (sum < 1000000)
        {
            ImGui::Text("pixels %2.2fk, %2.2fk", clipAvs / 1024, passAvs / 1024);
        }
        else
        {
            ImGui::Text("pixels %2.2fM, %2.2fM", clipAvs / 1024 / 1024, passAvs / 1024 / 1024);
        }

        if (gputime > 0)
        {

            float pixelsPerMS = sum / gputime / 1000000;
            ImGui::SameLine(0, 30);
            ImGui::Text("%2.1fMpx/ms", pixelsPerMS);
        }

        ImGui::Text("instances (%d / %d)", feedback.numInstAdded, feedback.numInstAdded + feedback.numInstRejected);


        
    }
    ImGui::PopFont();

    if (displayModeSinglePlant)
    {

        //ImGui::Text("%2.2f M tris", (float)totalBlocksToRender * VEG_BLOCK_SIZE * 2.f / 1000000.f);
    }
    else
    {
        ImGui::NewLine();
        ImGui::PushFont(_gui->getFont("header2"));
        {
            R_FLOAT("lod Bias", loddingBias, 0.01f, 0.01f, 10.f, "");
        }
        ImGui::PopFont();

        ImGui::Text("plantZero, %2.2fpix - lod %d", feedback.plantZero_pixeSize, feedback.plantZeroLod);

        {
            

            ImGui::NewLine();
            ImGui::SameLine(190);
            if (ImGui::Button("show - all"))
            {
                firstLod = -1;
            }

            ImGui::NewLine();
            ImGui::Text("0:  %6d      gpu - %1.3fms   ", feedback.numLod[0], gputimeBB);

            for (int i = 1; i < 10; i++)
            {
                if (root)
                {
                    if (root->getLodInfo(i))
                    {
                        ImGui::Text("%2d:", i);
                        ImGui::SameLine(30);
                        ImGui::Text("%6d", feedback.numLod[i]);
                        TOOLTIP("#");
                        ImGui::SameLine(100);
                        ImGui::Text("%3d", root->getLodInfo(i)->numBlocks);
                        TOOLTIP("blocks per plant");
                        ImGui::SameLine(150);
                        ImGui::Text("%3d", root->getLodInfo(i)->numVerts);
                        TOOLTIP("verts per plant");
                        ImGui::SameLine(200);
                        ImGui::Text("%2.2f", feedback.numLod[i] * root->getLodInfo(i)->numVerts * 2 / 1000000.f);
                        std::string tt = std::to_string(feedback.numLod[i] * root->getLodInfo(i)->numVerts * 2) + " tri's";
                        TOOLTIP(tt.c_str());

                        ImGui::SameLine(240);
                        std::string name = "show##" + std::to_string(i);
                        if (ImGui::Button(name.c_str()))
                        {
                            firstLod = i - 1;   // offset -1 because the billboard  (lod-0) is not oin the final export
                        }
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

    /*
    auto& style = ImGui::GetStyle();
    style.FrameBorderSize = 1;
    ImGui::NewLine();
    ImGui::Checkbox("show debug colours", &showDebugInShader);
    ImGui::Checkbox("show pivots", &showNumPivots);
    style.FrameBorderSize = 0;
    */

    ImGui::NewLine();
    ImGui::Text("use to limit plants and lods to display ???");
    ImGui::DragInt2("plants", &firstPlant, 0.1f, 0, 1000);
    TOOLTIP("0 displays all\n1 or above only shows the central plant");
    //ImGui::DragInt("lods", &firstLod, 0.1f, -1, 1000);
}

void _rootPlant::renderGui_lodbake(Gui* _gui)
{
}

void _rootPlant::renderGui_other(Gui* _gui)
{
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




void _rootPlant::renderGui_rightPanel(Gui* _gui)
{
    float W = ImGui::GetColumnWidth();

    renderGui_load(_gui);



    {

        ImGui::PushFont(_gui->getFont("default"));
        {
            auto& style = ImGui::GetStyle();
            style.Colors[ImGuiCol_Button] = ImVec4(0.0f, 0.04f, 0.04f, 1.0f);
            if (showMaterialsPanel) style.Colors[ImGuiCol_Button] = ImVec4(0.1f, 0.2f, 0.0f, 1.0f);
            if (ImGui::Button("materials 'm'", ImVec2(W * 0.32f, 0))) {
                showMaterialsPanel = !showMaterialsPanel;
            }

            style.Colors[ImGuiCol_Button] = ImVec4(0.0f, 0.04f, 0.04f, 1.0f);
            if (showLargePanel) style.Colors[ImGuiCol_Button] = ImVec4(0.1f, 0.2f, 0.0f, 1.0f);
            ImGui::SameLine(0, 10);
            if (ImGui::Button("details 'n'", ImVec2(W * 0.32f, 0))) {
                showLargePanel = !showLargePanel;
            }

            style.Colors[ImGuiCol_Button] = ImVec4(0.0f, 0.04f, 0.04f, 1.0f);
            if (bakingView) style.Colors[ImGuiCol_Button] = ImVec4(0.1f, 0.2f, 0.0f, 1.0f);
            //if (textureTool) style.Colors[ImGuiCol_Button] = ImVec4(0.01f, 0.01f, 0.0f, 1.0f);
            ImGui::SameLine(0, 10);
            if (ImGui::Button("baking", ImVec2(W * 0.32f, 0))) {
                bakingView = !bakingView;
                if (bakingView) buildFullResolution();
            }
            TOOLTIP("'b'");
        }
        ImGui::PopFont();
    }


    ImGui::PushFont(_gui->getFont("default"));
    {
        auto& style = ImGui::GetStyle();
        {
            style.Colors[ImGuiCol_Button] = ImVec4(0.0f, 0.15f, 0.15f, 0.0f);
            if (render_Normal) style.Colors[ImGuiCol_Button] = ImVec4(0.3f, 0.3f, 0.0f, 1.0f);
            if (ImGui::Button("normal", ImVec2(W * 0.32f, 0))) {
                render_Normal = true;
                render_Clip = false;
                render_Pivot = false;
                reloadShader();
            }

            ImGui::SameLine(0, 10);
            style.Colors[ImGuiCol_Button] = ImVec4(0.16f, 0.05f, 0.0f, 0.0f);
            if (render_Clip) style.Colors[ImGuiCol_Button] = ImVec4(0.3f, 0.3f, 0.0f, 1.0f);
            if (ImGui::Button("clipping", ImVec2(W * 0.32f, 0))) {
                render_Normal = false;
                render_Clip = true;
                render_Pivot = false;
                reloadShader();
            }

            ImGui::SameLine(0, 10);
            style.Colors[ImGuiCol_Button] = ImVec4(0.16f, 0.05f, 0.0f, 0.0f);
            if (render_ZOnly) style.Colors[ImGuiCol_Button] = ImVec4(0.3f, 0.3f, 0.0f, 1.0f);
            if (ImGui::Button("Z-only", ImVec2(W * 0.32f, 0))) { render_ZOnly = !render_ZOnly;   reloadShader(); }
        }

        {
            style.Colors[ImGuiCol_Button] = ImVec4(0.16f, 0.05f, 0.0f, 0.0f);
            if (render_PixelCount) style.Colors[ImGuiCol_Button] = ImVec4(0.3f, 0.3f, 0.0f, 1.0f);
            if (ImGui::Button("pix_count", ImVec2(W * 0.32f, 0))) { render_PixelCount = !render_PixelCount;   reloadShader(); }


            ImGui::SameLine(0, 10);
            style.Colors[ImGuiCol_Button] = ImVec4(0.16f, 0.05f, 0.0f, 0.0f);
            if (render_EarlyZ) style.Colors[ImGuiCol_Button] = ImVec4(0.3f, 0.3f, 0.0f, 1.0f);
            if (ImGui::Button("early-Z", ImVec2(W * 0.32f, 0))) { render_EarlyZ = !render_EarlyZ;   reloadShader(); }

            ImGui::SameLine(0, 10);
            style.Colors[ImGuiCol_Button] = ImVec4(0.16f, 0.05f, 0.0f, 0.0f);
            if (render_FrontToback) style.Colors[ImGuiCol_Button] = ImVec4(0.3f, 0.3f, 0.0f, 1.0f);
            if (ImGui::Button("front->back", ImVec2(W * 0.32f, 0))) { render_FrontToback = !render_FrontToback; reloadShader(); }

        }

        {
            style.Colors[ImGuiCol_Button] = ImVec4(0.16f, 0.05f, 0.0f, 0.0f);
            if (render_Pivot) style.Colors[ImGuiCol_Button] = ImVec4(0.3f, 0.3f, 0.0f, 1.0f);
            if (ImGui::Button("pivots", ImVec2(W * 0.32f, 0))) {
                render_Normal = false;
                render_Clip = false;
                render_Pivot = true;
                reloadShader();
            }

            ImGui::SameLine(0, 10);
            style.Colors[ImGuiCol_Button] = ImVec4(0.16f, 0.05f, 0.0f, 0.0f);
            if (render_alphaBlend) style.Colors[ImGuiCol_Button] = ImVec4(0.3f, 0.3f, 0.0f, 1.0f);
            if (ImGui::Button("alphablend", ImVec2(W * 0.32f, 0))) { render_alphaBlend = !render_alphaBlend;   reloadShader(); }
        }




        style.Colors[ImGuiCol_Button] = ImVec4(0.16f, 0.05f, 0.0f, 0.0f);
        ImGui::PopFont();
    }

    if (root)
    {
        pixelmm = extents.y * 1000.f / pixelSize;
        for (uint lod = 0; lod < 100; lod++)   // backwards since it fixes abug in build()
        {
            levelOfDetail* lodInfo = root->getLodInfo(lod);
            if (lodInfo && (pixelmm < lodInfo->pixelSize))
            {
                expectedLod = lod;
            }
        }


        ImGui::NewLine();
        float W = ImGui::GetColumnWidth();
        auto& style = ImGui::GetStyle();
        style.Colors[ImGuiCol_Header] = ImVec4(0.001f, 0.001f, 0.001f, 1.0f);  // selectable
        style.WindowPadding = ImVec2(0, 0);
        ImGui::PushFont(_gui->getFont("header1"));
        {
            ImGui::BeginChildFrame(37, ImVec2(W, 60));
            ImGui::Text(root->name.c_str());
            ImGui::EndChildFrame();
        }
        ImGui::PopFont();

        if (root)
        {
            root->treeView();
            //style.FrameBorderSize = 0;
        }

        {
            style.Colors[ImGuiCol_Button] = ImVec4(0.01f, 0.01f, 0.01f, 1.0f);
            ImGui::NewLine();
            ImGui::PushFont(_gui->getFont("default"));
            {
                if (ImGui::Button("build all lods", ImVec2(W * 0.48f, 0))) {
                    buildAllLods();
                }

                ImGui::SameLine(0, 10);
                if (ImGui::Button("build single", ImVec2(W * 0.48f, 0))) {
                    buildFullResolution();
                }

                ImGui::NewLine();
                ImGui::SameLine(W / 2, 0);
                ImGui::Text("build time %2.2f ms", buildTime);

            }
            ImGui::PopFont();
        }


        //ImGui::Separator();
        ImGui::PushFont(_gui->getFont("header2"));
        {
            ImGui::NewLine();
            ImGui::SameLine(W / 2, 0);
            if (extents.y > 1.0f)
            {
                ImGui::Text("( %3.2f, %3.2f )m", extents.x, extents.y);
            }
            else
            {
                ImGui::Text("( %3.2f, %3.2f )cm", extents.x * 100, extents.y * 100);
            }
            ImGui::Text("%d pix,  %3.1f mm/pixel", (int)pixelSize, 1000.f * pixelmm);
            ImGui::Text("lod %d  {exp - %d},  %d verts", currentLOD, expectedLod, totalBlocksToRender * VEG_BLOCK_SIZE);
        }
        ImGui::PopFont();


        ImGui::NewLine();




    }









    ImGui::NewLine();
    renderGui_perf(_gui);


    auto& style = ImGui::GetStyle();

    ImGui::NewLine();

    style.Colors[ImGuiCol_Header] = ImVec4(0.01f, 0.01f, 0.03f, 1.f);
    if (ImGui::TreeNodeEx("Wind", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_OpenOnArrow))
    {
        /*
        if (ImGui::DragFloat3("wind dir", &windDir.x, 0.01f, -1.f, 1.f))
        {
            windDir.y = 0;
            windDir = glm::normalize(windDir);
        }*/
        ImGui::DragFloat("strength", &windStrength, 0.1f, 0.f, 100.f, "%3.2fm/s");
        ImGui::SameLine(0, 20);
        ImGui::Text("%3.2f km/h", windStrength * 3.6f);

        ImGui::TreePop();
    }

    style.Colors[ImGuiCol_Header] = ImVec4(0.03f, 0.03f, 0.01f, 1.f);
    if (ImGui::TreeNodeEx("Plant spacing", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_OpenOnArrow))
    {
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

        ImGui::TreePop();
    }


    style.Colors[ImGuiCol_Header] = ImVec4(0.01f, 0.03f, 0.01f, 1.f);
    //ImGuiTreeNodeFlags_DefaultOpen
    if (ImGui::TreeNodeEx("Vegetation BDRF", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_OpenOnArrow))
    {
        ImGui::DragFloat("camRot", &camRot, 0.01f, 0.f, 3.14159265f, "%3.2fm");
        ImGui::DragFloat("camPitch", &camPitch, 0.01f, 0.f, 1.5707963267f, "%3.2fm");
        ImGui::DragFloat("shadowPitch", &shadowPitch, 0.01f, 0.f, 1.5707963267f, "%3.2fm");
        if (ImGui::Button("bakeRGB")) { buildOneMap(shadowPitch); }
        if (ImGui::Button("buildBDRF")) { buildBDRF(); }
        camRot = 0;
        camPitch = 0;
        shadowPitch = 0;

        ImGui::TreePop();
    }


}

void _rootPlant::renderGui_load(Gui* _gui)
{
    float W = ImGui::GetColumnWidth();
    ImGui::PushFont(_gui->getFont("header2"));
    {
        if (ImGui::Button("Load", ImVec2(W * 0.3f, 0))) {
            std::filesystem::path filepath;
            FileDialogFilterVec filters = { {"leaf"}, {"stem"}, {"clump"}, {"flower"}, {"tree"} };
            if (openFileDialog(filters, filepath))
            {
                _rootPlant::selectedMaterial = nullptr;
                if (root) delete root;
                if (filepath.string().find(".leaf") != std::string::npos) { root = new _leafBuilder;  _rootPlant::selectedPart = root; }
                if (filepath.string().find(".stem") != std::string::npos) { root = new _stemBuilder;  _rootPlant::selectedPart = root; }
                if (filepath.string().find(".clump") != std::string::npos) { root = new _clumpBuilder;  _rootPlant::selectedPart = root; }
                if (filepath.string().find(".flower") != std::string::npos) { root = new _flowerBuilder;  _rootPlant::selectedPart = root; }
                if (filepath.string().find(".tree") != std::string::npos) { root = new _treeBuilder;  _rootPlant::selectedPart = root; }

                if (root)
                {
                    root->path = materialCache::getRelative(filepath.string());
                    root->name = filepath.filename().string();
                    root->loadPath();
                    buildFullResolution();
                    extents = root->calculate_extents(bakeViewAdjusted);
                    builInstanceBuffer();
                    root_path = materialCache::getRelative(filepath.parent_path().string());
                }
            }
        }
        ImGui::SameLine(0, 10);
        if (ImGui::Button("import", ImVec2(W * 0.3f, 0)))
        {
            importBinary();
        }
        ImGui::SameLine(0, 10);
        if (ImGui::Button("clearBinary", ImVec2(W * 0.3f, 0)))
        {
            numBinaryPlants = 0;
            binVertexOffset = 0;
            binPlantOffset = 0;
            binPivotOffset = 0;
            builInstanceBuffer();
        }
    }
    ImGui::PopFont();


    if (ImGui::Button("new Clump", ImVec2(W * 0.23f, 0))) { if (root) delete root; root = new _clumpBuilder;  _rootPlant::selectedPart = root; _rootPlant::selectedMaterial = nullptr; }
    ImGui::SameLine(0, 10);
    if (ImGui::Button("new Stem", ImVec2(W * 0.23f, 0))) { if (root) delete root; root = new _stemBuilder;  _rootPlant::selectedPart = root; _rootPlant::selectedMaterial = nullptr; }
    ImGui::SameLine(0, 10);
    if (ImGui::Button("new Flower", ImVec2(W * 0.23f, 0))) { if (root) delete root; root = new _flowerBuilder;  _rootPlant::selectedPart = root; _rootPlant::selectedMaterial = nullptr; }
    ImGui::SameLine(0, 10);
    if (ImGui::Button("new Leaf", ImVec2(W * 0.23f, 0))) { if (root) delete root; root = new _leafBuilder;  _rootPlant::selectedPart = root; _rootPlant::selectedMaterial = nullptr; }

    if (ImGui::Button("new Tree", ImVec2(W * 0.23f, 0))) { if (root) delete root; root = new _treeBuilder;  _rootPlant::selectedPart = root; _rootPlant::selectedMaterial = nullptr; }


    //ImGui::NewLine();
}


void _rootPlant::renderGui_Lodding(Gui* _gui)
{
    float columnWidth = ImGui::GetWindowWidth() - 10;
    int flags = ImGuiTreeNodeFlags_Framed;// | ImGuiTreeNodeFlags_OpenOnArrow; //ImGuiTreeNodeFlags_DefaultOpen | 
    auto& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Header] = ImVec4(0.01f, 0.01f, 0.06f, 1.f);
    if (ImGui::TreeNodeEx("LOD", flags))
    {
        if (selectedPart)
        {

            ImGui::PushFont(_gui->getFont("default"));

            if (selectedPart == root)
            {
                //ImGui::SameLine(0, 0);
                /*
                * temporary rem ove, I think its not needed
                if (ImGui::Button("calculate extents", ImVec2(columnWidth / 2 - 10, 0)))
                {
                    settings.pixelSize = 0.00005f;   // half a mm
                    settings.isBaking = true;
                    settings.exclusionCylinder = { 0, 0 };
                    build();   // to generate new extents
                    settings.isBaking = false;
                    displayModeSinglePlant = true;
                    anyChange = true;
                    selectedPart->changed = true;
                    extents = selectedPart->calculate_extents(bakeViewAdjusted);
                }
                */
            }

            style.Colors[ImGuiCol_Button] = ImVec4(0.01f, 0.01f, 0.01f, 1.f);
            if (ImGui::Button("Build all lods", ImVec2(columnWidth - 25, 40)))
            {
                buildAllLods();
            }


            for (uint lod = 0; lod < 100; lod++)
            {
                levelOfDetail* lodInfo = selectedPart->getLodInfo(lod);
                if (lodInfo)
                {
                    if (currentLOD == lod)
                    {
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.16f, 0.05f, 0.0f, 1.0f));
                    }
                    else
                    {
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
                    }

                    float lineHeight = ImGui::GetFontSize() * 1.5f;
                    ImGui::BeginChildFrame(5678 + lod, ImVec2(columnWidth - 25, lineHeight * 3));
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
                            //ImGui::PopFont();
                        }
                        ImGui::PopFont();

                        ImGui::SameLine(40, 0);
                        ImGui::SetNextItemWidth(100);
                        if (ImGui::DragInt("##numPix", &(lodInfo->numPixels), 0.1f, 8, 2000, "%d pix"))
                        {
                            anyChange = true;
                            //lodInfo->pixelSize = extents.y / lodInfo->numPixels;// *lodInfo->geometryPixelScale;;
                            settings.pixelSize = lodInfo->pixelSize * 0.001f; // not suew why its needed
                            selectedPart->changed = true;
                        }
                        TOOLTIP("pixel height of crossover");

                        ImGui::SameLine(0, 20);
                        ImGui::SetNextItemWidth(100);
                        //ImGui::Text("%5.1fmm", lodInfo->pixelSize * 1000.f);
                        if (ImGui::DragFloat("##pixelSize-mm", &(lodInfo->pixelSize), 0.1f, 0.1f, 500.f, "%2.1fmm", 2.f))
                        {
                            anyChange = true;
                            settings.pixelSize = lodInfo->pixelSize * 0.001f;
                            selectedPart->changed = true;
                        }
                        TOOLTIP("build detail");

                        ImGui::PushFont(_gui->getFont("header2"));
                        {
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.1f, 0.1f, 1.0f));
                            //ImGui::PushFont(_gui->getFont("header2"));
                            ImGui::SameLine(columnWidth - 25 - 80, 0);
                            //if (ImGui::Button("build", ImVec2(60, 0)));
                            if (ImGui::Button("build", ImVec2(80, 0)))
                            {
                                displayModeSinglePlant = true;
                                settings.pixelSize = lodInfo->pixelSize * 0.001f;
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
                        if (ImGui::Combo("##type", &lodInfo->bakeType, "none\0diamond\0'6 tri'\0'12 tri'\0")) { selectedPart->changed = true; }
                        TOOLTIP("none - do not use any baked information \ndiamond - 2 triangles in diamond pattern.\n'6 tri' 6 triangle ribbon \n'12 tri' 12 triangle ribbon")

                            ImGui::SameLine(0, 10);
                        ImGui::SetNextItemWidth(60);
                        if (ImGui::DragInt("bake_idx", &(lodInfo->bakeIndex), 0.1f, 0, 2)) { selectedPart->changed = true; }
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

            ImGui::PopFont();
            style.Colors[ImGuiCol_Button] = ImVec4(0.01f, 0.01f, 0.01f, 1.f);
            if (ImGui::Button("Build full", ImVec2(columnWidth - 25, 40)))
            {
                buildFullResolution();
            }


        }
        ImGui::TreePop();
    }
}





void _rootPlant::renderGui_Baking(Gui* _gui)
{
    uint gui_id = 199994;
    float columnWidth = ImGui::GetWindowWidth() - 10;
    int flags = ImGuiTreeNodeFlags_Framed;// | ImGuiTreeNodeFlags_OpenOnArrow; //ImGuiTreeNodeFlags_DefaultOpen | 
    auto& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Header] = ImVec4(0.06f, 0.01f, 0.01f, 1.f);

    ImGui::PushFont(_gui->getFont("header2"));
    if (ImGui::TreeNodeEx("Baking", flags))
    {
        ImGui::PushFont(_gui->getFont("default"));
        {

            if (selectedPart)
            {
                style.Colors[ImGuiCol_Button] = ImVec4(0.01f, 0.01f, 0.01f, 1.f);
                if (ImGui::Button("Bake materials", ImVec2(columnWidth - 25, 40)))
                {
                    spdlog::info("New bake started - {}", root->name.c_str());

                    std::filesystem::path PT = selectedPart->path;
                    std::string resource = terrafectorEditorMaterial::rootFolder;

                    std::string newDir = "\"" + resource + PT.parent_path().string() + "\\bake_" + PT.stem().string() + "\"";
                    replaceAllVEG(newDir, "/", "\\");
                    std::string delCmd = "rmdir /S /Q " + newDir;
                    system(delCmd.c_str());

                    std::string makeCmd = "mkdir " + newDir; //"\"" + resource + PT.parent_path().string() + "\\bake_" + PT.stem().string() + "\"";
                    //replaceAllVEG(newDir, "/", "\\");
                    system(makeCmd.c_str());
                    spdlog::info("{}", makeCmd.c_str());

                    for (int seed = 1000; seed < 1001; seed++)
                    {
                        for (int j = 0; j < 100; j++)
                        {
                            levelOfDetail* lod = root->getLodInfo(j);
                            if (lod)
                            {
                                settings.pixelSize = lod->pixelSize * 0.001f;
                                spdlog::info("lod found - {}, {:.2f}mm", j, lod->pixelSize * 1000.f);
                            }
                        }

                        settings.seed = seed;
                        //settings.pixelSize = 0.01f;
                        settings.isBaking = true;    //??????
                        _ribbonBuilder.clear();     //??? Mayeb unnesesary, I think buiild will do thois
                        //build(true); // For baking
                        buildFullResolution();
                        settings.isBaking = false;
                        spdlog::info("build done - {} verts", (int)_ribbonBuilder.numPacked());

                        bakeViewMatrix = glm::mat4(1.0);
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




                        extents = selectedPart->calculate_extents(bakeViewAdjusted);

                        for (int i = 0; i < 10; i++)
                        {

                            if (selectedPart->getBakeInfo(i) && (selectedPart->getBakeInfo(i)->pixHeight > 0))
                            {
                                bakeViewAdjusted[0][0] = r.x;
                                bakeViewAdjusted[0][1] = r.y;
                                bakeViewAdjusted[0][2] = r.z;

                                bakeViewAdjusted[1][0] = u.x;
                                bakeViewAdjusted[1][1] = u.y;
                                bakeViewAdjusted[1][2] = u.z;

                                bakeViewAdjusted[2][0] = d.x;
                                bakeViewAdjusted[2][1] = d.y;
                                bakeViewAdjusted[2][2] = d.z;

                                auto BK = selectedPart->getBakeInfo(showBake);
                                if (BK)
                                {
                                    ROLL(bakeViewAdjusted, BK->yaw);
                                    PITCH(bakeViewAdjusted, -BK->pitch);
                                }

                                bake(selectedPart->path, std::to_string(settings.seed), selectedPart->getBakeInfo(i), bakeViewAdjusted);
                                selectedPart->getBakeInfo(i)->material.reload();
                                selectedPart->changed = true;
                            }
                        }
                    }
                    selectedPart->savePath();
                    selectedPart->changedForSave = false;
                }

                for (int i = 0; i < 10; i++)
                {
                    lodBake* bake = selectedPart->getBakeInfo(i);
                    if (bake)
                    {

                        if (i == showBake)
                        {
                            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.16f, 0.05f, 0.0f, 1.0f));
                        }
                        else
                        {
                            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
                        }

                        bool isForbaking = bake->pixHeight > 0;
                        float lineHeight = ImGui::GetFontSize() * 1.5f * (1 + isForbaking * 11);
                        float columnWidth = ImGui::GetWindowWidth() - 10;
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
                                R_CURVE("start / stop", bake->bake_V, "", "");
                                R_FLOAT("translucency", bake->translucency, 0.01f, 0.1f, 10.f, "");
                                //ImGui::SameLine(0, 10);
                                R_FLOAT("alpha", bake->alphaPow, 0.01f, 0.1f, 10.f, "");
                                CHECKBOX("diamond", &bake->forceDiamond, "This bake will use diamond instead of rectangular geometry\n???check if this is still supported");
                                //ImGui::SameLine(0, 10);
                                CHECKBOX("faceCamera", &bake->faceCamera, "");
                                CHECKBOX("bake AO", &bake->bakeAOToAlbedo, "if used as a billboard, bake the ao to the texture itself for added depth\nif used as a code with vertex ao, leave out");
                                //ImGui::SameLine(0, 10);
                                CHECKBOX("bake alpha", &bake->useAlphaInBake, "Bake an alpha oval for smoother side and top crossovers");
                                bake->alphaOval.x = extents.x * bake->bakeWidth;
                                bake->alphaOval.y = extents.y * bake->bake_V.y;
                                //R_LENGTH_EX("alphaOval", bake->alphaOval, 0.01f, 0.f, 20.f, "", "");

                                R_FLOAT("pitch", bake->pitch, 0.01f, 0.1f, 10.f, "");
                                TOOLTIP("0 for all large plants \nslightly tilted for smaller plants especially bake - 0")
                                    R_FLOAT("yaw", bake->yaw, 0.01f, 0.1f, 10.f, "");
                                TOOLTIP("try to keep this 0, it can break random variations")
                            }

                            selectedPart->changed |= changed;
                        }
                        ImGui::EndChildFrame();
                        ImGui::PopStyleColor();
                        if (ImGui::IsItemClicked(0)) showBake = i;

                        /*
                        columnWidth = ImGui::GetWindowWidth() / 2 - 5;
                        style.Colors[ImGuiCol_Header] = ImVec4(0.01f, 0.01f, 0.01f, 1.f);
                        std::string bakeStr = "bake-" + std::to_string(i);
                        if (ImGui::TreeNodeEx(bakeStr.c_str(), ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_OpenOnArrow))
                        {
                            bool changed = false;

                            bool isForbaking = bake->pixHeight > 0;
                            CHECKBOX("use this bake", &isForbaking, "");
                            if (isForbaking && bake->pixHeight == 0)
                            {
                                // we turned on
                                bake->pixHeight = 32;
                            }
                            if (!isForbaking)
                            {
                                // we turned off
                                bake->pixHeight = 0;
                            }

                            if (isForbaking)
                            {
                                R_INT("height", bake->pixHeight, 0, 512, "");
                                R_FLOAT("width", bake->bakeWidth, 0.01f, 0.1f, 10.f, "");
                                R_CURVE("start / stop", bake->bake_V, "", "");
                                R_FLOAT("translucency", bake->translucency, 0.01f, 0.1f, 10.f, "");
                                //ImGui::SameLine(0, 10);
                                R_FLOAT("alpha", bake->alphaPow, 0.01f, 0.1f, 10.f, "");
                                CHECKBOX("diamond", &bake->forceDiamond, "This bake will use diamond instead of rectangular geometry\n???check if this is still supported");
                                //ImGui::SameLine(0, 10);
                                CHECKBOX("faceCamera", &bake->faceCamera, "");
                                CHECKBOX("bake AO", &bake->bakeAOToAlbedo, "if used as a billboard, bake the ao to the texture itself for added depth\nif used as a code with vertex ao, leave out");
                                //ImGui::SameLine(0, 10);
                                CHECKBOX("bake alpha", &bake->useAlphaInBake, "Bake an alpha oval for smoother side and top crossovers");
                                bake->alphaOval.x = extents.x * bake->bakeWidth;
                                bake->alphaOval.y = extents.y * bake->bake_V.y;
                                //R_LENGTH_EX("alphaOval", bake->alphaOval, 0.01f, 0.f, 20.f, "", "");
                            }

                            if (changed) selectedPart->changed = true;

                            ImGui::TreePop();
                        }
                        if (bake->pixHeight > 0 && ImGui::IsItemHovered())
                        {
                            // would be nice if we could show
                            //auto& MAT = _plantMaterial::static_materials_veg.materialVector[selectedPart->getBakeInfo(i)->material.index];
                            //_plantMaterial::static_materials_veg.dispTexIndex = MAT._constData.albedoTexture;
                        }
                        */
                    }
                }
            }

        }
        ImGui::PopFont();
        ImGui::TreePop();
    }
    ImGui::PopFont();
}


ImVec2 toIMVec2(float2 V)
{
    return ImVec2(V.x, V.y);
}


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
    renderContext->clearFbo(textureToolData.fbo.get(), clearColor, 1.0f, 0, FboAttachmentType::All);    // depth
    renderContext->clearRtv(textureToolData.fbo->getRenderTargetView(0), glm::vec4(0.0, 0.0f, 0.0f, 0.0f));
    renderContext->clearRtv(textureToolData.fbo->getRenderTargetView(1), glm::vec4(0.5, 0.5f, 1.0f, 0.0f));
    renderContext->clearRtv(textureToolData.fbo->getRenderTargetView(2), glm::vec4(0.0, 0.0f, 0.0f, 0.0f));
    renderContext->clearRtv(textureToolData.fbo->getRenderTargetView(3), glm::vec4(0.0, 0.0f, 0.0f, 0.0f));
    renderContext->clearRtv(textureToolData.fbo->getRenderTargetView(4), glm::vec4(0.0, 0.0f, 0.0f, 0.0f));

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

    textureExtractShader.drawInstanced(renderContext, 1, 1);
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


void _rootPlant::renderGui_textureTool(Gui* _gui, int _header, float2 _screen, Gui::Window& _hud)
{
    bool changed = false;
    uint gui_id = 1003;
    auto& style = ImGui::GetStyle();
    uint displaySize = 800;


    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.02f, 0.02f, 0.02f, 1.0f);
    ImGui::BeginChildFrame(33199, ImVec2(0, 0), 0);
    {
        ImVec2 TL = ImGui::GetCursorScreenPos();
        float2 topLeft = float2(TL.x + 300, TL.y + 300);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        ImGui::PushFont(_gui->getFont("default"));
        ImGui::Text("texture tool");
        ImGui::Text("%s", textureToolData.path.c_str());

        if (ImGui::Button("load", ImVec2(140, 0)))
        {
            std::filesystem::path path;
            if (openFileDialog({ {"textureTool"} }, path))
            {
                std::ifstream is(path);
                cereal::JSONInputArchive archive(is);
                archive(textureToolData);
                changed = false;

                textureToolData.tex_albedo = Texture::createFromFile(terrafectorEditorMaterial::rootFolder + textureToolData.albedo, true, true);
                textureToolData.tex_alpha = Texture::createFromFile(terrafectorEditorMaterial::rootFolder + textureToolData.alpha, true, true);
                textureToolData.tex_normal = Texture::createFromFile(terrafectorEditorMaterial::rootFolder + textureToolData.normal, true, false);
                textureToolData.tex_translucency = Texture::createFromFile(terrafectorEditorMaterial::rootFolder + textureToolData.translucency, true, true);
                textureToolData.changed = false;
            }
        }

        if (textureToolData.changed)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.2f, 0.0f, 1.0f));
            ImGui::SameLine(0, 20);
            if (ImGui::Button("save", ImVec2(140, 0)))
            {
                std::ofstream os(terrafectorEditorMaterial::rootFolder + textureToolData.path);
                cereal::JSONOutputArchive archive(os);
                archive(textureToolData);
                textureToolData.changed = false;
            }
            ImGui::PopStyleColor();
        }

        //ImGui::SameLine(0, 20);
        if (ImGui::Button("save - as", ImVec2(140, 0)))
        {
            std::filesystem::path path = terrafectorEditorMaterial::rootFolder + textureToolData.path;
            if (saveFileDialog({ {"textureTool"} }, path))
            {
                std::ofstream os(path);
                cereal::JSONOutputArchive archive(os);
                textureToolData.path = materialCache::getRelative(path.parent_path().string() + "//" + path.stem().string());

                archive(textureToolData);
                textureToolData.changed = false;
            }
        }

        if (ImGui::Button("export textures", ImVec2(300, 0)))
        {
            //??? 
            exportTextures();
        }

        ImGui::NewLine();

        if (ImGui::Button("albedo", ImVec2(100, 0)))
        {
            std::filesystem::path filepath = terrafectorEditorMaterial::rootFolder + textureToolData.albedo;
            FileDialogFilterVec filters = { {"png"}, {"tga"}, {"tif"}, {"jpg"}, { "jpeg" } };
            if (openFileDialog(filters, filepath))
            {
                textureToolData.albedo = materialCache::getRelative(filepath.string());
                textureToolData.tex_albedo = Texture::createFromFile(terrafectorEditorMaterial::rootFolder + textureToolData.albedo, true, true);
                textureToolData.changed = true;
            }
        }
        ImGui::SameLine(0, 30);
        ImGui::Text(textureToolData.albedo.c_str());


        if (ImGui::Button("alpha", ImVec2(100, 0)))
        {
            std::filesystem::path filepath = terrafectorEditorMaterial::rootFolder + textureToolData.alpha;
            FileDialogFilterVec filters = { {"png"}, {"tga"}, {"tif"}, {"jpg"}, { "jpeg" } };
            if (openFileDialog(filters, filepath))
            {
                textureToolData.alpha = materialCache::getRelative(filepath.string());
                textureToolData.tex_alpha = Texture::createFromFile(terrafectorEditorMaterial::rootFolder + textureToolData.alpha, true, true);
                textureToolData.changed = true;
            }
        }
        ImGui::SameLine(0, 30);
        ImGui::Text(textureToolData.alpha.c_str());


        if (ImGui::Button("normal", ImVec2(100, 0)))
        {
            std::filesystem::path filepath = terrafectorEditorMaterial::rootFolder + textureToolData.normal;
            FileDialogFilterVec filters = { {"png"}, {"tga"}, {"tif"}, {"jpg"}, { "jpeg" } };
            if (openFileDialog(filters, filepath))
            {
                textureToolData.normal = materialCache::getRelative(filepath.string());
                textureToolData.tex_normal = Texture::createFromFile(terrafectorEditorMaterial::rootFolder + textureToolData.normal, true, false);
                textureToolData.changed = true;
            }
        }
        ImGui::SameLine(0, 30);
        ImGui::Text(textureToolData.normal.c_str());


        if (ImGui::Button("translucency", ImVec2(100, 0)))
        {
            std::filesystem::path filepath = terrafectorEditorMaterial::rootFolder + textureToolData.translucency;
            FileDialogFilterVec filters = { {"png"}, {"tga"}, {"tif"}, {"jpg"}, { "jpeg" } };
            if (openFileDialog(filters, filepath))
            {
                textureToolData.translucency = materialCache::getRelative(filepath.string());
                textureToolData.tex_translucency = Texture::createFromFile(terrafectorEditorMaterial::rootFolder + textureToolData.translucency, true, true);
                textureToolData.changed = true;
            }
        }
        ImGui::SameLine(0, 30);
        ImGui::Text(textureToolData.translucency.c_str());

        CHECKBOX("flip red", &textureToolData.flipRed, "");
        CHECKBOX("flip green", &textureToolData.flipGreen, "");
        ImGui::SetNextItemWidth(300);
        ImGui::DragFloat("strength", &textureToolData.normalStrenth, 0.01f, 0.1f, 5.f);


        ImGui::NewLine();
        ImGui::Text("sub-textures");
        if (ImGui::Button("-", ImVec2(140, 0)))
        {
            if (textureToolData.maps.size() > 0)
            {
                textureToolData.maps.pop_back();
            }
        }
        ImGui::SameLine(0, 20);
        if (ImGui::Button("+", ImVec2(140, 0)))
        {
            textureToolData.maps.emplace_back();
        }

        static int activeMap = 0;
        for (int i = 0; i < textureToolData.maps.size(); i++)
        {
            auto& M = textureToolData.maps[i];
            style.Colors[ImGuiCol_FrameBg] = ImVec4(0.02f, 0.06f, 0.02f, 1.0f);
            if (i == activeMap) style.Colors[ImGuiCol_FrameBg] = ImVec4(0.06f, 0.04f, 0.02f, 1.0f);
            ImGui::BeginChildFrame(331 + i, ImVec2(300, 100), 0);
            {
                ImGui::DragInt("width", &M.texWidth, 1, 1, 16);
                ImGui::DragInt("height", &M.texHeight, 1, 1, 16);
                ImGui::DragInt("mips", &M.numMips, 1, 0, 12);
                int x = (int)pow(2, M.numMips);
                M.texWidth = __max(1, M.texWidth);
                M.texHeight = __max(1, M.texHeight);
                ImGui::Text("smallest (%d, %d)", M.texWidth * 4 * x, M.texHeight * 4 * x);
            }
            ImGui::EndChildFrame();
            if (ImGui::IsItemClicked()) activeMap = i;
        }


        if (textureToolData.fbo)
        {
            ImU32 col32B = ImColor(ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
            float2 top = topLeft + float2(displaySize + 50 + textureToolData.w * 0.5, -300);
            float2 bottom = top + float2(0, textureToolData.h + 20);
            draw_list->AddLine(toIMVec2(bottom), toIMVec2(top), col32B);


            ImGui::SetCursorPos(ImVec2(350.f + displaySize, 10));
            _hud.image("AL", textureToolData.fbo->getColorTexture(0), float2(textureToolData.w, textureToolData.h));

            ImGui::SetCursorPos(ImVec2(360.f + displaySize + textureToolData.w, 10));
            _hud.image("NR", textureToolData.fbo->getColorTexture(1), float2(textureToolData.w, textureToolData.h));

            ImGui::SetCursorPos(ImVec2(350.f + displaySize, 60.f + textureToolData.h));
            _hud.image("TR", textureToolData.fbo->getColorTexture(2), float2(textureToolData.w, textureToolData.h));

            ImGui::SetCursorPos(ImVec2(360.f + displaySize + textureToolData.w, 60.f + textureToolData.h));
            _hud.image("LG", textureToolData.fbo->getColorTexture(4), float2(textureToolData.w, textureToolData.h));
        }

        static int TextToShow = 0;

        if (textureToolData.tex_albedo)
        {
            ImGui::SetCursorPos(ImVec2(300, 10));
            _hud.image("alb", textureToolData.tex_albedo, float2(128, 128));
            if (ImGui::IsItemClicked()) TextToShow = 0;

            ImGui::SetCursorPos(ImVec2(300, 300));
            if (TextToShow == 0)    _hud.image("large", textureToolData.tex_albedo, float2(displaySize, displaySize));
            if (TextToShow == 1)    _hud.image("large", textureToolData.tex_alpha, float2(displaySize, displaySize));
            if (TextToShow == 2)    _hud.image("large", textureToolData.tex_normal, float2(displaySize, displaySize));
            if (TextToShow == 3)    _hud.image("large", textureToolData.tex_translucency, float2(displaySize, displaySize));
            if (ImGui::IsItemHovered())
            {
                if (textureToolData.maps.size() > 0)
                {
                    activeMap = __min(activeMap, textureToolData.maps.size());
                    auto& M = textureToolData.maps[activeMap];

                    bool changed = false;

                    if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_LeftCtrl))
                    {
                        M.width -= ImGui::GetMouseDragDelta().y / displaySize / 5;
                        M.width = __max(0.001f, __min(1.f, M.width));

                        M.bezierOffset -= ImGui::GetMouseDragDelta(1).y / displaySize / 5;
                        changed = true;
                    }
                    else
                    {
                        if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_LeftShift))
                        {


                            if (ImGui::IsMouseDragging(0))
                            {
                                float2 mouse = float2(ImGui::GetMouseDragDelta(0).x, ImGui::GetMouseDragDelta(0).y) * 0.1f;
                                M.start += mouse / (float)displaySize;
                                changed = true;
                            }
                            if (ImGui::IsMouseDragging(1))
                            {
                                float2 mouse = float2(ImGui::GetMouseDragDelta(1).x, ImGui::GetMouseDragDelta(1).y) * 0.1f;
                                M.stop += mouse / (float)displaySize;
                                changed = true;
                            }
                            if (ImGui::IsMouseDragging(2))
                            {
                                //float2 mouse = float2(ImGui::GetMouseDragDelta(2).x, ImGui::GetMouseDragDelta(2).y) * 0.1f;
                                //M.bezier += mouse / (float)displaySize;
                                //changed = true;
                            }
                        }
                        else
                        {
                            float2 mouse = float2(ImGui::GetMousePos().x, ImGui::GetMousePos().y);

                            if (ImGui::IsMouseDragging(0))
                            {
                                M.start = (mouse - topLeft) / (float)displaySize;
                                changed = true;
                            }
                            if (ImGui::IsMouseDragging(1))
                            {
                                M.stop = (mouse - topLeft) / (float)displaySize;
                                changed = true;
                            }
                            if (ImGui::IsMouseDragging(2))
                            {
                                //M.bezier = (mouse - topLeft) / (float)displaySize;
                                //changed = true;
                            }
                        }
                    }





                    if (changed)
                    {
                        // bezier halfway
                        float2 half = (M.start + M.stop) * 0.5f;
                        float2 T = glm::normalize(M.stop - M.start);
                        float2 BT = float2(T.y, -T.x);
                        M.bezier = half + BT * M.bezierOffset;

                        ImGui::ResetMouseDragDelta(0);
                        ImGui::ResetMouseDragDelta(1);
                        ImGui::ResetMouseDragDelta(2);
                    }


                }
            }
        }

        if (textureToolData.tex_alpha)
        {
            ImGui::SetCursorPos(ImVec2(450, 10));
            _hud.image("alpha", textureToolData.tex_alpha, float2(128, 128));
            if (ImGui::IsItemClicked()) TextToShow = 1;
        }

        if (textureToolData.tex_normal)
        {
            ImGui::SetCursorPos(ImVec2(600, 10));
            _hud.image("normal", textureToolData.tex_normal, float2(128, 128));
            if (ImGui::IsItemClicked()) TextToShow = 2;
        }

        if (textureToolData.tex_translucency)
        {
            ImGui::SetCursorPos(ImVec2(750, 10));
            _hud.image("trans", textureToolData.tex_translucency, float2(128, 128));
            if (ImGui::IsItemClicked()) TextToShow = 3;
        }



        for (int i = 0; i < textureToolData.maps.size(); i++)
        {
            activeMap = __min(activeMap, textureToolData.maps.size());
            auto& M = textureToolData.maps[i];

            ImU32 col32B = ImColor(ImVec4(0.4f, 0.4f, 0.4f, 1.0f));

            if (i == activeMap)
            {
                GenerateATexture(activeMap, false);
                col32B = ImColor(ImVec4(0.0f, 0.0f, 1.0f, 1.0f));
            }

            float2 start = topLeft + M.start * (float)displaySize;
            float2 stop = topLeft + M.stop * (float)displaySize;

            float2 dir = glm::normalize(stop - start);
            float2 norm = float2(dir.y, -dir.x);

            float2 A = (M.start + norm * M.width) * (float)displaySize + topLeft;
            float2 B = (M.start - norm * M.width) * (float)displaySize + topLeft;
            float2 C = (M.stop - norm * M.width) * (float)displaySize + topLeft;
            float2 D = (M.stop + norm * M.width) * (float)displaySize + topLeft;

            draw_list->AddCircle(toIMVec2(start), 20, col32B);
            draw_list->AddCircle(toIMVec2(stop), 4, col32B, 12, 2);

            draw_list->AddLine(toIMVec2(start), toIMVec2(stop), col32B);

            draw_list->AddLine(toIMVec2(A), toIMVec2(B), col32B);
            draw_list->AddLine(toIMVec2(B), toIMVec2(C), col32B);
            draw_list->AddLine(toIMVec2(C), toIMVec2(D), col32B);
            draw_list->AddLine(toIMVec2(D), toIMVec2(A), col32B);



            // new bezier version
            float2 bez = topLeft + M.bezier * (float)displaySize;
            draw_list->AddCircle(toIMVec2(bez), 4, ImColor(ImVec4(1.0f, 0.0f, 0.0f, 1.0f)), 12, 2);

        }

        ImGui::PopFont();

    }
    ImGui::EndChildFrame();

    textureToolData.changed |= changed;
}

ImVec2 toImVec2(float2 f) { return ImVec2(f.x, f.y); }

void _rootPlant::renderGui_HUD(Gui* _gui, int _header, float2 _screen)
{
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.0f));

    Gui::Window hud(_gui, "##hud", { _screen.x, _screen.y }, { 0, 0 }, Gui::WindowFlags::Empty | Gui::WindowFlags::NoResize);

    if (textureTool)
    {
        hud.windowPos(0, _header);
        hud.windowSize((int)_screen.x, (int)_screen.y - _header);
    }
    else
    {
        hud.windowPos(layout.left, _header);
        hud.windowSize((int)_screen.x - layout.left, (int)_screen.y - _header - layout.bottom);

        if (!showMaterialsPanel)
        {
            hud.windowSize((int)_screen.x - layout.left, (int)_screen.y - _header);
        }
    }

    {
        ImGui::PushFont(_gui->getFont("header0"));
        {
            if (root)
            {
                ImGui::SameLine(30);
                ImGui::Text(root->name.c_str());
            }
        }
        ImGui::PopFont();

        if (textureTool)
        {
            renderGui_textureTool(_gui, _header, _screen, hud);
        }

        else if (root && bakingView)
        {



            // would be nice if we could show
                            //
                            //_plantMaterial::static_materials_veg.dispTexIndex = MAT._constData.albedoTexture;

            ImDrawList* draw_list = ImGui::GetWindowDrawList();

            draw_list->AddRect(ImVec2(bakeViewportTL.x, bakeViewportTL.y), ImVec2(bakeViewportTL.x + bakeViewportSize, bakeViewportTL.y + bakeViewportSize), ImColor(ImVec4(0.3f, 0.3f, 0.3f, 1.0f)));

            ImVec2 mid = ImVec2(bakeViewportTL.x + bakeViewportSize * 0.5f, bakeViewportTL.y + bakeViewportSize * 0.5f);


            auto& style = ImGui::GetStyle();



            auto* BK = root->getBakeInfo(showBake);
            if (BK)
            {

                if (BK->material.index >= 0)
                {
                    auto& MAT = _plantMaterial::static_materials_veg.materialVector[BK->material.index];

                    if (MAT._constData.albedoTexture >= 0)
                    {
                        ImGui::SetCursorPos(ImVec2(20.f, 100));
                        hud.image("al", _plantMaterial::static_materials_veg.textureVector[MAT._constData.albedoTexture], float2(180, 180));
                    }
                    /*
                    if (MAT._constData.normalTexture >= 0)
                    {
                        ImGui::SetCursorPos(ImVec2(20.f, 400));
                        hud.image("nr", _plantMaterial::static_materials_veg.textureVector[MAT._constData.normalTexture], float2(128, 128));
                    }

                    if (MAT._constData.translucencyTexture >= 0)
                    {
                        ImGui::SetCursorPos(ImVec2(20.f, 700));
                        hud.image("tr", _plantMaterial::static_materials_veg.textureVector[MAT._constData.translucencyTexture], float2(128, 128));
                    }
                    */
                }

                //ImGui::NewLine();
                //ImGui::SameLine(0, 500);
                /*
                if (ImGui::Button("BAKE")) {
                    bake(selectedPart->path, std::to_string(settings.seed), selectedPart->getBakeInfo(showBake), bakeViewAdjusted);
                    selectedPart->getBakeInfo(showBake)->material.reload();
                    selectedPart->savePath();
                }
                */
                bool changed = false;
                uint gui_id = 19994;
                /*
                ImGui::SetNextItemWidth(200);
                ImGui::DragFloat("width", &BK->bakeWidth, 0.005f, 0.01f, 1.f);
                ImGui::SetNextItemWidth(200);
                ImGui::DragFloat2("start stop", &BK->bake_V.x, 0.01f, 0.0f, 1.f);

                ImGui::NewLine();
                ImGui::SameLine(0, 500);
                ImGui::SetNextItemWidth(200);
                ImGui::DragFloat("PITCH", &BK->pitch, 0.005f, 0.0f, 1.5f);

                ImGui::NewLine();
                ImGui::SameLine(0, 500);
                ImGui::SetNextItemWidth(200);
                ImGui::DragFloat("YAW", &BK->yaw, 0.005f, 0.0f, 3.2f);
                */
                //float w1 = (extents.x / extents.y) * 500 * BK->bakeWidth *;
                //float h0 = 500 * (1.0f - BK->bake_V.x) - 500;
                //float h1 = 0 - 500 * (BK->bake_V.y);


                float half = bakeViewportSize / 2.f;
                float w1 = (BK->alphaOval.x / extents.y) * half;
                float h0 = half * (1.0f - BK->bake_V.x) - half;
                float h1 = -half * (BK->alphaOval.y / extents.y);

                float2 M = bakeViewportTL + half;
                float2 TL = M + float2(-w1, h0);
                float2 BR = M + float2(w1, h1);

                draw_list->AddRect(toImVec2(TL), toImVec2(BR), ImColor(ImVec4(0.3f, 0.3f, 0.9f, 1.0f)));


                if (BK->useAlphaInBake)
                {
                    float2 BM = M + float2(0, h1 * 0.85f);
                    float2 B = BM - float2(w1 * 0.85f, 0);

                    TL = M + float2(-w1 * 0.85f, h0);    // axtiallu the bottom
                    float2 A = TL + float2(0, 0.5f * (h0 + h1));
                    // TODO: draw_list->AddBezierCurve(toImVec2(TL), toImVec2(A), toImVec2(B), toImVec2(BM), ImColor(ImVec4(0.3f, 0.3f, 0.9f, 1.0f)), 2, 30);

                    //float2 BM = M + float2(0, h1 * 0.85f);
                    B = BM + float2(w1 * 0.85f, 0);

                    TL = M + float2(w1 * 0.85f, h0);    // axtiallu the bottom
                    A = TL + float2(0, 0.5f * (h0 + h1));
                    // TODO: draw_list->AddBezierCurve(toImVec2(TL), toImVec2(A), toImVec2(B), toImVec2(BM), ImColor(ImVec4(0.3f, 0.3f, 0.9f, 1.0f)), 2, 30);
                    //draw_list->AddBezierCurve(ImVec2(1200 + w1, 700 + h0), ImVec2(1200 + w1, 700 + (h1 + h0) * 0.5f), ImVec2(1200 + w1, 700 + h1), ImVec2(1200, 700 + h1), ImColor(ImVec4(0.3f, 0.3f, 0.9f, 1.0f)), 2, 30);
                    //draw_list->AddBezierCurve(ImVec2(1200 - w1, 700 + h0), ImVec2(1200 - w1, 700 + (h1 + h0) * 0.5f), ImVec2(1200 - w1, 700 + h1), ImVec2(1200, 700 + h1), ImColor(ImVec4(0.3f, 0.3f, 0.9f, 1.0f)), 2, 30);
                    //draw_list->AddBezierCurve(ImVec2(1200 + w1, 700 + h0), ImVec2(1200 + w1, 700 + (h1 + h0) * 0.5f), ImVec2(1200 + w1, 700 + h1), ImVec2(1200, 700 + h1), ImColor(ImVec4(0.3f, 0.3f, 0.9f, 1.0f)), 2, 30);
                }

            }
        }
    }
    hud.release();
    ImGui::PopStyleColor();
}



void _rootPlant::renderGui(Gui* _gui, int _header, float2 _screen)
{
    uint gui_id = 99994;
    _plantBuilder::_gui = _gui;





    {
        auto& style = ImGui::GetStyle();
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.03f, 0.03f, 0.03f, 1.0f);
        if (selectedPart)
        {
            if (selectedPart->changedForSave)
            {
                style.Colors[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.05f, 0.0f, 1.0f);
            }
        }
    }

    //ImGui::SetWindowFocus("vegetation builder");
    Gui::Window builderPanel(_gui, "vegetation builder", { 900, 900 }, { 100, 100 }, Falcor::Gui::WindowFlags::NoResize);
    builderPanel.windowPos(0, _header);
    builderPanel.windowSize(layout.left, (int)_screen.y - _header);
    ImGui::PushFont(_gui->getFont("bold"));
    {
        float columnWidth = ImGui::GetWindowWidth() - 14;
        auto& style = ImGui::GetStyle();
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.01f, 0.01f, 0.01f, 1.0f);

        style.Colors[ImGuiCol_Header] = ImVec4(0.01f, 0.01f, 0.01f, 1.0f);  // selectable
        style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.01f, 0.01f, 0.01f, 1.0f);  // selectable
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.03f, 0.03f, 0.03f, 1.0f);  // selectable

        style.Colors[ImGuiCol_ChildBg] = ImVec4(0.01f, 0.01f, 0.01f, 1.0f);  // selectable
        style.Colors[ImGuiCol_Button] = ImVec4(0.01f, 0.01f, 0.01f, 1.0f);  // selectable
        style.Colors[ImGuiCol_PopupBg] = ImVec4(0.02f, 0.01f, 0.01f, 1.0f);  // selectable


        style.FramePadding = ImVec2(2, 2);
        style.WindowPadding = ImVec2(7, 7);
        style.FrameBorderSize = 0;

        int flags = ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_OpenOnArrow; //ImGuiTreeNodeFlags_DefaultOpen | 


        if (selectedPart)
        {
            anyChange |= selectedPart->renderGui();  // now render the selected item.
        }

        ImGui::NewLine();
        renderGui_Lodding(_gui);

        ImGui::NewLine();
        renderGui_Baking(_gui);


        ImGui::NewLine();
        if (ImGui::TreeNodeEx("Other", flags))
        {
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

            /*
            ImGui::DragFloat("camRot", &camRot, 0.01f, 0.f, 3.14159265f, "%3.2fm");
            ImGui::DragFloat("camPitch", &camPitch, 0.01f, 0.f, 1.5707963267f, "%3.2fm");
            ImGui::DragFloat("shadowPitch", &shadowPitch, 0.01f, 0.f, 1.5707963267f, "%3.2fm");
            if (ImGui::Button("bakeRGB")) { buildOneMap(shadowPitch); }
            if (ImGui::Button("buildBDRF")) { buildBDRF(); }

            float camRot = 0;
            float camPitch = 0;
            float shadowPitch = 0;
            */
            ImGui::TreePop();
        }


        if (root && anyChange)
        {
            //ImGui::Text("re building...");
            if (displayModeSinglePlant)
            {
                _ribbonBuilder.clear(); // why every ti,e just before a build - AH its to acommodate buildAll maybe
                build();
                if (settings.pixelSize < 0.002f)
                {
                    extents = root->calculate_extents(bakeViewAdjusted);
                }

                levelOfDetail* lod = root->getLodInfo(currentLOD);
                if (lod)
                {
                    lod->numVerts = (int)_ribbonBuilder.numVerts();
                    lod->numBlocks = _ribbonBuilder.numPacked() / VEG_BLOCK_SIZE;
                    lod->unused = _ribbonBuilder.numPacked() - _ribbonBuilder.numVerts();
                }
            }
            anyChange = false;
        }
    }
    ImGui::PopFont(); // default
    builderPanel.release();

    renderGui_HUD(_gui, _header, _screen);

    {
        auto& style = ImGui::GetStyle();
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.02f, 0.02f, 0.02f, 1.0f);
    }








    auto& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.03f, 0.03f, 0.03f, 1.0f);

    if (showMaterialsPanel)
    {
        ImGui::PushFont(_gui->getFont("header2"));
        {
            Gui::Window vegmatPanel(_gui, "Vegetation material##tfPanel", { 900, 900 }, { 100, 100 });
            {

                if (_plantMaterial::static_materials_veg.renderGuiSelect(_gui)) {
                    //reset(true);
                }
            }
            vegmatPanel.release();

            Gui::Window vegcachePanel(_gui, "Vegetation cache##tfPanel", { 900, 900 }, { 100, 100 });
            {
                _plantMaterial::static_materials_veg.renderGui(_gui, vegcachePanel);
            }
            vegcachePanel.release();


            Gui::Window vegtexPanel(_gui, "Vegetation textures##tfPanel", { 900, 900 }, { 100, 100 });
            {
                _plantMaterial::static_materials_veg.renderGuiTextures(_gui, vegtexPanel);
            }
            vegtexPanel.release();


        }
        ImGui::PopFont();
    }


    if (showLargePanel)
    {
        ImGui::PushFont(_gui->getFont("header2"));
        {
            auto& oldStyle = ImGui::GetStyle();
            auto& style = ImGui::GetStyle();
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0.03f, 0.03f, 0.03f, 1.0f);
            Gui::Window vegLargePanel(_gui, "Vegetation details##tfPanel", { 900, 900 }, { 100, 100 });
            {
                //_plantMaterial::static_materials_veg.renderGuiTextures(_gui, vegLargePanel);
            }
            vegLargePanel.release();
            style = oldStyle;   // reset it
        }
        ImGui::PopFont();
    }
}


void _rootPlant::buildAllLods()
{
    _ribbonBuilder.clearPivot();
    LOGTHEBUILD = true;
    if (LOGTHEBUILD)
    {
        spdlog::info("buildAllLods()n");
    }

    displayModeSinglePlant = false;

    uint start = 0;
    float Y = extents.y;
    spdlog::info("buildAllLods() Y = {:.2f}m", Y);

    _ribbonBuilder.clear();

    uint startBlock[16][16];
    uint numBlocks[16][16];
    for (int pIndex = 0; pIndex < 8; pIndex++)
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
                spdlog::info("lod {} : pixelSize = {:.2f}mm", lod, lodInfo->pixelSize);

                //lodInfo->pixelSize = Y / lodInfo->numPixels;// *lodInfo->geometryPixelScale;
                settings.pixelSize = lodInfo->pixelSize * 0.001f;
                settings.seed = 1000 + pIndex;
                //rootPitch = 2.f / 8.f * pIndex;
                build(pIndex * 256 * sizeof(_plant_anim_pivot));
                lodInfo->numVerts = (int)_ribbonBuilder.numVerts();
                lodInfo->numBlocks = _ribbonBuilder.numPacked() / VEG_BLOCK_SIZE - start;
                lodInfo->unused = _ribbonBuilder.numPacked() - _ribbonBuilder.numVerts();
                lodInfo->startBlock = start;

                spdlog::info("post build {} verts", lodInfo->numVerts);

                startBlock[pIndex][lod] = start;
                numBlocks[pIndex][lod] = lodInfo->numBlocks;


                plantBuf[pIndex].numLods = __max(plantBuf[pIndex].numLods, lod);    // omdat ons nou agteruit gaan
                plantBuf[pIndex].lods[lod - 1].pixSize = (float)lodInfo->numPixels;
                plantBuf[pIndex].lods[lod - 1].numBlocks = lodInfo->numBlocks;
                plantBuf[pIndex].lods[lod - 1].startVertex = start;



                start += lodInfo->numBlocks;

                spdlog::info("plant lod : {}, {}mm, {} blocks", lod, (float)lodInfo->pixelSize * 1000.f, lodInfo->numBlocks);
            }
        }
        // need to make a copy of the pivot data here after most detailed build I guess
        //plantpivotData->setBlob(ribbonVertex::pivotPoints.data(), , ribbonVertex::pivotPoints.size() * sizeof(_plant_anim_pivot));
    }

    // Now log this
    spdlog::info("buildAllLods() : {}", root->name.c_str());
    spdlog::info("size : {:.2f}, {:.2f}", plantBuf[0].size.x, plantBuf[0].size.y);
    spdlog::info("lod, blocks, startV, pixSize");
    for (int i = 0; i < plantBuf[0].numLods; i++)
    {
        spdlog::info("{} : {}, {}, {:.2f}", i, plantBuf[0].lods[i].numBlocks, plantBuf[0].lods[i].startVertex, plantBuf[0].lods[i].pixSize);
    }


    plantData->setBlob(plantBuf.data(), 0, 8 * sizeof(plant));
    numBinaryPlants = 8;
    builInstanceBuffer();
    spdlog::info("just set plants");

    int numV = __min(65536 * 8, _ribbonBuilder.numPacked());
    vertexData->setBlob(_ribbonBuilder.getPackedData(), 0, numV * sizeof(ribbonVertex8));                // FIXME uploads should be smaller
    spdlog::info("just set verts ({}), packed {}, numMaterials {}", numV, (int)_ribbonBuilder.packed.size(), (int)_plantMaterial::static_materials_veg.materialVector.size());
    for (int i = 0; i < (int)_plantMaterial::static_materials_veg.materialVector.size(); i++)
    {
        spdlog::info("material {}, {}", i, _plantMaterial::static_materials_veg.materialVector[i].displayName.c_str());
    }

    settings.seed = 1000;

    binaryPlantOnDisk OnDisk;

    OnDisk.numP = 1;
    OnDisk.numV = numV;
    for (int i = 0; i < numV; i++)
    {
        int idx = (_ribbonBuilder.packed[i].b >> 8) & 0x3ff;
        spdlog::info("{},", idx);


        _vegMaterial M;
        M.path = _plantMaterial::static_materials_veg.materialVector[idx].relativePath;
        M.name = _plantMaterial::static_materials_veg.materialVector[idx].displayName;
        M.index = idx;
        OnDisk.materials[idx] = M;
    }
    spdlog::info("found {} materials %", (int)OnDisk.materials.size());

    lodBake* lodZero = root->getBakeInfo(0);
    if (lodZero)
    {
        OnDisk.billboardMaterial = lodZero->material;
    }

    spdlog::info("about to save");

    std::string resource = terrafectorEditorMaterial::rootFolder;
    std::ofstream os(resource + root->path + ".binary");
    cereal::JSONOutputArchive archive(os);
    archive(OnDisk);

    spdlog::info("about to save binary");

    std::ofstream osData(resource + root->path + ".binaryData", std::ios::binary);
    osData.write((const char*)plantBuf.data(), OnDisk.numP * sizeof(plant));
    osData.write((const char*)_ribbonBuilder.getPackedData(), numV * sizeof(ribbonVertex8));
    // If n ot existing en pand the ivot points
    if (_ribbonBuilder.pivotPoints.size() < 256) _ribbonBuilder.pivotPoints.resize(256);
    osData.write((const char*)_ribbonBuilder.pivotPoints.data(), OnDisk.numP * 256 * sizeof(_plant_anim_pivot));
}



void binaryPlantOnDisk::onLoad(std::string path, uint vOffset)
{
    spdlog::info("onLoad()  {}", path.c_str());
    plantData.resize(numP);
    vertexData.resize(numV);
    pivotData.resize(numP * 256);

    std::ifstream osData(path + "Data", std::ios::binary);
    osData.read((char*)plantData.data(), numP * sizeof(plant));
    osData.read((char*)vertexData.data(), numV * sizeof(ribbonVertex8));
    osData.read((char*)pivotData.data(), numP * 256 * sizeof(_plant_anim_pivot));

    spdlog::info("{} plants, {} verts, {} pivots", numP, numV, numP * 256);

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
    spdlog::info("set voFFSET {} BLOCKS", vOffset);
    for (auto& P : plantData)
    {
        P.billboardMaterialIndex = billboardIndex;
        for (int i = 0; i < P.numLods; i++)
        {
            spdlog::info("LOD {}, start {}, size {}, pixSize {:.2f}", i, P.lods[i].startVertex, P.lods[i].numBlocks, P.lods[i].pixSize);
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

    binaryPlantOnDisk OnDisk;
    std::ifstream os(filepath);
    cereal::JSONInputArchive archive(os);
    archive(OnDisk);

    OnDisk.onLoad(filepath.string(), binVertexOffset / sizeof(ribbonVertex8));

    plantData->setBlob(OnDisk.plantData.data(), binPlantOffset, OnDisk.numP * sizeof(plant));

    int numV = __min(65536 * 8, OnDisk.numV);
    vertexData->setBlob(OnDisk.vertexData.data(), binVertexOffset, numV * sizeof(ribbonVertex8));

    plantpivotData->setBlob(OnDisk.pivotData.data(), binPivotOffset, OnDisk.numP * 256 * sizeof(_plant_anim_pivot));

    spdlog::info("sizeof(ribbonVertex8) {}", (int)sizeof(ribbonVertex8));
    spdlog::info("sizeof(plant) {}", (int)sizeof(plant));
    spdlog::info("sizeof(_plant_anim_pivot) {}", (int)sizeof(_plant_anim_pivot));

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

//#pragma optimize("", off)

#define bakeSuperSample 8
#define bakeMipToSave 3
void _rootPlant::bake(std::string _path, std::string _seed, lodBake* _info, glm::mat4 VIEW)
{
    spdlog::info("_rootPlant::bake()");
    spdlog::info("extents {:.3f}, {:.3f}", _info->extents.x, _info->extents.y);
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
    spdlog::info("bake hgt {}, width {}, lods {}, {:.2f} horScale, {:.2f} fW pix", _info->pixHeight, newIW, maxMIPY, HorizontalScale, fW);

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
        renderContext->clearFbo(fbo.get(), clearColor, 1.0f, 0, FboAttachmentType::All);    // depth
        renderContext->clearRtv(fbo->getRenderTargetView(0), glm::vec4(0.0, 0.0f, 0.0f, 0.0f));
        renderContext->clearRtv(fbo->getRenderTargetView(1), glm::vec4(0.5, 0.5f, 1.0f, 0.0f));
        renderContext->clearRtv(fbo->getRenderTargetView(2), glm::vec4(0.5, 0.5f, 1.0f, 0.0f));
        renderContext->clearRtv(fbo->getRenderTargetView(3), glm::vec4(0.0, 0.0f, 0.0f, 0.0f));
        renderContext->clearRtv(fbo->getRenderTargetView(4), glm::vec4(1.0, 1.0f, 1.0f, 1.0f));
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

        bakeShader.drawInstanced(renderContext, 32, totalBlocksToRender);
    }
    renderContext->flush(true);

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
    renderContext->flush(true);



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
    vegetationShader_GOURAUD.Vars()->setTexture("gPreviousFrame", _previousFrame);
    //vegetationShader_DEBUG_PIVOTS.Vars()->setTexture("gPreviousFrame", _previousFrame);
    //vegetationShader_DEBUG_PIXELS.Vars()->setTexture("gPreviousFrame", _previousFrame);

    billboardShader.Vars()->setTexture("terrainShadow", shadow);
    vegetationShader.Vars()->setTexture("terrainShadow", shadow);
    vegetationShader_GOURAUD.Vars()->setTexture("terrainShadow", shadow);
    //vegetationShader_DEBUG_PIVOTS.Vars()->setTexture("terrainShadow", shadow);
    //vegetationShader_DEBUG_PIXELS.Vars()->setTexture("terrainShadow", shadow);

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
    RC = _renderContext;
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
        bakeViewMatrix = glm::mat4(1.0);

        // PITCH(bakeViewMatrix, -0.01f);
        glm::mat4 tip = selectedPart->getTip(true); // FIXME later bool for stem length only
        bakeViewAdjusted = bakeViewMatrix;
        float3 u = glm::normalize((float3)tip[3] - (float3)bakeViewMatrix[3]);
        //float3 u = float3(0, 1, 0);

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

        //bakeViewAdjusted[3][0] = -d.x * 10.f;
        //bakeViewAdjusted[3][1] = -d.y * 10.f + 1000;
        //bakeViewAdjusted[3][2] = -d.z * 10.f;
        bakeViewAdjusted[3][0] = 0;
        bakeViewAdjusted[3][1] = 1000;
        bakeViewAdjusted[3][2] = 0;


        if (root)
        {
            auto BK = root->getBakeInfo(showBake);
            if (BK)
            {
                ROLL(bakeViewAdjusted, BK->yaw);
                PITCH(bakeViewAdjusted, -BK->pitch);
            }
        }

        //bakeViewAdjusted = glm::mat4(1.0);
        extents = selectedPart->calculate_extents(bakeViewAdjusted);

        glm::mat4 V, P, VP;
        V = glm::inverse(bakeViewAdjusted);
        //V = glm::mat4(1.0);
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                //             V[j][i] = _view[j][i];
            }
        }

        float W = extents.y * 1.0f;
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

    FALCOR_PROFILE("vegetation");
    renderContext = _renderContext;

    camVector = (float3(0, 1000, 0) + (float3)settings.root[1] * extents.y / 2.f) - camPos;
    m_halfAngle_to_Pixels = halfAngle_to_Pixels;
    pixelSize = m_halfAngle_to_Pixels * extents.y / glm::length(camVector);

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
        compute_calulate_lod.Vars()["gConstantBuffer"]["halfAngle_to_Pixels"] = halfAngle_to_Pixels;
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
            if (bakingView)
            {
                camPos = bakeViewAdjusted[3].xyz + bakeViewAdjusted[2].xyz * 1000.f;
                vegetationShader.Vars()["gConstantBuffer"]["windStrength"] = 0;
                vegetationShader.Vars()["gConstantBuffer"]["eyePos"] = camPos;
            }

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

        }

        {
            /*
            vegetationShader_DEBUG_PIVOTS.State()->setFbo(_fbo);
            vegetationShader_DEBUG_PIVOTS.State()->setViewport(0, _viewport, true);
            vegetationShader_DEBUG_PIVOTS.State()->setRasterizerState(rasterstate);
            vegetationShader_DEBUG_PIVOTS.State()->setBlendState(blendstate);
            vegetationShader_DEBUG_PIVOTS.Vars()["gConstantBuffer"]["viewproj"] = _viewproj;
            vegetationShader_DEBUG_PIVOTS.Vars()["gConstantBuffer"]["eyePos"] = camPos;
            vegetationShader_DEBUG_PIVOTS.Vars()["gConstantBuffer"]["time"] = time;
            vegetationShader_DEBUG_PIVOTS.Vars()["gConstantBuffer"]["windDir"] = windDir;
            vegetationShader_DEBUG_PIVOTS.Vars()["gConstantBuffer"]["windStrength"] = windStrength;
            */
        }

        {
            /*
            vegetationShader_DEBUG_PIXELS.State()->setFbo(_fbo);
            vegetationShader_DEBUG_PIXELS.State()->setViewport(0, _viewport, true);
            vegetationShader_DEBUG_PIXELS.State()->setRasterizerState(rasterstate);
            vegetationShader_DEBUG_PIXELS.State()->setBlendState(blendstate);
            vegetationShader_DEBUG_PIXELS.Vars()["gConstantBuffer"]["viewproj"] = _viewproj;
            vegetationShader_DEBUG_PIXELS.Vars()["gConstantBuffer"]["eyePos"] = camPos;
            vegetationShader_DEBUG_PIXELS.Vars()["gConstantBuffer"]["time"] = time;
            vegetationShader_DEBUG_PIXELS.Vars()["gConstantBuffer"]["windDir"] = windDir;
            vegetationShader_DEBUG_PIXELS.Vars()["gConstantBuffer"]["windStrength"] = windStrength;
            */
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
                // single plant
                /*
                if (showDebugInShader)
                    vegetationShader_DEBUG_PIXELS.drawInstanced(_renderContext, VEG_BLOCK_SIZE, totalBlocksToRender);
                else if (showNumPivots)
                    vegetationShader_DEBUG_PIVOTS.drawInstanced(_renderContext, VEG_BLOCK_SIZE, totalBlocksToRender);
                else
                */
                vegetationShader.drawInstanced(_renderContext, VEG_BLOCK_SIZE, totalBlocksToRender);
            }
            else
            {
                /*
                if (showDebugInShader)
                    vegetationShader_DEBUG_PIXELS.renderIndirect(_renderContext, drawArgs_vegetation);
                else if (showNumPivots)
                    vegetationShader_DEBUG_PIVOTS.renderIndirect(_renderContext, drawArgs_vegetation);
                else
                */
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
                    //vegetationShader.renderIndirect(_renderContext, drawArgs_vegetation, nullptr, 20, 1);
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
                pos = { d_1_1(generator) * instanceArea[0], 1000.f, d_1_1(generator) * instanceArea[0] * extents.x };
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
    vegetationShader_GOURAUD.Vars()["gConstantBuffer"]["shadowViewProj"] = _shadow_viewproj;
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

    RC->clearTexture(RGB_MAP.get());
    shadowPitch = _sunAngle;
    bakeShadowMap(RC);

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
                        compute_clearBuffers.dispatch(RC, 1, 1);

                        compute_calulate_lod.Vars()["gConstantBuffer"]["view"] = _view;
                        compute_calulate_lod.Vars()["gConstantBuffer"]["frustum"] = _clipFrustum;
                        compute_calulate_lod.Vars()["gConstantBuffer"]["eyePos"] = camPos;
                        compute_calulate_lod.Vars()["gConstantBuffer"]["lodBias"] = loddingBias;
                        //compute_calulate_lod.Vars()["gConstantBuffer"]["halfAngle_to_Pixels"] = halfAngle_to_Pixels;
                        compute_calulate_lod.Vars()["gConstantBuffer"]["firstPlant"] = firstPlant;
                        compute_calulate_lod.Vars()["gConstantBuffer"]["lastPlant"] = lastPlant;
                        compute_calulate_lod.Vars()["gConstantBuffer"]["firstLod"] = firstLod;
                        compute_calulate_lod.Vars()["gConstantBuffer"]["lastLod"] = lastLod;
                        compute_calulate_lod.dispatch(RC, MAX_PLANT_INSTANCES / 256, 1);
                    }



                    {
                        RC->clearFbo(rgbFbo.get(), float4(0, 0, 0.0, 1), 1.0f, 0, FboAttachmentType::All);
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


                        vegetationShader_RGB_SAMPLE.renderIndirect(RC, drawArgs_vegetation);
                    }

                    RC->flush(true);
                    // Now do compute into second tecture
                    {
                        compute_sampleRGBtoPixel.Vars()["gConstants"]["pix"] = uint2(x, y);
                        compute_sampleRGBtoPixel.dispatch(RC, 32, 8, 1);
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

    compute_sampleRGBtoPixel_ToTexture.dispatch(RC, 4, 2, 1);
    RC->flush(true);
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
        RC->updateTextureData(RGB_MAP.get(), dataGGX);
        RC->flush(true);
        RGB_MAP->captureToFile(0, 0, name.c_str());

        name = "e:/test_RGB/guess_" + std::to_string(sunI) + ".png";
        RC->updateTextureData(RGB_MAP.get(), data);
        RC->flush(true);
        RGB_MAP->captureToFile(0, 0, name.c_str());
    }
}
