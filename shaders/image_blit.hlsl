/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include "drift_common.hlsl"

struct VertInput {
	DRIFT_ATTR0 float2 uv;
	DRIFT_ATTR1 float4 tint;
};

struct FragInput {
	float4 position : SV_POSITION;
	float2 uv;
	float4 tint;
};

void VShader(in VertInput IN, out FragInput FRAG){
	float2 pos = (2*IN.uv - 1)*float2(320, 240);
	float2 clip_pos = mul(float2x3(DRIFT_MATRIX_VP), float3(pos, 1));
	FRAG.position = float4(clip_pos, 0, 1);
	FRAG.uv = IN.uv;
	FRAG.tint = IN.tint;
}

Texture2D _texture : DRIFT_TEX1;
SamplerState _repeat : DRIFT_SAMP2;

float4 FShader(in FragInput FRAG) : SV_TARGET0 {
	float3 color = FRAG.tint*_texture.Sample(DriftNearest, FRAG.uv);
	return float4(color, 1);
}
