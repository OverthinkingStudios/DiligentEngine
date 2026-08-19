#pragma once
#include "Falcor.h"
//#include "lru_cache.h"
using namespace Falcor;





class shadowMap
{
public:
    void init(uint _size);
    void startRender();
    void stopRender();
    void setToShaders();    // I think

public:
    Fbo::SharedPtr		fboNear;
    Fbo::SharedPtr		fboVeg;
    Fbo::SharedPtr		fboFar;
    float4x4            frustum;
    float4x4            viewproj, view;
};


class cascadeShadows
{
public:
    void init(uint _cascades, uint _size);
    void update(float3 _pos, float3 _dir, float _tan, float3 _sunDir, float3 _sunUp, float3 _sunRight);
    uint currentMap();


    uint size = 2048;
    uint cascades = 4;
    
    //LRUCache<uint32_t, shadowMap> shadowCache;
};
