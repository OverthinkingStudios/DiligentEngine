// ribbonBuilder.cpp - pack() is the byte-exact contract with
// render_vegetation_ribbons.hlsl: sqrt-encoded radius, 81.487 yaw/pitch
// quantizer, inverted startBit, block-boundary duplication with the leafRoot
// fixups. Do not "improve" any of it.

#include "terrain.h"    // brings in the hlsli-shared structs/aliases and ribbonBuilder.h

#include <cmath>
#include <cstdlib>      // __min/__max (MSVC macros)

#include "glm/gtx/compatibility.hpp"    // glm::saturate on vectors


// Load-bearing until the underlying issue is understood - do not remove.
// This TU only runs at build time, so it costs no frame time.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif


float ribbonVertex::objectScale = 0.0002f;  //0.002 for trees  // 32meter block 2mm presision
float ribbonVertex::radiusScale = 0.0100f;//  so biggest radius now objectScale / 2.0f;
float ribbonVertex::O = 16384.0f * 0.0002f * 0.5f;  // spelled out rather than read from objectScale - cross-TU static init order is not guaranteed
float3 ribbonVertex::objectOffset = float3(ribbonVertex::O, ribbonVertex::O * 0.5f, ribbonVertex::O);
uint ribbonVertex::S_root = 0;

float ribbonBuilder::V_MAX = 127.f; //Maximum V before overflow


ribbonVertex8 ribbonVertex::pack()
{
    int x14 = ((int)((position.x + objectOffset.x) / objectScale)) & 0x3fff;
    int y16 = ((int)((position.y + objectOffset.y) / objectScale)) & 0xffff;
    int z14 = ((int)((position.z + objectOffset.z) / objectScale)) & 0x3fff;

    int DiamondFlag = (int)diamond + (((int)pointSprite) << 1);

    int u7 = ((int)(uv.x * 127.f)) & 0x7f;
    int v15 = ((int)(uv.y * 255.f)) & 0x7fff;
    int radius8 = (int)(pow(__min(1.0f, radius / radiusScale), 0.5f) * 255.f) & 0xff;        // square

    float up_yaw = atan2(bitangent.z, bitangent.x) + 3.1415926535897932f;
    float up_pitch = atan2(bitangent.y, glm::length(float2(bitangent.x, bitangent.z))) + 1.570796326794f;
    int up_yaw9 = ((int)(up_yaw * 81.487f)) & 0x1ff;
    int up_pitch8 = ((int)(up_pitch * 81.487f)) & 0xff;

    float left_yaw = atan2(tangent.z, tangent.x) + 3.1415926535897932f;
    float left_pitch = atan2(tangent.y, glm::length(float2(tangent.x, tangent.z))) + 1.570796326794f;
    int left_yaw9 = ((int)(left_yaw * 81.487f)) & 0x1ff;
    int left_pitch8 = ((int)(left_pitch * 81.487f)) & 0xff;
    {
        // Self-check that doubles as documentation of the shader-side decode:
        // 0.01227 = 2pi/512.
        float plane, x, y, z;
        z = sin((left_yaw9 - 256) * 0.01227f);
        x = cos((left_yaw9 - 256) * 0.01227f);
        y = sin((left_pitch8 - 128) * 0.01227f);
        plane = cos((left_pitch8 - 128) * 0.01227f);
        float3 reconstruct = float3(plane * x, y, plane * z);
        if (glm::dot(reconstruct, tangent) < 0.98f)
        {
            bool bCm = true;
            (void)bCm;
        }
    }


    float coneyaw = atan2(lightCone.z, lightCone.x) + 3.1415926535897932f;
    float conepitch = atan2(lightCone.y, glm::length(float2(lightCone.x, lightCone.z))) + 1.570796326794f;
    int coneYaw9 = ((int)(coneyaw * 81.17f)) & 0x1ff;       // 81.17 here, NOT the 81.487 used for the other angles - deliberate, do not "fix"
    int conePitch8 = ((int)(conepitch * 81.17f)) & 0xff;
    int cone7 = (int)((lightCone.w + 1.f) * 63.5f);
    int depth8 = (int)(glm::clamp(lightDepth, 0.f, 2.0f) * 127.5f);

    uint albedo = (int)glm::clamp((albedoScale - 0.1f) / 0.008f, 0.f, 255.f);
    uint translucency = (int)glm::clamp((translucencyScale - 0.1f) / 0.008f, 0.f, 255.f);

    uint AoA = (int)glm::clamp(ambientOcclusion * 255, 0.f, 255.f);

    uint L_stiff = (int)glm::clamp((leafStiffness - 0.1f) / 0.004f, 0.f, 255.f);
    uint L_freq = (int)glm::clamp((leafFrequency) / 0.004f, 0.f, 255.f);
    uint L_index = (int)glm::clamp((leafIndex) / 0.004f, 0.f, 255.f);            // Deprecated sin ce Bezier this space is actualyl free



    ribbonVertex8 p;
    p.a = ((faceCamera & 0x1) << 31) + (startBit << 30) + (x14 << 16) + y16;
    p.b = (z14 << 18) + ((material & 0x3ff) << 8) + (DiamondFlag & 0x3);  // This leaves 6 bits free here we added pointsprite
    p.c = (up_yaw9 << 23) + (up_pitch8 << 15) + v15;
    p.d = (left_yaw9 << 23) + (left_pitch8 << 15) + (u7 << 8) + radius8;
    p.e = (coneYaw9 << 23) + (conePitch8 << 15) + (cone7 << 8) + depth8;
    p.f = (AoA << 24) + (shadow << 16) + (albedo << 8) + translucency;
    p.g = ((pivots[0] & 0xff) << 24) + ((pivots[1] & 0xff) << 16) + ((pivots[2] & 0xff) << 8) + (pivots[3] & 0xff);
    p.h = (L_index << 24) + (L_stiff << 16) + (L_freq << 8) + leafRoot; // add stiffnes etc later
    return p;
}






void ribbonBuilder::setup(float scale, float radius, float3 offset)
{
    ribbonVertex::objectScale = scale;
    ribbonVertex::radiusScale = radius;
    ribbonVertex::objectOffset = offset;
}



void ribbonBuilder::clearPivot()
{
    tooManyPivots = false;
    pivotMap.clear();
    pivotPoints.clear();
}



void ribbonBuilder::clearStats(int _max)
{
    lod_startBlock = 0;
    totalRejectedVerts = 0;
    maxBlocks = _max;

    ribbons.clear();
}



void ribbonBuilder::clear()
{
    //printf(terrafectorSystem::_logfile, "\n\nribbonBuilder::clear()\n");
    packed.clear();
    ribbons.clear();
}




void    ribbonBuilder::startRibbon(bool _cameraFacing, uint pv[4])
{
    //fprintf(terrafectorSystem::_logfile, "\nribbonBuilder::startRibbon()\n");
    pushStart = false;       // prepare for a new ribbon to start
    vertex.faceCamera = _cameraFacing;
    vertex.S_root = 0;

    vertex.pivots[0] = pv[0];
    vertex.pivots[1] = pv[1];
    vertex.pivots[2] = pv[2];
    vertex.pivots[3] = pv[3];
}

void    ribbonBuilder::startRibbon(bool _cameraFacing, std::array<uint, 4> pv)
{
    //fprintf(terrafectorSystem::_logfile, "\nribbonBuilder::startRibbon()\n");
    pushStart = false;       // prepare for a new ribbon to start
    vertex.faceCamera = _cameraFacing;
    vertex.S_root = 0;

    vertex.pivots[0] = pv[0];
    vertex.pivots[1] = pv[1];
    vertex.pivots[2] = pv[2];
    vertex.pivots[3] = pv[3];
}



void ribbonBuilder::set(glm::mat4 _node, float _radius, int _material, float2 _uv, float _albedo, float _translucency, bool _clearLeafRoot,
    float _stiff, float _freq, float _index, bool _diamond, bool _pointSprite)
{
    //fprintf(terrafectorSystem::_logfile, "  set : mat %d  -   \n", _material);

    if (_material > mat_vector_size_Sanity)
    {
        bool CM = true;
        (void)CM;
    }
    if (_material < 0)
    {
        bool CM = true;
        (void)CM;
    }

    if (_uv.y < 0)
    {
        bool bCM = true;
        (void)bCM;
    }
    if (((int)(ribbons.size() / VEG_BLOCK_SIZE) - lod_startBlock) >= maxBlocks)
    {
        totalRejectedVerts++;
        return;
    }

    vertex.position = _node[3];
    if (fabs(vertex.position.x) > 200 || fabs(vertex.position.z) > 200)
    {
        bool bCM = true;
        (void)bCM;
    }
    vertex.radius = _radius;
    vertex.bitangent = _node[1];  // right handed matrix : up
    vertex.tangent = _node[0];
    vertex.material = _material;
    vertex.uv = _uv;
    vertex.albedoScale = _albedo;
    vertex.translucencyScale = _translucency;

    vertex.leafStiffness = _stiff;
    vertex.leafFrequency = _freq;
    vertex.leafIndex = _index;         // also deprected, since BEzier

    vertex.diamond = _diamond;
    vertex.pointSprite = _pointSprite;

    vertex.leafRoot = vertex.S_root;
    if (_clearLeafRoot)
    {
        vertex.S_root = 0;
    }
    else
    {
        vertex.S_root++;
    }

    vertex.startBit = pushStart;    // badly named its teh inverse, but after the first bit we clear iyt for teh rest of teh ribbon
    pushStart = true;

    uint idx = (uint)ribbons.size();
    if ((idx > 0) && (idx % VEG_BLOCK_SIZE == 0) && vertex.startBit == true)
    {
        // start of a new block, but we are in teh middle of a ribbon, repeat the last one as a start
        ribbonVertex R = ribbons.back();
        R.startBit = false;

        // TODO: this branch zeroes vertex.S_root but leaves R.leafRoot untouched,
        // while the else-branch bumps both. Asymmetric; may be intentional.
        if (_clearLeafRoot)
        {
            vertex.S_root = 0;
        }
        else
        {
            R.leafRoot++; // This is the previous one plus 1 more
            vertex.leafRoot = R.leafRoot + 1;  // advance teh new vertex one more
            vertex.S_root = vertex.leafRoot + 1;
        }

        //fprintf(terrafectorSystem::_logfile, "repeat\n");
        //fprintf(terrafectorSystem::_logfile, "V  (%2.2f, %2.2f, %2.2f)m  r - %2.3fm %d\n", R.position.x, R.position.y, R.position.z, R.radius, R.startBit);
        ribbons.push_back(R);
    }

    //fprintf(terrafectorSystem::_logfile, "V  (%2.2f, %2.2f, %2.2f)m  r - %2.3fm %d\n", vertex.position.x, vertex.position.y, vertex.position.z, vertex.radius, vertex.startBit);
    ribbons.push_back(vertex);


}







uint ribbonBuilder::pushPivot(uint _guid, _plant_anim_pivot _pivot)
{
    // in future this function will do the packing when we pack _plant_anim_pivot tighter
    if (pivotPoints.size() < 255)
    {
        /*
        auto it = pivotMap.find(_guid);
        if (it != pivotMap.end())
        {
            //if (LOGTHEBUILD) { fprintf(terrafectorSystem::_logfile, "PIVOT %d - found  %d\n", _guid, it->second); }
            return it->second;
        }
        else
        */
        {
            //if (LOGTHEBUILD) { fprintf(terrafectorSystem::_logfile, "new pivot %d, ", _guid); }
            pivotMap[_guid] = (int)ribbonBuilder::pivotPoints.size();
            pivotPoints.push_back(_pivot);
            return pivotMap[_guid];
        }
    }
    else
    {
        //if (LOGTHEBUILD) { fprintf(terrafectorSystem::_logfile, "tooManyPivots\n"); }
        tooManyPivots = true;
        return 255; // so just turn it off
    }
}



float3 ribbonBuilder::egg(float2 extents, float3 vector, float yOffset)
{
    float3 V = glm::normalize(vector) * extents.x;
    if (V.y > 0)
    {
        V.y *= extents.y * (1.f - yOffset);
    }
    else
    {
        V.y *= extents.y * yOffset;
    }
    return V;
}



void ribbonBuilder::lightBasic(float2 extents, float plantDepth, float yOffset)
{
    for (auto& R : ribbons)
    {
        float midHeight = yOffset * extents.y;
        float3 Ldir = R.position - float3(0, midHeight, 0);
        float3 edge = egg(extents, Ldir, yOffset);
        R.lightCone = float4(glm::normalize(Ldir), 0);    // 0 is just 180 degrees so wide, fixme tighter at ythe bottom
        float depthMeters = __max(0.f, glm::length(edge) - glm::length(Ldir));
        R.lightDepth = depthMeters / plantDepth;
        if (R.position.y < midHeight)
        {
            R.lightDepth = (depthMeters + (midHeight - R.position.y)) / plantDepth;
        }

        float3 P = R.position;
        P.y = 0;
        float dx = glm::length(P);
        float aoW = 0.3f + 0.7f * dx / extents.x;
        float aoH = 0.4f + 0.6f * R.position.y / extents.y;
        R.ambientOcclusion = __max(aoW, aoH);
        if (R.position.y < midHeight)
        {
            float scale = (midHeight - R.position.y) / midHeight;
            R.ambientOcclusion *= (1.f - scale * 0.5f);
        }
    }
}


/*  I think I want an envelope - but lest do egg first, with very small H
*   very not good for long branch with leaves at the tip
*   The medium term is likely some sort of cubemap, where each voxel calculates a direction and horizon and maybe some sort of density
*   and then some way to do this for specific sub branches, once at highest resolution and then save it all
*   the mess migth eb what to do with branches that leaves long lines, or should we do it for leaves only?
*/
void ribbonBuilder::lightBranch(uint from, uint to, float3 root, float3 tip, float plantDepth, float yOffset, float rootAO)
{
    (void)plantDepth; (void)yOffset; (void)rootAO;
    float3 middle = (root * 0.6667f) + (tip * 0.3333f);
    float w = glm::length(tip - root) * 0.5f;    // just 1/3 width
    (void)w;
    // just cheat and use half lenghts for now
    for (uint i = from; i < to; i++)
    {
        auto& R = ribbons[i];
        float3 Ldir = R.position - middle;
        R.lightCone = float4(glm::normalize(Ldir), 0);    // 0 is just 180 degrees so wide, fixme tighter at ythe bottom
        //float depthMeters = __max(0, glm::length(edge) - glm::length(Ldir));
        //R.lightDepth = depthMeters / plantDepth;
    }
}




void ribbonBuilder::finalizeAndFillLastBlock()
{
    if (ribbons.size() > 0)
    {
        ribbonVertex LAST = ribbons.back();
        LAST.startBit = false;
        LAST.radius = 0;
        ribbonVertex8 pck_last = LAST.pack();

        int last = (int)(ribbons.size() % VEG_BLOCK_SIZE);
        int unusedVerts = 0;
        if (last > 0) unusedVerts = VEG_BLOCK_SIZE - last;
        for (int i = 0; i < unusedVerts; i++)
        {
            packed.push_back(pck_last);
        }
    }
}




void ribbonBuilder::pack()
{
    //fprintf(terrafectorSystem::_logfile, "\n\npack()\n");
    for (auto& R : ribbons)
    {
        ribbonVertex8 pck = R.pack();
        packed.push_back(pck);
        //fprintf(terrafectorSystem::_logfile, "V  (%2.2f, %2.2f, %2.2f)m  r - %2.3fm %d\n", R.position.x, R.position.y, R.position.z, R.radius, R.startBit);
    }
}





float2 ribbonBuilder::calculate_extents(glm::mat4 view)
{
    float2 extents = float2(0, 0);
    float3 origin = view[3];

    for (auto& R : ribbons)
    {
        extents.x = __max(extents.x, fabs(glm::dot(R.position - origin, (float3)view[0])));
        extents.y = __max(extents.y, glm::dot(R.position - origin, (float3)view[1]));               //  y is positive only
    }

    int cnt[8] = { 0, 0, 0, 0, 0, 0, 0, 0};
    float step = extents.y / 8.f;
    for (auto& R : ribbons)
    {
        uint bucket = (uint)glm::clamp(glm::dot(R.position, (float3)view[1]) / step, 0.f, 7.99f);
        buckets_8[bucket] = __max(glm::dot(R.position, (float3)view[0]), buckets_8[bucket]);
        cnt[bucket]++;
    }

    dU[0] = __max(buckets_8[0], buckets_8[1]);
    dU[1] = __max(buckets_8[2], buckets_8[3]);
    dU[2] = __max(buckets_8[4], buckets_8[5]);
    // TODO: bucket 7 twice, bucket 6 never read - looks like a typo for
    // (buckets_8[6], buckets_8[7]). It feeds a baked-asset contract though, so
    // changing it invalidates already baked data.
    dU[3] = __max(buckets_8[7], buckets_8[7]);
    dU /= extents.x;
    dU += 0.05f;
    glm::saturate(dU);  // TODO: returns a copy, so this leaves dU unclamped - deliberately left as is; the += 0.05f above can push it past 1

    return extents;
}
