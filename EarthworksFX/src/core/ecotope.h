#pragma once

#include "Falcor.h"
using namespace Falcor;
#include"hlsl/terrain/groundcover_defines.hlsli"
#include"hlsl/terrain/terrainDefines.hlsli"
#include"hlsl/terrain/gpuLights_defines.hlsli"

#include "cereal/cereal.hpp"
#include "cereal/types/map.hpp"
#include "cereal/types/vector.hpp"
#include "cereal/types/list.hpp"
#include "cereal/types/array.hpp"
#include "cereal/types/string.hpp"

//#include "vegetationBuilder.h"
class _rootPlant;   // forward declaration


/*

// ??? fixcme move to one of the slang define files
struct _GPU_ecotopePlant
{
	uint		idx;
	uint		lod;
	float		density;
	float		scale;		// ???
	float		size;		// I have to pack this better foir GPU damn ;-) 

	//float		scaleVariation;	  // not gettign used now, but maybe keep - packs better without
};

// not great  but packs into GPU mem ok'ish
// should expand to bump / terrafector later
// maybe this should become 2 buffers to pack tighter
// ek dink hierdie verdwyn totaal en al
struct _GPU_ecotope
{
	uint	numplants = 0;
	uint	X1;			//??? can we use any other info
	uint	X2;
	uint	X3;
	_GPU_ecotopePlant	plants[256];
	uint	plantLOD[8192];
	float	plantSize[8192];
};
*/

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

    /*
    * path
    * name
    * relativerDensity
    * size in meters
    * variation
    * lod its on
    *
    * and sipaly all of this sorted into lod
    */

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

/*
struct spritePlant {
    float width;
    float height;
    uint albedoIndex;
    uint normalIndex;
    uint translusenceIndex;
    // and maybe extend to lodding info in the future, 
};

class spriteCache {
public:
    uint find_insert_plant(const std::filesystem::path _path);

    void setTextures(ShaderVar& var);

    void rebuildStructuredBuffer();


    std::vector<spritePlant>	    plantVector;
    std::vector<Texture::SharedPtr>	textureVector;
    Buffer::SharedPtr plantBuffer = nullptr;
};
*/


class ecotope {
public:
	ecotope();
	virtual ~ecotope() { ; }

	void loadTexture(int i);
	void reloadTextures(std::string _resPath);

	template<class Archive>
	void serialize(Archive & archive)
	{
		archive(CEREAL_NVP(name));
		archive(CEREAL_NVP(weights));
		archive(CEREAL_NVP(albedoName), CEREAL_NVP(noiseName), CEREAL_NVP(displacementName), CEREAL_NVP(roughnessName), CEREAL_NVP(aoName));
		archive(CEREAL_NVP(plants));
	}
	void GuiTextures(Gui *_pGui);
	void GuiWeights(Gui *_pGui);
	void GuiPlants(Gui *_pGui);
	void renderGUI(Gui *_pGui);
	void renderPlantGUI(Gui *_pGui);
	uint selectedPlant = 0;

	void addPlant();

	std::string name = "new ecotope";
	std::array<std::array<float, 4>, 6> weights;	  // maps to 5 float4's but better for cereal to stay STL and do memcpy - waste space for consistency

	std::string albedoName;
	std::string noiseName;
	std::string displacementName;
	std::string roughnessName;
	std::string aoName;

	//std::array<std::vector<_displayPlant>, 16> plants;
    std::vector<_displayPlant> plants;
    std::array<float, 16> totalPlantDensity;

	Texture::SharedPtr	texAlbedo;
	Texture::SharedPtr	texNoise;
	Texture::SharedPtr	texDisplacement;
	Texture::SharedPtr	texRoughness;
	Texture::SharedPtr	texAO;

    bool hasChanged = false;
    
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

class ecotopeSystem {
public:
	ecotopeSystem() { ; }
	virtual ~ecotopeSystem() { ; }

	template<class Archive>
	void serialize(Archive & archive)
	{
		archive(CEREAL_NVP(ecotopes));
	}

	void load();
    void load(std::string _path, std::string _resourcePath);
	void save();
	void renderGUI(Gui *_pGui);
	void renderSelectedGUI(Gui *_pGui);
	void renderPlantGUI(Gui *_pGui);
	void addEcotope();

    ecotopeGpuConstants* getConstants() {
        return &constantbuffer;
    }

    static std::string resPath;

	std::vector<ecotope>	ecotopes;		// FIXME LIST

	int selectedEcotope = 0;
    bool showDebug = false;

	// runtime
    //Buffer::SharedPtr constantBuffer;
	void rebuildRuntime();
    //void resetPlantIndex(uint lod);

    ecotopeGpuConstants constantbuffer;
    //float totalDensity[12][16];             // ecotope / lod
    uint plantIndex[24][16][64];            // total density and 64 index values  and  (24 ecotopes , 16 lods, 64 possible values)
    Buffer::SharedPtr piBuffer = nullptr;
    Buffer::SharedPtr getPLantBuffer() { return piBuffer; }
    uint plantDensity[24][16];            // total density and 64 index values  and  (24 ecotopes , 16 lods)
    Buffer::SharedPtr pdBuffer = nullptr;
    Buffer::SharedPtr getPLantDesityBuffer() { return pdBuffer; }

    bool change = false;
    static float terrainSize;
    static _rootPlant *pVegetation;
};

