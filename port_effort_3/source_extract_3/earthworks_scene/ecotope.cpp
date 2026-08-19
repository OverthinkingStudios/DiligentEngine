#include "ecotope.h"

#include "cereal/archives/binary.hpp"
#include "cereal/archives/json.hpp"
#include "cereal/archives/xml.hpp"
#include <sstream>
#include <fstream>

// FIXME THIS ONE IS CRITICAL FOR SOME REAASON JSON error
#pragma optimize("", off)

#include "vegetationBuilder.h"

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


void ecotope::reloadTextures(std::string _resPath)
{
    //fprintf(terrafectorSystem::_logfile, "loading ecotope resources from  %s\n", resPath.c_str());

    texAlbedo = Texture::createFromFile(_resPath + albedoName, true, false);
    texNoise = Texture::createFromFile(_resPath + noiseName, false, false);
    texDisplacement = Texture::createFromFile(_resPath + displacementName, false, false);
    texRoughness = Texture::createFromFile(_resPath + roughnessName, false, false);
    texAO = Texture::createFromFile(_resPath + aoName, false, false);
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
    std::ifstream is(_path);
    cereal::JSONInputArchive archive(is);
    serialize(archive);
    rebuildRuntime();

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



void ecotopeSystem::addEcotope() {
    ecotope E;
    ecotopes.push_back(E);
}



void ecotopeSystem::rebuildRuntime() {

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
                    
                    int idx =  ecotopeSystem::pVegetation->importBinary(P.path);   // return vlaue still wronf since we load 4 variations
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
        piBuffer = Buffer::createTyped(ResourceFormat::R32Uint, sizeof(uint) * 12 * 16 * 65, Resource::BindFlags::ShaderResource);
    }
    piBuffer->setBlob(plantIndex, 0, sizeof(uint) * 24 * 16 * 64);

    if (pdBuffer == nullptr)
    {
        pdBuffer = Buffer::createTyped(ResourceFormat::R32Uint, sizeof(uint) * 12 * 16 * 65, Resource::BindFlags::ShaderResource);
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
