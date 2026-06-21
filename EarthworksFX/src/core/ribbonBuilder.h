#pragma once
#include "Falcor.h"

#include"terrafector.h"                             //??? why is this needed here
#include"hlsl/terrain/vegetation_defines.hlsli"
#include"hlsl/terrain/groundcover_defines.hlsli"    // FIXME combine these two

using namespace Falcor;




struct ribbonVertex
{
    ribbonVertex8 pack();

    static float objectScale;   //  0.002 for trees  // 32meter block 2mm presision
    static float radiusScale;   //  so biggest radius now objectScale / 2.0f;
    static float O;
    static float3 objectOffset;



    int     faceCamera = 0;    
    bool    diamond = false;   
    bool    startBit = false;
    float3  position;
    int     material;
    float2  uv;
    float3  bitangent;
    float3  tangent;
    float   radius;

    float4  lightCone = { 0, 1, 0, 1 };
    float   lightDepth = 0.2f;
    float ambientOcclusion = 1.f;
    unsigned char shadow = 255;         // ??? I think this in now unused
    float albedoScale = 1.f;
    float translucencyScale = 1.f;

    uint pivots[4] = { 0, 0, 0, 0 };

    static uint S_root;                 // I think this needs looking at
    uint    leafRoot = 0;
    float   leafStiffness = 1.f;
    float   leafFrequency = 10.f;
    float   leafIndex = 0.f;
};




struct ribbonBuilder
{
    void setup(float scale, float radius, float3 offset);
    void clearStats(int _max);
    void clearPivot();
    void clear();

    void startRibbon(bool _cameraFacing, uint pv[4]);
    void startRibbon(bool _cameraFacing, std::array<uint, 4> pv);
    void set(glm::mat4 _node, float _radius, int _material, float2 _uv, float _albedo, float _translucency, bool _clearLeafRoot = true,
        float _stiff = 0.5f, float _freq = 0.1f, float _index = 0.f, bool _diamond = false);
    uint pushPivot(uint _guid, _plant_anim_pivot _pivot);
    uint getRoot() { return vertex.S_root; }
    void setRoot(uint _r) { vertex.S_root = _r; }
    
    float3 egg(float2 extents, float3 vector, float yOffset);
    void lightBasic(float2 extents, float plantDepth, float yOffset);
    void lightBranch(uint from, uint to, float3 root, float3 tip, float plantDepth, float yOffset, float rootAO);
    
    uint numPacked() { return packed.size(); }
    uint numVerts() { return ribbons.size(); }

    void finalizeAndFillLastBlock();
    void pack();

    ribbonVertex8* getPackedData() {        return packed.data();    }
    

    float2 calculate_extents(glm::mat4 view);
    float buckets_8[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    float4 dU;

    ribbonVertex vertex;        // used for packing since some things accumulate
    std::vector<ribbonVertex>        ribbons;        // can we get this non static
    std::vector<ribbonVertex8>       packed;

    static float V_MAX;

    int numPivots() { return pivotPoints.size(); }
    std::vector<_plant_anim_pivot>   pivotPoints;
    std::map<int, int> pivotMap;

    bool    pushStart;
    int     lod_startBlock;         // This is the blok this lod started on
    int     maxBlocks;              // this will not accept more verts once we push past ? But how to handle when pushing lods
    int     totalRejectedVerts;     // this will not accept more verts once we push past ? But how to handle when pushing lods

    // build errors and warnigns
    bool tooManyPivots = false;
};

