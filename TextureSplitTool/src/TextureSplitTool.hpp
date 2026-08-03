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








// gor printf arguments
#include <stdarg.h> 
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

    void tooltip(const char* _tooltip, ...) {
        va_list args;
        va_start(args, _tooltip);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(_tooltip, args);
    }

    void tooltip(ImFont* _font, const char* _tooltip, ...) {
        va_list args;
        va_start(args, _tooltip);
        ImGui::PushFont(_font);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(_tooltip, args);
        ImGui::PopFont();
    }

    void text(ImFont* _font, const char* _fmt, ...) {
        va_list args;
        va_start(args, _fmt);
        ImGui::PushFont(_font);
        //ImGui::TextUnformatted(_txt, args);
        ImGui::TextV(_fmt, args);
        ImGui::PopFont();
        va_end(args);
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

    bool dragInt(const char* _name, int* _data, float _speed, int _min, int _max, const char* _tooltip = "",
                   const char* _fmt = "%d") {
        ImGui::PushID(_data);
        BEGIN;
        if (ImGui::DragInt("##drag", _data, _speed, _min, _max, _fmt)) changed = true;
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















//??? can we just place FBO inside namepsce Diligent later for cleanyp
//try to make all my own copde calls into higher levelfunction sin IOTS common and never this lowe level
class render_target {
    public:
    void setup(int2 _size, int _numTargets, Diligent::RefCntAutoPtr<Diligent::IRenderDevice> _pDevice,
               Diligent::RefCntAutoPtr<Diligent::IDeviceContext> _pImmediateContext);
    int2 getSize() { return size; }

    int2 size = int2(0, 0);
    int numtargets = 0;

    Diligent::RefCntAutoPtr<Diligent::ITexture> pTexture[8];
    // BUGFIX: these were uninitialized raw pointers - anything reading them before
    // setup() (e.g. the FBO preview in the GUI) dereferenced garbage. Zero-init.
    Diligent::ITextureView* pRTV[8] = {};
    Diligent::ITextureView* pSRV[8] = {};
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
        if (_version >= 102) {
            archive(numMips);  // BUGFIX: was never serialized -> silently reset to 5 on every load
        }
    }
};
CEREAL_CLASS_VERSION(oneTexture, 102);

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
    void load(const char *name);
    void load();
    void save();
    void save_as();
    void reloadTextures();
    void onRender();

    void init();
    void renderToTexture(int _slot);
    void exportNow();

    std::string path;
    std::string name = "please load something";
    std::array<std::string, 5> texturePaths;

    Diligent::RefCntAutoPtr<Diligent::ITexture> tex_input[5];
    // BUGFIX: zero-init; slots whose file is missing were dangling and got handed
    // to Diligent as SRVs (machine-dependent garbage -> likely device removal).
    Diligent::ITextureView* pSRV[5] = {};

    // 1x1 fallback textures so a shader slot is NEVER left unbound (unbound
    // descriptors are undefined behaviour on the GPU and a classic TDR trigger).
    Diligent::RefCntAutoPtr<Diligent::ITexture> tex_fallback_white;
    Diligent::RefCntAutoPtr<Diligent::ITexture> tex_fallback_normal;
    Diligent::ITextureView* pFallbackWhiteSRV = nullptr;
    Diligent::ITextureView* pFallbackNormalSRV = nullptr;

    // Fbo::SharedPtr fbo;

    bool flipRed = false;
    bool flipGreen = false;
    float normalScale = 1.f;

    std::vector<oneTexture> textures;

    // material properties
    bool changed = false;
    bool changed_for_save = false;
    texTypes mainViewType = tex_albedo;
    float zoom = 1.f;
    float2 pan = {0, 0};
    int requestDelete = -1;
    int currentTexture = -1;
    bool clickMode = false;  // do we click or dag for entry
    int clickCount = 0;

    render_target FBO;
    Diligent::RefCntAutoPtr<Diligent::IRenderDevice> m_pDevice;  // needed toload texturesa dlgnt_device
    Diligent::RefCntAutoPtr<Diligent::IDeviceContext> m_pImmediateContext;
    Diligent::RefCntAutoPtr<Diligent::ISwapChain> m_pSwapChain;

    Diligent::RefCntAutoPtr<Diligent::IShader> VS;
    Diligent::RefCntAutoPtr<Diligent::IShader> GS;
    Diligent::RefCntAutoPtr<Diligent::IShader> PS;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> PSO;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> SRB;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceVariable> rsVARS[10];
    // BUGFIX: gConstantBuffer in extractTextures.hlsl was never created or bound -
    // the GS/PS read undefined values. This is its backing buffer.
    Diligent::RefCntAutoPtr<Diligent::IBuffer> constantsCB;

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
