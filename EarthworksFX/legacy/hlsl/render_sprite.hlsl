


static const int xOffset[4] = { -1, -1, 1, 1 };
static const int yOffset[4] = { 1, -1, 1, -1 };
//static const float xTex[4] = { 0.f, 0.f, 1.f, 1.f };
//static const float yTex[4] = { 0.f, 1.f, 0.f, 1.f };
static const float xTex[4] = { 0.1f, 0.1f, 0.9f, 0.9f };
static const float yTex[4] = { 0.1f, 0.9f, 0.1f, 0.9f };


SamplerState gSampler;
Texture2DArray gTex : register(t0);
Texture2DArray gNorm : register(t1);
Texture2DArray gTranclucent : register(t2);
TextureCube gCube;


cbuffer gConstantBuffer
{
    float4x4 view;
	float4x4 proj;
	float3 	eye;
	int		alpha_pass;
	uint 	start_index;
};

float2 vectorRotate(float2 vec, float sinAngle, float cosAngle)
{
    float2 result;
    result.x = vec.x * cosAngle - vec.y * sinAngle;
    result.y = vec.y * cosAngle + vec.x * sinAngle;
    return result;
}

struct vertex
{
	float3 pos;
	uint idx;		// [........][......FR][uuuuvvvv][dududvdv]
	float3 up;
	uint w;			//[wwwwwwww][wwwwwwww][ssssssss][aaaaaaaa]
};

struct VSOut
{
    float4 pos : SV_POSITION;
    float3 texCoords : TEXCOORD;
	float3 sun : TEXCOORD1;
	float4 ambientOcclusion : TEXCOORD2;
};

StructuredBuffer<vertex> VB;

// I am completely vertex shader limited
// See if we can move this to a compute shader, and do the discards, and projection code there

VSOut vsMain(uint vId : SV_VertexID, uint iId : SV_InstanceID)
{
    VSOut output;
	
	

	vertex V = VB[iId + start_index];
	float width = ((float)(V.w >> 16)) / 1024.0f*1;
	int bFade = (V.idx>>17)&0x1;
	
	float4 P1 = mul(float4(V.pos, 1), view);
	float4 P2 = mul(P1, proj);
	//float pixelWidthRatio = 2. / proj[0][0];
	//float pixelWidth = P2.w * pixelWidthRatio / width;
	float pixelWidth = width * proj[0][0] / P2.w;
	float alpha = 1;
	if (pixelWidth < 0.0003) alpha=0;
	if (bFade)
	{
		alpha = saturate((0.1 -pixelWidth ) * 15.1);
	}
	if (alpha==0) width = 0;

	
	output.pos = P2;
	output.texCoords = float3(0, 0, 0);
	output.sun = float3(0, 0, 0);
	output.ambientOcclusion = float4(0, 0, 0, 0);
	
	
	if (alpha>0)
	{
	
		// extract the vertex
		
		int mix = (V.idx>>16)&0x1;
		
		float u = (float)(( V.idx>>12 ) & 0xf ) / 16.0f;
		float v = (float)(( V.idx>>8 ) & 0xf ) / 16.0f;
		float du = (float)(( V.idx>>4 ) & 0xf ) / 16.0f;
		float dv = (float)(( V.idx ) & 0xf ) / 16.0f;
		float AO = (float)(V.w & 0xff) / 255.0f;
		float shadow = (float)((V.w >> 8) & 0xff) / 255.0f;
		
		float3 up = V.up;
		float3 dir = normalize(eye - V.pos);
		float3 right = normalize(cross(up, dir));
		float3 up2;
		if (mix)	up2 = cross(dir, right) * length(up);
		else 		up2 = up;
		
		
		float3 position = V.pos + (xOffset[vId] * right * 0.5 * width ) + (yOffset[vId] * up2 * 0.5);
		float4 viewPos = mul(float4(position, 1.f), view);

		float3 SUN = float3(0.33f, 0.37f, 0.05f);
		normalize(SUN);
		output.sun.x = dot(SUN, right);
		output.sun.y = dot(SUN, normalize(up2));
		output.sun.z = dot(SUN, dir);
		output.sun = normalize(output.sun);

		output.pos = mul(viewPos, proj);
		
		
		
		// texture coordinates ---------------------------------------------
		output.texCoords = float3(u + du*xTex[vId], v + dv*yTex[vId], 0);
		
		float ao = (float)(V.w & 0xff) / 255.0f;
		output.ambientOcclusion.r = ao*ao;													// AO
		output.ambientOcclusion.g = (float)((V.w >> 8) & 0xff) / 255.0f;	// shadows
		output.ambientOcclusion.b = alpha_pass;										// alpha pass
		output.ambientOcclusion.a = alpha;
		

	}
	
    return output;
}






#define _I_ao 			vOut.ambientOcclusion.r
#define _I_shadow 		vOut.ambientOcclusion.g
#define _I_alphaPass	vOut.ambientOcclusion.b
#define _I_alpha 		vOut.ambientOcclusion.a



float4 psMain(VSOut vOut) : SV_TARGET
{


	float4 samp = gTex.Sample(gSampler, vOut.texCoords); 
	
	
	samp.a *= _I_alpha;
	
	if (_I_alphaPass<0.5)
	{
		clip( samp.a - 0.910 );
		samp.a = 1.0;
	}
	else
	{
		clip( samp.a - 0.2 );
		clip( 0.91 - samp.a );
	}
	
	
	
	
	
	
	float4 norm = gNorm.Sample(gSampler, vOut.texCoords); 
	float4 trans = gTranclucent.Sample(gSampler, vOut.texCoords); 
	
	norm.x = (norm.x-0.5) * 2;
	norm.y = (norm.y-0.5) * 2;

	//norm = normalize(norm);
	
	float3 ambient;
	
	// ambient colour
	//float3 ambient =  gCube.SampleLevel(gSampler, norm.rgb, 6).rgb;
									//float3 ambient = gCube.SampleLevel(gSampler, float3(0,-1,0), 6).rgb;
	//ambient *=   _I_ao ;
		
	float3 sun_col = float3(1.0, 0.9, 0.7) * 2.5f;
	ambient = float3(0.11, 0.14, 0.15) * 2.2f;
	ambient *=   lerp( 0.5, 1.0, _I_ao );
	
	float dS = dot(norm.xyz, vOut.sun);
	float dT = 0;
	if (dS<0) dT = 0.93;
										//float3 sun = saturate(  saturate() + (trans.r*0.2)  )  * vOut.ambientOcclusion.g;//* vOut.ambientOcclusion.r;
	float3 sun = 0.4 * saturate(  saturate(dS) + trans.r*dT )  * _I_shadow;
	//samp.rgb *=  (sun*sun_col + ambient ) * 12;
	
	
	// specular
	//float3 reflect =  shAttr.E - 2 * dot(shAttr.E, shAttr.N) * shAttr.N;
	
	// Phase function  https://cs.dartmouth.edu/wjarosz/publications/dissertation/chapter4.pdf
	float k = 0.8 - trans.r*1.6;
	float costheta = dot(norm.xyz, vOut.sun);
	float t1 = (1+k*costheta);
	float pS = (1 - k*k) / (12.57*t1*t1);
	
	samp.rgb *=  (pS*sun_col + ambient ) * 2;
	
	float back = saturate(costheta) * saturate(1.5 - trans.r);
	float omni = 0.3 * trans.r * _I_shadow;
	float forward = saturate(-costheta) * trans.r;
	
	float4 colour = float4((back + (omni + forward) )*1.0*lerp( 0.0, 1.0, _I_shadow ) + ambient , 1) * samp; 
	colour.g += forward * _I_shadow * samp.g * 0.02 ;
	
	return colour;
}
