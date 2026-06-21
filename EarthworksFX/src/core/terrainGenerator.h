
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

#if defined(EARTHWORKS_WITH_OPENJPH)
#    if defined(__has_include) && __has_include(<openjph/ojph_codestream.h>)
#        include <openjph/ojph_arg.h>
#        include <openjph/ojph_mem.h>
#        include <openjph/ojph_file.h>
#        include <openjph/ojph_codestream.h>
#        include <openjph/ojph_params.h>
#        include <openjph/ojph_message.h>
#    else
#        include <ojph_arg.h>
#        include <ojph_mem.h>
#        include <ojph_file.h>
#        include <ojph_codestream.h>
#        include <ojph_params.h>
#        include <ojph_message.h>
#    endif
#else
#    error "EarthworksFX requires EARTHWORKS_WITH_OPENJPH (OpenJPH via Earthworks)."
#endif


using namespace Falcor;

//#pragma optimize("", off)




class terrainGenerator
{
public:
    terrainGenerator() { ; }
    virtual ~terrainGenerator() { ; }
    
    void onGuiMenubar(Gui* pGui);
    void renderGui_rightPanel(Gui* _gui);

    void onGuiRender_Setup(Gui* _gui, Gui::Window& _window);
    void onGuiRender_Files(Gui* _gui, Gui::Window& _window);
    void onGuiRender_Map(Gui* _gui, Gui::Window& _window, bool _elevation, bool _change);
    void onGuiRender_Landcover(Gui* _gui, Gui::Window& _window);
    void onGuiRender(Gui* _gui, int _header, float2 _screenSize);

    void Create_GIS_directories();
    void Create_Terrain_directories();

    void Create_Image_Overview();
    void Create_Hgt_Overview();

    void clickMap(float2 pos, int lod, int penSize, bool _hgt, int set);

    void hgt_to_jpeg2000();
    void hgt_tile_gdal(int lod, int y, int x);
    void img_to_jpeg2000();
    /*
    void bil_to_jp2();
    void bil_to_jp2(std::string file, const uint size, FILE* summary, uint _lod, uint _y, uint _x, float _xstart, float _ystart, float _size);

    void generateGdalPhotos();
    void writeGdal(jp2Map _map, std::ofstream& _gdal, std::string _input);
    void writeGdalClear(std::ofstream& _gdal);
    void bil_to_jp2Photos();
    void bil_to_jp2Photos(std::string file, const uint size, uint _lod, uint _y, uint _x);
    uint bil_to_jp2PhotosMemory(std::ofstream& _file, std::string filename, const uint size, uint _lod, uint _y, uint _x);
    */

    bool changed = false;
    int gui_mode = 0;

    std::string name;
    std::string terrain_path;       // to terrains
    std::string gis_path;   //.to gis daTA
    std::string gdal_path;   //.to gis daTA

    bool useCT = false;
    std::string proj4;
    std::string CT;

    int size_km = 40;

    std::string elevation_files;
    std::string image_files;
    std::string landcover_files;

    Texture::SharedPtr	  mapBackground;
    Texture::SharedPtr	  mapTiles;
    Texture::SharedPtr	  mapHillshade;

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

    unsigned char tileView[512][512][4];
    float2 windowPos;
    int totalTilesHgt;
    int totalTilesImg;

public:
    template<class Archive>
    void serialize(Archive& _archive, std::uint32_t const _version)
    {
        _archive(CEREAL_NVP(name));
        _archive(CEREAL_NVP(terrain_path));
        _archive(CEREAL_NVP(gis_path));
        _archive(CEREAL_NVP(gdal_path));
        
        _archive(CEREAL_NVP(useCT));
        _archive(CEREAL_NVP(proj4));        
        _archive(CEREAL_NVP(CT));

        _archive(CEREAL_NVP(size_km));

        _archive(CEREAL_NVP(elevation_files));
        _archive(CEREAL_NVP(image_files));
        _archive(CEREAL_NVP(landcover_files));
        
    }

};
CEREAL_CLASS_VERSION(terrainGenerator, 100);
