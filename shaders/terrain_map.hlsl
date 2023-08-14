/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

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

const float4 BIOME_TINT[] = {
	{1.0, 1.0, 0.5},
	{0.5, 1.0, 0.5},
	{0.5, 1.0, 1.0},
	{1.0, 0.5, 0.5},
};

#define SDF_MAX_DIST (32/4)

float4 FShader(in FragInput FRAG) : SV_TARGET0{
	float4 gather4 = _tiles.Load(float4(FRAG.uv_sdf, 0));
	float2 gather2 = lerp(gather4.yw, gather4.xz, frac(FRAG.uv_sdf.x));
	float sdf = lerp(gather2.y, gather2.x, frac(FRAG.uv_sdf.y));
	
	float4 biome = DriftAtlas.Sample(DriftLinear, FRAG.uv_biome);
	float3 color = biome[0]*BIOME_TINT[0] + biome[1]*BIOME_TINT[1] + biome[2]*BIOME_TINT[2] + biome[3]*BIOME_TINT[3];
	// Fog of War
	color *= smoothstep(0, 1, DriftAtlas.Sample(DriftLinear, float3(FRAG.uv_biome.xy, DRIFT_ATLAS_VISIBILITY)).r);
	// Solid terrain.
	color *= smoothstep(0, fwidth(FRAG.uv_sdf.x), DRIFT_SDF_MAX_DIST*(sdf - 0.5));
	// Shading
	color *= smoothstep(-1, 1, 2*sdf - 1);
	
	return float4(color, sdf);
}
