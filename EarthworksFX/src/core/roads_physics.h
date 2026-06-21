#pragma once

#include "terrafector.h"
#include "roads_bezier.h"



#define numBezierCache 16
#define bezierCacheSize 10



struct testBezierReturn
{
    bool bHit = false;
    uint index;
    float u, v;			// uv for index segment, translated vy UV mapping
    uint bezier;
    uint bezierCacheIndex;
    float s, t;
    int t_idx;
};



struct plane_2d
{
    float dst;
    glm::vec2 norm;		// test vec2, should work Y = 0

    void frompoints(glm::vec3 A, glm::vec3 B);
    float distance(glm::vec2 P);
};



struct cachePoint
{
    float3 A;
    float dA;
    float3 B;
    float dB;

    plane_2d planeTangent;
    plane_2d planePerp;
};



// a semi-random cahche since its faster the LRU
struct bezierSegment
{
    float3 A;		// FIXME float4 for UV's
    float radius;
    float3 B;
    float optimalShift;	//8

    plane_2d planeTangent;
    plane_2d planeInner;
    plane_2d planeOuter;	//17

    float distance;	// For AI - so from the start I guess
    float length;	// in teh middle, avergawe of left and right side
    float camber;
    float curveRadius;

    // EXTRA STUFF
    float3 optimalPos;
    float optimalTemp;
};



struct cacheItem
{
    uint counter = 999999;		// for LRU-ish, start big
    int bezierIndex = -1;	// points back to 
    bezierSegment points[numBezierCache + 1];
};



struct physicsBezierLayer
{
    bezier_edge edge[2];
    float offset[2];
    uint material;
};



struct physicsBezier
{
    physicsBezier() { ; }
    physicsBezier(splinePoint a, splinePoint b, bool bRight, uint _guid, uint _index, bool _fullWidth = false);
    void addLayer(bezier_edge inner, bezier_edge outer, uint material, float w0, float w1);

    void binary_import(FILE* file);
    void binary_export(FILE* file);

    glm::vec2 center;
    float radius;
    plane_2d startPlane;
    plane_2d endPlane;
    int cacheIndex = -1;	// -1 if not cached
    bool bRighthand;	// this comes from a right hand side of the road - needed for editing - not currently exported

    glm::vec4 bezier[2][4];
    std::vector<physicsBezierLayer> layers;

    // editor info only
    uint roadGUID;
    uint index;
};




struct bezierCache
{
    uint findEmpty();
    void solveStats();
    void cacheSpline(physicsBezier* pBez, uint cacheSlot);
    void increment();
    void clear();

    std::array<cacheItem, bezierCacheSize> cache;
    uint numUsed;
    uint numFree;
};




struct bezierOneIntersection
{
    bezierOneIntersection() { ; }
    bezierOneIntersection(glm::vec2 P, uint boundingIndex);

    void updatePosition(glm::vec2 P, float distanceTraveled);

    glm::vec2 pos;
    bool bHit = false;
    float distanceTillNextSearch = 0;		// to improve the wide searches if we are far away from everything - set during wide search
    int boundIndex = -1;
    int cacheIndex = -1;
    int t_idx = 0;

    glm::vec2 UV;
    float d0;		// distances in from the edge for V calculation  SHIT NAQME RETHINK
    float d1;
    float bezierHeight;	// before displacement maps

    bool bRighthand;
    uint roadGUID;
    uint index;
    uint lane;		// set later by the road
    float a;
    float W;
};



struct bezierIntersection
{
    void updatePosition(glm::vec2 P);

    glm::vec2 pos;
    uint cellHash = 0;
    std::vector<bezierOneIntersection> beziers;

    // and this is what we are trying to solve for
    //bool bHit = false;
    uint numHits = 0;
    float height;
    float grip;
    float wetness;

    // stuff to make it useful for editing
    // some is added back by the road
    bool bRightHand;	// editor only
    int lane;
};



struct bezierFastGrid
{
    void allocate(float size, uint numX, uint numY);
    void populateBezier();
    inline uint getHash(glm::vec2 pos);
    std::vector<uint> getBlockLookup(uint hash);
    void insertBezier(glm::vec2 pos, float radius, uint index);

    void binary_import(FILE* file);
    void binary_export(FILE* file);

    float size;
    int2 offset;
    uint nX, nY;	// just assume simmetry aroudn the origin for now
    uint maxHash;

    // temp data for the build
    std::vector<std::vector<uint>> buidData;
};



class ODE_bezier
{
public:
    void intersect(bezierIntersection* pI);
    bool intersectBezier(bezierOneIntersection* pI);
    bool testBounds(bezierOneIntersection* pI);
    void blendLayers(bezierIntersection* pI);
    void setGrid(float size, uint numX, uint numY);
    void buildGridFromBezier();
    void clear();

public:
    std::vector<physicsBezier> bezierBounding;
    std::vector<physicsBezier> bezierAI;	// test for Kevin
    bezierFastGrid	gridLookup;
    bezierCache cache;
    bool needsReCache = false;
};





