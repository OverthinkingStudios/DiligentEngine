

// A constant buffer with parameters that are common to both the fog and the cloud system





cbuffer FogCloudCommonParams : register(b0) {

	float3 sun_direction;
	float pad_fog1;

	float cloudBase;
	float cloudThickness;
	float2 paddcloudB;
}

