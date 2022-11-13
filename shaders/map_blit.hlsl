#include "drift_common.hlsl"

struct VertInput {
	DRIFT_ATTR0 float2 uv;
	DRIFT_ATTR1 float4 reproj0;
	DRIFT_ATTR2 float4 reproj1;
};

struct FragInput {
	float4 position : SV_POSITION;
	float2 uv;
};

void VShader(in VertInput IN, out FragInput FRAG){
	float2x3 reproj = {
		IN.reproj0.x, IN.reproj0.y, IN.reproj0.z,
		IN.reproj1.x, IN.reproj1.y, IN.reproj1.z,
	};
	
	FRAG.position = float4(mul(reproj, float3(2*IN.uv - 1, 1)), 0, 1);
	FRAG.uv = IN.uv;
}

Texture2D _texture : DRIFT_TEX1;

float4 FShader(in FragInput FRAG) : SV_TARGET0 {
	return float4(_texture.Sample(DriftLinear, FRAG.uv).rgb, 1);
}
