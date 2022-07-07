#include "drift_common.hlsl"

struct VertInput {
	DRIFT_ATTR0 float2 uv;
};

struct FragInput {
	float4 position : SV_POSITION;
	float2 world_pos;
	float2 uv;
};

void VShader(in VertInput IN, out FragInput FRAG){
	FRAG.position = float4(2*IN.uv - 1, 0, 1);
	FRAG.world_pos = mul(float2x3(DRIFT_MATRIX_VP_INV), float3(2*IN.uv - 1, 1));
	FRAG.uv = IN.uv;
}

#define ROW_HEIGHT 3.464101615138 // 2*sqrt(3)
#define UV_COEF float2(2, ROW_HEIGHT)

float4 Spheres(float2 uv, float2 ssuv){
	float3 n = float3(uv, 1 - dot(uv, uv));
	float l = length(uv);
	float mask = 1 - smoothstep(1 - fwidth(l), 1, l);
	return mask*float4(n, 1);
}

float4 FShader(in FragInput FRAG) : SV_TARGET0 {
	// float2 uv1 = FRAG.position.xy/4/UV_COEF;
	float2 uv1 = FRAG.world_pos.xy/8/UV_COEF;
	
	// Odd and even rows.
	float4 n1 = Spheres(frac(uv1 + 0.0)*UV_COEF - (0.5*UV_COEF), FRAG.uv);
	float4 n2 = Spheres(frac(uv1 + 0.5)*UV_COEF - (0.5*UV_COEF), FRAG.uv);
	float4 n = n1 + n2;
	n.z += 1 - n.w;
	n.xyz = float3(0, 0, 1);
	
	float3 color = SampleLightField(normalize(n.xyz), FRAG.uv, 1);
	return float4(color.rgb, 0);
}
