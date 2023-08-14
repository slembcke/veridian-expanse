/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include "drift_common.hlsl"

struct VertInput {
	DRIFT_ATTR0 float2 uv;
};

struct FragInput {
	float4 position : SV_POSITION;
	float2 uv;
	float3 uv_static;
	float3 uv_jitter;
	// float2 uv_hex;
	float4 scatter;
	float4 transmit;
};

cbuffer Locals : DRIFT_UBO1 {
	float4 _scatter[5];
	float4 _transmit[5];
	float4 _effect_tint;
	float _effect_static;
	float _effect_heat;
}

#define ROW_HEIGHT 3.464101615138 // 2*sqrt(3)
#define UV_COEF float2(2, ROW_HEIGHT)

void VShader(in VertInput IN, out FragInput FRAG){
	float2 clip_coord = 2*IN.uv - 1;
	FRAG.position = float4(clip_coord, 0, 1);
	FRAG.uv = IN.uv;
	
	float2 pixel = (IN.uv - 0.5)*(DRIFT_INTERNAL_EXTENTS/DRIFT_ATLAS_SIZE);
	FRAG.uv_static = float3((pixel + DRIFT_JITTER)/float2(6, 1), 15);
	FRAG.uv_jitter = float3(pixel + DRIFT_JITTER, 15);
	// FRAG.uv_hex = (pixel + 4*(2*DRIFT_JITTER - 1))/(32*UV_COEF);
	
	float2 uv_biome = mul(float2x3(DRIFT_MATRIX_VP_INV), float3(clip_coord, 1));
	uv_biome = uv_biome/(256*32*8) + 0.5; // TODO magic numbers for map size
	
	float4 biome = DriftAtlas.Sample(DriftLinear, float3(uv_biome, DRIFT_ATLAS_BIOME));
	float space = 1 - (biome[0] + biome[1] + biome[2] + biome[3]);
	FRAG.scatter = biome[0]*_scatter[0] + biome[1]*_scatter[1] + biome[2]*_scatter[2] + biome[3]*_scatter[3] + space*_scatter[4];
	FRAG.transmit = biome[0]*_transmit[0] + biome[1]*_transmit[1] + biome[2]*_transmit[2] + biome[3]*_transmit[3] + space*_transmit[4];
	
	FRAG.scatter.rgb = SRGBToLinear(FRAG.scatter.rgb);
	FRAG.transmit.rgb = SRGBToLinear(FRAG.transmit.rgb);
}

Texture2D _texture : DRIFT_TEX1;
SamplerState _repeat : DRIFT_SAMP2;

float3 HBD(float3 rgb){
	float3 x = max(0, rgb - 0.004);
	return (x*(6.2*x + 0.5))/(x*(6.2*x + 1.7) + 0.06);
}

float4 FShader(in FragInput FRAG) : SV_TARGET0 {
	float4 blue_noise = DriftAtlas.Sample(_repeat, FRAG.uv_jitter);
	blue_noise.xy -= blue_noise.zw;
	float3 noise_static = DriftAtlas.Sample(_repeat, FRAG.uv_static) - 0.5;
	float2 jitter = _effect_heat*blue_noise/DRIFT_INTERNAL_EXTENTS;
	jitter.x += noise_static*(1.2*_effect_static)/DRIFT_INTERNAL_EXTENTS.x;
	
	// float2 uv_hex = FRAG.uv_hex;
	// // Odd and even rows.
	// float2 off0 = frac(uv_hex + 0.0)*UV_COEF - (0.5*UV_COEF);
	// float2 off1 = frac(uv_hex + 0.5)*UV_COEF - (0.5*UV_COEF);
	// float2 off = dot(off0, off0) < dot(off1, off1) ? off0 : off1;
	// off *= pow(length(off), 6)/DRIFT_INTERNAL_EXTENTS;
	
	// Jitter mask
	float2 delta = FRAG.uv - 0.5;
	delta.y *= DRIFT_INTERNAL_EXTENTS.y/DRIFT_INTERNAL_EXTENTS.x;
	jitter *= lerp(1, 4, smoothstep(0, 0.5, length(delta)));
	
	// Get color and apply haze.
	float3 color = 1*_texture.Sample(DriftNearest, FRAG.uv + jitter);
	float3 light = DriftLightfield.Sample(DriftLinear, float3(FRAG.uv, 0));
	float3 shadow =  DriftShadowfield.Sample(DriftLinear, float3(FRAG.uv, 0));
	color = color*FRAG.transmit + FRAG.scatter.rgb*lerp(light, shadow, FRAG.scatter.a);
	color = LinearToSRGB(color);
	// color = HBD(color);
	// color = LinearToSRGB(color/(color + 1));
	
	color += (0.2*_effect_static)*noise_static;
	// TODO bother with triangle noise or something?
	// float dither = frac(dot(FRAG.position.xy, float2(0.7548776662, 0.56984029))) - 0.5;
	return float4(color*_effect_tint + blue_noise.rgb/256, 1);
}
