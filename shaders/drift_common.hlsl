#define DRIFT_ATTR0 [[vk::location(0)]]
#define DRIFT_ATTR1 [[vk::location(1)]]
#define DRIFT_ATTR2 [[vk::location(2)]]
#define DRIFT_ATTR3 [[vk::location(3)]]
#define DRIFT_ATTR4 [[vk::location(4)]]
#define DRIFT_ATTR5 [[vk::location(5)]]
#define DRIFT_ATTR6 [[vk::location(6)]]
#define DRIFT_ATTR7 [[vk::location(7)]]

const float DRIFT_FORESHORTENING = 0.94;

cbuffer DriftGlobals : register(b0) {
	row_major float2x4 DRIFT_MATRIX_V;
	row_major float2x4 DRIFT_MATRIX_P;
	row_major float2x4 DRIFT_MATRIX_TERRAIN;
	row_major float2x4 DRIFT_MATRIX_VP;
	row_major float2x4 DRIFT_MATRIX_VP_INV;
	row_major float2x4 DRIFT_MATRIX_REPROJ;
	float2 DRIFT_PIXEL_EXTENTS;
	float2 DRIFT_SCREEN_EXTENTS;
	float2 DRIFT_BUFFER_EXTENTS;
	float DRIFT_ATLAS_SIZE, DRIFT_ATLAS_BIOME;
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
float3 LinearToSRGB(float3 color){
	return max(1.055*pow(color, 0.416666667) - 0.055, 0);
}

// TODO no longer used with packed components right?
int2 U8ToSigned(uint2 n){return (int2(n) ^ 0x80) - 0x80;}

struct DriftSpriteVertInput {
	// Vertex attribs:
	DRIFT_ATTR0 float2 uv;
	// Instance attribs:
	DRIFT_ATTR1 float4 mat0;
	DRIFT_ATTR2 float4 mat1_z;
	DRIFT_ATTR3 float4 color;
	DRIFT_ATTR4 uint4 frame_bounds;
	DRIFT_ATTR5 uint2 anchor;
	DRIFT_ATTR6 uint layer;
};

struct DriftSpriteFragInput {
	float4 position : SV_POSITION;
	float2 ssuv, ssuv_prev;
	float3 uv;
	nointerpolation float4 uv_bounds;
	nointerpolation float4 color;
	// TODO combine with props?
	nointerpolation float2 uv_scale;
};

void DriftSpriteVShader(in DriftSpriteVertInput IN, out DriftSpriteFragInput FRAG){
	// Transform:
	float2x3 transform = float2x3(IN.mat0.xz, IN.mat1_z.x, IN.mat0.yw, IN.mat1_z.y);
	float2 size = IN.frame_bounds.zw - IN.frame_bounds.xy + 1;
	float2 world_pos = mul(transform, float3(size*IN.uv - U8ToSigned(IN.anchor.xy), 1));
	float2 clip_pos = mul(float2x3(DRIFT_MATRIX_VP), float3(world_pos, 1));
	
	// Output:
	FRAG.position = float4(clip_pos, 0, 1/(1 - IN.mat1_z.z*(1 - DRIFT_FORESHORTENING)));
	FRAG.ssuv = 0.5*clip_pos + 0.5;
	FRAG.ssuv_prev = 0.5 + 0.5*mul(float2x3(DRIFT_MATRIX_REPROJ), float3(2*FRAG.ssuv - 1, 1)).xy;
	FRAG.uv = float3(lerp(IN.frame_bounds.xy, IN.frame_bounds.zw + 1, IN.uv)/DRIFT_ATLAS_SIZE, IN.layer);
	FRAG.uv_bounds = IN.frame_bounds/DRIFT_ATLAS_SIZE;
	FRAG.color = IN.color;
	float2x2 scale = mul(float2x2(DRIFT_MATRIX_V), float2x2(transform));
	FRAG.uv_scale = DRIFT_ATLAS_SIZE*float2(length(scale[0]), length(scale[1]));
}

float2 DoubleAngle(float2 n){
	return float2(n.x*n.x - n.y*n.y, 2.0*n.x*n.y);
}

// TODO z-ish?
float3 SampleLightField(float3 normal, float2 uv, float shadows){
	float3 a0 = lerp(DriftLightfield.Sample(DriftLinear, float3(uv, 0)), DriftShadowfield.Sample(DriftLinear, float3(uv, 0)), shadows);
	float3 a1 = lerp(DriftLightfield.Sample(DriftLinear, float3(uv, 1)), DriftShadowfield.Sample(DriftLinear, float3(uv, 1)), shadows);
	float3 b1 = lerp(DriftLightfield.Sample(DriftLinear, float3(uv, 2)), DriftShadowfield.Sample(DriftLinear, float3(uv, 2)), shadows);
	float3 a2 = lerp(DriftLightfield.Sample(DriftLinear, float3(uv, 3)), DriftShadowfield.Sample(DriftLinear, float3(uv, 3)), shadows);
	float3 b2 = lerp(DriftLightfield.Sample(DriftLinear, float3(uv, 4)), DriftShadowfield.Sample(DriftLinear, float3(uv, 4)), shadows);
	
	float2 g1 = normal.xy;
	float2 g2 = DoubleAngle(g1);
	return max(0, a0 + a1*g1.xxx + b1*g1.yyy + a2*g2.xxx + b2*g2.yyy);
}
