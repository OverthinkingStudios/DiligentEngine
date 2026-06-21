



uint pack_pos(uint x, uint z, uint uHgt, uint rnd)
{
	return (x << 24) + (z << 14) + ((rnd & 0x0c03) << 12) + (uHgt &0xfff);
}

uint repack_pos(uint x, uint z, uint uHgt, uint rnd)							// only adds 1 extra random bit
{
	return (x << 23) + (z << 13) + ((rnd & 0x0401) << 12) + (uHgt &0xfff);
}

uint pack_SRTI(uint bScale, uint rotation, uint tile_index, uint index)
{
	return (bScale << 31) + ((rotation & 0x1ff) << 22) + ((tile_index & 0x3ff) << 11) + (index & 0x7ff);
}


float SCALE(uint srti)
{
	return 1.0f + ((srti>>31) * (128.0 - ((srti>>22)&0xff)) / 256.0);
}

float ROTATION(uint srti)
{
	return ((srti >> 22) & 0x1ff) * 0.012271846303085f;		// 512 to radians magfic number
}

float2 ROTATIONxy(uint srti)
{
    float2 rot;
    sincos( ((srti >> 22) & 0x1ff) * 0.012271846303085f, rot.x, rot.y );		// 512 to radians magfic number
    return rot;
}

uint PLANT_INDEX(uint srti)
{
	return srti & 0x7ff;
}

uint TILE_INDEX(uint srti)
{
	return (srti >> 11) & 0x7ff;
}


float3 unpack_pos(uint pos, float3 orig, float scale)
{
	uint x = pos >> 22 & 0x3ff;
	uint z = pos >> 12 & 0x3ff;
	uint y = pos & 0xfff;
	return float3(x, y, z) * scale * float3( 1.032258, 1,  1.032258) + orig;
}


		

// just swap the tile index out
uint repack_SRTI(uint srti, uint tile_index)
{
	return (srti & 0xffc007ff) + ((tile_index & 0x3ff) << 11);
}


uint replace_SRTI_index(uint srti, uint index)
{
	return (srti & 0xfffff800) + (index & 0x3ff);
}

/*
uint repack_XYZ(uint xyz, uint rnd, uint uHgt)
{
	uint ans = (xyz&0x7fdff000) << 1;				// remove most significantr bits X, Z and remove Y
	ans += (rnd&0x401) << 12;						// add two random bits in
	ans += (uHgt &0xfff);	
	return ans;
}
*/
