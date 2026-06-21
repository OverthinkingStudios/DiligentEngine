 #pragma once
 
//#include "gpuLights_defines.hlsli"

// so include anything this far behind a light as well ??? play with it, significantly large is probably fine
#define light_bias 5.0




bool light_intersect_Volume(float3 pos, float radius, gpu_LIGHT light )
{
	float3 lightPos = pos - light.position;
	float R = length(lightPos);
	if ( R > (radius + light.radius) ) return false;
	return true;
/*
	//float3 p_R = pos - (light.position - light.direction * radius);
	//float f = dot( light.direction, normalize(p_R));
	float f = dot( light.direction, normalize(lightPos));
	
	
	if (R < (radius + light.radius/4.0) && f > 0.6 )
	{
		return true;
	}
	
	return false;
    */
}



/*
	does a fast intersect test and returns the light value at the poition
	(0, 0, 0) means a miss, does not even need to test, just add it, could test with all()
	radius is the world size of the point we are testing
*/
bool light_intersect(float3 pos, float radius, gpu_LIGHT light )
{
	float R = length(pos - light.position);
	
	float3 p_R = pos - (light.position - light.direction * radius * 1.2);
	float f = dot( light.direction, normalize(p_R));
	
	
	//if (R < (radius + light.radius) && f > 0.5 )
	if (R < (radius + light.radius) && (f > 0.6) )
	{
		return true;
	}
	
	return false;
}


		
		
float3 light_intensity(float3 pos, float radius, gpu_LIGHT light )
{

	float3 colour = float3(0, 0, 0);


	//float4 lightPos = mul(float4(pos, 1), light.invMat);
	float3 lightPos = pos - light.position;
	float R = length(lightPos);
	
	float3 p_R = pos - (light.position - light.direction * radius * 0);
	
	// scale Y axis
	p_R.y *= 1.1;
	float3 scaleDir = normalize(p_R);
	float f = dot( light.direction, scaleDir);
	//float f = dot( light.direction, normalize(lightPos));
	
	if ((R < radius + light.radius) && (f > 0.6))
	{
		float R2 = R * R;
		float3 dir = normalize(lightPos.xyz);
		float strenth = pow( saturate( f ), light.power );
		colour += light.colour * strenth  / R2 ;
	}
	
	
	return colour;
}


int getNumLights( int4 lightIndex[4] )
{
	return lightIndex[0][0] >> 24;
}

int getLightIndex( int4 lightIndex[4], int i )
{
	int y = i >> 4;
	int x = (i % 16) >> 2;
	int shift = 8 * (i%4);
	return ( lightIndex[y][x] >> shift ) & 0xff;
}



