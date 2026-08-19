#include "roadNetwork.h"

#include "assimp/scene.h"
#include "assimp/Exporter.hpp"
using namespace Assimp;

#pragma optimize( "", off )


bool roadNetwork::showMaterials;
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




