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
	float3 uv_jitter;
};

void VShader(in VertInput IN, out FragInput FRAG){
	float2 clip_coord = 2*IN.uv - 1;
	FRAG.position = float4(clip_coord, 0, 1);
	FRAG.uv = IN.uv;
	
	float2 pixel = (IN.uv - 0.5)*(DRIFT_INTERNAL_EXTENTS/DRIFT_ATLAS_SIZE);
	FRAG.uv_jitter = float3(pixel + DRIFT_JITTER, 15);
}

Texture2D _texture : DRIFT_TEX1;
SamplerState _repeat : DRIFT_SAMP2;

float4 FShader(in FragInput FRAG) : SV_TARGET0 {
	float4 blue = 4*DriftAtlas.Sample(_repeat, FRAG.uv_jitter);
	float2 jitter = (blue.xy - blue.zw)/DRIFT_INTERNAL_EXTENTS;
	float3 color = _texture.Sample(DriftNearest, FRAG.uv + jitter);
	return float4(LinearToSRGB(color), 1);
}
