/***************************************************************************
 # Copyright (c) 2015-22, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
#include "Earthworks_4.h"
#include "EarthworksDebug.h"
//#include "Utils/UI/TextRenderer.h"
#include "imgui.h"
//#include "Core/Platform/MonitorInfo.h"
#include <filesystem>
#include <iostream>

 //#pragma optimize("", off)

FILE* logFile;

#define TOOLTIP(x)  if (ImGui::IsItemHovered()) {ImGui::SetTooltip(x);}

void Earthworks_4::onGuiMenubar(Gui* _gui)
{
    auto& style = ImGui::GetStyle();
    switch (terrain.terrainMode)
    {
    case _terrainMode::vegetation:      style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.02f, 0.04f, 0.01f, 1.0f);   break;
    case _terrainMode::ecotope:         style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.01f, 0.06f, 0.06f, 1.0f);   break;
    case _terrainMode::terrafector:     style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.24f, 0.14f, 0.0f, 1.0f);   break;
    case _terrainMode::roads:           style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.005f, 0.005f, 0.005f, 1.0f);   break;
    case _terrainMode::glider:          style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.04f, 0.04f, 0.04f, 1.0f);   break;
    case _terrainMode::terrainBuilder:  style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.01f, 0.01f, 0.01f, 1.0f);   break;
    case _terrainMode::textureTool:     style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.01f, 0.04f, 0.04f, 1.0f);   break;
    }

    if (ImGui::BeginMainMenuBar())
    {
        ImGui::PushFont(_gui->getFont("header2"));
        {
            ImGui::SetCursorPos(ImVec2(10, -3));
            ImGui::Text("Earthworks 4");

            ImGui::SetCursorPos(ImVec2(screenSize.x - 150, -3));
            ImGui::Text("%3.1f fps", 1000.0 / gpFramework->getFrameRate().getAverageFrameTime());

            ImGui::SetCursorPos(ImVec2(screenSize.x - 25, -3));
            if (ImGui::Selectable("X")) { gpFramework->getWindow()->shutdown(); }
        }
        ImGui::PopFont();

        terrain.onGuiMenubar(_gui);

        if (ImGui::BeginMenu("settings"))
        {
            ImGui::Text("camera");
            float focal = camera->getFocalLength();
            float nearP = camera->getNearPlane();
            float farP = camera->getFarPlane();
            if (ImGui::SliderFloat("focal length", &focal, 5, 100, "%.1fmm")) { camera->setFocalLength(focal); }
            if (ImGui::DragFloat("near", &nearP, 0.01f, 0.01f, 5.f, "%.1fm")) { camera->setNearPlane(nearP); }
            if (ImGui::DragFloat("far", &farP, 10, 1000, 100000, "%.1fm")) { camera->setFarPlane(farP); }
            if (ImGui::MenuItem("reset")) {
                camera->setDepthRange(0.3f, 20000.0f);
                //camera->setAspectRatio(1920.0f / 1080.0f);
                camera->setFocalLength(15.0f);
                camera->setPosition(float3(0, 500, 0));
            }
            ImGui::Text("(%2.0f, %2.0f, %2.0f)m", camera->getPosition().x, camera->getPosition().y, camera->getPosition().z );

            ImGui::Separator();
            ImGui::Text("Time of day");
            ImGui::SetNextItemWidth(300);
            ImGui::DragFloat("angle", &terrain.shadowEdges.sunAngle, 0.01f, 0, 3.14f, "%1.2f");
            if (ImGui::Button("change"))
            {
                terrain.shadowEdges.requestNewShadow = true;
            }

            ImGui::SetNextItemWidth(300);
            ImGui::DragFloat("haze turbidity", &atmosphere.params.haze_Turbidity, 0.01f, 1.f, 15.f, "%1.2f");

            ImGui::SetNextItemWidth(300);
            ImGui::DragFloat("fog turbidity", &atmosphere.params.fog_Turbidity, 0.01f, 1.f, 125.f, "%1.2f");

            ImGui::SetNextItemWidth(300);
            ImGui::DragFloat("fog base", &atmosphere.params.fog_BaseAltitudeKm, 0.001f, 0.f, 1.f, "%1.2fkm");

            ImGui::SetNextItemWidth(300);
            ImGui::DragFloat("fog height", &atmosphere.params.fog_AltitudeKm, 0.001f, 0.05f, 1.f, "%1.2f");

            ImGui::Separator();
            ImGui::Checkbox("vsync", &refresh.vsync);
            TOOLTIP("Not currently working on latest NVidia drivers, something changed.");
            ImGui::Checkbox("only on change", &refresh.minimal);
            TOOLTIP("ultimate low power - only refresh on changes\n'V' to toggle");

            ImGui::Separator();
            ImGui::Text("editor layout");
            ImGui::DragInt("left", &layout.left_width, 1, 100, 1000);
            ImGui::DragInt("right", &layout.left_width, 1, 100, 1000);
            ImGui::DragInt("bottom", &layout.left_width, 1, 100, 1000);


            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("help"))
        {
            ImGui::Checkbox("about Earthworks 4", &showAbout);
            ImGui::EndMenu();
        }

        //ImGui::SameLine(0, 100);
        //ImGui::Text(terrain.lastfile.terrain.c_str());

        layout.header_height = (int)ImGui::GetWindowHeight();

        ImGui::EndMainMenuBar();
    }

}



void Earthworks_4::initGui(Gui* _gui)
{
    LOG_LINE(1, "initGui");
    _gui->addFont("H1", "Framework/Fonts/Sienthas.otf", screenSize.y / 14);
    _gui->addFont("H2", "Framework/Fonts/Sienthas.otf", screenSize.y / 17);

    float scale = 1.f;
    if (screenSize.y > 1600.f) scale = 0.9f;
    _gui->addFont("header0", "Framework/Fonts/Nunito-Regular.ttf", scale * screenSize.y / 25);
    _gui->addFont("header1", "Framework/Fonts/Nunito-Regular.ttf", scale * screenSize.y / 42);
    _gui->addFont("header2", "Framework/Fonts/Nunito-Bold.ttf", scale * screenSize.y / 55);
    _gui->addFont("default", "Framework/Fonts/Nunito-Regular.ttf", scale * screenSize.y / 65);
    _gui->addFont("bold", "Framework/Fonts/Nunito-Bold.ttf", scale * screenSize.y / 65);
    _gui->addFont("italic", "Framework/Fonts/Nunito-Italic.ttf", scale * screenSize.y / 65);
    _gui->addFont("small", "Framework/Fonts/Nunito-Bold.ttf", scale * screenSize.y / 100);

    guiStyle();

    ImGui::GetIO().FontDefault = _gui->getFont("default");
    //ImGui::PushFont(_gui->getFont("default"));

    firstGUI = false;
}



void Earthworks_4::onGuiRender(Gui* _gui)
{
    if (firstGUI)
    {
        initGui(_gui);
    }


    ImGuiIO& io = ImGui::GetIO();
    io.WantCaptureMouse = false;

    if (showGui)
    {
        //ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.0f));
        //Gui::Window hud(_gui, "##hud", { screenSize.x, screenSize.y }, { 0, 0 }, Gui::WindowFlags::Empty | Gui::WindowFlags::NoResize | Gui::WindowFlags::NoBringToFront);
        //{

        //    hud.windowPos(0, 150);
        //    hud.windowSize((int)screenSize.x, (int)screenSize.y - 150);
            //terrain.onGuiRenderParaglider(hud, _gui, screenSize, &atmosphere.params);
        ImGui::PushFont(_gui->getFont("default"));
        {
            onGuiMenubar(_gui);

            terrain.onGuiRender(_gui, layout.header_height, screenSize, &atmosphere.params);

            if (aboutTex && showAbout) {
                Gui::Window a(_gui, "##About", showAbout, { aboutTex->getWidth(), aboutTex->getHeight() + 20 }, { 0, 100 });
                {
                    a.windowPos(((int)screenSize.x - aboutTex->getWidth()) / 2, ((int)screenSize.y - aboutTex->getHeight()) / 2 + 32);
                    a.image("#about", aboutTex, float2(aboutTex->getWidth(), aboutTex->getHeight()));
                }
                a.release();
            }
        }
        ImGui::PopFont();
        //}
        //hud.release();
    }
    else
    {
        // ??? maybe a fullscreen hud but most likely leave that for downstream
    }
}



void Earthworks_4::onLoad(RenderContext* _renderContext)
{
    LOG_BLOCK("Earthworks_4::onLoad", 0);
    terrafectorSystem::logTimeX();
    //std::cout << "onLoad()\n";
    spdlog::info("Earthworks_4::onLoad()");

    graphicsState = GraphicsState::create();

    BlendState::Desc bsDesc;
    bsDesc.setRtBlend(0, true).setRtParams(0, BlendState::BlendOp::Add, BlendState::BlendOp::Add, BlendState::BlendFunc::SrcAlpha, BlendState::BlendFunc::OneMinusSrcAlpha, BlendState::BlendFunc::One, BlendState::BlendFunc::Zero);
    graphicsState->setBlendState(BlendState::create(bsDesc));

    DepthStencilState::Desc dsDesc;
    dsDesc.setDepthEnabled(true);
    dsDesc.setDepthWriteMask(true);
    graphicsState->setDepthStencilState(DepthStencilState::create(dsDesc));

    RasterizerState::Desc rsDesc;
    rsDesc.setCullMode(RasterizerState::CullMode::None);
    graphicsState->setRasterizerState(RasterizerState::create(rsDesc));

    camera = Camera::create();
    camera->setDepthRange(0.1f, 40000.0f);
    camera->setAspectRatio(1920.0f / 1080.0f);
    camera->setFocalLength(15.0f);
    camera->setPosition(float3(0, 900, 0));
    camera->setTarget(float3(0, 900, 100));





    terrafectorSystem::logTimeX();
    spdlog::info("atmosphere.onLoad()");
    //std::cout << "  atmosphere\n";
    {
        LOG_BLOCK("atmosphereAndFog::onLoad", 0);

        atmosphere.onLoad(_renderContext, logFile);



        terrain.plants_Root.inscatter = atmosphere.getFar().inscatter;
        terrain.plants_Root.outscatter = atmosphere.getFar().outscatter;
        terrain.plants_Root.sunlightTexture = atmosphere.sunlightTexture;

        /*
        terrain.plants_Root.vegetationShader.Vars()->setTexture("gAtmosphereInscatter", atmosphere.getFar().inscatter);
        terrain.plants_Root.vegetationShader.Vars()->setTexture("gAtmosphereOutscatter", atmosphere.getFar().outscatter);
        terrain.plants_Root.vegetationShader.Vars()->setTexture("SunInAtmosphere", atmosphere.sunlightTexture);

        terrain.plants_Root.billboardShader.Vars()->setTexture("gAtmosphereInscatter", atmosphere.getFar().inscatter);
        terrain.plants_Root.billboardShader.Vars()->setTexture("gAtmosphereOutscatter", atmosphere.getFar().outscatter);
        terrain.plants_Root.billboardShader.Vars()->setTexture("SunInAtmosphere", atmosphere.sunlightTexture);
        */
    }

    std::cout << "  terrain\n";
    terrain.onLoad(_renderContext, logFile);

    {
        terrain.terrainShader.Vars()->setTexture("gAtmosphereInscatter", atmosphere.getFar().inscatter);
        terrain.terrainShader.Vars()->setTexture("gAtmosphereOutscatter", atmosphere.getFar().outscatter);
        terrain.terrainShader.Vars()->setTexture("SunInAtmosphere", atmosphere.sunlightTexture);

        terrain.terrainSpiteShader.Vars()->setTexture("gAtmosphereInscatter", atmosphere.getFar().inscatter);
        terrain.terrainSpiteShader.Vars()->setTexture("gAtmosphereOutscatter", atmosphere.getFar().outscatter);
        terrain.terrainSpiteShader.Vars()->setTexture("SunInAtmosphere", atmosphere.sunlightTexture);

        //terrain.rappersvilleShader.Vars()->setTexture("gAtmosphereInscatter", atmosphere.getFar().inscatter);
        //terrain.rappersvilleShader.Vars()->setTexture("gAtmosphereOutscatter", atmosphere.getFar().outscatter);
        //terrain.rappersvilleShader.Vars()->setTexture("SunInAtmosphere", atmosphere.sunlightTexture);

        //terrain.gliderwingShader.Vars()->setTexture("gAtmosphereInscatter", atmosphere.getFar().inscatter);
        //terrain.gliderwingShader.Vars()->setTexture("gAtmosphereOutscatter", atmosphere.getFar().outscatter);
        //terrain.gliderwingShader.Vars()->setTexture("SunInAtmosphere", atmosphere.sunlightTexture);

        //terrain.triangleShader.Vars()->setTexture("gAtmosphereInscatter", atmosphere.getFar().inscatter);
        //terrain.triangleShader.Vars()->setTexture("gAtmosphereOutscatter", atmosphere.getFar().outscatter);
        terrain.triangleShader.Vars()->setTexture("gAtmosphereInscatter_Sky", atmosphere.getFar().inscatter_sky);
    }

    {
        /*
        LOG_BLOCK("terrain.cfdStart", 0);
        LOG_LINE(0, "just testing");
        terrafectorSystem::logTimeX();
        std::cout << "  cfd\n";
        spdlog::info("terrain.cfdStart()");
        terrain.cfdStart();
        std::thread thread_obj_cfd(&terrainManager::cfdThread, &terrain);
        thread_obj_cfd.detach();
        */
    }


    /*
    std::thread thread_obj_paraglider(&terrainManager::paragliderThread, &terrain, std::ref(glider.glider_barrier));
    thread_obj_paraglider.detach();
    std::thread thread_obj_paraglider_B(&terrainManager::paragliderThread_B, &terrain, std::ref(glider.glider_barrier));
    thread_obj_paraglider_B.detach();
    */

    //terrain.terrainShader.Vars()->setTexture("gSmokeAndDustInscatter", compressed_Albedo_Array);
    //terrain.terrainShader.Vars()->setTexture("gSmokeAndDustOutscatter", compressed_Albedo_Array);

    FILE* file = fopen("camera.bin", "rb");
    if (file) {
        CameraData data;
        fread(&data, sizeof(CameraData), 1, file);
        camera->getData() = data;
        fclose(file);
    }

    std::ifstream is("earthworks4_presets.xml");
    if (is.good()) {
        cereal::XMLInputArchive archive(is);
        serialize(archive, 100);
    }

    aboutTex = Texture::createFromFile("earthworks_4/about.dds", false, true);

    postProcess.tonemapper.load("Samples/Earthworks_4/hlsl/compute_tonemapper.hlsl", "vsMain", "psMain", Vao::Topology::TriangleList);
    // pixelShader::load always attaches an index buffer, which forces the compat's
    // DrawIndexed path - that only ever rasterized one triangle for this full-screen
    // pass (half the screen). Swap in a null-IB VAO so the tonemapper uses the same
    // non-indexed draw path the debug grid uses correctly; vsMain builds a 6-vertex quad.
    postProcess.tonemapper.State()->setVao(Vao::create(Vao::Topology::TriangleList, VertexLayout::create(), Vao::BufferVec{}, nullptr, ResourceFormat::R16Uint));
    //loadColorCube("F:/terrains/colorcubes/K_TONE Vintage_KODACHROME.cube");
    loadColorCube(terrain.settings.dirResource + "/colorcubes/ColdChrome.cube");

    BlendState::Desc blendDesc;
    blendDesc.setRtBlend(0, true);
    blendDesc.setRtParams(0, BlendState::BlendOp::Subtract, BlendState::BlendOp::Add, BlendState::BlendFunc::SrcAlpha, BlendState::BlendFunc::OneMinusSrcAlpha, BlendState::BlendFunc::SrcAlpha, BlendState::BlendFunc::OneMinusSrcAlpha);
    overlayBlendstate = BlendState::create(blendDesc);

    // --- debug orientation / movement grid ------------------------------------
    debugGridProgram = GraphicsProgram::createFromFile("Samples/Earthworks_4/hlsl/debugGrid.hlsl", "vsMain", "psMain");
    if (debugGridProgram)
    {
        debugGridState = GraphicsState::create();
        debugGridState->setProgram(debugGridProgram);

        RasterizerState::Desc gridRsDesc;
        gridRsDesc.setCullMode(RasterizerState::CullMode::None);
        RasterizerState::SharedPtr gridRs = RasterizerState::create(gridRsDesc);

        // Ground grid: no depth test, drawn on top of the tonemapped image so the
        // world layout stays readable regardless of what is in front of it.
        DepthStencilState::Desc gridDsDesc;
        gridDsDesc.setDepthEnabled(false);
        gridDsDesc.setDepthWriteMask(false);
        debugGridState->setDepthStencilState(DepthStencilState::create(gridDsDesc));
        debugGridState->setRasterizerState(gridRs);

        // Globe: depth-tested (LessEqual) but does NOT write depth, so terrain (which
        // wrote depth to the HDR FBO) occludes it and its silhouette becomes visible.
        debugGlobeState = GraphicsState::create();
        debugGlobeState->setProgram(debugGridProgram);
        DepthStencilState::Desc globeDsDesc;
        globeDsDesc.setDepthEnabled(true);
        globeDsDesc.setDepthWriteMask(false);
        globeDsDesc.setDepthFunc(DepthStencilState::Func::LessEqual);
        debugGlobeState->setDepthStencilState(DepthStencilState::create(globeDsDesc));
        debugGlobeState->setRasterizerState(gridRs);

        debugGridVars = GraphicsVars::create(debugGridProgram->getActiveVersion()->getReflector());

        // Line list with no index buffer -> the compat layer takes the non-indexed
        // Draw() path and the vertex shader builds the geometry from SV_VertexID.
        VertexLayout::SharedPtr gridLayout = VertexLayout::create();
        Vao::BufferVec          gridEmptyVbo;
        debugGridVao = Vao::create(Vao::Topology::LineList, gridLayout, gridEmptyVbo, nullptr, ResourceFormat::R16Uint);
        debugGridState->setVao(debugGridVao);
        debugGlobeState->setVao(debugGridVao);
    }



    {
        // BAD - for STEG
        //terrain.shadowEdges.load(terrain.settings.dirRoot + "/gis/_export/root4096.bil", -global_sun_direction.y);
        terrain.shadowEdges.load(terrain.settings.dirRoot + "/elevation/root4096.bil", -global_sun_direction.y);
        terrain.shadowEdges.sunAngle = 0.95605f;
        terrain.shadowEdges.dAngle = 0.0001f;
        terrain.shadowEdges.requestNewShadow = true;

        terrain.terrainShadowTexture = Texture::create2D(4096, 4096, Falcor::ResourceFormat::RG32Float, 1, 1, terrain.shadowEdges.shadowH, Falcor::Resource::BindFlags::UnorderedAccess | Falcor::Resource::BindFlags::ShaderResource);
        terrain.shadowEdges.launchSolveThread();

        atmosphere.setTerrainShadow(terrain.terrainShadowTexture);
    }
    LOG_LINE(1, "end of onLoad");
}


// move to a postprocess class
void Earthworks_4::loadColorCube(std::string name)
{
    std::string line;
    float3 color;
    std::ifstream cube(name);
    if (cube.good())
    {
        std::getline(cube, line);
        std::getline(cube, line);
        std::getline(cube, line);
        std::getline(cube, line);
        std::getline(cube, line);

        float3 colorcube[33][33][33];
        for (int i = 0; i < 33; i++)
        {
            for (int j = 0; j < 33; j++)
            {
                for (int k = 0; k < 33; k++)
                {
                    cube >> colorcube[i][j][k].x >> colorcube[i][j][k].y >> colorcube[i][j][k].z;
                }
            }
        }
        postProcess.colorCube = Texture::create3D(33, 33, 33, Falcor::ResourceFormat::RGB32Float, 1, colorcube, Falcor::Resource::BindFlags::ShaderResource);
    }
    else
    {
        spdlog::error("ERROR - loadColorCube() - failed");
    }
}






void Earthworks_4::onFrameUpdate(RenderContext* _renderContext)
{

    {
        if (terrain.shadowEdges.shadowReady)
        {
            LOG_LINE(1, "terrain.shadowEdges.shadowReady");
            spdlog::info("terrain.shadowEdges.shadowReady");
            FALCOR_PROFILE("shadow update");
            _renderContext->updateTextureData(terrain.terrainShadowTexture.get(), terrain.shadowEdges.shadowH);
            global_sun_direction = terrain.shadowEdges.sunAng;
            terrain.shadowEdges.shadowReady = false;
        }
    }

    {
        FALCOR_PROFILE("onFrameUpdate");

        shaderLightBuffer lightBuffer;
        lightBuffer.sunRightVector = glm::normalize(glm::cross(float3(0, 1, 0), global_sun_direction));
        lightBuffer.sunUpVector = glm::normalize(glm::cross(global_sun_direction, lightBuffer.sunRightVector));
        lightBuffer.sunDirection = global_sun_direction;

        lightBuffer.screenSize = screenSize;

        lightBuffer.fog_far_log_F = atmosphere.getFar().m_logEnd;
        lightBuffer.fog_far_one_over_k = atmosphere.getFar().m_oneOverK;
        lightBuffer.fog_far_Start = atmosphere.getFar().m_params._near;

        lightBuffer.fog_near_log_F = atmosphere.getNear().m_logEnd;
        lightBuffer.fog_near_one_over_k = atmosphere.getNear().m_oneOverK;
        lightBuffer.fog_near_Start = atmosphere.getNear().m_params._near;

        terrain.updateShaderConstants(hdrPreviousFrame, lightBuffer);

        if (ew::gDebug.toggles.syncCamera)
            terrain.setCamera(CameraType_Main_Center, toGLM(camera->getViewMatrix()), toGLM(camera->getProjMatrix()), camera->getPosition(), true, 1920);

        if (ew::gDebug.toggles.terrainUpdate)
            terrain.update(_renderContext);

        //if (terrain.terrainMode != _terrainMode::vegetation)
        if (ew::gDebug.toggles.atmosphere)
        {
            //atmosphere.setSmokeTime(terrain.cfd.clipmap.lodOffsets, terrain.cfd.clipmap.lodScales);
            atmosphere.setSMOKE(terrain.cfd.sliceVolumeTexture);

            atmosphere.setSunDirection(global_sun_direction);
            atmosphere.getFar().setCamera(camera);
            atmosphere.computeSunInAtmosphere(_renderContext);
            atmosphere.computeVolumetric(_renderContext);
        }
    }
}



void Earthworks_4::onFrameRender(RenderContext* _renderContext, const Fbo::SharedPtr& pTargetFbo)
{
    ew::gDebug.beginFrame();

    // The desktop host does not forward the 1..7 mode keys; honour a UI-driven
    // mode request here so update() and the render below both see it this frame.
    if (ew::gDebug.toggles.requestTerrainMode >= 0)
    {
        terrain.terrainMode = static_cast<_terrainMode>(ew::gDebug.toggles.requestTerrainMode);
        ew::gDebug.toggles.requestTerrainMode = -1;
    }
    ew::gDebug.live.terrainMode = static_cast<int>(terrain.terrainMode);

    onFrameUpdate(_renderContext);

    

    if (!terrain.fullResetDoNotRender)
    {
        FALCOR_PROFILE("onFrameRender");

        // clear
        {
            graphicsState->setFbo(pTargetFbo);
            const float4 clearColor(0.0f, 0.f, 0.f, 1);
            _renderContext->clearFbo(pTargetFbo.get(), clearColor, 1.0f, 0, FboAttachmentType::All);
            _renderContext->clearFbo(hdrFbo.get(), clearColor, 1.0f, 0, FboAttachmentType::All);
        }

        terrain.onFrameRender(_renderContext, pTargetFbo, camera, viewport3d);

        // Orientation globe: drawn INTO the HDR FBO (before tonemapping) with depth
        // testing so terrain occludes it. Where terrain exists the globe lines vanish,
        // so the terrain silhouette is visible even if the terrain shades black.
        if (showDebugGrid && ew::gDebug.toggles.debugGlobe)
            renderDebugGlobe(_renderContext, hdrFbo);

        if (ew::gDebug.toggles.tonemapper)
        {
            FALCOR_PROFILE("tonemapper");
            postProcess.tonemapper.Vars()->setTexture("hdr", hdrFbo->getColorTexture(0));
            postProcess.tonemapper.Vars()->setTexture("cube", postProcess.colorCube);
            postProcess.tonemapper.Vars()->setSampler("linearSampler", terrain.sampler_Clamp);
            postProcess.tonemapper.Vars()["gConstants"]["debugView"] = ew::gDebug.toggles.tonemapperView;
            postProcess.tonemapper.State()->setFbo(pTargetFbo);
            postProcess.tonemapper.State()->setRasterizerState(graphicsState->getRasterizerState());
            // 6 indices -> the two triangles of a full-screen quad (see vsMain). Drawing
            // only 3 gave a half-screen triangle because the shared index buffer forces an
            // indexed draw and the oversized-triangle trick clips badly on this backend.
            postProcess.tonemapper.drawInstanced(_renderContext, 6, 1);
            ew::gDebug.live.tonemapperDraws++;
        }

        if (ew::gDebug.toggles.overlay)
        {
            onRenderOverlay(_renderContext, pTargetFbo);
            ew::gDebug.live.overlayDraws++;
        }

        // World ground grid: drawn ON TOP of the final image (no depth test) so the
        // terrain area boundary / 1 km grid stays readable at all times.
        if (showDebugGrid && ew::gDebug.toggles.debugGroundGrid)
            renderDebugGroundGrid(_renderContext, pTargetFbo);

        glm::vec4 srcRect = glm::vec4(0, 0, screenSize.x, screenSize.y);
        glm::vec4 dstRect = glm::vec4(0, 0, screenSize.x * 0.5f, screenSize.y * 0.5f);
        _renderContext->blit(hdrFbo->getColorTexture(0)->getSRV(0, 1, 0, 1), hdrPreviousFrame->getRTV(), srcRect, dstRect, Sampler::Filter::Linear);

        if (false && refresh.minimal)
        {
            Sleep(20);       // aim for 15fps in this mode
        }
    }

    ew::gDebug.endFrame();
}



// Line counts / world extents shared by the globe and ground-grid draws.
// The globe values must match the ranges in debugGrid.hlsl.
namespace {
    constexpr int   kLatLines  = 17;      // latitudes  -80..+80 in 10 deg steps
    constexpr int   kLonLines  = 24;      // longitudes every 15 deg
    constexpr int   kSeg       = 96;      // segments per circle
    constexpr float kGlobeRad  = 3000.0f; // globe radius around the camera (m)

    // Total terrain footprint: 40 km x 40 km centred on the origin (see jp2Map::set,
    // wSize = 40000, wOffset = -20000). Draw a 1 km grid across it.
    constexpr float kAreaMin     = -20000.0f;
    constexpr float kAreaMax      =  20000.0f;
    constexpr float kKmSpacing   =  1000.0f;
    constexpr int   kKmLines     = 41;    // (40000 / 1000) + 1, edge-inclusive
    constexpr float kGroundY     = 0.0f;  // ground-plane height (0 = sea level)
}

void Earthworks_4::setDebugGridConstants(int drawMode)
{
    // Mirror the terrain/skydome convention exactly (see render_triangles.hlsl).
    const float4x4 viewproj = camera->getViewProjMatrix().getTranspose();
    debugGridVars["gConstantBuffer"]["viewproj"]    = viewproj;
    debugGridVars["gConstantBuffer"]["eye"]         = camera->getPosition();
    debugGridVars["gConstantBuffer"]["globeRadius"] = kGlobeRad;
    debugGridVars["gConstantBuffer"]["areaMin"]     = float3(kAreaMin, kGroundY, kAreaMin);
    debugGridVars["gConstantBuffer"]["areaMax"]     = float3(kAreaMax, kGroundY, kAreaMax);
    debugGridVars["gConstantBuffer"]["kmSpacing"]   = kKmSpacing;
    debugGridVars["gConstantBuffer"]["drawMode"]    = drawMode;
    debugGridVars["gConstantBuffer"]["latLines"]    = kLatLines;
    debugGridVars["gConstantBuffer"]["lonLines"]    = kLonLines;
    debugGridVars["gConstantBuffer"]["segments"]    = kSeg;
    debugGridVars["gConstantBuffer"]["kmLines"]     = kKmLines;
}

void Earthworks_4::renderDebugGlobe(RenderContext* _renderContext, const Fbo::SharedPtr& pHdrFbo)
{
    if (!debugGridProgram || !debugGlobeState || !debugGridVars || !camera)
        return;

    debugGlobeState->setFbo(pHdrFbo);
    debugGlobeState->setViewport(0, viewport3d, true);
    setDebugGridConstants(0);

    const uint32_t globeVerts = (kLatLines + kLonLines) * kSeg * 2;
    _renderContext->drawInstanced(debugGlobeState.get(), debugGridVars.get(), globeVerts, 1, 0, 0);
    ew::gDebug.live.debugGlobeDraws++;
}

void Earthworks_4::renderDebugGroundGrid(RenderContext* _renderContext, const Fbo::SharedPtr& pTargetFbo)
{
    if (!debugGridProgram || !debugGridState || !debugGridVars || !camera)
        return;

    debugGridState->setFbo(pTargetFbo);
    debugGridState->setViewport(0, viewport3d, true);
    setDebugGridConstants(1);

    const uint32_t groundVerts = kKmLines * 2 /*axes*/ * 2 /*endpoints*/;
    _renderContext->drawInstanced(debugGridState.get(), debugGridVars.get(), groundVerts, 1, 0, 0);
    ew::gDebug.live.debugGridDraws++;
}



// This migth move to ImGui completely
void Earthworks_4::onRenderOverlay(RenderContext* _renderContext, const Fbo::SharedPtr& pTargetFbo)
{
    Texture::SharedPtr tex = terrafectorEditorMaterial::static_materials.getDisplayTexture();
    if (!tex) tex = _plantMaterial::static_materials_veg.getDisplayTexture();
    if (!tex) {
        tex = roadNetwork::displayThumbnail;
    }
    //tex = terrain.plants_Root.RGB_MAP;  // FIXME hardcocded for test
    if (tex)
    {
        uint w = tex->getWidth();
        uint h = tex->getHeight();
        uint scale = __min(8, __max(1, 1200 / h));
        glm::vec4 srcRect = glm::vec4(0, 0, w, h);
        glm::vec4 dstRect = glm::vec4(600, 30, 600, 30) + glm::vec4(0, 0, w * scale, h * scale);
        _renderContext->blit(tex->getSRV(0, 1, 0, 1), pTargetFbo->getColorTexture(0)->getRTV(), srcRect, dstRect, Sampler::Filter::Point, overlayBlendstate);
        terrafectorEditorMaterial::static_materials.dispTexIndex = -1;
        _plantMaterial::static_materials_veg.dispTexIndex = -1;
    }
}


void Earthworks_4::onShutdown()
{
    terrain.onShutdown();

    FILE* file = fopen("camera.bin", "wb");
    if (file) {
        CameraData& data = camera->getData();
        fwrite(&data, sizeof(CameraData), 1, file);
        fclose(file);
    }
    std::ofstream os("earthworks4_presets.xml");
    if (os.good()) {
        cereal::XMLOutputArchive archive(os);
        serialize(archive, 100);
    }
}



bool Earthworks_4::onKeyEvent(const KeyboardEvent& _keyEvent)
{
    terrain.onKeyEvent(_keyEvent);

    if (_keyEvent.type == KeyboardEvent::Type::KeyPressed)
    {
        if (_keyEvent.key == Input::Key::Q)
        {
            showGui = !showGui;
        }
        if (_keyEvent.key == Input::Key::V)
        {
            refresh.minimal = !refresh.minimal;
        }
        if (_keyEvent.key == Input::Key::G)
        {
            showDebugGrid = !showDebugGrid;
        }
    }

    return false;
}



bool Earthworks_4::onMouseEvent(const MouseEvent& _mouseEvent)
{
    return terrain.onMouseEvent(_mouseEvent, screenSize, screenMouseScale, screenMouseOffset, camera);
}



void Earthworks_4::onResizeSwapChain(uint32_t _width, uint32_t _height)
{
    auto monitorDescs = MonitorInfo::getMonitorDescs();
    screenSize = float2(_width, _height);
    viewport3d = GraphicsState::Viewport(0.f, 0.f, screenSize.x, screenSize.y, 0.f, 1.f);
    camera->setAspectRatio(screenSize.x / screenSize.y);

    screenMouseScale.x = _width / screenSize.x;
    screenMouseScale.y = _height / screenSize.y;
    screenMouseOffset.x = 0;
    screenMouseOffset.y = 0;

    hdrFbo = Fbo::create2D(_width, _height, Fbo::Desc().setDepthStencilTarget(ResourceFormat::D24UnormS8).setColorTarget(0u, ResourceFormat::R11G11B10Float));

    hdrPreviousFrame = Texture::create2D(_width / 2, _height / 2, ResourceFormat::R11G11B10Float, 1, 1, nullptr, Falcor::Resource::BindFlags::AllColorViews);

    terrafectorSystem::logTimeX();
    spdlog::info("Earthworks_4::onResizeSwapChain()  {}, {}", _width, _height);

    //SampleConfig cfg = this->getConfig();
}



void Earthworks_4::guiStyle()
{
#define HI(v)   ImVec4(0.502f, 0.075f, 0.256f, v)
#define MED(v)  ImVec4(0.455f, 0.198f, 0.301f, v)
#define LOW(v)  ImVec4(0.232f, 0.201f, 0.271f, v)
#define BG(v)   ImVec4(0.200f, 0.220f, 0.270f, v)
#define TEXTG(v) ImVec4(0.860f, 0.930f, 0.890f, v)
#define LIME(v) ImVec4(0.38f, 0.52f, 0.10f, v);
#define DARKLIME(v) ImVec4(0.06f, 0.1f, 0.03f, v);
#define GREY(i,v) ImVec4(i, i, i, v);

    auto& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.01f, 0.01f, 0.01f, 0.90f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.13f, 0.14f, 0.17f, 0.70f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.13f, 0.14f, 0.17f, 0.90f);

    style.Colors[ImGuiCol_Button] = GREY(0.01f, 1.f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.01f, 0.06f, 0.06f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.18f, 0.10f, 0.04f, 1.0f);

    style.Colors[ImGuiCol_HeaderHovered] = DARKLIME(1.f);

    style.Colors[ImGuiCol_Tab] = ImVec4(0.03f, 0.03f, 0.03f, 1.f);
    style.Colors[ImGuiCol_TabHovered] = DARKLIME(1.f);
    style.Colors[ImGuiCol_TabActive] = DARKLIME(1.f);

    style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.04f, 0.04f, 0.04f, 1.0f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.03f, 0.03f, 0.03f, 1.0f);

    style.Colors[ImGuiCol_ModalWindowDimBg] = DARKLIME(0.3f);

    style.Colors[ImGuiCol_FrameBg] = GREY(0.f, 0.93f);
    style.Colors[ImGuiCol_FrameBgHovered] = GREY(0.03f, 0.93f);
    style.Colors[ImGuiCol_FrameBgActive] = GREY(0.02f, 0.93f);

    style.FrameRounding = 0.f;
    style.FramePadding = ImVec2(0, 0);

    style.ScrollbarSize = 25;

    style.WindowPadding = ImVec2(0, 0);              // Padding within a window.
    style.WindowRounding = 0.f;                      // Radius of window corners rounding. Set to 0.0f to have rectangular windows.
    style.WindowBorderSize = 0.f;

}



//int main(int argc, char** argv)
//{
//    logFile = fopen("log.txt", "w");
//    terrafectorSystem::logStartTime = high_resolution_clock::now();
//    terrafectorSystem::_logfile = logFile;
//
//    JLogger::instancePtr()->open("log.cpp");
//    {
//        LOG_BLOCK("", 0);
//        std::filesystem::path currentPath = std::filesystem::current_path();
//        LOG_LINE(1, currentPath.string());
//
//
//        bool allScreens = false;
//
//        for (int i = 0; i < argc; i++)
//        {
//            if (std::string(argv[i]).find("-allscreens") != std::string::npos) allScreens = true;
//        }
//
//        SampleConfig config;
//        config.windowDesc.title = "Earthworks 4";
//        config.windowDesc.resizableWindow = true;
//        config.windowDesc.mode = Window::WindowMode::Normal;
//        if (allScreens) {
//            //config.windowDesc.mode = Window::WindowMode::AllScreens;
//        }
//        config.windowDesc.width = 2560;// 3260;
//        config.windowDesc.height = 1340;// 1840;
//        //config.windowDesc.monitor = 1;
//        // HDR
//        //config.windowDesc.monitor = 1;
//        //config.deviceDesc.colorFormat = ResourceFormat::RGB10A2Unorm;
//
//        Earthworks_4::UniquePtr pRenderer = std::make_unique<Earthworks_4>();
//
//        Sample::run(config, pRenderer);
//    }
//
//    JLogger::instancePtr()->close();
//    fclose(logFile);
//    return 0;
//}
