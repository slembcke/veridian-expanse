/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include "drift_common.hlsl"

struct VertInput {
	DRIFT_ATTR0 float2 uv;
};

struct FragInput {
	float4 position : SV_POSITION;
	float2 uv;
};

void VShader(in VertInput IN, out FragInput FRAG){
	float2 clip_coord = 2*IN.uv - 1;
	FRAG.position = float4(clip_coord, 0, 1);
	FRAG.uv = (IN.uv*DRIFT_VIRTUAL_EXTENTS + round(0.5*(DRIFT_INTERNAL_EXTENTS - DRIFT_VIRTUAL_EXTENTS)))/DRIFT_INTERNAL_EXTENTS;
}

Texture2D _texture : DRIFT_TEX1;

float4 FShader(in FragInput FRAG) : SV_TARGET0 {
	// TODO minor... present size doesn't seem to match raw size during resizing?
	// Visualise pixel offset error.
	// return float4(2*abs(0.5 - frac(FRAG.uv.xy*DRIFT_INTERNAL_EXTENTS)), 0, 1);
	
	float3 color = _texture.Sample(DriftNearest, FRAG.uv);
	float3 bl = saturate(_texture.Sample(DriftLinear, FRAG.uv));
	// Apply sharpening.
	color = lerp(bl, color, DRIFT_SHARPENING);
	
	return float4(color.rgb, 1);
}
