#include "TextureSplitTool.hpp"

#include "imgui.h"
#include "overthinkingEnv.h"
// #include "GraphicsAccessories.hpp"
#include "FileWrapper.hpp"
#include "MapHelper.hpp"
// NOTE: removed `#pragma optimize("", off)` - besides the perf hit it changes
// uninitialized-memory patterns, which can mask/unmask exactly the kind of
// machine-dependent bug we were chasing. Re-add locally when stepping through.
gui _Gui;

#pragma optimize("", off)

float header_height;




void saveTexture(Diligent::RefCntAutoPtr<Diligent::IRenderDevice> m_pDevice, 
                 Diligent::RefCntAutoPtr<Diligent::IDeviceContext> pContext,
                 Diligent::RefCntAutoPtr<Diligent::ITexture> _tex,
                 std::string filename, bool _alpha, Diligent::IMAGE_FILE_FORMAT _format) 
{
    Diligent::TextureDesc StagingTexDesc;
    StagingTexDesc.Name = "Staging texture for download";
    StagingTexDesc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    StagingTexDesc.Usage = Diligent::USAGE_STAGING;
    StagingTexDesc.CPUAccessFlags = Diligent::CPU_ACCESS_READ;
    StagingTexDesc.Width = _tex->GetDesc().Width;
    StagingTexDesc.Height = _tex->GetDesc().Height;
    StagingTexDesc.Format = _tex->GetDesc().Format;

    Diligent::RefCntAutoPtr<Diligent::ITexture> pStagingTex;
    m_pDevice->CreateTexture(StagingTexDesc, nullptr, &pStagingTex);

    // copy data
    Diligent::CopyTextureAttribs CopyAttribs(_tex, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, pStagingTex,
                                             Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    pContext->CopyTexture(CopyAttribs);
    pContext->Flush();  // Flush the context to ensure the GPU finishes the copy operation
    pContext->WaitForIdle();

    // map the data
    Diligent::MappedTextureSubresource MappedData;
    //MAP_FLAG_DO_NOT_WAIT
    pContext->MapTextureSubresource(pStagingTex, 0, 0, Diligent::MAP_READ, Diligent::MAP_FLAG_NONE, nullptr,
                                    MappedData);
    {
        Diligent::Image::EncodeInfo Info;
        Info.Width = StagingTexDesc.Width;
        Info.Height = StagingTexDesc.Height;
        Info.TexFormat = StagingTexDesc.Format;
        Info.KeepAlpha = _alpha;
        Info.FlipY = m_pDevice->GetDeviceInfo().IsGLDevice();
        Info.pData = MappedData.pData;
        Info.Stride = static_cast<Diligent::Uint32>(MappedData.Stride);
        Info.FileFormat = _format;
        Info.JpegQuality = 95;

        Diligent::RefCntAutoPtr<Diligent::IDataBlob> pEncodedImage;
        Diligent::Image::Encode(Info, &pEncodedImage);

        FILE* f = fopen(filename.c_str(), "wb");
        if (f) {
            fwrite(pEncodedImage->GetDataPtr(), 1, pEncodedImage->GetSize(), f);
            fclose(f);
        }
    }
    pContext->UnmapTextureSubresource(pStagingTex, 0, 0);
}






earthworksPaths ew_paths;
std::string earthworksPaths::root = "";

bool earthworksPaths::make_relative(std::string& _path) {
    clean(_path);
    clean(root);
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

std::string earthworksPaths::get_pathNoExt(std::string& _path) {
    size_t stop = _path.find_last_of(".");
    return _path.substr(0, stop);
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

void render_target::setup(int2 _size, int _numTargets, Diligent::RefCntAutoPtr<Diligent::IRenderDevice> _pDevice,
                          Diligent::RefCntAutoPtr<Diligent::IDeviceContext> _pImmediateContext) {
    if (_size != size) {
        size = _size;
        numtargets = _numTargets;

        Diligent::TextureDesc RTDesc;
        RTDesc.Name = "render target";
        RTDesc.Type = Diligent::RESOURCE_DIM_TEX_2D;
        RTDesc.Width = size.x;   // Desired width
        RTDesc.Height = size.y;  // Desired height
        RTDesc.MipLevels = 1;
        RTDesc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
        RTDesc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
        RTDesc.ClearValue.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
        // Match the clear color actually used in renderToTexture() (green) -
        // a mismatched optimal-clear value causes slow clears / warnings.
        RTDesc.ClearValue.Color[0] = 0.f;
        RTDesc.ClearValue.Color[1] = 1.f;
        RTDesc.ClearValue.Color[2] = 0.f;
        RTDesc.ClearValue.Color[3] = 1.f;

        for (int i = 0; i < _numTargets; i++) {
            pTexture[i].Release();
            pRTV[i] = nullptr;
            pSRV[i] = nullptr;
            _pDevice->CreateTexture(RTDesc, nullptr, &pTexture[i]);
            if (pTexture[i]) {  // BUGFIX: CreateTexture can fail; was a null deref
                pRTV[i] = pTexture[i]->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
                pSRV[i] = pTexture[i]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
            }
        }
    }
}

// CPU mirror of gConstantBuffer in extractTextures.hlsl. Layout must match HLSL
// cbuffer packing exactly: no float2 straddles a 16-byte boundary here, and an
// HLSL cbuffer bool is 4 bytes (hence int). 80 bytes total.
struct ExtractTexturesConstants {
    float2 A, B, C, D;           // corners of the extraction quad (UV space)
    float2 start, stop, bezier;  // curve control points (UV space)
    float width;                 // half-width of the strip (UV space)
    float padd;
    int flipRed;
    int flipGreen;
    float nStrength;
    int toSRGB;

    // MAterial
    float4 albedoScale[2];
    float roughness[2];
};
static_assert(sizeof(ExtractTexturesConstants) == 120, "must match gConstantBuffer in extractTextures.hlsl");

void textureTool::init() {
    Diligent::RefCntAutoPtr<Diligent::IShaderSourceInputStreamFactory> pShaderSourceFactory;
    m_pDevice->GetEngineFactory()->CreateDefaultShaderSourceStreamFactory("shaders", &pShaderSourceFactory);

    Diligent::ShaderCreateInfo ShaderCI;
    ShaderCI.EntryPoint = "vsMain";
    ShaderCI.FilePath = "hlsl/terrain/extractTextures.hlsl";
    ShaderCI.Macros = {};
    ShaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
    ShaderCI.Desc.Name = "vsMain";
    ShaderCI.pShaderSourceStreamFactory = pShaderSourceFactory;
    // BUGFIX: was `true`, but the shader uses a standalone SamplerState (gSmpLinear),
    // not the <texture>_sampler convention combined samplers require.
    ShaderCI.Desc.UseCombinedTextureSamplers = false;
    ShaderCI.CompileFlags = Diligent::SHADER_COMPILE_FLAG_NONE;
    ShaderCI.ShaderCompiler = Diligent::SHADER_COMPILER_DXC;
    m_pDevice->CreateShader(ShaderCI, &VS);

    ShaderCI.EntryPoint = "gsMain";
    ShaderCI.Desc.Name = "gsMain";
    ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_GEOMETRY;
    m_pDevice->CreateShader(ShaderCI, &GS);

    ShaderCI.EntryPoint = "psMain";
    ShaderCI.Desc.Name = "psMain";
    ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    m_pDevice->CreateShader(ShaderCI, &PS);

    // Backing buffer for gConstantBuffer. BUGFIX: this buffer did not exist at all -
    // the geometry and pixel shaders read a completely unbound constant buffer.
    {
        Diligent::BufferDesc CBDesc;
        CBDesc.Name = "extractTextures constants";
        CBDesc.Size = sizeof(ExtractTexturesConstants);
        CBDesc.Usage = Diligent::USAGE_DYNAMIC;
        CBDesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
        CBDesc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
        constantsCB.Release();
        m_pDevice->CreateBuffer(CBDesc, nullptr, &constantsCB);
    }

    // 1x1 fallback textures (white / flat normal). Bound whenever an input slot is
    // empty so the pixel shader never samples an unbound descriptor (undefined on
    // the GPU; a prime suspect for the machine-dependent black screen / TDR).
    {
        auto make1x1 = [&](const char* _name, Diligent::Uint32 _rgba,
                           Diligent::RefCntAutoPtr<Diligent::ITexture>& _tex) {
            Diligent::TextureDesc desc;
            desc.Name = _name;
            desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
            desc.Width = 1;
            desc.Height = 1;
            desc.MipLevels = 1;
            desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
            desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
            Diligent::TextureSubResData level0;
            level0.pData = &_rgba;
            level0.Stride = 4;
            Diligent::TextureData data(&level0, 1);
            _tex.Release();
            m_pDevice->CreateTexture(desc, &data, &_tex);
        };
        make1x1("fallback white", 0xFFFFFFFFu, tex_fallback_white);
        make1x1("fallback flat normal", 0xFFFF8080u, tex_fallback_normal);  // RGBA (128,128,255,255)
        pFallbackWhiteSRV = tex_fallback_white->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
        pFallbackNormalSRV = tex_fallback_normal->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
    }

    Diligent::GraphicsPipelineStateCreateInfo PSOCreateInfo;

    // BUGFIX: sampler was named "Tex" (matches nothing in the shader) so gSmpLinear
    // stayed unbound -> sampling through a null sampler descriptor. Name it after
    // the shader's SamplerState. Linear filtering matches the original's intent
    // (the old Falcor path bound sampler_Ribbons to "gSmpLinear").
    Diligent::ImmutableSamplerDesc samDesc;
    samDesc.ShaderStages = Diligent::SHADER_TYPE_PIXEL;
    samDesc.Desc.MagFilter = Diligent::FILTER_TYPE_LINEAR;
    samDesc.Desc.MinFilter = Diligent::FILTER_TYPE_LINEAR;
    samDesc.Desc.MipFilter = Diligent::FILTER_TYPE_LINEAR;
    samDesc.Desc.AddressU = Diligent::TEXTURE_ADDRESS_WRAP;
    samDesc.Desc.AddressV = Diligent::TEXTURE_ADDRESS_WRAP;
    samDesc.Desc.AddressW = Diligent::TEXTURE_ADDRESS_WRAP;
    samDesc.Desc.MipLODBias = 0.0f;
    samDesc.Desc.ComparisonFunc = Diligent::COMPARISON_FUNC_NEVER;
    samDesc.SamplerOrTextureName = "gSmpLinear";

    PSOCreateInfo.PSODesc.ResourceLayout.ImmutableSamplers = &samDesc;
    PSOCreateInfo.PSODesc.ResourceLayout.NumImmutableSamplers = 1;

    PSOCreateInfo.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_POINT_LIST;
    PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
    PSOCreateInfo.GraphicsPipeline.RasterizerDesc.DepthClipEnable = false;

    PSOCreateInfo.GraphicsPipeline.NumRenderTargets = 5;
    for (int i = 0; i < 5; i++) {
        PSOCreateInfo.GraphicsPipeline.RTVFormats[i] = Diligent::TEX_FORMAT_RGBA8_UNORM;
    }
    PSOCreateInfo.GraphicsPipeline.DSVFormat = Diligent::TEX_FORMAT_UNKNOWN;
    // BUGFIX: DepthEnable defaults to true; with DSVFormat UNKNOWN and no DSV bound
    // that is an invalid pipeline (D3D12 rejects it, others get undefined behaviour).
    PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
    PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;

    PSOCreateInfo.pVS = VS;
    PSOCreateInfo.pGS = GS;
    PSOCreateInfo.pPS = PS;

    // BUGFIX: "gdisplacement" removed - it does not exist in extractTextures.hlsl
    // (the shader consumes 4 inputs; the tool's 5th slot is not part of the bake).
    // DYNAMIC instead of MUTABLE: these are re-Set() on every bake, which MUTABLE
    // variables do not allow (only once per SRB).
    Diligent::ShaderResourceVariableDesc Vars[] = {
        {Diligent::SHADER_TYPE_PIXEL, "galbedo", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {Diligent::SHADER_TYPE_PIXEL, "galpha", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {Diligent::SHADER_TYPE_PIXEL, "gnormal", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {Diligent::SHADER_TYPE_PIXEL, "gtranslucency", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}};

    PSOCreateInfo.PSODesc.ResourceLayout.NumVariables = _countof(Vars);
    PSOCreateInfo.PSODesc.ResourceLayout.Variables = Vars;

    PSO.Release();
    SRB.Release();
    m_pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &PSO);

    // gConstantBuffer is not listed in Vars -> default variable type (STATIC), so it
    // must be bound on the PSO before the SRB is created. The GS reads A/B/D and the
    // PS reads the curve/flip values; the VS doesn't reference it (null -> skipped).
    if (auto* pVar = PSO->GetStaticVariableByName(Diligent::SHADER_TYPE_GEOMETRY, "gConstantBuffer"))
        pVar->Set(constantsCB);
    if (auto* pVar = PSO->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "gConstantBuffer"))
        pVar->Set(constantsCB);

    PSO->CreateShaderResourceBinding(&SRB, true);

    rsVARS[0] = SRB->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "galbedo");
    rsVARS[1] = SRB->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "galpha");
    rsVARS[2] = SRB->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "gnormal");
    rsVARS[3] = SRB->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "gtranslucency");
}

void textureTool::exportNow() {
    save();

    for (int i = 0; i < textures.size(); i++) {
        renderToTexture(i);

        m_pImmediateContext->Flush();  // Flush the context to ensure the GPU finishes the copy operation
        m_pImmediateContext->WaitForIdle();

        std::string baseName = ew_paths.get_pathNoExt(path);
        std::string filename = ew_paths.get_full(baseName + "_" + std::to_string(i) + "_albedo.png");
        saveTexture(m_pDevice, m_pImmediateContext, FBO.pTexture[0], filename, true, Diligent::IMAGE_FILE_FORMAT_PNG);

        if (tex_input[2]) {
            std::string filename = ew_paths.get_full(baseName + "_" + std::to_string(i) + "_normal.png");
            saveTexture(m_pDevice, m_pImmediateContext, FBO.pTexture[1], filename, false,
                        Diligent::IMAGE_FILE_FORMAT_PNG);
        }

        if (tex_input[3]) {
            std::string filename = ew_paths.get_full(baseName + "_" + std::to_string(i) + "_translucency.png");
            saveTexture(m_pDevice, m_pImmediateContext, FBO.pTexture[2], filename, false,
                        Diligent::IMAGE_FILE_FORMAT_PNG);
        }
    }
}

void textureTool::renderToTexture(int _slot) {
    // BUGFIX: was called with out-of-range indices (see load()); guard everything.
    if (_slot < 0 || _slot >= (int)textures.size() || !PSO || !constantsCB) return;

    auto& T = textures[_slot];
    int w = (int)(T.texWidth * 4 * pow(2, T.numMips));
    int h = (int)(T.texHeight * 4 * pow(2, T.numMips));

    FBO.setup(int2(w, h), 5, m_pDevice, m_pImmediateContext);
    if (!FBO.pRTV[0]) return;  // render target creation failed

    // The GUI stores start/stop/bezier/width in image PIXEL coordinates, but the
    // shader works in UV space -> convert using the albedo input's dimensions.
    // (width is divided by X only - same approximation for non-square textures
    // as the original tool.)
    float2 imgSize(1.f, 1.f);
    if (tex_input[tex_albedo]) {
        const auto& desc = tex_input[tex_albedo]->GetDesc();
        imgSize = float2((float)desc.Width, (float)desc.Height);
    }
    float2 uvStart = T.start / imgSize;
    float2 uvStop = T.stop / imgSize;
    float2 uvBezier = T.bezier / imgSize;
    float uvWidth = T.width / imgSize.x;

    float2 seg = uvStop - uvStart;
    float segLen = glm::length(seg);
    if (segLen < 1e-6f) return;  // degenerate extents - normalize() below would produce NaNs

    // BUGFIX: fill gConstantBuffer (previously never uploaded at all). A/B/C/D are
    // the quad corners, computed on the CPU exactly like the original Falcor tool.
    {
        float2 dir = seg / segLen;
        float2 norm = float2(dir.y, -dir.x);

        Diligent::MapHelper<ExtractTexturesConstants> CB(m_pImmediateContext, constantsCB, Diligent::MAP_WRITE,
                                                         Diligent::MAP_FLAG_DISCARD);
        CB->A = uvStart + norm * uvWidth;
        CB->B = uvStart - norm * uvWidth;
        CB->C = uvStop - norm * uvWidth;
        CB->D = uvStop + norm * uvWidth;
        CB->start = uvStart;
        CB->stop = uvStop;
        CB->bezier = uvBezier;
        CB->width = uvWidth;
        CB->padd = 0.f;
        CB->flipRed = flipRed ? 1 : 0;
        CB->flipGreen = flipGreen ? 1 : 0;
        CB->nStrength = normalScale;
        CB->toSRGB = 0;  // original only sets this for the final export, not the preview

        CB->albedoScale[0].rgb = material._constData.albedoScale[0];
        CB->albedoScale[1].rgb = material._constData.albedoScale[1];

        CB->roughness[0] = material._constData.roughness[0];
        CB->roughness[1] = material._constData.roughness[1];
    }

    // BUGFIX: command order. Previous order was Commit -> SetRenderTargets -> SetPSO,
    // but SetPipelineState invalidates committed resources, so the draw executed with
    // NOTHING bound (garbage descriptors on the GPU in release builds - the prime
    // black-screen candidate). Required order: targets -> PSO -> commit -> draw.
    m_pImmediateContext->SetRenderTargets(5, FBO.pRTV, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    float ClearColor[] = {0.0f, 1.0f, 0.0f, 1.0f};
    for (int i = 0; i < 5; i++) {
        m_pImmediateContext->ClearRenderTarget(FBO.pRTV[i], ClearColor,
                                               Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    m_pImmediateContext->SetPipelineState(PSO);

    // Only 4 shader inputs (the displacement slot is not part of the bake). Missing
    // inputs get a 1x1 fallback so no descriptor is ever left unbound.
    for (int i = 0; i < 4; i++) {
        Diligent::ITextureView* fallback = (i == tex_normal) ? pFallbackNormalSRV : pFallbackWhiteSRV;
        if (rsVARS[i]) rsVARS[i]->Set(pSRV[i] ? pSRV[i] : fallback);
    }
    m_pImmediateContext->CommitShaderResources(SRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    Diligent::DrawAttribs DrawAttrs(1, Diligent::DRAW_FLAG_VERIFY_ALL);
    m_pImmediateContext->Draw(DrawAttrs);

    // unbind
    m_pImmediateContext->Flush();
    m_pImmediateContext->SetRenderTargets(0, nullptr, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);

    
    // 2. Define the transition barrier
    Diligent::StateTransitionDesc Barrier;
    Barrier.pResource = pSRV[0]; 
    Barrier.OldState = Diligent::RESOURCE_STATE_RENDER_TARGET;
    Barrier.NewState = Diligent::RESOURCE_STATE_SHADER_RESOURCE;
    //Barrier.TransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;

    // 3. Execute the transition
    m_pImmediateContext->TransitionResourceStates(1, &Barrier);
    
}

void textureTool::reloadTextures() {
    for (int i = 0; i < 5; i++) {
        tex_input[i].Release();
        pSRV[i] = nullptr;  // BUGFIX: left dangling when the file below doesn't exist
        std::string fullPath = ew_paths.get_full(texturePaths[i]);
        if (texturePaths[i].length() > 0 && std::filesystem::exists(fullPath)) {
            Diligent::TextureLoadInfo LoadInfo{texNames[i]};
            LoadInfo.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
            CreateTextureFromFile(fullPath.c_str(), LoadInfo, m_pDevice, &tex_input[i]);
            if (tex_input[i])  // BUGFIX: load can fail (bad format etc.) -> was a null deref
                pSRV[i] = tex_input[i]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
        }
    }
}

void textureTool::load(const char* name) {
    std::ifstream is(name);
    cereal::JSONInputArchive archive(is);
    archive(*this);
    changed = false;
    reloadTextures();
    // BUGFIX: was textures.size() - one past the end; onRender() then indexed
    // textures[] out of bounds every frame.
    currentTexture = (int)textures.size() - 1;
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

            load(ew_paths.get_full(FileName).c_str());
        }
        changed_for_save = false;
    }
    
}

void textureTool::save() {
    std::ofstream os(ew_paths.get_full(path));
    cereal::JSONOutputArchive archive(os);
    archive(*this);
    changed = false;
    changed_for_save = false;
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
        changed_for_save = false;
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
            pSRV[_slot] = nullptr;
            CreateTextureFromFile(ew_paths.get_full(FileName).c_str(), LoadInfo, m_pDevice, &tex_input[_slot]);
            if (tex_input[_slot])  // BUGFIX: load can fail -> was a null deref
                pSRV[_slot] = tex_input[_slot]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
        } else {
            // warn no tin poath
        }
    }
}

void textureTool::clear_texture(uint _slot) {
    texturePaths[_slot].clear();
    tex_input[_slot].Release();
    pSRV[_slot] = nullptr;  // BUGFIX: was left dangling after the texture died
}

void textureTool::onGuiMenubar() {
    if (ImGui::BeginMenu("file")) {
        if (ImGui::MenuItem("load", "Ctrl+O")) {
            load();
        }
        if (ImGui::MenuItem("save", "")) {
            save();
        }
        if (ImGui::MenuItem("save-as", "Ctrl+S")) {
            save_as();
        }

        ImGui::NewLine();
        if (ImGui::MenuItem("export", "Ctrl+S")) {
            exportNow();
        }
        
        
        
        ImGui::EndMenu();
    }

    
    ImGui::SameLine(0, 100);
    //ImGui::SetCursorPosX(ImGui::GetIO().DisplaySize.x - 300);
    ImGui::SetCursorPosY(header_height - h_H1 - 7);
    _Gui.text(font_H1, name.c_str());
    _Gui.tooltip(font_normal, path.c_str());
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
                // keep currentTexture a valid index (or -1 when empty)
                if (currentTexture >= (int)textures.size()) currentTexture = (int)textures.size() - 1;
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
            ImVec2 size = {(float)tex_input[mainViewType]->GetDesc().GetWidth(),
                           (float)tex_input[mainViewType]->GetDesc().GetHeight()};
            ImGui::ImageWithBg(reinterpret_cast<ImTextureID>(pSRV[mainViewType]), size * zoom, ImVec2(0, 0),
                               ImVec2(1, 1), ImVec4(0.0f, 0.0f, 0.0f, 1.0f));

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
                            case 2: {
                                float2 tangent = glm::normalize(T.stop - T.start);
                                float2 right = float2(-tangent.y, tangent.x);
                                float2 diff = imagePixelPos - T.start;
                                // BUGFIX: clamp to >= 1 pixel. A (near-)collinear 3rd click gave
                                // width ~0 -> aspect exploding -> a multi-gigapixel render target
                                // request below. Also guards the division right after.
                                T.width = __max(abs(glm::dot(diff, right)), 1.f);

                                // auto setup the aspect
                                // can be done a lot better by alowing 2:3 etc
                                float aspect = glm::length(T.stop - T.start) / (T.width * 2.f);
                                // BUGFIX: clamp to the GUI's own [1,16] range (mips<=7 -> max 8k)
                                if (aspect > 1.f) {
                                    T.texWidth = 1;
                                    T.texHeight = Diligent::clamp((int)(aspect + 0.5f), 1, 16);
                                } else {
                                    T.texWidth = Diligent::clamp((int)(1.f / aspect + 0.5f), 1, 16);
                                    T.texHeight = 1;
                                }
                                clickCount++;
                                clickMode = false;
                                changed = true;
                            } break;
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
                                    changed = true;
                                    break;
                                case 2:
                                    T.stop = imagePixelPos;
                                    changed = true;
                                    break;
                                case 3:
                                    T.bezier = imagePixelPos;
                                    changed = true;
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
                    currentTexture = (int)textures.size();
                    textures.emplace_back();
                    clickMode = true;
                    clickCount = 0;
                }
                if (ImGui::MenuItem("delete")) {
                    // BUGFIX: bounds check - currentTexture could be -1 or == size()
                    if (currentTexture >= 0 && currentTexture < (int)textures.size()) {
                        textures.erase(textures.begin() + currentTexture);
                        if (currentTexture >= (int)textures.size()) currentTexture = (int)textures.size() - 1;
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
            ImGui::ImageWithBg(reinterpret_cast<ImTextureID>(pSRV[i]), ImVec2(128, 128), ImVec2(0, 0), ImVec2(1, 1),
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



    // ImGui::BeginChildFrame(1002, ImVec2(40 * 8, 40 * 8), 0);
    {
        float y = ImGui::GetCursorPosY();
        float font_height = ImGui::GetFontSize();
        if (currentTexture >= 0 && currentTexture < textures.size()) {

            int scale = 4 * (int)pow(2, textures[currentTexture].numMips);
            int W = (int)textures[currentTexture].texWidth * scale;
            int H = (int)textures[currentTexture].texHeight * scale;
            
            _Gui.text(font_H2, "(%d, %d)", W, H);
            ImGui::NewLine();
            changed |= _Gui.dragInt("width", &textures[currentTexture].texWidth, 0.1f, 1, 16);
            changed |= _Gui.dragInt("height", &textures[currentTexture].texHeight, 0.1f, 1, 16);
            changed |= _Gui.dragInt("mips", &textures[currentTexture].numMips, 0.1f, 0, 7);


            ImGui::SetCursorPosY(y);
            ImGui::SetCursorPosX(250);


            ImGui::BeginTable("GridSelectableTable", 8, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_Borders,
                              ImVec2(font_height * 8, font_height * 8));
            {
                for (int row = 1; row <= 8; row++) {
                    ImGui::TableNextRow(ImGuiTableRowFlags_None);

                    for (int col = 1; col <= 8; col++) {
                        ImGui::TableNextColumn();
                        ImGui::SetNextItemWidth(-FLT_MIN);

                        bool is_highlighted =
                            (col <= textures[currentTexture].texWidth && row <= textures[currentTexture].texHeight);

                        ImGui::PushStyleColor(ImGuiCol_Header,
                                              is_highlighted ? IM_COL32(0, 119, 182, 200) : IM_COL32(0, 0, 0, 0));

                        ImGui::PushID(199990 + row * 345 + col);
                        ImGui::Selectable("##cell", is_highlighted, ImGuiSelectableFlags_None);
                        ImGui::PopID();
                        ImGui::PopStyleColor();

                        // Check if this specific cell is active/hovered to set the drag endpoint
                        if (ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                            textures[currentTexture].texWidth = col;
                            textures[currentTexture].texHeight = row;
                            changed = true;
                        }
                    }
                }
            }
            ImGui::EndTable();

            
            
            
        }
    }
    // ImGui::EndChildFrame();

    ImVec2 space = ImGui::GetContentRegionAvail();

    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
    // ImGui::BeginChildFrame(1001, ImVec2(space.x, ImGui::GetFontSize() * 16), 0);
    {
        if (currentTexture >= 0 && currentTexture < textures.size()) {
            ImGui::SetNextItemWidth(8 * 20);

            //??? Do vidually instead with a table up to maybe 8x8? Just click in a celland all left top will light
        }
    }
    // ImGui::EndChildFrame();

    ImGui::Separator();

    // FIXME outputZoom
    // BUGFIX: FBO.pSRV[0] was an uninitialized pointer until the first bake -
    // this handed garbage to the ImGui renderer on every frame before that.

    static float ZOOM = 1.f;
    _Gui.dragFloat("zoom", &ZOOM, 0.02f, 1.f, 4.f);

    if (FBO.pSRV[0]) {
        ImGui::ImageWithBg(reinterpret_cast<ImTextureID>(FBO.pSRV[0]),
                           ImVec2(FBO.getSize().x * ZOOM, FBO.getSize().y * ZOOM), ImVec2(0, 0), ImVec2(1, 1),
                           ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    }
    ImGui::SameLine(0, 10);
    if (FBO.pSRV[1]) {
        ImGui::ImageWithBg(reinterpret_cast<ImTextureID>(FBO.pSRV[1]),
                           ImVec2(FBO.getSize().x * ZOOM, FBO.getSize().y * ZOOM), ImVec2(0, 0), ImVec2(1, 1),
                           ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    }

    if (FBO.pSRV[2]) {
        ImGui::ImageWithBg(reinterpret_cast<ImTextureID>(FBO.pSRV[2]),
                           ImVec2(FBO.getSize().x * ZOOM, FBO.getSize().y * ZOOM), ImVec2(0, 0), ImVec2(1, 1),
                           ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    }
    ImGui::SameLine(0, 10);
    if (FBO.pSRV[4]) {
        ImGui::ImageWithBg(reinterpret_cast<ImTextureID>(FBO.pSRV[4]),
                           ImVec2(FBO.getSize().x * ZOOM, FBO.getSize().y * ZOOM), ImVec2(0, 0), ImVec2(1, 1),
                           ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    }

    ImGui::Separator();
}
/*
void textureTool::renderGui_main() {
    ImGuiStyle& style = ImGui::GetStyle();

    
    //ImGuiIO& io = ImGui::GetIO();

    // Set a permanent, writeable path (e.g., in your executable directory or user app data)
    //std::string ini = io.IniFilename; 
    //if (ImGui::BeginTable("SplitterTable", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings)) {
    
    

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
*/
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
/*
void textureTool::renderGui_right() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_FrameBg] = changed_for_save ? ImVec4(0.3f, 0.2f, 0.0f, 1.0f) : ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
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
*/
void textureTool::onRender() {
    // BUGFIX: upper bound was missing; load() used to leave currentTexture one past
    // the end (also fixed), which made this bake from a non-existent element.
    if (changed && currentTexture >= 0 && currentTexture < (int)textures.size()) {
        renderToTexture(currentTexture);
        changed = false;
    }
}

void textureTool::renderGui() {
    if (requestDelete >= 0) {
        clear_texture(requestDelete);
        requestDelete = -1;
    }

    ImGuiStyle& style = ImGui::GetStyle();


    ImGui::PushFont(font_normal);
    {
        float menu_bar_height = ImGui::GetFrameHeight();
        ImGui::SetNextWindowPos(ImVec2(0, header_height));
        ImGui::SetNextWindowSize(ImGui::GetMainViewport()->Size - ImVec2(0, header_height));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);  // Dark gray background
        ImGui::Begin("##main", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
        {
            if (ImGui::IsKeyPressed(ImGuiKey::ImGuiMod_Ctrl)) {
                if (ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_S)) {
                    save_as();
                }
                //if (ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_O)) {
                //    load();
               // }
            }

            if (ImGui::BeginTable("SplitterTable", 3, ImGuiTableFlags_Resizable)) {
                // Track sizing context across frames
                ImGui::TableNextRow();

                // --- LEFT PANE ---
                ImGui::TableNextColumn();
                ImGui::BeginChild("LeftPaneChild", ImVec2(0, 0), ImGuiChildFlags_None);
                renderGui_A();
                ImGui::EndChild();

                // --- RIGHT PANE ---
                ImGui::TableNextColumn();
                ImGui::BeginChild("MiddlePaneChild", ImVec2(0, 0), ImGuiChildFlags_None);
                renderGui_B();
                ImGui::EndChild();

                ImGui::TableNextColumn();
                ImGui::BeginChild("RightPaneChild", ImVec2(0, 0), ImGuiChildFlags_None);
                renderGui_C();
                ImGui::EndChild();

                ImGui::EndTable();
            }
        }
        ImGui::End();
        /*
        float x = ImGui::GetMainViewport()->Size.x - 200;
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->Pos + ImVec2(x, 0));
        ImGui::SetNextWindowSize(ImVec2(200, ImGui::GetMainViewport()->Size.y));
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);  // Dark gray background
        ImGui::Begin("##right", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
        {
            renderGui_right();
        }
        ImGui::End();
        */
        ImGui::PopStyleVar();
    }
    ImGui::PopFont();
    // DockSpaceOverViewport

    changed_for_save |= changed;
}

#include <shlobj.h>
std::filesystem::path resolveRootPath() {
    // Get %SavedGames% path from Windows
    PWSTR saved_games_path = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(FOLDERID_SavedGames, 0, NULL, &saved_games_path);
    if (FAILED(hr) || saved_games_path == nullptr) {
        throw std::runtime_error("SHGetKnownFolderPath(FOLDERID_SavedGames) failed");
    }

    std::filesystem::path root = std::filesystem::path(saved_games_path) / "earthworksTextureTool";
    CoTaskMemFree(saved_games_path);
    return root;
}

TextureSplitTool::TextureSplitTool()
    : Diligent::EarthworksFXApplicationBase("TextureSplitTool", "texture-split-tool", overthinking::Env::Stage::Dev) {
    savedGamesFile = resolveRootPath() / "textureTool.info";

    std::filesystem::path local = Diligent::FileSystem::GetLocalAppDataDirectory();
    savedGamesFile = local / "textureTool.info";

    if (std::filesystem::is_regular_file(savedGamesFile)) {
        std::ifstream is(savedGamesFile);
        cereal::JSONInputArchive archive(is);
        archive(info);
        earthworksPaths::root = info.dataRootFolder;
    }
}

TextureSplitTool::~TextureSplitTool() {
    std::ofstream os(savedGamesFile);
    cereal::JSONOutputArchive archive(os);
    archive(info);
}

void TextureSplitTool::Initialize() {
    //const char* gameroot = std::getenv("ACSMP_GAMEROOT");
    //if (gameroot) {
    //    earthworksPaths::root = gameroot;  //        +;
    //    earthworksPaths::root += "\\textureTool\\";
    //}
}

void TextureSplitTool::OnConfigureSettings(Diligent::EarthworksFXAppSettings& settings) {
    // No terrain scene: build the environment + shaders only and render manually.
    settings.CreateScene = false;
    settings.VSync = true;
    // FIXME needs fulklscreen flag as well
}

void TextureSplitTool::OnModifyEngineInitInfo(const ModifyEngineInitInfoAttribs& attribs) {}

void TextureSplitTool::OnGraphicsReady() {
    ImGuiIO& io = ImGui::GetIO();
    
    font_small = io.Fonts->AddFontFromFileTTF("fonts/Plus_Jakarta_Sans/PlusJakartaSans-VariableFont_wght.ttf", 16.f);
    font_normal = io.Fonts->AddFontFromFileTTF("fonts/Plus_Jakarta_Sans/PlusJakartaSans-VariableFont_wght.ttf", 20.f);
    font_H1 = io.Fonts->AddFontFromFileTTF("fonts/Plus_Jakarta_Sans/PlusJakartaSans-VariableFont_wght.ttf", 26.f);
    font_H2 = io.Fonts->AddFontFromFileTTF("fonts/Plus_Jakarta_Sans/PlusJakartaSans-VariableFont_wght.ttf", 32.f);
    font_H3 = io.Fonts->AddFontFromFileTTF("fonts/Plus_Jakarta_Sans/PlusJakartaSans-VariableFont_wght.ttf", 48.f);

    ImGui::PushFont(font_small);
    h_small = ImGui::CalcTextSize("Small Text").y;
    ImGui::PopFont();

    ImGui::PushFont(font_normal);
    h_normal = ImGui::CalcTextSize("Small Text").y;
    ImGui::PopFont();

    ImGui::PushFont(font_H1);
    h_H1 = ImGui::CalcTextSize("Small Text").y;
    ImGui::PopFont();

    ImGui::PushFont(font_H2);
    h_H2 = ImGui::CalcTextSize("Small Text").y;
    ImGui::PopFont();

    ImGui::PushFont(font_H3);
    h_H3 = ImGui::CalcTextSize("Small Text").y;
    ImGui::PopFont();

    texture_tool.m_pDevice = m_pDevice;
    texture_tool.m_pImmediateContext = m_pImmediateContext;
    texture_tool.m_pSwapChain = m_pSwapChain;
    texture_tool.init();

    // TEMP to reproduce the crash
    //texture_tool.name = "TEST.textureTool";
    //texture_tool.path = "TEST.textureTool";
    //texture_tool.load((earthworksPaths::root + texture_tool.path).c_str());
}

void TextureSplitTool::OnRender() {}

void TextureSplitTool::OnUpdate(double current_time, double elapsed_time, bool do_update_ui) {
    //???EarthworksFXApplicationBase::Update(current_time, elapsed_time, do_update_ui);

    texture_tool.onRender();

    Diligent::ITextureView* pRTV = m_pSwapChain->GetCurrentBackBufferRTV();
    const float Zero[] = {0.0f, 0.0f, 0.0f, 1.0f};  // Clear the default render target
    m_pImmediateContext->SetRenderTargets(1, &pRTV, m_pSwapChain->GetDepthBufferDSV(),
                                          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_pImmediateContext->ClearRenderTarget(pRTV, Zero, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void TextureSplitTool::OnWindowResized(Diligent::Uint32 width, Diligent::Uint32 height) {}

void TextureSplitTool::onGuiMenubar() {
    auto& style = ImGui::GetStyle();

    style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.02f, 0.04f, 0.01f, 1.0f);
    if (texture_tool.changed_for_save) style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.3f, 0.2f, 0.01f, 1.0f);

    ImGui::PushFont(font_H2);
    if (ImGui::BeginMainMenuBar()) {

        header_height = ImGui::GetWindowHeight();
        
        //ImGui::SetCursorPos(ImVec2(10, 15));
        //ImGui::SetCursorPosY(header_height - h_small - 7);        
        //_Gui.text(font_small, "earthworks");
        ImGui::SetCursorPosY(header_height - h_normal - 7);
        _Gui.text(font_normal, "Texture Tool");

        ImGui::SameLine(0, 100);
        ImGui::PushFont(font_normal);
        {
            texture_tool.onGuiMenubar();

            if (ImGui::BeginMenu("file")) {
                ImGui::Separator();

                if (ImGui::MenuItem("change root folder")) {
                    std::string FolderName = Diligent::FileSystem::OpenFolderDialog("data root folder");
                    if (!FolderName.empty()) {
                        info.dataRootFolder = FolderName;
                        earthworksPaths::root = info.dataRootFolder;
                    }
                }

                ImGui::EndMenu();
            }
        }
        ImGui::PopFont();

        //ImGui::SameLine(0, 100);
        ImGui::SetCursorPosX(ImGui::GetIO().DisplaySize.x - 500);
        ImGui::SetCursorPosY(header_height - h_normal - 7);
        //ImGui::SetCursorPos(ImVec2(600, 15));
        _Gui.text(font_normal, info.dataRootFolder.c_str());
       

        
        ImGui::EndMainMenuBar();
    }
    ImGui::PopFont();
}

void TextureSplitTool::UpdateUI() {
    EarthworksFXApplicationBase::UpdateUI();  // I dont want the common UI

    texture_tool.m_pDevice = m_pDevice;
    texture_tool.m_pImmediateContext = m_pImmediateContext;
    texture_tool.m_pSwapChain = m_pSwapChain;

    onGuiMenubar();

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
