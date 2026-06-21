#include "roadNetwork.h"

#include "imgui.h"
#include "imgui_internal.h"

#include "assimp/scene.h"
#include "assimp/Exporter.hpp"
using namespace Assimp;

#pragma optimize( "", off )


bool roadNetwork::showMaterials;
Texture::SharedPtr roadNetwork::displayThumbnail;
extern void cubic_Casteljau_Full(float t, glm::vec3 P0, glm::vec3 P1, glm::vec3 P2, glm::vec3 P3, glm::vec3& pos, glm::vec3& vel, glm::vec3& acc);
extern glm::vec3 cubic_Casteljau(float t, glm::vec3 P0, glm::vec3 P1, glm::vec3 P2, glm::vec3 P3);
extern glm::vec3 cubic_Casteljau(float t, bezierPoint* A, bezierPoint* B);
extern glm::vec3 del_cubic_Casteljau(float t0, float t1, bezierPoint* A, bezierPoint* B);


ODE_bezier roadNetwork::odeBezier;	// for the physics lookup, so this can build it, save and load binary etc
std::vector<cubicDouble>	roadNetwork::staticBezierData;
std::vector<bezierLayer>	roadNetwork::staticIndexData;
std::vector<bezierLayer>	roadNetwork::staticIndexData_BakeOnly;



roadNetwork::roadNetwork()
{
    odeBezier.setGrid(50.0f, 500, 500);		// so 25x25km we do not need the edges
    roadSectionsList.reserve(10000);
    intersectionList.reserve(10000);
}



bool roadNetwork::renderpopupGUI(Gui* _gui, intersection* _intersection)
{
    bool bChanged = false;

    static float3 COL;
    ImGui::ColorEdit3("TEST", &COL.x);
    ImGui::PushFont(_gui->getFont("roboto_32"));
    {
        ImGui::Text("Intersection : %d", _intersection->GUID);
        if (_intersection->lidarLOD >= 7) {
            ImGui::SliderInt("I quality", &_intersection->buildQuality, 0, 2);
        }
        else {
            ImGui::Text("lidar warning - too low LOD used - %d", _intersection->lidarLOD);
        }

        if (ImGui::Button("normal up")) { _intersection->anchorNormal = glm::vec3(0, 1, 0); solveIntersection(); _intersection->customNormal = true; }
        if (ImGui::DragFloat3("up", &_intersection->anchorNormal.x, 0.01f, -1, 1)) {
            _intersection->anchorNormal = glm::normalize(_intersection->anchorNormal);
            solveIntersection();
            _intersection->customNormal = true;
            bChanged = true;

        }

        if (ImGui::Selectable("export XML")) {
            std::filesystem::path path;
            FileDialogFilterVec filters = { {"intersection.xml"} };
            if (saveFileDialog(filters, path))
            {
                std::ofstream os(path);
                cereal::XMLOutputArchive archive(os);
                _intersection->serialize(archive, 101);
            }
        }


        ImGui::Checkbox("custom Normal", &_intersection->customNormal);
        ImGui::DragFloat("height", &_intersection->heightOffset, -2.0f, 2.0f);
        ImGui::Checkbox("do not Push", &_intersection->doNotPush);

        for (auto rd : _intersection->roadLinks)
        {
            ImGui::Text("%d", rd.roadGUID);
        }
    }
    ImGui::PopFont();

    return bChanged;
}



std::array<std::string, 9> laneNames{ "shoulder in", "turn in", "lane 1", "lane 2", "lane 3", "turn", "shoulder", "sidewalk", "clearing" };
void roadNetwork::renderPopupVertexGUI(Gui* _gui, glm::vec2 _pos, uint _vertex)
{
    /*
    Gui::Window avxWindow(_gui, "Avx", { 200, 200 }, { 100, 100 });
    {
        ImGui::SetWindowPos(ImVec2(_pos.x + 100, _pos.y - 100));

        ImGui::Text("vertex %d", _vertex);
        ImGui::Separator();


        // camber
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        ImVec2 A = window->Rect().GetTL();
        A.x += 100;
        A.y += 90 + currentRoad->points[_vertex].camber * 50.0f;
        ImVec2 B = window->Rect().GetTL();
        B.x += 0;
        B.y += 90 - currentRoad->points[_vertex].camber * 50.0f;
        ImVec2 C = window->Rect().GetTL();
        C.x += 50;
        C.y += 90;
        window->DrawList->AddLine(A, C, ImColor(0, 0, 255, 255), 5.0f);
        window->DrawList->AddLine(C, B, ImColor(0, 255, 0, 255), 5.0f);

        switch (currentRoad->points[_vertex].constraint) {
        case e_constraint::none:
            ImGui::Text("free =  %3.1f", currentRoad->points[_vertex].camber);
            break;
        case e_constraint::camber:
            ImGui::Text("comstrained =  %3.1f", currentRoad->points[_vertex].camber);
            break;
        case e_constraint::fixed:		//??? is this used
            ImGui::Text("fixed");
            break;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("use left - right arrows to modify while hovering vertex");

    }
    avxWindow.release();

    Gui::Window bvxWindow(_gui, "Bvx", { 200, 200 }, { 100, 100 });
    {
        ImGui::SetWindowPos(ImVec2(_pos.x - 300, _pos.y - 200));
    }
    bvxWindow.release();

    Gui::Window cvxWindow(_gui, "Cvx", { 200, 200 }, { 100, 100 });
    {
        ImGui::SetWindowPos(ImVec2(_pos.x - 100, _pos.y + 200));
    }
    cvxWindow.release();
    */
}



void roadNetwork::renderPopupSegmentGUI(Gui* _gui, glm::vec2  _pos, uint _idx)
{
    /*
    Gui::Window seginfoWindow(_gui, "SEGInfo", { 200, 200 }, { 100, 100 });
    {
        ImGui::SetWindowPos(ImVec2(_pos.x + 100, _pos.y - 100));
    }
    seginfoWindow.release();
    */
}

void replaceAll_RD(std::string& str, const std::string& from, const std::string& to) {
    if (from.empty())
        return;
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // In case 'to' contains 'from', like replacing 'x' with 'yx'
    }
}


// FIXME selection
void roadNetwork::loadRoadMaterial(uint _side, uint _slot)
{

    std::filesystem::path path;
    FileDialogFilterVec filters = { {"roadMaterial.jpg"} };
    if (openFileDialog(filters, path))
    {
        std::string filename = path.string();

        if (filename.find("jpg") != std::string::npos)
        {
            filename = filename.substr(0, filename.size() - 4);
        }

        uint idx = roadMaterialCache::getInstance().find_insert_material(filename);
        for (int i = selectFrom; i < selectTo; i++)
        {
            currentRoad->points[i].setMaterialIndex(_side, _slot, idx);
        }

        terrafectorEditorMaterial::static_materials.rebuildAll();
    }
}



void roadNetwork::dragdropRoadMaterial(uint _side, uint _slot, uint _index)
{
    for (int i = selectFrom; i < selectTo; i++)
    {
        currentRoad->points[i].setMaterialIndex(_side, _slot, _index);
    }
}



void roadNetwork::clearRoadMaterial(uint _side, uint _slot)
{
    for (int i = selectFrom; i <= selectTo; i++)
    {
        currentRoad->points[i].setMaterialIndex(_side, _slot, -1);
    }
}



uint roadNetwork::getRoadMaterial(uint _side, uint _slot, uint _index)
{
    return currentRoad->points[_index].getMaterialIndex(_side, _slot);
}



const std::string roadNetwork::getMaterialName(uint _side, uint _slot)
{
    int idx = currentRoad->points[selectFrom].getMaterialIndex(_side, _slot);
    if (idx >= 0 && idx < roadMaterialCache::getInstance().materialVector.size()) {
        return roadMaterialCache::getInstance().materialVector.at(idx).displayName;
    }
    return "";
}



const std::string roadNetwork::getMaterialPath(uint _side, uint _slot)
{
    int idx = currentRoad->points[selectFrom].getMaterialIndex(_side, _slot);
    if (idx >= 0 && idx < roadMaterialCache::getInstance().materialVector.size()) {
        return roadMaterialCache::getInstance().materialVector.at(idx).relativePath;
    }
    return "";
}



const Texture::SharedPtr roadNetwork::getMaterialTexture(uint _side, uint _slot)
{
    int idx = currentRoad->points[selectFrom].getMaterialIndex(_side, _slot);
    if (idx >= 0 && idx < roadMaterialCache::getInstance().materialVector.size()) {
        return roadMaterialCache::getInstance().materialVector.at(idx).thumbnail;
    }
    return nullptr;
}



#define TOOLTIP(x)  if (ImGui::IsItemHovered()) {ImGui::SetTooltip(x);}

#define MATERIAL(side, slot, tooltip)	{ \
												ImGui::PushID(9999 + side * 1000 + slot); \
													uint mat = getRoadMaterial(side, slot, selectFrom); \
													if (ImGui::Button(getMaterialName(side, slot).c_str(), ImVec2(190, 0))) { loadRoadMaterial(side, slot); } \
													TOOLTIP( tooltip ); \
													if (ImGui::IsItemHovered()) { roadNetwork::displayThumbnail = getMaterialTexture(side, slot); } \
													ImGui::SameLine(); \
													if (ImGui::Button("X")) { ;}\
												ImGui::PopID(); \
										} \


#define DRAGDROP(side, slot)        {  \
                                        if (ImGui::BeginDragDropTarget())  \
                                        {  \
                                            auto dragDropPayload = ImGui::AcceptDragDropPayload("ROADMATERIAL");  \
                                            if(dragDropPayload && dragDropPayload->IsDataType("ROADMATERIAL") && (dragDropPayload->Data != nullptr))  \
                                            {  \
                                                dragdropRoadMaterial(side, slot, 5);  \
                                            }  \
                                            ImGui::EndDragDropTarget();  \
                                        }  \
                                    }  \




#define DISPLAY_MATERIAL(side, slot, tooltip)	{ \
										bool same = true; \
										int mat =  getRoadMaterial(side, slot, selectFrom); \
										for (int sel = selectFrom; sel < selectTo; sel++) \
										{ \
											if (getRoadMaterial(side, slot, sel) != mat) same &= false; 	 \
										} \
										\
										std::string mat_name = "... various ..."; \
										if (same) { \
											style.Colors[ImGuiCol_Button] = ImVec4(0.02f, 0.02f, 0.02f, 1.0f);  \
											mat_name = getMaterialName(side, slot); \
										} \
										else { \
											style.Colors[ImGuiCol_Button] = ImVec4(0.72f, 0.42f, 0.12f, 1.0f); 	\
										}  \
										if (selectTo <= selectFrom) { style.Colors[ImGuiCol_Button] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f); }\
										 \
										 ImGui::PushID(9999 + side * 1000 + slot); \
												if (ImGui::Button(mat_name.c_str(), ImVec2(120, 0))) { if (selectTo > selectFrom) loadRoadMaterial(side, slot); } \
                                                DRAGDROP(side, slot) \
												TOOLTIP( tooltip ); \
												if (same && ImGui::IsItemHovered()) { roadNetwork::displayThumbnail = getMaterialTexture(side, slot); } \
												ImGui::SameLine(); \
												if (ImGui::Button("X")) { clearRoadMaterial(side, slot);}\
										ImGui::PopID(); \
										} \


//??? can we do this with a function, this is sillt
#define DISPLAY_WIDTH(_lane, _min, _max, _SIDE) {					\
									bool same = true; 					\
									float val_0 = _road->points[selectFrom]._SIDE[_lane].laneWidth; 					\
									float val = 0; 					\
									float MIN = 99999; 					\
									float MAX = -99999; 					\
									int total = selectTo - selectFrom + 1; 					\
									for (int sel = selectFrom; sel <= selectTo; sel++) 					\
									{ 					\
										val += _road->points[sel]._SIDE[_lane].laneWidth; 					\
										if (_road->points[sel]._SIDE[_lane].laneWidth > MAX )   MAX = _road->points[sel]._SIDE[_lane].laneWidth; 					\
										if (_road->points[sel]._SIDE[_lane].laneWidth < MIN )   MIN = _road->points[sel]._SIDE[_lane].laneWidth; 					\
										if (_road->points[sel]._SIDE[_lane].laneWidth != val_0) same &= false; 					\
									} 					\
									val /= total; 					\
									 										\
									if (same) { 					\
										if (val == 0) { 					\
											style.Colors[ImGuiCol_FrameBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.95f); 					\
										} 					\
										else { 					\
											style.Colors[ImGuiCol_FrameBg] = ImVec4(0.42f, 0.02f, 0.02f, 0.95f); 					\
										} 					\
									} 					\
									else { 					\
										style.Colors[ImGuiCol_FrameBg] = ImVec4(0.72f, 0.42f, 0.12f, 0.95f); 					\
									} 					\
									 					\
									if (ImGui::DragFloat("", &val, 0.01f, _min, _max, "%2.2f m")) 					\
									{ 					\
										for (int sel = selectFrom; sel <= selectTo; sel++) 					\
										{ 					\
											_road->points[sel]._SIDE[_lane].laneWidth = val; 					\
										} 					\
									} 					\
									 					\
									if (same) { 					\
									} 					\
									else { 					\
										if (ImGui::IsItemHovered()) {ImGui::SetTooltip("VARIOUS VALUES %2.2f = %2.2f m", MIN, MAX);} 					\
									} 					\
								} 					\





void roadNetwork::saveRoadGeometry(roadSection* _road, int _vertex)
{
    std::filesystem::path path;
    FileDialogFilterVec filters = { {"roadwidths.xml"} };
    if (saveFileDialog(filters, path))
    {
        FILE* file = fopen(path.string().c_str(), "w");
        if (file) {
            for (int i = 0; i < 9; i++)
            {
                fprintf(file, "%f\n", _road->points[_vertex].lanesLeft[i].laneWidth);
                fprintf(file, "%f\n", _road->points[_vertex].lanesRight[i].laneWidth);
            }
        }
        fclose(file);
    }
}





void roadNetwork::loadRoadGeometry(roadSection* _road, int _from, int _to)
{
    float wL[9], wR[9];
    std::filesystem::path path;
    FileDialogFilterVec filters = { {"roadwidths.xml"} };
    if (openFileDialog(filters, path))
    {
        FILE* file = fopen(path.string().c_str(), "r");
        if (file) {
            for (int i = 0; i < 9; i++)
            {
                fscanf(file, "%f\n", &wL[i]);
                fscanf(file, "%f\n", &wR[i]);
            }
        }
        fclose(file);
    }

    for (int v = _from; v <= _to; v++)
    {
        for (int i = 0; i < 9; i++)
        {
            _road->points[v].lanesLeft[i].laneWidth = wL[i];
            _road->points[v].lanesRight[i].laneWidth = wR[i];
        }
    }

    isDirty = true;
}





// FIXME should likely be names and then lookup for future use
void roadNetwork::saveRoadMaterials(roadSection* _road, int _vertex)
{
    std::filesystem::path path;
    FileDialogFilterVec filters = { {"roadmaterials"} };
    if (saveFileDialog(filters, path))
    {
        FILE* file = fopen(path.string().c_str(), "w");
        if (file) {
            //for (int i = 0; i < 15; i++) fprintf(file, "%d\n", _road->points[_vertex].matLeft[i]);
            //for (int i = 0; i < 2; i++) fprintf(file, "%d\n", _road->points[_vertex].matCenter[i]);
            //for (int i = 0; i < 15; i++) fprintf(file, "%d\n", _road->points[_vertex].matRight[i]);
            //fwrite(&_road->points[_vertex].matLeft[0], sizeof(int), 15, file);
            //fwrite(&_road->points[_vertex].matCenter[0], sizeof(int), 2, file);
            //fwrite(&_road->points[_vertex].matRight[0], sizeof(int), 15, file);

            //roadMaterialCache::getInstance().materialVector[idx].relativePath

            //int m_size = (int)roadMaterialCache::getInstance().materialVector.size();
            //fprintf(file, "%d sizeof materialVector\n");
            
            for (int i = 0; i < 15; i++)
            {
                int idx = _road->points[_vertex].matLeft[i];
                if ( idx >= 0)
                    fprintf(file, "%s\n", roadMaterialCache::getInstance().materialVector[idx].relativePath.c_str());
                    //fprintf(file, "L\n");
                else
                    fprintf(file, "\n");
            }

            for (int i = 0; i < 2; i++)
            {
                int idx = _road->points[_vertex].matCenter[i];
                if (idx >= 0)
                    fprintf(file, "%s\n", roadMaterialCache::getInstance().materialVector[idx].relativePath.c_str());
                else
                    fprintf(file, "\n");
            }

            for (int i = 0; i < 15; i++)
            {
                int idx = _road->points[_vertex].matRight[i];
                if (idx >= 0)
                    fprintf(file, "%s\n", roadMaterialCache::getInstance().materialVector[idx].relativePath.c_str());
                    //fprintf(file, "R\n");
                else
                    fprintf(file, "\n");
            }

            
        }
        fclose(file);
    }
}

int  load_left[15];
int  load_center[2];
int  load_right[15];



void roadNetwork::loadRoadMaterials(roadSection* _road, int _from, int _to)
{
    char filename[256];

    std::filesystem::path path;
    FileDialogFilterVec filters = { {"roadmaterials"} };
    if (openFileDialog(filters, path))
    {
        FILE* file = fopen(path.string().c_str(), "r");
        if (file) {
            //for (int i = 0; i < 15; i++) fscanf(file, "%d\n", &load_left[i]);
            //for (int i = 0; i < 2; i++) fscanf(file, "%d\n", &load_center[i]);
            //for (int i = 0; i < 15; i++) fscanf(file, "%d\n", &load_right[i]);

            for (int i = 0; i < 15; i++)
            {
                load_left[i] = -1;
                memset(filename, 0, 256);
                int k = fscanf(file, "%s\n", filename);
                if (filename[1])
                    load_left[i] = roadMaterialCache::getInstance().find_insert_material(terrafectorEditorMaterial::rootFolder + filename);
            }

            for (int i = 0; i < 2; i++)
            {
                load_center[i] = -1;
                memset(filename, 0, 256);
                int k = fscanf(file, "%s\n", filename);
                if (filename[1])
                    load_center[i] = roadMaterialCache::getInstance().find_insert_material(terrafectorEditorMaterial::rootFolder + filename);
            }

            for (int i = 0; i < 15; i++)
            {
                load_right[i] = -1;
                memset(filename, 0, 256);
                int k = fscanf(file, "%s\n", filename);
                if (filename[1])
                    load_right[i] = roadMaterialCache::getInstance().find_insert_material(terrafectorEditorMaterial::rootFolder + filename);
            }
        }
        fclose(file);
    }

    isDirty = true;
    terrafectorEditorMaterial::static_materials.rebuildAll();
}



void roadNetwork::loadRoadMaterialsAll(roadSection* _road, int _from, int _to)
{
    loadRoadMaterials(_road, _from, _to);

    for (int idx = _from; idx <= _to; idx++)
    {
        for (int i = 0; i < 15; i++) _road->points[idx].matLeft[i] = load_left[i];
        for (int i = 0; i < 2; i++) _road->points[idx].matCenter[i] = load_center[i];
        for (int i = 0; i < 15; i++) _road->points[idx].matRight[i] = load_right[i];
    }
}



void roadNetwork::loadRoadMaterialsVerge(roadSection* _road, int _from, int _to)
{
    loadRoadMaterials(_road, _from, _to);

    for (int idx = _from; idx <= _to; idx++)
    {
        _road->points[idx].matLeft[0] = load_left[0];
        _road->points[idx].matRight[0] = load_right[0];
    }
}



void roadNetwork::loadRoadMaterialsSidewalk(roadSection* _road, int _from, int _to)
{
    loadRoadMaterials(_road, _from, _to);

    for (int idx = _from; idx <= _to; idx++)
    {
        _road->points[idx].matLeft[1] = load_left[1];
        _road->points[idx].matLeft[2] = load_left[2];
        _road->points[idx].matRight[1] = load_right[1];
        _road->points[idx].matRight[2] = load_right[2];
    }
}



void roadNetwork::loadRoadMaterialsAsphalt(roadSection* _road, int _from, int _to)
{
    loadRoadMaterials(_road, _from, _to);

    for (int idx = _from; idx <= _to; idx++)
    {
        _road->points[idx].matLeft[3] = load_left[3];
        _road->points[idx].matRight[3] = load_right[3];
    }
}



void roadNetwork::loadRoadMaterialsLines(roadSection* _road, int _from, int _to)
{
    loadRoadMaterials(_road, _from, _to);

    for (int idx = _from; idx <= _to; idx++)
    {
        for (int i = 4; i < 10; i++) _road->points[idx].matLeft[i] = load_left[i];
        for (int i = 0; i < 2; i++) _road->points[idx].matCenter[i] = load_center[i];
        for (int i = 4; i < 10; i++) _road->points[idx].matRight[i] = load_right[i];
    }
}



void roadNetwork::loadRoadMaterialsWT(roadSection* _road, int _from, int _to)
{
    loadRoadMaterials(_road, _from, _to);

    for (int idx = _from; idx <= _to; idx++)
    {
        for (int i = 10; i < 15; i++) _road->points[idx].matLeft[i] = load_left[i];
        for (int i = 10; i < 15; i++) _road->points[idx].matRight[i] = load_right[i];
    }
}





bool roadNetwork::renderpopupGUI(Gui* _gui, roadSection* _road, int _vertex) {

    if (!_road) return false;

    auto& style = ImGui::GetStyle();
    ImGuiStyle oldStyle = style;
    //style.Colors[ImGuiCol_WindowBg] = ImVec4(0.00f, 0.02f, 0.01f, 0.80f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.13f, 0.14f, 0.17f, 0.70f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.13f, 0.14f, 0.17f, 0.90f);

    style.Colors[ImGuiCol_Button] = ImVec4(0.47f, 0.77f, 0.83f, 0.5f);
    //	style.Colors[ImGuiCol_ButtonHovered] = MED(0.86f);

    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.5f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);

    style.FrameRounding = 6.0f;

    roadNetwork::displayThumbnail = nullptr;

    ImGui::PushFont(_gui->getFont("roboto_32"));
    {
        ImGui::Text("Road %d, [%d]", _road->GUID, _road->points.size());
    }
    ImGui::PopFont();

    ImGui::PushItemWidth(160);
    ImGui::SliderInt("q", &_road->buildQuality, 0, 2);
    ImGui::PopItemWidth();

    ImGui::Checkbox("is closed loop", &_road->isClosedLoop);
    TOOLTIP("set for tracks\nIt will snap the last point to the first if closer than 20m appart, \nand we have more than 5 points to avoid mess at start of a road");

    ImGui::Checkbox("is linear marking", &_road->isBasicLinearMarking);
    TOOLTIP("Is this a basic single spline linear feature");

    ImGui::NewLine();
    ImGui::Checkbox("is fixed UV", &_road->isIntergerUV);
    TOOLTIP("Does teh U value repeat so we can tile in intersections or use absolute diatance/nuse TRUE for roads with intersection\nuse FALSE for road markings");
    if (ImGui::DragFloat("U repeat distance", &_road->uvScale, 0.1f, 1.f, 16.f, "%.2f m"))
    {
        _road->solveRoad();
    }
    TOOLTIP("Distance where U repeats\nThis does get scaled by the material U scale as well so not absolute\nleave on 8.0 for most roads as materials are set up that way");
    
    if (_road->points.size() == 0) return false;

    if (_road->isBasicLinearMarking)
    {

    }


    // Selection #$###########################################################################
    if (_vertex >= (int)_road->points.size()) _vertex = (int)_road->points.size() - 1;

    if (selectionType == 0) {
        selectFrom = _vertex;
        selectTo = _vertex;
    }
    if (selectionType == 2) {
        selectFrom = 0;
        selectTo = (int)_road->points.size() - 1;
    }

    /*
    for (uint i = selectFrom; i <= selectTo; i++)
    {
        // camber

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        //ImGuiWindow* window = ImGui::GetCurrentWindow();
        ImVec2 A;// = window->Rect().GetTL();
        A.x += 150;
        A.y += 200 + _road->points[i].camber * 70.0f;
        ImVec2 B;// = window->Rect().GetTL();
        B.x += 10;
        B.y += 200 - _road->points[i].camber * 70.0f;
        ImVec2 C;// = window->Rect().GetTL();
        C.x += 80;
        C.y += 200;
        drawList->AddLine(A, C, ImColor(0, 0, 255, 255), 5.0f);
        drawList->AddLine(C, B, ImColor(0, 155, 0, 255), 5.0f);
    }

    ImGui::SetCursorPosY(200);
    switch (_road->points[_vertex].constraint) {
    case e_constraint::none:
        ImGui::Text("Camber - free =  %3.2f", _road->points[_vertex].camber);
        break;
    case e_constraint::camber:
        ImGui::Text("Camber - constrained =  %3.2f", _road->points[_vertex].camber);
        break;
    case e_constraint::fixed:
        ImGui::Text("Camber - fixed");
        break;
    }
    */




    ImGui::NewLine();
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.02f, 0.0f, 0.0f, 1.0f);
    ImGui::BeginChildFrame(199, ImVec2(180, 50), 0);
    {
        ImGui::PushItemWidth(50);
        if (selectionType == 0)	style.Colors[ImGuiCol_Button] = ImVec4(0.6f, 0.4f, 0.0f, 1.0f);
        if (ImGui::Button("None", ImVec2(50, 0))) { selectionType = 0; }
        TOOLTIP("select None\nDisplay and actions apply to the last vertex under the mouse\nctrl-d");
        style.Colors[ImGuiCol_Button] = ImVec4(0.47f, 0.77f, 0.83f, 0.5f);

        if (selectionType == 1)	style.Colors[ImGuiCol_Button] = ImVec4(0.6f, 0.4f, 0.0f, 1.0f);
        ImGui::SameLine();
        if (ImGui::Button("Range", ImVec2(50, 0))) { selectionType = 1; }
        TOOLTIP("select Range\nClick and shift Click for start and end of range");
        style.Colors[ImGuiCol_Button] = ImVec4(0.47f, 0.77f, 0.83f, 0.5f);

        if (selectionType == 2)	style.Colors[ImGuiCol_Button] = ImVec4(0.6f, 0.4f, 0.0f, 1.0f);
        ImGui::SameLine();
        if (ImGui::Button("All", ImVec2(50, 0))) { selectionType = 2; }
        TOOLTIP("select All\nctrl-a");
        style.Colors[ImGuiCol_Button] = ImVec4(0.47f, 0.77f, 0.83f, 0.5f);

        ImGui::PopItemWidth();


        ImGui::PushFont(_gui->getFont("roboto_26"));
        {

            ImGui::PushItemWidth(70);
            if (ImGui::DragInt("##start", &selectFrom, 1, 0, (int)_road->points.size())) { ; }
            ImGui::SameLine();
            ImGui::Text(":");
            ImGui::SameLine();
            if (ImGui::DragInt("##end", &selectTo, 1, selectFrom, (int)_road->points.size())) { ; }
            ImGui::PopItemWidth();

        }
        ImGui::PopFont();
    }
    ImGui::EndChildFrame();



    ImGui::NewLine();
    ImGui::Checkbox("show materials", &showMaterials);
    TOOLTIP("space - toggle");





    ImGui::PushFont(_gui->getFont("roboto_26"));
    {
        ImGui::SetCursorPosX(30);
        ImGui::Text("geometry");

        ImGui::SameLine();
        ImGui::SetCursorPosX(230);  // 200 + 150
        ImGui::Text("base");
        ImGui::SameLine();
        ImGui::SetCursorPosX(350 - 60);
        if (ImGui::Button("X##base")) {
            clearRoadMaterial(0, splinePoint::matName::verge);
            clearRoadMaterial(0, splinePoint::matName::sidewalk);
            clearRoadMaterial(0, splinePoint::matName::gutter);
            clearRoadMaterial(0, splinePoint::matName::tarmac);

            clearRoadMaterial(2, splinePoint::matName::verge);
            clearRoadMaterial(2, splinePoint::matName::sidewalk);
            clearRoadMaterial(2, splinePoint::matName::gutter);
            clearRoadMaterial(2, splinePoint::matName::tarmac);
        }

        ImGui::SameLine();
        ImGui::SetCursorPosX(380);
        ImGui::Text("lines");
        ImGui::SameLine();
        ImGui::SetCursorPosX(500 - 60);
        if (ImGui::Button("X##lines")) {
            clearRoadMaterial(0, splinePoint::matName::shoulder);
            clearRoadMaterial(0, splinePoint::matName::outerTurn);
            clearRoadMaterial(0, splinePoint::matName::lane2);
            clearRoadMaterial(0, splinePoint::matName::lane1);
            clearRoadMaterial(0, splinePoint::matName::innerTurn);
            clearRoadMaterial(0, splinePoint::matName::innerShoulder);

            clearRoadMaterial(1, 0);
            clearRoadMaterial(1, 1);

            clearRoadMaterial(2, splinePoint::matName::shoulder);
            clearRoadMaterial(2, splinePoint::matName::outerTurn);
            clearRoadMaterial(2, splinePoint::matName::lane2);
            clearRoadMaterial(2, splinePoint::matName::lane1);
            clearRoadMaterial(2, splinePoint::matName::innerTurn);
            clearRoadMaterial(2, splinePoint::matName::innerShoulder);
        }

        ImGui::SameLine();
        ImGui::SetCursorPosX(500);
        ImGui::Text("wear&tear");
        ImGui::SameLine();
        ImGui::SetCursorPosX(650 - 40);
        if (ImGui::Button("X##wt")) {
            clearRoadMaterial(0, splinePoint::matName::rubberOuter);
            clearRoadMaterial(0, splinePoint::matName::rubberLane3);
            clearRoadMaterial(0, splinePoint::matName::rubberLane2);
            clearRoadMaterial(0, splinePoint::matName::rubberLane1);
            clearRoadMaterial(0, splinePoint::matName::rubberInner);

            clearRoadMaterial(2, splinePoint::matName::rubberOuter);
            clearRoadMaterial(2, splinePoint::matName::rubberLane3);
            clearRoadMaterial(2, splinePoint::matName::rubberLane2);
            clearRoadMaterial(2, splinePoint::matName::rubberLane1);
            clearRoadMaterial(2, splinePoint::matName::rubberInner);
        }
    }
    ImGui::PopFont();

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);

    float tempY = ImGui::GetCursorPosY();



    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.0f, 0.07f, 0.0f, 1.0f);
    ImGui::BeginChildFrame(100, ImVec2(660, 280), 0);
    {
        for (int i = 8; i >= 0; i--) {
            style.Colors[ImGuiCol_Button] = ImVec4(0.22f, 0.02f, 0.02f, 0.05f);
            if (!editRight && editLaneIndex == i) { style.Colors[ImGuiCol_Button] = ImVec4(0.22f, 0.02f, 0.02f, 0.95f); }
            if (ImGui::Button(laneNames[i].c_str(), ImVec2(110, 0))) { editLaneIndex = i; editRight = false; }

            ImGui::SameLine();
            ImGui::SetCursorPosX(90);
            ImGui::PushID(3333 + i);
            ImGui::PushItemWidth(90);
            {
                if (i == 0) { DISPLAY_WIDTH(i, -10, 50, lanesLeft); }
                else { DISPLAY_WIDTH(i, 0, 50, lanesLeft); }
            }
            ImGui::PopItemWidth();
            ImGui::PopID();
        }
    }
    ImGui::EndChildFrame();



    float lineY = (ImGui::GetCursorPosY() - tempY) / 9.0f;
    style.Colors[ImGuiCol_Button] = ImVec4(0.01f, 0.05f, 0.08f, 0.9f);

    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.0f);
    ImGui::SetCursorPos(ImVec2(200, tempY));
    ImGui::BeginChildFrame(101, ImVec2(150, 260), 0);
    {
        DISPLAY_MATERIAL(0, splinePoint::verge, "VERGE \nLine between the outermost lane and the shoulder.\n make sure something is selected before loading");
        DISPLAY_MATERIAL(0, splinePoint::sidewalk, "SIDEWALK \nLine between the outermost lane and the shoulder.\n make sure something is selected before loading");
        DISPLAY_MATERIAL(0, splinePoint::gutter, "GUTTER \nLine between the outermost lane and the shoulder.\n make sure something is selected before loading");
        ImGui::SetCursorPosY(lineY * 5);
        DISPLAY_MATERIAL(0, splinePoint::tarmac, "ROAD SURFACE \nLine between the outermost lane and the shoulder.\n make sure something is selected before loading");
    }
    ImGui::EndChildFrame();

    ImGui::SetCursorPos(ImVec2(350, tempY));
    ImGui::BeginChildFrame(102, ImVec2(150, 280), 0);
    {
        ImGui::SetCursorPosY(lineY * 2);


        DISPLAY_MATERIAL(0, splinePoint::shoulder, "SHOULDER \nLine between the outermost lane and the shoulder.\n make sure something is selected before loading");
        DISPLAY_MATERIAL(0, splinePoint::outerTurn, "OUTER TURN LANE \nUsually dotted line to get into a turn lane \nApplied to the outside of the lane next to it.\n make sure something is selected before loading");
        ImGui::NewLine();
        DISPLAY_MATERIAL(0, splinePoint::lane2, "LANE 2 \nOutside line to the next lane if there is one. \nOnly applies to lane 3 it it exists.\n make sure something is selected before loading");
        DISPLAY_MATERIAL(0, splinePoint::lane1, "LANE 1 \nOutside line to the next lane if there is one. \nOnly applies to lane 2 or 3 if they exist.\n make sure something is selected before loading");
        DISPLAY_MATERIAL(0, splinePoint::innerTurn, "INNER TURN LANE \nUsually dotted line to get into a turn lane \nApplied to the inside of the lane next to it.\n make sure something is selected before loading");
        DISPLAY_MATERIAL(0, splinePoint::innerShoulder, "INNER SHOULDER \nLine between a triffic island and the first lane.\n make sure something is selected before loading");
    }
    ImGui::EndChildFrame();

    ImGui::SetCursorPos(ImVec2(500, tempY));
    ImGui::BeginChildFrame(103, ImVec2(150, 280), 0);
    {
        ImGui::SetCursorPosY(lineY * 3);
        DISPLAY_MATERIAL(0, splinePoint::rubberOuter, "OUTER TURN 2 \nRubbering, tyre wear, oil and dirt \nApplied to only this lane\n make sure something is selected before loading");
        DISPLAY_MATERIAL(0, splinePoint::rubberLane3, "LANE 3 \nRubbering, tyre wear, oil and dirt \nApplied to only this lane\n make sure something is selected before loading");
        DISPLAY_MATERIAL(0, splinePoint::rubberLane2, "LANE 2 \nRubbering, tyre wear, oil and dirt \nApplied to only this lane\n make sure something is selected before loading");
        DISPLAY_MATERIAL(0, splinePoint::rubberLane1, "LANE 1 \nRubbering, tyre wear, oil and dirt \nApplied to only this lane\n make sure something is selected before loading");
        DISPLAY_MATERIAL(0, splinePoint::rubberInner, "INNER TURN \nRubbering, tyre wear, oil and dirt \nApplied to only this lane\n make sure something is selected before loading");
    }
    ImGui::EndChildFrame();


    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 20);
    {
        ImGui::SetCursorPosX(350);
        DISPLAY_MATERIAL(1, 0, "CENTERLINE");
        ImGui::SameLine();
        ImGui::SetCursorPosX(500);
        DISPLAY_MATERIAL(1, 1, "CENTER CRACKS");
    }
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 20);


    tempY = ImGui::GetCursorPosY();
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.02f, 0.02f, 0.07f, 1.f);
    ImGui::BeginChildFrame(200, ImVec2(660, 280), 0);
    for (int i = 0; i < 9; i++) {
        style.Colors[ImGuiCol_Button] = ImVec4(0.22f, 0.02f, 0.02f, 0.05f);
        if (editRight && editLaneIndex == i) { style.Colors[ImGuiCol_Button] = ImVec4(0.22f, 0.02f, 0.02f, 0.95f); }
        if (ImGui::Button(laneNames[i].c_str(), ImVec2(110, 0))) { editLaneIndex = i; editRight = true; }

        ImGui::SameLine();
        ImGui::SetCursorPosX(90);
        ImGui::PushItemWidth(90);
        ImGui::PushID(999 + i);
        {
            {
                if (i == 0) { DISPLAY_WIDTH(i, -10, 50, lanesRight); }
                else { DISPLAY_WIDTH(i, 0, 50, lanesRight); }
            }
        }
        ImGui::PopItemWidth();
        ImGui::PopID();
    }
    ImGui::EndChildFrame();






    style.Colors[ImGuiCol_Button] = ImVec4(0.01f, 0.05f, 0.08f, 0.9f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.0f);
    ImGui::SetCursorPos(ImVec2(200, tempY));
    ImGui::BeginChildFrame(201, ImVec2(150, 260), 0);
    {

        ImGui::SetCursorPosY(lineY * 3);
        DISPLAY_MATERIAL(2, splinePoint::tarmac, "ROAD SURFACE \nLine between the outermost lane and the shoulder.\n make sure something is selected before loading");
        ImGui::SetCursorPosY(lineY * 6);
        DISPLAY_MATERIAL(2, splinePoint::gutter, "GUTTER \nLine between the outermost lane and the shoulder.\n make sure something is selected before loading");
        DISPLAY_MATERIAL(2, splinePoint::sidewalk, "SIDEWALK \nLine between the outermost lane and the shoulder.\n make sure something is selected before loading");
        DISPLAY_MATERIAL(2, splinePoint::verge, "VERGE \nLine between the outermost lane and the shoulder.\n make sure something is selected before loading");
    }
    ImGui::EndChildFrame();

    ImGui::SetCursorPos(ImVec2(350, tempY));
    ImGui::BeginChildFrame(202, ImVec2(150, 280), 0);
    {
        DISPLAY_MATERIAL(2, splinePoint::innerShoulder, "INNER SHOULDER \nLine between a traffic island and the first lane.\n make sure something is selected before loading");
        DISPLAY_MATERIAL(2, splinePoint::innerTurn, "INNER TURN LANE \nUsually dotted line to get into a turn lane \nApplied to the inside of the lane next to it.\n make sure something is selected before loading");
        DISPLAY_MATERIAL(2, splinePoint::lane1, "LANE 1 \nOutside line to the next lane if there is one. \nOnly applies to lane 2 or 3 if they exist.\n make sure something is selected before loading");
        DISPLAY_MATERIAL(2, splinePoint::lane2, "LANE 2 \nOutside line to the next lane if there is one. \nOnly applies to lane 3 it it exists.\n make sure something is selected before loading");
        ImGui::NewLine();
        DISPLAY_MATERIAL(2, splinePoint::outerTurn, "OUTER TURN LANE \nUsually dotted line to get into a turn lane \nApplied to the outside of the lane next to it.\n make sure something is selected before loading");
        DISPLAY_MATERIAL(2, splinePoint::shoulder, "SHOULDER \nLine between the outermost lane and the shoulder.\n make sure something is selected before loading");
    }
    ImGui::EndChildFrame();

    ImGui::SetCursorPos(ImVec2(500, tempY));
    ImGui::BeginChildFrame(203, ImVec2(150, 280), 0);
    {
        ImGui::SetCursorPosY(lineY * 1);
        DISPLAY_MATERIAL(2, splinePoint::rubberInner, "INNER TURN \nRubbering, tyre wear, oil and dirt \nApplied to only this lane\n make sure something is selected before loading");
        DISPLAY_MATERIAL(2, splinePoint::rubberLane1, "LANE 1 \nRubbering, tyre wear, oil and dirt \nApplied to only this lane\n make sure something is selected before loading");
        DISPLAY_MATERIAL(2, splinePoint::rubberLane2, "LANE 2 \nRubbering, tyre wear, oil and dirt \nApplied to only this lane\n make sure something is selected before loading");
        DISPLAY_MATERIAL(2, splinePoint::rubberLane3, "LANE 3 \nRubbering, tyre wear, oil and dirt \nApplied to only this lane\n make sure something is selected before loading");
        DISPLAY_MATERIAL(2, splinePoint::rubberOuter, "OUTER TURN 2 \nRubbering, tyre wear, oil and dirt \nApplied to only this lane\n make sure something is selected before loading");

    }
    ImGui::EndChildFrame();



    // Load and save section
    style.Colors[ImGuiCol_Button] = ImVec4(0.022f, 0.052f, 0.072f, 0.9f);
    ImGui::NewLine();
    tempY = ImGui::GetCursorPosY();
    float cursorBelow = ImGui::GetCursorPosY();
    tempY = 150;


    ImGui::SetCursorPos(ImVec2(200, tempY));
    if (ImGui::Button("Load all", ImVec2(100, 0)))
    {
        loadRoadMaterialsAll(_road, selectFrom, selectTo);
    }

    ImGui::SetCursorPos(ImVec2(300, tempY));
    if (ImGui::Button("Save all", ImVec2(100, 0)))
    {
        saveRoadMaterials(_road, selectFrom);
    }


    ImGui::SetCursorPos(ImVec2(200, tempY + 50));
    if (ImGui::Button("Load tarmac", ImVec2(140, 0))) {
        loadRoadMaterialsAsphalt(_road, selectFrom, selectTo);
    }

    ImGui::SetCursorPos(ImVec2(200, tempY + 80));
    if (ImGui::Button("Load verges", ImVec2(140, 0))) {
        loadRoadMaterialsVerge(_road, selectFrom, selectTo);
    }




    ImGui::SetCursorPos(ImVec2(350, tempY + 80));
    if (ImGui::Button("Load lines", ImVec2(140, 0))) {
        loadRoadMaterialsLines(_road, selectFrom, selectTo);
    }
    //	ImGui::SetCursorPos(ImVec2(580, tempY));
    //	if (ImGui::Button("Save lines", ImVec2(100, 0))) { ; }

    ImGui::SetCursorPos(ImVec2(500, tempY + 80));
    if (ImGui::Button("Load w&t", ImVec2(140, 0))) {
        loadRoadMaterialsWT(_road, selectFrom, selectTo);
    }
    //	ImGui::SetCursorPos(ImVec2(820, tempY));
    //	if (ImGui::Button("Save w&t", ImVec2(100, 0))) { ; }


    ImGui::SetCursorPos(ImVec2(0, cursorBelow));
    if (ImGui::Button("Load geom", ImVec2(50, 0))) { loadRoadGeometry(_road, selectFrom, selectTo); }
    ImGui::SetCursorPos(ImVec2(50, cursorBelow));
    if (ImGui::Button("Save geom", ImVec2(50, 0))) { saveRoadGeometry(_road, selectFrom); }


    ImGui::NewLine();
    ImGui::Text("Sampler offsets");
    if (ImGui::DragFloat("Height", &_road->points[_vertex].height_AboveAnchor, 0.1f, -10.0f, 10.0f, "%2.2f m")) {
        _road->points[_vertex].solveMiddlePos();
    }
    if (ImGui::DragFloat("Tangent", &_road->points[_vertex].tangent_Offset, 0.1f, -50.0f, 50.0f, "%2.2f m")) {
        _road->points[_vertex].solveMiddlePos();
    }


    ImGui::NewLine();

    ImGui::Checkbox("is bridge", &_road->points[_vertex].isBridge);
    if (_road->points[_vertex].isBridge)
    {
        char txt[256];
        strncpy(txt, _road->points[_vertex].bridgeName.c_str(), _road->points[_vertex].bridgeName.length());
        if (ImGui::InputText("name", &txt[0], 256))
        {
            _road->points[_vertex].bridgeName = txt;
        }

    }

    ImGui::Checkbox("isAIonly", &_road->points[_vertex].isAIonly);
    ImGui::NewLine();
    


    style = oldStyle;

    return showMaterials;
}



void roadNetwork::renderGUI(Gui* _gui)
{
    ImGui::PushFont(_gui->getFont("roboto_26"));
    {
        int numRoads = roadSectionsList.size();
        int numOverlay = 0;
        for (auto& R : roadSectionsList)
        {
            if (R.isBasicLinearMarking) {
                numOverlay++;
                numRoads--;
            }
        }
        ImGui::Text("%d roads", numRoads);
        ImGui::Text("%d overlay", numOverlay);
        ImGui::Text("%d intersections", (int)intersectionList.size());
        ImGui::Text("%4.2f km   (%d%%)", getDistance() / 1000.0f, getDone());
    }
    ImGui::PopFont();

    char txt[256];
    sprintf(txt, "#bezier - %d (%d Kb)", (int)debugNumBezier, ((int)debugNumBezier * (int)sizeof(cubicDouble)) / 1024);
    ImGui::Text(txt);
    sprintf(txt, "#layer - %d (%d Kb)", (int)debugNumIndex, ((int)debugNumIndex * (int)sizeof(bezierLayer) / 1024));
    ImGui::Text(txt);
    sprintf(txt, "#ODE - %d (%d Kb)", (int)odeBezier.bezierBounding.size(), ((int)odeBezier.bezierBounding.size() * (int)sizeof(physicsBezier)) / 1024);
    ImGui::Text(txt);
    TOOLTIP("Data allocated to Open Dynamics Engine physics representations ???\nNot sure if this should stay");

    ImGui::NewLine();

    float W = 100;// ImGui::GetWindowWidth() / 2 - 15;
    auto& style = ImGui::GetStyle();
    style.ButtonTextAlign = ImVec2(0.0, 0.5);
    style.Colors[ImGuiCol_Button] = ImVec4(0.03f, 0.03f, 0.3f, 0.5f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.03f, 0.03f, 0.3f, 0.5f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.03f, 0.03f, 0.3f, 0.5f);
    if (ImGui::Button("Load last file")) {
        load(lastUsedFilename);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(lastUsedFilename.string().c_str());

    if (ImGui::Button("Load", ImVec2(W, 0))) {
        load();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save", ImVec2(W, 0))) { saveDialog(); }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("'ctrl - s' for quick save");

    static int upgradeFrom = ROADNETWORK_CEREAL_VERSION - 1;
    ImGui::SetNextItemWidth(W);
    ImGui::DragInt("##upgrade", &upgradeFrom, 1, 100, ROADNETWORK_CEREAL_VERSION - 1);
    ImGui::SameLine();
    if (ImGui::Button("Upgrade", ImVec2(W, 0))) { upgrade(upgradeFrom); }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Use this ONLY when the branchData format has changed and you HAVE to upgrade\n It will reset branchData in your file otherwise to their default value");

    ImGui::Separator();
    ImGui::NewLine();
}



void roadNetwork::renderGUI_3d(Gui* mpGui)
{
    if (ImGui::BeginPopupContextWindow(false))
    {
        if (ImGui::Selectable("New Ecotope 3D")) { ; }
        if (ImGui::Selectable("Load")) { ; }
        if (ImGui::Selectable("Save")) { ; }

        ImGui::EndPopup();
    }
}



void roadNetwork::newRoadSpline()
{
    roadSectionsList.emplace_back();
    std::vector<roadSection>::iterator it = roadSectionsList.end();
    it--;
    currentRoad = &(*it);
    currentRoad->GUID = (int)roadSectionsList.size() - 1;
    currentIntersection = nullptr;
}


void roadNetwork::newRoadSplineBasic()
{
    roadSectionsList.emplace_back();
    std::vector<roadSection>::iterator it = roadSectionsList.end();
    it--;
    currentRoad = &(*it);
    currentRoad->GUID = (int)roadSectionsList.size() - 1;
    currentIntersection = nullptr;

    // now make it basic
    currentRoad->isBasicLinearMarking = true;
}




void roadNetwork::newIntersection()
{
    intersectionList.emplace_back();
    currentIntersection = &intersectionList.back();
    currentIntersection->GUID = (int)intersectionList.size() - 1;	// cant we automate?
    currentRoad = nullptr;
}



float roadNetwork::getDistance() {
    float total = 0;
    uint numRoads = (uint)roadSectionsList.size();
    for (uint i = 0; i < numRoads; i++) {
        roadSection* pRoad = &roadSectionsList[i];
        if (!pRoad->isBasicLinearMarking) {
            total += pRoad->getDistance();
        }
    }

    return total;
}



int roadNetwork::getDone()
{
    float total = 0;
    float q = 0;
    uint numRoads = (uint)roadSectionsList.size();
    for (uint i = 0; i < numRoads; i++) {
        roadSection* pRoad = &roadSectionsList[i];
        total += pRoad->getDistance();

        if (pRoad->buildQuality == 2) {
            q += pRoad->getDistance();
        }
    }

    return (int)(q / total * 100.0f);
}



void roadNetwork::load()
{
    std::filesystem::path path;
    FileDialogFilterVec filters = { {"roadnetwork"} };
    if (openFileDialog(filters, lastUsedFilename))
    {
        load(lastUsedFilename, ROADNETWORK_CEREAL_VERSION);
    }
    updateAllRoads();
}



void roadNetwork::load(std::filesystem::path _path, uint _version)
{
    lastUsedFilename = _path;

    std::ifstream is(_path, std::ios::binary);
    if (!is.fail()) {
        cereal::BinaryInputArchive archive(is);
        serialize(archive, _version);

        for (auto& roadSection : roadSectionsList) {
            if (roadSection.int_GUID_start < intersectionList.size()) {
                roadSection.startLink = intersectionList.at(roadSection.int_GUID_start).findLink(roadSection.GUID);
                //roadSection.startLink->roadPtr = &roadSectionsList.at(roadSection.int_GUID_start);
            }
            if (roadSection.int_GUID_end < intersectionList.size()) {
                roadSection.endLink = intersectionList.at(roadSection.int_GUID_end).findLink(roadSection.GUID);
                //roadSection.endLink->roadPtr = &roadSectionsList.at(roadSection.int_GUID_end);
            }
            roadSection.solveRoad();
            roadSection.solveRoad();
        }

        roadMaterialCache::getInstance().reloadMaterials();
        terrafectorEditorMaterial::static_materials.rebuildAll();

    }

    currentRoad = nullptr;
}


void roadNetwork::upgrade(uint _FROM)
{
    std::filesystem::path path;
    FileDialogFilterVec filters = { {"roadnetwork"} };
    if (openFileDialog(filters, lastUsedFilename))
    {
        load(lastUsedFilename, _FROM);
    }

    char _upgrade[256];
    sprintf(_upgrade, "%s.%d.roadnetwork", lastUsedFilename.string().c_str(), ROADNETWORK_CEREAL_VERSION);
    lastUsedFilename = _upgrade;
    save();
}

void roadNetwork::save()
{
    std::ofstream os(lastUsedFilename, std::ios::binary);
    cereal::BinaryOutputArchive archive(os);
    serialize(archive, ROADNETWORK_CEREAL_VERSION);
}

void roadNetwork::saveDialog()
{
    std::filesystem::path path;
    FileDialogFilterVec filters = { {"roadnetwork"} };
    if (saveFileDialog(filters, lastUsedFilename))
    {
        save();
    }
}



bool replace(std::string& str, const std::string& from, const std::string& to) {
    size_t start_pos = str.find(from);
    if (start_pos == std::string::npos)
        return false;
    str.replace(start_pos, from.length(), to);
    return true;
}



void roadNetwork::quickSave()
{
    time_t now = time(0);
    tm* ltm = localtime(&now);

    char _time[256];
    sprintf(_time, "_%d_%d_%d_%d_%d.roadnetwork", ltm->tm_mon, ltm->tm_mday, ltm->tm_hour, ltm->tm_min, ltm->tm_sec);

    std::string newname = lastUsedFilename.string();
    newname.replace(newname.find(".roadnetwork"), sizeof(".roadnetwork") - 1, _time);

    std::ofstream os(newname, std::ios::binary);
    cereal::BinaryOutputArchive archive(os);
    serialize(archive, 101);
}

/*
bool replace(std::string& str, const std::string& from, const std::string& to) {
    size_t start_pos = str.find(from);
    if (start_pos == std::string::npos)
        return false;
    str.replace(start_pos, from.length(), to);
    return true;
}
*/
void roadNetwork::exportBinary() {

    char name[256];
    std::filesystem::path path;
    FileDialogFilterVec filters = { {"gpu"} };
    if (saveFileDialog(filters, path))
    {
        sprintf(name, "attrib -r \"%s\"", path.string().c_str());
        system(name);

        updateAllRoads(true);

        FILE* file = fopen(path.string().c_str(), "wb");
        if (file) {
            uint matSize = (uint)terrafectorEditorMaterial::static_materials.materialVector.size();
            uint textureSize = (uint)terrafectorEditorMaterial::static_materials.textureVector.size();

            fwrite(&textureSize, 1, sizeof(uint), file);// textures
            fwrite(&matSize, 1, sizeof(uint), file);// materials
            fwrite(&debugNumBezier, 1, sizeof(uint), file);
            fwrite(&debugNumIndex, 1, sizeof(uint), file);


            char name[512];
            for (uint i = 0; i < textureSize; i++) {
                std::string path = terrafectorEditorMaterial::static_materials.textureVector[i]->getSourcePath().string();
                path = path.substr(13, path.size());
                path = path.substr(0, path.rfind("."));
                path += ".texture";
                memset(name, 0, 512);
                sprintf(name, "%s", path.c_str());
                fwrite(name, 1, 512, file);
            }

            // materials
            for (uint i = 0; i < matSize; i++) {
                fwrite(&terrafectorEditorMaterial::static_materials.materialVector[i]._constData, 1, sizeof(TF_material), file);
            }




            for (uint i = 0; i < debugNumBezier; i++) {
                //fwrite(staticBezierData.data(), debugNumBezier, sizeof(cubicDouble), file);
                fwrite(&staticBezierData[i], 1, sizeof(cubicDouble), file);
            }

            for (uint i = 0; i < debugNumIndex; i++) {
                //fwrite(staticIndexData.data(), debugNumIndex, sizeof(bezierLayer), file);
                fwrite(&staticIndexData[i], 1, sizeof(bezierLayer), file);
            }

            fclose(file);
        }
        std::string filename = path.string();
        replace(filename, "gpu", "ode");
        sprintf(name, "attrib -r \"%s\"", filename.c_str());
        system(name);

        file = fopen(filename.c_str(), "wb");
        if (file) {
            uint size = (uint)odeBezier.bezierBounding.size();
            fwrite(&size, 1, sizeof(uint), file);
            for (uint i = 0; i < size; i++) {
                //fwrite(odeBezier.bezierBounding.data(), size, sizeof(physicsBezier), file);
                //fwrite(&odeBezier.bezierBounding[i], 1, sizeof(physicsBezier), file);
                odeBezier.bezierBounding[i].binary_export(file);
            }

            odeBezier.buildGridFromBezier();
            odeBezier.gridLookup.binary_export(file);

            fclose(file);
        }


        replace(filename, "ode", "ai");
        sprintf(name, "attrib -r \"%s\"", filename.c_str());
        system(name);

        file = fopen(filename.c_str(), "wb");
        if (file) {
            uint size = (uint)odeBezier.bezierAI.size();
            fwrite(&size, 1, sizeof(uint), file);
            for (uint i = 0; i < size; i++) {
                odeBezier.bezierAI[i].binary_export(file);
            }

            //odeBezier.buildGridFromBezier();
            //odeBezier.gridLookup.binary_export(file);

            fclose(file);
        }
    }
}

std::string blockFromPosition(glm::vec3 _pos)
{
    uint y = (uint)floor((_pos.z + ecotopeSystem::terrainSize * 0.5f) / (ecotopeSystem::terrainSize / 16.f));
    uint x = (uint)floor((_pos.x + ecotopeSystem::terrainSize * 0.5f) / (ecotopeSystem::terrainSize / 16.f));
    std::string answer = char(65 + x) + std::to_string(y);
    return answer;
}

void roadNetwork::exportBridges() {

    aiScene* scene = new aiScene;
    scene->mRootNode = new aiNode();

    scene->mMaterials = new aiMaterial * [1];
    scene->mMaterials[0] = nullptr;
    scene->mNumMaterials = 1;

    scene->mMaterials[0] = new aiMaterial();

    scene->mMeshes = new aiMesh * [1];
    scene->mMeshes[0] = new aiMesh();
    scene->mMeshes[0]->mMaterialIndex = 0;
    scene->mNumMeshes = 1;

    scene->mRootNode->mMeshes = new unsigned int[1];
    scene->mRootNode->mMeshes[0] = 0;
    scene->mRootNode->mNumMeshes = 1;

    //scene->mMetaData = new aiMetadata(); // workaround, issue #3781

    auto pMesh = scene->mMeshes[0];

    pMesh->mVertices = new aiVector3D[128];
    pMesh->mNumVertices = 128;

    pMesh->mFaces = new aiFace[63];
    pMesh->mNumFaces = 63;
    pMesh->mPrimitiveTypes = aiPrimitiveType_POLYGON;

    for (uint i = 0; i < 63; i++) {
        aiFace& face = pMesh->mFaces[i];

        face.mIndices = new unsigned int[4];
        face.mNumIndices = 4;

        face.mIndices[0] = i * 2 + 0;
        face.mIndices[1] = i * 2 + 1;
        face.mIndices[2] = i * 2 + 3;
        face.mIndices[3] = i * 2 + 2;
    }


    char listfilename[256];
    sprintf(listfilename, "%s_export//bridges//bridgelist.txt", rootPath.c_str());
    FILE* listfile = fopen(listfilename, "w");
    if (listfile)
    {
        for (auto& road : roadSectionsList)
        {
            int cnt = 0;
            for (auto& pnt : road.points)
            {
                if (pnt.isBridge)
                {
                    glm::vec3 origin = (pnt.bezier[left].pos + road.points[cnt + 1].bezier[left].pos + pnt.bezier[right].pos + road.points[cnt + 1].bezier[right].pos) * 0.25f;
                    fprintf(listfile, "%5.4f, %5.4f, %5.4f, %s\n", origin.x, origin.y, origin.z, pnt.bridgeName.c_str());

                    char filename[256];
                    sprintf(filename, "%s_export//bridges//bridge_%s_%s.obj", rootPath.c_str(), blockFromPosition(origin).c_str(), pnt.bridgeName.c_str());
                    if (pnt.bridgeName.length() == 0) {
                        sprintf(filename, "%s_export//bridges//%splease name me %d.dae", rootPath.c_str(), blockFromPosition(origin).c_str(), cnt);
                    }

                    {
                        for (int y = 0; y < 64; y++) {
                            float t = (float)y / 63.0f;
                            bezierPoint* pntThis = &pnt.bezier[left];
                            bezierPoint* pntNext = &road.points[cnt + 1].bezier[left];
                            glm::vec3 A = cubic_Casteljau(t, pntThis, pntNext);// -origin;

                            pntThis = &pnt.bezier[right];
                            pntNext = &road.points[cnt + 1].bezier[right];
                            glm::vec3 B = cubic_Casteljau(t, pntThis, pntNext);// -origin; absolute coordinates

                            pMesh->mVertices[y * 2 + 0] = aiVector3D(A.x, A.y, A.z);
                            pMesh->mVertices[y * 2 + 1] = aiVector3D(B.x, B.y, B.z);
                        }
                    }
                    Exporter exp;
                    //exp.Export(scene, "fbx", filename);
                    exp.Export(scene, "obj", filename);
                }
                cnt++;
            }
        }

        fclose(listfile);
    }
}


void roadNetwork::exportRoads(int _numSplits)
{
    float blocksize = ecotopeSystem::terrainSize / 16.f;
    float halfsize = ecotopeSystem::terrainSize / 2.f;

    for (uint y = 0; y < 16; y++)
    {
        for (uint x = 0; x < 16; x++)
        {
            std::string answer = char(65 + x) + std::to_string(y);
            glm::vec3 center = glm::vec3(x * blocksize - halfsize + (blocksize / 2), 0, y * blocksize - halfsize + (blocksize / 2));
            exportRoads(_numSplits, center, blocksize * 0.66f, answer);
        }
    }
}


void roadNetwork::fillMesh(roadSection& road, aiMesh* _mesh, uint _numsplits, bool bLeft, uint lane)
{
    uint numBezier = (uint)road.points.size() - 1;
    uint numfaces = _numsplits - 1;

    _mesh->mFaces = new aiFace[numfaces * numBezier];
    _mesh->mNumFaces = numfaces * numBezier;
    _mesh->mPrimitiveTypes = aiPrimitiveType_POLYGON;
    _mesh->mVertices = new aiVector3D[_numsplits * 2 * numBezier];
    _mesh->mNumVertices = _numsplits * 2 * numBezier;

    for (uint bez = 0; bez < numBezier; bez++)
    {
        for (uint i = 0; i < numfaces; i++)
        {
            aiFace& face = _mesh->mFaces[bez * numfaces + i];

            face.mIndices = new unsigned int[4];
            face.mNumIndices = 4;

            face.mIndices[0] = (bez * _numsplits + i) * 2 + 0;
            face.mIndices[1] = (bez * _numsplits + i) * 2 + 1;
            face.mIndices[2] = (bez * _numsplits + i) * 2 + 3;
            face.mIndices[3] = (bez * _numsplits + i) * 2 + 2;
        }

        bezierPoint* pntOut1 = &road.points[bez].bezier[left];
        bezierPoint* pntOut2 = &road.points[bez + 1].bezier[left];
        bezierPoint* pntIn1 = &road.points[bez].bezier[middle];
        bezierPoint* pntIn2 = &road.points[bez + 1].bezier[middle];

        for (int y = 0; y < _numsplits; y++)
        {
            float t = (float)y / (float)numfaces;
            glm::vec3 A = cubic_Casteljau(t, pntOut1, pntOut2);
            glm::vec3 B = cubic_Casteljau(t, pntIn1, pntIn2);

            if (road.points[bez].isBridge || road.points[bez].isAIonly)
            {
                _mesh->mVertices[bez * _numsplits * 2 + y * 2 + 0] = aiVector3D(0, 0, 0);
                _mesh->mVertices[bez * _numsplits * 2 + y * 2 + 1] = aiVector3D(0, 0, 0);
            }
            else
            {
                _mesh->mVertices[bez * _numsplits * 2 + y * 2 + 0] = aiVector3D(A.x, A.y, A.z);
                _mesh->mVertices[bez * _numsplits * 2 + y * 2 + 1] = aiVector3D(B.x, B.y, B.z);
            }
        }
    }
}

void roadNetwork::exportRoads(int _numSplits, glm::vec3 _center, float _size, std::string _blockName)
{
    int numRoad = (int)roadSectionsList.size();

    aiScene* scene = new aiScene;
    scene->mRootNode = new aiNode();

    scene->mMaterials = new aiMaterial * [1];
    scene->mMaterials[0] = nullptr;
    scene->mNumMaterials = 1;

    scene->mMaterials[0] = new aiMaterial();

    scene->mMeshes = new aiMesh * [numRoad];
    scene->mRootNode->mMeshes = new unsigned int[numRoad];
    for (int i = 0; i < numRoad; i++)
    {
        scene->mMeshes[i] = nullptr;
        scene->mRootNode->mMeshes[i] = i;
    }
    scene->mNumMeshes = numRoad;
    scene->mRootNode->mNumMeshes = numRoad;

    bool anyRoads = false;

    int cnt = 0;
    for (auto& road : roadSectionsList)
    {
        scene->mMeshes[cnt] = new aiMesh();
        scene->mMeshes[cnt]->mMaterialIndex = 0;
        scene->mMeshes[cnt]->mName = _blockName;
        auto pMesh = scene->mMeshes[cnt];
        uint numBez = (uint)road.points.size() - 1;

        // determine intersect
        bool aabb = false;
        for (auto& p : road.points)
        {
            glm::vec3 test = glm::abs(p.anchor - _center);
            if (test.x < _size && test.z < _size)
            {
                aabb = true;
            }
        }

        if (aabb && (road.points.size() > 2))
        {
            anyRoads = true;

            {
                pMesh->mNumFaces = (_numSplits - 1) * numBez * 6;
                pMesh->mFaces = new aiFace[pMesh->mNumFaces];
                pMesh->mPrimitiveTypes = aiPrimitiveType_POLYGON;

                for (uint j = 0; j < numBez; j++)
                {
                    for (uint i = 0; i < _numSplits - 1; i++)
                    {
                        for (uint k = 0; k < 6; k++)
                        {
                            aiFace& face = pMesh->mFaces[(j * (_numSplits - 1) + i) * 6 + k];

                            face.mIndices = new unsigned int[4];
                            face.mNumIndices = 4;

                            face.mIndices[0] = (j * _numSplits + i) * 7 + (k + 0);
                            face.mIndices[1] = (j * _numSplits + i) * 7 + (k + 1);

                            face.mIndices[2] = (j * _numSplits + i) * 7 + (7 + k + 1);
                            face.mIndices[3] = (j * _numSplits + i) * 7 + (7 + k + 0);
                        }
                    }
                }
            }

            {
                pMesh->mNumVertices = _numSplits * 7 * numBez;
                pMesh->mVertices = new aiVector3D[pMesh->mNumVertices];

                //for (auto &pnt : road.points)
                for (uint ZZ = 0; ZZ < numBez; ZZ++)
                {
                    auto& pnt = road.points[ZZ];

                    for (int y = 0; y < _numSplits; y++)
                    {
                        for (uint k = 0; k < 7; k++)
                        {
                            float total = (float)_numSplits - 1.0f;
                            float t = (float)y / total;
                            splinePoint P2 = road.points[ZZ + 1];

                            glm::vec3 L = cubic_Casteljau(t, &pnt.bezier[left], &P2.bezier[left]);
                            glm::vec3 M = cubic_Casteljau(t, &pnt.bezier[middle], &P2.bezier[middle]);
                            glm::vec3 R = cubic_Casteljau(t, &pnt.bezier[right], &P2.bezier[right]);
                            glm::vec3 PNT;
                            switch (k)
                            {
                            case 0: PNT = L; break;
                            case 1: PNT = glm::lerp(L, M, 0.33f);  PNT.y += 0.05f; break;
                            case 2: PNT = glm::lerp(L, M, 0.67f);  PNT.y += 0.08686315f; break;
                            case 3: PNT = M; PNT.y += 0.1f; break;
                            case 4: PNT = glm::lerp(M, R, 0.33f);  PNT.y += 0.08686315f; break;
                            case 5: PNT = glm::lerp(M, R, 0.67f);  PNT.y += 0.05f; break;
                            case 6: PNT = R; break;
                            }

                            //PNT = L;
                            //if (k == 3) PNT = M;
                            //if (k > 3) PNT = R;

                            pMesh->mVertices[(ZZ * _numSplits + y) * 7 + k] = aiVector3D(PNT.x, PNT.y, PNT.z);
                            /*
                            bezierPoint* pntThis = &pnt.bezier[left];
                            bezierPoint* pntNext = &road.points[ZZ + 1].bezier[left];
                            glm::vec3 A = cubic_Casteljau(t, pntThis, pntNext);// -origin;

                            pntThis = &pnt.bezier[right];
                            pntNext = &road.points[ZZ + 1].bezier[right];
                            glm::vec3 B = cubic_Casteljau(t, pntThis, pntNext);// -origin; absolute coordinates

                            if (pnt.isBridge || pnt.isAIonly)
                            {
                                pMesh->mVertices[ZZ * _numSplits * 2 + y * 2 + 0] = aiVector3D(0, 0, 0);
                                pMesh->mVertices[ZZ * _numSplits * 2 + y * 2 + 1] = aiVector3D(0, 0, 0);
                            }
                            else
                            {
                                pMesh->mVertices[ZZ * _numSplits * 2 + y * 2 + 0] = aiVector3D(A.x, A.y, A.z);
                                pMesh->mVertices[ZZ * _numSplits * 2 + y * 2 + 1] = aiVector3D(B.x, B.y, B.z);
                            }
                            */
                        }
                    }
                }
            }
        }
        else
        {
            pMesh->mNumFaces = 0;
            pMesh->mNumVertices = 0;
        }

        cnt++;
    }

    if (anyRoads)
    {
        char filename[256];
        sprintf(filename, "%s//_export//roads//roads_%s.fbx", rootPath.c_str(), _blockName.c_str());
        Exporter exp;
        exp.Export(scene, "fbx", filename);
        sprintf(filename, "%s//_export//roads//roads_%s.obj", rootPath.c_str(), _blockName.c_str());
        exp.Export(scene, "obj", filename);
    }
    /*
    std::filesystem::path path;
    FileDialogFilterVec filters = { {"fbx"} };
    if (saveFileDialog(filters, path))
    {
        Exporter exp;
        exp.Export(scene, "fbx", path.string().c_str());
    }
    */
}




void roadNetwork::exportAI()
{
    std::filesystem::path path;
    FileDialogFilterVec filters = { {"track_layout"} };
    if (saveFileDialog(filters, path))
    {
        FILE* file = fopen(path.string().c_str(), "wb");
        pathBezier.exportAI(file);
        fclose(file);

        FILE* fileTxt = fopen((path.string() + ".txt").c_str(), "w");
        pathBezier.exportAITXTDEBUG(file);
        fclose(fileTxt);
    }
}



void roadNetwork::loadAI()
{
    std::filesystem::path path;
    FileDialogFilterVec filters = { {"track_layout"} };
    if (openFileDialog(filters, path))
    {
        FILE* file = fopen(path.string().c_str(), "rb");
        pathBezier.load(file);
        fclose(file);
    }
}



void roadNetwork::addRoad() {
    if (bHIT) {
        AI_bezier::road R;
        R.roadIndex = hitRoadGuid;
        R.bForward = true;
        pathBezier.roads.push_back(R);
    }
}



void roadNetwork::exportAI_CSV()
{
    std::filesystem::path path;
    FileDialogFilterVec filters = { {"csv"} };
    if (saveFileDialog(filters, path))
    {
        FILE* file = fopen((path.string() + ".left.csv").c_str(), "w");
        pathBezier.exportCSV(file, 0);
        fclose(file);

        file = fopen((path.string() + ".center.csv").c_str(), "w");
        pathBezier.exportCSV(file, 1);
        fclose(file);

        file = fopen((path.string() + ".right.csv").c_str(), "w");
        pathBezier.exportCSV(file, 2);
        fclose(file);

        file = fopen((path.string() + ".GATES.csv").c_str(), "w");
        pathBezier.exportGates(file);
        fclose(file);
    }
}



/*	temp code to push some material data top all troad to populate Eifel*/
void roadNetwork::TEMP_pushAllMaterial()
{
    if (this->currentRoad)
    {
        for (auto& roadSection : this->roadSectionsList)
        {
            for (auto& point : roadSection.points)
            {
                point.matRight = currentRoad->points[0].matRight;
                point.matLeft = currentRoad->points[0].matLeft;
                point.matCenter = currentRoad->points[0].matCenter;
            }
        }
    }
}



void roadNetwork::currentRoadtoAI()
{
    /*	This is a hacky version
        just push the whole current road
    */
    if (currentRoad)
    {
        pathBezier.clear();

        // Nordsliefe hillclimb
        //pathBezier.startpos = vec3(5713.95f, 561.05f, 1859.83f);
        //pathBezier.endpos = vec3(6623.0f, 550.0f, 2360.0f);

        // Full Nordsliefe not looped yet
        //pathBezier.startpos = vec3(3916.0f, 575.0f, 4942.0f);
        //pathBezier.endpos = vec3(4009.0f, 570.0f, 4883.0f);

        // OVAL
        pathBezier.startpos = glm::vec3(0.0f, 0.5f, 61.0f);
        pathBezier.endpos = glm::vec3(0.0f, 0.5f, 61.0f);

        // IMOLA
        //pathBezier.startpos = vec3(50.3f, 17.08f, -407.9f);
        //pathBezier.endpos = vec3(50.3f, 17.08f, -407.9f);

        // Nordsliefe test best curve
        //pathBezier.startpos = vec3(232.0f, 386.0f, 1431.0f);
        //pathBezier.endpos = vec3(6491.0f, 558.0f, 2412.0f);

        // Hairpin hillclimb backwards
        //pathBezier.startpos = vec3(3383.0f, 409.0f, 2369.0f);
        //pathBezier.endpos = vec3(3220.0f, 369.0f, 2443.0f);

        // CLSOE LOOP GETS FIERST POINT
        pathBezier.isClosedPath = currentRoad->isClosedLoop;
        if (currentRoad->isClosedLoop)
        {
            pathBezier.startpos = currentRoad->points[0].anchor;
            pathBezier.endpos = currentRoad->points[0].anchor;
        }

        uint size = (uint)currentRoad->points.size() - 1;
        glm::vec4 bez[2][4];
        for (uint i = 0; i < size; i++)
        {

            bez[0][0] = currentRoad->points[i].bezier[0].pos_uv();
            bez[0][1] = currentRoad->points[i].bezier[0].forward_uv();
            bez[0][2] = currentRoad->points[i + 1].bezier[0].backward_uv();
            bez[0][3] = currentRoad->points[i + 1].bezier[0].pos_uv();

            bez[1][0] = currentRoad->points[i].bezier[2].pos_uv();
            bez[1][1] = currentRoad->points[i].bezier[2].forward_uv();
            bez[1][2] = currentRoad->points[i + 1].bezier[2].backward_uv();
            bez[1][3] = currentRoad->points[i + 1].bezier[2].pos_uv();

            pathBezier.pushSegment(bez);
        }

        // and the last one
        //bez[0][0] = currentRoad->points[size].bezier[0].pos_uv();
        //bez[1][0] = currentRoad->points[size].bezier[2].pos_uv();
        //pathBezier.pushSegment(bez);

        pathBezier.cacheAll();
        pathBezier.solveStartEnd();
    }
}



void roadNetwork::incrementLane(int index, float _amount, roadSection* _road, bool _symmetrical)
{
    if (index < 0)		// do all
    {
        if (_symmetrical || editRight) {
            for (auto& pnt : _road->points) {
                pnt.lanesRight[editLaneIndex].laneWidth += _amount;
                if ((editLaneIndex > 0) && pnt.lanesRight[editLaneIndex].laneWidth < 0) pnt.lanesRight[editLaneIndex].laneWidth = 0;
            }
        }
        if (_symmetrical || !editRight) {
            for (auto& pnt : _road->points) {
                pnt.lanesLeft[editLaneIndex].laneWidth += _amount;
                if ((editLaneIndex > 0) && pnt.lanesLeft[editLaneIndex].laneWidth < 0) pnt.lanesLeft[editLaneIndex].laneWidth = 0;
            }
        }
    }
    else				// selective
    {
        if (_symmetrical || editRight) {
            _road->points[index].lanesRight[editLaneIndex].laneWidth += _amount;
            if ((editLaneIndex > 0) && _road->points[index].lanesRight[editLaneIndex].laneWidth < 0) _road->points[index].lanesRight[editLaneIndex].laneWidth = 0;
            _road->lastEditedPoint = _road->points[index];
        }
        if (_symmetrical || !editRight) {
            _road->points[index].lanesLeft[editLaneIndex].laneWidth += _amount;
            if ((editLaneIndex > 0) && _road->points[index].lanesLeft[editLaneIndex].laneWidth < 0) _road->points[index].lanesLeft[editLaneIndex].laneWidth = 0;
            _road->lastEditedPoint = _road->points[index];
        }
    }
}



void roadNetwork::updateDynamicRoad()
{
    if (currentRoad || currentIntersection)
    {
        uint bezierCount = 0;
        odeBezier.clear();
        roadNetwork::staticBezierData.clear();
        roadNetwork::staticIndexData.clear();
        roadNetwork::staticIndexData_BakeOnly.clear();

        if (currentRoad) {
            //currentRoad->convertToGPU_Stylized(&bezierCount, false, selectFrom, selectTo);
            currentRoad->convertToGPU_Realistic(staticBezierData, staticIndexData, staticIndexData_BakeOnly, selectFrom, selectTo, true, showMaterials);

        }

        if (currentIntersection) {
            for (int i = 0; i < currentIntersection->roadLinks.size(); i++) {
                currentIntersection->roadLinks[i].roadPtr = &roadSectionsList.at(currentIntersection->roadLinks[i].roadGUID);
                //currentIntersection->roadLinks[i].roadPtr->convertToGPU_Stylized(&bezierCount);
                currentIntersection->roadLinks[i].roadPtr->convertToGPU_Realistic(staticBezierData, staticIndexData, staticIndexData_BakeOnly, 0, 0, true, false);
            }
            // This is for tarmac lanes that we dont do rigthnow
            //currentIntersection->convertToGPU(staticBezierData, staticIndexData);
        }

        debugNumBezier = (uint)staticBezierData.size();
        debugNumIndex = (uint)staticIndexData.size();
    }
}



void roadNetwork::updateAllRoads(bool _forExport)
{
    uint bezierCount = 0;
    odeBezier.clear();
    roadNetwork::staticBezierData.clear();
    roadNetwork::staticIndexData.clear();
    roadNetwork::staticIndexData_BakeOnly.clear();

    for (auto& roadSection : roadSectionsList) {
        roadSection.convertToGPU_Realistic(staticBezierData, staticIndexData, staticIndexData_BakeOnly);
    }

    // 	for (auto &intersection : intersectionList) {
    // 		intersection.convertToGPU(staticBezierData, staticIndexData);
    // 	}

    debugNumBezier = (uint)staticBezierData.size();
    debugNumIndex = (uint)staticIndexData.size();

    odeBezier.buildGridFromBezier();

    isDirty = true;
}

void roadNetwork::physicsTest(glm::vec3 pos)
{
    physicsMouseIntersection.updatePosition(glm::vec2(pos.x, pos.z));
    odeBezier.intersect(&physicsMouseIntersection);
}



void roadNetwork::lanesFromHit()
{
    bHIT = false;
    hitRandomFeedbackValue = 0;

    for (int i = 0; i < physicsMouseIntersection.beziers.size(); i++)
    {
        bezierOneIntersection* pI = &physicsMouseIntersection.beziers[i];
        hitRandomFeedbackValue++;

        if (pI->bHit) {
            bHIT = true;
            hitRoadGuid = pI->roadGUID;
            hitRoadIndex = pI->index;
            hitRoadLane;
            hitRoadRight = pI->bRighthand;
            dA = pI->a;
            dW = pI->W;

            roadSection* road = &roadSectionsList[hitRoadGuid];

            float dA = 1.0f - pI->UV.x;
            float dB = pI->UV.x;
            float sum = 0;
            float widthRight = (road->points[hitRoadIndex].widthRight * dA) + (road->points[hitRoadIndex + 1].widthRight * dB);
            float widthLeft = (road->points[hitRoadIndex].widthLeft * dA) + (road->points[hitRoadIndex + 1].widthLeft * dB);

            pI->a = dB;
            pI->W = widthRight;

            if (pI->bRighthand) {
                for (int i = 0; i < 7; i++) {
                    sum += (road->points[hitRoadIndex].lanesRight[i].laneWidth * dA) + (road->points[hitRoadIndex + 1].lanesRight[i].laneWidth * dB);
                    if (sum / widthRight > pI->UV.y) {
                        hitRoadLane = i;
                        break;
                    }
                }
            }
            else {
                for (int i = 0; i < 7; i++) {
                    sum += (road->points[hitRoadIndex].lanesLeft[i].laneWidth * dA) + (road->points[hitRoadIndex + 1].lanesLeft[i].laneWidth * dB);
                    if (sum / widthLeft > pI->UV.y) {
                        hitRoadLane = i;
                        break;
                    }
                }
            }
        }
    }
}



void roadNetwork::testHit(glm::vec3 pos)
{
    physicsMouseIntersection.updatePosition(glm::vec2(pos.x, pos.z));
    odeBezier.intersect(&physicsMouseIntersection);

    lanesFromHit();
}



void roadNetwork::testHitAI(glm::vec3 pos)
{
    AIpathIntersect.setPos(glm::vec2(pos.x, pos.z));

    for (uint i = 0; i < 1000; i++)
    {
        pathBezier.intersect(&AIpathIntersect);
    }
}



void roadNetwork::doSelect(glm::vec3 pos)
{
    // first subselection on curren road
    if (currentRoad)
    {
        for (int j = 0; j < currentRoad->points.size(); j++) {
            if (glm::length(currentRoad->points[j].anchor - pos) < 3.0f)
            {
                currentRoad->addSelection(j);
                return;
            }
        }
    }

    float distance = 10000;
    // then intersections
    for (int i = 0; i < intersectionList.size(); i++) {
        float L = glm::length(intersectionList[i].anchor - pos);
        if (L < 10.0f && L < distance) {
            currentIntersection = &intersectionList[i];
            distance = L;
            currentRoad = nullptr;
        }
    }

    //then new selectiosn on roads in general
    for (int i = 0; i < roadSectionsList.size(); i++) {
        for (int j = 0; j < roadSectionsList[i].points.size(); j++) {
            float L = glm::length(roadSectionsList[i].points[j].anchor - pos);
            if (L < 15.0f && L < distance)
            {
                distance = L;
                currentIntersection = nullptr;
                currentRoad = &roadSectionsList[i];				// new selection
                currentRoad->newSelection(j);
                currentRoad->solveRoad();
            }
        }
    }

    /*
    if (!currentIntersection)
    {
        currentRoad = &roadSectionsList[871];
    }*/
}



void roadNetwork::currentIntersection_findRoads()
{
    intersectionRoadLink link;
    currentIntersection->roadLinks.clear();

    float linkDistance = 10.0f;		// if closer than this, consider it a link

    for (auto& road : roadSectionsList)
    {
        if (road.points.size() >= 3)
        {
            if (glm::length(road.points[0].anchor - currentIntersection->anchor) < linkDistance)
            {
                road.int_GUID_start = currentIntersection->GUID;
                road.startLink = currentIntersection->findLink(road.GUID);

                link.roadPtr = &road;
                link.roadGUID = road.GUID;
                link.broadStart = true;
                float3 dirStart = road.points[1].anchor - currentIntersection->anchor;
                link.angle = atan2(dirStart.x, dirStart.z);
                currentIntersection->roadLinks.push_back(link);
            }
            else if (glm::length(road.points[road.points.size() - 1].anchor - currentIntersection->anchor) < linkDistance)
            {
                road.int_GUID_end = currentIntersection->GUID;
                road.endLink = currentIntersection->findLink(road.GUID);

                link.roadPtr = &road;
                link.roadGUID = road.GUID;
                link.broadStart = false;
                float3 dirEnd = road.points[road.points.size() - 2].anchor - currentIntersection->anchor;
                link.angle = atan2(dirEnd.x, dirEnd.z);
                currentIntersection->roadLinks.push_back(link);
            }
        }
    }

    std::sort(currentIntersection->roadLinks.begin(), currentIntersection->roadLinks.end());
    solveIntersection(true);
}



void roadNetwork::solve2RoadIntersection()
{
    for (auto& link : currentIntersection->roadLinks)								// move the road vertex to the intersection position
    {
        if (link.broadStart) {
            link.roadPtr->points.front().forceAnchor(currentIntersection->getAnchor(), currentIntersection->anchorNormal, currentIntersection->lidarLOD);
        }
        else {
            link.roadPtr->points.back().forceAnchor(currentIntersection->getAnchor(), currentIntersection->anchorNormal, currentIntersection->lidarLOD);
        }
    }
}



void roadNetwork::solveIntersection(bool _solveTangents)
{
    int numRoads = (int)currentIntersection->roadLinks.size();
    if (numRoads < 2) return;														// stop breaking the single road intersections

    //if (numRoads == 2) return solve2RoadIntersection();	// Do later but important


    for (auto& link : currentIntersection->roadLinks)								// move the road vertex to the intersection position
    {
        if (link.broadStart)
        {
            if (numRoads > 2) link.roadPtr->points.front().bAutoGenerate = false;
            link.roadPtr->points.front().forceAnchor(currentIntersection->getAnchor(), currentIntersection->anchorNormal, currentIntersection->lidarLOD);
        }
        else
        {
            if (numRoads > 2) link.roadPtr->points.back().bAutoGenerate = false;
            link.roadPtr->points.back().forceAnchor(currentIntersection->getAnchor(), currentIntersection->anchorNormal, currentIntersection->lidarLOD);
        }
    }


    // solve forward
    for (int i = 0; i < numRoads; i++)
    {
        intersectionRoadLink* link_A = &currentIntersection->roadLinks[i];
        intersectionRoadLink* link_B = &currentIntersection->roadLinks[(i + 1) % numRoads];
        splinePoint* pnt_A, * pnt_B;
        float width_A, width_B;

        if (link_A->broadStart) {
            pnt_A = &link_A->roadPtr->points[1];
            width_A = pnt_A->widthLeft;
        }
        else {
            pnt_A = &link_A->roadPtr->points[link_A->roadPtr->points.size() - 2];
            width_A = pnt_A->widthRight;
        }

        if (link_B->broadStart) {
            pnt_B = &link_B->roadPtr->points[1];
            width_B = pnt_B->widthRight;
        }
        else {
            pnt_B = &link_B->roadPtr->points[link_B->roadPtr->points.size() - 2];
            width_B = pnt_B->widthLeft;
        }

        float3 dir_A = glm::normalize(pnt_A->anchor - currentIntersection->getAnchor());
        float3 dir_B = glm::normalize(pnt_B->anchor - currentIntersection->getAnchor());
        link_A->theta = acos(glm::dot(dir_A, dir_B));
        float sintheta = sin(link_A->theta);
        float costheta = cos(link_A->theta);


        float3 right_A = glm::normalize(glm::cross(-dir_A, currentIntersection->anchorNormal));
        float3 right_B = glm::normalize(glm::cross(dir_B, currentIntersection->anchorNormal));

        dir_A = glm::normalize(glm::cross(currentIntersection->anchorNormal, -right_A));
        dir_B = glm::normalize(glm::cross(currentIntersection->anchorNormal, right_B));

        float3 vec_C, pos_C, tangent_C;

        if (link_A->cornerType == typeOfCorner::automatic) {
            link_A->cornerRadius = __max(2.0f, pow(width_A + width_B, 0.5f) * 1.4f * link_A->theta);		/// FIXME this can do with a project wide scale adjustment but seems good for now
        }

        if ((glm::dot(right_A, dir_B) < 0.5) || (numRoads == 2))			// this is an open corner, flat side of a T junction
        {
            float distance = (width_A + width_B) / 2.0f;
            //vec_C = glm::normalize(glm::normalize(right_A) + glm::normalize(right_B)) * distance;
            vec_C = (right_A * width_A + right_B * width_B) * 0.5f;
            tangent_C = glm::normalize(glm::cross(vec_C, currentIntersection->anchorNormal));
            pos_C = currentIntersection->getAnchor() + vec_C;
            link_A->bOpenCorner = true;

            link_A->pushBack_A = distance * 2;
            link_B->pushBack_B = distance * 2;
        }
        else {
            vec_C = dir_B * ((width_A + link_A->cornerRadius) / sintheta) + dir_A * ((width_B + link_A->cornerRadius) / sintheta);			//(wo + r0)/sintheta 5, 3
            //float3 C0 = dir_B * (width_A / sintheta);
            //float3 C1 = dir_A * (width_B / sintheta);
            //tangent_C = glm::normalize(C1 - C0);
            tangent_C = glm::normalize(glm::cross(vec_C, currentIntersection->anchorNormal));
            //float3 Cdir = glm::normalize(glm::normalize(C0) + glm::normalize(C1));
            //pos_C = currentIntersection->anchor + vec_C - (Cdir * link_A->cornerRadius);									// (r0 + r1)/2
            pos_C = currentIntersection->getAnchor() + vec_C - (glm::normalize(vec_C) * link_A->cornerRadius * 0.8f);									// (r0 + r1)/2
            link_A->bOpenCorner = false;

            link_A->pushBack_A = glm::dot(vec_C, dir_A) + link_A->cornerRadius * 0.8f;	// FIXME make these interactive so I can tweak it
            link_B->pushBack_B = glm::dot(vec_C, dir_B) + link_A->cornerRadius * 0.8f;
        }



        link_A->cornerUp_A = currentIntersection->anchorNormal;
        link_B->cornerUp_B = currentIntersection->anchorNormal;

        if (link_A->cornerType != typeOfCorner::artistic) {
            link_A->corner_A = pos_C;
            link_B->corner_B = pos_C;



            if (link_A->bOpenCorner) {
                link_A->cornerTangent_A = tangent_C * link_A->pushBack_A * 0.2f;
                link_B->cornerTangent_B = -tangent_C * link_B->pushBack_B * 0.2f;
            }
            else {
                float phi = 1.57079632679489f - (link_A->theta / 2.0f);
                float D = phi * link_A->cornerRadius / 3.0f;
                link_A->cornerTangent_A = tangent_C * D;
                link_B->cornerTangent_B = -tangent_C * D;
            }

        }
    }


    int splineSegment = 0;
    int indexSegment = 0;
    // do the pushback and set roads
    for (int i = 0; i < numRoads; i++) {

        intersectionRoadLink* link_A = &currentIntersection->roadLinks[i];
        intersectionRoadLink* link_PREV = &currentIntersection->roadLinks[(i - 1 + numRoads) % numRoads];

        splinePoint* pnt_A1, * pnt_A0;
        if (link_A->broadStart) {
            pnt_A0 = &link_A->roadPtr->points[0];
            pnt_A1 = &link_A->roadPtr->points[1];
        }
        else {
            pnt_A0 = &link_A->roadPtr->points[link_A->roadPtr->points.size() - 1];
            pnt_A1 = &link_A->roadPtr->points[link_A->roadPtr->points.size() - 2];
        }

        link_A->pushBack = __max(__max(link_A->pushBack_A, link_A->pushBack_B), pnt_A1->widthLeft + pnt_A1->widthRight);
        float3 dir = glm::normalize(pnt_A1->anchor - currentIntersection->getAnchor());

        // only push beack if we have enought points
        // thsi looks problematic - dir comes from A1
        if (!currentIntersection->doNotPush)
        {
            if (link_A->roadPtr->points.size() > 3) {
                pnt_A1->setAnchor(currentIntersection->getAnchor() + dir * link_A->pushBack, currentIntersection->anchorNormal, currentIntersection->lidarLOD);
            }
        }

        // OK this is the sorce of our issue - 
        // FIXME
        // CALC BETTER IF WE DOTN PUSH

        glm::vec3 left = glm::cross(currentIntersection->anchorNormal, dir);
        if (_solveTangents)
        {
            link_A->tangentVector = glm::normalize(glm::cross(left, currentIntersection->anchorNormal)) * link_A->pushBack * 0.33f;

            if (link_A->bOpenCorner) {
                link_A->tangentVector = glm::normalize(link_A->cornerTangent_A) * link_A->pushBack * 0.13f;
            }
            if (link_PREV->bOpenCorner) {
                link_A->tangentVector = glm::normalize(-link_PREV->cornerTangent_A) * link_A->pushBack * 0.13f;
            }

            if (link_A->tangentVector.x == 0)
            {
                bool bCM = true;
            }
        }

        //link_A->road->solveRoad();	// This is overkill but fix spline rendering first
    }

    // do the tangents from correct pushbacks
    // do pushback and set roads
    for (int i = 0; i < numRoads; i++)
    {
        intersectionRoadLink* link_A = &currentIntersection->roadLinks[i];
        intersectionRoadLink* link_B = &currentIntersection->roadLinks[(i - 1 + numRoads) % numRoads];

        if (link_A->cornerType != typeOfCorner::artistic) {
            if (!link_A->bOpenCorner) {
                float blend = glm::clamp((link_A->theta - 1.7f) * 3.0f, 0.0f, 1.0f);
                float r1 = tan(link_A->theta / 4.0f) * 1.333f * link_A->cornerRadius;
                float r2 = link_A->pushBack * 0.33f;
                float r = r2 * blend + r1 * (1.0f - blend);
                link_A->cornerTangent_A *= r;
            }
            else {
                link_A->cornerTangent_A *= link_A->pushBack * 0.33;
            }
        }

        if (link_B->cornerType != typeOfCorner::artistic) {
            if (!link_B->bOpenCorner) {
                float blend = glm::clamp((link_B->theta - 1.7f) * 3.0f, 0.0f, 1.0f);
                float r1 = tan(link_B->theta / 4.0f) * 1.333f * link_B->cornerRadius;
                float r2 = link_B->pushBack * 0.33f;
                float r = r2 * blend + r1 * (1.0f - blend);
                link_A->cornerTangent_B *= r;
            }
            else
            {
                link_A->cornerTangent_B *= link_B->pushBack * 0.33;
            }
        }
    }


    for (int i = 0; i < numRoads; i++)
    {
        currentIntersection->roadLinks[i].roadPtr->solveRoad();
    }
}



// basic editing
void  roadNetwork::breakCurrentRoad(uint _index)
{
    if (!currentRoad || _index >= currentRoad->points.size()) {
        return;
    }

    uint currentGUID = currentRoad->GUID;

    //new road
    roadSectionsList.emplace_back();
    roadSection& newRoad = roadSectionsList.back();
    newRoad.GUID = (int)roadSectionsList.size() - 1;

    currentRoad = &roadSectionsList[currentGUID];
    uint currentSize = (uint)currentRoad->points.size();

    for (uint i = _index; i < currentSize; i++)
    {
        newRoad.points.push_back(currentRoad->points[i]);
    }

    if (currentRoad->int_GUID_end >= 0)
    {
        intersectionRoadLink* link = intersectionList[currentRoad->int_GUID_end].findLink(currentGUID);
        if (link)
        {
            newRoad.int_GUID_end = currentRoad->int_GUID_end;
            link->roadGUID = newRoad.GUID;
            link->roadPtr = &roadSectionsList[newRoad.GUID];
            currentRoad->int_GUID_end = -1;
        }
    }

    for (uint i = _index + 1; i < currentSize; i++)
    {
        currentRoad->points.pop_back();
    }
}



void  roadNetwork::deleteCurrentRoad()
{
    currentRoad->points.clear();

    if (currentRoad->int_GUID_start >= 0) {
        currentIntersection = &intersectionList[currentRoad->int_GUID_start];
        currentIntersection_findRoads();
        currentRoad->int_GUID_start = -1;
        currentIntersection = nullptr;
    }

    if (currentRoad->int_GUID_end >= 0) {
        currentIntersection = &intersectionList[currentRoad->int_GUID_end];
        currentIntersection_findRoads();
        currentRoad->int_GUID_end = -1;
        currentIntersection = nullptr;
    }

    currentRoad->solveRoad();
    currentRoad = nullptr;
}



/*	Tricky function since we dont actually delete, it will break the GUID's
    we could in theory moce to std::map rather than std::array, would likely solve this
    for now boolean, dont do anything with it isValid dont display anything*/
void  roadNetwork::deleteCurrentIntersection()
{
    for (auto& link : currentIntersection->roadLinks)
    {
        roadSectionsList[link.roadGUID].breakLink(link.broadStart);
    }

    currentIntersection->roadLinks.clear();
    currentIntersection->anchor = float3(0, 0, 0);
    currentIntersection = nullptr;
}



void  roadNetwork::copyVertex(uint _index)
{
    clipboardPoint = currentRoad->points[_index];
    hasClipboardPoint = true;
}



void  roadNetwork::pasteVertexGeometry(uint _index)
{
    if (hasClipboardPoint) {
        for (int i = 0; i < 9; i++) {
            currentRoad->points[_index].lanesLeft[i] = clipboardPoint.lanesLeft[i];
            currentRoad->points[_index].lanesRight[i] = clipboardPoint.lanesRight[i];
        }
        currentRoad->points[_index].centerlineType = clipboardPoint.centerlineType;
        currentRoad->solveRoad();
    }
}



void roadNetwork::pasteVertexMaterial(uint _index)
{
    if (hasClipboardPoint) {
        for (int v = selectFrom; v < selectTo; v++) {
            for (int i = 0; i < 15; i++) {
                currentRoad->points[v].matLeft[i] = clipboardPoint.matLeft[i];
                currentRoad->points[v].matRight[i] = clipboardPoint.matRight[i];
            }
            for (int i = 0; i < 2; i++) {
                currentRoad->points[v].matCenter[i] = clipboardPoint.matCenter[i];
            }
        }
    }
}



void roadNetwork::pasteVertexAll(uint _index)
{
    if (hasClipboardPoint) {
        for (int i = 0; i < 9; i++) {
            currentRoad->points[_index].lanesLeft[i] = clipboardPoint.lanesLeft[i];
            currentRoad->points[_index].lanesRight[i] = clipboardPoint.lanesRight[i];
        }
        currentRoad->points[_index].centerlineType = clipboardPoint.centerlineType;
        currentRoad->solveRoad();


        for (int i = 0; i < 15; i++) {
            currentRoad->points[_index].matLeft[i] = clipboardPoint.matLeft[i];
            currentRoad->points[_index].matRight[i] = clipboardPoint.matRight[i];
        }
        for (int i = 0; i < 2; i++) {
            currentRoad->points[_index].matCenter[i] = clipboardPoint.matCenter[i];
        }
    }
}




