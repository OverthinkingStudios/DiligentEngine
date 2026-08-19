#pragma once

#include "terrafector.h"
#include"roads_physics.h"



struct aiIntersection
{
    void setPos(glm::vec2 _p);
    glm::vec2 pos;
    glm::vec2 dir;
    float delDistance;

    bool bHit = false;

    float dist_Left;
    float dist_Right;
    float	distance;		// how far past the start position we are
    uint	segment;		// current bezier patch we are on
    float	road_angle;	// relative to current tangent vector

    // and then some data describing the road up ahead
    uint numSteps;
    float camber[100];

    float lookahead_curvature;
    float lookahead_camber;
    float lookahead_apex_distance;
    uint lookahead_apex_left;			// none, left, right
    float lookahead_apex_camber;
    float lookahead_apex_curvature;
};



class AI_bezier
{
public:
    glm::vec3 startpos;
    glm::vec3 endpos;
    float start;	// in meters I guess
    float end;
    float length;
    uint startIdx;
    uint endIdx;

    uint numBezier;
    int isClosedPath = false;
    float pathLenght = 0.0f;

    std::vector<cubicDouble>	bezierPatches;
    std::vector<bezierSegment>  patchSegments;
    std::vector<bezierSegment>  segments;
    int kevRepeats = 100;
    float kevOutScale = 22;
    float kevInScale = 34;
    int kevSmooth = 5;
    int kevAvsCnt = 0;
    int kecMaxCnt = 9;
    float kevSampleMinSpacing = 5.0f;

    void intersect(aiIntersection* _pAI);
    float rootsearch(glm::vec2 _pos);

    void clear();
    void pushSegment(glm::vec4 bezier[2][4]);	// just cache as well
    void cacheAll();
    void solveStartEnd();


    void exportAI(FILE* file);
    void exportAITXTDEBUG(FILE* file);
    void exportCSV(FILE* file, uint _side);
    void exportGates(FILE* file);

    void save();
    void load(FILE* file);


    // Now excpand it for actual path generation 
    struct road {
        uint roadIndex;
        bool bForward;
    };
    std::vector<road>	roads;

    void setStart(glm::vec3 _start) { startpos = _start; }
    void setEnd(glm::vec3 _end) { endpos = _end; }
    void clearRoads() { roads.clear(); }
    void addRoad(road _road) { roads.push_back(_road); }
};
