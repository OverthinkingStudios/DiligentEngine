#include "TextureSplitTool.hpp"

#include "imgui.h"
//#include "GraphicsAccessories.hpp"
#include "FileWrapper.hpp"

gui _Gui;

earthworksPaths ew_paths;
std::string earthworksPaths::root = "F:/ESim_NextCloud/eSim-Plantwork/resources/";

bool earthworksPaths::make_relative(std::string& _path) {
    clean(_path);
    if (_path.find(root) == 0) {
        _path = _path.substr(root.length());
        return true;
    }

    return false;  // its not in the relative path
}

void earthworksPaths::make_full(std::string& _path) { _path = root + _path; }

std::string earthworksPaths::get_full(std::string& _path) { return (root + _path); }

std::string earthworksPaths::get_name(std::string& _path) {
    size_t start = _path.find_last_of("/\\") + 1;
    size_t stop = _path.find_last_of(".");
    return _path.substr(start, stop - start);
}

std::string earthworksPaths::get_fullname(std::string& _path) {
    size_t start = _path.find_last_of("/\\");
    return _path.substr(start);
}

void earthworksPaths::replaceAll(std::string& _str, const std::string& _from, const std::string& _to) {
    if (_from.empty()) return;

    size_t start_pos = 0;
    // Find the next occurrence starting from the last updated position
    while ((start_pos = _str.find(_from, start_pos)) != std::string::npos) {
        _str.replace(start_pos, _from.length(), _to);
        // Advance position past the replacement to handle cases where 'to' contains 'from'
        start_pos += _to.length();
    }
}

void earthworksPaths::clean(std::string& _path) {
    replaceAll(_path, "\\", "/");
    replaceAll(_path, "//", "/");
}

void earthworksPaths::to_back_slash(std::string& _path) { replaceAll(_path, "/", "\\"); }

void fbo::setup(int2 _size, int _numTargets, Diligent::RefCntAutoPtr<Diligent::IRenderDevice> _pDevice) {
    if (_size != size) {
        size = _size;
        numtargets = _numTargets;

        Diligent::TextureDesc RTDesc;
        RTDesc.Type = Diligent::RESOURCE_DIM_TEX_2D;
        RTDesc.Width = size.x;   // Desired width
        RTDesc.Height = size.y;  // Desired height
        RTDesc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
        RTDesc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;

        Diligent::GraphicsPipelineStateCreateInfo PSOCreateInfo;

        PSOCreateInfo.GraphicsPipeline.NumRenderTargets = _numTargets;
        PSOCreateInfo.GraphicsPipeline.DSVFormat = Diligent::TEX_FORMAT_UNKNOWN;  // Or your depth format

        for (int i = 0; i < _numTargets; i++) {
            _pDevice->CreateTexture(RTDesc, nullptr, &pRenderTarget[i]);
            pRTV[i] = pRenderTarget[i]->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
            pSRV[i] = pRenderTarget[i]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
            PSOCreateInfo.GraphicsPipeline.RTVFormats[i] = RTDesc.Format;
        }

        _pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &PSO);
    }
}

void fbo::bind(Diligent::RefCntAutoPtr<Diligent::IDeviceContext> _pImmediateContext) {
    float ClearColor[] = {0.0f, 0.0f, 0.0f, 1.0f};
    for (int i = 0; i < numtargets; i++) {
        _pImmediateContext->ClearRenderTarget(pRTV[i], ClearColor, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }
    // m_pImmediateContext->ClearDepthStencil(pDSV, CLEAR_DEPTH_FLAG, 1.0f, 0); // Clear depth if used

    _pImmediateContext->SetRenderTargets(numtargets, &pRTV[0], nullptr,
                                         Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    Diligent::Viewport VP{0, 0, static_cast<float>(size.x), static_cast<float>(size.y), 0.f, 1.f};
    _pImmediateContext->SetViewports(1, &VP, 0, 0);

    // 4. Issue draw calls
    _pImmediateContext->SetPipelineState(PSO);
    _pImmediateContext->CommitShaderResources(SRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    Diligent::DrawAttribs DrawAttrs;
    DrawAttrs.NumVertices = 4;
    DrawAttrs.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;  // Verify all states
    _pImmediateContext->Draw(DrawAttrs);

    _pImmediateContext->SetRenderTargets(0, nullptr, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void fbo::unbind() {}

// RefCntAutoPtr<IShader> VS = CreateShader(m_pDevice, nullptr, "FullScreenTriangleVS.fx", "FullScreenTriangleVS",
// SHADER_TYPE_VERTEX); ShaderCI.CompileFlags |= SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
void textureTool::init() {
    Diligent::RefCntAutoPtr<Diligent::IShaderSourceInputStreamFactory> pShaderSourceFactory;
    m_pDevice->GetEngineFactory()->CreateDefaultShaderSourceStreamFactory("shaders", &pShaderSourceFactory);
    // Create a compound shader source factory that will be able to load DiligentFX shaders.
    // auto pCompoundShaderSourceFactory =
    // CreateCompoundShaderSourceFactory({&Diligent::DiligentFXShaderSourceStreamFactory::GetInstance(),
    // pShaderSourceFactory});

    Diligent::ShaderCreateInfo ShaderCI;

    ShaderCI.EntryPoint = "vsMain";
    ShaderCI.FilePath = "extractTextures.hlsl";
    ShaderCI.Macros = {};
    ShaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
    ShaderCI.Desc.Name = "vsMain";
    ShaderCI.pShaderSourceStreamFactory = pShaderSourceFactory;
    ShaderCI.Desc.UseCombinedTextureSamplers = true;
    ShaderCI.CompileFlags = Diligent::SHADER_COMPILE_FLAG_NONE;
    //    VS = Diligent::RenderDeviceWithCache<false>{m_pDevice, nullptr}.CreateShader(ShaderCI);

    ShaderCI.EntryPoint = "psMain";
    ShaderCI.Desc.Name = "psMain";
    ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    //    PS = Diligent::RenderDeviceWithCache<false>{m_pDevice, nullptr}.CreateShader(ShaderCI);
}

void textureTool::exportNow() {
    for (int i = 0; i < textures.size(); i++) {
        renderToTexture(i);
    }
}

void textureTool::renderToTexture(int _slot) {
    int w = (int)(textures[_slot].texWidth * 4 * pow(2, textures[_slot].numMips));
    int h = (int)(textures[_slot].texHeight * 4 * pow(2, textures[_slot].numMips));

    Diligent::TextureDesc RTDesc;
    RTDesc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    RTDesc.Width = w;   // Desired width
    RTDesc.Height = h;  // Desired height
    RTDesc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
    RTDesc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;

    Diligent::RefCntAutoPtr<Diligent::ITexture> pRenderTarget[5];
    Diligent::ITextureView* pRTV[5];
    Diligent::ITextureView* pSRV[5];
    Diligent::GraphicsPipelineStateCreateInfo PSOCreateInfo;
    // ... (Set up shaders, input layout, primitive topology) ...

    PSOCreateInfo.GraphicsPipeline.NumRenderTargets = 5;
    PSOCreateInfo.GraphicsPipeline.DSVFormat = Diligent::TEX_FORMAT_UNKNOWN;  // Or your depth format
    //   float ClearColor[]                              = {0.0f, 0.0f, 0.0f, 1.0f};

    for (int i = 0; i < 5; i++) {
        m_pDevice->CreateTexture(RTDesc, nullptr, &pRenderTarget[i]);
        pRTV[i] = pRenderTarget[i]->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
        pSRV[i] = pRenderTarget[i]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);  // not needed here
        PSOCreateInfo.GraphicsPipeline.RTVFormats[i] = RTDesc.Format;
        // m_pImmediateContext->ClearRenderTarget(pRTV[i], ClearColor);
        // Diligent::ShaderResourceVariableX{FBO.SRB, Diligent::SHADER_TYPE_PIXEL,
        // "g_TextureEnvironmentMap"}.Set(pSRV[i]);
    }
    // m_pImmediateContext->ClearDepthStencil(pDSV, CLEAR_DEPTH_FLAG, 1.0f, 0); // Clear depth if used
    // mDevice->CreateGraphicsPipelineState(PSOCreateInfo, &mAsteroidsPSO);
    // m_pDevice.CreateGraphicsPipelineState(PSOCreateInfo, &PSO);

    /*
    // 2. Bind the offscreen render target (and depth buffer if available)
    m_pImmediateContext->SetRenderTargets(5, &pRTV, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // 3. Update the viewport to match the texture dimensions
    Diligent::Viewport VP{0, 0, static_cast<float>(RTDesc.Width), static_cast<float>(RTDesc.Height), 0.f, 1.f};
    m_pImmediateContext->SetViewports(1, &VP, 0, 0);

    // 4. Issue draw calls
    m_pImmediateContext->SetPipelineState(PSO);
    //m_pImmediateContext->CommitShaderResources(Diligent::m_pOffscreenSRB,
    Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    //m_pImmediateContext->Draw(DrawAttrs);
    */
}

void textureTool::reloadTextures() {
    for (int i = 0; i < 5; i++) {
        tex_input[i].Release();
        std::string fullPath = ew_paths.get_full(texturePaths[i]);
        if (std::filesystem::exists(fullPath)) {
            Diligent::TextureLoadInfo LoadInfo{texNames[i]};
            LoadInfo.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
            CreateTextureFromFile(fullPath.c_str(), LoadInfo, m_pDevice, &tex_input[i]);
        }
    }
}

void textureTool::load() {
    Diligent::FileDialogAttribs OpenDialogAttribs{Diligent::FILE_DIALOG_TYPE_OPEN};
    OpenDialogAttribs.Title = "texture tool";
    OpenDialogAttribs.Filter = "texture tool files\0*.textureTool\0";
    std::string FileName = Diligent::FileSystem::FileDialog(OpenDialogAttribs);
    if (!FileName.empty()) {
        if (ew_paths.make_relative(FileName)) {
            path = FileName;
            name = ew_paths.get_name(FileName);

            std::ifstream is(ew_paths.get_full(FileName));
            cereal::JSONInputArchive archive(is);
            archive(*this);
            changed = false;
            reloadTextures();
        }
    }
}

void textureTool::save() {
    std::ofstream os(ew_paths.get_full(path));
    cereal::JSONOutputArchive archive(os);
    archive(*this);
    changed = false;
}

void textureTool::save_as() {
    
    Diligent::FileDialogAttribs OpenDialogAttribs{Diligent::FILE_DIALOG_TYPE_SAVE};
    OpenDialogAttribs.Title = "texture tool";
    OpenDialogAttribs.Filter = "texture tool files\0*.textureTool\0";
    std::string FileName = Diligent::FileSystem::FileDialog(OpenDialogAttribs);
    if (FileName.find(".textureTool") == std::string::npos)
        FileName += ".textureTool";  // not sure why the dialog does not add teh extention
    if (!FileName.empty()) {
        if (ew_paths.make_relative(FileName)) {
            path = FileName;
            name = ew_paths.get_name(FileName);
            save();
        }
    }
    
}

void textureTool::load_texture(uint _slot) {
    
    Diligent::FileDialogAttribs OpenDialogAttribs{Diligent::FILE_DIALOG_TYPE_OPEN};
    OpenDialogAttribs.Title = "Select GLTF file";
    OpenDialogAttribs.Filter = "image files\0*.png;*.tif;*.jpg\0";
    std::string FileName = Diligent::FileSystem::FileDialog(OpenDialogAttribs);
    if (!FileName.empty()) {
        if (ew_paths.make_relative(FileName)) {
            texturePaths[_slot] = FileName;
            Diligent::TextureLoadInfo LoadInfo{texNames[_slot]};
            switch (_slot) {
                case 0:
                    LoadInfo.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
                    break;
                case 1:
                    LoadInfo.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
                    break;
                case 2:
                    LoadInfo.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
                    break;
                case 3:
                    LoadInfo.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
                    break;
                case 4:
                    LoadInfo.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
                    break;
            };
            tex_input[_slot].Release();
            CreateTextureFromFile(ew_paths.get_full(FileName).c_str(), LoadInfo, m_pDevice, &tex_input[_slot]);
        } else {
            // warn no tin poath
        }
    }
    
}

void textureTool::clear_texture(uint _slot) {
    texturePaths[_slot].clear();
    tex_input[_slot].Release();
}

void textureTool::renderGui_A() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec2 space = ImGui::GetContentRegionAvail();

    // ImGui::SetCursorPosY(200);
    for (int i = 0; i < 7; i++) ImGui::NewLine();
    for (int i = 0; i < textures.size(); i++) {
        ImGui::PushID(&textures[i]);
        style.Colors[ImGuiCol_FrameBg] =
            (currentTexture == i) ? ImVec4(0.3f, 0.2f, 0.02f, 1.0f) : ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
        ImGui::BeginChildFrame(1000 + i, ImVec2(space.x, ImGui::GetFontSize() * 2), 0);
        {
            ImGui::Text("%d - (%d, %d)", i, (int)(textures[i].texWidth * 4 * pow(2, textures[i].numMips)),
                        (int)(textures[i].texHeight * 4 * pow(2, textures[i].numMips)));
        }
        ImGui::EndChildFrame();

        if (ImGui::IsItemClicked(0)) currentTexture = i;

        if (ImGui::BeginPopupContextItem("left_context")) {
            if (ImGui::MenuItem("delete")) {
                textures.erase(textures.begin() + i);
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
}

ImVec2 toImVec2_b(float2 x) { return ImVec2(x.x, x.y); }
void textureTool::renderGui_TEX() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.FramePadding = ImVec2(0, 0);
    style.WindowPadding = ImVec2(0, 0);

    static int dragSelector = 0;
    ImGui::NewLine();
    ImVec2 space = ImGui::GetContentRegionAvail();

    ImGui::BeginChild(100, space, true, ImGuiWindowFlags_NoScrollWithMouse);
    {
        ImVec2 root_pos = ImGui::GetCursorScreenPos();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        static ImVec2 circlePos = {100, 100};

        if (tex_input[mainViewType]) {
            Diligent::ITextureView* pSRV =
                tex_input[mainViewType]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
            ImVec2 size = {(float)tex_input[mainViewType]->GetDesc().GetWidth(),
                           (float)tex_input[mainViewType]->GetDesc().GetHeight()};
            ImGui::ImageWithBg(reinterpret_cast<ImTextureID>(pSRV), size * zoom, ImVec2(0, 0), ImVec2(1, 1),
                               ImVec4(0.0f, 0.0f, 0.0f, 1.0f));

            // mouse to imagePixelCoordinates
            ImVec2 mouseRelative = ImGui::GetMousePos() - ImGui::GetWindowPos();
            ImVec2 imageScroll = ImVec2(ImGui::GetScrollX(), ImGui::GetScrollY());
            ImVec2 imageTotal = (imageScroll + mouseRelative) / zoom;
            float2 imagePixelPos = float2(imageTotal.x, imageTotal.y);

            if (ImGui::IsItemHovered()) {
                // zoom using mouse wheel
                float scroll = ImGui::GetIO().MouseWheel;
                if (scroll != 0.f) {
                    float oldZoom = zoom;
                    float zoom_min = ImGui::GetWindowWidth() / size.x;

                    zoom *= (1.f + scroll * 0.2f);
                    zoom = Diligent::clamp(zoom, zoom_min, 16.f);
                    ImVec2 imageScroll_2 = ((imageScroll + mouseRelative) / oldZoom) * zoom - mouseRelative;
                    ImGui::SetScrollX(__max(0, imageScroll_2.x));
                    ImGui::SetScrollY(__max(0, imageScroll_2.y));
                }

                if (clickMode) {
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        auto& T = textures[currentTexture];

                        switch (clickCount) {
                            case 0:
                                T.start = imagePixelPos;
                                clickCount++;
                                break;
                            case 1:
                                T.stop = imagePixelPos;
                                T.bezier = (T.start + T.stop) * 0.5f;
                                clickCount++;
                                break;
                            case 2:
                                float2 tangent = glm::normalize(T.stop - T.start);
                                float2 right = float2(-tangent.y, tangent.x);
                                float2 diff = imagePixelPos - T.start;
                                T.width = abs(glm::dot(diff, right));

                                // auto setup the aspect
                                // can be done a lot better by alowing 2:3 etc
                                float aspect = glm::length(T.stop - T.start) / (T.width * 2.f);
                                if (aspect > 1.f) {
                                    T.texWidth = 1;
                                    T.texHeight = (int)(aspect + 0.5f);
                                } else {
                                    T.texWidth = (int)(1.f / aspect + 0.5f);
                                    T.texHeight = 1;
                                }
                                clickCount++;
                                clickMode = false;
                                break;
                        }
                    }
                } else {
                    if (!ImGui::IsAnyMouseDown() && currentTexture >= 0 && currentTexture < textures.size()) {
                        auto& T = textures[currentTexture];
                        float A = glm::length(T.start - imagePixelPos);
                        float B = glm::length(T.stop - imagePixelPos);
                        float C = glm::length(T.bezier - imagePixelPos);

                        if (A < 10)
                            dragSelector = 1;
                        else if (B < 10)
                            dragSelector = 2;
                        else if (C < 10)
                            dragSelector = 3;
                        else
                            dragSelector = 0;
                    }

                    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                        if (dragSelector > 0) {
                            auto& T = textures[currentTexture];
                            switch (dragSelector) {
                                case 1:
                                    T.start = imagePixelPos;
                                    break;
                                case 2:
                                    T.stop = imagePixelPos;
                                    break;
                                case 3:
                                    T.bezier = imagePixelPos;
                                    break;
                            }
                        } else {
                            ImVec2 delta = ImGui::GetIO().MouseDelta;
                            ImGui::SetScrollX(ImGui::GetScrollX() - delta.x);
                            ImGui::SetScrollY(ImGui::GetScrollY() - delta.y);
                        }
                    }
                }
            }

            if (ImGui::BeginPopupContextItem("main_texture_context")) {
                if (ImGui::MenuItem("new")) {
                    currentTexture = textures.size();
                    textures.emplace_back();
                    clickMode = true;
                    clickCount = 0;
                }
                if (ImGui::MenuItem("delete")) {
                    if (textures.size() > 0) {
                        textures.erase(textures.begin() + currentTexture);
                    }
                }
                ImGui::EndPopup();
            }
        }

        for (int i = 0; i < textures.size(); i++) {
            auto& T = textures[i];
            ImU32 A = IM_COL32(128, 128, 128, 255);
            ImU32 B = IM_COL32(128, 228, 128, 255);
            ImU32 C = IM_COL32(128, 128, 228, 255);
            ImU32 D = IM_COL32(228, 128, 128, 255);
            ImU32 W = IM_COL32(255, 255, 255, 255);

            draw_list->AddCircle(root_pos + toImVec2_b(T.start) * zoom, 10, (currentTexture == i) ? B : A, 50, 3.f);
            draw_list->AddCircle(root_pos + toImVec2_b(T.stop) * zoom, 10, (currentTexture == i) ? C : A, 50, 3.f);
            draw_list->AddCircle(root_pos + toImVec2_b(T.bezier) * zoom, 10, (currentTexture == i) ? D : A, 50, 3.f);

            if (currentTexture == i) {
                switch (dragSelector) {
                    case 1:
                        draw_list->AddCircle(root_pos + toImVec2_b(T.start) * zoom, 12, W, 50, 3.f);
                        break;
                    case 2:
                        draw_list->AddCircle(root_pos + toImVec2_b(T.stop) * zoom, 12, W, 50, 3.f);
                        break;
                    case 3:
                        draw_list->AddCircle(root_pos + toImVec2_b(T.bezier) * zoom, 12, W, 50, 3.f);
                        ;
                        break;
                }
            }

            draw_list->AddBezierQuadratic(root_pos + toImVec2_b(T.start) * zoom, root_pos + toImVec2_b(T.bezier) * zoom,
                                          root_pos + toImVec2_b(T.stop) * zoom, (currentTexture == i) ? B : A, 2);

            float2 tangent = Diligent::normalize(T.bezier - T.start);
            float2 right = {-tangent.y, tangent.x};
            float2 tangent2 = Diligent::normalize(T.stop - T.bezier);
            float2 right2 = {-tangent2.y, tangent2.x};
            float2 tangentb = Diligent::normalize(T.stop - T.start);
            float2 rightb = {-tangentb.y, tangentb.x};

            draw_list->AddBezierQuadratic(root_pos + toImVec2_b(T.start + right * T.width) * zoom,
                                          root_pos + toImVec2_b(T.bezier + rightb * T.width) * zoom,
                                          root_pos + toImVec2_b(T.stop + right2 * T.width) * zoom,
                                          (currentTexture == i) ? B : A, 2);
            draw_list->AddBezierQuadratic(root_pos + toImVec2_b(T.start - right * T.width) * zoom,
                                          root_pos + toImVec2_b(T.bezier - rightb * T.width) * zoom,
                                          root_pos + toImVec2_b(T.stop - right2 * T.width) * zoom,
                                          (currentTexture == i) ? B : A, 2);
        }
    }
    ImGui::EndChild();
}

void textureTool::renderGui_B() {
    for (int i = 0; i < 5; i++) {
        ImGui::PushID(100 + i);
        ImGui::SameLine(0 + i * 150.f, 0);

        if (tex_input[i]) {
            Diligent::ITextureView* pSRV = tex_input[i]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
            ImGui::ImageWithBg(reinterpret_cast<ImTextureID>(pSRV), ImVec2(128, 128), ImVec2(0, 0), ImVec2(1, 1),
                               ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        } else {
            ImGui::Button(texNames[i], ImVec2(128, 128));
        }

        if (ImGui::IsItemClicked(0)) mainViewType = (texTypes)i;
        if (ImGui::BeginPopupContextItem("small_texture_context")) {
            if (ImGui::MenuItem("load")) {
                load_texture(i);
            }
            if (ImGui::MenuItem("clear")) {
                requestDelete = i;
            }

            ImGui::EndPopup();
        }

        ImGui::PopID();
    }

    renderGui_TEX();
}

void textureTool::renderGui_C() {
    _Gui.text(font_H1, "normal map");
    changed |= _Gui.checkbox("flip red", &flipRed);
    changed |= _Gui.checkbox("flip green", &flipGreen);
    changed |= _Gui.dragFloat("normal scale", &normalScale, 0.01f, 0.1f, 3.f);
    ImGui::Separator();

    ImVec2 space = ImGui::GetContentRegionAvail();

    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
    ImGui::BeginChildFrame(1001, ImVec2(space.x, ImGui::GetFontSize() * 6), 0);
    {
        if (currentTexture >= 0 && currentTexture < textures.size()) {
            //??? Do vidually instead with a table up to maybe 8x8? Just click in a celland all left top will light
            ImGui::DragInt("width (blocks)", &textures[currentTexture].texWidth, 0.02f, 1, 16);
            ImGui::DragInt("height (blocks)", &textures[currentTexture].texHeight, 0.02f, 1, 16);
            ImGui::DragInt("mips", &textures[currentTexture].numMips, 0.02f, 1, 8);
            ImGui::Text("(%d, %d)",
                        (int)(textures[currentTexture].texWidth * 4 * pow(2, textures[currentTexture].numMips)),
                        (int)(textures[currentTexture].texHeight * 4 * pow(2, textures[currentTexture].numMips)));
        }
    }
    ImGui::EndChildFrame();

    ImGui::Separator();
}

void textureTool::renderGui_main() {
    ImGuiStyle& style = ImGui::GetStyle();

    _Gui.text_centered(font_H3, name.c_str());
    _Gui.tooltip(font_H1, path.c_str());

    ImVec2 space = ImGui::GetContentRegionAvail();
    float A = 200;
    float C = (space.x - 200) * 0.4f;
    float B = space.x - A - C;

    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.0f);
    ImGui::BeginChildFrame(1, ImVec2(A, space.y), 0);
    {
        renderGui_A();
    }
    ImGui::EndChildFrame();

    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.02f, 0.02f, 0.2f, 0.0f);
    ImGui::SameLine();
    ImGui::BeginChildFrame(2, ImVec2(B, space.y), 0);
    {
        renderGui_B();
    }
    ImGui::EndChildFrame();

    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.02f, 0.2f, 0.02f, 0.0f);
    ImGui::SameLine();
    ImGui::BeginChildFrame(3, ImVec2(C, space.y), 0);
    {
        renderGui_C();
    }
    ImGui::EndChildFrame();
}

/*
if (ImGui::Button("load", ImVec2(140, 0)))
        {
            std::filesystem::path path;
            if (openFileDialog({ {"textureTool"} }, path))
            {
                std::ifstream is(path);
                cereal::JSONInputArchive archive(is);
                archive(textureToolData);
                changed = false;

                textureToolData.tex_albedo = Texture::createFromFile(terrafectorEditorMaterial::rootFolder +
textureToolData.albedo, true, true); textureToolData.tex_alpha =
Texture::createFromFile(terrafectorEditorMaterial::rootFolder + textureToolData.alpha, true, true);
                textureToolData.tex_normal = Texture::createFromFile(terrafectorEditorMaterial::rootFolder +
textureToolData.normal, true, false); textureToolData.tex_translucency =
Texture::createFromFile(terrafectorEditorMaterial::rootFolder + textureToolData.translucency, true, true);
                textureToolData.changed = false;
            }
        }

        if (textureToolData.changed)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.2f, 0.0f, 1.0f));
            ImGui::SameLine(0, 20);
            if (ImGui::Button("save", ImVec2(140, 0)))
            {
                std::ofstream os(terrafectorEditorMaterial::rootFolder + textureToolData.path);
                cereal::JSONOutputArchive archive(os);
                archive(textureToolData);
                textureToolData.changed = false;
            }
            ImGui::PopStyleColor();
        }

        //ImGui::SameLine(0, 20);
        if (ImGui::Button("save - as", ImVec2(140, 0)))
        {
            std::filesystem::path path = terrafectorEditorMaterial::rootFolder + textureToolData.path;
            if (saveFileDialog({ {"textureTool"} }, path))
            {
                std::ofstream os(path);
                cereal::JSONOutputArchive archive(os);
                textureToolData.path = materialCache::getRelative(path.parent_path().string() + "//" +
path.stem().string());

                archive(textureToolData);
                textureToolData.changed = false;
            }
        }
*/
void textureTool::renderGui_right() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_FrameBg] = changed ? ImVec4(0.3f, 0.2f, 0.0f, 1.0f) : ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
    ImGui::BeginChildFrame(200, ImVec2(0, 0), 0);
    {
        ImGui::NewLine();
        _Gui.text_centered(font_H1, "Texture tool");

        ImGui::PushFont(font_H1);
        {
            ImGui::NewLine();
            if (ImGui::Button("load", ImVec2(200, 0))) {
                load();
            }  // FIXME hard coded
            if (ImGui::Button("save", ImVec2(200, 0))) {
                save();
            }
            if (ImGui::Button("save-as", ImVec2(200, 0))) {
                save_as();
            }
        }
        ImGui::PopFont();
    }
    ImGui::EndChildFrame();
}

void textureTool::renderGui() {
    if (requestDelete >= 0) {
        clear_texture(requestDelete);
        requestDelete = -1;
    }

    ImGuiStyle& style = ImGui::GetStyle();

    // style.ScaleAllSizes(1.5f); ??? crahses

    ImGui::PushFont(font_normal);
    {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->Pos);
        ImGui::SetNextWindowSize(ImGui::GetMainViewport()->Size - ImVec2(200, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);  // Dark gray background
        ImGui::Begin("##main", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
        {
            renderGui_main();
        }
        ImGui::End();

        float x = ImGui::GetMainViewport()->Size.x - 200;
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->Pos + ImVec2(x, 0));
        ImGui::SetNextWindowSize(ImVec2(200, ImGui::GetMainViewport()->Size.y));
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);  // Dark gray background
        ImGui::Begin("##right", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
        {
            renderGui_right();
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }
    ImGui::PopFont();
    // DockSpaceOverViewport
}

TextureSplitTool::TextureSplitTool()
    : Diligent::EarthworksFXApplicationBase("TextureSplitTool", "texture-split-tool", overthinking::Env::Stage::Dev) {}

TextureSplitTool::~TextureSplitTool() = default;

void TextureSplitTool::Initialize() {}

void TextureSplitTool::OnConfigureSettings(Diligent::EarthworksFXAppSettings& settings) {
    // No terrain scene: build the environment + shaders only and render manually.
    settings.CreateScene = false;
    settings.VSync = true;
    // FIXME needs fulklscreen flag as well
}

void TextureSplitTool::OnModifyEngineInitInfo(const ModifyEngineInitInfoAttribs& attribs) {}

void TextureSplitTool::OnGraphicsReady() {
    ImGuiIO& io = ImGui::GetIO();
    font_small = io.Fonts->AddFontFromFileTTF("fonts/Plus_Jakarta_Sans/PlusJakartaSans-VariableFont_wght.ttf", 14.f);
    font_normal = io.Fonts->AddFontFromFileTTF("fonts/Plus_Jakarta_Sans/PlusJakartaSans-VariableFont_wght.ttf", 20.f);
    font_H1 = io.Fonts->AddFontFromFileTTF("fonts/Plus_Jakarta_Sans/PlusJakartaSans-VariableFont_wght.ttf", 30.f);
    font_H2 = io.Fonts->AddFontFromFileTTF("fonts/Plus_Jakarta_Sans/PlusJakartaSans-VariableFont_wght.ttf", 40.f);
    font_H3 = io.Fonts->AddFontFromFileTTF("fonts/Plus_Jakarta_Sans/PlusJakartaSans-VariableFont_wght.ttf", 50.f);
}

void TextureSplitTool::OnRender() {}

void TextureSplitTool::OnUpdate(double current_time, double elapsed_time, bool do_update_ui) {}

void TextureSplitTool::OnWindowResized(Diligent::Uint32 width, Diligent::Uint32 height) {}

void TextureSplitTool::UpdateUI() {
    EarthworksFXApplicationBase::UpdateUI();  // I dont want the common UI

    texture_tool.m_pDevice = m_pDevice;
    texture_tool.m_pImmediateContext = m_pImmediateContext;
    texture_tool.m_pSwapChain = m_pSwapChain;
    return texture_tool.renderGui();
}

void TextureSplitTool::SyncInput() {}

void TextureSplitTool::ReleaseSwapChainBuffers() {}

bool TextureSplitTool::HandleSampleNativeMessage(const void* native_msg_data) { return false; }

Diligent::AppBase::CommandLineStatus TextureSplitTool::ProcessSampleCommandLine(int argc, const char* const* argv) {
    return Diligent::AppBase::CommandLineStatus::OK;
}

namespace Diligent {

NativeAppBase* CreateApplication() { return new TextureSplitTool(); }

}  // namespace Diligent
