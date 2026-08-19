// Diligent/glslang: bind lookup tables as separate globals (no resource arrays).

RWStructuredBuffer<tileLookupStruct> viewRenderData_terrainLookup_0;
RWStructuredBuffer<tileLookupStruct> viewRenderData_terrainLookup_1;
RWStructuredBuffer<tileLookupStruct> viewRenderData_terrainLookup_2;
RWStructuredBuffer<tileLookupStruct> viewRenderData_terrainLookup_3;
RWStructuredBuffer<tileLookupStruct> viewRenderData_terrainLookup_4;
RWStructuredBuffer<tileLookupStruct> viewRenderData_terrainLookup_5;
RWStructuredBuffer<tileLookupStruct> viewRenderData_terrainLookup_6;
RWStructuredBuffer<tileLookupStruct> viewRenderData_terrainLookup_7;
RWStructuredBuffer<tileLookupStruct> viewRenderData_terrainLookup_8;
RWStructuredBuffer<tileLookupStruct> viewRenderData_terrainLookup_9;
RWStructuredBuffer<tileLookupStruct> viewRenderData_terrainLookup_10;
RWStructuredBuffer<tileLookupStruct> viewRenderData_terrainLookup_11;
RWStructuredBuffer<tileLookupStruct> viewRenderData_terrainLookup_12;
RWStructuredBuffer<tileLookupStruct> viewRenderData_terrainLookup_13;
RWStructuredBuffer<tileLookupStruct> viewRenderData_terrainLookup_14;
RWStructuredBuffer<tileLookupStruct> viewRenderData_terrainLookup_15;
RWStructuredBuffer<tileLookupStruct> viewRenderData_terrainLookup_16;
RWStructuredBuffer<tileLookupStruct> viewRenderData_terrainLookup_17;

RWStructuredBuffer<tileLookupStruct> viewRenderData_plantLookup_0;
RWStructuredBuffer<tileLookupStruct> viewRenderData_plantLookup_1;
RWStructuredBuffer<tileLookupStruct> viewRenderData_plantLookup_2;
RWStructuredBuffer<tileLookupStruct> viewRenderData_plantLookup_3;
RWStructuredBuffer<tileLookupStruct> viewRenderData_plantLookup_4;
RWStructuredBuffer<tileLookupStruct> viewRenderData_plantLookup_5;
RWStructuredBuffer<tileLookupStruct> viewRenderData_plantLookup_6;
RWStructuredBuffer<tileLookupStruct> viewRenderData_plantLookup_7;
RWStructuredBuffer<tileLookupStruct> viewRenderData_plantLookup_8;
RWStructuredBuffer<tileLookupStruct> viewRenderData_plantLookup_9;
RWStructuredBuffer<tileLookupStruct> viewRenderData_plantLookup_10;
RWStructuredBuffer<tileLookupStruct> viewRenderData_plantLookup_11;
RWStructuredBuffer<tileLookupStruct> viewRenderData_plantLookup_12;
RWStructuredBuffer<tileLookupStruct> viewRenderData_plantLookup_13;
RWStructuredBuffer<tileLookupStruct> viewRenderData_plantLookup_14;
RWStructuredBuffer<tileLookupStruct> viewRenderData_plantLookup_15;
RWStructuredBuffer<tileLookupStruct> viewRenderData_plantLookup_16;
RWStructuredBuffer<tileLookupStruct> viewRenderData_plantLookup_17;

RWStructuredBuffer<tileLookupStruct> viewRenderData_quadLookup_0;
RWStructuredBuffer<tileLookupStruct> viewRenderData_quadLookup_1;
RWStructuredBuffer<tileLookupStruct> viewRenderData_quadLookup_2;
RWStructuredBuffer<tileLookupStruct> viewRenderData_quadLookup_3;
RWStructuredBuffer<tileLookupStruct> viewRenderData_quadLookup_4;
RWStructuredBuffer<tileLookupStruct> viewRenderData_quadLookup_5;
RWStructuredBuffer<tileLookupStruct> viewRenderData_quadLookup_6;
RWStructuredBuffer<tileLookupStruct> viewRenderData_quadLookup_7;
RWStructuredBuffer<tileLookupStruct> viewRenderData_quadLookup_8;
RWStructuredBuffer<tileLookupStruct> viewRenderData_quadLookup_9;
RWStructuredBuffer<tileLookupStruct> viewRenderData_quadLookup_10;
RWStructuredBuffer<tileLookupStruct> viewRenderData_quadLookup_11;
RWStructuredBuffer<tileLookupStruct> viewRenderData_quadLookup_12;
RWStructuredBuffer<tileLookupStruct> viewRenderData_quadLookup_13;
RWStructuredBuffer<tileLookupStruct> viewRenderData_quadLookup_14;
RWStructuredBuffer<tileLookupStruct> viewRenderData_quadLookup_15;
RWStructuredBuffer<tileLookupStruct> viewRenderData_quadLookup_16;
RWStructuredBuffer<tileLookupStruct> viewRenderData_quadLookup_17;

void StoreTerrainLookup(uint view, uint idx, tileLookupStruct val)
{
    switch (view)
    {
    case 0:  viewRenderData_terrainLookup_0[idx] = val; break;
    case 1:  viewRenderData_terrainLookup_1[idx] = val; break;
    case 2:  viewRenderData_terrainLookup_2[idx] = val; break;
    case 3:  viewRenderData_terrainLookup_3[idx] = val; break;
    case 4:  viewRenderData_terrainLookup_4[idx] = val; break;
    case 5:  viewRenderData_terrainLookup_5[idx] = val; break;
    case 6:  viewRenderData_terrainLookup_6[idx] = val; break;
    case 7:  viewRenderData_terrainLookup_7[idx] = val; break;
    case 8:  viewRenderData_terrainLookup_8[idx] = val; break;
    case 9:  viewRenderData_terrainLookup_9[idx] = val; break;
    case 10: viewRenderData_terrainLookup_10[idx] = val; break;
    case 11: viewRenderData_terrainLookup_11[idx] = val; break;
    case 12: viewRenderData_terrainLookup_12[idx] = val; break;
    case 13: viewRenderData_terrainLookup_13[idx] = val; break;
    case 14: viewRenderData_terrainLookup_14[idx] = val; break;
    case 15: viewRenderData_terrainLookup_15[idx] = val; break;
    case 16: viewRenderData_terrainLookup_16[idx] = val; break;
    case 17: viewRenderData_terrainLookup_17[idx] = val; break;
    }
}

void StoreQuadLookup(uint view, uint idx, tileLookupStruct val)
{
    switch (view)
    {
    case 0:  viewRenderData_quadLookup_0[idx] = val; break;
    case 1:  viewRenderData_quadLookup_1[idx] = val; break;
    case 2:  viewRenderData_quadLookup_2[idx] = val; break;
    case 3:  viewRenderData_quadLookup_3[idx] = val; break;
    case 4:  viewRenderData_quadLookup_4[idx] = val; break;
    case 5:  viewRenderData_quadLookup_5[idx] = val; break;
    case 6:  viewRenderData_quadLookup_6[idx] = val; break;
    case 7:  viewRenderData_quadLookup_7[idx] = val; break;
    case 8:  viewRenderData_quadLookup_8[idx] = val; break;
    case 9:  viewRenderData_quadLookup_9[idx] = val; break;
    case 10: viewRenderData_quadLookup_10[idx] = val; break;
    case 11: viewRenderData_quadLookup_11[idx] = val; break;
    case 12: viewRenderData_quadLookup_12[idx] = val; break;
    case 13: viewRenderData_quadLookup_13[idx] = val; break;
    case 14: viewRenderData_quadLookup_14[idx] = val; break;
    case 15: viewRenderData_quadLookup_15[idx] = val; break;
    case 16: viewRenderData_quadLookup_16[idx] = val; break;
    case 17: viewRenderData_quadLookup_17[idx] = val; break;
    }
}

void StorePlantLookup(uint view, uint idx, tileLookupStruct val)
{
    switch (view)
    {
    case 0:  viewRenderData_plantLookup_0[idx] = val; break;
    case 1:  viewRenderData_plantLookup_1[idx] = val; break;
    case 2:  viewRenderData_plantLookup_2[idx] = val; break;
    case 3:  viewRenderData_plantLookup_3[idx] = val; break;
    case 4:  viewRenderData_plantLookup_4[idx] = val; break;
    case 5:  viewRenderData_plantLookup_5[idx] = val; break;
    case 6:  viewRenderData_plantLookup_6[idx] = val; break;
    case 7:  viewRenderData_plantLookup_7[idx] = val; break;
    case 8:  viewRenderData_plantLookup_8[idx] = val; break;
    case 9:  viewRenderData_plantLookup_9[idx] = val; break;
    case 10: viewRenderData_plantLookup_10[idx] = val; break;
    case 11: viewRenderData_plantLookup_11[idx] = val; break;
    case 12: viewRenderData_plantLookup_12[idx] = val; break;
    case 13: viewRenderData_plantLookup_13[idx] = val; break;
    case 14: viewRenderData_plantLookup_14[idx] = val; break;
    case 15: viewRenderData_plantLookup_15[idx] = val; break;
    case 16: viewRenderData_plantLookup_16[idx] = val; break;
    case 17: viewRenderData_plantLookup_17[idx] = val; break;
    }
}
