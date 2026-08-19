#pragma once

#include "terrafector.h"


class roadMaterialLayer
{
public:
    roadMaterialLayer() { ; }
    virtual ~roadMaterialLayer() { ; }
    bool renderGui(Gui* _gui, Gui::Window& _window);

    //static std::string rootFolder;

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const version)
    {
        archive(CEREAL_NVP(displayName));
        archive(CEREAL_NVP(bezierIndex));
        archive(CEREAL_NVP(sideA));
        archive(CEREAL_NVP(offsetA));
        archive(CEREAL_NVP(sideB));
        archive(CEREAL_NVP(offsetB));
        archive(CEREAL_NVP(material));
    }

public:
    std::string		displayName = "please change me";
    uint			bezierIndex = 0;
    uint			sideA = 0;
    float			offsetA = 0.0f;
    uint			sideB = 0;
    float			offsetB = 0.0f;
    std::string		material;
    int				materialIndex = -1;
};
CEREAL_CLASS_VERSION(roadMaterialLayer, 100);



struct roadMaterialGroup
{
    std::string				displayName = "please change me";
    std::string				relativePath;
    Texture::SharedPtr		thumbnail = nullptr;

    std::vector<roadMaterialLayer> layers;
    bool import(std::string _path);
    void save();
    bool renderGui(Gui* _gui, Gui::Window& _window);

    bool changedForSave = false;

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const version)
    {
        archive(CEREAL_NVP(displayName));
        archive(CEREAL_NVP(relativePath));
        archive(CEREAL_NVP(layers));
    }
};
CEREAL_CLASS_VERSION(roadMaterialGroup, 100);





class roadMaterialCache
{
public:
    static roadMaterialCache& getInstance()
    {
        static roadMaterialCache    instance;       // Guaranteed to be destroyed. // Instantiated on first use.
        return instance;
    }
private:
    roadMaterialCache() {}
public:
    roadMaterialCache(roadMaterialCache const&) = delete;
    void operator=(roadMaterialCache const&) = delete;

public:
    void renderGui(Gui* _gui, Gui::Window &_window);
    bool renderGuiSelect (Gui* _gui, Gui::Window& _window);
    void reFindMaterial(roadMaterialGroup &_material);
    void renameMoveMaterial(roadMaterialGroup& _material);
    uint find_insert_material(std::string _path);
    void reloadMaterials();
    std::string checkPath(std::string _root, std::string _file);

    int selectedMaterial = -1;

    std::vector<roadMaterialGroup>	materialVector;

    void replaceAllrm(std::string& str, const std::string& from, const std::string& to) {
        if (from.empty())
            return;
        size_t start_pos = 0;
        while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
            str.replace(start_pos, from.length(), to);
            start_pos += to.length(); // In case 'to' contains 'from', like replacing 'x' with 'yx'
        }
    }

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const version)
    {
        archive(CEREAL_NVP(materialVector));

        for (auto& mat : materialVector) {
            replaceAllrm(mat.relativePath, "\\", "/");
        }
    }
};
CEREAL_CLASS_VERSION(roadMaterialCache, 100);



