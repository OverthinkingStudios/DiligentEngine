#pragma once

// ---------------------------------------------------------------------------
// Fog volumes and the atmospheric-scattering parameter blocks shared with the
// HLSL side.
//
// The math oddities in this subsystem are CORRECT and deliberate:
// atan(0.5*h/f) WITHOUT the factor 2 (it wants the half angle), the
// acumulateFog in/out asymmetry on the shader side, and the setCamera ordering
// quirk (compute_Params.slice* are copied from the PREVIOUS call's m_Slice*
// values - steady-state identical, do not reorder).
// ---------------------------------------------------------------------------

#include "ewTypes.h"
#include "ewCamera.h"
#include "ewGpuContext.h"
#include "ewResources.h"
#include "ewShader.h"

// Local aliases for the types shared with HLSL, so this header stands on its
// own instead of depending on terrain.h being included first.
using atm_float2 = glm::vec2;
using atm_float3 = glm::vec3;
using atm_float4 = glm::vec4;


struct  fogCloudCommonParams {

    // Every member of this struct and of fogAtmosphericParams below is
    // explicitly zero-initialized. Nothing writes most of them before they
    // reach the fog compute, and the failure mode is silent rather than loud:
    // a non-zero parabolicProjection, say, flips the compute into its
    // parabolic path.
    atm_float3 sun_direction{ 0, 0, 0 };
    float pad_fog1 = 0;

    float cloudBase = 5000.f;
    float cloudThickness = 100.f;
    atm_float2 paddcloudB{ 0, 0 };
};

struct   fogAtmosphericParams {
    atm_float3 eye_direction{ 0, 0, 0 };
    float uv_offset = 0;

    atm_float3 eye_position{ 0, 0, 0 };
    float uv_scale = 0;

    atm_float3 dx{ 0, 0, 0 };
    float uv_pixSize = 0;

    atm_float3 dy{ 0, 0, 0 };
    uint32_t numSlices = 0;

    float sliceZero = 0;
    float sliceMultiplier = 0; // distance to the next slice (multiply with current slice)
    float sliceStep = 0;	   // size of this slice (multiply with current slice)
    // TODO: never set and never uploaded - only sliceStep, sliceMultiplier and
    // sliceZero are sent. Harmless as layout filler, but check the HLSL side does
    // not expect four floats written here.
    float slicePADD2 = 0;

    float timer = 0;
    int numFogLights = 0;
    atm_float2 paddTime{ 0, 0 };

    // possible temporary near density
    atm_float2 fogAltitude{ 0, 0 }; //(base, noise)	??? Not sure if base will be sued in the end, get from Fog texture
    atm_float2 fogDensity{ 0, 0 };	//(base, noise)
    atm_float2 fogUVoffset{ 0, 0 }; // animate to simulate wind
    float fogMip0Size = 0;	// Size of a single pixel on MIP 0, since compute shaders have to explicitely compute mip level
    float numFogVolumes = 0;

    // The member-initializer defaults below ARE the operative sky parameters -
    // nothing writes them at runtime, there is no GUI for them. Change a value
    // here and you change the sky.
    // ############################################################################################################################
    atm_float3 sunColourBeforeOzone{ 1.0f, 1.0f, 1.0f };
    float haze_Turbidity = 1.5f;

    atm_float3 haze_Colour{ 0.95f, 0.95f, 0.95f };
    float haze_AltitudeKm = 1.2f;

    atm_float3 fog_Colour{ 0.95f, 0.95f, 0.95f };
    float haze_BaseAltitudeKm = 0;

    float fog_AltitudeKm = 0.29f;
    float fog_BaseAltitudeKm = 0.85f;
    float fog_Turbidity = 8.5f;
    float globalExposure = 1.0f / 20000.0f;

    atm_float3 rain_Colour{ 0.1f, 0.1f, 0.1f };
    float rainFade = 1.0f;

    float ozone_Density = 0.7f;
    atm_float3 ozone_Colour{ 0.65f, 1.6f, 0.085f };

    atm_float4 refraction{ 0.022f, 0.027f, 0.03f, 0 };
    // ############################################################################################################################

    // info for parabolic projection
    float parabolicProjection = 0;
    float parabolicMapHalfSize = 0;
    float parabolicUpDown = 0;
    float parabolicPADD = 0;

    // Temporary block - debug sliders for atmosphere
    // ############################################################################################################################
    float tmp_IBL_scale = 0;
    float tmp_B = 0;
    float tmp_C = 0;
    float tmp_D = 0;
    // ############################################################################################################################
};


struct FogVolumeParameters {
    int m_x;
    int m_y;
    int m_z;
    Diligent::TEXTURE_FORMAT format;
    bool m_bRGBOut;
    bool bParabolic;
    float _near = 50.f;
    float _far = 20000.f;
};


class FogVolume {

public:
    /// _allocateTextures = false stores the parameters but skips the texture
    /// allocations - the step-6 gate for the DORMANT volumes (mainNear +
    /// parabolicFar, never computed or sampled). Scalar consumers
    /// (m_params/m_logEnd/m_oneOverK) are unaffected.
    void onLoad(FogVolumeParameters params, bool _allocateTextures = true);
    void updateFogparameters(fogAtmosphericParams params);
    void setCamera(ew::Camera::SharedPtr _camera);

public:
    FogVolumeParameters m_params;
    float m_logEnd = 0;	  // (k-1 / k) / log(far) - for exponential slices
    float m_oneOverK = 0; // 1.0 / k. Sometimes documented as 1.5/k ("the 0.5
                          // makes up for half pixel offsets"), but the shader
                          // is tuned against the 1.0/k the code computes.

    float m_SliceStep = 0; // Distance between two slices - multiplier
    float m_SliceZero = 0; // m_start / m_StepMultiplier   - the first slice is closer than the near plane
    // The four members above are zero-initialized because the first setCamera()
    // copies them into compute_Params BEFORE recomputing them, so frame 1
    // legitimately reads zeros rather than garbage.

    ew::Texture::SharedPtr inscatter;
    ew::Texture::SharedPtr inscatter_cloudbase;
    ew::Texture::SharedPtr inscatter_sky;

    ew::Texture::SharedPtr outscatter;
    ew::Texture::SharedPtr outscatter_cloudbase;
    ew::Texture::SharedPtr outscatter_sky;

    // parameters for compute shader to generate fog
    fogAtmosphericParams compute_Params;
};


class atmosphereAndFog{
public:
    void onLoad(ew::GpuContext* _renderContext);
    void computeSunInAtmosphere(ew::GpuContext* _renderContext);
    void computeVolumetric(ew::GpuContext* _renderContext);
    void setSunDirection(atm_float3 dir);
    void setTerrainShadow(ew::Texture::SharedPtr shadow);
    void setSMOKE(ew::Texture::SharedPtr textures[6][3]);
    void setSmokeTime(atm_float4 lodOffsets[6][2], atm_float4 lodScales[6]);
    FogVolume& getFar() { return mainFar; }
    FogVolume& getNear() { return mainNear; }
    FogVolume& getParabolic() { return parabolicFar; }


public:
    ew::Texture::SharedPtr  sunlightTexture = nullptr;
    fogAtmosphericParams params;

private:
    ew::computeShader		compute_sunSlice;

    ew::Texture::SharedPtr  phaseFunction = nullptr;
    fogCloudCommonParams common;


    FogVolume mainNear;
    FogVolume mainFar;
    FogVolume parabolicFar;
    ew::computeShader		compute_Atmosphere;

    ew::Sampler::SharedPtr			sampler_Trilinear;
    ew::Sampler::SharedPtr			sampler_Clamp;
};
