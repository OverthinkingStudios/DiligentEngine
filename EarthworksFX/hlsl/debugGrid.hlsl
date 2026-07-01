// Debug orientation / movement aids for the EarthworksFX -> DiligentEngine bring-up.
//
// Two independent primitives, each generated entirely from SV_VertexID (no vertex or
// index buffer required) and selected by `drawMode`:
//
//   drawMode 0  GLOBE        A latitude/longitude sphere centered on the camera.
//                            Renders orientation / rotation. Drawn INTO the HDR FBO
//                            with depth-testing enabled so terrain occludes it - the
//                            terrain silhouette then "cuts a hole" in the globe, which
//                            is how we can see terrain geometry even when it shades black.
//
//   drawMode 1  GROUND GRID  A world-anchored grid on the ground plane covering the
//                            whole terrain area [areaMin.xz .. areaMax.xz]:
//                              * the area boundary (outer edges, bright),
//                              * a regular grid every `kmSpacing` metres (1 km),
//                              * the world X / Z axes highlighted.
//                            Drawn ON TOP (depth disabled) so the layout is always
//                            readable, at y = areaMin.y (0 or the terrain minimum).
//
// It mirrors the cbuffer + transpose convention used by render_triangles.hlsl
// (mul(float4(pos,1), viewproj) with viewproj = camera->getViewProjMatrix().getTranspose())
// so it stays consistent with the terrain render path.

cbuffer gConstantBuffer
{
    float4x4 viewproj;

    float3   eye;          float globeRadius;
    float3   areaMin;      float kmSpacing;    // areaMin.y == areaMax.y == ground height
    float3   areaMax;      int   drawMode;     // 0 = globe, 1 = ground grid

    int      latLines;
    int      lonLines;
    int      segments;
    int      kmLines;                          // ground grid lines per axis (edge-inclusive)
};

struct VSOut
{
    float4 pos : SV_POSITION;
    float3 col : COLOR0;
};

static const float PI      = 3.14159265358979;
static const float DEG2RAD = PI / 180.0;

float3 spherePoint(float latDeg, float lonDeg)
{
    float lat = latDeg * DEG2RAD;
    float lon = lonDeg * DEG2RAD;
    float cl  = cos(lat);
    // Y is up in this app; poles along +/-Y, lon measured around the Y axis (+Z = lon 0).
    return float3(cl * sin(lon), sin(lat), cl * cos(lon));
}

// -------------------------------------------------------------------------------------
// GLOBE (drawMode 0)
// -------------------------------------------------------------------------------------
VSOut globeVertex(uint vId)
{
    VSOut o = (VSOut)0;

    const uint latVerts = (uint)(latLines * segments * 2);
    const uint lonVerts = (uint)(lonLines * segments * 2);

    float3 wpos = eye;
    float3 col  = float3(0.25, 0.25, 0.25);

    if (vId < latVerts)
    {
        // Latitude circles (constant latitude, swept around longitude).
        uint s    = vId / 2;
        uint endp = vId & 1u;
        uint li   = s / (uint)segments;
        uint si   = s % (uint)segments;

        float latDeg = lerp(-80.0, 80.0, (float)li / (float)(latLines - 1));
        float lonDeg = 360.0 * (float)(si + endp) / (float)segments;
        wpos = eye + globeRadius * spherePoint(latDeg, lonDeg);

        col = float3(0.0, 0.35, 0.5);
        if (abs(latDeg) < 0.001) col = float3(0.0, 0.9, 1.0); // equator
    }
    else
    {
        // Longitude lines (constant longitude, pole to pole).
        uint v    = vId - latVerts;
        uint s    = v / 2;
        uint endp = v & 1u;
        uint oi   = s / (uint)segments;
        uint si   = s % (uint)segments;

        float lonDeg = 360.0 * (float)oi / (float)lonLines;
        float latDeg = lerp(-90.0, 90.0, (float)(si + endp) / (float)segments);
        wpos = eye + globeRadius * spherePoint(latDeg, lonDeg);

        col = float3(0.5, 0.0, 0.35);
        if (lonDeg < 0.001)                   col = float3(0.2, 0.4, 1.0); // +Z meridian
        else if (abs(lonDeg - 90.0)  < 0.001) col = float3(1.0, 0.3, 0.3); // +X meridian
        else if (abs(lonDeg - 270.0) < 0.001) col = float3(1.0, 0.7, 0.2); // -X meridian
    }

    o.pos = mul(float4(wpos, 1.0), viewproj);
    o.col = col;
    return o;
}

// -------------------------------------------------------------------------------------
// GROUND GRID (drawMode 1) - world-anchored, covers the whole terrain area.
// Vertex ranges:  [0 .. 2N)   lines running along Z (constant X)
//                 [2N .. 4N)  lines running along X (constant Z)
// -------------------------------------------------------------------------------------
VSOut groundVertex(uint vId)
{
    VSOut o = (VSOut)0;

    const uint N       = (uint)kmLines;
    const uint zLines  = 2u * N;          // verts used by the "constant X" set
    const float gY     = areaMin.y;
    const float eps    = 0.5 * kmSpacing;

    float3 wpos = float3(0, gY, 0);
    float3 col  = float3(0.12, 0.22, 0.12);

    if (vId < zLines)
    {
        // Line running along Z at constant X.
        uint li   = vId / 2;
        uint endp = vId & 1u;
        float x   = areaMin.x + (float)li * kmSpacing;
        float z   = (endp == 0u) ? areaMin.z : areaMax.z;
        wpos = float3(x, gY, z);

        if (li == 0u || li == N - 1u)      col = float3(1.0, 0.8, 0.2);   // area boundary
        else if (abs(x) < eps)             col = float3(0.3, 0.5, 1.0);   // world Z axis (X==0)
    }
    else
    {
        // Line running along X at constant Z.
        uint v    = vId - zLines;
        uint li   = v / 2;
        uint endp = v & 1u;
        float z   = areaMin.z + (float)li * kmSpacing;
        float x   = (endp == 0u) ? areaMin.x : areaMax.x;
        wpos = float3(x, gY, z);

        if (li == 0u || li == N - 1u)      col = float3(1.0, 0.8, 0.2);   // area boundary
        else if (abs(z) < eps)             col = float3(1.0, 0.35, 0.35); // world X axis (Z==0)
    }

    o.pos = mul(float4(wpos, 1.0), viewproj);
    o.col = col;
    return o;
}

VSOut vsMain(uint vId : SV_VertexID)
{
    if (drawMode == 0) return globeVertex(vId);
    return groundVertex(vId);
}

float4 psMain(VSOut i) : SV_TARGET
{
    return float4(i.col, 1.0);
}
