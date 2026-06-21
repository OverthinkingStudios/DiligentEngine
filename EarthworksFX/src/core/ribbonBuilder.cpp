#include "ribbonBuilder.h"




float ribbonVertex::objectScale = 0.002f;  //0.002 for trees  // 32meter block 2mm presision
float ribbonVertex::radiusScale = 5.0f;//  so biggest radius now objectScale / 2.0f;
float ribbonVertex::O = 16384.0f * ribbonVertex::objectScale * 0.5f;
float3 ribbonVertex::objectOffset = float3(O, O * 0.5f, O);
uint ribbonVertex::S_root = 0;



ribbonVertex8 ribbonVertex::pack()
{
    int x14 = ((int)((position.x + objectOffset.x) / objectScale)) & 0x3fff;
    int y16 = ((int)((position.y + objectOffset.y) / objectScale)) & 0xffff;
    int z14 = ((int)((position.z + objectOffset.z) / objectScale)) & 0x3fff;

    int DiamondFlag = (int)diamond;

    int u7 = ((int)(uv.x * 127.f)) & 0x7f;
    int v15 = ((int)(uv.y * 255.f)) & 0x7fff;
    int radius8 = (int)(pow(__min(1.0f, radius / radiusScale), 0.5f) * 255.f) & 0xff;        // square

    float up_yaw = atan2(bitangent.z, bitangent.x) + 3.1415926535897932;
    float up_pitch = atan2(bitangent.y, length(float2(bitangent.x, bitangent.z))) + 1.570796326794;
    int up_yaw9 = ((int)(up_yaw * 81.487)) & 0x1ff;
    int up_pitch8 = ((int)(up_pitch * 81.487)) & 0xff;

    float left_yaw = atan2(tangent.z, tangent.x) + 3.1415926535897932;
    float left_pitch = atan2(tangent.y, length(float2(tangent.x, tangent.z))) + 1.570796326794;
    int left_yaw9 = ((int)(left_yaw * 81.487)) & 0x1ff;
    int left_pitch8 = ((int)(left_pitch * 81.487)) & 0xff;
    {
        float plane, x, y, z;
        z = sin((left_yaw9 - 256) * 0.01227);
        x = cos((left_yaw9 - 256) * 0.01227);
        y = sin((left_pitch8 - 128) * 0.01227);
        plane = cos((left_pitch8 - 128) * 0.01227);
        float3 reconstruct = float3(plane * x, y, plane * z);
        if (dot(reconstruct, tangent) < 0.98)
        {
            bool bCm = true;
        }
    }


    float coneyaw = atan2(lightCone.z, lightCone.x) + 3.1415926535897932;
    float conepitch = atan2(lightCone.y, length(float2(lightCone.x, lightCone.z))) + 1.570796326794;
    int coneYaw9 = ((int)(coneyaw * 81.17)) & 0x1ff;
    int conePitch8 = ((int)(conepitch * 81.17)) & 0xff;
    int cone7 = (int)((lightCone.w + 1.f) * 63.5f);
    int depth8 = (int)(clamp(lightDepth, 0.f, 2.0f) * 127.5f);

    uint albedo = (int)clamp((albedoScale - 0.1f) / 0.008f, 0.f, 255.f);
    uint translucency = (int)clamp((translucencyScale - 0.1f) / 0.008f, 0.f, 255.f);

    uint AoA = (int)clamp(ambientOcclusion * 255, 0.f, 255.f);

    uint L_stiff = (int)clamp((leafStiffness - 0.1f) / 0.004f, 0.f, 255.f);
    uint L_freq = (int)clamp((leafFrequency) / 0.004f, 0.f, 255.f);
    uint L_index = (int)clamp((leafIndex) / 0.004f, 0.f, 255.f);            // Deprecated sin ce Bezier this space is actualyl free



    ribbonVertex8 p;
    p.a = ((faceCamera & 0x1) << 31) + (startBit << 30) + (x14 << 16) + y16;
    p.b = (z14 << 18) + ((material & 0x3ff) << 8) + (DiamondFlag & 0x1);  // This leaves 7 bits free here
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
    packed.clear();
    ribbons.clear();
}




void    ribbonBuilder::startRibbon(bool _cameraFacing, uint pv[4])
{
    pushStart = false;       // prepare for a new ribbon to start
    vertex.faceCamera = _cameraFacing;
    vertex.S_root = 0;

    vertex.pivots[0] = pv[0];
    vertex.pivots[1] = pv[1];
    vertex.pivots[2] = pv[2];
    vertex.pivots[3] = pv[3];
}



void ribbonBuilder::set(glm::mat4 _node, float _radius, int _material, float2 _uv, float _albedo, float _translucency, bool _clearLeafRoot,
    float _stiff, float _freq, float _index, bool _diamond)
{
    //fprintf(terrafectorSystem::_logfile, "  set : mat %d  -   \n", _material);

    if (_uv.y < 0)
    {
        bool bCM = true;
    }
    if ((ribbons.size() / VEG_BLOCK_SIZE - lod_startBlock) >= maxBlocks)
    {
        totalRejectedVerts++;
        return;
    }

    vertex.position = _node[3];
    if (fabs(vertex.position.x) > 200 || fabs(vertex.position.z) > 200)
    {
        bool bCM = true;
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

    uint idx = ribbons.size();
    if ((idx > 0) && (idx % VEG_BLOCK_SIZE == 0) && vertex.startBit == true)
    {
        // start of a new block, but we are in teh middle of a ribbon, repeat the last one as a start
        ribbonVertex R = ribbons.back();
        R.startBit = false;

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

        ribbons.push_back(R);
    }

    ribbons.push_back(vertex);

    pushStart = true;
}







uint ribbonBuilder::pushPivot(uint _guid, _plant_anim_pivot _pivot)
{
    // in future this function will do the packing when we pack _plant_anim_pivot tighter
    if (pivotPoints.size() < 255)
    {
        auto it = pivotMap.find(_guid);
        if (it != pivotMap.end())
        {
            //if (LOGTHEBUILD) { fprintf(terrafectorSystem::_logfile, "PIVOT %d - found  %d\n", _guid, it->second); }
            return it->second;
        }
        else
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
        float depthMeters = __max(0, glm::length(edge) - glm::length(Ldir));
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
    float3 middle = (root * 0.6667f) + (tip * 0.3333f);
    // just cheat and use half lenghts for now
    for (int i = from; i < to; i++)
    {
        auto& R = ribbons[i];
    }
}




void ribbonBuilder::finalizeAndFillLastBlock()
{
    int last = ribbons.size() % VEG_BLOCK_SIZE;
    int unusedVerts = 0;
    if (last > 0) unusedVerts = VEG_BLOCK_SIZE - last;
    for (int i = 0; i < unusedVerts; i++)
    {
        packed.push_back(packed.front());
    }
}




void ribbonBuilder::pack()
{
    for (auto& R : ribbons)
    {
        ribbonVertex8 pck = R.pack();
        packed.push_back(pck);
    }
}




float2 ribbonBuilder::calculate_extents(glm::mat4 view)
{
    float2 extents = float2(0, 0);

    for (auto& R : ribbons)
    {
        extents.x = __max(extents.x, fabs(glm::dot(R.position, (float3)view[0])));
        extents.y = __max(extents.y, fabs(glm::dot(R.position, (float3)view[1])));
    }

    int cnt[8] = { 0, 0, 0, 0, 0, 0, 0, 0};
    float step = extents.y / 9.f;
    for (auto& R : ribbons)
    {
        uint bucket = (uint)glm::clamp(glm::dot(R.position, (float3)view[1]) / step, 0.f, 8.f);
        buckets_8[bucket] = __max(glm::dot(R.position, (float3)view[0]), buckets_8[bucket]);
        cnt[bucket]++;
    }

    dU[0] = __max(buckets_8[0], buckets_8[1]);
    dU[1] = __max(buckets_8[2], buckets_8[3]);
    dU[2] = __max(buckets_8[4], buckets_8[5]);
    dU[3] = __max(buckets_8[6], buckets_8[6]);
    dU /= extents.x;
    dU += 0.05f;
    glm::saturate(dU);

    return extents;
}




