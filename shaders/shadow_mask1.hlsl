#include "drift_common.hlsl"
#include "lighting.hlsl"

void VShader(MaskVertexInput IN, out MaskFragmentInput FRAG){
	VShaderMask(IN, FRAG);
}

void FShader(in MaskFragmentInput FRAG, out FragOutput1 OUT){
	float mask = FShaderMask(FRAG);
	OUT.a1 = float4(0, 0, 0, -mask);
	OUT.b1 = float4(0, 0, 0, -mask);
	OUT.a2 = float4(0, 0, 0, -mask);
	OUT.b2 = float4(0, 0, 0, -mask);
}
