
#include"roads_AI.h"
#include "imgui.h"
#include "imgui_internal.h"



extern void cubic_Casteljau_Full(float t, glm::vec3 P0, glm::vec3 P1, glm::vec3 P2, glm::vec3 P3, glm::vec3& pos, glm::vec3& vel, glm::vec3& acc);
extern glm::vec3 cubic_Casteljau(float t, glm::vec3 P0, glm::vec3 P1, glm::vec3 P2, glm::vec3 P3);
extern glm::vec3 cubic_Casteljau(float t, bezierPoint* A, bezierPoint* B);
extern glm::vec3 del_cubic_Casteljau(float t0, float t1, bezierPoint* A, bezierPoint* B);


/*  aiIntersection
    --------------------------------------------------------------------------------------------------------------------*/
void aiIntersection::setPos(glm::vec2 _p)
{
    glm::vec2 delta = _p - pos;

    float l = glm::length(delta);
    if (l > 0) {
        dir = glm::normalize(delta);
        delDistance += l;
        pos = _p;
    }
}



/*  AI_bezier
    --------------------------------------------------------------------------------------------------------------------*/
float cross2(glm::vec2 a, glm::vec2 b) {
    return a.x * b.y - b.x * a.y;
}



void AI_bezier::intersect(aiIntersection* _pAI)
{
    if (segments.size() > 0)
    {
        //_pAI->segment = __min(_pAI->segment, (uint)segments.size());
        float d0 = 0.0f;
        float d1 = 0.0f;

        if (!_pAI->bHit)
        {
            _pAI->segment = (uint)floor(rootsearch(_pAI->pos));
            _pAI->dist_Left = segments[_pAI->segment].planeInner.distance(_pAI->pos);
            _pAI->dist_Right = segments[_pAI->segment].planeOuter.distance(_pAI->pos);
        }

        // move it all one along
        if (_pAI->segment > 0 && _pAI->delDistance > segments[_pAI->segment].length && _pAI->dist_Left > -2.0f && _pAI->dist_Right < 2.0f)
        {
            _pAI->delDistance = 0.0f;
            uint numMoves = 0;

            // acurate position
            d0 = segments[_pAI->segment].planeTangent.distance(_pAI->pos);
            if (d0 < 0.0f && _pAI->segment > 0) {
                _pAI->segment--;
                d1 = d0;
                d0 = segments[_pAI->segment].planeTangent.distance(_pAI->pos);
            }

            d1 = segments[_pAI->segment + 1].planeTangent.distance(_pAI->pos);
            if (d1 >= 0.0f && _pAI->segment < segments.size() - 2) {
                _pAI->segment++;
                d0 = d1;
                d1 = segments[_pAI->segment + 1].planeTangent.distance(_pAI->pos);
            }

        }
        else {
            d0 = segments[_pAI->segment].planeTangent.distance(_pAI->pos);
            d1 = segments[_pAI->segment + 1].planeTangent.distance(_pAI->pos);
        }

        if (_pAI->segment > 0)
        {
            _pAI->dist_Left = segments[_pAI->segment].planeInner.distance(_pAI->pos);
            _pAI->dist_Right = segments[_pAI->segment].planeOuter.distance(_pAI->pos);

            if (_pAI->dist_Left > -2.0f && _pAI->dist_Right < 2.0f)
            {
                _pAI->bHit = true;
                _pAI->distance = segments[_pAI->segment].distance - start;

                _pAI->road_angle = cross2(_pAI->dir, segments[_pAI->segment].planeTangent.norm);

                _pAI->camber[0] = segments[_pAI->segment].camber;

                // NOW for lookahead
                // just pretend car is 2m wide for the moment
                /*
                uint apexCounter;
                uint maxLook = __min(100, segments.size() - _pAI->segment - 1);

                _pAI->lookahead_curvature = 0;
                _pAI->lookahead_camber = 0;
                for (uint i = 0; i < maxLook; i++)
                {
                    _pAI->lookahead_curvature += segments[_pAI->segment + i].camber;
                    _pAI->lookahead_camber += segments[_pAI->segment + i].camber;
                    _pAI->lookahead_apex_distance;
                }
                _pAI->lookahead_apex_left;
                _pAI->lookahead_apex_camber;
                _pAI->lookahead_apex_curvature
                */
            }
            else
            {
                _pAI->bHit = false;
            }

        }

    }
    else
    {
        _pAI->bHit = false;
    }
}



float AI_bezier::rootsearch(glm::vec2 _pos)
{
    float d0 = segments[0].planeTangent.distance(_pos);

    for (uint i = 1; i < segments.size(); i++) {
        float d1 = segments[i].planeTangent.distance(_pos);
        if (d0 >= 0 && d1 < 0)
        {

            if ((segments[i].planeInner.distance(_pos) > -2) && (segments[i].planeOuter.distance(_pos) < 2.0f))
            {
                return (float)i + (d0 / (d0 - d1));
            }
        }
        d0 = d1;
    }

    return 0.0f;
}



void AI_bezier::clear()
{
    segments.clear();

    bezierPatches.clear();
    bezierPatches.reserve(64);

    patchSegments.clear();
}



void AI_bezier::solveStartEnd()
{
    float startIndex = rootsearch(glm::vec2(startpos.x, startpos.z));
    if (startIndex != 0.0f)
    {
        uint idx = (uint)floor(startIndex);
        startIdx = idx;
        float frac_A = glm::fract(startIndex);
        float frac_B = 1.0f - frac_A;
        start = segments[idx].distance * frac_B + segments[idx + 1].distance * frac_A;
    }

    float endIndex = rootsearch(glm::vec2(endpos.x, endpos.z));
    if (endIndex != 0.0f)
    {
        uint idx = (uint)floor(endIndex);
        endIdx = idx;
        float frac_A = glm::fract(endIndex);
        float frac_B = 1.0f - frac_A;
        end = segments[idx].distance * frac_B + segments[idx + 1].distance * frac_A;
    }
}



// fix me split push from cacheAll();
void AI_bezier::pushSegment(glm::vec4 bezier[2][4])
{
    cubicDouble B;
    memcpy(B.data, bezier, sizeof(glm::vec4) * 2 * 4);
    bezierPatches.push_back(B);
}



void AI_bezier::cacheAll()
{
    // first the patches themselves
    numBezier = (uint)bezierPatches.size() - 1;
    segments.reserve(numBezier * 16);
    float stepSize = 1000;	// just big to begin

    for (uint i = 0; i <= numBezier; i++)
    {
        bezierSegment patch;
        patch.A = bezierPatches[i].data[0][0];
        patch.B = bezierPatches[i].data[1][0];
        patch.planeTangent.frompoints(patch.A, patch.B);

        patchSegments.push_back(patch);
    }

    for (uint i = 0; i < numBezier; i++)
    {
        patchSegments[i].planeInner.frompoints(patchSegments[i + 1].A, patchSegments[i].A);
        patchSegments[i].planeOuter.frompoints(patchSegments[i + 1].B, patchSegments[i].B);
        float width = glm::length(patchSegments[i].B - patchSegments[i].A);
        patchSegments[i].camber = atan2(patchSegments[i].A.y - patchSegments[i].B.y, width);	// see if there is faster way
        /*
        float camberFast = (patchSegments[i].A.y - patchSegments[i].B.y) / width;
        float errorPersent = fabs(camberFast - patchSegments[i].camber) / patchSegments[i].camber * 100.0f;
        if (errorPersent > 5.0f) {
            bool bCM = true;
        }
        */
    }

    uint bezSement = 0;
    bezierSegment S;
    glm::vec3 pos, vel, acc;
    float t = 0;
    S.A = bezierPatches[0].data[0][0];
    S.B = bezierPatches[0].data[1][0];
    S.planeTangent.frompoints(S.A, S.B);	// solve func
    S.optimalPos = (S.A + S.B) * 0.5f;
    S.optimalShift = 0.5f;

    segments.emplace_back(S);	// FIRST ONE  - COME BACK AND FIX IF LOOPING

    while (bezSement < numBezier)
    {
        t += 0.1f;
        float dst = 0;
        float3 a, b;
        while (dst < kevSampleMinSpacing)
        {
            a = cubic_Casteljau(t, bezierPatches[bezSement].data[0][0], bezierPatches[bezSement].data[0][1], bezierPatches[bezSement].data[0][2], bezierPatches[bezSement + 1].data[0][0]);
            b = cubic_Casteljau(t, bezierPatches[bezSement].data[1][0], bezierPatches[bezSement].data[1][1], bezierPatches[bezSement].data[1][2], bezierPatches[bezSement + 1].data[1][0]);
            //dst = __min(glm::length(a - S.A), glm::length(b - S.B));
            dst = (glm::length(a - S.A) + glm::length(b - S.B)) / 2;
            t += 0.01f;
            if (t > 1.0f)
            {
                t -= 1.0f;
                bezSement++;
            }
        }

        if (bezSement < numBezier)	// avoid the last one
        {
            S.A = a;
            S.B = b;
            S.planeTangent.frompoints(a, b);	// solve func
            S.optimalPos = (a + b) * 0.5f;
            S.optimalShift = 0.5f;

            segments.emplace_back(S);
        }
    }
    /*
    // place first #####################################################################
    for (uint i = 0; i < numBezier; i++)
    {

        // now cache individual sections and get length and distance from that
        for (int j = 0; j < 16; j++)
        {
            segments.emplace_back(S);
            float t = (float)j / 16.0f;
            uint idx = i * 16 + j;
            segments[idx].A = cubic_Casteljau(t, bezierPatches[i].data[0][0], bezierPatches[i].data[0][1], bezierPatches[i].data[0][2], bezierPatches[i + 1].data[0][0]);
            segments[idx].B = cubic_Casteljau(t, bezierPatches[i].data[1][0], bezierPatches[i].data[1][1], bezierPatches[i].data[1][2], bezierPatches[i + 1].data[1][0]);
            segments[idx].planeTangent.frompoints(segments[idx].A, segments[idx].B);


            //cubic_Casteljau_Full(t, bezierPatches[i].data[0][0], bezierPatches[i].data[0][1], bezierPatches[i].data[0][2], bezierPatches[i + 1].data[0][0], pos, vel, acc);
            //float num = vel.x * acc.z - vel.z * acc.x;	// 2d det
            //float length = glm::length(vel);
            //segments[idx].radius = length * length * length / num;
            segments[idx].optimalPos = (segments[idx].A + segments[idx].B) * 0.5f;
            segments[idx].optimalShift = 0.5f;
        }
    }
    */

    /*
    int WRAP(int _index)
    {
        int size = segments.size();
        while (_index < 0) _index += size();
        while (_index >= size) )index -= size;
        return _index;
    };*/

    // Loop hier en solve racing line ##################################################################
    uint SIZE = (uint)segments.size();
#define WRAP(x) ((x + SIZE)%SIZE)

    for (int L = 0; L < kevRepeats; L++)
    {
        // calc angles
        for (uint i = 0; i < SIZE; i++)
        {
            glm::vec3 T0 = (segments[WRAP(i)].optimalPos - segments[WRAP(i - 1)].optimalPos);
            glm::vec3 T1 = (segments[WRAP(i + 1)].optimalPos - segments[WRAP(i)].optimalPos);
            glm::vec2 P0 = glm::normalize(glm::vec2(T0.z, -T0.x));
            float L = glm::length(segments[WRAP(i + 1)].optimalPos - segments[WRAP(i - 1)].optimalPos);
            segments[i].radius = glm::dot(P0, glm::vec2(T1.x, T1.z)) / (L * L);
            segments[i].length = L * 0.5f;
        }

        // shifts
        for (uint i = 0; i < SIZE; i++)
        {
            float biggestR = 0;	// same sign
            float sgn = segments[i].radius / fabs(segments[i].radius);
            float sgnBiggest = 1.0f;

            float avsR = 0;	// same sign
            for (int j = -kevAvsCnt; j <= kevAvsCnt; j++) {
                avsR += segments[WRAP(i + j)].radius;
            }
            avsR /= (kevAvsCnt * 2 + 1);

            for (int j = -kecMaxCnt; j < kecMaxCnt; j++) {
                if (fabs(segments[WRAP(i + j)].radius) > biggestR)
                {
                    biggestR = fabs(segments[WRAP(i + j)].radius);
                    sgnBiggest = fabs(segments[WRAP(i + j)].radius) / segments[WRAP(i + j)].radius;
                }
            }
            biggestR *= sgnBiggest;

            float shift = (segments[i].radius - biggestR) * kevOutScale;		// Oopskop
            shift += avsR * kevInScale;
            shift = glm::clamp(shift, -0.1f, 0.1f);

            segments[i].optimalShift -= shift;
            if (segments[i].optimalShift > 0.8f) segments[i].optimalShift = 0.8f;	// FIXME on DISTANCE in METERS
            if (segments[i].optimalShift < 0.2f) segments[i].optimalShift = 0.2f;
        }

        // push out
        /*
        for (uint i = 50; i < segments.size() - 52; i++)
        {
            float push = 0;
            if (segments[i].optimalShift > 0.9f) push = 0.9f - segments[i].optimalShift;
            if (segments[i].optimalShift < 0.1f) push = 0.1f - segments[i].optimalShift;

            if (push != 0) {
                for (int j = -40; j < 40; j++) {
                    float scale = glm::smoothstep(0.0f, 1.0f, (40.0f - fabs((float)j)) / 40.0f) * 2.7f;
                    segments[i].optimalShift -= push * scale;
                }
            }
        }
        */

        // smooth out
        for (int k = 0; k < kevSmooth; k++)
        {
            for (uint i = 0; i < SIZE; i++)
            {
                //segments[i].optimalTemp = (segments[i - 2].optimalShift + segments[i - 1].optimalShift + segments[i].optimalShift + segments[i + 1].optimalShift + segments[i + 2].optimalShift) / 5.0f;
                //segments[i].optimalTemp = ( segments[i - 1].optimalShift + segments[i].optimalShift + segments[i + 1].optimalShift ) / 3.0f;
                segments[i].optimalTemp = (segments[WRAP(i - 1)].optimalShift * 0.5f + segments[i].optimalShift + segments[WRAP(i + 1)].optimalShift * 0.5f) / 2.0f;
            }

            for (uint i = 0; i < SIZE; i++)
            {
                segments[i].optimalShift = segments[i].optimalTemp;
            }
        }

        // solve position
        for (uint i = 0; i < segments.size(); i++)
        {
            segments[i].optimalPos = glm::lerp(segments[i].A, segments[i].B, segments[i].optimalShift);
        }


    }

    for (uint i = 0; i < segments.size() - 1; i++)
    {
        segments[i].planeInner.frompoints(segments[i + 1].A, segments[i].A);
        segments[i].planeOuter.frompoints(segments[i + 1].B, segments[i].B);
        segments[i].length = (glm::length(segments[i + 1].A - segments[i].A) + glm::length(segments[i + 1].B - segments[i].B)) * 0.5f;
        segments[i + 1].distance = segments[i].distance + segments[i].length;
        float width = glm::length(segments[i].B - segments[i].A);
        //segments[i].camber = atan2(segments[i].A.y - segments[i].B.y, width);	// see if there is faster way
        segments[i].camber = (segments[i].A.y - segments[i].B.y) / width;
        /*
        segments[i].radius = 100000;	// net baie groot
        if (i > 0 && i < segments.size() - 2) {
            float R_a = 0;
            float R_b = 0;
            vec3 AC = glm::normalize(segments[i + 1].A - segments[i - 1].A);
            vec2 Bperp = vec2(AC.z, -AC.x);
            vec3 AB = segments[i].A - segments[i - 1].A;
            //float l = glm::length(AB);
            float l = glm::length(segments[i + 1].A - segments[i - 1].A) * 0.5f;
            vec2 ABnorm = glm::normalize(vec2(AB.x, AB.z));
            float det = glm::dot(Bperp, ABnorm);
            if (fabs(det) > 0.00001) {
                R_a = l / det;
            }



            // B side
            AC = glm::normalize(segments[i + 1].B - segments[i - 1].B);
            Bperp = vec2(AC.z, -AC.x);
            AB = segments[i].B - segments[i - 1].B;
            l = glm::length(AB);
            ABnorm = glm::normalize(vec2(AB.x, AB.z));
            det = glm::dot(Bperp, ABnorm);
            if (fabs(det) > 0.00001) {
                R_b = l / det;
            }

            float R_max = R_a;
            //if (fabs(R_b) > fabs(R_a)) R_max = R_b;

            if (R_max != 0) {
                segments[i].radius = R_max;
            }

        }
        */
    }

    for (uint i = 0; i < numBezier; i++)
    {
        //patchSegments[i].length = (glm::length(patch.A - segments[size - 1].A) + glm::length(segments[size].B - segments[size - 1].B)) * 0.5f;
        //patchSegments[i].distance
    }
}



void AI_bezier::exportAI(FILE* file)
{
    fwrite(&isClosedPath, sizeof(int), 1, file);
    fwrite(&startpos, sizeof(float3), 1, file);
    fwrite(&endpos, sizeof(float3), 1, file);
    fwrite(&pathLenght, sizeof(float), 1, file);
    fwrite(&numBezier, sizeof(uint), 1, file);
    fwrite(&bezierPatches[0], sizeof(cubicDouble), bezierPatches.size(), file);
}



void AI_bezier::exportAITXTDEBUG(FILE* file)
{
    if (isClosedPath)	fprintf(file, "closed loop\n");
    else				fprintf(file, "open\n");
    fprintf(file, "start %2.3f, %2.3f, %2.3f\n", startpos.x, startpos.y, startpos.z);
    fprintf(file, "end %2.3f, %2.3f, %2.3f\n", endpos.x, endpos.y, endpos.z);
    fprintf(file, "pathLength %2.3fkm\n", pathLenght);
    fprintf(file, "# %d\n\n", numBezier);

    for (uint i = 0; i < (uint)bezierPatches.size(); i++) {
        fprintf(file, "%3d  (%2.3f, %2.3f, %2.3f, %2.3f)  (%2.3f, %2.3f, %2.3f, %2.3f) \n", i, bezierPatches[i].data[0][0].x, bezierPatches[i].data[0][0].y, bezierPatches[i].data[0][0].z, bezierPatches[i].data[0][0].w, bezierPatches[i].data[1][0].x, bezierPatches[i].data[1][0].y, bezierPatches[i].data[1][0].z, bezierPatches[i].data[1][0].w);
    }
}



void AI_bezier::exportGates(FILE* file)
{
    for (uint i = 0; i < numBezier; i++)
    {
        glm::vec3 startL = cubic_Casteljau(0, bezierPatches[i].data[0][0], bezierPatches[i].data[0][1], bezierPatches[i].data[0][2], bezierPatches[i + 1].data[0][0]);
        glm::vec3 midL = cubic_Casteljau(0.5, bezierPatches[i].data[0][0], bezierPatches[i].data[0][1], bezierPatches[i].data[0][2], bezierPatches[i + 1].data[0][0]);

        glm::vec3 startR = cubic_Casteljau(0, bezierPatches[i].data[1][0], bezierPatches[i].data[1][1], bezierPatches[i].data[1][2], bezierPatches[i + 1].data[1][0]);
        glm::vec3 midR = cubic_Casteljau(0.5, bezierPatches[i].data[1][0], bezierPatches[i].data[1][1], bezierPatches[i].data[1][2], bezierPatches[i + 1].data[1][0]);

        fprintf(file, "%f %f %f %f  %f %f\n", startL.x, startL.z, startR.x, startR.z, segments[i * 16].camber, segments[i * 16].radius);
    }
}



void AI_bezier::exportCSV(FILE* file, uint _side)
{
    for (uint i = startIdx; i <= endIdx; i++)
    {
        glm::vec3 pos;

        switch (_side) {
        case 0:
            pos = segments[i].A;
            break;
        case 1:
            //pos = (segments[i].A + segments[i].B) * 0.5f;
            pos = segments[i].optimalPos;
            break;
        case 2:
            pos = segments[i].B;
            break;
        }

        //fprintf(file, "%f %f %f %f %f %f\n", pos.x, pos.y, pos.z, segments[i].camber, segments[i].radius, segments[i].optimalShift);
        fprintf(file, "%f %f %f\n", pos.x, pos.y, pos.z);
    }
}



void AI_bezier::save() {
}



void AI_bezier::load(FILE* file)
{
    fread(&isClosedPath, sizeof(int), 1, file);
    fread(&startpos, sizeof(float3), 1, file);
    fread(&endpos, sizeof(float3), 1, file);
    fread(&pathLenght, sizeof(float), 1, file);
    fread(&numBezier, sizeof(uint), 1, file);
    bezierPatches.resize(numBezier + 1);
    fread(&bezierPatches[0], sizeof(cubicDouble), numBezier + 1, file);

    cacheAll();
    solveStartEnd();	//??? maybe save startend
}

