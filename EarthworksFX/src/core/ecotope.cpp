#include "terrain.h"    // brings the hlsli-shared structs/aliases + ecotope.h

#include "cereal/archives/binary.hpp"
#include "cereal/archives/json.hpp"
#include "cereal/archives/xml.hpp"
#include <sstream>
#include <fstream>
#include <cstdlib>   // __min (MSVC macro)

#include "ots/Log.hpp"

// CRITICAL, for some reason: with optimization on, the cereal JSON read below
// fails. Do not remove without re-testing an ecosystem load in a Release build.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif


namespace
{
//mimic hlsl saturate()
inline float saturate(float v)
{
    return v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
}
} // namespace


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
    // Empty names are common in the Steg data set; loading "_resPath +" an
    // empty name resolved to the directory itself and spammed one
    // createFromFile error per slot (44 per run). Null textures are handled
    // downstream (dummy-bound).
    auto loadIfNamed = [&](const std::string& name, bool generateMips) -> ew::Texture::SharedPtr {
        return name.empty() ? nullptr : ew::Texture::createFromFile(_resPath + name, generateMips, false);
    };
    texAlbedo = loadIfNamed(albedoName, true);
    texNoise = loadIfNamed(noiseName, false);
    texDisplacement = loadIfNamed(displacementName, false);
    texRoughness = loadIfNamed(roughnessName, false);
    texAO = loadIfNamed(aoName, false);
}


void ecotopeSystem::load(std::string _path, std::string _resourcePath)
{
    resPath = _resourcePath;
    std::ifstream is(_path);
    if (!is.good())
    {
        spdlog::error("ecotope: cannot open ecosystem '{}'", _path);
        return;
    }
    cereal::JSONInputArchive archive(is);
    serialize(archive);
    rebuildRuntime();

    for (size_t ect = 0; ect < ecotopes.size(); ect++)
    {
        ecotopes[ect].reloadTextures(ecotopeSystem::resPath);
    }
}


void ecotopeSystem::addEcotope() {
    ecotope E;
    ecotopes.push_back(E);
}



void ecotopeSystem::rebuildRuntime() {

    change = true;

    constantbuffer.numEcotopes = (int)ecotopes.size();

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

    // TODO: numEcotopes comes straight from the JSON and is not clamped, while
    // constantbuffer.ect is [12][5] and texScales [12]. An ecosystem file with 13 or
    // more ecotopes overruns the static_asserted cbuffer struct below.
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
            saturate(ecotopes[ect].totalPlantDensity[lod]); // clamp 0-1  (result deliberately discarded - the __min below does the clamping)

            plantDensity[ect][lod] = (uint)(__min(ecotopes[ect].totalPlantDensity[lod], 1.0f) * 65535);

            int slotCount = 0;
            for (auto& P : ecotopes[ect].plants)
            {
                if (P.lod == lod) {
                    //const std::string I = P.name.substr(0, P.name.find_last_of("_"));
                    //P.index = std::stoi(I);
                    // FIXME importBinary must not load the same one again needs a cache if I am going to use it like this

                    // importBinary is loud-and-graceful on a missing or
                    // corrupt binary: it returns -1, P.index keeps its
                    // serialized value and the plant stays dormant.
                    int idx = ecotopeSystem::pVegetation ? ecotopeSystem::pVegetation->importBinary(P.path) : -1;   // return vlaue still wronf since we load 4 variations
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
        // TODO: createTypedUint takes an ELEMENT count and is fed a byte size here,
        // so the allocation ends up roughly double the 24*16*64 uints actually
        // uploaded. Deliberate - do not "tidy" it into an underflow. Size this and
        // the pdBuffer below correctly and explicitly instead.
        piBuffer = ew::Buffer::createTypedUint(sizeof(uint) * 12 * 16 * 65, Diligent::BIND_SHADER_RESOURCE, nullptr, "ecotope plantIndex");
    }
    piBuffer->setBlob(plantIndex, 0, sizeof(uint) * 24 * 16 * 64);

    if (pdBuffer == nullptr)
    {
        pdBuffer = ew::Buffer::createTypedUint(sizeof(uint) * 12 * 16 * 65, Diligent::BIND_SHADER_RESOURCE, nullptr, "ecotope plantDensity");
    }
    pdBuffer->setBlob(plantDensity, 0, sizeof(uint) * 24 * 16);
}
