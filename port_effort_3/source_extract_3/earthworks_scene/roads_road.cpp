#include "roads_road.h"



#include "imgui.h"
#include "imgui_internal.h"

#include "cereal/archives/binary.hpp"
#include "cereal/archives/xml.hpp"
#include <fstream>

#include "assimp/scene.h"
#include "assimp/Exporter.hpp"


//#pragma optimize("", off)



void cubic_Casteljau_Full(float t, glm::vec3 P0, glm::vec3 P1, glm::vec3 P2, glm::vec3 P3, glm::vec3& pos, glm::vec3& vel, glm::vec3& acc)
{
    glm::vec3 pA = lerp(P0, P1, t);
    glm::vec3 pB = lerp(P1, P2, t);
    glm::vec3 pC = lerp(P2, P3, t);

    glm::vec3 pD = lerp(pA, pB, t);
    glm::vec3 pE = lerp(pB, pC, t);

    pos = lerp(pD, pE, t);
    vel = (pE - pD) * 3.0f;

    glm::vec3 D0 = P1 - P0;
    glm::vec3 D1 = P2 - P1;
    glm::vec3 D2 = P3 - P2;

    glm::vec3 DD0 = D1 - D0;
    glm::vec3 DD1 = D2 - D1;
    acc = lerp(DD0, DD1, t) * 6.0f;
}



glm::vec3 cubic_Casteljau(float t, glm::vec3 P0, glm::vec3 P1, glm::vec3 P2, glm::vec3 P3)
{
    glm::vec3 pA = lerp(P0, P1, t);
    glm::vec3 pB = lerp(P1, P2, t);
    glm::vec3 pC = lerp(P2, P3, t);

    glm::vec3 pD = lerp(pA, pB, t);
    glm::vec3 pE = lerp(pB, pC, t);

    return lerp(pD, pE, t);
}



glm::vec3 cubic_Casteljau(float t, bezierPoint* A, bezierPoint* B)
{
    return cubic_Casteljau(t, A->pos, A->forward(), B->backward(), B->pos);
}



glm::vec3 del_cubic_Casteljau(float t0, float t1, bezierPoint* A, bezierPoint* B)
{
    return cubic_Casteljau(t1, A->pos, A->forward(), B->backward(), B->pos) - cubic_Casteljau(t0, A->pos, A->forward(), B->backward(), B->pos);
}





/*  roadSection
    --------------------------------------------------------------------------------------------------------------------*/
    //std::vector<cubicDouble>	roadNetwork::staticBezierData;
    //std::vector<bezierLayer>	roadNetwork::staticIndexData;
    //ODE_bezier					roadNetwork::odeBezier;



bool isSaneVector(glm::vec3 v)
{
    if (std::isnan(v.x)) return false;
    if (std::isnan(v.y)) return false;
    if (std::isnan(v.z)) return false;

    if (fabs(v.x) > 100.f) return false;
    if (fabs(v.y) > 100.f) return false;
    if (fabs(v.z) > 100.f) return false;

    return true;
}



void roadSection::clearSelection()
{
    for (auto& point : points) {
        point.selected = false;
    }
}



void roadSection::selectAll()
{
    for (auto& point : points) {
        point.selected = true;
    }
}



void roadSection::newSelection(int index)
{
    clearSelection();
    points[index].selected = true;
}



void roadSection::addSelection(int index)	// do shift as well
{
    points[index].selected = !points[index].selected; //toggle
}



void roadSection::breakLink(bool bStart)
{
    if (bStart) {
        int_GUID_start = -1;
        points.front().bAutoGenerate = true;
    }
    else {
        int_GUID_end = -1;
        points.back().bAutoGenerate = true;
    }

    solveRoad();
}



void roadSection::setAIOnly(bool AI)
{
    for (auto& pnt : points)
    {
        pnt.isAIonly = AI;
    }
}



#define MATERIAL_EDIT_SELECT 2040
#define MATERIAL_EDIT_GREEN 2041
#define MATERIAL_EDIT_BLUE 2042
#define MATERIAL_EDIT_WHITEDOT 2043
#define MATERIAL_EDIT_REDDOT 2044


#define MATERIAL_BLEND 2045
#define MATERIAL_SOLID 2046
#define MATERIAL_CURVATURE 2047



void roadSection::convertToGPU_Realistic(std::vector<cubicDouble>& _bezier, std::vector<bezierLayer>& _index, std::vector<bezierLayer>& _index_BAKE, uint _from, uint _to, bool _stylized, bool _showMaterials)
{
    uint numSegments = (uint)points.size();
    if (numSegments < 2) return;				// Return if zero segments
    numSegments -= 1;

    int maxMaterial = (int)roadMaterialCache::getInstance().materialVector.size() - 1;

    if (points[0].isAIonly && !_stylized) return;				// Do not convert AI roads


    if (isBasicLinearMarking)
    {
        // right - Inner - Thsi is the only bezier associated
        for (uint i = 0; i < numSegments; i++)
        {
            float w1 = points[i].lanesRight[lane1].laneWidth * 0.5f;
            float w2 = points[i + 1].lanesRight[lane1].laneWidth * 0.5f;

            uint feedback = (uint)_bezier.size();

            points[i].right_Geom_Idx[0] = (uint)_bezier.size();
            _bezier.push_back(cubicDouble(points[i], points[i + 1]));
        }

        if (_showMaterials)
        {
            int material;


            for (uint i = 0; i < numSegments; i++)
            {
                bool isBridge = points[i].isBridge;
                material = __min(maxMaterial, points[i].matCenter[1]);

                if (material >= 0)
                {
                    for (auto& layer : roadMaterialCache::getInstance().materialVector[material].layers)
                    {
                        _index.push_back(bezierLayer(bezier_edge::center, bezier_edge::outside, layer.materialIndex, points[i].right_Geom_Idx[0], true, 0, 0));
                    }
                }
            }
        }
        else if (_stylized)
        {
            for (uint i = 0; i < numSegments; i++)
            {
                float w = points[i].lanesLeft[2].laneWidth * 0.5f;
                _index.push_back(bezierLayer(bezier_edge::center, bezier_edge::outside, MATERIAL_EDIT_WHITEDOT, points[i].right_Geom_Idx[0], true, 0, 0));
            }
        }

        return;
    }






    // Right hand side beziers ########################################################################################################
    // right - Inner
    for (uint i = 0; i < numSegments; i++)
    {
        uint feedback = (uint)_bezier.size();
        points[i].right_Geom_Idx[0] = (uint)_bezier.size();
        _bezier.push_back(cubicDouble(points[i], points[i + 1], true));
    }


    // FIXME right outer - but from 5 bez roads later
    for (uint i = 0; i < numSegments; i++)
    {
        points[i].right_Geom_Idx[1] = (uint)_bezier.size();
        //float W0 = 1.0f + __min(1.0f, (points[i].lanesRight[clearing].laneWidth / points[i].widthRight));
        //float W1 = 1.0f + __min(1.0f, (points[i + 1].lanesRight[clearing].laneWidth / points[i + 1].widthRight));
        _bezier.push_back(cubicDouble(points[i], points[i + 1], true, points[i].lanesRight[clearing].laneWidth, points[i + 1].lanesRight[clearing].laneWidth));		// Misuse my percentage system to create - But uise Width a, dVERGE
    }

    // And then do a spacial one for sizewlaks I guess



    // FIXME will include unnessesary empty beziers but just add a whole lane if any in use, its cleaner
    for (int lane = 0; lane < 7; lane++)
    {
        if (rightLaneInUse[lane])
        {
            for (uint i = 0; i < numSegments; i++)
            {
                points[i].right_Lane_Idx[lane] = (uint)_bezier.size();
                _bezier.push_back(cubicDouble(points[i], points[i + 1], true, points[i].lanesRight[lane].percentage, points[i + 1].lanesRight[lane].percentage));
            }
        }
    }





    // Right hand side layers ########################################################################################################

    if (_showMaterials)
    {
        int material;
        for (uint i = 0; i < numSegments; i++)
        {
            bool isBridge = points[i].isBridge;

            if (!_stylized) {
                _index_BAKE.push_back(bezierLayer(bezier_edge::center, bezier_edge::outside, MATERIAL_SOLID, points[i].right_Geom_Idx[0], false, 0, 0, isBridge, isBridge));
                _index_BAKE.push_back(bezierLayer(bezier_edge::center, bezier_edge::outside, MATERIAL_BLEND, points[i].right_Geom_Idx[1], false, 0, 0, isBridge, isBridge));
            }

            // verge
            material = __min(maxMaterial, points[i].matRight[splinePoint::matName::verge]);
            if (material >= 0)
            {
                for (auto& layer : roadMaterialCache::getInstance().materialVector[material].layers)
                {
                    _index.push_back(bezierLayer((bezier_edge)layer.sideA, (bezier_edge)layer.sideB, layer.materialIndex, points[i].right_Geom_Idx[layer.bezierIndex], false, layer.offsetA, layer.offsetB, isBridge, isBridge));
                }
            }


            // road
            material = __min(maxMaterial, points[i].matRight[splinePoint::matName::tarmac]);
            if (material >= 0)
            {
                for (auto& layer : roadMaterialCache::getInstance().materialVector[material].layers)
                {
                    _index.push_back(bezierLayer((bezier_edge)layer.sideA, (bezier_edge)layer.sideB, layer.materialIndex, points[i].right_Geom_Idx[layer.bezierIndex], false, layer.offsetA, layer.offsetB, isBridge, isBridge));
                    if ((int)points[i].right_Geom_Idx[layer.bezierIndex] < GUID)
                    {
                        bool bCM = true;
                    }
                }
            }

            // sidewalk
            material = __min(maxMaterial, points[i].matRight[splinePoint::matName::sidewalk]);
            if (material >= 0)
            {
                for (auto& layer : roadMaterialCache::getInstance().materialVector[material].layers)
                {
                    _index.push_back(bezierLayer((bezier_edge)layer.sideA, (bezier_edge)layer.sideB, layer.materialIndex, points[i].right_Geom_Idx[layer.bezierIndex], false, layer.offsetA, layer.offsetB, isBridge, isBridge));
                }
            }

            // gutter
            material = __min(maxMaterial, points[i].matRight[splinePoint::matName::gutter]);
            if (material >= 0)
            {
                for (auto& layer : roadMaterialCache::getInstance().materialVector[material].layers)
                {
                    _index.push_back(bezierLayer((bezier_edge)layer.sideA, (bezier_edge)layer.sideB, layer.materialIndex, points[i].right_Geom_Idx[layer.bezierIndex], false, layer.offsetA, layer.offsetB, isBridge, isBridge));
                }
            }


            // lanes paint
            for (int laneNr = innerShoulder; laneNr <= outerShoulder; laneNr++)
            {
                bezier_edge side = bezier_edge::outside;
                int laneToDisplay = laneNr;

                if (rightLaneInUse[laneNr])
                {
                    // DAMN DISMAL MAPPING HERE - not sure this maps perfectly
                    switch (laneNr)
                    {
                    case innerShoulder:	material = __min(maxMaterial, points[i].matRight[splinePoint::matName::innerShoulder]); break;
                    case innerTurn:		material = __min(maxMaterial, points[i].matRight[splinePoint::matName::innerTurn]); side = bezier_edge::center; laneToDisplay = lane1;	 break;
                    case lane1:			material = __min(maxMaterial, points[i].matRight[splinePoint::matName::lane1]); break;
                    case lane2:			material = __min(maxMaterial, points[i].matRight[splinePoint::matName::lane2]); break;
                    case lane3:			material = __min(maxMaterial, points[i].matRight[splinePoint::matName::lane2]); break;
                    case outerTurn:		material = __min(maxMaterial, points[i].matRight[splinePoint::matName::outerTurn]);		laneToDisplay = laneNr;	 break;
                    case outerShoulder: material = __min(maxMaterial, points[i].matRight[splinePoint::matName::shoulder]);		side = bezier_edge::center; break;
                    }

                    if (material >= 0)
                    {
                        for (auto& layer : roadMaterialCache::getInstance().materialVector[material].layers)
                        {
                            // FIXME I think sides should be fixed here
                            _index.push_back(bezierLayer(side, side, layer.materialIndex, points[i].right_Lane_Idx[laneToDisplay], false, layer.offsetA, layer.offsetB, isBridge, isBridge));
                        }
                    }
                }
            }

            // lanes wear & tear
            for (int laneNr = innerTurn; laneNr <= outerTurn; laneNr++)
            {
                if (rightLaneInUse[laneNr])
                {
                    // DAMN DISMAL MAPPING HERE - not sure this maps perfectly
                    switch (laneNr)
                    {
                    case innerTurn:		material = __min(maxMaterial, points[i].matRight[splinePoint::matName::rubberInner]); break;
                    case lane1:			material = __min(maxMaterial, points[i].matRight[splinePoint::matName::rubberLane1]); break;
                    case lane2:			material = __min(maxMaterial, points[i].matRight[splinePoint::matName::rubberLane2]); break;
                    case lane3:			material = __min(maxMaterial, points[i].matRight[splinePoint::matName::rubberLane3]); break;
                    case outerTurn:		material = __min(maxMaterial, points[i].matRight[splinePoint::matName::rubberOuter]); break;
                    }

                    if (material >= 0)
                    {
                        for (auto& layer : roadMaterialCache::getInstance().materialVector[material].layers)
                        {
                            _index.push_back(bezierLayer((bezier_edge)layer.sideA, (bezier_edge)layer.sideB, layer.materialIndex, points[i].right_Lane_Idx[laneNr], false, layer.offsetA, layer.offsetB, isBridge, isBridge));
                        }
                    }
                }
            }
        }
    }










    // Left hand side beziers ########################################################################################################
    // left - Inner
    for (uint i = numSegments; i > 0; i--)
    {
        points[i - 1].left_Geom_Idx[0] = (uint)_bezier.size();
        _bezier.push_back(cubicDouble(points[i], points[i - 1], false));
    }



    // FIXME right outer - but from 5 bez roads later
    for (uint i = numSegments; i > 0; i--)
    {
        points[i - 1].left_Geom_Idx[1] = (uint)_bezier.size();
        //float W0 = 1.0f + __min(1.0f, (points[i].lanesLeft[clearing].laneWidth / points[i].widthLeft));
        //float W1 = 1.0f + __min(1.0f, (points[i - 1].lanesLeft[clearing].laneWidth / points[i - 1].widthLeft));
        _bezier.push_back(cubicDouble(points[i], points[i - 1], false, points[i].lanesLeft[clearing].laneWidth, points[i - 1].lanesLeft[clearing].laneWidth));		// Misuse my percentage system to create - But uise Width a, dVERGE
    }



    // FIXME will include unnessesary empty beziers but just add a whole lane if any in use, its cleaner
    for (int lane = 0; lane < 7; lane++)
    {
        if (leftLaneInUse[lane])
        {
            for (uint i = numSegments; i > 0; i--)
            {
                points[i - 1].left_Lane_Idx[lane] = (uint)_bezier.size();
                _bezier.push_back(cubicDouble(points[i], points[i - 1], false, points[i].lanesLeft[lane].percentage, points[i - 1].lanesLeft[lane].percentage));
            }
        }
    }



    // Left hand side layers ########################################################################################################
    if (_showMaterials)
    {
        int material;
        for (uint i = 0; i < numSegments; i++)
        {
            bool isBridge = points[i].isBridge;

            if (!_stylized) {
                _index_BAKE.push_back(bezierLayer(bezier_edge::center, bezier_edge::outside, MATERIAL_SOLID, points[i].left_Geom_Idx[0], true, 0, 0, isBridge, isBridge));
                _index_BAKE.push_back(bezierLayer(bezier_edge::center, bezier_edge::outside, MATERIAL_BLEND, points[i].left_Geom_Idx[1], true, 0, 0, isBridge, isBridge));
            }

            // verge
            material = __min(maxMaterial, points[i].matLeft[splinePoint::matName::verge]);
            if (material >= 0)
            {
                for (auto& layer : roadMaterialCache::getInstance().materialVector[material].layers)
                {
                    _index.push_back(bezierLayer((bezier_edge)layer.sideA, (bezier_edge)layer.sideB, layer.materialIndex, points[i].left_Geom_Idx[layer.bezierIndex], true, layer.offsetA, layer.offsetB, isBridge, isBridge));
                }
            }


            // road
            material = __min(maxMaterial, points[i].matLeft[splinePoint::matName::tarmac]);
            if (material >= 0)
            {
                for (auto& layer : roadMaterialCache::getInstance().materialVector[material].layers)
                {
                    _index.push_back(bezierLayer((bezier_edge)layer.sideA, (bezier_edge)layer.sideB, layer.materialIndex, points[i].left_Geom_Idx[layer.bezierIndex], true, layer.offsetA, layer.offsetB, isBridge, isBridge));
                }
            }

            // sidewalk
            material = __min(maxMaterial, points[i].matLeft[splinePoint::matName::sidewalk]);
            if (material >= 0)
            {
                for (auto& layer : roadMaterialCache::getInstance().materialVector[material].layers)
                {
                    _index.push_back(bezierLayer((bezier_edge)layer.sideA, (bezier_edge)layer.sideB, layer.materialIndex, points[i].left_Geom_Idx[layer.bezierIndex], false, layer.offsetA, layer.offsetB, isBridge, isBridge));
                }
            }

            // gutter
            material = __min(maxMaterial, points[i].matLeft[splinePoint::matName::gutter]);
            if (material >= 0)
            {
                for (auto& layer : roadMaterialCache::getInstance().materialVector[material].layers)
                {
                    _index.push_back(bezierLayer((bezier_edge)layer.sideA, (bezier_edge)layer.sideB, layer.materialIndex, points[i].left_Geom_Idx[layer.bezierIndex], false, layer.offsetA, layer.offsetB, isBridge, isBridge));
                }
            }

            // lanes paint
            for (int laneNr = innerShoulder; laneNr <= outerShoulder; laneNr++)
            {
                bezier_edge side = bezier_edge::outside;
                int laneToDisplay = laneNr;

                if (leftLaneInUse[laneNr])
                {
                    // DAMN DISMAL MAPPING HERE - not sure this maps perfectly
                    switch (laneNr)
                    {
                    case innerShoulder:	material = __min(maxMaterial, points[i].matLeft[splinePoint::matName::innerShoulder]); break;
                    case innerTurn:		material = __min(maxMaterial, points[i].matLeft[splinePoint::matName::innerTurn]); side = bezier_edge::center; laneToDisplay = lane1;	 break;
                    case lane1:			material = __min(maxMaterial, points[i].matLeft[splinePoint::matName::lane1]); break;
                    case lane2:			material = __min(maxMaterial, points[i].matLeft[splinePoint::matName::lane2]); break;
                    case lane3:			material = __min(maxMaterial, points[i].matLeft[splinePoint::matName::lane2]); break;
                    case outerTurn:		material = __min(maxMaterial, points[i].matLeft[splinePoint::matName::outerTurn]);		laneToDisplay = laneNr;	 break;
                    case outerShoulder: material = __min(maxMaterial, points[i].matLeft[splinePoint::matName::shoulder]);		side = bezier_edge::center; break;
                    }

                    if (material >= 0)
                    {
                        for (auto& layer : roadMaterialCache::getInstance().materialVector[material].layers)
                        {
                            // FIXME I think sides should be fixed here
                            _index.push_back(bezierLayer(side, side, layer.materialIndex, points[i].left_Lane_Idx[laneToDisplay], true, layer.offsetA, layer.offsetB, isBridge, isBridge));
                        }
                    }
                }
            }

            // lanes wear & tear
            for (int laneNr = innerTurn; laneNr <= outerTurn; laneNr++)
            {
                if (leftLaneInUse[laneNr])
                {
                    // DAMN DISMAL MAPPING HERE - not sure this maps perfectly
                    switch (laneNr)
                    {
                    case innerTurn:		material = __min(maxMaterial, points[i].matLeft[splinePoint::matName::rubberInner]); break;
                    case lane1:			material = __min(maxMaterial, points[i].matLeft[splinePoint::matName::rubberLane1]); break;
                    case lane2:			material = __min(maxMaterial, points[i].matLeft[splinePoint::matName::rubberLane2]); break;
                    case lane3:			material = __min(maxMaterial, points[i].matLeft[splinePoint::matName::rubberLane3]); break;
                    case outerTurn:		material = __min(maxMaterial, points[i].matLeft[splinePoint::matName::rubberOuter]); break;
                    }

                    if (material >= 0)
                    {
                        for (auto& layer : roadMaterialCache::getInstance().materialVector[material].layers)
                        {
                            _index.push_back(bezierLayer((bezier_edge)layer.sideA, (bezier_edge)layer.sideB, layer.materialIndex, points[i].left_Lane_Idx[laneNr], true, layer.offsetA, layer.offsetB, isBridge, isBridge));
                        }
                    }
                }
            }
        }





        // Centerline layers ########################################################################################################
        // road
        for (uint i = 0; i < numSegments; i++)
        {
            bool isBridge = points[i].isBridge;


            // inner shoulder 
            if (rightLaneInUse[innerShoulder])
            {
                material = __min(maxMaterial, points[i].matCenter[1]);
                if (material >= 0)
                {
                    for (auto& layer : roadMaterialCache::getInstance().materialVector[material].layers)
                    {
                        _index.push_back(bezierLayer((bezier_edge)layer.sideA, (bezier_edge)layer.sideB, layer.materialIndex, points[i].right_Lane_Idx[innerShoulder], false, layer.offsetA, layer.offsetB, isBridge, isBridge));
                    }
                }
            }
            if (leftLaneInUse[innerShoulder])
            {
                material = __min(maxMaterial, points[i].matCenter[1]);
                if (material >= 0)
                {
                    for (auto& layer : roadMaterialCache::getInstance().materialVector[material].layers)
                    {
                        _index.push_back(bezierLayer((bezier_edge)layer.sideA, (bezier_edge)layer.sideB, layer.materialIndex, points[i].left_Lane_Idx[innerShoulder], false, layer.offsetA, layer.offsetB, isBridge, isBridge));
                    }
                }
            }

            material = __min(maxMaterial, points[i].matCenter[0]);
            uint materialStripes = __min(maxMaterial, points[i].matCenter[1]);
            if (material >= 0 && (materialStripes >= 0))
            {
                for (auto& layer : roadMaterialCache::getInstance().materialVector[material].layers)
                {
                    _index.push_back(bezierLayer((bezier_edge)layer.sideA, (bezier_edge)layer.sideB, layer.materialIndex, points[i].left_Geom_Idx[layer.bezierIndex], true, layer.offsetA, layer.offsetB, isBridge, isBridge));
                }
            }
        }

        // Curvature side layers ########################################################################################################
        //_index.push_back(bezierLayer(bezier_edge::center, bezier_edge::outside, true, 1000, points[i].right_Geom_Idx[0], 0.0f, 0.0f, false, false));
        if (!_stylized) {
            for (uint i = 0; i < numSegments; i++)
            {
                bool isBridge = points[i].isBridge;
                _index.push_back(bezierLayer(bezier_edge::center, bezier_edge::outside, MATERIAL_CURVATURE, points[i].right_Geom_Idx[0], false, 0, 0, isBridge, isBridge));
                _index.push_back(bezierLayer(bezier_edge::center, bezier_edge::outside, MATERIAL_CURVATURE, points[i].left_Geom_Idx[0], true, 0, 0, isBridge, isBridge));
            }
        }

    }


    // stylized
    if (_stylized) {
        for (uint i = 0; i < numSegments; i++)
        {
            // SELECTION

            if (i >= _from && i < _to)
            {
                _index.push_back(bezierLayer(bezier_edge::outside, bezier_edge::outside, MATERIAL_EDIT_SELECT, points[i].right_Geom_Idx[0], false, 1.0f, 2.0f));
                _index.push_back(bezierLayer(bezier_edge::outside, bezier_edge::outside, MATERIAL_EDIT_SELECT, points[i].left_Geom_Idx[0], false, 1.0f, 2.0f));
            }

            if (!_showMaterials)
            {

                // middle
                uint feedback = points[i].right_Geom_Idx[0];
                _index.push_back(bezierLayer(bezier_edge::center, bezier_edge::center, MATERIAL_EDIT_WHITEDOT, points[i].right_Geom_Idx[0], false, -0.05f, 0.05f));
                // edge
                _index.push_back(bezierLayer(bezier_edge::outside, bezier_edge::outside, MATERIAL_EDIT_WHITEDOT, points[i].right_Geom_Idx[0], false, -0.05f, 0.05f));
                _index.push_back(bezierLayer(bezier_edge::outside, bezier_edge::outside, MATERIAL_EDIT_WHITEDOT, points[i].left_Geom_Idx[0], true, -0.05f, 0.05f));
                // sidewalk
                float sideR = points[i].lanesRight[side].laneWidth;
                float sideL = points[i].lanesLeft[side].laneWidth;
                _index.push_back(bezierLayer(bezier_edge::center, bezier_edge::center, MATERIAL_EDIT_REDDOT, points[i].right_Geom_Idx[1], false, sideR - 0.1f, sideR + 0.1f));
                _index.push_back(bezierLayer(bezier_edge::center, bezier_edge::center, MATERIAL_EDIT_REDDOT, points[i].left_Geom_Idx[1], true, sideL - 0.1f, sideL + 0.1f));
                // faredge
                _index.push_back(bezierLayer(bezier_edge::outside, bezier_edge::outside, MATERIAL_EDIT_REDDOT, points[i].right_Geom_Idx[1], false, -0.1f, 0.1f));
                _index.push_back(bezierLayer(bezier_edge::outside, bezier_edge::outside, MATERIAL_EDIT_REDDOT, points[i].left_Geom_Idx[1], true, -0.1f, 0.1f));

                for (int laneNr = innerTurn; laneNr <= outerTurn; laneNr++)
                {
                    if (rightLaneInUse[laneNr])
                    {
                        _index.push_back(bezierLayer(bezier_edge::center, bezier_edge::outside, MATERIAL_EDIT_BLUE, points[i].right_Lane_Idx[laneNr], false, 0, 0));
                    }
                    if (leftLaneInUse[laneNr])
                    {
                        _index.push_back(bezierLayer(bezier_edge::center, bezier_edge::outside, MATERIAL_EDIT_GREEN, points[i].left_Lane_Idx[laneNr], true, 0, 0));
                    }
                }
            }
        }
    }

}


// Things like tangents, will allow 100m long for now, refine later
void roadSection::solveStart()
{

    if (int_GUID_start >= 0)
    {
        //intersectionRoadLink* link = static_global_intersectionList->at(int_GUID_start).findLink(GUID);
        if (startLink) {

            // tests
            if (!isSaneVector(startLink->cornerTangent_A)) {
                bool bCM = true;
            }
            if (!isSaneVector(startLink->tangentVector)) {
                bool bCM = true;
            }
            if (!isSaneVector(startLink->cornerTangent_B)) {
                bool bCM = true;
            }

            points[0].bezier[left].pos = startLink->corner_A;
            points[0].bezier[left].tangentForward = glm::normalize(startLink->cornerTangent_A);
            points[0].bezier[left].tangentBack = points[0].bezier[left].tangentForward;
            points[0].bezier[left].dst_Forward = glm::length(startLink->cornerTangent_A);

            points[0].bezier[middle].tangentForward = glm::normalize(startLink->tangentVector);
            points[0].bezier[middle].tangentBack = points[0].bezier[middle].tangentForward;
            points[0].bezier[middle].dst_Back = 0;
            points[0].bezier[middle].dst_Forward = glm::length(startLink->tangentVector) * 2;

            points[0].bezier[right].pos = startLink->corner_B;
            points[0].bezier[right].tangentForward = glm::normalize(startLink->cornerTangent_B);
            points[0].bezier[right].tangentBack = points[0].bezier[right].tangentForward;
            points[0].bezier[right].dst_Forward = glm::length(startLink->cornerTangent_B);
        }

        // and adjust the lanes, widths etc
        for (int i = 0; i < 9; i++)
        {
            points[0].lanesLeft[i].laneWidth = points[1].lanesLeft[i].laneWidth;
            points[0].lanesRight[i].laneWidth = points[1].lanesRight[i].laneWidth;

            /*
            float2 P = points[0].lanesLeft[i].percentage;
            if (startLink->roadPtr->points.size() > 0) {
                if (startLink->broadStart) {
                    P = (points[0].lanesLeft[i].percentage + startLink->roadPtr->points.front().lanesRight[i].percentage) * 0.5f;
                }
                else {
                    P = (points[0].lanesLeft[i].percentage + startLink->roadPtr->points.back().lanesLeft[i].percentage) * 0.5f;
                }
            }
            points[0].lanesLeft[i].percentage = P;
            */
        }
    }

}



void roadSection::solveEnd()
{
    // snap for closed roads
    if (isClosedLoop && (points.size() > 4)) {
        points.back() = points.front();
    }

    if (int_GUID_end >= 0)
    {
        //intersectionRoadLink* link = static_global_intersectionList->at(int_GUID_end).findLink(GUID);
        if (endLink) {

            // tests
            if (!isSaneVector(endLink->cornerTangent_A)) {
                bool bCM = true;
            }
            if (!isSaneVector(endLink->tangentVector)) {
                bool bCM = true;
            }
            if (!isSaneVector(endLink->cornerTangent_B)) {
                bool bCM = true;
            }

            int idx = (int)points.size() - 1;

            points[idx].bezier[left].pos = endLink->corner_B;
            points[idx].bezier[left].tangentForward = -glm::normalize(endLink->cornerTangent_B);
            points[idx].bezier[left].tangentBack = points[idx].bezier[left].tangentForward;
            points[idx].bezier[left].dst_Back = glm::length(endLink->cornerTangent_B);

            points[idx].bezier[middle].tangentForward = -glm::normalize(endLink->tangentVector);
            points[idx].bezier[middle].tangentBack = points[idx].bezier[middle].tangentForward;
            points[idx].bezier[middle].dst_Back = glm::length(endLink->tangentVector) * 2; // FIXME 2 may not be perfect

            points[idx].bezier[right].pos = endLink->corner_A;
            points[idx].bezier[right].tangentForward = -glm::normalize(endLink->cornerTangent_A);
            points[idx].bezier[right].tangentBack = points[idx].bezier[right].tangentForward;
            points[idx].bezier[right].dst_Back = glm::length(endLink->cornerTangent_A);

            // and adjust the lanes, widths etc
            for (int i = 0; i < 9; i++)
            {
                points[idx].lanesLeft[i].laneWidth = points[idx - 1].lanesLeft[i].laneWidth;
                points[idx].lanesRight[i].laneWidth = points[idx - 1].lanesRight[i].laneWidth;

                /*
                float2 P = points[idx].lanesLeft[i].percentage;
                if (endLink->roadPtr->points.size() > 0) {
                    if (endLink->broadStart) {
                        P = (points[idx].lanesLeft[i].percentage + endLink->roadPtr->points.front().lanesLeft[i].percentage) * 0.5f;
                    }
                    else {
                        P = (points[idx].lanesLeft[i].percentage + endLink->roadPtr->points.back().lanesRight[i].percentage) * 0.5f;
                    }
                }
                */


                //points[idx].lanesLeft[i].percentage = P;
            }
        }
    }
}



void roadSection::solvePercentages()
{

}


void roadSection::saveType(int index)
{
    std::filesystem::path path;
    FileDialogFilterVec filters = { {"roadType"} };
    if (saveFileDialog(filters, path))
    {
        std::ofstream os(path, std::ios::binary);
        cereal::BinaryOutputArchive archive(os);

        points[index].serializeType(archive);
    }
}



void roadSection::loadSelected(int index)
{
    std::filesystem::path path;
    FileDialogFilterVec filters = { {"roadType"} };
    if (openFileDialog(filters, path))
    {
        std::ifstream is(path, std::ios::binary);
        cereal::BinaryInputArchive archive(is);

        points[index].serializeType(archive);
        points[index].solveMiddlePos();
        lastEditedPoint = points[index];

        solveRoad();
    }
}



void roadSection::loadCompleteRoad()
{
    std::filesystem::path path;
    FileDialogFilterVec filters = { {"roadType"} };
    if (openFileDialog(filters, path))
    {
        std::ifstream is(path, std::ios::binary);
        cereal::BinaryInputArchive archive(is);

        splinePoint loadPnt;
        loadPnt.serializeType(archive);
        lastEditedPoint = loadPnt;

        for (auto& pnt : points) {
            pnt.centerlineType = loadPnt.centerlineType;
            pnt.lanesLeft = loadPnt.lanesLeft;
            pnt.lanesRight = loadPnt.lanesRight;
            pnt.solveMiddlePos();
        }

        solveRoad();
    }
}




/*	This works ok'ish
    could do with a special case when the roads become pretty straight, I suspect pullForward and pullBack becomes tiny

    clould very well try to add pull towards the next and previous points as well. As long as this was well balanced
    with the sideways pull it could conteract this effectively
    */
void roadSection::optimizeTangents(int lane)
{
    std::vector<splinePoint>::iterator pnt;
    std::vector<splinePoint>::iterator start = points.begin();
    if ((int_GUID_start >= 0)) start++;
    std::vector<splinePoint>::iterator end = points.end();
    if ((int_GUID_end >= 0)) end--;


    for (pnt = start; pnt != end; pnt++)
    {
        float continuity = pnt->C;
        if (lane == 1) continuity = 1.0f; //???

        bezierPoint* pntThis = &pnt->bezier[lane];
        // tests
        if (!isSaneVector(pntThis->pullBack)) {
            bool bCM = true;
            pntThis->pullBack = float3(0, 0, 0);
        }
        if (!isSaneVector(pntThis->pullForward)) {
            bool bCM = true;
            pntThis->pullForward = float3(0, 0, 0);
        }
        if (!isSaneVector(pntThis->tangentForward)) {
            bool bCM = true;
            pntThis->tangentForward = glm::normalize(pnt->anchor - end->anchor);
        }
        if (!isSaneVector(pntThis->tangentBack)) {
            bool bCM = true;
            pntThis->tangentBack = glm::normalize(pnt->anchor - end->anchor);
        }

        float3 pull = pntThis->pullBack - pntThis->pullForward;
        float3 pullForward = lerp(-pntThis->pullForward, pull, continuity);
        float3 pullBack = lerp(pntThis->pullBack, pull, continuity);

        pntThis->tangentForward = glm::normalize(pntThis->tangentForward - pullForward);
        pntThis->tangentBack = glm::normalize(pntThis->tangentBack - pullBack);
    }
}



/*	This seems to work pretty well
    it tries to optimize the lengths so that the individual steps inside the bezier spline is
    as evenly spaced as possible. In a way that spacing is one indication of energy in the spline

    I think I need to rewqrite this to optimize for curvature instead
    */
void roadSection::optimizeSpacing(int lane)
{
    std::vector<splinePoint>::iterator pnt, next;
    bezierPoint* pntThis, * pntNext;

    for (pnt = points.begin(), next = ++points.begin(); next != points.end(); pnt++, next++)
    {
        pntThis = &pnt->bezier[lane];
        pntNext = &next->bezier[lane];

        float straightLength = glm::length(pntThis->pos - pntNext->pos);
        float min = straightLength * 0.1f;
        float max = __min(pntThis->length * 0.5f, straightLength * 0.7f);

        float da = glm::length(del_cubic_Casteljau(0.00f, 0.10f, pntThis, pntNext));
        float dm = glm::length(del_cubic_Casteljau(0.45f, 0.55f, pntThis, pntNext));
        float db = glm::length(del_cubic_Casteljau(0.90f, 1.00f, pntThis, pntNext));

        pntThis->dst_Forward *= 1.0f + (dm - da) / (dm + da);
        pntNext->dst_Back *= 1.0f + (dm - db) / (dm + db);

        if (isnan(pntThis->dst_Forward))		pntThis->dst_Forward = 5.0f;
        if (isnan(pntNext->dst_Back))
        {
            pntNext->dst_Back = 5.0f;
        }
        //glm::clamp(pntThis->dst_Front, min, max);
        //glm::clamp(pntNext->dst_Back, min, max);
    }
}



void roadSection::solveUV(int lane)
{
    float L = points.back().bezier[middle].totalLength;
    float repeats = points.back().bezier[middle].totalLength / uvScale;		// all repeats come from the middle, so same amount left middle and right
    if (isIntergerUV)
    {
        repeats = ceil(repeats);
        repeats = __max(1.0f, repeats);
    }
    
    float rDistance = points.back().bezier[lane].totalLength / repeats;
    rDistance = __max(1.0f, rDistance);

    if (isnan(rDistance))
    {
        bool bCM = true;
    }
    float startLength = 0.0f;
    float dU = 0;
    for (auto& pnt : points)
    {
        // use the old dU for u and u_back
        pnt.bezier[lane].u = startLength / rDistance;
        if (isnan(pnt.bezier[lane].u))
        {
            bool bCM = true;
        }

        pnt.bezier[lane].u_back = pnt.bezier[lane].u - dU * 0.3333f;

        // calculate new dU, use that for u_forward
        dU = pnt.bezier[lane].length / rDistance;
        pnt.bezier[lane].u_forward = pnt.bezier[lane].u + dU * 0.3333f;

        startLength += pnt.bezier[lane].length;
    }
}



/*	Solves the width amd calculate new positions for the left and right points*/
void roadSection::solveWidthFromLanes()
{
    for (int i = 0; i < 9; i++)
    {
        leftLaneInUse[i] = false;
        rightLaneInUse[i] = false;
    }

    for (auto& pnt : points)
    {
        pnt.widthLeft = 0.01f;	// cant be zero, in case we eliminate a lane, so add 1 cm
        pnt.widthRight = 0.01f;


        for (int i = 0; i < 7; i++)	// first 7 lanes take me to the edge of the tar
        {
            pnt.widthLeft += pnt.lanesLeft[i].laneWidth;
            pnt.widthRight += pnt.lanesRight[i].laneWidth;
        }

        float wLanesLeft = 0.0f;
        float wLanesRight = 0.0f;
        for (int i = 1; i < 4; i++)	// first 7 lanes take me to the edge of the tar
        {
            wLanesLeft += pnt.lanesLeft[i].laneWidth;
            wLanesRight += pnt.lanesRight[i].laneWidth;
        }
        if ((wLanesLeft == 0.0f) || (wLanesRight == 0.0f)) {
            // we ahve a one way section
            pnt.perpendicular_Offset = (wLanesRight - wLanesLeft) / 2.0f;
            pnt.solveMiddlePos();
        }

        glm::vec3 tang = pnt.bezier[middle].tangentForward;
        float3 perp = glm::normalize(glm::cross(pnt.anchorNormal, pnt.bezier[middle].tangentForward));	// FIXME AVERAGE of tangents - BUT MIDDLE SHOULD ALWUS be ok
        switch (pnt.constraint) {
        case e_constraint::none:
            pnt.camber = perp.y;
            pnt.perpendicular = perp;
            break;
        case e_constraint::camber:
        {
            float a = sqrt((1.0f - pnt.camber * pnt.camber) / (perp.x * perp.x + perp.z * perp.z));
            pnt.perpendicular.x = a * perp.x;
            pnt.perpendicular.y = pnt.camber;
            pnt.perpendicular.z = a * perp.z;
        }
        break;
        case e_constraint::fixed:
            break;
        }


        if (pnt.bAutoGenerate) {
            pnt.bezier[right].pos = pnt.bezier[middle].pos - (pnt.perpendicular * pnt.widthRight);
            pnt.bezier[left].pos = pnt.bezier[middle].pos + (pnt.perpendicular * pnt.widthLeft);
        }

        // and now lanes from width ;-)
        float sumLeft = 0;
        float sumRight = 0;
        float actualWidthLeft = glm::length(pnt.bezier[left].pos - pnt.bezier[middle].pos);
        float actualWidthRight = glm::length(pnt.bezier[right].pos - pnt.bezier[middle].pos);
        float scale_L = pnt.widthLeft / actualWidthLeft;
        float offset_L = 1.0f - scale_L;
        float scale_R = pnt.widthRight / actualWidthRight;
        float offset_R = 1.0f - scale_R;

        for (int i = 0; i < 7; i++)	// first 7 lanes take me to the edge of the tar
        {
            if (pnt.lanesLeft[i].laneWidth > 0)
            {
                leftLaneInUse[i] = true;
            }
            if (pnt.lanesRight[i].laneWidth > 0)
            {
                rightLaneInUse[i] = true;
            }

            pnt.lanesLeft[i].percentage.x = (sumLeft / pnt.widthLeft);
            pnt.lanesRight[i].percentage.x = (sumRight / pnt.widthRight);
            if (i > 1) {
                pnt.lanesLeft[i].percentage.x = offset_L + scale_L * (sumLeft / pnt.widthLeft);
                pnt.lanesRight[i].percentage.x = offset_R + scale_R * (sumRight / pnt.widthRight);
            }
            sumLeft += pnt.lanesLeft[i].laneWidth;
            sumRight += pnt.lanesRight[i].laneWidth;

            pnt.lanesLeft[i].percentage.y = (sumLeft / pnt.widthLeft);
            pnt.lanesRight[i].percentage.y = (sumRight / pnt.widthRight);
            if (i > 1) {
                pnt.lanesLeft[i].percentage.y = offset_L + scale_L * (sumLeft / pnt.widthLeft);
                pnt.lanesRight[i].percentage.y = offset_R + scale_R * (sumRight / pnt.widthRight);
            }
        }

        // and now for the exceptions
        // inner turn lane ----------------------------------------------------------------------------------------------
        if (pnt.lanesLeft[innerTurn].laneWidth == 0) pnt.lanesLeft[innerTurn].percentage.y = pnt.lanesLeft[lane1].percentage.y;
        if (pnt.lanesRight[innerTurn].laneWidth == 0) pnt.lanesRight[innerTurn].percentage.y = pnt.lanesRight[lane1].percentage.y;

        // outer turn ---------------------------------------------------------------------------------------------------
        int lastLaneLeft = lane1;
        int lastLaneRight = lane1;
        for (int i = lane1; i <= lane3; i++)
        {
            if (pnt.lanesLeft[i].laneWidth > 0) lastLaneLeft = i;
            if (pnt.lanesRight[i].laneWidth > 0) lastLaneRight = i;
        }

        if (pnt.lanesLeft[outerTurn].laneWidth == 0) pnt.lanesLeft[outerTurn].percentage.x = pnt.lanesLeft[lastLaneLeft].percentage.x;
        if (pnt.lanesRight[outerTurn].laneWidth == 0) pnt.lanesRight[outerTurn].percentage.x = pnt.lanesRight[lastLaneRight].percentage.x;
    }
}



void roadSection::solveEnergyAndLength(int lane, int _min, int _max)
{
    float3 bezierPoints[65];
    std::vector<splinePoint>::iterator pnt, next;
    bezierPoint* pntThis, * pntNext;

    int index = 0;
    float inspectLength;
    for (pnt = points.begin(), next = ++points.begin(); next != points.end(); pnt++, next++, index++)
    {
        pntThis = &pnt->bezier[lane];
        pntNext = &next->bezier[lane];

        if ((index >= _min) && (index <= _max))
        {


            // solve bezier ----------------------------------------------------------------------------
            for (int i = 0; i < 65; i++)
            {
                bezierPoints[i] = cubic_Casteljau((float)i / 64.0f, pntThis, pntNext);
            }

            // calculate lengths -----------------------------------------------------------------------
            float dL = 0;
            for (int i = 0; i < 64; i++)
            {
                dL += glm::length(bezierPoints[i] - bezierPoints[i + 1]);
            }
            pntThis->length = dL;
            //pntThis->totalLength += dL;
            //pntNext->totalLength = pntThis->totalLength;

            // calculate the PULL ---------------------------------------------------------------------
            float3 pF = float3(0, 0, 0);
            float3 pB = float3(0, 0, 0);
            uint step = 1;
            for (uint i = 0; i < 32 - 2 * step; i += step)
            {
                pF += glm::cross((bezierPoints[i + step] - bezierPoints[i]), (bezierPoints[i + 2 * step] - bezierPoints[i + step]));
                pB += glm::cross((bezierPoints[64 - step - i] - bezierPoints[64 - 2 * step - i]), (bezierPoints[64 - i] - bezierPoints[64 - step - i]));
            }
            if (!isSaneVector(pF)) {
                bool bCM = true;
            }
            if (!isSaneVector(pB)) {
                bool bCM = true;
            }
            pntThis->pullForward = glm::cross(pF, pntThis->tangentForward);
            pntNext->pullBack = glm::cross(pB, pntNext->tangentBack);

            //pntThis->pullForward.y *= 0.0f;
            //pntThis->pullBack.y *= 0.0f;

            // long straight section stabilization

            // DO LEGHTH  FROM MIDDLE LANE ALWAYS FOR width conservation
            float lengthMiddle = glm::length((next->bezier[1].pos - pnt->bezier[1].pos));

            float3 dir = normalize(pntNext->pos - pntThis->pos) * lengthMiddle * 0.145f;		// HIERDIE DEEL WERK

            if ((pnt == points.begin()) || (next == --points.end())) {
                // start and end go parallel
                dir = normalize(next->bezier[1].pos - pnt->bezier[1].pos) * lengthMiddle * 0.145f;
            }

            //dir.y *= 1.1f;
            pntThis->pullForward += dir;
            pntNext->pullBack -= dir;

            //pntThis->pullForward.y += dir.y * 100;
            //pntThis->pullBack.y -= dir.y * 100;

            pntThis->pullForward.x *= pnt->B;
            pntThis->pullForward.y *= pnt->B;
            pntThis->pullForward.z *= pnt->B;

            pntNext->pullBack.x *= 1.0f - next->B;
            pntNext->pullBack.y *= 1.0f - next->B;
            pntNext->pullBack.z *= 1.0f - next->B;
        }


        if (isnan(pntThis->length)) {
            bool bCM = true;
            pntThis->length = 1;
        }

        pntThis->totalLength += pntThis->length;
        pntNext->totalLength = pntThis->totalLength;
        inspectLength = pntThis->totalLength;
    }

    if (inspectLength == 0) {
        int cnt = (int)points.size();
        bool bCM = true;
    }
}



void roadSection::solveRoad(int index)
{
    if (points.size() < 2) return;

    int min = 0;
    int max = (int)points.size();
    if (index > 0) {
        min = __max(0, index - 5);
        max = __min((int)points.size(), index + 5);
    }

    solveWidthFromLanes();
    solveStart();				// FIXME the intersection should just force this like the pushback
    solveEnd();

    /*
    for (int lane = 0; lane < 3; lane++)
    {
        points[0].bezier[lane].totalLength = 0;
        solveEnergyAndLength(lane, min, max);
        solveUV(lane);
        optimizeSpacing(lane);
        optimizeTangents(lane);
    }
    */

    points[0].bezier[1].totalLength = 0;
    solveEnergyAndLength(1, min, max);
    solveUV(1);
    optimizeSpacing(1);
    optimizeTangents(1);

    points[0].bezier[0].totalLength = 0;
    solveEnergyAndLength(0, min, max);
    solveUV(0);
    optimizeSpacing(0);
    optimizeTangents(0);

    points[0].bezier[2].totalLength = 0;
    solveEnergyAndLength(2, min, max);
    solveUV(2);
    optimizeSpacing(2);
    optimizeTangents(2);

}



void roadSection::setCenterline(uint type) {
    for (auto& pnt : points) {
        pnt.centerlineType = type;
    }
}



splinePoint roadSection::lastEditedPoint;
void roadSection::pushPoint(float3 p, float3 n, uint _lod)
{
    clearSelection();

    //??? Effort to see if new point shopuld always match the current end point
    float distance = 0;
    if (points.size()) {
        lastEditedPoint = points.back();
        distance = length(p - lastEditedPoint.anchor);
    }



    // so we have to be close - 100m, and it cannot be linked to an intersection already
    // would like direction as well
    if (distance < 100 && (int_GUID_end == -1))
    {
        splinePoint sP;// = lastEditedPoint;
        for (int i = 0; i < 9; i++) {
            sP.lanesLeft[i].laneWidth = lastEditedPoint.lanesLeft[i].laneWidth;
            sP.lanesRight[i].laneWidth = lastEditedPoint.lanesRight[i].laneWidth;
        }
        sP.centerlineType = lastEditedPoint.centerlineType;

        sP.curvature = lastEditedPoint.curvature;

        // materials
        for (int i = 0; i < 15; i++)
        {
            sP.matLeft[i] = lastEditedPoint.matLeft[i];
            sP.matRight[i] = lastEditedPoint.matRight[i];
        }
        for (int i = 0; i < 2; i++)
        {
            sP.matCenter[i] = lastEditedPoint.matCenter[i];
        }


        sP.height_AboveAnchor = lastEditedPoint.height_AboveAnchor;
        sP.tangent_Offset = lastEditedPoint.tangent_Offset;

        if (points.size()) {
            //		lastEditedPoint = points.back();

            float3 tangent = glm::normalize(p - (--points.end())->anchor);
            sP.bezier[left].tangentForward = tangent;
            sP.bezier[middle].tangentForward = tangent;
            sP.bezier[right].tangentForward = tangent;

            sP.bezier[left].tangentBack = tangent;
            sP.bezier[middle].tangentBack = tangent;
            sP.bezier[right].tangentBack = tangent;
        }
        else {
            sP.bAutoGenerate = true;
        }

        if (isBasicLinearMarking)
        {
            for (int i = 0; i < 9; i++) {
                sP.lanesLeft[i].laneWidth = 0.f;
                if (i != 2)
                {
                    sP.lanesRight[i].laneWidth = 0.f;
                }
            }
        }

        sP.B = 0.5f;
        sP.setAnchor(p, n, _lod);
        points.push_back(sP);
        solveRoad();
    }
}



void roadSection::insertPoint(int index, float3 p, float3 n, uint _lod) {
    clearSelection();

    std::vector<splinePoint>::iterator it = points.begin();
    for (int i = 0; i <= index; i++) {
        it++;
    }
    splinePoint sP = *it;
    sP.setAnchor(p, n, _lod);
    sP.selected = true;
    sP.bezier[left].dst_Back = 1.0f;
    sP.bezier[left].dst_Forward = 1.0f;
    sP.bezier[middle].dst_Back = 1.0f;
    sP.bezier[middle].dst_Forward = 1.0f;
    sP.bezier[right].dst_Back = 1.0f;
    sP.bezier[right].dst_Forward = 1.0f;
    sP.B = 0.5f;

    points.insert(it, sP);
    solveRoad();
}



void roadSection::deletePoint(int index) {
    if (points.size() > 3)
    {
        std::vector<splinePoint>::iterator it = points.begin();
        for (int i = 0; i < index; i++) {
            it++;
        }
        points.erase(it);
    }
}



float roadSection::getDistance() {

    float distance = 0;

    if (points.size() >= 2) {

        std::vector<splinePoint>::iterator it = points.begin();
        glm::vec3 P = it->anchor;

        for (int i = 1; i < points.size(); i++) {
            it++;
            distance += length(it->anchor - P);
            P = it->anchor;
        }
    }

    return distance;
}



bool roadSection::testAgainstPoint(splineTest* testdata, bool includeEnd) {
    // first points, and save closets 2
    int first = -1;
    int second = -1;
    //testdata->bVertex = false;
    //testdata->bSegment = false;
    bool bThisVertex = false;
    bool bThisSegment = false;

    //testdata->testDistance = 100000.0f;
    int offset = 0;
    if (!includeEnd) offset = 1;
    for (int i = offset; i < points.size() - offset; i++)
    {
        float3 offset = points[i].anchor - testdata->pos;
        offset.y = 0;	// test only in XZ plane
        float D = glm::length(offset);
        if (D < testdata->testDistance)
        {
            testdata->testDistance = D;
            second = first;
            first = i;
            if (D < 5.0f) {
                bThisVertex = true;
                testdata->returnPos = points[i].anchor;
                testdata->index = i;
            }
        }

    }

    if (!bThisVertex) {
        //testdata->testDistance = 100000.0f;
        // now, are we between points
        // recalculate this SIOMPLY FROM FIRST IT CAN BE EITHER SIDE
        for (int i = offset; i < points.size() - 1 - offset; i++)
        {
            float3 A = points[i].anchor;
            float3 B = points[i + 1].anchor;
            float3 dir = glm::normalize(B - A);
            float D = glm::length(B - A);
            float3 Pa = testdata->pos - A;
            float Dl = glm::dot(Pa, dir);
            float dMid = glm::length(testdata->pos - (A + B) * 0.5f);
            float dPerp = glm::length(Pa - dir * Dl);

            if ((Dl < 0) && (i == 0)) {
                // before
            }

            if ((Dl > D) && (i == points.size() - 2)) {
                // after
            }

            if ((Dl >= 0) && (Dl <= D) && (dPerp < testdata->testDistance) && (dPerp < (points[i].widthLeft + points[i].widthRight))) {
                testdata->testDistance = dPerp;
                if (dPerp < 30) {
                    bThisSegment = true;
                    testdata->returnPos = A + dir * Dl;
                    testdata->index = i;
                }
            }

        }
    }


    if (bThisVertex || bThisSegment) {
        testdata->bVertex = bThisVertex;
        testdata->bSegment = bThisSegment;
        return true;
    }

    return false;
}
/*
vec3 bezier(vec3 P0, vec3 P1, vec3 P2, vec3 P3, float t) {
    float tt = 1.0f - t;
    return tt*tt*tt*P0 + 3 * tt*tt*t*P1 + 3 * tt*t*t*P2 + t*t*t*P3;
}
*/
void roadSection::findUV(glm::vec3 p, splineUV* uv) {

    std::vector<splinePoint>::iterator it = points.begin();
    std::vector<splinePoint>::iterator it2;
    for (it = points.begin(); it != points.end(); it++) {
        it2 = it;
        it2++;
        if (it2 != points.end()) {
            findUVTile(p, it->bezier[left].pos, it->bezier[middle].pos, it2->bezier[middle].pos, it2->bezier[left].pos, it->bezier[left].forward(), it->bezier[middle].forward(), it2->bezier[middle].backward(), it2->bezier[left].backward(), uv);
            findUVTile(p, it->bezier[middle].pos, it->bezier[right].pos, it2->bezier[right].pos, it2->bezier[middle].pos, it->bezier[middle].forward(), it->bezier[right].forward(), it2->bezier[right].backward(), it2->bezier[middle].backward(), uv);
        }
    }
}

void roadSection::findUVTile(glm::vec3 p, glm::vec3 c1, glm::vec3 c2, glm::vec3 c3, glm::vec3 c4, glm::vec3 b1, glm::vec3 b2, glm::vec3 b3, glm::vec3 b4, splineUV* uv) {
    glm::vec3 N1 = glm::cross(glm::vec3(0, 1, 0), glm::normalize(c1 - c2));
    float d1 = glm::dot(p - c1, N1);
    glm::vec3 N2 = glm::cross(glm::vec3(0, 1, 0), glm::normalize(c3 - c4));
    float d2 = glm::dot(p - c3, N2);



    if ((d1 >= 0) && (d2 >= 0))
    {
        float U0 = d1 / (d1 + d2);

        glm::vec3 E1 = cubic_Casteljau(U0, c1, b1, b4, c4);
        glm::vec3 E2 = cubic_Casteljau(U0, c2, b2, b3, c3);
        glm::vec3 Ne = glm::cross(glm::vec3(0, 1, 0), glm::normalize(E2 - E1));
        glm::vec3 De = glm::normalize(E2 - E1);
        float V0 = glm::dot(p - E1, De) / glm::length(E2 - E1);

        glm::vec3 P0 = E1 + (E2 - E1) * V0;


        if ((V0 >= 0) && (V0 <= 1)) {
            uv->pos = P0;
            uv->U = U0;
            uv->V = V0;
            uv->E1 = E1;
            uv->E2 = E2;
        }
    }
}
