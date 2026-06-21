#include "cascadeShadowMaps.h"



void shadowMap::init(uint _size)
{
    Fbo::Desc desc;
    desc.setDepthStencilTarget(ResourceFormat::D24UnormS8);
    desc.setColorTarget(0u, ResourceFormat::R8Uint);        // add , true if we want to write to it UAV
    //fbo = Fbo::create2D(_size, _size, desc);
}



void shadowMap::startRender()
{
}



void shadowMap::stopRender()
{
}



void shadowMap::setToShaders()
{
}





void cascadeShadows::init(uint _cascades, uint _size)
{
    //shadowCache.resize(40);

    /*
    cascades = _cascades;
    size = _size;
    maps.resize(cascades);
    for (auto &M : maps)
    {
        M.init(_size);
    }*/
}

//?? of do we want freedoem ca chnage orientation to better fit frustum
// I quite like that, but id depends how I blend
void cascadeShadows::update(float3 _pos, float3 _dir, float _tan, float3 _sunDir, float3 _sunUp, float3 _sunRight)
{
    float distance = 20.f;  // just somethign to test
    float3 center = _pos + _dir * distance;  // just something
    float radius = distance * _tan;

    float3 R, U, D;
    U = { 0, 1, 0};
    D = _sunDir;
    R = glm::normalize(glm::cross(U, D));
    U = glm::normalize(glm::cross(D, R));
    glm::mat4  ViewMat = glm::mat4(1.0);
    ViewMat[0] = float4(R, 0);
    ViewMat[1] = float4(U, 0);
    ViewMat[2] = float4(D, 0);
    ViewMat[3] = float4(center, 1);

    glm::mat4 V, P, VP;
    V = glm::inverse(ViewMat);
    P = glm::orthoLH(-distance, distance, -distance, distance, 0.0f, distance * 10.f);
    VP = P * V;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            //maps[0].viewproj[j][i] = VP[j][i];
            //maps[0].view[j][i] = ViewMat[j][i];
        }
    }
    /*

    // remeber that these are transposed as well here
    maps[0].frustum[0][0] = P[0][3] + P[0][0];
    maps[0].frustum[0][1] = P[1][3] + P[1][0];
    maps[0].frustum[0][2] = P[2][3] + P[2][0];
    maps[0].frustum[0][3] = P[3][3] + P[3][0];

    maps[0].frustum[1][0] = P[0][3] - P[0][0];
    maps[0].frustum[1][1] = P[1][3] - P[1][0];
    maps[0].frustum[1][2] = P[2][3] - P[2][0];
    maps[0].frustum[1][3] = P[3][3] - P[3][0];

    maps[0].frustum[2][0] = P[0][3] + P[0][1];
    maps[0].frustum[2][1] = P[1][3] + P[1][1];
    maps[0].frustum[2][2] = P[2][3] + P[2][1];
    maps[0].frustum[2][3] = P[3][3] + P[3][1];

    maps[0].frustum[3][0] = P[0][3] - P[0][1];
    maps[0].frustum[3][1] = P[1][3] - P[1][1];
    maps[0].frustum[3][2] = P[2][3] - P[2][1];
    maps[0].frustum[3][3] = P[3][3] - P[3][1];
    */
}


uint cascadeShadows::currentMap()
{
    // but use this to cycle through the maps - and work out how to map this to cameras in terrain
    return 0;
}
