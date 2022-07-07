#pragma once

#define SDF_MAX_DIST (DRIFT_TERRAIN_TILE_SIZE/4)

static inline u8 DriftSDFEncode(float x){
	const float quantization_fix = 0.5f/256.0f;
	return (u8)DriftClamp(255*(0.5f + 0.5f*x/SDF_MAX_DIST + quantization_fix), 0, 255);
}

static inline float DriftSDFDecode(u8 x){
	return (2*x/255.0f - 1)*SDF_MAX_DIST;
}

static inline float DriftSDFValue(DriftVec3 c){return hypotf(c.x, c.y) + c.z;}

void DriftSDFFloodRow(DriftVec3* dst, uint dst_stride, DriftVec3* src, uint len, int r);
