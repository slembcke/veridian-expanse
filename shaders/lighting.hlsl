struct VertInput {
	// Vertex attribs:
	DRIFT_ATTR0 float2 uv;
	// Instance attribs:
	DRIFT_ATTR1 float4 mat0;
	DRIFT_ATTR2 float2 mat1;
	DRIFT_ATTR3 float4 color;
	DRIFT_ATTR4 uint4 bounds;
	DRIFT_ATTR5 uint2 anchor;
	DRIFT_ATTR6 uint layer;
};

struct FragInput {
	float4 position : SV_POSITION;
	float3 uv;
	float4 uv_bounds;
	float2 dir;
	nointerpolation float4 color;
};

struct FragOutput0 {
	float4 a0 : SV_TARGET0;
};

struct FragOutput1 {
	float4 a1 : SV_TARGET0;
	float4 b1 : SV_TARGET1;
	float4 a2 : SV_TARGET2;
	float4 b2 : SV_TARGET3;
};

void VShaderLight(in VertInput IN, out FragInput FRAG){
	// Transform:
	float2x3 transform = float2x3(IN.mat0.xz, IN.mat1.x, IN.mat0.yw, IN.mat1.y);
	float2 size = IN.bounds.zw - IN.bounds.xy + 1;
	float2 local_pos = 2*(IN.uv - U8ToSigned(IN.anchor)/size);
	float2 world_pos = mul(transform, float3(local_pos, 1));
	float2 clip_pos = mul(float2x3(DRIFT_MATRIX_VP), float3(world_pos, 1));
	
	// Output:
	FRAG.position = float4(clip_pos, 0, 1);
	FRAG.uv = float3(lerp(IN.bounds.xy, IN.bounds.zw + 1, IN.uv)/DRIFT_ATLAS_SIZE, IN.layer);
	FRAG.uv_bounds = IN.bounds/DRIFT_ATLAS_SIZE;
	FRAG.dir = -mul(float2x2(DRIFT_MATRIX_V), mul(float2x2(transform), local_pos));
	FRAG.color = IN.color;
}

float2x3 inverse2x3(float2x3 m){
	return float2x3(
		 m._m11, -m._m01, m._m01*m._m12 - m._m11*m._m02,
		-m._m10,  m._m00, m._m10*m._m02 - m._m00*m._m12
	)/(m._m00*m._m11 - m._m01*m._m10);
}

void VShaderShadow(in VertInput IN, out FragInput FRAG){
	// Transform:
	float2x3 transform = float2x3(IN.mat0.xz, IN.mat1.x, IN.mat0.yw, IN.mat1.y);
	float2 size = IN.bounds.zw - IN.bounds.xy + 1;
	float2 clip_pos = 2*IN.uv - 1;
	float2 world_pos = mul(float2x3(DRIFT_MATRIX_VP_INV), float3(clip_pos, 1));
	float2 local_pos = mul(inverse2x3(transform), float3(world_pos, 1));
	float2 texel_pos = lerp(IN.bounds.xy, IN.bounds.zw + 1, 0.5*local_pos) + U8ToSigned(IN.anchor);
	
	// Output:
	FRAG.position = float4(clip_pos, 0, 1);
	FRAG.uv = float3(texel_pos/DRIFT_ATLAS_SIZE, IN.layer);
	FRAG.uv_bounds = IN.bounds/DRIFT_ATLAS_SIZE;
	FRAG.dir = -mul(float2x2(DRIFT_MATRIX_V), mul(float2x2(transform), local_pos));
	FRAG.color = IN.color;
}

struct LightFieldSample {
	float a0, a1, b1, a2, b2;
};

LightFieldSample GenerateSample(float2 dir, float faked_z){
	const float C0 = 0.318309886184; // 1/pi
	const float C1 = 0.5;
	const float C2 = 0.212206590789; // 2/(3*pi)
	
	// TODO What's up with the div-by-0 here?
	float3 direction = normalize(float3(dir, faked_z + 1e-5));
	float2 g1 = C1*direction.xy;
	float2 g2 = C2*DoubleAngle(direction.xy);
	
	// Kinda mostly workable hack to approximate a light not on the xy plane.
	// Should figure out a more accurate curve for this.
	float a0 = lerp(C0, 1.0, direction.z);
	return LightFieldSample(a0, g1.x, g1.y, g2.x, g2.y);
}

struct MaskVertexInput {
	// Vertex attribs:
	DRIFT_ATTR0 float2 occluder_coord;
	// Instance attribs:
	DRIFT_ATTR1 float4 endpoints;
	// DRIFT_ATTR1 float3 properties;
};

struct MaskFragmentInput {
	float4 position : SV_POSITION;
	float opacity;
	float4 penumbras;
	float3 edges;
	float4 light_position;
	float4 endpoints;
};

cbuffer Locals : DRIFT_UBO1 {
	row_major float2x4 LightMatrix;
	float LightRadius;
}

float2x2 adjugate(float2x2 m){return float2x2(m[1][1], -m[1][0], -m[0][1], m[0][0]);}

void VShaderMask(MaskVertexInput IN, out MaskFragmentInput FRAG){
	// Unpack input.
	float2 endpoint_a = IN.endpoints.zw;
	float2 endpoint_b = IN.endpoints.xy;
	float2 light_pos = mul(float2x3(LightMatrix), float3(0, 0, 1));
	FRAG.opacity = 1;
	
	// Determinant of the light matrix to check if it's flipped.
	float flip = 1;//sign(determinant(float2x2(LightMatrix)));
	
	// Deltas from the segment to the light.
	float2 endpoint = lerp(endpoint_a, endpoint_b, IN.occluder_coord.x);
	float2 delta_a = endpoint_a - light_pos;
	float2 delta_b = endpoint_b - light_pos;
	float2 delta = endpoint - light_pos;
	
	// Offsets from the light to the edge of the light volume.
	float2 offset_a = flip*float2(-LightRadius,  LightRadius)*normalize(delta_a).yx;
	float2 offset_b = flip*float2( LightRadius, -LightRadius)*normalize(delta_b).yx;
	float2 offset = lerp(offset_a, offset_b, IN.occluder_coord.x);
	
	// Vertex projection.
	float w = IN.occluder_coord.y;
	float2 swept_edge = delta - offset;
	float3 proj_pos = float3(lerp(swept_edge, endpoint, w), w);
	FRAG.position = float4(mul(float2x3(DRIFT_MATRIX_VP), proj_pos), 0, w);

	// Penumbras.
	float2 penumbra_a = mul(adjugate(float2x2( offset_a, -delta_a)), delta - lerp(offset, delta_a, w));
	float2 penumbra_b = mul(adjugate(float2x2(-offset_b,  delta_b)), delta - lerp(offset, delta_b, w));
	FRAG.penumbras = (LightRadius > 0.0 ? float4(penumbra_a, penumbra_b) : float4(0, 0, 1, 1));

	// Clipping values.
	float2 seg_delta = endpoint_b - endpoint_a;
	float2 seg_normal = seg_delta.yx*float2(-1.0, 1.0);
	FRAG.edges.xy = -mul(adjugate(float2x2(seg_delta, delta_a + delta_b)), delta - offset*(1 - w));
	FRAG.edges.y *= 2.0;
	FRAG.edges.z = flip*dot(seg_normal, swept_edge)*(1 - w);

	// deltas for light penetration.
	float light_penetration = 16;
	FRAG.light_position = float4(proj_pos.xy, 0.0, w*light_penetration);
	FRAG.endpoints = float4(endpoint_a, endpoint_b)/light_penetration;
}

float FShaderMask(in MaskFragmentInput FRAG){
	// Light penetration.
	float closest_t = clamp(FRAG.edges.x/abs(FRAG.edges.y), -0.5, 0.5);
	float2 closest_p = (0.5 - closest_t)*FRAG.endpoints.xy + (0.5 + closest_t)*FRAG.endpoints.zw;
	float2 penetration = closest_p - FRAG.light_position.xy/FRAG.light_position.w;
	float attenuation = min(dot(penetration, penetration), 1);

	// Penumbra mixing.
	float2 penumbras = smoothstep(-1, 1, FRAG.penumbras.xz/FRAG.penumbras.yw);
	float occlusion = 1 - dot(penumbras, step(FRAG.penumbras.yw, 0));
	
	return FRAG.opacity*attenuation*occlusion*step(FRAG.edges.z, 0);
}
