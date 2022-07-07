#include "drift_common.hlsl"
#include "lighting.hlsl"

void VShader(in VertInput IN, out FragInput FRAG){
	VShaderLight(IN, FRAG);
}

void FShader(in FragInput FRAG, out FragOutput0 OUT){
	LightFieldSample field = GenerateSample(FRAG.dir, FRAG.color.a);
	
	float3 intensity = FRAG.color*DriftAtlas.Sample(DriftLinear, FRAG.uv);
	OUT.a0 = float4(intensity*field.a0, 1);
}
