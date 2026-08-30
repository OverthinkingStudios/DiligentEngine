#pragma once

// ---------------------------------------------------------------------------
// Terrafector materials and the mesh-terrafector LOD split/cache pipeline.
//
// Include from terrain.h AFTER the hlsli-defines block: this relies on the
// HLSL-shared aliases (float2/float3/float4/uint) being in scope.
// materials.hlsli brings in TF_material - the byte contract shared with the
// three terrafector shaders, cereal AND the raw fwrite exports.
// ---------------------------------------------------------------------------

#include "ewResources.h"
#include "ewShader.h"

#include <cstddef>      // offsetof (the TF_material byte-contract asserts)
#include <unordered_map>
#include <list>
#include <chrono>

#include "cereal/archives/binary.hpp"
#include "cereal/archives/xml.hpp"
#include "cereal/archives/json.hpp"
#include "cereal/cereal.hpp"
#include "cereal/types/map.hpp"
#include "cereal/types/vector.hpp"
#include "cereal/types/list.hpp"
#include "cereal/types/array.hpp"
#include "cereal/types/string.hpp"
#include <fstream>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "ecotope.h"

#include "hlsl/terrain/materials.hlsli"


// Part of the authoring-file format contract: the serialize() functions depend
// on the exact NVP names these emit. Guarded because TextureSplitTool.hpp
// carries its own equivalent copies.
#ifndef archive_float2
#define archive_float2(v) {archive(CEREAL_NVP(v.x)); archive(CEREAL_NVP(v.y));}
#define archive_float3(v) {archive(CEREAL_NVP(v.x)); archive(CEREAL_NVP(v.y)); archive(CEREAL_NVP(v.z));}
#define archive_float4(v) {archive(CEREAL_NVP(v.x)); archive(CEREAL_NVP(v.y)); archive(CEREAL_NVP(v.z)); archive(CEREAL_NVP(v.w));}
#endif



// FIXME move to hlsl
class  triVertex {
public:
    glm::vec3   pos;
    float       alpha;      // might have to be full float4 colour   but look at float16

    glm::vec2   uv;         // might have to add second UV
    uint        material;
    float       buffer;         // now 32

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        (void)_version;
        archive_float3(pos);
        archive_float2(uv);
        archive(CEREAL_NVP(material));
        archive(CEREAL_NVP(alpha));
    }
};
CEREAL_CLASS_VERSION(triVertex, 100);

// The 32-byte stride is load-bearing: triVertex is the StructuredBuffer
// element of the mesh-terrafector VB (render_meshTerrafector.hlsl `triVertex`)
// AND a cereal/.lodNCache payload - the trailing `buffer` pad is part of the
// contract.
static_assert(sizeof(triVertex) == 32, "triVertex must stay exactly 32 bytes (GPU stride + cache format)");



class triangleBlock {
public:
private:
    std::array<glm::ivec4, 128> index;
};

class tileTriangleBlock {
public:
    void clear();
    void clearRemapping(uint _size);
    void remapMaterials(uint* _map);
    void insertTriangle(const uint material, const uint F[3], const aiMesh* _mesh, bool _yup);
    void insertTriangle(const uint material, const float3 pos[3], const float2 uv[3]);
private:
public:
    std::vector<triVertex> verts;
    std::vector<int> remapping;
    std::vector<triangleBlock> indexBlocks;
    std::vector<uint> tempIndexBuffer;

    uint vertexReuse;

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        (void)_version;
        archive(CEREAL_NVP(verts));
        archive(CEREAL_NVP(tempIndexBuffer));
    }
};
CEREAL_CLASS_VERSION(tileTriangleBlock, 100);



class lodTriangleMesh {
public:
    void create(uint _lod);
    void remapMaterials(uint* _map);
    void prepForMesh(aiAABB _aabb, uint _size, std::string _name, bool _yup);
    int insertTriangle(const uint material, const uint F[3], const aiMesh* _mesh);
    int insertTriangle(const uint material, const float3 pos[3], const float2 uv[3]);
    void logStats();
    void save(const std::string _path);
    bool load(const std::string _path);


private:
    uint xMin, xMax, yMin, yMax;
    uint lod;
    uint grid;
    float tileSize;
    float bufferSize;
    bool Yup = false;       // default to MAX Z up, Y north X east

public:
    std::vector<tileTriangleBlock> tiles;
    std::vector<std::string> materialNames;

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        (void)_version;
        archive(CEREAL_NVP(lod));
        archive(CEREAL_NVP(materialNames));
        archive(CEREAL_NVP(tiles));
    }
};
CEREAL_CLASS_VERSION(lodTriangleMesh, 100);



struct gpuTileTerrafector
{
    uint numVerts = 0;
    uint numTriangles = 0;
    uint numBlocks = 0;
    ew::Buffer::SharedPtr vertex = nullptr;
    ew::Buffer::SharedPtr index = nullptr;
};



class lodTriangleMesh_LoadCombiner {
public:
    void create(uint _lod);
    void addMesh(std::string _path, lodTriangleMesh &mesh, bool _remapMat = true);
    void loadToGPU(std::string _path, bool _log);
    gpuTileTerrafector* getTile(uint _index) {
        if (_index < gpuTiles.size()) {
            return &gpuTiles[_index];
        }
        return nullptr;
    }

private:
    uint lod, grid;
    std::vector<tileTriangleBlock> tiles;
    std::vector <gpuTileTerrafector> gpuTiles;
};




class terrafectorEditorMaterial;

class materialCache {
public:
    materialCache() { ; }
	virtual ~materialCache() { ; }

    static std::string getRelative(std::string _path);
    static void cleanPath(std::string& _path);

	uint find_insert_material(const std::string _path, const std::string _name);        // only called from lodTriangleMesh_LoadCombiner for fbx files
    uint find_insert_material(const std::filesystem::path _path);
	int find_insert_texture(const std::filesystem::path _path, bool isSRGB);

    // Binds the whole gmyTextures_T array by name: only the actually-used
    // prefix is passed and the layer dummy-pads the remaining declared slots,
    // because unset descriptor-array elements are UB on Vulkan.
    // Not implemented: bindless binding - it would mean marking gmyTextures_T
    // as a runtime-sized descriptor array (Diligent
    // SHADER_VARIABLE_FLAG_UNFILLED_MUTABLE) with MUTABLE variable type in the
    // three terrafector PSOs, binding once here on texture-set change instead
    // of per-draw, and dropping the per-commit 4096-descriptor writes.
    void setTextures(ew::pixelShader& _shader);
	void rebuildStructuredBuffer();
	void rebuildAll();
    ew::Texture::SharedPtr getDisplayTexture();     // consumed by the render-overlay blit path; editor-only in practice, dispTexIndex is only set >= 0 from the GUI

	std::vector<terrafectorEditorMaterial>	materialVector;
	int selectedMaterial = -1;      // also read/written by the terrain stamp code, not just the GUI
	std::vector<ew::Texture::SharedPtr>		textureVector;
	int dispTexIndex = -1;          // see getDisplayTexture
    float texture_memory_in_Mb = 0;
    ew::Buffer::SharedPtr sb_Terrafector_Materials = nullptr;

    // ew::Texture carries no source path or name; find_insert_texture and the
    // EVO exports need both, so they are mirrored here, index-aligned with
    // textureVector.
    std::vector<std::filesystem::path>      texturePathVector;
    std::vector<std::string>                textureNameVector;
};



#define TFMATERIAL_VERSION 101
#define TFMATERIAL_VERSION_LOAD 101
class terrafectorEditorMaterial {
public:
	terrafectorEditorMaterial();
	virtual ~terrafectorEditorMaterial();


	static materialCache static_materials;

	uint32_t blendHash();

	void import(std::filesystem::path _path, bool _replacePath = true);
    void save();
	void eXport(std::filesystem::path _path);
	void reloadTextures();

	static std::string rootFolder;

	template<class Archive>
	void serialize(Archive & archive, std::uint32_t const version)
	{
		archive(CEREAL_NVP(displayName));

		archive(CEREAL_NVP(useAbsoluteElevation));

		archive(CEREAL_NVP(texturePaths));
		archive(CEREAL_NVP(textureNames));

        archive(CEREAL_NVP(submaterialPaths));

        if (version > 100)
        {
            archive(CEREAL_NVP(isStamp));
            archive(CEREAL_NVP(stampWidth));
            archive(CEREAL_NVP(stampHeight));
        }

		// structured buffer data - although texture pointers are incomplete
		archive(CEREAL_NVP(_constData));
		_constData.useAbsoluteElevation = useAbsoluteElevation;


	}

	// should maybe be done with friend
public:
	std::string			  displayName = "Not set!";
    std::filesystem::path 			  fullPath;			// for quick save
	bool				isModified = false;
    std::array<std::string, 8>	  submaterialPaths = { "", "", "", "", "", "", "", "" };
    ew::Texture::SharedPtr		thumbnail = nullptr;    // filled in by materialCache::find_insert_material; only the editor GUI reads it

	bool			useAbsoluteElevation = true;	// deprecated

	enum tfTextures { baseAlpha, detailAlpha, baseElevation, detailElevation, baseAlbedo, detailAlbedo, baseRoughness, detailRoughness, ecotope, tfTextureSize };
	std::array<bool, 9> tfSRGB = { false, false, false, false, true, true, false, false, false };

	std::array<std::string, 9>	texturePaths;
	std::array<std::string, 9>	textureNames;

    bool isStamp = false;
    float stampWidth = 1.f;
    float stampHeight = 1.f;
	//int overlayTextureIndex = -1;


	enum tfMaterialTypes { tfmat_standard, tfmat_rubber, tfmat_puddle, tfmat_legacyRubber };
/*
	struct {
		float	reflectance = 0;
		float	microFiber = 0;
		float	microShadow = 0;
		float	lightWrap = 0;
	} _materialFixedData;
    */

	struct {
		float2  uvScale = {1.0f, 1.0f};
		int	materialType = 0;
		float	worldSize = 4.0f;

		float2  uvScaleClampAlpha = { 1.0f, 1.0f };
		uint	debugAlpha = 0;
		float	uvRotation = 0.0f;

		float	useAlpha = 1;				// bool
		float	vertexAlphaScale = 0.0f;
		float	baseAlphaScale = 0.0f;
		float	detailAlphaScale = 0.0f;

		uint	baseAlphaTexture = 0;
		float	baseAlphaBrightness = 0.0f;
		float	baseAlphaContrast = 1.0f;
		uint	baseAlphaClampU = 0;		// bool

		// The explicit zeros from here on matter: a material whose import fails
		// would otherwise carry indeterminate values. The gates
		// (useElevation / useColour / useEcotopes) are zeroed for the same
		// reason - do not drop the initializers.
		uint	detailAlphaTexture = 0;
		float	detailAlphaBrightness = 0.0f;
		float	detailAlphaContrast = 1.0f;
		float	useAbsoluteElevation = 0;

		uint	useElevation = 0;
		float	useVertexY = 0;
		float	YOffset = 0;
		uint	baseElevationTexture = 0;

		float	baseElevationScale = 0.0f;
		float	baseElevationOffset = 0.5f;
		uint	detailElevationTexture = 0;
		float	detailElevationScale = 0.0f;

		float3	buf_____02;
		float	detailElevationOffset = 0.5f;

		float3	buf_____03;
		int		standardMaterialType = 0;		// mircrowshadow etc

		uint	useColour = 0;
		uint	baseAlbedoTexture = 0;
		float	albedoBlend = 0;
		uint	detailAlbedoTexture = 0;

		float3	albedoScale = {0.5f, 0.5f, 0.5f};
		float	uvWorldRotation = 0.0f;

		uint	baseRoughnessTexture = 0;
		float	roughnessBlend = 0;
		uint	detailRoughnessTexture = 0;
		float	roughnessScale = 1.0f;

		float3	buf_____05;
		float	porosity = 0.5f;

		uint	useEcotopes = 0;
		float	permanenceElevation = 0;
		float	permanenceColour = 0;
		float	permanenceEcotopes = 0;

		float	cullA = 0;
		float	cullB = 0;
		float	cullC = 0;
		uint	ecotopeTexture = 0;

        std::array<uint, 8>	subMaterials = {0, 0, 0, 0, 0, 0, 0, 0};

		std::array<float4, 15>	ecotopeMasks;

		template<class Archive>
		void serialize(Archive & archive)
		{

			archive(CEREAL_NVP(materialType));
			archive(CEREAL_NVP(uvScale.x), CEREAL_NVP(uvScale.y));
			archive(CEREAL_NVP(worldSize));
            archive(CEREAL_NVP(uvRotation));
            archive(CEREAL_NVP(uvWorldRotation));

			archive(CEREAL_NVP(uvScaleClampAlpha.x), CEREAL_NVP(uvScaleClampAlpha.y));

			archive(CEREAL_NVP(useAlpha), CEREAL_NVP(vertexAlphaScale), CEREAL_NVP(baseAlphaScale), CEREAL_NVP(detailAlphaScale));
			archive(CEREAL_NVP(baseAlphaTexture), CEREAL_NVP(baseAlphaBrightness), CEREAL_NVP(baseAlphaContrast), CEREAL_NVP(baseAlphaClampU));
			archive(CEREAL_NVP(detailAlphaTexture), CEREAL_NVP(detailAlphaBrightness), CEREAL_NVP(detailAlphaContrast));
			archive(CEREAL_NVP(useElevation), CEREAL_NVP(useVertexY), CEREAL_NVP(YOffset), CEREAL_NVP(baseElevationTexture));
			archive(CEREAL_NVP(baseElevationScale), CEREAL_NVP(baseElevationOffset), CEREAL_NVP(detailElevationTexture), CEREAL_NVP(detailElevationScale));
			archive(CEREAL_NVP(detailElevationOffset));
			archive(CEREAL_NVP(standardMaterialType));
			archive(CEREAL_NVP(useColour), CEREAL_NVP(baseAlbedoTexture), CEREAL_NVP(albedoBlend), CEREAL_NVP(detailAlbedoTexture));
			archive(CEREAL_NVP(albedoScale.x), CEREAL_NVP(albedoScale.y), CEREAL_NVP(albedoScale.z));
			archive(CEREAL_NVP(baseRoughnessTexture), CEREAL_NVP(roughnessBlend), CEREAL_NVP(detailRoughnessTexture), CEREAL_NVP(roughnessScale));
			archive(CEREAL_NVP(porosity));
			archive(CEREAL_NVP(useEcotopes), CEREAL_NVP(permanenceElevation), CEREAL_NVP(permanenceColour), CEREAL_NVP(permanenceEcotopes));
			archive(CEREAL_NVP(cullA), CEREAL_NVP(cullB), CEREAL_NVP(cullC), CEREAL_NVP(ecotopeTexture));

            archive(CEREAL_NVP(subMaterials));

			for (int i = 0; i < 15; i++) {
				archive(CEREAL_NVP(ecotopeMasks[i].x), CEREAL_NVP(ecotopeMasks[i].y), CEREAL_NVP(ecotopeMasks[i].z), CEREAL_NVP(ecotopeMasks[i].w));
			}



		}
	} _constData;		// 512 bytes, mirroring TF_material


	void rebuildConstantbuffer();
	void rebuildConstantbufferData();
	int textureIndex[9] = {-1, -1, -1, -1, -1, -1, -1, -1, -1};
};
CEREAL_CLASS_VERSION(terrafectorEditorMaterial, TFMATERIAL_VERSION);


// --- TF_material byte contract -----------------------------------------------
// _constData mirrors materials.hlsli's TF_material BYTE-FOR-BYTE: the same
// bytes are (1) the HLSL StructuredBuffer element of `materials` in all three
// terrafector shaders, (2) the cereal-JSON field list of .terrafectorMaterial
// files (name-keyed, so field ORDER may change without breaking assets), and
// (3) the raw fwrite payload of Materials.gpu / exportBinary (order-sensitive:
// reordering fields changes that on-disk format).
// The `buf_____NN` float3 pads are load-bearing. The asserted 512 bytes are
// 15 rows x 16 B + 32 B subMaterials + 240 B ecotopeMasks.
// The layout must ALSO be identical under Vulkan std430 (see the comment on
// TF_material in materials.hlsli): float2 on 8-byte boundaries, float3 only at
// the start of a 16-byte row. The offsets below pin exactly the rows where the
// two schemes could diverge.
static_assert(sizeof(TF_material) == 512, "TF_material contract: 15*16 + 32 + 240 = 512 bytes");
static_assert(offsetof(TF_material, uvScale) == 0 && offsetof(TF_material, materialType) == 8,
              "TF_material row 1: float2 must lead the row (std430 aligns float2 to 8)");
static_assert(offsetof(TF_material, useAlpha) == 32, "TF_material row 3 moved");
static_assert(offsetof(TF_material, useElevation) == 80, "TF_material elevation row moved");
static_assert(offsetof(TF_material, buf_____02) == 112 && offsetof(TF_material, buf_____03) == 128 && offsetof(TF_material, buf_____05) == 192,
              "TF_material float3 pads must sit at 16-byte row starts (std430 aligns float3 to 16)");
static_assert(offsetof(TF_material, useColour) == 144, "TF_material colour row moved");
static_assert(offsetof(TF_material, subMaterials) == 240, "TF_material subMaterials moved");
static_assert(offsetof(TF_material, ecotopeMasks) == 272, "TF_material ecotopeMasks moved");
static_assert(sizeof(decltype(terrafectorEditorMaterial::_constData)) == sizeof(TF_material),
              "terrafectorEditorMaterial::_constData must mirror TF_material byte-for-byte");
static_assert(offsetof(decltype(terrafectorEditorMaterial::_constData), subMaterials) == offsetof(TF_material, subMaterials),
              "_constData/TF_material field drift before subMaterials");
static_assert(offsetof(decltype(terrafectorEditorMaterial::_constData), ecotopeMasks) == offsetof(TF_material, ecotopeMasks),
              "_constData/TF_material field drift before ecotopeMasks");




enum tfTypes { tf_heading, tf_fbx, tf_triMesh, tf_bezier, tf_quad };
class terrafectorElement
{
public:
	terrafectorElement(tfTypes _t, const std::string _name) : type(_t), name(_name) { ; }
	virtual ~terrafectorElement() { ; }

	//private:
	tfTypes type;
	std::string name;
	std::string path;
	std::vector<terrafectorElement> children;

    bool isMeshCached(const std::string _path);
    void splitAndCacheMesh(const std::string _path);
	terrafectorElement &find_insert(const std::string _name, const tfTypes _type= tf_heading, const std::string _path="");

    void loadPath(std::string _path);

	struct subMesh {
		uint index;
		uint sortVal;
		uint sortIndex;
		std::string materialName;
	};
	std::vector<subMesh>	materials;
	bool visible = true;
    bool bakeOnly = false;


};





class terrafectorSystem
{
public:
    terrafectorSystem() { ;	}
	virtual ~terrafectorSystem() { ; }

	template<class Archive>
	void serialize(Archive & archive) {	/*archive(groups); */}

    void loadPath(std::string _path, std::string _exportPath, bool _rebuild = false);
    void exportMaterialBinary(std::string _path, std::string _evoRoot);

public:

	static bool needsRefresh;
	static ecotopeSystem *pEcotopes;
	static FILE *_logfile;
    static std::chrono::time_point<std::chrono::high_resolution_clock>  logStartTime;

    static void logTimeX()
    {
        auto a = std::chrono::high_resolution_clock::now();
        float delta_ms = (float)std::chrono::duration_cast<std::chrono::microseconds>(a - terrafectorSystem::logStartTime).count() / 1000.f;
        fprintf(_logfile, "%3.3fms    :    ", delta_ms);
    }
    /*
    static void logTab()
    {
        fprintf(_logfile, "    ");
    }
    static void logHeader()
    {
        //fprintf(_logfile, "%3.3fms    :    ", delta_ms);
    }
    */
	terrafectorElement root = terrafectorElement(tf_heading, "root");

    static lodTriangleMesh_LoadCombiner loadCombine_LOD2;       // will only be used if flagged by artists - large ecotopes only - roughly 40 -> 20 meter pixels this is the far horizon
    static lodTriangleMesh_LoadCombiner loadCombine_LOD4;       // 10m pixel 2.5km tile
    static lodTriangleMesh_LoadCombiner loadCombine_LOD6;       // 2.5m pixel 600m tile

    static lodTriangleMesh_LoadCombiner loadCombine_LOD4_top;       // 10m pixel 2.5km tile
    static lodTriangleMesh_LoadCombiner loadCombine_LOD6_top;       // 2.5m pixel 600m tile

    static lodTriangleMesh_LoadCombiner loadCombine_LOD7_stamps;       // 1.26m pixel 312m tile

    static lodTriangleMesh_LoadCombiner loadCombine_LOD4_bakeLow;       // all baking will happen at lod4 level
    static lodTriangleMesh_LoadCombiner loadCombine_LOD4_bakeHigh;
    static lodTriangleMesh_LoadCombiner loadCombine_LOD4_overlay;
};
