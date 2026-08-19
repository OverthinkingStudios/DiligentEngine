

#include "compute_bc6h_functions.hlsli"
#include "terrainDefines.hlsli"

SamplerState 		gSampler;
Texture2D<float3> 	gSource;
RWTexture2D<uint4> 	gOutput;



cbuffer gConstants			// ??? wonder if this just works here, then we can skip structured buffer for it
{
	float4x4 view;
	float4x4 proj;
	float3 	eye;
	int		alpha_pass;
	uint 	start_index;
};







[numthreads(tile_cs_ThreadSize, tile_cs_ThreadSize, 1)]			
void main(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadId)
{
	uint3 crd = ((groupId.xyz * tile_cs_ThreadSize) + groupThreadId.xyz) * 4;
	crd.z = 0;
	uint4 block     = uint4( 0, 0, 0, 0 );
    float blockMSLE = 0.0f;
	
	// 0 1 2 3
    // 4 5 6 7
    // 8 9 10 11
    // 12 13 14 15
	float3 texels[ 16 ];
	for (uint y=0; y<4; y++)
	{
		for (uint x=0; x<4; x++)
		{
			texels[y*4+x] = gSource.Load(crd + uint3(x, y, 0));
		}
	}
	
	EncodeP1( block, blockMSLE, texels );
	/*
#ifdef QUALITY
    for ( uint i = 0; i < 32; ++i )
    {
        EncodeP2Pattern( block, blockMSLE, i, texels );
    }
#endif
*/

	
	gOutput[crd.xy/4] = block;
	/*
	if ( blockMSLE < 10.5 )
	{
		gOutput[crd.xy/4] = block;
	}
	else
	{
		for ( uint i = 0; i < 32; ++i )
		{
			EncodeP2Pattern( block, blockMSLE, i, texels );
			//if ( blockMSLE < 0.05 ) break;
		}
		gOutput[crd.xy/4] = block;
	}
	*/
}











/*
uint4 PSMain( PSInput input ) : SV_Target
{
    // gather texels for current 4x4 block
    // 0 1 2 3
    // 4 5 6 7
    // 8 9 10 11
    // 12 13 14 15
    float2 uv       = input.m_pos.xy * TextureSizeRcp * 4.0f - TextureSizeRcp;
    float2 block0UV = uv;
    float2 block1UV = uv + float2( 2.0f * TextureSizeRcp.x, 0.0f );
    float2 block2UV = uv + float2( 0.0f, 2.0f * TextureSizeRcp.y );
    float2 block3UV = uv + float2( 2.0f * TextureSizeRcp.x, 2.0f * TextureSizeRcp.y );
    float4 block0X  = SrcTexture.GatherRed( PointSampler, block0UV );
    float4 block1X  = SrcTexture.GatherRed( PointSampler, block1UV );
    float4 block2X  = SrcTexture.GatherRed( PointSampler, block2UV );
    float4 block3X  = SrcTexture.GatherRed( PointSampler, block3UV );
    float4 block0Y  = SrcTexture.GatherGreen( PointSampler, block0UV );
    float4 block1Y  = SrcTexture.GatherGreen( PointSampler, block1UV );
    float4 block2Y  = SrcTexture.GatherGreen( PointSampler, block2UV );
    float4 block3Y  = SrcTexture.GatherGreen( PointSampler, block3UV );
    float4 block0Z  = SrcTexture.GatherBlue( PointSampler, block0UV );
    float4 block1Z  = SrcTexture.GatherBlue( PointSampler, block1UV );
    float4 block2Z  = SrcTexture.GatherBlue( PointSampler, block2UV );
    float4 block3Z  = SrcTexture.GatherBlue( PointSampler, block3UV );

    float3 texels[ 16 ];
    texels[ 0 ]     = float3( block0X.w, block0Y.w, block0Z.w );
    texels[ 1 ]     = float3( block0X.z, block0Y.z, block0Z.z );
    texels[ 2 ]     = float3( block1X.w, block1Y.w, block1Z.w );
    texels[ 3 ]     = float3( block1X.z, block1Y.z, block1Z.z );
    texels[ 4 ]     = float3( block0X.x, block0Y.x, block0Z.x );
    texels[ 5 ]     = float3( block0X.y, block0Y.y, block0Z.y );
    texels[ 6 ]     = float3( block1X.x, block1Y.x, block1Z.x );
    texels[ 7 ]     = float3( block1X.y, block1Y.y, block1Z.y );
    texels[ 8 ]     = float3( block2X.w, block2Y.w, block2Z.w );
    texels[ 9 ]     = float3( block2X.z, block2Y.z, block2Z.z );
    texels[ 10 ]    = float3( block3X.w, block3Y.w, block3Z.w );
    texels[ 11 ]    = float3( block3X.z, block3Y.z, block3Z.z );
    texels[ 12 ]    = float3( block2X.x, block2Y.x, block2Z.x );
    texels[ 13 ]    = float3( block2X.y, block2Y.y, block2Z.y );
    texels[ 14 ]    = float3( block3X.x, block3Y.x, block3Z.x );
    texels[ 15 ]    = float3( block3X.y, block3Y.y, block3Z.y );

    uint4 block     = uint4( 0, 0, 0, 0 );
    float blockMSLE = 0.0f;

    EncodeP1( block, blockMSLE, texels );
#ifdef QUALITY
    for ( uint i = 0; i < 32; ++i )
    {
        EncodeP2Pattern( block, blockMSLE, i, texels );
    }
#endif

    return block;
}
*/