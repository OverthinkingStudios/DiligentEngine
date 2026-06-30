// Debug orientation/movement grid for the EarthworksFX -> DiligentEngine bring-up.
//
// Renders two reference frames as colored line segments, generated entirely from
// SV_VertexID (no vertex/index buffer required):
//   1) A latitude/longitude "globe" centered on the camera  -> orientation / rotation.
//   2) A world-anchored ground grid that snaps to the camera -> translation / parallax.
//
// It deliberately mirrors the cbuffer + transpose convention used by render_triangles.hlsl
// (mul(float4(pos,1), viewproj) with viewproj = camera->getViewProjMatrix().getTranspose())
// so it is guaranteed consistent with the terrain render path.

cbuffer gConstantBuffer
{
    float4x4 viewproj;
    float3   eye;
    float    globeRadius;
    float    groundSpacing;
    float    groundHeight;
    int      latLines;
    int      lonLines;
    int      segments;
    int      groundLines;
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

VSOut vsMain(uint vId : SV_VertexID)
{
    VSOut o = (VSOut)0;

    const uint latVerts    = (uint)(latLines * segments * 2);
    const uint lonVerts    = (uint)(lonLines * segments * 2);
    const uint groundXVerts = (uint)(groundLines * 2);

    float3 wpos = float3(0, 0, 0);
    float3 col  = float3(0.25, 0.25, 0.25);

    if (vId < latVerts)
    {
        // Globe latitude circles (constant latitude, swept around longitude).
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
    else if (vId < latVerts + lonVerts)
    {
        // Globe longitude lines (constant longitude, pole to pole).
        uint v    = vId - latVerts;
        uint s    = v / 2;
        uint endp = v & 1u;
        uint oi   = s / (uint)segments;
        uint si   = s % (uint)segments;

        float lonDeg = 360.0 * (float)oi / (float)lonLines;
        float latDeg = lerp(-90.0, 90.0, (float)(si + endp) / (float)segments);
        wpos = eye + globeRadius * spherePoint(latDeg, lonDeg);

        col = float3(0.5, 0.0, 0.35);
        if (lonDeg < 0.001)                         col = float3(0.2, 0.4, 1.0); // +Z meridian
        else if (abs(lonDeg - 90.0)  < 0.001)       col = float3(1.0, 0.3, 0.3); // +X meridian
        else if (abs(lonDeg - 270.0) < 0.001)       col = float3(1.0, 0.7, 0.2); // -X meridian
    }
    else if (vId < latVerts + lonVerts + groundXVerts)
    {
        // Ground grid lines running along Z (constant X), snapped to the camera.
        uint v    = vId - latVerts - lonVerts;
        uint li   = v / 2;
        uint endp = v & 1u;

        float snapX = round(eye.x / groundSpacing) * groundSpacing;
        float snapZ = round(eye.z / groundSpacing) * groundSpacing;
        float halfE = (float)(groundLines / 2) * groundSpacing;

        float lineX = snapX + ((float)li - (float)(groundLines / 2)) * groundSpacing;
        float z     = snapZ + (endp == 0u ? -halfE : halfE);
        wpos = float3(lineX, groundHeight, z);

        col = float3(0.15, 0.3, 0.15);
        if (abs(lineX) < 0.5 * groundSpacing) col = float3(0.3, 0.4, 1.0); // world Z axis (X==0)
    }
    else
    {
        // Ground grid lines running along X (constant Z), snapped to the camera.
        uint v    = vId - latVerts - lonVerts - groundXVerts;
        uint li   = v / 2;
        uint endp = v & 1u;

        float snapX = round(eye.x / groundSpacing) * groundSpacing;
        float snapZ = round(eye.z / groundSpacing) * groundSpacing;
        float halfE = (float)(groundLines / 2) * groundSpacing;

        float lineZ = snapZ + ((float)li - (float)(groundLines / 2)) * groundSpacing;
        float x     = snapX + (endp == 0u ? -halfE : halfE);
        wpos = float3(x, groundHeight, lineZ);

        col = float3(0.15, 0.3, 0.15);
        if (abs(lineZ) < 0.5 * groundSpacing) col = float3(1.0, 0.3, 0.3); // world X axis (Z==0)
    }

    o.pos = mul(float4(wpos, 1.0), viewproj);
    o.col = col;
    return o;
}

float4 psMain(VSOut i) : SV_TARGET
{
    return float4(i.col, 1.0);
}
