/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include "drift_common.hlsl"
#include "lighting.hlsl"

void VShader(MaskVertexInput IN, out MaskFragmentInput FRAG){
	VShaderMask(IN, FRAG);
}

void FShader(in MaskFragmentInput FRAG, out FragOutput0 OUT){
	float mask = FShaderMask(FRAG);
	OUT.a0 = float4(0, 0, 0, -mask);
}
