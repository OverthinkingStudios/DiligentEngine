#include "terrain.h"

#include "ots/Log.hpp"


//#pragma optimize( "", off )


/*  roadMaterialGroup
    --------------------------------------------------------------------------------------------------------------------*/
bool roadMaterialGroup::import(std::string _relativepath)
{
    std::ifstream is(terrafectorEditorMaterial::rootFolder + _relativepath);
    if (is.fail()) {
        displayName = "failed to load";
        relativePath = _relativepath;
        return false;
    }
    else
    {
        cereal::JSONInputArchive archive(is);
        serialize(archive, 0);
        relativePath = _relativepath;
        displayName = relativePath.substr(relativePath.find_last_of("\\/") + 1);
        displayName = displayName.substr(0, displayName.find_last_of("."));
        return true;
    }
}

void roadMaterialGroup::save()
{
    std::ofstream os(terrafectorEditorMaterial::rootFolder + relativePath);
    cereal::JSONOutputArchive archive(os);
    serialize(archive, 0);
}

/*  roadMaterialCache
    --------------------------------------------------------------------------------------------------------------------*/


uint roadMaterialCache::find_insert_material(std::string _path)
{
    replaceAllrm(_path, "\\", "/");
    replaceAllrm(_path, "//", "/");     // remove double

    if (_path.find(terrafectorEditorMaterial::rootFolder) == 0)
    {
        std::string relative = _path.substr(terrafectorEditorMaterial::rootFolder.length());

        for (uint i = 0; i < materialVector.size(); i++)
        {
            if (materialVector[i].relativePath.compare(relative) == 0)
            {
                // try to load the thumbnail
                if (!materialVector[i].thumbnail) {
                    // Gated on existence: an editor-only asset, and the ew
                    // loader logs missing files loudly.
                    if (std::filesystem::exists(_path + ".jpg"))
                        materialVector[i].thumbnail = ew::Texture::createFromFile(_path + ".jpg", false, true);
                    if (!materialVector[i].thumbnail) fprintf(terrafectorSystem::_logfile, "		Road material - failed to load %s.jpg\n", _path.c_str());
                }
                return i;
            }
        }

        // not found - add new
        fprintf(terrafectorSystem::_logfile, "	roadMaterialCache - add %s\n", _path.c_str());
        materialVector.emplace_back();
        materialVector.back().import(relative);
        if (std::filesystem::exists(_path + ".jpg"))    // optional thumbnail, see above
            materialVector.back().thumbnail = ew::Texture::createFromFile(_path + ".jpg", false, true);

        // load all the terrafector Materials and set
        roadMaterialGroup& current = materialVector.back();

        fprintf(terrafectorSystem::_logfile, "		  roadMaterialCache (%s)  %d layers\n", _path.c_str(), (int)current.layers.size());
        fflush(terrafectorSystem::_logfile);
        for (uint i = 0; i < current.layers.size(); i++)
        {
            std::string file = terrafectorEditorMaterial::rootFolder + current.layers[i].material;
            replaceAllrm(file, "//", "/");     // remove double
            current.layers[i].materialIndex = (int)terrafectorEditorMaterial::static_materials.find_insert_material(file);
        }

        return (uint)(materialVector.size() - 1);
    }
    else
    {
        fprintf(terrafectorSystem::_logfile, "		  failed to find rootpath  (%s)  in   %s\n", terrafectorEditorMaterial::rootFolder.c_str(), _path.c_str());
        fflush(terrafectorSystem::_logfile);
        spdlog::error("roads: road material '{}' is outside rootFolder '{}' - falls back to material 0",
                      _path, terrafectorEditorMaterial::rootFolder);
    }
    return 0;	//?? is this right
}


std::string roadMaterialCache::checkPath(std::string _root, std::string _file)
{
    // The existence check is required: recursive_directory_iterator's
    // constructor throws on a missing directory.
    if (std::filesystem::exists(_root))
    {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(_root))
        {
            std::string newPath = entry.path().string();
            replaceAllrm(newPath, "\\", "/");

            if (!entry.is_directory() && (newPath.find(_file) != std::string::npos))
            {
                return newPath;
            }
        }
    }
    return "";
}



void roadMaterialCache::reloadMaterials()
{
    for (auto& mat : materialVector)
    {
        if (!std::filesystem::exists(terrafectorEditorMaterial::rootFolder + mat.relativePath))
        {
            std::string filename = mat.relativePath.substr(mat.relativePath.find_last_of("/\\") + 1);
            std::string returnName = checkPath(terrafectorEditorMaterial::rootFolder + "roadMaterials/", filename);
            if (returnName.find(terrafectorEditorMaterial::rootFolder) == 0)
            {
                std::string relative = returnName.substr(terrafectorEditorMaterial::rootFolder.length());

                fprintf(terrafectorSystem::_logfile, "	ROAD MATERIAL CACHE - FILE RELOCATE from  %s     to    %s\n", mat.relativePath.c_str(), relative.c_str());
                fflush(terrafectorSystem::_logfile);

                mat.relativePath = relative;
                mat.save();
            }
        }


        fprintf(terrafectorSystem::_logfile, "	roadMaterialCache - load %s\n", mat.relativePath.c_str());
        fflush(terrafectorSystem::_logfile);
        if (!mat.import(mat.relativePath))
        {
            reFindMaterial(mat);
        }




        roadMaterialCache::getInstance().find_insert_material(terrafectorEditorMaterial::rootFolder + mat.relativePath);
        for (auto& layer : mat.layers)
        {
            std::string file = terrafectorEditorMaterial::rootFolder + layer.material;
            replaceAllrm(file, "//", "/");
            layer.materialIndex = (int)terrafectorEditorMaterial::static_materials.find_insert_material(file);
        }
    }
}


// Relocation fallback for a missing .roadMaterial - reached from the
// reloadMaterials() load path, not just from the editor.
void roadMaterialCache::reFindMaterial(roadMaterialGroup& _material)
{
    // Not implemented: the editor file dialog that let the user point at the
    // moved file. LOUD error instead, because every layer of this group falls
    // back to material 0 in the bake.
    spdlog::error("roads: road material '{}' failed to load and cannot be relocated without the editor - its layers bake with material 0",
                  _material.relativePath);
}
