#pragma once

#include "roads_intersection.h"
#include "roads_AI.h"



#define ROADNETWORK_CEREAL_VERSION 103
class roadNetwork {
public:
    roadNetwork();
    virtual ~roadNetwork() { ; }

    bool renderpopupGUI(Gui* _gui, roadSection* _road, int _vertex);
    bool renderpopupGUI(Gui* _gui, intersection* _intersection);
    void renderPopupVertexGUI(Gui* mpGui, glm::vec2 _pos, uint _idx);
    void renderPopupSegmentGUI(Gui* mpGui, glm::vec2  _pos, uint _idx);
    void renderGUI(Gui* _gui);
    void renderGUI_3d(Gui* _gui);

    void saveRoadGeometry(roadSection* _road, int _vertex);
    void loadRoadGeometry(roadSection* _road, int _from, int _to);

    void saveRoadMaterials(roadSection* _road, int _vertex);
    void loadRoadMaterials(roadSection* _road, int _from, int _to);
    void loadRoadMaterialsAll(roadSection* _road, int _from, int _to);
    void loadRoadMaterialsVerge(roadSection* _road, int _from, int _to);
    void loadRoadMaterialsSidewalk(roadSection* _road, int _from, int _to);
    void loadRoadMaterialsAsphalt(roadSection* _road, int _from, int _to);
    void loadRoadMaterialsLines(roadSection* _road, int _from, int _to);
    void loadRoadMaterialsWT(roadSection* _road, int _from, int _to);
    
    bool popupVisible = false;

    void newRoadSplineBasic();
    void newRoadSpline();
    void newIntersection();

    float getDistance();	// how many km of roads do we have
    int getDone();		// what percentage is at highest quality level

    void updateDynamicRoad();
    void updateAllRoads(bool _forExport = false);

    std::filesystem::path lastUsedFilename;
    void load();
    void load(std::filesystem::path _path, uint _version = ROADNETWORK_CEREAL_VERSION);
    void upgrade(uint _FROM);
    void save();
    void saveDialog();
    void quickSave();
    void exportBinary();
    void exportBridges();
    void exportRoads(int _numSplits);
    void exportRoads(int _numSplits, glm::vec3 _center, float _size, std::string _blockName);
    void fillMesh(roadSection& road, aiMesh* mesh, uint _numsplits, bool bLeft, uint lane);

    void TEMP_pushAllMaterial();

    void currentRoadtoAI();
    void exportAI();
    void exportAI_CSV();
    void loadAI();
    bool AI_path_mode = false;
    void ai_clearPath() { pathBezier.clearRoads(); }
    void addRoad();

    // testing and intersection
    void physicsTest(glm::vec3 pos);
    void doSelect(glm::vec3 pos);
    void testHit(glm::vec3 pos);
    void testHitAI(glm::vec3 pos);
    void lanesFromHit();

    // basic editing
    void breakCurrentRoad(uint _index);
    void deleteCurrentRoad();
    void deleteCurrentIntersection();
    bool hasClipboardPoint = false;
    splinePoint clipboardPoint;
    splinePoint lastusedPoint;
    void copyVertex(uint _index);
    void pasteVertexGeometry(uint _index);
    void pasteVertexMaterial(uint _index);
    void pasteVertexAll(uint _index);

    void currentIntersection_findRoads();
    void solveIntersection(bool _solveTangents = false);
    void solve2RoadIntersection();

    std::vector<roadSection> roadSectionsList;
    std::vector<intersection> intersectionList;

    roadSection* currentRoad = nullptr;
    std::vector<int> subSelection;
    intersection* currentIntersection = nullptr;
    roadSection* intersectionSelectedRoad = nullptr;

    static ODE_bezier odeBezier;	// for the physics lookup, so this can build it, save and load binary etc
    static std::vector<cubicDouble>	staticBezierData;
    static std::vector<bezierLayer>	staticIndexData;
    static std::vector<bezierLayer>	staticIndexData_BakeOnly;
    uint debugNumBezier;
    uint debugNumIndex;

    AI_bezier pathBezier;
    aiIntersection	AIpathIntersect;


    bool isDirty = false;
    bezierIntersection physicsMouseIntersection;
    bool bHIT;
    int hitRandomFeedbackValue;
    uint hitRoadGuid;
    uint hitRoadIndex;
    uint hitRoadLane;
    bool hitRoadRight;
    float dA, dW;



    std::string rootPath;



    // interactive lane editing
    bool	editRight = true;
    int		editLaneIndex = 0;
    void incrementLane(int index, float _amount, roadSection* _road, bool _symmetrical);
    void shiftLaneSelect(int i) {
        if (editRight) {
            editLaneIndex += i;
            if (editLaneIndex > 8) editLaneIndex = 8;
            if (editLaneIndex < 0) {
                editLaneIndex = 0;
                editRight = false;
                return;
            }
        }
        else {
            editLaneIndex -= i;
            if (editLaneIndex > 8) editLaneIndex = 8;
            if (editLaneIndex < 0) {
                editLaneIndex = 0;
                editRight = true;
                return;
            }
        }

    }
    void setEditRight(bool _r) { editRight = _r; }
    void setEditLane(int _l) { editLaneIndex = _l; }

    //static roadMaterialCache roadMatCache;
    void loadRoadMaterial(uint _side, uint _slot);
    void dragdropRoadMaterial(uint _side, uint _slot, uint _index);
    void clearRoadMaterial(uint _side, uint _slot);
    uint getRoadMaterial(uint _side, uint _slot, uint _index);
    int selectFrom;
    int selectTo;
    uint selectionType = 0;
    const std::string getMaterialName(uint _side, uint _slot);
    const std::string getMaterialPath(uint _side, uint _slot);
    const Texture::SharedPtr getMaterialTexture(uint _side, uint _slot);
    static Texture::SharedPtr displayThumbnail;
    static bool showMaterials;

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const version)
    {
        if (version >= 101) {
            archive(CEREAL_NVP(roadMaterialCache::getInstance()));
        }
        archive(CEREAL_NVP(roadSectionsList));
        archive(CEREAL_NVP(intersectionList));
    }
};

CEREAL_CLASS_VERSION(roadNetwork, ROADNETWORK_CEREAL_VERSION);
