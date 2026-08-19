// vegetationBuilder_Trees.cpp - the _treeBuilder half of the vegetation
// system: Grove OBJ stream-import, cutting the result into reusable branch
// assets, and the canopy cubemap that bakes their lighting.
//
// The OBJ carries no tree topology - it is a flat triangle stream. Nodes,
// branch starts, side branches, leaves and dead ends are all inferred
// geometrically while streaming (ring planarity, node radii, point-in-cylinder
// tests), which is why the reader is a small state machine over readVertex().

#include "terrain.h"    // brings in the hlsli-shared structs/aliases and vegetationBuilder.h

#include <algorithm>
#include <chrono>
#include <cstdlib>      // __min/__max (MSVC macros)

#include "glm/gtx/compatibility.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include "ots/Log.hpp"

using namespace std::chrono;

// Load-bearing until the underlying issue is understood - do not remove.
// This TU only runs at build time, so it costs no frame time.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif

//extern bool anyChange;


extern ribbonBuilder _ribbonBuilder;


std::uniform_int_distribution<> DD_0_255_tree(0, 255);   // for pivot shifts

void _cubemap::toCube(float3 _v)
{
    float3 vAbs = float3(fabs(_v.x), fabs(_v.y), fabs(_v.z));

    if ((vAbs.x > vAbs.y) && (vAbs.x > vAbs.z))
    {
        face = (_v.x > 0) ? 0 : 1;
        _v /= vAbs.x;
        dx = _v.z * cubeHalfSize + cubeHalfSize + 1.f;
        dy = _v.y * cubeHalfSize + cubeHalfSize + 1.f;
    }
    else if (vAbs.y > vAbs.z)
    {
        face = (_v.y > 0) ? 2 : 3;
        _v /= vAbs.y;
        dx = _v.x * cubeHalfSize + cubeHalfSize + 1.f;
        dy = _v.z * cubeHalfSize + cubeHalfSize + 1.f;
    }
    else
    {
        face = (_v.z > 0) ? 4 : 5;
        _v /= vAbs.z;
        dx = _v.x * cubeHalfSize + cubeHalfSize + 1.f;
        dy = _v.y * cubeHalfSize + cubeHalfSize + 1.f;
    }

    x = (int)floor(dx);
    y = (int)floor(dy);
    dx -= x;
    dy -= y;        // not nie reg nie ek wil middelpunte van pixels he
}


float3 _cubemap::toVec(int face, int y, int x)
{
    float3 V(0.f);
    float S = cubeHalfSize + 1.f;
    float SS = (float)cubeHalfSize;
    switch (face)
    {
    case 0:
        V.x = 1;
        V.z = (x - S) / SS;
        V.y = (y - S) / SS;
        break;
    case 1:
        V.x = -1;
        V.z = (x - S) / SS;
        V.y = (y - S) / SS;
        break;
    case 2:
        V.y = 1;
        V.x = (x - S) / SS;
        V.z = (y - S) / SS;
        break;
    case 3:
        V.y = -1;
        V.x = (x - S) / SS;
        V.z = (y - S) / SS;
        break;
    case 4:
        V.z = 1;
        V.x = (x - S) / SS;
        V.y = (y - S) / SS;
        break;
    case 5:
        V.z = -1;
        V.x = (x - S) / SS;
        V.y = (y - S) / SS;
        break;
    }

    return glm::normalize(V);
}

/*
float3 _cubemap::toVec(glm::int3 c)
{
} */


float _cubemap::sampleDistance(float3 _v)
{
    toCube(_v);
    return data[face][y][x].d;
}


void _cubemap::clear()
{
    for (int f = 0; f < 6; f++)
    {
        for (int yy = 0; yy < cubeHalfSize * 2 + 2; yy++)
        {
            for (int xx = 0; xx < cubeHalfSize * 2 + 2; xx++)
            {
                data[f][yy][xx].d = 0;
                data[f][yy][xx].cone = 0;
                data[f][yy][xx].sum = 0;
                data[f][yy][xx].dir = float3(0, 0, 0);
            }
        }
    }
}

void _cubemap::writeDistance(float3 _v)     // fixme alpha
{
    glm::vec3 V = glm::normalize((_v - center) * scale);

    toCube(V);
    data[face][y][x].d = __max(data[face][y][x].d, glm::length(_v - center));
}


// net later vir smooth lookup
void _cubemap::solveEdges()
{
    float max = 0;
    for (int f = 0; f < 6; f++)
    {
        for (int yy = 0; yy < cubeHalfSize * 2 + 2; yy++)
        {
            for (int xx = 0; xx < cubeHalfSize * 2 + 2; xx++)
            {
                max = __max(max, data[f][yy][xx].d);
            }
        }
    }

    for (int f = 0; f < 6; f++)
    {
        for (int yy = 0; yy < cubeHalfSize * 2 + 2; yy++)
        {
            for (int xx = 0; xx < cubeHalfSize * 2 + 2; xx++)
            {
                data[f][yy][xx].d = __max(data[f][yy][xx].d, max * 0.5f);
            }
        }
    }



    for (int i = 1; i < cubeHalfSize * 2 + 1; i++)
    {
        //data[0][0][i] = data[3][0][i];
    }
}

void _cubemap::solve()
{
    for (int f = 0; f < 6; f++)
    {
        for (int yy = 1; yy < cubeHalfSize * 2 + 1; yy++)
        {
            for (int xx = 1; xx < cubeHalfSize * 2 + 1; xx++)
            {
                if (xx == 8 && yy == 8)
                {
                    bool bCM = true;
                    (void)bCM;
                }
                float3 V = toVec(f, yy, xx);
                float3 U = float3(0, 1, 0);
                if ((face == 2) || (face == 3)) {
                    U = float3(0, 0, 1);
                }

                float3 R = glm::normalize(glm::cross(U, V));
                U = glm::cross(V, R);

                float scale2 = 0.4f; // about 30 degrees
                float3 v1 = V - U * scale2;
                float3 v2 = V + (U * scale2 * 0.5f) + (R * scale2 * 0.8616f);
                float3 v3 = V + (U * scale2 * 0.5f) - (R * scale2 * 0.8616f);

                float d1 = sampleDistance(v1);
                float d2 = sampleDistance(v2);
                float d3 = sampleDistance(v3);
                v1 *= d1;
                v2 *= d2;
                v3 *= d3;

                float3 middle = (v1 + v2 + v3) * 0.333333f;
                float CONE = (data[f][yy][xx].d - glm::length(middle)) / data[f][yy][xx].d;

                data[f][yy][xx].dir = glm::normalize(glm::cross(v2 - v1, v3 - v1));
                data[f][yy][xx].cone = CONE;
            }
        }
    }
}


float4 _cubemap::light(float3 _p, float* _depth)
{
    float3 virtualCenter = center;
    if (_p.y < center.y)
    {
        _p.y = glm::lerp(_p.y, center.y, 0.5f);
    }

    toCube(_p - virtualCenter);
    *_depth = data[face][y][x].d - glm::length(_p - virtualCenter);
    return float4(data[face][y][x].dir, data[face][y][x].cone);
}







void _treeBuilder::loadPath()
{
    if (std::filesystem::exists(terrafectorEditorMaterial::rootFolder + path))
    {
        try
        {
            std::ifstream is(terrafectorEditorMaterial::rootFolder + path);
            cereal::JSONInputArchive archive(is);
            archive(*this);
            changed = false;
        }
        catch (const std::exception& e)
        {
            spdlog::error("vegetation: _treeBuilder::loadPath failed to parse '{}' - {}", path, e.what());
        }
    }
    else
    {
        spdlog::error("vegetation: File does not exists in the relative tree structure - {}", path);
    }
}


void _treeBuilder::savePath()
{
    std::ofstream os(terrafectorEditorMaterial::rootFolder + path);
    cereal::JSONOutputArchive archive(os);
    archive(*this);
    changed = false;
}









void _treeBuilder::clear_build_info()
{
    numInstancePacked = 0;
    numVertsPacked = 0;
    debugnumPivots = 0;
    /*
    for (auto& L : branches.branchData)
    {
        if (L.plantPtr)  L.plantPtr->clear_build_info();
    } */
}


float2 _treeBuilder::calculate_extents(glm::mat4 view)
{
    float2 extents = _ribbonBuilder.calculate_extents(view);

    std::filesystem::path full_path = path;

    // lod 0
    float4 dd0 = glm::saturate(_ribbonBuilder.dU / lod_bakeInfo[0].bakeWidth);
    lod_bakeInfo[0].extents = extents;
    lod_bakeInfo[0].dU[0] = dd0[0];
    lod_bakeInfo[0].dU[1] = dd0[1];
    lod_bakeInfo[0].dU[2] = dd0[2];
    lod_bakeInfo[0].dU[3] = dd0[3];
    lod_bakeInfo[0].material.name = "bake_0";
    lod_bakeInfo[0].material.path = full_path.parent_path().string() + "\\bake_" + full_path.stem().string() + "\\" + lod_bakeInfo[0].material.name + ".vegetationMaterial";;

    // lod 1
    dd0 = glm::saturate(_ribbonBuilder.dU / lod_bakeInfo[1].bakeWidth);
    lod_bakeInfo[1].extents = extents;
    lod_bakeInfo[1].dU[0] = dd0[0];
    lod_bakeInfo[1].dU[1] = dd0[1];
    lod_bakeInfo[1].dU[2] = dd0[2];
    lod_bakeInfo[1].dU[3] = dd0[3];
    lod_bakeInfo[1].material.name = "bake_1";
    lod_bakeInfo[1].material.path = full_path.parent_path().string() + "\\bake_" + full_path.stem().string() + "\\" + lod_bakeInfo[1].material.name + ".vegetationMaterial";;

    // lod 2
    dd0 = glm::saturate(_ribbonBuilder.dU / lod_bakeInfo[2].bakeWidth);
    lod_bakeInfo[2].extents = extents;
    lod_bakeInfo[2].dU[0] = dd0[0];
    lod_bakeInfo[2].dU[1] = dd0[1];
    lod_bakeInfo[2].dU[2] = dd0[2];
    lod_bakeInfo[2].dU[3] = dd0[3];
    lod_bakeInfo[2].material.name = "bake_2";
    lod_bakeInfo[2].material.path = full_path.parent_path().string() + "\\bake_" + full_path.stem().string() + "\\" + lod_bakeInfo[2].material.name + ".vegetationMaterial";;

    return extents;
}


glm::mat4 _treeBuilder::getTip(bool includeChildren)
{
    (void)includeChildren;
    //if (NODES.size() > 0)       return NODES.back();    // since its only direction test with this
    return (glm::mat4(1.f));
}


glm::mat4 _treeBuilder::build_2(buildSetting _settings, lodBake* pBake, bool _faceCamera, bool _diamond)
{
    (void)_settings; (void)pBake; (void)_faceCamera; (void)_diamond;
    return (glm::mat4(1.f));
}


glm::mat4 _treeBuilder::build_4(buildSetting _settings, lodBake* pBake, bool _faceCamera)
{
    (void)_settings; (void)pBake; (void)_faceCamera;
    return (glm::mat4(1.f));
}

// loads and builds one brach = shall I nor ratgher just keep them in memory
void _treeBuilder::build_BRANCH(uint _idx, buildSetting _settings, bool bottom)
{
    (void)bottom;
    int material_idx = branch_Material.index;
    glm::mat4 N(1.f);
    auto& BRANCH = tempActualBranches[_idx];

    for (uint i = 0; i < BRANCH.numPivots; i++)
    {
        _ribbonBuilder.pushPivot(i, BRANCH.pivots[i]);
    }

    for (auto& B : BRANCH.branches)
    {
        //_ribbonBuilder.startRibbon(true, B.pivots);
        _ribbonBuilder.startRibbon(true, {255, 255, 255, 255});
        float v = 0.0f;
        auto prev = B.nodes.front();
        bool useThisOne = true;

        for (auto& node : B.nodes)
        {
            v += glm::length(node.pos - prev.pos) / node.radius * 0.1f;
            useThisOne = true;

            if (useThisOne)
            {
                N = glm::mat4(1.f);

                //This should just be in node
                float3 Nr = glm::normalize(glm::cross(float3(1, 0, 0), node.dir));
                float3 T = glm::normalize(glm::cross(node.dir, Nr));

                N[0].x = T.x; // tangent
                N[0].y = T.y;
                N[0].z = T.z;

                N[1].x = node.dir.x; // bitangent
                N[1].y = node.dir.y;
                N[1].z = node.dir.z;

                N[2].x = Nr.x;
                N[2].y = Nr.y;
                N[2].z = Nr.z;

                N[3].x = node.pos.x;
                N[3].y = node.pos.y;
                N[3].z = node.pos.z;

                float W = node.radius;
                _ribbonBuilder.set(N, W, material_idx, float2(1.f, 0.f), 1.f, 1.f, true, 1, 1);
                useThisOne = false;
            }

            {
                float stepThis = stepFactor * pow((endRadius / node.radius), 0.3f);/// 0.2 IS TE ERG
                useThisOne = (node.radius < startRadius) && (node.radius > endRadius) &&
                                (glm::length(node.pos - (float3)N[3]) / node.radius > stepThis);
            }

            prev = node;
        }
    }

    // TWIGS
    for (auto& B : BRANCH.branches)
    {
        auto& node = B.nodes.back();
        N = glm::mat4(1.f);

        //This should just be in node
        float3 Nr = glm::normalize(glm::cross(float3(1, 0, 0), node.dir));
        float3 T = glm::normalize(glm::cross(node.dir, Nr));

        N[0].x = T.x; // tangent
        N[0].y = T.y;
        N[0].z = T.z;

        N[1].x = node.dir.x; // bitangent
        N[1].y = node.dir.y;
        N[1].z = node.dir.z;

        N[2].x = Nr.x;
        N[2].y = Nr.y;
        N[2].z = Nr.z;

        N[3].x = node.pos.x;
        N[3].y = node.pos.y;
        N[3].z = node.pos.z;

        _plantRND t = twigs.get();
        _settings.root = N;
        _settings.doNotAddPivot = true;
        if (t.plantPtr) t.plantPtr->build(_settings, true);
    }
}

void _treeBuilder::build_one_branch(uint _root, uint _idx, buildSetting _settings, bool bottom)
{
    (void)_settings;
    auto& branch = branches[_idx];
    int material_idx = branch_Material.index;



    if (branch.isVisible && branch.nodes.size() > 1)
        //if (branchCount <= 0)
    {
        _ribbonBuilder.startRibbon(true, _settings.pivotIndex);
        float v = 0.0f;
        auto prev = branch.nodes.front();
        bool useThisOne = true;
        uint node_count = 0;

        uint START = 0;
        if (!bottom) START = branch.start_node;
        uint STOP = (uint)branch.nodes.size();
        if (bottom) STOP = __min((uint)branch.nodes.size(), branch.start_node + 1);


        if (_idx != _root)
        {
            START = 0;
            STOP = (uint)branch.nodes.size();
        }

        glm::mat4 N(1.f);

        //for (auto& node : branch.nodes)
        for (uint k = START; k < STOP; k++)
        {
            auto& node = branch.nodes[k];
            v += glm::length(node.pos - prev.pos) / node.radius * 0.1f;
            //bool first = (prev.pos == node.pos);

            useThisOne = true;
            if (useThisOne)
            {
                N = glm::mat4(1.f);

                //This should just be in node
                float3 Nr = glm::normalize(glm::cross(float3(1, 0, 0), node.dir));
                float3 T = glm::normalize(glm::cross(node.dir, Nr));

                N[0].x = T.x; // tangent
                N[0].y = T.y;
                N[0].z = T.z;

                N[1].x = node.dir.x; // bitangent
                N[1].y = node.dir.y;
                N[1].z = node.dir.z;

                N[2].x = Nr.x;
                N[2].y = Nr.y;
                N[2].z = Nr.z;

                N[3].x = node.pos.x;
                N[3].y = node.pos.y;
                N[3].z = node.pos.z;

                float W = node.radius;
                if (showBranchSplitWide && (k == START))
                {
                    W *= 5;
                }
                _ribbonBuilder.set(N, W, material_idx, float2(1.f, 0.f), 1.f, 1.f, true, 1, 1);

                useThisOne = false;
            }


            // now see if we are goingt to keep it or overwrite it

            {
                float stepThis = stepFactor * pow((endRadius / node.radius), 0.3f);/// 0.2 IS TE ERG

                if ((node.radius < startRadius) &&
                    (node.radius > endRadius) &&
                    (glm::length(node.pos - (float3)N[3]) / node.radius > stepThis))
                {
                    useThisOne = true;
                }
            }

            prev = node;
            node_count++;
        }
    }
}

glm::mat4 _treeBuilder::build(buildSetting _settings, bool _addVerts, bool _extents)
{
    (void)_addVerts; (void)_extents;
    if (branches.size() == 0) return (glm::mat4(1.f));
    packSettings vertex_pack_Settings;
    vertex_pack_Settings.objectSize = 30.f;
    vertex_pack_Settings.radiusScale = 1.5f;
    _ribbonBuilder.setup(vertex_pack_Settings.getScale(), vertex_pack_Settings.radiusScale, vertex_pack_Settings.getOffset());



    // add the branches
    uint branchCount = 0;
    (void)branchCount;

    if (drawAllDropped)
    {
        for (int i = 0; i < (int)allDroppedTwigs.size(); i++)
        {
            build_one_branch(allDroppedTwigs[i], allDroppedTwigs[i], _settings, false);
        }
    }
    else
    {
        if (drawRoot)
        {
            if (drawOnlyOne)
            {
                build_one_branch(sorted[theOneToDraw].index, sorted[theOneToDraw].index, _settings, true);
            }
            else
            {
                for (int i = 0; i < numBranches; i++)
                {
                    build_one_branch(sorted[i].index, sorted[i].index, _settings, true);
                    branchCount++;
                }
            }
        }

        if (drawBranches)
        {
            if (drawOnlyOne)
            {
                build_BRANCH(theOneToDraw, _settings, false);
            }
            else
            {
                for (int i = 0; i < numBranches; i++)
                {
                    auto& B = branches[sorted[i].index];

                    for (int j = 0; j < (int)B.all_twigs.size(); j++)
                    {
                        build_one_branch(sorted[i].index, B.all_twigs[j], _settings, false);
                    }
                }
            }
        }
    }



    return (glm::mat4(1.f));
}

// Not implemented: the file-dialog picker is an editor flow. The explicit-path
// overload below carries the whole Grove OBJ import.
void _treeBuilder::loadFromFile()
{
    spdlog::error("vegetation: _treeBuilder::loadFromFile - the file dialog is not implemented; call loadFromFile(path) with a Grove .obj instead");
}

void _treeBuilder::loadFromFile(const std::filesystem::path& filepath)
{
    if (!std::filesystem::exists(filepath))
    {
        spdlog::error("vegetation: _treeBuilder::loadFromFile - '{}' not found", filepath.string());
        return;
    }
    path = materialCache::getRelative(filepath.string());
    name = filepath.filename().string().substr(0, filepath.filename().string().length() - 4);
    objfile = fopen(filepath.string().c_str(), "r");
    if (!objfile)
    {
        spdlog::error("vegetation: _treeBuilder::loadFromFile - cannot open '{}'", filepath.string());
        return;
    }
    load_obj();
}


float3 _treeBuilder::readVertex()
{
    float3 v(0.f);
    char type[256];
    int ret = fscanf(objfile, "%255s %f %f %f\n", type, &v.x, &v.y, &v.z);
    if (ret < 4) {
        enfOfFile = true;
    }
    else {
        enfOfFile = false;
    }
    totalVerts++;
    return v;
}

void _treeBuilder::readahead1()
{
    verts[numVerts] = readVertex();
    float offset = glm::dot(nodeDir, verts[numVerts]) - nodeOffset;
    if (fabs(offset) > 0.003) {
        isPlanar = false;
    }
    numVerts++;
}


void _treeBuilder::read2()
{
    verts[1] = readVertex();
    verts[2] = readVertex();
    numVerts = 3;
    nodeDir = glm::normalize(glm::cross(verts[1] - verts[0], verts[2] - verts[0]));
    nodeOffset = glm::dot(nodeDir, verts[0]);
    isPlanar = true;
}


void _treeBuilder::readHeader()
{
    enfOfFile = false;
    char header[256];
    int ret = 0;
    ret = fscanf(objfile, "%39[^\n]\n", header); (void)ret;
    ret = fscanf(objfile, "%39[^\n]\n", header); (void)ret;
    ret = fscanf(objfile, "%39[^\n]\n", header); (void)ret;

    verts[0] = readVertex();
    numVerts = 1;
    branchMode = true;
}


void _treeBuilder::testBranchLeaves()
{
    float3 center = (verts[0] + verts[1] + verts[2]) / 3.0f;
    for (int j = 1; j < (int)currentBranch->nodes.size(); j++) {         // start at 1, dont seacr that first node, it breaks branch inetrsections
        float3 nodeCenter = currentBranch->nodes[j].pos;
        float dist = glm::length(nodeCenter - center);
        if (fabs(dist) < 0.004f) {
            //we have a leaf, add it in future
            float l1 = glm::length(verts[1] - verts[0]);
            float l2 = glm::length(verts[2] - verts[1]);
            float l3 = glm::length(verts[0] - verts[2]);
            float radius = 0.19f * (l1 + l2 + l3);
            (void)radius;

            _leafNode L;
            L.pos = currentBranch->nodes[j].pos;
            L.dir = nodeDir;
            L.branchNode = j;
            L.branchIndex = (int)branches.size() - 1;
            branchLeaves.push_back(L);
            currentBranch->leaves.push_back(L);

            verts[0] = readVertex();
            read2();
            return;
        }
    }

    // we dont, this is the start of the next branch, and we have 3 already
    branchMode = true;
    branches.emplace_back();
    currentBranch = &branches.back();
    oldNumVerts = 1000;
}


bool pointCylindar(const glm::vec3& point, const glm::vec3& A, const glm::vec3& B, const float radius)
{
    glm::vec3 dir = B - A;
    float L = glm::length(dir);
    dir = glm::normalize(dir);
    glm::vec3 P = point - A;
    float dist = glm::dot(P, dir);
    if ((dist >= -radius) && (dist < (L + radius))) {
        glm::vec3 Pline = dir * dist;
        if (glm::length(P - Pline) < radius) {
            return true;
        }
    }

    return false;
}

void _branch::findParentBranches()
{
    glm::vec3 P_0;
    float R_0;

    for (auto& B : branches)
    {
        B.parentIndex = -1;
        B.parentNode = 0;
    }

    for (int i = 0; i < (int)branches.size(); i++)
    {
        auto& A = branches[i];
        if (A.parentIndex >= 0) continue; // have already found it

        P_0 = A.nodes[0].pos;
        R_0 = A.nodes[0].radius;

        for (int j = i - 1; j >= 0; j--)   // soek vorentoe
        {
            auto& B = branches[j];

            if (B.nodes[0].radius > R_0)    //dis n groter tak
            {
                for (int n = 0; n < (int)B.nodes.size() - 1; n++)
                {
                    if (pointCylindar(P_0, B.nodes[n].pos, B.nodes[n + 1].pos, B.nodes[n].radius * 2))
                    {
                        A.parentIndex = j;
                        A.parentNode = n;

                        // Walk the parent chain by index. A reference cannot be
                        // re-seated in C++, so `P = branches[P.parentIndex]`
                        // would copy-assign over the branch instead of moving
                        // to it.
                        A.pivotDepth = 0;
                        int walk = i;
                        do
                        {
                            A.pivotDepth++;
                            walk = branches[walk].parentIndex;
                        } while (walk >= 0 && branches[walk].parentIndex >= 0);
                    }
                }
            }
        }
    }
}

void _branch::sumLeaves()
{
    for (auto& B : branches)
    {
        B.numLeaves = 0;
        B.leavesAVS = float3(0, 0, 0);
        B.extents = float3(0, 0, 0);
        B.leavesFurthest = 0;
    }

    for (auto& B : branches)
    {
        // Index walk up the parent chain, for the same reason as in
        // findParentBranches.
        float3 leafPos = B.nodes.back().pos;    //FIXME dead
        int walk = (int)(&B - branches.data());
        do
        {
            auto& P = branches[walk];
            P.numLeaves++;
            P.leavesAVS += leafPos;
            P.leavesFurthest = __max(P.leavesFurthest, glm::length(leafPos - B.nodes[0].pos));

            walk = P.parentIndex;
        } while (walk >= 0);
    }

    for (auto& B : branches)
    {
        if (B.numLeaves > 1)
        {
            B.leavesAVS /= (float)B.numLeaves;
            B.extents = glm::normalize(B.leavesAVS) * B.leavesFurthest;
        }
    }
}

// FIXME add a start pivot depth
void _branch::generatePivots()
{
    numPivots = 0;

    for (auto& B : branches)
    {
        if ((B.pivotDepth < 4) && (numPivots < 255))
        {
            B.tempPivot = numPivots;
            auto& PVT = pivots[numPivots];

            PVT.root = B.nodes[0].pos;

            PVT.stiffness = 3.0f;           // FIXME  NumLeaves shopuld influence both of these
            PVT.frequency = 1.0f;
            PVT.extent = B.extents;
            PVT.offset = DD_0_255_tree(_rootPlant::generator);
            PVT.shift = 0;
            PVT.padd1 = 0;
            PVT.padd2 = 0;

            numPivots++;
        }
    }
}

void _branch::propagatePivots()
{
    for (auto& B : branches)
    {
        if (B.parentIndex >= 0)
        {
            B.pivots = branches[B.parentIndex].pivots;

            if (B.pivotDepth < 4)
            {
                B.pivots[B.pivotDepth] = B.tempPivot;
            }
        }
    }
}

void _treeBuilder::findSideBranches()
{
    numSideBranchesFound = 0;
    for (int B = 0; B < (int)branches.size(); B++)
    {
        glm::vec3 P0 = branches[B].nodes[0].pos;
        float R0 = branches[B].nodes[0].radius;
        for (int C = B - 1; C >= 0; C--)   // try <B again
        {
            if (branches[B].rootBranch >= 0) continue; // have al;ready found it

            if (branches[C].nodes[0].radius > R0)
            {
                for (int n = 0; n < (int)branches[C].nodes.size() - 1; n++)
                {
                    //if (branches[C].nodes[n].radius < (R0 * 0.6)) break;

                    if (pointCylindar(P0, branches[C].nodes[n].pos, branches[C].nodes[n + 1].pos, branches[C].nodes[n].radius * 4))
                    {
                        branches[B].rootBranch = C;
                        branches[B].sideNode = n;
                        if (branches[C].isDead)
                        {
                            branches[B].isDead = true;
                        }
                        branches[C].sideBranches.push_back(B);
                        numSideBranchesFound++;
                        break;
                    }
                }
            }
        }
    }

    for (int B = 0; B < (int)branches.size(); B++)
    {
        if (!branches[B].isDead)
        {
            endLeaves.emplace_back();
            endLeaves.back().pos = branches[B].nodes.back().pos;
            endLeaves.back().dir = branches[B].nodes.back().dir;
            endLeaves.back().branchIndex = B;
        }
    }
}


void _treeBuilder::propagateDead(int root)
{
    (void)root;
}


void _treeBuilder::load_obj()
{
    branches.clear();
    branchLeaves.clear();
    endLeaves.clear();
    totalVerts = 0;
    numDeadEnds = 0;
    numBadEnds = 0;

    readHeader();
    branches.emplace_back();
    currentBranch = &branches.back();
    read2();
    oldNumVerts = 1000; // just bog to alwasy keep first node
    while (!enfOfFile)
    {
        if (branchMode)
        {
            while (isPlanar) {
                readahead1();
            }

            // solve vert and save in branch
            float3 center = float3(0, 0, 0);
            for (uint j = 0; j < numVerts - 1; j++) {
                center += verts[j] * (1.0f / (numVerts - 1));
            }

            float l1 = glm::length(verts[1] - verts[0]);
            float l2 = glm::length(verts[2] - verts[1]);
            float l3 = glm::length(verts[0] - verts[2]);
            float radius = 0.19f * (l1 + l2 + l3);

            if (oldNumVerts < (int)numVerts) {
                if ((numVerts == 8) && (!currentBranch->isDead)) {
                    // so when the verts increase we have hit the endcap of a branch
                    /*
                    endLeaves.emplace_back();
                    endLeaves.back().pos = center;
                    endLeaves.back().dir = nodeDir;
                    */
                }
                else if (numVerts == 5) {
                    numDeadEnds++;
                }
                else {
                    numBadEnds++;
                }
                branchMode = false;
            }
            oldNumVerts = numVerts;

            //if(branches.size()==1 && currentBranch->nodes)
            if (currentBranch->nodes.size() == 1)
            {
                currentBranch->nodes[0].radius = radius;
            }
            _branchnode BN;
            BN.pos = center;
            BN.radius = radius;
            BN.dir = nodeDir;
            currentBranch->nodes.push_back(BN);


            static bool testOnce = true;
            if (currentBranch->nodes.size() == 1)
            {
                if (testOnce && (currentBranch->nodes.size() == 1) && (radius > 0.3))   // catch first branch
                {
                    testOnce = false;
                    currentBranch->isVisible = true;
                }
                else
                {
                    //currentBranch->isVisible = false;
                }
            }

            if ((fabs(center.x) > 2) || (center.z < -2))
            {
                //currentBranch->isVisible = false;
            }


            //if ((center.z > -2) || (abs(center.x) > 2))
            if ((center.z < -2) || (fabs(center.x) < 2))
            {
                //currentBranch->isVisible = false;
            }

            if (center.y > 9)
            {
                //    currentBranch->isVisible = false;
            }

            /*
            if (branches.size() > 30 && branches.size() <  1830)
            {
                currentBranch->isDead = true;
            }
            */


            verts[0] = verts[numVerts - 1];
            read2();
        }
        else
        {
            testBranchLeaves();
        }

    }

    fclose(objfile);

    findSideBranches();
    disableFloating();
    countLeavesEtc();
    calcSubTwigs();
    propagateDead(3);
    calcLight();
    //rebuildRibbons();
}



void _treeBuilder::disableFloating()
{
    numFloating = 0;
    for (auto& B : branches)
    {
        if (B.rootBranch == -1)
        {
            numFloating++;
            B.isVisible = false;
        }
    }
    branches.front().isVisible = true;  //reinstate first
}

void _treeBuilder::countLeavesEtc()
{
    for (auto& B : branches)
    {
        if (B.isVisible)
        {
            float3 tip = B.nodes.back().pos;



            B.branchDepth = 0;
            int root = B.rootBranch;
            while (root >= 0)
            {
                if (!B.isDead)
                {
                    branches[root].numLeaves++;
                    branches[root].leavesAvsPosition += tip;
                }
                // en doien die takkie diktes ook
                //sumOfSticks


                root = branches[root].rootBranch;
                B.branchDepth++;
            }
        }
    }

    uint count = 0;
    sorted.clear();
    for (auto& B : branches)
    {
        if (B.numLeaves > 0)
        {
            B.leavesAvsPosition *= (1.f / (float)B.numLeaves);
        }
        if (B.isVisible && B.nodes.size())
        {
            sorted.emplace_back();
            sorted.back().index = count;
            sorted.back().radius = B.nodes[0].radius;
        }
        count++;
    }



    std::sort(sorted.begin(), sorted.end());
}



bool _treeBuilder::testBranch(uint _root, uint _idx, int* _bottomIndex)
{
    if (_idx == _root)
    {
        return true;
    }

    for (int i = 0; i < numBranches; i++)
    {
        if (_idx == sorted[i].index) return false; // we hit a main branch going down, not ourselves
    }

    if (branches[_idx].rootBranch >= 0)
    {
        *_bottomIndex = _idx;   //this is the lowest we went
        return testBranch(_root, branches[_idx].rootBranch, _bottomIndex);
    }

    return false;
}

void _treeBuilder::calcSubTwigs()
{
    avsLeavesInBranch = 0;
    float largestRadius = 0;
    /*
    for (int i = 0; i < numBranches; i++)
    {
        auto& B = branches[sorted[i].index];
        smallestRadius = __min(smallestRadius, B.nodes[0].radius);
        B.numLeavesSmall = 0;
    }
    smallestRadius *= 1.3f;
    */
    allDroppedTwigs.clear();

    for (int i = 0; i < numBranches; i++)
    {
        auto& B = branches[sorted[i].index];
        {
            B.start_node = (uint)B.nodes.size();
            B.all_twigs.clear();
            B.leavesAvsPosition = float3(0, 0, 0);

            /*
            for (int j = 0; j < B.nodes.size(); j++)
            {
                if (B.nodes[j].radius > smallestRadius) B.start_node = j;
            }
            if (B.start_node < 5) B.start_node = 0;
            */

            for (uint k = 0; k < branches.size(); k++)
            {
                int bottomIndex = -1;
                if (branches[k].isVisible && testBranch(sorted[i].index, k, &bottomIndex))
                {
                    bool isDropped = true;
                    if (sorted[i].index == k)
                    {
                        B.numLeavesSmall++;
                        B.leavesAvsPosition += branches[k].nodes.back().pos;
                        B.all_twigs.push_back(k);
                        isDropped = false;
                    }

                    if (!branches[k].isDead)
                    {
                        //if (branches[k].rootBranch == sorted[i].index && (bottomIndex >= 0))
                        if (bottomIndex >= 0)
                        {
                            // FIXME THISD IS ONYL IF it closer to the rootand puching that further
                            // ????? HOW NOW BROWN COW
                            float R0 = branches[bottomIndex].nodes[0].radius;
                            int insert = branches[bottomIndex].sideNode;
                            int parent = branches[bottomIndex].rootBranch;
                            float R1 = branches[parent].nodes[insert].radius;
                            if ((branches[bottomIndex].numLeaves >  10) ||
                                ((R0 / R1) > 0.3f))
                            {
                                B.start_node = __min(B.start_node, (uint)branches[bottomIndex].sideNode);
                                B.numLeavesSmall++;
                                B.leavesAvsPosition += branches[k].nodes.back().pos;
                                B.all_twigs.push_back(k);
                                largestRadius = __max(largestRadius, branches[k].nodes[0].radius);
                                isDropped = false;
                            }
                        }
                    }
                    if (isDropped) allDroppedTwigs.push_back(k);
                }
            }
            B.deadRootBranch = B.all_twigs.size() < 10;
            if (B.numLeavesSmall > 1)
            {
                B.leavesAvsPosition /= B.numLeavesSmall;
                float3 VECTOR = B.leavesAvsPosition - B.nodes[B.start_node].pos;
                B.LENGTH = glm::length(VECTOR);
                VECTOR = glm::normalize(VECTOR);
                B.PITCH = asin(VECTOR.y) * 57.f;
            }
        }
        avsLeavesInBranch += (float)B.all_twigs.size();
    }
    avsLeavesInBranch /= numBranches;

    int cnt = 0;
    for (int i = 0; i < numBranches; i++)
    {
        auto& B = branches[sorted[i].index];
        if ((float)B.all_twigs.size() > avsLeavesInBranch)
        {
            avsLeavesInBranch_High += (float)B.all_twigs.size();
            cnt++;
        }

        for (uint n = 0; n < B.nodes.size(); n++)
        {
            if (B.nodes[n].radius < largestRadius && (n < B.start_node))
            {
                B.start_node = n;
            }
        }
    }
    avsLeavesInBranch_High /= cnt;

    for (int i = 0; i < numBranches; i++)
    {
        auto& B = branches[sorted[i].index];
        if (B.start_node < 5)
        {
            B.start_node = 0;
        }
        else
        {
            B.start_node -= 2;
        }

    }
}

void _treeBuilder::calcLight()
{
    // center
    light.center = float3(0, 0, 0);
    light.Min = float3(10000, 10000, 10000);
    light.Max = float3(-10000, -10000, -10000);
    int cnt = (int)endLeaves.size();
    for (int b = 0; b < cnt; b++)
    {
        light.center += endLeaves[b].pos;
        light.Min = glm::min(light.Min, endLeaves[b].pos);
        light.Max = glm::max(light.Max, endLeaves[b].pos);
    }
    if (cnt > 0)
    {
        light.center /= cnt;
    }
    glm::vec3 extents = light.Max - light.Min;
    light.scale = 2.0f / extents;

    light.cubemap.clear();
    light.cubemap.center = light.center;
    light.cubemap.scale = light.scale;
    light.cubemap.twigOffset = 0.1f;


    for (int b = 0; b < cnt; b++)
    {
        light.cubemap.writeDistance(endLeaves[b].pos + endLeaves[b].dir * 0.5f);
    }

    for (int b = 0; b < (int)branchLeaves.size(); b++)
    {
        light.cubemap.writeDistance(branchLeaves[b].pos + branchLeaves[b].dir * 0.2f);
    }

    light.cubemap.solveEdges();
    light.cubemap.solve();
}


float3 YAW_VEC(float3 vector, float angle)
{
    glm::mat4 M_YAW = glm::rotate(glm::mat4(1.f), angle, glm::vec3(0, 1, 0));
    float3 A = glm::vec4(vector, 0) * M_YAW;
    return A;
}

void _treeBuilder::buildTreeRootAndBranches()
{
    myBranchCollection.branches.clear();
    tempActualBranches.clear();


    std::filesystem::path full_path = terrafectorEditorMaterial::rootFolder + path;
    full_path.remove_filename();

    for (int i = 0; i < (int)sorted.size(); i++)
    {
        if (i < numBranches)
        {
            auto& B = branches[sorted[i].index];
            if (!B.deadRootBranch)
            {
                _branch BRANCH;

                BRANCH.numPivots = 0;

                float3 rootPosition = B.nodes[B.start_node].pos;
                float3 rootAvsLeaves = B.leavesAvsPosition - rootPosition;
                float rootYaw = -atan2(rootAvsLeaves.z, rootAvsLeaves.x);

                float wmax = -100000000;
                float hmax = -100000000;
                float wmin = 100000000;
                float hmin = 100000000;
                for (int j = 0; j < (int)B.all_twigs.size(); j++)
                {
                    auto& thisBranch = branches[B.all_twigs[j]];
                    BRANCH.branches.emplace_back();
                    auto& newB = BRANCH.branches.back();

                    uint START = thisBranch.start_node;
                    uint STOP = (uint)thisBranch.nodes.size();
                    for (uint k = START; k < STOP; k++)
                    {
                        _minimalNode node;
                        node.pos = YAW_VEC(thisBranch.nodes[k].pos - rootPosition, rootYaw);
                        node.radius = thisBranch.nodes[k].radius;
                        node.dir = YAW_VEC(thisBranch.nodes[k].dir, rootYaw);
                        newB.nodes.push_back(node);

                        wmin = __min(wmin, node.pos.z);
                        wmax = __max(wmax, node.pos.z);

                        hmin = __min(hmin, node.pos.y);
                        hmax = __max(hmax, node.pos.y);
                    }
                }

                // now search for parent
                BRANCH.findParentBranches();
                BRANCH.sumLeaves();
                BRANCH.generatePivots();
                BRANCH.propagatePivots();

                BRANCH.stats.rootPitch = acos(B.nodes[B.start_node].dir.y);
                BRANCH.stats.rootRadius = B.nodes[B.start_node].radius;

                BRANCH.stats.leavesDistance = glm::length(rootAvsLeaves);
                rootAvsLeaves = glm::normalize(rootAvsLeaves);
                BRANCH.stats.leavesPitch = acos(rootAvsLeaves.y);
                BRANCH.stats.numLeaves = B.numLeavesSmall;
                BRANCH.stats.height = hmax - hmin;
                BRANCH.stats.width = wmax - wmin;

                static int cnt = 0;
                BRANCH.stats.name = "branch_" + std::to_string(cnt) + "_" + std::to_string(BRANCH.stats.numLeaves) + ".treeBranch";
                cnt++;

                myBranchCollection.branches.push_back(BRANCH.stats);
                tempActualBranches.push_back(BRANCH);


                std::string branchName = full_path.string() + "\\branches\\" + BRANCH.stats.name;
                std::ofstream stream(branchName);
                if (stream.good()) {
                    cereal::JSONOutputArchive archive(stream);
                    archive(BRANCH);
                }
            }
        }
    }

    {
        std::filesystem::path collection_path = terrafectorEditorMaterial::rootFolder + path;
        collection_path.remove_filename();
        collection_path += "\\branch.collection";

        std::ofstream stream(collection_path.string());
        if (stream.good()) {
            cereal::JSONOutputArchive archive(stream);
            archive(myBranchCollection);
        }
    }

    myTreeRoot.branches.clear();
    std::string treeName = full_path.string() + ".rootTree";
    std::ofstream stream(treeName);
    if (stream.good()) {
        cereal::JSONOutputArchive archive(stream);
        archive(myTreeRoot);
    }
}






float _branchCollection::compare(_branchStats _stats, _branchStats* closest)
{
    float lowest = 100000000;
    for (auto& B : branches)
    {
        float score = 1.f;
        score *= 1.f - glm::clamp(fabs(_stats.leavesPitch - B.leavesPitch) / 30.f, 0.f, 1.f);    // 30 grade score zero

        float leafScore = (float)_stats.numLeaves / (float)B.numLeaves;
        if (leafScore > 1.f) leafScore = 1.f / leafScore;
        leafScore = pow(leafScore, 2.f);
        score *= leafScore;

        float distScore = _stats.leavesDistance / B.leavesDistance;
        if (distScore > 1.f) distScore = 1.f / distScore;
        distScore = pow(distScore, 2.f);
        score *= distScore;

        if (score < lowest)
        {
            closest = &B;
            lowest = score;
        }
    }
    return lowest;
}
