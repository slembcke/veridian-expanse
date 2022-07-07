#include "drift_common.hlsl"

void VShader(in DriftSpriteVertInput IN, out DriftSpriteFragInput FRAG){
	DriftSpriteVShader(IN, FRAG);
}

float4 FShader(in DriftSpriteFragInput FRAG) : SV_TARGET0 {
	float3 uv = FRAG.uv;
	uv.xy = clamp(uv, FRAG.uv_bounds.xy, FRAG.uv_bounds.zw);
	return FRAG.color*DriftAtlas.Sample(DriftNearest, uv);
}
