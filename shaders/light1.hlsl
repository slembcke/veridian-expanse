#include "drift_common.hlsl"
#include "lighting.hlsl"

void VShader(in VertInput IN, out FragInput FRAG){
	VShaderLight(IN, FRAG);
}

void FShader(in FragInput FRAG, out FragOutput1 OUT){
	LightFieldSample field = GenerateSample(FRAG.dir, FRAG.color.a);
	
	float3 intensity = FRAG.color*DriftAtlas.Sample(DriftLinear, FRAG.uv);
	OUT.a1 = float4(intensity*field.a1, 1);
	OUT.b1 = float4(intensity*field.b1, 1);
	OUT.a2 = float4(intensity*field.a2, 1);
	OUT.b2 = float4(intensity*field.b2, 1);
}
