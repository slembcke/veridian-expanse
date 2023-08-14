/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include "drift_common.hlsl"

struct VertInput {
	DRIFT_ATTR0 float2 uv;
	DRIFT_ATTR1 float wave;
	// Instanced:
	DRIFT_ATTR2 float4 seg;
};

struct FragInput {
	float4 position : SV_POSITION;
	float dist;
	float wave;
};

void VShader(in VertInput IN, out FragInput FRAG){
	float2 world_pos = lerp(IN.seg.xy, IN.seg.zw, IN.uv.x/64);
	float2 n = normalize(IN.seg.xy - IN.seg.zw).yx*float2(1, -1);
	world_pos += (10*IN.uv.y - 1*IN.wave)*n;
	
	float2 clip_pos = mul(float2x3(DRIFT_MATRIX_VP), float3(world_pos, 1));
	FRAG.position = float4(clip_pos, 0, 1);
	FRAG.dist = 1 - abs(IN.uv.y);
	FRAG.wave = IN.wave;
}

float4 FShader(in FragInput FRAG) : SV_TARGET0 {
	float d = FRAG.dist, glow = 0.25*d*d*d;
	float mask = smoothstep(0, fwidth(d), d - 0.9);
	float4 plasma_color = 0.5*float4(0.00f, 0.27f, 1.26f, 0.00f);
	return plasma_color*(abs(FRAG.wave)*(mask + glow));
}
