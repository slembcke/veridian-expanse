/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#define DRIFT_ATTR0 [[vk::location(0)]]
#define DRIFT_ATTR1 [[vk::location(1)]]
#define DRIFT_ATTR2 [[vk::location(2)]]
#define DRIFT_ATTR3 [[vk::location(3)]]
#define DRIFT_ATTR4 [[vk::location(4)]]
#define DRIFT_ATTR5 [[vk::location(5)]]
#define DRIFT_ATTR6 [[vk::location(6)]]
#define DRIFT_ATTR7 [[vk::location(7)]]

const float DRIFT_FORESHORTENING = 0.94;
const float DRIFT_SDF_MAX_DIST = 8;

cbuffer DriftGlobals : register(b0) {
	row_major float2x4 DRIFT_MATRIX_V;
	row_major float2x4 DRIFT_MATRIX_P;
	row_major float2x4 DRIFT_MATRIX_TERRAIN;
	row_major float2x4 DRIFT_MATRIX_VP;
	row_major float2x4 DRIFT_MATRIX_VP_INV;
	row_major float2x4 DRIFT_MATRIX_REPROJ;
	float2 DRIFT_JITTER;
	float2 DRIFT_RAW_EXTENTS;
	float2 DRIFT_VIRTUAL_EXTENTS;
	float2 DRIFT_INTERNAL_EXTENTS;
	float DRIFT_ATLAS_SIZE, DRIFT_SHARPENING, DRIFT_GRADMUL;
	float DRIFT_ATLAS_BIOME, DRIFT_ATLAS_VISIBILITY;
};

#define DRIFT_UBO1 register(b1)
#define DRIFT_UBO2 register(b2)
#define DRIFT_UBO3 register(b3)

SamplerState DriftNearest : register(s4);
SamplerState DriftLinear : register(s5);
#define DRIFT_SAMP2 register(s6)
#define DRIFT_SAMP3 register(s7)

Texture2DArray DriftAtlas : register(t8);
#define DRIFT_TEX1 register(t9)
#define DRIFT_TEX2 register(t10)
#define DRIFT_TEX3 register(t11)
#define DRIFT_TEX4 register(t12)
Texture2D DriftPrevFrame : register(t13);
Texture2DArray DriftLightfield : register(t14);
Texture2DArray DriftShadowfield : register(t15);

// https://chilliant.blogspot.com/2012/08/srgb-approximations-for-hlsl.html
float3 SRGBToLinear(float3 srgb){
	return srgb*(srgb*(srgb*0.305306011 + 0.682171111) + 0.012522878);
}

float3 LinearToSRGB(float3 rgb){
	return max(1.055*pow(rgb, 0.416666667) - 0.055, 0);
}

int2 U8ToSigned(uint2 n){return (int2(n) ^ 0x80) - 0x80;}

struct DriftSpriteVertInput {
	// Vertex attribs:
	DRIFT_ATTR0 float2 uv;
	// Instance attribs:
	DRIFT_ATTR1 float4 mat0;
	DRIFT_ATTR2 float2 mat1;
	DRIFT_ATTR3 float4 color;
	DRIFT_ATTR4 uint4 frame_bounds;
	DRIFT_ATTR5 uint3 frame_props;
	DRIFT_ATTR6 uint3 sprite_props;
};

struct DriftSpriteFragInput {
	float4 position : SV_POSITION;
	float2 ssuv, ssuv_prev;
	float3 uv;
	nointerpolation float4 uv_bounds;
	nointerpolation float4 color;
	// TODO combine with props?
	nointerpolation float4 uv_scale_shiny;
};

void DriftSpriteVShader(in DriftSpriteVertInput IN, out DriftSpriteFragInput FRAG, bool apply_rounding){
	// Transform:
	if(apply_rounding) IN.mat1.xy = round(IN.mat1.xy);
	float2x3 transform = float2x3(IN.mat0.xz, IN.mat1.x, IN.mat0.yw, IN.mat1.y);
	float2 size = IN.frame_bounds.zw - IN.frame_bounds.xy + 1;
	float2 world_pos = mul(transform, float3(size*IN.uv - U8ToSigned(IN.frame_props.xy), 1));
	float2 clip_pos = mul(float2x3(DRIFT_MATRIX_VP), float3(world_pos, 1));
	// Apply foreshortening.
	clip_pos *= 1 - IN.sprite_props[0]*(1 - DRIFT_FORESHORTENING)/255;
	
	// Output:
	FRAG.position = float4(clip_pos, 0, 1);
	FRAG.ssuv = 0.5*clip_pos + 0.5;
	FRAG.ssuv_prev = 0.5 + 0.5*mul(float2x3(DRIFT_MATRIX_REPROJ), FRAG.position.xyw).xy;
	FRAG.uv = float3(lerp(IN.frame_bounds.xy, IN.frame_bounds.zw + 1, IN.uv)/DRIFT_ATLAS_SIZE, IN.frame_props.z);
	FRAG.uv_bounds = IN.frame_bounds/DRIFT_ATLAS_SIZE;
	FRAG.color = IN.color;
	float2x2 scale = DRIFT_GRADMUL*mul(float2x2(DRIFT_MATRIX_V), float2x2(transform));
	FRAG.uv_scale_shiny.xy = DRIFT_ATLAS_SIZE*float2(length(scale[0]), length(scale[1]));
	FRAG.uv_scale_shiny.z = IN.sprite_props.y/255.0;
}

float2 DoubleAngle(float2 n){
	return float2(n.x*n.x - n.y*n.y, 2.0*n.x*n.y);
}

float3 SampleLightField(float3 normal, float2 uv, float shadows){
	float3 a0 = lerp(DriftLightfield.Sample(DriftLinear, float3(uv, 0)), DriftShadowfield.Sample(DriftLinear, float3(uv, 0)), shadows);
	float3 a1 = lerp(DriftLightfield.Sample(DriftLinear, float3(uv, 1)), DriftShadowfield.Sample(DriftLinear, float3(uv, 1)), shadows);
	float3 b1 = lerp(DriftLightfield.Sample(DriftLinear, float3(uv, 2)), DriftShadowfield.Sample(DriftLinear, float3(uv, 2)), shadows);
	float3 a2 = lerp(DriftLightfield.Sample(DriftLinear, float3(uv, 3)), DriftShadowfield.Sample(DriftLinear, float3(uv, 3)), shadows);
	float3 b2 = lerp(DriftLightfield.Sample(DriftLinear, float3(uv, 4)), DriftShadowfield.Sample(DriftLinear, float3(uv, 4)), shadows);
	
	// TODO
	// normal.xy = normalize(normal.xy);
	// float z = sqrt(1 - normal.z*normal.z);
	
	float2 g1 = normal.xy;
	float2 g2 = DoubleAngle(g1);
	return max(0, a0 + a1*g1.xxx + b1*g1.yyy + a2*g2.xxx + b2*g2.yyy);
}
