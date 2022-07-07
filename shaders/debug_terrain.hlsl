#include "drift_common.hlsl"

struct VertInput {
	// Vertex attribs:
	DRIFT_ATTR0 float2 uv;
	// Instance attribs:
	DRIFT_ATTR1 float4 data;
};

struct FragInput {
	float4 position : SV_POSITION;
	float3 uv_sdf;
	float3 uv_biome;
	nointerpolation float2 uv_scale;
};

void VShader(in VertInput IN, out FragInput FRAG){
	// Unpack:
	uint layer = 0x4;
	
	// Transform:
	float2 map_pos = (IN.data.xy + IN.uv)*pow(2, IN.data.z);
	float2 world_pos = mul(float2x3(DRIFT_MATRIX_TERRAIN), float3(map_pos, 1));
	float2 clip_pos = mul(float2x3(DRIFT_MATRIX_VP), float3(world_pos, 1));
	
	// Output:
	FRAG.position = float4(clip_pos, 0, 1);
	FRAG.uv_sdf = float3(32*IN.uv, IN.data.w);
	FRAG.uv_biome = float3(map_pos/DRIFT_ATLAS_SIZE, DRIFT_ATLAS_BIOME);
	float2x2 scale = DRIFT_MATRIX_V;
	FRAG.uv_scale = DRIFT_ATLAS_SIZE*float2(length(scale[0]), length(scale[1]));
}

SamplerState _repeat : DRIFT_SAMP2;
Texture2DArray _tiles : DRIFT_TEX1;

float4 FShader(in FragInput FRAG) : SV_TARGET0{
	// Calculate density values.
	float4 gather4 = _tiles.Load(float4(FRAG.uv_sdf, 0));
	float2 density_blend = frac(FRAG.uv_sdf.xy);
	float2 density_deriv = lerp(gather4.zy - gather4.ww, gather4.xx - gather4.yz, density_blend.yx);
	float2 gather2 = lerp(gather4.yw, gather4.xz, density_blend.x);
	float density = lerp(gather2.y, gather2.x, density_blend.y);
	
	float4 color = density.rrrr;
	// color = gather4.rrrr;
	color.rg += 0.01*lerp(float2(-3,0), float2(9,0), step(color.a, 0.5));
	color.rgb *= smoothstep(0, fwidth(FRAG.uv_sdf.x)/8, abs(density - 0.5));

	
	// float4 biome = DriftAtlas.Sample(DriftLinear, FRAG.uv_biome);
	// float4 biome_rb_ga = lerp(float4(0, 4, biome.rb), float4(2, 6, biome.ga), step(biome.rb, biome.ga).xyxy);
	// float biome_idx = lerp(biome_rb_ga.x, biome_rb_ga.y, step(biome_rb_ga.z, biome_rb_ga.w));
	// color = 0;
	// color[biome_idx] = 1;
	// color = floor(biome*16)/16;
	
	float2 grid = step(fwidth(FRAG.uv_sdf), FRAG.uv_sdf);
	color.rgb *= (grid.x*grid.y);
	return color;
}
