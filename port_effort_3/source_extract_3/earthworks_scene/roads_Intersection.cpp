#include "roads_intersection.h"



#include "imgui.h"
#include "imgui_internal.h"

#include "cereal/archives/binary.hpp"
#include "cereal/archives/xml.hpp"
#include <sstream>
#include <fstream>
#include <filesystem>
#include <direct.h>

#include "assimp/scene.h"
#include "assimp/Exporter.hpp"




intersectionRoadLink* intersection::findLink(int roadGUID)
{
    for (int i = 0; i < roadLinks.size(); i++) {
        if (roadLinks[i].roadGUID == roadGUID) {
            return &roadLinks[i];
        }
    }
    return nullptr;
}



void intersection::clearRoads()
{
    roadLinks.clear();
}



bool intersection::isClose(glm::vec3 P, float dist)
{
    return(glm::length(P - this->anchor) < dist);
}



void intersection::updatePosition(float3 _pos, float3 _normal, uint _lod)
{
    lidarLOD = _lod;

    if (!customNormal) {
        anchorNormal = _normal;
    }
    anchor = _pos;// +anchorNormal * heightOffset;

    for (auto& link : roadLinks)
    {
        if (link.broadStart)
        {
            link.roadPtr->points[0].forceAnchor(_pos, _normal, _lod);
        }
        else
        {
            link.roadPtr->points[link.roadPtr->points.size() - 1].forceAnchor(_pos, _normal, _lod);
        }
    }

    updateTarmacLanes();
}



void intersection::updateCorner(float3 pos, float3 normal, int idx, int flags)
{
    int size = (int)roadLinks.size();


    glm::vec3 dir = pos - this->anchor;
    float L = glm::length(dir);
    float3 R = glm::cross(this->anchorNormal, dir);
    dir = glm::cross(R, this->anchorNormal);
    float3 posInPlane = this->anchor + dir;
    pos = posInPlane;

    if (flags == 0)
    {
        roadLinks[idx].corner_A = pos;
        roadLinks[(idx + 1) % size].corner_B = pos;
    }

    if (flags == -1)
    {
        float3 tangent = pos - roadLinks[idx].corner_A;
        roadLinks[idx].cornerTangent_A = tangent;
        roadLinks[(idx + 1) % size].cornerTangent_B = glm::normalize(-tangent) * glm::length(roadLinks[(idx + 1) % size].cornerTangent_B);
    }

    if (flags == 1)
    {
        float3 tangent = pos - roadLinks[idx].corner_B;
        roadLinks[idx].cornerTangent_B = tangent;
        roadLinks[(idx - 1 + size) % size].cornerTangent_A = glm::normalize(-tangent) * glm::length(roadLinks[(idx - 1 + size) % size].cornerTangent_A);
    }

    if (flags == 2)		// center tangents
    {
        glm::vec3 tangent = pos - anchor;
        glm::vec3 right = glm::cross(anchorNormal, tangent);
        roadLinks[idx].tangentVector = glm::cross(right, anchorNormal);
        if (roadLinks[idx].tangentVector.x == 0)
        {
            bool bCM = true;
        }
    }

    roadLinks[idx].roadPtr->solveRoad();
    roadLinks[(idx + 1) % size].roadPtr->solveRoad();
    //GLOBAL_roadSectionsList->at(roadLinks[idx].roadGUID).solveRoad();
    //GLOBAL_roadSectionsList->at(roadLinks[(idx + 1) % size].roadGUID).solveRoad();

    updateTarmacLanes();
}



void intersection::convertToGPU(std::vector<cubicDouble>& _bezier, std::vector<bezierLayer>& _index)
{

    uint size = (uint)lanesTarmac.size() / 2;
    if (size == 0) return;


    for (uint i = 0; i < size; i++)
    {
        //_index.push_back(bezierLayer(bezier_edge::outside, bezier_edge::center, MATERIAL_EDIT_BLUE, _bezier.size(), false, 0.0f, 0.0f, false, false));
        _index.push_back(bezierLayer(bezier_edge::outside, bezier_edge::outside, MATERIAL_EDIT_REDDOT, _bezier.size(), false, -0.1f, -0.2f, false, false));
        _index.push_back(bezierLayer(bezier_edge::center, bezier_edge::center, MATERIAL_EDIT_REDDOT, _bezier.size(), false, 0.1f, 0.2f, false, false));
        _bezier.push_back(lanesTarmac[i * 2]);


        //_index.push_back(bezierLayer(bezier_edge::outside, bezier_edge::center, MATERIAL_EDIT_GREEN, _bezier.size(), false, 0.0f, 0.0f, false, false));
        _index.push_back(bezierLayer(bezier_edge::outside, bezier_edge::outside, MATERIAL_EDIT_REDDOT, _bezier.size(), false, -0.1f, -0.2f, false, false));
        _index.push_back(bezierLayer(bezier_edge::center, bezier_edge::center, MATERIAL_EDIT_REDDOT, _bezier.size(), false, 0.1f, 0.2f, false, false));
        _bezier.push_back(lanesTarmac[i * 2 + 1]);
    }
}



void intersection::updateTarmacLanes()
{
    uint numInter = (uint)roadLinks.size();

    uint numTar = 3;
    if (numInter == 4) numTar = 5;


    lanesTarmac.resize(numTar * 2);


    uint index = 0;

    // From this road to the next one, there form basis of turn lanes
    for (uint j = 0; j < numInter; j++) {

        uint next = (j + 1) % numInter;

        float3 perp = glm::normalize(anchor - roadLinks[j].corner_A);


        if (roadLinks[j].broadStart)
        {
            float width = roadLinks[j].roadPtr->points[1].widthLeft + roadLinks[j].roadPtr->points[1].widthRight;

            // scale it on ang;e
            if (!roadLinks[j].bOpenCorner)
            {
                width *= 1.4f;
            }

            lanesTarmac[index + 0].data[0][0] = roadLinks[j].roadPtr->points[1].bezier[right].pos_uv();
            lanesTarmac[index + 0].data[0][1] = roadLinks[j].roadPtr->points[1].bezier[right].backward_uv();
            lanesTarmac[index + 0].data[0][2] = float4(roadLinks[j].corner_A + roadLinks[j].cornerTangent_A + perp * width, 0);
            lanesTarmac[index + 0].data[0][3] = float4(roadLinks[j].corner_A + perp * width, 0);
            lanesTarmac[index + 1].data[0][0] = float4(roadLinks[j].corner_A + perp * width, 0);
            lanesTarmac[index + 1].data[0][1] = float4(roadLinks[j].corner_A - roadLinks[j].cornerTangent_A + perp * width, 0);

            lanesTarmac[index + 0].data[1][0] = roadLinks[j].roadPtr->points[1].bezier[left].pos_uv();
            lanesTarmac[index + 0].data[1][1] = roadLinks[j].roadPtr->points[1].bezier[left].backward_uv();
            lanesTarmac[index + 0].data[1][2] = float4(roadLinks[j].corner_A + roadLinks[j].cornerTangent_A, 0);
            lanesTarmac[index + 0].data[1][3] = float4(roadLinks[j].corner_A, 0);
            lanesTarmac[index + 1].data[1][0] = float4(roadLinks[j].corner_A, 0);
            lanesTarmac[index + 1].data[1][1] = float4(roadLinks[j].corner_A - roadLinks[j].cornerTangent_A, 0);
        }
        else
        {
            uint idx1 = (uint)roadLinks[j].roadPtr->points.size() - 2;
            
            float width = roadLinks[j].roadPtr->points[idx1].widthLeft + roadLinks[j].roadPtr->points[idx1].widthRight;

            if (!roadLinks[j].bOpenCorner)
            {
                width *= 1.4f;
            }

            lanesTarmac[index + 0].data[0][0] = roadLinks[j].roadPtr->points[idx1].bezier[left].pos_uv();
            lanesTarmac[index + 0].data[0][1] = roadLinks[j].roadPtr->points[idx1].bezier[left].forward_uv();
            lanesTarmac[index + 0].data[0][2] = float4(roadLinks[j].corner_A + roadLinks[j].cornerTangent_A + perp * width, 0);
            lanesTarmac[index + 0].data[0][3] = float4(roadLinks[j].corner_A + perp * width, 0);
            lanesTarmac[index + 1].data[0][0] = float4(roadLinks[j].corner_A + perp * width, 0);
            lanesTarmac[index + 1].data[0][1] = float4(roadLinks[j].corner_A - roadLinks[j].cornerTangent_A + perp * width, 0);

            lanesTarmac[index + 0].data[1][0] = roadLinks[j].roadPtr->points[idx1].bezier[right].pos_uv();
            lanesTarmac[index + 0].data[1][1] = roadLinks[j].roadPtr->points[idx1].bezier[right].forward_uv();
            lanesTarmac[index + 0].data[1][2] = float4(roadLinks[j].corner_A + roadLinks[j].cornerTangent_A, 0);
            lanesTarmac[index + 0].data[1][3] = float4(roadLinks[j].corner_A, 0);
            lanesTarmac[index + 1].data[1][0] = float4(roadLinks[j].corner_A, 0);
            lanesTarmac[index + 1].data[1][1] = float4(roadLinks[j].corner_A - roadLinks[j].cornerTangent_A, 0);
        }


        if (roadLinks[next].broadStart)
        {
            lanesTarmac[index + 1].data[0][2] = roadLinks[next].roadPtr->points[1].bezier[left].backward_uv();
            lanesTarmac[index + 1].data[0][3] = roadLinks[next].roadPtr->points[1].bezier[left].pos_uv();

            lanesTarmac[index + 1].data[1][2] = roadLinks[next].roadPtr->points[1].bezier[right].backward_uv();
            lanesTarmac[index + 1].data[1][3] = roadLinks[next].roadPtr->points[1].bezier[right].pos_uv();
        }
        else
        {
            lanesTarmac[index + 1].data[0][2] = roadLinks[next].roadPtr->points[roadLinks[next].roadPtr->points.size() - 2].bezier[right].forward_uv();
            lanesTarmac[index + 1].data[0][3] = roadLinks[next].roadPtr->points[roadLinks[next].roadPtr->points.size() - 2].bezier[right].pos_uv();

            lanesTarmac[index + 1].data[1][2] = roadLinks[next].roadPtr->points[roadLinks[next].roadPtr->points.size() - 2].bezier[left].forward_uv();
            lanesTarmac[index + 1].data[1][3] = roadLinks[next].roadPtr->points[roadLinks[next].roadPtr->points.size() - 2].bezier[left].pos_uv();
        }
        index += 2;
    }




    // Now for straight across
    if (numInter == 4)
    {
        for (uint j = 0; j < 2; j++) {

            uint next = (j + 2) % numInter;

            if (roadLinks[j].broadStart)
            {
                float width = roadLinks[j].roadPtr->points[1].widthLeft + roadLinks[j].roadPtr->points[1].widthRight;

                lanesTarmac[index + 0].data[0][0] = roadLinks[j].roadPtr->points[1].bezier[right].pos_uv();
                lanesTarmac[index + 0].data[0][1] = roadLinks[j].roadPtr->points[1].bezier[right].backward_uv();
            }
            else
            {
                uint idx1 = (uint)roadLinks[j].roadPtr->points.size() - 2;
                float width = roadLinks[j].roadPtr->points[idx1].widthLeft + roadLinks[j].roadPtr->points[idx1].widthRight;

                lanesTarmac[index + 0].data[0][0] = roadLinks[j].roadPtr->points[idx1].bezier[left].pos_uv();
                lanesTarmac[index + 0].data[0][1] = roadLinks[j].roadPtr->points[idx1].bezier[left].forward_uv();

                lanesTarmac[index + 0].data[1][0] = roadLinks[j].roadPtr->points[idx1].bezier[right].pos_uv();
                lanesTarmac[index + 0].data[1][1] = roadLinks[j].roadPtr->points[idx1].bezier[right].forward_uv();
            }


            if (roadLinks[next].broadStart)
            {
                lanesTarmac[index + 0].data[0][2] = roadLinks[next].roadPtr->points[1].bezier[left].backward_uv();
                lanesTarmac[index + 0].data[0][3] = roadLinks[next].roadPtr->points[1].bezier[left].pos_uv();

                lanesTarmac[index + 0].data[1][2] = roadLinks[next].roadPtr->points[1].bezier[right].backward_uv();
                lanesTarmac[index + 0].data[1][3] = roadLinks[next].roadPtr->points[1].bezier[right].pos_uv();
            }
            else
            {
                lanesTarmac[index + 0].data[0][2] = roadLinks[next].roadPtr->points[roadLinks[next].roadPtr->points.size() - 2].bezier[right].forward_uv();
                lanesTarmac[index + 0].data[0][3] = roadLinks[next].roadPtr->points[roadLinks[next].roadPtr->points.size() - 2].bezier[right].pos_uv();

                lanesTarmac[index + 0].data[1][2] = roadLinks[next].roadPtr->points[roadLinks[next].roadPtr->points.size() - 2].bezier[left].forward_uv();
                lanesTarmac[index + 0].data[1][3] = roadLinks[next].roadPtr->points[roadLinks[next].roadPtr->points.size() - 2].bezier[left].pos_uv();
            }
            


            index += 1;  // tjhis skips one unused
        }
    }
}

