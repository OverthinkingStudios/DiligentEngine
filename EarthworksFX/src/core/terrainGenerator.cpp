/***************************************************************************
 # Copyright (c) 2015-22, NVIDIA CORPORATION. All rights reserved.
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
#include "terrainGenerator.h"
#include "imgui.h"
#include <imgui_internal.h>

#include <filesystem>
#include <direct.h>
#include <cstdlib> // Required header

#pragma optimize("", off)

#define TOOLTIP(x)  if (ImGui::IsItemHovered()) {ImGui::SetTooltip(x);}




void terrainGenerator::onGuiMenubar(Gui* pGui)
{
}

void terrainGenerator::renderGui_rightPanel(Gui* _gui)
{
    auto& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Button] = ImVec4(0.001f, 0.001f, 0.001f, 1.0f);

    ImGui::PushFont(_gui->getFont("header1"));
    {
        if (ImGui::Button("Load", ImVec2(300, 0)))
        {
            std::filesystem::path filepath = terrain_path;
            FileDialogFilterVec filters = { {"terrainBuilder"} };
            if (openFileDialog(filters, filepath))
            {
                std::ifstream is(filepath);
                cereal::JSONInputArchive archive(is);
                archive(*this);
                changed = false;
                mapBackground = Texture::createFromFile(gis_path + name + "\\_export\\photo_index.jpg", true, true);
                mapHillshade = Texture::createFromFile(gis_path + name + "\\_export\\hillshade.jpg", true, true);
                memset(tileView, 155, 512 * 512 * 4);
                mapTiles = Texture::create2D(512, 512, Falcor::ResourceFormat::BGRA8UnormSrgb, 1, 1, tileView, Falcor::Resource::BindFlags::ShaderResource);
            }
        }
        if (changed)
        {
            style.Colors[ImGuiCol_Button] = ImVec4(0.5f, 0.5f, 0.0f, 0.7f);
        }
        if (ImGui::Button("Save", ImVec2(300, 0)))
        {
            std::filesystem::path filepath = terrain_path + "//" + name;
            if (!std::filesystem::exists(filepath))
            {
                Create_Terrain_directories();
            }

            if (std::filesystem::exists(filepath))
            {
                filepath += ".terrainBuilder";
                std::ofstream os(filepath);
                cereal::JSONOutputArchive archive(os);
                archive(*this);
                changed = false;
            }
        }

        style.Colors[ImGuiCol_Button] = ImVec4(0.001f, 0.001f, 0.001f, 1.0f);

    }
    ImGui::PopFont();

    ImGui::NewLine();
    ImGui::Text(name.c_str());
    ImGui::NewLine();

    //ImGui::Text("statistics");
}


void terrainGenerator::onGuiRender_Setup(Gui* _gui, Gui::Window& _window)
{
    auto& style = ImGui::GetStyle();
    auto& oldstyle = ImGui::GetStyle();


    {
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);

        ImGui::PushFont(_gui->getFont("header0"));
        char txt[2048];
        sprintf(txt, "%s", name.c_str());
        ImGui::SetNextItemWidth(800);
        if (ImGui::InputText("##name", txt, 256, ImGuiInputTextFlags_CharsNoBlank))
        {
            name = txt;
            changed = true;
        }
        ImGui::PopFont();

        ImGui::PushFont(_gui->getFont("header1"));
        ImGui::NewLine();
        sprintf(txt, "%s", proj4.c_str());
        ImGui::SetNextItemWidth(800);
        if (ImGui::InputText("##t_srs", txt, 2048))
        {
            proj4 = txt;
            changed = true;
        }

        if (ImGui::Checkbox("use CT instead\n(coordinate transform)", &useCT))
        {
            changed = true;
        }
        TOOLTIP("Get this value from QGIS is there are import transform warnings coming");

        sprintf(txt, "%s", CT.c_str());
        ImGui::SetNextItemWidth(800);
        if (ImGui::InputText("##ct", txt, 2048))
        {
            CT = txt;
            changed = true;
        }
        TOOLTIP("coordinate transform");


        ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
        ImGui::Text(CT.c_str());
        ImGui::PopTextWrapPos();




        ImGui::PopFont();

        ImGui::NewLine();
        ImGui::SetNextItemWidth(400);
        ImGui::DragInt("size", &size_km, 1, 10, 500, "%dkm");

        
        if (ImGui::Button("load gis root (chooseFolderDialog missing)", ImVec2(200, 0)))
        {
           /* std::filesystem::path folderpath;
            if (chooseFolderDialog(folderpath))
            {
                gis_path = folderpath.string() + "\\";
            }*/
        }
        TOOLTIP("gis terrain_path");

        ImGui::SameLine(0, 30);
        ImGui::Text(gis_path.c_str());

        ImGui::SameLine(0, 30);
        if (ImGui::Button("Create GIS directories"))
        {
            Create_GIS_directories();
        }




        if (ImGui::Button("load terrain root (chooseFolderDialog missing)", ImVec2(200, 0)))
        {
            /*std::filesystem::path folderpath;
            if (chooseFolderDialog(folderpath))
            {
                terrain_path = folderpath.string() + "\\";
            }*/
        }
        TOOLTIP("terrain terrain_path");

        ImGui::SameLine(0, 30);
        ImGui::Text(terrain_path.c_str());



        ImGui::SameLine(0, 30);
        if (ImGui::Button("Create Terrain directories"))
        {
            Create_Terrain_directories();
        }


        if (ImGui::Button("gdal path (chooseFolderDialog missing)", ImVec2(200, 0)))
        {
           /* std::filesystem::path folderpath;
            if (chooseFolderDialog(folderpath))
            {
                gdal_path = folderpath.string() + "\\";
            }*/
        }

        ImGui::SameLine(0, 30);
        ImGui::Text(gdal_path.c_str());
        style = oldstyle;
    }

}


void terrainGenerator::onGuiRender_Files(Gui* _gui, Gui::Window& _window)
{
    ImGui::PushFont(_gui->getFont("header1"));

    std::string elevation_path = gis_path + name + "\\elevation";
    std::string image_path = gis_path + name + "\\photos";
    std::string landcover_path = gis_path + name + "\\landcover";

    ImGui::Text("elevation - these have to be ordered bottom to top and teh easiets way is to name them 00_filename  01_filename etc, alphabetical");
    if (ImGui::Button("load elevation"))
    {
        elevation_files.clear();

        std::string target_ext = ".tif"; // Ensure you include the dot
        std::vector<std::filesystem::path> matching_files;

        if (std::filesystem::exists(elevation_path) && std::filesystem::is_directory(elevation_path)) {
            // Use recursive_directory_iterator to search subfolders, 
            // or directory_iterator for just the top-level folder.
            for (const auto& entry : std::filesystem::directory_iterator(elevation_path)) {
                if (entry.is_regular_file() && entry.path().extension() == target_ext) {
                    matching_files.push_back(entry.path());
                    elevation_files += entry.path().filename().string() + " ";
                }
            }
        }
    }
    TOOLTIP(elevation_path.c_str());


    ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
    ImGui::PushFont(_gui->getFont("default"));
    ImGui::Text(elevation_files.c_str());
    ImGui::PopFont();
    ImGui::PopTextWrapPos();

    if (ImGui::Button("create hillshade"))
    {
        Create_Hgt_Overview();
    }


    if (ImGui::Button("load Images"))
    {
        image_files.clear();

        std::string target_ext = ".tif"; // Ensure you include the dot
        std::vector<std::filesystem::path> matching_files;

        if (std::filesystem::exists(image_path) && std::filesystem::is_directory(image_path)) {
            // Use recursive_directory_iterator to search subfolders, 
            // or directory_iterator for just the top-level folder.
            for (const auto& entry : std::filesystem::directory_iterator(image_path)) {
                if (entry.is_regular_file() && entry.path().extension() == target_ext) {
                    matching_files.push_back(entry.path());
                    image_files += entry.path().filename().string() + " ";
                }
            }
        }
    }
    TOOLTIP(elevation_path.c_str());

    ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
    ImGui::PushFont(_gui->getFont("default"));
    ImGui::Text(image_files.c_str());
    ImGui::PopFont();
    ImGui::PopTextWrapPos();

    if (ImGui::Button("create image overview"))
    {
        Create_Image_Overview();
    }

    if (ImGui::Button("load Landcover"))
    {
        landcover_files.clear();

        std::string target_ext = ".tif"; // Ensure you include the dot
        std::vector<std::filesystem::path> matching_files;

        if (std::filesystem::exists(landcover_path) && std::filesystem::is_directory(landcover_path)) {
            // Use recursive_directory_iterator to search subfolders, 
            // or directory_iterator for just the top-level folder.
            for (const auto& entry : std::filesystem::directory_iterator(landcover_path)) {
                if (entry.is_regular_file() && entry.path().extension() == target_ext) {
                    matching_files.push_back(entry.path());
                    landcover_files += entry.path().filename().string() + " ";
                }
            }
        }
    }
    TOOLTIP(elevation_path.c_str());

    ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
    ImGui::PushFont(_gui->getFont("default"));
    ImGui::Text(landcover_files.c_str());
    ImGui::PopFont();
    ImGui::PopTextWrapPos();

    ImGui::PopFont();
}


void terrainGenerator::clickMap(float2 pos, int lod, int penSize, bool _elevation, int set)
{
    int2 lod4 = pos * 16.f;
    int2 lod6 = pos * 64.f;
    int2 lod8 = pos * 256.f;

    lod6 = glm::max(lod6, int2(1, 1));
    lod6 = glm::min(lod6, int2(62, 62));

    lod8 = glm::max(lod8, int2(2, 2));
    lod8 = glm::min(lod8, int2(253, 253));

    if (_elevation)
    {
        switch (lod)
        {
        case 0:
            tiles_hgt_4[lod4.y][lod4.x] = set;
            break;
        case 1:
            for (int i = lod6.y - 1; i <= lod6.y + 1; i++)
            {
                for (int j = lod6.x - 1; j <= lod6.x + 1; j++)
                {
                    tiles_hgt_6[i][j] = set;
                }
            }            
            break;
        case 2:
            for (int i = lod8.y - 2; i <= lod8.y + 2; i++)
            {
                for (int j = lod8.x - 2; j <= lod8.x + 2; j++)
                {
                    tiles_hgt_8[i][j] = set;
                }
            }
            break;
        }
    }
    else
    {
        switch (lod)
        {
        case 0:
            tiles_img_4[lod4.y][lod4.x] = set;
            break;
        case 1:
            for (int i = lod6.y - 1; i <= lod6.y + 1; i++)
            {
                for (int j = lod6.x - 1; j <= lod6.x + 1; j++)
                {
                    tiles_img_6[i][j] = set;
                }
            }
            break;
        case 2:
            for (int i = lod8.y - 2; i <= lod8.y + 2; i++)
            {
                for (int j = lod8.x - 2; j <= lod8.x + 2; j++)
                {
                    tiles_img_8[i][j] = set;
                }
            }
            break;
        }
    }    
}


void terrainGenerator::onGuiRender_Map(Gui* _gui, Gui::Window& _window, bool _elevation, bool _change)
{
    static int current_lod = 0; // Index pointer tracked by ImGui
    const char* items[] = { "lod-4", "lod-6", "lod-8" };

    static int current_pen = 0; // Index pointer tracked by ImGui
    const char* items_pen[] = { "1", "3", "5" };

    ImGui::SetCursorPos(ImVec2(1100, 60));
    ImGui::BeginChildFrame(15675, ImVec2(300, 1000));
    {
        ImGui::ListBox("Lod", &current_lod, items, IM_ARRAYSIZE(items), 4);
        //ImGui::ListBox("pen size", &current_pen, items_pen, IM_ARRAYSIZE(items), 4);


        switch (current_lod)
        {
        case 0:
        {
            float lod4Size = size_km * 1000.f / pow(2, 4) / 992.f;      //992 becauseof borders to 1024
            ImGui::Text("lod 4 - %2.2fm pixel size", lod4Size);
        }
            break;
        case 1:
        {
            float lod6Size = size_km * 1000.f / pow(2, 6) / 992.f;      //992 becauseof borders to 1024
            ImGui::Text("lod 6 - %2.2fm pixel size", lod6Size);
        }
            break;
        case 2:
        {
            float lod8Size = size_km * 1000.f / pow(2, 8) / 992.f;      //992 becauseof borders to 1024
            ImGui::Text("lod 8 - %2.2fm pixel size", lod8Size);
        }
            break;
        }
        
        if (_elevation)
        {
            ImGui::Text("%d tiles  %2.1fMb aproximate", totalTilesHgt, totalTilesHgt * 0.128f);
            if (ImGui::Button("export to jpeg2000"))
            {
                hgt_to_jpeg2000();
            }
        }
        else
        {
            ImGui::Text("%d tiles  %2.1fMb aproximate", totalTilesImg, totalTilesHgt * 0.128f);
            if (ImGui::Button("export to jpeg2000"))
            {
                img_to_jpeg2000();
            }
        }
        
    }
    ImGui::EndChildFrame();

    


    float mapSize = 1024;
    ImGui::SetCursorPos(ImVec2(10.f, 60.f));
    if(_elevation) _window.image("AL", mapHillshade, float2(mapSize, mapSize));
    else            _window.image("AL", mapBackground, float2(mapSize, mapSize));

    ImGui::SetCursorPos(ImVec2(10.f, 60.f));
    _window.image("tiles", mapTiles, float2(mapSize, mapSize));

    bool clicked = _change;
    if (ImGui::IsItemHovered())
    {
        float2 mouse = float2(ImGui::GetMousePos().x, ImGui::GetMousePos().y) - windowPos - float2(10, 60);
        

        
        
        if (ImGui::IsMouseDown(0))
        {
            clickMap(mouse / mapSize, current_lod, current_pen, _elevation, 1);
            clicked = true;
        }

        // clear
        if (ImGui::IsMouseDown(1))
        {
            clickMap(mouse / mapSize, current_lod, current_pen, _elevation, 0);
            clicked = true;
        }        
    }


    if (clicked)
    {
        memset(tileView, 0, 512 * 512 * 4);
        totalTilesHgt = 17;
        totalTilesImg = 17;

        for (int y = 0; y < 16; y++)
        {
            for (int x = 0; x < 16; x++)
            {
                if ((_elevation && tiles_hgt_4[y][x] > 0) || (!_elevation && tiles_img_4[y][x] > 0))
                {
                    for (int j = 0; j < 32; j++)
                    {
                        for (int i = 0; i < 32; i++)
                        {
                            uint X = x * 32 + i;
                            uint Y = y * 32 + j;
                            tileView[Y][X][0] = 255;
                            tileView[Y][X][3] = 100;
                        }
                    }
                    if (_elevation) totalTilesHgt++;
                    else            totalTilesImg++;
                }
            }
        }

        for (int y = 0; y < 64; y++)
        {
            for (int x = 0; x < 64; x++)
            {
                if ((_elevation && tiles_hgt_6[y][x] > 0) || (!_elevation && tiles_img_6[y][x] > 0))
                {
                    for (int j = 0; j < 8; j++)
                    {
                        for (int i = 0; i < 8; i++)
                        {
                            uint X = x * 8 + i;
                            uint Y = y * 8 + j;
                            tileView[Y][X][0] /= 2;
                            tileView[Y][X][1] = 255;
                            tileView[Y][X][3] = 100;
                        }
                    }
                    if (_elevation) totalTilesHgt++;
                    else            totalTilesImg++;
                }
            }
        }

        for (int y = 0; y < 256; y++)
        {
            for (int x = 0; x < 256; x++)
            {
                if ((_elevation && tiles_hgt_8[y][x] > 0) || (!_elevation && tiles_img_8[y][x] > 0))
                {
                    for (int j = 0; j < 2; j++)
                    {
                        for (int i = 0; i < 2; i++)
                        {
                            uint X = x * 2 + i;
                            uint Y = y * 2 + j;
                            tileView[Y][X][0] /= 2;
                            tileView[Y][X][1] /= 2;
                            tileView[Y][X][2] = 255;
                            tileView[Y][X][3] = 100;
                        }
                    }
                    if (_elevation) totalTilesHgt++;
                    else            totalTilesImg++;
                }
            }
        }

        _renderContext->updateTextureData(mapTiles.get(), tileView);
    }

    changed |= clicked;
}

/*
try {
    std::ifstream is("data.json");
    cereal::JSONInputArchive archive(is);
    
    MyClass myObject;
    archive(myObject); // Deserialization occurs here
} 
catch (const cereal::Exception& e) {
    std::cerr << "Cereal Serialization Error: " << e.what() << '\n';
    // Handle error (e.g., reset to default values, log telemetry)
}
catch (const std::exception& e) {
    std::cerr << "Standard Exception: " << e.what() << '\n';
}
*/

void terrainGenerator::onGuiRender_Landcover(Gui* _gui, Gui::Window& _window)
{
}



void terrainGenerator::onGuiRender(Gui* _gui, int _header, float2 _screenSize)
{
    auto& style = ImGui::GetStyle();
    auto& oldstyle = ImGui::GetStyle();

    style.FramePadding = ImVec2(10, 10);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.035f, 0.025f, 0.02f, 1.0f);
    Gui::Window builderPanel(_gui, "terrain builder", { 900, 900 }, { 100, 100 }, Falcor::Gui::WindowFlags::NoResize);
    {
        builderPanel.windowPos(0, _header);
        builderPanel.windowSize((int)_screenSize.x, (int)_screenSize.y - _header); // FIXME teh 300 should be right panel
        windowPos = float2(0, _header);

        ImGui::PushFont(_gui->getFont("header1"));
        if (ImGui::BeginTabBar("MyTabBarID"))
        {
            if (ImGui::BeginTabItem("Setup")) {
                onGuiRender_Setup(_gui, builderPanel);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Input data")) {
                onGuiRender_Files(_gui, builderPanel);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Elevation")) {
                onGuiRender_Map(_gui, builderPanel, true, !ImGui::IsItemActivated());
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Image")) {
                onGuiRender_Map(_gui, builderPanel, false, !ImGui::IsItemActivated());
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Landcover")) {
                onGuiRender_Landcover(_gui, builderPanel);
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();         // Always close your TabBar
        }
        ImGui::PopFont();
    }
    builderPanel.release();

    style = oldstyle;
}



void terrainGenerator::Create_GIS_directories()
{
    _chdir(gis_path.c_str());
    system(("mkdir " + name).c_str());

    _chdir((gis_path + name).c_str());
    {
        system("mkdir _export");
        system("mkdir _temp");
        system("mkdir elevation");
        system("mkdir landcover");
        system("mkdir photos");
        system("mkdir vector");
    }
}


//mkdir -p project/{src,docs,tests}/{subfolder1,subfolder2}
void terrainGenerator::Create_Terrain_directories()
{
    _chdir(terrain_path.c_str());
    system(("mkdir " + name).c_str());

    //system("mkdir -p %s//{elevation, bake, ecosystem, terrafectors, _export, overlay, gis, roads}", name.c_str());

    _chdir((terrain_path + name).c_str());
    {
        system("mkdir elevation");
        system("mkdir bake");
        system("mkdir ecosystem");
        system("mkdir terrafectors");
        system("mkdir _export");
        system("mkdir overlay");
        system("mkdir gis");
        system("mkdir roads");

        {
            _chdir((terrain_path + name + "/terrafectors").c_str());
            system("mkdir 10_bakeOnlyBottom");
            system("mkdir 20_base");
            system("mkdir 30_roads");
            system("mkdir 40_bakeOnlyTop");
            system("mkdir 50_top");
            system("mkdir 60_overlay");
        }

        {
            _chdir((terrain_path + name + "/_export").c_str());
            system("mkdir bridges");
            system("mkdir roads");
            system("mkdir scene");
            system("mkdir tiles");
        }

        {
            _chdir((terrain_path + name + "/gis").c_str());
            system("mkdir elevation");
            system("mkdir photos");
            system("mkdir _export");
            system("mkdir _temp");
            {
            }
        }
    }
}

void terrainGenerator::Create_Image_Overview()
{
    float half = size_km * 1000.f / 2.f;
    std::string cmd = gdal_path + "gdalwarp ";
    cmd += " -t_srs \"" + proj4 + "\" ";
    cmd += " -te " + std::to_string(-half) + " " + std::to_string(-half) + " " + std::to_string(half) + " " + std::to_string(half);
    cmd += " -ts 2048 2048";
    cmd += " -r cubicspline -multi -overwrite -ot byte -of jpeg ";
    cmd += image_files;
    cmd += gis_path + name + "\\_export\\photo_index.jpg";

    _chdir((gis_path + name + "//photos").c_str());
    system(cmd.c_str());
}


void terrainGenerator::Create_Hgt_Overview()
{
    float half = size_km * 1000.f / 2.f;
    std::string cmd = gdal_path + "gdalwarp ";
    cmd += " -t_srs \"" + proj4 + "\" ";
    if (useCT)
    {
        cmd += " -ct \"" + CT + "\" ";
    }
    else
    {
        cmd += " -t_srs \"" + proj4 + "\" ";
    }
    cmd += " -te " + std::to_string(-half) + " " + std::to_string(-half) + " " + std::to_string(half) + " " + std::to_string(half);
    cmd += " -ts 2048 2048";
    cmd += " -r cubicspline -multi -overwrite -ot Float32 ";
    cmd += elevation_files;
    cmd += gis_path + name + "\\_export\\hgt_2k.bil";

    _chdir((gis_path + name + "//elevation").c_str());
    system(cmd.c_str());

    cmd = gdal_path + "gdaldem hillshade -of jpeg hgt_2k.bil  hillshade.jpg";
    _chdir((gis_path + name + "//_export").c_str());
    system(cmd.c_str());
}




void terrainGenerator::hgt_to_jpeg2000()
{
}

void terrainGenerator::hgt_tile_gdal(int lod, int y, int x)
{
}

void terrainGenerator::img_to_jpeg2000()
{
}
