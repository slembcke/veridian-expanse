#include "drift_common.hlsl"

struct VertInput {
	DRIFT_ATTR0 float2 position;
	DRIFT_ATTR1 float2 uv;
	DRIFT_ATTR2 float4 color;
};

struct FragInput {
	float4 position : SV_POSITION;
	float3 uv;
	float4 color;
};

void VShader(in VertInput IN, out FragInput FRAG){
	FRAG.position = float4(2*IN.position/DRIFT_PIXEL_EXTENTS - 1, 0, 1);
	FRAG.position.y *= -1;
	
	FRAG.uv = float3(IN.uv, 0);
	FRAG.color = IN.color;
}

Texture2DArray Texture : DRIFT_TEX1;

float4 FShader(in FragInput FRAG) : SV_TARGET0 {
	return FRAG.color*Texture.Sample(DriftNearest, FRAG.uv);
}
