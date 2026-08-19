
#pragma once
#include "Falcor.h"

#include <future> // Required for std::async and std::future
#include <thread> // Required for std::this_thread::sleep_for
#include "Barrier.hpp"

#include "cereal/cereal.hpp"
#include "cereal/archives/binary.hpp"
#include "cereal/archives/json.hpp"
#include "cereal/archives/xml.hpp"
#include "cereal/types/map.hpp"
#include "cereal/types/vector.hpp"
#include "cereal/types/list.hpp"
#include "cereal/types/array.hpp"
#include "cereal/types/string.hpp"

#include <fstream>

#include "../../external/openJPH/include/openjph/ojph_arg.h"
#include "../../external/openJPH/include/openjph/ojph_mem.h"
#include "../../external/openJPH/include/openjph/ojph_file.h"
#include "../../external/openJPH/include/openjph/ojph_codestream.h"
#include "../../external/openJPH/include/openjph/ojph_params.h"
#include "../../external/openJPH/include/openjph/ojph_message.h"


using namespace Falcor;

//#pragma optimize("", off)



struct jp2Map_Gen {
    void set(uint lod, uint y, uint x, float wSize = 40000.f, float wOffset = -20000.f);
    void save(std::ofstream& _os);
    void saveBinary(std::ofstream& _os);
    //void loadBinary(std::ifstream& _is);

    uint lod;
    uint y;
    uint x;

    float2 origin;
    float size;

    float hgt_offset = 0;       // elevation on;y
    float hgt_scale = 0;

    uint fileOffset = 0;
    uint sizeInBytes = 0;


    template<class Archive>
    void serialize(Archive& archive)
    {
        archive(CEREAL_NVP(lod));
        archive(CEREAL_NVP(y));
        archive(CEREAL_NVP(x));

        archive(CEREAL_NVP(origin.x));
        archive(CEREAL_NVP(origin.y));
        archive(CEREAL_NVP(size));

        archive(CEREAL_NVP(hgt_offset));
        archive(CEREAL_NVP(hgt_scale));

        archive(CEREAL_NVP(fileOffset));
        archive(CEREAL_NVP(sizeInBytes));
    }
};

struct jp2File_Gen
{
    void save(std::ofstream& _os);
    void saveBinary(std::ofstream& _os);
    //void loadBinary(std::ifstream& _is);
    std::string filename;
    std::vector<jp2Map_Gen> tiles;
    uint sizeInBytes;
    uint32_t hash;


    template<class Archive>
    void serialize(Archive& archive)
    {
        archive(CEREAL_NVP(filename));
        archive(CEREAL_NVP(hash));
        archive(CEREAL_NVP(sizeInBytes));
        archive(CEREAL_NVP(tiles));
    }
};



// simplified version only for creation
struct jp2Dir_Gen
{
    void save(std::string _name);
    //void load(std::string _name);
    //void cache0(std::string _path);
    //void cacheHash(uint32_t hash);
    std::string path;


    void saveBinary(std::string _name);
    //void loadBinary(std::string _name);


    std::vector<jp2File_Gen> files;

    //std::map<uint32_t, uint> fileHashmap;
    //std::map<uint32_t, uint2> tileHash;
    //LRUCache<uint32_t, std::shared_ptr<std::vector<unsigned char>>> cache;
    //std::vector<unsigned char> dataRoot;

    template<class Archive>
    void serialize(Archive& archive)
    {
        archive(files);
    }
};


struct terrainGeneratorPaths
{
    std::string terrains;
    std::string gis_data;
    std::string gdal_bin;

    template<class Archive>
    void serialize(Archive& _archive, std::uint32_t const _version)
    {
        _archive(CEREAL_NVP(terrains));
        _archive(CEREAL_NVP(gis_data));
        _archive(CEREAL_NVP(gdal_bin));
    }
};
CEREAL_CLASS_VERSION(terrainGeneratorPaths, 100);




struct terrainSettings
{
    std::string name;
    std::string proj4;
    float       size;

    template<class Archive>
    void serialize(Archive& _archive, std::uint32_t const _version)
    {
        _archive(CEREAL_NVP(name));
        _archive(CEREAL_NVP(proj4));
        _archive(CEREAL_NVP(size));
    }
};
CEREAL_CLASS_VERSION(terrainSettings, 100);


enum _tabs {Setup, Files, Test, Elevation, Image, Landcover};

class terrainGenerator
{
public:
    terrainGenerator();
    virtual ~terrainGenerator() { ; }

    void loadPaths();
    void savePaths();
    
    void onGuiMenubar(Gui* pGui);
    void renderGui_rightPanel(Gui* _gui);

    void onGuiRender_Setup(Gui* _gui, Gui::Window& _window);
    void onGuiRender_Files(Gui* _gui, Gui::Window& _window);
    void onGuiRender_Test(Gui* _gui, Gui::Window& _window);
    void onGuiRender_Setup_Right(Gui* _gui);
    void onGuiRender_Map_Right(Gui* _gui, bool _elevation);
    void onGuiRender_Map(Gui* _gui, Gui::Window& _window, bool _elevation, bool _change);
    void onGuiRender_Landcover(Gui* _gui, Gui::Window& _window);
    void onGuiRender(Gui* _gui, int _header, float2 _screenSize);

    void Create_GIS_directories();
    void Create_Terrain_directories();

    void Merge_HillShade();
    void Create_Image_Overview();
    void Create_Image_Detail(float _width, int _size, float2 _center = float2(0, 0));
    void Create_Hgt_Overview();
    void Create_Hgt_Detail(float _width, int _size, float2 _center);

    void clickMap(float2 pos, int lod, int penSize, bool _hgt, int set);

    void hgt_to_jpeg2000();
    void hgt_tile_gdal(int lod, int y, int x, int size = 1024);
    void hgt_tile_jp2(int lod, int y, int x, int size, float xstart, float ystart, float blockSize);

    void img_to_jpeg2000();
    void img_tile_gdal(int lod, int y, int x, int size = 1024);

    void codestream_rgb(ojph::codestream& _codestream, int _size, float _delta);
    uint img_to_jp2PhotosMemory(std::ofstream& _file, const uint size, uint _lod, uint _y, uint _x);


    bool changed = false;
    int gui_mode = 0;
    _tabs currentTab;
    int current_lod = 0; // Index pointer tracked by ImGui
    int current_pen = 0; // Index pointer tracked by ImGui

    std::string name;
    std::string terrain_path;       // DEPRECATED
    std::string gis_path;           // DEPRECATED
    std::string gdal_path;          // DEPRECATED

    terrainGeneratorPaths   paths;

    float   latt = 47.f;
    float   lon = 10.f;
    bool    useTransverseMercator = false;
    bool useCT = false;       // DEPRECATED
    std::string proj4;
    std::string CT;       // DEPRECATED

    int size_km = 40;

    std::string elevation_files;
    std::string image_files;
    std::string landcover_files;

    Texture::SharedPtr	  mapBackground = nullptr;
    Texture::SharedPtr	  mapTiles = nullptr;
    int map_op = 0;
    int map_scale = 4;

    RenderContext* _renderContext;

    // 4 is automatically included its just 16 tiles
    int tiles_hgt_4[16][16];
    int tiles_hgt_6[64][64];
    int tiles_hgt_8[256][256];  // 15cm @ 40km
    int tiles_hgt_10[1024][1024];  // just here if we want to create larger worlds

                                // 10 m @ 40km
    int tiles_img_4[16][16];    // 2.4m @ 40km
    int tiles_img_6[64][64];    // 60cm @ 40km
    int tiles_img_8[256][256];  // 15cm @ 40km
    int tiles_img_10[1024][1024];  // just here if we want to create larger worlds

    float hgt_jp2_Quant = 0.0001f;
    float img_jp2_Quant = 0.04f;

    unsigned char tileView[512][512][4];
    float2 windowPos;
    int totalTilesHgt;
    int totalTilesImg;

    int threadTilesHgt;
    int threadTilesImg;
    bool cancelBuild;
    FILE* dataFileHgt;

    int img_clamp = 10;
    float redPow = 1.f;
    float greenPow = 1.f;
    float bluePow = 1.f;
    float img_Saturation = 1.4f;
    Texture::SharedPtr	  testImage = nullptr;
    Texture::SharedPtr	  testHillshade = nullptr;

    
    

public:
    template<class Archive>
    void serialize(Archive& _archive, std::uint32_t const _version)
    {
        _archive(CEREAL_NVP(name));
        _archive(CEREAL_NVP(terrain_path)); //DEPRECATED
        _archive(CEREAL_NVP(gis_path));     //DEPRECATED
        _archive(CEREAL_NVP(gdal_path));    //DEPRECATED

        // _archive(CEREAL_NVP(latt));
        // _archive(CEREAL_NVP(lon));
        // _archive(CEREAL_NVP(useTransverseMercator));        
        _archive(CEREAL_NVP(useCT));        // DEPRECATED
        _archive(CEREAL_NVP(proj4));        
        _archive(CEREAL_NVP(CT));           // DEPRECATED

        _archive(CEREAL_NVP(size_km));

        _archive(CEREAL_NVP(elevation_files));
        _archive(CEREAL_NVP(image_files));
        _archive(CEREAL_NVP(landcover_files));

        if (_version >= 101)
        {
            _archive(CEREAL_NVP(tiles_hgt_4));
            _archive(CEREAL_NVP(tiles_hgt_6));
            _archive(CEREAL_NVP(tiles_hgt_8));
            _archive(CEREAL_NVP(tiles_hgt_10));

            _archive(CEREAL_NVP(tiles_img_4));
            _archive(CEREAL_NVP(tiles_img_6));
            _archive(CEREAL_NVP(tiles_img_8));
            _archive(CEREAL_NVP(tiles_img_10));
        }

        if (_version >= 102)
        {
            _archive(CEREAL_NVP(hgt_jp2_Quant));
            _archive(CEREAL_NVP(img_jp2_Quant));
        }
    }

};
CEREAL_CLASS_VERSION(terrainGenerator, 102);
