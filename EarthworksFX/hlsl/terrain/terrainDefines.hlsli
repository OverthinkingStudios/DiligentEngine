#pragma once

#define tile_cs_ThreadSize 8
#define tile_cs_ThreadSize_Generate 8

#define tile_numPixels 256
#define tile_BorderPixels 4
#define tile_InnerPixels 248
#define tile_toBorder 256.0f/248.0f

#define numVertPerTile  32768

// 16384 allows for a plant every 2x2 pixels, and roughly 128 Mb (16384 X 8 byte struct. 1024 tiles)
#define numQuadsPerTile  32768

// 4096 allows for a plant every 4x4 pixels, and roughly 32 Mb (4096 X 8 byte struct. 1024 tiles)
#define numPlantsPerTile  4096


struct Terrain_vertex
{
	uint	idx;
	float	hgt;
};


struct centerFeedback {
	float min;
	float max;
	float A;
	float B;
};
