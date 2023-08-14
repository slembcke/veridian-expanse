/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include "drift_common.hlsl"

void VShader(in DriftSpriteVertInput IN, out DriftSpriteFragInput FRAG){
	DriftSpriteVShader(IN, FRAG, false);
	FRAG.color.rgb = SRGBToLinear(FRAG.color.rgb);
}

float4 FShader(in DriftSpriteFragInput FRAG) : SV_TARGET0 {
	float3 uv = FRAG.uv;
	uv.xy = clamp(uv, FRAG.uv_bounds.xy, FRAG.uv_bounds.zw);
	float4 color = FRAG.color*DriftAtlas.Sample(DriftNearest, uv).a;
	
	float terrain_density = DriftPrevFrame.Sample(DriftNearest, FRAG.ssuv_prev).a;
	float mask = step(-0.25, terrain_density);
	return color*mask;
}
