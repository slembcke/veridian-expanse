/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include "drift_common.hlsl"

struct VertInput {
	DRIFT_ATTR0 float2 position;
	DRIFT_ATTR1 float2 uv;
	DRIFT_ATTR2 float4 color;
};

struct FragInput {
	float4 position : SV_POSITION;
	float3 uv;
	float4 color;
};

void VShader(in VertInput IN, out FragInput FRAG){
	FRAG.position = float4(2*IN.position/DRIFT_RAW_EXTENTS - 1, 0, 1);
	FRAG.position.y *= -1;
	
	FRAG.uv = float3(IN.uv, 0);
	FRAG.color = IN.color;
}

Texture2DArray Texture : DRIFT_TEX1;

float4 FShader(in FragInput FRAG) : SV_TARGET0 {
	return FRAG.color*Texture.Sample(DriftNearest, FRAG.uv);
}
