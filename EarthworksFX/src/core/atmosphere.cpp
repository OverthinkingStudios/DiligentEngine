// Camera-aligned volumetric fog (exponentially spaced depth slices) plus the
// 512x256 sun-in-atmosphere lookup that fog, terrain and vegetation all sample.

#include "atmosphere.h"

#include <cmath>

#include "ots/Log.hpp"

#include "EarthworksDebug.h"   // loadOptions.allocDormantFogVolumes (step-6 gate)

extern unsigned int phaseX;
extern unsigned int phaseY;
extern float phaseData[];

using Diligent::BIND_SHADER_RESOURCE;
using Diligent::BIND_UNORDERED_ACCESS;

void FogVolume::onLoad(FogVolumeParameters params, bool _allocateTextures)
{
    m_params = params;
    if (!_allocateTextures)
        return;   // dormant volume: parameters only (step-6 gate, see header)
    inscatter = ew::Texture::create3D(params.m_x, params.m_y, params.m_z, params.format, nullptr, BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE, "fog inscatter");
    inscatter_cloudbase = ew::Texture::create2D(params.m_x, params.m_y, Diligent::TEX_FORMAT_RGBA16_FLOAT, 1, 1, nullptr, BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE, "fog inscatter_cloudbase");
    inscatter_sky = ew::Texture::create2D(params.m_x, params.m_y, Diligent::TEX_FORMAT_RGBA16_FLOAT, 1, 1, nullptr, BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE, "fog inscatter_sky");

    outscatter = ew::Texture::create3D(params.m_x, params.m_y, params.m_z, params.format, nullptr, BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE, "fog outscatter");
    outscatter_cloudbase = ew::Texture::create2D(params.m_x, params.m_y, Diligent::TEX_FORMAT_RGBA16_FLOAT, 1, 1, nullptr, BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE, "fog outscatter_cloudbase");
    outscatter_sky = ew::Texture::create2D(params.m_x, params.m_y, Diligent::TEX_FORMAT_RGBA16_FLOAT, 1, 1, nullptr, BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE, "fog outscatter_sky");
}

// TODO: only these 4 of the ~30 fogAtmosphericParams fields reach compute_Params.
// The rest keep their member-initialiser defaults and are never synced from
// atmosphereAndFog::params, so the public params only drives the sun-LUT pass.
void FogVolume::updateFogparameters(fogAtmosphericParams params)
{
    compute_Params.haze_Turbidity = params.haze_Turbidity;
    compute_Params.fog_Turbidity = params.fog_Turbidity;
    compute_Params.fog_BaseAltitudeKm = params.fog_BaseAltitudeKm;
    compute_Params.fog_AltitudeKm = params.fog_AltitudeKm;
}

void FogVolume::setCamera(ew::Camera::SharedPtr _camera)
{
    // The one place core code decomposes the view matrix: transposing it puts
    // the camera basis (right, up, -forward) into the columns, which only holds
    // for a right-handed glm view matrix.
    glm::mat4 W = glm::transpose(_camera->getViewMatrix());

    atm_float3 dir = atm_float3(W[2]) * -1.f;
    atm_float3 up = atm_float3(W[1]);
    atm_float3 right = atm_float3(W[0]);

    //note these functions are overridden in vr and triple... (see CameraStereo and CameraTriple)
    // atan(0.5*h/f) WITHOUT the usual factor 2: this wants the HALF angle,
    // because the tan() below builds the per-pixel step vectors dX/dY from it.
    // Adding the 2 doubles the fog frustum. Do not "fix" it.
    float fovY = std::atan(0.5f * _camera->getFrameHeight() / _camera->getFocalLength());
    // TODO: fovX derives the width from getFrameHeight() * aspect, which is only
    // right if getFrameHeight() is the physical film-back height. There is no
    // getFrameWidth() anywhere - add one, or an assertion that states which it is.
    float fovX = std::atan(0.5f * _camera->getFrameHeight() * _camera->getAspectRatio() / _camera->getFocalLength());

    float tan_fovH = tan(fovX);
    float tan_fovV = tan(fovY);
    atm_float3 dX = right / (m_params.m_x / 2.0f) * tan_fovH;
    atm_float3 dY = up / (m_params.m_y / 2.0f) * tan_fovV * (-1.0f); // inverted due to texture coordinates

    compute_Params.dx = dX;
    compute_Params.dy = dY;
    compute_Params.eye_direction =
        dir - dX * (m_params.m_x / 2.0f - 0.5f) - dY * (m_params.m_y / 2.0f - 0.5f); // move to the center of the top leftmost pixel  ??? should I use edges instead of centers
    compute_Params.eye_position = _camera->getPosition();
    compute_Params.numSlices = m_params.m_z;
    // The slice constants are copied BEFORE they are recomputed at the end of
    // this function, so the shader always sees the previous call's values.
    // Steady-state identical - do not reorder.
    compute_Params.sliceZero = m_SliceZero;
    compute_Params.sliceMultiplier = m_SliceStep;
    compute_Params.sliceStep = m_SliceStep - 1.0f;

    m_logEnd = ((float)m_params.m_z - 1.0f) / ((float)m_params.m_z) / (log(m_params._far / m_params._near));
    m_oneOverK = 1.0f / (float)m_params.m_z;

    float base = m_params._far / m_params._near;
    m_SliceStep = pow(base, 1.0f / (m_params.m_z - 1.0f));
    m_SliceZero = m_params._near / m_SliceStep;
}


void atmosphereAndFog::onLoad(ew::GpuContext* _renderContext)
{
    (void)_renderContext;

    Diligent::SamplerDesc samplerDesc;
    samplerDesc.MinFilter = Diligent::FILTER_TYPE_LINEAR;
    samplerDesc.MagFilter = Diligent::FILTER_TYPE_LINEAR;
    samplerDesc.MipFilter = Diligent::FILTER_TYPE_LINEAR;
    samplerDesc.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = Diligent::TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxAnisotropy = 1;
    sampler_Clamp = ew::Sampler::create(samplerDesc);

    samplerDesc.AddressU = Diligent::TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressV = Diligent::TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressW = Diligent::TEXTURE_ADDRESS_WRAP;
    samplerDesc.MaxAnisotropy = 8;
    sampler_Trilinear = ew::Sampler::create(samplerDesc);

    mainFar.onLoad(FogVolumeParameters{ 256/4, 128/4, 256, Diligent::TEX_FORMAT_R11G11B10_FLOAT, true, false, 50.f, 20000.f });
    //mainFar.onLoad(FogVolumeParameters{ 256, 128, 256, Diligent::TEX_FORMAT_R11G11B10_FLOAT, true, false, 50.f, 20000.f });
    // PORT-REVIEW (step 6): mainNear and parabolicFar are DORMANT - never
    // computed or sampled, but the extract allocated them anyway (~130 MB of
    // idle GPU memory, mostly mainNear's two 256x128x256 RGBA16F volumes).
    // Gated behind a load option; their scalar parameters (the fog_near_*
    // members of shaderLightBuffer) still come through unchanged.
    const bool allocDormant = ew::gDebug.loadOptions.allocDormantFogVolumes;
    if (!allocDormant)
        spdlog::info("atmosphere: dormant fog volumes (mainNear/parabolicFar) NOT allocated - saves ~130 MB (gDebug.loadOptions.allocDormantFogVolumes)");
    mainNear.onLoad(FogVolumeParameters{ 256, 128, 256, Diligent::TEX_FORMAT_RGBA16_FLOAT, false, false, 1.f, 100.f }, allocDormant);
    parabolicFar.onLoad(FogVolumeParameters{ 128, 128, 32, Diligent::TEX_FORMAT_R11G11B10_FLOAT, true, true, 100.f, 20000.f }, allocDormant);

    phaseFunction = ew::Texture::create2D(phaseX, phaseY, Diligent::TEX_FORMAT_R32_FLOAT, 1, 1, phaseData, BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE, "phaseFunction");
    sunlightTexture = ew::Texture::create2D(512, 256, Diligent::TEX_FORMAT_RGBA32_FLOAT, 1, 1, nullptr, BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE, "sunlightTexture");

    compute_sunSlice.load("hlsl/atmosphere/compute_sunlightInAtmosphere.hlsl");
    compute_sunSlice.setTexture("gResult", sunlightTexture);

    compute_Atmosphere.load("hlsl/atmosphere/compute_volumeFogAtmosphericScatter.hlsl");
    compute_Atmosphere.setTexture("SunInAtmosphere", sunlightTexture);
    compute_Atmosphere.setTexture("hazePhaseFunction", phaseFunction);
    compute_Atmosphere.setTexture("gInscatter", mainFar.inscatter);
    compute_Atmosphere.setTexture("gInscatter_cloudBase", mainFar.inscatter_cloudbase);
    compute_Atmosphere.setTexture("gInscatter_sky", mainFar.inscatter_sky);
    compute_Atmosphere.setTexture("gOutscatter", mainFar.outscatter);
    compute_Atmosphere.setTexture("gOutscatter_cloudBase", mainFar.outscatter_cloudbase);
    compute_Atmosphere.setTexture("gOutscatter_sky", mainFar.outscatter_sky);

    // Dummy binds so every declared slot gets a view of the right dimension -
    // the ew layer's automatic fallback is 2D-only, and a 2D view in a cube or
    // 3D slot trips Vulkan's view-type validation. envMap's IBL samples are
    // dead code (the result is overwritten by a constant), and the gCfd_T_*
    // volumes belong to the cfd/smoke system that is not part of this build:
    // 1x1x1 zero volumes, and the zeroed gCfdParams scales make sample_cfd's
    // bounds checks reject them anyway.
    {
        const uint32_t zeroTexel = 0;
        ew::Texture::SharedPtr dummyCube = ew::Texture::createCube(1, Diligent::TEX_FORMAT_RGBA8_UNORM, &zeroTexel, BIND_SHADER_RESOURCE, "atmosphere dummy envMap");
        compute_Atmosphere.setTexture("envMap", dummyCube);

        ew::Texture::SharedPtr dummyVolume = ew::Texture::create3D(1, 1, 1, Diligent::TEX_FORMAT_RGBA8_UNORM, &zeroTexel, BIND_SHADER_RESOURCE, "atmosphere dummy cfd");
        for (int i = 0; i < 12; i++)
            compute_Atmosphere.setTexture("gCfd_T_" + std::to_string(i), dummyVolume);
    }
}


void atmosphereAndFog::computeSunInAtmosphere(ew::GpuContext* _renderContext)
{
    mainFar.updateFogparameters(params);
    mainNear.updateFogparameters(params);
    parabolicFar.updateFogparameters(params);

    compute_sunSlice.setVariable("FogCloudCommonParams", "sun_direction", common.sun_direction);
    compute_sunSlice.setVariable("FogCloudCommonParams", "cloudBase", common.cloudBase);
    compute_sunSlice.setVariable("FogCloudCommonParams", "cloudThickness", common.cloudThickness);

    compute_sunSlice.setVariable("FogAtmosphericParams", "eye_direction", params.eye_direction);
    compute_sunSlice.setVariable("FogAtmosphericParams", "uv_offset", params.uv_offset);
    compute_sunSlice.setVariable("FogAtmosphericParams", "eye_position", params.eye_position);
    compute_sunSlice.setVariable("FogAtmosphericParams", "uv_scale", params.uv_scale);
    compute_sunSlice.setVariable("FogAtmosphericParams", "dx", params.dx);
    compute_sunSlice.setVariable("FogAtmosphericParams", "uv_pixSize", params.uv_pixSize);
    compute_sunSlice.setVariable("FogAtmosphericParams", "dy", params.dy);
    compute_sunSlice.setVariable("FogAtmosphericParams", "numSlices", params.numSlices);
    compute_sunSlice.setVariable("FogAtmosphericParams", "sliceZero", params.sliceZero);
    compute_sunSlice.setVariable("FogAtmosphericParams", "sliceMultiplier", params.sliceMultiplier);
    compute_sunSlice.setVariable("FogAtmosphericParams", "sliceStep", params.sliceStep);

    compute_sunSlice.setVariable("FogAtmosphericParams", "timer", params.timer);
    compute_sunSlice.setVariable("FogAtmosphericParams", "numFogLights", params.numFogLights);
    compute_sunSlice.setVariable("FogAtmosphericParams", "paddTime", params.paddTime);

    compute_sunSlice.setVariable("FogAtmosphericParams", "fogAltitude", params.fogAltitude);
    compute_sunSlice.setVariable("FogAtmosphericParams", "fogDensity", params.fogDensity);
    compute_sunSlice.setVariable("FogAtmosphericParams", "fogUVoffset", params.fogUVoffset);
    compute_sunSlice.setVariable("FogAtmosphericParams", "fogMip0Size", params.fogMip0Size);
    compute_sunSlice.setVariable("FogAtmosphericParams", "numFogVolumes", params.numFogVolumes);

    compute_sunSlice.setVariable("FogAtmosphericParams", "sunColourBeforeOzone", params.sunColourBeforeOzone);
    compute_sunSlice.setVariable("FogAtmosphericParams", "haze_Turbidity", params.haze_Turbidity);
    compute_sunSlice.setVariable("FogAtmosphericParams", "haze_Colour", params.haze_Colour);
    compute_sunSlice.setVariable("FogAtmosphericParams", "haze_AltitudeKm", params.haze_AltitudeKm);
    compute_sunSlice.setVariable("FogAtmosphericParams", "fog_Colour", params.fog_Colour);
    compute_sunSlice.setVariable("FogAtmosphericParams", "haze_BaseAltitudeKm", params.haze_BaseAltitudeKm);
    compute_sunSlice.setVariable("FogAtmosphericParams", "fog_AltitudeKm", params.fog_AltitudeKm);
    compute_sunSlice.setVariable("FogAtmosphericParams", "fog_BaseAltitudeKm", params.fog_BaseAltitudeKm);
    compute_sunSlice.setVariable("FogAtmosphericParams", "fog_Turbidity", params.fog_Turbidity);
    compute_sunSlice.setVariable("FogAtmosphericParams", "globalExposure", params.globalExposure);
    compute_sunSlice.setVariable("FogAtmosphericParams", "rain_Colour", params.rain_Colour);
    compute_sunSlice.setVariable("FogAtmosphericParams", "rainFade", params.rainFade);
    compute_sunSlice.setVariable("FogAtmosphericParams", "ozone_Density", params.ozone_Density);
    compute_sunSlice.setVariable("FogAtmosphericParams", "ozone_Colour", params.ozone_Colour);
    compute_sunSlice.setVariable("FogAtmosphericParams", "refraction", params.refraction);

    compute_sunSlice.setVariable("FogAtmosphericParams", "parabolicProjection", params.parabolicProjection);
    compute_sunSlice.setVariable("FogAtmosphericParams", "parabolicMapHalfSize", params.parabolicMapHalfSize);
    compute_sunSlice.setVariable("FogAtmosphericParams", "parabolicUpDown", params.parabolicUpDown);

    compute_sunSlice.setVariable("FogAtmosphericParams", "tmp_IBL_scale", params.tmp_IBL_scale);

    compute_sunSlice.dispatch(_renderContext, 512 / 32, 256 / 32);
}


void atmosphereAndFog::computeVolumetric(ew::GpuContext* _renderContext)
{
    compute_Atmosphere.setVariable("FogCloudCommonParams", "sun_direction", common.sun_direction);
    compute_Atmosphere.setVariable("FogCloudCommonParams", "cloudBase", common.cloudBase);
    compute_Atmosphere.setVariable("FogCloudCommonParams", "cloudThickness", common.cloudThickness);

    compute_Atmosphere.setVariable("FogAtmosphericParams", "eye_direction", mainFar.compute_Params.eye_direction);
    compute_Atmosphere.setVariable("FogAtmosphericParams", "uv_offset", mainFar.compute_Params.uv_offset);
    compute_Atmosphere.setVariable("FogAtmosphericParams", "eye_position", mainFar.compute_Params.eye_position);
    compute_Atmosphere.setVariable("FogAtmosphericParams", "uv_scale", mainFar.compute_Params.uv_scale);
    compute_Atmosphere.setVariable("FogAtmosphericParams", "dx", mainFar.compute_Params.dx);
    compute_Atmosphere.setVariable("FogAtmosphericParams", "uv_pixSize", mainFar.compute_Params.uv_pixSize);
    compute_Atmosphere.setVariable("FogAtmosphericParams", "dy", mainFar.compute_Params.dy);
    compute_Atmosphere.setVariable("FogAtmosphericParams", "numSlices", mainFar.compute_Params.numSlices);
    compute_Atmosphere.setVariable("FogAtmosphericParams", "sliceZero", mainFar.compute_Params.sliceZero);
    compute_Atmosphere.setVariable("FogAtmosphericParams", "sliceMultiplier", mainFar.compute_Params.sliceMultiplier);
    compute_Atmosphere.setVariable("FogAtmosphericParams", "sliceStep", mainFar.compute_Params.sliceStep);

    compute_Atmosphere.setVariable("FogAtmosphericParams", "timer", mainFar.compute_Params.timer);
    compute_Atmosphere.setVariable("FogAtmosphericParams", "numFogLights", mainFar.compute_Params.numFogLights);
    compute_Atmosphere.setVariable("FogAtmosphericParams", "paddTime", mainFar.compute_Params.paddTime);

    compute_Atmosphere.setVariable("FogAtmosphericParams", "fogAltitude", mainFar.compute_Params.fogAltitude);
    compute_Atmosphere.setVariable("FogAtmosphericParams", "fogDensity", mainFar.compute_Params.fogDensity);
    compute_Atmosphere.setVariable("FogAtmosphericParams", "fogUVoffset", mainFar.compute_Params.fogUVoffset);
    compute_Atmosphere.setVariable("FogAtmosphericParams", "fogMip0Size", mainFar.compute_Params.fogMip0Size);
    compute_Atmosphere.setVariable("FogAtmosphericParams", "numFogVolumes", mainFar.compute_Params.numFogVolumes);

    compute_Atmosphere.setVariable("FogAtmosphericParams", "sunColourBeforeOzone", mainFar.compute_Params.sunColourBeforeOzone);
    compute_Atmosphere.setVariable("FogAtmosphericParams", "haze_Turbidity", mainFar.compute_Params.haze_Turbidity);
    compute_Atmosphere.setVariable("FogAtmosphericParams", "haze_Colour", mainFar.compute_Params.haze_Colour);
    compute_Atmosphere.setVariable("FogAtmosphericParams", "haze_AltitudeKm", mainFar.compute_Params.haze_AltitudeKm);
    compute_Atmosphere.setVariable("FogAtmosphericParams", "fog_Colour", mainFar.compute_Params.fog_Colour);
    compute_Atmosphere.setVariable("FogAtmosphericParams", "haze_BaseAltitudeKm", mainFar.compute_Params.haze_BaseAltitudeKm);
    compute_Atmosphere.setVariable("FogAtmosphericParams", "fog_AltitudeKm", mainFar.compute_Params.fog_AltitudeKm);
    compute_Atmosphere.setVariable("FogAtmosphericParams", "fog_BaseAltitudeKm", mainFar.compute_Params.fog_BaseAltitudeKm);
    compute_Atmosphere.setVariable("FogAtmosphericParams", "fog_Turbidity", mainFar.compute_Params.fog_Turbidity);
    compute_Atmosphere.setVariable("FogAtmosphericParams", "globalExposure", mainFar.compute_Params.globalExposure);
    compute_Atmosphere.setVariable("FogAtmosphericParams", "rain_Colour", mainFar.compute_Params.rain_Colour);
    compute_Atmosphere.setVariable("FogAtmosphericParams", "rainFade", mainFar.compute_Params.rainFade);
    compute_Atmosphere.setVariable("FogAtmosphericParams", "ozone_Density", mainFar.compute_Params.ozone_Density);
    compute_Atmosphere.setVariable("FogAtmosphericParams", "ozone_Colour", mainFar.compute_Params.ozone_Colour);
    compute_Atmosphere.setVariable("FogAtmosphericParams", "refraction", mainFar.compute_Params.refraction);

    compute_Atmosphere.setVariable("FogAtmosphericParams", "parabolicProjection", mainFar.compute_Params.parabolicProjection);
    compute_Atmosphere.setVariable("FogAtmosphericParams", "parabolicMapHalfSize", mainFar.compute_Params.parabolicMapHalfSize);
    compute_Atmosphere.setVariable("FogAtmosphericParams", "parabolicUpDown", mainFar.compute_Params.parabolicUpDown);

    compute_Atmosphere.setVariable("FogAtmosphericParams", "tmp_IBL_scale", mainFar.compute_Params.tmp_IBL_scale);

    compute_Atmosphere.setSampler("linearSampler", sampler_Trilinear);
    compute_Atmosphere.setSampler("clampSampler", sampler_Clamp);

    compute_Atmosphere.dispatch(_renderContext, mainFar.m_params.m_x / 8, mainFar.m_params.m_y / 8, 1);
}

void atmosphereAndFog::setSunDirection(atm_float3 dir)
{
    common.sun_direction = dir;
}

void atmosphereAndFog::setTerrainShadow(ew::Texture::SharedPtr shadow)
{
    compute_Atmosphere.setTexture("terrainShadow", shadow);
}

// Re-entry point for the cfd/smoke system, which is not part of this build -
// nothing calls the two feeders below. They address the flattened gCfd_*
// texture and constant names that compute_volumeFog.hlsli declares.
void atmosphereAndFog::setSMOKE(ew::Texture::SharedPtr textures[6][3])
{
    for (uint32_t lod = 0; lod < 6; lod++)
    {
        compute_Atmosphere.setTexture("gCfd_T_" + std::to_string(lod * 2 + 0), textures[lod][0]);
        compute_Atmosphere.setTexture("gCfd_T_" + std::to_string(lod * 2 + 1), textures[lod][1]);
    }
}

void atmosphereAndFog::setSmokeTime(atm_float4 lodOffsets[6][2], atm_float4 lodScales[6])
{
    for (uint32_t lod = 0; lod < 6; lod++)
    {
        compute_Atmosphere.setVariable("gCfdParams", "gCfd_offset_" + std::to_string(lod * 2 + 0), lodOffsets[lod][0]);
        compute_Atmosphere.setVariable("gCfdParams", "gCfd_offset_" + std::to_string(lod * 2 + 1), lodOffsets[lod][1]);

        compute_Atmosphere.setVariable("gCfdParams", "gCfd_scale_" + std::to_string(lod * 2 + 0), lodScales[lod]);
        compute_Atmosphere.setVariable("gCfdParams", "gCfd_scale_" + std::to_string(lod * 2 + 1), lodScales[lod]);
    }
}
