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
#include "terrain.h"
#include "imgui.h"
#include <imgui_internal.h>
#include <random>
//#include "Utils/UI/TextRenderer.h"
#include "assimp/Exporter.hpp"
using namespace Assimp;
#include <chrono>
using namespace std::chrono;

#pragma optimize("", off)

#define TOOLTIP(x)  if (ImGui::IsItemHovered()) {ImGui::SetTooltip(x);}




_lastFile terrainManager::lastfile;



void setupVert(ribbonVertex* r, int start, float3 pos, float radius, int _mat = 0)
{
    r->faceCamera = true;
    r->startBit = start;
    r->position = pos;
    r->radius = radius;
    r->bitangent = float3(0, 1, 0);
    r->tangent = float3(0, 0, 1);
    r->material = _mat;
    r->albedoScale = 127;
    r->translucencyScale = 127;
    r->uv = float2(1, 4);

    if (start == 1)
    {
        ribbonVertex* rprev = r--;
        float3 tangent = glm::normalize(r->position - rprev->position);
        r->bitangent = tangent;
        rprev->bitangent = tangent;
    }
}













#include <iostream>
#include <fstream>
void _shadowEdges::solveThread()
{
    while (1)
    {
        if (requestNewShadow)
        {
            sunAngle += dAngle;
            sunAng.x = -cos(sunAngle);
            sunAng.y = -sin(sunAngle);
            sunAng.z = 0;
            solve(-sunAng.y, sunAng.x > 0);
            shadowReady = true;
            requestNewShadow = false;
        }
        else
        {
            Sleep(10);
        }
    }
}

void _shadowEdges::solve(float _angle, bool dx)
{
    for (int y = 0; y < 4095; y++)
    {
        for (int x = 0; x < 4095; x++)
        {
            //shadowH[y][x] = float2(height[y][x] - 15.f, 0.f);
            shadowH[y][x] = float2(-5000.f, 0.f);   // when splittign the two
        }
    }

    float a_min = _angle + 0.02f;
    float a_max = _angle - 0.02f;

    for (int y = 1; y < 4094; y++)
    {
        for (int x = 1; x < 4094; x++)
        {

            if (dx && (Nx[y][x - 1] < a_min) && (Nx[y][x] > a_max))
            {
                float H = height[y][x];

                for (int j = x + 1; j < 4096; j++)
                {
                    H -= _angle * 9.765625f;
                    if (H > shadowH[y][j].x)
                    {
                        float softDepth = (float)(j - x) * 9.765625f / 10.f;
                        shadowH[y][j] = float2(H - 0.f, softDepth);
                    }
                    else break;
                }
            }
            else if (!dx && (-Nx[y][x + 1] < a_min) && (-Nx[y][x] > a_max))
            {
                float H = height[y][x];

                for (int j = x - 1; j > 0; j--)
                {
                    H -= _angle * 9.765625f;
                    if (H > shadowH[y][j].x)
                    {
                        float softDepth = (float)(x - j) * 9.765625f / 10.f;
                        shadowH[y][j] = float2(H - 0.f, softDepth);
                    }
                    else break;
                }
            }
        }
    }
}

void _shadowEdges::load(std::string filename, float _angle)
{
    int edgeCnt = 0;
    int shadowEdge = 0;

    std::ifstream ifs;
    ifs.open(filename, std::ios::binary);

    if (ifs)
    {
        ifs.read((char*)height, 4096 * 4096 * 4);
        ifs.close();

        for (int y = 0; y < 4095; y++)
        {
            for (int x = 0; x < 4095; x++)
            {
                Nx[y][x] = (height[y][x] - height[y][x + 1]) / 9.765625f;    // 10 meter between pixels SHIT NOT TRUE

                shadowH[y][x] = float2(height[y][x] - 5.f, 0.f);
                // remove this and pass terrein height seperate
            }
        }

        /*
        float angle = _angle;  // about 10 degrees
        float a_min = angle;
        float a_max = angle -0.01f;

        memset(edge, 0, 4096 * 4096);
        for (int y = 1; y < 4094; y++)
        {
            for (int x = 1; x < 4094; x++)
            {
                if (Nx[y][x] > a_min)
                {
                    edge[y][x] = (unsigned char)(128.f * saturate(Nx[y][x] - a_min));  // hill shade
                }


                if ((Nx[y][x-1] < a_min) && (Nx[y][x] > a_max))
                {
                    edge[y][x] = 255;
                    edgeCnt++;


                    float H = height[y][x];
                    for (int j = x + 1; j < 4096; j++)
                    {
                        H -= angle * 9.765625f;
                        if (H > shadowH[y][j].x)
                        {
                            edge[y][j] = 0;

                            float softDepth = (float)(j-x) * 9.765625f / 10.f;
                            shadowH[y][j] = float2(H - 0.f, softDepth);
                            //shadowEdge++;
                            //break;
                        }
                        else break;
                    }

                }



                /*
                float H = height[y][x+1];
                for (int j = x + 2; j < 4095; j++)
                {
                    H += angle * 9.765625f;          // about 10 degrees
                    if (H > 1500) break;
                    if (H < height[y][j])
                    {
                        edge[y][x] /= 4;
                        shadowEdge++;
                        break;
                    }
                }
                */



                /*
                if (Nx[y][x] < -0.03)
                {
                    if ((Nx[y][x + 1] - Nx[y][x]) > 0.05f)
                    {
                        edge[y][x] = 255;

                        // SUNRISE - is this pixel in shadow when it can also cast
                        float H = height[y][x];
                        for (int j = x + 1; j < 4095; j++)
                        {
                            H -= Nx[y][x] * 9.765625f;
                            if (H < height[y][j])
                            {
                                edge[y][x] = 100;
                                break;
                            }
                        }

                        // sunrise how far can it cast a shadow if it all

                    }

                    if (edge[y][x] == 255)
                    {
                        // VERY EARLY MORENIGN SHASDOWN

                        float H = height[y][x];
                        for (int j = x + 1; j < 4095; j++)
                        {
                            //H -= Nx[y][x] * 9.765625f;
                            H += 0.03f * 9.765625f;          // about 10 degrees
                            if (H < height[y][j])
                            {
                                edge[y][x] = 50;
                                break;
                            }
                        }

                    }
                }
                */

                //  }
              //}
              /*
              std::ofstream ofs;
              ofs.open(filename + ".edge.raw", std::ios::binary);
              if (ofs)
              {
                  ofs.write((char*)edge, 4096 * 4096);
                  ofs.close();
              }
              */
    }
}




















void _terrainSettings::renderGui(Gui* _gui)
{
    ImGui::PushFont(_gui->getFont("roboto_32"));
    {
        static const int maxSize = 2048;
        char buf[maxSize];
        sprintf_s(buf, maxSize, name.c_str());
        ImGui::SetNextItemWidth(300);
        if (ImGui::InputText("##name", buf, maxSize))
        {
            name = buf;
        }

        ImGui::NewLine();
        ImGui::PushFont(_gui->getFont("roboto_20"));
        {
            ImGui::Text("projection");
            sprintf_s(buf, maxSize, projection.c_str());
            ImGui::SetNextItemWidth(1000);
            if (ImGui::InputText("##projection", buf, maxSize))
            {
                projection = buf;
            }
        }
        ImGui::Text("size");
        ImGui::PopFont();

        ImGui::SetNextItemWidth(150);
        ImGui::DragFloat("##size", &size, 1, 1000, 200000, "%5.0f m ");

        ImGui::NewLine();
        sprintf_s(buf, maxSize, dirRoot.c_str());
        ImGui::SetNextItemWidth(600);
        if (ImGui::InputText("root", buf, maxSize))
        {
            dirRoot = buf;
        }

        sprintf_s(buf, maxSize, dirExport.c_str());
        ImGui::SetNextItemWidth(600);
        if (ImGui::InputText("export(EVO)", buf, maxSize))
        {
            dirExport = buf;
        }

        sprintf_s(buf, maxSize, dirGis.c_str());
        ImGui::SetNextItemWidth(600);
        if (ImGui::InputText("gis", buf, maxSize))
        {
            dirGis = buf;
        }

        sprintf_s(buf, maxSize, dirResource.c_str());
        ImGui::SetNextItemWidth(600);
        if (ImGui::InputText("resource", buf, maxSize))
        {
            dirResource = buf;
        }

        ImGui::NewLine();

        ImGui::NewLine();
        if (ImGui::Button("Save"))
        {
            std::filesystem::path path;
            FileDialogFilterVec filters = { {"terrainSettings.json"} };
            if (saveFileDialog(filters, path))
            {
                std::ofstream os(path);
                cereal::JSONOutputArchive archive(os);
                serialize(archive, 100);
            }
        }
    }
    ImGui::PopFont();
}


void quadtree_tile::init(uint _index)
{
    index = _index;
    parent = nullptr;
    child[0] = nullptr;
    child[1] = nullptr;
    child[2] = nullptr;
    child[3] = nullptr;
}

void quadtree_tile::set(uint _lod, uint _x, uint _y, float _size, float4 _origin, quadtree_tile* _parent)
{
    lod = _lod;
    x = _x;
    y = _y;
    size = _size;
    origin = _origin;

    boundingSphere = origin + float4(size / 2, 0, size / 2, 0);
    boundingSphere.w = 1.0f;

    parent = _parent;
    child[0] = nullptr;
    child[1] = nullptr;
    child[2] = nullptr;
    child[3] = nullptr;

    if (parent)
    {
        origin.y = parent->boundingSphere.y - size * 2;
        boundingSphere.y = parent->boundingSphere.y;
    }
    else
    {
        origin.y = size * 2;
        boundingSphere.y = 0;
    }

    numQuads = 0;
    numPlants = 0;

    forSplit = false;
    forRemove = false;

    elevationHash = 0;
}






terrainManager::terrainManager()
{

}



terrainManager::~terrainManager()
{
    std::ofstream os("lastFile.xml");
    if (os.good()) {
        cereal::XMLOutputArchive archive(os);
        lastfile.road = mRoadNetwork.lastUsedFilename.string();
        lastfile.stamps = mRoadStampCollection.lastUsedFilename.string();
        archive(CEREAL_NVP(lastfile));
    }
}



void terrainManager::onLoad(RenderContext* pRenderContext, FILE* _logfile)
{
    LOG_BLOCK("terrainManager::onLoad", 0);

    std::filesystem::path currentPath = std::filesystem::current_path();

    // Move the constructor code here
    std::ifstream is("lastFile.xml");
    if (is.good()) {
        cereal::XMLInputArchive archive(is);
        archive(CEREAL_NVP(lastfile));

        fprintf(_logfile, " loading lastfile.xml\n");
        fflush(_logfile);

        mRoadNetwork.lastUsedFilename = lastfile.road;
        mRoadStampCollection.lastUsedFilename = lastfile.stamps;
    }
    else
    {
        fprintf(_logfile, " ERROR - unable to load lastfile.xml, shutting down\n");
        fflush(_logfile);
        LOG_LINE(3, "unable to load lastfile.xml, shutting down");
        gpFramework->getWindow()->shutdown();
        return;
    }

    std::string appendedName = currentPath.string() + lastfile.terrain;
    std::ifstream isT(lastfile.terrain);
    std::ifstream isT_2(appendedName);

    fprintf(_logfile, " %s\n", lastfile.terrain.c_str());

    fprintf(_logfile, "  %s\n", appendedName.c_str());
    fflush(_logfile);
    if (isT.good()) {
        terrafectorSystem::logTimeX();
        fprintf(_logfile, " loading absolute terrain %s\n", lastfile.terrain.c_str());
        fflush(_logfile);
        cereal::JSONInputArchive archive(isT);
        settings.serialize(archive, 100);

        std::ifstream a(lastfile.dir_Terrains);
        std::ifstream b(lastfile.dir_Resource);
        std::ifstream c(lastfile.dir_GIS);

        if (!std::filesystem::exists(lastfile.dir_Terrains))
        {
            fprintf(_logfile, "filedialog dir_Terrains\n");
            fflush(_logfile);
            std::filesystem::path path;
            FileDialogFilterVec filters = { {} };
            if (openFileDialog(filters, path))
            {
                lastfile.dir_Terrains = path.parent_path().string();
            }
        }

        if (!std::filesystem::exists(lastfile.dir_Resource))
        {
            fprintf(_logfile, "filedialog dir_Resource\n");
            fflush(_logfile);
            std::filesystem::path path;
            FileDialogFilterVec filters = { {} };
            if (openFileDialog(filters, path))
            {
                lastfile.dir_Resource = path.parent_path().string();
            }
        }

        if (!std::filesystem::exists(lastfile.dir_GIS))
        {
            fprintf(_logfile, "filedialog dir_GIS\n");
            fflush(_logfile);
            std::filesystem::path path;
            FileDialogFilterVec filters = { {} };
            if (openFileDialog(filters, path))
            {
                lastfile.dir_GIS = path.parent_path().string();
            }
        }

        settings.dirRoot = lastfile.dir_Terrains + settings.dirRoot;
        settings.dirResource = lastfile.dir_Resource;
        settings.dirGis = lastfile.dir_GIS;

        fprintf(_logfile, "root - %s\n", settings.dirRoot.c_str());
        fprintf(_logfile, "gis - %s\n", settings.dirGis.c_str());
        fprintf(_logfile, "resource - %s\n\n", settings.dirResource.c_str());
        fflush(_logfile);
    }
    else if (isT_2.good())
    {
        cereal::JSONInputArchive archive(isT_2);
        settings.serialize(archive, 100);

        fprintf(_logfile, "loading relative terrain, prepending root directory - %s\n", appendedName.c_str());
        fprintf(_logfile, "root - %s\n", settings.dirRoot.c_str());
        fprintf(_logfile, "gis - %s\n", settings.dirGis.c_str());
        fprintf(_logfile, "resource - %s\n\n", settings.dirResource.c_str());
        fflush(_logfile);

        settings.dirRoot = currentPath.string() + settings.dirRoot;
        settings.dirGis = currentPath.string() + settings.dirGis;
        settings.dirResource = currentPath.string() + settings.dirResource;

        fprintf(_logfile, "root - %s\n", settings.dirRoot.c_str());
        fprintf(_logfile, "gis - %s\n", settings.dirGis.c_str());
        fprintf(_logfile, "resource - %s\n\n", settings.dirResource.c_str());
        fflush(_logfile);
    }
    else
    {
        std::filesystem::path path;
        FileDialogFilterVec filters = { {"terrainSettings.json"} };
        if (openFileDialog(filters, path))
        {
            lastfile.terrain = path.string();
            std::ifstream isT(lastfile.terrain);
            if (isT.good()) {
                cereal::JSONInputArchive archive(isT);
                settings.serialize(archive, 100);
            }
            else
            {
                fprintf(_logfile, " ERROR - unable to load a terrain  (/terrain/__somewhere__.terrainSettings.json)\n");
                fprintf(_logfile, " shutting down\n");
                gpFramework->getWindow()->shutdown();
                return;
            }
        }
    }


    terrafectorSystem::pEcotopes = &mEcosystem;
    ecotopeSystem::pVegetation = &plants_Root;

    if (terrainMode == _terrainMode::vegetation)
    {
        //fprintf(_logfile, "terrain.onLoad() - abandn - VEGETATION mode\n");
        //return;
    }

    fprintf(_logfile, "\nterrain.onLoad()\n");
    fflush(_logfile);

    terrafectors._logfile = _logfile;

    {
        Sampler::Desc samplerDesc;
        samplerDesc.setAddressingMode(Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp).setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear).setMaxAnisotropy(8);
        sampler_Clamp = Sampler::create(samplerDesc);

        samplerDesc.setAddressingMode(Sampler::AddressMode::Wrap, Sampler::AddressMode::Wrap, Sampler::AddressMode::Wrap).setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear).setMaxAnisotropy(8);
        sampler_Trilinear = Sampler::create(samplerDesc);

        samplerDesc.setAddressingMode(Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp).setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear).setMaxAnisotropy(4);
        sampler_ClampAnisotropic = Sampler::create(samplerDesc);

        samplerDesc.setAddressingMode(Sampler::AddressMode::Clamp, Sampler::AddressMode::Wrap, Sampler::AddressMode::Wrap).setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear).setMaxAnisotropy(1);
        sampler_Ribbons = Sampler::create(samplerDesc);
    }

    {
        split.debug_texture = Texture::create2D(tile_numPixels, tile_numPixels, Falcor::ResourceFormat::RGBA8Unorm, 1, 1, nullptr, Falcor::Resource::BindFlags::UnorderedAccess);
        //split.bicubic_upsample_texture = Texture::create2D(tile_numPixels, tile_numPixels, Falcor::ResourceFormat::R32Float, 1, 1, nullptr, Falcor::Resource::BindFlags::UnorderedAccess);
        split.normals_texture = Texture::create2D(tile_numPixels, tile_numPixels, Falcor::ResourceFormat::R11G11B10Float, 1, 1, nullptr, Falcor::Resource::BindFlags::UnorderedAccess);
        split.vertex_A_texture = Texture::create2D(tile_numPixels / 2, tile_numPixels / 2, Falcor::ResourceFormat::R16Uint, 1, 1, nullptr, Resource::BindFlags::ShaderResource | Falcor::Resource::BindFlags::UnorderedAccess);
        split.vertex_B_texture = Texture::create2D(tile_numPixels / 2, tile_numPixels / 2, Falcor::ResourceFormat::R16Uint, 1, 1, nullptr, Resource::BindFlags::ShaderResource | Falcor::Resource::BindFlags::UnorderedAccess);
    }

    {
        std::vector<glm::uint16> vertexData(tile_numPixels / 2 * tile_numPixels / 2);
        memset(vertexData.data(), 0, tile_numPixels / 2 * tile_numPixels / 2 * sizeof(glm::uint16));	  // set to zero's
        split.vertex_clear = Texture::create2D(tile_numPixels / 2, tile_numPixels / 2, Falcor::ResourceFormat::R16Uint, 1, 1, vertexData.data(), Resource::BindFlags::ShaderResource);

        // kante
        for (uint i = 1; i < 128; i += 2)
        {
            vertexData[(1 << 7) + i] = (1 << 7) + i;
            vertexData[(127 << 7) + i] = (127 << 7) + i;
            vertexData[(i << 7) + 1] = (i << 7) + 1;
            vertexData[(i << 7) + 127] = (i << 7) + 127;
        }

        for (uint y = 9; y < 128; y += 8)
        {
            for (uint x = 9; x < 128; x += 8)
            {
                vertexData[(y << 7) + x] = (y << 7) + x;
            }
        }

        for (uint i = 5; i < 128; i += 4)
        {
            vertexData[(5 << 7) + i] = (5 << 7) + i;
            vertexData[(125 << 7) + i] = (125 << 7) + i;
            vertexData[(i << 7) + 5] = (i << 7) + 5;
            vertexData[(i << 7) + 125] = (i << 7) + 125;
        }
        split.vertex_preload = Texture::create2D(tile_numPixels / 2, tile_numPixels / 2, Falcor::ResourceFormat::R16Uint, 1, 1, vertexData.data(), Resource::BindFlags::ShaderResource);
    }

    {
        split.buffer_tileCenters = Buffer::createStructured(sizeof(float4), numTiles);
        //split.buffer_tileCenter_readback = Buffer::create(sizeof(float4) * numTiles, Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess, Buffer::CpuAccess::Read);
        split.buffer_tileCenter_readback = Buffer::create(sizeof(float4) * numTiles, Resource::BindFlags::None, Buffer::CpuAccess::Read);
        //split.tileCenters.resize(numTiles);
    }


    {
        // u16 noise texture
        std::mt19937 generator(2);
        std::uniform_int_distribution<> distribution(0, 65535);
        std::vector<unsigned short> random(256 * 256);
        for (int i = 0; i < 256 * 256; i++)
        {
            random[i] = distribution(generator);    // FIXME for 12 ecotopes
        }
        split.noise_u16 = Texture::create2D(256, 256, ResourceFormat::R16Uint, 1, 1, random.data());

        // frame buffer
        Fbo::Desc desc;
        desc.setDepthStencilTarget(ResourceFormat::D24UnormS8);			// keep for now, not sure why, but maybe usefult for cuts
        desc.setColorTarget(0u, ResourceFormat::R32Float, true);		// elevation
        desc.setColorTarget(1u, ResourceFormat::R11G11B10Float, true);	// albedo
        desc.setColorTarget(2u, ResourceFormat::R11G11B10Float, true);	// pbr
        desc.setColorTarget(3u, ResourceFormat::R11G11B10Float, true);	// alpha
        desc.setColorTarget(4u, ResourceFormat::RGBA8Unorm, true);		// ecotopes  ? R11G11B10Float 
        desc.setColorTarget(5u, ResourceFormat::RGBA8Unorm, true);		// ecotopes
        desc.setColorTarget(6u, ResourceFormat::RGBA8Unorm, true);		// ecotopes
        desc.setColorTarget(7u, ResourceFormat::RGBA8Unorm, true);		// ecotopes
        split.tileFbo = Fbo::create2D(tile_numPixels, tile_numPixels, desc, 1, 8);
        split.bakeFbo = Fbo::create2D(split.bakeSize, split.bakeSize, desc, 1, 8);
        bake.copy_texture = Texture::create2D(split.bakeSize, split.bakeSize, Falcor::ResourceFormat::R32Float, 1, 1, nullptr, Resource::BindFlags::None);

        Fbo::Desc descVegBake;
        desc.setDepthStencilTarget(ResourceFormat::D24UnormS8);			// keep for now, not sure why, but maybe usefult for cuts
        desc.setColorTarget(0u, ResourceFormat::RGBA8Unorm, true);		// albedo
        desc.setColorTarget(1u, ResourceFormat::RGBA8Unorm, true);	    // normal
        desc.setColorTarget(2u, ResourceFormat::R11G11B10Float, true);	// pbr
        desc.setColorTarget(3u, ResourceFormat::R11G11B10Float, true);	// extra
        bakeFbo_plants = Fbo::create2D(1024, 1024, desc, 1, 1);

        compressed_Normals_Array = Texture::create2D(tile_numPixels, tile_numPixels, Falcor::ResourceFormat::R11G11B10Float, numTiles, 1, nullptr, Falcor::Resource::BindFlags::ShaderResource);	  // Now an array	  at 1024 tiles its 256 Mb , Fair bit but do-ablwe
        //compressed_Albedo_Array = Texture::create2D(tile_numPixels, tile_numPixels, Falcor::ResourceFormat::BC6HU16, numTiles, 1, nullptr, Falcor::Resource::BindFlags::ShaderResource);
        compressed_Albedo_Array = Texture::create2D(tile_numPixels, tile_numPixels, Falcor::ResourceFormat::R11G11B10Float, numTiles, 1, nullptr, Falcor::Resource::BindFlags::ShaderResource);
        compressed_PBR_Array = Texture::create2D(tile_numPixels, tile_numPixels, Falcor::ResourceFormat::BC6HU16, numTiles, 1, nullptr, Falcor::Resource::BindFlags::ShaderResource);
        height_Array = Texture::create2D(tile_numPixels, tile_numPixels, Falcor::ResourceFormat::R32Float, numTiles, 1, nullptr, Falcor::Resource::BindFlags::ShaderResource);	  // Now an array	  1024 tiles is 64 MB - really nice and small

        split.drawArgs_quads = Buffer::createStructured(sizeof(t_DrawArguments), numRenderViews, Resource::BindFlags::UnorderedAccess | Resource::BindFlags::IndirectArg);
        //split.drawArgs_plants = Buffer::createStructured(sizeof(t_DrawArguments), 1, Resource::BindFlags::UnorderedAccess | Resource::BindFlags::IndirectArg);
        //split.drawArgs_clippedloddedplants = Buffer::createStructured(sizeof(t_DrawArguments), numRenderViews, Resource::BindFlags::UnorderedAccess | Resource::BindFlags::IndirectArg);
        split.drawArgs_tiles = Buffer::createStructured(sizeof(t_DrawArguments), numRenderViews, Resource::BindFlags::UnorderedAccess | Resource::BindFlags::IndirectArg);
        split.dispatchArgs_plants = Buffer::createStructured(sizeof(t_DispatchArguments), numRenderViews, Resource::BindFlags::UnorderedAccess | Resource::BindFlags::IndirectArg);

        split.buffer_feedback = Buffer::createStructured(sizeof(GC_feedback), 1);
        split.buffer_feedback_read = Buffer::createStructured(sizeof(GC_feedback), 1, Resource::BindFlags::None, Buffer::CpuAccess::Read);

        split.buffer_tiles = Buffer::createStructured(sizeof(gpuTile), numTiles);
        split.buffer_tiles_readback = Buffer::createStructured(sizeof(gpuTile), numTiles, Resource::BindFlags::None, Buffer::CpuAccess::Read);
        split.buffer_instance_quads = Buffer::createStructured(sizeof(instance_PLANT), numTiles * numQuadsPerTile);
        split.buffer_instance_plants = Buffer::createStructured(sizeof(instance_PLANT), numTiles * numPlantsPerTile);
        split.buffer_clippedloddedplants = Buffer::createStructured(sizeof(xformed_PLANT), 1024 * 1024); //32 bytes

        for (int i = 0; i < numRenderViews; i++)
        {
            split.buffer_lookup_terrain[i] = Buffer::createStructured(sizeof(tileLookupStruct), lookupSizeTerrain[i]);
            split.buffer_lookup_quads[i] = Buffer::createStructured(sizeof(tileLookupStruct), lookupSizeBillboard[i]);
            split.buffer_lookup_plants[i] = Buffer::createStructured(sizeof(tileLookupStruct), lookupSizePlants[i]);
        }

        split.buffer_terrain = Buffer::createStructured(sizeof(Terrain_vertex), numVertPerTile * numTiles);

        terrainShader.load("Samples/Earthworks_4/hlsl/terrain/render_Tiles.hlsl", "vsMain", "psMain", Vao::Topology::TriangleList);
        terrainShader.Vars()->setBuffer("tiles", split.buffer_tiles);
        // not here, irt depends on teh view we render
        //terrainShader.Vars()->setBuffer("tileLookup", split.buffer_lookup_terrain);

        terrainShader.Vars()->setTexture("gAlbedoArray", compressed_Albedo_Array);
        terrainShader.Vars()->setTexture("gPBRArray", compressed_PBR_Array);
        terrainShader.Vars()->setTexture("gNormArray", compressed_Normals_Array);
        terrainShader.Vars()->setSampler("gSmpAniso", sampler_ClampAnisotropic);
        terrainShader.Vars()->setBuffer("VB", split.buffer_terrain);

        terrainShader.Vars()["PerFrameCB"]["gisOverlayStrength"] = gis_overlay.strenght;
        terrainShader.Vars()["PerFrameCB"]["redStrength"] = gis_overlay.redStrength;
        terrainShader.Vars()["PerFrameCB"]["redScale"] = gis_overlay.redScale;
        terrainShader.Vars()["PerFrameCB"]["redOffset"] = gis_overlay.redOffset;
        terrainShader.Vars()->setSampler("gSmpLinearClamp", sampler_Clamp);

        spriteTexture = Texture::createFromFile(settings.dirRoot + "/ecosystem/sprite_diff.DDS", true, true);
        spriteNormalsTexture = Texture::createFromFile(settings.dirRoot + "/ecosystem/sprite_norm.DDS", true, false);

        terrainSpiteShader.load("Samples/Earthworks_4/hlsl/terrain/render_tile_sprite.hlsl", "vsMain", "psMain", Vao::Topology::PointList, "gsMain");
        terrainSpiteShader.Vars()->setBuffer("tiles", split.buffer_tiles);
        //terrainSpiteShader.Vars()->setBuffer("tileLookup", split.buffer_lookup_quads);
        terrainSpiteShader.Vars()->setBuffer("instanceBuffer", split.buffer_instance_quads);        // WHY BOTH
        terrainSpiteShader.Vars()->setSampler("gSampler", sampler_ClampAnisotropic);
        terrainSpiteShader.Vars()->setSampler("gSmpLinearClamp", sampler_Ribbons);




        ribbonData[0] = Buffer::createStructured(sizeof(unsigned int) * 6, 1024 * 1024 * 10); // just a nice amount for now
        ribbonData[1] = Buffer::createStructured(sizeof(unsigned int) * 6, 1024 * 1024 * 10); // just a nice amount for now



        {
            /*
            LOG_BLOCK("loading buildings /rappersville", 0);
            terrafectorSystem::logTimeX();
            fprintf(_logfile, "load %s\n", (settings.dirRoot + "/buildings/rappersville").c_str());
            fflush(_logfile);

            std::string filerappersville = settings.dirRoot + "/buildings/rappersville";
            std::ifstream ifs;
            ifs.open(filerappersville + ".info.txt");
            int numVerts = 0;
            if (ifs)
            {
                ifs >> numVerts;
                ifs.close();
            }
            numVerts = __max(numVerts, 128);    // jusy not zeo

            std::vector<_buildingVertex> VERTS;
            VERTS.resize(numVerts);
            ifs.open(filerappersville + ".raw", std::ios::binary);
            if (ifs)
            {
                ifs.read((char*)VERTS.data(), VERTS.size() * sizeof(_buildingVertex));
                ifs.close();
            }

            rappersvilleData = Buffer::createStructured(sizeof(_buildingVertex), numVerts); // just a nice amount for now
            rappersvilleData->setBlob(VERTS.data(), 0, numVerts * sizeof(_buildingVertex));


            rappersvilleShader.load("Samples/Earthworks_4/hlsl/terrain/render_Buildings_Far.hlsl", "vsMain", "psMain", Vao::Topology::TriangleList);
            rappersvilleShader.Vars()->setBuffer("vertexBuffer", rappersvilleData);

            numrapperstri = numVerts / 3;
            //drawArgs_rappersville = Buffer::createStructured(sizeof(t_DrawArguments), 1, Resource::BindFlags::UnorderedAccess | Resource::BindFlags::IndirectArg);
            */
        }


        {
            /*
            LOG_BLOCK("gliderwingData", 0);
            gliderwingData[0] = Buffer::createStructured(sizeof(_buildingVertex), 16384); // will be arounf 1300 so just enough
            gliderwingData[1] = Buffer::createStructured(sizeof(_buildingVertex), 16384); // will be arounf 1300 so just enough

            gliderwingShader.load("Samples/Earthworks_4/hlsl/terrain/render_GliderWing.hlsl", "vsMain", "psMain", Vao::Topology::TriangleStrip);


            RasterizerState::Desc rsDesc;
            rsDesc.setFillMode(RasterizerState::FillMode::Solid).setCullMode(RasterizerState::CullMode::Front);
            rasterstateGliderWing = RasterizerState::create(rsDesc);
            */
        }



        ribbonShader.load("Samples/Earthworks_4/hlsl/terrain/render_ribbons.hlsl", "vsMain", "psMain", Vao::Topology::LineStrip, "gsMain");
        ribbonShader.Vars()->setBuffer("instanceBuffer", ribbonData[0]);
        ribbonShader.Vars()->setBuffer("materials", _plantMaterial::static_materials_veg.sb_vegetation_Materials);
        ribbonShader.Vars()->setBuffer("instances", split.buffer_clippedloddedplants);
        ribbonShader.Vars()->setSampler("gSampler", sampler_Ribbons);              // fixme only cvlamlX
        ribbonShader.Vars()->setSampler("gSamplerClamp", sampler_ClampAnisotropic);              // fixme only cvlamlX




        compute_bakeFloodfill.load("Samples/Earthworks_4/hlsl/terrain/compute_bakeFloodfill.hlsl");





        triangleData = Buffer::createStructured(sizeof(triangleVertex), 16384); // just a nice amount for now

        triangleShader.load("Samples/Earthworks_4/hlsl/terrain/render_triangles.hlsl", "vsMain", "psMain", Vao::Topology::TriangleList);
        triangleShader.Vars()->setBuffer("instanceBuffer", triangleData);        // WHY BOTH
        triangleShader.Vars()->setBuffer("instances", split.buffer_clippedloddedplants);
        triangleShader.Vars()->setSampler("gSampler", sampler_ClampAnisotropic);
        triangleShader.Vars()->setSampler("gSmpLinearClamp", sampler_ClampAnisotropic);

        triangleData_VegHuman = Buffer::createStructured(sizeof(triangleVertex), 16384); // just a nice amount for now
        veghumanShader.load("Samples/Earthworks_4/hlsl/terrain/render_triangles.hlsl", "vsMain", "psMain", Vao::Topology::TriangleList);
        veghumanShader.Vars()->setBuffer("instanceBuffer", triangleData);
        veghumanShader.Vars()->setBuffer("instances", split.buffer_clippedloddedplants);
        veghumanShader.Vars()->setSampler("gSampler", sampler_ClampAnisotropic);
        veghumanShader.Vars()->setSampler("gSmpLinearClamp", sampler_ClampAnisotropic);

        {
            LOG_BLOCK("thermalsData", 0);
            thermalsData = Buffer::createStructured(sizeof(float4), numThermals * 100); // just a nice amount for now
            //            thermalsData->setBlob(thermals.data(), 0, numThermals * 100 * sizeof(float4));

            thermalsShader.load("Samples/Earthworks_4/hlsl/terrain/render_thermalRibbons.hlsl", "vsMain", "psMain", Vao::Topology::LineStrip, "gsMain");
            thermalsShader.Vars()->setBuffer("vertexBuffer", thermalsData);        // WHY BOTH
            thermalsShader.Vars()->setSampler("gSampler", sampler_ClampAnisotropic);
        }


        std::cout << "      cfd\n";
        {
            LOG_BLOCK("cfd", 0);
            cfd.sliceVTexture[0] = Texture::create2D(128, 128, Falcor::ResourceFormat::RGB32Float, 1, 1, nullptr, Falcor::Resource::BindFlags::ShaderResource);
            cfd.sliceVTexture[1] = Texture::create2D(128, 128, Falcor::ResourceFormat::RGB32Float, 1, 1, nullptr, Falcor::Resource::BindFlags::ShaderResource);
            cfd.sliceDataTexture[0] = Texture::create2D(128, 128, Falcor::ResourceFormat::RGB32Float, 1, 1, nullptr, Falcor::Resource::BindFlags::ShaderResource);
            cfd.sliceDataTexture[1] = Texture::create2D(128, 128, Falcor::ResourceFormat::RGB32Float, 1, 1, nullptr, Falcor::Resource::BindFlags::ShaderResource);

            // FIXME kry die afmetinsg van .cfd af
            cfd.sliceVolumeTexture[0][0] = Texture::create3D(128, 32, 128, Falcor::ResourceFormat::R5G6B5Unorm, 1, nullptr, Falcor::Resource::BindFlags::ShaderResource);
            cfd.sliceVolumeTexture[0][1] = Texture::create3D(128, 32, 128, Falcor::ResourceFormat::R5G6B5Unorm, 1, nullptr, Falcor::Resource::BindFlags::ShaderResource);
            cfd.sliceVolumeTexture[0][2] = Texture::create3D(128, 32, 128, Falcor::ResourceFormat::R5G6B5Unorm, 1, nullptr, Falcor::Resource::BindFlags::ShaderResource);

            cfd.sliceVolumeTexture[1][0] = Texture::create3D(128, 64, 128, Falcor::ResourceFormat::R5G6B5Unorm, 1, nullptr, Falcor::Resource::BindFlags::ShaderResource);
            cfd.sliceVolumeTexture[1][1] = Texture::create3D(128, 64, 128, Falcor::ResourceFormat::R5G6B5Unorm, 1, nullptr, Falcor::Resource::BindFlags::ShaderResource);
            cfd.sliceVolumeTexture[1][2] = Texture::create3D(128, 64, 128, Falcor::ResourceFormat::R5G6B5Unorm, 1, nullptr, Falcor::Resource::BindFlags::ShaderResource);

            cfd.sliceVolumeTexture[2][0] = Texture::create3D(128, 128, 128, Falcor::ResourceFormat::R5G6B5Unorm, 1, nullptr, Falcor::Resource::BindFlags::ShaderResource);
            cfd.sliceVolumeTexture[2][1] = Texture::create3D(128, 128, 128, Falcor::ResourceFormat::R5G6B5Unorm, 1, nullptr, Falcor::Resource::BindFlags::ShaderResource);
            cfd.sliceVolumeTexture[2][2] = Texture::create3D(128, 128, 128, Falcor::ResourceFormat::R5G6B5Unorm, 1, nullptr, Falcor::Resource::BindFlags::ShaderResource);

            cfd.sliceVolumeTexture[3][0] = Texture::create3D(128, 128, 128, Falcor::ResourceFormat::R5G6B5Unorm, 1, nullptr, Falcor::Resource::BindFlags::ShaderResource);
            cfd.sliceVolumeTexture[3][1] = Texture::create3D(128, 128, 128, Falcor::ResourceFormat::R5G6B5Unorm, 1, nullptr, Falcor::Resource::BindFlags::ShaderResource);
            cfd.sliceVolumeTexture[3][2] = Texture::create3D(128, 128, 128, Falcor::ResourceFormat::R5G6B5Unorm, 1, nullptr, Falcor::Resource::BindFlags::ShaderResource);

            cfd.sliceVolumeTexture[4][0] = Texture::create3D(128, 128, 128, Falcor::ResourceFormat::R5G6B5Unorm, 1, nullptr, Falcor::Resource::BindFlags::ShaderResource);
            cfd.sliceVolumeTexture[4][1] = Texture::create3D(128, 128, 128, Falcor::ResourceFormat::R5G6B5Unorm, 1, nullptr, Falcor::Resource::BindFlags::ShaderResource);
            cfd.sliceVolumeTexture[4][2] = Texture::create3D(128, 128, 128, Falcor::ResourceFormat::R5G6B5Unorm, 1, nullptr, Falcor::Resource::BindFlags::ShaderResource);

            cfd.sliceVolumeTexture[5][0] = Texture::create3D(128, 128, 128, Falcor::ResourceFormat::R5G6B5Unorm, 1, nullptr, Falcor::Resource::BindFlags::ShaderResource);
            cfd.sliceVolumeTexture[5][1] = Texture::create3D(128, 128, 128, Falcor::ResourceFormat::R5G6B5Unorm, 1, nullptr, Falcor::Resource::BindFlags::ShaderResource);
            cfd.sliceVolumeTexture[5][2] = Texture::create3D(128, 128, 128, Falcor::ResourceFormat::R5G6B5Unorm, 1, nullptr, Falcor::Resource::BindFlags::ShaderResource);

            cfdSliceShader.load("Samples/Earthworks_4/hlsl/terrain/render_cfdSlice.hlsl", "vsMain", "psMain", Vao::Topology::TriangleStrip);
            cfdSliceShader.Vars()->setSampler("gSampler", sampler_Trilinear);
            cfdSliceShader.Vars()->setSampler("gSamplerClamp", sampler_ClampAnisotropic);
            cfdSliceShader.Vars()->setTexture("gV", cfd.sliceVTexture[0]);
            cfdSliceShader.Vars()->setTexture("gData", cfd.sliceDataTexture[0]);
            cfdSliceShader.Vars()->setTexture("gV_1", cfd.sliceVTexture[1]);
            cfdSliceShader.Vars()->setTexture("gData_1", cfd.sliceDataTexture[1]);
        }



        vegetation.skyTexture = Texture::createFromFile(settings.dirResource + "/skies/alps_bc.dds", false, true);
        vegetation.envTexture = Texture::createFromFile(settings.dirResource + "/skies/alps_IR_bc.dds", false, true);
        vegetation.dappledLightTexture = Texture::createFromFile(settings.dirResource + "/vegetation/dappled_noise_01.jpg", false, true);
        triangleShader.Vars()->setTexture("gSky", vegetation.skyTexture);
        ribbonShader.Vars()->setTexture("gEnv", vegetation.envTexture);




        // Loadss the sky triangles - DO BERTEER ###########################################################################################################
        {

            LOG_BLOCK("Load sky triangles - do better", 0);
            unsigned int flags =
                aiProcess_FlipUVs |
                aiProcess_Triangulate |
                aiProcess_PreTransformVertices |
                //aiProcess_JoinIdenticalVertices |
                aiProcess_GenBoundingBoxes;


            triangleVertex testribbonsFile[50 * 128];
            memset(testribbonsFile, 0, 50 * 128 * sizeof(triangleVertex));
            uint vertCount = 0;
            Assimp::Importer importer;
            const aiScene* scene = nullptr;
            char name[256];

            //for (int F = 1; F <= 16; F++)
            {
                sprintf(name, (settings.dirResource + "/cube.fbx").c_str());

                scene = importer.ReadFile(name, flags);
                if (scene)
                {
                    aiMesh* M = scene->mMeshes[0];
                    uint numSegments = M->mNumFaces;
                    for (uint j = 0; j < numSegments; j++)
                    {
                        aiFace face = M->mFaces[j];
                        for (int idx = 0; idx < 3; idx++)
                        {
                            aiVector3D V = M->mVertices[face.mIndices[idx]];
                            aiVector3D N = M->mNormals[face.mIndices[idx]];
                            aiVector3D U = M->mTextureCoords[0][face.mIndices[idx]];
                            testribbonsFile[vertCount].pos = float3(V.x, V.y, V.z) * 0.01f;
                            testribbonsFile[vertCount].norm = float3(N.x, N.y, N.z);
                            testribbonsFile[vertCount].u = U.x;
                            testribbonsFile[vertCount].v = U.y;
                            vertCount++;
                        }
                    }
                }
            }


            triangleData->setBlob(testribbonsFile, 0, 50 * 128 * sizeof(triangleVertex));

        }

        // vegetation human shape
        /*
        {
            unsigned int flags =     aiProcess_FlipUVs | aiProcess_Triangulate | aiProcess_PreTransformVertices |  aiProcess_GenBoundingBoxes;

            triangleVertex testribbonsFile[50 * 128];
            memset(testribbonsFile, 0, 50 * 128 * sizeof(triangleVertex));
            uint vertCount = 0;
            Assimp::Importer importer;
            const aiScene* scene = nullptr;
            char name[256];

            {
                sprintf(name, (settings.dirResource + "/vegetationHuman.fbx").c_str());

                scene = importer.ReadFile(name, flags);
                if (scene)
                {
                    aiMesh* M = scene->mMeshes[0];
                    uint numSegments = M->mNumFaces;
                    for (uint j = 0; j < numSegments; j++)
                    {
                        aiFace face = M->mFaces[j];
                        for (int idx = 0; idx < 3; idx++)
                        {
                            aiVector3D V = M->mVertices[face.mIndices[idx]];
                            aiVector3D N = M->mNormals[face.mIndices[idx]];
                            aiVector3D U = M->mTextureCoords[0][face.mIndices[idx]];
                            testribbonsFile[vertCount].pos = float3(V.x, V.y, V.z) * 0.01f;
                            testribbonsFile[vertCount].norm = float3(N.x, N.y, N.z);
                            testribbonsFile[vertCount].u = U.x;
                            testribbonsFile[vertCount].v = U.y;
                            vertCount++;
                        }
                    }
                }
            }

            triangleData_VegHuman->setBlob(testribbonsFile, 0, 50 * 128 * sizeof(triangleVertex));
        }
        */


        std::cout << "      shaders\n";
        {
            LOG_BLOCK("shaders", 0);

            compute_TerrainUnderMouse.load("Samples/Earthworks_4/hlsl/terrain/compute_terrain_under_mouse.hlsl");
            compute_TerrainUnderMouse.Vars()->setSampler("gSampler", sampler_Clamp);
            compute_TerrainUnderMouse.Vars()->setTexture("gHeight", height_Array);
            compute_TerrainUnderMouse.Vars()->setBuffer("tiles", split.buffer_tiles);
            compute_TerrainUnderMouse.Vars()->setBuffer("groundcover_feedback", split.buffer_feedback);

            // clear
            split.compute_tileClear.load("Samples/Earthworks_4/hlsl/terrain/compute_tileClear.hlsl");
            split.compute_tileClear.Vars()->setBuffer("feedback", split.buffer_feedback);
            split.compute_tileClear.Vars()->setBuffer("DrawArgs_Terrain", split.drawArgs_tiles);
            split.compute_tileClear.Vars()->setBuffer("DrawArgs_Quads", split.drawArgs_quads);
            //split.compute_tileClear.Vars()->setBuffer("DrawArgs_Plants", split.drawArgs_plants);
            //split.compute_tileClear.Vars()->setBuffer("DrawArgs_ClippedLoddedPlants", split.drawArgs_clippedloddedplants);
            split.compute_tileClear.Vars()->setBuffer("DispatchArgs_Plants", split.dispatchArgs_plants);


            // plants clip lod animate
            split.compute_clipLodAnimatePlants.load("Samples/Earthworks_4/hlsl/terrain/compute_clipLodAnimatePlants.hlsl");
            split.compute_clipLodAnimatePlants.Vars()->setBuffer("tiles", split.buffer_tiles);
            //split.compute_clipLodAnimatePlants.Vars()->setBuffer("tileLookup", split.buffer_lookup_plants); FO later per view
            split.compute_clipLodAnimatePlants.Vars()->setBuffer("plantBuffer", split.buffer_instance_plants);
            split.compute_clipLodAnimatePlants.Vars()->setBuffer("output", split.buffer_clippedloddedplants);
            //split.compute_clipLodAnimatePlants.Vars()->setBuffer("drawArgs_Plants", split.drawArgs_clippedloddedplants);
            split.compute_clipLodAnimatePlants.Vars()->setBuffer("feedback", split.buffer_feedback);



            // split merge
            split.compute_tileSplitMerge.load("Samples/Earthworks_4/hlsl/terrain/compute_tileSplitMerge.hlsl");
            split.compute_tileSplitMerge.Vars()->setBuffer("tiles", split.buffer_tiles);
            split.compute_tileSplitMerge.Vars()->setBuffer("feedback", split.buffer_feedback);

            // generate
            split.compute_tileGenerate.load("Samples/Earthworks_4/hlsl/terrain/compute_tileGenerate.hlsl");
            split.compute_tileGenerate.Vars()->setBuffer("quad_instance", split.buffer_instance_quads);
            split.compute_tileGenerate.Vars()->setBuffer("tiles", split.buffer_tiles);
            split.compute_tileGenerate.Vars()->setTexture("gNoise", split.noise_u16);
            split.compute_tileGenerate.Vars()->setTexture("gHgt", split.tileFbo->getColorTexture(0));
            split.compute_tileGenerate.Vars()->setTexture("gEct1", split.tileFbo->getColorTexture(4));
            split.compute_tileGenerate.Vars()->setTexture("gEct2", split.tileFbo->getColorTexture(5));
            split.compute_tileGenerate.Vars()->setTexture("gEct3", split.tileFbo->getColorTexture(6));
            split.compute_tileGenerate.Vars()->setTexture("gEct4", split.tileFbo->getColorTexture(7));

            // passthrough
            split.compute_tilePassthrough.load("Samples/Earthworks_4/hlsl/terrain/compute_tilePassthrough.hlsl");
            split.compute_tilePassthrough.Vars()->setBuffer("quad_instance", split.buffer_instance_quads);
            split.compute_tilePassthrough.Vars()->setBuffer("plant_i", split.buffer_instance_plants);
            split.compute_tilePassthrough.Vars()->setBuffer("feedback", split.buffer_feedback);
            split.compute_tilePassthrough.Vars()->setBuffer("tiles", split.buffer_tiles);
            split.compute_tilePassthrough.Vars()->setTexture("gHgt", split.tileFbo->getColorTexture(0));
            split.compute_tilePassthrough.Vars()->setTexture("gNoise", split.noise_u16);

            // build lookup
            split.compute_tileBuildLookup.load("Samples/Earthworks_4/hlsl/terrain/compute_tileBuildLookup.hlsl");
            split.compute_tileBuildLookup.Vars()->setBuffer("tiles", split.buffer_tiles);
            split.compute_tileBuildLookup.Vars()->setBuffer("DrawArgs_Quads", split.drawArgs_quads);
            split.compute_tileBuildLookup.Vars()->setBuffer("DrawArgs_Terrain", split.drawArgs_tiles);
            split.compute_tileBuildLookup.Vars()->setBuffer("feedback", split.buffer_feedback);
            split.compute_tileBuildLookup.Vars()->setBuffer("DispatchArgs_Plants", split.dispatchArgs_plants);
            split.compute_tileBuildLookup.Vars()->setBuffer("tileCenters", split.buffer_tileCenters);  // to clear unused tile
            auto& block = split.compute_tileBuildLookup.Vars()->getParameterBlock("viewRenderData");
            ShaderVar terrainLookup = block->findMember("terrainLookup");
            ShaderVar plantLookup = block->findMember("plantLookup");
            ShaderVar quadLookup = block->findMember("quadLookup");
            for (int i = 0; i < numRenderViews; i++)
            {
                terrainLookup[i] = split.buffer_lookup_terrain[i];
                plantLookup[i] = split.buffer_lookup_plants[i];
                quadLookup[i] = split.buffer_lookup_quads[i];
            }

            // bicubic
            split.compute_tileBicubic.load("Samples/Earthworks_4/hlsl/terrain/compute_tileBicubic.hlsl");
            split.compute_tileBicubic.Vars()->setSampler("linearSampler", sampler_Clamp);
            split.compute_tileBicubic.Vars()->setTexture("gOutput", split.tileFbo->getColorTexture(0));
            split.compute_tileBicubic.Vars()->setTexture("gOutputAlbedo", split.tileFbo->getColorTexture(1));
            split.compute_tileBicubic.Vars()->setTexture("gOutputPermanence", split.tileFbo->getColorTexture(3));
            split.compute_tileBicubic.Vars()->setTexture("gDebug", split.debug_texture);
            //split.compute_tileBicubic.Vars()->setTexture("gOuthgt_TEMPTILLTR", split.tileFbo->getColorTexture(0));

            // ecotopes
            split.compute_tileEcotopes.load("Samples/Earthworks_4/hlsl/terrain/compute_tileEcotopes.hlsl");
            split.compute_tileEcotopes.Vars()->setSampler("linearSampler", sampler_Clamp);
            split.compute_tileEcotopes.Vars()->setTexture("gHeight", split.tileFbo->getColorTexture(0));
            split.compute_tileEcotopes.Vars()->setTexture("gAlbedo", split.tileFbo->getColorTexture(1));
            split.compute_tileEcotopes.Vars()->setTexture("gInPermanence", split.tileFbo->getColorTexture(3));
            split.compute_tileEcotopes.Vars()->setTexture("gInEct_0", split.tileFbo->getColorTexture(4));
            split.compute_tileEcotopes.Vars()->setTexture("gInEct_1", split.tileFbo->getColorTexture(5));
            split.compute_tileEcotopes.Vars()->setTexture("gInEct_2", split.tileFbo->getColorTexture(6));
            split.compute_tileEcotopes.Vars()->setTexture("gInEct_3", split.tileFbo->getColorTexture(7));
            split.compute_tileEcotopes.Vars()->setTexture("gNoise", split.noise_u16);
            split.compute_tileEcotopes.Vars()->setBuffer("tiles", split.buffer_tiles);
            split.compute_tileEcotopes.Vars()->setBuffer("quad_instance", split.buffer_instance_quads);
            split.compute_tileEcotopes.Vars()->setBuffer("feedback", split.buffer_feedback);


            // normals
            split.compute_tileNormals.load("Samples/Earthworks_4/hlsl/terrain/compute_tileNormals.hlsl");
            split.compute_tileNormals.Vars()->setTexture("gInHgt", split.tileFbo->getColorTexture(0));
            split.compute_tileNormals.Vars()->setTexture("gOutNormals", split.normals_texture);
            split.compute_tileNormals.Vars()->setTexture("gOutput", split.debug_texture);
            split.compute_tileNormals.Vars()->setBuffer("tiles", split.buffer_tiles);

            // vertices
            split.compute_tileVerticis.load("Samples/Earthworks_4/hlsl/terrain/compute_tileVertices.hlsl");
            split.compute_tileVerticis.Vars()->setSampler("linearSampler", sampler_Clamp);
            split.compute_tileVerticis.Vars()->setTexture("gInHgt", split.tileFbo->getColorTexture(0));
            split.compute_tileVerticis.Vars()->setTexture("gOutVerts", split.vertex_A_texture);
            split.compute_tileVerticis.Vars()->setTexture("gDebug", split.debug_texture);
            split.compute_tileVerticis.Vars()->setBuffer("tileCenters", split.buffer_tileCenters);
            split.compute_tileVerticis.Vars()->setBuffer("tiles", split.buffer_tiles);

            // jumpflood
            // It may even be faster to set this up twice and hop between the two
            split.compute_tileJumpFlood.load("Samples/Earthworks_4/hlsl/terrain/compute_tileJumpFlood.hlsl");
            split.compute_tileJumpFlood.Vars()->setTexture("gDebug", split.debug_texture);

            // delaunay
            split.compute_tileDelaunay.load("Samples/Earthworks_4/hlsl/terrain/compute_tileDelaunay.hlsl");
            split.compute_tileDelaunay.Vars()->setTexture("gInHgt", split.tileFbo->getColorTexture(0));
            split.compute_tileDelaunay.Vars()->setTexture("gInVerts", split.vertex_B_texture);
            split.compute_tileDelaunay.Vars()->setBuffer("VB", split.buffer_terrain);
            split.compute_tileDelaunay.Vars()->setBuffer("tiles", split.buffer_tiles);
            //split.compute_tileDelaunay.Vars()->setBuffer("tileDrawargsArray", split.drawArgs_tiles);

            // elevation mipmap
            /*
            split.compute_tileElevationMipmap.load("Samples/Earthworks_4/hlsl/terrain/compute_tileElevationMipmap.hlsl");
            split.compute_tileElevationMipmap.Vars()->setTexture("gInHgt", split.tileFbo->getColorTexture(0));
            split.compute_tileElevationMipmap.Vars()->setTexture("gDebug", split.debug_texture);
            const auto& mipmapReflector = split.compute_tileElevationMipmap.Reflector()->getDefaultParameterBlock();
            ParameterBlock::BindLocation bind_mip1 = mipmapReflector->getResourceBinding("gMip1");
            ParameterBlock::BindLocation bind_mip2 = mipmapReflector->getResourceBinding("gMip2");
            ParameterBlock::BindLocation bind_mip3 = mipmapReflector->getResourceBinding("gMip3");
            split.compute_tileElevationMipmap.Vars()->setUav(bind_mip1, split.tileFbo->getColorTexture(0)->getUAV(1));
            split.compute_tileElevationMipmap.Vars()->setUav(bind_mip2, split.tileFbo->getColorTexture(0)->getUAV(2));
            split.compute_tileElevationMipmap.Vars()->setUav(bind_mip3, split.tileFbo->getColorTexture(0)->getUAV(3));
            */
            // BC6H compressor
            split.bc6h_texture = Texture::create2D(tile_numPixels / 4, tile_numPixels / 4, Falcor::ResourceFormat::RGBA32Uint, 1, 1, nullptr, Falcor::Resource::BindFlags::UnorderedAccess);
            split.compute_bc6h.load("Samples/Earthworks_4/hlsl/terrain/compute_bc6h.hlsl");
            split.compute_bc6h.Vars()->setTexture("gOutput", split.bc6h_texture);
        }

    }



    {
        LOG_BLOCK("stuff", 0);
        allocateTiles(numTiles);

        LOG_LINE(1, "elevationCache.resize");
        elevationCache.resize(45);
        loadElevationHash(pRenderContext);

        LOG_LINE(1, "imageCache.resize");
        imageCache.resize(45);
        loadImageHash(pRenderContext);

        init_TopdownRender();

        mSpriteRenderer.onLoad();
    }



    terrafectorEditorMaterial::rootFolder = settings.dirResource + "/";
    ecotopeSystem::resPath = settings.dirResource + "/";
    fprintf(_logfile, "terrafectorEditorMaterial::rootFolder = %s\n", terrafectorEditorMaterial::rootFolder.c_str());
    fflush(_logfile);

    {
        LOG_BLOCK("more stuff", 0);
        _plantMaterial::static_materials_veg.sb_vegetation_Materials = Buffer::createStructured(sizeof(sprite_material), 1024 * 8);      // just a lot


        plants_Root.envTexture = vegetation.envTexture;
        plants_Root.dappledLightTexture = vegetation.dappledLightTexture;


        plants_Root.onLoad();
        /*
        plants_Root.vegetationShader.Vars()->setTexture("gEnv", vegetation.envTexture);
        //plants_Root.vegetationShader.Vars()->setBuffer("feedback", split.buffer_feedback);
        plants_Root.vegetationShader.Vars()->setBuffer("feedback_Veg", plants_Root.buffer_feedback);
        plants_Root.vegetationShader.Vars()->setTexture("gDappledLight", vegetation.dappledLightTexture);

        plants_Root.billboardShader.Vars()->setTexture("gEnv", vegetation.envTexture);
        plants_Root.billboardShader.Vars()->setBuffer("feedback_Veg", plants_Root.buffer_feedback);
        */



        terrainSpiteShader.Vars()->setBuffer("plant_buffer", plants_Root.plantData);
        terrainSpiteShader.Vars()->setBuffer("materials", _plantMaterial::static_materials_veg.sb_vegetation_Materials);


        split.compute_clipLodAnimatePlants.Vars()->setBuffer("block_buffer", plants_Root.blockData);
        split.compute_clipLodAnimatePlants.Vars()->setBuffer("plant_buffer", plants_Root.plantData);
        split.compute_clipLodAnimatePlants.Vars()->setBuffer("drawArgs_Plants", plants_Root.drawArgs_vegetation);
        split.compute_clipLodAnimatePlants.Vars()->setBuffer("feedback_Veg", plants_Root.buffer_feedback);
        split.compute_clipLodAnimatePlants.Vars()->setBuffer("instance_out", plants_Root.instanceData);

        split.compute_tilePassthrough.Vars()->setBuffer("plant_buffer", plants_Root.plantData);

        split.compute_tileClear.Vars()->setBuffer("feedback_Veg", plants_Root.buffer_feedback);
        split.compute_tileClear.Vars()->setBuffer("DrawArgs_Plants", plants_Root.drawArgs_vegetation);
    }


    {
        LOG_BLOCK("mEcosystem.load - needs a name set", 0);
        mEcosystem.terrainSize = settings.size;
        //mEcosystem.load(settings.dirRoot + "/ecosystem/steg.ecosystem", settings.dirResource + "/");    // FIXME MOVE To lastFILE
    }

    {
        LOG_BLOCK("terrafectors.loadPath", 0);
        terrafectors.loadPath(settings.dirRoot + "/terrafectors", settings.dirRoot + "/bake", false);
        mRoadNetwork.rootPath = settings.dirRoot + "/";
    }

    {
        /*
        LOG_BLOCK("paraglider", 0);
        std::cout << "      paraglider\n";

        terrafectorSystem::logTimeX();
        fprintf(_logfile, "start of paraBuilder\n");


        AirSim.setup();
        paraBuilder.setxfoilDir(settings.dirResource + "/xfoil");
        paraBuilder.xfoil_shape("naca4415");
        paraBuilder.buildCp();
        paraBuilder.buildWing();

        //paraBuilder.buildLinesFILE_NOVA_AONIC_medium();
        paraBuilder.buildLines();
        paraBuilder.generateLines();

        paraBuilder.builWingConstraints();
        //paraBuilder.builLineConstraints();

        paraBuilder.visualsPack(paraRuntime.ribbon, paraRuntime.packedRibbons, paraRuntime.ribbonCount, paraRuntime.changed);

        paraRuntime.setxfoilDir(settings.dirResource + "/xfoil");
        paraRuntime.setWing("naca4415");

        //paraRuntime.setWind(settings.dirRoot + "/gis/_export/root4096.bil", glm::normalize(float3(-1, 0, 0.4f)));
        paraRuntime.loadWind(settings.dirRoot + "\\cfd\\");

        paraRuntime.setup(paraBuilder.x, paraBuilder.w, paraBuilder.cross, paraBuilder.spanSize, paraBuilder.chordSize, paraBuilder.constraints, false, cameraViews[CameraType_Main_Center].view);
        paraRuntime.Cp = paraBuilder.Cp;
        memcpy(paraRuntime.CPbrakes, paraBuilder.CPbrakes, sizeof(float) * 6 * 11 * 25);
        //paraRuntime.CPbrakes = paraBuilder.CPbrakes;
        paraRuntime.linesLeft = paraBuilder.linesLeft;
        paraRuntime.linesRight = paraBuilder.linesRight;
        glider.loaded = true;



        newGliderRuntime.importBin();
        newGliderRuntime.solve(0.000f, -1);
        //newGliderRuntime.process_xfoil_Cp(settings.dirResource + "/xfoil/Omega/");
        newGliderRuntime.loadXFoil_Cp(settings.dirResource + "/xfoil/Omega/", 52);  // FIXXXME rib numer is a little hardcoded, and process this to a singel file
        glider.newGliderLoaded = true;
        */
    }

    terrafectorSystem::logTimeX();
    fprintf(_logfile, "end of paraBuilder\n");

    //_swissBuildings buildings;
    //buildings.process(settings.dirRoot + "/buildings/testQGISDXF.dxf");
    //buildings.processGeoJSON(settings.dirRoot + "/buildings/QGIS.geojson");
    //buildings.processGeoJSON("E:/rappersville/", "walls");
    //buildings.processGeoJSON("E:/rappersville/", "roofs");
    //buildings.processWallRoof(settings.dirRoot + "/buildings/");
    //buildings.processWallRoof("E:/rappersville/");
    //buildings.processWallRoof("E:/rappersville/");


    //_plantMaterial::static_materials_veg.materialVector.emplace_back();
    //_plantMaterial::static_materials_veg.materialVector[0].fullPath = terrafectorEditorMaterial::rootFolder + "//test";
    //_plantMaterial::static_materials_veg.selectedMaterial = 0;

    //_plantMaterial::static_materials_veg.find_insert_texture(terrafectorEditorMaterial::rootFolder + "textures\\bark\\Oak1_albedo.dds", true);
    //_plantMaterial::static_materials_veg.find_insert_texture(terrafectorEditorMaterial::rootFolder + "textures\\bark\\Oak1_sprite_normal.dds", false);
    //_plantMaterial::static_materials_veg.find_insert_texture(terrafectorEditorMaterial::rootFolder + "textures\\twigs\\dandelion_leaf1_albedo.dds", true);
    //_plantMaterial::static_materials_veg.find_insert_texture(terrafectorEditorMaterial::rootFolder + "textures\\twigs\\dandelion_leaf1_normal.dds", false);



}




void terrainManager::init_TopdownRender()
{

    terrafectorEditorMaterial::static_materials.sb_Terrafector_Materials = Buffer::createStructured(sizeof(TF_material), 2048); // FIXME hardcoded
    split.shader_spline3D.load("Samples/Earthworks_4/hlsl/terrain/render_spline.hlsl", "vsMain", "psMain", Vao::Topology::TriangleList);
    split.shader_spline3D.Vars()->setBuffer("materials", terrafectorEditorMaterial::static_materials.sb_Terrafector_Materials);
    split.shader_spline3D.Vars()->setSampler("gSmpLinear", sampler_Trilinear);
    //split.shader_spline3D.Program()->getReflector()
    split.shader_splineTerrafector.load("Samples/Earthworks_4/hlsl/terrain/render_splineTerrafector.hlsl", "vsMain", "psMain", Vao::Topology::TriangleList);
    //split.shader_splineTerrafector.State()->setFbo(split.tileFbo);
    split.shader_splineTerrafector.Vars()->setBuffer("materials", terrafectorEditorMaterial::static_materials.sb_Terrafector_Materials);


    // mesh terrafector shader
    split.shader_meshTerrafector.load("Samples/Earthworks_4/hlsl/terrain/render_meshTerrafector.hlsl", "vsMain", "psMain", Vao::Topology::TriangleList);
    split.shader_meshTerrafector.Vars()->setSampler("gSmpLinear", sampler_Trilinear);
    //split.shader_meshTerrafector.Vars()["PerFrameCB"]["gConstColor"] = false;
    split.shader_meshTerrafector.State()->setFbo(split.tileFbo);
    split.shader_meshTerrafector.Vars()->setBuffer("materials", terrafectorEditorMaterial::static_materials.sb_Terrafector_Materials);

    DepthStencilState::Desc depthDesc;
    depthDesc.setDepthEnabled(true);
    depthDesc.setDepthWriteMask(false);
    depthDesc.setStencilEnabled(false);

    depthDesc.setDepthFunc(DepthStencilState::Func::Greater);
    split.depthstateFuther = DepthStencilState::create(depthDesc);

    depthDesc.setDepthFunc(DepthStencilState::Func::LessEqual);
    split.depthstateCloser = DepthStencilState::create(depthDesc);

    depthDesc.setDepthFunc(DepthStencilState::Func::Always);
    split.depthstateAll = DepthStencilState::create(depthDesc);

    RasterizerState::Desc rsDesc;
    rsDesc.setFillMode(RasterizerState::FillMode::Solid).setCullMode(RasterizerState::CullMode::None);
    split.rasterstateSplines = RasterizerState::create(rsDesc);

    BlendState::Desc blendDesc;
    blendDesc.setRtBlend(0, true);
    blendDesc.setRtParams(0, BlendState::BlendOp::Add, BlendState::BlendOp::Add, BlendState::BlendFunc::SrcAlpha, BlendState::BlendFunc::OneMinusSrcAlpha, BlendState::BlendFunc::Zero, BlendState::BlendFunc::Zero);
    split.blendstateSplines = BlendState::create(blendDesc);

    blendDesc.setIndependentBlend(true);
    for (int i = 0; i < 8; i++)
    {
        // clear all
        blendDesc.setRenderTargetWriteMask(i, true, true, true, true);
        blendDesc.setRtBlend(i, true);
        blendDesc.setRtParams(i, BlendState::BlendOp::Add, BlendState::BlendOp::Add, BlendState::BlendFunc::SrcAlpha, BlendState::BlendFunc::OneMinusSrcAlpha, BlendState::BlendFunc::SrcAlpha, BlendState::BlendFunc::OneMinusSrcAlpha);
    }
    //??? hoekom het ek dit gedoen
    blendDesc.setRtParams(0, BlendState::BlendOp::Add, BlendState::BlendOp::Add, BlendState::BlendFunc::One, BlendState::BlendFunc::OneMinusSrcAlpha, BlendState::BlendFunc::One, BlendState::BlendFunc::OneMinusSrcAlpha);
    split.blendstateRoadsCombined = BlendState::create(blendDesc);

    blendDesc.setIndependentBlend(true);
    for (int i = 0; i < 8; i++)
    {
        // clear all
        blendDesc.setRenderTargetWriteMask(i, true, true, true, true);
        blendDesc.setRtBlend(i, true);
        blendDesc.setRtParams(i, BlendState::BlendOp::Add, BlendState::BlendOp::Add, BlendState::BlendFunc::SrcAlpha, BlendState::BlendFunc::OneMinusSrcAlpha, BlendState::BlendFunc::SrcAlphaSaturate, BlendState::BlendFunc::One);
    }
    blendDesc.setRtParams(3, BlendState::BlendOp::Add, BlendState::BlendOp::Add, BlendState::BlendFunc::SrcAlpha, BlendState::BlendFunc::OneMinusSrcAlpha, BlendState::BlendFunc::SrcAlphaSaturate, BlendState::BlendFunc::One);
    blendstateVegBake = BlendState::create(blendDesc);

    splines.bezierData = Buffer::createStructured(sizeof(cubicDouble), splines.maxBezier);
    splines.indexData = Buffer::createStructured(sizeof(bezierLayer), splines.maxIndex);
    splines.indexDataBakeOnly = Buffer::createStructured(sizeof(bezierLayer), splines.maxIndex);
    splines.indexData_LOD4 = Buffer::createStructured(sizeof(bezierLayer), splines.maxIndex * 2); //*2 for safety
    splines.indexData_LOD6 = Buffer::createStructured(sizeof(bezierLayer), splines.maxIndex * 2); //*2 for safety
    splines.indexData_LOD8 = Buffer::createStructured(sizeof(bezierLayer), splines.maxIndex * 2); //*2 for safety   // 8Mb for now 1M points 8bytes per bez

    splines.dynamic_bezierData = Buffer::createStructured(sizeof(cubicDouble), splines.maxDynamicBezier);
    splines.dynamic_indexData = Buffer::createStructured(sizeof(bezierLayer), splines.maxDynamicIndex);
}


void terrainManager::allocateTiles(uint numTiles)
{
    quadtree_tile tile;

    m_tiles.clear();
    m_tiles.reserve(numTiles);
    split.cpuTiles.clear();
    split.cpuTiles.resize(numTiles);


    for (uint i = 0; i < numTiles; i++)
    {
        tile.init(i);
        m_tiles.push_back(tile);
    }

    reset();
}

void terrainManager::reset(bool _fullReset)
{
    fullResetDoNotRender = _fullReset;

    m_free.clear();
    m_used.clear();

    for (uint i = 0; i < m_tiles.size(); i++)
    {
        m_free.push_back(&m_tiles[i]);
    }

    quadtree_tile* root = m_free.front();
    m_free.pop_front();
    root->set(0, 0, 0, settings.size, float4(-0.5f * settings.size, 0, -0.5f * settings.size, 0.0f), nullptr);

    m_used.push_back(root);

    // FIXME this could be done faster by changing the cache compute shader to alwas clear if its Tile zero thats caching
    // or add a special shader for that
    if (split.buffer_tiles) {
        for (uint i = 0; i < m_tiles.size(); i++)
        {
            split.cpuTiles[i].lod = 0;
            split.cpuTiles[i].Y = 0;
            split.cpuTiles[i].X = 0;
            split.cpuTiles[i].flags = 0;

            split.cpuTiles[i].origin = float3(0, 0, 0);
            split.cpuTiles[i].scale_1024 = 0;

            split.cpuTiles[i].numQuads = 0;
            split.cpuTiles[i].numPlants = 0;
            split.cpuTiles[i].numTriangles = 0;
            split.cpuTiles[i].numVerticis = 0;
        }
        split.buffer_tiles->setBlob(&split.cpuTiles, 0, m_tiles.size() * sizeof(gpuTile));
        // lets hope for now the upload is now automatic
        //split.buffer_tiles->>uploadToGPU();
    }
}

uint32_t getHashFromTileCoords(unsigned int lod, unsigned int y, unsigned int x) {
    return (lod << 28) + (y << 14) + (x);
}

void terrainManager::loadElevationHash(RenderContext* pRenderContext)
{
    std::string fullpath = settings.dirRoot + "/elevations.txt";
    LOG_LINE(1, fullpath.c_str());
    elevationTileHashmap.clear();
    reset();


    FILE* pFileHgt = fopen(fullpath.c_str(), "r");
    if (pFileHgt)
    {
        heightMap map;
        int texSize;
        char filename[256];
        int items = 1;
        uint linesProcessed = 0;
        do {
            items = fscanf(pFileHgt, "%d %d %d %d %f %f %f %f %f %s\n", &map.lod, &map.y, &map.x, &texSize, &map.origin.x, &map.origin.y, &map.size, &map.hgt_offset, &map.hgt_scale, filename);
            if (items > 0)
            {
                linesProcessed++;
                uint32_t hash = getHashFromTileCoords(map.lod, map.y, map.x);

                fullpath = settings.dirRoot + "/" + filename;
                if (map.lod == 0)
                {
                    std::vector<float> data;
                    data.resize(texSize * texSize * 2);  //the extra ios space for the miopmaps


                    FILE* pData = fopen(fullpath.c_str(), "rb");
                    if (pData)
                    {
                        fread(data.data(), sizeof(float), texSize * texSize, pData);
                        fclose(pData);
                    }
                    /*
                    for (int y = 0; y < texSize; y++)
                    {
                        for (int x = 0; x < texSize; x++)
                        {
                            data[y * texSize + x] = 1000.f;
                        }
                    }*/
                    split.rootElevation = Texture::create2D(texSize, texSize, Falcor::ResourceFormat::R32Float, 1, 8, data.data(), Resource::BindFlags::ShaderResource | Resource::BindFlags::RenderTarget);
                    split.rootElevation->generateMips(pRenderContext);
                    map.hgt_offset = 0;
                    map.hgt_scale = 1;
                    elevationTileHashmap[hash] = map;
                }
                else
                {
                    map.filename = fullpath;
                    elevationTileHashmap[hash] = map;
                }
            }
        } while (items > 0);


        fclose(pFileHgt);
    }

}


void terrainManager::loadImageHash(RenderContext* pRenderContext)
{
    /*
    heightMap map;
    int texSize;
    std::ifstream ifs;
    std::string fullpath = settings.dirRoot + "/orthophotos.txt";

    imageTileHashmap.clear();
    ifs.open(fullpath);
    if (ifs)
    {
        while (ifs >> map.lod >> map.y >> map.x >> texSize >> map.origin.x >> map.origin.y >> map.size >> map.filename)
        {
            uint32_t hash = getHashFromTileCoords(map.lod, map.y, map.x);
            map.filename = settings.dirRoot + "/" + map.filename;
            imageTileHashmap[hash] = map;
        }
        ifs.close();
    }
    */

    LOG_LINE(1, "imageDirectory load...");
    std::string fullpath = settings.dirRoot + "/orthophotos.json";
    LOG_LINE(1, fullpath.c_str());
    imageDirectory.load(fullpath);
    imageDirectory.cache0(settings.dirRoot + "/orthophoto/");
    //and load 0, 0, 0

    //imageDirectory.cache.set(imageDirectory.files[0].hash, )


}



void terrainManager::onShutdown()
{
}


std::string blockFromPositionB(glm::vec3 _pos)
{
    float halfsize = ecotopeSystem::terrainSize / 2.f;
    float blocksize = ecotopeSystem::terrainSize / 16.f;

    uint y = (uint)floor((_pos.z + halfsize) / blocksize);
    uint x = (uint)floor((_pos.x + halfsize) / blocksize);
    std::string answer = char(65 + x) + std::to_string(y);
    return answer;
}



void replaceAllterrain(std::string& str, const std::string& from, const std::string& to) {
    if (from.empty())
        return;
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // In case 'to' contains 'from', like replacing 'x' with 'yx'
    }
}

void terrainManager::onGuiRender_Debug(Gui* _gui)
{
    ImGui::PushFont(_gui->getFont("default"));
    {
        ImGui::Text("Debug info");
        ImGui::Columns(3);
        {
            ImGui::Text("%d of %d tiles used", (int)m_used.size(), (int)m_tiles.size());
            ImGui::NewLine();
            ImGui::Text("info for main_CENTER");   // see if we can expand for any chosen view

            ImGui::NewLine();
            ImGui::SameLine(50, 0);
            ImGui::Text("#");
            ImGui::SameLine(100, 0);
            ImGui::Text("tri - k");
            ImGui::SameLine(200, 0);
            ImGui::Text("bb - k");
            ImGui::SameLine(300, 0);
            ImGui::Text("plant");


            for (uint L = 0; L < 18; L++)
            {
                ImGui::Text("%3d", L);
                ImGui::SameLine(50, 0);
                ImGui::Text("%5d", split.feedback.numTiles[L]);
                ImGui::SameLine(100, 0);
                ImGui::Text("%5d", (int)((float)split.feedback.numTris[L] / 1000.f));
                ImGui::SameLine(200, 0);
                ImGui::Text("%d", (int)((float)split.feedback.numSprite[L] / 1000.f));
                ImGui::SameLine(300, 0);
                ImGui::Text("%5d", split.feedback.numPlantsLOD[L]);

            }
        }
        ImGui::NextColumn();
        {
            ImGui::Text("Tile based total per view");
            ImGui::Text(".");
            ImGui::SameLine(100, 0);
            ImGui::Text("terrain");
            ImGui::SameLine(250, 0);
            ImGui::Text("bb");
            ImGui::SameLine(400, 0);
            ImGui::Text("plants");

            for (int i = 0; i < numRenderViews; i++)
            {
                ImGui::Text(viewNames[i].c_str());
                if (1 << i & viewMask)
                {
                    ImGui::SameLine(100, 0);
                    ImGui::Text("%d k", split.feedback.numTerrainVerts[i] / 1000);
                    ImGui::SameLine(250, 0);
                    ImGui::Text("%d k", split.feedback.numQuads[i] / 1000);
                    ImGui::SameLine(400, 0);
                    ImGui::Text("%d", split.feedback.numPlants[i]);
                    // FIXME test and warn for buffer overruns
                }
            }
            ImGui::Text("%d ok, %d clip", split.feedback.vegRibbonOKPixels, split.feedback.vegRibbonClippedPixels);

        }
        ImGui::NextColumn();
        {
            float start = 1.f;
            float end = 3000.f;
            float base = 3000.f / 1.f;
            float step = 1.07f;
            float m_SliceStep = pow(base, 1.0f / (400.f - 1.0f));
            float m_SliceZero = 1.f / m_SliceStep;
            // should really log some more memory useage and maybe GPU speed here
            float Z = m_SliceZero;
            float depth = m_SliceStep;
            for (int i = 0; i < 128; i++)
            {
                Z = start * pow(step, i);
                float ZZ = start * pow(step, i + 1) - Z;
                ImGui::Text("%4.2f,  %4.2f,   %d", Z, ZZ, plants_Root.feedback.numBlock_Z[i]);
            }
        }
    }
    ImGui::PopFont();
}



void terrainManager::onGuiRender_Right_Ecotope(Gui* _gui, Gui::Window& _window)
{
    mEcosystem.renderGUI(_gui);
    if (ImGui::BeginPopupContextWindow(false))
    {
        if (ImGui::Selectable("New Ecotope")) { mEcosystem.addEcotope(); }
        if (ImGui::Selectable("Load")) { mEcosystem.load(); }
        if (ImGui::Selectable("Save")) { mEcosystem.save(); }
        ImGui::EndPopup();
    }

    ImGui::NewLine();
    ImGui::NewLine();
    ImGui::Separator();
    ImGui::Text("%d plants, %d tex, %2.2f Mb", plants_Root.importPathVector.size(), _plantMaterial::static_materials_veg.textureVector.size(), _plantMaterial::static_materials_veg.texMb);

    // do billobard as well
    ImGui::NewLine();
    ImGui::Text("billboards %d", plants_Root.feedback.numBillboard);
    auto& profiler = Profiler::instance();
    auto event = profiler.getEvent("/onFrameUpdate/update/update_dirty/clip_lod_animate");
    billboardGpuTime = event->getGpuTimeAverage();
    ImGui::Text("gpu %1.3fms", billboardGpuTime);

    //ImGui::Text("pixels %d, %d", plants_Root.feedback.numPixClip, plants_Root.feedback.numPixPass);
    //ImGui::Text("INST %d, %d", plants_Root.feedback.numInstAdded, plants_Root.feedback.numInstAdded);
    ImGui::NewLine();
    float numTri = (float)plants_Root.feedback.numBlocks * VEG_BLOCK_SIZE * 2.f / 1000000.f;
    ImGui::Text("plants %d in, %d out ", plants_Root.feedback.numPlant, plants_Root.feedback.numFrustDiscard);
    ImGui::Text("%d, %d, %d, %d ", plants_Root.feedback.numLod[0], plants_Root.feedback.numLod[1], plants_Root.feedback.numLod[2], plants_Root.feedback.numLod[3]);
    ImGui::Text("%2.2f M tri, %d blocks", numTri, plants_Root.feedback.numBlocks);
    ImGui::Text("gpu %1.3fms", plants_Root.gputime);
    ImGui::Text("clipLOD %d inst", plants_Root.feedback.numInstanceAddedComputeClipLod);



    ImGui::NewLine();
    ImGui::NewLine();
    ImGui::Separator();
    ImGui::Text("T     %1.1fms", stream.terrainCacheTime);
    ImGui::Text("T tex %1.1fms", stream.terrainCacheJPHTime);
    ImGui::Text("I     %1.1fms", stream.imageCacheTime);
    ImGui::Text("I tex %1.1fms", stream.imageCacheJPHTime);
    ImGui::Text("I io  %1.1fms", stream.imageCacheIOTime);
}


void terrainManager::onGuiRender_Right_Terrafector(Gui* _gui, Gui::Window& _window)
{
    //terrafectors.renderGui(_gui);
    ImGui::NewLine();
    //roadMaterialCache::getInstance().renderGui(_gui);

    ImGui::Text("Stamps");
    ImGui::Text("%d - stamps", mRoadStampCollection.stamps.size());
    ImGui::Text("%d - materials", mRoadStampCollection.materialMap.size());


    if (ImGui::Button("Load - Previous"))
    {
        loadStamp();
    }
    if (ImGui::Button("Load"))
    {
        FileDialogFilterVec filters = { {"stamps"} };
        if (openFileDialog(filters, mRoadStampCollection.lastUsedFilename))
        {
            loadStamp();
            reset(true);
        }
    }
    if (ImGui::Button("Save"))
    {
        FileDialogFilterVec filters = { {"stamps"} };
        if (saveFileDialog(filters, mRoadStampCollection.lastUsedFilename))
        {
            saveStamp();
        }
    }
}



void terrainManager::onGuiRender_Right_Roads(Gui* _gui, Gui::Window& _window)
{
    auto& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Button] = ImVec4(0.03f, 0.03f, 0.3f, 0.5f);
    mRoadNetwork.renderGUI(_gui);


    //if (ImGui::Button("bake - EVO", ImVec2(W, 0))) { bake(false); }
    //if (ImGui::Button("bake - MAX", ImVec2(W, 0))) { bake(true); }

    if (ImGui::Button("bezier -> lod4")) { bezierRoadstoLOD(4); }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Forces update of road GPU branchData");

    if (ImGui::Checkbox("show baked", &bSplineAsTerrafector)) { reset(true); }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("'b'");
    ImGui::Checkbox("show road icons", &showRoadOverlay);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("'n'");
    ImGui::Checkbox("show road splines", &showRoadSpline);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("'m'");

    ImGui::NewLine();

    
    style.ButtonTextAlign = ImVec2(0.0, 0.5);
    if (ImGui::DragFloat("overlay", &gis_overlay.strenght, 0.01f, 0, 1, "%1.2f strength")) {
        terrainShader.Vars()["PerFrameCB"]["gisOverlayStrength"] = gis_overlay.strenght;
    }

    if (ImGui::DragFloat("redStrength", &gis_overlay.redStrength, 0.01f, 0, 1)) {
        terrainShader.Vars()["PerFrameCB"]["redStrength"] = gis_overlay.redStrength;
    }
    if (ImGui::DragFloat("redScale", &gis_overlay.redScale, 0.01f, 0, 100)) {
        terrainShader.Vars()["PerFrameCB"]["redScale"] = gis_overlay.redScale;
    }
    if (ImGui::DragFloat("redOffset", &gis_overlay.redOffset, 0.01f, 0, 1)) {
        terrainShader.Vars()["PerFrameCB"]["redOffset"] = gis_overlay.redOffset;
    }

    if (ImGui::DragFloat("overlay Strength", &gis_overlay.terrafectorOverlayStrength, 0.01f, 0, 1)) {
        reset(true);
    }
    if (ImGui::DragFloat("roads alpha", &gis_overlay.splineOverlayStrength, 0.01f, 0, 1));

    ImGui::Checkbox("bakeBakeOnlyData", &gis_overlay.bakeBakeOnlyData);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Disabeling this will produce a WRONG result\nIt is only here for speed testing as it mimics EVO pipeline");





    ImGui::Separator();
    float W = ImGui::GetWindowWidth() - 5;
    ImGui::DragInt("# of splits", &bake.roadMaxSplits, 1, 8, 128);
    if (ImGui::Button("roads -> fbx", ImVec2(W, 0))) { mRoadNetwork.exportRoads(bake.roadMaxSplits); }

    if (ImGui::Button("bridges -> fbx", ImVec2(W, 0))) { mRoadNetwork.exportBridges(); }
    ImGui::DragInt2("lod", &exportLodMin, 1, 0, 20);
    //ImGui::DragInt("lod-MAX", &exportLodMax, 1, 0, 20);
    if (exportLodMax < exportLodMin) exportLodMax = exportLodMin;
    if (ImGui::Button("grab frame to fbx", ImVec2(W, 0))) { sceneToMax(); }


    if (ImGui::Button("roads/materials -> EVO", ImVec2(W, 0))) {


        //mRoadNetwork.exportBinary();
        terrafectors.exportMaterialBinary(settings.dirRoot + "/bake", lastfile.EVO + "/");


        char command[512];
        sprintf(command, "attrib -r %s/Terrafectors/TextureList.gpu", (settings.dirExport).c_str());
        std::string sCmd = command;
        replaceAllterrain(sCmd, "/", "\\");
        system(sCmd.c_str());

        sprintf(command, "%s/bake/TextureList.gpu %s/Terrafectors", settings.dirRoot.c_str(), (settings.dirExport).c_str());
        sCmd = command;
        replaceAllterrain(sCmd, "/", "\\");
        sCmd = "copy / Y " + sCmd;
        system(sCmd.c_str());

        sprintf(command, "attrib -r %s/Terrafectors/Materials.gpu", (settings.dirExport).c_str());
        sCmd = command;
        replaceAllterrain(sCmd, "/", "\\");
        system(sCmd.c_str());

        sprintf(command, "%s/bake/Materials.gpu %s/Terrafectors", settings.dirRoot.c_str(), (settings.dirExport).c_str());
        sCmd = command;
        replaceAllterrain(sCmd, "/", "\\");
        sCmd = "copy / Y " + sCmd;
        system(sCmd.c_str());

        // terrafectors and roads as well, might become one file in future
        sprintf(command, "attrib -r %s/Terrafectors/terrafector*", (settings.dirExport).c_str());
        sCmd = command;
        replaceAllterrain(sCmd, "/", "\\");
        system(sCmd.c_str());

        sprintf(command, "%s/bake/terrafector* %s/Terrafectors", settings.dirRoot.c_str(), (settings.dirExport).c_str());
        sCmd = command;
        replaceAllterrain(sCmd, "/", "\\");
        sCmd = "copy / Y " + sCmd;
        system(sCmd.c_str());


        sprintf(command, "attrib -r %s/Terrafectors/roadbezier*", (settings.dirExport).c_str());
        sCmd = command;
        replaceAllterrain(sCmd, "/", "\\");
        system(sCmd.c_str());

        sprintf(command, "%s/bake/roadbezier* %s/Terrafectors", settings.dirRoot.c_str(), (settings.dirExport).c_str());
        sCmd = command;
        replaceAllterrain(sCmd, "/", "\\");
        sCmd = "copy / Y " + sCmd;
        system(sCmd.c_str());
    }


    ImGui::DragFloat("jp2 quality", &bake.quality, 0.0001f, 0.0001f, 0.01f, "%3.5f");
    if (ImGui::Button("bake - EVO", ImVec2(W, 0))) { bake_start(false); }
    if (ImGui::Button("bake - fakeVeg", ImVec2(W, 0))) { bake_start(true); }


    ImGui::NewLine();
    ImGui::Separator();
    ImGui::Text("profile");
    auto& profiler = Profiler::instance();
    {
        auto event = profiler.getEvent("/onFrameUpdate/update/update_dirty");
        float gputime = event->getGpuTimeAverage();
        ImGui::Text("update dirty %2.3fms", gputime);
    }
    {
        auto event = profiler.getEvent("/onFrameRender/terrainManager");
        float gputime = event->getGpuTimeAverage();
        ImGui::Text("render terrain %2.3fms", gputime);
    }
    {
        auto event = profiler.getEvent("/onFrameRender/billboards");
        float gputime = event->getGpuTimeAverage();
        ImGui::Text("render quads %2.3fms", gputime);
    }
    {
        auto event = profiler.getEvent("/onFrameRender/vegetation");
        float gputime = event->getGpuTimeAverage();
        ImGui::Text("render plants %2.3fms", gputime);
        // do lod plants as well
    }


    ImGui::NewLine();
    ImGui::Separator();
    ImGui::Text("Artificial Intelligence");


    ImGui::Checkbox("AI mode", &mRoadNetwork.AI_path_mode);
    if (mRoadNetwork.AI_path_mode) {
        if (ImGui::Button("clear Path")) { mRoadNetwork.ai_clearPath(); }
        ImGui::Separator();
        if (ImGui::Button("currentRoad to AI")) {
            mRoadNetwork.currentRoadtoAI();
            updateDynamicRoad(true);
        }
        if (ImGui::Button("export AI")) { mRoadNetwork.exportAI(); }
        if (ImGui::Button("export CSV")) { mRoadNetwork.exportAI_CSV(); }
        if (ImGui::Button("load AI")) { mRoadNetwork.loadAI(); }
        ImGui::Text("start - %3.2f, %3.2f, %3.2f", mRoadNetwork.pathBezier.startpos.x, mRoadNetwork.pathBezier.startpos.y, mRoadNetwork.pathBezier.startpos.z);
        ImGui::Text("end - %3.2f, %3.2f, %3.2f", mRoadNetwork.pathBezier.endpos.x, mRoadNetwork.pathBezier.endpos.y, mRoadNetwork.pathBezier.endpos.z);
        for (uint i = 0; i < mRoadNetwork.pathBezier.roads.size(); i++) {
            ImGui::Text("%d, %d", mRoadNetwork.pathBezier.roads[i].roadIndex, (int)mRoadNetwork.pathBezier.roads[i].bForward);
        }

        ImGui::DragInt("kevRepeats", &mRoadNetwork.pathBezier.kevRepeats, 1, 1, 200);
        ImGui::DragFloat("kevOutScale", &mRoadNetwork.pathBezier.kevOutScale, 1, 0, 200);
        ImGui::DragFloat("kevInScale", &mRoadNetwork.pathBezier.kevInScale, 1, 0, 200);
        ImGui::DragInt("kevSmooth", &mRoadNetwork.pathBezier.kevSmooth, 1, 0, 100);
        ImGui::DragInt("kevAvsCnt", &mRoadNetwork.pathBezier.kevAvsCnt, 1, 0, 10);
        ImGui::DragInt("kecMaxCnt", &mRoadNetwork.pathBezier.kecMaxCnt, 1, 0, 20);
        ImGui::DragFloat("kevSampleMinSpacing", &mRoadNetwork.pathBezier.kevSampleMinSpacing, 1, 1, 20);

    }
}

void terrainManager::onGuiRender_Right(Gui* _gui, int _header, float2 _screenSize)
{
    auto& oldStyle = ImGui::GetStyle();
    auto& style = ImGui::GetStyle();

    Gui::Window rightPanel(_gui, "##rightPanel", true, { 200, 200 }, { 100, 100 }, Falcor::Gui::WindowFlags::NoResize);
    rightPanel.windowPos((int)_screenSize.x - 300, _header);
    rightPanel.windowSize(300, (int)_screenSize.y - _header);
    {

        ImGui::PushFont(_gui->getFont("header1"));
        ImGui::PushItemWidth(ImGui::GetWindowWidth() - 5);
        if (ImGui::Combo("###modeSelector", (int*)&terrainMode, "Vegetation\0Ecotope\0Terrafector\0Road network\0Glider Builder\0Terrain Builder\0Texture Tool\0")) { ; }
        ImGui::PopItemWidth();
        ImGui::PopFont();

        ImGui::PushFont(_gui->getFont("default"));
        ImGui::NewLine();

        switch (terrainMode)
        {
        case _terrainMode::vegetation:            plants_Root.renderGui_rightPanel(_gui);            break;
        case _terrainMode::ecotope:               onGuiRender_Right_Ecotope(_gui, rightPanel);            break;
        case _terrainMode::terrafector:           onGuiRender_Right_Terrafector(_gui, rightPanel);            break;
        case _terrainMode::roads:                 onGuiRender_Right_Roads(_gui, rightPanel);            break;
        case _terrainMode::glider:                onGuiRender_Right_Glider(_gui, rightPanel);            break;
        case _terrainMode::terrainBuilder:        newTerrainBuilder.renderGui_rightPanel(_gui);      break;

        }
        ImGui::PopFont();
        style = oldStyle;   // reset it
    }
    rightPanel.release();
}



void terrainManager::onGuiRender_Main_Ecotope(Gui* _gui)
{
    Gui::Window ecotopePanel(_gui, "Ecotope##tfPanel", { 900, 900 }, { 100, 100 });
    {
        mEcosystem.renderSelectedGUI(_gui);
    }
    ecotopePanel.release();
}


void terrainManager::onGuiRender_Main_Terrafector(Gui* _gui)
{
    Gui::Window terrafectorMaterialPanel_2(_gui, "Terrafector materials##2", { 900, 900 }, { 100, 100 });
    {
        terrafectorEditorMaterial::static_materials.renderGui(_gui, terrafectorMaterialPanel_2);
    }
    terrafectorMaterialPanel_2.release();
}


void terrainManager::onGuiRender_Main_Roads(Gui* _gui)
{
    Gui::Window tfPanel(_gui, "Material##tfPanel", { 900, 900 }, { 100, 100 });
    {
        if (terrafectorEditorMaterial::static_materials.renderGuiSelect(_gui))                reset(true);
    }
    tfPanel.release();

    Gui::Window rmPanel(_gui, "Road mat##tfPanel", { 900, 900 }, { 100, 100 });
    {
        if (roadMaterialCache::getInstance().renderGuiSelect(_gui, rmPanel))
        {
            mRoadNetwork.updateAllRoads();
            reset(true);
        }
    }
    rmPanel.release();

    Gui::Window terrafectorMaterialPanel(_gui, "Terrafector materials", { 900, 900 }, { 100, 100 });
    {
        terrafectorEditorMaterial::static_materials.renderGui(_gui, terrafectorMaterialPanel);
    }
    terrafectorMaterialPanel.release();

    Gui::Window roadMaterialPanel(_gui, "Road materials", { 900, 900 }, { 100, 100 });
    {
        roadMaterialCache::getInstance().renderGui(_gui, roadMaterialPanel);
    }
    roadMaterialPanel.release();

    Gui::Window terrafectorPanel(_gui, "Terrafectors", { 900, 900 }, { 100, 100 });
    {
        terrafectors.renderGui(_gui, terrafectorPanel);
    }
    terrafectorPanel.release();

    Gui::Window texturePanel(_gui, "Textures", { 900, 900 }, { 100, 100 });
    {
        terrafectorEditorMaterial::static_materials.renderGuiTextures(_gui, texturePanel);
        //terrafectors.renderGui(_gui, terrafectorPanel);
    }
    texturePanel.release();



    if (mRoadNetwork.currentIntersection || mRoadNetwork.currentRoad)
    {
        Gui::Window roadPanel(_gui, "Road##roadPanel", { 200, 200 }, { 100, 100 });
        {
            //ImGui::PushFont(_gui->getFont("roboto_20"));
            static bool fullWidth = false;
            roadPanel.windowSize(180 + fullWidth * 460, 0);

            if (mRoadNetwork.currentIntersection) {
                fullWidth = mRoadNetwork.renderpopupGUI(_gui, mRoadNetwork.currentIntersection);

                if (mRoadNetwork.currentRoad && (splineTest.bVertex || splineTest.bSegment)) {
                    ImGui::SetCursorPosY(300);
                    mRoadNetwork.renderpopupGUI(_gui, mRoadNetwork.intersectionSelectedRoad, splineTest.index);
                }
            }

            if (mRoadNetwork.currentRoad) {
                static uint _from = 0;
                static uint _to = 0;
                fullWidth = mRoadNetwork.renderpopupGUI(_gui, mRoadNetwork.currentRoad, splineTest.index);
                if ((_from != mRoadNetwork.selectFrom) || (_to != mRoadNetwork.selectTo)) {
                    updateDynamicRoad(true);
                }
            }

            //ImGui::PopFont();
        }
        roadPanel.release();
    }
}


void terrainManager::onGuiRender_Main_Glider(Gui* _gui)
{
}



void terrainManager::onGuiRender_Main(Gui* _gui, int _header, float2 _screenSize)
{
    switch (terrainMode)
    {
    case _terrainMode::vegetation:          plants_Root.renderGui(_gui, _header, _screenSize);      break;
    case _terrainMode::ecotope:             onGuiRender_Main_Ecotope(_gui);                         break;
    case _terrainMode::terrafector:         onGuiRender_Main_Terrafector(_gui);                     break;
    case _terrainMode::roads:               onGuiRender_Main_Roads(_gui);                           break;
    case _terrainMode::terrainBuilder:      newTerrainBuilder.onGuiRender(_gui, _header, _screenSize);                    break;
    }
}



void terrainManager::onGuiRender(Gui* _gui, int _header, float2 _screen, fogAtmosphericParams* pAtmosphere)
{
    if (requestPopupSettings) {
        ImGui::OpenPopup("settings");
        requestPopupSettings = false;
    }
    if (ImGui::BeginPopup("settings"))      // modal
    {
        settings.renderGui(_gui);
        ImGui::EndPopup();
    }
    if (requestPopupDebug) {
        ImGui::OpenPopup("debug");
        requestPopupDebug = false;
    }
    if (ImGui::BeginPopup("debug"))      // modal
    {
        ImGui::Text("%d", m_used.size());
        ImGui::EndPopup();
    }





    onGuiRender_Right(_gui, _header, _screen);

    onGuiRender_Main(_gui, _header, _screen - float2(300, _header));    // FIXME reight panel currently hardcodedat 300 pixels BAD

    if (debug)
    {
        Gui::Window debuginfo(_gui, "##debuginfo", { 200, 200 }, { 100, 100 });
        {
            onGuiRender_Debug(_gui);
        }
        debuginfo.release();
    }






    

    if (terrainMode != _terrainMode::vegetation)
    {
        if (true /*!ImGui::IsAnyWindowHovered()*/)
        {
            if (splineTest.bVertex)
            {
                std::stringstream tooltip;

                if (mRoadNetwork.currentRoad)
                {
                    tooltip << "camber   " << (int)(mRoadNetwork.currentRoad->points[splineTest.index].camber * 57.2958f) << "º     <-   ->\n";
                    tooltip << "T   " << mRoadNetwork.currentRoad->points[splineTest.index].T << "      <-   ->\n";
                    tooltip << "C   " << mRoadNetwork.currentRoad->points[splineTest.index].C << "      <-   ->\n";
                    tooltip << "B   " << mRoadNetwork.currentRoad->points[splineTest.index].B << "      ctrl + left mouse + drag\n";

                    ImGui::PushFont(_gui->getFont("roboto_26"));
                    ImGui::SetTooltip(tooltip.str().c_str());
                    ImGui::PopFont();
                }
            }
            else
            {
                char TTTEXT[1024];
                uint idx = split.feedback.tum_idx;
                sprintf(TTTEXT, "%s\n(%3.1f, %3.1f, %3.1f)\nlodyx %d %d %d\n%3.1fm", blockFromPositionB(split.feedback.tum_Position).c_str(),
                    split.feedback.tum_Position.x, split.feedback.tum_Position.y, split.feedback.tum_Position.z,
                    m_tiles[idx].lod, m_tiles[idx].y, m_tiles[idx].x, glm::length(split.feedback.tum_Position - this->cameraOrigin)
                );

                if (terrainMode == _terrainMode::terrafector)
                {
                    sprintf(TTTEXT, "(%3.1f, %3.1f, %3.1f)\nheight %2.2fm\nscale %2.2f %2.2f",
                        split.feedback.tum_Position.x, split.feedback.tum_Position.y, split.feedback.tum_Position.z,
                        mCurrentStamp.height, mCurrentStamp.scale.x, mCurrentStamp.scale.y);
                }
                //sprintf(TTTEXT, "splinetest %d (%d, %d) \n", splineTest.index, splineTest.bVertex, splineTest.bSegment);
                auto& style = ImGui::GetStyle();
                style.Colors[ImGuiCol_Text] = ImVec4(0.50f, 0.5, 0.5, 1.f);
                style.Colors[ImGuiCol_PopupBg] = ImVec4(0.00f, 0.f, 0.f, 0.8f);

                ImGui::PushFont(_gui->getFont("roboto_26"));
                ImGui::SetTooltip(TTTEXT);
                ImGui::PopFont();
            }
        }
    }

}


// Fixme input files to a list on disk
void terrainManager::writeGdal(jp2Map _map, std::ofstream& _gdal, std::string _input)
{
    std::string imageName = "img_" + std::to_string(_map.lod) + "_" + std::to_string(_map.y) + "_" + std::to_string(_map.x);
    //_gdal << "gdalwarp -t_srs \"+proj=tmerc +lat_0=47.27 +lon_0=9.07 +k_0=1 +x_0=0 +y_0=0 +ellps=GRS80 +units=m\" -te ";
    _gdal << "gdalwarp -t_srs " << settings.projection;
    _gdal << " -te " << _map.origin.x << " " << (-1 * _map.origin.y) - _map.size << " " << _map.origin.x + _map.size << " " << (-1 * _map.origin.y) << " ";
    _gdal << "-ts 1024 1024 -r cubicspline -multi -overwrite -ot byte ";
    _gdal << _input << " ";
    _gdal << "../_temp/" << imageName << ".tif \n";
    _gdal << "gdal_translate -ot byte ../_temp/" << imageName << ".tif ../_temp/" << imageName << ".bil \n\n";
}

void terrainManager::writeGdalClear(std::ofstream& _gdal)
{
    _gdal << "\n\n";
    _gdal << "del \"../_temp/*.tif\"\n";
    _gdal << "del \"../_temp/*.hdr\"\n";
    _gdal << "del \"../_temp/*.prj\"\n";
    _gdal << "del \"../_temp/*.xml\"\n";
    _gdal << "\n\n";
}


void jp2Map::set(uint _lod, uint _y, uint _x, float _wSize, float _wOffset)
{
    lod = _lod;
    y = _y;
    x = _x;

    float scale = 1.f / (float)pow(2, lod);
    float sizeT = _wSize * scale;
    float sizeTotal = sizeT * tile_toBorder;
    float sizeBorder = (sizeTotal - sizeT) / 2.f;

    size = sizeTotal;
    origin.x = _wOffset - sizeBorder + (x * sizeT);
    origin.y = _wOffset - sizeBorder + (y * sizeT);
}


void jp2Map::save(std::ofstream& _os)
{
    _os << lod << " " << y << " " << x << " " << origin.x << " " << origin.y << " " << size << " ";
    _os << hgt_offset << " " << hgt_scale << " " << fileOffset << "\n";
}

void jp2Map::saveBinary(std::ofstream& _os)
{
    _os << lod << y << x << origin.x << origin.y << size << hgt_offset << hgt_scale << fileOffset;
}

void jp2Map::loadBinary(std::ifstream& _is)
{
    _is >> lod >> y >> x >> origin.x >> origin.y >> size >> hgt_offset >> hgt_scale >> fileOffset;
}




void jp2File::save(std::ofstream& _os)
{
    _os << filename << "\n";
    for (auto& T : tiles)
    {
        T.save(_os);
    }
}

void jp2File::saveBinary(std::ofstream& _os)
{
    _os << filename << "\n";
    uint numTiles = tiles.size();
    _os << numTiles;
    for (auto& T : tiles)
    {
        T.saveBinary(_os);
    }
}

void jp2File::loadBinary(std::ifstream& _is)
{
    uint numTiles;
    _is >> filename;
    _is >> numTiles;
    for (int i = 0; i < numTiles; i++)
    {
        tiles.emplace_back();
        tiles.back().loadBinary(_is);
    }
}





void jp2Dir::save(std::string _name)
{
    std::ofstream os(_name.c_str());
    if (os.good()) {
        cereal::JSONOutputArchive archive(os);
        serialize(archive);
        /*
        for (auto F : files)
        {
            F.save(os);
        }*/
    }
}

void jp2Dir::load(std::string _name)
{
    std::ifstream is(_name.c_str());
    if (is.good()) {
        cereal::JSONInputArchive archive(is);
        serialize(archive);
    }

    fileHashmap.clear();
    tileHash.clear();
    for (int i = 0; i < files.size(); i++)
    {
        fileHashmap[files[i].hash] = i;


        //if (i == 0)// TMEP totdat ek .bin strem en cache
        {
            for (int j = 0; j < files[i].tiles.size(); j++)
            {
                uint32_t hash = getHashFromTileCoords(files[i].tiles[j].lod, files[i].tiles[j].y, files[i].tiles[j].x);
                tileHash[hash] = uint2(i, j);
            }
        }
    }

    cache.resize(40);   // can add up if I get this wrong
}


//#pragma optimize("", off)
void jp2Dir::cache0(std::string _path)
{
    LOG_LINE(1, "jp2Dir::cache0");
    if (files.size() > 0)
    {
        path = _path;   // save for rest of session

        dataRoot.clear();
        dataRoot.resize(files[0].sizeInBytes);
        std::ifstream is(_path + files[0].filename, std::ios::binary);
        if (is.good()) {
            is.read((char*)dataRoot.data(), files[0].sizeInBytes);
            is.close();
        }
    }
    else
    {

    }
    /*
    std::vector<unsigned char> data(files[0].sizeInBytes);
    std::ifstream is(files[0].filename, std::ios::binary);
    if (is.good()) {
        is.read((char *)data.data(), files[0].sizeInBytes);
        auto sharedPtr = std::make_shared<std::vector<uint8_t>>(data);
        cache.set(files[0].hash, sharedPtr);
        is.close();
    }
    */
}

//std::thread t1(imageDirectory.cacheHash(), hash);
void jp2Dir::cacheHash(uint32_t hash)
{
    LOG_LINE(1, "jp2Dir::cacheHash");
    if (hash == 0) return;  // because that is all pre loaded

    std::map<uint32_t, uint>::iterator file_it = fileHashmap.find(hash);
    if (file_it != fileHashmap.end())
    {
        // such a file exists
        uint idx = file_it->second;
        std::shared_ptr<std::vector<unsigned char>> data;
        if (!cache.get(hash, data))
        {
            // but not cahched, so cache it
            std::vector<unsigned char> dataRoot;
            dataRoot.resize(files[idx].sizeInBytes);
            std::ifstream is(path + files[idx].filename, std::ios::binary);
            if (is.good()) {
                is.read((char*)dataRoot.data(), files[idx].sizeInBytes);
                is.close();
            }
            auto share = std::make_shared<std::vector<unsigned char>>(dataRoot);
            cache.set(hash, share);

            fprintf(terrafectorSystem::_logfile, "cacheHash()   %s   %s \n", path.c_str(), files[idx].filename.c_str());
        }
    }
}


void jp2Dir::saveBinary(std::string _name)
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

void jp2Dir::loadBinary(std::string _name)
{
    std::ifstream is(_name.c_str(), std::ios::binary);
    if (is.good()) {
        uint numFiles;
        is >> numFiles;
        for (int i = 0; i < numFiles; i++)
        {
            files.emplace_back();
            files.back().loadBinary(is);
        }
    }
}



void terrainManager::generateGdalPhotos()
{
    jp2Dir data;
    jp2Map _mapElement;
    jp2Dir jp2;
    std::ofstream of_gdal;
    std::string inLow = "2025250_Mosaik_RGB.tif 2025150_Mosaik_RGB.tif 2023450_Mosaik_RGB.tif 2023360_Mosaik_RGB.tif ";
    std::string inAll = "2025250_Mosaik_RGB.tif 2025150_Mosaik_RGB.tif 2023450_Mosaik_RGB.tif 2023360_Mosaik_RGB.tif ";

    std::ifstream ifLOD68map;
    unsigned char map[256][256];

    ifLOD68map.open("E:/terrains/switserland_Steg/gis/photos/LOD_6_8.raw", std::ios::binary);
    if (ifLOD68map)
    {
        ifLOD68map.read((char*)&map[0][0], 256 * 256);
        ifLOD68map.close();
    }

    of_gdal.open("F:/terrains_gis/austria_Wechsel/photos/gdal_photos_NEW.bat");
    if (of_gdal)
    {
        jp2.files.emplace_back();
        jp2.files.back().filename = "0_0_0.bin";
        jp2.files.back().hash = getHashFromTileCoords(0, 0, 0);

        _mapElement.set(0, 0, 0);
        jp2.files.back().tiles.push_back(_mapElement);
        writeGdal(_mapElement, of_gdal, inLow);

        // 9.6m
        for (uint y = 0; y < 4; y++) {
            for (uint x = 0; x < 4; x++) {
                _mapElement.set(2, y, x);
                jp2.files.back().tiles.push_back(_mapElement);
                writeGdal(_mapElement, of_gdal, inLow);
            }
        }

        // 2.4m
        for (uint y = 0; y < 16; y++) {
            for (uint x = 0; x < 16; x++) {
                _mapElement.set(4, y, x);
                jp2.files.back().tiles.push_back(_mapElement);
                writeGdal(_mapElement, of_gdal, inLow);
            }
        }

        /*
        // 61cm - top
        for (uint y = 8; y < 24; y++) {
            for (uint x = 8; x < 56; x++) {
                _mapElement.set(6, y, x);
                jp2.files.back().tiles.push_back(_mapElement);
                writeGdal(_mapElement, of_gdal, inLow);
            }
        }

        // 61cm - wechsel
        for (uint y = 36; y < 52; y++) {
            for (uint x = 12; x < 32; x++) {
                _mapElement.set(6, y, x);
                jp2.files.back().tiles.push_back(_mapElement);
                writeGdal(_mapElement, of_gdal, inLow);
            }
        }
        */
        writeGdalClear(of_gdal);



        std::vector<jp2Map> block_files;
        /*
        // top 61cm
        for (uint ty = 1; ty < 6; ty++)
        {
            for (uint tx = 2; tx < 14; tx++)
            {
                block_files.clear();

                for (uint y = ty * 4; y < ty * 4 + 4; y++) {
                    for (uint x = tx * 4; x < tx * 4 + 4; x++) {
                        {
                            _mapElement.set(6, y, x); // 61 cm
                            block_files.push_back(_mapElement);
                            writeGdal(_mapElement, of_gdal, inAll);
                        }
                    }
                }
            }
        }

        // wechsel 61cm
        for (uint ty = 9; ty < 13; ty++)
        {
            for (uint tx = 3; tx < 8; tx++)
            {
                block_files.clear();

                for (uint y = ty * 4; y < ty * 4 + 4; y++) {
                    for (uint x = tx * 4; x < tx * 4 + 4; x++) {
                        {
                            _mapElement.set(6, y, x); // 61 cm
                            block_files.push_back(_mapElement);
                            writeGdal(_mapElement, of_gdal, inAll);
                        }
                    }
                }
            }
        }


        // top 15cm
        for (uint ty = 2; ty < 4; ty++)
        {
            for (uint tx = 11; tx < 13; tx++)
            {
                block_files.clear();

                for (uint y = ty * 16; y < ty * 16 + 16; y++) {
                    for (uint x = tx * 16; x < tx * 16 + 16; x++) {
                        //if (map[y][x] > 200)
                        {
                            _mapElement.set(8, y, x);
                            block_files.push_back(_mapElement);
                            writeGdal(_mapElement, of_gdal, inAll);
                        }
                    }
                }
            }
        }

        // wechsel 15cm
        for (uint ty = 11; ty < 12; ty++)
        {
            for (uint tx = 5; tx < 7; tx++)
            {
                block_files.clear();

                for (uint y = ty * 16; y < ty * 16 + 16; y++) {
                    for (uint x = tx * 16; x < tx * 16 + 16; x++) {
                        if (map[y][x] > 200)
                        {
                            _mapElement.set(8, y, x);
                            block_files.push_back(_mapElement);
                            writeGdal(_mapElement, of_gdal, inAll);
                        }
                    }
                }
            }
        }
        */

        for (uint ty = 0; ty < 16; ty++)
        {
            for (uint tx = 0; tx < 16; tx++)
            {
                block_files.clear();

                for (uint y = ty * 4; y < ty * 4 + 4; y++) {
                    for (uint x = tx * 4; x < tx * 4 + 4; x++) {
                        //if (map[y * 4][x * 4] > 100)
                        if ((tx >= 2 && tx < 14 && ty >= 1 && ty < 6) ||
                            (tx >= 3 && tx < 8 && ty >= 9 && ty < 13))
                        {
                            _mapElement.set(6, y, x); // 61 cm
                            block_files.push_back(_mapElement);
                            writeGdal(_mapElement, of_gdal, inAll);
                        }
                    }
                }

                for (uint y = ty * 16; y < ty * 16 + 16; y++) {
                    for (uint x = tx * 16; x < tx * 16 + 16; x++) {
                        //if (map[y][x] > 200)
                        if ((tx >= 11 && tx < 13 && ty >= 2 && ty < 4) ||
                            (tx >= 11 && tx < 12 && ty >= 5 && ty < 7))
                        {
                            _mapElement.set(8, y, x);
                            block_files.push_back(_mapElement);
                            writeGdal(_mapElement, of_gdal, inAll);
                        }
                    }
                }

                // push if we have any
                if (block_files.size() > 0)
                {
                    jp2.files.emplace_back();
                    jp2.files.back().filename = "4_" + std::to_string(ty) + "_" + std::to_string(tx) + ".bin";
                    jp2.files.back().hash = getHashFromTileCoords(4, ty, tx);
                    for (auto& t : block_files) {
                        jp2.files.back().tiles.push_back(t);
                    }

                    writeGdalClear(of_gdal);
                }
            }
        }



        of_gdal.close();

        jp2.save("F:/terrains_gis/austria_Wechsel/photos/photo_tiles.json");
        //jp2.saveBinary("E:/terrains/switserland_Steg/gis/photos/photo_tiles.bin");

        //jp2Dir jp2Test;
        //jp2Test.loadBinary("E:/terrains/switserland_Steg/gis/photos/photo_tiles.bin");
        /*
        std::ofstream os("E:/terrains/switserland_Steg/gis/photos/photo_tiles.json");
        if (os.good()) {
            cereal::JSONOutputArchive archive(os);
            jp2.serialize(archive);
        }*/
    }

    return;


    float leftT, bottomT, rightT, topT, sizeT, sizeBrd, sizeTotal;
    int lod = 0;
    int x = 0;
    int y = 0;
    float sizeWorld = 40000;
    float offset = -20000;
    float scale;




    std::ofstream ofs, ofSummary;
    //ofs.open(settings.dirGis + "/photos/gdal_photos.bat");
    ofSummary.open("E:/terrains/switserland_Steg/gis/photos/gdal_photos_0_2_4.txt");
    ofs.open("E:/terrains/switserland_Steg/gis/photos/gdal_photos_0_2_4.bat");
    if (ofs)
    {
        jp2Map _mapElement;
        lod = 0;
        scale = 1.f / (float)pow(2, lod);
        sizeT = sizeWorld * scale;
        sizeTotal = sizeT * tile_toBorder;
        sizeBrd = (sizeTotal - sizeT) / 2.f;

        leftT = offset - sizeBrd;
        bottomT = offset - sizeBrd;
        rightT = leftT + sizeTotal;
        topT = bottomT + sizeTotal;

        std::string imageName = "img_" + std::to_string(lod) + "_" + std::to_string(y) + "_" + std::to_string(x);
        ofs << "gdalwarp -t_srs \"+proj=tmerc +lat_0=47.27 +lon_0=9.07 +k_0=1 +x_0=0 +y_0=0 +ellps=GRS80 +units=m\" -te ";
        ofs << leftT << " " << bottomT << " " << rightT << " " << topT << " ";
        ofs << "-ts 1024 1024 -r cubicspline -multi -overwrite -ot byte ";
        ofs << "Images_2m.tif ";
        //ofs << "Images_2m.tif A1.tif  A2.tif  A3.tif B1.tif  B2.tif  B3.tif ";
        //ofs << "C1.tif  C2.tif  C3.tif D1.tif  D2.tif  D3.tif ";
        ofs << "../_temp/" << imageName << ".tif \n";
        ofs << "gdal_translate -ot byte ../_temp/" << imageName << ".tif ../_temp/" << imageName << ".bil \n\n\n";
        ofs << "\n\n\n\n\n";

        ofSummary << lod << " " << y << " " << x << " 1024 " << leftT << " " << topT * -1 << " " << sizeTotal;
        ofSummary << " orthophoto/" << imageName << ".jp2\n";


        lod = 2;
        scale = 1.f / (float)pow(2, lod);
        sizeT = sizeWorld * scale;
        sizeTotal = sizeT * tile_toBorder;
        sizeBrd = (sizeTotal - sizeT) / 2.f;

        for (y = 0; y < 4; y++)
        {
            for (x = 0; x < 4; x++)
            {
                leftT = offset + x * sizeT - sizeBrd;
                bottomT = offset + (3 - y) * sizeT - sizeBrd;
                rightT = leftT + sizeTotal;
                topT = bottomT + sizeTotal;

                std::string imageName = "img_" + std::to_string(lod) + "_" + std::to_string(y) + "_" + std::to_string(x);
                ofs << "gdalwarp -t_srs \"+proj=tmerc +lat_0=47.27 +lon_0=9.07 +k_0=1 +x_0=0 +y_0=0 +ellps=GRS80 +units=m\" -te ";
                ofs << leftT << " " << bottomT << " " << rightT << " " << topT << " ";
                ofs << "-ts 1024 1024 -r cubicspline -multi -overwrite -ot byte ";
                ofs << "Images_2m.tif ";
                //ofs << "Images_2m.tif A1.tif  A2.tif  A3.tif B1.tif  B2.tif  B3.tif ";
                //ofs << "C1.tif  C2.tif  C3.tif D1.tif  D2.tif  D3.tif ";
                ofs << "../_temp/" << imageName << ".tif \n";
                ofs << "gdal_translate -ot byte ../_temp/" << imageName << ".tif ../_temp/" << imageName << ".bil \n\n\n";

                ofSummary << lod << " " << y << " " << x << " 1024 " << leftT << " " << topT * -1 << " " << sizeTotal;
                ofSummary << " orthophoto/" << imageName << ".jp2\n";
            }
        }


        lod = 4;
        scale = 1.f / (float)pow(2, lod);
        sizeT = sizeWorld * scale;
        sizeTotal = sizeT * tile_toBorder;
        sizeBrd = (sizeTotal - sizeT) / 2.f;

        for (y = 0; y < 16; y++)
        {
            for (x = 0; x < 16; x++)
            {
                leftT = offset + x * sizeT - sizeBrd;
                bottomT = offset + (15 - y) * sizeT - sizeBrd;
                rightT = leftT + sizeTotal;
                topT = bottomT + sizeTotal;

                std::string imageName = "img_" + std::to_string(lod) + "_" + std::to_string(y) + "_" + std::to_string(x);
                ofs << "gdalwarp -t_srs \"+proj=tmerc +lat_0=47.27 +lon_0=9.07 +k_0=1 +x_0=0 +y_0=0 +ellps=GRS80 +units=m\" -te ";
                ofs << leftT << " " << bottomT << " " << rightT << " " << topT << " ";
                ofs << "-ts 1024 1024 -r cubicspline -multi -overwrite -ot byte ";
                ofs << "Images_2m.tif ";
                //ofs << "Images_2m.tif A1.tif  A2.tif  A3.tif B1.tif  B2.tif  B3.tif ";
                //ofs << "C1.tif  C2.tif  C3.tif D1.tif  D2.tif  D3.tif ";
                ofs << "../_temp/" << imageName << ".tif \n";
                ofs << "gdal_translate -ot byte ../_temp/" << imageName << ".tif ../_temp/" << imageName << ".bil \n\n\n";

                ofSummary << lod << " " << y << " " << x << " 1024 " << leftT << " " << topT * -1 << " " << sizeTotal;
                ofSummary << " orthophoto/" << imageName << ".jp2\n";
            }
        }

        ofs.close();
        ofSummary.close();
    }


    //std::ifstream ifLOD68map;
    //unsigned char map[256][256];

    ifLOD68map.open("E:/terrains/switserland_Steg/gis/photos/LOD_6_8.raw", std::ios::binary);
    if (ifLOD68map)
    {
        ifLOD68map.read((char*)&map[0][0], 256 * 256);
        ifLOD68map.close();

        // Step through at LOD 4
        for (uint ty = 0; ty < 16; ty++)
        {
            for (uint tx = 0; tx < 16; tx++)
            {
                std::string name = "E:/terrains/switserland_Steg/gis/photos/gdal_photos_lod4_";
                name += std::to_string(ty) + "_" + std::to_string(tx) + ".txt";
                ofSummary.open(name.c_str());
                name += ".bat";
                ofs.open(name.c_str());
                if (ofs)
                {
                    lod = 6;
                    scale = 1.f / (float)pow(2, lod);
                    sizeT = sizeWorld * scale;
                    sizeTotal = sizeT * tile_toBorder;
                    sizeBrd = (sizeTotal - sizeT) / 2.f;

                    for (y = ty * 4; y < ty * 4 + 4; y++)
                    {
                        for (x = tx * 4; x < tx * 4 + 4; x++)
                        {
                            if (map[y * 4][x * 4] > 100)
                            {
                                leftT = offset + x * sizeT - sizeBrd;
                                bottomT = offset + (63 - y) * sizeT - sizeBrd;
                                rightT = leftT + sizeTotal;
                                topT = bottomT + sizeTotal;

                                std::string imageName = "img_" + std::to_string(lod) + "_" + std::to_string(y) + "_" + std::to_string(x);
                                ofs << "gdalwarp -t_srs \"+proj=tmerc +lat_0=47.27 +lon_0=9.07 +k_0=1 +x_0=0 +y_0=0 +ellps=GRS80 +units=m\" -te ";
                                ofs << leftT << " " << bottomT << " " << rightT << " " << topT << " ";
                                ofs << "-ts 1024 1024 -r cubicspline -multi -overwrite -ot byte ";
                                ofs << "Images_2m.tif ";
                                ofs << "A1.tif  A2.tif  A3.tif B1.tif  B2.tif  B3.tif ";
                                ofs << "C1.tif  C2.tif  C3.tif D1.tif  D2.tif  D3.tif ";
                                ofs << "../_temp/" << imageName << ".tif \n";
                                ofs << "gdal_translate -ot byte ../_temp/" << imageName << ".tif ../_temp/" << imageName << ".bil \n\n\n";


                                ofSummary << lod << " " << y << " " << x << " 1024 " << leftT << " " << topT * -1 << " " << sizeTotal;
                                ofSummary << " orthophoto/" << imageName << ".jp2\n";
                            }
                        }
                    }




                    lod = 8;
                    scale = 1.f / (float)pow(2, lod);
                    sizeT = sizeWorld * scale;
                    sizeTotal = sizeT * tile_toBorder;
                    sizeBrd = (sizeTotal - sizeT) / 2.f;

                    for (y = ty * 16; y < ty * 16 + 16; y++)
                    {
                        for (x = tx * 16; x < tx * 16 + 16; x++)
                        {
                            if (map[y][x] > 200)
                            {
                                leftT = offset + x * sizeT - sizeBrd;
                                bottomT = offset + (255 - y) * sizeT - sizeBrd;
                                rightT = leftT + sizeTotal;
                                topT = bottomT + sizeTotal;

                                std::string imageName = "img_" + std::to_string(lod) + "_" + std::to_string(y) + "_" + std::to_string(x);
                                ofs << "gdalwarp -t_srs \"+proj=tmerc +lat_0=47.27 +lon_0=9.07 +k_0=1 +x_0=0 +y_0=0 +ellps=GRS80 +units=m\" -te ";
                                ofs << leftT << " " << bottomT << " " << rightT << " " << topT << " ";
                                ofs << "-ts 1024 1024 -r cubicspline -multi -overwrite -ot byte ";
                                ofs << "Images_2m.tif ";
                                ofs << "A1.tif  A2.tif  A3.tif B1.tif  B2.tif  B3.tif ";
                                ofs << "C1.tif  C2.tif  C3.tif D1.tif  D2.tif  D3.tif ";
                                ofs << "../_temp/" << imageName << ".tif \n";
                                ofs << "gdal_translate -ot byte ../_temp/" << imageName << ".tif ../_temp/" << imageName << ".bil \n\n\n";

                                ofSummary << lod << " " << y << " " << x << " 1024 " << leftT << " " << topT * -1 << " " << sizeTotal;
                                ofSummary << " orthophoto/" << imageName << ".jp2\n";
                            }
                        }
                    }


                    ofSummary.close();
                    ofs.close();
                }
            }
        }
    }
    /*

        // steg 6 y 12-20  x 12-24
        // steg 8 y 32-40  x 29-33

        // walensee 6  y 48-64 x 20-64
        // walensee 8  y 112-120  x 120-128
        ofs.open("E:/terrains/switserland_Steg/gis/photos/gdal_photos_lod_steg.bat");
        if (ofs)
        {
            lod = 6;
            scale = 1.f / (float)pow(2, lod);
            sizeT = sizeWorld * scale;
            sizeTotal = sizeT * tile_toBorder;
            sizeBrd = (sizeTotal - sizeT) / 2.f;

            for (y = 12; y < 20; y++)
            {
                for (x = 12; x < 24; x++)
                {
                    leftT = offset + x * sizeT - sizeBrd;
                    bottomT = offset + (63 - y) * sizeT - sizeBrd;
                    rightT = leftT + sizeTotal;
                    topT = bottomT + sizeTotal;

                    std::string imageName = "img_" + std::to_string(lod) + "_" + std::to_string(y) + "_" + std::to_string(x);
                    ofs << "gdalwarp -t_srs \"+proj=tmerc +lat_0=47.27 +lon_0=9.07 +k_0=1 +x_0=0 +y_0=0 +ellps=GRS80 +units=m\" -te ";
                    ofs << leftT << " " << bottomT << " " << rightT << " " << topT << " ";
                    ofs << "-ts 1024 1024 -r cubicspline -multi -overwrite -ot byte ";
                    ofs << "Images_2m.tif ";
                    ofs << "A1.tif  A2.tif  A3.tif B1.tif  B2.tif  B3.tif ";
                    ofs << "C1.tif  C2.tif  C3.tif D1.tif  D2.tif  D3.tif ";
                    ofs << "../_temp/" << imageName << ".tif \n";
                    ofs << "gdal_translate -ot byte ../_temp/" << imageName << ".tif ../_temp/" << imageName << ".bil \n\n\n";


                }
            }

            // steg 8 y 32-40  x 29-33  // Fre
            lod = 8;
            scale = 1.f / (float)pow(2, lod);
            sizeT = sizeWorld * scale;
            sizeTotal = sizeT * tile_toBorder;
            sizeBrd = (sizeTotal - sizeT) / 2.f;

            for (y = 64; y < 80; y++)
            {
                for (x = 58; x < 66; x++)
                {
                    leftT = offset + x * sizeT - sizeBrd;
                    bottomT = offset + (255 - y) * sizeT - sizeBrd;
                    rightT = leftT + sizeTotal;
                    topT = bottomT + sizeTotal;

                    std::string imageName = "img_" + std::to_string(lod) + "_" + std::to_string(y) + "_" + std::to_string(x);
                    ofs << "gdalwarp -t_srs \"+proj=tmerc +lat_0=47.27 +lon_0=9.07 +k_0=1 +x_0=0 +y_0=0 +ellps=GRS80 +units=m\" -te ";
                    ofs << leftT << " " << bottomT << " " << rightT << " " << topT << " ";
                    ofs << "-ts 1024 1024 -r cubicspline -multi -overwrite -ot byte ";
                    ofs << "Images_2m.tif ";
                    ofs << "A1.tif  A2.tif  A3.tif B1.tif  B2.tif  B3.tif ";
                    ofs << "C1.tif  C2.tif  C3.tif D1.tif  D2.tif  D3.tif ";
                    ofs << "../_temp/" << imageName << ".tif \n";
                    ofs << "gdal_translate -ot byte ../_temp/" << imageName << ".tif ../_temp/" << imageName << ".bil \n\n\n";


                }
            }
            ofs.close();

        }


        // walensee 6  y 48-64 x 20-64
        // walensee 8  y 112-120  x 120-128
        ofs.open("E:/terrains/switserland_Steg/gis/photos/gdal_photos_lod_walensee.bat");
        if (ofs)
        {
            lod = 6;
            scale = 1.f / (float)pow(2, lod);
            sizeT = sizeWorld * scale;
            sizeTotal = sizeT * tile_toBorder;
            sizeBrd = (sizeTotal - sizeT) / 2.f;

            for (y = 48; y < 64; y++)
            {
                for (x = 20; x < 64; x++)
                {
                    leftT = offset + x * sizeT - sizeBrd;
                    bottomT = offset + (63 - y) * sizeT - sizeBrd;
                    rightT = leftT + sizeTotal;
                    topT = bottomT + sizeTotal;

                    std::string imageName = "img_" + std::to_string(lod) + "_" + std::to_string(y) + "_" + std::to_string(x);
                    ofs << "gdalwarp -t_srs \"+proj=tmerc +lat_0=47.27 +lon_0=9.07 +k_0=1 +x_0=0 +y_0=0 +ellps=GRS80 +units=m\" -te ";
                    ofs << leftT << " " << bottomT << " " << rightT << " " << topT << " ";
                    ofs << "-ts 1024 1024 -r cubicspline -multi -overwrite -ot byte ";
                    ofs << "Images_2m.tif ";
                    ofs << "A1.tif  A2.tif  A3.tif B1.tif  B2.tif  B3.tif ";
                    ofs << "C1.tif  C2.tif  C3.tif D1.tif  D2.tif  D3.tif ";
                    ofs << "../_temp/" << imageName << ".tif \n";
                    ofs << "gdal_translate -ot byte ../_temp/" << imageName << ".tif ../_temp/" << imageName << ".bil \n\n\n";


                }
            }

            // walensee 8  y 112-120  x 120-128
            lod = 8;
            scale = 1.f / (float)pow(2, lod);
            sizeT = sizeWorld * scale;
            sizeTotal = sizeT * tile_toBorder;
            sizeBrd = (sizeTotal - sizeT) / 2.f;

            for (y = 224; y < 240; y++)
            {
                for (x = 240; x < 256; x++)
                {
                    leftT = offset + x * sizeT - sizeBrd;
                    bottomT = offset + (255 - y) * sizeT - sizeBrd;
                    rightT = leftT + sizeTotal;
                    topT = bottomT + sizeTotal;

                    std::string imageName = "img_" + std::to_string(lod) + "_" + std::to_string(y) + "_" + std::to_string(x);
                    ofs << "gdalwarp -t_srs \"+proj=tmerc +lat_0=47.27 +lon_0=9.07 +k_0=1 +x_0=0 +y_0=0 +ellps=GRS80 +units=m\" -te ";
                    ofs << leftT << " " << bottomT << " " << rightT << " " << topT << " ";
                    ofs << "-ts 1024 1024 -r cubicspline -multi -overwrite -ot byte ";
                    ofs << "Images_2m.tif ";
                    ofs << "A1.tif  A2.tif  A3.tif B1.tif  B2.tif  B3.tif ";
                    ofs << "C1.tif  C2.tif  C3.tif D1.tif  D2.tif  D3.tif ";
                    ofs << "../_temp/" << imageName << ".tif \n";
                    ofs << "gdal_translate -ot byte ../_temp/" << imageName << ".tif ../_temp/" << imageName << ".bil \n\n\n";


                }
            }
            ofs.close();
        }



        ofSummary.open("E:/terrains/switserland_Steg/gis/photos/gdal_photos_Summary_lod7.txt");
        {
            // steg 6 y 12-20  x 12-24
        // steg 8 y 32-40  x 29-33

        // walensee 6  y 48-64 x 20-64
        // walensee 8  y 112-120  x 120-128

            lod = 6;
            scale = 1.f / (float)pow(2, lod);
            sizeT = sizeWorld * scale;
            sizeTotal = sizeT * tile_toBorder;
            sizeBrd = (sizeTotal - sizeT) / 2.f;


            // Steg
            for (y = 12; y < 20; y++)
            {
                for (x = 12; x < 24; x++)
                {
                    leftT = offset + x * sizeT - sizeBrd;
                    bottomT = offset + (63 - y) * sizeT - sizeBrd;
                    rightT = leftT + sizeTotal;
                    topT = bottomT + sizeTotal;

                    std::string imageName = "img_" + std::to_string(lod) + "_" + std::to_string(y) + "_" + std::to_string(x);
                    ofSummary << lod << " " << y << " " << x << " 1024 " << leftT << " " << topT * -1 << " " << sizeTotal;
                    ofSummary << " orthophoto/" << imageName << ".jp2\n";
                }
            }


            // Walensee
            for (y = 48; y < 64; y++)
            {
                for (x = 20; x < 64; x++)
                {
                    leftT = offset + x * sizeT - sizeBrd;
                    bottomT = offset + (63 - y) * sizeT - sizeBrd;
                    rightT = leftT + sizeTotal;
                    topT = bottomT + sizeTotal;

                    std::string imageName = "img_" + std::to_string(lod) + "_" + std::to_string(y) + "_" + std::to_string(x);
                    ofSummary << lod << " " << y << " " << x << " 1024 " << leftT << " " << topT * -1 << " " << sizeTotal;
                    ofSummary << " orthophoto/" << imageName << ".jp2\n";
                }
            }

            // steg 6 y 12-20  x 12-24
       // steg 8 y 32-40  x 29-33

       // walensee 6  y 48-64 x 20-64
       // walensee 8  y 112-120  x 120-128
            lod = 8;
            scale = 1.f / (float)pow(2, lod);
            sizeT = sizeWorld * scale;
            sizeTotal = sizeT * tile_toBorder;
            sizeBrd = (sizeTotal - sizeT) / 2.f;


            // Steg
            for (y = 64; y < 80; y++)
            {
                for (x = 58; x < 66; x++)
                {
                    leftT = offset + x * sizeT - sizeBrd;
                    bottomT = offset + (255 - y) * sizeT - sizeBrd;
                    rightT = leftT + sizeTotal;
                    topT = bottomT + sizeTotal;

                    std::string imageName = "img_" + std::to_string(lod) + "_" + std::to_string(y) + "_" + std::to_string(x);
                    ofSummary << lod << " " << y << " " << x << " 1024 " << leftT << " " << topT * -1 << " " << sizeTotal;
                    ofSummary << " orthophoto/" << imageName << ".jp2\n";
                }
            }


            // Walensee
            for (y = 224; y < 240; y++)
            {
                for (x = 240; x < 256; x++)
                {
                    leftT = offset + x * sizeT - sizeBrd;
                    bottomT = offset + (255 - y) * sizeT - sizeBrd;
                    rightT = leftT + sizeTotal;
                    topT = bottomT + sizeTotal;

                    std::string imageName = "img_" + std::to_string(lod) + "_" + std::to_string(y) + "_" + std::to_string(x);
                    ofSummary << lod << " " << y << " " << x << " 1024 " << leftT << " " << topT * -1 << " " << sizeTotal;
                    ofSummary << " orthophoto/" << imageName << ".jp2\n";
                }
            }

            ofSummary.close();
        }
        */
}
//#pragma optimize("", off)
void terrainManager::bil_to_jp2Photos()
{
    float leftT, topT, size;
    int lod, y, x, pixSize;
    //char name[256];
    std::string name;
    std::string filename;
    std::ifstream ifSummary;

    jp2Dir jp2;
    jp2.load("F:/terrains_gis/austria_Wechsel/photos/photo_tiles.json");
    for (auto& F : jp2.files)
    {
        std::string binfile = "F:/ESim_NextCloud/eSim-Plantwork/terrains/austria_Wechsel/orthophoto/" + F.filename;
        std::ofstream of_jp2;
        of_jp2.open(binfile, std::ios::binary);
        if (of_jp2.good())
        {
            F.sizeInBytes = 0;
            for (auto& T : F.tiles)
            {
                T.fileOffset = F.sizeInBytes;
                filename = "F:/terrains_gis/austria_Wechsel/_temp/img_" + std::to_string(T.lod) + "_" + std::to_string(T.y) + "_" + std::to_string(T.x);
                T.sizeInBytes = bil_to_jp2PhotosMemory(of_jp2, filename, 1024, T.lod, T.y, T.x);
                F.sizeInBytes += T.sizeInBytes;
            }
            of_jp2.close();
        }
    }
    jp2.save("F:/ESim_NextCloud/eSim-Plantwork/terrains/austria_Wechsel/orthophotos.json");
    return;


    ifSummary.open("E:/terrains/switserland_Steg/gis/photos/gdal_photos_Summary_lod7.txt");
    if (ifSummary)
    {
        ifSummary >> lod >> y >> x >> pixSize >> leftT >> topT >> size >> name;
        filename = "E:/terrains/switserland_Steg/gis/_temp/img_" + std::to_string(lod) + "_" + std::to_string(y) + "_" + std::to_string(x);
        bil_to_jp2Photos(filename, 1024, lod, y, x);
        while (!ifSummary.eof())
        {
            ifSummary >> lod >> y >> x >> pixSize >> leftT >> topT >> size >> name;
            filename = "E:/terrains/switserland_Steg/gis/_temp/img_" + std::to_string(lod) + "_" + std::to_string(y) + "_" + std::to_string(x);
            bil_to_jp2Photos(filename, 1024, lod, y, x);
        }

        ifSummary.close();
    }

    /*
    FILE* file;
    fopen_s(&file, "E:/terrains/switserland_Steg/gis/photos/gdal_photos_Summary.txt", "r");
    int numread;
    do
    {
        char filename[256];
        int lod, y, x, blockSize;
        float xstart, ystart, size;
        memset(filename, 0, 256);
        //numread = fscanf_s(file, "%d %d %d %d %f %f %f %s", &lod, &y, &x, &blockSize, &xstart, &ystart, &size, filename, 256);
        numread = fscanf_s(file, "%d %d %d %d %f %f %f %s", &lod, &y, &x, &blockSize, &xstart, &ystart, &size, filename, 256);
        filename = "E:/terrains/switserland_Steg/gis/_temp/img_" + std::to_string(lod) + "_" + std::to_string(y) + "_" + std::to_string(x);
        if (numread > 0) {
            bil_to_jp2Photos(pathOnly + filename, blockSize, summary, lod, y, x, xstart, ystart, size);
        }
    } while (numread > 0);

    fclose(file);
    */

}


//#pragma optimize("", off)
uint terrainManager::bil_to_jp2PhotosMemory(std::ofstream& _file, std::string filename, const uint size, uint _lod, uint _y, uint _x)
{
    unsigned char data[1024][3][1024];
    unsigned char data1[1024][1024];


    ojph::codestream codestream;
    ojph::mem_outfile j2c_file;
    j2c_file.open();
    {
        // set up
        ojph::param_siz siz = codestream.access_siz();
        siz.set_image_extent(ojph::point(size, size));
        siz.set_num_components(3);
        siz.set_component(0, ojph::point(1, 1), 8, false);
        siz.set_component(1, ojph::point(1, 1), 8, false);
        siz.set_component(2, ojph::point(1, 1), 8, false);
        siz.set_image_offset(ojph::point(0, 0));
        siz.set_tile_size(ojph::size(size, size));
        siz.set_tile_offset(ojph::point(0, 0));

        ojph::param_cod cod = codestream.access_cod();
        cod.set_num_decomposition(5);
        cod.set_block_dims(64, 64);
        cod.set_progression_order("CPRL");      // ??? "RPCL"
        cod.set_color_transform(true);
        cod.set_reversible(false);

        codestream.access_qcd().set_irrev_quant(0.04f);
        codestream.set_planar(false);

        FILE* bilData = fopen((filename + ".bil").c_str(), "rb");
        if (bilData)
        {
            fread(data, sizeof(char), size * size * 3, bilData);
            codestream.write_headers(&j2c_file);

            uint next_comp;
            ojph::line_buf* cur_line = codestream.exchange(NULL, next_comp);

            //for (uint cmp = 0; cmp < 3; cmp++)
            {
                for (uint i = 0; i < size; i++)
                {
                    //base->read(cur_line, next_comp);
                    //fread(data, sizeof(float), size, bilData);

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



void terrainManager::bil_to_jp2Photos(std::string file, const uint size, uint _lod, uint _y, uint _x)
{
    unsigned char data[1024][3][1024];
    unsigned char data1[1024][1024];


    ojph::codestream codestream;
    ojph::j2c_outfile j2c_file;
    j2c_file.open((file + ".jp2").c_str());
    {
        // set up
        ojph::param_siz siz = codestream.access_siz();
        siz.set_image_extent(ojph::point(size, size));
        siz.set_num_components(3);
        siz.set_component(0, ojph::point(1, 1), 8, false);
        siz.set_component(1, ojph::point(1, 1), 8, false);
        siz.set_component(2, ojph::point(1, 1), 8, false);
        siz.set_image_offset(ojph::point(0, 0));
        siz.set_tile_size(ojph::size(size, size));
        siz.set_tile_offset(ojph::point(0, 0));

        ojph::param_cod cod = codestream.access_cod();
        cod.set_num_decomposition(5);
        cod.set_block_dims(64, 64);
        //if (num_precints != -1)
        //	cod.set_precinct_size(num_precints, precinct_size);
        //cod.set_progression_order("RPCL");
        cod.set_progression_order("CPRL");
        cod.set_color_transform(true);
        cod.set_reversible(false);

        codestream.access_qcd().set_irrev_quant(0.05f);
        codestream.set_planar(false);





        FILE* bilData = fopen((file + ".bil").c_str(), "rb");
        if (bilData)
        {
            fread(data, sizeof(char), size * size * 3, bilData);
            codestream.write_headers(&j2c_file);

            uint next_comp;
            ojph::line_buf* cur_line = codestream.exchange(NULL, next_comp);

            //for (uint cmp = 0; cmp < 3; cmp++)
            {
                for (uint i = 0; i < size; i++)
                {
                    //base->read(cur_line, next_comp);
                    //fread(data, sizeof(float), size, bilData);

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
            }
            fclose(bilData);
            /*
            if (_lod == 4 && _y == 4 && _x == 13)
            {
                std::ofstream ofs;
                ofs.open("E:/terrains/switserland_Steg/gis/temp_rgb.raw", std::ios::binary);
                if (ofs)
                {
                    for (uint i = 0; i < size; i++) {
                        for (uint j = 0; j < size; j++) {
                            ofs.write((char*)&data[i][0][j], 1);
                            ofs.write((char*)&data[i][1][j], 1);
                            ofs.write((char*)&data[i][2][j], 1);
                        }
                    }
                    ofs.close();
                }


                ofs.open("E:/terrains/switserland_Steg/gis/temp_red.raw", std::ios::binary);
                if (ofs)
                {
                    for (uint i = 0; i < size; i++) {
                        for (uint j = 0; j < size; j++) {
                            data1[i][j] = data[i][0][j];
                        }
                    }

                    ofs.write((char*)data1, 1024 * 1024);

                    ofs.close();
                }

                ofs.open("E:/terrains/switserland_Steg/gis/temp_green.raw", std::ios::binary);
                if (ofs)
                {
                    for (uint i = 0; i < size; i++) {
                        for (uint j = 0; j < size; j++) {
                            ofs.write((char*)&data[i][1][j], 1);
                        }
                    }

                    ofs.close();
                }

                ofs.open("E:/terrains/switserland_Steg/gis/temp_blue.raw", std::ios::binary);
                if (ofs)
                {
                    for (uint i = 0; i < size; i++) {
                        for (uint j = 0; j < size; j++) {
                            ofs.write((char*)&data[i][2][j], 1);
                        }
                    }

                    ofs.close();
                }
            }*/
        }


    }
    codestream.flush();
    codestream.close();


    //fprintf(summary, "%d %d %d %d %f %f %f %f %f elevation/hgt_%d_%d_%d.jp2\n", _lod, _y, _x, size, _xstart, _ystart, _size, data_min, (data_max - data_min), _lod, _y, _x);
}








void terrainManager::bil_to_jp2()
{
    std::filesystem::path path;
    FileDialogFilterVec filters = { {"txt"} }; // select the filanemaes file
    if (openFileDialog(filters, path))
    {
        FILE* file, * summary;
        fopen_s(&file, path.string().c_str(), "r");
        int numread;
        std::string pathOnly = path.string().substr(0, path.string().find_last_of("/\\") + 1);
        fopen_s(&summary, (pathOnly + "elevations.txt").c_str(), "w");
        do
        {
            char filename[256];
            int lod, y, x, blockSize;
            float xstart, ystart, size;
            memset(filename, 0, 256);
            numread = fscanf_s(file, "%d %d %d %d %f %f %f %s", &lod, &y, &x, &blockSize, &xstart, &ystart, &size, filename, 256);
            if (numread > 0) {
                bil_to_jp2(pathOnly + filename, blockSize, summary, lod, y, x, xstart, ystart, size);
            }
        } while (numread > 0);

        fclose(file);
    }
}



void terrainManager::bil_to_jp2(std::string file, const uint size, FILE* summary, uint _lod, uint _y, uint _x, float _xstart, float _ystart, float _size)
{

    // Find the minimum and maximum
    float data_min = 9999999.0f;
    float data_max = -9999999.0f;
    float data[1024];

    FILE* bilData = fopen((file + ".bil").c_str(), "rb");
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
    j2c_file.open((file + ".jp2").c_str());
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





        FILE* bilData = fopen((file + ".bil").c_str(), "rb");
        if (bilData)
        {
            codestream.write_headers(&j2c_file);

            uint next_comp;
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


    fprintf(summary, "%d %d %d %d %f %f %f %f %f elevation/hgt_%d_%d_%d.jp2\n", _lod, _y, _x, size, _xstart, _ystart, _size, data_min, (data_max - data_min), _lod, _y, _x);
}


void terrainManager::onGuiMenubar(Gui* pGui)
{
    bool b = false;

    ImGui::SetCursorPos(ImVec2(150, 0));
    auto& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Text] = ImVec4(0.38f, 0.52f, 0.10f, 1);
    //ImGui::Text(presets.name.c_str());
    style.Colors[ImGuiCol_Text] = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);

    ImGui::SetCursorPos(ImVec2(250, 0));
    if (ImGui::BeginMenu("terrain"))
    {
        if (ImGui::MenuItem("load"))
        {
            std::filesystem::path path;
            FileDialogFilterVec filters = { {"terrainSettings.json"} };
            if (openFileDialog(filters, path))
            {
                std::ifstream is(path);
                lastfile.terrain = path.string();
            }
        }
        if (ImGui::MenuItem("process bil -> jp2"))
        {
            bil_to_jp2();
        }
        if (ImGui::MenuItem("generatePhotoGdal"))
        {
            generateGdalPhotos();
        }
        if (ImGui::MenuItem("process bil -> jp2 - Photos"))
        {
            bil_to_jp2Photos();
        }
        if (ImGui::MenuItem("settings"))
        {
            requestPopupSettings = true;
        }
        if (ImGui::MenuItem("debug"))
        {
            requestPopupDebug = true;
        }
        ImGui::EndMenu();
    }

    float x = ImGui::GetCursorPosX();
    ImGui::SetCursorPos(ImVec2(screenSize.x - 1200, 0));
    ImGui::Text(lastfile.terrain.c_str());
    ImGui::SetCursorPos(ImVec2(x, 0));
}

//mimic hlsl all()
bool all(float4 in)
{
    if (in.x == 0) return false;
    if (in.y == 0) return false;
    if (in.z == 0) return false;
    if (in.w == 0) return false;
    return true;
}


// FIXME NOT GREAT to rede every frame but also likely reaaly fast
void terrainManager::calculateSurfaceFlags()
{
    memset(frustumFlags, 0, sizeof(uint4) * 1024);		// clear all

    for (auto& tile : m_used)
    {
        frustumFlags[tile->index].x |= 1 << 31;   // just makr as active
        float boundingSphereSize = tile->size * 1.0f;// very generous but missing here is FATAL
        for (int i = 0; i < CameraType_MAX; i++) {
            if (cameraViews[i].bUse) {
                float4 viewBS = cameraViews[i].view * tile->boundingSphere;
                float4 test = saturate(viewBS * cameraViews[i].frustumMatrix + float4(boundingSphereSize, boundingSphereSize, boundingSphereSize, boundingSphereSize));
                if (all(test))
                {
                    frustumFlags[tile->index].y |= 1 << i;  // visible for plants

                    bool surface = tile->parent && tile->parent->main_ShouldSplit && tile->child[0] == nullptr;
                    // FIXME surface should depend on camera type, or mianshouldSplit should be per surface
                    if (surface)
                    {
                        frustumFlags[tile->index].x |= 1 << i;
                    }
                }
            }
        }
    }
}

void terrainManager::testForSurfaceEnv()
{
}


#define mainMaxLod 15
bool terrainManager::testForSplit(quadtree_tile* _tile)
{
    _tile->main_ShouldSplit = false;
    _tile->env_ShouldSplit = false;

    if (_tile->lod > mainMaxLod)
        return false;

    float scale = _tile->size / glm::length(float3(_tile->boundingSphere) - cameraOrigin);
    float boundingSphereSize = _tile->size * 0.9f;

    for (int i = 0; i < CameraType_MAX; i++) {
        if (cameraViews[i].bUse) {

            float4 viewBS = cameraViews[i].view * _tile->boundingSphere;
            float4 test = saturate(viewBS * cameraViews[i].frustumMatrix + float4(boundingSphereSize, boundingSphereSize, boundingSphereSize, boundingSphereSize));
            bool inFrust = all(test);

            viewBS.w = 0;
            float distance = length(viewBS) + 0.01f;
            float fovscale = glm::length(cameraViews[i].proj[0]);
            float m_halfAngle_to_Pixels = cameraViews[i].resolution * fovscale / 4.0f;
            float lod_Pix = _tile->size / distance * m_halfAngle_to_Pixels;



            if (lod_Pix > 150 && inFrust)
            {
                _tile->main_ShouldSplit = true;
            }
            else if (lod_Pix > 300)
            {
                _tile->main_ShouldSplit = true;
            }
        }
    }

    if (_tile->main_ShouldSplit && !_tile->child[0]) {
        _tile->forSplit = true;
        return true;
    }



    return false;
}

bool terrainManager::testFrustum(quadtree_tile* _tile)
{
    return true;
}

void terrainManager::markChildrenForRemove(quadtree_tile* _tile)
{
    for (uint i = 0; i < 4; i++) {
        if (_tile->child[i]) {
            markChildrenForRemove(_tile->child[i]);
            _tile->child[i]->forRemove = true;
            _tile->child[i] = nullptr;
        }
    }
}


uint terrainManager::gisHash(glm::vec3 _position)
{
    uint z = (uint)floor((_position.z + (settings.size / 2.0f)) * 16.0f / settings.size);
    uint x = (uint)floor((_position.x + (settings.size / 2.0f)) * 16.0f / settings.size);

    return (z << 16) + x;
}

void terrainManager::gisReload(glm::vec3 _position)
{
    float halfsize = ecotopeSystem::terrainSize / 2.f;
    float blocksize = ecotopeSystem::terrainSize / 16.f;
    float buffersize = blocksize * (3200.f / 2500.f); // this ratio was set by eifel, keep it
    float edgesize = (buffersize - blocksize) / 2;

    uint hash = gisHash(_position);
    if (hash != gis_overlay.hash)
    {
        gis_overlay.hash = hash;
        uint x = hash & 0xffff;
        uint z = hash >> 16;

        std::filesystem::path path = settings.dirRoot + "/overlay/" + char(65 + hash & 0xff) + "_" + std::to_string(z) + "_image.dds";
        gis_overlay.texture = Texture::createFromFile(path, true, true, Resource::BindFlags::ShaderResource);

        gis_overlay.box = glm::vec4(-edgesize - halfsize + x * blocksize, -edgesize - halfsize + z * blocksize, buffersize, buffersize);    // fixme this sort of asumes 40 x 40 km

        terrainShader.Vars()->setTexture("gGISAlbedo", gis_overlay.texture);
        terrainShader.Vars()["PerFrameCB"]["showGIS"] = (int)gis_overlay.show;
        terrainShader.Vars()["PerFrameCB"]["gisBox"] = gis_overlay.box;
    }
}


void terrainManager::clearCameras()
{
    for (uint i = 0; i < CameraType_MAX; i++) {
        cameraViews[i].bUse = false;
    }
}

void terrainManager::setCamera(unsigned int _index, glm::mat4 viewMatrix, glm::mat4 projMatrix, float3 position, bool b_use, float _resolution)
{
    if (_index < CameraType_MAX)
    {
        cameraOrigin = position;	// SURE so last set camera does this, but they should just about all be the same right?

        cameraViews[_index].bUse = b_use;
        cameraViews[_index].resolution = _resolution;
        cameraViews[_index].view = viewMatrix;
        cameraViews[_index].proj = projMatrix;
        cameraViews[_index].viewProj = cameraViews[_index].view * cameraViews[_index].proj;
        cameraViews[_index].position = position;

        // remeber that these are transposed as well here
        cameraViews[_index].frustumPlane[0].x = cameraViews[_index].proj[0][3] + cameraViews[_index].proj[0][0];
        cameraViews[_index].frustumPlane[0].y = cameraViews[_index].proj[1][3] + cameraViews[_index].proj[1][0];
        cameraViews[_index].frustumPlane[0].z = cameraViews[_index].proj[2][3] + cameraViews[_index].proj[2][0];
        cameraViews[_index].frustumPlane[0].w = cameraViews[_index].proj[3][3] + cameraViews[_index].proj[3][0];

        cameraViews[_index].frustumPlane[1].x = cameraViews[_index].proj[0][3] - cameraViews[_index].proj[0][0];
        cameraViews[_index].frustumPlane[1].y = cameraViews[_index].proj[1][3] - cameraViews[_index].proj[1][0];
        cameraViews[_index].frustumPlane[1].z = cameraViews[_index].proj[2][3] - cameraViews[_index].proj[2][0];
        cameraViews[_index].frustumPlane[1].w = cameraViews[_index].proj[3][3] - cameraViews[_index].proj[3][0];

        cameraViews[_index].frustumPlane[2].x = cameraViews[_index].proj[0][3] + cameraViews[_index].proj[0][1];
        cameraViews[_index].frustumPlane[2].y = cameraViews[_index].proj[1][3] + cameraViews[_index].proj[1][1];
        cameraViews[_index].frustumPlane[2].z = cameraViews[_index].proj[2][3] + cameraViews[_index].proj[2][1];
        cameraViews[_index].frustumPlane[2].w = cameraViews[_index].proj[3][3] + cameraViews[_index].proj[3][1];

        cameraViews[_index].frustumPlane[3].x = cameraViews[_index].proj[0][3] - cameraViews[_index].proj[0][1];
        cameraViews[_index].frustumPlane[3].y = cameraViews[_index].proj[1][3] - cameraViews[_index].proj[1][1];
        cameraViews[_index].frustumPlane[3].z = cameraViews[_index].proj[2][3] - cameraViews[_index].proj[2][1];
        cameraViews[_index].frustumPlane[3].w = cameraViews[_index].proj[3][3] - cameraViews[_index].proj[3][1];

        cameraViews[_index].frustumMatrix = glm::mat4x4(
            cameraViews[_index].frustumPlane[0].x, cameraViews[_index].frustumPlane[0].y, cameraViews[_index].frustumPlane[0].z, cameraViews[_index].frustumPlane[0].w,
            cameraViews[_index].frustumPlane[1].x, cameraViews[_index].frustumPlane[1].y, cameraViews[_index].frustumPlane[1].z, cameraViews[_index].frustumPlane[1].w,
            cameraViews[_index].frustumPlane[2].x, cameraViews[_index].frustumPlane[2].y, cameraViews[_index].frustumPlane[2].z, cameraViews[_index].frustumPlane[2].w,
            cameraViews[_index].frustumPlane[3].x, cameraViews[_index].frustumPlane[3].y, cameraViews[_index].frustumPlane[3].z, cameraViews[_index].frustumPlane[3].w);
    }

    viewMask = 0;
    for (uint i = 0; i < CameraType_MAX; i++)
    {
        if (cameraViews[i].bUse)
        {
            viewMask |= 1 << i;
        }
    }
}



void replaceAll_TM(std::string& str, const std::string& from, const std::string& to) {
    if (from.empty())
        return;
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // In case 'to' contains 'from', like replacing 'x' with 'yx'
    }
}


void terrainManager::updateShaderConstants(Texture::SharedPtr _previousFrame, shaderLightBuffer _buffer)
{
    // FXIME pretty sure we can do this in onLoad
    terrainShader.Vars()->setTexture("terrainShadow", terrainShadowTexture);
    //rappersvilleShader.Vars()->setTexture("terrainShadow", terrainShadowTexture);
    //gliderwingShader.Vars()->setTexture("terrainShadow", terrainShadowTexture);
    terrainSpiteShader.Vars()->setTexture("terrainShadow", terrainShadowTexture);

    ribbonShader.Vars()->setTexture("gPreviousFrame", _previousFrame);
    terrainSpiteShader.Vars()->setTexture("gPreviousFrame", _previousFrame);

    terrainShader.Vars()["LightsCB"]["sunDirection"] = _buffer.sunDirection;
    terrainShader.Vars()["LightsCB"]["sunRightVector"] = _buffer.sunRightVector;
    terrainShader.Vars()["LightsCB"]["sunUpVector"] = _buffer.sunUpVector;
    terrainShader.Vars()["LightsCB"]["screenSize"] = _buffer.screenSize;
    terrainShader.Vars()["LightsCB"]["fog_far_Start"] = _buffer.fog_far_Start;
    terrainShader.Vars()["LightsCB"]["fog_far_log_F"] = _buffer.fog_far_log_F;
    terrainShader.Vars()["LightsCB"]["fog_far_one_over_k"] = _buffer.fog_far_one_over_k;

    /*rappersvilleShader.Vars()["LightsCB"]["sunDirection"] = _buffer.sunDirection;
    rappersvilleShader.Vars()["LightsCB"]["sunRightVector"] = _buffer.sunRightVector;
    rappersvilleShader.Vars()["LightsCB"]["sunUpVector"] = _buffer.sunUpVector;
    rappersvilleShader.Vars()["LightsCB"]["screenSize"] = _buffer.screenSize;
    rappersvilleShader.Vars()["LightsCB"]["fog_far_Start"] = _buffer.fog_far_Start;
    rappersvilleShader.Vars()["LightsCB"]["fog_far_log_F"] = _buffer.fog_far_log_F;
    rappersvilleShader.Vars()["LightsCB"]["fog_far_one_over_k"] = _buffer.fog_far_one_over_k;
    */
    /*gliderwingShader.Vars()["LightsCB"]["sunDirection"] = _buffer.sunDirection;
    gliderwingShader.Vars()["LightsCB"]["sunRightVector"] = _buffer.sunRightVector;
    gliderwingShader.Vars()["LightsCB"]["sunUpVector"] = _buffer.sunUpVector;
    gliderwingShader.Vars()["LightsCB"]["screenSize"] = _buffer.screenSize;
    gliderwingShader.Vars()["LightsCB"]["fog_far_Start"] = _buffer.fog_far_Start;
    gliderwingShader.Vars()["LightsCB"]["fog_far_log_F"] = _buffer.fog_far_log_F;
    gliderwingShader.Vars()["LightsCB"]["fog_far_one_over_k"] = _buffer.fog_far_one_over_k;
    */

    terrainSpiteShader.Vars()["LightsCB"]["sunDirection"] = _buffer.sunDirection;
    terrainSpiteShader.Vars()["LightsCB"]["sunRightVector"] = _buffer.sunRightVector;
    terrainSpiteShader.Vars()["LightsCB"]["sunUpVector"] = _buffer.sunUpVector;
    terrainSpiteShader.Vars()["LightsCB"]["screenSize"] = _buffer.screenSize;
    terrainSpiteShader.Vars()["LightsCB"]["fog_far_Start"] = _buffer.fog_far_Start;
    terrainSpiteShader.Vars()["LightsCB"]["fog_far_log_F"] = _buffer.fog_far_log_F;
    terrainSpiteShader.Vars()["LightsCB"]["fog_far_one_over_k"] = _buffer.fog_far_one_over_k;

    plants_Root.updateShaderConstants(_previousFrame, terrainShadowTexture, _buffer);
}



bool terrainManager::update(RenderContext* _renderContext)
{
    bake.renderContext = _renderContext;

    if ((terrainMode == _terrainMode::vegetation) ||
        (terrainMode == _terrainMode::glider) ||
        (terrainMode == _terrainMode::terrainBuilder) ||
        (terrainMode == _terrainMode::textureTool))
    {
        fullResetDoNotRender = false;
        return false;
    }


    FALCOR_PROFILE("update");
    bool dirty = false;

    {
        {
            FALCOR_PROFILE("update_bake");
            if (bake.inProgress)
            {
                bake.renderContext = _renderContext;
                bake_frame();
            }

            if (mEcosystem.change) {
                mEcosystem.change = false;
                reset(true);
            }
        }


        {
            //fprintf(terrafectorSystem::_logfile, "update_roads\n");
            //fflush(terrafectorSystem::_logfile);
            FALCOR_PROFILE("update_roads");
            updateDynamicRoad(false);
            mRoadNetwork.testHit(split.feedback.tum_Position);


            if (mRoadNetwork.isDirty)
            {
                splines.numStaticSplines = __min((int)roadNetwork::staticBezierData.size(), splines.maxBezier);
                splines.numStaticSplinesIndex = __min((int)roadNetwork::staticIndexData.size(), splines.maxIndex);
                if (splines.numStaticSplines > 0)
                {
                    splines.bezierData->setBlob(roadNetwork::staticBezierData.data(), 0, splines.numStaticSplines * sizeof(cubicDouble));
                    splines.indexData->setBlob(roadNetwork::staticIndexData.data(), 0, splines.numStaticSplinesIndex * sizeof(bezierLayer));
                    mRoadNetwork.isDirty = false;
                }
                splines.numStaticSplinesBakeOnlyIndex = __min((int)roadNetwork::staticIndexData_BakeOnly.size(), splines.maxIndex);
                if (splines.numStaticSplinesBakeOnlyIndex > 0)
                {
                    splines.indexDataBakeOnly->setBlob(roadNetwork::staticIndexData_BakeOnly.data(), 0, splines.numStaticSplinesBakeOnlyIndex * sizeof(bezierLayer));
                    mRoadNetwork.isDirty = false;
                }

                bezierRoadstoLOD(4);
                mRoadNetwork.isDirty = false;
            }
        }

        {
            FALCOR_PROFILE("update_splitmerge");
            //split.compute_tileClear.dispatch(_renderContext, 1, 1);


            for (auto& tile : m_used)
            {
                tile->reset();
            }

            for (auto& tile : m_used)
            {
                dirty |= testForSplit(tile);

                if (!tile->main_ShouldSplit && tile->child[0]) {
                    markChildrenForRemove(tile);
                }
            }

            if (!dirty) fullResetDoNotRender = false;

            for (auto itt = m_used.begin(); itt != m_used.end();)               // do al merges
            {
                if ((*itt)->forRemove) {
                    (*itt)->forRemove = false;
                    m_free.push_back(*itt);
                    itt = m_used.erase(itt);
                }
                else {
                    ++itt;
                }
            }
        }
    }

    splitOne(_renderContext);





    //if (dirty)    // for now update every frame - its fast
    {
        FALCOR_PROFILE("update_dirty");

        {
            FALCOR_PROFILE("update_readback");

            // readback of tile centers
            _renderContext->copyResource(split.buffer_tileCenter_readback.get(), split.buffer_tileCenters.get());
            const float4* pData = (float4*)split.buffer_tileCenter_readback.get()->map(Buffer::MapType::Read);
            std::memcpy(&split.tileCenters, pData, sizeof(float4) * numTiles);
            split.buffer_tileCenter_readback.get()->unmap();
            for (auto& tile : m_used)
            {
                if (split.tileCenters[tile->index].x > 0)
                {
                    tile->origin.y = split.tileCenters[tile->index].x;           // THIS is very wrong, .x contains the middl;em but i also think its unused
                    tile->boundingSphere.y = split.tileCenters[tile->index].x;
                }
            }
        }

        split.compute_tileClear.dispatch(_renderContext, 1, 1);


        calculateSurfaceFlags();



        auto pCB = split.compute_tileBuildLookup.Vars()->getParameterBlock("gConstants");
        pCB->setBlob(frustumFlags, 0, 1024 * sizeof(uint4));	// FIXME number of tiles
        uint cnt = (numTiles + 31) >> 5;
        {
            FALCOR_PROFILE("tileBuildLookup");
            split.compute_tileBuildLookup.dispatch(_renderContext, cnt, 1);
        }
        //FIXME this of coarse adds it in teh worst fashion, interleaving 64 triangles or quads of diffirent tiles with one another
        // see what to do diffirent VERY bad for materials and texture reads

        // FIXME THIS SHPULD BE IN RENDER - its not update specific but camera specific
        // FIXME2025 MOIVE THIS

    }




    if (hasChanged) {
        hasChanged = false;
        return true;
    }
    return false;
}







void terrainManager::hashAndCache_Thread(quadtree_tile* pTile)
{
    auto start = high_resolution_clock::now();

    if (jphData.size() < 1048576) jphData.resize(1048576);
    //std::array<unsigned short, 1048576> data;

    auto hashelement = elevationTileHashmap[pTile->elevationHash];
    ojph::codestream codestream;
    ojph::j2c_infile j2c_file;
    j2c_file.open(elevationTileHashmap[pTile->elevationHash].filename.c_str());
    codestream.enable_resilience();
    codestream.set_planar(false);
    codestream.read_headers(&j2c_file);
    codestream.create();
    uint next_comp;



    for (int i = 0; i < 1024; ++i)
    {
        ojph::line_buf* line = codestream.pull(next_comp);
        int32_t* dp = line->i32;
        for (int j = 0; j < 1024; j++) {
            int16_t val = (int16_t)*dp++;
            jphData[i * 1024 + j] = val;
        }
    }
    codestream.close();
    auto start_b = high_resolution_clock::now();
    //textureCacheElement map;
   // cacheTexture = Texture::create2D(1024, 1024, Falcor::ResourceFormat::R16Unorm, 1, 1, data.data(), Resource::BindFlags::ShaderResource);
    //elevationCache.set(pTile->elevationHash, map);

    auto stop = high_resolution_clock::now();
    stream.terrainCacheTime = (double)duration_cast<microseconds>(stop - start).count() / 1000.;
    stream.terrainCacheJPHTime = (double)duration_cast<microseconds>(stop - start_b).count() / 1000.;
    hashCount = 2;
    cacheHash = pTile->elevationHash;
}



bool terrainManager::hashAndCache(quadtree_tile* pTile)
{
    if (hashCount == 2)
    {
        LOG_LINE(1, "elevationCache.set 1");
        textureCacheElement map;
        map.texture = Texture::create2D(1024, 1024, Falcor::ResourceFormat::R16Unorm, 1, 1, jphData.data(), Resource::BindFlags::ShaderResource);;
        elevationCache.set(cacheHash, map);
        cacheTexture.reset();
        hashCount = 0;
    }

    uint32_t hash = getHashFromTileCoords(pTile->lod, pTile->y, pTile->x);
    std::map<uint32_t, heightMap>::iterator it = elevationTileHashmap.find(hash);
    if (it != elevationTileHashmap.end()) {
        pTile->elevationHash = hash;
    }

    if (pTile->elevationHash > 0)
    {
        textureCacheElement map;
        LOG_LINE(1, "elevationCache.get");
        if (!elevationCache.get(pTile->elevationHash, map))
        {
            if (hashCount == 0)
            {
                hashCount++;
                hashFuture = std::async(std::launch::async, &terrainManager::hashAndCache_Thread, this, pTile);
            }
            //hashFuture.wait();
            //elevationCache.get(pTile->elevationHash, map);
            //split.compute_tileBicubic.Vars()->setTexture("gInput", map.texture);
            return false;
        }
        else
        {

            split.compute_tileBicubic.Vars()->setTexture("gInput", map.texture);
            return true;
        }
    }

    return true;
}



void terrainManager::hashAndCacheImages_Thread(quadtree_tile* pTile)
{
    if (imageDirectory.files.size() == 0) return;
    if (pTile->imageHash == 0)
    {
        bool CM = true;
    }

    auto start = high_resolution_clock::now();

    if (jphImageData.size() < 1024 * 1024 * 4) jphImageData.resize(1024 * 1024 * 4);
    //std::array<unsigned char, 1024 * 1024 * 4> data;
    std::shared_ptr<std::vector<unsigned char>> dataCache;

    ojph::codestream codestream;
    ojph::mem_infile j2c_file;
    //j2c_file.open(imageTileHashmap[pTile->imageHash].filename.c_str());
    LOG_LINE(1, "imageDirectory 1");
    std::map<uint32_t, uint2>::iterator itH = imageDirectory.tileHash.find(pTile->imageHash);
    if (itH == imageDirectory.tileHash.end()) {
        fprintf(terrafectorSystem::_logfile, "FAILED tileHash.find\n");
        fflush(terrafectorSystem::_logfile);
    }
    jp2File& file = imageDirectory.files[itH->second.x];
    jp2Map& mapTile = imageDirectory.files[itH->second.x].tiles[itH->second.y];

    if (itH->second.x == 0)
    {
        j2c_file.open(&imageDirectory.dataRoot[mapTile.fileOffset], mapTile.sizeInBytes);
    }
    else
    {
        if (imageDirectory.cache.get(file.hash, dataCache))
        {
            j2c_file.open(dataCache->data() + mapTile.fileOffset, mapTile.sizeInBytes);
        }
        else
        {

            bool bCM = true;
            fprintf(terrafectorSystem::_logfile, "FIX imageCache.resize(55);  its still too small\n");
            fprintf(terrafectorSystem::_logfile, "offset %d, lod %d, %s\n", mapTile.fileOffset, mapTile.lod, file.filename.c_str());
            fprintf(terrafectorSystem::_logfile, "itH file %d, tile %d\n", itH->second.x, itH->second.y);
            fprintf(terrafectorSystem::_logfile, "Bug is liekly here at teh ned void jp2Dir::load(std::string _name). The thing that changed is that I changed FOV, so diffirent amount fo tiles vissible\n");


            fflush(terrafectorSystem::_logfile);
            // should never get here but handle it somehow
            // split.compute_tileBicubic.Vars()->setTexture("gInputAlbedo", map.texture);
            // I think it was with very fast movement, not sure how that was possible
        }
    }
    codestream.enable_resilience();
    codestream.set_planar(false);
    codestream.read_headers(&j2c_file);
    codestream.create();
    uint next_comp;



    for (int i = 0; i < 1024; i++)
    {
        for (int j = 0; j < 3; j++)
        {
            ojph::line_buf* line = codestream.pull(next_comp);
            int32_t* dp = line->i32;
            for (int k = 0; k < 1024; k++) {
                jphImageData[(i * 1024 * 4) + (k * 4) + next_comp] = (unsigned char)*dp++;
            }
        }
    }
    codestream.close();
    auto start_b = high_resolution_clock::now();
    //textureCacheElement map;
    //cacheTextureImage = Texture::create2D(1024, 1024, Falcor::ResourceFormat::RGBA8UnormSrgb, 1, 1, jphImageData.data(), Resource::BindFlags::ShaderResource);
    //cacheTextureImage = Texture::create2D(1024, 1024, Falcor::ResourceFormat::RGBA8UnormSrgb, 1, 1,nullptr, Resource::BindFlags::ShaderResource);
    //imageCache.set(pTile->imageHash, map);

    auto stop = high_resolution_clock::now();
    stream.imageCacheTime = (double)duration_cast<microseconds>(stop - start).count() / 1000.;
    stream.imageCacheJPHTime = (double)duration_cast<microseconds>(stop - start_b).count() / 1000.;
    hashCountImage = 2;
    cacheHashImage = pTile->imageHash;
}

/*  The weird carsh comes from my cache being too small, so tiles gets deleted tehn needed at another resolution
*/
bool terrainManager::hashAndCacheImages(quadtree_tile* pTile)
{
    if (imageDirectory.files.size() == 0) return true;

    if (hashCountImage == 2)
    {
        LOG_LINE(1, "imageCache.set 1");
        textureCacheElement map;
        map.texture = Texture::create2D(1024, 1024, Falcor::ResourceFormat::RGBA8UnormSrgb, 1, 1, jphImageData.data(), Resource::BindFlags::ShaderResource);;
        //map.texture = Texture::create2D(1024, 1024, Falcor::ResourceFormat::RGBA8UnormSrgb, 1, 1, nullptr, Resource::BindFlags::ShaderResource);;
        imageCache.set(cacheHashImage, map);
        cacheTextureImage.reset();
        hashCountImage = 0;
    }

    uint32_t hash = getHashFromTileCoords(pTile->lod, pTile->y, pTile->x);

    // First load the JP2 Data, BUT 0,0,0 IS  PRELOADED, thisis n own thread so should look like no time
    auto start_c = high_resolution_clock::now();
    imageDirectory.cacheHash(hash);
    auto stop_c = high_resolution_clock::now();
    double time = (double)duration_cast<microseconds>(stop_c - start_c).count() / 1000.;
    if (time > 1.f)     stream.imageCacheIOTime = time;

    std::map<uint32_t, uint2>::iterator it = imageDirectory.tileHash.find(hash);
    if (it != imageDirectory.tileHash.end()) {
        pTile->imageHash = hash;
    }
    //    else
    //    {
            //fprintf(terrafectorSystem::_logfile, "FAILED %d %d %d   imageDirectory.tileHash.find(hash);\n", pTile->lod, pTile->y, pTile->x);
            //fflush(terrafectorSystem::_logfile);
      //  }

        //if (pTile->imageHash > 0)
    {
        textureCacheElement map;
        LOG_LINE(1, "imageCache.get");
        if (!imageCache.get(pTile->imageHash, map))     // so we dont have teh image on GPU
        {
            if (hashCountImage == 0)
            {
                hashCountImage++;
                hashIFuture = std::async(std::launch::async, &terrainManager::hashAndCacheImages_Thread, this, pTile);
            }
            return false;
        }
        else
        {
            split.compute_tileBicubic.Vars()->setTexture("gInputAlbedo", map.texture);
            return true;
        }
    }

    return true;
}



void terrainManager::setChild(quadtree_tile* pTile, int y, int x)
{
    int childIdx = (y << 1) + x;
    float origX = pTile->size / 2.0f * x;
    float origY = pTile->size / 2.0f * y;

    pTile->child[childIdx] = m_free.front();
    //printf("%d, ",pTile->child[childIdx]->index);
    m_free.pop_front();
    pTile->child[childIdx]->set(pTile->lod + 1, pTile->x * 2 + x, pTile->y * 2 + y, pTile->size / 2.0f, pTile->origin + float4(origX, 0, origY, 0), pTile);
    m_used.push_back(pTile->child[childIdx]);
    pTile->child[childIdx]->elevationHash = pTile->elevationHash;
    pTile->child[childIdx]->imageHash = pTile->imageHash;
}


void terrainManager::splitOne(RenderContext* _renderContext)
{
    /* Bloody hell, pick the best one to do*/
    if (m_free.size() < 8) return;


    // FIXME PICK A BETTER ONE HERE
    for (auto& tile : m_used)
    {
        if (tile->forSplit)
        {
            bool dataReady = true;
            {
                //FALCOR_PROFILE("hashAndCache");
                hasChanged = true;
                dataReady &= hashAndCache(tile);
            }

            {
                //FALCOR_PROFILE("hashAndCacheImages");
                dataReady &= hashAndCacheImages(tile);
            }

            if (dataReady)
            {

                FALCOR_PROFILE("split");

                setChild(tile, 0, 0);
                setChild(tile, 0, 1);
                setChild(tile, 1, 0);
                setChild(tile, 1, 1);

                {
                    FALCOR_PROFILE("cs_tile_SplitMerge");
                    tileForSplit children[4];
                    for (int i = 0; i < 4; i++) {
                        children[i].index = tile->child[i]->index;
                        children[i].lod = tile->child[i]->lod;
                        children[i].y = tile->child[i]->y;
                        children[i].x = tile->child[i]->x;

                        children[i].origin = tile->child[i]->origin;
                        children[i].scale = tile->child[i]->size;
                    }

                    /*const auto& pReflector = split.compute_tileSplitMerge.Vars()->getReflection();
                    const auto& pDefaultBlock = pReflector->getDefaultParameterBlock();
                    const auto& mPerLightCbLoc = pDefaultBlock->getResourceBinding("gConstants");
                    auto pCB = split.compute_tileSplitMerge.Vars()->getParameterBlock(mPerLightCbLoc); */
                    auto pCB = split.compute_tileSplitMerge.Vars()->getParameterBlock("gConstants");
                    pCB->setBlob(children, 0, 4 * sizeof(tileForSplit));
                    split.compute_tileSplitMerge.dispatch(_renderContext, 1, 1);
                }


                {
                    FALCOR_PROFILE("extract_all");
                    for (int i = 0; i < 4; i++) {
                        splitChild(tile->child[i], _renderContext);
                        testForSplit(tile->child[i]);		// so its frustum flags are set
                    }
                }
                return;
            }

        }
    }


    {
        FALCOR_PROFILE("split");
        {
            FALCOR_PROFILE("cs_tile_SplitMerge");
        }
        {
            FALCOR_PROFILE("extract_all");
            {
                //FALCOR_PROFILE("bicubic");
            }
            {
                //FALCOR_PROFILE("renderTopdown");
            }
            {
                //FALCOR_PROFILE("compute");
                {
                    // FALCOR_PROFILE("copy_VERT");
                }
                {
                    //FALCOR_PROFILE("compress_copy_Albedo");
                }
                {
                    //FALCOR_PROFILE("normals");
                }
                {
                    //FALCOR_PROFILE("verticies");
                }
                {
                    //FALCOR_PROFILE("copy normals");
                }
                {
                    //FALCOR_PROFILE("jumpflood");
                }
                {
                    //FALCOR_PROFILE("copy_PBR");
                }
                {
                    //FALCOR_PROFILE("delaunay");
                }
            }
        }
    }
}

void terrainManager::splitChild(quadtree_tile* _tile, RenderContext* _renderContext)
{
    const uint32_t cs_w = tile_numPixels / tile_cs_ThreadSize;
    const float2 origin = float2(_tile->origin.x, _tile->origin.z);
    const float outerSize = _tile->size * tile_numPixels / tile_InnerPixels;
    const float pixelSize = outerSize / tile_numPixels;
    float halfsize = ecotopeSystem::terrainSize / 2.f;
    float blocksize = ecotopeSystem::terrainSize / 16.f;


    {
        // Not nessesary but nice where we lack data for now
        _renderContext->clearFbo(split.tileFbo.get(), glm::vec4(0.3f, 0.3f, 0.3f, 1.0f), 1.0f, 0, FboAttachmentType::All);

        _renderContext->clearRtv(split.tileFbo.get()->getRenderTargetView(3), glm::vec4(1.0f, 0.07f, 1.0f, 0.0f)); // PBR

        _renderContext->clearRtv(split.tileFbo.get()->getRenderTargetView(4), glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
        _renderContext->clearRtv(split.tileFbo.get()->getRenderTargetView(5), glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
        _renderContext->clearRtv(split.tileFbo.get()->getRenderTargetView(6), glm::vec4(1.0f, 1.0f, 0.0f, 0.0f));
        _renderContext->clearRtv(split.tileFbo.get()->getRenderTargetView(7), glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
    }


    {
        //FALCOR_PROFILE("bicubic");
        heightMap& elevationMap = elevationTileHashmap[_tile->elevationHash];

        float2 bicubicOffset = (origin - elevationMap.origin) / elevationMap.size;
        float S = pixelSize / elevationMap.size;
        float2 bicubicSize = float2(S, S);

        if (_tile->elevationHash == 0)
        {
            split.compute_tileBicubic.Vars()->setTexture("gInput", split.rootElevation);
        }
        split.compute_tileBicubic.Vars()["gConstants"]["offset"] = bicubicOffset;
        split.compute_tileBicubic.Vars()["gConstants"]["size"] = bicubicSize;
        split.compute_tileBicubic.Vars()["gConstants"]["hgt_offset"] = elevationMap.hgt_offset;
        split.compute_tileBicubic.Vars()["gConstants"]["hgt_scale"] = elevationMap.hgt_scale;
        split.compute_tileBicubic.Vars()["gConstants"]["isHeight"] = 1;
        split.compute_tileBicubic.dispatch(_renderContext, cs_w, cs_w);
    }

    if (imageDirectory.files.size() > 0)
    {
        // copy the image tiles to diffuse
        //heightMap& imageMap = imageTileHashmap[_tile->imageHash];
        //heightMap& imageMap = imageDirectory.tileHash

        std::map<uint32_t, uint2>::iterator itH = imageDirectory.tileHash.find(_tile->imageHash);
        //jp2File& file = imageDirectory.files[itH->second.x];
        jp2Map& mapTile = imageDirectory.files[itH->second.x].tiles[itH->second.y];

        float S = pixelSize / mapTile.size;
        split.compute_tileBicubic.Vars()["gConstants"]["offset"] = (origin - mapTile.origin) / mapTile.size;
        split.compute_tileBicubic.Vars()["gConstants"]["size"] = float2(S, S);
        split.compute_tileBicubic.Vars()["gConstants"]["isHeight"] = 0;
        split.compute_tileBicubic.dispatch(_renderContext, cs_w, cs_w);
    }

    {
        splitRenderTopdown(_tile, _renderContext);
        _renderContext->copySubresource(height_Array.get(), _tile->index, split.tileFbo->getColorTexture(0).get(), 0);  // for picking only
    }

    {
        //FALCOR_PROFILE("compute");

        {
            //FALCOR_PROFILE("ecotope");

            heightMap& elevationMap = elevationTileHashmap[0];
            split.compute_tileEcotopes.Vars()->setTexture("gLowresHgt", split.rootElevation);
            split.compute_tileEcotopes.Vars()->setBuffer("plantIndex", mEcosystem.getPLantBuffer());
            split.compute_tileEcotopes.Vars()->setBuffer("plantDensity", mEcosystem.getPLantDesityBuffer());

            auto pCB = split.compute_tileEcotopes.Vars()->getParameterBlock("gConstants");

            // bad herer but lets set the textures
            auto& block = split.compute_tileEcotopes.Vars()->getParameterBlock("gmyTextures");
            ShaderVar& var = block->findMember("T");        // FIXME pre get
            {
                for (size_t i = 0; i < mEcosystem.ecotopes.size(); i++)
                {
                    var[i] = mEcosystem.ecotopes[i].texAlbedo;
                    var[12 + i] = mEcosystem.ecotopes[i].texNoise;
                }
            }



            //mEcosystem.resetPlantIndex(_tile->lod);
            ecotopeGpuConstants C = *mEcosystem.getConstants();
            C.pixelSize = pixelSize;
            C.tileXY = float2(_tile->x, _tile->y);
            C.padd2 = float2(elevationMap.hgt_offset, elevationMap.hgt_scale);
            C.lowResSize = _tile->size / settings.size / 248.0f;
            C.lowResOffset = float2(_tile->origin.x + halfsize, _tile->origin.z + halfsize) / settings.size;
            C.lod = _tile->lod;
            C.tileIndex = _tile->index;
            pCB->setBlob(&C, 0, sizeof(ecotopeGpuConstants));
            if (C.numEcotopes > 0)
            {
                split.compute_tileEcotopes.dispatch(_renderContext, cs_w, cs_w);
            }
        }

        {
            //FALCOR_PROFILE("passthrough");
            uint cnt = (numQuadsPerTile) >> 8;	  // FIXME - hiesdie oordoen is es dan stadig - dit behoort Compute indoirect te wees  en die regte getal te gebruik
            split.compute_tilePassthrough.Vars()["gConstants"]["parent_index"] = _tile->parent->index;
            split.compute_tilePassthrough.Vars()["gConstants"]["child_index"] = _tile->index;
            split.compute_tilePassthrough.Vars()["gConstants"]["dX"] = _tile->x & 0x1;
            split.compute_tilePassthrough.Vars()["gConstants"]["dY"] = _tile->y & 0x1;
            split.compute_tilePassthrough.dispatch(_renderContext, cnt, 1);
        }

        {
            // Do this early to avoid stalls
            //FALCOR_PROFILE("copy_VERT");
            _renderContext->copyResource(split.vertex_B_texture.get(), split.vertex_clear.get());			// not 100% sure this clear is needed
            _renderContext->copyResource(split.vertex_A_texture.get(), split.vertex_preload.get());
        }

        {
            // compress and copy colour data
            //FALCOR_PROFILE("compress_copy_Albedo");
            //split.compute_bc6h.Vars()->setTexture("gSource", split.tileFbo->getColorTexture(1));
            //split.compute_bc6h.dispatch(_renderContext, cs_w / 4, cs_w / 4);
            //_renderContext->copySubresource(compressed_Albedo_Array.get(), _tile->index, split.bc6h_texture.get(), 0);

            _renderContext->copySubresource(compressed_Albedo_Array.get(), _tile->index, split.tileFbo->getColorTexture(1).get(), 0);
        }

        {
            //FALCOR_PROFILE("normals");
            split.compute_tileNormals.Vars()["gConstants"]["pixSize"] = pixelSize;
            split.compute_tileNormals.dispatch(_renderContext, cs_w, cs_w);
        }

        {
            //FALCOR_PROFILE("verticies");
            float scale = 1.0f;
            if (_tile->lod < 7)  scale = 1.3f;
            if (_tile->lod == 13)  scale = 1.2f;
            if (_tile->lod == 14)  scale = 1.5f;
            if (_tile->lod == 15)  scale = 2.0f;
            if (_tile->lod >= 16)  scale = 3.2f;
            scale *= 2.5;

            split.compute_tileVerticis.Vars()->setSampler("linearSampler", sampler_Clamp);
            split.compute_tileVerticis.Vars()["gConstants"]["constants"] = float4(pixelSize * scale, 0, 0, _tile->index);
            split.compute_tileVerticis.dispatch(_renderContext, cs_w / 2, cs_w / 2);
        }

        {
            //FALCOR_PROFILE("copy normals");
            _renderContext->copySubresource(compressed_Normals_Array.get(), _tile->index, split.normals_texture.get(), 0);
            /*
            PROFILE(normalsBC6H);
            csBC6H_compressor.getVars()->setTexture("gSource", mpNormals);
            csBC6H_compressor.dispatch(pRenderContext, w / 4, h / 4);
            pRenderContext->copySubresource(mpCompressed_Normals_Array.get(), pTile->m_idx, mpCompressed_TMP.get(), 0);
            */
        }

        /*
        // both of these need to chnage to work on child not parent
        {
          FALCOR_PROFILE("cs_tile_Generate");
          uint32_t w_gen = tile_numPixels / tile_cs_ThreadSize_Generate - 1;
          uint32_t h_gen = tile_numPixels / tile_cs_ThreadSize_Generate - 1;
          cs_tile_Generate.getCB()->setVariable<uint>(0, pTile->m_idx);
          cs_tile_Generate.dispatch(pRenderContext, w_gen, h_gen);
        }

        {
          FALCOR_PROFILE("cs_tile_Passthrough");
          uint cnt = (numQuadsPerTile) >> 8;	  // FIXME - hiesdie oordoen is es dan stadig - dit behoort Compute indoirect te wees  en die regte getal te gebruik
          cs_tile_Passthrough.getCB()->setVariable<uint>(0, pTile->m_idx);
          cs_tile_Passthrough.getCB()->setVariable<uint>(sizeof(int), pTile->m_X & 0x1);
          cs_tile_Passthrough.getCB()->setVariable<uint>(sizeof(int) * 2, pTile->m_Y & 0x1);
          cs_tile_Passthrough.dispatch(pRenderContext, cnt, 1);
        }
        */

        // jumpflood algorithm (1+JFA+1) tp build voroinoi diagram ------------------------------------------------------------------------
        // ek weet 32 en 6 loops is goed
        {
            //FALCOR_PROFILE("jumpflood");

            uint step = 4;
            for (int j = 0; j < 3; j++) {
                split.compute_tileJumpFlood.Vars()["gConstants"]["step"] = step;
                if (j & 0x1) {
                    split.compute_tileJumpFlood.Vars()->setTexture("gInVerts", split.vertex_B_texture);
                    split.compute_tileJumpFlood.Vars()->setTexture("gOutVerts", split.vertex_A_texture);
                }
                else {
                    split.compute_tileJumpFlood.Vars()->setTexture("gInVerts", split.vertex_A_texture);
                    split.compute_tileJumpFlood.Vars()->setTexture("gOutVerts", split.vertex_B_texture);

                }

                split.compute_tileJumpFlood.dispatch(_renderContext, cs_w / 2, cs_w / 2);
                step /= 2;
                if (step < 1) step = 1;
            }

            //split.compute_tileJumpFlood.Vars()->setTexture("gInVerts", NULL);
            //split.compute_tileJumpFlood.Vars()->setTexture("gOutVerts", NULL);
        }

        {
            //FALCOR_PROFILE("copy_PBR");
            split.compute_bc6h.Vars()->setTexture("gSource", split.tileFbo->getColorTexture(2));
            split.compute_bc6h.dispatch(_renderContext, cs_w / 4, cs_w / 4);
            _renderContext->copySubresource(compressed_PBR_Array.get(), _tile->index, split.bc6h_texture.get(), 0);
        }


        {
            //FALCOR_PROFILE("delaunay");
            uint32_t firstVertex = _tile->index * numVertPerTile;
            uint32_t zero = 0;
            split.buffer_terrain->getUAVCounter()->setBlob(&zero, 0, sizeof(uint32_t));	// damn, I misuse increment, count in 1's but write in 3's
            split.compute_tileDelaunay.Vars()["gConstants"]["tile_Index"] = _tile->index;
            split.compute_tileDelaunay.dispatch(_renderContext, cs_w / 2, cs_w / 2);
            //mpRenderContext->copyResource(mpDefaultFBO->getColorTexture(0).get(), mpVertsMap.get());
            //mpVertsMap->captureToFile(0, 0, "e:/voroinoi_verts.png");
        }
    }


}



void terrainManager::splitRenderTopdown(quadtree_tile* _pTile, RenderContext* _renderContext)
{
    //FALCOR_PROFILE("renderTopdown");

    // set up the camera -----------------------
    float s = _pTile->size / 2.0f;
    float x = _pTile->origin.x + s;
    float z = _pTile->origin.z + s;
    glm::mat4 V, P, VP;
    V[0] = glm::vec4(1, 0, 0, 0);
    V[1] = glm::vec4(0, 0, 1, 0);
    V[2] = glm::vec4(0, -1, 0, 0);
    V[3] = glm::vec4(-x, z, 0, 1);

    s *= 256.0f / 248.0f;
    P = glm::orthoLH(-s, s, -s, s, -10000.0f, 10000.0f);

    //viewproj = view * proj;
    VP = P * V;    //??? order

    rmcv::mat4 view, proj, viewproj;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            view[j][i] = V[j][i];
            proj[j][i] = P[j][i];
            viewproj[j][i] = VP[j][i];
        }
    }


    {
        split.shader_meshTerrafector.State()->setFbo(split.tileFbo);
        split.shader_meshTerrafector.State()->setRasterizerState(split.rasterstateSplines);
        split.shader_meshTerrafector.State()->setBlendState(split.blendstateRoadsCombined);
        split.shader_meshTerrafector.State()->setDepthStencilState(split.depthstateAll);

        split.shader_meshTerrafector.Vars()["gConstantBuffer"]["viewproj"] = viewproj;
        split.shader_meshTerrafector.Vars()["gConstantBuffer"]["overlayAlpha"] = 1.0f;

        auto& block = split.shader_meshTerrafector.Vars()->getParameterBlock("gmyTextures");
        ShaderVar& var = block->findMember("T");        // FIXME pre get
        terrafectorEditorMaterial::static_materials.setTextures(var);
    }

    if (bSplineAsTerrafector)           // Now render the roadNetwork
    {
        split.shader_splineTerrafector.State()->setFbo(split.tileFbo);
        split.shader_splineTerrafector.State()->setRasterizerState(split.rasterstateSplines);
        split.shader_splineTerrafector.State()->setDepthStencilState(split.depthstateAll);
        split.shader_splineTerrafector.State()->setBlendState(split.blendstateRoadsCombined);

        split.shader_splineTerrafector.Vars()["gConstantBuffer"]["viewproj"] = viewproj;
        split.shader_splineTerrafector.Vars()["gConstantBuffer"]["startOffset"] = 0;
        split.shader_splineTerrafector.Vars()->setBuffer("materials", terrafectorEditorMaterial::static_materials.sb_Terrafector_Materials);

        auto& block = split.shader_splineTerrafector.Vars()->getParameterBlock("gmyTextures");
        ShaderVar& var = block->findMember("T");        // FIXME pre get
        terrafectorEditorMaterial::static_materials.setTextures(var);

        split.shader_splineTerrafector.Vars()->setBuffer("splineData", splines.bezierData);     // not created yet
    }


    // Mesh bake low
    if (gis_overlay.bakeBakeOnlyData)
    {
        if (_pTile->lod >= 4)
        {
            gpuTileTerrafector* tile = terrafectorSystem::loadCombine_LOD4_bakeLow.getTile((_pTile->y >> (_pTile->lod - 4)) * 16 + (_pTile->x >> (_pTile->lod - 4)));
            if (tile)
            {
                if (tile->numBlocks > 0)
                {
                    split.shader_meshTerrafector.Vars()->setBuffer("vertexData", tile->vertex);
                    split.shader_meshTerrafector.Vars()->setBuffer("indexData", tile->index);
                    split.shader_meshTerrafector.drawInstanced(_renderContext, 128 * 3, tile->numBlocks);
                }
            }
        }

        if (bSplineAsTerrafector)           // Now render the roadNetwork
        {
            split.shader_splineTerrafector.Vars()->setBuffer("indexData", splines.indexDataBakeOnly);
            split.shader_splineTerrafector.drawIndexedInstanced(_renderContext, 64 * 6, splines.numStaticSplinesBakeOnlyIndex);
        }

        if (_pTile->lod >= 4)
        {
            gpuTileTerrafector* tile = terrafectorSystem::loadCombine_LOD4_bakeHigh.getTile((_pTile->y >> (_pTile->lod - 4)) * 16 + (_pTile->x >> (_pTile->lod - 4)));
            if (tile)
            {
                if (tile->numBlocks > 0)
                {
                    split.shader_meshTerrafector.Vars()->setBuffer("vertexData", tile->vertex);
                    split.shader_meshTerrafector.Vars()->setBuffer("indexData", tile->index);
                    split.shader_meshTerrafector.drawInstanced(_renderContext, 128 * 3, tile->numBlocks);
                }
            }
        }
    }






    if (_pTile->lod >= 6)
    {
        gpuTileTerrafector* tile = terrafectorSystem::loadCombine_LOD6.getTile((_pTile->y >> (_pTile->lod - 6)) * 64 + (_pTile->x >> (_pTile->lod - 6)));
        if (tile)
        {
            if (tile->numBlocks > 0)
            {
                split.shader_meshTerrafector.Vars()->setBuffer("vertexData", tile->vertex);
                split.shader_meshTerrafector.Vars()->setBuffer("indexData", tile->index);
                split.shader_meshTerrafector.drawInstanced(_renderContext, 128 * 3, tile->numBlocks);
            }
        }
    }
    else if (_pTile->lod >= 4)
    {
        gpuTileTerrafector* tile = terrafectorSystem::loadCombine_LOD4.getTile((_pTile->y >> (_pTile->lod - 4)) * 16 + (_pTile->x >> (_pTile->lod - 4)));
        if (tile)
        {
            if (tile->numBlocks > 0)
            {
                split.shader_meshTerrafector.Vars()->setBuffer("vertexData", tile->vertex);
                split.shader_meshTerrafector.Vars()->setBuffer("indexData", tile->index);
                split.shader_meshTerrafector.drawInstanced(_renderContext, 128 * 3, tile->numBlocks);
            }
        }
    }
    else if (_pTile->lod >= 2)
    {
        gpuTileTerrafector* tile = terrafectorSystem::loadCombine_LOD2.getTile((_pTile->y >> (_pTile->lod - 2)) * 4 + (_pTile->x >> (_pTile->lod - 2)));
        if (tile)
        {
            if (tile->numBlocks > 0)
            {
                split.shader_meshTerrafector.Vars()->setBuffer("vertexData", tile->vertex);
                split.shader_meshTerrafector.Vars()->setBuffer("indexData", tile->index);
                split.shader_meshTerrafector.drawInstanced(_renderContext, 128 * 3, tile->numBlocks);
            }
        }
    }


    // OVER:AY ######################################################
    if (gis_overlay.terrafectorOverlayStrength > 0)
        if (_pTile->lod >= 4)
        {
            gpuTileTerrafector* tile = terrafectorSystem::loadCombine_LOD4_overlay.getTile((_pTile->y >> (_pTile->lod - 4)) * 16 + (_pTile->x >> (_pTile->lod - 4)));
            if (tile)
            {
                if (tile->numBlocks > 0)
                {
                    split.shader_meshTerrafector.Vars()["gConstantBuffer"]["overlayAlpha"] = gis_overlay.terrafectorOverlayStrength;
                    split.shader_meshTerrafector.Vars()->setBuffer("vertexData", tile->vertex);
                    split.shader_meshTerrafector.Vars()->setBuffer("indexData", tile->index);
                    split.shader_meshTerrafector.drawInstanced(_renderContext, 128 * 3, tile->numBlocks);
                }
            }
        }


    //??? should probably be in the roadnetwork code, but look at the optimize step first
    if (bSplineAsTerrafector)           // Now render the roadNetwork
    {
        if (_pTile->lod >= 8)
        {
            quadtree_tile* P8 = _pTile;
            while (P8->lod > 8) P8 = P8->parent;
            split.shader_splineTerrafector.Vars()["gConstantBuffer"]["startOffset"] = splines.startOffset_LOD8[P8->y][P8->x];
            split.shader_splineTerrafector.Vars()->setBuffer("indexData", splines.indexData_LOD8);
            split.shader_splineTerrafector.drawIndexedInstanced(_renderContext, 64 * 6, splines.numIndex_LOD8[P8->y][P8->x]);
        }
        else if (_pTile->lod >= 6)
        {
            quadtree_tile* P6 = _pTile;
            while (P6->lod > 6) P6 = P6->parent;
            split.shader_splineTerrafector.Vars()["gConstantBuffer"]["startOffset"] = splines.startOffset_LOD6[P6->y][P6->x];
            split.shader_splineTerrafector.Vars()->setBuffer("indexData", splines.indexData_LOD6);
            split.shader_splineTerrafector.drawIndexedInstanced(_renderContext, 64 * 6, splines.numIndex_LOD6[P6->y][P6->x]);
        }
        else if (_pTile->lod >= 4)
        {
            quadtree_tile* P4 = _pTile;
            while (P4->lod > 4) P4 = P4->parent;
            split.shader_splineTerrafector.Vars()["gConstantBuffer"]["startOffset"] = splines.startOffset_LOD4[P4->y][P4->x];
            split.shader_splineTerrafector.Vars()->setBuffer("indexData", splines.indexData_LOD4);
            split.shader_splineTerrafector.drawIndexedInstanced(_renderContext, 64 * 6, splines.numIndex_LOD4[P4->y][P4->x]);
        }
    }


    // STAMPS #################################################################################################################
    if (_pTile->lod >= 7)
    {
        gpuTileTerrafector* tile = terrafectorSystem::loadCombine_LOD7_stamps.getTile((_pTile->y >> (_pTile->lod - 7)) * 128 + (_pTile->x >> (_pTile->lod - 7)));
        if (tile)
        {
            if (tile->numBlocks > 0)
            {
                split.shader_meshTerrafector.Vars()->setBuffer("vertexData", tile->vertex);
                split.shader_meshTerrafector.Vars()->setBuffer("indexData", tile->index);
                split.shader_meshTerrafector.drawInstanced(_renderContext, 128 * 3, tile->numBlocks);
            }
        }
    }



    // TOP #################################################################################################################
    if (_pTile->lod >= 6)
    {
        gpuTileTerrafector* tile = terrafectorSystem::loadCombine_LOD6_top.getTile((_pTile->y >> (_pTile->lod - 6)) * 64 + (_pTile->x >> (_pTile->lod - 6)));
        if (tile)
        {
            if (tile->numBlocks > 0)
            {
                split.shader_meshTerrafector.Vars()->setBuffer("vertexData", tile->vertex);
                split.shader_meshTerrafector.Vars()->setBuffer("indexData", tile->index);
                split.shader_meshTerrafector.drawInstanced(_renderContext, 128 * 3, tile->numBlocks);
            }
        }
    }
    else if (_pTile->lod >= 4)
    {
        gpuTileTerrafector* tile = terrafectorSystem::loadCombine_LOD4_top.getTile((_pTile->y >> (_pTile->lod - 4)) * 16 + (_pTile->x >> (_pTile->lod - 4)));
        if (tile)
        {
            if (tile->numBlocks > 0)
            {
                split.shader_meshTerrafector.Vars()->setBuffer("vertexData", tile->vertex);
                split.shader_meshTerrafector.Vars()->setBuffer("indexData", tile->index);
                split.shader_meshTerrafector.drawInstanced(_renderContext, 128 * 3, tile->numBlocks);
            }
        }
    }
}



void terrainManager::shadowSetup(shadowMap& _shadow)
{
    ShadowMap = _shadow;
}

void terrainManager::shadowRenderFar()
{
}

void terrainManager::shadowRenderNear()
{
}

void terrainManager::shadowRenderSoft()
{
}

void terrainManager::shadowRender(RenderContext* pRenderContext)
{
    renderContext = pRenderContext;
}





void terrainManager::onFrameRender(RenderContext* _renderContext, const Fbo::SharedPtr& _fbo, Camera::SharedPtr _camera, GraphicsState::Viewport _viewport)
{
    newTerrainBuilder._renderContext = _renderContext;

    rmcv::mat4 view_ribbon, proj_ribbon, viewproj_ribbon;
    rmcv::mat4 view, proj, viewproj;
    {
        {
            _camera->setUpVector(float3(0, 1, 0));
        }




        if ((terrainMode == _terrainMode::glider) && requestParaPack == false)
            //if ((terrainMode == 4) && AirSim.changed)
        {
            //FALCOR_PROFILE("setBlob");


            //cfd.originRequest = float3(3185, 900, -14377);  //A
            //cfd.originRequest = float3(3190, 800, -15900);  //A_A  test for air video in the north
            //cfd.originRequest = float3(-5000, 900, -14302);  //B

            /*
            //cfd.originRequest = paraRuntime.pilotPos();
            cfd.originRequest = float3(-1425 + 2800, 425 + 140 + 800, 12533);
            //cfd.originRequest = float3(1184, 422 + 20, 14829);
            cfd.originRequest = float3(9224 + 580, 1458, 4291);     // my smoke in teh mifddle
            //cfd.originRequest = float3(11500, 1818, 11800);     // south better mounrains


            cfd.originRequest = float3(12500, 1362 + 300, 4701);
            cfd.originRequest = float3(8753, 1257 + 300, 6619);
            //cfd.originRequest = float3(15033, 1498 + 300, 7446);



            //cfd.originRequest = float3(1852, 1500, 10910);
            */

            //cfd.velocityRequets[0] = paraRuntime.pilotPos();
            //cfd.velocityRequets[1] = paraRuntime.wingPos(0);
            //cfd.velocityRequets[2] = paraRuntime.wingPos(25);
            //cfd.velocityRequets[3] = paraRuntime.wingPos(49);

            
            //if (!useFreeCamWhileGliding)
            //{
            //    _camera->setFarPlane(40000);
            //    _camera->setUpVector(float3(0, 1, 0));
            //    //_camera->setUpVector(paraRuntime.camUp);
            //    _camera->setTarget(paraEyeLocal + paraRuntime.camDir * 100.f);
            //    _camera->setPosition(paraEyeLocal);

            //    view_ribbon = _camera->getViewMatrix().getTranspose();
            //    proj_ribbon = _camera->getProjMatrix().getTranspose();
            //    viewproj_ribbon = _camera->getViewProjMatrix().getTranspose();


            //    _camera->setTarget(paraCamPos + paraRuntime.camDir * 100.f);
            //    _camera->setPosition(paraCamPos);
            //}



            //paraRuntime.pack_canopy();
            //paraRuntime.pack_lines();
            //paraRuntime.pack_feedback();
            //paraRuntime.pack();
            /*
            paraRuntime.changed = false;
            ribbonInstanceNumber = 1;
            bufferidx = (bufferidx + 1) % 2;
            ribbonData[bufferidx]->setBlob(paraRuntime.packedRibbons, 0, paraRuntime.ribbonCount * sizeof(unsigned int) * 6);
            numLoadedRibbons = paraRuntime.ribbonCount;

            //ribbonData->setBlob(AirSim.packedRibbons, 0, AirSim.ribbonCount * sizeof(unsigned int) * 6);

            ribbonShader.Vars()->setBuffer("instanceBuffer", ribbonData[bufferidx]);

            ribbonShader.State()->setFbo(_fbo);
            ribbonShader.State()->setViewport(0, _viewport, true);

            ribbonShader.Vars()["gConstantBuffer"]["fakeShadow"] = 4;
            ribbonShader.Vars()["gConstantBuffer"]["objectScale"] = float3(ribbonVertex::objectScale, ribbonVertex::objectScale, ribbonVertex::objectScale);
            ribbonShader.Vars()["gConstantBuffer"]["objectOffset"] = ribbonVertex::objectOffset;
            ribbonShader.Vars()["gConstantBuffer"]["radiusScale"] = ribbonVertex::radiusScale;

            ribbonShader.State()->setRasterizerState(split.rasterstateSplines);
            ribbonShader.State()->setBlendState(split.blendstateSplines);


            gliderwingData[bufferidx]->setBlob(paraRuntime.packedWing, 0, paraRuntime.wingVertexCount * sizeof(_gliderwingVertex));
            wingloadedCnt = paraRuntime.wingVertexCount;
            gliderwingShader.Vars()->setBuffer("vertexBuffer", gliderwingData[bufferidx]);

            requestParaPack = true;
            */
        }


        {
            view = _camera->getViewMatrix().getTranspose();
            proj = _camera->getProjMatrix().getTranspose();
            viewproj = _camera->getViewProjMatrix().getTranspose();
        }
    }

    if (terrainMode == _terrainMode::vegetation)
    {
        {
            FALCOR_PROFILE("skydome - veg");

            triangleShader.State()->setFbo(_fbo);
            triangleShader.State()->setViewport(0, _viewport, true);
            triangleShader.Vars()["gConstantBuffer"]["viewproj"] = viewproj;
            triangleShader.Vars()["gConstantBuffer"]["eye"] = _camera->getPosition();
            triangleShader.Vars()["gConstantBuffer"]["useSkyDome"] = 0;
            triangleShader.State()->setRasterizerState(split.rasterstateSplines);
            triangleShader.State()->setBlendState(split.blendstateSplines);
            triangleShader.drawInstanced(_renderContext, 36, 1);
        }

        rmcv::mat4 clipPlanes;
        //rmcv::mat4 view, clip;
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                clipPlanes[j][i] = cameraViews[CameraType_Main_Center].frustumMatrix[j][i];
            }
        }

        float fovscale = glm::length(cameraViews[CameraType_Main_Center].proj[1]);
        float m_halfAngle_to_Pixels = cameraViews[CameraType_Main_Center].resolution * fovscale / 2.f;


        plants_Root.render(_renderContext, _fbo, _viewport, viewproj, _camera->getPosition(), view, clipPlanes, m_halfAngle_to_Pixels);



        return;
    }

    if (terrainMode == _terrainMode::glider)
    {

        {
            FALCOR_PROFILE("skydome");

            triangleShader.State()->setFbo(_fbo);
            triangleShader.State()->setViewport(0, _viewport, true);
            triangleShader.Vars()["gConstantBuffer"]["viewproj"] = viewproj;
            triangleShader.Vars()["gConstantBuffer"]["eye"] = _camera->getPosition();
            triangleShader.Vars()["gConstantBuffer"]["useSkyDome"] = 0;
            triangleShader.State()->setRasterizerState(split.rasterstateSplines);
            triangleShader.State()->setBlendState(split.blendstateSplines);
            triangleShader.drawInstanced(_renderContext, 36, 1);
            //triangleShader.renderIndirect(_renderContext, split.drawArgs_clippedloddedplants);
        }





        if (_plantMaterial::static_materials_veg.modified)
        {
            auto& block = ribbonShader.Vars()->getParameterBlock("textures");
            ShaderVar& var = block->findMember("T");        // FIXME pre get
            _plantMaterial::static_materials_veg.setTextures(var);
            _plantMaterial::static_materials_veg.modified = false;
        }

        if (_plantMaterial::static_materials_veg.modifiedData)
        {
            _plantMaterial::static_materials_veg.rebuildStructuredBuffer();
            _plantMaterial::static_materials_veg.modifiedData = false;
        }


        if (!useFreeCamWhileGliding && (terrainMode == _terrainMode::glider) && numLoadedRibbons > 1)
            //if ((terrainMode == 4) && AirSim.ribbonCount > 1)
        {
            ribbonShader.Vars()["gConstantBuffer"]["viewproj"] = viewproj_ribbon;
            ribbonShader.Vars()["gConstantBuffer"]["eyePos"] = paraEyeLocal;// _camera->getPosition();

            static float spacing = 1.0f;

            ribbonShader.Vars()["gConstantBuffer"]["offset"] = float3(0, 0, 0);
            ribbonShader.Vars()["gConstantBuffer"]["repeatScale"] = spacing;
            ribbonShader.Vars()["gConstantBuffer"]["numSide"] = 1;

            ribbonShader.Vars()["gConstantBuffer"]["objectScale"] = float3(ribbonVertex::objectScale, ribbonVertex::objectScale, ribbonVertex::objectScale);
            ribbonShader.Vars()["gConstantBuffer"]["objectOffset"] = ribbonVertex::objectOffset;
            ribbonShader.Vars()["gConstantBuffer"]["radiusScale"] = ribbonVertex::radiusScale;


            ribbonShader.Vars()["gConstantBuffer"]["ROOT"] = float3(0, 0, 0);// paraRuntime.ROOT;

            static float time = 0.0f;
            time += 0.01f;  // FIXME I NEED A Timer
            ribbonShader.Vars()["gConstantBuffer"]["time"] = time;





            // ribbonShader.drawInstanced(_renderContext, paraRuntime.ribbonCount, 1);
            ribbonShader.drawInstanced(_renderContext, numLoadedRibbons, 1);
            //ribbonShader.drawInstanced(_renderContext, AirSim.ribbonCount, 1);
        }


        //return;
    }

    {
        FALCOR_PROFILE("PLANTS_clip_lod");
        rmcv::mat4 view, clip;
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                view[j][i] = cameraViews[1].view[j][i];
                clip[j][i] = cameraViews[1].frustumMatrix[j][i];
            }
        }

        float fovscale = glm::length(cameraViews[CameraType_Main_Center].proj[1]);
        float m_halfAngle_to_Pixels = cameraViews[CameraType_Main_Center].resolution * fovscale / 2.f;

        split.compute_clipLodAnimatePlants.Vars()["gConstantBuffer"]["view"] = view;
        split.compute_clipLodAnimatePlants.Vars()["gConstantBuffer"]["clip"] = clip;
        split.compute_clipLodAnimatePlants.Vars()["gConstantBuffer"]["halfAngle_to_Pixels"] = m_halfAngle_to_Pixels;
        split.compute_clipLodAnimatePlants.Vars()->setBuffer("tileLookup", split.buffer_lookup_plants[CameraType_Main_Center]);
        split.compute_clipLodAnimatePlants.dispatchIndirect(_renderContext, split.dispatchArgs_plants.get(), 0);//225
    }


    {
        FALCOR_PROFILE("terrainManager");


        terrainShader.State()->setFbo(_fbo);
        terrainShader.State()->setViewport(0, _viewport, true);
        terrainShader.Vars()["gConstantBuffer"]["view"] = view;
        terrainShader.Vars()["gConstantBuffer"]["viewproj"] = viewproj;
        terrainShader.Vars()["gConstantBuffer"]["eye"] = _camera->getPosition();
        terrainShader.Vars()->setBuffer("tileLookup", split.buffer_lookup_terrain[CameraType_Main_Center]);      // FIXME2025 set the ccorrect view not 0
        terrainShader.renderIndirect(_renderContext, split.drawArgs_tiles, nullptr, CameraType_Main_Center, 1);
    }
    {
        /*
        FALCOR_PROFILE("buildings");

        rappersvilleShader.State()->setFbo(_fbo);
        rappersvilleShader.State()->setViewport(0, _viewport, true);
        rappersvilleShader.Vars()["PerFrameCB"]["view"] = view;
        rappersvilleShader.Vars()["PerFrameCB"]["viewproj"] = viewproj;
        rappersvilleShader.Vars()["PerFrameCB"]["eye"] = _camera->getPosition();
        rappersvilleShader.drawInstanced(_renderContext, 3, numrapperstri);
        */
    }

    {
        terrainSpiteShader.Vars()["gConstantBuffer"]["alpha_pass"] = 0;
        terrainSpiteShader.Vars()->setTexture("gEnv", vegetation.envTexture);

        // FIXME need a way to do only on change
        auto& blockBB = terrainSpiteShader.Vars()->getParameterBlock("textures");
        ShaderVar varBBTextures = blockBB->findMember("T");
        _plantMaterial::static_materials_veg.setTextures(varBBTextures);

    }
    {
        FALCOR_PROFILE("billboards");
        terrainSpiteShader.State()->setFbo(_fbo);
        terrainSpiteShader.State()->setViewport(0, _viewport, true);
        terrainSpiteShader.Vars()["gConstantBuffer"]["viewproj"] = viewproj;
        terrainSpiteShader.Vars()["gConstantBuffer"]["eye"] = _camera->getPosition();

        glm::vec3 D = glm::vec3(view[2][0], view[2][1], -view[2][2]);
        //D = paraRuntime.camDir;
        glm::vec3 R = glm::normalize(glm::cross(glm::vec3(0, 1, 0), D));
        terrainSpiteShader.Vars()["gConstantBuffer"]["right"] = R;

        terrainSpiteShader.Vars()->setBuffer("tileLookup", split.buffer_lookup_quads[CameraType_Main_Center]);

        terrainSpiteShader.renderIndirect(_renderContext, split.drawArgs_quads, nullptr, CameraType_Main_Center, 1);

        //terrainSpiteShader.Vars()["gConstantBuffer"]["alpha_pass"] = 1;
        //terrainSpiteShader.renderIndirect(_renderContext, split.drawArgs_quads);
    }
    {
        rmcv::mat4 clip;
        //rmcv::mat4 view, clip;
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                clip[j][i] = cameraViews[CameraType_Main_Center].frustumMatrix[j][i];
            }
        }
        float fovscale = glm::length(cameraViews[CameraType_Main_Center].proj[1]);
        float m_halfAngle_to_Pixels = cameraViews[CameraType_Main_Center].resolution * fovscale / 2.f;
        plants_Root.render(_renderContext, _fbo, _viewport, viewproj, _camera->getPosition(), view, clip, m_halfAngle_to_Pixels, true);
    }



    {
        FALCOR_PROFILE("skydome");

        triangleShader.State()->setFbo(_fbo);
        triangleShader.State()->setViewport(0, _viewport, true);
        triangleShader.Vars()["gConstantBuffer"]["viewproj"] = viewproj;
        triangleShader.Vars()["gConstantBuffer"]["eye"] = _camera->getPosition();
        triangleShader.Vars()["gConstantBuffer"]["useSkyDome"] = 0;
        triangleShader.State()->setRasterizerState(split.rasterstateSplines);
        triangleShader.State()->setBlendState(split.blendstateSplines);
        triangleShader.drawInstanced(_renderContext, 36, 1);
        //triangleShader.renderIndirect(_renderContext, split.drawArgs_clippedloddedplants);
    }


    /*
    if (cfd.recordingCFD)
    {
        cfd.recordingCFD = cfd.clipmap.import_V(settings.dirRoot + "/cfd/dump/east3ms__" + std::to_string(cfd.cfd_play_k) + "sec");
        cfd.clipmap.streamlines(float3(-2800, 450, 12500), cfd.flowlines.data(), float3(1, 0, 0));
        thermalsData->setBlob(cfd.flowlines.data(), 0, numThermals * 100 * sizeof(float4));
        cfd.cfd_play_k++;
    } */

    //if (cfd.clipmap.showStreamlines)
    //{
    //    FALCOR_PROFILE("CFD");

    //    if (cfd.newFlowLines)
    //    {
    //        //FALCOR_PROFILE("CFD update");
    //        thermalsData->setBlob(cfd.flowlines.data(), 0, numThermals * 100 * sizeof(float4));
    //        cfd.newFlowLines = false;
    //    }


    //    thermalsShader.State()->setFbo(_fbo);
    //    thermalsShader.State()->setViewport(0, _viewport, true);
    //    thermalsShader.Vars()["gConstantBuffer"]["viewproj"] = viewproj;
    //    thermalsShader.Vars()["gConstantBuffer"]["eyePos"] = _camera->getPosition();
    //    thermalsShader.State()->setRasterizerState(rasterstateGliderWing);
    //    thermalsShader.drawInstanced(_renderContext, 100, numThermals);
    //}






    {
        /*
        FALCOR_PROFILE("glider wing");
        gliderwingShader.State()->setFbo(_fbo);
        gliderwingShader.State()->setViewport(0, _viewport, true);
        gliderwingShader.Vars()["PerFrameCB"]["viewproj"] = viewproj;
        gliderwingShader.Vars()["PerFrameCB"]["eye"] = _camera->getPosition();
        gliderwingShader.State()->setRasterizerState(rasterstateGliderWing);
        gliderwingShader.drawInstanced(_renderContext, wingloadedCnt, 1);
        */
    }


    {

        //FALCOR_PROFILE("ribbonShader");

        ribbonShader.State()->setFbo(_fbo);
        ribbonShader.State()->setViewport(0, _viewport, true);
        ribbonShader.Vars()["gConstantBuffer"]["viewproj"] = viewproj;
        ribbonShader.Vars()["gConstantBuffer"]["eyePos"] = _camera->getPosition();

        ribbonShader.State()->setRasterizerState(split.rasterstateSplines);
        ribbonShader.State()->setBlendState(split.blendstateSplines);

        //ribbonShader.drawInstanced(_renderContext, 128, 10024);
        //ribbonShader.renderIndirect(_renderContext, split.drawArgs_clippedloddedplants);

        //buffer_lookup_plants


    }

    if (terrainMode == _terrainMode::terrafector)
    {
        split.shader_spline3D.State()->setFbo(_fbo);
        split.shader_spline3D.State()->setRasterizerState(split.rasterstateSplines);
        split.shader_spline3D.State()->setBlendState(split.blendstateSplines);
        split.shader_spline3D.State()->setDepthStencilState(split.depthstateAll);
        split.shader_spline3D.State()->setViewport(0, _viewport, true);

        split.shader_spline3D.Vars()["gConstantBuffer"]["viewproj"] = viewproj;
        split.shader_spline3D.Vars()["gConstantBuffer"]["alpha"] = gis_overlay.splineOverlayStrength;

        split.shader_spline3D.Vars()->setBuffer("materials", terrafectorEditorMaterial::static_materials.sb_Terrafector_Materials);

        auto& block = split.shader_spline3D.Vars()->getParameterBlock("gmyTextures");
        ShaderVar& var = block->findMember("T");        // FIXME pre get
        terrafectorEditorMaterial::static_materials.setTextures(var);

        split.shader_spline3D.Vars()->setBuffer("splineData", splines.dynamic_bezierData);
        split.shader_spline3D.Vars()->setBuffer("indexData", splines.dynamic_indexData);
        split.shader_spline3D.drawIndexedInstanced(_renderContext, 64 * 6, splines.numDynamicStampIndex);
    }

    if ((splines.numStaticSplines || splines.numDynamicSplines) && showRoadSpline && !bSplineAsTerrafector)
    {
        //FALCOR_PROFILE("splines");

        split.shader_spline3D.State()->setFbo(_fbo);
        split.shader_spline3D.State()->setRasterizerState(split.rasterstateSplines);
        split.shader_spline3D.State()->setBlendState(split.blendstateSplines);
        split.shader_spline3D.State()->setDepthStencilState(split.depthstateAll);
        split.shader_spline3D.State()->setViewport(0, _viewport, true);

        //split.shader_spline3D.Vars()["gConstantBuffer"]["view"] = view;
        //split.shader_spline3D.Vars()["gConstantBuffer"]["proj"] = proj;
        split.shader_spline3D.Vars()["gConstantBuffer"]["viewproj"] = viewproj;
        split.shader_spline3D.Vars()["gConstantBuffer"]["alpha"] = gis_overlay.splineOverlayStrength;

        split.shader_spline3D.Vars()->setBuffer("materials", terrafectorEditorMaterial::static_materials.sb_Terrafector_Materials);

        auto& block = split.shader_spline3D.Vars()->getParameterBlock("gmyTextures");
        ShaderVar& var = block->findMember("T");        // FIXME pre get
        terrafectorEditorMaterial::static_materials.setTextures(var);

        if (splines.numDynamicSplines > 0)
        {
            split.shader_spline3D.Vars()->setBuffer("splineData", splines.dynamic_bezierData);
            split.shader_spline3D.Vars()->setBuffer("indexData", splines.dynamic_indexData);
            split.shader_spline3D.drawIndexedInstanced(_renderContext, 64 * 6, splines.numDynamicSplinesIndex);
        }
        else if (splines.numStaticSplines > 0)
        {
            split.shader_spline3D.Vars()->setBuffer("splineData", splines.bezierData);
            split.shader_spline3D.Vars()->setBuffer("indexData", splines.indexData);
            split.shader_spline3D.drawIndexedInstanced(_renderContext, 64 * 6, splines.numStaticSplinesIndex);
        }
        /*
        if (terrainMode == _terrainMode::terrafector)
        {
            // render static stamps
            split.shader_spline3D.Vars()->setBuffer("splineData", splines.bezierData);
            split.shader_spline3D.Vars()->setBuffer("indexData", splines.indexData);
            split.shader_spline3D.drawIndexedInstanced(_renderContext, 64 * 6, splines.numStaticSplinesIndex);
        }
        */

    }

    {
        //FALCOR_PROFILE("sprites");
        if (showRoadOverlay) {
            mSpriteRenderer.onRender(_camera, _renderContext, _fbo, _viewport, nullptr);
        }

    }

    {
        FALCOR_PROFILE("terrain_under_mouse");
        compute_TerrainUnderMouse.Vars()["gConstants"]["mousePos"] = mousePosition;
        compute_TerrainUnderMouse.Vars()["gConstants"]["mouseDir"] = mouseDirection;
        compute_TerrainUnderMouse.Vars()["gConstants"]["mouseCoords"] = mouseCoord;
        compute_TerrainUnderMouse.Vars()->setTexture("gHDRBackbuffer", _fbo->getColorTexture(0));

        compute_TerrainUnderMouse.dispatch(_renderContext, 1, 1);

        _renderContext->copyResource(split.buffer_feedback_read.get(), split.buffer_feedback.get());

        const uint8_t* pData = (uint8_t*)split.buffer_feedback_read->map(Buffer::MapType::Read);
        std::memcpy(&split.feedback, pData, sizeof(GC_feedback));
        split.buffer_feedback_read->unmap();

        mouse.hit = false;
        if (split.feedback.tum_idx > 0)
        {
            if (ImGui::IsMouseDown(1))  // pan
            {
            }
            else if (ImGui::IsMouseDown(2)) // orbit
            {
            }
            else
            {
                mouse.hit = true;
                mouse.terrain = split.feedback.tum_Position;
                mouse.cameraHeight = split.feedback.heightUnderCamera;
                mouse.toGround = mouse.terrain - _camera->getPosition();
                mouse.mouseToHeightRatio = glm::length(mouse.toGround) / (_camera->getPosition().y - mouse.cameraHeight);
            }

            if (!ImGui::IsMouseDown(1)) {
                mouse.pan = mouse.terrain;
                mouse.panDistance = glm::length(mouse.toGround);
            }
            if (!ImGui::IsMouseDown(2)) mouse.orbit = mouse.terrain;

        }
    }

    if (debug)
    {
        glm::vec4 srcRect = glm::vec4(0, 0, tile_numPixels, tile_numPixels);
        glm::vec4 dstRect;

        for (int i = 0; i < 8; i++)
        {
            dstRect = glm::vec4(250 + i * 150, 60, 250 + i * 150 + 128, 60 + 128);
            _renderContext->blit(split.tileFbo->getColorTexture(i)->getSRV(0, 1, 0, 1), _fbo->getColorTexture(0)->getRTV(), srcRect, dstRect, Sampler::Filter::Linear);
        }

        dstRect = glm::vec4(250 + 8 * 150, 60, 250 + 8 * 150 + tile_numPixels * 2, 60 + tile_numPixels * 2);
        _renderContext->blit(split.debug_texture->getSRV(0, 1, 0, 1), _fbo->getColorTexture(0)->getRTV(), srcRect, dstRect, Sampler::Filter::Point);


        //dstRect = vec4(512, 612, 1024, 1124);
        //pRenderContext->blit(mpCompressed_Normals->getSRV(0, 1, 0, 1), _fbo->getColorTexture(0)->getRTV(), srcRect, dstRect, Sampler::Filter::Linear);
    }

    /*
    if (debug)
    {
        glm::vec4 srcRect = glm::vec4(0, 0, tile_numPixels, tile_numPixels);
        glm::vec4 dstRect;

        for (int i = 0; i < 8; i++)
        {
            dstRect = glm::vec4(250 + i * 150, 60, 250 + i * 150 + 128, 60 + 128);
            _renderContext->blit(split.tileFbo->getColorTexture(i)->getSRV(0, 1, 0, 1), _fbo->getColorTexture(0)->getRTV(), srcRect, dstRect, Sampler::Filter::Linear);
        }

        dstRect = glm::vec4(250 + 8 * 150, 60, 250 + 8 * 150 + tile_numPixels * 2, 60 + tile_numPixels * 2);
        _renderContext->blit(split.debug_texture->getSRV(0, 1, 0, 1), _fbo->getColorTexture(0)->getRTV(), srcRect, dstRect, Sampler::Filter::Point);


        //dstRect = vec4(512, 612, 1024, 1124);
        //pRenderContext->blit(mpCompressed_Normals->getSRV(0, 1, 0, 1), _fbo->getColorTexture(0)->getRTV(), srcRect, dstRect, Sampler::Filter::Linear);
        char debugTxt[1024];
        TextRenderer::setColor(float3(1, 1, 1));
        sprintf(debugTxt, "%d of %d tiles used", (int)m_used.size(), (int)m_tiles.size());
        TextRenderer::render(_renderContext, debugTxt, _fbo, { 100, 300 });

        sprintf(debugTxt, "%d tiles / triangles  %d  {%d}max", split.feedback.numTerrainTiles, split.feedback.numTerrainVerts, split.feedback.maxTriangles);
        TextRenderer::render(_renderContext, debugTxt, _fbo, { 100, 320 });

        sprintf(debugTxt, "%d tiles with quads    %d total quads, %d blocks, {%d}max", split.feedback.numQuadTiles, split.feedback.numQuads, split.feedback.numQuadBlocks, split.feedback.maxQuads);
        TextRenderer::render(_renderContext, debugTxt, _fbo, { 100, 340 });

        sprintf(debugTxt, "%d tiles with plants    %d total plants (%d)  {%d}max", split.feedback.numPlantTiles, split.feedback.numPlants, split.feedback.numPlantBlocks, split.feedback.maxPlants);
        TextRenderer::render(_renderContext, debugTxt, _fbo, { 100, 360 });

        sprintf(debugTxt, "%d clipped plants", split.feedback.numPostClippedPlants);
        TextRenderer::render(_renderContext, debugTxt, _fbo, { 100, 380 });



        for (uint L = 0; L < 18; L++)
        {
            float numBB = (float)split.feedback.numSprite[L] / 1000000.f;
            sprintf(debugTxt, "%3d %5d %5.2f %5d %5d", L, split.feedback.numTiles[L], numBB, split.feedback.numPlantsLOD[L], split.feedback.numTris[L]);
            TextRenderer::render(_renderContext, debugTxt, _fbo, { 100, 450 + L * 20 });
        }
    }
    */
}



void terrainManager::bake_start(bool _toMAX)
{
    bakeToMax = _toMAX;
    char name[256];
    sprintf(name, "%s/bake/EVO/tiles.txt", settings.dirRoot.c_str());
    bake.txt_file = fopen(name, "w");
    bake.tileInfoForBinaryExport.clear();
    bake.inProgress = true;

    elevationMap oneTile;
    uint total = (uint)elevationTileHashmap.size();
    std::map<uint32_t, heightMap>::iterator itt;

    for (itt = elevationTileHashmap.begin(); itt != elevationTileHashmap.end(); itt++)
    {

        if (itt->second.lod < 3) {
            oneTile.lod = itt->second.lod;
            oneTile.y = itt->second.y;
            oneTile.x = itt->second.x;
            oneTile.origin = itt->second.origin;
            oneTile.tileSize = itt->second.size;
            oneTile.heightOffset = itt->second.hgt_offset;
            oneTile.heightScale = itt->second.hgt_scale;

            bake.tileInfoForBinaryExport.push_back(oneTile);
            fprintf(bake.txt_file, "%d %d %d %d %f %f, size %f, (%f, %f)\n", oneTile.lod, oneTile.y, oneTile.x, split.bakeSize, oneTile.heightOffset, oneTile.heightScale, oneTile.tileSize, oneTile.origin.x, oneTile.origin.y);
        }
    }

    bake.tileHash.clear();

    int highestLOD = 8;
    if (bakeToMax)
    {
        highestLOD = 7;
    }
    for (uint lod = 4; lod < highestLOD; lod++) {
        uint size = 1 << lod;
        unsigned char value;

        sprintf(name, "%s/bake/lod%d.raw", settings.dirRoot.c_str(), lod);
        FILE* tilemap = fopen(name, "rb");
        if (tilemap) {
            for (uint y = 0; y < size; y++) {
                for (uint x = 0; x < size; x++) {
                    fread(&value, 1, 1, tilemap);
                    if (value == 255) {
                        bake.tileHash.push_back(getHashFromTileCoords(lod, y, x));
                    }
                }
            }

            fclose(tilemap);
        }
    }
    bake.itterator = bake.tileHash.begin();
}

void replaceAlltm(std::string& str, const std::string& from, const std::string& to) {
    if (from.empty())
        return;
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // In case 'to' contains 'from', like replacing 'x' with 'yx'
    }
}

void terrainManager::bake_frame()
{
    if (bake.inProgress)
    {
        uint32_t hash = *bake.itterator;
        uint32_t lod = hash >> 28;
        bake_Setup(0.0f, lod, (hash >> 14) & 0x3FFF, hash & 0x3FFF, bake.renderContext);

        bake.itterator++;
        if (bake.itterator == bake.tileHash.end()) {
            bake.inProgress = false;
            fclose(bake.txt_file);

            if (!bakeToMax)
            {
                // the end. Now save tileInfoForBinaryExport
                char name[256];
                sprintf(name, "%s/bake/EVO/tiles.list", settings.dirRoot.c_str());
                FILE* info_file = fopen(name, "wb");
                if (info_file) {
                    fwrite(&bake.tileInfoForBinaryExport[0], sizeof(elevationMap), bake.tileInfoForBinaryExport.size(), info_file);
                    fclose(info_file);
                }


                std::string command;
                sprintf(name, "attrib -r %s/Elevations/*.*", (settings.dirExport).c_str());
                command = name;
                replaceAlltm(command, "/", "\\");
                system(command.c_str());
                sprintf(name, "copy /Y %s/bake/EVO/*.* %s/Elevations/", settings.dirRoot.c_str(), (settings.dirExport).c_str());
                command = name;
                replaceAlltm(command, "/", "\\");
                replaceAlltm(command, "\\Y", "/Y");
                system(command.c_str());
            }
        }
    }
}




void terrainManager::bake_Setup(float _size, uint _lod, uint _y, uint _x, RenderContext* _renderContext)
{
    uint32_t hashParent = 0;
    uint32_t hashLod = _lod;
    uint32_t hashY = _y;
    uint32_t hashX = _x;
    std::map<uint32_t, heightMap>::iterator it_Bicubic;

    hashParent = getHashFromTileCoords(hashLod, hashY, hashX);
    it_Bicubic = elevationTileHashmap.find(hashParent);
    while (it_Bicubic == elevationTileHashmap.end())
    {
        hashLod -= 1;
        hashY = hashY >> 1;
        hashX = hashX >> 1;
        hashParent = getHashFromTileCoords(hashLod, hashY, hashX);
        it_Bicubic = elevationTileHashmap.find(hashParent);
    }

    if (it_Bicubic != elevationTileHashmap.end())
    {
        if (hashParent > 0)			// now search for it and cache it
        {
            textureCacheElement map;
            LOG_LINE(1, "elevationCache.get 2");
            if (!elevationCache.get(hashParent, map))
            {
                std::array<unsigned short, 1048576> data;

                ojph::codestream codestream;
                ojph::j2c_infile j2c_file;
                j2c_file.open(elevationTileHashmap[hashParent].filename.c_str());
                codestream.enable_resilience();
                codestream.set_planar(false);
                codestream.read_headers(&j2c_file);
                codestream.create();
                uint next_comp;

                for (int i = 0; i < 1024; ++i)
                {
                    ojph::line_buf* line = codestream.pull(next_comp);
                    int32_t* dp = line->i32;
                    for (int j = 0; j < 1024; j++) {
                        int16_t val = (int16_t)*dp++;
                        data[i * 1024 + j] = val;
                    }
                }
                codestream.close();

                LOG_LINE(1, "elevationCache.set 2");
                map.texture = Texture::create2D(1024, 1024, Falcor::ResourceFormat::R16Unorm, 1, 1, data.data(), Resource::BindFlags::ShaderResource);
                elevationCache.set(hashParent, map);
            }

            split.compute_tileBicubic.Vars()->setTexture("gInput", map.texture);
        }

        const uint32_t cs_w = split.bakeSize / tile_cs_ThreadSize;

        const float size = (settings.size / (1 << _lod));
        const float outerSize = size * tile_numPixels / tile_InnerPixels;
        const float pixelSize = outerSize / split.bakeSize;
        float halfsize = ecotopeSystem::terrainSize / 2.f;
        float blocksize = ecotopeSystem::terrainSize / 16.f;
        const float2 origin = float2(-halfsize, -halfsize) + float2(_x * size, _y * size) - float2(pixelSize * 4 * 4, pixelSize * 4 * 4);

        {
            const glm::vec4 clearColor(0.1f, 0.1f, 0.9f, 1.0f);
            _renderContext->clearFbo(split.bakeFbo.get(), clearColor, 1.0f, 0, FboAttachmentType::All);
        }

        {
            textureCacheElement map;
            LOG_LINE(1, "elevationCache.get 3");
            if (!elevationCache.get(hashParent, map)) {
                split.compute_tileBicubic.Vars()->setTexture("gInput", map.texture);
            }

            heightMap& elevationMap = elevationTileHashmap[hashParent];

            // FIXME this is ghorrible 
            // cs_tile_Bicubic takes the inner tile coordinates, and compensates for 4 pixerls internall
            // so if we compendate for 12 here the other 4 is in teh shader
            const float2 originBC = float2(-halfsize, -halfsize) + float2(_x * size, _y * size) - float2(pixelSize * 4 * 3, pixelSize * 4 * 3);
            float2 bicubicOffset = (originBC - elevationMap.origin) / elevationMap.size;
            float S = pixelSize / elevationMap.size;
            float2 bicubicSize = float2(S, S);



            split.compute_tileBicubic.Vars()["gConstants"]["offset"] = bicubicOffset;
            split.compute_tileBicubic.Vars()["gConstants"]["size"] = bicubicSize;
            split.compute_tileBicubic.Vars()["gConstants"]["hgt_offset"] = elevationMap.hgt_offset;
            split.compute_tileBicubic.Vars()["gConstants"]["hgt_scale"] = elevationMap.hgt_scale;

            //split.compute_tileBicubic.Vars()->setTexture("gOuthgt_TEMPTILLTR", split.bakeFbo->getColorTexture(0));
            split.compute_tileBicubic.Vars()->setTexture("gOutput", split.bakeFbo->getColorTexture(0));
            split.compute_tileBicubic.dispatch(_renderContext, cs_w, cs_w);
            //split.compute_tileBicubic.Vars()->setTexture("gOuthgt_TEMPTILLTR", split.tileFbo->getColorTexture(0));
        }

        bake_RenderTopdown(_size, _lod, _y, _x, _renderContext);

        // FIXME MAY HAVE TO ECOTOPE SHADER in future

        _renderContext->flush(true);

        char outName[512];

        if (bakeToMax)
        {
            sprintf(outName, "%s/bake/fakeVeg/%d_%d_%d_albedo.png", settings.dirRoot.c_str(), _lod, _y, _x);
            split.bakeFbo->getColorTexture(1).get()->captureToFile(0, 0, outName, Bitmap::FileFormat::PngFile, Bitmap::ExportFlags::None);

            sprintf(outName, "%s/bake/fakeVeg/%d_%d_%d_ect_1.png", settings.dirRoot.c_str(), _lod, _y, _x);
            split.bakeFbo->getColorTexture(4).get()->captureToFile(0, 0, outName, Bitmap::FileFormat::PngFile, Bitmap::ExportFlags::None);

            sprintf(outName, "%s/bake/fakeVeg/%d_%d_%d_ect_2.png", settings.dirRoot.c_str(), _lod, _y, _x);
            split.bakeFbo->getColorTexture(5).get()->captureToFile(0, 0, outName, Bitmap::FileFormat::PngFile, Bitmap::ExportFlags::None);

            sprintf(outName, "%s/bake/fakeVeg/%d_%d_%d_ect_3.png", settings.dirRoot.c_str(), _lod, _y, _x);
            split.bakeFbo->getColorTexture(6).get()->captureToFile(0, 0, outName, Bitmap::FileFormat::PngFile, Bitmap::ExportFlags::None);

            sprintf(outName, "%s/bake/fakeVeg/%d_%d_%d_ect_4.png", settings.dirRoot.c_str(), _lod, _y, _x);
            split.bakeFbo->getColorTexture(7).get()->captureToFile(0, 0, outName, Bitmap::FileFormat::PngFile, Bitmap::ExportFlags::None);
        }


        _renderContext->resourceBarrier(split.bakeFbo->getColorTexture(0).get(), Resource::State::CopySource);
        uint32_t subresource = bake.copy_texture->getSubresourceIndex(0, 0);
        std::vector<glm::uint8> textureData = gpDevice->getRenderContext()->readTextureSubresource(split.bakeFbo->getColorTexture(0).get(), 0);
        float* pF = (float*)textureData.data();
        uint ret_size = (uint)textureData.size();
        float A = pF[0];
        float B = pF[1000];
        float C = pF[10000];
        float D = pF[30000];
        bool bCM = true;

        if (bakeToMax)
        {
            sprintf(outName, "%s/bake/fakeVeg/%d_%d_%d_hgt.raw", settings.dirRoot.c_str(), _lod, _y, _x);
            FILE* file = fopen(outName, "wb");
            if (file) {
                fwrite(pF, sizeof(float), split.bakeSize * split.bakeSize, file);
                fclose(file);
            }
        }

        // find minmax
        float max = 0;
        float min = 100000000;
        for (int y = 0; y < split.bakeSize * split.bakeSize; y++) {
            if (pF[y] < min) min = pF[y];
            if (pF[y] > max) max = pF[y];
        }

        min -= 0.5f;
        float range = max - min + 0.5f;

        fprintf(bake.txt_file, "%d %d %d %d %f %f %f, size %f, (%f, %f)\n", _lod, _y, _x, split.bakeSize, min, max, range, outerSize, origin.x, origin.y);

        elevationMap oneTile;
        oneTile.lod = _lod;
        oneTile.y = _y;
        oneTile.x = _x;
        oneTile.origin = origin;
        oneTile.tileSize = outerSize;
        oneTile.heightOffset = min;
        oneTile.heightScale = range;

        bake.tileInfoForBinaryExport.push_back(oneTile);

        // Now output JP2
        if (!bakeToMax)
        {
            //// FIXME load from settings file CEREAL
            sprintf(outName, "%s/bake/EVO/hgt_%d_%d_%d.jp2", settings.dirRoot.c_str(), _lod, _y, _x);
            ojph::codestream codestream;
            ojph::j2c_outfile j2c_file;
            j2c_file.open(outName);

            {
                // set up
                ojph::param_siz siz = codestream.access_siz();
                siz.set_image_extent(ojph::point(split.bakeSize, split.bakeSize));
                siz.set_num_components(1);
                siz.set_component(0, ojph::point(1, 1), 16, false);		//??? unsure about the subsampling point()
                siz.set_image_offset(ojph::point(0, 0));
                siz.set_tile_size(ojph::size(split.bakeSize, split.bakeSize));
                siz.set_tile_offset(ojph::point(0, 0));

                ojph::param_cod cod = codestream.access_cod();
                cod.set_num_decomposition(5);
                cod.set_block_dims(64, 64);
                //if (num_precints != -1)
                //	cod.set_precinct_size(num_precints, precinct_size);
                cod.set_progression_order("RPCL");
                cod.set_color_transform(false);
                cod.set_reversible(false);
                codestream.access_qcd().set_irrev_quant(bake.quality);


                {
                    codestream.write_headers(&j2c_file);

                    uint next_comp;
                    ojph::line_buf* cur_line = codestream.exchange(NULL, next_comp);

                    for (uint i = 0; i < split.bakeSize; ++i)
                    {

                        int32_t* dp = cur_line->i32;
                        for (uint j = 0; j < split.bakeSize; j++) {
                            float fVal = pF[i * 1024 + j];
                            int32_t shortVal = (unsigned short)((fVal - min) / range * 65536.0f);
                            *dp++ = shortVal;
                        }
                        cur_line = codestream.exchange(cur_line, next_comp);
                    }
                }

            }
            codestream.flush();
            codestream.close();
        }
    }

}




void terrainManager::bake_RenderTopdown(float _size, uint _lod, uint _y, uint _x, RenderContext* _renderContext)
{
    float terrainSize = settings.size;
    uint gridSize = (uint)pow(2, _lod);
    float tileSize = terrainSize / gridSize;
    float tileOuterSize = tileSize * 256.0f / 248.0f;

    // set up the camera -----------------------
    float s = tileOuterSize / 2.0f;
    float x = (_x + 0.5f) * tileSize - (terrainSize / 2.0f);
    float z = (_y + 0.5f) * tileSize - (terrainSize / 2.0f);
    glm::mat4 V, P, VP;
    V[0] = glm::vec4(1, 0, 0, 0);
    V[1] = glm::vec4(0, 0, 1, 0);
    V[2] = glm::vec4(0, -1, 0, 0);
    V[3] = glm::vec4(-x, z, 0, 1);


    P = glm::orthoLH(-s, s, -s, s, -10000.0f, 10000.0f);

    //viewproj = view * proj;
    VP = P * V;    //??? order

    rmcv::mat4 view, proj, viewproj;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            view[j][i] = V[j][i];
            proj[j][i] = P[j][i];
            viewproj[j][i] = VP[j][i];
        }
    }


    {
        split.shader_meshTerrafector.State()->setFbo(split.bakeFbo);
        split.shader_meshTerrafector.State()->setRasterizerState(split.rasterstateSplines);
        split.shader_meshTerrafector.State()->setBlendState(split.blendstateRoadsCombined);
        split.shader_meshTerrafector.State()->setDepthStencilState(split.depthstateAll);

        split.shader_meshTerrafector.Vars()["gConstantBuffer"]["viewproj"] = viewproj;
        split.shader_meshTerrafector.Vars()["gConstantBuffer"]["overlayAlpha"] = 1.0f;



        auto& block = split.shader_meshTerrafector.Vars()->getParameterBlock("gmyTextures");
        ShaderVar& var = block->findMember("T");        // FIXME pre get
        terrafectorEditorMaterial::static_materials.setTextures(var);
    }

    //if (bSplineAsTerrafector)           // Now render the roadNetwork
    {
        split.shader_splineTerrafector.State()->setFbo(split.bakeFbo);
        split.shader_splineTerrafector.State()->setRasterizerState(split.rasterstateSplines);
        split.shader_splineTerrafector.State()->setDepthStencilState(split.depthstateAll);
        split.shader_splineTerrafector.State()->setBlendState(split.blendstateRoadsCombined);

        split.shader_splineTerrafector.Vars()["gConstantBuffer"]["viewproj"] = viewproj;
        split.shader_splineTerrafector.Vars()["gConstantBuffer"]["startOffset"] = 0;
        split.shader_splineTerrafector.Vars()->setBuffer("materials", terrafectorEditorMaterial::static_materials.sb_Terrafector_Materials);

        auto& block = split.shader_splineTerrafector.Vars()->getParameterBlock("gmyTextures");
        ShaderVar& var = block->findMember("T");        // FIXME pre get
        terrafectorEditorMaterial::static_materials.setTextures(var);

        split.shader_splineTerrafector.Vars()->setBuffer("splineData", splines.bezierData);     // not created yet
    }


    // Mesh bake low
    if (gis_overlay.bakeBakeOnlyData)
    {
        if (_lod >= 4)
        {
            gpuTileTerrafector* tile = terrafectorSystem::loadCombine_LOD4_bakeLow.getTile((_y >> (_lod - 4)) * 16 + (_x >> (_lod - 4)));
            if (tile)
            {
                if (tile->numBlocks > 0)
                {
                    split.shader_meshTerrafector.Vars()->setBuffer("vertexData", tile->vertex);
                    split.shader_meshTerrafector.Vars()->setBuffer("indexData", tile->index);
                    split.shader_meshTerrafector.drawInstanced(_renderContext, 128 * 3, tile->numBlocks);
                }
            }
        }

        //if (bSplineAsTerrafector)           // Now render the roadNetwork
        {
            split.shader_splineTerrafector.Vars()->setBuffer("indexData", splines.indexDataBakeOnly);
            split.shader_splineTerrafector.drawIndexedInstanced(_renderContext, 64 * 6, splines.numStaticSplinesBakeOnlyIndex);
        }

        if (_lod >= 4)
        {
            gpuTileTerrafector* tile = terrafectorSystem::loadCombine_LOD4_bakeHigh.getTile((_y >> (_lod - 4)) * 16 + (_x >> (_lod - 4)));
            if (tile)
            {
                if (tile->numBlocks > 0)
                {
                    split.shader_meshTerrafector.Vars()->setBuffer("vertexData", tile->vertex);
                    split.shader_meshTerrafector.Vars()->setBuffer("indexData", tile->index);
                    split.shader_meshTerrafector.drawInstanced(_renderContext, 128 * 3, tile->numBlocks);
                }
            }
        }
    }






    if (_lod >= 6)
    {
        gpuTileTerrafector* tile = terrafectorSystem::loadCombine_LOD6.getTile((_y >> (_lod - 6)) * 64 + (_x >> (_lod - 6)));
        if (tile)
        {
            if (tile->numBlocks > 0)
            {
                split.shader_meshTerrafector.Vars()->setBuffer("vertexData", tile->vertex);
                split.shader_meshTerrafector.Vars()->setBuffer("indexData", tile->index);
                split.shader_meshTerrafector.drawInstanced(_renderContext, 128 * 3, tile->numBlocks);
            }
        }
    }
    else if (_lod >= 4)
    {
        gpuTileTerrafector* tile = terrafectorSystem::loadCombine_LOD4.getTile((_y >> (_lod - 4)) * 16 + (_x >> (_lod - 4)));
        if (tile)
        {
            if (tile->numBlocks > 0)
            {
                split.shader_meshTerrafector.Vars()->setBuffer("vertexData", tile->vertex);
                split.shader_meshTerrafector.Vars()->setBuffer("indexData", tile->index);
                split.shader_meshTerrafector.drawInstanced(_renderContext, 128 * 3, tile->numBlocks);
            }
        }
    }
    else if (_lod >= 2)
    {
        gpuTileTerrafector* tile = terrafectorSystem::loadCombine_LOD2.getTile((_y >> (_lod - 2)) * 4 + (_x >> (_lod - 2)));
        if (tile)
        {
            if (tile->numBlocks > 0)
            {
                split.shader_meshTerrafector.Vars()->setBuffer("vertexData", tile->vertex);
                split.shader_meshTerrafector.Vars()->setBuffer("indexData", tile->index);
                split.shader_meshTerrafector.drawInstanced(_renderContext, 128 * 3, tile->numBlocks);
            }
        }
    }





    //??? should probably be in the roadnetwork code, but look at the optimize step first
    //if (bSplineAsTerrafector)           // Now render the roadNetwork
    {
        if (_lod >= 8)
        {
            split.shader_splineTerrafector.Vars()["gConstantBuffer"]["startOffset"] = splines.startOffset_LOD8[_y >> (_lod - 8)][_x >> (_lod - 8)];
            split.shader_splineTerrafector.Vars()->setBuffer("indexData", splines.indexData_LOD8);
            split.shader_splineTerrafector.drawIndexedInstanced(_renderContext, 64 * 6, splines.numIndex_LOD8[_y >> (_lod - 8)][_x >> (_lod - 8)]);
        }
        else if (_lod >= 6)
        {
            split.shader_splineTerrafector.Vars()["gConstantBuffer"]["startOffset"] = splines.startOffset_LOD6[_y >> (_lod - 6)][_x >> (_lod - 6)];
            split.shader_splineTerrafector.Vars()->setBuffer("indexData", splines.indexData_LOD6);
            split.shader_splineTerrafector.drawIndexedInstanced(_renderContext, 64 * 6, splines.numIndex_LOD6[_y >> (_lod - 6)][_x >> (_lod - 6)]);
        }
        else if (_lod >= 4)
        {
            split.shader_splineTerrafector.Vars()["gConstantBuffer"]["startOffset"] = splines.startOffset_LOD4[_y >> (_lod - 4)][_x >> (_lod - 4)];
            split.shader_splineTerrafector.Vars()->setBuffer("indexData", splines.indexData_LOD4);
            split.shader_splineTerrafector.drawIndexedInstanced(_renderContext, 64 * 6, splines.numIndex_LOD4[_y >> (_lod - 4)][_x >> (_lod - 4)]);
        }
    }

}



void terrainManager::sceneToMax()
{

    std::filesystem::path path;
    FileDialogFilterVec filters = { {"fbx"}, {"obj"} };
    if (saveFileDialog(filters, path))
    {

        char filename[1024];
        uint numMeshes = 0;
        for (auto& tile : m_used)
        {
            bool surface = tile->parent && tile->parent->main_ShouldSplit && tile->child[0] == nullptr && (tile->lod >= exportLodMin) && (tile->lod <= exportLodMax);
            surface = surface || (tile->lod == exportLodMax);   // add all top level
            if (surface)
            {
                numMeshes++;
                //sprintf(filename, "%s/albedo_%d.jpg", path.root_directory().string().c_str(), tile->index);
                //compressed_Albedo_Array->captureToFile(0, tile->index, filename, Bitmap::FileFormat::JpegFile, Bitmap::ExportFlags::None);

                //std::vector<uint8_t> textureData;
                //uint32_t subresource = compressed_Albedo_Array->getSubresourceIndex(tile->index, 0);
                //textureData = pContext->readTextureSubresource(this, subresource);
            }
        }

        aiScene* scene = new aiScene;
        scene->mRootNode = new aiNode();

        scene->mMaterials = new aiMaterial * [numMeshes];
        scene->mNumMaterials = numMeshes;

        scene->mMeshes = new aiMesh * [numMeshes];
        scene->mRootNode->mMeshes = new unsigned int[numMeshes];
        for (int i = 0; i < numMeshes; i++)
        {
            scene->mMeshes[i] = nullptr;
            scene->mRootNode->mMeshes[i] = i;
        }
        scene->mNumMeshes = numMeshes;
        scene->mRootNode->mNumMeshes = numMeshes;


        uint meshCount = 0;
        for (auto& tile : m_used)
        {
            bool surface = tile->parent && tile->parent->main_ShouldSplit && tile->child[0] == nullptr && (tile->lod >= exportLodMin) && (tile->lod <= exportLodMax);
            surface = surface || (tile->lod == exportLodMax);   // add all top level
            if (surface)
            {
                scene->mMaterials[meshCount] = new aiMaterial();
                scene->mMeshes[meshCount] = new aiMesh();
                scene->mMeshes[meshCount]->mMaterialIndex = meshCount;
                auto pMesh = scene->mMeshes[meshCount];

                pMesh->mFaces = new aiFace[248 * 248];
                pMesh->mNumFaces = 248 * 248;
                pMesh->mPrimitiveTypes = aiPrimitiveType_POLYGON;

                for (uint j = 0; j < 248; j++)
                {
                    for (uint i = 0; i < 248; i++)
                    {
                        aiFace& face = pMesh->mFaces[j * 248 + i];

                        face.mIndices = new unsigned int[4];
                        face.mNumIndices = 4;

                        face.mIndices[0] = ((j + 4) * 256) + (i + 4 + 1);
                        face.mIndices[1] = ((j + 4) * 256) + (i + 4);
                        face.mIndices[2] = ((j + 4) * 256) + 256 + (i + 4);
                        face.mIndices[3] = ((j + 4) * 256) + 256 + (i + 4 + 1);
                    }
                }

                pMesh->mVertices = new aiVector3D[256 * 256];
                pMesh->mNumVertices = 256 * 256;

                //pMesh->mTextureCoords[0] = new aiVector3D[256 * 256];
                //pMesh->mNumUVComponents[0] = 256 * 256;

                std::vector<glm::uint8> textureData = gpDevice->getRenderContext()->readTextureSubresource(height_Array.get(), tile->index);
                float* pF = (float*)textureData.data();
                uint ret_size = (uint)textureData.size();

                for (uint y = 0; y < 256; y++)
                {
                    for (uint x = 0; x < 256; x++)
                    {
                        uint index = y * 256 + x;
                        pMesh->mVertices[index] = aiVector3D(tile->origin.x + x * tile->size / 248.0f, pF[index], tile->origin.z + y * tile->size / 248.0f);
                    }
                }
                meshCount++;
            }
        }


        Exporter exp;
        if (path.string().find("fbx") != std::string::npos)
        {
            exp.Export(scene, "fbx", path.string());
        }
        else if (path.string().find("obj") != std::string::npos)
        {
            exp.Export(scene, "obj", path.string());
        }
    }
}


void terrainManager::updateDynamicStamp()
{
}


void terrainManager::stamp_to_Bezier(stamp& S, cubicDouble* BEZ, bezierLayer* IDX, int _index)
{
    float3 A = S.pos - S.right - S.dir;
    float3 B = S.pos + S.right - S.dir;
    float3 C = S.pos - S.right + S.dir;
    float3 D = S.pos + S.right + S.dir;

    BEZ->data[0][0] = { A, 0 };
    BEZ->data[0][1] = { A, 0 };
    BEZ->data[0][2] = { C, 1 };
    BEZ->data[0][3] = { C, 1 };

    BEZ->data[1][0] = { B, 0 };
    BEZ->data[1][1] = { B, 0 };
    BEZ->data[1][2] = { D, 1 };
    BEZ->data[1][3] = { D, 1 };

    *IDX = bezierLayer(bezier_edge::outside, bezier_edge::center, S.material, _index, true, 0, 0);
    IDX->B |= 0x1 << 29; // isQuad
}


void terrainManager::currentStamp_to_Bezier()
{
    splineTest.bSegment = false;
    splineTest.bVertex = false;
    splineTest.testDistance = 1000;
    splineTest.bCenter = false;
    splineTest.cornerNum = -1;
    splineTest.pos = split.feedback.tum_Position;
    splineTest.bStreetview = false;

    mCurrentStamp.material = terrafectorEditorMaterial::static_materials.selectedMaterial;
    int matIdx = mCurrentStamp.material;
    if (matIdx >= 0)
    {
        terrafectorEditorMaterial mat = terrafectorEditorMaterial::static_materials.materialVector[matIdx];

        float3 N = split.feedback.tum_Normal;
        mCurrentStamp.right = { cos(mCurrentStamp.rotation), 0, -sin(mCurrentStamp.rotation) };
        mCurrentStamp.dir = glm::normalize(glm::cross(mCurrentStamp.right, N)) * 0.5f * mat.stampHeight * mCurrentStamp.scale.y;
        mCurrentStamp.right = glm::normalize(glm::cross(N, mCurrentStamp.dir)) * 0.5f * mat.stampWidth * mCurrentStamp.scale.x;

        mCurrentStamp.pos = split.feedback.tum_Position + (N * mCurrentStamp.height);

        // FIXME scale dir and right here

        cubicDouble BEZ;
        bezierLayer IDX;
        stamp_to_Bezier(mCurrentStamp, &BEZ, &IDX, 0);

        splines.numDynamicStampIndex = 1;
        splines.dynamic_bezierData->setBlob(&BEZ, 0, sizeof(cubicDouble));
        splines.dynamic_indexData->setBlob(&IDX, 0, sizeof(bezierLayer));
    }
    else
    {
        splines.numDynamicStampIndex = 0;
    }

}


void terrainManager::allStamps_to_Bezier()
{
    /*
    int size = mRoadStampCollection.stamps.size();
    roadNetwork::staticBezierData.resize(size);
    roadNetwork::staticIndexData.resize(size);

    for (int i = 0; i < size; i++)
    {
        stamp_to_Bezier(mRoadStampCollection.stamps[i], &roadNetwork::staticBezierData.at(i), &roadNetwork::staticIndexData.at(i), i);
    }

    splines.numStaticSplines = size;
    splines.numStaticSplinesIndex = size;
    splines.bezierData->setBlob(roadNetwork::staticBezierData.data(), 0, splines.numStaticSplines * sizeof(cubicDouble));
    splines.indexData->setBlob(roadNetwork::staticIndexData.data(), 0, splines.numStaticSplinesIndex * sizeof(bezierLayer));

    allStamps_to_Terrafector();
    */
}

void terrainManager::allStamps_to_Terrafector()
{
    int size = mRoadStampCollection.stamps.size();
    terrafectorSystem::loadCombine_LOD7_stamps.create(7);   // the create clears it
    lodTriangleMesh lodder_stamp;
    lodder_stamp.create(7);

    float3 pos[3];
    float2 uv[2];
    for (auto& S : mRoadStampCollection.stamps)
    {
        if (S.scale.x != 0)
        {
            pos[0] = S.pos - S.right - S.dir;
            pos[1] = S.pos + S.right - S.dir;
            pos[2] = S.pos + S.right + S.dir;
            uv[0] = { 0, 0 };
            uv[1] = { 1, 0 };
            uv[2] = { 1, 1 };
            lodder_stamp.insertTriangle(S.material, pos, uv);

            pos[0] = S.pos - S.right - S.dir;
            pos[1] = S.pos + S.right + S.dir;
            pos[2] = S.pos - S.right + S.dir;
            uv[0] = { 0, 0 };
            uv[1] = { 1, 1 };
            uv[2] = { 0, 1 };
            lodder_stamp.insertTriangle(S.material, pos, uv);
        }
    }

    terrafectorSystem::loadCombine_LOD7_stamps.addMesh("", lodder_stamp, false);
    terrafectorSystem::loadCombine_LOD7_stamps.loadToGPU("", true);
}



void terrainManager::updateDynamicRoad(bool _bezierChanged) {

    if (this->terrainMode == _terrainMode::terrafector)
    {
        currentStamp_to_Bezier();
        return;
    }

    // active road ----------------------------------------------------------------------------------------------------------------
    splineTest.bSegment = false;
    splineTest.bVertex = false;
    splineTest.testDistance = 1000;
    splineTest.bCenter = false;
    splineTest.cornerNum = -1;
    splineTest.pos = split.feedback.tum_Position;
    splineTest.bStreetview = false;

    //mRoadNetwork.testHit(feedback.tum_Position);

    static bool bRefresh;
    if (_bezierChanged) { bRefresh = true; }
    if (mRoadNetwork.currentRoad || mRoadNetwork.currentIntersection)
    {
        if (bRefresh || mRoadNetwork.isDirty)
        {
            mRoadNetwork.updateDynamicRoad();
            splines.numDynamicSplines = __min(splines.maxDynamicBezier, (int)roadNetwork::staticBezierData.size());
            splines.numDynamicSplinesIndex = __min(splines.maxDynamicIndex, (int)roadNetwork::staticIndexData.size());
            if (splines.numDynamicSplines > 0)
            {
                splines.dynamic_bezierData->setBlob(roadNetwork::staticBezierData.data(), 0, splines.numDynamicSplines * sizeof(cubicDouble));
                splines.dynamic_indexData->setBlob(roadNetwork::staticIndexData.data(), 0, splines.numDynamicSplinesIndex * sizeof(bezierLayer));

                mRoadNetwork.isDirty = false;
            }
        }
        if (!_bezierChanged) { bRefresh = false; }
    }
    else
    {
        splines.numDynamicSplines = 0;
        splines.numDynamicSplinesIndex = 0;
    }

    mRoadNetwork.intersectionSelectedRoad = nullptr;


    if (mRoadNetwork.currentRoad && mRoadNetwork.currentRoad->points.size() >= 3)
    {
        mSpriteRenderer.clearDynamic();
        mRoadNetwork.currentRoad->testAgainstPoint(&splineTest);

        // mouse to spline markers ---------------------------------------------------------------------------------------

        if (splineTest.bVertex) {
            mSpriteRenderer.pushMarker(splineTest.returnPos, 4, 3.0f);
        }
        if (splineTest.bSegment) {
            mSpriteRenderer.pushMarker(splineTest.returnPos, 1);
        }

        // show selection
        for (auto& point : mRoadNetwork.currentRoad->points)
        {
            mSpriteRenderer.pushMarker(point.anchor, 2 - point.constraint, 1.5f);

        }

        if (mRoadNetwork.AI_path_mode)
        {

            int ssize = (int)mRoadNetwork.pathBezier.segments.size();

            for (int i = 0; i < ssize - 1; i++) {
                float3 O1 = mRoadNetwork.pathBezier.segments[i].optimalPos;
                float3 O2 = mRoadNetwork.pathBezier.segments[i + 1].optimalPos;
                mSpriteRenderer.pushLine(O1, O2, 1, 0.5f);

                O1 = mRoadNetwork.pathBezier.segments[i].A;
                O2 = mRoadNetwork.pathBezier.segments[i].B;
                mSpriteRenderer.pushLine(O1, O2, 1, 0.3f);
            }
        }

        mSpriteRenderer.loadDynamic();

    }






    if (mRoadNetwork.currentIntersection)
    {
        mRoadNetwork.updateDynamicRoad();

        mSpriteRenderer.clearDynamic();
        intersection* I = mRoadNetwork.currentIntersection;

        //mSpriteRenderer.pushMarker(I->anchor, 3 + I->buildQuality, 4.0);			// anchor
        float distAnchor = glm::length(I->anchor - splineTest.pos);
        if (distAnchor < splineTest.testDistance && distAnchor < 8.0f) {
            splineTest.testDistance = distAnchor;
            splineTest.bCenter = true;
            splineTest.bStreetview = false;
        }


        uint RLsize = (uint)I->roadLinks.size();
        for (uint i = 0; i < RLsize; i++) {
            intersectionRoadLink* R = &I->roadLinks[i];
            intersectionRoadLink* RB = &I->roadLinks[(i + RLsize - 1) % RLsize];


            // First test against all the roads attached, BUT EXCLUDE the first vertex
            if (R->roadPtr->testAgainstPoint(&splineTest, false)) {
                mRoadNetwork.intersectionSelectedRoad = R->roadPtr;
                splineTest.bCenter = false;
            }

            float3 Z = R->corner_A - splineTest.pos;
            Z.y = 0;
            float distCorner = glm::length(Z);
            if ((distCorner < 3.0) && (distCorner < splineTest.testDistance)) {
                splineTest.testDistance = distCorner;
                splineTest.bCenter = false;
                splineTest.bSegment = false;
                splineTest.bVertex = false;
                splineTest.cornerNum = i;
                splineTest.cornerFlag = 0;
                mRoadNetwork.intersectionSelectedRoad = nullptr;

            }


            float distA;
            if (R->cornerType == typeOfCorner::artistic) {
                Z = (R->corner_A + R->cornerTangent_A) - splineTest.pos;
                Z.y = 0;
                distA = glm::length(Z);
                if (distA < 3.0 && distA < splineTest.testDistance) {
                    splineTest.testDistance = distA;
                    splineTest.bCenter = false;
                    splineTest.bSegment = false;
                    splineTest.bVertex = false;
                    splineTest.cornerNum = i;
                    splineTest.cornerFlag = -1;
                    mRoadNetwork.intersectionSelectedRoad = nullptr;

                }
            }


            if (RB->cornerType == typeOfCorner::artistic) {
                Z = (R->corner_B + R->cornerTangent_B) - splineTest.pos;
                Z.y = 0;
                distA = glm::length(Z);
                if (distA < 5.0 && distA < splineTest.testDistance) {
                    splineTest.testDistance = distA;
                    splineTest.bCenter = false;
                    splineTest.bSegment = false;
                    splineTest.bVertex = false;
                    splineTest.cornerNum = i;
                    splineTest.cornerFlag = 1;
                    mRoadNetwork.intersectionSelectedRoad = nullptr;
                }
            }


            Z = (I->anchor + R->tangentVector) - splineTest.pos;
            Z.y = 0;
            distA = glm::length(Z);
            if (distA < 5.0 && distA < splineTest.testDistance) {
                splineTest.testDistance = distA;
                splineTest.bCenter = false;
                splineTest.cornerNum = i;
                splineTest.cornerFlag = 2;
                mRoadNetwork.intersectionSelectedRoad = nullptr;
            }




            mSpriteRenderer.pushMarker(R->corner_A, 2 - R->cornerType, 0.5f);						// corner anchor
            //if (R->cornerType == typeOfCorner::artistic)
            {
                mSpriteRenderer.pushLine(R->corner_A, R->corner_A + R->cornerTangent_A, R->cornerType, 0.2f);
                mSpriteRenderer.pushLine(R->corner_B, R->corner_B + R->cornerTangent_B, RB->cornerType, 0.2f);
                mSpriteRenderer.pushMarker(R->corner_A + R->cornerTangent_A, 2 - R->cornerType, 0.5f);			// anchor
                mSpriteRenderer.pushMarker(R->corner_B + R->cornerTangent_B, 2 - RB->cornerType, 0.5f);			// anchor
            }

            mSpriteRenderer.pushLine(I->anchor, I->anchor + R->tangentVector, 0, 0.3f);
            mSpriteRenderer.pushMarker(I->anchor + R->tangentVector, 0, 0.5);			// center tangent
        }






        // now show what we selected ----------------------------------------------------------------------------------
        if (mRoadNetwork.intersectionSelectedRoad) {
            mSpriteRenderer.pushMarker(splineTest.returnPos, 4, 1.0f);
        }
        else {
            if (splineTest.bCenter) {
                mSpriteRenderer.pushMarker(I->anchor, 4, 1.0);
            }
            else if (splineTest.cornerNum >= 0) {
                intersectionRoadLink* R = &I->roadLinks[splineTest.cornerNum];
                switch (splineTest.cornerFlag) {
                case -1:
                    mSpriteRenderer.pushMarker(R->corner_A + R->cornerTangent_A, 4, 1.0);
                    break;
                case 0:
                    mSpriteRenderer.pushMarker(R->corner_A, 4, 1.0);
                    break;
                case 1:
                    mSpriteRenderer.pushMarker(R->corner_B + R->cornerTangent_B, 4, 1.0);
                    break;
                case 2:
                    mSpriteRenderer.pushMarker(I->anchor + R->tangentVector, 4, 1.0);
                    break;
                }
            }
        }

        mSpriteRenderer.loadDynamic();
    }

}



bool terrainManager::onKeyEvent(const KeyboardEvent& keyEvent)
{
    bool keyPressed = (keyEvent.type == KeyboardEvent::Type::KeyPressed);

    splineTest.bCtrl = keyEvent.hasModifier(Input::Modifier::Ctrl);
    splineTest.bShift = keyEvent.hasModifier(Input::Modifier::Shift);
    splineTest.bAlt = keyEvent.hasModifier(Input::Modifier::Alt);

    // terrain mode
    if (splineTest.bCtrl)
    {
        if (keyPressed && keyEvent.key == Input::Key::Key1) terrainMode = _terrainMode::vegetation;
        if (keyPressed && keyEvent.key == Input::Key::Key2) terrainMode = _terrainMode::ecotope;
        if (keyPressed && keyEvent.key == Input::Key::Key3) terrainMode = _terrainMode::terrafector;
        if (keyPressed && keyEvent.key == Input::Key::Key4) terrainMode = _terrainMode::roads;
        //if (keyPressed && keyEvent.key == Input::Key::Key5) terrainMode = _terrainMode::glider;
        if (keyPressed && keyEvent.key == Input::Key::Key6) terrainMode = _terrainMode::terrainBuilder;
        if (keyPressed && keyEvent.key == Input::Key::Key7) terrainMode = _terrainMode::textureTool;

    }



    if (terrainMode == _terrainMode::glider)   // Paragliding
    {
        if (keyPressed && keyEvent.key == Input::Key::Escape) {
            renderGui_Menu = !renderGui_Menu;
        }
        if (keyPressed && keyEvent.key == Input::Key::H) {
            renderGui_Hud = !renderGui_Hud;
        }
        if (keyPressed && keyEvent.key == Input::Key::J) {
            useFreeCamWhileGliding = !useFreeCamWhileGliding;
        }
        if (keyPressed && keyEvent.key == Input::Key::K) {
            GliderDebugVisuals = !GliderDebugVisuals;
        }

    }




    if (keyEvent.type == KeyboardEvent::Type::KeyPressed)
    {
        //if (keyEvent.key == Input::Key::Space)   paraRuntime.playpause();
        //if (keyEvent.key == Input::Key::S)   paraRuntime.runstep();


        if (keyEvent.hasModifier(Input::Modifier::Ctrl))
        {
            if (keyEvent.key == Input::Key::D)          // debug
            {
                debug = !debug;
            }
            if (keyEvent.key == Input::Key::O)
            {
                gisReload(split.feedback.tum_Position);
            }
        }

        switch (terrainMode)
        {
        case 0:
            plants_Root.onKeyEvent(keyEvent);
            break;

        case 2:
            // Terrafector - placinf stamps in here
            if (keyPressed)
            {
                if (keyEvent.key == Input::Key::B)
                {
                    bSplineAsTerrafector = !bSplineAsTerrafector;
                    mRoadNetwork.updateAllRoads();
                    return true;
                }
            }
            break;


        case 3:


            // selection 
            if (mRoadNetwork.currentRoad && splineTest.bCtrl)
            {
                switch (keyEvent.key)
                {
                case Input::Key::A:	// select all
                    mRoadNetwork.currentRoad->selectAll();
                    mRoadNetwork.selectionType = 2;
                    return true;
                    break;
                case Input::Key::D:	// select all
                    mRoadNetwork.currentRoad->clearSelection();
                    mRoadNetwork.selectionType = 0;
                    return true;
                    break;
                }
            }

            if (keyPressed)
            {
                if (keyEvent.hasModifier(Input::Modifier::Ctrl))		// CTRL + key
                {
                    switch (keyEvent.key)
                    {
                    case Input::Key::R:
                        terrafectors.loadPath(settings.dirRoot + "/terrafectors", settings.dirRoot + "/bake");
                        break;
                    case Input::Key::C:		// copy
                        if (splineTest.bVertex) {
                            mRoadNetwork.copyVertex(splineTest.index);
                        }
                        break;
                    case Input::Key::V:		// paste
                        if (keyEvent.hasModifier(Input::Modifier::Shift))		// CTRL + SHIFT  + key
                        {
                            if (splineTest.bVertex) {
                                mRoadNetwork.pasteVertexMaterial(splineTest.index);
                            }
                        }
                        else {
                            if (splineTest.bVertex) {
                                mRoadNetwork.pasteVertexGeometry(splineTest.index);
                            }
                        }
                        break;
                    }
                }
                else
                {
                    switch (keyEvent.key)
                    {
                    case Input::Key::Del:
                        if (mRoadNetwork.currentIntersection) mRoadNetwork.deleteCurrentIntersection();
                        if (mRoadNetwork.currentRoad) mRoadNetwork.deleteCurrentRoad();
                        break;
                    case Input::Key::Space:
                        if (mRoadNetwork.currentRoad) mRoadNetwork.showMaterials = !mRoadNetwork.showMaterials;
                        break;

                    case Input::Key::Q:
                        if (mRoadNetwork.currentRoad) mRoadNetwork.currentRoad->solveRoad(splineTest.index);
                        if (mRoadNetwork.currentIntersection) mRoadNetwork.solveIntersection();
                        mRoadNetwork.updateAllRoads();
                        break;
                    }


                }

                mRoadNetwork.updateAllRoads();
                updateDynamicRoad(true);
            }

            switch (keyEvent.key)
            {
            case Input::Key::Escape:	// deselect all
                if (keyPressed) {
                    {
                        if (mRoadNetwork.popupVisible) {
                            ImGui::CloseCurrentPopup();
                            // stuff that, dpoesnt work no key or mouse events, cunsumed by popup
                        }
                        else {
                            mRoadNetwork.currentRoad = nullptr;
                            mRoadNetwork.currentIntersection = nullptr;
                            mRoadNetwork.intersectionSelectedRoad = nullptr;
                            mRoadNetwork.updateAllRoads();
                            updateDynamicRoad(true);
                        }
                    }
                }
                return true;
                break;
            case Input::Key::S:
                if (keyPressed && keyEvent.hasModifier(Input::Modifier::Ctrl)) {
                    mRoadNetwork.quickSave();
                }
                break;
            case Input::Key::X:
                // road new spline
                if (keyPressed && keyEvent.hasModifier(Input::Modifier::Ctrl)) {
                    if (mRoadNetwork.currentIntersection) {
                        mRoadNetwork.currentRoad = nullptr;
                        mRoadNetwork.currentIntersection_findRoads();
                        //updateStaticRoad();
                        updateDynamicRoad(true);
                    }
                }
                return true;
                break;
            case Input::Key::G:
                // road new spline
                if (keyPressed) {
                    mRoadNetwork.newRoadSpline();
                    //updateStaticRoad();

                }
                return true;
                break;

            case Input::Key::Y:
                // road new spline
                if (keyPressed) {
                    mRoadNetwork.newRoadSplineBasic();
                    //updateStaticRoad();

                }
                return true;
                break;


            case Input::Key::F:
                // new node
                if (keyPressed) {
                    mRoadNetwork.newIntersection();
                    mRoadNetwork.currentIntersection->updatePosition(split.feedback.tum_Position, split.feedback.tum_Normal, m_tiles[split.feedback.tum_idx].lod);
                    mRoadNetwork.currentIntersection_findRoads();
                    updateDynamicRoad(true);

                }
                return true;
                break;
            case Input::Key::H:
                if (keyPressed && splineTest.bVertex) {
                    mRoadNetwork.breakCurrentRoad(splineTest.index);
                    //updateStaticRoad();
                    updateDynamicRoad(true);
                }
                return true;
                break;
            case Input::Key::J:
                if (keyPressed) {
                    mRoadNetwork.deleteCurrentRoad();
                    //updateStaticRoad();
                    updateDynamicRoad(true);
                }
                return true;
                break;
            case Input::Key::K:
                if (keyPressed) {
                    mRoadNetwork.deleteCurrentIntersection();
                    //updateStaticRoad();
                    updateDynamicRoad(true);
                }
                return true;
                break;
            case Input::Key::B:
                if (keyPressed) {
                    if (splineTest.bVertex && keyEvent.hasModifier(Input::Modifier::Ctrl)) {
                        mRoadNetwork.currentRoad->points[splineTest.index].isBridge = !mRoadNetwork.currentRoad->points[splineTest.index].isBridge;
                    }
                    else
                    {
                        bSplineAsTerrafector = !bSplineAsTerrafector;
                        mRoadNetwork.updateAllRoads();
                        reset(true);
                    }
                }
                return true;
                break;
            case Input::Key::N:
                if (keyPressed) {
                    showRoadOverlay = !showRoadOverlay;
                }
                return true;
                break;
            case Input::Key::M:
                if (keyPressed) {
                    showRoadSpline = !showRoadSpline;
                }
                return true;
                break;
            case Input::Key::Up:
                if (splineTest.bVertex) {
                    if (keyEvent.hasModifier(Input::Modifier::Ctrl)) {
                        mRoadNetwork.currentRoad->points[splineTest.index].addTangent(1.0f);
                        mRoadNetwork.currentRoad->solveRoad(splineTest.index);
                    }
                    else {
                        mRoadNetwork.currentRoad->points[splineTest.index].addHeight(0.1f);
                        mRoadNetwork.currentRoad->solveRoad(splineTest.index);
                    }
                }
                break;

            case Input::Key::Down:
                if (splineTest.bVertex) {
                    if (keyEvent.hasModifier(Input::Modifier::Ctrl)) {
                        mRoadNetwork.currentRoad->points[splineTest.index].addTangent(-1.0f);
                        mRoadNetwork.currentRoad->solveRoad(splineTest.index);
                    }
                    else {
                        mRoadNetwork.currentRoad->points[splineTest.index].addHeight(-0.1f);
                        mRoadNetwork.currentRoad->solveRoad(splineTest.index);
                    }
                }
                break;

            case Input::Key::Left:
                if (splineTest.bVertex) {
                    mRoadNetwork.currentRoad->points[splineTest.index].camber += 0.01f;
                    mRoadNetwork.currentRoad->points[splineTest.index].constraint = e_constraint::camber;
                    mRoadNetwork.currentRoad->solveRoad(splineTest.index);
                }
                break;
            case Input::Key::Right:
                if (splineTest.bVertex) {
                    mRoadNetwork.currentRoad->points[splineTest.index].camber -= 0.01f;
                    mRoadNetwork.currentRoad->points[splineTest.index].constraint = e_constraint::camber;
                    mRoadNetwork.currentRoad->solveRoad(splineTest.index);
                }
                break;
            }

            break;
        default:
            break;
        }
    }
    return false;
}


bool terrainManager::onMouseEvent(const MouseEvent& mouseEvent, glm::vec2 _screenSize, glm::vec2 _mouseScale, glm::vec2 _mouseOffset, Camera::SharedPtr _camera)
{

    if ((terrainMode == _terrainMode::vegetation) || (terrainMode == _terrainMode::glider && !useFreeCamWhileGliding))
    {
        glm::vec2 pos = (mouseEvent.pos * _mouseScale) + _mouseOffset;
        glm::vec2 diff;
        if (pos.x > 0 && pos.x < 1 && pos.y > 0 && pos.y < 1)
        {
            pos.y = 1.0 - pos.y;
            diff = pos - mousePositionOld;
            mousePositionOld = pos;
        }


        switch (mouseEvent.type)
        {
        case MouseEvent::Type::Move:
        {
            if (ImGui::IsMouseDown(1))
            {
                mouseVegPitch += diff.y * 3.0f;
                mouseVegPitch = glm::clamp(mouseVegPitch, -1.f, 1.5f);

                mouseVegYaw += diff.x * 10.0f;
                while (mouseVegYaw < 0) mouseVegYaw += 6.28318530718f;
                while (mouseVegYaw > 6.28318530718f) mouseVegYaw -= 6.28318530718f;
            }
            if (ImGui::IsMouseDown(0) && splineTest.bCtrl) // left
            {
                mouseVegHeight -= diff.y * 1.1;
                mouseVegHeight = clamp(mouseVegHeight, 0.0f, 10.0f);
            }
        }
        break;
        case MouseEvent::Type::Wheel:
        {
            float scale = 1.0 - mouseEvent.wheelDelta.y / 9.0f;
            mouseVegOrbit *= scale;
            mouseVegOrbit = glm::clamp(mouseVegOrbit, 0.1f, 5000.f);
        }
        break;
        }

        //mouseVegYaw += 0.002f;
        if ((terrainMode == _terrainMode::vegetation))
        {
            glm::vec3 camPos;
            camPos.y = sin(mouseVegPitch);
            camPos.x = cos(mouseVegPitch) * sin(mouseVegYaw);
            camPos.z = cos(mouseVegPitch) * cos(mouseVegYaw);
            _camera->setPosition((camPos * mouseVegOrbit) + float3(0, 1000 + mouseVegHeight, 0));
            _camera->setTarget(glm::vec3(0, 1000.f + mouseVegHeight, 0));

        }
        if ((terrainMode == _terrainMode::glider))
        {
            //_camera->setTarget(paraRuntime.ROOT);
        }
        return false;   // needs to ber false for Zoom to work, otherwise we consume the mouse
    }
    else {
        bool bEdit = false;
        if ((terrainMode == _terrainMode::terrafector))
        {
            bEdit = onMouseEvent_Stamps(mouseEvent, _screenSize, _camera);
        }
        if ((terrainMode == _terrainMode::roads))
        {
            bEdit = onMouseEvent_Roads(mouseEvent, _screenSize, _camera);
        }


        if (!bEdit)
        {
            glm::vec2 pos = (mouseEvent.pos * _mouseScale) + _mouseOffset;
            if (pos.x > 0 && pos.x < 1 && pos.y > 0 && pos.y < 1)
            {
                pos.y = 1.0 - pos.y;
                glm::vec3 N = glm::unProject(glm::vec3(pos * _screenSize, 0.0f), toGLM(_camera->getViewMatrix()), toGLM(_camera->getProjMatrix()), glm::vec4(0, 0, _screenSize));
                glm::vec3 F = glm::unProject(glm::vec3(pos * _screenSize, 1.0f), toGLM(_camera->getViewMatrix()), toGLM(_camera->getProjMatrix()), glm::vec4(0, 0, _screenSize));
                mouseDirection = glm::normalize(F - N);
                screenSize = _screenSize;
                mousePosition = _camera->getPosition();
                mouseCoord = mouseEvent.pos * _screenSize;



                switch (mouseEvent.type)
                {
                case MouseEvent::Type::Move:
                {
                    //if (bRightButton)  // PAN
                    if (ImGui::IsMouseDown(1))
                    {
                        glm::vec3 newPos = mouse.pan - mouseDirection * (_camera->getPosition().y - mouse.pan.y) / fabs(mouseDirection.y);
                        glm::vec3 deltaPos = newPos - _camera->getPosition();

                        glm::vec3 newTarget = _camera->getTarget() + deltaPos;
                        _camera->setPosition(newPos);
                        _camera->setTarget(newTarget);
                        hasChanged = true;
                    }

                    // orbit
                    if (ImGui::IsMouseDown(2))
                    {
                        glm::vec3 D = _camera->getTarget() - _camera->getPosition();
                        glm::vec3 U = glm::vec3(0, 1, 0);
                        glm::vec3 R = glm::normalize(glm::cross(U, D));
                        glm::vec2 diff = pos - mousePositionOld;
                        glm::mat4 yaw = glm::rotate(glm::mat4(1.0f), diff.x * 10.0f, glm::vec3(0, 1, 0));

                        glm::vec3 Dnorm = glm::normalize(D);
                        if ((Dnorm.y < -0.99f) && (diff.y < 0)) diff.y = 0;
                        if ((Dnorm.y > 0.0f) && (diff.y > 0)) diff.y = 0;

                        glm::mat4 pitch = glm::rotate(glm::mat4(1.0f), diff.y * 10.0f, R);
                        mouse.toGround = glm::vec4(mouseDirection, 0) * mouse.orbitRadius * yaw * pitch;
                        glm::vec4 newDir = glm::vec4(D, 0) * yaw * pitch;

                        _camera->setPosition(mouse.orbit - mouse.toGround);
                        _camera->setTarget(mouse.orbit - mouse.toGround + glm::vec3(newDir));
                        hasChanged = true;
                    }
                    mousePositionOld = pos;
                }
                break;
                case MouseEvent::Type::Wheel:
                {
                    if (mouse.hit)
                    {
                        float scale = 1.0 - mouseEvent.wheelDelta.y / 6.0f;
                        mouse.toGround *= scale;
                        glm::vec3 newPos = mouse.terrain - mouse.toGround;
                        glm::vec3 deltaPos = newPos - _camera->getPosition();
                        glm::vec3 newTarget = _camera->getTarget() + deltaPos;

                        _camera->setPosition(newPos);
                        _camera->setTarget(newTarget);
                        hasChanged = true;
                    }
                }
                break;
                case MouseEvent::Type::ButtonDown:
                {
                    if (mouseEvent.button == Input::MouseButton::Middle)
                    {
                        mouse.orbitRadius = glm::length(mouse.toGround);
                    }
                }
                break;
                case MouseEvent::Type::ButtonUp:
                {
                }
                break;
                }


                // rebuild from new camera
                {
                    glm::vec3 N = glm::unProject(glm::vec3(pos * _screenSize, 0.0f), toGLM(_camera->getViewMatrix()), toGLM(_camera->getProjMatrix()), glm::vec4(0, 0, _screenSize));
                    glm::vec3 F = glm::unProject(glm::vec3(pos * _screenSize, 1.0f), toGLM(_camera->getViewMatrix()), toGLM(_camera->getProjMatrix()), glm::vec4(0, 0, _screenSize));
                    mouseDirection = glm::normalize(F - N);
                    screenSize = _screenSize;
                    mousePosition = _camera->getPosition();
                    mouseCoord = mouseEvent.pos * _screenSize;
                }

                return false;
            }
        }
        return true;
    }
}



bool terrainManager::onMouseEvent_Stamps(const MouseEvent& mouseEvent, glm::vec2 _screenSize, Camera::SharedPtr _camera)
{
    switch (mouseEvent.type)
    {
    case MouseEvent::Type::Move:
    {
    }
    break;
    case MouseEvent::Type::Wheel:
    {
        if (splineTest.bCtrl)
        {
            if (splineTest.bAlt)
            {
                mCurrentStamp.rotation += mouseEvent.wheelDelta.y / 30.0f;
            }
            else if (splineTest.bShift)
            {
                mCurrentStamp.height += mouseEvent.wheelDelta.y / 30.0f;
            }
            else
            {
                mCurrentStamp.rotation += mouseEvent.wheelDelta.y / 3.0f;
            }


            return true;
        }

        if (splineTest.bShift)
        {
            if (splineTest.bAlt)
            {
                mCurrentStamp.scale.x *= 1.f + mouseEvent.wheelDelta.y / 30.0f;
            }
            else
            {
                mCurrentStamp.scale.y *= 1.f + mouseEvent.wheelDelta.y / 30.0f;
            }
            return true;
        }


    }
    case MouseEvent::Type::ButtonDown:
    {
        if (mouseEvent.button == Input::MouseButton::Left)
        {
            if (stampEditPosisiton >= mRoadStampCollection.stamps.size())
            {
                mRoadStampCollection.add(mCurrentStamp);
                stampEditPosisiton = mRoadStampCollection.stamps.size();
            }
            else
            {
                mRoadStampCollection.stamps[stampEditPosisiton] = mCurrentStamp;    // FIXME does nto add material could cause a problem
                stampEditPosisiton = mRoadStampCollection.stamps.size();
            }

            //allStamps_to_Bezier();
            allStamps_to_Terrafector();
            reset(true);
        }

        if (mouseEvent.button == Input::MouseButton::Right)
        {
            if (splineTest.bAlt)
            {
                mCurrentStamp.height = 0;
                mCurrentStamp.scale = { 1, 1 };
            }
            if (splineTest.bCtrl)
            {
                // delete closest but set current stamp to it
                int idx = mRoadStampCollection.find(split.feedback.tum_Position);
                if (idx >= 0)
                {
                    mCurrentStamp = mRoadStampCollection.stamps[idx];
                    terrafectorEditorMaterial::static_materials.selectedMaterial = mCurrentStamp.material;
                    mRoadStampCollection.stamps[idx].scale = { 0, 0 };  // then set scale to 0 if this is justa delte
                    stampEditPosisiton = idx;
                    allStamps_to_Terrafector();
                    reset(true);
                }
            }
            if (splineTest.bShift)
            {
                // Just copy current stamp
                int idx = mRoadStampCollection.find(split.feedback.tum_Position);
                if (idx >= 0)
                {
                    mCurrentStamp = mRoadStampCollection.stamps[idx];
                    terrafectorEditorMaterial::static_materials.selectedMaterial = mCurrentStamp.material;
                }
            }
        }
    }
    }

    return false;
}


bool terrainManager::onMouseEvent_Roads(const MouseEvent& mouseEvent, glm::vec2 _screenSize, Camera::SharedPtr _camera)
{
    static bool bDragEvent;
    static glm::vec2 prevPos;
    glm::vec2 diff = mouseEvent.pos - prevPos;
    prevPos = mouseEvent.pos;

    switch (mouseEvent.type)
    {
    case MouseEvent::Type::Move:
    {
        mRoadNetwork.testHit(split.feedback.tum_Position);

        bDragEvent = true;
        if (bLeftButton)
        {
            if (splineTest.bVertex) {
                if (mRoadNetwork.currentRoad) {
                    if (splineTest.bCtrl)
                    {
                        mRoadNetwork.currentRoad->points[splineTest.index].B += diff.x * 10.0f;
                        mRoadNetwork.currentRoad->points[splineTest.index].B = __min(1, __max(0, mRoadNetwork.currentRoad->points[splineTest.index].B));
                    }
                    else if (splineTest.bShift)
                    {
                        mRoadNetwork.currentRoad->points[splineTest.index].tangent_Offset += diff.x * 100.0f;
                        mRoadNetwork.currentRoad->points[splineTest.index].solveMiddlePos();
                    }
                    else if (splineTest.bAlt)
                    {
                    }
                    else
                    {
                        mRoadNetwork.currentRoad->points[splineTest.index].setAnchor(split.feedback.tum_Position, split.feedback.tum_Normal, m_tiles[split.feedback.tum_idx].lod);
                    }

                    mRoadNetwork.currentRoad->solveRoad(splineTest.index);
                }
                else if (mRoadNetwork.intersectionSelectedRoad) {
                    mRoadNetwork.intersectionSelectedRoad->points[splineTest.index].setAnchor(split.feedback.tum_Position, split.feedback.tum_Normal, m_tiles[split.feedback.tum_idx].lod);
                    mRoadNetwork.intersectionSelectedRoad->solveRoad(splineTest.index);
                    mRoadNetwork.solveIntersection();
                }
            }

            if (mRoadNetwork.currentIntersection) {
                if (splineTest.bCenter) {
                    mRoadNetwork.currentIntersection->updatePosition(split.feedback.tum_Position, split.feedback.tum_Normal, m_tiles[split.feedback.tum_idx].lod);
                    {
                        FALCOR_PROFILE("solveIntersection");
                        mRoadNetwork.solveIntersection();
                    }
                }

                if (splineTest.cornerNum >= 0) {
                    mRoadNetwork.currentIntersection->updateCorner(split.feedback.tum_Position, split.feedback.tum_Normal, splineTest.cornerNum, splineTest.cornerFlag);
                    mRoadNetwork.currentIntersection->roadLinks[splineTest.cornerNum].cornerType = typeOfCorner::artistic;
                }
            }
            updateDynamicRoad(true);
        }
    }
    break;
    case MouseEvent::Type::Wheel:

        if (splineTest.bShift) {
            if (splineTest.bVertex && splineTest.bCtrl) {
                if (mRoadNetwork.currentRoad) {
                    mRoadNetwork.incrementLane(-1, mouseEvent.wheelDelta.y / 10.0f, mRoadNetwork.currentRoad, splineTest.bAlt);
                    mRoadNetwork.currentRoad->solveRoad(splineTest.index);
                    updateDynamicRoad(true);
                }
                else if (mRoadNetwork.intersectionSelectedRoad) {
                    mRoadNetwork.incrementLane(-1, mouseEvent.wheelDelta.y / 10.0f, mRoadNetwork.intersectionSelectedRoad, splineTest.bAlt);
                    mRoadNetwork.intersectionSelectedRoad->solveRoad(splineTest.index);
                    mRoadNetwork.solveIntersection();
                    updateDynamicRoad(true);
                }
                return true;
            }
        }
        else {
            if (splineTest.bVertex && splineTest.bCtrl) {
                if (mRoadNetwork.currentRoad) {
                    mRoadNetwork.incrementLane(splineTest.index, mouseEvent.wheelDelta.y / 10.0f, mRoadNetwork.currentRoad, splineTest.bAlt);
                    mRoadNetwork.currentRoad->solveRoad(splineTest.index);
                    updateDynamicRoad(true);
                }
                else if (mRoadNetwork.intersectionSelectedRoad) {
                    mRoadNetwork.incrementLane(splineTest.index, mouseEvent.wheelDelta.y / 10.0f, mRoadNetwork.intersectionSelectedRoad, splineTest.bAlt);
                    mRoadNetwork.intersectionSelectedRoad->solveRoad(splineTest.index);
                    mRoadNetwork.solveIntersection();
                    updateDynamicRoad(true);
                }
                return true;
            }


            if (splineTest.bCtrl && mRoadNetwork.currentIntersection) {
                if (mRoadNetwork.currentIntersection && splineTest.cornerNum >= 0) {
                    if (splineTest.bAlt) {
                        mRoadNetwork.currentIntersection->roadLinks[splineTest.cornerNum].cornerRadius += mouseEvent.wheelDelta.y / 20.0f;
                        mRoadNetwork.currentIntersection->roadLinks[splineTest.cornerNum].cornerType = typeOfCorner::radius;
                    }
                    else {
                        mRoadNetwork.currentIntersection->roadLinks[splineTest.cornerNum].cornerRadius += mouseEvent.wheelDelta.y / 5.0f;
                        mRoadNetwork.currentIntersection->roadLinks[splineTest.cornerNum].cornerType = typeOfCorner::radius;
                    }
                    if (mRoadNetwork.currentIntersection->roadLinks[splineTest.cornerNum].cornerRadius < 0.2f) mRoadNetwork.currentIntersection->roadLinks[splineTest.cornerNum].cornerRadius = 0.2f;
                    mRoadNetwork.solveIntersection();
                    updateDynamicRoad(true);
                    return true;
                }
            }
        }
        break;

    case MouseEvent::Type::ButtonDown:
    {
        if (mouseEvent.button == Input::MouseButton::Left)
        {
            bLeftButton = true;

            // SUB selection
            if (splineTest.bCtrl) {
                if (splineTest.bVertex) {
                    mRoadNetwork.selectionType = 1;
                    int mid = (mRoadNetwork.selectFrom + mRoadNetwork.selectTo) / 2;
                    if (splineTest.index < mRoadNetwork.selectFrom) {
                        mRoadNetwork.selectFrom = splineTest.index;
                    }
                    else if (splineTest.index > mRoadNetwork.selectTo) {
                        mRoadNetwork.selectTo = splineTest.index;
                    }
                    else {
                        if (splineTest.index < mid) {
                            mRoadNetwork.selectFrom = splineTest.index;
                        }
                        else {
                            mRoadNetwork.selectTo = splineTest.index;
                        }
                    }

                }
            }


            // Selection but now add all the possible subselections
            if (splineTest.bCtrl) {								// selection


                if (mRoadNetwork.AI_path_mode) {
                    mRoadNetwork.addRoad();
                }
                else
                {
                    mRoadNetwork.doSelect(split.feedback.tum_Position);

                    if (mRoadNetwork.bHIT) {
                        mRoadNetwork.setEditRight(mRoadNetwork.hitRoadRight);
                        mRoadNetwork.setEditLane(mRoadNetwork.hitRoadLane);
                        //updateStaticRoad();

                    }
                    updateDynamicRoad(true);

                }
            }
            else if (mRoadNetwork.currentRoad) {
                if (!splineTest.bVertex && !splineTest.bSegment) {
                    mRoadNetwork.currentRoad->pushPoint(split.feedback.tum_Position, split.feedback.tum_Normal, m_tiles[split.feedback.tum_idx].lod);
                    updateDynamicRoad(true);
                }

                if (splineTest.bSegment) {
                    mRoadNetwork.currentRoad->insertPoint(splineTest.index, split.feedback.tum_Position, split.feedback.tum_Normal, m_tiles[split.feedback.tum_idx].lod);
                    updateDynamicRoad(true);
                }
            }

        }
        if (mouseEvent.button == Input::MouseButton::Middle)
        {
            bMiddelButton = true;
        }
        if (mouseEvent.button == Input::MouseButton::Right)
        {
            bRightButton = true;

            if (splineTest.bCtrl)
            {
                if (mRoadNetwork.currentRoad && splineTest.bVertex && mRoadNetwork.currentRoad->points.size() > splineTest.index) {
                    mRoadNetwork.currentRoad->deletePoint(splineTest.index);
                    updateDynamicRoad(true);
                }

                if (mRoadNetwork.currentIntersection) {
                    if (splineTest.cornerNum >= 0) {
                        mRoadNetwork.currentIntersection->roadLinks[splineTest.cornerNum].cornerType = typeOfCorner::automatic;
                        mRoadNetwork.solveIntersection();
                        updateDynamicRoad(true);
                    }
                    if (splineTest.bCenter) {
                        mRoadNetwork.currentIntersection->customNormal = false;
                    }
                }
            }
        }
    }
    break;


    case MouseEvent::Type::ButtonUp:
    {
        if (mouseEvent.button == Input::MouseButton::Left)
        {
            bLeftButton = false;
            bDragEvent = false;
        }
        if (mouseEvent.button == Input::MouseButton::Middle)
        {
            bMiddelButton = false;
            bDragEvent = false;
        }
        if (mouseEvent.button == Input::MouseButton::Right)
        {
            //bShowRoadPopup = true;
            bDragEvent = false;
            //popupPos = mouseEvent.pos * m_ScreenSize;
            bRightButton = false;
        }
    }
    break;
    }
    return false;
}


void terrainManager::onHotReload(HotReloadFlags reloaded)
{}



bool testBezier(cubicDouble& _bez, glm::vec3 _pos, float _size)
{
    return false;
}



void terrainManager::bezierRoadstoLOD(uint _lod)
{
#define outsideLine 	(roadNetwork::staticIndexData[i].A >> 31) & 0x1
#define insideLine 		(roadNetwork::staticIndexData[i].A >> 30) & 0x1
#define index  			roadNetwork::staticIndexData[i].A & 0x1ffff

    //std::vector<bezierLayer> lod4[16][16];
    //std::vector<bezierLayer> lod6[64][64];
    //std::vector<bezierLayer> lod8[256][256];
    // clear
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            splines.lod4[y][x].clear();
        }
    }
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            splines.lod6[y][x].clear();
        }
    }
    for (int y = 0; y < 256; y++) {
        for (int x = 0; x < 256; x++) {
            splines.lod8[y][x].clear();
        }
    }

    fprintf(terrafectorSystem::_logfile, "\n\n\nbezierRoadstoLOD\n");

    for (uint i = 0; i < splines.numStaticSplinesIndex; i++)
    {
        cubicDouble& BEZ = roadNetwork::staticBezierData[index];

        float3 perpStart = glm::normalize(BEZ.data[1][0] - BEZ.data[0][0]);
        float3 perpEnd = glm::normalize(BEZ.data[1][3] - BEZ.data[0][3]);

        float w0 = ((roadNetwork::staticIndexData[i].B >> 14) & 0x3fff) * 0.002f - 16.0f;			// -32 .. 33.536m in mm resolution
        float w1 = (roadNetwork::staticIndexData[i].B & 0x3fff) * 0.002f - 16.0f;

        float3 startInside = float3(BEZ.data[insideLine][0]) + w0 * perpStart;
        float3 startOutside = float3(BEZ.data[outsideLine][0]) + w1 * perpStart;
        float3 endInside = float3(BEZ.data[insideLine][3]) + w0 * perpStart;
        float3 endOutside = float3(BEZ.data[outsideLine][3]) + w1 * perpStart;

        float splineWidth = __max(glm::length(startOutside - startInside), glm::length(endOutside - endInside));

        for (int lod = 4; lod <= 8; lod += 2)
        {
            float scale = 1.0f / pow(2, lod);
            float tileSize = settings.size * scale;
            float pixelSize = tileSize / 248.0f;
            float borderSize = (pixelSize * 4.0f) + splineWidth;    // add splineWidth to compensate for curve
            //??? How to boos tarmac since left right that one is double
            // boost lod4 a little since I dont k ow trarmact yert
            if (lod == 4) splineWidth *= 2.0f; // doesnt work, we need Special splines that includes runoff areas
            if ((lod == 8) || splineWidth > pixelSize)
            {
                float xMin = __min(__min(BEZ.data[0][0].x, BEZ.data[0][3].x), __min(BEZ.data[1][0].x, BEZ.data[1][3].x)) - 80;      // 39 is the buffer size for lod4
                float xMax = __max(__max(BEZ.data[0][0].x, BEZ.data[0][3].x), __max(BEZ.data[1][0].x, BEZ.data[1][3].x)) + 80;
                float yMin = __min(__min(BEZ.data[0][0].z, BEZ.data[0][3].z), __min(BEZ.data[1][0].z, BEZ.data[1][3].z)) - 80;
                float yMax = __max(__max(BEZ.data[0][0].z, BEZ.data[0][3].z), __max(BEZ.data[1][0].z, BEZ.data[1][3].z)) + 80;

                float halfsize = ecotopeSystem::terrainSize / 2.f;
                float blocksize = ecotopeSystem::terrainSize / 16.f;
                uint gMinX = (uint)floor((xMin - borderSize + halfsize) / tileSize);
                uint gMaxX = (uint)ceil((xMax + borderSize + halfsize) / tileSize);
                uint gMinY = (uint)floor((yMin - borderSize + halfsize) / tileSize);
                uint gMaxY = (uint)ceil((yMax + borderSize + halfsize) / tileSize);

                for (int y = gMinY; y < gMaxY; y++) {
                    for (int x = gMinX; x < gMaxX; x++)
                    {
                        switch (lod)
                        {
                        case 4: splines.lod4[y][x].push_back(roadNetwork::staticIndexData[i]); break;
                        case 6: splines.lod6[y][x].push_back(roadNetwork::staticIndexData[i]); break;
                        case 8: splines.lod8[y][x].push_back(roadNetwork::staticIndexData[i]); break;
                        }

                    }
                }
            }
        }
    }

    FILE* file = fopen((settings.dirRoot + "/bake/roadbeziers_lod4.gpu").c_str(), "wb");
    FILE* datafile = fopen((settings.dirRoot + "/bake/roadbeziers_lod4_data.gpu").c_str(), "wb");
    if (file && datafile)
    {
        //uint lod = 4;
        //fwrite(&lod, sizeof(uint), 1, file);

        uint start = 0;
        uint largest = 0;
        for (int y = 0; y < 16; y++) {
            for (int x = 0; x < 16; x++) {
                int size = splines.lod4[y][x].size();
                fwrite(&size, sizeof(uint), 1, file);
                fwrite(&start, sizeof(uint), 1, file);

                if (size > 0)
                {
                    largest = __max(largest, size);
                    splines.indexData_LOD4->setBlob(splines.lod4[y][x].data(), start * sizeof(bezierLayer), size * sizeof(bezierLayer));
                    fwrite(splines.lod4[y][x].data(), sizeof(bezierLayer), size, datafile);
                }
                splines.startOffset_LOD4[y][x] = start;
                splines.numIndex_LOD4[y][x] = size;
                start += size;
                //fprintf(terrafectorSystem::_logfile, "%6d", size);
            }
            //fprintf(terrafectorSystem::_logfile, "\n");
        }
        fclose(file);
        fclose(datafile);

        fprintf(terrafectorSystem::_logfile, "\nLOD 4. Total beziers %d from %d.   Most beziers in a block = %d\n", start, splines.numStaticSplinesIndex, largest);
        fprintf(terrafectorSystem::_logfile, "using %3.1f Mb\n", ((float)(start * sizeof(bezierLayer)) / 1024.0f / 1024.0f));
    }



    file = fopen((settings.dirRoot + "/bake/roadbeziers_lod6.gpu").c_str(), "wb");
    datafile = fopen((settings.dirRoot + "/bake/roadbeziers_lod6_data.gpu").c_str(), "wb");
    if (file && datafile)
    {
        //uint lod = 6;
        //fwrite(&lod, sizeof(uint), 1, file);

        uint start = 0;
        uint largest = 0;
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
                int size = splines.lod6[y][x].size();
                fwrite(&size, sizeof(uint), 1, file);
                fwrite(&start, sizeof(uint), 1, file);


                if (size > 0)
                {
                    largest = __max(largest, size);
                    splines.indexData_LOD6->setBlob(splines.lod6[y][x].data(), start * sizeof(bezierLayer), size * sizeof(bezierLayer));
                    fwrite(splines.lod6[y][x].data(), sizeof(bezierLayer), size, datafile);
                }
                splines.startOffset_LOD6[y][x] = start;
                splines.numIndex_LOD6[y][x] = size;
                start += size;
            }
        }
        fclose(file);
        fclose(datafile);

        fprintf(terrafectorSystem::_logfile, "\nLOD 6. Total beziers %d from %d.   Most beziers in a block = %d\n", start, splines.numStaticSplinesIndex, largest);
        fprintf(terrafectorSystem::_logfile, "using %3.1f Mb\n", ((float)(start * sizeof(bezierLayer)) / 1024.0f / 1024.0f));
    }


    file = fopen((settings.dirRoot + "/bake/roadbeziers_lod8.gpu").c_str(), "wb");
    datafile = fopen((settings.dirRoot + "/bake/roadbeziers_lod8_data.gpu").c_str(), "wb");
    if (file && datafile)
    {
        //uint lod = 8;
        //fwrite(&lod, sizeof(uint), 1, file);

        uint start = 0;
        uint largest = 0;
        for (int y = 0; y < 256; y++) {
            for (int x = 0; x < 256; x++) {
                int size = splines.lod8[y][x].size();
                fwrite(&size, sizeof(uint), 1, file);
                fwrite(&start, sizeof(uint), 1, file);

                if (size > 0)
                {
                    largest = __max(largest, size);
                    splines.indexData_LOD8->setBlob(splines.lod8[y][x].data(), start * sizeof(bezierLayer), size * sizeof(bezierLayer));
                    fwrite(splines.lod8[y][x].data(), sizeof(bezierLayer), size, datafile);
                }
                splines.startOffset_LOD8[y][x] = start;
                splines.numIndex_LOD8[y][x] = size;
                start += size;
            }
        }
        fclose(file);
        fclose(datafile);

        fprintf(terrafectorSystem::_logfile, "\nLOD 8. Total beziers %d from %d.   Most beziers in a block = %d\n", start, splines.numStaticSplinesIndex, largest);
        fprintf(terrafectorSystem::_logfile, "using %3.1f Mb\n", ((float)(start * sizeof(bezierLayer)) / 1024.0f / 1024.0f));
    }





    file = fopen((settings.dirRoot + "/bake/roadbeziers_bezier.gpu").c_str(), "wb");
    if (file)
    {
        fwrite(roadNetwork::staticBezierData.data(), sizeof(cubicDouble), splines.numStaticSplines, file);
        fclose(file);
    }


}




//void terrainManager::cfdStart()
//{
//
//}
//
//void terrainManager::cfdThread()
//{
//    bool firstStart = true;
//    cfd.numLines = 200;
//    cfd.flowlines.resize(cfd.numLines * 100);
//
//    //cfd.clipmap.heightToSmap(settings.dirRoot + "/gis/_export/root4096.bil");
//    cfd.rootPath = settings.dirRoot + "\\cfd\\";
//    cfd.rootFile = "Walenstad_August5_1500.skewT_5000";
//    //cfd.clipmap.loadSkewT(cfd.rootPath + cfd.rootFile);
//    //cfd.clipmap.build(settings.dirRoot + "/cfd");
//
//    //uint seconds = 0;// 27040;
//    //cfd.clipmap.import_V(settings.dirRoot + "/cfd/east3ms__" + std::to_string(seconds) + "sec");
//
//    //cfd.clipmap.setWind(float3(2, 0, 0), float3(5, 0, 0));
//    //cfd.clipmap.simulate_start(32.0f);
//
//
//    fprintf(terrafectorSystem::_logfile, "cfdThread()\n");
//    fprintf(terrafectorSystem::_logfile, "T %f %f %f\n", cfd.clipmap.skewT_corners_Data[0][0].x, cfd.clipmap.skewT_corners_Data[0][50].x, cfd.clipmap.skewT_corners_Data[0][99].x);
//    fprintf(terrafectorSystem::_logfile, "H %f %f %f\n", cfd.clipmap.skewT_corners_Data[0][0].y, cfd.clipmap.skewT_corners_Data[0][50].y, cfd.clipmap.skewT_corners_Data[0][99].y);
//    fprintf(terrafectorSystem::_logfile, "V %f %f %f\n", glm::length(cfd.clipmap.skewT_corners_V[0][0]), glm::length(cfd.clipmap.skewT_corners_V[0][50]), glm::length(cfd.clipmap.skewT_corners_V[0][99]));
//    fflush(terrafectorSystem::_logfile);
//    /*
//    uint saveidx = 0;
//    for (int k = 0; k < 256 * 64 * 100; k++)   //1500 = 937 seconds in 210
//    //for (int k = 0; k < 256 * 300; k++)   //1500 = 937 seconds in 210
//    {
//        cfd.clipmap.simulate(20.0f);
//        if (k % 64 == 0)
//        {
//            uint seconds = (k * 20) / 32;
//            cfd.clipmap.export_V(settings.dirRoot + "/cfd/dump/east3ms__" + std::to_string(saveidx) + "sec");
//            saveidx++;
//        }
//    } /**/
//
//    // infinite loop
//    static int k = 0;
//    while (1)
//    {
//        if (cfd.pause)
//        {
//            Sleep(10);
//        }
//        else
//        {
//            cfd.clipmap.shiftOrigin(cfd.originRequest);
//
//            if (firstStart || cfd.clipmap.windrequest)
//            {
//                if (cfd.clipmap.windSeperateSkewt)
//                {
//                    cfd.clipmap.setWind(cfd.clipmap.newWind, cfd.clipmap.newWind);
//                }
//                else
//                {
//                    cfd.clipmap.loadSkewT(cfd.rootPath + cfd.rootFile);
//                }
//                cfd.clipmap.simulate_start(32.0f);
//                cfd.clipmap.windrequest = false;
//                firstStart = false;
//                fprintf(terrafectorSystem::_logfile, "cfd.clipmap.setWind on %s\n", cfd.rootPath.c_str());
//                if (cfd.clipmap.windSeperateSkewt) {
//                    fprintf(terrafectorSystem::_logfile, "windSeperateSkewt (%2.1f, %2.1f, %2.1f) m/s\n", cfd.clipmap.newWind.x, cfd.clipmap.newWind.y, cfd.clipmap.newWind.z);
//                }
//            }
//
//
//
//            cfd.clipmap.simulate(32.0f);
//
//            if (k % 2 == 0)
//            {
//                float3 R = glm::normalize(glm::cross(float3(0, 1, 0), cfd.velocityAnswers[0]));
//                cfd.clipmap.streamlines(cfd.originRequest, cfd.flowlines.data(), R);
//                cfd.newFlowLines = true;
//            }
//
//            for (int i = 0; i < 4; i++) // Look yp my glider velocity requests
//            {
//                float rootAltitude = 350.f;   // in voxel space
//                float3 origin = float3(-20000, rootAltitude, -20000);
//                float3 P = (cfd.velocityRequets[i] - origin) * cfd.clipmap.lods[0].oneOverSize;
//                float3 p5 = P * 32.f;
//                p5 -= cfd.clipmap.lods[5].offset;
//                cfd.velocityAnswers[i] = cfd.clipmap.lods[5].sample(cfd.clipmap.lods[5].v, p5);
//            }
//
//
//        }
//        k++;
//    }
//}