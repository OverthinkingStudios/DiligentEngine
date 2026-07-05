#pragma once
#ifndef TEXTURESPLITTOOL_SRC_TEXTURESPLITTOOL_HPP_
#define TEXTURESPLITTOOL_SRC_TEXTURESPLITTOOL_HPP_

#include "EarthworksFXApplicationBase.hpp"







#include "TextureLoader.h"
#include "TextureUtilities.h"


#include <string>
#include <array>
#include <vector>
/*
* this comes from Earthworks, precicely terrafectors although it should be in common
#define archive_float2(v)         \
    {                             \
        archive(CEREAL_NVP(v.x)); \
        archive(CEREAL_NVP(v.y)); \
    }
#define archive_float3(v)         \
    {                             \
        archive(CEREAL_NVP(v.x)); \
        archive(CEREAL_NVP(v.y)); \
        archive(CEREAL_NVP(v.z)); \
    }
#define archive_float4(v)         \
    {                             \
        archive(CEREAL_NVP(v.x)); \
        archive(CEREAL_NVP(v.y)); \
        archive(CEREAL_NVP(v.z)); \
        archive(CEREAL_NVP(v.w)); \
    }
    */
ImFont* font_small;
ImFont* font_normal;
ImFont* font_H1;
ImFont* font_H2;
ImFont* font_H3;










class gui {
    public:
    gui() { ; }
    ~gui() { ; }

    //    ImGui::PushID(global_guid++);
#define BEGIN                      \
    bool changed = false;          \
    ImGui::Text(_name);            \
    ImGui::SameLine(TEXT_WIDTH, 0); \
    ImGui::SetNextItemWidth(ITEM_WIDTH);

#define END            \
    tooltip(_tooltip); \
    ImGui::PopID();    \
    return changed;

    void tooltip(const char* _tooltip) {
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(_tooltip);
    }

    void tooltip(ImFont* _font, const char* _tooltip) {
        ImGui::PushFont(_font);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(_tooltip);
        ImGui::PopFont();
    }

    void text(ImFont* _font, const char* _txt) {
        ImGui::PushFont(_font);
        ImGui::TextUnformatted(_txt);
        ImGui::PopFont();
    }

    void text_centered(ImFont* _font, const char* _txt) {
        ImGui::PushFont(_font);
        {
            float windowWidth = ImGui::GetContentRegionAvail().x;
            float textWidth = ImGui::CalcTextSize(_txt).x;
            ImGui::SameLine((windowWidth - textWidth) * 0.5f, 0);
            ImGui::TextUnformatted(_txt);
        }
        ImGui::PopFont();
    }

    bool checkbox(const char* _name, bool* _val, const char* _tooltip = "") {
        ImGuiStyle& style = ImGui::GetStyle();
        ImGui::PushID(_val);
        BEGIN;
        style.FrameBorderSize = 2;
        if (ImGui::Checkbox("##checkbox", _val)) {
            changed = true;
        }
        style.FrameBorderSize = 0;
        END;
    }

    bool dragFloat(const char* _name, float* _data, float _speed, float _min, float _max, const char* _tooltip = "",
                   const char* _fmt = "%3.2f") {
        ImGui::PushID(_data);
        BEGIN;
        if (ImGui::DragFloat("##drag", _data, _speed, _min, _max, _fmt)) changed = true;
        END;
    }

    private:
    inline static float TEXT_WIDTH = 160;
    inline static float ITEM_WIDTH = 80;
    inline static unsigned int global_guid = 1000000;
};


















#include <filesystem>
// FIXME makethis a singleton
class earthworksPaths {
    public:
    static std::string root;

    bool make_relative(std::string& _path);
    void make_full(std::string& _path);
    std::string get_full(std::string& _path);
    std::string get_name(std::string& _path);
    std::string get_fullname(std::string& _path);
    void replaceAll(std::string& _str, const std::string& _from, const std::string& _to);
    void clean(std::string& _path);
    void to_back_slash(std::string& _path);

    private:
    template <class Archive>
    void serialize(Archive& _archive, unsigned int const _version) {
        _archive(CEREAL_NVP(root));
    }
};
CEREAL_CLASS_VERSION(earthworksPaths, 100);
















class fbo {
    public:
    void setup(int2 _size, int _numTargets, Diligent::RefCntAutoPtr<Diligent::IRenderDevice> _pDevice);
    void bind(Diligent::RefCntAutoPtr<Diligent::IDeviceContext> _pImmediateContext);
    void unbind();

    private:
    int2 size = int2(0, 0);
    int numtargets;

    Diligent::RefCntAutoPtr<Diligent::ITexture> pRenderTarget[8];
    Diligent::ITextureView* pRTV[8];
    Diligent::ITextureView* pSRV[8];

    public:
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> PSO{};
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> SRB{};
};


















/*  This is a single "rectangle", can be curved*/
class oneTexture {
    public:
    int texWidth = 1;  // these are on teh small end but multiply by 4 still block based
    int texHeight = 2;
    int numMips = 5;
    float2 start = float2(0.5f, 0.5f);
    float2 stop = float2(0.5f, 0.4f);
    float2 bezier = float2(0.f, 0.f);
    float width = 0.05f;  // all in UV coordinates
    float bezierOffset = 0.f;

    std::array<float, 9> offset = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    std::array<float, 9> extents = {1, 1, 1, 1, 1, 1, 1, 1, 1};

    float2 a, b, c, d;

    template <class Archive>
    void serialize(Archive& archive, unsigned int const _version) {
        archive(texWidth);
        archive(texHeight);
        archive_float2(start);
        archive_float2(stop);
        archive_float2(bezier);
        archive(width);

        archive(offset);
        archive(extents);

        if (_version >= 101) {
            archive(bezierOffset);
        }
    }
};
CEREAL_CLASS_VERSION(oneTexture, 101);

enum texTypes { tex_albedo, tex_alpha, tex_normal, tex_translucency, tex_displacement };

const char* texNames[] = {"albedo", "alpha", "normal", "translucency", "displacement"};

class textureTool {
    public:
    void renderGui_A();
    void renderGui_TEX();
    void renderGui_B();
    void renderGui_C();
    void renderGui_main();
    void renderGui_right();
    void renderGui();
    void load_texture(uint _slot);
    void clear_texture(uint _slot);
    void load();
    void save();
    void save_as();
    void reloadTextures();

    void init();
    void renderToTexture(int _slot);
    void exportNow();

    std::string path;
    std::string name = "please load something";
    std::array<std::string, 5> texturePaths;

    Diligent::RefCntAutoPtr<Diligent::ITexture> tex_input[5];

    // Fbo::SharedPtr fbo;

    bool flipRed = false;
    bool flipGreen = false;
    float normalScale = 1.f;

    std::vector<oneTexture> textures;

    // material properties
    bool changed = false;
    texTypes mainViewType = tex_albedo;
    float zoom = 1.f;
    float2 pan = {0, 0};
    int requestDelete = -1;
    int currentTexture = -1;
    bool clickMode = false;  // do we click or dag for entry
    int clickCount = 0;

    fbo FBO;
    Diligent::RefCntAutoPtr<Diligent::IRenderDevice> m_pDevice;  // needed toload texturesa dlgnt_device
    Diligent::RefCntAutoPtr<Diligent::IDeviceContext> m_pImmediateContext;
    Diligent::RefCntAutoPtr<Diligent::ISwapChain> m_pSwapChain;

    Diligent::RefCntAutoPtr<Diligent::IShader> VS;
    Diligent::RefCntAutoPtr<Diligent::IShader> PS;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> PSO;

    template <class Archive>
    void serialize(Archive& archive, unsigned int const _version) {
        archive(CEREAL_NVP(path));
        archive(CEREAL_NVP(texturePaths));

        archive(CEREAL_NVP(flipRed));
        archive(CEREAL_NVP(flipGreen));
        archive(CEREAL_NVP(normalScale));

        archive(CEREAL_NVP(textures));
    }
};
CEREAL_CLASS_VERSION(textureTool, 100);






























/// TextureSplitTool - an EarthworksFX app that runs WITHOUT a terrain scene
/// (EarthworksFXAppSettings::CreateScene == false). It still gets the complete
/// rendering environment (device, swap chain, ImGui, Falcor device/framework and
/// the Earthworks shaders), and drives all rendering itself.
///
/// Every overridable base hook is declared here - most are empty - so this class
/// doubles as a reference for what an EarthworksFX application can customize.
class TextureSplitTool final : public Diligent::EarthworksFXApplicationBase
{
public:
    TextureSplitTool();
    virtual ~TextureSplitTool();

protected:
    // --- one-time setup -----------------------------------------------------

    /// After logging/config init, before any graphics exist.
    void Initialize() override;

    /// Tweak window/device settings. This is where the tool opts out of the scene.
    void OnConfigureSettings(Diligent::EarthworksFXAppSettings& settings) override;

    /// Tweak engine creation (extra device features, swap chain format, ...).
    void OnModifyEngineInitInfo(const ModifyEngineInitInfoAttribs& attribs) override;

    /// Device, swap chain, ImGui and the rendering environment are ready. Create
    /// the tool's GPU resources / pipelines here.
    void OnGraphicsReady() override;

    // --- per-frame loop -----------------------------------------------------

    /// Per-frame scene render. With no terrain scene, the tool renders here using
    /// GetRenderContext() / GetTargetFbo().
    void OnRender() override;

    /// Per-frame update.
    void OnUpdate(double current_time, double elapsed_time, bool do_update_ui) override;

    /// Swap-chain resize. Recreate size-dependent targets here, e.g.
    /// SetTargetFbo(Falcor::Fbo::createFromSwapChain(m_pSwapChain)).
    void OnWindowResized(Diligent::Uint32 Width, Diligent::Uint32 Height) override;

    /// App ImGui pass. Call DrawCommonUI() to keep the shared overlay.
    void UpdateUI() override;

    // --- input --------------------------------------------------------------

    /// Feed live mouse state into the (absent) Earthworks camera. Unused here.
    void SyncInput() override;

    // --- misc ---------------------------------------------------------------

    /// Release swap-chain-sized resources before a resize/fullscreen switch.
    void ReleaseSwapChainBuffers() override;

    /// Handle a raw platform message. Return true if consumed.
    bool HandleSampleNativeMessage(const void* native_msg_data) override;

    /// Parse app-specific command-line arguments.
    Diligent::AppBase::CommandLineStatus ProcessSampleCommandLine(int argc, const char* const* argv) override;

private:
    textureTool texture_tool;
};

#endif  // TEXTURESPLITTOOL_SRC_TEXTURESPLITTOOL_HPP_
