#include "vegetationBuilder.h"
#include "imgui.h"
#include "PerlinNoise.hpp"          //https://github.com/Reputeless/PerlinNoise/blob/master/PerlinNoise.hpp

using namespace std::chrono;
#pragma optimize("", off)

//extern bool anyChange;
#define TOOLTIP(x)  if (ImGui::IsItemHovered()) {ImGui::SetTooltip(x);}

//ImVec4 selected_color = ImVec4(0.4f, 0.1f, 0.0f, 1);
//ImVec4 stem_color = ImVec4(0.05f, 0.02f, 0.0f, 1);
//ImVec4 leaf_color = ImVec4(0.01f, 0.04f, 0.01f, 1);
//ImVec4 mat_color = ImVec4(0.0f, 0.01f, 0.07f, 1);

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
    float3 V;
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
        for (int y = 0; y < cubeHalfSize * 2 + 2; y++)
        {
            for (int x = 0; x < cubeHalfSize * 2 + 2; x++)
            {
                data[f][y][x].d = 0;
                data[f][y][x].cone = 0;
                data[f][y][x].sum = 0;
                data[f][y][x].dir = float3(0, 0, 0);
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
        for (int y = 0; y < cubeHalfSize * 2 + 2; y++)
        {
            for (int x = 0; x < cubeHalfSize * 2 + 2; x++)
            {
                max = __max(max, data[f][y][x].d);
            }
        }
    }

    for (int f = 0; f < 6; f++)
    {
        for (int y = 0; y < cubeHalfSize * 2 + 2; y++)
        {
            for (int x = 0; x < cubeHalfSize * 2 + 2; x++)
            {
                data[f][y][x].d = __max(data[f][y][x].d, max * 0.5f);
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
        for (int y = 1; y < cubeHalfSize * 2 + 1; y++)
        {
            for (int x = 1; x < cubeHalfSize * 2 + 1; x++)
            {
                if (x == 8 && y == 8)
                {
                    bool bCM = true;
                }
                float3 V = toVec(f, y, x);
                float3 U = float3(0, 1, 0);
                if ((face == 2) || (face == 3)) {
                    U = float3(0, 0, 1);
                }

                float3 R = normalize(cross(U, V));
                U = cross(V, R);

                float scale = 0.4f; // about 30 degrees
                float3 v1 = V - U * scale;
                float3 v2 = V + (U * scale * 0.5f) + (R * scale * 0.8616f);
                float3 v3 = V + (U * scale * 0.5f) - (R * scale * 0.8616f);

                float d1 = sampleDistance(v1);
                float d2 = sampleDistance(v2);
                float d3 = sampleDistance(v3);
                v1 *= d1;
                v2 *= d2;
                v3 *= d3;

                float3 middle = (v1 + v2 + v3) * 0.333333f;
                float CONE = (data[f][y][x].d - length(middle)) / data[f][y][x].d;

                data[f][y][x].dir = normalize(cross(v2 - v1, v3 - v1));
                data[f][y][x].cone = CONE;
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
    *_depth = data[face][y][x].d - length(_p - virtualCenter);
    return float4(data[face][y][x].dir, data[face][y][x].cone);
}







void _treeBuilder::loadPath()
{
    if (std::filesystem::exists(terrafectorEditorMaterial::rootFolder + path))
    {
        std::ifstream is(terrafectorEditorMaterial::rootFolder + path);
        cereal::JSONInputArchive archive(is);
        archive(*this);
        changed = false;
    }
    else
    {
        reportError(fmt::format("{}\nError: {}", "File does not exists in the relative tree structure", ""));
    }
}


void _treeBuilder::savePath()
{
    std::ofstream os(terrafectorEditorMaterial::rootFolder + path);
    cereal::JSONOutputArchive archive(os);
    archive(*this);
    changed = false;
}


bool _treeBuilder::renderGui()
{
    int flags = ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_OpenOnArrow;
    int flagsB = ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_OpenOnArrow;
    unsigned int gui_id = 1001;
    auto& style = ImGui::GetStyle();


    renderGuiHeader(filters);

    ImGui::Text("testing trees");


    ImGui::NewLine();
    style.Colors[ImGuiCol_Header] = ImVec4(0.01f, 0.01f, 0.01f, 1.f);
    if (ImGui::TreeNodeEx("light", flagsB))
    {
        ImGui::PushFont(_gui->getFont("italic"));
        {
            R_FLOAT("uv scale", shadowUVScale, 0.001f, 0.001f, 10.f, "");
            R_FLOAT("softness", shadowSoftness, 0.01f, 0.01f, 0.3f, "");
            R_FLOAT("depth", shadowDepth, 0.01f, 0.01f, 10.f, "");
            R_FLOAT("hgt", shadowPenetationHeight, 0.01f, 0.05f, 0.95f, "");
        }
        ImGui::PopFont();
        ImGui::TreePop();
    }


    ImGui::NewLine();
    style.Colors[ImGuiCol_Header] = ImVec4(0.01f, 0.01f, 0.01f, 1.f);
    if (ImGui::TreeNodeEx("animation", flags))
    {
        ImGui::SameLine(0, 50);
        float l = 1;
        /*
        if (NODES.size() >= 2)
        {
            l = glm::length(NODES.back()[3] - NODES.front()[3]);
        }
        if (l == 0) l = 1; // avoid devide by zero
        ImGui::Text("%2.3fHz", 1.f / (rootFrequency() / sqrt(l)));
        */
        //CHECKBOX("hasPivot", &hasPivot, "");
        R_FLOAT("stiffness", ossilation_stiffness, 0.1f, 0.8f, 20.f, "");
        R_FLOAT("sqrt(sway)", ossilation_constant_sqrt, 0.1f, 1.01f, 100.f, "");
        //R_FLOAT("root-tip", ossilation_power, 0.01f, 0.02f, 1.8f, "");

        ImGui::TreePop();
    }


    ImGui::PushID(gui_id);
    if (ImGui::TreeNodeEx(tree_name.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Framed))
    {
        TOOLTIP(tree_path.c_str());
        if (ImGui::IsItemClicked())  loadFromFile();
        ImGui::TreePop();
        //changed = true;
    }
    ImGui::PopID();
    gui_id++;

    twigs.renderGui("twigs", gui_id);
    branch_Material.renderGui(gui_id);
    trunk_Material.renderGui(gui_id);

    if (ImGui::Button("buildTreeRootAndBranches"))
    {
        buildTreeRootAndBranches();
    }
    
    
    

    CHECKBOX("drawRoot", &drawRoot, "");
    CHECKBOX("drawBranches", &drawBranches, "");
    int numOld = numBranches;
    R_INT("#", numBranches, 10, 255, "");
    if (numOld != numBranches) calcSubTwigs();

    

    ImGui::Text("stats...");
    ImGui::Text("avsLeaves %2.1f", avsLeavesInBranch);
    ImGui::Text("avsHigh %2.1f", avsLeavesInBranch_High);

    if (ImGui::Button("draw_all")) {
        drawOnlyOne = !drawOnlyOne; changed = true;
    }
    if (ImGui::Button("draw_all_dropped")) {
        drawAllDropped = !drawAllDropped; changed = true;
    }
    

    int i = 0;
    for (auto& B : myBranchCollection.branches)
    {
        std::string name = "show##%d" + std::to_string(i);
        if (ImGui::Button(name.c_str())) {
            theOneToDraw = i; drawOnlyOne = true; changed = true;
        }
        ImGui::SameLine(0, 30);
        ImGui::Text("%3d, %2.2fmm, %2.2fm, %2.2frad (%2.1f, %2.1f)m", B.numLeaves, B.rootRadius * 1000.f, B.leavesDistance, B.leavesPitch, B.width, B.height);
        i++;
    }
    /*
    for (int i = 0; i < sorted.size(); i++)
    {
        if (i < numBranches)
        {
            auto& B = branches[sorted[i].index];
            if (!B.deadRootBranch)
            {
                std::string name = "show##%d" + std::to_string(i);
                if (ImGui::Button(name.c_str())) {
                    theOneToDraw = i; drawOnlyOne = true; changed = true;
                }
                ImGui::SameLine(0, 30);

                ImGui::Text("%3d, %2d pvt, %2.2f, %2.2fm", B.numLeavesSmall, B.branchDepth, B.PITCH, B.LENGTH);
                
                if (B.all_twigs.size() < avsLeavesInBranch)
                {
                    ImGui::SameLine(0, 30);
                    ImGui::Text("<50%");
                }
            }
        }
    }
    */
    changedForSave |= changed;
    //anyChange |= changed;
    bool RET = changed;
    changed = false;
    return RET;
}


void _treeBuilder::treeView()
{
    ImVec4 selected_color = ImVec4(0.4f, 0.1f, 0.0f, 1);
    ImVec4 stem_color = ImVec4(0.05f, 0.02f, 0.0f, 1);
    ImVec4 leaf_color = ImVec4(0.01f, 0.04f, 0.01f, 1);
    ImVec4 mat_color = ImVec4(0.0f, 0.01f, 0.07f, 1);

    auto& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Header] = stem_color;
    int flags = ImGuiTreeNodeFlags_Leaf;
    if (_rootPlant::selectedPart == this) {
        style.FrameBorderSize = 3;
        flags = flags | ImGuiTreeNodeFlags_Selected;
        style.Colors[ImGuiCol_Border] = ImVec4(0.7f, 0.7f, 0.7f, 1);
    }

    if (changedForSave) style.Colors[ImGuiCol_Header] = selected_color;

    TREE_LINE(name.c_str(), path.c_str(), flags)
    {
        if (ImGui::IsItemHovered()) {
            if (_ribbonBuilder.tooManyPivots)     ImGui::SetTooltip("ERROR - more than 255 pivots");
            else ImGui::SetTooltip("%d instances \n%d verts \n%d / %d pivots ", numInstancePacked, numVertsPacked, numPivots, _ribbonBuilder.numPivots());
        }

        style.FrameBorderSize = 0;
        CLICK_PART;
        /*
        ImGui::Text("branches");

        for (auto& L : branches.branchData) { if (L.plantPtr) L.plantPtr->treeView(); }
        if (unique_tip)
        {
            ImGui::Text("tip");
            for (auto& L : tip.data) { if (L.plantPtr) L.plantPtr->treeView(); }
        }
        */
        ImGui::TreePop();
    }
}



void _treeBuilder::clear_build_info()
{
    numInstancePacked = 0;
    numVertsPacked = 0;
    numPivots = 0;
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
    //if (NODES.size() > 0)       return NODES.back();    // since its only direction test with this
    return (glm::mat4(1.f));
}


glm::mat4 _treeBuilder::build_2(buildSetting _settings, uint _bakeIndex, bool _faceCamera)
{
    return (glm::mat4(1.f));
}


glm::mat4 _treeBuilder::build_4(buildSetting _settings, uint _bakeIndex, bool _faceCamera)
{
    return (glm::mat4(1.f));
}

// loads and builds one brach = shall I nor ratgher just keep them in memory
void _treeBuilder::build_BRANCH(uint _idx, buildSetting _settings, bool bottom)
{
    int material_idx = branch_Material.index;
    glm::mat4 N;
    auto& BRANCH = tempActualBranches[_idx];

    /*
    //hasPivot && _addVerts && 
    if (!_settings.doNotAddPivot)
    {
        auto& B = BRANCH.branches.front();
        float3 extent = float3(1, 0, 0);// BRANCH.stats.leavesDistance;
        float ext_L = glm::length(extent);
        extent = glm::normalize(extent) / ext_L;

        _plant_anim_pivot p;
        p.root = B.nodes[0].pos;
        p.extent = extent;
        p.frequency = rootFrequency() * sqrt(ext_L);
        p.stiffness = ossilation_stiffness;
        p.shift = ossilation_power;           // DEPRECATED
        p.offset = DD_0_255_tree(_rootPlant::generator);

        _settings.pivotIndex[_settings.pivotDepth] = _ribbonBuilder.pushPivot(_settings.seed, p);
        _settings.pivotDepth += 1;
        numPivots++;
    }
    */
    //_ribbonBuilder.pushPivot(_settings.seed, p);  loop

    for (int i = 0; i < BRANCH.numPivots; i++)
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
            v += length(node.pos - prev.pos) / node.radius * 0.1f;
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
                float stepThis = stepFactor * pow((endRadius / node.radius), 0.3);/// 0.2 IS TE ERG
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
        uint STOP = branch.nodes.size();
        if (bottom) STOP = __min(branch.nodes.size(), branch.start_node + 1);


        if (_idx != _root)
        {
            START = 0;
            STOP = branch.nodes.size();
        }
        
        //for (auto& node : branch.nodes)
        for (int k = START; k < STOP; k++)
        {
            auto& node = branch.nodes[k];
            v += length(node.pos - prev.pos) / node.radius * 0.1f;
            //bool first = (prev.pos == node.pos);

            glm::mat4 N;

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
                float stepThis = stepFactor * pow((endRadius / node.radius), 0.3);/// 0.2 IS TE ERG

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

glm::mat4 _treeBuilder::build(buildSetting _settings, bool _addVerts)
{
    if (branches.size() == 0) return (glm::mat4(1.f));
    packSettings vertex_pack_Settings;
    vertex_pack_Settings.objectSize = 30.f;
    vertex_pack_Settings.radiusScale = 1.5f;
    _ribbonBuilder.setup(vertex_pack_Settings.getScale(), vertex_pack_Settings.radiusScale, vertex_pack_Settings.getOffset());

    

    // add the branches
    uint branchCount = 0;

    if (drawAllDropped)
    {
        for (int i = 0; i < allDroppedTwigs.size(); i++)
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
                /*
                auto& B = branches[sorted[theOneToDraw].index];

                for (int j = 0; j < B.all_twigs.size(); j++)
                {
                    build_one_branch(sorted[theOneToDraw].index, B.all_twigs[j], _settings, false);
                }
                */
            }
            else
            {
                for (int i = 0; i < numBranches; i++)
                {
                    auto& B = branches[sorted[i].index];

                    for (int j = 0; j < B.all_twigs.size(); j++)
                    {
                        build_one_branch(sorted[i].index, B.all_twigs[j], _settings, false);
                    }
                }
            }
        }
    }



    return (glm::mat4(1.f));
}

void _treeBuilder::loadFromFile()
{
    std::filesystem::path filepath;
    FileDialogFilterVec filters = { {"obj"} };
    if (openFileDialog(filters, filepath))
    {
        path = materialCache::getRelative(filepath.string());
        name = filepath.filename().string().substr(0, filepath.filename().string().length() - 4);
        objfile = fopen(filepath.string().c_str(), "r");
        load_obj();
    }
}


float3 _treeBuilder::readVertex()
{
    float3 v;
    char type[256];
    int ret = fscanf(objfile, "%s %f %f %f\n", type, &v.x, &v.y, &v.z);
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
    fscanf(objfile, "%39[^\n]\n", header);
    fscanf(objfile, "%39[^\n]\n", header);
    fscanf(objfile, "%39[^\n]\n", header);

    verts[0] = readVertex();
    numVerts = 1;
    branchMode = true;
}


void _treeBuilder::testBranchLeaves()
{
    float3 center = (verts[0] + verts[1] + verts[2]) / 3.0f;
    for (int j = 1; j < currentBranch->nodes.size(); j++) {         // start at 1, dont seacr that first node, it breaks branch inetrsections
        float3 nodeCenter = currentBranch->nodes[j].pos;
        float dist = glm::length(nodeCenter - center);
        if (fabs(dist) < 0.004f) {
            //we have a leaf, add it in future
            float l1 = glm::length(verts[1] - verts[0]);
            float l2 = glm::length(verts[2] - verts[1]);
            float l3 = glm::length(verts[0] - verts[2]);
            float radius = 0.19f * (l1 + l2 + l3);

            _leafNode L;
            L.pos = currentBranch->nodes[j].pos;
            L.dir = nodeDir;
            L.branchNode = j;
            L.branchIndex = branches.size() - 1;
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

    for (int i = 0; i < branches.size(); i++)
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
                for (int n = 0; n < B.nodes.size() - 1; n++)
                {
                    if (pointCylindar(P_0, B.nodes[n].pos, B.nodes[n + 1].pos, B.nodes[n].radius * 2))
                    {
                        A.parentIndex = j;
                        A.parentNode = n;

                        auto& P = A;
                        do
                        {
                            A.pivotDepth++;
                            P = branches[P.parentIndex];
                        } while (P.parentIndex >= 0);
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
        auto& P = B;
        float3 leafPos = B.nodes.back().pos;    //FIXME dead
        do
        {
            P.numLeaves++;
            P.leavesAVS += leafPos;
            P.leavesFurthest = __max(P.leavesFurthest, glm::length(leafPos - B.nodes[0].pos));

            if (P.parentIndex >= 0) P = branches[P.parentIndex];
        } while (P.parentIndex >= 0);
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
    for (int B = 0; B < branches.size(); B++)
    {
        glm::vec3 P0 = branches[B].nodes[0].pos;
        float R0 = branches[B].nodes[0].radius;
        for (int C = B - 1; C >= 0; C--)   // try <B again
        {
            if (branches[B].rootBranch >= 0) continue; // have al;ready found it

            if (branches[C].nodes[0].radius > R0)
            {
                for (int n = 0; n < branches[C].nodes.size() - 1; n++)
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

    for (int B = 0; B < branches.size(); B++)
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
            for (int j = 0; j < numVerts - 1; j++) {
                center += verts[j] * (1.0f / (numVerts - 1));
            }

            float l1 = glm::length(verts[1] - verts[0]);
            float l2 = glm::length(verts[2] - verts[1]);
            float l3 = glm::length(verts[0] - verts[2]);
            float radius = 0.19f * (l1 + l2 + l3);

            if (oldNumVerts < numVerts) {
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

            if ((abs(center.x) > 2) || (center.z < -2))
            {
                //currentBranch->isVisible = false;
            }


            //if ((center.z > -2) || (abs(center.x) > 2))
            if ((center.z < -2) || (abs(center.x) < 2))
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
            B.start_node = B.nodes.size();;
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
                                B.start_node = __min(B.start_node, branches[bottomIndex].sideNode);
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
        avsLeavesInBranch += B.all_twigs.size();
    }
    avsLeavesInBranch /= numBranches;

    int cnt = 0;
    for (int i = 0; i < numBranches; i++)
    {
        auto& B = branches[sorted[i].index];
        if (B.all_twigs.size() > avsLeavesInBranch)
        {
            avsLeavesInBranch_High += B.all_twigs.size();
            cnt++;
        }

        for (int i = 0; i < B.nodes.size(); i++)
        {
            if (B.nodes[i].radius < largestRadius && (i < B.start_node))
            {
                B.start_node = i;
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
    int cnt = endLeaves.size();
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

    for (int b = 0; b < branchLeaves.size(); b++)
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

    for (int i = 0; i < sorted.size(); i++)
    {
        if (i < numBranches)
        {
            auto& B = branches[sorted[i].index];
            if (!B.deadRootBranch)
            {
                _branch BRANCH;
                auto& rootBranch = branches[0];

                BRANCH.numPivots = 0;
               
                float3 rootPosition = B.nodes[B.start_node].pos;
                float3 rootAvsLeaves = B.leavesAvsPosition - rootPosition;
                float rootYaw = -atan2(rootAvsLeaves.z, rootAvsLeaves.x);

                float wmax = -100000000;
                float hmax = -100000000;
                float wmin = 100000000;
                float hmin = 100000000;
                for (int j = 0; j < B.all_twigs.size(); j++)
                {
                    auto& thisBranch = branches[B.all_twigs[j]];
                    BRANCH.branches.emplace_back();
                    auto& newB = BRANCH.branches.back();

                    uint START = thisBranch.start_node;
                    uint STOP = thisBranch.nodes.size();
                    for (int k = START; k < STOP; k++)
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

                    /// FUUUUUUUUUUUUUUUUCK
                    // this needs parent to find as well as strict ordering. i.e. parent MUST alwats be solved when we get here
                    //newB.pivots = { 255, 255, 255, 255 };
                    /*
                    newB.pivotDepth = thisBranch.branchDepth - rootBranch.branchDepth;
                    if ((newB.pivotDepth < 4) && (BRANCH.numPivots < 255))
                    {
                        newB.tempPivot = BRANCH.numPivots;
                        auto& PVT = BRANCH.pivots[BRANCH.numPivots];

                        PVT.root = rootBranch.nodes[0].pos;

                        int numLeaves = thisBranch.numLeaves;
                        float3 extAvs = thisBranch.leavesAvsPosition - PVT.root;
                        float ext_L = glm::length(extAvs);
                        
                        PVT.stiffness = ossilation_stiffness;           // FIXME  NumLeaves shopuld influence both of these
                        PVT.frequency = rootFrequency() * sqrt(ext_L);
                        PVT.extent = extAvs * 1.3f;
                        PVT.offset = DD_0_255_tree(_rootPlant::generator);

                        BRANCH.numPivots++;
                    }
                    */
                }

                // now search for parent
                BRANCH.findParentBranches();
                BRANCH.sumLeaves();
                BRANCH.generatePivots();
                BRANCH.propagatePivots();
                /*
                // now add pivot data
                {
                    for (auto& B : BRANCH.branches)
                    {
                        if (B.parentIndex >= 0 && B.pivotDepth < 4)
                        {
                            B.pivots = BRANCH.branches[B.parentIndex].pivots;
                            B.pivots[B.pivotDepth] = B.tempPivot;
                        }
                    }
                    //need to save a temp pivot abovemthen read from parent herte,thren combine
                }*/

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
        std::filesystem::path full_path = terrafectorEditorMaterial::rootFolder + path;
        full_path.remove_filename();
        full_path += "\\branch.collection";

        std::ofstream stream(full_path.string());
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
