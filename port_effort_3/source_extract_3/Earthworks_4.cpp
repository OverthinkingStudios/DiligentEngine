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
#include "Utils/UI/TextRenderer.h"
#include "Core/Platform/MonitorInfo.h"
#include <filesystem>
#include <iostream>

 //#pragma optimize("", off)

FILE* logFile;



void Earthworks_4::onLoad(RenderContext* _renderContext)
{
    LOG_BLOCK("Earthworks_4::onLoad", 0);
    terrafectorSystem::logTimeX();
    std::cout << "onLoad()\n";
    fprintf(logFile, "Earthworks_4::onLoad()\n");
    fflush(logFile);

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
    std::cout << "  atmosphere\n";
    fprintf(logFile, "atmosphere.onLoad()\n");
    fflush(logFile);
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

        //terrain.triangleShader.Vars()->setTexture("gAtmosphereInscatter", atmosphere.getFar().inscatter);
        //terrain.triangleShader.Vars()->setTexture("gAtmosphereOutscatter", atmosphere.getFar().outscatter);
        terrain.triangleShader.Vars()->setTexture("gAtmosphereInscatter_Sky", atmosphere.getFar().inscatter_sky);
    }

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

    postProcess.tonemapper.load("Samples/Earthworks_4/hlsl/compute_tonemapper.hlsl", "vsMain", "psMain", Vao::Topology::TriangleList);
    //loadColorCube("F:/terrains/colorcubes/K_TONE Vintage_KODACHROME.cube");
    loadColorCube(terrain.settings.dirResource + "/colorcubes/ColdChrome.cube");

    {
        // BAD - for STEG
        //terrain.shadowEdges.load(terrain.settings.dirRoot + "/gis/_export/root4096.bil", -global_sun_direction.y);
        terrain.shadowEdges.load(terrain.settings.dirRoot + "/elevation/root4096.bil", -global_sun_direction.y);
        terrain.shadowEdges.sunAngle = 0.95605f;
        terrain.shadowEdges.dAngle = 0.0001f;
        terrain.shadowEdges.requestNewShadow = true;

        terrain.terrainShadowTexture = Texture::create2D(4096, 4096, Falcor::ResourceFormat::RG32Float, 1, 1, terrain.shadowEdges.shadowH, Falcor::Resource::BindFlags::UnorderedAccess | Falcor::Resource::BindFlags::ShaderResource);

        std::thread thread_shadows(&_shadowEdges::solveThread, &terrain.shadowEdges);
        thread_shadows.detach();

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
        fprintf(logFile, "ERROR - loadColorCube() - failed\n");
        fflush(logFile);
    }
}






void Earthworks_4::onFrameUpdate(RenderContext* _renderContext)
{

    {
        if (terrain.shadowEdges.shadowReady)
        {
            LOG_LINE(1, "terrain.shadowEdges.shadowReady");
            fprintf(logFile, "terrain.shadowEdges.shadowReady\n");
            fflush(logFile);
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

        terrain.setCamera(CameraType_Main_Center, toGLM(camera->getViewMatrix()), toGLM(camera->getProjMatrix()), camera->getPosition(), true, 1920);

        terrain.update(_renderContext);

        {
            atmosphere.setSunDirection(global_sun_direction);
            atmosphere.getFar().setCamera(camera);
            atmosphere.computeSunInAtmosphere(_renderContext);
            atmosphere.computeVolumetric(_renderContext);
        }
    }
}



void Earthworks_4::onFrameRender(RenderContext* _renderContext, const Fbo::SharedPtr& pTargetFbo)
{
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

        terrain.onFrameRender(_renderContext, hdrFbo, camera, viewport3d);

        {
            FALCOR_PROFILE("tonemapper");
            postProcess.tonemapper.Vars()->setTexture("hdr", hdrFbo->getColorTexture(0));
            postProcess.tonemapper.Vars()->setTexture("cube", postProcess.colorCube);
            postProcess.tonemapper.Vars()->setSampler("linearSampler", terrain.sampler_Clamp);
            postProcess.tonemapper.State()->setFbo(pTargetFbo);
            postProcess.tonemapper.State()->setRasterizerState(graphicsState->getRasterizerState());
            postProcess.tonemapper.drawInstanced(_renderContext, 3, 1);
        }

        _renderContext->blit(hdrFbo->getColorTexture(0)->getSRV(0, 1, 0, 1), hdrPreviousFrame->getRTV());

        if (refresh.minimal)
        {
            Sleep(20);       // aim for 15fps in this mode
        }
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
        if (_keyEvent.key == Input::Key::V)
        {
            refresh.minimal = !refresh.minimal;
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
    fprintf(logFile, "Earthworks_4::onResizeSwapChain()  %d, %d\n", _width, _height);
    fflush(logFile);

    //SampleConfig cfg = this->getConfig();
}



int main(int argc, char** argv)
{
    logFile = fopen("log.txt", "w");
    terrafectorSystem::logStartTime = high_resolution_clock::now();
    terrafectorSystem::_logfile = logFile;

    JLogger::instancePtr()->open("log.cpp");
    {
        LOG_BLOCK("", 0);
        std::filesystem::path currentPath = std::filesystem::current_path();
        LOG_LINE(1, currentPath.string());


        bool allScreens = false;

        for (int i = 0; i < argc; i++)
        {
            if (std::string(argv[i]).find("-allscreens") != std::string::npos) allScreens = true;
        }

        SampleConfig config;
        config.windowDesc.title = "Earthworks 4";
        config.windowDesc.resizableWindow = true;
        config.windowDesc.mode = Window::WindowMode::Normal;
        if (allScreens) {
            //config.windowDesc.mode = Window::WindowMode::AllScreens;
        }
        config.windowDesc.width = 2560;// 3260;
        config.windowDesc.height = 1340;// 1840;
        //config.windowDesc.monitor = 1;
        // HDR
        //config.windowDesc.monitor = 1;
        //config.deviceDesc.colorFormat = ResourceFormat::RGB10A2Unorm;

        Earthworks_4::UniquePtr pRenderer = std::make_unique<Earthworks_4>();

        Sample::run(config, pRenderer);
    }

    JLogger::instancePtr()->close();
    fclose(logFile);
    return 0;
}
