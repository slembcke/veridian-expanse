#include "drift_common.hlsl"
#include "lighting.hlsl"

void VShader(in VertInput IN, out FragInput FRAG){
	VShaderShadow(IN, FRAG);
}

void FShader(in FragInput FRAG, out FragOutput0 OUT){
	LightFieldSample field = GenerateSample(FRAG.dir, FRAG.color.a);
	
	float3 intensity = FRAG.color*DriftAtlas.Sample(DriftLinear, FRAG.uv);
	float2 mask = step(FRAG.uv_bounds.xy, FRAG.uv.xy)*step(FRAG.uv.xy, FRAG.uv_bounds.zw);
	intensity *= mask.x*mask.y;
	
	OUT.a0 = float4(-intensity*field.a0, 1);
}
