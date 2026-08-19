#pragma once

#include"roads_bezier.h"
#include"roads_materials.h"



enum typeOfCorner { automatic, radius, artistic };
class roadSection;
class intersection;
class stampPad;


class intersectionRoadLink
{
public:
    bool operator < (const intersectionRoadLink& str) const
    {
        return (angle < str.angle);
    }

    float angle;
    roadSection* roadPtr;
    uint roadGUID;
    bool broadStart;	// are we attached to the start, if false the end

    float pushBack_A;
    float pushBack_B;
    float pushBack;
    float3 cornerUp_A;
    float3 cornerUp_B;

    float3 corner_A;
    float3 corner_B;
    float3 cornerTangent_A;
    float3 cornerTangent_B;

    float3 tangentVector;			// from the center at the road connection angle, and perpendicular to the intersection andchorNormal

    int cornerType = typeOfCorner::automatic;
    float cornerRadius = 5.0f;
    bool bOpenCorner;
    float theta; // the angle of this corner

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const version)
    {
        archive(CEREAL_NVP(angle));
        archive(CEREAL_NVP(roadGUID));
        archive(CEREAL_NVP(broadStart));
        archive_float3(corner_A);
        archive_float3(cornerTangent_A);
        archive_float3(corner_B);
        archive_float3(cornerTangent_B);
        archive_float3(tangentVector);
        archive(CEREAL_NVP(cornerType));
        archive(CEREAL_NVP(cornerRadius));
    }
};
CEREAL_CLASS_VERSION(intersectionRoadLink, 100);



struct splineTest
{
    float3 pos;
    float3 returnPos;
    bool bVertex;
    bool bSegment;
    int index;

    bool bStreetview;
    int streetviewIndex;

    // intersections
    int cornerNum = -1;
    int cornerFlag;	// tangent or point
    bool bCenter;

    bool bCtrl;
    bool bAlt;
    bool bShift;

    // lets see what we can add for lanes
    bool bRight;
    int lane;

    float testDistance;
};



struct splineUV
{
    float3 pos;
    float U;
    float V;
    glm::vec3 dU;
    glm::vec3 dV;
    glm::vec3 N;			// ??? maybe textyure dep[endent
    glm::vec3 E1;
    glm::vec3 E2;
};



class roadSection
{
public:
    roadSection() { ; }
    virtual ~roadSection() { ; }

    void convertToGPU_Realistic(std::vector<cubicDouble> &_bezier, std::vector<bezierLayer> &_index, std::vector<bezierLayer>& _index_BAKE, uint _from = 99999, uint _to = 88888, bool _stylized = false, bool _showMaterials = true);
    void clearSelection();
    void selectAll();
    void newSelection(int index);
    void addSelection(int index);
    void breakLink(bool bStart);
    void setAIOnly(bool AI);


    int GUID;
    int int_GUID_start = -1;
    int int_GUID_end = -1;
    std::vector<splinePoint> points;

    void saveType(int index);
    void loadSelected(int index);
    void loadCompleteRoad();

    void optimizeTangents(int lane);
    void optimizeSpacing(int lane);
    void solveUV(int lane);
    void solveWidthFromLanes();
    void solveEnergyAndLength(int lane, int _min, int _max);
    void solveRoad(int index = -1);
    void solveStart();
    void solveEnd();
    void solvePercentages();

    void setCenterline(uint type);

    void pushPoint(float3 p, float3 n, uint _lod);
    void insertPoint(int index, float3 p, float3 n, uint _lod);
    void deletePoint(int index);
    bool testAgainstPoint(splineTest* testdata, bool includeEnd = true);
    float getDistance();

    void findUV(glm::vec3 p, splineUV* uv);
    void findUVTile(glm::vec3 p, glm::vec3 c1, glm::vec3 c2, glm::vec3 c3, glm::vec3 c4, glm::vec3 b1, glm::vec3 b2, glm::vec3 b3, glm::vec3 b4, splineUV* uv);

    std::array<bool, 9> leftLaneInUse;
    std::array<bool, 9> rightLaneInUse;

    int buildQuality = 0;

    static splinePoint lastEditedPoint;
    static std::vector<intersection>* static_global_intersectionList;
    intersectionRoadLink* startLink = nullptr;      // FIXME solve these after load
    intersectionRoadLink* endLink = nullptr;

    bool isClosedLoop = false;

    bool isBasicLinearMarking = false;


    bool isIntergerUV = true;  // so will this tile when we reach intersections, clasically true
    float uvScale = 8.f;    // one repeat every 8 meters



    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const version)
    {
        archive(CEREAL_NVP(GUID));
        archive(CEREAL_NVP(int_GUID_start));
        archive(CEREAL_NVP(int_GUID_end));
        archive(CEREAL_NVP(points));
        archive(CEREAL_NVP(buildQuality));

        if (version >= 101) {
            archive(CEREAL_NVP(isClosedLoop));
        }
        if (version >= 102) {
            archive(CEREAL_NVP(isBasicLinearMarking));
        }
        if (version >= 103) {
            archive(CEREAL_NVP(isIntergerUV));
            archive(CEREAL_NVP(uvScale));
        }
    }
};
CEREAL_CLASS_VERSION(roadSection, 103);




class stampPad
{
public:
    stampPad() { ; }
    virtual ~stampPad() { ; }

    void renderGUI(Gui* _gui) { ; }

    std::string materialName;
    int material;
    float width = 1.f;
    float height = 1.f;

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const version)
    {
        archive(CEREAL_NVP(materialName));
        archive(CEREAL_NVP(width));
        archive(CEREAL_NVP(height));

        material = roadMaterialCache::getInstance().find_insert_material(materialName);
        terrafectorEditorMaterial::static_materials.rebuildAll();
    }
};
CEREAL_CLASS_VERSION(stampPad, 100);


class stamp
{
public:
    stamp() { ; }
    virtual ~stamp() { ; }

    int material = 1;
    float rotation;
    float3 pos;
    float3 right;
    float3 dir;

    float height = 0.f;
    float2 scale = { 1, 1 };

    // width , height maybe as % from material
    /// ??? guids


    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const version)
    {
        archive(CEREAL_NVP(material));
        archive(CEREAL_NVP(rotation));
        archive_float3(pos);
        archive_float3(right);
        archive_float3(dir);
        archive(CEREAL_NVP(height));
        archive_float2(scale);
    }
};
CEREAL_CLASS_VERSION(stamp, 100);




class stampCollection
{
public:
    stampCollection() { ; }
    virtual ~stampCollection() { ; }

    void reloadMaterials()
    {
        std::map<int, std::string>  newMap;
        for (auto& mat : materialMap)
        {
            std::string relative = materialCache::getRelative(mat.second.c_str());
            
            fprintf(terrafectorSystem::_logfile, "       relative   %s\n", relative.c_str());
            std::string cleanPath = terrafectorEditorMaterial::rootFolder + relative;
            int idx = terrafectorEditorMaterial::static_materials.find_insert_material(cleanPath);
            newMap[idx] = relative;
        }
        materialMap.clear();
        materialMap = newMap;
    }


    void add(stamp &S)
    {
        if (materialMap.find(S.material) == materialMap.end())
        {
            std::string name = terrafectorEditorMaterial::static_materials.materialVector[S.material].fullPath.string();
            name = materialCache::getRelative(name);
            materialMap[S.material] = name;
        }
        stamps.push_back(S);
    }

    int find(float3 _pos)
    {
        int idx = -1;
        float  minL = 3.f;
        for (int i=0; i<stamps.size(); i++)
        {
            float L = glm::length(stamps[i].pos - _pos);
            if (L < minL)
            {
                minL = L;
                idx = i;
            }
        }
        return idx;
    }

    std::filesystem::path lastUsedFilename;

    std::map<int, std::string>  materialMap;
    std::vector<stamp> stamps;



    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const version)
    {
        archive(materialMap);
        archive(stamps);
    }
};
CEREAL_CLASS_VERSION(stampCollection, 100);





