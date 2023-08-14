/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include "drift_common.hlsl"
#include "lighting.hlsl"

struct foo {
	DRIFT_ATTR0 float2 uv;
};

struct bar {
	float4 position : SV_POSITION;
	float2 uv;
};

void VShader(in foo IN, out bar FRAG){
	FRAG.position = float4(2*IN.uv - 1, 0, 1);
	FRAG.uv = IN.uv;
}

void FShader(in bar FRAG, out FragOutput1 OUT){
	OUT.a1 = float4(DriftLightfield.Sample(DriftLinear, float3(FRAG.uv, 1)).rgb, 1);
	OUT.b1 = float4(DriftLightfield.Sample(DriftLinear, float3(FRAG.uv, 2)).rgb, 1);
	OUT.a2 = float4(DriftLightfield.Sample(DriftLinear, float3(FRAG.uv, 3)).rgb, 1);
	OUT.b2 = float4(DriftLightfield.Sample(DriftLinear, float3(FRAG.uv, 4)).rgb, 1);
}
