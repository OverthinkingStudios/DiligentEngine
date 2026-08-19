#pragma once

// ---------------------------------------------------------------------------
// Ecotopes: the per-terrain material/plant palettes the tile bake samples.
//
// Include this from terrain.h AFTER the hlsli-defines block - the declarations
// below rely on the HLSL-shared structs and aliases (float2/float4/int2/uint,
// the groundcover/terrain/gpuLights defines) already being in scope.
// ---------------------------------------------------------------------------

#include "ewResources.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "cereal/cereal.hpp"
#include "cereal/types/map.hpp"
#include "cereal/types/vector.hpp"
#include "cereal/types/list.hpp"
#include "cereal/types/array.hpp"
#include "cereal/types/string.hpp"

// Defined in vegetationBuilder.h, which terrain.h includes after this header;
// the pointer below only needs the declaration.
class _rootPlant;


/*	This is incomplete, sort of placeholder till I work out the rendering part*/
struct _displayPlant
{
	float				density = 0.25f;		  // redefine relative to size - think about it
	float				scale = 1.0f;               // likely unused
	float				scaleVariation = 0.3f;
	std::string			name;
	std::string			path;
    int                 lod = 10;
    uint                index = 1;      // on load from some sort of cache  ZERO is unused

    int percentageOfLodTotalInt;

	template<class Archive>
	void serialize(Archive & archive)
	{
		archive(CEREAL_NVP(density));
		archive(CEREAL_NVP(scale));
		archive(CEREAL_NVP(scaleVariation));
		archive(CEREAL_NVP(name));
        archive(CEREAL_NVP(path));
        archive(CEREAL_NVP(lod));
	}
};


class ecotope {
public:
	ecotope();
	virtual ~ecotope() { ; }

	void reloadTextures(std::string _resPath);

	template<class Archive>
	void serialize(Archive & archive)
	{
		archive(CEREAL_NVP(name));
		archive(CEREAL_NVP(weights));
		archive(CEREAL_NVP(albedoName), CEREAL_NVP(noiseName), CEREAL_NVP(displacementName), CEREAL_NVP(roughnessName), CEREAL_NVP(aoName));
		archive(CEREAL_NVP(plants));
	}
	std::string name = "new ecotope";
	std::array<std::array<float, 4>, 6> weights;	  // maps to 5 float4's but better for cereal to stay STL and do memcpy - waste space for consistency

	std::string albedoName;
	std::string noiseName;
	std::string displacementName;
	std::string roughnessName;
	std::string aoName;

    std::vector<_displayPlant> plants;
    std::array<float, 16> totalPlantDensity;

	ew::Texture::SharedPtr	texAlbedo;
	ew::Texture::SharedPtr	texNoise;
	ew::Texture::SharedPtr	texDisplacement;
	ew::Texture::SharedPtr	texRoughness;
	ew::Texture::SharedPtr	texAO;

};


struct ecotopeGpuConstants
{
    int numEcotopes = 0;
    int debug = -1;
    float pixelSize;
    int lod;

    float2 lowResOffset;
    float lowResSize;
    uint tileIndex;

    int2 tileXY;
    float2 padd2;

    float4 ect[12][5];	  // 16		2560
    float4 texScales[12]; // texture size, displacement scale, pixSize, 0
    //uint4 totalDensity[12][16];
    //uint4 plantIndex[12][64];  // 2576	5648        // u16 better but not in constant buffer
};
// Uploaded as a raw cbuffer blob (setBlob) - this layout must equal the DXC
// cbuffer layout of compute_tileEcotopes.hlsl's gConstants byte-for-byte.
// 48-byte scalar head + 72 float4s.
static_assert(sizeof(ecotopeGpuConstants) == 48 + (12 * 5 + 12) * 16,
              "ecotopeGpuConstants must stay blob-compatible with the gConstants cbuffer (1200 bytes)");
static_assert(offsetof(ecotopeGpuConstants, ect) == 48,
              "ecotopeGpuConstants::ect must sit at cbuffer offset 48");

class ecotopeSystem {
public:
	ecotopeSystem() { ; }
	virtual ~ecotopeSystem() { ; }

	template<class Archive>
	void serialize(Archive & archive)
	{
		archive(CEREAL_NVP(ecotopes));
	}

	// Not implemented: the dialog-driven load()/save() editor flows - only the
	// explicit-path load below exists.
    void load(std::string _path, std::string _resourcePath);

	void addEcotope();

    ecotopeGpuConstants* getConstants() {
        return &constantbuffer;
    }

    static std::string resPath;

	std::vector<ecotope>	ecotopes;		// FIXME LIST

	// runtime
	void rebuildRuntime();

    ecotopeGpuConstants constantbuffer;
    // TODO: plantIndex and plantDensity are sized for 24 ecotopes while
    // ecotopeGpuConstants::ect is [12][5] and texScales is [12]. The two limits must
    // agree - rebuildRuntime() writes ect[] unclamped from the JSON's numEcotopes.
    uint plantIndex[24][16][64];            // total density and 64 index values  and  (24 ecotopes , 16 lods, 64 possible values)
    ew::Buffer::SharedPtr piBuffer = nullptr;
    ew::Buffer::SharedPtr getPLantBuffer() { return piBuffer; }
    uint plantDensity[24][16];            // total density and 64 index values  and  (24 ecotopes , 16 lods)
    ew::Buffer::SharedPtr pdBuffer = nullptr;
    ew::Buffer::SharedPtr getPLantDesityBuffer() { return pdBuffer; }

    bool change = false;
    static float terrainSize;
    static _rootPlant *pVegetation;
};
