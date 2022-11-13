#include "drift_common.hlsl"

void VShader(in DriftSpriteVertInput IN, out DriftSpriteFragInput FRAG){
	DriftSpriteVShader(IN, FRAG, true);
}

float4 FShader(in DriftSpriteFragInput FRAG) : SV_TARGET0 {
	// Visualise pixel offset error.
	// return float4(2*abs(0.5 - frac(FRAG.uv.xy*DRIFT_ATLAS_SIZE)), 0, 1);
	
	float3 uv = FRAG.uv;
	uv.xy = clamp(uv, FRAG.uv_bounds.xy, FRAG.uv_bounds.zw);
	return FRAG.color*DriftAtlas.Sample(DriftNearest, uv);
}
