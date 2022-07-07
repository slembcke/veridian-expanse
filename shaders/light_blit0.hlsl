#include "drift_common.hlsl"
#include "lighting.hlsl"

struct foo {
	DRIFT_ATTR0 float2 uv;
};

struct bar {
	float4 position : SV_POSITION;
	float2 uv;
};

void VShader(in foo IN, out bar FRAG){
	FRAG.position = float4(2*IN.uv - 1, 0, 1);
	FRAG.uv = IN.uv;
}

void FShader(in bar FRAG, out FragOutput0 OUT){
	OUT.a0 = float4(DriftLightfield.Sample(DriftLinear, float3(FRAG.uv, 0)).rgb, 1);
}
