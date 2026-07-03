#include "buildings.h"
#include "EarthworksDebug.h"
#include "vegetationBuilder.h"   // shaderLightBuffer

#include <spdlog/spdlog.h>
#include <fstream>
#include <vector>

// PORT NOTE: logic restored from the commented-out "rappersville" blocks in
// terrain.cpp (~897 load, ~3926 LightsCB wiring, ~5266 render) and
// Earthworks_4.cpp (~288 atmosphere textures). See buildings.h.

void buildingsRenderer::load(const std::string& _basePath)
{
    spdlog::info("buildings: loading {}", _basePath);

    int numVerts = 0;
    {
        std::ifstream ifs(_basePath + ".info.txt");
        if (!ifs)
        {
            spdlog::warn("buildings: missing {}.info.txt - buildings disabled", _basePath);
            return;
        }
        ifs >> numVerts;
    }
    if (numVerts < 3)
    {
        spdlog::warn("buildings: {}.info.txt reports {} verts - buildings disabled", _basePath, numVerts);
        return;
    }

    std::vector<vertex> verts(numVerts);
    {
        std::ifstream ifs(_basePath + ".raw", std::ios::binary);
        if (!ifs)
        {
            spdlog::warn("buildings: missing {}.raw - buildings disabled", _basePath);
            return;
        }
        ifs.read(reinterpret_cast<char*>(verts.data()), verts.size() * sizeof(vertex));
        if (ifs.gcount() != static_cast<std::streamsize>(verts.size() * sizeof(vertex)))
        {
            spdlog::warn("buildings: {}.raw shorter than {} verts x 48 B - buildings disabled", _basePath, numVerts);
            return;
        }
    }

    vertexData = Buffer::createStructured(sizeof(vertex), numVerts);
    vertexData->setBlob(verts.data(), 0, numVerts * sizeof(vertex));

    shader.load("Samples/Earthworks_4/hlsl/terrain/render_Buildings_Far.hlsl", "vsMain", "psMain", Vao::Topology::TriangleList);
    shader.Vars()->setBuffer("vertexBuffer", vertexData);

    // The original never bound samplers here (Falcor supplied defaults); the
    // compat layer's dummy-sampler fallback would probably cover it, but this
    // is new code, so bind them explicitly. shadow() uses gSmpLinear,
    // sunLight()/atmosphere use gSmpLinearClamp - linear clamp suits both.
    {
        Sampler::Desc samplerDesc;
        samplerDesc.setAddressingMode(Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp)
                   .setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear);
        sampler_Clamp = Sampler::create(samplerDesc);
        shader.Vars()->setSampler("gSmpLinear", sampler_Clamp);
        shader.Vars()->setSampler("gSmpLinearClamp", sampler_Clamp);
    }

    numTriangles = numVerts / 3;
    spdlog::info("buildings: {} verts, {} triangles", numVerts, numTriangles);
}


void buildingsRenderer::updateShaderConstants(Texture::SharedPtr _terrainShadow, const shaderLightBuffer& _buffer)
{
    if (!loaded()) return;

    shader.Vars()->setTexture("terrainShadow", _terrainShadow);

    shader.Vars()["LightsCB"]["sunDirection"] = _buffer.sunDirection;
    shader.Vars()["LightsCB"]["sunRightVector"] = _buffer.sunRightVector;
    shader.Vars()["LightsCB"]["sunUpVector"] = _buffer.sunUpVector;
    shader.Vars()["LightsCB"]["screenSize"] = _buffer.screenSize;
    shader.Vars()["LightsCB"]["fog_far_Start"] = _buffer.fog_far_Start;
    shader.Vars()["LightsCB"]["fog_far_log_F"] = _buffer.fog_far_log_F;
    shader.Vars()["LightsCB"]["fog_far_one_over_k"] = _buffer.fog_far_one_over_k;
}


void buildingsRenderer::setAtmosphere(Texture::SharedPtr _inscatter, Texture::SharedPtr _outscatter, Texture::SharedPtr _sunlight)
{
    if (!loaded()) return;

    shader.Vars()->setTexture("gAtmosphereInscatter", _inscatter);
    shader.Vars()->setTexture("gAtmosphereOutscatter", _outscatter);
    shader.Vars()->setTexture("SunInAtmosphere", _sunlight);
}


void buildingsRenderer::render(RenderContext* _renderContext, const Fbo::SharedPtr& _fbo, const GraphicsState::Viewport& _viewport,
                               const rmcv::mat4& _view, const rmcv::mat4& _viewproj, const float3& _eye)
{
    if (!loaded() || !ew::gDebug.toggles.buildings) return;

    FALCOR_PROFILE("buildings");

    shader.State()->setFbo(_fbo);
    shader.State()->setViewport(0, _viewport, true);
    shader.Vars()["PerFrameCB"]["view"] = _view;
    shader.Vars()["PerFrameCB"]["viewproj"] = _viewproj;
    shader.Vars()["PerFrameCB"]["eye"] = _eye;
    shader.drawInstanced(_renderContext, 3, numTriangles);
    ew::gDebug.live.buildingDraws++;
}
