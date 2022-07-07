#include "drift_common.hlsl"

struct VertInput {
	// Vertex attribs:
	DRIFT_ATTR0 float2 uv;
	// Instance attribs:
	DRIFT_ATTR1 float4 data;
};

struct FragInput {
	float4 position : SV_POSITION;
	float2 uv_light;
	float3 uv_sdf;
	float3 uv;
	float3 uv_biome;
	float2 parallax;
	nointerpolation float2 uv_scale;
};

void VShader(in VertInput IN, out FragInput FRAG){
	// Transform:
	// IN.data.z *= 0.99; // inset for debugging.
	float2 map_pos = (IN.data.xy + IN.uv)*pow(2, IN.data.z);
	float2 world_pos = mul(float2x3(DRIFT_MATRIX_TERRAIN), float3(map_pos, 1));
	float2 clip_pos = mul(float2x3(DRIFT_MATRIX_VP), float3(world_pos, 1));
	
	// Output:
	FRAG.position = float4(clip_pos, 0, 1);
	FRAG.uv_light = 0.5*clip_pos + 0.5;
	FRAG.uv = float3(world_pos/DRIFT_ATLAS_SIZE, DRIFT_ATLAS_BIOME + 1);
	
	// FRAG.uv.xy *= DRIFT_FORESHORTENING;
	float pfactor = -(1 - 1/DRIFT_FORESHORTENING)/DRIFT_ATLAS_SIZE;
	FRAG.parallax = mul(float2x2(DRIFT_MATRIX_VP_INV), clip_pos)*pfactor;
	
	FRAG.uv_sdf = float3(32*IN.uv, IN.data.w);
	FRAG.uv_biome = float3(map_pos/DRIFT_ATLAS_SIZE, DRIFT_ATLAS_BIOME);
	
	float2x2 scale = DRIFT_MATRIX_V;
	FRAG.uv_scale = DRIFT_ATLAS_SIZE*float2(length(scale[0]), length(scale[1]));
}

SamplerState _repeat : DRIFT_SAMP2;
Texture2DArray _tiles : DRIFT_TEX1;

#define SDF_MAX_DIST (32/4)

float4 FShader(in FragInput FRAG) : SV_TARGET0{
	float4 gather4 = _tiles.Load(float4(FRAG.uv_sdf, 0));
	gather4 = SDF_MAX_DIST*(2*gather4 - 1);
	
	// Calculate sdf values.
	float2 sdf_blend = frac(FRAG.uv_sdf.xy);
	float2 sdf_deriv = lerp(gather4.ww - gather4.zy, gather4.yz - gather4.xx, sdf_blend.yx);
	float2 gather2 = lerp(gather4.yw, gather4.xz, sdf_blend.x);
	float sdf = lerp(gather2.y, gather2.x, sdf_blend.y);
	float sdf_mask = step(0, sdf);
	
	float height = saturate(1 - sdf/5);
	height *= height;
	// return float4(height.rrr, 1);
	float2 dheight = (2*height)*sdf_deriv;
	// return float4(dheight, 0, 1);
	
	float pcoef = (1 - height)/sqrt(1 + dot(dheight, dheight));
	float2 uv2 = FRAG.uv + pcoef*FRAG.parallax;
	// return float4(frac(16*uv2.xy)*sdf_mask, 0, 1);
	
	float4 biome = DriftAtlas.Sample(DriftLinear, FRAG.uv_biome);
	float4 biome_rb_ga = lerp(float4(0, 4, biome.rb), float4(2, 6, biome.ga), step(biome.rb, biome.ga).xyxy);
	float biome_idx = lerp(biome_rb_ga.x, biome_rb_ga.y, step(biome_rb_ga.z, biome_rb_ga.w));
	float layer = FRAG.uv.z + biome_idx;
	
	float4 albedo = DriftAtlas.Sample(_repeat, float3(uv2, layer));
	albedo.rgb *= lerp(0.5, 1.0, sdf_mask);
	albedo.rgb *= smoothstep(-1, 0, sdf); // Temporary
	
	// Normals
	// float3 normal = DriftAtlas.Sample(_repeat, float3(uv2, layer + 1));
	// float2 deriv = 4*(normal.xy - 0.5)/float2(-normal.z, normal.z);
	float2 deriv = dheight + 2*(2*DriftAtlas.Sample(_repeat, float3(uv2, layer + 1)) - 1);
	deriv = float2(dot(deriv, ddx(FRAG.uv.xy)), dot(deriv, ddy(FRAG.uv.xy)))*FRAG.uv_scale;
	float3 n = float3(-deriv, 1)/sqrt(1 + dot(deriv, deriv));
	
	float3 light = SampleLightField(n, FRAG.uv_light, 1);
	return float4(albedo.rgb*light, sdf);
}
