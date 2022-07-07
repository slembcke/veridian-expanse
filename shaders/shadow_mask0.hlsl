#include "drift_common.hlsl"
#include "lighting.hlsl"

void VShader(MaskVertexInput IN, out MaskFragmentInput FRAG){
	VShaderMask(IN, FRAG);
}

void FShader(in MaskFragmentInput FRAG, out FragOutput0 OUT){
	float mask = FShaderMask(FRAG);
	OUT.a0 = float4(0, 0, 0, -mask);
}
