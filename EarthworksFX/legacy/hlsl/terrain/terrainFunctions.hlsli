


// this now assumes 256 maps so we pack to 8 but - Guess they could go into defines as well
uint pack_tile_pos(uint2 crd)
{
	return (crd.x << 8) + (crd.y & 0xff);
}

int2 unpack_tile_pos(uint V)
{
	return int2(V & 0x7f, (V>>7)&0x7f);
}
