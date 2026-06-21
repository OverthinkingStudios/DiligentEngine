

#include "material.hlsli"
#include "render_Common.hlsli"

static const float M_PIf = 3.14159265359f;
#define oneOverPi 0.318309

/*******************************************************************************************
GGX normal distribution function (NDF).
*/
float evalGGXDistribution(float3 N, float3 H, float roughness)
{
    // This doesn't look correct, so disabled    
    float a2 = roughness * roughness;
    // dot products that we need
    float NoH = saturate(dot(N, H));
    // D term
    float D_denom = (NoH * a2 - NoH) * NoH + 1.0f;
    D_denom = M_PIf * D_denom * D_denom;
    return a2 / D_denom;
}

float evalGGXDistribution(float3 H, float2 rgns)
{
    // Numerically robust (w.r.t rgns=0) implementation of anisotropic GGX
    float anisoU = rgns.y < rgns.x ? rgns.y / rgns.x : 1.f;
    float anisoV = rgns.x < rgns.y ? rgns.x / rgns.y : 1.f;
    float r = min(rgns.x, rgns.y);
    float NoH2 = H.z * H.z;
    //float2 Hproj = float2(H.x, H.y);
    float exponent = dot(H.xy / float2(anisoU*anisoU, anisoV*anisoV), H.xy);		// this is the expensive bit
    float root = NoH2 * r*r + exponent;
	
    return r*r / (M_PIf * anisoU * anisoV * root * root);
}


float GeometrySchlickGGX(float NdotV, float k)
{
    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;
	
    return nom / denom;
}
  
float GeometrySmith(float3 N, float3 V, float3 L, float k)
{
    float NdotV = max(dot(N, V), 0.1);					// FIXME JOPHAN - clamp to zero causes black on perfect edge on pixels and that is just WRONG
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = GeometrySchlickGGX(NdotV, k);
    float ggx2 = GeometrySchlickGGX(NdotL, k);
	
    return ggx1 * ggx2;
}

float3 fromLocal(in float3 v, in float3 t, in float3 b, in float3 n)
{
    return -t * v.x + b * v.y + n * v.z;
}

float3 toLocal(in float3 v, in float3 t, in float3 b, in float3 n)
{
    return float3(dot(v, t), dot(v, b), dot(v, n));
}


// ??? I remoced all the normalize() functions as the maths should give normal lenths, but maybe check again
void applyNormalMap(in float3 dN, inout float3 n, inout float3 t, inout float3 b)
{
	n = (t * dN.x) + (b * dN.y) + (n * dN.z);
    b = b - n * dot(b, n);						// Orthogonalize tangent frame, 
    t = cross(b, n);
}


void applyNormalMap_MIKKT(in float3 dN, inout float3 n, inout float3 t, inout float3 b)
{
	n = normalize( t*dN.x - b*dN.y + n*dN.z );
	
	//t = normalize(t - n * dot(t, n));						// Orthogonalize tangent frame, 	??? Ek dink dit doen niks vir IBL, dalk vir lights
    //b = normalize(cross(n, t));
	
	
    //b = b - n * dot(b, n);						// Orthogonalize tangent frame, 
    //t = cross(b, n);
}

float3 RGBtoNormal(float3 T)
{
	return normalize(T*2 - 1);
}

float3 RGtoNormal(float2 T)			// FIXME ugly code
{
	float3 tmp = float3(T*2 - 1, 0);
	tmp.z = sqrt(1 - tmp.x*tmp.x - tmp.y*tmp.y);
	tmp.y *= -1;
	return tmp;
}

inline float pow5(float V)
{
	float V2 = V*V;
	return V2 * V2 * V;
}

inline float schlick( float f0, float V )
{
	return lerp( f0, 1, pow5(V) );			// feels to me like lerp should be faster for this
}

inline float schlick_gloss( float f0, float V, float gloss )
{
	return lerp( f0, lerp(f0, 1, gloss), pow5(V) );			// feels to me like lerp should be faster for this
}



float2 rand_2_0004(in float2 uv)
{
    float noiseX = (frac(sin(dot(uv, float2(12.9898,78.233)      )) * 43758.5453));
    float noiseY = (frac(sin(dot(uv, float2(12.9898,78.233) * 2.0)) * 43758.5453));
    return float2(noiseX, noiseY) * 0.004;
}


// FIXMe maybe prepare attib should just get the diff value here, seems little silly
void lightEmissive ( in SH_Attributes Attr, in MAT_LAYER M, inout float3 diff)
{

	if (M.texSlot[2].x)	
	{
		diff += matTextures[M.texSlot[2].x].Sample(gSmpAniso, Attr.uv).rgb * 0.6;
	}

}


void lightIBL( in SH_Attributes Attr, in layer_material mat, inout float3 diff, inout float3 spec)
{
	//float anisotropic_scale = abs(1 - dot(Attr.E, mat.N));
	//anisotropic_scale = pow( anisotropic_scale, 6) * 0.5;
	//float cube_mip = saturate(pow(mat.roughness, 0.5) + Attr.CURVATURE.z/10 + anisotropic_scale) * 6;
	
	float anisotropic_scale = saturate(dot(Attr.E, mat.N));
	anisotropic_scale = pow(anisotropic_scale, 0.2);
	float mipScale = lerp(3.0f, 0.0f, anisotropic_scale);
	
	
	float CRV = Attr.CURVATURE.z ;
	CRV = pow(CRV, 1.5) * 2;
	float cube_mip = clamp(0, 6, CRV * 6 + mipScale);
	
	//float cube_mip = clamp(0, 6, Attr.CURVATURE.z * 0.5 + mipScale);
	
	float rghMip = saturate(pow(mat.roughness, 0.5)) * 6;
	
	cube_mip = max(cube_mip, rghMip);

		// FIXME regular linear sample fpor speed	
	diff += mat.diff.rgb * mat.AO * mat.mask * gCube_Far.SampleLevel(gSmpAniso, mat.N*float3(1, 1, -1), 6).rgb;						//  70us
	spec +=  mat.spec.rgb * mat.cavity * mat.mask * gCube_Far.SampleLevel(gSmpAniso, mat.R*float3(-1, -1, 1), cube_mip).rgb;			// 100us		R11G11B10Float  2048
	/*
	if (Attr.CURVATURE.z > 1.0f)
	{
		diff = float3(0, 0.3, 0.2);
	}
	
	if (Attr.CURVATURE.z > 2.0f)
	{
		diff = float3(0, 0, 1);
	}
	
	if (Attr.CURVATURE.z > 3.0f)
	{
		diff = float3(0, 1, 1);
	}
	
	if (Attr.CURVATURE.z > 4.0f)
	{
		diff = float3(0, 1, 0);
	}
	
	if (Attr.CURVATURE.z > 5.0f)
	{
		diff = float3(1, 1, 0);
	}
	
	if (Attr.CURVATURE.z > 6.0f)
	{
		diff = float3(1, 0, 0);
	}
	*/
	//diff *= 0.001f;
	
	//diff.r = mipScale;
	
	
	//diff *= 0.00f;
	//spec *= 0.00f;
	
	//diff.r = mat.fresnel;
	//diff.rg += frac(Attr.uv * 10);//B * 0.5f + 0.5f;
	//diff.rgb += frac(abs((Attr.N)) * 10);
	//diff.rgb += frac(abs((mat.R)) * 10);
	/*
	if ( (dot(-Attr.E, mat.N)) >= 0)
	{
		spec.r = 1;
		spec.gb *= 0.1;
	}
	*/
}



void lightLayer( in SH_Attributes Attr, in layer_material mat, in float3 lightDir, in float3 lightIntensity, inout float3 diff, inout float3 spec)
{
	// BRDF's --------------------------------------------------------------------------------------------
	//diff += max(0.f, pow(saturate(dot(mat.N, -lightDir)), 1.2)) * lightIntensity * mat.diff.rgb * mat.cavity;			// maak seker die 1/pi hoort hier      * oneOverPi
	diff += saturate(dot(mat.N, -lightDir)) * lightIntensity * mat.diff.rgb * mat.cavity;
	
	float k_direct = (mat.roughness + 1) * (mat.roughness + 1) / 8;
	float specGeom = GeometrySmith(mat.N, Attr.E, -lightDir, k_direct);
	if (specGeom > 0)
	{
		float2 rg = max(mat.roughness / 2.0f, 0.002);
		rg.xy += pow(Attr.CURVATURE.yx, 3.0) / 2;
		
		// compute halfway vector
		float3 hW = normalize(Attr.E - lightDir);
		float3 h = normalize(float3(dot(hW, mat.T), dot(hW, mat.B), dot(hW, mat.N)));
		
		float dist = evalGGXDistribution(h, saturate(rg) );								// Fixme, maybe add curvature compensation, but we can pre bake it into roughness
		spec += lightIntensity * dist;// * specGeom * mat.cavity * mat.spec.rgb;			// FIXMe add large angle self shadowing
	}
	
}

