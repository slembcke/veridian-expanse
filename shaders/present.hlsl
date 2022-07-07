#include "drift_common.hlsl"

struct VertInput {
	DRIFT_ATTR0 float2 uv;
};

struct FragInput {
	float4 position : SV_POSITION;
	float2 uv0;
};

void VShader(in VertInput IN, out FragInput FRAG){
	FRAG.position = float4(2*IN.uv.xy - 1, 0, 1);
	
	// float2 ratio = DRIFT_SCREEN_EXTENTS/DRIFT_BUFFER_EXTENTS;
	FRAG.uv0 = IN.uv;//*ratio;
}

Texture2D _texture : DRIFT_TEX1;

float4 FShader(in FragInput FRAG) : SV_TARGET0 {
	float3 color = _texture.Sample(DriftNearest, FRAG.uv0);
	float3 bl = saturate(_texture.Sample(DriftLinear, FRAG.uv0));
	
	// Apply unsharp mask
	color += color - bl;
	
	// Temporary haze
	float3 light = DriftLightfield.Sample(DriftLinear, float3(FRAG.uv0, 0));
	float3 shadow =  DriftShadowfield.Sample(DriftLinear, float3(FRAG.uv0, 0));
	color = float3(1)*color + float3(0.6, 0.3, 0.0)*lerp(light, shadow, 0.3); // Light biome
	// color = float3(1)*color + float3(0.6, 0.6, 0.0)*lerp(light, shadow, 0.3); // Radio biome
	// color = float3(1)*color + float3(0.6, 0.7, 0.8)*lerp(light, shadow, 0.3); // Cryo biome
	
	// TODO bother with triangle noise or something?
	float dither = frac(dot(FRAG.position.xy, float2(0.7548776662, 0.56984029)))/128;
	return float4(color.rgb + dither, 1);
}
