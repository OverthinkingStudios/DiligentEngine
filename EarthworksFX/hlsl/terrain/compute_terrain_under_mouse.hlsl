
#include "groundcover_defines.hlsli"
#include "terrainDefines.hlsli"
				


SamplerState gSampler;
RWStructuredBuffer<gpuTile> 			tiles;
RWStructuredBuffer<GC_feedback>			groundcover_feedback;
Texture2DArray<float>					gHeight;

Texture2D					gHDRBackbuffer;
Texture2D					gDepthBuffer;

cbuffer gConstants			// ??? wonder if this just works here, then we can skip structured buffer for it
{
	float3 mousePos;
	float3 mouseDir;
	float2 mouseCoords;
};


bool raySphere(float3 pos, float3 ray, float4 sphere)
{
	float3 R2 = sphere.xyz - pos;
	float dR = dot( normalize(ray), R2);
	if (dR > 0)
	{
		float3 perp = R2 - ray * dR;
	
		if( length(perp) < sphere.w) return true;
	}
	
	return false;
}


[numthreads(1, 1, 1)]		
void main(uint3 coord : SV_DispatchThreadId)
{
	groundcover_feedback[0].tum_idx = 0;
	//int t = coord.x;
    float hitDistance = 100000;

	//if (t<1001)		// FIXME hardcoded
    for (int t = 0; t < 1001; t++)
    {
        if ((tiles[t].flags >> 31))		// it is on the bottom level		//??? add a a multiply in somehwrere
        {
            float4 sphere;
            sphere.xyz = tiles[t].origin + float3(512, 2048, 512) * tiles[t].scale_1024;
            sphere.w = tiles[t].scale_1024 * 512.0 * 1.5; // so iets
			
            float3 R2 = sphere.xyz - mousePos;
            float dR = dot(normalize(mouseDir), R2);
            if (dR > 0)
            {
				
                float3 perp = R2 - mouseDir * dR;
				if( length(perp) < sphere.w)
				{
                    float3 P = mousePos + length(R2) * mouseDir; // Start at the center of the tile roughly
                    float distance = length(P - mousePos);
                    float3 uvw = (P - tiles[t].origin) / (tiles[t].scale_1024 * 1024);
                    float H = P.y - gHeight.Load(uint4(uvw.x * 256, uvw.z * 256, t, 0));
                    float step = tiles[t].scale_1024 * 16; // arbitrary, but 16 is 4 pixels at a time roughly, fine enough for this
                    bool bHit = false;
					
                    for (int i = 0; i < 20; i++)
                    {
                        if (!any(uvw.xz - saturate(uvw.xz)))
                        {
                            if ((abs(H) < step) && (distance < hitDistance))
                            {
                                hitDistance = distance;
                                groundcover_feedback[0].tum_idx = t;
                                groundcover_feedback[0].tum_Position = P;
								
								// now calculate the normal  - couple of points around us 4, 5 maybe
                                float uvStep = 0.5f / (tiles[t].scale_1024 * 1024);
                                uvw = (P - tiles[t].origin) / (tiles[t].scale_1024 * 1024);
                                uvw.y = uvw.z;
                                uvw.z = t;
                                float h0 = gHeight.SampleLevel(gSampler, uvw, 0).r;
                                float dx = gHeight.SampleLevel(gSampler, uvw + float3(uvStep, 0, 0), 0).r - gHeight.SampleLevel(gSampler, uvw - float3(uvStep, 0, 0), 0).r;
                                float dy = gHeight.SampleLevel(gSampler, uvw + float3(0, uvStep, 0), 0).r - gHeight.SampleLevel(gSampler, uvw - float3(0, uvStep, 0), 0).r;
								
								//float dx = gHeight.Load( uint4(uvw.x * 256 + 1, uvw.z * 256, tiles[t].index, 0)) - gHeight.Load( uint4(uvw.x * 256 - 1, uvw.z * 256, tiles[t].index, 0));
								//float dy = gHeight.Load( uint4(uvw.x * 256, uvw.z * 256, tiles[t].index, 0)) - gHeight.Load( uint4(uvw.x * 256 - 1, uvw.z * 256, tiles[t].index, 0));
								
                                float3 a = float3(1.0, dx, 0);
                                float3 b = -float3(0, dy, 1.0);
                                groundcover_feedback[0].tum_Normal = normalize(cross(a, b));
								//break;
                                i == 100000;
                            }
                        }
                        P += mouseDir * H; //(H / mouseDir.y) * 0.1;
                        distance = length(P - mousePos);
                        uvw = (P - tiles[t].origin) / (tiles[t].scale_1024 * 1024);
                        H = P.y - gHeight.Load(uint4(uvw.x * 256, uvw.z * 256, t, 0));
                    }
                }
            }
        }
    }
	
	
	// FIXME misuse this a little ;-)
	groundcover_feedback[0].colourUnderMouse = gHDRBackbuffer.Load( uint3(mouseCoords, 0) ).rgb;
}


