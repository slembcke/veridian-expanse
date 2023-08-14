/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include "drift_common.hlsl"
#include "lighting.hlsl"

void VShader(in VertInput IN, out FragInput FRAG){
	VShaderLight(IN, FRAG);
}

void FShader(in FragInput FRAG, out FragOutput0 OUT){
	LightFieldSample field = GenerateSample(FRAG.dir, FRAG.color.a);
	
	float3 intensity = FRAG.color*DriftAtlas.Sample(DriftLinear, FRAG.uv);
	OUT.a0 = float4(intensity*field.a0, 1);
}
