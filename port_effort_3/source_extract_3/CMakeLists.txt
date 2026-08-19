add_falcor_executable(Earthworks_4)


set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /STACK:18388608")

target_sources(Earthworks_4 PRIVATE
    Earthworks_4.cpp
    Earthworks_4.h

    latexgen.h
    
    computeShader.cpp
    computeShader.h
    earthworks_scene/Sprites.h
    earthworks_scene/Sprites.cpp
    earthworks_scene/Sprite_defines.h
	pixelShader.cpp
    pixelShader.h
    
    Barrier.hpp

    earthworks_scene/atmosphere.cpp
    earthworks_scene/atmosphere.h
    earthworks_scene/volumeFogPhaseFunctions.cpp

    earthworks_scene/earthworksScene.h
    earthworks_scene/earthworksScene.cpp
    earthworks_scene/cascadeShadowMaps.h
    earthworks_scene/cascadeShadowMaps.cpp

    earthworks_scene/ecotope.h
    earthworks_scene/ecotope.cpp
    earthworks_scene/roads_AI.cpp
    earthworks_scene/roads_AI.h
    earthworks_scene/roads_bezier.cpp
    earthworks_scene/roads_bezier.h
    earthworks_scene/roads_Intersection.cpp
    earthworks_scene/roads_Intersection.h
    earthworks_scene/roads_materials.cpp
    earthworks_scene/roads_materials.h
    earthworks_scene/roads_physics.cpp
    earthworks_scene/roads_physics.h
    earthworks_scene/roads_road.cpp
    earthworks_scene/roads_road.h
    earthworks_scene/roadNetwork.cpp
    earthworks_scene/roadNetwork.h
    earthworks_scene/terrafector.cpp
    earthworks_scene/terrafector.h
	earthworks_scene/terrain.cpp
    earthworks_scene/terrain.h
    earthworks_scene/lru_cache.h
    earthworks_scene/terrainGenerator.cpp
    earthworks_scene/terrainGenerator.h

    earthworks_scene/ribbonBuilder.h
    earthworks_scene/ribbonBuilder.cpp
	earthworks_scene/vegetationBuilder.h
    earthworks_scene/vegetationBuilder.cpp
    earthworks_scene/vegetationBuilder_Trees.cpp
    earthworks_scene/PerlinNoise.hpp

    earthworks_scene/imGuiHelper.h

    glider.cpp
    glider_runtime.cpp
    glider.h
    cfd.h
    cfd.cpp
	


	hlsl/terrain/compute_tileVertices.hlsl
	hlsl/terrain/compute_bc6h_functions.hlsli
	hlsl/terrain/gpuLights_defines.hlsli
	hlsl/terrain/gpuLights_functions.hlsli
	hlsl/terrain/groundcover_defines.hlsli
	hlsl/terrain/groundcover_functions.hlsli
    hlsl/terrain/materials.hlsli
	hlsl/terrain/terrainDefines.hlsli
	hlsl/terrain/terrainFunctions.hlsli
	hlsl/terrain/compute_bc6h.hlsl
	hlsl/terrain/compute_tileBicubic.hlsl
	hlsl/terrain/compute_tileBuildLookup.hlsl
	hlsl/terrain/compute_tileClear.hlsl
	hlsl/terrain/compute_tileDelaunay.hlsl
	hlsl/terrain/compute_tileEcotopes.hlsl
	hlsl/terrain/compute_tileGenerate.hlsl
	hlsl/terrain/compute_tileJumpFlood.hlsl
	hlsl/terrain/compute_tileNormals.hlsl
	hlsl/terrain/compute_tilePassthrough.hlsl
	hlsl/terrain/compute_tileSplitMerge.hlsl
    hlsl/terrain/compute_terrain_under_mouse.hlsl
    hlsl/terrain/compute_clipLodAnimatePlants.hlsl

    hlsl/terrain/compute_bakeFloodfill.hlsl

    hlsl/terrain/extractTextures.hlsl

    hlsl/terrain/render_meshTerrafector.hlsl
    hlsl/terrain/render_ribbons.hlsl
    hlsl/terrain/vegetation_defines.hlsli
    hlsl/terrain/render_vegetation_ribbons.hlsl
    hlsl/terrain/compute_vegetation_lod.hlsl
    hlsl/terrain/compute_vegetation_clear.hlsl
    hlsl/terrain/compute_vegetation_sortCombine.hlsl
    hlsl/terrain/render_thermalRibbons.hlsl
    hlsl/terrain/render_cfdSlice.hlsl
    hlsl/terrain/render_splineTerrafector.hlsl
    hlsl/terrain/render_spline.hlsl
    hlsl/terrain/render_tile_sprite.hlsl
    hlsl/terrain/render_Tiles.hlsl
    hlsl/terrain/render_triangles.hlsl
    hlsl/terrain/render_Buildings_Far.hlsl
    hlsl/terrain/render_GliderWing.hlsl

    hlsl/terrain/compute_sampleRGBtoPixel.hlsl

    hlsl/atmosphere/compute_fogCloudAtmosphereCommon.hlsli
    hlsl/atmosphere/compute_volumeFog.hlsli
    hlsl/atmosphere/compute_sunlightInAtmosphere.hlsl
    hlsl/atmosphere/compute_volumeFogAtmosphericScatter.hlsl
    hlsl/atmosphere/compute_volumeFogLights.hlsl
    hlsl/atmosphere/compute_volumeFogSmokeAndDust.hlsl

    
    hlsl/render_sprite.hlsl
    hlsl/render_Common.hlsli
    hlsl/PBR.hlsli
    hlsl/material.hlsli
    hlsl/compute_tonemapper.hlsl
)



target_copy_shaders(Earthworks_4 Samples/Earthworks_4)

target_source_group(Earthworks_4 "Samples")


target_link_libraries(Earthworks_4 PUBLIC Dinput8.lib PUBLIC DXGuid.lib PUBLIC  Xinput9_1_0.lib PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/harupdf/hpdf.lib")

#this is here to process teh swiss buildingas at least do in a se[perate project]
target_include_directories(Earthworks_4 PUBLIC "nlohmann")

include_directories("${PROJECT_SOURCE_DIR}/harupdf")
include_directories("${PROJECT_SOURCE_DIR}/harupdf/include")


target_include_directories(Earthworks_4 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(Earthworks_4 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/earthworks_scene)

