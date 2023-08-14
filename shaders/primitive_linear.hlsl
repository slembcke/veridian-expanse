/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include "drift_common.hlsl"

struct VertInput {
	DRIFT_ATTR0 float2 uv;
	DRIFT_ATTR1 float4 endpoints;
	DRIFT_ATTR2 float2 radii;
	DRIFT_ATTR3 float4 color;
};

struct FragInput {
	float4 position : SV_POSITION;
	float2 uv;
	float4 color;
	float2 props;
};

void VShader(in VertInput IN, out FragInput FRAG){
	float2 uv = 2*IN.uv - 1;
	float2 offset = uv;
	
	float2 delta = IN.endpoints.zw - IN.endpoints.xy;
	float len = length(delta);
	if(len > 0){
		float2 dir = delta/len;
		offset = uv.x*dir + uv.y*float2(-dir.y, dir.x);	
	}
	
	float2 world_pos = lerp(IN.endpoints.xy, IN.endpoints.zw, IN.uv.x) + IN.radii[0]*offset;
	float2 clip_pos = mul(float2x3(DRIFT_MATRIX_VP), float3(world_pos, 1));
	
	FRAG.position = float4(clip_pos, 0, 1);
	FRAG.uv = uv;
	FRAG.color = float4(SRGBToLinear(IN.color.rgb), IN.color.a);
	FRAG.props.x = 0.5*len/IN.radii[0];
	FRAG.props.y = 1 - IN.radii[1]/IN.radii[0];
}

float4 FShader(in FragInput FRAG) : SV_TARGET0 {
	FRAG.uv.x *= FRAG.props.x + 1;
	if(FRAG.uv.x < -FRAG.props.x){
		FRAG.uv.x += FRAG.props.x;
	} else 	if(FRAG.uv.x > FRAG.props.x){
		FRAG.uv.x -= FRAG.props.x;
	} else {
		FRAG.uv.x = 0;
	}

	float l = 1 - length(FRAG.uv), fw = length(fwidth(FRAG.uv));
	float mask = smoothstep(0, fw, l)*smoothstep(-fw, 0, FRAG.props.y - l);
	return FRAG.color*mask;
}
