#include "atmosphere.h"

extern unsigned int phaseX;
extern unsigned int phaseY;
extern float phaseData[];

void FogVolume::onLoad(FogVolumeParameters params)
{
    m_params = params;
    inscatter = Texture::create3D(params.m_x, params.m_y, params.m_z, params.format, 1, nullptr, Falcor::Resource::BindFlags::UnorderedAccess | Falcor::Resource::BindFlags::ShaderResource);
    inscatter_cloudbase = Texture::create2D(params.m_x, params.m_y, Falcor::ResourceFormat::RGBA16Float, 1, 1, nullptr, Falcor::Resource::BindFlags::UnorderedAccess | Falcor::Resource::BindFlags::ShaderResource);
    inscatter_sky = Texture::create2D(params.m_x, params.m_y, Falcor::ResourceFormat::RGBA16Float, 1, 1, nullptr, Falcor::Resource::BindFlags::UnorderedAccess | Falcor::Resource::BindFlags::ShaderResource);

    outscatter = Texture::create3D(params.m_x, params.m_y, params.m_z, params.format, 1, nullptr, Falcor::Resource::BindFlags::UnorderedAccess | Falcor::Resource::BindFlags::ShaderResource);
    outscatter_cloudbase = Texture::create2D(params.m_x, params.m_y, Falcor::ResourceFormat::RGBA16Float, 1, 1, nullptr, Falcor::Resource::BindFlags::UnorderedAccess | Falcor::Resource::BindFlags::ShaderResource);
    outscatter_sky = Texture::create2D(params.m_x, params.m_y, Falcor::ResourceFormat::RGBA16Float, 1, 1, nullptr, Falcor::Resource::BindFlags::UnorderedAccess | Falcor::Resource::BindFlags::ShaderResource);
}

void FogVolume::updateFogparameters(fogAtmosphericParams params)
{
    compute_Params.haze_Turbidity = params.haze_Turbidity;
    compute_Params.fog_Turbidity = params.fog_Turbidity;
    compute_Params.fog_BaseAltitudeKm = params.fog_BaseAltitudeKm;
    compute_Params.fog_AltitudeKm = params.fog_AltitudeKm;
}

void FogVolume::setCamera(Camera::SharedPtr _camera)
{
    glm::mat4 W = toGLM(_camera->getViewMatrix().getTranspose());

    float3 dir = W[2] * -1.f;
    float3 up = W[1];
    float3 right = W[0];

    //note these functions are overridden in vr and triple... (see CameraStereo and CameraTriple)
    float fovY = std::atan(0.5f * _camera->getFrameHeight() / _camera->getFocalLength());
    float fovX = std::atan(0.5f * _camera->getFrameHeight() * _camera->getAspectRatio() / _camera->getFocalLength());

    float tan_fovH = tan(fovX);
    float tan_fovV = tan(fovY);
    float3 dX = right / (m_params.m_x / 2.0f) * tan_fovH;
    float3 dY = up / (m_params.m_y / 2.0f) * tan_fovV * (-1.0f); // inverted due to texture coordinates

    compute_Params.dx = dX;
    compute_Params.dy = dY;
    compute_Params.eye_direction =
        dir - dX * (m_params.m_x / 2.0f - 0.5f) - dY * (m_params.m_y / 2.0f - 0.5f); // move to the center of the top leftmost pixel  ??? should I use edges instead of centers
    compute_Params.eye_position = _camera->getPosition();
    compute_Params.numSlices = m_params.m_z;
    compute_Params.sliceZero = m_SliceZero;
    compute_Params.sliceMultiplier = m_SliceStep;
    compute_Params.sliceStep = m_SliceStep - 1.0f;

    // update
    m_logEnd = ((float)m_params.m_z - 1.0f) / ((float)m_params.m_z) / (log(m_params._far / m_params._near));
    m_oneOverK = 1.0f / (float)m_params.m_z;

    float base = m_params._far / m_params._near;
    m_SliceStep = pow(base, 1.0f / (m_params.m_z - 1.0f));
    m_SliceZero = m_params._near / m_SliceStep;
}


void atmosphereAndFog::onLoad(RenderContext* _renderContext, FILE* _logfile)
{
    
    Sampler::Desc samplerDesc;
    samplerDesc.setAddressingMode(Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp).setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear).setMaxAnisotropy(1);
    sampler_Clamp = Sampler::create(samplerDesc);
    samplerDesc.setAddressingMode(Sampler::AddressMode::Wrap, Sampler::AddressMode::Wrap, Sampler::AddressMode::Wrap).setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear).setMaxAnisotropy(8);
    sampler_Trilinear = Sampler::create(samplerDesc);

    mainFar.onLoad(FogVolumeParameters{ 256/4, 128/4, 256, Falcor::ResourceFormat::R11G11B10Float, true, false, 50.f, 20000.f });
    //mainFar.onLoad(FogVolumeParameters{ 256, 128, 256, Falcor::ResourceFormat::R11G11B10Float, true, false, 50.f, 20000.f });
    mainNear.onLoad(FogVolumeParameters{ 256, 128, 256, Falcor::ResourceFormat::RGBA16Float, false, false, 1.f, 100.f });
    parabolicFar.onLoad(FogVolumeParameters{ 128, 128, 32, Falcor::ResourceFormat::R11G11B10Float, true, true, 100.f, 20000.f });

    phaseFunction = Texture::create2D(phaseX, phaseY, Falcor::ResourceFormat::R32Float, 1, 1, phaseData, Falcor::Resource::BindFlags::UnorderedAccess | Falcor::Resource::BindFlags::ShaderResource);
    sunlightTexture = Texture::create2D(512, 256, Falcor::ResourceFormat::RGBA32Float, 1, 1, nullptr, Falcor::Resource::BindFlags::UnorderedAccess | Falcor::Resource::BindFlags::ShaderResource);

    compute_sunSlice.load("Samples/Earthworks_4/hlsl/atmosphere/compute_sunlightInAtmosphere.hlsl");
    compute_sunSlice.Vars()->setTexture("gResult", sunlightTexture);

    compute_Atmosphere.load("Samples/Earthworks_4/hlsl/atmosphere/compute_volumeFogAtmosphericScatter.hlsl");
    compute_Atmosphere.Vars()->setTexture("SunInAtmosphere", sunlightTexture);
    compute_Atmosphere.Vars()->setTexture("hazePhaseFunction", phaseFunction);
    compute_Atmosphere.Vars()->setTexture("envMap", Texture::createCube(1, Falcor::ResourceFormat::RGBA8Unorm, Falcor::Resource::BindFlags::ShaderResource));
    compute_Atmosphere.Vars()->setTexture("gInscatter", mainFar.inscatter);
    compute_Atmosphere.Vars()->setTexture("gInscatter_cloudBase", mainFar.inscatter_cloudbase);
    compute_Atmosphere.Vars()->setTexture("gInscatter_sky", mainFar.inscatter_sky);
    compute_Atmosphere.Vars()->setTexture("gOutscatter", mainFar.outscatter);
    compute_Atmosphere.Vars()->setTexture("gOutscatter_cloudBase", mainFar.outscatter_cloudbase);
    compute_Atmosphere.Vars()->setTexture("gOutscatter_sky", mainFar.outscatter_sky);
}


void atmosphereAndFog::computeSunInAtmosphere(RenderContext* _renderContext)
{
    mainFar.updateFogparameters(params);
    mainNear.updateFogparameters(params);
    parabolicFar.updateFogparameters(params);

    FALCOR_PROFILE("sunlight");
    compute_sunSlice.Vars()["FogCloudCommonParams"]["sun_direction"] = common.sun_direction;
    compute_sunSlice.Vars()["FogCloudCommonParams"]["cloudBase"] = common.cloudBase;
    compute_sunSlice.Vars()["FogCloudCommonParams"]["cloudThickness"] = common.cloudThickness;

    compute_sunSlice.Vars()["FogAtmosphericParams"]["eye_direction"] = params.eye_direction;
    compute_sunSlice.Vars()["FogAtmosphericParams"]["uv_offset"] = params.uv_offset;
    compute_sunSlice.Vars()["FogAtmosphericParams"]["eye_position"] = params.eye_position;
    compute_sunSlice.Vars()["FogAtmosphericParams"]["uv_scale"] = params.uv_scale;
    compute_sunSlice.Vars()["FogAtmosphericParams"]["dx"] = params.dx;
    compute_sunSlice.Vars()["FogAtmosphericParams"]["uv_pixSize"] = params.uv_pixSize;
    compute_sunSlice.Vars()["FogAtmosphericParams"]["dy"] = params.dy;
    compute_sunSlice.Vars()["FogAtmosphericParams"]["numSlices"] = params.numSlices;
    compute_sunSlice.Vars()["FogAtmosphericParams"]["sliceZero"] = params.sliceZero;
    compute_sunSlice.Vars()["FogAtmosphericParams"]["sliceMultiplier"] = params.sliceMultiplier;
    compute_sunSlice.Vars()["FogAtmosphericParams"]["sliceStep"] = params.sliceStep;

    compute_sunSlice.Vars()["FogAtmosphericParams"]["timer"] = params.timer;
    compute_sunSlice.Vars()["FogAtmosphericParams"]["numFogLights"] = params.numFogLights;
    compute_sunSlice.Vars()["FogAtmosphericParams"]["paddTime"] = params.paddTime;

    compute_sunSlice.Vars()["FogAtmosphericParams"]["fogAltitude"] = params.fogAltitude;
    compute_sunSlice.Vars()["FogAtmosphericParams"]["fogDensity"] = params.fogDensity;
    compute_sunSlice.Vars()["FogAtmosphericParams"]["fogUVoffset"] = params.fogUVoffset;
    compute_sunSlice.Vars()["FogAtmosphericParams"]["fogMip0Size"] = params.fogMip0Size;
    compute_sunSlice.Vars()["FogAtmosphericParams"]["numFogVolumes"] = params.numFogVolumes;

    compute_sunSlice.Vars()["FogAtmosphericParams"]["sunColourBeforeOzone"] = params.sunColourBeforeOzone;
    compute_sunSlice.Vars()["FogAtmosphericParams"]["haze_Turbidity"] = params.haze_Turbidity;
    compute_sunSlice.Vars()["FogAtmosphericParams"]["haze_Colour"] = params.haze_Colour;
    compute_sunSlice.Vars()["FogAtmosphericParams"]["haze_AltitudeKm"] = params.haze_AltitudeKm;
    compute_sunSlice.Vars()["FogAtmosphericParams"]["fog_Colour"] = params.fog_Colour;
    compute_sunSlice.Vars()["FogAtmosphericParams"]["haze_BaseAltitudeKm"] = params.haze_BaseAltitudeKm;
    compute_sunSlice.Vars()["FogAtmosphericParams"]["fog_AltitudeKm"] = params.fog_AltitudeKm;
    compute_sunSlice.Vars()["FogAtmosphericParams"]["fog_BaseAltitudeKm"] = params.fog_BaseAltitudeKm;
    compute_sunSlice.Vars()["FogAtmosphericParams"]["fog_Turbidity"] = params.fog_Turbidity;
    compute_sunSlice.Vars()["FogAtmosphericParams"]["globalExposure"] = params.globalExposure;
    compute_sunSlice.Vars()["FogAtmosphericParams"]["rain_Colour"] = params.rain_Colour;
    compute_sunSlice.Vars()["FogAtmosphericParams"]["rainFade"] = params.rainFade;
    compute_sunSlice.Vars()["FogAtmosphericParams"]["ozone_Density"] = params.ozone_Density;
    compute_sunSlice.Vars()["FogAtmosphericParams"]["ozone_Colour"] = params.ozone_Colour;
    compute_sunSlice.Vars()["FogAtmosphericParams"]["refraction"] = params.refraction;

    compute_sunSlice.Vars()["FogAtmosphericParams"]["parabolicProjection"] = params.parabolicProjection;
    compute_sunSlice.Vars()["FogAtmosphericParams"]["parabolicMapHalfSize"] = params.parabolicMapHalfSize;
    compute_sunSlice.Vars()["FogAtmosphericParams"]["parabolicUpDown"] = params.parabolicUpDown;

    compute_sunSlice.Vars()["FogAtmosphericParams"]["tmp_IBL_scale"] = params.tmp_IBL_scale;

    compute_sunSlice.dispatch(_renderContext, 512 / 32, 256 / 32);
}


void atmosphereAndFog::computeVolumetric(RenderContext* _renderContext)
{
    FALCOR_PROFILE("volumeFog");


    compute_Atmosphere.Vars()["FogCloudCommonParams"]["sun_direction"] = common.sun_direction;
    compute_Atmosphere.Vars()["FogCloudCommonParams"]["cloudBase"] = common.cloudBase;
    compute_Atmosphere.Vars()["FogCloudCommonParams"]["cloudThickness"] = common.cloudThickness;

    compute_Atmosphere.Vars()["FogAtmosphericParams"]["eye_direction"] = mainFar.compute_Params.eye_direction;
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["uv_offset"] = mainFar.compute_Params.uv_offset;
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["eye_position"] = mainFar.compute_Params.eye_position;
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["uv_scale"] = mainFar.compute_Params.uv_scale;
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["dx"] = mainFar.compute_Params.dx;
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["uv_pixSize"] = mainFar.compute_Params.uv_pixSize;
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["dy"] = mainFar.compute_Params.dy;
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["numSlices"] = mainFar.compute_Params.numSlices;
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["sliceZero"] = mainFar.compute_Params.sliceZero;
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["sliceMultiplier"] = mainFar.compute_Params.sliceMultiplier;
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["sliceStep"] = mainFar.compute_Params.sliceStep;

    compute_Atmosphere.Vars()["FogAtmosphericParams"]["timer"] = mainFar.compute_Params.timer;
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["numFogLights"] = mainFar.compute_Params.numFogLights;
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["paddTime"] = mainFar.compute_Params.paddTime;

    compute_Atmosphere.Vars()["FogAtmosphericParams"]["fogAltitude"] = mainFar.compute_Params.fogAltitude;
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["fogDensity"] = mainFar.compute_Params.fogDensity;
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["fogUVoffset"] = mainFar.compute_Params.fogUVoffset;
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["fogMip0Size"] = mainFar.compute_Params.fogMip0Size;
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["numFogVolumes"] = mainFar.compute_Params.numFogVolumes;

    compute_Atmosphere.Vars()["FogAtmosphericParams"]["sunColourBeforeOzone"] = mainFar.compute_Params.sunColourBeforeOzone;
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["haze_Turbidity"] = mainFar.compute_Params.haze_Turbidity;
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["haze_Colour"] = mainFar.compute_Params.haze_Colour;
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["haze_AltitudeKm"] = mainFar.compute_Params.haze_AltitudeKm;
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["fog_Colour"] = mainFar.compute_Params.fog_Colour;
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["haze_BaseAltitudeKm"] = mainFar.compute_Params.haze_BaseAltitudeKm;
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["fog_AltitudeKm"] = mainFar.compute_Params.fog_AltitudeKm;
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["fog_BaseAltitudeKm"] = mainFar.compute_Params.fog_BaseAltitudeKm;
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["fog_Turbidity"] = mainFar.compute_Params.fog_Turbidity;
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["globalExposure"] = mainFar.compute_Params.globalExposure;
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["rain_Colour"] = mainFar.compute_Params.rain_Colour;
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["rainFade"] = mainFar.compute_Params.rainFade;
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["ozone_Density"] = mainFar.compute_Params.ozone_Density;
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["ozone_Colour"] = mainFar.compute_Params.ozone_Colour;
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["refraction"] = mainFar.compute_Params.refraction;

    compute_Atmosphere.Vars()["FogAtmosphericParams"]["parabolicProjection"] = mainFar.compute_Params.parabolicProjection;
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["parabolicMapHalfSize"] = mainFar.compute_Params.parabolicMapHalfSize;
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["parabolicUpDown"] = mainFar.compute_Params.parabolicUpDown;

    compute_Atmosphere.Vars()["FogAtmosphericParams"]["tmp_IBL_scale"] = mainFar.compute_Params.tmp_IBL_scale;

    compute_Atmosphere.Vars()->setSampler("linearSampler", sampler_Trilinear);
    compute_Atmosphere.Vars()->setSampler("clampSampler", sampler_Clamp);

    compute_Atmosphere.dispatch(_renderContext, mainFar.m_params.m_x / 8, mainFar.m_params.m_y / 8, 1);
}

void atmosphereAndFog::setSunDirection(float3 dir)
{
    common.sun_direction = dir;
}

void atmosphereAndFog::setTerrainShadow(Texture::SharedPtr shadow)
{
    compute_Atmosphere.Vars()->setTexture("terrainShadow", shadow);
}

void atmosphereAndFog::setSMOKE(Texture::SharedPtr textures[6][3])
{
    for (uint lod = 0; lod < 6; lod++)
    {
        compute_Atmosphere.Vars()->setTexture(("gCfd_T_" + std::to_string(lod * 2 + 0)).c_str(), textures[lod][0]);
        compute_Atmosphere.Vars()->setTexture(("gCfd_T_" + std::to_string(lod * 2 + 1)).c_str(), textures[lod][1]);
    }
}

void atmosphereAndFog::setSmokeTime(float4 lodOffsets[6][2], float4 lodScales[6])
{
    //void materialCache::rebuildStructuredBuffer()  look here bfot betetr handling
    //compute_Atmosphere.Vars()["FogAtmosphericParams"]["smk_dTime"] = lodOffsets[5].w;
    /*
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["cfdOffset_time_0"] = lodOffsets[0];
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["cfdOffset_time_1"] = lodOffsets[1];
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["cfdOffset_time_2"] = lodOffsets[2];
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["cfdOffset_time_3"] = lodOffsets[3];
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["cfdOffset_time_4"] = lodOffsets[4];
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["cfdOffset_time_5"] = lodOffsets[5];

    compute_Atmosphere.Vars()["FogAtmosphericParams"]["cfdScale_0"] = lodScales[0];
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["cfdScale_1"] = lodScales[1];
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["cfdScale_2"] = lodScales[2];
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["cfdScale_3"] = lodScales[3];
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["cfdScale_4"] = lodScales[4];
    compute_Atmosphere.Vars()["FogAtmosphericParams"]["cfdScale_5"] = lodScales[5];
    */

    for (uint lod = 0; lod < 6; lod++)
    {
        const int i0 = static_cast<int>(lod * 2);
        const int i1 = i0 + 1;
        compute_Atmosphere.Vars()[("gCfd_offset_" + std::to_string(i0)).c_str()] = lodOffsets[lod][0];
        compute_Atmosphere.Vars()[("gCfd_offset_" + std::to_string(i1)).c_str()] = lodOffsets[lod][1];
        compute_Atmosphere.Vars()[("gCfd_scale_" + std::to_string(i0)).c_str()]   = lodScales[lod];
        compute_Atmosphere.Vars()[("gCfd_scale_" + std::to_string(i1)).c_str()]   = lodScales[lod];
    }
}
