#include "ecotope.h"

#include "cereal/archives/binary.hpp"
#include "cereal/archives/json.hpp"
#include "cereal/archives/xml.hpp"
#include <sstream>
#include <fstream>
#include "imgui.h"

#pragma optimize("", off)

#include "vegetationBuilder.h"

namespace
{

bool splitAtTerrains(const std::filesystem::path& path,
                     std::filesystem::path& prefix,
                     std::filesystem::path& fromTerrains)
{
    prefix.clear();
    fromTerrains.clear();
    bool foundTerrains = false;
    for (const auto& component : path)
    {
        if (!foundTerrains)
        {
            if (component == "terrains")
            {
                foundTerrains = true;
                fromTerrains = component;
            }
            else
            {
                prefix /= component;
            }
        }
        else
        {
            fromTerrains /= component;
        }
    }
    return foundTerrains;
}

std::filesystem::path resolveStoredPath(const std::filesystem::path& storedPath,
                                        const std::filesystem::path& ecosystemFilePath)
{
    if (storedPath.empty() || std::filesystem::exists(storedPath))
        return storedPath;

    if (!storedPath.is_absolute())
        return storedPath;

    std::filesystem::path storedPrefix;
    std::filesystem::path storedFromTerrains;
    std::filesystem::path ecosystemPrefix;
    std::filesystem::path ecosystemFromTerrains;
    if (!splitAtTerrains(storedPath, storedPrefix, storedFromTerrains))
        return storedPath;
    if (!splitAtTerrains(ecosystemFilePath, ecosystemPrefix, ecosystemFromTerrains))
        return storedPath;

    const std::filesystem::path resolved = ecosystemPrefix / storedFromTerrains;
    if (std::filesystem::exists(resolved))
        return resolved;

    return storedPath;
}

} // namespace

/*
uint spriteCache::find_insert_plant(const std::filesystem::path _path)
{
    return 0;
}

void spriteCache::setTextures(ShaderVar& var)
{

}

void spriteCache::rebuildStructuredBuffer()
{

}
*/


std::string ecotopeSystem::resPath;

float ecotopeSystem::terrainSize;
_rootPlant* ecotopeSystem::pVegetation;


ecotope::ecotope() {
    for (int j = 0; j < 6; j++)
    {
        weights[j][0] = 0;
        weights[j][1] = 0;
        weights[j][2] = 0;
        weights[j][3] = 0;
    }
}


void ecotope::loadTexture(int i) {

    //std::string filename, fullpath;
    //if (openFileDialog("Supported Formats\0*.dds;\0\0", filename))
    std::filesystem::path path;
    FileDialogFilterVec filters = { {"dds"} };
    if (openFileDialog(filters, path))
    {
        //if (findFileInDataDirectories(filename, fullpath) == true)
        {
            switch (i) {
            case 0: albedoName = path.string(); break;
            case 1: noiseName = path.string(); break;
            case 2: displacementName = path.string(); break;
            case 3: roughnessName = path.string(); break;
            case 4: aoName = path.string(); break;
            }

            reloadTextures(ecotopeSystem::resPath);
        }
    }
}

void ecotope::reloadTextures(std::string _resPath)
{
    //fprintf(terrafectorSystem::_logfile, "loading ecotope resources from  %s\n", resPath.c_str());

    texAlbedo = Texture::createFromFile(_resPath + albedoName, true, false);
    texNoise = Texture::createFromFile(_resPath + noiseName, false, false);
    texDisplacement = Texture::createFromFile(_resPath + displacementName, false, false);
    texRoughness = Texture::createFromFile(_resPath + roughnessName, false, false);
    texAO = Texture::createFromFile(_resPath + aoName, false, false);
}

std::array<std::string, 5> tfWeights = { "Height", "Concavity", "Slope", "Aspect", "Texture" };
std::array<std::string, 5> tfTexHeading = { "albedo", "noise", "displacement", "roughness", "ao" };
void ecotope::GuiTextures(Gui* _pGui)
{
    ImGui::Text("albedo");
    ImGui::SameLine(140);
    if (ImGui::Selectable(albedoName.size() ? albedoName.c_str() : tfTexHeading[0].c_str())) { loadTexture(0); }
    ImGui::Text("noise");
    ImGui::SameLine(140);
    if (ImGui::Selectable(noiseName.size() ? noiseName.c_str() : tfTexHeading[1].c_str())) { loadTexture(1); }
    ImGui::Text("displacement");
    ImGui::SameLine(140);
    if (ImGui::Selectable(displacementName.size() ? displacementName.c_str() : tfTexHeading[2].c_str())) { loadTexture(2); }
    ImGui::Text("roughness");
    ImGui::SameLine(140);
    if (ImGui::Selectable(roughnessName.size() ? roughnessName.c_str() : tfTexHeading[3].c_str())) { loadTexture(3); }
    ImGui::Text("ao");
    ImGui::SameLine(140);
    if (ImGui::Selectable(aoName.size() ? aoName.c_str() : tfTexHeading[4].c_str())) { loadTexture(4); }

    ImGui::NewLine();
}

void ecotope::GuiWeights(Gui* _pGui)
{

    for (int i = 0; i < 5; i++) {
        ImGui::PushID(i);
        //ImGui::Text(tfWeights[i].c_str());
        if (ImGui::SliderFloat(tfWeights[i].c_str(), &weights[i][0], 0, 1, "%.2f")) hasChanged = true;
        if (weights[i][0] > 0) {
            switch (i) {
            case 0:
                if (ImGui::DragFloat("###a", &weights[i][1], 1, 0, 1000, "bottom %3.0f m")) hasChanged = true;
                if (ImGui::DragFloat("###b", &weights[i][2], 1, 10, 500, "top %3.0f m")) hasChanged = true;
                if (ImGui::DragFloat("###c", &weights[i][3], 0.1f, 0.1f, 20, "smooth %.1f")) hasChanged = true;
                break;
            case 1:
                if (ImGui::DragFloat("###a", &weights[i][1], 1, -50, 50, "disp %3.1f m")) hasChanged = true;
                if (ImGui::DragFloat("###b", &weights[i][2], 1, -200, 200, "bias %3.1f m")) hasChanged = true;
                if (ImGui::DragFloat("###c", &weights[i][3], 1, 0, 8, "mip %3.1f m")) hasChanged = true;
                break;
            case 2:
                if (ImGui::DragFloat("min", &weights[i][1], 0.01f, 0, 1, "%3.2f")) hasChanged = true;
                if (ImGui::DragFloat("max", &weights[i][2], 0.01f, 0, 1, "%3.2f")) hasChanged = true;
                if (ImGui::DragFloat("smooth", &weights[i][3], 0.1f, 0.1f, 20, "%.1f")) hasChanged = true;
                break;
            case 3:
                if (ImGui::DragFloat("###a", &weights[i][1], 0.01f, -1, 1, "dX %.2f")) {
                    glm::vec3 A = glm::normalize(glm::vec3(weights[3][1], weights[3][2], weights[3][3]));

                    weights[3][1] = A.x;
                    weights[3][2] = A.y;
                    weights[3][3] = A.z;
                    hasChanged = true;
                }
                if (ImGui::DragFloat("###b", &weights[i][2], 0.01f, -1, 1, "dY %.2f")) {
                    glm::vec3 A = glm::normalize(glm::vec3(weights[3][1], weights[3][2], weights[3][3]));
                    weights[3][1] = A.x;
                    weights[3][2] = A.y;
                    weights[3][3] = A.z;
                    hasChanged = true;
                }
                if (ImGui::DragFloat("###c", &weights[i][3], 0.01f, -1, 1, "dZ %.2f")) {
                    glm::vec3 A = glm::normalize(glm::vec3(weights[3][1], weights[3][2], weights[3][3]));
                    weights[3][1] = A.x;
                    weights[3][2] = A.y;
                    weights[3][3] = A.z;
                    hasChanged = true;
                }
                break;
            case 4:
                if (ImGui::DragFloat("###c", &weights[i][1], 1, 1, 100, "%.2f uv")) hasChanged = true;
                if (ImGui::DragFloat("###a", &weights[i][2], 0.01f, 0, 1, "%.2f scale")) hasChanged = true;
                if (ImGui::DragFloat("###b", &weights[i][3], 0.01f, 0, 1, "%.2f offset")) hasChanged = true;
                break;
            }
        }
        ImGui::PopID();

        ImGui::NewLine();
    }

}

void ecotope::addPlant()
{
    //std::string filename, fullpath;
    //if (openFileDialog("Supported Formats\0*.fbx;\0\0", filename))		//??? format
    std::filesystem::path path;
    //FileDialogFilterVec filters = { {"plant"}, {"sprite"}, {"fbx"}, {"png"}, {"someEVoFormat"} };
    FileDialogFilterVec filters = { {"binary"} };
    if (openFileDialog(filters, path))
    {
        {
            _displayPlant P;
            P.path = path.string();
            P.name = P.path.substr(P.path.find_last_of("/\\") + 1);
            selectedPlant = plants.size();
            /*
            if (path.string().find("sprite") != std::string::npos)
            {
                const std::string I = P.name.substr(0, P.name.find_last_of("_"));
                P.index = std::stoi(I);
            }
            */
            int idx =  ecotopeSystem::pVegetation->importBinary(path);   // return vlaue still wronf since we load 4 variations
            if (idx >= 0)
            {
                P.index = idx;
                plants.push_back(P);
            }
        }
    }
}

void ecotope::GuiPlants(Gui* _pGui)
{
    if (ImGui::Button("add plant")) {
        addPlant();
    }

    ImGui::NewLine();

    for (uint lod = 0; lod < 16; lod++)
    {
        bool use = false;
        for (auto P : plants) { if (P.lod == lod) use = true; }

        if (use)
        {
            ImGui::NewLine();
            ImGui::Text("lod - %d", lod);
            for (int i = 0; i < plants.size(); i++)
            {
                if (plants[i].lod == lod) {
                    if (i == selectedPlant) {
                        ImGui::PushFont(_pGui->getFont("roboto_20_bold"));
                        ImGui::Text("%s", plants[i].name.c_str());
                        ImGui::PopFont();
                    }
                    else {
                        char name[256];
                        sprintf(name, "%s##%d", plants[i].name.c_str(), i);
                        if (ImGui::Selectable(name))
                        {
                            selectedPlant = i;
                        }
                    }
                }
            }
        }
    }
}


void ecotope::renderGUI(Gui* _pGui) {
    ImGui::PushFont(_pGui->getFont("header1"));
    ImGui::Text(name.c_str());
    ImGui::PopFont();
    ImGui::NewLine();

    ImGui::PushFont(_pGui->getFont("default"));
    {
        //Feedback
        
        GuiTextures(_pGui);

        ImGui::Columns(3);
        GuiWeights(_pGui);
        ImGui::NextColumn();
        GuiPlants(_pGui);
        ImGui::NextColumn();
        renderPlantGUI(_pGui);
    }
    ImGui::PopFont();
}



void ecotope::renderPlantGUI(Gui* _gui)
{
    if (selectedPlant < plants.size())
    {
        //Gui::Window plantPanel(_gui, "Plant##tfPanel", { 900, 900 }, { 100, 100 });
        //{
        ImGui::Text("index : %d", plants[selectedPlant].index);
        ImGui::Text(plants[selectedPlant].path.c_str());
        ImGui::Text(plants[selectedPlant].name.c_str());
        ImGui::DragFloat("relative density", &plants[selectedPlant].density, 0.001f, 0.0f, 1.0f);
        //ImGui::DragFloat("scale", &plants[selectedPlant].scale, 0, 1000, 0.1f);
        ImGui::DragFloat("variance", &plants[selectedPlant].scaleVariation, 0, 1000, 0.1f);
        ImGui::DragInt("lod", &plants[selectedPlant].lod, 0.1f, 2, 16);
        ImGui::Text("pixelSize for lod %d = %f", plants[selectedPlant].lod, ecotopeSystem::terrainSize / 248.0f / pow(2, plants[selectedPlant].lod));
        ImGui::Text("spacing for density %3.2f = %3.2f m between plants", plants[selectedPlant].density, ecotopeSystem::terrainSize / 248.0f / pow(2, plants[selectedPlant].lod) / plants[selectedPlant].density);
        ImGui::Text("integer density %d / 64", plants[selectedPlant].percentageOfLodTotalInt);
        ImGui::Text("idx  %d", plants[selectedPlant].index);
        //}
        //plantPanel.release();
    }
}




void ecotopeSystem::load()
{
    // FIXME broken find a nicver way for respath
    std::filesystem::path path;
    FileDialogFilterVec filters = { {"ecosystem"} };
    if (openFileDialog(filters, path))
    {
        load(path.string(), ecotopeSystem::resPath);
    }
    
}


void ecotopeSystem::load(std::string _path, std::string _resourcePath)
{
    resPath = _resourcePath;
    ecosystemFilePath = std::filesystem::path(_path);
    std::ifstream is(_path);
    cereal::JSONInputArchive archive(is);
    serialize(archive);
    rebuildRuntime(ecosystemFilePath);

    for (int ect = 0; ect < ecotopes.size(); ect++)
    {
        ecotopes[ect].reloadTextures(ecotopeSystem::resPath);
    }
}


void ecotopeSystem::save() {
    std::filesystem::path path;
    FileDialogFilterVec filters = { {"ecosystem"} };
    if (saveFileDialog(filters, path))
    {
        std::ofstream os(path.string());
        cereal::JSONOutputArchive archive(os);
        serialize(archive);
    }
}



void ecotopeSystem::renderGUI(Gui* _pGui)
{
    auto& style = ImGui::GetStyle();
    ImGuiStyle oldStyle = style;
    style.ButtonTextAlign = ImVec2(0, 0);
    style.Colors[ImGuiCol_Button] = ImVec4(0.0f, 0.0f, 0.0f, 0.9f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.1f, 0.3f, 0.2f, 0.7f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    float W = ImGui::GetWindowWidth();
    ImGui::NewLine();


    change = false;
    ImGui::PushFont(_pGui->getFont("bold"));
    
    

    for (int i = 0; i < ecotopes.size(); i++) {

        if (i % 3 == 0) ImGui::NewLine();

        if (i == selectedEcotope) {
            style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            {
                char nameEdit[1024];
                sprintf(nameEdit, "%s", ecotopes[i].name.c_str());
                if (ImGui::InputText("", nameEdit, 1024)) {
                    ecotopes[i].name = nameEdit;
                }
            }
        }
        else {
            style.Colors[ImGuiCol_Text] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
            char nameEdit[1024];
            sprintf(nameEdit, "%s", ecotopes[i].name.c_str());
            if (ImGui::Button(nameEdit, ImVec2(W - 10, 30))) {
                selectedEcotope = i;
                
                if (showDebug)
                {
                    constantbuffer.debug = selectedEcotope;
                    change = true;
                }
            }
        }
    }
    ImGui::PopFont();

    ImGui::NewLine();
    if (ImGui::Button("rebuild GPU buffers")) {
        change = true;
    }
    //if (ImGui::DragInt("debug", &constantbuffer.debug, 1, 0, 999)) {
    //    change = true;
    //}
    if (ImGui::Checkbox("show debug", &showDebug))
    {
        if (showDebug) {
            constantbuffer.debug = selectedEcotope;
        }
        else {
            constantbuffer.debug = -1;
        }
        change = true;
    }

    style = oldStyle;


    for (auto& ect : ecotopes)
    {
        change |= ect.hasChanged;
        ect.hasChanged = false;
    }
    if (change)
    {
        rebuildRuntime(ecosystemFilePath);
    }
}


void ecotopeSystem::renderSelectedGUI(Gui* _pGui)
{
    if (selectedEcotope < ecotopes.size()) {
        ecotopes[selectedEcotope].renderGUI(_pGui);
    }
}

void ecotopeSystem::renderPlantGUI(Gui* _pGui)
{
    //	if (selectedEcotope < ecotopes.size()) {
    //		ecotopes[selectedEcotope].renderPlantGUI(_pGui);
    //	}
}

void ecotopeSystem::addEcotope() {
    ecotope E;
    ecotopes.push_back(E);
}



void ecotopeSystem::rebuildRuntime(const std::filesystem::path& ecosystemPath) {

    change = true;

    constantbuffer.numEcotopes = ecotopes.size();
    //constantbuffer.debug = 0;
    //constantbuffer.pixelSize = 999;

    //constantbuffer.lowResOffset = 999;
    //constantbuffer.lowResSize = 999;

    //constantbuffer.tileXY = 999;

    //memset(plantIndex, 0, sizeof(uint) * 24 * 16 * 64);
    //memset(plantDensity, 0, sizeof(uint) * 24 * 16);

    for (int e = 0; e < 24; e++)
    {
        for (int l = 0; l < 16; l++)
        {
            plantDensity[e][l] = 0;
            for (int p = 0; p < 64; p++)
            {
                plantIndex[e][l][p] = 65535;
            }
        }
    }

    for (int ect = 0; ect < constantbuffer.numEcotopes; ect++)
    {
        constantbuffer.ect[ect][0] = float4(ecotopes[ect].weights[0][0], ecotopes[ect].weights[0][1], ecotopes[ect].weights[0][2], ecotopes[ect].weights[0][3]);
        constantbuffer.ect[ect][1] = float4(ecotopes[ect].weights[1][0], ecotopes[ect].weights[1][1], ecotopes[ect].weights[1][2], ecotopes[ect].weights[1][3]);
        constantbuffer.ect[ect][2] = float4(ecotopes[ect].weights[2][0], ecotopes[ect].weights[2][1], ecotopes[ect].weights[2][2], ecotopes[ect].weights[2][3]);
        constantbuffer.ect[ect][3] = float4(ecotopes[ect].weights[3][0], ecotopes[ect].weights[3][1], ecotopes[ect].weights[3][2], ecotopes[ect].weights[3][3]);
        constantbuffer.ect[ect][4] = float4(ecotopes[ect].weights[4][0], ecotopes[ect].weights[4][1], ecotopes[ect].weights[4][2], ecotopes[ect].weights[4][3]);

        constantbuffer.texScales[ect] = float4(4.0f, 0.2f, 0, 0);     // 4m, 20cm displacement


        
        for (int lod = 0; lod < 16; lod++)
        {


            ecotopes[ect].totalPlantDensity[lod] = 0;
            for (auto& P : ecotopes[ect].plants)
            {
                if (P.lod == lod) {
                    ecotopes[ect].totalPlantDensity[lod] += P.density;
                }
            }
            saturate(ecotopes[ect].totalPlantDensity[lod]); // clamp 0-1
            /*if (ecotopes[ect].totalPlantDensity[lod] > 0)
            {
                if (ecotopes[ect].totalPlantDensity[lod] > 1)
                {
                    ecotopes[ect].totalPlantDensity[lod] = 1;
                }
            }*/

            //constantbuffer.totalDensity[i][lod].x = (uint)(__min(ecotopes[i].totalPlantDensity[lod], 1.0f) * 65535);
            plantDensity[ect][lod] = (uint)(__min(ecotopes[ect].totalPlantDensity[lod], 1.0f) * 65535);

            int slotCount = 0;
            for (auto& P : ecotopes[ect].plants)
            {
                if (P.lod == lod) {
                    //const std::string I = P.name.substr(0, P.name.find_last_of("_"));
                    //P.index = std::stoi(I);
                    // FIXME importBinary must not load the same one again needs a cache if I am going to use it like this
                    const std::filesystem::path plantPath = resolveStoredPath(P.path, ecosystemPath);
                    if (plantPath != P.path)
                        P.path = plantPath.string();

                    int idx = ecotopeSystem::pVegetation->importBinary(plantPath);   // return vlaue still wronf since we load 4 variations
                    if (idx >= 0)
                    {
                        P.index = idx;
                    }
                    // FISME use of (int) allows this to round down to fewer than 64 spots and that last 0 will cause trouble
                    P.percentageOfLodTotalInt = (int)(P.density / ecotopes[ect].totalPlantDensity[lod] * 64.0f);
                    int to = __min(64, slotCount + P.percentageOfLodTotalInt);
                    for (int j = slotCount; j < to; j++) {
                        plantIndex[ect][lod][j] = P.index;
                        slotCount++;
                    }
                }
            }
        }
    }

    if (piBuffer == nullptr)
    {
        piBuffer = Buffer::createTyped(Diligent::TEX_FORMAT_R32_UINT, sizeof(uint) * 12 * 16 * 65, Resource::BindFlags::ShaderResource);
    }
    piBuffer->setBlob(plantIndex, 0, sizeof(uint) * 24 * 16 * 64);

    if (pdBuffer == nullptr)
    {
        pdBuffer = Buffer::createTyped(Diligent::TEX_FORMAT_R32_UINT, sizeof(uint) * 12 * 16 * 65, Resource::BindFlags::ShaderResource);
    }
    pdBuffer->setBlob(plantDensity, 0, sizeof(uint) * 24 * 16);
}


/*
void ecotopeSystem::resetPlantIndex(uint lod)
{
    for (int i = 0; i < constantbuffer.numEcotopes; i++)
    {
        int slotCount = 0;
        for (auto& P : ecotopes[i].plants)
        {
            if (P.lod == lod) {
                P.percentageOfLodTotalInt = (int)(P.density / ecotopes[i].totalPlantDensity[lod] * 64.0f);
                int to = __min(64, slotCount + P.percentageOfLodTotalInt);
                for (int j = slotCount; j < to; j++) {
                    constantbuffer.plantIndex[i][j].x = 34;// P.index;
                }
            }
        }
    }

}

*/
