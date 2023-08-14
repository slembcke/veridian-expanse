/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include "drift_common.hlsl"

struct VertInput {
	DRIFT_ATTR0 float2 uv;
	DRIFT_ATTR1 float4 reproj0;
	DRIFT_ATTR2 float4 reproj1;
};

struct FragInput {
	float4 position : SV_POSITION;
	float2 uv;
};

void VShader(in VertInput IN, out FragInput FRAG){
	float2x3 reproj = {
		IN.reproj0.x, IN.reproj0.y, IN.reproj0.z,
		IN.reproj1.x, IN.reproj1.y, IN.reproj1.z,
	};
	
	FRAG.position = float4(mul(reproj, float3(2*IN.uv - 1, 1)), 0, 1);
	FRAG.uv = IN.uv;
}

Texture2D _texture : DRIFT_TEX1;

float4 FShader(in FragInput FRAG) : SV_TARGET0 {
	return float4(_texture.Sample(DriftLinear, FRAG.uv).rgb, 1);
}
