#include "terrafector.h"

#pragma optimize("", off)

#include "cereal/archives/binary.hpp"
#include "cereal/archives/json.hpp"
#include "cereal/archives/xml.hpp"
#include <sstream>
#include <fstream>
#include <iostream>
#include <filesystem>

//lodTriangleMesh     terrafectorSystem::lod_4_mesh;


const JLogger::SharedPtr& JLogger::instancePtr()
{
    static JLogger::SharedPtr pInstance;
    if (!pInstance) pInstance = std::make_shared<JLogger>();
    return pInstance;
}

void JLogger::log(uint _type, std::string _text)
{
    tab(stack.size());
    time_stack();
    type(_type);
    fprintf(file, "%s\n", _text.c_str());
    fflush(file);
}

void JLogger::logMulti(uint _type, std::string _text)
{
    tab(stack.size());
    fprintf(file, "    %s\n", _text.c_str());
}

void JLogger::startBlock(char *_name, uint _type)
{
    tab(stack.size());
    time();
    fprintf(file, "    %s {     \n", _name);
    stack.emplace();
    stack.top().startTime = high_resolution_clock::now();
    stack.top().type = _type;
    fflush(file);
}

void JLogger::endBlock()
{
    
    tab(stack.size() - 1);
    fprintf(file, "}  ");
    time_stack();
    fprintf(file, "\n\n");
    stack.pop();
    fflush(file);
}

void JLogger::open(char *_name)
{
    file = fopen(_name, "w");
    startTime = high_resolution_clock::now();
}

void JLogger::close()
{
    fclose(file);
}


void JLogger::tab(int depth)
{
    for (int i = 0; i < depth; i++)
    {
        fprintf(file, "    ");
    }
}

void JLogger::time()
{
    auto a = high_resolution_clock::now();
    float delta_s = (float)duration_cast<microseconds>(a - startTime).count() / 1000000.;
    fprintf(file, "%4.3f  ", delta_s);
}

void JLogger::time_stack()
{
    auto a = high_resolution_clock::now();
    float delta_s = (float)duration_cast<microseconds>(a - stack.top().startTime).count() / 1000000.;
    fprintf(file, "%4.3f ", delta_s);
}

void JLogger::type(uint _type)
{
    std::string logTypes[4] = { "verb", "info", "warn", "erro" };
    fprintf(file, "{%s} ", logTypes[_type].c_str());
}

//#pragma optimize( "", off )


bool forceAllTerrfectorRebuild = false;
bool terrafectorSystem::needsRefresh = false;
lodTriangleMesh_LoadCombiner terrafectorSystem::loadCombine_LOD2;
lodTriangleMesh_LoadCombiner terrafectorSystem::loadCombine_LOD4;
lodTriangleMesh_LoadCombiner terrafectorSystem::loadCombine_LOD6;
lodTriangleMesh_LoadCombiner terrafectorSystem::loadCombine_LOD4_top;
lodTriangleMesh_LoadCombiner terrafectorSystem::loadCombine_LOD6_top;
lodTriangleMesh_LoadCombiner terrafectorSystem::loadCombine_LOD7_stamps;
lodTriangleMesh_LoadCombiner terrafectorSystem::loadCombine_LOD4_bakeLow;
lodTriangleMesh_LoadCombiner terrafectorSystem::loadCombine_LOD4_bakeHigh;
lodTriangleMesh_LoadCombiner terrafectorSystem::loadCombine_LOD4_overlay;

ecotopeSystem* terrafectorSystem::pEcotopes = nullptr;
//GraphicsProgram::SharedPtr		terrafectorSystem::topdownProgramForBlends = nullptr;
FILE* terrafectorSystem::_logfile;
std::chrono::time_point<std::chrono::high_resolution_clock>  terrafectorSystem::logStartTime;
uint logTab;


materialCache terrafectorEditorMaterial::static_materials;


void replaceAll(std::string& str, const std::string& from, const std::string& to) {
    if (from.empty())
        return;
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // In case 'to' contains 'from', like replacing 'x' with 'yx'
    }
}


void tileTriangleBlock::clear()
{
}

void tileTriangleBlock::clearRemapping(uint _size)
{
    remapping.resize(_size);
    for (auto& remap : remapping) {
        remap = -1;
    }
    vertexReuse = 0;
}



void tileTriangleBlock::remapMaterials(uint* _map)
{
    for (auto& V : verts) {
        V.material = _map[V.material];
    }
}


void tileTriangleBlock::insertTriangle(const uint _material, const float3 pos[3], const float2 uv[3])
{
    for (int i = 0; i < 3; i++)
    {
        triVertex V;
        V.pos.x = pos[i].x;
        V.pos.y = pos[i].y;
        V.pos.z = pos[i].z;

        V.uv.x = uv[i].x;
        V.uv.y = uv[i].y;

        V.material = _material;

        V.alpha = 1;

        // FIXME for now I do not reuse verts, not great, but also noit thAT BAD, COULD BE BETETR
        tempIndexBuffer.push_back(verts.size());

        verts.push_back(V);


    }
}

void tileTriangleBlock::insertTriangle(const uint _material, const uint _F[3], const aiMesh* _mesh, bool _yup)
{
    for (int i = 0; i < 3; i++)
    {
        if (remapping[_F[i]] < 0) {
            remapping[_F[i]] = verts.size();
            triVertex V;
            V.pos.x = _mesh->mVertices[_F[i]].x; // SWAP zy - NOT GOOD
            V.pos.y = _mesh->mVertices[_F[i]].z;
            V.pos.z = _mesh->mVertices[_F[i]].y * (-1.0f);
            if (_yup)
            {
                V.pos.y = _mesh->mVertices[_F[i]].y;
                V.pos.z = _mesh->mVertices[_F[i]].z;
            }

            if (_mesh->HasTextureCoords(0))
            {
                V.uv.x = _mesh->mTextureCoords[0][_F[i]].x;
                V.uv.y = _mesh->mTextureCoords[0][_F[i]].y;
            }

            V.material = _material;


            V.alpha = 1;
            if (_mesh->HasVertexColors(0)) {
                V.alpha = _mesh->mColors[0][_F[i]].r;
            }

            verts.push_back(V);
        }
        else {
            vertexReuse++;
        }

        tempIndexBuffer.push_back(remapping[_F[i]]);
    }
}


void lodTriangleMesh::create(uint _lod)
{
    lod = _lod;
    grid = (uint)pow(2, _lod);
    tileSize = ecotopeSystem::terrainSize / grid; // FIxeme no boundary and hardcoded for 40k
    bufferSize = tileSize / 248.0f * 4.0f;
    tiles.resize(grid * grid);
    materialNames.clear();
}



void lodTriangleMesh::remapMaterials(uint* _map)
{
    for (auto& tile : tiles) {
        tile.remapMaterials(_map);
    }
}



void lodTriangleMesh::prepForMesh(aiAABB _aabb, uint _size, std::string _name, bool _yup)
{
    Yup = _yup;
    float halfsize = ecotopeSystem::terrainSize / 2.f;

    for (auto& tile : tiles) {
        tile.clearRemapping(_size);
    }

    materialNames.push_back(_name);

    xMin = (uint)__max(0, floor((_aabb.mMin.x + halfsize) / tileSize) - 1);
    xMax = (uint)__min(grid, floor((_aabb.mMax.x + halfsize) / tileSize) + 2);
    yMin = (uint)__max(0, floor((-_aabb.mMax.y + halfsize) / tileSize) - 1);
    yMax = (uint)__min(grid, floor((-_aabb.mMin.y + halfsize) / tileSize) + 2);
    if (Yup)
    {
        yMin = (uint)__max(0, floor((_aabb.mMin.z + halfsize) / tileSize) - 1);
        yMax = (uint)__min(grid, floor((_aabb.mMax.z + halfsize) / tileSize) + 2);
    }

    // test for possible YZ flip issues
    float xRange = _aabb.mMax.x - _aabb.mMin.x;
    float yRange = _aabb.mMax.y - _aabb.mMin.y;
    float zRange = _aabb.mMax.z - _aabb.mMin.z;
    if ((_aabb.mMin.z < -1) || (_aabb.mMax.z < -1))
    {
        fprintf(terrafectorSystem::_logfile, "          YZ-flip error likely: z values are negative\n");
    }
    if ((_aabb.mMin.z > 2000) || (_aabb.mMax.z > 2000))
    {
        fprintf(terrafectorSystem::_logfile, "          YZ-flip error likely: z values larger than 2000m\n");
    }
    if (yRange < zRange)
    {
        fprintf(terrafectorSystem::_logfile, "          YZ-flip error likely: yRange < zRange\n");
    }
}


int lodTriangleMesh::insertTriangle(const uint _material, const float3 pos[3], const float2 uv[3])
{
    float halfsize = ecotopeSystem::terrainSize / 2.f;

    float left, right, top, bottom;
    int count = 0;

    for (int y = 0; y < grid; y++)
    {
        bottom = -halfsize + (y * tileSize);
        top = bottom + tileSize;
        bottom -= bufferSize;
        top += bufferSize;

        for (int x = 0; x < grid; x++)
        {
            left = -halfsize + (x * tileSize);
            right = left + tileSize;

            left -= bufferSize;
            right += bufferSize;
            bool flags[4] = { true, true, true, true };

            for (int j = 0; j < 3; j++)     // vertex
            {
                flags[0] &= (pos[j].x < left);
                flags[1] &= (pos[j].x > right);
                flags[2] &= (pos[j].z < bottom);
                flags[3] &= (pos[j].z > top);
            }

            if (!(flags[0] || flags[1] || flags[2] || flags[3]))
            {
                tiles[y * grid + x].insertTriangle(_material, pos, uv);
                count++;
            }
        }
    }
    return count;
}



int lodTriangleMesh::insertTriangle(const uint _material, const uint _F[3], const aiMesh* _mesh)
{
    float halfsize = ecotopeSystem::terrainSize / 2.f;

    float left, right, top, bottom;
    int count = 0;

    for (int y = yMin; y < yMax; y++)
    {
        bottom = -halfsize + (y * tileSize);
        top = bottom + tileSize;
        bottom -= bufferSize;
        top += bufferSize;

        for (int x = xMin; x < xMax; x++)
        {
            left = -halfsize + (x * tileSize);
            right = left + tileSize;

            left -= bufferSize;
            right += bufferSize;
            bool flags[4] = { true, true, true, true };

            for (int j = 0; j < 3; j++)     // vertex
            {
                flags[0] &= (_mesh->mVertices[_F[j]].x < left);
                flags[1] &= (_mesh->mVertices[_F[j]].x > right);
                flags[2] &= (-_mesh->mVertices[_F[j]].y < bottom);
                flags[3] &= (-_mesh->mVertices[_F[j]].y > top);
                if (Yup)
                {
                    flags[2] &= (_mesh->mVertices[_F[j]].z < bottom);
                    flags[3] &= (_mesh->mVertices[_F[j]].z > top);
                }
            }

            if (!(flags[0] || flags[1] || flags[2] || flags[3]))
            {
                tiles[y * grid + x].insertTriangle(_material, _F, _mesh, Yup);
                count++;
            }
        }
    }
    return count;
}


void lodTriangleMesh::logStats()
{
    int i = 0;
    for (auto& tile : tiles) {
        if (tile.verts.size() > 0) {
            fprintf(terrafectorSystem::_logfile, "\n		(%d, %d) - %d)\n", i >> 4, i & 0xf, (int)tile.verts.size());
            fflush(terrafectorSystem::_logfile);
        }
        i++;
    }
}



void lodTriangleMesh::save(const std::string _path)
{
    std::ofstream os(_path, std::ios::binary);
    cereal::BinaryOutputArchive archive(os);
    serialize(archive, 100);
}



bool lodTriangleMesh::load(const std::string _path)
{
    std::ifstream is(_path, std::ios::binary);
    if (!is.fail())
    {
        cereal::BinaryInputArchive archive(is);
        serialize(archive, 100);
        return true;
    }
    return false;
}




void lodTriangleMesh_LoadCombiner::addMesh(std::string _path, lodTriangleMesh& _mesh, bool _remapMat)
{
    if (_remapMat)
    {
        // load materials and generate mapping
        uint materialRemap[4096];
        uint i = 0;
        for (auto& material : _mesh.materialNames) {
            materialRemap[i] = terrafectorEditorMaterial::static_materials.find_insert_material(_path, material);
            i++;
        }

        // Fix material in place
        _mesh.remapMaterials(materialRemap);
    }

    float3 V[3];
    // Append
    for (uint i = 0; i < tiles.size(); i++)
    {
        uint startIndex = tiles[i].verts.size();
        uint startBuffer = tiles[i].tempIndexBuffer.size();

        tiles[i].verts.insert(tiles[i].verts.end(), _mesh.tiles[i].verts.begin(), _mesh.tiles[i].verts.end());
        tiles[i].tempIndexBuffer.insert(tiles[i].tempIndexBuffer.end(), _mesh.tiles[i].tempIndexBuffer.begin(), _mesh.tiles[i].tempIndexBuffer.end());

        for (uint j = startBuffer; j < tiles[i].tempIndexBuffer.size(); j++)
        {
            tiles[i].tempIndexBuffer[j] += startIndex;
            /*
            uint index = j % 3;
            uint IDX = tiles[i].tempIndexBuffer.back();
            V[j] = tiles[i].verts[IDX].pos;
            if (index == 2)
            {
                float circumferance = glm::length(V[1] - V[0]) + glm::length(V[2] - V[1]) + glm::length(V[0] - V[2]);
                bool bCM = true;
            }
            */
        }

    }
}



void lodTriangleMesh_LoadCombiner::create(uint _lod)
{
    tiles.clear();
    gpuTiles.clear();

    lod = _lod;
    grid = (uint)pow(2, _lod);
    tiles.resize(grid * grid);
    //void loadToGPU();
}



void lodTriangleMesh_LoadCombiner::loadToGPU(std::string _path, bool _log)
{
    gpuTiles.clear();
    gpuTileTerrafector tfTile;
    int mostTri = 0;
    int vertexData = 0;
    int indexData = 0;

    for (auto& tile : tiles)
    {
        tfTile.numVerts = tile.verts.size();
        tfTile.numTriangles = tile.tempIndexBuffer.size();          // this is actualy indicis
        tfTile.numBlocks = (uint)ceil((float)tfTile.numTriangles / (128.0f * 3.0f));
        mostTri = __max(mostTri, tfTile.numTriangles);



        // buffer indicis
        tile.tempIndexBuffer.resize(tfTile.numBlocks * 128 * 3);
        for (uint i = tfTile.numTriangles; i < tfTile.numBlocks * 128 * 3; i++)
        {
            tile.tempIndexBuffer[i] = 0;
        }

        if (tfTile.numBlocks > 0)
        {
            tfTile.vertex = Buffer::createStructured(
                sizeof(triVertex),
                tfTile.numVerts,
                Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess,
                Buffer::CpuAccess::None,
                tile.verts.data());

            tfTile.index = Buffer::createStructured(
                sizeof(unsigned int),
                tfTile.numBlocks * 128 * 3,
                Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess,
                Buffer::CpuAccess::None,
                tile.tempIndexBuffer.data());

            vertexData += sizeof(triVertex) * tfTile.numVerts;
            indexData += sizeof(unsigned int) * tfTile.numBlocks * 128 * 3;
        }



        gpuTiles.push_back(tfTile);
    }



    if (_path.size() > 0)
    {
        FILE* file = fopen(_path.c_str(), "wb");
        if (file)
        {
            fwrite(&lod, sizeof(uint), 1, file);
            for (uint i = 0; i < tiles.size(); i++)
            {
                fwrite(&gpuTiles[i].numVerts, sizeof(uint), 1, file);
                fwrite(&gpuTiles[i].numTriangles, sizeof(uint), 1, file);
                fwrite(&gpuTiles[i].numBlocks, sizeof(uint), 1, file);
                if (gpuTiles[i].numBlocks > 0)
                {
                    fwrite(tiles[i].verts.data(), sizeof(triVertex), gpuTiles[i].numVerts, file);
                    fwrite(tiles[i].tempIndexBuffer.data(), sizeof(uint), gpuTiles[i].numBlocks * 128 * 3, file);
                }

                if (_log)
                {
                    if (i % (int)(pow(2, lod)) == 0)fprintf(terrafectorSystem::_logfile, "\n");
                    fprintf(terrafectorSystem::_logfile, "%7.d ", gpuTiles[i].numVerts);
                }
            }
            fclose(file);
        }
    }


    // now release all CPU memory
    tiles.clear();

    if (_log)
    {
        fprintf(terrafectorSystem::_logfile, "\n  lod %d  %dx%d\n", lod, grid, grid);
        fprintf(terrafectorSystem::_logfile, "  block with most triangles has %d\n", mostTri / 3);
        fprintf(terrafectorSystem::_logfile, "  %d Mb VB   %d Mb IB\n\n", vertexData / 1024 / 1024, indexData / 1024 / 1024);
    }



}



// ############################################################################################################################
std::string materialCache::getRelative(std::string _path)
{
    //fprintf(terrafectorSystem::_logfile, "path -  %s\n", _path.c_str());
    cleanPath(_path);
    cleanPath(terrafectorEditorMaterial::rootFolder);
    //fprintf(terrafectorSystem::_logfile, "cleanpath -  %s\n", _path.c_str());
    //fprintf(terrafectorSystem::_logfile, "root -  %s\n", terrafectorEditorMaterial::rootFolder.c_str());
    if (_path.find(terrafectorEditorMaterial::rootFolder) == 0)
    {
        _path = _path.substr(terrafectorEditorMaterial::rootFolder.length());
    }
    return _path;
}


void materialCache::cleanPath(std::string& _path)
{
    // to forward slash \ -> /
    size_t start_pos = 0;
    while ((start_pos = _path.find("\\", start_pos)) != std::string::npos) {
        _path.replace(start_pos, 1, "/");
        start_pos += 1;
    }

    // remove double slashes // -> /
    start_pos = 0;
    while ((start_pos = _path.find("//", start_pos)) != std::string::npos) {
        _path.replace(start_pos, 1, "/");
        start_pos += 1;
    }
}



// only called from lodTriangleMesh_LoadCombiner, for materials in fbx files
uint materialCache::find_insert_material(const std::string _path, const std::string _name)
{
    logTab++;
    std::filesystem::path fullPath = _path + _name + ".terrafectorMaterial";
    if (std::filesystem::exists(fullPath))
    {
        logTab--;
        return find_insert_material(fullPath);
    }
    else
    {
        // Now we have to seacrh, but use the first one we find
        std::string fullName = _name + ".terrafectorMaterial";
        std::filesystem::path rootpath = fullPath = terrafectorEditorMaterial::rootFolder + "/terrafectorMaterials";
        for (const auto& entry : std::filesystem::recursive_directory_iterator(fullPath))
        {
            std::string newPath = entry.path().string();
            cleanPath(newPath);
            //replaceAll(newPath, "\\", "/");
            //replaceAll(newPath, "//", "/"); // remove double slashes
            if (newPath.find(fullName) != std::string::npos)
            {
                logTab--;
                return find_insert_material(newPath);
            }
        }
    }

    // we got here, not good
    for (uint i = 0; i < logTab; i++)   fprintf(terrafectorSystem::_logfile, "  ");
    fprintf(terrafectorSystem::_logfile, "error : material - %s does not exist\n", _name.c_str());
    logTab--;

    return 0;
}



uint materialCache::find_insert_material(const std::filesystem::path _path)
{



    logTab++;
    for (uint i = 0; i < materialVector.size(); i++)
    {
        if (materialVector[i].fullPath.compare(_path) == 0)
        {
            //for (uint i = 0; i < logTab; i++)   fprintf(terrafectorSystem::_logfile, "  ");
            //fprintf(terrafectorSystem::_logfile, "materialCache - found (%s)\n", _path.filename().string().c_str());
            logTab--;

            if (!materialVector[i].thumbnail) {
                materialVector[i].thumbnail = Texture::createFromFile(_path.string() + ".jpg", false, true);
            }

            return i;
        }
    }

    // else add new


    uint materialIndex = (uint)materialVector.size();
    materialVector.emplace_back();
    terrafectorEditorMaterial& material = materialVector.back();
    material.import(_path);
    material.thumbnail = Texture::createFromFile(_path.string() + ".jpg", false, true);

    for (uint i = 0; i < logTab; i++)   fprintf(terrafectorSystem::_logfile, "  ");
    fprintf(terrafectorSystem::_logfile, "add Material[%d] - %s\n", materialIndex, _path.filename().string().c_str());
    fflush(terrafectorSystem::_logfile);

    logTab--;
    return materialIndex;
}


int materialCache::find_insert_texture(const std::filesystem::path _path, bool isSRGB)
{
    logTab++;
    for (uint i = 0; i < textureVector.size(); i++)
    {
        if (textureVector[i]->getSourcePath().compare(_path) == 0)
        {
            logTab--;
            return i;
        }
    }

    std::string ddsFilename = _path.string();
    if (_path.string().find(".dds") == std::string::npos)
    {
        ddsFilename = _path.string() + ".earthworks.dds";
    }
    if (!std::filesystem::exists(ddsFilename))
    {
        std::string pathOnly = ddsFilename.substr(0, ddsFilename.find_last_of("\\/") + 1);
        std::string cmdExp = "F:\\terrains\\_resources\\Compressonator\\CompressonatorCLI -miplevels 6 \"" + _path.string() + "\" " + "F:\\terrains\\_resources\\Compressonator\\temp_mip.dds";

        fprintf(terrafectorSystem::_logfile, "%s\n", cmdExp.c_str());
        system(cmdExp.c_str());
        if (isSRGB)
        {
            std::string cmdExp2 = "F:\\terrains\\_resources\\Compressonator\\CompressonatorCLI -fd BC6H  F:\\terrains\\_resources\\Compressonator\\temp_mip.dds \"" + ddsFilename + "\"";
            system(cmdExp2.c_str());
        }
        else
        {
            std::string cmdExp2 = "F:\\terrains\\_resources\\Compressonator\\CompressonatorCLI -fd BC7 -Quality 0.01 F:\\terrains\\_resources\\Compressonator\\temp_mip.dds " + ddsFilename + "\"";
            system(cmdExp2.c_str());
        }
    }
    Texture::SharedPtr tex = Texture::createFromFile(ddsFilename, true, isSRGB);
    if (tex)
    {
        tex->setSourcePath(_path);
        tex->setName(_path.filename().string());
        textureVector.emplace_back(tex);

        float compression = 4.0f;
        if (isSRGB) compression = 4.0f;

        texture_memory_in_Mb += (float)(tex->getWidth() * tex->getHeight() * 4.0f * 1.333f) / 1024.0f / 1024.0f / compression;	// for 4:1 compression + MIPS

        for (uint i = 0; i < logTab; i++)   fprintf(terrafectorSystem::_logfile, "  ");
        fprintf(terrafectorSystem::_logfile, "%s\n", tex->getName().c_str());
        fflush(terrafectorSystem::_logfile);

        logTab--;
        return (uint)(textureVector.size() - 1);
    }
    else
    {
        for (uint i = 0; i < logTab; i++)   fprintf(terrafectorSystem::_logfile, "  ");
        fprintf(terrafectorSystem::_logfile, "failed %s \n", _path.string().c_str());
        fflush(terrafectorSystem::_logfile);

        logTab--;
        return -1;
    }
}


Texture::SharedPtr materialCache::getDisplayTexture()
{
    if (dispTexIndex >= 0) {
        return textureVector.at(dispTexIndex);
    }
    else {
        return nullptr;
    }
}



void materialCache::setTextures(ShaderVar& _var)
{
    for (size_t i = 0; i < textureVector.size(); i++)
    {
        _var[i] = textureVector[i];
    }
}


// FIXME could also just do the individual one
void materialCache::rebuildStructuredBuffer()
{
    size_t offset = 0;
    for (auto& mat : materialVector)
    {
        sb_Terrafector_Materials->setBlob(&mat._constData, offset, sizeof(TF_material));
        offset += sizeof(TF_material);
    }
}


void materialCache::rebuildAll()
{
    for (auto& mat : materialVector)
    {
        if (mat._constData.materialType == 1)
        {
            for (uint idx = 0; idx < 8; idx++)
            {
                mat._constData.subMaterials[idx] &= 0x00ff0000;

                if (mat.submaterialPaths[idx].size())
                {
                    uint matIdx = terrafectorEditorMaterial::static_materials.find_insert_material(terrafectorEditorMaterial::rootFolder + mat.submaterialPaths[idx]);
                    mat._constData.subMaterials[idx] &= 0xffff0000;
                    mat._constData.subMaterials[idx] += matIdx;
                }
            }
        }

        mat.rebuildConstantbufferData();
    }

    rebuildStructuredBuffer();
}


bool terrafectorElement::isMeshCached(const std::string _path)
{
    if (!std::filesystem::exists(_path + ".lod4Cache"))
    {
        return false;
    }
    if (!std::filesystem::exists(_path))
    {
        return false;
    }
    auto timeFile = std::filesystem::last_write_time(_path);
    auto timeCache = std::filesystem::last_write_time(_path + ".lod4Cache");

    return (timeFile < timeCache);
}



void terrafectorElement::splitAndCacheMesh(const std::string _path)
{
    Assimp::Importer importer;
    lodTriangleMesh lodder_2;
    lodTriangleMesh lodder_4;
    lodTriangleMesh lodder_6;
    lodder_2.create(2);
    lodder_4.create(4);
    lodder_6.create(6);

    bool useLOD2 = _path.find("LOD2") != std::string::npos;
    bool bakeOnlyOverlay = (_path.find("bakeOnly") != std::string::npos) || (_path.find("overlay") != std::string::npos);
    bool top = (_path.find("50_top") != std::string::npos);
    int num2 = 0;
    int num4 = 0;
    int num6 = 0;

    unsigned int flags =
        aiProcess_FlipUVs |
        aiProcess_Triangulate |
        aiProcess_PreTransformVertices |
        aiProcess_JoinIdenticalVertices |
        aiProcess_GenBoundingBoxes;

    const aiScene* scene = nullptr;
    scene = importer.ReadFile(_path.c_str(), flags);



    if (scene)
    {
        glm::int4 index;
        triVertex V[3];
        for (uint i = 0; i < scene->mNumMeshes; i++)
        {
            aiMesh* M = scene->mMeshes[i];

            fprintf(terrafectorSystem::_logfile, "BB (%f - %f,%f - %f,%f - %f) \n", M->mAABB.mMin.x, M->mAABB.mMax.x, M->mAABB.mMin.y, M->mAABB.mMax.y, M->mAABB.mMin.z, M->mAABB.mMax.z);

            lodder_2.prepForMesh(M->mAABB, M->mNumVertices, scene->mMaterials[M->mMaterialIndex]->GetName().C_Str(), true);
            lodder_4.prepForMesh(M->mAABB, M->mNumVertices, scene->mMaterials[M->mMaterialIndex]->GetName().C_Str(), true);
            lodder_6.prepForMesh(M->mAABB, M->mNumVertices, scene->mMaterials[M->mMaterialIndex]->GetName().C_Str(), true);
            for (uint j = 0; j < M->mNumFaces; j++)
            {
                if (!bakeOnlyOverlay && useLOD2)    num2 += lodder_2.insertTriangle(i, M->mFaces[j].mIndices, M);
                num4 += lodder_4.insertTriangle(i, M->mFaces[j].mIndices, M);
                if (!bakeOnlyOverlay) num6 += lodder_6.insertTriangle(i, M->mFaces[j].mIndices, M);
            }
        }


        if (num2 > 0) lodder_2.save(_path + ".lod2Cache");
        if (num4 > 0) lodder_4.save(_path + ".lod4Cache");
        if (num6 > 0) lodder_6.save(_path + ".lod6Cache");
        std::filesystem::path P = _path;
        if (num2 > 0) terrafectorSystem::loadCombine_LOD2.addMesh(P.parent_path().string() + "/", lodder_2);

        if (top)
        {
            if (num6 > 0) terrafectorSystem::loadCombine_LOD6_top.addMesh(P.parent_path().string() + "/", lodder_6);
        }
        else
        {
            if (num6 > 0) terrafectorSystem::loadCombine_LOD6.addMesh(P.parent_path().string() + "/", lodder_6);
        }

        if (num4 > 0)
        {
            if (_path.find("bakeOnlyBottom") != std::string::npos)
            {
                terrafectorSystem::loadCombine_LOD4_bakeLow.addMesh(P.parent_path().string() + "/", lodder_4);
            }
            else if (_path.find("bakeOnlyTop") != std::string::npos)
            {
                terrafectorSystem::loadCombine_LOD4_bakeHigh.addMesh(P.parent_path().string() + "/", lodder_4);
            }
            else if (_path.find("overlay") != std::string::npos)
            {
                terrafectorSystem::loadCombine_LOD4_overlay.addMesh(P.parent_path().string() + "/", lodder_4);
            }
            else
            {
                if (top)
                {
                    terrafectorSystem::loadCombine_LOD4_top.addMesh(P.parent_path().string() + "/", lodder_4);
                }
                else
                {
                    terrafectorSystem::loadCombine_LOD4.addMesh(P.parent_path().string() + "/", lodder_4);
                }
            }
        }

        for (auto& matName : lodder_4.materialNames)
        {
            subMesh S;
            S.materialName = matName;
            S.index = terrafectorEditorMaterial::static_materials.find_insert_material(P.parent_path().string() + "/", matName);
            materials.push_back(S);
        }

    }
}



terrafectorElement& terrafectorElement::find_insert(const std::string _name, tfTypes _type, const std::string _path)
{
    logTab++;
    for (int i = 0; i < children.size(); i++)
    {
        if (children[i].name.compare(_name) == 0)
        {
            return children.at(i);
        }
    }

    children.emplace_back(_type, _name);
    if (_name.find("bakeOnly") != std::string::npos) {
        children.back().bakeOnly = true;
    }

    if (_type == tf_heading)
    {
        for (uint i = 0; i < logTab; i++)   fprintf(terrafectorSystem::_logfile, "  ");
        fprintf(terrafectorSystem::_logfile, "%s\n", _name.c_str());
        fflush(terrafectorSystem::_logfile);
    }

    if (_type == tf_fbx)
    {
        terrafectorElement& me = children.back();
        me.path = _path;
        me.name = _path.substr(_path.find_last_of("\\/") + 1, _path.size());
        //std::string fullName = terrafectorEditorMaterial::rootFolder + _path;
        std::string fullName = _path;
        std::string fullPath = fullName.substr(0, fullName.find_last_of("\\/") + 1);

        for (uint i = 0; i < logTab; i++)   fprintf(terrafectorSystem::_logfile, "  ");
        fprintf(terrafectorSystem::_logfile, "add mesh - %s\n", fullName.c_str());
        fflush(terrafectorSystem::_logfile);


        if (forceAllTerrfectorRebuild || !isMeshCached(_path)) {
            splitAndCacheMesh(_path);
        }
        else
        {
            bool top = (_path.find("50_top") != std::string::npos);

            lodTriangleMesh lodder2;
            lodTriangleMesh lodder4;
            lodTriangleMesh lodder6;
            bool use2 = lodder2.load(_path + ".lod2Cache");
            bool use4 = lodder4.load(_path + ".lod4Cache");
            bool use6 = lodder6.load(_path + ".lod6Cache");

            std::filesystem::path P = _path;
            if (use2)   terrafectorSystem::loadCombine_LOD2.addMesh(P.parent_path().string() + "/", lodder2);
            if (top)
            {
                if (use6)   terrafectorSystem::loadCombine_LOD6_top.addMesh(P.parent_path().string() + "/", lodder6);
            }
            else {
                if (use6)   terrafectorSystem::loadCombine_LOD6.addMesh(P.parent_path().string() + "/", lodder6);
            }
            if (use4)
            {
                if (_path.find("bakeOnlyBottom") != std::string::npos)
                {
                    terrafectorSystem::loadCombine_LOD4_bakeLow.addMesh(P.parent_path().string() + "/", lodder4);
                }
                else if (_path.find("bakeOnlyTop") != std::string::npos)
                {
                    terrafectorSystem::loadCombine_LOD4_bakeHigh.addMesh(P.parent_path().string() + "/", lodder4);
                }
                else if (_path.find("overlay") != std::string::npos)
                {
                    terrafectorSystem::loadCombine_LOD4_overlay.addMesh(P.parent_path().string() + "/", lodder4);
                }
                else
                {
                    if (top)
                    {
                        terrafectorSystem::loadCombine_LOD4_top.addMesh(P.parent_path().string() + "/", lodder4);
                    }
                    else
                    {
                        terrafectorSystem::loadCombine_LOD4.addMesh(P.parent_path().string() + "/", lodder4);
                    }
                }
            }

            for (auto& matName : lodder4.materialNames)
            {
                subMesh S;
                S.materialName = matName;
                S.index = terrafectorEditorMaterial::static_materials.find_insert_material(P.parent_path().string() + "/", matName);
                me.materials.push_back(S);
            }
        }

    }

    logTab--;
    return children.back();
}




void terrafectorElement::loadPath(std::string _path)
{
    if (std::filesystem::exists(_path))
    {
        for (const auto& entry : std::filesystem::directory_iterator(_path))
        {
            std::string newPath = entry.path().string();
            //replaceAll(newPath, "\\", "/");

            if (entry.is_directory())
            {
                terrafectorElement& e = find_insert(entry.path().filename().string());
                e.loadPath(newPath);
            }
            else
            {
                std::string ext = entry.path().extension().string();
                if (ext.find("fbx") != std::string::npos ||
                    ext.find("obj") != std::string::npos ||
                    ext.find("dxf") != std::string::npos) {
                    terrafectorElement& e = find_insert(entry.path().filename().string(), tf_fbx, newPath);
                }
            }
        }
    }
}


// class terrafectorMaterial ######################################################################################################################################################

std::string terrafectorEditorMaterial::rootFolder;

terrafectorEditorMaterial::terrafectorEditorMaterial() {
    for (int i = 0; i < 15; i++)
        for (int j = 0; j < 4; j++)
            _constData.ecotopeMasks[i][j] = 0;
}

terrafectorEditorMaterial::~terrafectorEditorMaterial() {
}

// unique hash of blendstates 
uint32_t terrafectorEditorMaterial::blendHash() {
    uint32_t hash = 0;
    return hash;
}

void terrafectorEditorMaterial::import(std::filesystem::path  _path, bool _replacePath) {

    //_path += "_xml";
    std::ifstream is(_path);
    if (is.fail()) {
        displayName = "failed to load";
        fullPath = _path;
    }
    else
    {
        cereal::JSONInputArchive archive(is);
        serialize(archive, TFMATERIAL_VERSION_LOAD);
        //archive(*this);


        if (_replacePath) fullPath = _path;
        reloadTextures();
        isModified = false;
    }
}




void terrafectorEditorMaterial::save()
{
    std::ofstream os(fullPath);
    cereal::JSONOutputArchive archive(os);
    serialize(archive, TFMATERIAL_VERSION);
}


// FIXME has to take root path into account
void terrafectorEditorMaterial::reloadTextures()
{
    for (int idx = 0; idx < tfTextureSize; idx++) {
        if (texturePaths[idx].size()) {
            char txt[1000];
            sprintf(txt, "%s", (rootFolder + texturePaths[idx]).c_str());

            textureIndex[idx] = terrafectorEditorMaterial::static_materials.find_insert_texture(rootFolder + texturePaths[idx], tfSRGB[idx]);
            textureNames[idx] = texturePaths[idx].substr(texturePaths[idx].find_last_of("\\/") + 1);
        }
    }
}


void terrafectorEditorMaterial::eXport(std::filesystem::path _path) {
    {
        std::ofstream os(_path);
        cereal::JSONOutputArchive archive(os);
        serialize(archive, TFMATERIAL_VERSION);
        isModified = false;
    }
}




void terrafectorEditorMaterial::rebuildConstantbufferData()
{
    // rework the indicis
    _constData.baseAlphaTexture = textureIndex[baseAlpha];
    _constData.detailAlphaTexture = textureIndex[detailAlpha];
    _constData.baseElevationTexture = textureIndex[baseElevation];
    _constData.detailElevationTexture = textureIndex[detailElevation];
    _constData.baseAlbedoTexture = textureIndex[baseAlbedo];
    _constData.detailAlbedoTexture = textureIndex[detailAlbedo];
    _constData.baseRoughnessTexture = textureIndex[baseRoughness];
    _constData.detailRoughnessTexture = textureIndex[detailRoughness];
    _constData.ecotopeTexture = textureIndex[ecotope];
}

void terrafectorEditorMaterial::rebuildConstantbuffer()
{
    rebuildConstantbufferData();

    //constantBuffer->setBlob(&_constData, 0, sizeof(_constData));
    //constantBuffer->uploadToGPU();
    terrafectorSystem::needsRefresh = true;

    // also update structures buffer for the cvache
    static_materials.rebuildStructuredBuffer();
}










void terrafectorSystem::loadPath(std::string _path, std::string _exportPath, bool _rebuild)
{
    forceAllTerrfectorRebuild = _rebuild;

    fprintf(terrafectorSystem::_logfile, "terrafectorSystem::loadPath    %s\n", _path.c_str());
    logTab = 0;

    terrafectorSystem::loadCombine_LOD2.create(2);
    terrafectorSystem::loadCombine_LOD4.create(4);
    terrafectorSystem::loadCombine_LOD6.create(6);
    terrafectorSystem::loadCombine_LOD4_top.create(4);
    terrafectorSystem::loadCombine_LOD6_top.create(6);
    terrafectorSystem::loadCombine_LOD7_stamps.create(7);
    terrafectorSystem::loadCombine_LOD4_bakeLow.create(4);
    terrafectorSystem::loadCombine_LOD4_bakeHigh.create(4);
    terrafectorSystem::loadCombine_LOD4_overlay.create(4);

    root.loadPath(_path);
    fprintf(terrafectorSystem::_logfile, "\n\n");

    bool donotlog = false;
    //fprintf(terrafectorSystem::_logfile, "loadCombine_LOD2.loadToGPU()\n");
    terrafectorSystem::loadCombine_LOD2.loadToGPU(_exportPath + "/terrafector_lod2.gpu", donotlog);   // this also releases CPU memory
    //fprintf(terrafectorSystem::_logfile, "loadCombine_LOD4.loadToGPU()\n");
    terrafectorSystem::loadCombine_LOD4.loadToGPU(_exportPath + "/terrafector_lod4.gpu", donotlog);   // this also releases CPU memory
    //fprintf(terrafectorSystem::_logfile, "loadCombine_LOD6.loadToGPU()\n");
    terrafectorSystem::loadCombine_LOD6.loadToGPU(_exportPath + "/terrafector_lod6.gpu", donotlog);   // this also releases CPU memory

    //fprintf(terrafectorSystem::_logfile, "loadCombine_LOD4_top.loadToGPU()\n");
    terrafectorSystem::loadCombine_LOD4_top.loadToGPU(_exportPath + "/terrafector_lod4_top.gpu", donotlog);   // this also releases CPU memory
    //fprintf(terrafectorSystem::_logfile, "loadCombine_LOD6_top.loadToGPU()\n");
    terrafectorSystem::loadCombine_LOD6_top.loadToGPU(_exportPath + "/terrafector_lod6_top.gpu", donotlog);   // this also releases CPU memory


    //fprintf(terrafectorSystem::_logfile, "loadCombine_LOD4_bakeLow.loadToGPU()\n");
    terrafectorSystem::loadCombine_LOD4_bakeLow.loadToGPU("", donotlog);   // this also releases CPU memory
    //fprintf(terrafectorSystem::_logfile, "loadCombine_LOD4_bakeHigh.loadToGPU()\n");
    terrafectorSystem::loadCombine_LOD4_bakeHigh.loadToGPU("", donotlog);   // this also releases CPU memory
    //fprintf(terrafectorSystem::_logfile, "loadCombine_LOD4_overlay.loadToGPU()\n");
    terrafectorSystem::loadCombine_LOD4_overlay.loadToGPU("", donotlog);   // this also releases CPU memory

    terrafectorEditorMaterial::static_materials.rebuildAll();

    /*
    fprintf(terrafectorSystem::_logfile, "\n\nmeshLoadCombiner\n");
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            fprintf(terrafectorSystem::_logfile, "%6d", terrafectorSystem::loadCombine_LOD4_bakeLow.getTile(y * 16 + x)->numVerts);
        }
        fprintf(terrafectorSystem::_logfile, "\n");
    }
    */
}



void terrafectorSystem::exportMaterialBinary(std::string _path, std::string _evoRoot)
{
    uint textureSize = (uint)terrafectorEditorMaterial::static_materials.textureVector.size();
    uint matSize = (uint)terrafectorEditorMaterial::static_materials.materialVector.size();

    std::string textureName = _path + "/TextureList.gpu";
    FILE* file = fopen(textureName.c_str(), "wb");
    if (file)
    {
        fwrite(&textureSize, 1, sizeof(uint), file);
        fwrite(&matSize, 1, sizeof(uint), file);

        char name[512];
        for (uint i = 0; i < textureSize; i++) {
            std::string path = terrafectorEditorMaterial::static_materials.textureVector[i]->getSourcePath().string();
            path = path.substr(13, path.size());
            path = path.substr(0, path.rfind("."));
            path += ".texture";
            memset(name, 0, 512);
            sprintf(name, "%s", path.c_str());
            fwrite(name, 1, 512, file);

            // Now test if it exists in EVO
            //C:/Kunos/acevo_content/content/
            if (!std::filesystem::exists(_evoRoot + path))
            {
                fprintf(terrafectorSystem::_logfile, "EVO _ DOES NOT EXISTS %s\n", (_evoRoot + path).c_str());
            }
        }


        fclose(file);
    }



    textureName = _path + "/Materials.gpu";
    file = fopen(textureName.c_str(), "wb");
    if (file)
    {
        for (uint i = 0; i < matSize; i++)
        {
            fwrite(&terrafectorEditorMaterial::static_materials.materialVector[i]._constData, 1, sizeof(TF_material), file);
        }
        fclose(file);
    }
}
