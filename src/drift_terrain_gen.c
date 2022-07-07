#include <stdio.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

#include "drift_game.h"

float perlin_grad(int ix, int iy, float x, float y) {
	float random = 2920.f * sinf(ix * 21942.f + iy * 171324.f + 8912.f) * cosf(ix * 23157.f * iy * 217832.f + 9758.f);
	return (x - ix)*cosf(random) + (y - iy)*sinf(random);
}

float perlin(float x, float y) {
	int ix = (int)x;
	int iy = (int)y;
	float fx = x - ix;
	float fy = y - iy;

	return DriftLerp(
		DriftLerp(perlin_grad(ix + 0, iy + 0, x, y), perlin_grad(ix + 1, iy + 0, x, y), DriftHermite5(fx)),
		DriftLerp(perlin_grad(ix + 0, iy + 1, x, y), perlin_grad(ix + 1, iy + 1, x, y), DriftHermite5(fx)),
		DriftHermite5(fy)
	);
}

static uint MAP_SIZE;
static float* VALUES;
static DriftVec3* CELLS0;
static DriftVec3* CELLS1;

static void generate_row(tina_job* job){
	uint y = tina_job_get_description(job)->user_idx;
	if(y % 256 == 0){printf("\r  map: %d%%", 100*y/MAP_SIZE); fflush(stdout);}
	
	for(uint x = 0; x < MAP_SIZE; x++){
		DriftVec2 pos = {x, y};
		float value = 0;
		value += (perlin(pos.x/32, pos.y/32))/1;
		value += (perlin(pos.x/16, pos.y/16))/2;
		// value += (perlin(pos.x/8, pos.y/8))/4;
		float dx = (int)x - (int)MAP_SIZE/2, dy = (int)y - (int)MAP_SIZE/2;
		float dsq = (dx*dx + dy*dy)*128/MAP_SIZE/MAP_SIZE;
		VALUES[x + y*MAP_SIZE] = 0.2f + dsq*dsq - fabsf(value);
	}
}

#define ROWS_PER_JOB 64

static inline DriftVec3 dist_est(DriftVec3 cmin, float x0, float x1, float dx, float dy){
	float w = x0*x1 < 0 ? fabsf(x0 - x1)/(fabsf(x0) + FLT_MIN) : 0;
	return cmin.z > w ? cmin : (DriftVec3){{dx, dy, w}};
}

static void init_job(tina_job* job){
	uint j = tina_job_get_description(job)->user_idx;
	if(j % 256 == 0){printf("\r  init: %d%%", 100*j/MAP_SIZE); fflush(stdout);}
	
	float* x = VALUES + j*MAP_SIZE;
	DriftVec3* c = CELLS0 + j*MAP_SIZE;
	for(int i = 0; i < (int)MAP_SIZE; i++){
		float x0, x1 = x[i], x2;
		// TODO this is a disaster... I'm 100% sure I'm reading outside the buffer here.
		// Kinda doesn't matter since this is just tmp code. >_>
		DriftVec3 cmin = {};
		cmin = dist_est(cmin, x1, x[(i - 1)&(MAP_SIZE - 1)],  1, 0);
		cmin = dist_est(cmin, x1, x[(i + 1)&(MAP_SIZE - 1)], -1, 0);
		cmin = dist_est(cmin, x1, x[(i - (int)MAP_SIZE)], 0,  1);
		cmin = dist_est(cmin, x1, x[(i + (int)MAP_SIZE)], 0, -1);
		cmin = dist_est(cmin, x1, x[(i - 1 - (int)MAP_SIZE)],  1,  1);
		cmin = dist_est(cmin, x1, x[(i + 1 - (int)MAP_SIZE)], -1,  1);
		cmin = dist_est(cmin, x1, x[(i - 1 + (int)MAP_SIZE)],  1, -1);
		cmin = dist_est(cmin, x1, x[(i + 1 + (int)MAP_SIZE)], -1, -1);
		float w = cmin.z + FLT_MIN;
		c[i] = (DriftVec3){{cmin.x/w, cmin.y/w, 0}};
	}
}

typedef struct {
	DriftVec3* dst;
	DriftVec3* src;
	int r;
} FloodContext;

static void flood_job(tina_job* job){
	FloodContext* ctx = tina_job_get_description(job)->user_data;
	uint i0 = tina_job_get_description(job)->user_idx*ROWS_PER_JOB;
	if(i0 % 256 == 0){printf("\r  flood(r = %d): %d%%", ctx->r, 100*i0/MAP_SIZE); fflush(stdout);}
	
	for(uint i = i0; i < i0 + ROWS_PER_JOB; i += 1) DriftSDFFloodRow(ctx->dst + i, MAP_SIZE, ctx->src + i*MAP_SIZE, MAP_SIZE, ctx->r);
}

static void encode_tile(tina_job* job){
	uint idx = tina_job_get_description(job)->user_idx;
	DriftTerrainTileCoord coord = {.x = idx % DRIFT_TERRAIN_TILEMAP_SIZE, .y = idx / DRIFT_TERRAIN_TILEMAP_SIZE};
	
	DriftTerrainDensity* density = tina_job_get_description(job)->user_data;
	u8* samples = density[idx].samples;
	DriftVec2 origin = {coord.x*DRIFT_TERRAIN_TILE_SIZE, coord.y*DRIFT_TERRAIN_TILE_SIZE};
	for(uint y = 0; y < DRIFT_TERRAIN_TILE_SIZE; y++){
		for(uint x = 0; x < DRIFT_TERRAIN_TILE_SIZE; x++){
			float value = VALUES[((coord.x*DRIFT_TERRAIN_TILE_SIZE + x)&(MAP_SIZE - 1)) + ((coord.y*DRIFT_TERRAIN_TILE_SIZE + y)&(MAP_SIZE - 1))*MAP_SIZE];
			samples[x + y*DRIFT_TERRAIN_TILE_SIZE] = DriftSDFEncode(value);
		}
	}
	
	for(uint i = DRIFT_TERRAIN_TILE_SIZE_SQ - 1; i > DRIFT_TERRAIN_TILE_SIZE; i--){
		int a = samples[i - 1*DRIFT_TERRAIN_TILE_SIZE - 1];
		int b = samples[i - 1*DRIFT_TERRAIN_TILE_SIZE - 0];
		int c = samples[i - 0*DRIFT_TERRAIN_TILE_SIZE - 1];
		// samples[i] -= c + b - a;
		// samples[i] ^= 0x80;
	}
}

void DriftTerrainGen(tina_job* job){
	MAP_SIZE = DRIFT_TERRAIN_TILE_SIZE*DRIFT_TERRAIN_TILEMAP_SIZE;
	VALUES = DriftAlloc(DriftSystemMem, MAP_SIZE*MAP_SIZE*sizeof(*VALUES));
	DriftThrottledParallelFor(job, "generate_row", generate_row, NULL, MAP_SIZE);
	puts("");
	
	CELLS0 = DriftAlloc(DriftSystemMem, MAP_SIZE*MAP_SIZE*sizeof(*CELLS0));
	CELLS1 = DriftAlloc(DriftSystemMem, MAP_SIZE*MAP_SIZE*sizeof(*CELLS1));
	DriftThrottledParallelFor(job, "init_job", init_job, NULL, MAP_SIZE);
	
	DriftThrottledParallelFor(job, "flood_job", flood_job, &(FloodContext){.dst = CELLS1, .src = CELLS0, .r = 1}, MAP_SIZE/ROWS_PER_JOB);
	puts("");
	DriftThrottledParallelFor(job, "flood_job", flood_job, &(FloodContext){.dst = CELLS0, .src = CELLS1, .r = 1}, MAP_SIZE/ROWS_PER_JOB);
	puts("");
	for(uint r = SDF_MAX_DIST/2; r > 0; r >>= 1){
		DriftThrottledParallelFor(job, "flood_job", flood_job, &(FloodContext){.dst = CELLS1, .src = CELLS0, .r = r}, MAP_SIZE/ROWS_PER_JOB);
		puts("");
		DriftThrottledParallelFor(job, "flood_job", flood_job, &(FloodContext){.dst = CELLS0, .src = CELLS1, .r = r}, MAP_SIZE/ROWS_PER_JOB);
		puts("");
	}
	
	puts("  resolve");
	for(uint i = 0; i < MAP_SIZE*MAP_SIZE; i++) VALUES[i] = copysignf(DriftSDFValue(CELLS0[i]), VALUES[i]);
	
	DriftDealloc(DriftSystemMem, CELLS0, 0);
	DriftDealloc(DriftSystemMem, CELLS1, 0);
	
	puts("  encode");
	size_t density_size = DRIFT_TERRAIN_TILEMAP_SIZE_SQ*sizeof(DriftTerrainDensity);
	DriftTerrainDensity* density = DriftAlloc(DriftSystemMem, density_size);
	DriftThrottledParallelFor(job, "encode_tile", encode_tile, density, DRIFT_TERRAIN_TILEMAP_SIZE_SQ);
	DriftDealloc(DriftSystemMem, VALUES, 0);
	
	puts("  write");
	size_t len = density_size/DRIFT_TERRAIN_FILE_SPLITS;
	for(uint i = 0; i < DRIFT_TERRAIN_FILE_SPLITS; i++){
		char filename[64];
		snprintf(filename, sizeof(filename), "../bin/terrain%d.bin", i);
		
		FILE* file = fopen(filename, "wb");
		DRIFT_ASSERT_HARD(file, "Failed to open '%s' for writting.", filename);
		fwrite((u8*)density + i*len, len, 1, file);
		fclose(file);
	}
	
	size_t tile_stride = DRIFT_TERRAIN_TILE_SIZE;
	size_t map_stride = DRIFT_TERRAIN_TILE_SIZE*DRIFT_TERRAIN_TILEMAP_SIZE;
	u8* map = DriftAlloc(DriftSystemMem, map_stride*map_stride);
	
	for(uint i = 0; i < DRIFT_TERRAIN_TILEMAP_SIZE_SQ; i++){
		uint x = i % DRIFT_TERRAIN_TILEMAP_SIZE, y = i / DRIFT_TERRAIN_TILEMAP_SIZE;
		u8* origin = map + x*tile_stride + y*DRIFT_TERRAIN_TILE_SIZE_SQ*DRIFT_TERRAIN_TILEMAP_SIZE;
		u8* samples = density[i].samples;
		for(uint j = 0; j < DRIFT_TERRAIN_TILE_SIZE; j++){
			u8* dst = origin + j*map_stride;
			u8* src = samples + j*tile_stride;
			memcpy(dst, src, tile_stride);
		}
	}
	
	puts("  write png");
	stbi_flip_vertically_on_write(true);
	stbi_write_png("map.png", map_stride, map_stride, 1, map, map_stride);
	
	DriftApp* app = tina_job_get_description(job)->user_data;
	DriftAppHaltScheduler(app);
}
