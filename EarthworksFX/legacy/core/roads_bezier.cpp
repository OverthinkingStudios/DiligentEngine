

#include"roads_bezier.h"



cubicDouble::cubicDouble(splinePoint a, splinePoint b)
{
    data[0][0] = a.bezier[left].pos_uv();
    data[0][1] = a.bezier[left].forward_uv();
    data[0][2] = b.bezier[left].backward_uv();
    data[0][3] = b.bezier[left].pos_uv();

    data[1][0] = a.bezier[right].pos_uv();
    data[1][1] = a.bezier[right].forward_uv();
    data[1][2] = b.bezier[right].backward_uv();
    data[1][3] = b.bezier[right].pos_uv();
}



cubicDouble::cubicDouble(splinePoint a, splinePoint b, bool bRight)
{

    if (bRight)
    {
        data[0][0] = a.bezier[middle].pos_uv();
        data[0][1] = a.bezier[middle].forward_uv();
        data[0][2] = b.bezier[middle].backward_uv();
        data[0][3] = b.bezier[middle].pos_uv();

        data[1][0] = a.bezier[right].pos_uv();
        data[1][1] = a.bezier[right].forward_uv();
        data[1][2] = b.bezier[right].backward_uv();
        data[1][3] = b.bezier[right].pos_uv();
    }
    // left hand side, add backwards
    else
    {
        data[0][0] = a.bezier[middle].pos_uv();
        data[0][1] = a.bezier[middle].backward_uv();
        data[0][2] = b.bezier[middle].forward_uv();
        data[0][3] = b.bezier[middle].pos_uv();

        data[1][0] = a.bezier[left].pos_uv();
        data[1][1] = a.bezier[left].backward_uv();
        data[1][2] = b.bezier[left].forward_uv();
        data[1][3] = b.bezier[left].pos_uv();
    }

}

/*	Special version for the outside edge
    */
cubicDouble::cubicDouble(splinePoint a, splinePoint b, bool bRight, float w0, float w1)
{
    if (bRight) {
        data[0][0] = a.bezier[right].pos_uv();
        data[0][1] = a.bezier[right].forward_uv();
        data[0][2] = b.bezier[right].backward_uv();
        data[0][3] = b.bezier[right].pos_uv();

        float3 up1 = a.anchorNormal;
        float3 up2 = b.anchorNormal;
        float3 tangent1 = a.bezier[right].forward_uv() - a.bezier[right].pos_uv();
        float3 tangent2 = b.bezier[right].pos_uv() - b.bezier[right].backward_uv();
        float distance = glm::length(b.bezier[right].pos_uv() - a.bezier[right].pos_uv());
        float3 perp1 = -glm::normalize(glm::cross(up1, tangent1));
        float3 perp2 = -glm::normalize(glm::cross(up2, tangent2));

        data[1][0] = a.bezier[right].pos_uv();
        data[1][3] = b.bezier[right].pos_uv();

        data[1][0] += float4(perp1 * w0, 0);
        data[1][3] += float4(perp2 * w1, 0);

        float distance2 = glm::length(data[1][3] - data[1][0]);
        float scale = distance2 / distance;

        data[1][1] = data[1][0] + float4(tangent1 * scale, 0);
        data[1][2] = data[1][3] - float4(tangent2 * scale, 0);

    }
    else
    {
        data[0][0] = a.bezier[left].pos_uv();
        data[0][1] = a.bezier[left].backward_uv();
        data[0][2] = b.bezier[left].forward_uv();
        data[0][3] = b.bezier[left].pos_uv();

        float3 up1 = a.anchorNormal;
        float3 up2 = b.anchorNormal;
        float3 tangent1 = a.bezier[left].backward_uv() - a.bezier[left].pos_uv();
        float3 tangent2 = b.bezier[left].pos_uv() - b.bezier[left].forward_uv();
        float distance = glm::length(b.bezier[left].pos_uv() - a.bezier[left].pos_uv());
        float3 perp1 = -glm::normalize(glm::cross(up1, tangent1));
        float3 perp2 = -glm::normalize(glm::cross(up2, tangent2));

        data[1][0] = a.bezier[left].pos_uv();
        data[1][3] = b.bezier[left].pos_uv();

        data[1][0] += float4(perp1 * w0, 0);
        data[1][3] += float4(perp2 * w1, 0);

        float distance2 = glm::length(data[1][3] - data[1][0]);
        float scale = distance2 / distance;

        data[1][1] = data[1][0] + float4(tangent1 * scale, 0);
        data[1][2] = data[1][3] - float4(tangent2 * scale, 0);
    }
}



cubicDouble::cubicDouble(splinePoint a, splinePoint b, bool bRight, float2 A, float2 D)
{
    float2 B = A;// glm::lerp(A, D, 0.333f);
    float2 C = D;// glm::lerp(A, D, 0.666f);

    // fixme lerp a en b ook
    if (bRight) {
        float4 fwd = a.bezier[right].forward_uv() - a.bezier[right].pos_uv() + a.bezier[middle].pos_uv();
        float4 bck = b.bezier[right].backward_uv() - b.bezier[right].pos_uv() + b.bezier[middle].pos_uv();

        data[0][0] = glm::lerp(a.bezier[middle].pos_uv(), a.bezier[right].pos_uv(), A.x);				// remember to pack UVs here
        data[0][1] = glm::lerp(fwd, a.bezier[right].forward_uv(), B.x);
        data[0][2] = glm::lerp(bck, b.bezier[right].backward_uv(), C.x);
        data[0][3] = glm::lerp(b.bezier[middle].pos_uv(), b.bezier[right].pos_uv(), D.x);

        // tanget


        data[1][0] = glm::lerp(a.bezier[middle].pos_uv(), a.bezier[right].pos_uv(), A.y);
        data[1][1] = glm::lerp(fwd, a.bezier[right].forward_uv(), B.y);
        data[1][2] = glm::lerp(bck, b.bezier[right].backward_uv(), C.y);
        data[1][3] = glm::lerp(b.bezier[middle].pos_uv(), b.bezier[right].pos_uv(), D.y);
    }
    // left hand side, add backwards
    else {
        float4 bck = a.bezier[left].backward_uv() - a.bezier[left].pos_uv() + a.bezier[middle].pos_uv();
        float4 fwd = b.bezier[left].forward_uv() - b.bezier[left].pos_uv() + b.bezier[middle].pos_uv();
        

        data[0][0] = glm::lerp(a.bezier[middle].pos_uv(), a.bezier[left].pos_uv(), A.x);		// remember to pack UVs here
        data[0][1] = glm::lerp(bck, a.bezier[left].backward_uv(), B.x);
        data[0][2] = glm::lerp(fwd, b.bezier[left].forward_uv(), C.x);
        data[0][3] = glm::lerp(b.bezier[middle].pos_uv(), b.bezier[left].pos_uv(), D.x);

        data[1][0] = glm::lerp(a.bezier[middle].pos_uv(), a.bezier[left].pos_uv(), A.y);
        data[1][1] = glm::lerp(bck, a.bezier[left].backward_uv(), B.y);
        data[1][2] = glm::lerp(fwd, b.bezier[left].forward_uv(), C.y);
        data[1][3] = glm::lerp(b.bezier[middle].pos_uv(), b.bezier[left].pos_uv(), D.y);

        // FIXME we have to be able to say if UV's fliup or not
        data[0][0].w *= -1;
        data[0][1].w *= -1;
        data[0][2].w *= -1;
        data[0][3].w *= -1;

        data[1][0].w *= -1;
        data[1][1].w *= -1;
        data[1][2].w *= -1;
        data[1][3].w *= -1;
    }
}




/*  splinePoint
    --------------------------------------------------------------------------------------------------------------------*/
splinePoint::splinePoint()
{
    lanesLeft[2].laneWidth = 2.5f;		// first lane	??? so seems I like cumulative rather than absolute ???
    lanesLeft[2].type = 2;
    lanesLeft[6].laneWidth = 0.5f;		// shoulder
    lanesLeft[6].type = 2;
    lanesLeft[7].laneWidth = 0.5f;		// gravel
    lanesLeft[7].type = 10;
    lanesLeft[8].laneWidth = 2.5f;		// clearing
    lanesLeft[8].type = 11;


    lanesRight[2].laneWidth = 2.5f;		// first lane
    lanesRight[2].type = 2;
    lanesRight[6].laneWidth = 0.5f;		// shoulder
    lanesRight[6].type = 2;
    lanesRight[7].laneWidth = 0.5f;		// gravel
    lanesRight[7].type = 10;
    lanesRight[8].laneWidth = 2.5f;		// clearing
    lanesRight[8].type = 11;

    for (uint i = 0; i < 15; i++)
    {
        matLeft[i] = -1;
        matRight[i] = -1;
    }
    matCenter[0] = -1;
    matCenter[1] = -1;

}


//Positions, so anything inside the 200,000km world for now, just make this big it just test for errors in code
bool isSanePosition(glm::vec3 v)
{
    if (std::isnan(v.x)) return false;
    if (std::isnan(v.y)) return false;
    if (std::isnan(v.z)) return false;

    if (fabs(v.x) > 100000.f) return false;
    if (fabs(v.y) > 100000.f) return false;
    if (fabs(v.z) > 100000.f) return false;

    return true;
}



void splinePoint::solveMiddlePos()
{
    if (bAutoGenerate) {
        bezier[middle].pos = anchor + glm::vec3(0, height_AboveAnchor, 0) + (bezier[middle].tangentForward * tangent_Offset) + (perpendicular * perpendicular_Offset);	// straight up down

        if (!isSanePosition(bezier[middle].pos))
        {
            bool bCM = true;
        }
    }
}



void splinePoint::addHeight(float h)
{
    height_AboveAnchor += h;
    solveMiddlePos();
}



void splinePoint::addTangent(float _t)
{
    tangent_Offset += _t;
    solveMiddlePos();
}



void splinePoint::setAnchor(glm::vec3 _P, glm::vec3 _N, uint _lod)
{
    anchor = _P;
    anchorNormal = _N;
    lidarLod = _lod;

    solveMiddlePos();
}



void splinePoint::forceAnchor(glm::vec3 _P, glm::vec3 _N, uint _lod)
{
    anchor = _P;
    anchorNormal = _N;
    height_AboveAnchor = 0;
    bezier[middle].pos = _P;
    lidarLod = _lod;
}



void splinePoint::setMaterialIndex(uint _side, uint _slot, uint _index)
{
    switch (_side)
    {
    case 0:
        if (_slot < matLeft.size()) {
            matLeft[_slot] = _index;
        }
        break;
    case 1:
        if (_slot < matCenter.size()) {
            matCenter[_slot] = _index;
        }
        break;
    case 2:
        if (_slot < matRight.size()) {
            matRight[_slot] = _index;
        }
        break;
    }
}



int splinePoint::getMaterialIndex(uint _side, uint _slot)
{
    switch (_side)
    {
    case 0:
        if (_slot < matLeft.size()) {
            return matLeft[_slot];
        }
        break;
    case 1:
        if (_slot < matCenter.size()) {
            return matCenter[_slot];
        }
        break;
    case 2:
        if (_slot < matRight.size()) {
            return matRight[_slot];
        }
        break;
    }
    return 0;
}



