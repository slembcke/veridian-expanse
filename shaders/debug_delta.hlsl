#include "drift_common.hlsl"

struct VertInput {
	DRIFT_ATTR0 float2 uv;
};

struct FragInput {
	float4 position : SV_POSITION;
	float2 uv0, uv1;
};

void VShader(in VertInput IN, out FragInput FRAG){
	FRAG.position = float4(2*IN.uv.xy - 1, 0, 1);
	FRAG.uv0 = IN.uv;
	FRAG.uv1 = 0.5 + 0.5*mul(float2x3(DRIFT_MATRIX_REPROJ), float3(2*IN.uv - 1, 1)).xy;
}

Texture2D _texture : DRIFT_TEX1;

float4 FShader(in FragInput FRAG) : SV_TARGET0 {
	float3 curr = _texture.Sample(DriftLinear, FRAG.uv0);
	float3 prev = DriftPrevFrame.Sample(DriftLinear, FRAG.uv1);
	return float4(1, 0, 1, 1)*length(curr - prev);
}
