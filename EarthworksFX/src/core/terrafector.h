#pragma once



#include "Falcor.h"		// for glm includes float4 etc
#include<unordered_map>
#include<list>

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

using namespace std::chrono;

#include "ecotope.h"



using namespace Falcor;
#include"hlsl/terrain/groundcover_defines.hlsli"
#include"hlsl/terrain/terrainDefines.hlsli"
#include"hlsl/terrain/gpuLights_defines.hlsli"
#include"hlsl/terrain/materials.hlsli"


#define archive_float2(v) {archive(CEREAL_NVP(v.x)); archive(CEREAL_NVP(v.y));}
#define archive_float3(v) {archive(CEREAL_NVP(v.x)); archive(CEREAL_NVP(v.y)); archive(CEREAL_NVP(v.z));}
#define archive_float4(v) {archive(CEREAL_NVP(v.x)); archive(CEREAL_NVP(v.y)); archive(CEREAL_NVP(v.z)); archive(CEREAL_NVP(v.w));}



class logBlock
{
public:
    uint type;
    std::chrono::time_point<std::chrono::high_resolution_clock>  startTime;
};

class JLogger
{
private:
    std::stack<logBlock>    stack;
    std::chrono::time_point<std::chrono::high_resolution_clock>  startTime;

    static std::string indent(int depth) { return std::string(static_cast<size_t>(depth) * 4, ' '); }

    static spdlog::level::level_enum levelForType(uint type)
    {
        switch (type)
        {
        case 3: return spdlog::level::err;
        case 2: return spdlog::level::warn;
        default: return spdlog::level::info;
        }
    }

    static const char* typeLabel(uint type)
    {
        static const char* logTypes[4] = {"verb", "info", "warn", "erro"};
        return logTypes[type < 4 ? type : 0];
    }

    float elapsedSecondsSince(const std::chrono::time_point<std::chrono::high_resolution_clock>& from) const
    {
        const auto now = high_resolution_clock::now();
        return static_cast<float>(duration_cast<microseconds>(now - from).count()) / 1000000.f;
    }

public:
    using SharedPtr = std::shared_ptr<JLogger>;

    static const JLogger::SharedPtr& instancePtr();
    
    void startBlock(char* _name, uint _type);
    void endBlock();
    void open(char* _name);
    void close();

    void log(uint _type, std::string _text);
    void logMulti(uint _type, std::string _text);
};

class logBlockEvent
{
public:
    logBlockEvent(char* _name, uint _type)
    {
        JLogger::instancePtr()->startBlock(_name, _type);
    }

    ~logBlockEvent()
    {
        JLogger::instancePtr()->endBlock();
    }

private:
};

#define LOG_BLOCK(_name, _type) logBlockEvent _logEvent##__LINE__(_name, _type)
#define LOG_LINE(_type, _text) JLogger::instancePtr()->log(_type, _text)
#define LOG_MULTI(_type, _text) JLogger::instancePtr()->log(_type, _text)

// FIXME move to hlsl
class  triVertex {
public:
    glm::float3 pos;
    float       alpha;      // might have to be full float4 colour   but look at float16

    glm::float2 uv;         // might have to add second UV
    uint        material;
    float       buffer;         // now 32

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const _version)
    {
        archive_float3(pos);
        archive_float2(uv);
        archive(CEREAL_NVP(material));
        archive(CEREAL_NVP(alpha));
    }
};
CEREAL_CLASS_VERSION(triVertex, 100);



class triangleBlock {
public:
private:
    std::array<glm::int4, 128> index;
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
    Buffer::SharedPtr vertex = nullptr;
    Buffer::SharedPtr index = nullptr;
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

	void setTextures(ShaderVar& var);
	void rebuildStructuredBuffer();
	void rebuildAll();
    void reFindMaterial(std::filesystem::path currentPath);
    void renameMoveMaterial(terrafectorEditorMaterial& _material);

    void renderGui(Gui* mpGui, Gui::Window& _window);
    void renderGuiTextures(Gui* mpGui, Gui::Window& _window);
    bool renderGuiSelect(Gui* mpGui);
    Texture::SharedPtr getDisplayTexture();

	std::vector<terrafectorEditorMaterial>	materialVector;
	int selectedMaterial = -1;
	std::vector<Texture::SharedPtr>			textureVector;
	int dispTexIndex = -1;
    float texture_memory_in_Mb = 0;
    Buffer::SharedPtr sb_Terrafector_Materials = nullptr;
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
	void import(bool _replacePath = true);
    void save();
	void eXport(std::filesystem::path _path);
	void eXport();
	void reloadTextures();
	void loadTexture(int idx);
    void loadSubMaterial(int idx);
    void clearSubMaterial(int idx);

	bool renderGUI(Gui *mpGui);

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
    Texture::SharedPtr		thumbnail = nullptr;

	bool			useAbsoluteElevation = true;	// deprecated

	enum tfTextures { baseAlpha, detailAlpha, baseElevation, detailElevation, baseAlbedo, detailAlbedo, baseRoughness, detailRoughness, ecotope, tfTextureSize };
	std::array<std::string, 9> tfElement = { "##baseAlpha", "##detailAlpha", "##baseElevation", "##detailElevation", "##baseAlbedo", "##detailAlbedo", "##baseRoughness", "##detailRoughness", "##ecotope" };
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
		int	materialType = 0;
		float2  uvScale = {1.0f, 1.0f};
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

		uint	detailAlphaTexture = 0;
		float	detailAlphaBrightness = 0.0f;
		float	detailAlphaContrast = 1.0f;
		float	useAbsoluteElevation;

		uint	useElevation = 0;
		float	useVertexY;
		float	YOffset;
		uint	baseElevationTexture;

		float	baseElevationScale = 0.0f;
		float	baseElevationOffset = 0.5f;
		uint	detailElevationTexture;
		float	detailElevationScale = 0.0f;

		float	detailElevationOffset = 0.5f;
		float3	buf_____02;

		int		standardMaterialType = 0;		// mircrowshadow etc
		float3	buf_____03;

		uint	useColour = 0;
		uint	baseAlbedoTexture;
		float	albedoBlend;
		uint	detailAlbedoTexture;

		float3	albedoScale = {0.5f, 0.5f, 0.5f};
		float	uvWorldRotation = 0.0f;

		uint	baseRoughnessTexture;
		float	roughnessBlend;
		uint	detailRoughnessTexture;
		float	roughnessScale = 1.0f;

		float	porosity = 0.5f;
		float3	buf_____05;

		uint	useEcotopes = 0;
		float	permanenceElevation = 0;
		float	permanenceColour = 0;
		float	permanenceEcotopes = 0;

		float	cullA = 0;
		float	cullB = 0;
		float	cullC = 0;
		uint	ecotopeTexture;

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
	} _constData;		// 464 bytes for now


	void rebuildConstantbuffer();
	void rebuildConstantbufferData();
	int textureIndex[9] = {-1, -1, -1, -1, -1, -1, -1, -1, -1};
	BlendState::SharedPtr		blendstate;
};
CEREAL_CLASS_VERSION(terrafectorEditorMaterial, TFMATERIAL_VERSION);




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
	void renderGui(Gui* _pGui, float tab = 0);

    void loadPath(std::string _path);

	TriangleMesh::SharedPtr		pMesh;
	struct subMesh {
		uint index;
		uint sortVal;
		uint sortIndex;
		std::string materialName;
	};
	std::vector<subMesh>	materials;
	bool visible = true;
	bool bExpanded = false;
    bool bakeOnly = false;

    
}; 





class terrafectorSystem
{
public:
    terrafectorSystem() { ;	}
	virtual ~terrafectorSystem() { ; }

	void loadFile();
	void saveFile();
	template<class Archive>
	void serialize(Archive & archive) {	/*archive(groups); */}

	void renderGui(Gui* mpGui, Gui::Window& _window);
    void loadPath(std::string _path, std::string _exportPath, bool _rebuild = false);
    void exportMaterialBinary(std::string _path, std::string _evoRoot);

public:

	static bool needsRefresh;
	static ecotopeSystem *pEcotopes;
	static FILE *_logfile;
    static std::chrono::time_point<std::chrono::high_resolution_clock>  logStartTime;

    static void logTimeX()
    {
        auto a = high_resolution_clock::now();
        float delta_ms = (float)duration_cast<microseconds>(a - terrafectorSystem::logStartTime).count() / 1000.;
        spdlog::info("{:.3f}ms    :", delta_ms);
    }
    /*
    static void logTab()
    {
        spdlog::info("");
    }
    static void logHeader()
    {
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

