// Debug orientation and movement aids.
//
// Two independent primitives, each generated entirely from SV_VertexID (no vertex or
// index buffer required) and selected by `drawMode`:
//
//   drawMode 0  GLOBE        A sparse compass sphere centered on the camera, radius
//                            matched to the corners of the 40x40 km terrain area
//                            (20000 * sqrt(2) m). Deliberately minimal:
//                              * meridians every 45 deg: North (-Z) red, South (+Z)
//                                black, East/West grey, the four diagonals faint;
//                              * latitude circles only at the horizon (grey) and
//                                +/-45 deg (faint pitch markers).
//                            Drawn INTO the HDR FBO with depth-testing enabled so
//                            terrain occludes it - the terrain silhouette then "cuts
//                            a hole" in the globe.
//
//   drawMode 1  GROUND GRID  The LIVE terrain quadtree: one rectangle outline per
//                            leaf tile (CPU-side quadtree state, uploaded per frame
//                            into `tileRects`), colour-coded by LOD and inset by 1%
//                            of the tile size so neighbouring outlines stay separate
//                            and splits are easy to spot. Drawn ON TOP (depth
//                            disabled), draped at the terrain height under each
//                            tile (tileHeights, CPU heightfield) + groundY.
//
// It mirrors the cbuffer + transpose convention used by render_triangles.hlsl
// (mul(float4(pos,1), viewproj) with viewproj = camera->getViewProjMatrix().getTranspose())
// so it stays consistent with the terrain render path.

cbuffer gConstantBuffer
{
    float4x4 viewproj;

    float3   eye;          float globeRadius;

    float    groundY;                           // ground-plane height for the tile grid
    int      drawMode;                          // 0 = globe, 1 = ground grid
    int      latLines;                          // globe latitude circles (odd; middle = horizon)
    int      lonLines;                          // globe meridians (8 = every 45 deg)

    int      segments;                          // segments per globe circle
    int      tileCount;                         // entries in tileRects actually drawn
};

// One entry per quadtree leaf tile: xy = world-space origin (x, z), z = size, w = lod.
StructuredBuffer<float4> tileRects;
// Terrain height per tile (max over centre + corners, CPU heightfield) so the
// grid drapes onto the terrain instead of drawing at sea level.
StructuredBuffer<float> tileHeights;

struct VSOut
{
    float4 pos : SV_POSITION;
    float3 col : COLOR0;
};

static const float PI      = 3.14159265358979;
static const float DEG2RAD = PI / 180.0;

static const float3 kFaint = float3(0.14, 0.14, 0.14);  // subtle in-between lines

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

    float3 wpos = eye;
    float3 col  = kFaint;

    if (vId < latVerts)
    {
        // Latitude circles: horizon plus faint +/-45 deg pitch markers.
        uint s    = vId / 2;
        uint endp = vId & 1u;
        uint li   = s / (uint)segments;
        uint si   = s % (uint)segments;

        float latDeg = lerp(-45.0, 45.0, (float)li / (float)(latLines - 1));
        float lonDeg = 360.0 * (float)(si + endp) / (float)segments;
        wpos = eye + globeRadius * spherePoint(latDeg, lonDeg);

        col = (abs(latDeg) < 0.001) ? float3(0.5, 0.5, 0.5)  // horizon
                                    : kFaint;                // +/-45 pitch markers
    }
    else
    {
        // Meridians, pole to pole, every 45 deg. Cardinals stand out, diagonals faint.
        uint v    = vId - latVerts;
        uint s    = v / 2;
        uint endp = v & 1u;
        uint oi   = s / (uint)segments;
        uint si   = s % (uint)segments;

        float lonDeg = 360.0 * (float)oi / (float)lonLines;
        float latDeg = lerp(-90.0, 90.0, (float)(si + endp) / (float)segments);
        wpos = eye + globeRadius * spherePoint(latDeg, lonDeg);

        // World compass (from the GDAL import in terrain.cpp writeGdal:
        // easting = +X, northing = -Z): NORTH = -Z = lon 180, SOUTH = +Z = lon 0.
        // Before the F20 un-mirroring these labels were accidentally swapped.
        if (oi == 4u)      col = float3(0.9, 0.05, 0.05);    // North (-Z), compass red
        else if (oi == 0u) col = float3(0.04, 0.04, 0.04);   // South (+Z), compass black
        else if ((oi & 1u) == 0u) col = float3(0.45, 0.45, 0.45); // East / West, grey
        else               col = kFaint;                     // diagonals
    }

    o.pos = mul(float4(wpos, 1.0), viewproj);
    o.col = col;
    return o;
}

// -------------------------------------------------------------------------------------
// GROUND GRID (drawMode 1) - live quadtree leaf tiles.
// 8 vertices per tile: 4 edges x 2 endpoints, rectangle inset by 1% of the tile size
// so adjacent tiles do not draw on top of each other and splits read clearly.
// -------------------------------------------------------------------------------------
static const float3 kLodPalette[8] =
{
    float3(0.30, 0.30, 0.30),   // lod 0 (whole area)
    float3(0.25, 0.40, 0.90),   // 1 blue
    float3(0.10, 0.75, 0.85),   // 2 cyan
    float3(0.15, 0.80, 0.25),   // 3 green
    float3(0.90, 0.85, 0.15),   // 4 yellow
    float3(0.95, 0.55, 0.10),   // 5 orange
    float3(0.95, 0.20, 0.15),   // 6 red
    float3(0.85, 0.25, 0.85),   // 7 magenta (then wraps)
};

VSOut groundVertex(uint vId)
{
    VSOut o = (VSOut)0;

    uint tile = vId / 8u;
    uint v    = vId % 8u;
    if (tile >= (uint)tileCount) tile = 0u;

    float4 r     = tileRects[tile];
    float  inset = r.z * 0.01;
    float2 mn    = r.xy + inset;
    float2 mx    = r.xy + r.z - inset;

    uint edge = v / 2u;
    uint endp = v & 1u;

    float2 p;
    if (edge == 0u)      p = float2(endp ? mx.x : mn.x, mn.y);   // -Z edge
    else if (edge == 1u) p = float2(mx.x, endp ? mx.y : mn.y);   // +X edge
    else if (edge == 2u) p = float2(endp ? mn.x : mx.x, mx.y);   // +Z edge
    else                 p = float2(mn.x, endp ? mn.y : mx.y);   // -X edge

    // Sit 2 m above the sampled terrain height so the outline is not z-buried
    // by the surface it describes (drawn without depth test anyway, but this
    // also keeps the projected position visually on the terrain).
    o.pos = mul(float4(p.x, groundY + tileHeights[tile] + 2.0, p.y, 1.0), viewproj);
    o.col = kLodPalette[(uint)r.w & 7u];
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
