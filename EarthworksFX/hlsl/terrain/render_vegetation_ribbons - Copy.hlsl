
#include "groundcover_defines.hlsli"
#include "groundcover_functions.hlsli"		
#include "vegetation_defines.hlsli"
#include "../render_Common.hlsli"




SamplerState gSampler;
SamplerState gSamplerClamp;             // for blending with hald-buffer
Texture2D gAlbedo : register(t0);
TextureCube gEnv : register(t1);
Texture2D gHalfBuffer : register(t2);

Texture2D gDappledLight : register(t3);

struct ribbonTextures
{
    Texture2D<float4> T[4096];
};
ParameterBlock<ribbonTextures> textures;



StructuredBuffer<sprite_material> materials;
//StructuredBuffer<ribbonVertex8> instanceBuffer;     // THIS HAS TO GO
//StructuredBuffer<xformed_PLANT> instances;

StructuredBuffer<plant>             plant_buffer;
StructuredBuffer<plant_instance> instance_buffer;
StructuredBuffer<block_data> block_buffer;
StructuredBuffer<ribbonVertex8> vertex_buffer;

cbuffer gConstantBuffer
{
    float4x4 viewproj;
    float3 eyePos;
    
    float time;
    float bake_radius_alpha;
    int bake_AoToAlbedo;

    float3 windDir;
    float windStrength;

    int showDEBUG;
    
};


cbuffer PerFrameCB
{
    bool gConstColor;
    float3 gAmbient;
	
	// volume fog parameters
    float2 screenSize;
    float fog_far_Start;
    float fog_far_log_F; //(k-1 / k) / log(far)		// FIXME might be k-2 to make up for half pixel offsets
	
    float fog_far_one_over_k; // 1.0 / k
    float fog_near_Start;
    float fog_near_log_F; //(k-1 / k) / log(far)		// FIXME might be k-2 to make up for half pixel offsets
    float fog_near_one_over_k; // 1.0 / k
	
    float gisOverlayStrength;
    int showGIS;
    float redStrength;
    float redScale;
    float4 gisBox;
	
    float redOffset;
    float3 padding;	
};

#define AmbietOcclusion lighting.w
#define AlbedoScale colour.x
#define TranslucencyScale colour.y
#define Shadow sunUV.z
#define PlantIdx flags.w


float4 sunLight(float3 posKm)
{
    float2 sunUV;
    float dX = dot(posKm.xz, normalize(sunDirection.xz));
    sunUV.x = saturate(0.5 - (dX / 1600.0f));
    sunUV.y = 1 - saturate(posKm.y / 100.0f);
    return SunInAtmosphere.SampleLevel(gSmpLinearClamp, sunUV, 0) * 0.07957747154594766788444188168626;
}

/*
struct PSIn
{
    float4 pos : SV_POSITION;
    half3 normal : NORMAL;
    half3 tangent : TANGENT;
    half3 binormal : BINORMAL;
    
    half3 diffuseLight : COLOR;
    //float3 specularLight : COLOR1;
    
    float2 uv : TEXCOORD; // texture uv
    float4 lighting : TEXCOORD1; // uv, sunlight, ao
    nointerpolation uint4 flags : TEXCOORD2; // material        BLENDINDICES[n]
    float3 eye : TEXCOORD3;                 // can this become SVPR  or per plant somehow, feels overkill here per vertex
    float4 colour : TEXCOORD4;
    float2 lineScale : TEXCOORD5;   // its w and h for boillboard
    float3 sunUV : TEXCOORD6;
};
*/
struct PSIn
{
    float4 pos : SV_POSITION;
    
    
    half3 normal : NORMAL;
    half3 tangent : TANGENT;
    half3 binormal : BINORMAL;
    
    half3 diffuseLight : COLOR;
    //float3 specularLight : COLOR1;
    
    float2 uv : TEXCOORD; // texture uv
    float4 lighting : TEXCOORD1; // uv, sunlight, ao
    nointerpolation uint4 flags : TEXCOORD2; // material        BLENDINDICES[n]
    float3 eye : TEXCOORD3; // can this become SVPR  or per plant somehow, feels overkill here per vertex
    float4 colour : TEXCOORD4;
    float3 lineScale : TEXCOORD5;   // its w and h for boillboard
    float3 sunUV : TEXCOORD6;

    float4 viewTangent : TEXCOORD7;
    float4 viewBinormal : TEXCOORD8;

    uint shadingRate : SV_ShadingRate;
};



inline float3 rot_xz(const float3 v, const float yaw)
{
    float s, c;
    sincos(yaw, s, c);
    return float3((v.x * c) + (v.z * s), v.y, (-v.x * s) + (v.z * c));
}


inline float3 yawPitch_9_8bit(int yaw, int pitch, const float r) // 9, 8 bits 8 b
{
    float plane, x, y, z;
    sincos((yaw - 256) * 0.01227 - r, z, x);
    sincos((pitch - 128) * 0.01227, y, plane);
    return float3(plane * x, y, plane * z);
}

/*
inline void extractPosition(inout PSIn o, const ribbonVertex8 v, const float scale, const float rotation, const float3 root)
{
    float3 p = float3((v.a >> 16) & 0x3fff, v.a & 0xffff, (v.b >> 18) & 0x3fff) * objectScale - objectOffset;
    o.pos.xyz = root + rot_xz(p, rotation) * scale;
    o.pos.w = 1;
    o.eye = normalize(o.pos.xyz - eyePos);
}*/

// These two can optimize, its only tangent that differs
inline void extractTangent(inout PSIn o, const ribbonVertex8 v, const float rotation)
{
    o.binormal = yawPitch_9_8bit(v.c >> 23, (v.c >> 15) & 0xff, rotation); // remember to add rotation to yaw
    o.tangent = normalize(cross(o.binormal, -o.eye));
    o.normal = cross(o.binormal, o.tangent);
}

inline void extractTangentFlat(inout PSIn o, const ribbonVertex8 v, const float rotation)
{
    o.binormal = yawPitch_9_8bit(v.c >> 23, (v.c >> 15) & 0xff, rotation); // remember to add rotation to yaw
    o.tangent = yawPitch_9_8bit(v.d >> 23, (v.d >> 15) & 0xff, rotation); // remember to add rotation to yaw
    // this is done in animate  o.normal = cross(o.binormal, o.tangent);
}

inline void extractUVRibbon(inout PSIn o, const ribbonVertex8 v)
{
    o.uv = float2(((v.d >> 8) & 0x7f) * 0.00390625, ((v.c) & 0x7fff) * 0.00390625); // allows 16 V repeats scales are wrong decide
    o.uv.y = 1 - o.uv.y;
}

inline void extractFlags(inout PSIn o, const ribbonVertex8 v)
{
    o.flags.x = (v.b >> 8) & 0x2ff; //  material
    o.flags.y = (v.a >> 30) & 0x1; //  start bool
    o.flags.z = (v.d & 0xff); //  int radius
}


float rand_1_05(in float2 uv)
{
    float2 noise = (frac(sin(dot(uv, float2(12.9898, 78.233) * 2.0)) * 43758.5453));
    return frac(noise.x + noise.y);
}

float3x3 AngleAxis3x3(float angle, float3 axis)
{
    float c, s;
    sincos(angle, s, c);

    float t = 1 - c;
    float x = axis.x;
    float y = axis.y;
    float z = axis.z;

    return float3x3(
        t * x * x + c, t * x * y - s * z, t * x * z + s * y,
        t * x * y + s * z, t * y * y + c, t * y * z - s * x,
        t * x * z - s * y, t * y * z + s * x, t * z * z + c
    );
}

float windstrength(float D, float R, float strength, float variance)
{
    float value = sin(D * 0.6 - time * 0.85);
    return strength + (value * variance);
}

float sway(float D, float strength, float variance)
{
    float value = sin(D * 2.1 - time * 1.5);
    return strength + (value * variance);
}

float twist(float D, float strength, float variance)
{
    float value = sin(D * 3.1 - time * 3.5);
    return strength + (value * variance);
}

void windAnimate(inout PSIn vertex, float3 plantRoot, int _rotate)
{
    float3 root = float3(0, 0, 0);
    float3 dir = float3(0, 1, 0);
    float dirL = 0.4f;

    float3 relative = vertex.pos.xyz - plantRoot - root;
    float dL = saturate(dot(relative, dir) / dirL);
    float dL2 = pow(dL, 0.5);

    float3 windDir = float3(1, 0, 0);
    float3 windRight = normalize(cross(windDir, dir));
    float windStrength = windstrength(dot(windDir, vertex.pos.xyz), dot(windRight, vertex.pos.xyz), 0.2, 0.1); // already converted to radians - figure out how

    float W = windStrength * dL2 * 1 * sway(rand_1_05(plantRoot.xz), 0.2, 0.0093);
    float3x3 rot = mul(AngleAxis3x3(-W, windRight), AngleAxis3x3(-W * 2, dir));

    float Wside = windStrength * dL2 * sway(rand_1_05(plantRoot.xz), 0.5, 0.043);
    rot = mul(rot, AngleAxis3x3(Wside, windDir));

    
    float3 newV = mul(rot, relative);

    float scale = pow(rcp(abs(1 + W)), 0.2);
    vertex.pos.xyz = plantRoot + root + newV * scale;

    vertex.binormal = mul(rot, vertex.binormal);
    vertex.normal = mul(rot, vertex.normal );

}


void windAnimate2(inout PSIn vertex, float3 plantRoot)  // plantRoot only for randomness for now
{
    float3 root = float3(0, 0, 0);
    float3 dir = float3(0, 1, 0);
    float dirL = 2.4f;  // maize

    float3 relative = vertex.pos.xyz - root;
    //float dL = saturate(dot(relative, dir) / dirL);
    float dL = saturate(length(relative) / dirL);
    float dl2 = pow(dL, 1.8);
    float biNormScale = 1.8 * pow(dL, 0.8);

    float3 windDir = float3(0, 0, 1);
    float3 windRight = normalize(cross(relative, dir));
    float windStrength = (0.2 + 0.15 * sin(rand_1_05(plantRoot.xz) + time * 0.3) + 0.06 * sin(rand_1_05(plantRoot.xy) + time * 2.5));
    windStrength += 0.01 * sin(rand_1_05(plantRoot.xz) + time * 6.05);
    //windStrength = 0.6 * sin(time*0.3);
    windStrength *= 0.1;

    float scale = windStrength * dl2 / dL;
    scale *= scale;
    scale += 1;
    scale = 1 / sqrt(scale);
    
    vertex.pos.xyz += windDir * windStrength * dirL * dl2;
    vertex.pos.xyz *= scale;

    float flutter = frac(vertex.pos.x * 1) + frac(vertex.pos.y * 1) + sin(time * frac(vertex.pos.x * 1) * 0.01) * 0.5 + sin(time * 5) * 0.1 + sin(time * 8) * 0.05;

    
    // this one must scale look at dezmoz
    vertex.binormal = normalize(vertex.binormal + windDir * windStrength * biNormScale);
    //vertex.tangent = mul(AngleAxis3x3(flutter, vertex.binormal), vertex.tangent); // flutter
    vertex.normal = cross(vertex.binormal, vertex.tangent);
}

void windAnimateOnce(inout PSIn vertex, float3 root, float3 rootDir, float stiffness, float frequency, float flutter, float _bendStrength)
{
    float3 relative = vertex.pos.xyz - root;
    float rnd = frac(rand_1_05(root.xz));

    float3 windRight = normalize(cross(rootDir, windDir)); // HAS to come from teh root binormal
    float bendStrength = 0.5 * _bendStrength + 0.3 * sin(time / frequency * 6 + rnd * 10);

    //windStrength *= 10;
    bendStrength *= stiffness * pow(windStrength / 25.f, 1.5);
    //windStrength = 1.5;

    float3x3 rot = AngleAxis3x3(-bendStrength, windRight);

    float scale = 1.f;// / pow(1 + bendStrength, 0.41); //length compensation

    //float flutterStrength = 0.2 + 0.1 * sin(time * frequency * 3 + rnd * 4);
    //float3x3 rotflutter = AngleAxis3x3(-bendStrength * flutter, vertex.binormal);
    
    vertex.pos.xyz = root + mul(relative, rot) * scale;
    vertex.binormal = mul(vertex.binormal, rot);
    //vertex.tangent = mul(vertex.tangent, rot);
    //vertex.tangent = mul(vertex.tangent, rotflutter);
    //vertex.tangent = mul(AngleAxis3x3(flutter, vertex.binormal), vertex.tangent); // flutter
    // normal is done after
}

/*
Need to find a way to run this semi in reverse without applying then run backwards to calculate
- Wind has a bend strength, and ossilate strength, where bend strength depends on teh dot procutct of wind and binormal
  as it starst to line up we want to reduce bend strength but keep ossilate strength
- The other option is simpy to reduce bend strenhtg on lower levels 
- The other option would be to compute all of the base pivots in compute beforehand, and onyl do the leaves here
*/
void windAnimateAll(inout PSIn vertex, uint g, uint h, uint vId, const plant PLANT, float rotation, float Iscale)
{
    //float frequencyShift = sqrt(1.f / Iscale); //??? rsqrt(Iscale)
    float frequencyShift = Iscale; //??? rsqrt(Iscale)  wghile sqrt is teh right maths this is one random number we have and betetr to exageraet the frequencies between teh different plants
    
    float scale = 2.1    +0.8 * sin(time * 0.07);
    // leaf first
    if ((h & 0xff) > 0)
    {
        ribbonVertex8 vRoot = vertex_buffer[vId - (h & 0xff)];
        float3 root = float3((vRoot.a >> 16) & 0x3fff, vRoot.a & 0xffff, (vRoot.b >> 18) & 0x3fff) * PLANT.scale - PLANT.offset;
        float3 rootDir = yawPitch_9_8bit(vRoot.c >> 23, (vRoot.c >> 15) & 0xff, rotation);

        float stiff =         ((h >> 16) & 0xff) * 0.004 * 30;
        float freq = ((h >> 8) & 0xff) * 0.004;
        float index = ((h >> 24) & 0xff) * 0.004;
        
        float dx =     index * (root.y / 2.5); // secodn scales overall plant
        windAnimateOnce(vertex, root, rootDir, dx * scale * stiff * 1, freq * frequencyShift, 0.f, 0.0f);
    }

    //float pivotShift = pow(saturate(vertex.pos.y / 3.2), 0.2);
    //windAnimateOnce(vertex, float3(0, 0, 0), float3(0, 1, 0), pivotShift / 2 * scale, 0.95f / Iscale, 0.f, 1.f);

    float pivotShift = pow(saturate(dot(vertex.pos.xyz - PLANT.rootPivot.root, PLANT.rootPivot.extent)), PLANT.rootPivot.shift) / PLANT.rootPivot.stiffness;
    windAnimateOnce(vertex, PLANT.rootPivot.root, PLANT.rootPivot.extent, pivotShift / 2 * scale, PLANT.rootPivot.frequency * frequencyShift, 0.f, 1.f);
}


PSIn
    vsMain(
    uint vId : SV_VertexID, uint iId : SV_InstanceID)
{
    PSIn output = (PSIn) 0;

#if defined(_BILLBOARD)
    const plant_instance INSTANCE = instance_buffer[vId];
    const plant PLANT = plant_buffer[INSTANCE.plant_idx];

    output.pos.xyz = INSTANCE.position;
    output.pos.w = 1;
    output.eye = normalize(output.pos.xyz - eyePos);

    output.binormal = float3(0, 1, 0);
    output.tangent = normalize(cross(output.binormal, -output.eye));
    output.normal = cross(output.binormal, output.tangent);

    output.AlbedoScale = 1;
    output.TranslucencyScale = 1;

    output.Shadow = 0;
    output.AmbietOcclusion = 1;

    if ((dot(output.pos.xyz, sunRightVector)) > 10 && (dot(output.pos.xyz, sunRightVector)) < 50)
    {
        output.Shadow = 1;
    }

    output.flags.x = PLANT.billboardMaterialIndex;
    output.PlantIdx = INSTANCE.plant_idx;

    output.lineScale.xy = PLANT.size * INSTANCE.scale;

    output.lineScale.z = 0.6;

    output.viewTangent = mul( float4( output.tangent * output.lineScale.x, 0), viewproj);
    output.viewBinormal = mul( float4( output.binormal * output.lineScale.y, 0), viewproj);
    output.pos = mul(output.pos, viewproj);

    output.diffuseLight = sunLight(output.pos.xyz * 0.001).rgb;

    output.shadingRate = 1;
    
    return output;
#else

    
    const block_data BLOCK = block_buffer[iId];
    const plant_instance INSTANCE = instance_buffer[BLOCK.instance_idx];
    const plant PLANT = plant_buffer[INSTANCE.plant_idx];
    const ribbonVertex8 v = vertex_buffer[BLOCK.vertex_offset + vId];
    

    // position
    {
        float3 p = float3((v.a >> 16) & 0x3fff, v.a & 0xffff, (v.b >> 18) & 0x3fff) * PLANT.scale - PLANT.offset;
        //output.pos.xyz = INSTANCE.position + rot_xz(p, INSTANCE.rotation) * INSTANCE.scale;
        output.pos.xyz = rot_xz(p, INSTANCE.rotation);
        output.pos.w = 1;
        // also after animate output.eye = normalize(output.pos.xyz - eyePos);

        output.colour.a = 1; // new alpha component but just for bake
#if defined(_BAKE)
        float3 rootPos = float3(0, 0, 0);
        rootPos.y = 0;
        //output.pos.xyz =  rootPos + rot_xz(p, INSTANCE.rotation) * INSTANCE.scale;
        output.pos.xyz =  rot_xz(p, INSTANCE.rotation);
        output.pos.w = 1;
        //output.eye = normalize(output.pos.xyz - eyePos);
        
        p.y = 0;
        float R = length(p);
        if (R > 0.3f)      output.colour.a = 0;
        output.colour.a = 1.f - smoothstep(bake_radius_alpha * 0.75f, bake_radius_alpha, R);
        //float tipFade = 1 - 
        //if(bake_AoToAlbedo > 0.5)
        //{
         //   output.colour.a = 1.f;
        //}
#endif
    }
    
    
    if (v.a >> 31)
    {
        extractTangentFlat(output, v, INSTANCE.rotation);
    }
    else
    {
        extractTangent(output, v, INSTANCE.rotation); // Likely only after abnimate, do only once
    }
    
    extractUVRibbon(output, v);
    extractFlags(output, v);
    //extractColor(output, v);
    {
        output.AlbedoScale = 0.1 + ((v.f >> 8) & 0xff) * 0.008; // albedo
        output.TranslucencyScale = ((v.f >> 0) & 0xff) * 0.008; //tanslucency
        //output.colour.b = saturate((v.d & 0xff) * PLANT.radiusScale / length(output.pos.xyz - eyePos) * 200);
        //output.colour.b = saturate((output.colour.b - 0.5) * 50);
    }
    
    {
        float3 lightCone = yawPitch_9_8bit(v.e >> 23, (v.e >> 15) & 0xff, INSTANCE.rotation);
        float sunCone = (((v.e >> 8) & 0x7f) * 0.01575) - 1; //cone7
        float sunDepth = (v.e & 0xff) * 0.00784; //depth8   // sulight % how deep inside this plant 0..1 sun..shadow
        float a = saturate(dot(normalize(lightCone - sunDirection * PLANT.sunTilt), sunDirection)); // - sunCone * 0 sunCosTheta sunCone biasess this bigger or smaller 0 is 180 degrees
        //float b = sunDepth + a * (PLANT.bDepth - sunDepth);
        output.Shadow = saturate(a * (sunDepth) + sunDepth); // darker to middle
        // look at top one again, This one is BAD BAD BAD at making very transparent planst work

        //if (abs(dot(output.pos.xyz, sunRightVector)) < 2 && abs(dot(output.pos.xyz, sunRightVector)) < 5 && abs(dot(output.pos.xyz, sunUpVector)) < 15)
        if ((dot(output.pos.xyz, sunRightVector)) > 10 && (dot(output.pos.xyz, sunRightVector)) < 50)
        {
            output.Shadow = 1;
            //output.Sunlight = 0;
        }

        

        output.AmbietOcclusion = pow(((v.f >> 24) / 255.f), 3);
        
#if defined(_BAKE)
    output.lighting.rgb = lightCone;
#endif
    }

    

    //output.AmbietOcclusion = 0.5f;

    // Now do wind animation
    // maybe before lighting, although for now it doesnt seem to matter
    // --------------------------------------------------------------------------------------------------------
    //windAnimate(output, INSTANCE.position, v.a >> 31);

    //windAnimate2(output, INSTANCE.position);
#if defined(_BAKE)
    output.pos.xyz *= INSTANCE.scale;
    float3 root = INSTANCE.position;
    root.y = 0;
    output.pos.xyz += root;
#else
    windAnimateAll(output, v.g, v.h, BLOCK.vertex_offset + vId, PLANT, INSTANCE.rotation, INSTANCE.scale);
    //SCALE and root here.
    output.pos.xyz *= INSTANCE.scale;
    output.pos.xyz += INSTANCE.position;
#endif

    

    output.eye = normalize(output.pos.xyz - eyePos);
    if (!(v.a >> 31))
    {
        output.tangent = normalize(cross(output.binormal, -output.eye));
    }
    output.normal = cross(output.binormal, output.tangent);


    if ((dot(output.pos.xyz, sunRightVector)) > 10 && (dot(output.pos.xyz, sunRightVector)) < 50)
    {
        output.Shadow = 1;
    }

    output.diffuseLight = sunLight(output.pos.xyz * 0.001).rgb;

    // thsi value determines if it splits into 2 or 4 during the geometry shader
    float d = length(output.pos.xyz - eyePos);
    //flags.w is now plantIdxoutput.flags.w = output.flags.z * PLANT.radiusScale / d * 10000;
    output.lineScale.x = pow(output.flags.z / 255.f, 2) * PLANT.radiusScale;

    //output.viewTangent = mul(float4(output.tangent * output.lineScale.x, 0), viewproj);
    //output.pos = mul(output.pos, viewproj);
    
    output.PlantIdx = INSTANCE.plant_idx;
    output.shadingRate = 1;
    return output;
#endif
}




#if defined(_BILLBOARD)
[maxvertexcount(4)]
void gsMain(point PSIn pt[1], inout TriangleStream<PSIn> OutputStream)
{
    PSIn v = pt[0];
    float scale = 1 - pt[0].lineScale.z;
/*
    v.uv = float2(0.5, 1.1);
    v.pos = pt[0].pos - pt[0].viewBinormal * 0.1;
    v.AmbietOcclusion = 0.3;
    OutputStream.Append(v);

    v.uv = float2(1.0 -  scale/2 , 0.5);
    v.pos = pt[0].pos + pt[0].viewTangent * pt[0].lineScale.z + pt[0].viewBinormal * 0.5;
    v.AmbietOcclusion = 0.6;
    OutputStream.Append(v);
        
    v.uv = float2(0.0 + scale/2, 0.5);
    v.pos = pt[0].pos - pt[0].viewTangent * pt[0].lineScale.z + pt[0].viewBinormal * 0.5;
    OutputStream.Append(v);

    v.uv = float2(0.5, -0.1);
    v.pos = pt[0].pos + pt[0].viewBinormal * 1.1;
    v.AmbietOcclusion = 1.0;
    OutputStream.Append(v);
*/

    v.uv = float2(0.0, 1);
    //v.AmbietOcclusion = 1.03;
    v.pos = pt[0].pos - pt[0].viewTangent;
    OutputStream.Append(v);

    v.uv = float2(1.0, 1);
    v.pos = pt[0].pos + pt[0].viewTangent;
    OutputStream.Append(v);
        
    v.uv = float2(0.0, 0);
    //v.AmbietOcclusion = 1.0;
    v.pos = pt[0].pos - pt[0].viewTangent + pt[0].viewBinormal;
    OutputStream.Append(v);

    v.uv = float2(1.0, 0);
    v.pos = pt[0].pos + pt[0].viewTangent + pt[0].viewBinormal;
    OutputStream.Append(v);
  
}
#else
// HOLY fukcing shit, this is bad anythign above 6
// at 6 I can still get gain out of this
[maxvertexcount(4)]
void gsMain(line PSIn L[2], inout TriangleStream<PSIn> OutputStream)
{

    
    //float lineScale = 1.f;
    PSIn v;

    //const float3 sunDir = normalize(float3(-0.6, -0.5, -0.8));
    //const float3 sunR = normalize(cross(float3(0, 1, 0), sunDir));
    //const float3 sunU = normalize(cross(sunDir, sunR));             // ThisHAS to come in a a constant
    
    
    //if ((L[1].flags.y == 1) && (L[0].flags.w > 1))
    if ((L[1].flags.y == 1))
    {
        v = L[0];
        v.uv.x = 0.5 + L[0].uv.x;
        v.pos = L[0].pos - float4(v.tangent * v.lineScale.x, 0);
        v.sunUV.x = dot(v.pos.xyz, sunRightVector);
        v.sunUV.y = dot(v.pos.xyz, sunUpVector);
        //v.pos = mul(L[0].pos + float4(v.tangent * v.lineScale, 0), viewproj);
        v.pos = mul(v.pos, viewproj);
        OutputStream.Append(v);

        v.uv.x = 0.5 - L[0].uv.x;
        v.pos = L[0].pos + float4(v.tangent * v.lineScale.x, 0);
        v.sunUV.x = dot(v.pos.xyz, sunRightVector);
        v.sunUV.y = dot(v.pos.xyz, sunUpVector);
        //v.pos = mul(L[0].pos - float4(v.tangent * v.lineScale, 0), viewproj);
        v.pos = mul(v.pos, viewproj);
        OutputStream.Append(v);

      /*
        // FIXME I have reassigned flags.w for now DO NOT USE THIS
        if (L[0].flags.w > 10)      // but we can do the test rigth here from radius
        {
            float l1 = length(L[1].pos.xyz - L[0].pos.xyz);
            float4 posBezier = float4(((L[0].pos.xyz + L[1].pos.xyz) * 0.5) + ((L[0].binormal - L[1].binormal) * l1 * 0.125), 1);

            v.uv.y = (L[0].uv.y + L[1].uv.y) * 0.5;
            v.uv.x = 0.5 + L[0].uv.x;
            v.pos = mul(posBezier - float4(v.tangent * pow(v.flags.z / 255.f, 2) * radiusScale, 0), viewproj);
            OutputStream.Append(v);

            v.uv.x = 0.5 - L[0].uv.x;
            v.pos = mul(posBezier + float4(v.tangent * pow(v.flags.z / 255.f, 2) * radiusScale, 0), viewproj);
            OutputStream.Append(v);
        }
        */
        
        v = L[1];
        v.uv.x = 0.5 + L[1].uv.x;
        v.pos = L[1].pos - float4(v.tangent * v.lineScale.x, 0);
        v.sunUV.x = dot(v.pos.xyz, sunRightVector);
        v.sunUV.y = dot(v.pos.xyz, sunUpVector);
        v.pos = mul(v.pos, viewproj);
        OutputStream.Append(v);

        v.uv.x = 0.5 - L[1].uv.x;
        v.pos = L[1].pos + float4(v.tangent * v.lineScale.x, 0);
        v.sunUV.x = dot(v.pos.xyz, sunRightVector);
        v.sunUV.y = dot(v.pos.xyz, sunUpVector);
        v.pos = mul(v.pos, viewproj);
        OutputStream.Append(v);
    }
}
#endif






#if defined(_BAKE)

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

    sprite_material MAT = materials[vOut.flags.x];


    float alpha = textures.T[MAT.albedoTexture].Sample(gSampler, vOut.uv.xy).a;
    if (MAT.alphaTexture >= 0)
    {
        //alpha = textures.T[MAT.alphaTexture].Sample(gSampler, vOut.uv.xy).r;
        //
    }
    alpha *= vOut.colour.a;

    float rnd = 0.25 + 0.5 * rand_1_05(vOut.pos.xy);
    clip(alpha - rnd);

    float3 color = textures.T[MAT.albedoTexture].Sample(gSampler, vOut.uv.xy).rgb;// * vOut.AlbedoScale * pow(vOut.AmbietOcclusion, 2);


    int frontback = (int) !isFrontFace;
    color = color * vOut.AlbedoScale  * MAT.albedoScale[frontback] * 2.f;
    if(bake_AoToAlbedo)
    {
        color = color * vOut.AmbietOcclusion;
    }
    output.albedo = float4(pow(color, 1.0 / 2.2), 1);
    

    float3 N = vOut.normal;
    if (MAT.normalTexture >= 0)
    {
        float3 n = (textures.T[MAT.normalTexture].Sample(gSampler, vOut.uv.xy).rgb * 2) - 1;
        //N = (-n.r * vOut.tangent) + (n.g * vOut.binormal) + (n.b * vOut.normal);
    }
    N *= (isFrontFace * 2 - 1);


    
//lighting.rgb
    float3 NAdjusted = N;
    NAdjusted = normalize(vOut.lighting.rgb);

    float3 NCone =vOut.lighting.rbg;
    NCone.r *= -1;
    NCone.b *= -1;

    N = normalize(NCone + N * 0.3);
    

    NAdjusted.r = -N.r * 0.5 + 0.5;
    NAdjusted.g = N.g * 0.5 + 0.5;
    NAdjusted.b = -N.b * 0.5 + 0.5;  //fixme -

//NAdjusted = vOut.lighting.xyz * 0.5 + 0.5;
//NAdjusted.b = 1 - NAdjusted.b;


//float instance_PLANT = saturate(dot(vOut.lighting.xyz, normalize(float3(0.8, 0.4, 0.4))));
//output.albedo *= instance_PLANT * 0.3 + 0.7;

    output.normal = float4(NAdjusted, 1);       // FIXME has to convert to camera space, cant eb too hard , uprigthmaybe, and 0-1 space
    output.normal_8 = float4(NAdjusted, 1);

    

    float trans = vOut.TranslucencyScale * MAT.translucency;
    {
        if (MAT.translucencyTexture >= 0)
        {
            trans *= dot(float3(0.3, 0.4, 0.3), textures.T[MAT.translucencyTexture].Sample(gSampler, vOut.uv.xy).rgb);
        }
    }
    trans = saturate(pow(trans, 1.0 / 2.2));
    output.extra = float4(0, 0, 0, 1-trans);

    return output;
}

#else

float4 psMain(PSIn vOut, bool isFrontFace : SV_IsFrontFace) : SV_TARGET
{
    /*
    if (isFrontFace)
        return float4(frac(vOut.uv.x), 1, frac(vOut.uv.y), 1);
    else
        return float4(1, 0, vOut.uv.y, 1);
    */

#if defined(_BILLBOARD)
    clip(vOut.uv.y);    
#endif
    
    const sprite_material MAT = materials[vOut.flags.x];
    const plant PLANT = plant_buffer[vOut.PlantIdx];

    int frontback = (int) !isFrontFace;
    float flipNormal = (isFrontFace * 2 - 1);
    
    float4 albedo = textures.T[MAT.albedoTexture].SampleBias(gSamplerClamp, vOut.uv.xy, -0.0); // Bias -1 gives much betetr alpha values
    albedo.rgb *= vOut.AlbedoScale * MAT.albedoScale[frontback] * 2.f;
    float alpha = pow(albedo.a, MAT.alphaPow);

    float rnd = 0.4    +0.15 * rand_1_05(vOut.pos.xy);
    if (showDEBUG)
    {
        if ((alpha - rnd) < 0)  return float4(1, 0, 0, 0.3);
    }
    clip(alpha - rnd);
    alpha = smoothstep(rnd, 1, alpha);
    
    float3 N = vOut.normal;
    if (MAT.normalTexture >= 0)
    {
        float3 normalTex = ((textures.T[MAT.normalTexture].Sample(gSamplerClamp, vOut.uv.xy).rgb) * 2.0) - 1.0;
        N = (normalTex.r * vOut.tangent) + (normalTex.g * vOut.binormal) + (normalTex.b * vOut.normal);
    }
    N *= flipNormal;
    

    float ndoth = saturate(dot(N, normalize(sunDirection + vOut.eye)));
    float ndots = dot(N, sunDirection);

    // sunlight dapple
    float dappled;
#if defined(_BILLBOARD)
    dappled = 1 - vOut.Shadow;
#else
    dappled = gDappledLight.Sample(gSampler, frac(vOut.sunUV.xy * PLANT.shadowUVScale)).r;
    dappled = smoothstep(vOut.Shadow - PLANT.shadowSoftness, vOut.Shadow + PLANT.shadowSoftness, dappled);
#endif

    // sunlight
    float3 color = vOut.diffuseLight * 3.14 * (saturate(ndots)) * albedo.rgb * dappled;

    // environment cube light
    color += 0.7 * gEnv.SampleLevel(gSampler, N * float3(1, 1, -1), 0).rgb * albedo.rgb * pow(vOut.AmbietOcclusion, 0.3);
    
    // specular sunlight
    float RGH = MAT.roughness[frontback] + 0.01;
    float pw = 1.f / RGH;
    color += pow(ndoth, pw) * pw * 0.02 * dappled * vOut.diffuseLight;

    // translucent light    
    float3 TN = vOut.normal * flipNormal;
    float3 trans = (saturate(-ndots)) * saturate(dot(-sunDirection, vOut.eye)) * vOut.TranslucencyScale * MAT.translucency * dappled;
    {
        if (MAT.translucencyTexture >= 0)
        {
            float t = textures.T[MAT.translucencyTexture].Sample(gSampler, vOut.uv.xy).r;
            trans *= pow(t, 2);
        }
    }
    color += trans * pow(albedo.rgb, 1.5) * 150 * vOut.diffuseLight;


    // apply JHFAA to edges    
    if (alpha < 0.7)
    {
        float2 uv = vOut.pos.xy / screenSize; //        float2(2560, 1440);
        float3 prev = gHalfBuffer.Sample(gSamplerClamp, uv).rgb;
        color = lerp(prev, color, alpha);
        if (showDEBUG)
        {
            return float4(alpha, alpha, 0, 1); // yellow pixels
        }
    }

    return float4(color, 1);
}

#endif


/*
float D3DX_FLOAT_to_SRGB(float val)
{ 
    if( val < 0.0031308f )
        val *= 12.92f;
    else
        val = 1.055f * pow(val,1.0f/2.4f) — 0.055f;
    return val;
}
*/
