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

#include <thread>

 //#pragma optimize("", off)

#define TOOLTIP(x)  if (ImGui::IsItemHovered()) {ImGui::SetTooltip(x);}


const char* items[] = { "lod-4", "lod-6", "lod-8" };
const char* items_pen[] = { "1", "3", "5" };
const char* map_operator[] = { " --operator=multiply ", " --operator=hsv-value ", " --operator=hard-light ", " --operator=overlay " };
const char* map_percentage[] = { " --opacity=0 ", " --opacity=25 ", " --opacity=50 ", " --opacity=75 ",  "--opacity=100" };


void jp2Map_Gen::set(uint _lod, uint _y, uint _x, float _wSize, float _wOffset)
{
    lod = _lod;
    y = _y;
    x = _x;

#define tile_toBorder 256.0f/248.0f

    float scale = 1.f / (float)pow(2, lod);
    float sizeT = _wSize * scale;
    float sizeTotal = sizeT * tile_toBorder;
    float sizeBorder = (sizeTotal - sizeT) / 2.f;

    size = sizeTotal;
    origin.x = _wOffset - sizeBorder + (x * sizeT);
    origin.y = _wOffset - sizeBorder + (y * sizeT);
}


void jp2Map_Gen::save(std::ofstream& _os)
{
    _os << lod << " " << y << " " << x << " " << origin.x << " " << origin.y << " " << size << " ";
    _os << hgt_offset << " " << hgt_scale << " " << fileOffset << "\n";
}

void jp2Map_Gen::saveBinary(std::ofstream& _os)
{
    _os << lod << y << x << origin.x << origin.y << size << hgt_offset << hgt_scale << fileOffset;
}





void jp2File_Gen::save(std::ofstream& _os)
{
    _os << filename << "\n";
    for (auto& T : tiles)
    {
        T.save(_os);
    }
}

void jp2File_Gen::saveBinary(std::ofstream& _os)
{
    _os << filename << "\n";
    uint numTiles = tiles.size();
    _os << numTiles;
    for (auto& T : tiles)
    {
        T.saveBinary(_os);
    }
}





void jp2Dir_Gen::save(std::string _name)
{
    std::ofstream os(_name.c_str());
    if (os.good()) {
        cereal::JSONOutputArchive archive(os);
        serialize(archive);
    }
}





void jp2Dir_Gen::saveBinary(std::string _name)
{
    std::ofstream os(_name.c_str(), std::ios::binary);
    if (os.good()) {
        uint numFiles = files.size();
        os << numFiles;
        for (auto F : files)
        {
            F.saveBinary(os);
        }
    }
}












void terrainGenerator::onGuiMenubar(Gui* pGui)
{
}

void terrainGenerator::renderGui_rightPanel(Gui* _gui)
{
    auto& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Button] = ImVec4(0.001f, 0.001f, 0.001f, 1.0f);
    style.FramePadding = ImVec2(10, 10);

    ImGui::PushFont(_gui->getFont("header1"));
    {
        if (ImGui::Button("Load", ImVec2(300, 0)))
        {
            std::filesystem::path filepath = paths.terrains;
            FileDialogFilterVec filters = { {"terrainBuilder"} };
            if (openFileDialog(filters, filepath))
            {
                std::ifstream is(filepath);
                cereal::JSONInputArchive archive(is);
                archive(*this);
                changed = false;
                mapBackground = Texture::createFromFile(paths.gis_data + name + "\\_export\\blended_index.jpg", true, true);

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
            std::filesystem::path filepath = paths.terrains + "//" + name;
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
    

    switch (currentTab)
    {
    case _tabs::Setup:
        onGuiRender_Setup_Right(_gui);
        break;
    case _tabs::Files:
        break;
    case _tabs::Test:
        break;
    case _tabs::Elevation:
        onGuiRender_Map_Right(_gui, true);
        break;
    case _tabs::Image:
        onGuiRender_Map_Right(_gui, false);
        break;
    case _tabs::Landcover:
        break;
    }

    
}




void terrainGenerator::onGuiRender_Setup(Gui* _gui, Gui::Window& _window)
{
    auto& style = ImGui::GetStyle();
    auto& oldstyle = ImGui::GetStyle();
    char txt[2048];

    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.01f, 0.01f, 0.01f, 1.0f);
    style.FramePadding = ImVec2(10, 10);

    ImGui::PushFont(_gui->getFont("header0"));
    {
        sprintf(txt, "%s", name.c_str());
        ImGui::SetNextItemWidth(800);
        if (ImGui::InputText("##name", txt, 256, ImGuiInputTextFlags_CharsNoBlank))
        {
            name = txt;
            changed = true;
        }
    }
    ImGui::PopFont();

    ImGui::NewLine();
    ImGui::SetNextItemWidth(200);
    ImGui::DragInt("size", &size_km, 1, 10, 500, "%dkm");
    ImGui::PushFont(_gui->getFont("default"));
    {
        ImGui::Text("Terrain is always a square.");
        ImGui::Text("If your data is odly shaped, use the drawing tools under elevation and image tabs to restrict");
        ImGui::Text("the data to an arbitrary shape.");
    }
    ImGui::PopFont();

    ImGui::PushFont(_gui->getFont("header1"));
    ImGui::NewLine();
    ImGui::Text("proj-4");
    ImGui::PushFont(_gui->getFont("default"));
    {
        ImGui::Text("Proj-4 transform. Use QGis, GDAL or a similar resource to find valid transforms,");
        ImGui::Text("or use a standard transverse mercator changing latt long to reflect the center of your world. It is appropriate for most terrains.");
    }
    ImGui::PopFont();

    {
        ImGui::Checkbox("transverse mercator", &useTransverseMercator);
        if (useTransverseMercator)
        {
            ImGui::SameLine(0, 30);
            ImGui::SetNextItemWidth(200);
            ImGui::InputFloat("latt", &latt);
            ImGui::SameLine(0, 30);
            ImGui::SetNextItemWidth(200);
            ImGui::InputFloat("lon", &lon);

            proj4 = "+proj=tmerc +lat_0=" + std::to_string(latt) + " +lon_0=" + std::to_string(lon) + " +k_0=1 + x_0=0 + y_0=0 +ellps=GRS80 +units=m";
        }
    }

    sprintf(txt, "%s", proj4.c_str());
    ImGui::SetNextItemWidth(1600);
    if (ImGui::InputText("##t_srs", txt, 2048))
    {
        proj4 = txt;
        changed = true;
    }

    ImGui::NewLine();
    ImGui::NewLine();


    if (ImGui::Button("gis ...", ImVec2(100, 0)))
    {
        std::filesystem::path folderpath;
        if (chooseFolderDialog(folderpath))
        {
            paths.gis_data = folderpath.string() + "\\";
            savePaths();
        }
    }
    ImGui::SameLine(0, 30);
    ImGui::Text(paths.gis_data.c_str());

    if (ImGui::Button("terrain ...", ImVec2(100, 0)))
    {
        std::filesystem::path folderpath;
        if (chooseFolderDialog(folderpath))
        {
            paths.terrains = folderpath.string() + "\\";
            savePaths();
        }
    }
    ImGui::SameLine(0, 30);
    ImGui::Text(paths.terrains.c_str());

    if (ImGui::Button("gdal ...", ImVec2(100, 0)))
    {
        std::filesystem::path folderpath;
        if (chooseFolderDialog(folderpath))
        {
            paths.gdal_bin = folderpath.string() + "\\";
            savePaths();
        }
    }
    ImGui::SameLine(0, 30);
    ImGui::Text(paths.gdal_bin.c_str());




    ImGui::NewLine();

    if (ImGui::Button("Create GIS directories"))
    {
        Create_GIS_directories();
    }
    ImGui::SameLine(0, 30);
    if (ImGui::Button("Create Terrain directories"))
    {
        Create_Terrain_directories();
    }

    style = oldstyle;
}


void terrainGenerator::onGuiRender_Files(Gui* _gui, Gui::Window& _window)
{
    ImGui::PushFont(_gui->getFont("header1"));

    std::string elevation_path = paths.gis_data + name + "\\elevation";
    std::string image_path = paths.gis_data + name + "\\orthophotos";
    std::string landcover_path = paths.gis_data + name + "\\landcover";

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

    


    if (ImGui::Button("load Images"))
    {
        image_files.clear();

        std::string target_ext = ".jpg"; // Ensure you include the dot
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



void terrainGenerator::onGuiRender_Test(Gui* _gui, Gui::Window& _window)
{
    static bool showImg = true;
    static int sizeLod = 8;
    static float2 center = { 0, 0 };

    ImGui::SetNextItemWidth(200);
    ImGui::DragInt("lod", &sizeLod, 1, 4, 10);
    float size = size_km * 1000.f / pow(2.f, sizeLod);
    ImGui::Text("size = %2.1fm, pixel size = %2.2fcm", size, size / 1024.f * 100.f);
    float half = size_km * 1000.f / 2.f;
    ImGui::SetNextItemWidth(200);
    ImGui::DragFloat2("center", &center.x, 100, -half, half);
    if (ImGui::Button("rebuild"))
    {
        Create_Image_Detail(size, 1024, center);
        Create_Hgt_Detail(size, 1024, center);
        testImage = Texture::createFromFile(paths.gis_data + name + "\\_export\\blended_detail.jpg", true, true);
        testHillshade = Texture::createFromFile(paths.gis_data + name + "\\_export\\hillshade_detail.jpg", true, true);
    }

    if (testImage)
    {
        ImGui::NewLine();
        //ImGui::SetCursorPos(ImVec2(20.f, 200.f));
        _window.image("I1", testImage, float2(128, 128));
        if (ImGui::IsItemClicked(0)) showImg = true;
    }

    if (testHillshade)
    {
        ImGui::NewLine();
        //ImGui::SetCursorPos(ImVec2(20.f, 400.f));
        _window.image("H1", testHillshade, float2(128, 128));
        if (ImGui::IsItemClicked(0)) showImg = false;
    }

    if (testImage && testHillshade)
    {
        float mapSize = 1024;
        ImGui::SetCursorPos(ImVec2(500.f, 60.f));
        _window.image("AL", showImg ? testImage : testHillshade, float2(mapSize, mapSize));
    }
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

void terrainGenerator::onGuiRender_Setup_Right(Gui* _gui)
{
    ImGui::Text("lod");
    ImGui::SameLine(50, 0);
    ImGui::Text("tile");
    ImGui::SameLine(150, 0);
    ImGui::Text("pixel");


    ImGui::NewLine();
    ImGui::NewLine();
    for (int i = 0; i < 20; i++)
    {
        ImGui::Text("%d", i);
        ImGui::SameLine(50, 0);

        float tile = size_km * 1000.f / pow(2, i);
        if (tile > 1000)
        {
            ImGui::Text("%2.1fkm", tile / 1000.f);
        }
        else if (tile > 1)
        {
            ImGui::Text("%2.1fm", tile);
        }
        else
        {
            ImGui::Text("%2.1fmm", tile * 1000.f);
        }
        ImGui::SameLine(150, 0);

        float pixel = size_km * 1000.f / pow(2, i) / 248.f;

        if (pixel > 1)
        {
            ImGui::Text("%2.1fm", pixel);
        }
        else
        {
            ImGui::Text("%2.1fmm", pixel * 1000);
        }

    }
}

void terrainGenerator::onGuiRender_Map_Right(Gui* _gui, bool _elevation)
{
    //static int current_lod = 0; // Index pointer tracked by ImGui
    

    ImGui::ListBox("Lod", &current_lod, items, IM_ARRAYSIZE(items), 4);
    //ImGui::ListBox("pen size", &current_pen, items_pen, IM_ARRAYSIZE(items), 4);


    ImGui::ListBox("mode", &map_op, map_operator, IM_ARRAYSIZE(map_operator), 4);
    ImGui::ListBox("scale", &map_scale, map_percentage, IM_ARRAYSIZE(map_percentage), 5);

    if (ImGui::Button("merge hillshade"))
    {
        Merge_HillShade();
        mapBackground = Texture::createFromFile(paths.gis_data + name + "\\_export\\blended_index.jpg", true, true);
    }

    if (ImGui::Button("create background"))
    {
        Create_Image_Overview();
        Create_Hgt_Overview();
        Merge_HillShade();
        mapBackground = Texture::createFromFile(paths.gis_data + name + "\\_export\\blended_index.jpg", true, true);
    }
    



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
        ImGui::NewLine();
        ImGui::Text("%d tiles  %2.1fMb aproximate", totalTilesHgt, totalTilesHgt * 0.128f);
        ImGui::DragFloat("quant_error", &hgt_jp2_Quant, 0.00001f, 0.00001f, 0.01f, "%.4f");
        TOOLTIP("Max Error\n0.001 small, roughly 30x compression ratio\nlarger values can save significant space at the cost of data errors\nSpecifically ringing");

        ImGui::NewLine();
        if (ImGui::Button("export to jpeg2000"))
        {
            threadTilesHgt = 0;
            cancelBuild = false;
            std::thread thread_hgt(&terrainGenerator::hgt_to_jpeg2000, this);
            thread_hgt.detach();
            //hgt_to_jpeg2000();
        }
        ImGui::Text("%d / %d", threadTilesHgt, totalTilesHgt);
        ImGui::ProgressBar((float)threadTilesHgt / (float)totalTilesHgt);
        if (ImGui::Button("cancel"))
        {
            cancelBuild = true;
        }
    }
    else
    {
        ImGui::Text("%d tiles  %2.1fMb aproximate", totalTilesImg, totalTilesImg * 0.128f);
        ImGui::DragFloat("quant_error", &img_jp2_Quant, 0.0001f, 0.0001f, 0.05f);
        TOOLTIP("Max Error/n0.04 small'ish, since images needs less acuracy then elevations");

        if (ImGui::Button("export to jpeg2000"))
        {
            threadTilesImg = 0;
            cancelBuild = false;
            std::thread thread_img(&terrainGenerator::img_to_jpeg2000, this);
            thread_img.detach();
        }
        ImGui::Text("%d / %d", threadTilesImg, totalTilesImg);
        ImGui::ProgressBar((float)threadTilesImg / (float)totalTilesImg);
        if (ImGui::Button("cancel"))
        {
            cancelBuild = true;
        }
    }
}




void terrainGenerator::onGuiRender_Map(Gui* _gui, Gui::Window& _window, bool _elevation, bool _change)
{
    if (!mapBackground) return;

    float mapSize = ImGui::GetWindowHeight() - 100;
    ImGui::SetCursorPos(ImVec2(10.f, 65.f));
    _window.image("AL", mapBackground, float2(mapSize, mapSize));

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

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddCircle(ImGui::GetMousePos(), 40.f, ImColor(ImVec4(0.1f, 0.1f, 0.1f, 0.7f)), 12, 6);
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
                            tileView[Y][X][3] = 50;
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
                            tileView[Y][X][3] = 50;
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
                            tileView[Y][X][3] = 50;
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
    ImGuiStyle oldstyle = ImGui::GetStyle();

    style.FramePadding = ImVec2(10, 10);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.025f, 0.025f, 0.025f, 1.0f);
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
                currentTab = _tabs::Setup;
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Input data")) {
                onGuiRender_Files(_gui, builderPanel);
                currentTab = _tabs::Files;
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Test")) {
                onGuiRender_Test(_gui, builderPanel);
                currentTab = _tabs::Test;
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Elevation")) {
                onGuiRender_Map(_gui, builderPanel, true, !ImGui::IsItemActivated());
                currentTab = _tabs::Elevation;
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Image")) {
                onGuiRender_Map(_gui, builderPanel, false, !ImGui::IsItemActivated());
                currentTab = _tabs::Image;
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Landcover")) {
                onGuiRender_Landcover(_gui, builderPanel);
                currentTab = _tabs::Landcover;
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
    _chdir(paths.gis_data.c_str());
    system(("mkdir " + name).c_str());

    _chdir((paths.gis_data + name).c_str());
    {
        system("mkdir _export");
        system("mkdir _temp");
        system("mkdir elevation");
        system("mkdir landcover");
        system("mkdir orthophotos");
        system("mkdir vector");
    }
}


//mkdir -p project/{src,docs,tests}/{subfolder1,subfolder2}
void terrainGenerator::Create_Terrain_directories()
{
    _chdir(paths.terrains.c_str());
    system(("mkdir " + name).c_str());

    //system("mkdir -p %s//{elevation, bake, ecosystem, terrafectors, _export, overlay, gis, roads}", name.c_str());

    _chdir((paths.terrains + name).c_str());
    {
        system("mkdir elevation");
        system("mkdir bake");
        system("mkdir ecosystem");
        system("mkdir terrafectors");
        system("mkdir _export");
        system("mkdir overlay");
        system("mkdir gis");
        system("mkdir roads");
        system("mkdir orthophoto");

        {
            _chdir((paths.terrains + name + "/terrafectors").c_str());
            system("mkdir 10_bakeOnlyBottom");
            system("mkdir 20_base");
            system("mkdir 30_roads");
            system("mkdir 40_bakeOnlyTop");
            system("mkdir 50_top");
            system("mkdir 60_overlay");
        }

        {
            _chdir((paths.terrains + name + "/_export").c_str());
            system("mkdir bridges");
            system("mkdir roads");
            system("mkdir scene");
            system("mkdir tiles");
        }

        {
            _chdir((paths.terrains + name + "/gis").c_str());
            system("mkdir elevation");
            system("mkdir orthophotos");
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
    std::string cmd = paths.gdal_bin + "gdalwarp -t_srs \"" + proj4 + "\" ";
    cmd += " -te " + std::to_string(-half) + " " + std::to_string(-half) + " " + std::to_string(half) + " " + std::to_string(half);
    cmd += " -ts 2048 2048";
    cmd += " -r cubicspline -overwrite -ot byte -of jpeg ";
    cmd += " -multi -wo NUM_THREADS=ALL_CPUS ";
    cmd += image_files;
    cmd += paths.gis_data + name + "\\_export\\photo_index.jpg";

    _chdir((paths.gis_data + name + "//orthophotos").c_str());
    system(cmd.c_str());
}

void terrainGenerator::Create_Image_Detail(float _width, int _size, float2 _center)
{
    float half = _width / 2.f;
    std::string cmd = paths.gdal_bin + "gdalwarp -t_srs \"" + proj4 + "\" ";
    cmd += " -te " + std::to_string(_center.x - half) + " " + std::to_string(_center.y - half) + " " + std::to_string(_center.x + half) + " " + std::to_string(_center.y + half);
    cmd += " -ts " + std::to_string(_size) + " " + std::to_string(_size);
    cmd += " -r cubicspline -overwrite -ot byte -of jpeg ";
    cmd += " -multi -wo NUM_THREADS=ALL_CPUS ";
    cmd += image_files;
    cmd += paths.gis_data + name + "\\_export\\photo_detail.jpg";

    _chdir((paths.gis_data + name + "//orthophotos").c_str());
    system(cmd.c_str());

    _chdir((paths.gis_data + name + "//_export").c_str());
    cmd = paths.gdal_bin + "gdal_translate photo_detail.jpg photo_detail.bil";
    system(cmd.c_str());

}

void terrainGenerator::Merge_HillShade()
{
    std::string cmd;
    _chdir((paths.gis_data + name + "//_export").c_str());
    cmd = paths.gdal_bin + "gdal raster blend --overwrite ";
    cmd += map_operator[map_op];
    cmd += map_percentage[map_scale];
    cmd += " photo_index.jpg hillshade.jpg  blended_index.jpg";
    std::cout << cmd << std::endl;
    system(cmd.c_str());
}

void terrainGenerator::Create_Hgt_Overview()
{
    float half = size_km * 1000.f / 2.f;
    std::string cmd = paths.gdal_bin + "gdalwarp ";
    cmd += " -t_srs \"" + proj4 + "\" ";
    if (useCT)
    {
        cmd += " -ct \"" + CT + "\" ";
    }
    else
    {
        //cmd += " -t_srs \"" + proj4 + "\" ";
    }
    cmd += " -te " + std::to_string(-half) + " " + std::to_string(-half) + " " + std::to_string(half) + " " + std::to_string(half);
    cmd += " -ts 2048 2048";
    cmd += " -r cubicspline -overwrite -ot Float32 ";
    cmd += " -multi -wo NUM_THREADS=ALL_CPUS ";
    cmd += elevation_files;
    cmd += paths.gis_data + name + "\\_export\\hgt_2k.bil";

    _chdir((paths.gis_data + name + "//elevation").c_str());
    system(cmd.c_str());

    cmd = paths.gdal_bin + "gdaldem hillshade -of jpeg hgt_2k.bil  hillshade.jpg";
    _chdir((paths.gis_data + name + "//_export").c_str());
    system(cmd.c_str());

    
}

void terrainGenerator::Create_Hgt_Detail(float _width, int _size, float2 _center)
{
    float half = _width / 2.f;
    std::string cmd = paths.gdal_bin + "gdalwarp ";
    cmd += " -t_srs \"" + proj4 + "\" ";
    if (useCT)
    {
        cmd += " -ct \"" + CT + "\" ";
    }
    else
    {
        //cmd += " -t_srs \"" + proj4 + "\" ";
    }
    cmd += " -te " + std::to_string(_center.x - half) + " " + std::to_string(_center.y - half) + " " + std::to_string(_center.x + half) + " " + std::to_string(_center.y + half);
    cmd += " -ts " + std::to_string(_size) + " " + std::to_string(_size);
    cmd += " -r cubicspline -overwrite -ot Float32 ";
    cmd += " -multi -wo NUM_THREADS=ALL_CPUS ";
    cmd += elevation_files;
    cmd += paths.gis_data + name + "\\_export\\hgt_detail.bil";

    _chdir((paths.gis_data + name + "//elevation").c_str());
    system(cmd.c_str());

    cmd = paths.gdal_bin + "gdaldem hillshade -of jpeg hgt_detail.bil  hillshade_detail.jpg";
    _chdir((paths.gis_data + name + "//_export").c_str());
    system(cmd.c_str());


    _chdir((paths.gis_data + name + "//_export").c_str());
    cmd = paths.gdal_bin + "gdal raster blend --overwrite --operator=hard-light --opacity=100 photo_detail.jpg hillshade_detail.jpg  blended_detail.jpg";
    std::cout << cmd << std::endl;
    system(cmd.c_str());


    ///https://gdal.org/en/stable/programs/gdal_raster_aspect.html#gdal-raster-aspect

}




void terrainGenerator::hgt_to_jpeg2000()
{
    _chdir((paths.gis_data + name + "\\_temp").c_str());
    //system("del *.*");
    _chdir((paths.gis_data + name + "\\elevation").c_str());

    fopen_s(&dataFileHgt, (paths.terrains + name + "\\elevations.txt").c_str(), "w");

    hgt_tile_gdal(0, 0, 0, 2048);

    for (int x = 0; x < 4; x++)
    {
        for (int y = 0; y < 4; y++)
        {
            hgt_tile_gdal(2, y, x);
        }
    }

    for (int x = 0; x < 16; x++)
    {
        for (int y = 0; y < 16; y++)
        {
            if (tiles_hgt_4[y][x] > 0)            hgt_tile_gdal(4, y, x);
        }
    }

    for (int x = 0; x < 64; x++)
    {
        for (int y = 0; y < 64; y++)
        {
            if (tiles_hgt_6[y][x] > 0)            hgt_tile_gdal(6, y, x);
        }
    }

    for (int x = 0; x < 256; x++)
    {
        for (int y = 0; y < 256; y++)
        {
            if (tiles_hgt_8[y][x] > 0)            hgt_tile_gdal(8, y, x);
        }
    }

    fclose(dataFileHgt);
}



void terrainGenerator::hgt_tile_gdal(int lod, int y, int x, int size)
{
    if (cancelBuild) return;

    int grid = (int)pow(2, lod);
    int halfgrid = grid / 2;
    float origin = -(size_km * 1000.f * 0.5f);
    float block = (size_km * 1000.f) / grid;
    float total = block * (256.f / 248.f);
    float edge = (total - block) * 0.5f;
    float xstart = origin + (x * block) - edge;
    float ystart = origin + (y * block) - edge;

    std::string cmd = paths.gdal_bin + "gdalwarp ";
    cmd += " -t_srs \"" + proj4 + "\" ";
    if (useCT)
    {
        cmd += " -ct \"" + CT + "\" ";
    }
    else
    {
        //cmd += " -t_srs \"" + proj4 + "\" ";
    }

    cmd += " -te " + std::to_string(xstart) + " " + std::to_string(-(ystart + total)) + " " + std::to_string(xstart + total) + " " + std::to_string(-ystart);
    cmd += " -ts " + std::to_string(size) + " " + std::to_string(size) + " ";
    cmd += " -r cubicspline -overwrite -ot Float32 ";
    cmd += " -multi -wo NUM_THREADS=ALL_CPUS ";
    cmd += elevation_files;
    //cmd += paths.gis_path + name + "\\_temp\\hgt_" + std::to_string(lod) + "_" + std::to_string(y) + "_" + std::to_string(x) + ".bil";
    cmd += paths.gis_data + name + "\\_temp\\hgt_tile.bil";

    system(cmd.c_str());

    if (lod == 0)
    {
        // copy bil
        cmd = "copy ..\\_temp\\hgt_tile.bil ";
        cmd += paths.terrains + name + "\\elevation\\hgt_0_0_0.bil";
        cmd += " /y";
        system(cmd.c_str());

        fprintf(dataFileHgt, "%d %d %d %d %f %f %f 0.0 10000.0 elevation/hgt_0_0_0.bil\n", lod, y, x, size, xstart, ystart, total);
    }
    else
    {
        hgt_tile_jp2(lod, y, x, size, xstart, ystart, total);
    }
    threadTilesHgt++;
}




void terrainGenerator::hgt_tile_jp2(int lod, int y, int x, int size, float xstart, float ystart, float blockSize)
{

    // Find the minimum and maximum
    float data_min = 9999999.0f;
    float data_max = -9999999.0f;
    float data[1024];

    std::string billname = paths.gis_data + name + "\\_temp\\hgt_tile.bil";
    std::string jp2name = paths.terrains + name + "\\elevation\\hgt_" + std::to_string(lod) + "_" + std::to_string(y) + "_" + std::to_string(x) + ".jp2";

    FILE* bilData = fopen(billname.c_str(), "rb");
    if (bilData) {

        for (uint i = 0; i < size; i++) {
            fread(data, sizeof(float), size, bilData);
            for (uint j = 0; j < size; j++) {
                data_min = __min(data_min, data[j]);
                data_max = __max(data_max, data[j]);
            }
        }
        fclose(bilData);
    }
    // now add 5 meter either side to allow for possible modifications
    data_min -= 5.0f;
    data_max += 5.0f;
    float data_scale = 65536.0f / (data_max - data_min);


    ojph::codestream codestream;
    ojph::j2c_outfile j2c_file;
    j2c_file.open(jp2name.c_str());
    {
        // set up
        ojph::param_siz siz = codestream.access_siz();
        siz.set_image_extent(ojph::point(size, size));
        siz.set_num_components(1);
        siz.set_component(0, ojph::point(1, 1), 16, false);		//??? unsure about the subsampling point()
        siz.set_image_offset(ojph::point(0, 0));
        siz.set_tile_size(ojph::size(size, size));
        siz.set_tile_offset(ojph::point(0, 0));

        ojph::param_cod cod = codestream.access_cod();
        cod.set_num_decomposition(5);
        cod.set_block_dims(64, 64);
        //if (num_precints != -1)
        //	cod.set_precinct_size(num_precints, precinct_size);
        cod.set_progression_order("RPCL");
        cod.set_color_transform(false);
        cod.set_reversible(false);
        codestream.access_qcd().set_irrev_quant(0.0001f);





        FILE* bilData = fopen(billname.c_str(), "rb");
        if (bilData)
        {
            codestream.write_headers(&j2c_file);

            int next_comp;
            ojph::line_buf* cur_line = codestream.exchange(NULL, next_comp);

            for (uint i = 0; i < size; ++i)
            {
                //base->read(cur_line, next_comp);
                float data[1024];
                unsigned short dataUint[1024];
                fread(data, sizeof(float), size, bilData);

                for (uint j = 0; j < size; j++)
                {
                    dataUint[j] = (uint)((data[j] - data_min) * data_scale);
                }

                int32_t* dp = cur_line->i32;
                for (uint j = 0; j < size; j++) {
                    *dp++ = (int32_t)dataUint[j];
                }
                cur_line = codestream.exchange(cur_line, next_comp);
            }
            fclose(bilData);
        }


    }
    codestream.flush();
    codestream.close();


    fprintf(dataFileHgt, "%d %d %d %d %f %f %f %f %f elevation/hgt_%d_%d_%d.jp2\n", lod, y, x, size, xstart, ystart, blockSize, data_min, (data_max - data_min), lod, y, x);
}



uint32_t getHashFromTileCoords_Gen(unsigned int lod, unsigned int y, unsigned int x) {
    return (lod << 28) + (y << 14) + (x);
}

void terrainGenerator::img_to_jpeg2000()
{
    jp2Dir_Gen jp2;
    jp2Map_Gen _mapElement;

    jp2.files.emplace_back();
    jp2.files.back().filename = "0_0_0.bin";
    jp2.files.back().hash = getHashFromTileCoords_Gen(0, 0, 0);

    _mapElement.set(0, 0, 0);
    jp2.files.back().tiles.push_back(_mapElement);

    for (uint y = 0; y < 4; y++) {
        for (uint x = 0; x < 4; x++) {
            _mapElement.set(2, y, x);
            jp2.files.back().tiles.push_back(_mapElement);
            //writeGdal(_mapElement, of_gdal, inLow);
        }
    }

    std::vector<jp2Map_Gen> block_files;

    for (uint ty = 0; ty < 16; ty++)
    {
        for (uint tx = 0; tx < 16; tx++)
        {
            block_files.clear();

            if (tiles_img_4[ty][tx] > 0)
            {
                _mapElement.set(4, ty, tx);     // 2.4 m roughly
                block_files.push_back(_mapElement);
                //writeGdal(_mapElement, of_gdal, inAll);
            }

            for (uint y = ty * 4; y < ty * 4 + 4; y++) {
                for (uint x = tx * 4; x < tx * 4 + 4; x++) {
                    if (tiles_img_6[y][x] > 0)
                    {
                        _mapElement.set(6, y, x); // 61 cm
                        block_files.push_back(_mapElement);
                        //writeGdal(_mapElement, of_gdal, inAll);
                    }
                }
            }

            for (uint y = ty * 16; y < ty * 16 + 16; y++) {
                for (uint x = tx * 16; x < tx * 16 + 16; x++) {
                    if (tiles_img_8[y][x] > 0)
                    {
                        _mapElement.set(8, y, x);
                        block_files.push_back(_mapElement);
                        //writeGdal(_mapElement, of_gdal, inAll);
                    }
                }
            }

            for (uint y = ty * 64; y < ty * 64 + 64; y++) {
                for (uint x = tx * 64; x < tx * 64 + 64; x++) {
                    if (tiles_img_10[y][x] > 0)
                    {
                        _mapElement.set(10, y, x);
                        block_files.push_back(_mapElement);
                        //writeGdal(_mapElement, of_gdal, inAll);
                    }
                }
            }

            // push if we have any
            if (block_files.size() > 0)
            {
                jp2.files.emplace_back();
                jp2.files.back().filename = "4_" + std::to_string(ty) + "_" + std::to_string(tx) + ".bin";
                jp2.files.back().hash = getHashFromTileCoords_Gen(4, ty, tx);
                for (auto& t : block_files) {
                    jp2.files.back().tiles.push_back(t);
                }
            }
        }
    }


    jp2.save((paths.terrains + name + "\\photo_tiles.json").c_str());



    // Now do the actual conversion
    // ######################################################################################################################################

    //jp2.load("F:/terrains_gis/austria_Wechsel/orthophotos/photo_tiles.json");
   // std::string filename;
    //threadTilesImg = 0;

    for (auto& F : jp2.files)
    {
        std::string binfile = paths.terrains + name + "\\orthophoto\\" + F.filename;
        std::ofstream of_jp2_bin;
        of_jp2_bin.open(binfile, std::ios::binary);
        if (of_jp2_bin.good())
        {
            F.sizeInBytes = 0;
            for (auto& T : F.tiles)
            {
                T.fileOffset = F.sizeInBytes;
                //filename = "F:/terrains_gis/austria_Wechsel/_temp/img_" + std::to_string(T.lod) + "_" + std::to_string(T.y) + "_" + std::to_string(T.x);
                T.sizeInBytes = img_to_jp2PhotosMemory(of_jp2_bin, 1024, T.lod, T.y, T.x);
                F.sizeInBytes += T.sizeInBytes;
                threadTilesImg++;
            }
            of_jp2_bin.close();
        }
    }
    jp2.save((paths.terrains + name + "\\orthophotos.json").c_str());
}




void terrainGenerator::img_tile_gdal(int lod, int y, int x, int size)
{
    int grid = (int)pow(2, lod);
    int halfgrid = grid / 2;
    float origin = -(size_km * 1000.f * 0.5f);
    float block = (size_km * 1000.f) / grid;
    float total = block * (256.f / 248.f);
    float edge = (total - block) * 0.5f;
    float xstart = origin + (x * block) - edge;
    float ystart = origin + (y * block) - edge;

    std::string cmd = paths.gdal_bin + "gdalwarp ";
    cmd += " -t_srs \"" + proj4 + "\" ";
    if (useCT)
    {
        //cmd += " -ct \"" + CT + "\" ";
    }
    else
    {
        //cmd += " -t_srs \"" + proj4 + "\" ";
    }

    cmd += " -te " + std::to_string(xstart) + " " + std::to_string(-(ystart + total)) + " " + std::to_string(xstart + total) + " " + std::to_string(-ystart);
    cmd += " -ts " + std::to_string(size) + " " + std::to_string(size) + " ";
    cmd += " -r cubicspline -overwrite -ot byte ";
    cmd += " -multi -wo NUM_THREADS=ALL_CPUS ";
    cmd += image_files;
    //cmd += paths.gis_path + name + "\\_temp\\hgt_" + std::to_string(lod) + "_" + std::to_string(y) + "_" + std::to_string(x) + ".bil";
    cmd += paths.gis_data + name + "\\_temp\\img_tile.bil";

    _chdir((paths.gis_data + name + "//orthophotos").c_str());

    system(cmd.c_str());


    //cmd = "gdal_translate -ot byte ..\\_temp\\img_tile.tif ../_temp/img_tile.bil";
    //system(cmd.c_str());
}


void terrainGenerator::codestream_rgb(ojph::codestream& _codestream, int _size, float _delta)
{
    // set up
    ojph::param_siz siz = _codestream.access_siz();

    siz.set_num_components(3);
    siz.set_component(0, ojph::point(1, 1), 8, false);
    siz.set_component(1, ojph::point(1, 1), 8, false);
    siz.set_component(2, ojph::point(1, 1), 8, false);

    siz.set_image_offset(ojph::point(0, 0));
    siz.set_image_extent(ojph::point(_size, _size));

    siz.set_tile_offset(ojph::point(0, 0));
    siz.set_tile_size(ojph::size(_size, _size));

    ojph::param_cod cod = _codestream.access_cod();
    cod.set_num_decomposition(5);
    cod.set_block_dims(64, 64);
    cod.set_progression_order("CPRL");      // ??? "RPCL"
    cod.set_color_transform(true);
    cod.set_reversible(false);

    _codestream.access_qcd().set_irrev_quant(_delta);
    _codestream.set_planar(false);
}



uint terrainGenerator::img_to_jp2PhotosMemory(std::ofstream& _file, const uint size, uint _lod, uint _y, uint _x)
{
    if (cancelBuild) return 0;

    img_tile_gdal(_lod, _y, _x, size);

    unsigned char data[1024][3][1024];
    //unsigned char data1[1024][1024];

    // JP2000 is also Band Interleaved by Line
    // but... int_32
    // ??? would it work to process the bil file to int32 instead
    // or since I have to do colour processing here, process that to int32 already

    ojph::codestream codestream;
    ojph::mem_outfile j2c_file;
    j2c_file.open();
    {
        codestream_rgb(codestream, 1024, img_jp2_Quant);

        FILE* bilData = fopen((paths.gis_data + name + "\\_temp\\img_tile.bil").c_str(), "rb");
        if (bilData)
        {
            fread(data, sizeof(char), size * size * 3, bilData);
            codestream.write_headers(&j2c_file);

            int next_comp;
            ojph::line_buf* cur_line = codestream.exchange(NULL, next_comp);
            /*
            for (uint i = 0; i < size; i++)
            {
                for (uint cmp = 0; cmp < 3; cmp++)
                {

                    if (cmp != next_comp)
                    {
                        bool cm = true;
                    }
                    int32_t* dp = cur_line->i32;
                    for (uint j = 0; j < size; j++) {
                        *dp++ = (int32_t)data[i][cmp][j];
                    }
                    cur_line = codestream.exchange(cur_line, next_comp);
                }
            }
            */
            for (uint i = 0; i < size * 3; i++)
            {
                int32_t* dp = cur_line->i32;
                for (uint j = 0; j < size; j++) {
                    *dp++ = (int32_t)data[i / 3][next_comp][j];
                }
                cur_line = codestream.exchange(cur_line, next_comp);
            }

            fclose(bilData);
        }
    }
    codestream.flush();


    _file.write((char*)j2c_file.get_data(), j2c_file.tell());
    uint numBytes = j2c_file.tell();

    codestream.close();
    return numBytes;


}


void terrainGenerator::loadPaths()
{
    try {
        std::ifstream is("paths.json");
        if (is.good()) {
            cereal::JSONInputArchive archive(is);
            archive(CEREAL_NVP(paths));
        }
    }
    catch (const cereal::RapidJSONException& e) {
        // Handle RapidJSON parsing failure
    }
    catch (const cereal::Exception& e) {
        // Handle stream issues, missing data, or version mismatches
    }
    catch (const std::exception& e) {
        // ??? is this needed
    }
}

void terrainGenerator::savePaths()
{
    try {
        std::ofstream os("paths.json");
        if (os.good()) {
            cereal::JSONOutputArchive archive(os);
            archive(CEREAL_NVP(paths));
        }
    }
    catch (const cereal::RapidJSONException& e) {
        // Handle RapidJSON parsing failure
    }
    catch (const cereal::Exception& e) {
        // Handle stream issues, missing data, or version mismatches
    }
    catch (const std::exception& e) {
        // ??? is this needed
    }
}

terrainGenerator::terrainGenerator()
{
    loadPaths();
}
