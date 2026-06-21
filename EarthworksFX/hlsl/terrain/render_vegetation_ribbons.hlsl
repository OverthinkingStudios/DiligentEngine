
#include "groundcover_defines.hlsli"
#include "groundcover_functions.hlsli"		
#include "vegetation_defines.hlsli"
#include "../PBR.hlsli"


StructuredBuffer<sprite_material>   materials;
StructuredBuffer<plant>             plant_buffer;
StructuredBuffer<_plant_anim_pivot> plant_pivot_buffer;
StructuredBuffer<plant_instance>    instance_buffer;
StructuredBuffer<block_data>        block_buffer;
StructuredBuffer<ribbonVertex8>     vertex_buffer;
StructuredBuffer<veg_sort>        sort;

globallycoherent  RWStructuredBuffer<vegetation_feedback> feedback_Veg;

Texture2D       gAlbedo : register(t0);
//TextureCube     gEnv : register(t1);
Texture2D       gDappledLight : register(t3);
Texture2D       highResShadow : register(t4);

struct ribbonTextures
{
    Texture2D<float4> T[4096];
};
ParameterBlock<ribbonTextures>
textures;


cbuffer gConstantBuffer
{
    float4x4 view;
    float4x4 viewproj;
    float3 eyePos;
    
    float time;
    float bake_radius_alpha;
    float bake_height_alpha;
    int bake_AoToAlbedo;

    float3 windDir;
    float windStrength;

    float4x4 shadowViewProj;

    uint drawIndex;

    float2 bake_AlphaOval;
};






// FIXME play with half res
struct PSIn
{
    centroid  float4 pos : SV_Position;

    float3 normal   : NORMAL;       // all in view space HALF feels great
    float3 tangent  : TANGENT;
    float3 binormal : BINORMAL;
    
    float4 diffuseLight : COLOR;    //??? float 4 with shadow in w
    float4 vertexLight  : COLOR1;   //??? I think for gauroad shading and maybe small ligths
    float3 debugColour  : COLOR2;
    float3 otherLights  : COLOR3;

    nointerpolation uint4 flags : TEXCOORD; // material, startbit, radius

    float4 worldPos     : TEXCOORD1;
    float2 uv           : TEXCOORD2;
    float4 colour       : TEXCOORD3;    // albeo scale, ttanslucency scale, alpha used for baking or w for fog distance

    float3 inscatter    : TEXCOORD4;
    float3 outscatter   : TEXCOORD5;

    

#if defined(_BAKE)
    float4 lighting : TEXCOORD10; // uv, sunlight, ao
#endif
};

// PSIn flags - far easier reading
#define AlbedoScale         colour.x
#define TranslucencyScale   colour.y
#define AmbietOcclusion     colour.z
#define FogDistance         colour.w            // doubled with alpha for baking

// valid as input into Geometry shader
#define ribbonWidth         pos.x         
#define ribbonHeight        pos.y
#define uScale              pos.z

#define Shadow          diffuseLight.w

#define material_IDX    flags.x
#define start_BIT       flags.y
#define diamond         flags.z
#define plant_IDX       flags.w

// vertex extraction flags
#define isCameraFacing  (v.a >> 31)
#define radius          (v.d & 0xff)
#define packDiamond     (v.b & 0x1)
#define unpackPosition() float3((v.a >> 16) & 0x3fff, v.a & 0xffff, (v.b >> 18) & 0x3fff)


// Basic HSL to GLSL conversions
#if defined(_VULCAN)
#define float3 vec3
#define float4 vec4
#define float3x3 mat3
#define float4x4 mat4
#endif


#if defined(_Z_UP)
float3x3 rotate_Y(const float yaw)
{
    const float s = sin(yaw);
    const float c = cos(yaw);
    return float3x3(c, 0, s,     s, 0, -c,      0, 1, 0);
}
#else
float3x3 rotate_Y(const float yaw)
{
    const float s = sin(yaw);
    const float c = cos(yaw);
    return float3x3( c, 0, s,    0, 1, 0,    -s, 0, c );
}
#endif
 


float3x3 AngleAxis3x3(float angle, float3 axis)
{
    float c, s;
    sincos(angle, s, c);
    const float t = 1 - c;
    const float xx = t * axis.x * axis.x;
    const float xy = t * axis.x * axis.y;
    const float xz = t * axis.x * axis.z;
    const float yy = t * axis.y * axis.y;
    const float yz = t * axis.y * axis.z;
    const float zz = t * axis.z * axis.z;

    return float3x3(
        xx + c,            xy - s * axis.z,   xz + s * axis.y,
        xy + s * axis.z,   yy + c,            yz - s * axis.x,
        xz - s * axis.y,   yz + s * axis.x,   zz + c            );
}


// r is needed since lightcone still uses it
inline float3 yawPitch_9_8bit(int yaw, int pitch, float r) // 9, 8 bits 8 b
{
    float plane, x, y, z;
    sincos((yaw - 256) * 0.01227 - r, z, x);
    sincos((pitch - 128) * 0.01227, y, plane);
    return float3(plane * x, y, plane * z);
}


// These two can optimize, its only tangent that differs
inline void extractTangent(inout PSIn o, const ribbonVertex8 v)
{
    o.binormal = yawPitch_9_8bit(v.c >> 23, (v.c >> 15) & 0xff, 0);
    o.tangent = yawPitch_9_8bit(v.d >> 23, (v.d >> 15) & 0xff, 0);
}

inline void extractUVRibbon(inout PSIn o, const ribbonVertex8 v)
{
    o.uv = float2(((v.d >> 8) & 0x7f) * 0.00390625, ((v.c) & 0x7fff) * 0.00390625); // allows 16 V repeats scales are wrong decide
}

inline void extractFlags(inout PSIn o, const ribbonVertex8 v)
{
    o.material_IDX = (v.b >> 8) & 0x2ff; //  material
    o.start_BIT = ((v.a >> 30) & 0x1) + ((v.b & 0x1) << 1); //  start bool   we can double pack
}












void bezierLeaf(_plant_anim_pivot PVT, inout float3 pos, inout float3 binorm, inout float3 tangent, float3 wind, float freq_scale, float tIn)
{
    const float3 rel = pos - PVT.root;
    const float S = length(rel);
    const float3 right = normalize(cross(rel, wind));

    wind /= PVT.stiffness; //??? pow() to scale effect better, sane for wind as well
    //wind *= 3;
    //  now ossilate
    float swayStrength = 0.5 * sin(time / PVT.frequency * 6.283 * freq_scale + PVT.offset);
    float sideStrength = 0.50 * sin(time / PVT.frequency * 4.83 * freq_scale + PVT.offset + 1);
    float3 c = normalize(rel) + tIn * (wind * (1 + swayStrength)) + tIn * (right * length(wind) * sideStrength);
    binorm += c - normalize(rel);
    binorm = normalize(binorm);

    pos = PVT.root + normalize(c) * S;

    float flutterStrength = 1 * length(wind) * 0.5 * PVT.shift * sin(time / PVT.frequency * 8.283 * freq_scale);
    float3 N = cross(binorm, tangent);
    tangent = normalize(tangent + N * flutterStrength);
}


void bezierPivotSum(_plant_anim_pivot PVT, inout float3 pos, inout float3 binorm, float3 wind, float freq_scale)
{
    const float S = 1.f / length(PVT.extent);
    const float3 rel = pos - PVT.root;
    const float3 b = normalize(rel) * 0.5;
    const float t = length(rel) / S;
    const float3 right = normalize(cross(rel, wind));

    const float pushScale = 1.f - abs(dot(normalize(PVT.extent), normalize(wind)));

    wind /= PVT.stiffness;
    
    //  now ossilate
    float swayStrength = 0.5 * sin(time / PVT.frequency * 1.283 * freq_scale + PVT.offset);
    float sideStrength = 0.4 * sin(time / PVT.frequency * 0.83 * freq_scale + PVT.offset + 1);
    float3 c = b * 2 + (wind * (pushScale + swayStrength)) + (right * length(wind) * sideStrength);
    
    float3 bc = normalize(c - b) * 0.5;
    c = b + bc;
    float scale = (1 / length(c) + 2.5) / 3.5 * S;

    float3 f = b * t;
    float3 g = lerp(b, c, t);
    binorm += normalize(g - f) - (b * 2);
    binorm = normalize(binorm);

    pos = PVT.root + lerp(f, g, t) * scale;
}




/*  This is a temporary function that mimics some form of wind schanging strenth and swirling as it moves over the terrain
    - In a real application it is likely replaced with a wind simulation and lookup
    - It also should most likely run inside compute and write to plant instance to save processign here
    - It returns a boolean since we cannot compyte crossproduct of (0, 0, 0) so animating with zero wind is imposssible
*/
inline float3 rot_xz(const float3 v, const float yaw)
{
    float s, c;
    sincos(yaw, s, c);
    return float3((v.x * c) + (v.z * s), v.y, (-v.x * s) + (v.z * c));
}

bool animateWind(inout float3 _wind, float3 _pos, float3 _plantRoot, float _rot)
{
    float dx = dot(_pos.xz, windDir.xz) * 0.1;
    float strenth = (1 + 0.4 * sin(dx - time * windStrength * 1 * 0.025));
    strenth = windStrength *(0.8 + smoothstep(0.4, 1.3, strenth));
    float ss =  sin(_pos.x * 0.5 - time * 0.4) + sin(_pos.z * 0.5 - time * 0.3);
    float3x3 rot = AngleAxis3x3(ss * 0.9, float3(0, 1, 0));
    _wind = normalize(mul(windDir, rot));
    _wind = rot_xz(_wind, _rot) * strenth * 0.01; // rotate the wind into the plant frame
    _wind.y = 0;


    return (strenth < 0.00001);
}


#define A ((_v.g >> 24) & 0xff)
#define B ((_v.g >> 16) & 0xff)
#define C ((_v.g >> 8) & 0xff)
#define D (_v.g & 0xff)
#define F (_v.h & 0xff)
float3 bezierAnimate(inout float3 _position, inout float3 _binormal, inout float3 _tangent, ribbonVertex8 _v, uint _vId, const plant _plant, const plant_instance _instance)
{
    float3 wind;
    if (animateWind(wind, _instance.position + _position, _instance.position, _instance.rotation))
        return 0; // there is an anomoly at 0, cant do cross product so avoid animating
        
    if (F > 0)
    {
        _plant_anim_pivot PVT;
        ribbonVertex8 vRoot = vertex_buffer[_vId - F];
        PVT.root = float3((vRoot.a >> 16) & 0x3fff, vRoot.a & 0xffff, (vRoot.b >> 18) & 0x3fff) * _plant.scale - _plant.offset;
        PVT.stiffness = 1 / (((_v.h >> 16) & 0xff) * 0.004);
        PVT.frequency = ((_v.h >> 8) & 0xff) * 0.004;
        PVT.shift = ((_v.h >> 24) & 0xff) * 0.004;
        PVT.offset = _vId - F;
        bezierLeaf(PVT, _position, _binormal, _tangent, wind, _instance.scale, PVT.shift);
    }

    const uint pivotOffset = _instance.plant_idx * 256;
    if (D < 255)
    {
        bezierPivotSum(plant_pivot_buffer[D + pivotOffset], _position, _binormal, wind, _instance.scale);
    }

    if (C < 255)
    {
        bezierPivotSum(plant_pivot_buffer[C + pivotOffset], _position, _binormal, wind, _instance.scale);
    }

    if (B < 255)
    {
       bezierPivotSum(plant_pivot_buffer[B + pivotOffset], _position, _binormal, wind, _instance.scale);
    }

    if (A < 255)
    {
        bezierPivotSum(plant_pivot_buffer[A + pivotOffset], _position, _binormal, wind, _instance.scale);
    }

    return 0;
}




PSIn vsMain(uint vId : SV_VertexID, uint iId : SV_InstanceID)
{
    PSIn output = (PSIn) 0;
    
#if defined(_BILLBOARD)
    
    const plant_instance INSTANCE = instance_buffer[vId];
    const plant PLANT = plant_buffer[INSTANCE.plant_idx];

    output.worldPos = float4(INSTANCE.position, 1);
    float3 eye = normalize(eyePos - output.worldPos.xyz);
    output.FogDistance = length(eyePos - output.worldPos.xyz);

    output.binormal = float3(0, 1, 0);
    output.tangent = normalize(cross(output.binormal, eye));
    output.normal = cross(output.tangent, output.binormal);

    output.AlbedoScale = 1;
    output.TranslucencyScale = 1;

    output.Shadow = 0;
    output.AmbietOcclusion = 1;

    if ((dot(output.pos.xyz, sunRightVector)) > 5 && (dot(output.pos.xyz, sunRightVector)) < 8)
    {
        output.Shadow = 1;
    }

    output.material_IDX = PLANT.billboardMaterialIndex;
    output.plant_IDX = INSTANCE.plant_idx;
    // misuse positin as inout to geometr ashader
    output.pos.xy = PLANT.size * INSTANCE.scale;
    output.pos.z = 0.8;

    //output.lineScale.z = 0.8;

    //output.pos = mul(output.pos, viewproj);

    //output.diffuseLight = sunLight(output.pos.xyz * 0.001).rgb;
    output.diffuseLight.rgb = sunLight(INSTANCE.position * 0.001).rgb;

    float SS = 1 - pow(shadow(output.worldPos.xyz, 0), 0.25); // Should realyl fo this in VS, just make sunlight zero
    output.Shadow = 0;//SS;

    float4 screen = mul(output.worldPos, viewproj);
    screen.xy /= screen.w;
    screen.xy *= 0.5;
    screen.xy += 0.5;
    screen.y = 1 - screen.y;
    vs_atmosphere(output.outscatter, output.inscatter, screen.xy, output.FogDistance);

    return output;


#else
    
    const block_data        BLOCK = block_buffer[iId + sort[drawIndex].offset];
    const plant_instance    INSTANCE = instance_buffer[BLOCK.instance_idx];
    const plant             PLANT = plant_buffer[INSTANCE.plant_idx];
    const ribbonVertex8     v = vertex_buffer[BLOCK.vertex_offset + vId];
    
    output.worldPos = float4(unpackPosition() * PLANT.scale - PLANT.offset, 1); //0.1ms
    
#if defined(_BAKE)
    float3 rootPos = float3(0, 0, 0);
    float3 p = unpackPosition() * PLANT.scale - PLANT.offset;
    output.pos =  float4(p, 1);
        
    p.y = 0;
    float R = length(p);
    if (R > 0.3f)      output.colour.a = 0;
    
    output.colour.a = 1;
    {
        output.colour.a = 1.f - smoothstep(bake_radius_alpha * 0.95f, bake_radius_alpha, R);
    }
    output.colour.a *= (1.f - smoothstep(bake_height_alpha * 0.96f, bake_height_alpha, output.pos.y)); //last 10% f16tof32 tip asdouble well

    extractTangent(output, v);
#endif
    
    float SS = 1 - pow(shadow(output.pos.xyz + INSTANCE.position, 0), 0.25); // Should realyl fo this in VS, just make sunlight zero

    extractTangent(output, v);
    extractUVRibbon(output, v);
    //extractFlags(output, v);
    output.material_IDX = (v.b >> 8) & 0x2ff; //  material
    //0.06ms
    
    // FIXME WHY IS THE NEXT ONE SO SLOW
//    output.start_BIT = ((v.a >> 30) & 0x1) +((v.b & 0x1) << 1); //  start bool   we can double pack
    output.start_BIT =  ((v.a >> 30) & 0x1); //  start bool   we can double pack
    //if (vId == 1) output.start_BIT = 0;
    
    output.diamond = (v.b & 0x1);
    //0.26 ms thsi is very slow
    
    output.AlbedoScale = 0.1 + ((v.f >> 8) & 0xff) * 0.008;
    output.TranslucencyScale = ((v.f >> 0) & 0xff) * 0.008;
    // then this is free - VERY ODD
    
    
    
    

#if defined(_BAKE)
    // FIXME I think this is now vbroejkn due to worldPos uses - TS
    output.worldPos.xyz *= INSTANCE.scale;
    float3 root = INSTANCE.position;
    root.y = 0;
    output.worldPos.xyz += root;
#else
    output.debugColour = bezierAnimate(output.worldPos.xyz, output.binormal, output.tangent, v, BLOCK.vertex_offset + vId, PLANT, INSTANCE);
    //0.12ms
    
    // this block is 0.2 slowish, but bezier above is quick
    float3x3 rot = rotate_Y(INSTANCE.rotation);
    output.worldPos.xyz = mul(output.worldPos.xyz, rot);
    output.worldPos.xyz *= INSTANCE.scale;
    output.worldPos.xyz += INSTANCE.position;

    // Rotate the binormal and tangent
    output.binormal = mul(output.binormal, rot);
    output.tangent = mul(output.tangent, rot);
#endif
    // then this is basically free again
    
    
    float3 eye = normalize(eyePos - output.worldPos.xyz);
    output.FogDistance = length(eyePos - output.worldPos.xyz);
    float4 screen = mul(output.worldPos, viewproj);
    screen.xy /= screen.w;
    screen.xy *= 0.5;
    screen.xy += 0.5;
    screen.y = 1 - screen.y;
    vs_atmosphere(output.outscatter, output.inscatter, screen.xy, output.FogDistance);
    // 0.2ms
    
    if (isCameraFacing)
    {
        output.tangent = normalize(cross(output.binormal, eye));
    }
    output.normal = cross(output.tangent, output.binormal );
    output.diffuseLight.rgb = sunLight(INSTANCE.position * 0.001).rgb; // should really happen lower but i guess its fast
    output.pos.x = pow(radius / 255.f, 2) * PLANT.radiusScale * INSTANCE.scale;
    output.plant_IDX = INSTANCE.plant_idx;
    //0.1ms - this is for 2.5 million sverts

    //if (iId == 0 && vId == 0) output.ribbonWidth = 1;

 // amper 20% van totale VS tyd
    {
        float3 lightCone = yawPitch_9_8bit(v.e >> 23, (v.e >> 15) & 0xff, INSTANCE.rotation);
        float sunCone = (((v.e >> 8) & 0x7f) * 0.01575) - 1; //cone7
        float sunDepth = (v.e & 0xff) * 0.00784; //depth8   // sulight % how deep inside this plant 0..1 sun..shadow
        float a = saturate(dot(normalize(lightCone - sunDirection * PLANT.sunTilt), sunDirection)); // - sunCone * 0 sunCosTheta sunCone biasess this bigger or smaller 0 is 180 degrees
        //output.Shadow = SS;// * saturate(a * sunDepth + sunDepth); // darker to middle
        output.Shadow = saturate(a * sunDepth + sunDepth); // darker to middle
        //output.Shadow = SS; // darker to middle
        output.AmbietOcclusion = pow(((v.f >> 24) / 255.f), 3);

        if ((dot(output.pos.xyz, sunRightVector)) > 5 && (dot(output.pos.xyz, sunRightVector)) < 8)
        {
            // output.Shadow = 1;
        }

        output.otherLights = 0;
        /*
        for (float x = 0; x < 2; x += 1)
        {
            for (float y = 0; y < 4; y += 1)
            {
                float3 pos = float3(3.1*(x - 5), 1001.07, 3.1*(y - 5));
                float3 col = float3(frac(0.6 + x * 1.7 + y * 1.3), frac(0.9 + x * 12.7), frac(0.3 + y * 3.3));
                float3 dir = normalize(output.worldPos.xyz - pos);
                float3 lightdir = rot_xz(float3(1, 0, 0), x * (0.3 + y * 0.7) * time * 0.01);
                float cone = smoothstep(0.906, 0.99, dot(lightdir, dir));
                float r = length(output.worldPos.xyz - pos);
                float a = saturate(dot(normalize(lightCone - dir * PLANT.sunTilt), dir));
                float shadow = saturate(a * sunDepth + sunDepth);
                output.otherLights += 5 * (col / r / r)  *(1-shadow) *cone;
            }
        }
        */
    }


#if defined(_BAKE)
    // THis is pre-roataed through a matrix so we can do it absoilutely here unlike in leafbuilder
    output.diffuseLight.a = 1.0;
    //float radius2 = length(output.worldPos.xz);

    float2 RR;
    if (bake_AlphaOval.y > 0)
    {
        RR.y = output.worldPos.y / bake_AlphaOval.y;
        RR.x = length(output.worldPos.xz) / bake_AlphaOval.x;
        float Aradius = length(RR);
        output.diffuseLight.a *= (1 - smoothstep(0.8, 1.1, Aradius));
    }
#endif


    //uint slot;
    //InterlockedAdd(feedback_Veg[0].numPixClip, 1, slot);

    // 0.2ms
#if defined(_GOURAUD_SHADING)    
    // now light it
    {
        const sprite_material MAT = materials[output.material_IDX];
        float3 albedo = textures.T[MAT.albedoTexture].SampleLevel(gSmpLinearClamp, float2(0.5, 0.5), 8).rgb;

        float dappled = pow(1 - output.Shadow, 2);
        float3 N = output.normal;
        if (dot(N, eye) > 0)
            N *= -1;
        float ndoth = saturate(dot(N, normalize(sunDirection - eye)));
        float ndots = dot(N, sunDirection);

        output.vertexLight.rgb = output.diffuseLight.rgb * 3.14 * (saturate(ndots)) * albedo * dappled;
        
        // environment cube light
        output.vertexLight.rgb += 1.16 * gEnv.SampleLevel(gSmpLinear, N * float3(1, 1, -1), 5).rgb * albedo.rgb * pow(output.AmbietOcclusion, 0.3);
    
    // specular sunlight
        float RGH = MAT.roughness[0] + 0.001; //?? frontback
        float pw = 1.f / RGH;
        output.vertexLight.rgb += pow(ndoth, pw) * 0.1 * dappled * output.diffuseLight.rgb;

        output.vertexLight.a = output.pos.x / length(output.worldPos.xyz - eyePos) * 10;
    }
#endif  //_GOURAUD_SHADING



    return output;
#endif
}



// geometry shaders
// ------------------------------------------------------------------------------------------------------
#if defined(_BILLBOARD)



[maxvertexcount(4)]
void gsMain(point PSIn pt[1], inout TriangleStream<PSIn> OutputStream)
{
    PSIn v = pt[0];
    float scale = 0;// -pt[0].uScale;
    float W = pt[0].ribbonWidth;
    float H = pt[0].ribbonHeight;

    v.uv = float2(0.5, 1.1);
    v.worldPos.xyz = pt[0].worldPos.xyz - 0.1 * v.binormal * H;
    v.AmbietOcclusion = 0.4;
    v.Shadow = 0.75;
    v.pos = mul(v.worldPos, viewproj);
    OutputStream.Append(v);

    v.uv = float2(1.0 -  scale/2 , 0.5);
    v.uv = float2(1.0, 0.5);
    v.worldPos.xyz = pt[0].worldPos.xyz + v.tangent * W + 0.5 * v.binormal * H;
    v.AmbietOcclusion = 0.7;
    v.Shadow = 0.45;
    v.pos = mul(v.worldPos, viewproj);
    OutputStream.Append(v);
        
    v.uv = float2(0.0 + scale/2, 0.5);
    v.uv = float2(0.0, 0.5);
    v.worldPos.xyz = pt[0].worldPos.xyz - v.tangent * W + 0.5 * v.binormal * H;
    v.Shadow = 0.45;
    v.pos = mul(v.worldPos, viewproj);
    OutputStream.Append(v);

    v.uv = float2(0.5, -0.1);
    v.worldPos.xyz = pt[0].worldPos.xyz + 1.1 * v.binormal * H;
    v.AmbietOcclusion = 1.0;
    v.Shadow = 0;
    v.pos = mul(v.worldPos, viewproj);
    OutputStream.Append(v);
}
#else

[maxvertexcount(4)]
void gsMain( line  PSIn L[2], inout    TriangleStream<PSIn> OutputStream)
{
    PSIn v;

    float W = L[0].ribbonWidth;

    if (L[1].start_BIT)
    {
        if (L[0].diamond)
        {
            
            PSIn v = L[0];
            float scale = 0;//           1 - L[0].uScale;
            
            v.uv = float2(0.5, 1.0);
            v.worldPos = L[0].worldPos;// -0.1 * (L[1].worldPos - L[0].worldPos);
            //v.sunUV.x = dot(v.worldPos.xyz, sunRightVector);
            //v.sunUV.y = dot(v.worldPos.xyz, sunUpVector);
            v.pos = mul(v.worldPos, viewproj);
            OutputStream.Append(v);
            
            // we should really interpolate here, but use start fo now
            v.uv = float2(1.0 + scale / 2, 0.6);
            v.worldPos = L[0].worldPos + 0.4 * (L[1].worldPos - L[0].worldPos) + float4(v.tangent * W * 1.01, 0);
            //v.sunUV.x = dot(v.worldPos.xyz, sunRightVector);
            //v.sunUV.y = dot(v.worldPos.xyz, sunUpVector);
            v.pos = mul(v.worldPos, viewproj);
            OutputStream.Append(v);
        
            
            v.uv = float2(0.0 - scale / 2, 0.6);
            v.worldPos = L[0].worldPos + 0.4 * (L[1].worldPos - L[0].worldPos) - float4(v.tangent * W * 1.01, 0);
            //v.sunUV.x = dot(v.worldPos.xyz, sunRightVector);
            //v.sunUV.y = dot(v.worldPos.xyz, sunUpVector);
            v.pos = mul(v.worldPos, viewproj);
            OutputStream.Append(v);

            // ONLY OVERSHOOT THE TOP? and by 20%
            v = L[1];
            v.uv = float2(0.5, -0.2);
            v.worldPos = L[1].worldPos + 0.2 * (L[1].worldPos - L[0].worldPos); //            +float4(v.binormal, 0);
            //v.sunUV.x = dot(v.worldPos.xyz, sunRightVector);
            //v.sunUV.y = dot(v.worldPos.xyz, sunUpVector);
            v.pos = mul(v.worldPos, viewproj);
            OutputStream.Append(v);
            
        }
        else
        {
            
            v = L[0];
            v.uv.x = 0.5 - L[0].uv.x;
            v.worldPos = L[0].worldPos - float4(v.tangent * W * 1.01, 0);
            //v.sunUV.x = dot(v.worldPos.xyz, sunRightVector);
            //v.sunUV.y = dot(v.worldPos.xyz, sunUpVector);
            v.pos = mul(v.worldPos, viewproj);
            OutputStream.Append(v);
            
            v.uv.x = 0.5 + L[0].uv.x;
            v.worldPos = L[0].worldPos + float4(v.tangent * W * 1.01, 0);
            //v.sunUV.x = dot(v.worldPos.xyz, sunRightVector);
            //v.sunUV.y = dot(v.worldPos.xyz, sunUpVector);
            v.pos = mul(v.worldPos, viewproj);
            OutputStream.Append(v);

            v = L[1];
            W = L[1].ribbonWidth;
            v.uv.x = 0.5 - L[1].uv.x;
            v.worldPos = L[1].worldPos - float4(v.tangent * W * 1.01, 0);

            //v.sunUV.x = dot(v.worldPos.xyz, sunRightVector);
            //v.sunUV.y = dot(v.worldPos.xyz, sunUpVector);
            v.pos = mul(v.worldPos, viewproj);
            OutputStream.Append(v);

            v.uv.x = 0.5 + L[1].uv.x;
            v.worldPos = L[1].worldPos + float4(v.tangent * W * 1.01, 0);

            //v.sunUV.x = dot(v.worldPos.xyz, sunRightVector);
            //v.sunUV.y = dot(v.worldPos.xyz, sunUpVector);
            v.pos = mul(v.worldPos, viewproj);
            OutputStream.Append(v);
            
        }
    }
}
#endif






#if defined(_BAKE)


float rand_1_05(in float2 uv)
{
    return frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453);
}



struct PS_OUTPUT_Bake
{
    float4 albedo: SV_Target0;
    float4 normal: SV_Target1;
    float4 normal_8: SV_Target2;
    float4 pbr: SV_Target3;
    float4 extra: SV_Target4;
};

PS_OUTPUT_Bake psMain(PSIn vOut, bool isFrontFace : SV_IsFrontFace) : SV_TARGET
{
    PS_OUTPUT_Bake output = (PS_OUTPUT_Bake)0;

    sprite_material MAT = materials[vOut.material_IDX];

    float alpha = 1;
    if (MAT.albedoTexture >= 0)
    {
        alpha = textures.T[MAT.albedoTexture].Sample(gSmpLinear, vOut.uv.xy).a;
        alpha = pow(alpha, MAT.alphaPow);
    }
    //alpha *= vOut.colour.a;
    alpha *= vOut.diffuseLight.a;

    float rnd = 0.1 + 0.8 * rand_1_05(vOut.uv.xy);
    clip(alpha - rnd);

    float3 color = 0.5;
    if (MAT.albedoTexture >= 0)
    {
        color = textures.T[MAT.albedoTexture].Sample(gSmpLinear, vOut.uv.xy).rgb;
    }

    int frontback = (int) !isFrontFace;
    color *= vOut.AlbedoScale  * MAT.albedoScale[frontback] * 2.f;

    if(bake_AoToAlbedo)
    {
        color = color * (0.9 + 0.1 * vOut.AmbietOcclusion);
    }
    //output.albedo = float4(pow(color, 1.0 / 2.2), alpha);
    output.albedo = float4(pow(color, 1.0 / 2.2), 1);
    //output.albedo = float4(1, 0, 0, 1);
    

    float3 N = vOut.normal;
    if (MAT.normalTexture >= 0)
    {
        float3 n = (textures.T[MAT.normalTexture].Sample(gSmpLinear, vOut.uv.xy).rgb * 2) - 1;
        N = (n.r * vOut.tangent) + (n.g * vOut.binormal) + (n.b * vOut.normal);
    }
    N *= (isFrontFace * 2 - 1);

    float3 NAdjusted;
    float3 NCone =vOut.lighting.rbg;
    NCone.r *= -1;
    NCone.b *= -1;
    NAdjusted.r = dot(N, view[0].xyz);
    NAdjusted.g = dot(N, view[1].xyz);
    NAdjusted.b = dot(N, view[2].xyz);
    NAdjusted *= 0.5;
    NAdjusted += 0.5;
    output.normal = float4(NAdjusted, 1);       // FIXME has to convert to camera space, cant eb too hard , uprigthmaybe, and 0-1 space
    output.normal_8 = float4(NAdjusted, 1);

    float trans = vOut.TranslucencyScale * MAT.translucency;
    {
        if (MAT.translucencyTexture >= 0)
        {
            trans *= dot(float3(0.3, 0.4, 0.3), textures.T[MAT.translucencyTexture].Sample(gSmpLinear, vOut.uv.xy).rgb);
        }
    }
    output.extra = float4(0, 0, 0, 1-trans);

    return output;
}

#elif defined(_RGB_SAMPLE)

float4 psMain(PSIn vOut, bool isFrontFace : SV_IsFrontFace) : SV_TARGET
{
    const sprite_material MAT = materials[vOut.material_IDX];
    const plant PLANT = plant_buffer[vOut.plant_IDX];
    const int frontback = (int)!isFrontFace;
    //const float flipNormal = (isFrontFace * 2 - 1);
    float alpha = textures.T[MAT.albedoTexture].Sample(gSmpLinear, vOut.uv).a;
    alpha = pow(alpha, MAT.alphaPow);

    float3 N = vOut.normal;
    const float flipNormal = (isFrontFace * 2 - 1);
    if (MAT.normalTexture >= 0)
    {

        float3 normalTex = ((textures.T[MAT.normalTexture].Sample(gSmpLinear, vOut.uv).rgb) * 2.0) - 1.0;
        N = normalize(-(normalTex.r * vOut.tangent) + (normalTex.g * vOut.binormal) + (normalTex.b * vOut.normal * flipNormal));
    }

    float3 eye = -normalize(eyePos - vOut.worldPos.xyz);

    float ndoth = saturate(dot(N, normalize(sunDirection + eye)));
    float ndots = dot(N, sunDirection);

    float RGH = MAT.roughness[frontback] + 0.001;
    float pw = 15.f / RGH;
    float spec = pow(ndoth, pw) * 1000;

    float4 shadowPos = mul(vOut.worldPos, shadowViewProj);
    shadowPos /= shadowPos.w;

    shadowPos.xy *= 0.5;
    shadowPos.xy += 0.5;
    shadowPos.z -= 0.001;
    shadowPos.y = 1 - shadowPos.y;

    float shadow = highResShadow.SampleCmp(gSamplerDepth, shadowPos.xy, shadowPos.z);
    float shadowRGB = highResShadow.Sample(gSmpLinear, shadowPos.xy).r;
    float r = saturate(1 - shadow);
    float g = saturate(-ndots) * shadow;
    float b = saturate(ndots) * shadow;
    return float4(r, g, b, 1);
}

#elif defined(_GOURAUD_SHADING)

float4 psMain(PSIn vOut, bool isFrontFace : SV_IsFrontFace) : SV_TARGET
{
    float2 uv = vOut.pos.xy / screenSize; //        float2(2560, 1440);
    float3 prev = gPreviousFrame.Sample(gSmpLinearClamp, uv).rgb;
    float alphaB = smoothstep(0, 0.3, vOut.uv.x) * smoothstep(1, 0.7, vOut.uv.x);
    float3 colorE = lerp(prev, vOut.vertexLight.rgb, alphaB);
    return float4(colorE, 1);
}

#elif defined(_DEPTH)

float4 psMain(PSIn vOut, bool isFrontFace : SV_IsFrontFace) : SV_TARGET
{
    const sprite_material MAT = materials[vOut.material_IDX];
    const plant PLANT = plant_buffer[vOut.plant_IDX];
    const int frontback = (int)!isFrontFace;
    const float flipNormal = (isFrontFace * 2 - 1);
    float alpha = textures.T[MAT.albedoTexture].Sample(gSmpLinear, vOut.uv).a;
    alpha = pow(alpha, MAT.alphaPow);
    clip(alpha - 0.5);

    return 1;   // This will write depth, but try accumulating vegetation soon
}

#else















/*
*   -   High chance outDepth is unnesesary, but leave to test some things
*   -   2.65ms 6M pixels [earlydepthstencil]
*       4.2ms 59M pixles
*       3.6ms 59M pixels with clipping - This still seems worthwhile
*       everythign here suggest that even thougf early depthstencil isnt working its not a full version of the shader either
*       all of this was done with the very basic flat colout shading, zero texture reads, zero lights, as simple as it gets
*
*       try 2, sunlight and textures in shader
*       7.7ms 8M pixels  [earlydepthstencil]
*       10.0ms 34M pixels
*
*       Same Scene
*       9.2M triangles 6.2ms  (13M, 6M)
*       3M tri, 2.5ms         (18M, 7M)very bad   / I suspect this is the extra triangles on egdes but not sure I can mearure them
*
*       1) too many triangles - VERY BAD for high quality pixels
*       2) too many clip() quite bad
*       3) Substantially pixel limited in all cases
*/


float rand_1_06(in float uv)
{
    return frac(sin(uv * 12.9898) * 43758.5453);
}


#define frontback  ((int)!isFrontFace)
#if defined(_EARLY_Z)
[earlydepthstencil]
#endif
//SV_DepthLessEqual
float4 psMain(PSIn vOut, bool isFrontFace : SV_IsFrontFace, out float outDepth : SV_DepthGreaterEqual) : SV_TARGET
//float4 psMain(PSIn vOut) : SV_TARGET
{
    outDepth = vOut.pos.z;

#if defined(_Z_ONLY)
{
    return 1;
}
#endif

/*
if (vOut.material_IDX >= 0)
{
    float4 col = 1;
    col.r = rand_1_06(vOut.material_IDX);
    col.g = rand_1_06(vOut.material_IDX + 1);
    col.b = rand_1_06(vOut.material_IDX + 2);
    return col;
}
*/

    surface_veg surface;
    const sprite_material MAT = materials[vOut.material_IDX];
    const plant PLANT = plant_buffer[vOut.plant_IDX];

    clip(vOut.uv.y - 0.001);    // clip unused part of diamonds
    
    if (MAT.albedoTexture >= 0)
    {
        surface.albedo = textures.T[MAT.albedoTexture].Sample(gSmpLinear, vOut.uv);
    }
    else
    {
        surface.albedo = float4(0.5, 0.5, 0.5, 1);
    }
    surface.albedo.rgb *= vOut.AlbedoScale * 2 * MAT.albedoScale[frontback];
    float alpha = pow(surface.albedo.a, MAT.alphaPow);

    //float G = dot(surface.albedo.rgb, float3(0.229, 0.587, 0.114));
    //surface.albedo.rgb = lerp(surface.albedo.rgb, G * float3(1.5, 0.9, 0.5), 0.7);
#if defined(_PIXEL_COUNT)
    {
        uint slot;
        if (alpha < 0.5)
        {
            InterlockedAdd(feedback_Veg[0].numPixClip, 1, slot);
        }
        else
        {
            InterlockedAdd(feedback_Veg[0].numPixPass, 1, slot);
        }
}
#endif

    

#if defined(_DEBUG_PIXELS)
    {
        if (vOut.uv.y < 0)         return float4(0, 0, 1, 1);  // blue for top tip of diamond
        if ((alpha - 0.5) < 0)     return float4(1, 0, 0, 0.3);
        if (alpha < 0.9)           return float4(1-alpha, 1-alpha, 0, 1);
        if (!isFrontFace)          return float4(1, 0, 1, 1);
    }
#endif

#if defined(_DEBUG_PIVOTS)
    {
        surface.albedo.rgb = surface.albedo.rgb * vOut.debugColour;
        surface.albedo.a = 1;
        return surface.albedo;
    }
#endif




    clip(alpha - 0.5);
    alpha = smoothstep(0.5 * 0.9, 0.8, alpha);

    
    const float flipNormal = (isFrontFace * 2 - 1);
    surface.normal = vOut.normal *flipNormal;
    
    if (MAT.normalTexture >= 0)
    {
        float3 normalTex = ((textures.T[MAT.normalTexture].Sample(gSmpLinear, vOut.uv).rgb) * 2.0) - 1.0;
        
        normalTex.b = 1 - (normalTex.r * normalTex.r + normalTex.g * normalTex.g);
        surface.normal = normalize((normalTex.r * vOut.tangent) + (normalTex.g * vOut.binormal) + (normalTex.b * vOut.normal * flipNormal));
    }
    /*
    float3 NAdjusted;
    //float3 NCone = vOut.lighting.rbg;
    //NCone.r *= -1;
    //NCone.b *= -1;
    NAdjusted.r = dot(surface.normal, view[0].xyz);
    NAdjusted.g = dot(surface.normal, view[1].xyz);
    NAdjusted.b = dot(surface.normal, view[2].xyz);
    //NAdjusted.rb = NAdjusted.g;
    NAdjusted *= 0.5;
    NAdjusted += 0.5;
    NAdjusted = pow(NAdjusted, 3.1);
    NAdjusted = surface.normal;
    */
   
    //return float4(NAdjusted, 1);

    float3 eye = -normalize(eyePos - vOut.worldPos.xyz);
    surface.NoH = saturate(dot(surface.normal, -normalize(sunDirection + eye)));
    surface.NoS = dot(surface.normal, -sunDirection);
    surface.NoE = dot(surface.normal, -eye);

    //return float4(surface.NoH, surface.NoH, surface.NoH, 1);

    // sunlight dapple
    float dappled;
#if defined(_BILLBOARD)
    dappled = 1 -vOut.Shadow;
#else
    float2 sunUV;
    sunUV.x = dot(vOut.worldPos.xyz, sunRightVector);
    sunUV.y = dot(vOut.worldPos.xyz, sunUpVector);
    dappled = gDappledLight.Sample(gSmpLinear, frac(sunUV.xy * PLANT.shadowUVScale * 10)).r;
    dappled = smoothstep(vOut.Shadow - PLANT.shadowSoftness, vOut.Shadow + PLANT.shadowSoftness, dappled);
    dappled = smoothstep(0.5 - PLANT.shadowSoftness, 0.5 + PLANT.shadowSoftness, dappled);
    //dappled = 1 - vOut.Shadow;
#endif

    // pre multiply shadow
    surface.fresnel = schlick(0.06, 1 - abs(dot(vOut.normal, sunDirection)));  // sun fresnel and needs smoothe for SSS
    surface.ao = pow(vOut.AmbietOcclusion, 0.3);
    surface.roughness = MAT.roughness[frontback] + 0.001;

    float gray = dot(surface.albedo.rgb, float3(0.229, 0.587, 0.114));
    surface.translucency = gray* pow(surface.albedo.rgb / gray, 2) *vOut.TranslucencyScale* MAT.translucency;
    if (MAT.translucencyTexture >= 0)
    {
        surface.translucency = textures.T[MAT.translucencyTexture].Sample(gSmpLinear, vOut.uv).rgb * vOut.TranslucencyScale * MAT.translucency;
    }

    float3 color = pbr_Vegetation(surface, vOut.diffuseLight.rgb * dappled, 1.0);
    // float3 color = pbr_Vegetation(surface, vOut.diffuseLight.rgb, 1.0);
    //color *= 0.1;
    color += vOut.otherLights * surface.albedo.rgb;
    color = clamp(color, 0, 10000); // just large but gets rid of NaN
    color *= vOut.outscatter;
    color += vOut.inscatter;
    //atmosphere(color, vOut.pos.xy, vOut.FogDistance);
    JHFAA_alpha(color, alpha, vOut.pos.xy); // 3% roughly

    return float4(color, 1);
    
}

#endif














// animate experietnts
    // Human
/*
    float3 humanPos = 4 * float3(sin(time * 0.1), 0, cos(time * 0.1));
    float3 humanDir = -normalize(float3(-humanPos.z, 0, humanPos.x)); //???
    float3 tangent = normalize(humanPos);
    float3 rel = _plantRoot - humanPos;
    rel.y = 0;
    if (length(rel) < 5.5)
    {
        if (dot(rel, tangent) < 0)
            tangent *= -1;
        tangent.y = -0.05;

        float3 vertDistance = _pos - humanPos;
        vertDistance.y = 0;
        strenth = 1.4 * pow(smoothstep(0.6, 0.3, length(vertDistance)), 2);
        float3 newWind = rot_xz(normalize(tangent), -_rot) * strenth;
        newWind = rot_xz(normalize(tangent), -_rot) * strenth;
        newWind.y = -0.2;
        _wind += newWind;

    }
*/
/*
    float3 humanPos = 2 * float3(sin(time * 0.1), 0, cos(time * 0.1));
    float3 humanDir = -normalize(float3(-humanPos.z, 0, humanPos.x)); //???
    float3 tangent = normalize(humanPos);
    float3 rel = _plantRoot - humanPos;
    rel.y = 0;
    if (length(rel) < 10.5)
    {

        float3 vertDistance = _pos - humanPos;
        vertDistance.y = 0;
        float wave = 1.5 + sin(time * length(vertDistance) * 0.002) ;
        strenth = 0.6 * pow(smoothstep(10.0, 5.3, length(vertDistance)), 2.2) * wave;
        float3 newWind = rot_xz(normalize(rel), -_rot) * strenth;
        //newWind.y = -0.2;


        float ss = sin(_pos.x * 1.3 - time * 1.0) + sin(_pos.z * 1.3 - time * 1.8);
        float3x3 rot = AngleAxis3x3(ss * 0.63, float3(0, 1, 0));
        newWind = normalize(mul(newWind, rot));


        _wind += newWind * strenth;

    }
*/
/*
    // Helicopter
    float2 helicopterPos = 20 * float2(sin(time * 0.05), cos(time * 0.05));
    float3 rel = _pos.xz - helicopterPos;
    if (length(rel) < 10)
    {
        float strenth = 50 * saturate(1.f / length(rel));
        //_wind.xz += strenth * normalize(rel);
    }

    // Vehicle
    float2 vehiclePos = 10 * float2(sin(time * 0.01), cos(time * 0.01));
    float2 vehicleDir = normalize(float2(-vehiclePos.y, vehiclePos.x)); //???
    float2 vehicleTangent = normalize(vehiclePos); //???
    rel = _pos.xz - vehiclePos;
    if (length(rel) < 20)
    {
        float2 dir = normalize(vehicleDir + vehicleTangent * 0.5);
        float strenth = 50 * saturate(1.f / length(rel)) * dot(normalize(rel), dir);
        //_wind.xz += strenth * vehicleTangent;
    }
*/
