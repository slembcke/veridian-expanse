/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include "drift_common.hlsl"

void VShader(in DriftSpriteVertInput IN, out DriftSpriteFragInput FRAG){
	DriftSpriteVShader(IN, FRAG, false);
}

float4 FShader(in DriftSpriteFragInput FRAG) : SV_TARGET0 {
	float3 uv = FRAG.uv;
	uv.xy = clamp(uv, FRAG.uv_bounds.xy, FRAG.uv_bounds.zw);
	float4 albedo = FRAG.color*DriftAtlas.Sample(DriftNearest, uv);
	albedo.rgb = SRGBToLinear(albedo.rgb);
	
	float terrain_density = DriftPrevFrame.Sample(DriftNearest, FRAG.ssuv_prev).a;
	albedo *= step(-0.25, terrain_density);
	
	// Normals
	float4 props = DriftAtlas.Sample(DriftNearest, float3(uv.xy, uv.z + 1));
	float2 deriv = 2*(2*props.rg - 1);
	deriv = float2(dot(deriv, ddx(FRAG.uv.xy)), dot(deriv, ddy(FRAG.uv.xy)))*FRAG.uv_scale_glow_shiny.xy;
	// return float4(0.5 + 0.5*deriv, 0, 1)*albedo.a;
	float3 n = float3(-deriv, 1)/sqrt(1 + dot(deriv, deriv));
	// return float4(0.5 + 0.5*n, 1)*albedo.a;
	
	// TODO aspect
	float2 reflect_offset = -0.15*deriv/dot(deriv, deriv);
	float3 reflect = DriftPrevFrame.Sample(DriftLinear, FRAG.ssuv_prev + reflect_offset).rgb;
	reflect *= (FRAG.uv_scale_glow_shiny[3]*albedo.a)*smoothstep(0, 3, dot(deriv, deriv));
	
	float glow = FRAG.uv_scale_glow_shiny[2]*props.b;
	float3 light = SampleLightField(normalize(n.xyz), FRAG.ssuv, 1) + reflect + glow;
	return float4(albedo.rgb*light, albedo.a);
}
