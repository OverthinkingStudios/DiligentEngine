#pragma once
#include "Falcor.h"
#include "computeShader.h"
using namespace Falcor;


struct  fogCloudCommonParams {

    float3 sun_direction;
    float pad_fog1;

    float cloudBase = 5000.f;
    float cloudThickness = 100.f; 
    float2 paddcloudB;
};

struct   fogAtmosphericParams {
    float3 eye_direction;
    float uv_offset;

    float3 eye_position;
    float uv_scale;

    float3 dx;
    float uv_pixSize;

    float3 dy;
    uint numSlices;

    float sliceZero;
    float sliceMultiplier; // distance to the next slice (multiply with current slice)
    float sliceStep;	   // size of this slice (multiply with current slice)
    float slicePADD2;

    float timer;
    int numFogLights;
    float2 paddTime;

    // possible temporary near density
    float2 fogAltitude; //(base, noise)	??? Not sure if base will be sued in the end, get from Fog texture
    float2 fogDensity;	//(base, noise)
    float2 fogUVoffset; // animate to simulate wind
    float fogMip0Size;	// Size of a single pixel on MIP 0, since compute shaders have to explicitely compute mip level
    float numFogVolumes;

    // Block for the new atmosphere
    // ############################################################################################################################
    float3 sunColourBeforeOzone{ 1.0f, 1.0f, 1.0f };
    float haze_Turbidity = 1.5f;

    float3 haze_Colour{ 0.95, 0.95, 0.95 };
    float haze_AltitudeKm = 1.2f;

    float3 fog_Colour{ 0.95, 0.95, 0.95 };
    float haze_BaseAltitudeKm = 0;

    float fog_AltitudeKm = 0.29f;
    float fog_BaseAltitudeKm = 0.85f;
    float fog_Turbidity = 8.5f;
    float globalExposure = 1.0f / 20000.0f;

    float3 rain_Colour{ 0.1, 0.1, 0.1 };
    float rainFade = 1.0f;

    float ozone_Density = 0.7f;
    float3 ozone_Colour{ 0.65, 1.6, 0.085 };

    float4 refraction{ 0.022, 0.027, 0.03, 0 };
    // ############################################################################################################################

    // info for parabolic projection
    float parabolicProjection;
    float parabolicMapHalfSize;
    float parabolicUpDown;
    float parabolicPADD;

    // Temporary block - debug sliders for atmosphere
    // ############################################################################################################################
    float tmp_IBL_scale;
    float tmp_B;
    float tmp_C;
    float tmp_D;
    // ############################################################################################################################
};


struct FogVolumeParameters {
    int m_x;
    int m_y;
    int m_z;
    Falcor::ResourceFormat format;
    bool m_bRGBOut;
    bool bParabolic;
    float _near = 50.f;
    float _far = 20000.f;
};


class FogVolume {

public:
    void onLoad(FogVolumeParameters params);
    void updateFogparameters(fogAtmosphericParams params);
    void setCamera(Camera::SharedPtr _camera);
    //void setLights(const std::vector<LightRenderData>& L);

public:
    FogVolumeParameters m_params;
    float m_logEnd;	  // (k-1 / k) / log(far) - for exponential slices
    float m_oneOverK; // 1.5 / k    - the 0.5 makes up for half pixel offsets

    float m_SliceStep; // Distance between two slices - multiplier
    float m_SliceZero; // m_start / m_StepMultiplier   - the first slice is closer than the near plane

    Texture::SharedPtr inscatter;
    Texture::SharedPtr inscatter_cloudbase;
    Texture::SharedPtr inscatter_sky;

    Texture::SharedPtr outscatter;
    Texture::SharedPtr outscatter_cloudbase;
    Texture::SharedPtr outscatter_sky;

    // parameters for compute shader to generate fog
    fogAtmosphericParams compute_Params;
    //VolumeFogLight compute_Lights[RENDERER_MAX_FOG_LIGHTS];
};


class atmosphereAndFog{
public:
    void onLoad(RenderContext* _renderContext, FILE* _logfile);
    void computeSunInAtmosphere(RenderContext* _renderContext);
    void computeVolumetric(RenderContext* _renderContext);
    void setSunDirection(float3 dir);
    void setTerrainShadow(Texture::SharedPtr shadow);
    void setSMOKE(Texture::SharedPtr textures[6][3]);
    void setSmokeTime(float4 lodOffsets[6][2], float4 lodScales[6]);
    FogVolume& getFar() { return mainFar; }
    FogVolume& getNear() { return mainNear; }
    FogVolume& getParabolic() { return parabolicFar; }
    

public:
    Texture::SharedPtr  sunlightTexture = nullptr;
    fogAtmosphericParams params;

private:
    computeShader		compute_sunSlice;
    
    Texture::SharedPtr  phaseFunction = nullptr;
    fogCloudCommonParams common;
    

    FogVolume mainNear;
    FogVolume mainFar;
    FogVolume parabolicFar;
    computeShader		compute_Atmosphere;

    Sampler::SharedPtr			sampler_Trilinear;
    Sampler::SharedPtr			sampler_Clamp;
};
